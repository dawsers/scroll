#include <limits.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/edges.h>
#include "sway/desktop/transaction.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/node.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/desktop/animation.h"

// Thickness of the dropzone when dragging to the edge of a layout container
#define DROP_LAYOUT_BORDER 30

struct seatop_move_tiling_event {
	struct sway_container *con;
	struct sway_node *target_node;
	enum wlr_edges target_edge;
	double ref_lx, ref_ly; // cursor's x/y at start of op
	bool threshold_reached;
	bool insert_after_target;
	struct sway_scene_rect *indicator_rect;
};

static void handle_end(struct sway_seat *seat) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	sway_scene_node_destroy(&e->indicator_rect->node);
	e->indicator_rect = NULL;
}

static void handle_motion_prethreshold(struct sway_seat *seat) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	double cx = seat->cursor->cursor->x;
	double cy = seat->cursor->cursor->y;
	double sx = e->ref_lx;
	double sy = e->ref_ly;

	// Get the scaled threshold for the output. Even if the operation goes
	// across multiple outputs of varying scales, just use the scale for the
	// output that the cursor is currently on for simplicity.
	struct wlr_output *wlr_output = wlr_output_layout_output_at(
			root->output_layout, cx, cy);
	double output_scale = wlr_output ? wlr_output->scale : 1;
	double threshold = config->tiling_drag_threshold * output_scale;
	threshold *= threshold;

	// If the threshold has been exceeded, start the actual drag
	if ((cx - sx) * (cx - sx) + (cy - sy) * (cy - sy) > threshold) {
		sway_scene_node_set_enabled(&e->indicator_rect->node, true);
		e->threshold_reached = true;
		cursor_set_image(seat->cursor, "grab", NULL);
	}
}

static void update_indicator(struct seatop_move_tiling_event *e, struct wlr_box *box) {
	sway_scene_node_set_position(&e->indicator_rect->node, box->x, box->y);
	sway_scene_rect_set_size(e->indicator_rect, box->width, box->height);
}

static void handle_motion_postthreshold(struct sway_seat *seat) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	struct wlr_surface *surface = NULL;
	double sx, sy;
	struct sway_cursor *cursor = seat->cursor;
	struct sway_node *node = node_at_coords(seat,
			cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

	if (!node) {
		// Eg. hovered over a layer surface such as swaybar
		e->target_node = NULL;
		e->target_edge = WLR_EDGE_NONE;
		return;
	}
	if (node->type == N_WORKSPACE) {
		// Empty workspace
		e->target_node = node;
		e->target_edge = WLR_EDGE_NONE;

		struct wlr_box drop_box;
		workspace_get_box(node->sway_workspace, &drop_box);
		update_indicator(e, &drop_box);
		return;
	}
	struct sway_container *con = node->sway_container;
	// If tiling container, exit
	if (container_is_floating(con)) {
		e->target_node = NULL;
		e->target_edge = WLR_EDGE_NONE;
		return;
	}
	// Deny moving within own workspace if this is the only child
	if (workspace_num_tiling_views(e->con->pending.workspace) == 1 &&
			con->pending.workspace == e->con->pending.workspace) {
		e->target_node = NULL;
		e->target_edge = WLR_EDGE_NONE;
		return;
	}
	struct sway_workspace *old_workspace = e->con->pending.workspace;
	bool move_parent = layout_get_type(old_workspace) == layout_modifiers_get_mode(old_workspace);
	struct sway_workspace *workspace = con->pending.workspace;
	enum sway_container_layout layout = layout_get_type(workspace);
	struct wlr_box node_box;
	node_get_box(&con->node, &node_box);
	node_box.x -= workspace->x;
	node_box.y -= workspace->y;
	int drop_layout_border = DROP_LAYOUT_BORDER;
	if (layout_scale_enabled(workspace)) {
		float scale = layout_scale_get(workspace);
		drop_layout_border *= scale;
		node_box.width *= scale;
		node_box.height *= scale;
	}
	struct wlr_box box = node_box;
	int gap_x = 0, gap_y = 0;
	if (move_parent) {
		// Need a top level container
		if (con->view) {
			con = con->pending.parent;
			if (layout == L_HORIZ) {
				gap_y = workspace->gaps_inner;
				gap_x = workspace->x;
				box.y = workspace->y;
				node_box.y += workspace->y;
				box.height = workspace->height - 2 * workspace->gaps_inner;
			} else {
				gap_x = workspace->gaps_inner;
				gap_y = workspace->y;
				box.x = workspace->x;
				node_box.x += workspace->x;
				box.width = workspace->width - 2 * workspace->gaps_inner;
			}
		}
	} else {
		gap_x = workspace->x;
		gap_y = workspace->y;
	}
	node_box.x += gap_x;
	node_box.y += gap_y;
	box.x += gap_x;
	box.y += gap_y;
	enum wlr_edges edge = WLR_EDGE_NONE;
	int thresh_top = node_box.y + drop_layout_border;
	int thresh_bottom = node_box.y + node_box.height - drop_layout_border;
	int thresh_left = node_box.x + drop_layout_border;
	int thresh_right = node_box.x + node_box.width - drop_layout_border;
	if (cursor->cursor->y < thresh_top) {
		edge = WLR_EDGE_TOP;
		box.height = drop_layout_border;
	} else if (cursor->cursor->y > thresh_bottom) {
		edge = WLR_EDGE_BOTTOM;
		box.y = box.y + box.height - drop_layout_border;
		box.height = drop_layout_border;
	} else if (cursor->cursor->x < thresh_left) {
		edge = WLR_EDGE_LEFT;
		box.width = drop_layout_border;
	} else if (cursor->cursor->x > thresh_right) {
		edge = WLR_EDGE_RIGHT;
		box.x = box.x + box.width - drop_layout_border;
		box.width = drop_layout_border;
	} else {
		// Swap
		edge = WLR_EDGE_NONE;
		box.width = box.width - 2 * drop_layout_border;
		box.height = box.height - 2 * drop_layout_border;
		box.x += drop_layout_border;
		box.y += drop_layout_border;
	}
	e->target_node = &con->node;
	e->target_edge = edge;
	update_indicator(e, &box);
	// Set focus on container so we can scroll the view
	seat_set_focus(seat, node);
	arrange_workspace(workspace);
	return;
}

static void handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	if (e->threshold_reached) {
		handle_motion_postthreshold(seat);
	} else {
		handle_motion_prethreshold(seat);
	}
	transaction_commit_dirty();
}

static void finalize_move(struct sway_seat *seat) {
	struct seatop_move_tiling_event *e = seat->seatop_data;

	if (!e->target_node) {
		seatop_begin_default(seat);
		return;
	}

	struct sway_container *con = e->con;
	struct sway_node *target_node = e->target_node;
	struct sway_workspace *workspace = target_node->type == N_WORKSPACE ?
		target_node->sway_workspace : target_node->sway_container->pending.workspace;
	enum wlr_edges edge = e->target_edge;

	if (target_node->type == N_WORKSPACE) {
		// Moving container into workspace
		layout_drag_container_to_workspace(con, workspace);
	} else {
		layout_drag_container_to_container(con, target_node->sway_container, workspace, edge);
	}
	ipc_event_window(con, "move");
	seat_set_focus(seat, &con->node);
	arrange_workspace(workspace);
	animation_set_type(ANIMATION_WINDOW_MOVE);
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

static void handle_unref(struct sway_seat *seat, struct sway_container *con) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	if (e->target_node == &con->node) { // Drop target
		e->target_node = NULL;
	}
	if (e->con == con) { // The container being moved
		seatop_begin_default(seat);
	}
}

static const struct sway_seatop_impl seatop_impl = {
	.button = handle_button,
	.pointer_motion = handle_pointer_motion,
	.tablet_tool_tip = handle_tablet_tool_tip,
	.unref = handle_unref,
	.end = handle_end,
};

void seatop_begin_move_tiling_threshold(struct sway_seat *seat,
		struct sway_container *con) {
	seatop_end(seat);

	struct seatop_move_tiling_event *e =
		calloc(1, sizeof(struct seatop_move_tiling_event));
	if (!e) {
		return;
	}

	const float *indicator = config->border_colors.focused.indicator;
	float color[4] = {
		indicator[0] * .5,
		indicator[1] * .5,
		indicator[2] * .5,
		indicator[3] * .5,
	};
	e->indicator_rect = sway_scene_rect_create(seat->scene_tree, 0, 0, color);
	if (!e->indicator_rect) {
		free(e);
		return;
	}

	e->con = con;
	e->ref_lx = seat->cursor->cursor->x;
	e->ref_ly = seat->cursor->cursor->y;

	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	container_raise_floating(con);
	transaction_commit_dirty();
	wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}

void seatop_begin_move_tiling(struct sway_seat *seat,
		struct sway_container *con) {
	seatop_begin_move_tiling_threshold(seat, con);
	struct seatop_move_tiling_event *e = seat->seatop_data;
	if (e) {
		e->threshold_reached = true;
		cursor_set_image(seat->cursor, "grab", NULL);
	}
}
