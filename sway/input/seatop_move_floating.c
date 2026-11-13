#include <wlr/types/wlr_cursor.h>
#include "sway/desktop/transaction.h"
#include "sway/desktop/animation.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/tree/layout.h"

struct seatop_move_floating_event {
	struct sway_container *con;
	// Container position relative to the cursor
	double dx, dy;
};

// The position of the cursor in the layout coordinates of con
static void cursor_layout_coords(struct sway_seat *seat, struct sway_container *con,
		double *x, double *y) {
	*x = seat->cursor->cursor->x;
	*y = seat->cursor->cursor->y;
	struct sway_workspace *ws = con->pending.workspace;
	if (!ws) {
		return;
	}
	if (layout_overview_workspaces_enabled()) {
		layout_overview_workspaces_local_to_global(ws, x, y);
	}
	struct sway_layout_transform transform;
	layout_transform_container_get(con, true, &transform);
	layout_transform_point_inv(&transform, x, y);
}

static void finalize_move(struct sway_seat *seat) {
	struct seatop_move_floating_event *e = seat->seatop_data;

	// We "move" the container to its own location
	// so it discovers its output again.
	container_floating_move_to(e->con, e->con->pending.x, e->con->pending.y, true);
	animation_set_type(ANIMATION_WINDOW_MOVE_FLOAT);
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
	double cx, cy;
	cursor_layout_coords(seat, e->con, &cx, &cy);
	container_floating_move_to(e->con, cx + e->dx, cy + e->dy, true);
	animation_set_type(ANIMATION_DISABLED);
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
	e->con = con;
	double cx, cy;
	cursor_layout_coords(seat, con, &cx, &cy);
	e->dx = con->pending.x - cx;
	e->dy = con->pending.y - cy;

	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	container_raise_floating(con);
	animation_set_type(ANIMATION_DISABLED);
	transaction_commit_dirty();

	cursor_set_image(cursor, "grab", NULL);
	wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
