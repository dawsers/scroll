#include <wlr/types/wlr_cursor.h>
#include "sway/desktop/transaction.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/tree/layout.h"

struct seatop_move_floating_event {
	struct sway_container *con;
	double x, y;	// original container position
	double cx, cy;	// original cursor position
};

static void finalize_move(struct sway_seat *seat) {
	struct seatop_move_floating_event *e = seat->seatop_data;

	// We "move" the container to its own location
	// so it discovers its output again.
	container_floating_move_to(e->con, e->con->pending.x, e->con->pending.y);
	transaction_commit_dirty();

	seatop_begin_default(seat);
}

static void handle_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wl_pointer_button_state state) {
	if (seat->cursor->pressed_button_count == 0) {
		finalize_move(seat);
	}
}

static void handle_tablet_tool_tip(struct sway_seat *seat,
		struct sway_tablet_tool *tool, uint32_t time_msec,
		enum wlr_tablet_tool_tip_state state) {
	if (state == WLR_TABLET_TOOL_TIP_UP) {
		finalize_move(seat);
	}
}
static void handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
	struct seatop_move_floating_event *e = seat->seatop_data;
	struct wlr_cursor *cursor = seat->cursor->cursor;
	double cx = cursor->x;
	double cy = cursor->y;
	double scale = 1.0;
	struct sway_workspace *workspace = e->con->pending.workspace;
	if (workspace) {
		if (layout_overview_workspaces_enabled()) {
			layout_overview_workspaces_local_to_global(workspace, &cx, &cy);
		}
		if (layout_scale_enabled(workspace)) {
			scale = layout_scale_get(workspace);
		}
	}
	container_floating_move_to(e->con, e->x + (cx - e->cx) / scale, e->y + (cy - e->cy) / scale);
	transaction_commit_dirty();
}

static void handle_unref(struct sway_seat *seat, struct sway_container *con) {
	struct seatop_move_floating_event *e = seat->seatop_data;
	if (e->con == con) {
		seatop_begin_default(seat);
	}
}

static const struct sway_seatop_impl seatop_impl = {
	.button = handle_button,
	.pointer_motion = handle_pointer_motion,
	.tablet_tool_tip = handle_tablet_tool_tip,
	.unref = handle_unref,
};

void seatop_begin_move_floating(struct sway_seat *seat,
		struct sway_container *con) {
	seatop_end(seat);

	struct sway_cursor *cursor = seat->cursor;
	struct seatop_move_floating_event *e =
		calloc(1, sizeof(struct seatop_move_floating_event));
	if (!e) {
		return;
	}
	double cx = cursor->cursor->x;
	double cy = cursor->cursor->y;
	if (layout_overview_workspaces_enabled() && con->pending.workspace) {
		layout_overview_workspaces_local_to_global(con->pending.workspace, &cx, &cy);
	}
	e->con = con;
	e->x = con->pending.x;
	e->y = con->pending.y;
	e->cx = cx;
	e->cy = cy;

	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	container_raise_floating(con);
	transaction_commit_dirty();

	cursor_set_image(cursor, "grab", NULL);
	wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
