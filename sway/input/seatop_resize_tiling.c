#include <wlr/types/wlr_cursor.h>
#include <wlr/util/edges.h>
#include "sway/commands.h"
#include "sway/desktop/transaction.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"

struct seatop_resize_tiling_event {
	struct sway_container *con;    // container to resize

	enum wlr_edges edge;
	enum wlr_edges edge_x, edge_y;
	double ref_lx, ref_ly;         // cursor's x/y at start of op
	double con_orig_width;       // width of the container at start
	double con_orig_height;      // height of the container at start
	double min_width, max_width;
	double min_height, max_height;
};

static void arrange_resized_container(struct sway_container *con) {
	enum sway_container_layout layout = layout_get_type(con->pending.workspace);
	struct sway_container *parent = con->pending.parent;
	struct sway_workspace *workspace = parent->pending.workspace;
	if (layout == L_HORIZ) {
		parent->pending.width = con->pending.width;
		parent->width_fraction = (parent->pending.width + 2.0 * workspace->gaps_inner) / workspace->width;
		for (int i = 0; i < parent->pending.children->length; ++i) {
			struct sway_container *child = parent->pending.children->items[i];
			child->pending.width = parent->pending.width;
			child->width_fraction = parent->width_fraction;
		}
		con->height_fraction = (con->pending.height + 2.0 * workspace->gaps_inner) / workspace->height;
	} else {
		parent->pending.height = con->pending.height;
		parent->height_fraction = (parent->pending.height + 2.0 * workspace->gaps_inner) / workspace->height;
		for (int i = 0; i < parent->pending.children->length; ++i) {
			struct sway_container *child = parent->pending.children->items[i];
			child->pending.height = parent->pending.height;
			child->height_fraction = parent->height_fraction;
		}
		con->width_fraction = (con->pending.width + 2.0 * workspace->gaps_inner) / workspace->width;
	}
	arrange_container(parent);
}

static void handle_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wl_pointer_button_state state) {
	struct seatop_resize_tiling_event *e = seat->seatop_data;

	if (seat->cursor->pressed_button_count == 0) {
		if (e->con) {
			arrange_resized_container(e->con);
			container_set_resizing(e->con, false);
			arrange_workspace(e->con->pending.workspace);
			layout_tiling_resize_callback(e->con);
		}
		transaction_commit_dirty();
		seatop_begin_default(seat);
	}
}

static void handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
	struct seatop_resize_tiling_event *e = seat->seatop_data;
	int amount_x = 0;
	int amount_y = 0;
	int moved_x = seat->cursor->cursor->x - e->ref_lx;
	int moved_y = seat->cursor->cursor->y - e->ref_ly;

	if (e->edge_x) {
		if (e->edge_x & WLR_EDGE_LEFT) {
			amount_x = e->con_orig_width - moved_x;
		} else if (e->edge_x & WLR_EDGE_RIGHT) {
			amount_x = e->con_orig_width + moved_x;
		}
		e->con->pending.width = fmax(fmin(amount_x, e->max_width), e->min_width);
	}
	if (e->edge_y) {
		if (e->edge_y & WLR_EDGE_TOP) {
			amount_y = e->con_orig_height - moved_y;
		} else if (e->edge_y & WLR_EDGE_BOTTOM) {
			amount_y = e->con_orig_height + moved_y;
		}
		e->con->pending.height = fmax(fmin(amount_y, e->max_height), e->min_height);
	}
	arrange_resized_container(e->con);
	transaction_commit_dirty();
}

static void handle_unref(struct sway_seat *seat, struct sway_container *con) {
	struct seatop_resize_tiling_event *e = seat->seatop_data;
	if (e->con == con) {
		seatop_begin_default(seat);
	}
}

static const struct sway_seatop_impl seatop_impl = {
	.button = handle_button,
	.pointer_motion = handle_pointer_motion,
	.unref = handle_unref,
};

void seatop_begin_resize_tiling(struct sway_seat *seat,
		struct sway_container *con, enum wlr_edges edge) {
	seatop_end(seat);

	struct seatop_resize_tiling_event *e =
		calloc(1, sizeof(struct seatop_resize_tiling_event));
	if (!e) {
		return;
	}
	e->con = con;
	e->edge = edge;

	e->ref_lx = seat->cursor->cursor->x;
	e->ref_ly = seat->cursor->cursor->y;

	container_set_resizing(e->con, true);

	if (edge & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)) {
		e->edge_x = edge & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
		e->con_orig_width = e->con->pending.width;
	}
	if (edge & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)) {
		e->edge_y = edge & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM);
		e->con_orig_height = e->con->pending.height;
	}

	view_get_constraints(e->con->view, &e->min_width, &e->max_width, &e->min_height, &e->max_height);
	e->min_width = fmax(e->min_height, 1.0) + 2.0 * e->con->pending.border_thickness;
	e->max_width += 2.0 * e->con->pending.border_thickness;
	e->min_height = fmax(e->min_height, 1.0) + 2.0 * e->con->pending.border_thickness;
	e->max_height += 2.0 * e->con->pending.border_thickness;

	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	transaction_commit_dirty();
	wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
