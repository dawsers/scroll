#include "sway/tree/layout.h"
#include "sway/output.h"
#include "sway/tree/workspace.h"
#include "sway/tree/arrange.h"
#include "sway/tree/space.h"
#include "sway/sway_text_node.h"
#include "sway/input/seat.h"
#include "util.h"
#include <wayland-util.h>
#include "sway/input/keyboard.h"
#include "sway/desktop/transaction.h"
#include "sway/input/cursor.h"
#include "wlr/types/wlr_cursor.h"
#include "sway/ipc-server.h"
#include <libevdev/libevdev.h>
#include "sway/desktop/animation.h"

struct sway_trails {
	list_t *trails;
	int active;
};

struct sway_trail {
	list_t *marks;
	int active;
};

static struct sway_trails *trails = NULL;

enum wlr_direction layout_to_wlr_direction(enum sway_layout_direction dir) {
	switch (dir) {
	case DIR_LEFT:
		return WLR_DIRECTION_LEFT;
	case DIR_RIGHT:
		return WLR_DIRECTION_RIGHT;
	case DIR_UP:
		return WLR_DIRECTION_UP;
	case DIR_DOWN:
		return WLR_DIRECTION_DOWN;
	case DIR_BEGIN:
		return WLR_DIRECTION_LEFT;
	case DIR_END:
		return WLR_DIRECTION_RIGHT;
	default:
		// In case of a user mistake
		return WLR_DIRECTION_RIGHT;
	}
}

double layout_get_default_width(struct sway_workspace *workspace) {
	struct sway_output *output = workspace->output;
	if (output->scroller_options.default_width > 0) {
		return output->scroller_options.default_width;
	}
	return config->layout_default_width;
}

double layout_get_default_height(struct sway_workspace *workspace) {
	struct sway_output *output = workspace->output;
	if (output->scroller_options.default_height > 0) {
		return output->scroller_options.default_height;
	}
	return config->layout_default_height;
}

list_t *layout_get_widths(struct sway_output *output) {
	if (output->scroller_options.widths) {
		return output->scroller_options.widths;
	}
	return config->layout_widths;
}

list_t *layout_get_heights(struct sway_output *output) {
	if (output->scroller_options.heights) {
		return output->scroller_options.heights;
	}
	return config->layout_heights;
}

static void layout_toggle_size_init(struct sway_workspace *workspace);

void layout_init(struct sway_workspace *workspace) {
	layout_modifiers_init(workspace);
	workspace->layout.overview = OVERVIEW_DISABLED;
	workspace->layout.mem_scale = -1.0f;  // disabled
	layout_toggle_size_init(workspace);
	ipc_event_scroller("new", workspace);
}

void layout_set_type(struct sway_workspace *workspace, enum sway_container_layout type) {
	workspace->layout.type = type;
}

enum sway_container_layout layout_get_type(struct sway_workspace *workspace) {
	return workspace->layout.type;
}

static void recreate_view_buffer(struct sway_container *view) {
	// Views using CSD need to be reconfigured, otherwise the content
	// is not in sync with our borders. Also, some views created while
	// in scaled mode need to be reconfigured, so reconfigure everything
	// just in case.
	view_configure(view->view, view->pending.content_x, view->pending.content_y,
		view->pending.content_width, view->pending.content_height);
	view_reconfigure(view->view);
	node_set_dirty(&view->node);
}

static void recreate_buffers(struct sway_workspace *workspace) {
	for (int i = 0; i < workspace->tiling->length; ++i) {
		const struct sway_container *con = workspace->tiling->items[i];
		for (int j = 0; j < con->pending.children->length; ++j) {
			struct sway_container *view = con->pending.children->items[j];
			recreate_view_buffer(view);
		}
	}
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *con = workspace->floating->items[i];
		if (con->view) {
			recreate_view_buffer(con);
		} else if (con->pending.children){
			for (int j = 0; j < con->pending.children->length; ++j) {
				struct sway_container *view = con->pending.children->items[j];
				recreate_view_buffer(view);
			}
		}
	}
}

static void workspace_set_scale(struct sway_workspace *workspace, double scale) {
	workspace->layers.tiling->node.info.scale = scale;
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *con = workspace->floating->items[i];
		layout_view_scale_set(con, scale);
	}
}

void layout_compute_bounding_box(list_t *children, double *minx, double *maxx,
		double *miny, double *maxy) {
	*minx = DBL_MAX;
	*maxx = -DBL_MAX;
	*miny = DBL_MAX;
	*maxy = -DBL_MAX;
	for (int i = 0; i < children->length; ++i) {
		const struct sway_container *con = children->items[i];
		if (con->pending.x < *minx) {
			*minx = con->pending.x;
		}
		if (con->pending.x + con->pending.width > *maxx) {
			*maxx = con->pending.x + con->pending.width;
		}
		if (con->pending.y < *miny) {
			*miny = con->pending.y;
		}
		if (con->pending.y + con->pending.height > *maxy) {
			*maxy = con->pending.y + con->pending.height;
		}
	}
}

void layout_overview_recompute_scale(struct sway_workspace *workspace, int gaps) {
	enum sway_layout_overview mode = layout_overview_mode(workspace);
	if (workspace->tiling->length == 0 && mode == OVERVIEW_TILING) {
		return;
	}
	if (workspace->floating->length == 0 && mode == OVERVIEW_FLOATING) {
		return;
	}
	if (mode == OVERVIEW_ALL &&
		workspace->tiling->length == 0 && workspace->floating->length == 0) {
		return;
	}
	double fw = 0.0, fh = 0.0;
	if (mode == OVERVIEW_FLOATING || mode == OVERVIEW_ALL) {
		double minx, maxx, miny, maxy;
		layout_compute_bounding_box(workspace->floating, &minx, &maxx, &miny, &maxy);
		fw = maxx - minx;
		fh = maxy - miny;
	}
	double w = gaps, maxh = 0.0;
	if (mode == OVERVIEW_TILING || mode == OVERVIEW_ALL) {
		for (int i = 0; i < workspace->tiling->length; ++i) {
			const struct sway_container *con = workspace->tiling->items[i];
			w += con->pending.width + 2 * gaps;
			double h = gaps;
			for (int j = 0; j < con->pending.children->length; ++j) {
				const struct sway_container *view = con->pending.children->items[j];
				h += view->pending.height + 2 * gaps;
			}
			if (h > maxh) {
				maxh = h;
			}
		}
	}
	if (w < fw) {
		w = fw;
	}
	if (maxh < fh) {
		maxh = fh;
	}
	double scale = fmin(fmin(workspace->width / w, workspace->height / maxh), 1.0);
	if (workspace->layers.tiling->node.info.scale != scale) {
		workspace_set_scale(workspace, scale);
		node_set_dirty(&workspace->node);
		recreate_buffers(workspace);
	}
}

static void overview_push_container_position(struct sway_container *container, void *data) {
	container->overview.x = container->pending.x;
	container->overview.y = container->pending.y;
}

static void overview_pop_container_position(struct sway_container *container, void *data) {
	container->pending.x = container->overview.x;
	container->pending.y = container->overview.y;
}

static void overview_push_positions(struct sway_workspace *workspace,
		enum sway_layout_overview mode) {
	for (int i = 0; i < workspace->tiling->length; ++i) {
		struct sway_container *container = workspace->tiling->items[i];
		overview_push_container_position(container, NULL);
		container_for_each_child(container, overview_push_container_position, NULL);
	}
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *container = workspace->floating->items[i];
		overview_push_container_position(container, NULL);
		container_for_each_child(container, overview_push_container_position, NULL);
	}
}

static void overview_pop_positions(struct sway_workspace *workspace,
		enum sway_layout_overview mode) {
	for (int i = 0; i < workspace->tiling->length; ++i) {
		struct sway_container *container = workspace->tiling->items[i];
		overview_pop_container_position(container, NULL);
		container_for_each_child(container, overview_pop_container_position, NULL);
	}
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *container = workspace->floating->items[i];
		overview_pop_container_position(container, NULL);
		container_for_each_child(container, overview_pop_container_position, NULL);
	}
}

void layout_overview_toggle(struct sway_workspace *workspace, enum sway_layout_overview mode) {
	if (workspace->layout.overview != OVERVIEW_DISABLED) {
		overview_pop_positions(workspace, mode);
		// Disable and restore old scale value
		workspace->layout.overview = OVERVIEW_DISABLED;
		workspace_set_scale(workspace, workspace->layout.mem_scale);
		node_set_dirty(&workspace->node);
		recreate_buffers(workspace);
		struct sway_seat *seat = input_manager_current_seat();
		struct sway_container * focus = seat_get_focused_container(seat);
		bool ws_focused = focus && focus->pending.workspace == workspace;
		if (ws_focused && focus->pending.fullscreen_layout == FULLSCREEN_ENABLED) {
			output_layer_shell_enable(workspace->output, LAYER_SHELL_OVERLAY);
		}
		bool fs_focused = ws_focused && focus->fullscreen;
		bool fs = workspace->layout.fullscreen || fs_focused;
		if (ws_focused && fs) {
			if (fs_focused) {
				container_set_fullscreen(focus, FULLSCREEN_WORKSPACE);
				arrange_root();
			} else if (workspace->layout.fullscreen) {
				if (config->fullscreen_movefocus == FULLSCREEN_MOVEFOCUS_FOLLOW) {
					container_set_fullscreen(focus, FULLSCREEN_WORKSPACE);
				} else {
					container_set_fullscreen(workspace->layout.fullscreen, FULLSCREEN_NONE);
				}
				arrange_root();
			}
		} else if (workspace->layout.fullscreen) {
			container_set_fullscreen(workspace->layout.fullscreen, FULLSCREEN_WORKSPACE);
			arrange_root();
		}
	} else {
		overview_push_positions(workspace, mode);
		output_layer_shell_enable(workspace->output, LAYER_SHELL_ALL);
		workspace->layout.mem_scale = layout_scale_get(workspace);
		workspace->layout.overview = mode;
		workspace->layout.fullscreen = workspace->fullscreen;
		node_set_dirty(&workspace->node);
		if (workspace->fullscreen) {
			container_fullscreen_disable(workspace->fullscreen);
			arrange_root();
		}
		// In case the next transaction_commit_dirty() is delayed, making 
		// the overview scale invalid until then, we precompute the overview
		// scale here to avoid problems. For example, in jump mode,
		// container_toggle_jump_decoration() needs the correct scale.
		layout_overview_recompute_scale(workspace, workspace->gaps_inner);
	}
	animation_set_type(ANIMATION_OVERVIEW);
	ipc_event_scroller("overview", workspace);
}

enum sway_layout_overview layout_overview_mode(struct sway_workspace *workspace) {
	return workspace->layout.overview;
}

bool layout_overview_workspaces_enabled() {
	return root->overview;
}

static const int workspaces_gap = 20;

void layout_overview_workspaces_toggle() {
	root->overview = !root->overview;
	for (int i = 0; i < root->outputs->length; i++) {
		struct sway_output *output = root->outputs->items[i];
		if (layout_overview_workspaces_enabled()) {
			struct wlr_box *usable_area = &output->usable_area;
			float oscale = output->wlr_output->scale;
			double left = round(usable_area->x * oscale);
			double top = round(usable_area->y * oscale);
			double uwidth = round(usable_area->width * oscale);
			double uheight = round(usable_area->height * oscale);
			int width = output->wlr_output->width;
			int height = output->wlr_output->height;
			const int length = output->current.workspaces->length;
			const int rows = ceil(sqrt(length));
			const double scale = fmin((uwidth - workspaces_gap * (rows + 1)) / (rows * width),
				(uheight - workspaces_gap * (rows + 1)) / (rows * height));
			const int per_row = length / rows;
			int remain = length % rows;
			int j = 0;
			for (int r = 0; r < rows; ++r) {
				int cols = per_row;
				if (remain > 0) {
					++cols;
					remain--;
				}
				double gapx = (uwidth - cols * scale * width) / (cols + 1);
				double gapy = (uheight - rows * scale * height) / (rows + 1);
				for (int c = 0; c < cols; ++c) {
					struct sway_workspace *child = output->current.workspaces->items[j++];
					sway_scene_node_reparent(&child->jump.tree->node, output->layers.shell_overlay);
					child->layout.fullscreen = child->fullscreen;
					child->jump.x = round(left + gapx + c * (scale * width + gapx));
					child->jump.y = round(top + gapy + r * (scale * height + gapy));
					child->jump.width = ceil(scale * width);
					child->jump.height = ceil(scale * height);
					child->jump.scale = scale;
					child->layers.tiling->node.info.workspace = child;
					node_set_dirty(&child->node);
					if (child->fullscreen) {
						container_set_fullscreen(child->fullscreen, FULLSCREEN_NONE);
						arrange_root();
					}
					for (int f = 0; f < child->floating->length; ++f) {
						struct sway_container *con = child->floating->items[f];
						con->scene_tree->node.info.workspace = child;
					}
				}
			}
		} else {
			for (int j = 0; j < output->current.workspaces->length; ++j) {
				struct sway_workspace *child = output->current.workspaces->items[j];
				sway_scene_node_reparent(&child->jump.tree->node, root->staging);
				child->layers.tiling->node.info.workspace = NULL;
				node_set_dirty(&child->node);
				if (child->layout.fullscreen) {
					container_set_fullscreen(child->layout.fullscreen, FULLSCREEN_WORKSPACE);
					arrange_root();
				}
				for (int f = 0; f < child->floating->length; ++f) {
					struct sway_container *con = child->floating->items[f];
					con->scene_tree->node.info.workspace = NULL;
				}
			}
		}
		output_damage_whole(output);
	}
}

void layout_scale_set(struct sway_workspace *workspace, double scale) {
	if (!workspace) {
		return;
	}
	workspace_set_scale(workspace, scale);
	node_set_dirty(&workspace->node);
	recreate_buffers(workspace);
	ipc_event_scroller("scale", workspace);
}

void layout_scale_reset(struct sway_workspace *workspace) {
	if (!workspace) {
		return;
	}
	workspace_set_scale(workspace, -1.0);
	node_set_dirty(&workspace->node);
	recreate_buffers(workspace);
	ipc_event_scroller("scale", workspace);
}

double layout_scale_get(struct sway_workspace *workspace) {
	return workspace->layers.tiling->node.info.scale;
}

bool layout_scale_enabled(struct sway_workspace *workspace) {
	if (!workspace) {
		return false;
	}
	return workspace->layers.tiling->node.info.scale > 0.0;
}

void layout_view_scale_set(struct sway_container *view, double scale) {
	view->scene_tree->node.info.scale = scale;
}

void layout_view_scale_reset(struct sway_container *view) {
	view->scene_tree->node.info.scale = -1.0;
}

double layout_view_scale_get(struct sway_container *view) {
	return view->scene_tree->node.info.scale;
}

bool layout_view_scale_enabled(struct sway_container *view) {
	return view->scene_tree->node.info.scale > 0.0;
}

void layout_modifiers_init(struct sway_workspace *workspace) {
	struct sway_scroller *layout = &workspace->layout;
	if (workspace && workspace->output) {
		layout->type = output_get_default_layout(workspace->output);
	} else {
		if (config->default_layout != L_NONE) {
			layout->type = config->default_layout;
		} else {
			layout->type = L_HORIZ;
		}
	}
	layout->modifiers.reorder = REORDER_AUTO;
	layout->modifiers.mode = layout->type;
	layout->modifiers.insert = INSERT_AFTER;
	layout->modifiers.focus = true;
	layout->modifiers.center_horizontal = false;
	layout->modifiers.center_vertical = false;
}

void layout_modifiers_set_reorder(struct sway_workspace *workspace, enum sway_layout_reorder reorder) {
	workspace->layout.modifiers.reorder = reorder;
	ipc_event_scroller("reorder", workspace);
}

enum sway_layout_reorder layout_modifiers_get_reorder(struct sway_workspace *workspace) {
	if (layout_overview_mode(workspace) != OVERVIEW_DISABLED) {
		workspace->layout.modifiers.reorder = REORDER_AUTO;
	}
	return workspace->layout.modifiers.reorder;
}

void layout_modifiers_set_mode(struct sway_workspace *workspace, enum sway_container_layout mode) {
	workspace->layout.modifiers.mode = mode;
	ipc_event_scroller("mode", workspace);
}

enum sway_container_layout layout_modifiers_get_mode(struct sway_workspace *workspace) {
	return workspace->layout.modifiers.mode;
}

void layout_modifiers_set_insert(struct sway_workspace *workspace, enum sway_layout_insert ins) {
	workspace->layout.modifiers.insert = ins;
	ipc_event_scroller("insert", workspace);
}

enum sway_layout_insert layout_modifiers_get_insert(struct sway_workspace *workspace) {
	return workspace->layout.modifiers.insert;
}

void layout_modifiers_set_focus(struct sway_workspace *workspace, bool focus) {
	workspace->layout.modifiers.focus = focus;
	ipc_event_scroller("focus", workspace);
}

bool layout_modifiers_get_focus(struct sway_workspace *workspace) {
	return workspace->layout.modifiers.focus;
}

void layout_modifiers_set_center_horizontal(struct sway_workspace *workspace, bool center) {
	workspace->layout.modifiers.center_horizontal = center;
	ipc_event_scroller("center_horizontal", workspace);
}

bool layout_modifiers_get_center_horizontal(struct sway_workspace *workspace) {
	if (layout_overview_mode(workspace) != OVERVIEW_DISABLED) {
		return false;
	}
	return workspace->layout.modifiers.center_horizontal;
}

void layout_modifiers_set_center_vertical(struct sway_workspace *workspace, bool center) {
	workspace->layout.modifiers.center_vertical = center;
	ipc_event_scroller("center_vertical", workspace);
}

bool layout_modifiers_get_center_vertical(struct sway_workspace *workspace) {
	if (layout_overview_mode(workspace) != OVERVIEW_DISABLED) {
		return false;
	}
	return workspace->layout.modifiers.center_vertical;
}

static int layout_insert_compute_index(list_t *list, void *active, enum sway_layout_insert pos) {
	switch (pos) {
	case INSERT_BEFORE:
	case INSERT_AFTER:
	default: {
		int index = list_find(list, active);
		return pos == INSERT_AFTER ? index + 1 : index;
	}
	case INSERT_BEGINNING:
		return 0;
	case INSERT_END:
		return list->length;
	}
}

// Try to set the new container in an approximate position where it will
// not remove the old active from view if they are neighbors.
static void position_new_container(struct sway_workspace *workspace,
		list_t *children, struct sway_container *active,
		struct sway_container *container, int container_idx) {

	int active_idx = list_find(children, active);
	if (active_idx >= 0) {
		if (layout_modifiers_get_mode(workspace) == L_HORIZ) {
			if (container_idx < active_idx) {
				double width = container->width_fraction * workspace->width;
				container->pending.x = active->pending.x - width;
			} else {
				container->pending.x = active->pending.x + active->current.width;
			}
		} else {
			if (container_idx < active_idx) {
				double height = container->height_fraction * workspace->height;
				container->pending.y = active->pending.y - height;
			} else {
				container->pending.y = active->pending.y + active->current.height;
			}
		}
	}
}

static void layout_container_add_view(struct sway_container *active, struct sway_container *view,
			enum sway_container_layout layout, enum sway_layout_insert pos) {
	struct sway_container *parent = active->pending.parent ? active->pending.parent : active;
	struct sway_container *ref = parent == active ? parent->pending.focused_inactive_child : active;
	container_insert_update_parent_fullscreen_layout(parent, view);
	int index = layout_insert_compute_index(parent->pending.children, ref, pos);
	container_insert_child(parent, view, index);
	view->width_fraction = parent->width_fraction;
	view->height_fraction = parent->height_fraction;
	view->toggle_size = parent->toggle_size;
	if (ref) {
		position_new_container(parent->pending.workspace, parent->pending.children,
			ref, view, index);
	}
}

// Wraps a view container into a container with layout, and returns it.
static struct sway_container *layout_wrap_into_container(struct sway_container *child,
		enum sway_container_layout layout) {
	struct sway_container *cont = container_create(NULL);
	cont->current.width = child->current.width;
	cont->current.height = child->current.height;
	cont->pending.width = child->pending.width;
	cont->pending.height = child->pending.height;
	cont->width_fraction = child->width_fraction;
	cont->height_fraction = child->height_fraction;
	cont->toggle_size = child->toggle_size;
	cont->current.x = child->current.x;
	cont->current.y = child->current.y;
	cont->pending.x = child->pending.x;
	cont->pending.y = child->pending.y;
	cont->pending.layout = layout;
	cont->current.focused_inactive_child = child;
	cont->pending.focused_inactive_child = child;
	cont->current.fullscreen_layout = child->current.fullscreen_layout;
	cont->pending.fullscreen_layout = child->pending.fullscreen_layout;

	list_add(cont->pending.children, child);
	child->pending.parent = cont;
	cont->pending.workspace = child->pending.workspace;
	container_update_representation(cont);
	node_set_dirty(&child->node);
	node_set_dirty(&cont->node);
	return cont;
}

static void layout_workspace_add_view(struct sway_workspace *workspace, struct sway_container *active, struct sway_container *view,
			enum sway_layout_insert pos) {
	int idx;
	if (!active) {
		idx = 0;
	} else if (active->pending.parent) {
		// active is in a container, need to insert at the level of its parent
		idx = layout_insert_compute_index(active->pending.workspace->tiling, active->pending.parent, pos);
	} else {
		idx = layout_insert_compute_index(active->pending.workspace->tiling, active, pos);
	}
	// Create a container and add view
	enum sway_container_layout mode = layout_get_type(workspace) == L_HORIZ ? L_VERT : L_HORIZ;
	struct sway_container *parent = layout_wrap_into_container(view, mode);
	// Set the container and view width/height
	if (view->width_fraction <= 0.0) {
		view->width_fraction = layout_get_default_width(workspace);
		// Start the animation from the center of the workspace
		view->current.x = workspace->x + 0.5 * workspace->width;
		view->pending.x = view->current.x;
		parent->current.x = view->current.x;
		parent->pending.x = view->current.x;
	}
	parent->width_fraction = view->width_fraction;
	if (view->height_fraction <= 0.0) {
		view->height_fraction = layout_get_default_height(workspace);
		view->current.y = workspace->y + 0.5 * workspace->height;
		view->pending.y = view->current.y;
		parent->current.y = view->current.y;
		parent->pending.y = view->current.y;
	}
	parent->height_fraction = view->height_fraction;
	// Insert the container
	workspace_insert_tiling_direct(workspace, parent, idx);

	// When adding a new view, reset REORDER to AUTO
	layout_modifiers_set_reorder(workspace, REORDER_AUTO);

	if (active) {
		position_new_container(workspace, workspace->tiling,
			active->pending.parent ? active->pending.parent : active, parent, idx);
	}
}

// Layout API
void layout_add_view(struct sway_workspace *workspace, struct sway_container *active, struct sway_container *view) {
	layout_view_scale_reset(view);
	enum sway_layout_insert pos = layout_modifiers_get_insert(workspace);
	enum sway_container_layout mode = layout_modifiers_get_mode(workspace);
	if (layout_get_type(workspace) == mode || workspace->tiling->length == 0) {
		layout_workspace_add_view(workspace, active, view, pos);
	} else {
		layout_container_add_view(active, view, mode, pos);
	}
}

static void layout_workspace_add_container(struct sway_workspace *workspace, struct sway_container *active, struct sway_container *container,
			enum sway_layout_insert pos) {
	int idx;
	if (!active) {
		idx = 0;
	} else if (active->pending.parent) {
		// active is in a container, need to insert at the level of its parent
		idx = layout_insert_compute_index(active->pending.workspace->tiling, active->pending.parent, pos);
	} else {
		idx = layout_insert_compute_index(active->pending.workspace->tiling, active, pos);
	}
	// The container needs to be of the opposite type of the layout type, if not,
	// flip the container layout
	if (layout_get_type(workspace) == container->pending.layout) {
		container->pending.layout = container->pending.layout == L_HORIZ ? L_VERT : L_HORIZ;
	}
	// Insert the container
	workspace_insert_tiling_direct(workspace, container, idx);
}

void layout_move_container_to_workspace(struct sway_container *container, struct sway_workspace *workspace) {
	struct sway_workspace *old_ws = container->pending.workspace;
	// Remove container from old_ws, updating the active if any
	// If we are in layout mode equal to the layout type, we move the whole container,
	// otherwise, we extract the view container and add it to the other workspace,
	// inserting it in the active container (if any), or creating a new one
	enum sway_container_layout mode = layout_modifiers_get_mode(old_ws);
	struct sway_container *active = workspace->current.focused_inactive_child;
	if (layout_get_type(old_ws) == mode) {
		// Move the whole container
		struct sway_container *con = container->view ? container->pending.parent : container;
		int old_idx = list_find(old_ws->tiling, con);
		list_del(old_ws->tiling, old_idx);
		node_set_dirty(&old_ws->node);
		// Find insertion point
		enum sway_layout_insert pos = layout_modifiers_get_insert(workspace);
		layout_workspace_add_container(workspace, active, con, pos);
		node_set_dirty(&con->node);
	} else {
		// Move only the view
		// Extract the view from the parent container
		struct sway_container *parent = container->pending.parent;
		list_t *siblings = parent->pending.children;
		list_del(siblings, list_find(siblings, container));
		container_detach_update_parent_fullscreen_layout(parent, container);
		container_update_representation(parent);
		node_set_dirty(&parent->node);
		container_reap_empty(parent);
		layout_add_view(workspace, active, container);
		node_set_dirty(&container->node);
	}
	workspace_update_representation(workspace);
	node_set_dirty(&workspace->node);
}

enum sway_operation {
	OPERATION_FOCUS,
	OPERATION_UNFOCUS,
	OPERATION_RESIZE,
	OPERATION_TOGGLE
};

static void apply_container_sizes(struct sway_container *container,
		double new_width, double new_height, enum sway_operation op);

// Dragging
static void drag_parent_container_to_workspace(struct sway_container *container, struct sway_workspace *workspace) {
	if (container->view) {
		return;
	}
	struct sway_workspace *old_workspace = container->pending.workspace;
	// top level container
	int old_idx = list_find(old_workspace->tiling, container);
	if (old_idx >= 0) {
		// Only if moving a parent container, not if moving a view wrapped into a new container (still not in the tiling list)
		list_del(old_workspace->tiling, old_idx);
	}
	//node_set_dirty(&old_workspace->node);
	if (old_workspace != workspace) {
		container->pending.layout = workspace->layout.type == L_HORIZ ? L_VERT : L_HORIZ;
		arrange_workspace(old_workspace);
	}
	// Find insertion point
	enum sway_layout_insert pos = layout_modifiers_get_insert(workspace);
	struct sway_container *active = workspace->current.focused_inactive_child;
	layout_workspace_add_container(workspace, active, container, pos);
}

// This function inserts
// container into workspace according to the current insertion mode.
// If the node is a top level container, it relocates it. If the node is a view
// container, it is first extracted from its parent container (if it has other
// siblings), and relocated to its own container.
void layout_drag_container_to_workspace(struct sway_container *container, struct sway_workspace *workspace) {
	struct sway_workspace *old_workspace = container->pending.workspace;
	enum sway_container_layout layout = layout_get_type(old_workspace);
	enum sway_container_layout mode = layout_modifiers_get_mode(old_workspace);
	if (mode == layout) {
		container = container->pending.parent;
		drag_parent_container_to_workspace(container, workspace);
	} else {
		// Extract the view from the parent container if needed
		struct sway_container *parent = container->pending.parent;
		list_t *siblings = parent->pending.children;
		if (siblings->length > 1) {
			list_del(siblings, list_find(siblings, container));
			container_update_representation(parent);
			node_set_dirty(&parent->node);
			apply_container_sizes(parent, layout_toggle_size_width_fraction(workspace),
				layout_toggle_size_height_fraction(workspace), OPERATION_UNFOCUS);
			// Create a new container for extracted child
			parent = layout_wrap_into_container(container, parent->pending.layout);
		}
		node_set_dirty(&parent->node);
		drag_parent_container_to_workspace(parent, workspace);
	}
	apply_container_sizes(container, layout_toggle_size_width_fraction(workspace),
		layout_toggle_size_height_fraction(workspace), OPERATION_FOCUS);
	layout_maximize_if_single(workspace);
}

static void remove_active_parent_container_from_workspace(int idx, struct sway_workspace *workspace) {
	list_del(workspace->tiling, idx);
	workspace_update_representation(workspace);
	node_set_dirty(&workspace->node);
}

static void move_container_before(struct sway_container *container, struct sway_container *target,
		struct sway_workspace *workspace) {
	struct sway_workspace *old_workspace = container->pending.workspace;
	int cidx = list_find(old_workspace->tiling, container);
	int tidx = list_find(workspace->tiling, target);
	if (cidx < 0 || tidx < 0) {
		// Shouldn't happen, but just in case
		return;
	}
	if (old_workspace != workspace) {
		remove_active_parent_container_from_workspace(cidx, old_workspace);
		container->pending.layout = workspace->layout.type == L_HORIZ ? L_VERT : L_HORIZ;
		workspace_insert_tiling_direct(workspace, container, tidx);
		return;
	}
	// Move container
	if (cidx == tidx - 1) {
		// Correct position
		return;
	}
	if (cidx < tidx) {
		list_move_to(workspace->tiling, tidx - 1, container);
	} else {
		list_move_to(workspace->tiling, tidx, container);
	}
}

static void move_container_after(struct sway_container *container, struct sway_container *target,
		struct sway_workspace *workspace) {
	struct sway_workspace *old_workspace = container->pending.workspace;
	int cidx = list_find(old_workspace->tiling, container);
	int tidx = list_find(workspace->tiling, target);
	if (cidx < 0 || tidx < 0) {
		// Shouldn't happen, but just in case
		return;
	}
	if (old_workspace != workspace) {
		remove_active_parent_container_from_workspace(cidx, old_workspace);
		container->pending.layout = workspace->layout.type == L_HORIZ ? L_VERT : L_HORIZ;
		workspace_insert_tiling_direct(workspace, container, tidx + 1);
		return;
	}
	// Move container
	if (cidx == tidx + 1) {
		// Correct position
		return;
	}
	if (cidx < tidx) {
		list_move_to(workspace->tiling, tidx, container);
	} else {
		list_move_to(workspace->tiling, tidx + 1, container);
	}
}

static void swap_containers(struct sway_container *container, struct sway_container *target,
		struct sway_workspace *workspace) {
	struct sway_workspace *old_workspace = container->pending.workspace;
	int cidx = list_find(old_workspace->tiling, container);
	int tidx = list_find(workspace->tiling, target);
	if (cidx < 0 || tidx < 0) {
		// Shouldn't happen, but just in case
		return;
	}
	if (old_workspace != workspace) {
		remove_active_parent_container_from_workspace(cidx, old_workspace);
		remove_active_parent_container_from_workspace(tidx, workspace);
		container->pending.layout = workspace->layout.type == L_HORIZ ? L_VERT : L_HORIZ;
		target->pending.layout = old_workspace->layout.type == L_HORIZ ? L_VERT : L_HORIZ;
		workspace_insert_tiling_direct(old_workspace, target, cidx);
		workspace_insert_tiling_direct(workspace, container, tidx);
		return;
	}
	// Move container
	if (cidx == tidx) {
		// Correct position
		return;
	}
	list_swap(workspace->tiling, cidx, tidx);
}

static void insert_children_relative(struct sway_container *container, struct sway_container *target, int offset) {
	struct sway_workspace *old_workspace = container->pending.workspace;
	int cidx = list_find(old_workspace->tiling, container);
	remove_active_parent_container_from_workspace(cidx, old_workspace);
	int idx = list_find(target->pending.children, target->current.focused_inactive_child);
	if (idx >= 0) {
		offset = offset == 0 ? idx : idx + 1;
	}
	list_t *children = container->pending.children;
	while (children->length > 0) {
		struct sway_container *con = children->items[children->length - 1];
		container_insert_update_parent_fullscreen_layout(target, con);
		con->pending.parent = target;
		con->pending.workspace = target->pending.workspace;
		list_insert(target->pending.children, offset, con);
		list_del(children, children->length - 1);
	}
	container_reap_empty(container);
	container_update_representation(target);
	node_set_dirty(&target->node);
}

static struct sway_container *extract_view(struct sway_container *view) {
	struct sway_container *parent = view->pending.parent;
	list_t *siblings = parent->pending.children;
	int idx = list_find(siblings, view);
	list_del(siblings, idx);
	container_detach_update_parent_fullscreen_layout(parent, view);
	container_update_representation(parent);
	node_set_dirty(&parent->node);
	container_reap_empty(parent);
	return view;
}

// Extract view container from parent and insert into target's parent, before target,
// possibly removing container's parent if empty
static void insert_view_before(struct sway_container *container, struct sway_container *target) {
	container = extract_view(container);
	container->pending.workspace = target->pending.workspace;
	struct sway_container *parent = target->pending.parent;
	container_insert_update_parent_fullscreen_layout(parent, container);
	int tidx = list_find(parent->pending.children, target);
	list_insert(parent->pending.children, tidx, container);
	container->pending.parent = parent;
	container_update_representation(parent);
	node_set_dirty(&parent->node);
}

// Extract view container from parent and insert into target's parent, after target,
// possibly removing container's parent if empty
static void insert_view_after(struct sway_container *container, struct sway_container *target) {
	container = extract_view(container);
	container->pending.workspace = target->pending.workspace;
	struct sway_container *parent = target->pending.parent;
	container_insert_update_parent_fullscreen_layout(parent, container);
	int tidx = list_find(parent->pending.children, target);
	list_insert(parent->pending.children, tidx + 1, container);
	container->pending.parent = parent;
	container_update_representation(parent);
	node_set_dirty(&parent->node);
}

static void extract_view_and_insert_before(struct sway_container *container,
			struct sway_container *reference, struct sway_workspace *workspace) {
	container = extract_view(container);
	container->pending.workspace = workspace;
	container = layout_wrap_into_container(container, layout_get_type(workspace) == L_HORIZ ? L_VERT : L_HORIZ);
	int idx = list_find(workspace->tiling, reference);
	list_insert(workspace->tiling, idx, container);
	node_set_dirty(&container->node);
}

static void extract_view_and_insert_after(struct sway_container *container,
			struct sway_container *reference, struct sway_workspace *workspace) {
	container = extract_view(container);
	container->pending.workspace = workspace;
	container = layout_wrap_into_container(container, layout_get_type(workspace) == L_HORIZ ? L_VERT : L_HORIZ);
	int idx = list_find(workspace->tiling, reference);
	list_insert(workspace->tiling, idx + 1, container);
	node_set_dirty(&container->node);
}

// Swap views in possibly different containers of possibly different workspaces
static void	swap_views(struct sway_container *container, struct sway_container *target) {
	struct sway_container *cparent = container->pending.parent;
	struct sway_container *tparent = target->pending.parent;
	int cidx = list_find(cparent->pending.children, container);
	int tidx = list_find(tparent->pending.children, target);
	if (cparent == tparent) {
		list_swap(cparent->pending.children, cidx, tidx);
		container_update_representation(cparent);
		node_set_dirty(&cparent->node);
		return;
	}
	struct sway_workspace *cworkspace = container->pending.workspace;
	struct sway_workspace *tworkspace = target->pending.workspace;
	list_del(cparent->pending.children, cidx);
	list_del(tparent->pending.children, tidx);
	container_detach_update_parent_fullscreen_layout(cparent, container);
	container_detach_update_parent_fullscreen_layout(tparent, target);
	container_insert_update_parent_fullscreen_layout(cparent, target);
	container_insert_update_parent_fullscreen_layout(tparent, container);
	list_insert(cparent->pending.children, cidx, target);
	list_insert(tparent->pending.children, tidx, container);
	container->pending.parent = tparent;
	target->pending.parent = cparent;
	target->pending.workspace = cworkspace;
	container->pending.workspace = tworkspace;
	container_update_representation(cparent);
	node_set_dirty(&cparent->node);
	container_update_representation(tparent);
	node_set_dirty(&tparent->node);
}

// Move a container into/near target according to edge
// 1. If container is a top level container, if edge is parallel to layout type,
// insert every children of container into target, and remove container.
// 2. If container is a top level container and edge is not parallel to layout
// type, move container to the new position.
// 3. If container is a view container:
// 3.1 if edge is parallel to layout type, extract container from its parent
// and insert it into target, possibly removing the old parent if empty
// 3.2 if edge is not parallel, extract container from its parent, create a new
// parent container and move it to the correct location.
//
void layout_drag_container_to_container(struct sway_container *container, struct sway_container *target,
		struct sway_workspace *workspace, enum wlr_edges edge) {
	struct sway_workspace *old_workspace = container->pending.workspace;
	bool move_parent = layout_get_type(old_workspace) == layout_modifiers_get_mode(old_workspace);
	enum sway_container_layout layout = layout_get_type(workspace);
	if (move_parent) {
		// Get parent (top level container)
		container = container->pending.parent;
		if (target->view) {
			target = target->pending.parent;
		}
		if (container == target) {
			return;
		}
		if (layout == L_HORIZ) {
			switch (edge) {
			case WLR_EDGE_TOP:
				// Parallel
				insert_children_relative(container, target, 0);
				break;
			case WLR_EDGE_BOTTOM:
				// Parallel
				insert_children_relative(container, target, target->pending.children->length);
				break;
			case WLR_EDGE_LEFT:
				// Move container
				move_container_before(container, target, workspace);
				break;
			case WLR_EDGE_RIGHT:
				// Move container
				move_container_after(container, target, workspace);
				break;
			case WLR_EDGE_NONE:
				// Swap containers
				swap_containers(container, target, workspace);
				break;
			}
		} else {
			switch (edge) {
			case WLR_EDGE_TOP:
				// Move container
				move_container_before(container, target, workspace);
				break;
			case WLR_EDGE_BOTTOM:
				// Move container
				move_container_after(container, target, workspace);
				break;
			case WLR_EDGE_LEFT:
				// Parallel
				insert_children_relative(container, target, 0);
				break;
			case WLR_EDGE_RIGHT:
				// Parallel
				insert_children_relative(container, target, target->pending.children->length);
				break;
			case WLR_EDGE_NONE:
				// Swap containers
				swap_containers(container, target, workspace);
				break;
			}
		}
	} else {
		if (container == target) {
			return;
		}
		// Check if parent can be a top level container...???
		if (layout == L_HORIZ) {
			switch (edge) {
			case WLR_EDGE_TOP:
				// Parallel
				insert_view_before(container, target);
				break;
			case WLR_EDGE_BOTTOM:
				// Parallel
				insert_view_after(container, target);
				break;
			case WLR_EDGE_LEFT:
				// Move container
				extract_view_and_insert_before(container, target->pending.parent, workspace);
				break;
			case WLR_EDGE_RIGHT:
				// Move container
				extract_view_and_insert_after(container, target->pending.parent, workspace);
				break;
			case WLR_EDGE_NONE:
				// Swap views
				swap_views(container, target);
				break;
			}
		} else {
			switch (edge) {
			case WLR_EDGE_TOP:
				// Move container
				extract_view_and_insert_before(container, target->pending.parent, workspace);
				break;
			case WLR_EDGE_BOTTOM:
				// Move container
				extract_view_and_insert_after(container, target->pending.parent, workspace);
				break;
			case WLR_EDGE_LEFT:
				// Parallel
				insert_view_before(container, target);
				break;
			case WLR_EDGE_RIGHT:
				// Parallel
				insert_view_after(container, target);
				break;
			case WLR_EDGE_NONE:
				// Swap views
				swap_views(container, target);
				break;
			}
		}
	}
	arrange_workspace(workspace);
	workspace_update_representation(workspace);
	node_set_dirty(&workspace->node);
	if (old_workspace && old_workspace != workspace) {
		arrange_workspace(old_workspace);
		workspace_update_representation(old_workspace);
		node_set_dirty(&old_workspace->node);
	}
}

static int layout_direction_compute_index(list_t *list, void *item,
		enum sway_layout_direction dir, bool detach) {
	int index;
	switch (dir) {
	case DIR_LEFT:
	case DIR_UP:
	case DIR_RIGHT:
	case DIR_DOWN:
	default: {
		index = list_find(list, item);
		return dir == DIR_RIGHT || dir == DIR_DOWN ? index + 1 : detach ? index : index - 1;
	}
	case DIR_BEGIN:
		return 0;
	case DIR_END:
		return list->length - 1;
	}
}

static bool need_to_detach(enum sway_container_layout parent_layout, enum sway_layout_direction dir) {
	if (dir == DIR_LEFT || dir == DIR_RIGHT) {
		if (parent_layout == L_HORIZ) {
			return false;
		}
	} else if (dir == DIR_UP || dir == DIR_DOWN) {
		if (parent_layout == L_VERT) {
			return false;
		}
	} else if (dir == DIR_BEGIN || dir == DIR_END) {
		return false;
	}
	return true;
}

// Detaches a container from its parent container and wraps it into a container
static struct sway_container *layer_container_detach(struct sway_container *child) {
	struct sway_container *parent = child->pending.parent;
	list_t *siblings = parent->pending.children;
	list_del(siblings, list_find(siblings, child));
	container_detach_update_parent_fullscreen_layout(parent, child);
	struct sway_container *new_parent = layout_wrap_into_container(child, parent->pending.layout);
	container_update_representation(parent);
	node_set_dirty(&parent->node);
	return new_parent;
}

// Inserts a top level orphan container (view) into a container
static void layout_insert_into_container(struct sway_container *parent, struct sway_container *child, int idx) {
	container_insert_update_parent_fullscreen_layout(parent, child);
	list_insert(parent->pending.children, idx, child);
	child->pending.parent = parent;
	child->pending.workspace = parent->pending.workspace;
	parent->current.focused_inactive_child = child;
	parent->pending.focused_inactive_child = child;
	container_update_representation(parent);
	node_set_dirty(&child->node);
	node_set_dirty(&parent->node);
}

static bool layout_move_container_nomode(struct sway_container *container, enum sway_layout_direction dir) {
	// Don't allow containers to move out of their fullscreen or floating parent
	if (container->pending.fullscreen_mode || container_is_floating(container)) {
		return false;
	}

	struct sway_container *parent = container->pending.parent;
	enum sway_container_layout parent_layout = container_parent_layout(container);
	struct sway_workspace *workspace = container->pending.workspace;

	if (parent->pending.children->length > 1) {
		// If container has siblings, detach and insert at the top level using dir.
		list_t *children;
		int new_index;
		if (need_to_detach(parent_layout, dir)) {
			// Here, moving to the left should return parent's index (parent will be afer)
			// Moving to the right should return parent's index + 1
			new_index = max(layout_direction_compute_index(workspace->tiling, parent, dir, true), 0);
			container = layer_container_detach(container);
			children = workspace->tiling;
			list_insert(children, new_index, container);
			node_set_dirty(&workspace->node);
			apply_container_sizes(parent, layout_toggle_size_width_fraction(workspace),
				layout_toggle_size_height_fraction(workspace), OPERATION_UNFOCUS);
			apply_container_sizes(container, layout_toggle_size_width_fraction(workspace),
				layout_toggle_size_height_fraction(workspace), OPERATION_FOCUS);
			layout_maximize_if_single(workspace);
		} else {
			children = parent->pending.children;
			new_index = layout_direction_compute_index(children, container, dir, false);
			if (new_index < 0 || new_index >= children->length) {
				return false;
			}
			list_move_to(children, new_index, container);
			node_set_dirty(&parent->node);
		}
	} else {
		// If beginning or end, move parent container to location
		// Compute neighbor
		int neighbor_index = layout_direction_compute_index(workspace->tiling, parent, dir, false);
		if (neighbor_index < 0 || neighbor_index >= workspace->tiling->length) {
			return false;
		}
		struct sway_container *new_parent = workspace->tiling->items[neighbor_index];
		if (new_parent == parent) {
			return false;
		}
		int index = list_find(workspace->tiling, parent);
		if (dir == DIR_BEGIN || dir == DIR_END) {
			list_swap(workspace->tiling, neighbor_index, index);
		} else {
			list_del(workspace->tiling, index);
			// Remove container from old parent
			list_del(parent->pending.children, 0);
			// Insert container into neighbor
			enum sway_layout_insert pos = layout_modifiers_get_insert(workspace);
			struct sway_container *active = new_parent->current.focused_inactive_child;
			int new_index = layout_insert_compute_index(new_parent->pending.children, active, pos);
			layout_insert_into_container(new_parent, container, new_index);
			// Delete old parent container
			container_reap_empty(parent);
		}
		apply_container_sizes(new_parent, layout_toggle_size_width_fraction(workspace),
			layout_toggle_size_height_fraction(workspace), OPERATION_FOCUS);
		layout_maximize_if_single(workspace);
		node_set_dirty(&workspace->node);
	}
	return true;
}

static bool layout_move_container_mode(struct sway_container *container, enum sway_layout_direction dir) {
	// Look for a suitable ancestor of the container to move within
	struct sway_container *current = container;

	while (current) {
		// Don't allow containers to move out of their fullscreen or floating parent
		if (current->pending.fullscreen_mode || container_is_floating(current)) {
			return false;
		}

		bool can_move = false;
		int desired;
		int idx = container_sibling_index(current);
		enum sway_container_layout parent_layout = current->pending.parent ?
			container_parent_layout(current) : layout_get_type(current->pending.workspace);
		list_t *siblings = container_get_siblings(current);

		if (dir == DIR_LEFT || dir == DIR_RIGHT) {
			if (parent_layout == L_HORIZ) {
				can_move = true;
				desired = idx + (dir == DIR_LEFT ? -1 : 1);
			}
		} else if (dir == DIR_UP || dir == DIR_DOWN) {
			if (parent_layout == L_VERT) {
				can_move = true;
				desired = idx + (dir == DIR_UP ? -1 : 1);
			}
		} else if (dir == DIR_BEGIN || dir == DIR_END) {
			// We move the container within this container if the layout mode is the
			// same as the container's
			struct sway_workspace *workspace = config->handler_context.workspace;
			enum sway_container_layout mode = layout_modifiers_get_mode(workspace);
			if (mode == parent_layout) {
				can_move = true;
				desired = dir == DIR_BEGIN ? 0 : siblings->length - 1;
			}
		}
		
		if (can_move && siblings->length > 1) {
			if (desired >= 0 && desired < siblings->length) {
				list_del(siblings, idx);
				list_insert(siblings, desired, current);
				if (current->pending.parent) {
					node_set_dirty(&current->pending.parent->node);
					node_set_dirty(&current->node);
				} else {
					node_set_dirty(&current->pending.workspace->node);
				}
				return true;
			}
		}

		current = current->pending.parent;
	}

	return false;
}

bool layout_move_container(struct sway_container *container, enum sway_layout_direction dir, bool nomode) {
	if (nomode) {
		return layout_move_container_nomode(container, dir);
	} else {
		return layout_move_container_mode(container, dir);
	}
}

static void switch_workspace(struct sway_workspace *workspace) {
	// Focus workspace
	struct sway_seat *seat = input_manager_current_seat();
	struct sway_node *next = seat_get_focus_inactive(seat, &workspace->node);
	if (next == NULL) {
		next = &workspace->node;
	}
	seat_set_focus(seat, next);
	seat_consider_warp_to_focus(seat);
}

static void container_toggle_jump_decoration(struct sway_workspace *workspace,
		struct sway_container *con, char *text,	double width, double height) {
	if (!text) {
		if (con->jump.text) {
			sway_scene_node_destroy(con->jump.text->node);
			con->jump.text = NULL;
		}
		return;
	} else if (!con->jump.text) {
		con->jump.text = sway_text_node_create(con->jump.tree,
			text, config->jump_labels_color, false);
	} else {
		sway_text_node_set_text(con->jump.text, text);
	}
	sway_text_node_set_background(con->jump.text, config->jump_labels_background);
	double jscale = config->jump_labels_scale;
	double wscale = workspace && layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0;
	double scale = fmin(width / con->jump.text->width, height / con->jump.text->height);
	sway_text_node_scale(con->jump.text, jscale * scale * wscale);
	int x = 0.5 * wscale * (width - con->jump.text->width * jscale * scale);
	int y = 0.5 * wscale * (height - con->jump.text->height * jscale * scale);
	sway_scene_node_set_position(&con->jump.tree->node, x, y);
	sway_scene_node_set_enabled(&con->jump.tree->node, true);
	node_set_dirty(&con->node);
}

struct jump_data {
	void *specific;
	void *common;
};

struct jump_specific_data {
	list_t *workspaces;
};

struct jump_common_data {
	uint32_t keys_pressed;
	uint32_t nkeys;
	uint32_t window_number;
	uint32_t nwindows;
	void (*keyboard_key_end)(void *data, bool focus);
};

static void override_input(bool override, sway_keyboard_cb_fn callback, void *data,
		sway_seat_button_cb_fn button_callback, void *button_data) {
	struct sway_seat *seat = input_manager_current_seat();
	struct sway_seat_device *seat_device, *next;
	wl_list_for_each_safe(seat_device, next, &seat->devices, link) {
		struct sway_keyboard *keyboard = seat_device->keyboard;
		if (keyboard == NULL) {
			continue;
		}
		if (override) {
			sway_keyboard_set_keypress_cb(keyboard, callback, data);
		} else {
			sway_keyboard_set_keypress_cb(keyboard, NULL, NULL);
		}
	}
	struct sway_keyboard_group *keyboard_group, *next_kg;
	wl_list_for_each_safe(keyboard_group, next_kg, &seat->keyboard_groups, link) {
		if (override) {
			sway_keyboard_set_keypress_cb(keyboard_group->seat_device->keyboard, callback, data);
		} else {
			sway_keyboard_set_keypress_cb(keyboard_group->seat_device->keyboard, NULL, NULL);
		}
	}
	if (override) {
		sway_seat_set_button_cb(seat, button_callback, button_data);
	} else {
		sway_seat_set_button_cb(seat, NULL, NULL);
	}
}

static char *generate_label(uint32_t i, const char *keys, uint32_t nkeys) {
	int ksize = strlen(keys);
	char *label = malloc((nkeys + 1) * sizeof(char));
	for (uint32_t n = 0, div = i; n < nkeys; ++n) {
		uint32_t rem = div % ksize;
		label[nkeys - 1 -n] = keys[rem];
		div = div / ksize;
	}
	label[nkeys] = 0x0;
	return label;
}

static void jump_handle_keyboard_key_end(void *data, bool focus) {
	struct jump_data *jump_data = data;
	struct jump_specific_data *specific = jump_data->specific;
	struct jump_common_data *common = jump_data->common;
	// Finished, remove decorations and prepare focus
	struct sway_container *focused = NULL;
	for (int w = 0; w < specific->workspaces->length; ++w) {
		struct sway_workspace *workspace = specific->workspaces->items[w];
		if (layout_overview_mode(workspace) == OVERVIEW_TILING) {
			for (int i = 0; i < workspace->tiling->length; ++i) {
				struct sway_container *container = workspace->tiling->items[i];
				for (int j = 0; j < container->pending.children->length; ++j) {
					struct sway_container *view = container->pending.children->items[j];
					container_toggle_jump_decoration(workspace, view, NULL, 0, 0);
					if (focus) {
						if (view->jump.id == (int32_t)common->window_number) {
							focused = view;
						}
					}
					view->jump.id = -1;
				}
			}
		} else {
			for (int i = 0; i < workspace->floating->length; ++i) {
				struct sway_container *view = workspace->floating->items[i];
				container_toggle_jump_decoration(workspace, view, NULL, 0, 0);
				if (focus) {
					if (view->jump.id == (int32_t)common->window_number) {
						focused = view;
					}
				}
				view->jump.id = -1;
			}
		}
	}

	if (focused) {
		struct sway_seat *seat = input_manager_current_seat();
		seat_set_focus_container(seat, focused);
		if (layout_overview_mode(focused->pending.workspace) != OVERVIEW_TILING) {
			sway_scene_node_raise_to_top(&focused->scene_tree->node);
		}
		seat_consider_warp_to_focus(seat);
	}

	for (int w = 0; w < specific->workspaces->length; ++w) {
		struct sway_workspace *workspace = specific->workspaces->items[w];
		if (layout_overview_mode(workspace) != OVERVIEW_TILING) {
			// Restore floating windows positions
			// When calling overview, the positions saved will be the ones
			// already "organized", which are not the initial ones, restore them.
			for (int i = 0; i < workspace->floating->length; ++i) {
				struct sway_container *view = workspace->floating->items[i];
				view->overview.x = view->jump.x;
				view->overview.y = view->jump.y;
				node_set_dirty(&view->node);
			}
		}
		// Restore original view
		layout_overview_toggle(workspace, OVERVIEW_DISABLED);
	}
	root->jumping = false;
	list_free(specific->workspaces);
	free(specific);
	free(common);
	free(jump_data);
	animation_set_type(ANIMATION_JUMP);
	transaction_commit_dirty();
	override_input(false, NULL, NULL, NULL, NULL);
}

static void jump_handle_keyboard_key(struct sway_keyboard *keyboard,
		struct wlr_keyboard_key_event *event, void *data) {
	struct jump_data *jump_data = data;
	struct jump_common_data *common = jump_data->common;

	uint32_t keycode = event->keycode + 8; // Because to xkbcommon it's +8 from libinput
	const xkb_keysym_t keysym = xkb_state_key_get_one_sym(keyboard->wlr->xkb_state, keycode);

	if (event->state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		return;
	}
	// Check if key is valid, otherwise exit
	bool valid = false;
	for (uint32_t i = 0; i < strlen(config->jump_labels_keys); ++i) {
		char keyname[2] = { config->jump_labels_keys[i], 0x0 };
		xkb_keysym_t key = xkb_keysym_from_name(keyname, XKB_KEYSYM_NO_FLAGS);
		if (key && key == keysym) {
			common->window_number = common->window_number * strlen(config->jump_labels_keys) + i;
			valid = true;
			break;
		}
	}
	bool focus = false;
	if (valid) {
		common->keys_pressed++;
		if (common->keys_pressed == common->nkeys) {
			if (common->window_number < common->nwindows) {
				focus = true;
			}
		} else {
			return;
		}
	}
	common->keyboard_key_end(jump_data, focus);
}

static bool jump_handle_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wl_pointer_button_state state, struct sway_node *node, void *data) {
	struct jump_data *jump_data = data;
	struct jump_common_data *common = jump_data->common;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED && button == BTN_LEFT) {
		struct sway_container *con = seat_get_focused_container(seat);
		sway_scene_node_raise_to_top(&con->scene_tree->node);
		common->keyboard_key_end(data, false);
		return true;
	}
	return false;
}

void layout_jump() {
	if (root->jumping) {
		return;
	}
	struct jump_data *jump_data = calloc(1, sizeof(struct jump_data));
	struct jump_specific_data *specific = calloc(1, sizeof(struct jump_specific_data));
	struct jump_common_data *common = calloc(1, sizeof(struct jump_common_data));
	jump_data->specific = specific;
	jump_data->common = common;
	specific->workspaces = create_list();

	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		struct sway_workspace *workspace = output->current.active_workspace;
		if (workspace->tiling->length > 0) {
			list_add(specific->workspaces, workspace);
		}
	}
	if (specific->workspaces->length == 0) {
		list_free(specific->workspaces);
		free(specific);
		free(common);
		free(jump_data);
		return;
	}

	uint32_t nwindows = 0;
	for (int i = 0; i < specific->workspaces->length; ++i) {
		struct sway_workspace *workspace = specific->workspaces->items[i];
		enum sway_layout_overview mode = layout_overview_mode(workspace);
		if (mode != OVERVIEW_TILING) {
			if (mode != OVERVIEW_DISABLED) {
				layout_overview_toggle(workspace, OVERVIEW_DISABLED);
			}
			layout_overview_toggle(workspace, OVERVIEW_TILING);
		}
		for (int i = 0; i < workspace->tiling->length; ++i) {
			struct sway_container *container = workspace->tiling->items[i];
			nwindows += container->pending.children->length;
		}
	}
	animation_set_type(ANIMATION_JUMP);
	uint32_t nkeys = nwindows == 1 ? 1 : ceil(log10(nwindows) / log10(strlen(config->jump_labels_keys)));
	common->nwindows = nwindows;
	common->nkeys = nkeys;
	common->keyboard_key_end = jump_handle_keyboard_key_end;

	for (int i = 0, n = 0; i < specific->workspaces->length; ++i) {
		struct sway_workspace *workspace = specific->workspaces->items[i];
		for (int i = 0; i < workspace->tiling->length; ++i) {
			struct sway_container *container = workspace->tiling->items[i];
			for (int j = 0; j < container->pending.children->length; ++j) {
				struct sway_container *view = container->pending.children->items[j];
				view->jump.id = n;
				char *label = generate_label(n++, config->jump_labels_keys, nkeys);
				container_toggle_jump_decoration(workspace, view, label, view->pending.width, view->pending.height);
				free(label);
			}
		}
	}
	root->jumping = true;
	override_input(true, jump_handle_keyboard_key, jump_data, jump_handle_button, jump_data);
}

static struct sway_container *find_minx(list_t *children, list_t *done) {
	struct sway_container *container = NULL;
	double minx = DBL_MAX;
	double miny = DBL_MAX;
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *con = children->items[i];
		if (list_find(done, con) < 0) {
			if (con->pending.x < minx) {
				minx = con->pending.x;
				miny = con->pending.y;
				container = con;
			} else if (con->pending.x == minx && con->pending.y < miny) {
				minx = con->pending.x;
				miny = con->pending.y;
				container = con;
			}
		}
	}
	return container;
}
static bool containers_overlap(struct sway_container *con1, struct sway_container *con2) {
	if (con1->pending.x >= con2->pending.x + con2->pending.width ||
		con1->pending.x + con1->pending.width <= con2->pending.x ||
		con1->pending.y >= con2->pending.y + con2->pending.height ||
		con1->pending.y + con1->pending.height <= con2->pending.y) {
		return false;
	}
	return true;
}

static void prepare_overlapping_list(list_t *children, struct sway_container *container, list_t *overlap) {
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *con = children->items[i];
		if (con == container || !containers_overlap(con, container)) {
			continue;
		}
		list_add(overlap, con);
	}
}

static void move_overlapping(list_t *children, struct sway_container *container) {
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *con = children->items[i];
		if (con == container || !containers_overlap(con, container)) {
			continue;
		}
		list_t *overlap = create_list();
		prepare_overlapping_list(children, con, /*done, */overlap);
		double dx = container->pending.x <= con->pending.x ?
			(container->pending.x + container->pending.width) - con->pending.x :
			container->pending.x - (con->pending.x + con->pending.width);
		double dy = container->pending.y <= con->pending.y ?
			(container->pending.y + container->pending.height) - con->pending.y :
			container->pending.y - (con->pending.y + con->pending.height);
		if (fabs(dx) < fabs(dy)) {
			con->pending.x += dx;
			for (int j = 0; j < overlap->length; ++j) {
				struct sway_container *over = overlap->items[j];
				if (over != container) {
					over->pending.x += dx;
				}
			}
		} else {
			con->pending.y += dy;
			for (int j = 0; j < overlap->length; ++j) {
				struct sway_container *over = overlap->items[j];
				if (over != container) {
					over->pending.y += dy;
				}
			}
		}
		list_free(overlap);
	}
}

static void organize_floating_windows(struct sway_workspace *workspace) {
	list_t *children = workspace->floating;
	// Disable any full screen window
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *con = children->items[i];
		if (con == workspace->fullscreen) {
			container_fullscreen_disable(con);
			arrange_root();
		}
	}
	// Save positions
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *con = children->items[i];
		con->jump.x = con->pending.x;
		con->jump.y = con->pending.y;
	}
	// Find minx container with lowest y (if more than one)
	list_t *done = create_list();
	while (true) {
		struct sway_container *container = find_minx(children, done);
		if (container) {
			list_add(done, container);
			move_overlapping(children, container);
		} else {
			break;
		}
	}
	list_free(done);
	// Move bounding box to the workspace output's center
	double minx, maxx, miny, maxy;
	layout_compute_bounding_box(children, &minx, &maxx, &miny, &maxy);
	const double offsetx = workspace->output->lx + 0.5 * (workspace->output->width - (minx + maxx));
	const double offsety = workspace->output->ly + 0.5 * (workspace->output->height - (miny + maxy));
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *con = children->items[i];
		con->pending.x += offsetx;
		con->pending.y += offsety;
		arrange_container(con);
		node_set_dirty(&con->node);
	}
}

void layout_jump_floating() {
	if (root->jumping) {
		return;
	}
	struct jump_data *jump_data = calloc(1, sizeof(struct jump_data));
	struct jump_specific_data *specific = calloc(1, sizeof(struct jump_specific_data));
	struct jump_common_data *common = calloc(1, sizeof(struct jump_common_data));
	jump_data->specific = specific;
	jump_data->common = common;
	specific->workspaces = create_list();

	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		struct sway_workspace *workspace = output->current.active_workspace;
		if (workspace->floating->length > 0) {
			list_add(specific->workspaces, workspace);
		}
	}
	if (specific->workspaces->length == 0) {
		list_free(specific->workspaces);
		free(specific);
		free(common);
		free(jump_data);
		return;
	}

	uint32_t nwindows = 0;
	for (int i = 0; i < specific->workspaces->length; ++i) {
		struct sway_workspace *workspace = specific->workspaces->items[i];
		// Move windows so they don't overlap and scale the workspace to fit
		organize_floating_windows(workspace);
		enum sway_layout_overview mode = layout_overview_mode(workspace);
		if (mode != OVERVIEW_FLOATING) {
			if (mode != OVERVIEW_DISABLED) {
				layout_overview_toggle(workspace, OVERVIEW_DISABLED);
			}
			layout_overview_toggle(workspace, OVERVIEW_FLOATING);
		}
		nwindows += workspace->floating->length;
	}
	animation_set_type(ANIMATION_JUMP);
	uint32_t nkeys = nwindows == 1 ? 1 : ceil(log10(nwindows) / log10(strlen(config->jump_labels_keys)));
	common->nwindows = nwindows;
	common->nkeys = nkeys;
	common->keyboard_key_end = jump_handle_keyboard_key_end;

	for (int i = 0, n = 0; i < specific->workspaces->length; ++i) {
		struct sway_workspace *workspace = specific->workspaces->items[i];
		for (int i = 0; i < workspace->floating->length; ++i) {
			struct sway_container *view = workspace->floating->items[i];
			view->jump.id = n;
			char *label = generate_label(n++, config->jump_labels_keys, nkeys);
			container_toggle_jump_decoration(workspace, view, label, view->pending.width, view->pending.height);
			free(label);
		}
	}
	root->jumping = true;
	override_input(true, jump_handle_keyboard_key, jump_data, jump_handle_button, jump_data);
}

struct jump_scratchpad_common_data {
	struct sway_container *focused;
	uint32_t keys_pressed;
	uint32_t nkeys;
	uint32_t window_number;
	uint32_t nwindows;
	void (*keyboard_key_end)(void *data, bool focus);
};

struct jump_scratchpad_specific_data {
	struct sway_workspace *workspace;
	list_t *floating;
	list_t *workspaces;
};

static void jump_scratchpad_handle_keyboard_key_end(void *data, bool focus) {
	struct jump_data *jump_data = data;
	struct jump_scratchpad_common_data *common = jump_data->common;
	struct jump_scratchpad_specific_data *specific = jump_data->specific;
	// Finished, remove decorations
	struct sway_workspace *workspace = specific->workspace;
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *view = workspace->floating->items[i];
		container_toggle_jump_decoration(workspace, view, NULL, 0, 0);
	}

	// Restore floating windows positions
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *view = workspace->floating->items[i];
		view->overview.x = view->jump.x;
		view->overview.y = view->jump.y;
		node_set_dirty(&view->node);
		// And restore scratchpad's NULL workspace
		view->pending.workspace = NULL;
	}
	// Restore original view
	layout_overview_toggle(workspace, OVERVIEW_DISABLED);

	// Now restore the original floating windows, and enable the focused
	// scratchpad container
	workspace->floating = specific->floating;
	// When calling jump:
	// 1. If only one window in the scratchpad, focus that one
	// 2. If cancelling jump: Do nothing (keep scratchpad on or off like before)
	// 3. If selecting one, hide the previous one and show the new one

	// Restore the previous state
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *view = workspace->floating->items[i];
		// Restore workspace we deleted earlier
		view->pending.workspace = workspace;
		if (view->scratchpad) {
			if (focus) {
				// If we will change focus, hide any scratchpad windows shown
				root_scratchpad_hide(view);
			}
		} else {
			// Re-enable the non-scratchpad floating windows in the scene graph
			sway_scene_node_reparent(&view->scene_tree->node, root->layers.floating);
		}
	}
	if (focus) {
		root_scratchpad_show(common->focused);
	} else {
		struct sway_seat *seat = input_manager_current_seat();
		struct sway_container *con = seat_get_focused_container(seat);
		if (con && con->scratchpad) {
			root_scratchpad_show(con);
		}
	}
	node_set_dirty(&workspace->node);

	root->jumping = false;
	free(specific);
	free(common);
	free(jump_data);
	animation_set_type(ANIMATION_JUMP);
	transaction_commit_dirty();
	override_input(false, NULL, NULL, NULL, NULL);
}

static void jump_scratchpad_handle_keyboard_key(struct sway_keyboard *keyboard,
		struct wlr_keyboard_key_event *event, void *data) {
	struct jump_data *jump_data = data;
	struct jump_scratchpad_specific_data *specific = jump_data->specific;
	struct jump_scratchpad_common_data *common = jump_data->common;

	uint32_t keycode = event->keycode + 8; // Because to xkbcommon it's +8 from libinput
	const xkb_keysym_t keysym = xkb_state_key_get_one_sym(keyboard->wlr->xkb_state, keycode);

	if (event->state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		return;
	}
	// Check if key is valid, otherwise exit
	bool valid = false;
	for (uint32_t i = 0; i < strlen(config->jump_labels_keys); ++i) {
		char keyname[2] = { config->jump_labels_keys[i], 0x0 };
		xkb_keysym_t key = xkb_keysym_from_name(keyname, XKB_KEYSYM_NO_FLAGS);
		if (key && key == keysym) {
			common->window_number = common->window_number * strlen(config->jump_labels_keys) + i;
			valid = true;
			break;
		}
	}
	bool focus = false;
	if (valid) {
		common->keys_pressed++;
		if (common->keys_pressed == common->nkeys) {
			if (common->window_number < common->nwindows) {
				for (int i = 0; i < specific->workspace->floating->length; ++i) {
					struct sway_container *con = specific->workspace->floating->items[i];
					if (con->jump.id == (int32_t)common->window_number) {
						focus = true;
						common->focused = con;
					}
					con->jump.id = -1;
				}
			}
		} else {
			return;
		}
	}
	common->keyboard_key_end(jump_data, focus);
}

static bool jump_scratchpad_handle_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wl_pointer_button_state state, struct sway_node *node, void *data) {
	struct jump_data *jump_data = data;
	struct jump_scratchpad_common_data *common = jump_data->common;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED && button == BTN_LEFT) {
		struct sway_container *con = seat_get_focused_container(seat);
		if (con) {
			common->focused = con;
			common->keyboard_key_end(data, true);
		} else {
			common->keyboard_key_end(data, false);
		}
		return true;
	}
	return false;
}

void layout_jump_scratchpad(struct sway_workspace *workspace) {
	if (root->scratchpad->length == 0 || root->jumping) {
		return;
	}
	struct jump_data *jump_data = calloc(1, sizeof(struct jump_data));
	struct jump_scratchpad_specific_data *specific = calloc(1, sizeof(struct jump_scratchpad_specific_data));
	struct jump_scratchpad_common_data *common = calloc(1, sizeof(struct jump_scratchpad_common_data));
	jump_data->specific = specific;
	jump_data->common = common;

	specific->workspace = workspace;
	specific->floating = workspace->floating;
	// Disable all the floating windows in the workspace
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *view = workspace->floating->items[i];
		view->pending.workspace = NULL;
		if (!view->scratchpad) {
			sway_scene_node_reparent(&view->scene_tree->node, root->staging);
		}
	}

	// Make the scratchpad look like the workspace's floating windows
	workspace->floating = root->scratchpad;
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *view = root->scratchpad->items[i];
		view->pending.workspace = workspace;
	}

	// Move windows so they don't overlap and scale the workspace to fit
	organize_floating_windows(workspace);
	enum sway_layout_overview mode = layout_overview_mode(workspace);
	if (mode != OVERVIEW_FLOATING) {
		if (mode != OVERVIEW_DISABLED) {
			layout_overview_toggle(workspace, OVERVIEW_DISABLED);
		}
		layout_overview_toggle(workspace, OVERVIEW_FLOATING);
	}

	animation_set_type(ANIMATION_JUMP);
	uint32_t nwindows = workspace->floating->length;
	uint32_t nkeys = nwindows == 1 ? 1 : ceil(log10(nwindows) / log10(strlen(config->jump_labels_keys)));
	common->nwindows = nwindows;
	common->nkeys = nkeys;
	common->keyboard_key_end = jump_scratchpad_handle_keyboard_key_end;

	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *view = workspace->floating->items[i];
		view->jump.id = i;
		char *label = generate_label(i, config->jump_labels_keys, nkeys);
		container_toggle_jump_decoration(workspace, view, label, view->pending.width, view->pending.height);
		free(label);
	}
	root->jumping = true;
	override_input(true, jump_scratchpad_handle_keyboard_key, jump_data, jump_scratchpad_handle_button, jump_data);
}

static void jump_trailmark_handle_keyboard_key_end(void *data, bool focus) {
	struct jump_data *jump_data = data;
	struct jump_scratchpad_specific_data *specific = jump_data->specific;
	struct jump_scratchpad_common_data *common = jump_data->common;
	// Finished, remove decorations
	struct sway_workspace *workspace = specific->workspace;
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *view = workspace->floating->items[i];
		container_toggle_jump_decoration(workspace, view, NULL, 0, 0);
	}

	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *view = workspace->floating->items[i];
		view->overview.x = view->jump.x;
		view->overview.y = view->jump.y;
		node_set_dirty(&view->node);
		view->pending.workspace = specific->workspaces->items[i];
		if (view->pending.parent) {
			view->pending.parent->pending.workspace = view->pending.workspace;
		}
		node_set_dirty(&view->pending.workspace->node);
	}
	// Restore original trailmarked views
	layout_overview_toggle(workspace, OVERVIEW_DISABLED);

	list_free(specific->workspaces);
	list_free(workspace->floating);
	workspace->floating = specific->floating;

	// Restore the previous state of the current workspace
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *view = workspace->floating->items[i];
		// Restore workspace we deleted earlier
		view->pending.workspace = workspace;
		if (!layout_trails_trailmarked(view->view)) {
			// Re-enable the non-trailmarked floating windows in the scene graph
			sway_scene_node_reparent(&view->scene_tree->node, root->layers.floating);
		}
	}
	if (focus) {
		struct sway_seat *seat = input_manager_current_seat();
		struct sway_container * con = seat_get_focused_container(seat);
		if (con && common->focused->pending.workspace->fullscreen == con) {
			container_fullscreen_disable(common->focused->pending.workspace->fullscreen);
			arrange_root();
		}
		seat_set_focus_workspace(seat, common->focused->pending.workspace);
		seat_set_focus_container(seat, common->focused);
		sway_scene_node_raise_to_top(&common->focused->scene_tree->node);
		seat_consider_warp_to_focus(seat);
	}
	node_set_dirty(&workspace->node);

	root->jumping = false;
	free(specific);
	free(common);
	free(jump_data);
	root_set_default_filters(root);
	animation_set_type(ANIMATION_JUMP);
	transaction_commit_dirty();
	override_input(false, NULL, NULL, NULL, NULL);
}

static bool trailmarks_workspace_filter(struct sway_workspace *workspace, void *filter_data) {
	return true;
}

static bool trailmarks_container_filter(struct sway_workspace *workspace,
		struct sway_container *container, void *filter_data) {
	struct jump_data *data = filter_data;
	struct jump_scratchpad_specific_data *specific = data->specific;
	if (workspace != specific->workspace) {
		return false;
	}
	struct sway_trail *trail = trails->trails->items[trails->active];
	for (int i = 0; i < trail->marks->length; ++i) {
		struct sway_view *view = trail->marks->items[i];
		struct sway_container *con = view->container;
		if (container == con || container == con->pending.parent) {
			return true;
		}
	}
	return false;
}

void layout_jump_trailmark(struct sway_workspace *workspace) {
	if (!workspace || root->jumping) {
		return;
	}
	// Similarly to jump scratchpad, disable any floating windows in the current
	// workspace, make all the windows in the trail float in the current workspace
	// and toggle jump floating ONLY for the current output.
	if (layout_trails_length() == 0) {
		return;
	}
	if (layout_trails_active_length() <= 1) {
		return;
	}

	struct jump_data *jump_data = calloc(1, sizeof(struct jump_data));
	struct jump_scratchpad_specific_data *specific = calloc(1, sizeof(struct jump_scratchpad_specific_data));
	struct jump_scratchpad_common_data *common = calloc(1, sizeof(struct jump_scratchpad_common_data));
	jump_data->specific = specific;
	jump_data->common = common;

	specific->workspace = workspace;
	specific->floating = workspace->floating;
	// Disable all the containers in the workspace
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *view = workspace->floating->items[i];
		if (!layout_trails_trailmarked(view->view)) {
			view->pending.workspace = NULL;
			sway_scene_node_reparent(&view->scene_tree->node, root->staging);
		}
	}

	// Make the active trail look like the workspace's floating windows
	workspace->floating = create_list();
	specific->workspaces = create_list();
	struct sway_trail *trail = trails->trails->items[layout_trails_active()];
	for (int i = 0; i < trail->marks->length; ++i) {
		struct sway_view *view = trail->marks->items[i];
		struct sway_container *con = view->container;
		list_add(workspace->floating, con);
		list_add(specific->workspaces, con->pending.workspace);
		con->pending.workspace = workspace;
		con->current.workspace = workspace;
		if (con->pending.parent) {
			con->pending.parent->pending.workspace = workspace;
			con->pending.parent->current.workspace = workspace;
		}
	}
	// This makes sure the workspace gets initialized if it is not (empty)
	arrange_workspace(workspace);

	// Add root filters
	root->filters.workspace_filter = trailmarks_workspace_filter;
	root->filters.workspace_filter_data = NULL;
	root->filters.container_filter = trailmarks_container_filter;
	root->filters.container_filter_data = jump_data;

	// Move windows so they don't overlap and scale the workspace to fit
	organize_floating_windows(workspace);
	enum sway_layout_overview mode = layout_overview_mode(workspace);
	if (mode != OVERVIEW_FLOATING) {
		if (mode != OVERVIEW_DISABLED) {
			layout_overview_toggle(workspace, OVERVIEW_DISABLED);
		}
		layout_overview_toggle(workspace, OVERVIEW_FLOATING);
	}

	animation_set_type(ANIMATION_JUMP);
	uint32_t nwindows = workspace->floating->length;
	uint32_t nkeys = nwindows == 1 ? 1 : ceil(log10(nwindows) / log10(strlen(config->jump_labels_keys)));
	common->nwindows = nwindows;
	common->nkeys = nkeys;
	common->keyboard_key_end = jump_trailmark_handle_keyboard_key_end;

	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *view = workspace->floating->items[i];
		view->jump.id = i;
		char *label = generate_label(i, config->jump_labels_keys, nkeys);
		container_toggle_jump_decoration(workspace, view, label, view->pending.width, view->pending.height);
		free(label);
	}
	root->jumping = true;
	override_input(true, jump_scratchpad_handle_keyboard_key, jump_data, jump_scratchpad_handle_button, jump_data);
}

static void workspace_toggle_jump_decoration(struct sway_workspace *ws, char *text) {
	if (!text) {
		if (ws->jump.text) {
			sway_scene_node_destroy(ws->jump.text->node);
			ws->jump.text = NULL;
		}
		return;
	} else if (!ws->jump.text) {
		ws->jump.text = sway_text_node_create(ws->jump.tree,
			text, config->jump_labels_color, false);
	} else {
		sway_text_node_set_text(ws->jump.text, text);
	}
	sway_text_node_set_background(ws->jump.text, config->jump_labels_background);
	double jscale = config->jump_labels_scale;
	double scale = fmin((double) ws->width / ws->jump.text->width,
		(double) ws->height / ws->jump.text->height);
	const double oscale = ws->output->wlr_output->scale;
	const double wscale = ws->jump.scale;
	sway_text_node_scale(ws->jump.text, jscale * scale * wscale);
	int x = ws->jump.x + 0.5 * (ws->jump.width - ws->jump.text->width * jscale * scale * wscale * oscale);
	int y = ws->jump.y + 0.5 * (ws->jump.height - ws->jump.text->height * jscale * scale * wscale * oscale);
	x /= oscale;
	y /= oscale;
	sway_scene_node_set_position(&ws->jump.tree->node, x, y);
	sway_scene_node_set_enabled(&ws->jump.tree->node, true);
}

static void jump_workspaces_handle_keyboard_key_end(void *data, bool focus) {
	struct jump_data *jump_data = data;
	struct jump_specific_data *specific = jump_data->specific;
	struct jump_common_data *common = jump_data->common;
	layout_overview_workspaces_toggle();

	uint32_t n = 0;
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		for (int j = 0; j < output->current.workspaces->length; ++j) {
			struct sway_workspace *child = output->current.workspaces->items[j];
			if (focus && n == common->window_number) {
				switch_workspace(child);
			}
			++n;
			workspace_toggle_jump_decoration(child, NULL);
		}
	}

	root->jumping = false;
	free(specific);
	free(common);
	free(jump_data);
	transaction_commit_dirty();
	override_input(false, NULL, NULL, NULL, NULL);
}

static bool jump_workspaces_handle_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wl_pointer_button_state state, struct sway_node *node, void *data) {
	if (state == WL_POINTER_BUTTON_STATE_PRESSED && button == BTN_LEFT) {
		struct jump_data *jump_data = data;
		struct jump_common_data *common = jump_data->common;
		bool focus = false;
		uint32_t n = 0;
		for (int i = 0; i < root->outputs->length; ++i) {
			struct sway_output *output = root->outputs->items[i];
			const double oscale = output->wlr_output->scale;
			const double x = oscale * (seat->cursor->cursor->x - output->lx);
			const double y = oscale * (seat->cursor->cursor->y - output->ly);
			for (int j = 0; j < output->current.workspaces->length; ++j) {
				struct sway_workspace *child = output->current.workspaces->items[j];
				// If this workspace is the one under the cursor, set focused
				// and jump_data->window_number to n
				if (x >= child->jump.x &&
					x <= child->jump.x + child->jump.width &&
					y >= child->jump.y &&
					y <= child->jump.y + child->jump.height) {
					focus = true;
					common->window_number = n;
				}
				++n;
			}
		}
		common->keyboard_key_end(data, focus);
		return true;
	}
	return false;
}

void layout_jump_workspaces() {
	if (root->jumping) {
		return;
	}
	layout_overview_workspaces_toggle();

	struct jump_data *jump_data = calloc(1, sizeof(struct jump_data));
	struct jump_specific_data *specific = calloc(1, sizeof(struct jump_specific_data));
	struct jump_common_data *common = calloc(1, sizeof(struct jump_common_data));
	jump_data->specific = specific;
	jump_data->common = common;
	specific->workspaces = NULL;

	uint32_t nworkspaces = 0;
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		nworkspaces += output->current.workspaces->length;
	}
	if (nworkspaces == 0) {
		free(specific);
		free(common);
		free(jump_data);
		layout_overview_workspaces_toggle();
		transaction_commit_dirty();
		return;
	}

	root->jumping = true;
	uint32_t nkeys = nworkspaces == 1 ? 1 : ceil(log10(nworkspaces) / log10(strlen(config->jump_labels_keys)));
	common->nwindows = nworkspaces;
	common->nkeys = nkeys;
	common->keyboard_key_end = jump_workspaces_handle_keyboard_key_end;

	for (int i = 0, n = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		for (int j = 0; j < output->current.workspaces->length; ++j) {
			struct sway_workspace *child = output->current.workspaces->items[j];
			char *label = generate_label(n++, config->jump_labels_keys, nkeys);
			workspace_toggle_jump_decoration(child, label);
			free(label);
		}
	}
	transaction_commit_dirty();

	override_input(true, jump_handle_keyboard_key, jump_data, jump_workspaces_handle_button, jump_data);
}

struct jump_container_specific_data {
	struct sway_container *container;
	struct sway_container *fs;
	bool jumping;
};

static void container_jump(struct jump_data *jump_data) {
	struct jump_container_specific_data *specific = jump_data->specific;
	struct jump_common_data *common = jump_data->common;

	struct sway_container *container = specific->container;
	struct sway_workspace *workspace = container->pending.workspace;
	static enum sway_layout_reorder reorder = REORDER_AUTO;
	if (specific->jumping) {
		container->jump.jumping = false;
		layout_modifiers_set_reorder(workspace, reorder);
		for (int i = 0; i < container->pending.children->length; ++i) {
			struct sway_container *con = container->pending.children->items[i];
			container_toggle_jump_decoration(workspace, con, NULL, 0, 0);
		}
	} else {
		specific->jumping = true;
		container->jump.jumping = true;
		for (int i = 0; i < container->pending.children->length; ++i) {
			struct sway_container *con = container->pending.children->items[i];
			if (con->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
				container_fullscreen_disable(con);
				specific->fs = con;
			}
		}
		if (specific->fs) {
			arrange_workspace(workspace);
		}
		reorder = layout_modifiers_get_reorder(workspace);
		layout_modifiers_set_reorder(workspace, REORDER_LAZY);
		if (layout_get_type(workspace) == L_HORIZ) {
			double y = workspace->y;
			double height = 0.0;
			for (int i = 0; i < container->pending.children->length; ++i) {
				struct sway_container *con = container->pending.children->items[i];
				height += con->pending.height;
			}
			for (int i = 0; i < container->pending.children->length; ++i) {
				struct sway_container *con = container->pending.children->items[i];
				con->pending.y = y;
				sway_scene_node_raise_to_top(&con->scene_tree->node);
				y += con->pending.height / height * workspace->height;
			}
			for (int i = 0; i < container->pending.children->length; ++i) {
				struct sway_container *con = container->pending.children->items[i];
				double cheight = con->pending.height / height * workspace->height;
				char *label = generate_label(i, config->jump_labels_keys, common->nkeys);
				container_toggle_jump_decoration(workspace, con, label, container->pending.width, cheight);
				free(label);
			}
		} else {
			double x = workspace->x;
			double width = 0.0;
			for (int i = 0; i < container->pending.children->length; ++i) {
				struct sway_container *con = container->pending.children->items[i];
				width += con->pending.width;
			}
			for (int i = 0; i < container->pending.children->length; ++i) {
				struct sway_container *con = container->pending.children->items[i];
				con->pending.x = x;
				sway_scene_node_raise_to_top(&con->scene_tree->node);
				x += con->pending.width / width * workspace->width;
			}
			for (int i = 0; i < container->pending.children->length; ++i) {
				struct sway_container *con = container->pending.children->items[i];
				double cwidth = con->pending.width / width * workspace->width;
				char *label = generate_label(i, config->jump_labels_keys, common->nkeys);
				container_toggle_jump_decoration(workspace, con, label, cwidth, container->pending.height);
				free(label);
			}
		}
	}
	node_set_dirty(&container->node);

	animation_set_type(ANIMATION_JUMP);
	transaction_commit_dirty();
}

static void jump_container_handle_keyboard_key_end(void *data, bool focus) {
	struct jump_data *jump_data = data;
	container_jump(jump_data);

	struct jump_container_specific_data *specific = jump_data->specific;
	struct jump_common_data *common = jump_data->common;

	if (focus) {
		struct sway_container *view = specific->container->pending.children->items[common->window_number];
		if (config->fullscreen_movefocus != FULLSCREEN_MOVEFOCUS_NONE) {
			if (config->fullscreen_movefocus == FULLSCREEN_MOVEFOCUS_FOLLOW && specific->fs) {
				container_set_fullscreen(view, FULLSCREEN_WORKSPACE);
				arrange_workspace(view->pending.workspace);
			}
		}
		struct sway_seat *seat = input_manager_current_seat();
		seat_set_focus_container(seat, view);
		seat_consider_warp_to_focus(seat);
	} else {
		struct sway_seat *seat = input_manager_current_seat();
		struct sway_container *view = seat_get_focused_container(seat);
		if (view && view == specific->fs) {
			container_set_fullscreen(view, FULLSCREEN_WORKSPACE);
			arrange_workspace(view->pending.workspace);
		}
	}

	root->jumping = false;
	free(specific);
	free(common);
	free(jump_data);
	animation_set_type(ANIMATION_JUMP);
	transaction_commit_dirty();
	override_input(false, NULL, NULL, NULL, NULL);
}

static bool jump_container_handle_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wl_pointer_button_state state, struct sway_node *node, void *data) {
	if (state == WL_POINTER_BUTTON_STATE_PRESSED && button == BTN_LEFT) {
		jump_container_handle_keyboard_key_end(data, false);
		return true;
	}
	return false;
}

void layout_jump_container(struct sway_container *container) {
	if (root->jumping) {
		return;
	}
	if (container->pending.parent) {
		container = container->pending.parent;
	}
	if (!container->pending.children || container->pending.children->length <= 1) {
		return;
	}
	struct jump_data *jump_data = calloc(1, sizeof(struct jump_data));
	struct jump_container_specific_data *specific = calloc(1, sizeof(struct jump_container_specific_data));
	struct jump_common_data *common = calloc(1, sizeof(struct jump_common_data));
	jump_data->specific = specific;
	jump_data->common = common;
	specific->container = container;

	uint32_t ncontainers = container->pending.children->length;
	uint32_t nkeys = ceil(log10(ncontainers) / log10(strlen(config->jump_labels_keys)));
	common->nwindows = ncontainers;
	common->nkeys = nkeys;
	common->keyboard_key_end = jump_container_handle_keyboard_key_end;
	specific->jumping = false;

	root->jumping = true;
	container_jump(jump_data);

	override_input(true, jump_handle_keyboard_key, jump_data, jump_container_handle_button, jump_data);
}


// When the workspace is scaled, offsets are not valid to check cursor or bounds,
// because offsets and sizes are not scaled. We need to re-compute them using the
// active offset as the only ground truth, with everything else relative to it.

static struct sway_container *get_mouse_container(struct sway_seat *seat) {
	struct sway_workspace *workspace = seat->workspace;
	double scale = layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0;

	// If there is a pinned container, check it before anything else
	struct sway_container *pin = workspace->gesture.pin;
	if (workspace->gesture.pin) {
		if (seat->cursor->cursor->x >= pin->pending.x - scale * workspace->gaps_inner &&
			seat->cursor->cursor->x < pin->pending.x + scale * (pin->pending.width + workspace->gaps_inner)) {
			return pin;
		}
	}

	struct sway_container *container = workspace->current.focused_inactive_child;
	enum sway_container_layout layout = layout_get_type(workspace);
	int active_idx = list_find(workspace->tiling, container);
	if (layout == L_HORIZ) {
		double offset = container->pending.x;
		for (int i = active_idx; i < workspace->tiling->length; ++i) {
			struct sway_container *con = workspace->tiling->items[i];
			double x0 = offset - scale * workspace->gaps_inner;
			double x1 = x0 + scale * (con->pending.width + 2.0 * workspace->gaps_inner);
			if (seat->cursor->cursor->x >= x0 && seat->cursor->cursor->x < x1) {
				return con;
			}
			offset = x1;
		}
		offset = container->pending.x;
		for (int i = active_idx - 1; i >= 0; i--) {
			struct sway_container *con = workspace->tiling->items[i];
			double x1 = offset - scale * workspace->gaps_inner;
			double x0 = x1 - scale * (con->pending.width + 2.0 * workspace->gaps_inner);
			if (seat->cursor->cursor->x >= x0 && seat->cursor->cursor->x < x1) {
				return con;
			}
			offset = x0;
		}
	} else {
		double offset = container->pending.y;
		for (int i = active_idx; i < workspace->tiling->length; ++i) {
			struct sway_container *con = workspace->tiling->items[i];
			double y0 = offset - scale * workspace->gaps_inner;
			double y1 = y0 + scale * (con->pending.height + 2.0 * workspace->gaps_inner);
			if (seat->cursor->cursor->y >= y0 && seat->cursor->cursor->y < y1) {
				return con;
			}
			offset = y1;
		}
		offset = container->pending.y;
		for (int i = active_idx - 1; i >= 0; i--) {
			struct sway_container *con = workspace->tiling->items[i];
			double y1 = offset - scale * workspace->gaps_inner;
			double y0 = y1 - scale * (con->pending.height + 2.0 * workspace->gaps_inner);
			if (seat->cursor->cursor->y >= y0 && seat->cursor->cursor->y < y1) {
				return con;
			}
			offset = y0;
		}
	}
	return container;
}

static void scroll_workspace(struct sway_workspace *workspace, double dx, double dy) {
	if (layout_get_type(workspace) == L_HORIZ) {
		for (int i = 0; i < workspace->tiling->length; ++i) {
			struct sway_container *con = workspace->tiling->items[i];
			con->pending.x += dx;
		}
	} else {
		for (int i = 0; i < workspace->tiling->length; ++i) {
			struct sway_container *con = workspace->tiling->items[i];
			con->pending.y += dy;
		}
	}
	node_set_dirty(&workspace->node);
	transaction_commit_dirty();
}

static void scroll_container(struct sway_container *container, double dx, double dy) {
	if (container->pending.layout == L_HORIZ) {
		list_t *children = container->pending.children;
		for (int i = 0; i < children->length; ++i) {
			struct sway_container *con = children->items[i];
			con->current.x += dx;
			con->pending.x += dx;
		}
	} else {
		list_t *children = container->pending.children;
		for (int i = 0; i < children->length; ++i) {
			struct sway_container *con = children->items[i];
			con->current.y += dy;
			con->pending.y += dy;
		}
	}
	node_set_dirty(&container->node);
	transaction_commit_dirty();
}

static void layout_scroll_float_pinned_container(struct sway_workspace *workspace) {
	struct sway_container *pin = workspace->gesture.pin;
	int pidx = list_find(workspace->tiling, pin);
	if (pidx < 0) {
		return;
	}
	// Move windows that are outside of the viewport on the pin side to be
	// adjacent to the rest
	enum sway_layout_pin pos = workspace->gesture.pin_position;
	double scale = layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0;
	enum sway_container_layout layout = layout_get_type(workspace);
	if (layout == L_HORIZ) {
		const double move = scale * (2.0 * workspace->gaps_inner + pin->pending.width);
		if (pos == PIN_BEGINNING) {
			for (int i = 0; i < pidx; i++) {
				struct sway_container *con = workspace->tiling->items[i];
				con->pending.x += move;
			}
		} else {
			for (int i = pidx + 1; i < workspace->tiling->length; i++) {
				struct sway_container *con = workspace->tiling->items[i];
				con->pending.x -= move;
			}
		}
	} else {
		const double move = scale * (2.0 * workspace->gaps_inner + pin->pending.height);
		if (pos == PIN_BEGINNING) {
			for (int i = 0; i < pidx; i++) {
				struct sway_container *con = workspace->tiling->items[i];
				con->pending.y += move;
			}
		} else {
			for (int i = pidx + 1; i < workspace->tiling->length; i++) {
				struct sway_container *con = workspace->tiling->items[i];
				con->pending.y -= move;
			}
		}
	}
	// Float the pin
	list_del(workspace->tiling, pidx);
	list_add(workspace->floating, pin);
	if (layout_scale_enabled(workspace)) {
		layout_view_scale_set(pin, layout_scale_get(workspace));
	} else {
		layout_view_scale_reset(pin);
	}
	node_set_dirty(&workspace->node);
	//node_set_dirty(&pin->node);
	transaction_commit_dirty();
}

static void layout_scroll_unfloat_pinned_container(struct sway_workspace *workspace) {
	struct sway_container *pin = workspace->gesture.pin;
	int fidx = list_find(workspace->floating, pin);
	if (fidx < 0) {
		return;
	}
	enum sway_layout_pin pos = workspace->gesture.pin_position;
	enum sway_container_layout layout = layout_get_type(workspace);
	struct sway_seat *seat = input_manager_current_seat();
	double scale = layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0;
	// There will be one container that overlaps the inner edge of the pin.
	// That container's center point will give the future location of the
	// container with respect to the pin.
	bool inserted = false;
	if (layout == L_HORIZ) {
		const double px0 = pin->pending.x;
		const double px1 = pin->pending.x + scale * pin->pending.width;
		const double p = pos == PIN_BEGINNING ? px1 : px0;
		for (int i = 0; i < workspace->tiling->length; ++i) {
			struct sway_container *con = workspace->tiling->items[i];
			const double cx1 = con->pending.x + scale * con->pending.width;
			const double cm = con->pending.x + 0.5 * scale * con->pending.width;
			if (cx1 > p) {
				if (cm > p) {
					list_insert(workspace->tiling, i, pin);
					seat_set_focus_container(seat, pos == PIN_BEGINNING ? con : pin);
				} else {
					list_insert(workspace->tiling, i + 1, pin);
					seat_set_focus_container(seat, pos == PIN_BEGINNING ? pin : con);
				}
				inserted = true;
				break;
			}
		}
	} else {
		const double py0 = pin->pending.y;
		const double py1 = pin->pending.y + pin->pending.height;
		const double p = pos == PIN_BEGINNING ? py1 : py0;
		for (int i = 0; i < workspace->tiling->length; ++i) {
			struct sway_container *con = workspace->tiling->items[i];
			const double cy1 = con->pending.y + con->pending.height;
			const double cm = con->pending.y + 0.5 * con->pending.height;
			if (cy1 > p) {
				if (cm > p) {
					list_insert(workspace->tiling, i, pin);
					seat_set_focus_container(seat, pos == PIN_BEGINNING ? con : pin);
				} else {
					list_insert(workspace->tiling, i + 1, pin);
					seat_set_focus_container(seat, pos == PIN_BEGINNING ? pin : con);
				}
				inserted = true;
				break;
			}
		}
	}
	if (!inserted) {
		list_add(workspace->tiling, pin);
		seat_set_focus_container(seat, pin);
	}
	list_del(workspace->floating, fidx);
	layout_view_scale_reset(pin);
	layout_pin_set(workspace, pin, pos);
	node_set_dirty(&workspace->node);
	node_set_dirty(&pin->node);
	transaction_commit_dirty();
	workspace->gesture.scrolling = false;
}

// Gestures
bool layout_scroll_begin(struct sway_seat *seat) {
	struct sway_workspace *workspace = seat->workspace;
	// Check if we can scroll
	double scale = layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0;
	double total_width = 0.0, max_height = 0.0;
	const int gap = workspace->gaps_inner;
	for (int i = 0; i < workspace->tiling->length; ++i) {
		struct sway_container *con = workspace->tiling->items[i];
		total_width += scale * (con->pending.width + 2.0 * gap);
		double total_height = 0.0;
		for (int j = 0; j < con->pending.children->length; ++j) {
			struct sway_container *child = con->pending.children->items[j];
			total_height += scale * (child->pending.height + 2.0 * gap);
		}
		if (total_height > max_height) {
			max_height = total_height;
		}
	}
	bool can_scroll_x = total_width > workspace->width;
	bool can_scroll_y = max_height > workspace->height;
	if (!can_scroll_x && !can_scroll_y) {
		return false;
	}

	workspace->gesture.scrolling = true;
	workspace->gesture.dx = 0.0;
	workspace->gesture.dy = 0.0;

	// If there is a pinned container, float it.
	struct sway_container *pin = layout_pin_enabled(workspace) ? layout_pin_get_container(workspace) : NULL;
	workspace->gesture.pin = pin;
	enum sway_container_layout layout = layout_get_type(workspace);
	if (pin &&
		((layout == L_HORIZ && can_scroll_x) ||
		 (layout == L_VERT && can_scroll_y))) {
		workspace->gesture.pin_position = layout_pin_get_position(workspace);
		layout_pin_remove(workspace, pin);
		layout_scroll_float_pinned_container(workspace);
	}
	return true;
}

void layout_scroll_update(struct sway_seat *seat, double dx, double dy, float sensitivity) {
	struct sway_workspace *workspace = seat->workspace;
	if (workspace->tiling->length == 0) {
		return;
	}
	double scale = layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0;
	dx *= sensitivity * scale;
	dy *= sensitivity * scale;

	enum sway_layout_direction scrolling_direction;
	if (fabs(dx) > fabs(dy)) {
		scrolling_direction = dx > 0.0 ? DIR_RIGHT : DIR_LEFT;
		workspace->gesture.dx += dx;
	} else {
		scrolling_direction = dy > 0.0 ? DIR_DOWN : DIR_UP; 
		workspace->gesture.dy += dy;
	}

	switch (scrolling_direction) {
	case DIR_UP:
	case DIR_DOWN:
		if (layout_get_type(workspace) == L_HORIZ) {
			scroll_container(get_mouse_container(seat), dx, dy);
		} else {
			scroll_workspace(workspace, dx, dy);
		}
		break;
	case DIR_LEFT:
	case DIR_RIGHT:
		if (layout_get_type(workspace) == L_HORIZ) {
			scroll_workspace(workspace, dx, dy);
		} else {
			scroll_container(get_mouse_container(seat), dx, dy);
		}
		break;
	default:
		break;
	}
}

static void scroll_end_horizontal(struct sway_seat *seat, list_t *children, int active_idx,
		enum sway_layout_direction scrolling_direction) {
	struct sway_container *active = children->items[active_idx];
	struct sway_workspace *workspace = active->pending.workspace;
	double scale = layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0;
	if (scrolling_direction == DIR_LEFT) {
		struct sway_container *new_active = children->items[children->length - 1];
		double offset = active->pending.x + scale * (active->pending.width + 2.0 * workspace->gaps_inner);
		// Take the first after active that has its left edge in the viewport
		for (int i = active_idx + 1; i < children->length; ++i) {
			struct sway_container *con = children->items[i];
			double x0 = offset;
			if (x0 > 0 && x0 < workspace->width) {
				new_active = con;
				break;
			}
			offset += scale * (con->pending.width + 2.0 * workspace->gaps_inner);
		}
		if (new_active->pending.children) {
			new_active = new_active->current.focused_inactive_child;
		}
		seat_set_focus_container(seat, new_active);
    } else if (scrolling_direction == DIR_RIGHT) {
		struct sway_container *new_active = children->items[0];
		double offset = active->pending.x - 2.0 * scale * workspace->gaps_inner;
		// Take the first before active that has its right edge in the viewport
		for (int i = active_idx - 1; i >= 0; i--) {
			struct sway_container *con = children->items[i];
			double x1 = offset;
			if (x1 > 0 && x1 < workspace->width) {
				new_active = con;
				break;
			}
			offset -= scale * (con->pending.width + 2.0 * workspace->gaps_inner);
		}
		if (new_active->pending.children) {
			new_active = new_active->current.focused_inactive_child;
		}
		seat_set_focus_container(seat, new_active);
	}
}

static void scroll_end_vertical(struct sway_seat *seat, list_t *children, int active_idx,
		enum sway_layout_direction scrolling_direction) {
	struct sway_container *active = children->items[active_idx];
	struct sway_workspace *workspace = active->pending.workspace;
	double scale = layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0;
	if (scrolling_direction == DIR_UP) {
		struct sway_container *new_active = children->items[children->length - 1];
		double offset = active->pending.y + scale * (active->pending.height + 2.0 * workspace->gaps_inner);
		// Take the first after active that has its top edge in the viewport
		for (int i = active_idx + 1; i < children->length; ++i) {
			struct sway_container *con = children->items[i];
			double y0 = offset;
			if (y0 > 0 && y0 < workspace->height) {
				new_active = con;
				break;
			}
			offset += scale * (con->pending.height + 2.0 * workspace->gaps_inner);
		}
		if (new_active->pending.children) {
			new_active = new_active->current.focused_inactive_child;
		}
		seat_set_focus_container(seat, new_active);
    } else if (scrolling_direction == DIR_DOWN) {
		struct sway_container *new_active = children->items[0];
		double offset = active->pending.y - 2.0 * scale * workspace->gaps_inner;
		// Take the first before active that has its bottom edge in the viewport
		for (int i = active_idx - 1; i >= 0; i--) {
			struct sway_container *con = children->items[i];
			double y1 = offset;
			if (y1 > 0 && y1 < workspace->height) {
				new_active = con;
				break;
			}
			offset -= scale * (con->pending.height + 2.0 * workspace->gaps_inner);
		}
		if (new_active->pending.children) {
			new_active = new_active->current.focused_inactive_child;
		}
		seat_set_focus_container(seat, new_active);
	}
}

static bool scrolling_in_pin_direction(enum sway_container_layout layout,
		enum sway_layout_direction dir) {
	if ((layout == L_HORIZ && (dir == DIR_LEFT || dir == DIR_RIGHT)) ||
		(layout == L_VERT && (dir == DIR_UP || dir == DIR_DOWN))) {
		return true;
	}
	return false;
}

bool layout_scroll_end(struct sway_seat *seat) {
	struct sway_workspace *workspace = seat->workspace;
	if (!workspace->gesture.scrolling) {
		return false;
	}

	enum sway_layout_direction scrolling_direction;
	enum sway_container_layout layout = layout_get_type(workspace);
	if (fabs(workspace->gesture.dx) > fabs(workspace->gesture.dy)) {
		scrolling_direction = workspace->gesture.dx > 0.0 ? DIR_RIGHT : DIR_LEFT;
	} else {
		scrolling_direction = workspace->gesture.dy > 0.0 ? DIR_DOWN : DIR_UP;
	}

	if (workspace->gesture.pin) {
		layout_scroll_unfloat_pinned_container(workspace);
		if (scrolling_in_pin_direction(layout, scrolling_direction)) {
			return true;
		}
	}

	workspace->gesture.scrolling = false;
	if (workspace->tiling->length == 0) {
		return true;
	}
	if (scrolling_direction == DIR_LEFT || scrolling_direction == DIR_RIGHT) {
		if (layout == L_HORIZ) {
			struct sway_container *active = workspace->current.focused_inactive_child;
			int active_idx = max(list_find(workspace->tiling, active), 0);
			scroll_end_horizontal(seat, workspace->tiling, active_idx, scrolling_direction);
		} else {
			struct sway_container *container = get_mouse_container(seat);
			struct sway_container *active = container->current.focused_inactive_child;
			int active_idx = max(list_find(container->pending.children, active), 0);
			scroll_end_horizontal(seat, container->pending.children, active_idx, scrolling_direction);			
		}
	} else {
		if (layout == L_HORIZ) {
			struct sway_container *container = get_mouse_container(seat);
			struct sway_container *active = container->current.focused_inactive_child;
			int active_idx = max(list_find(container->pending.children, active), 0);
			scroll_end_vertical(seat, container->pending.children, active_idx, scrolling_direction);			
		} else {
			struct sway_container *active = workspace->current.focused_inactive_child;
			int active_idx = max(list_find(workspace->tiling, active), 0);
			scroll_end_vertical(seat, workspace->tiling, active_idx, scrolling_direction);
		}
	}
	arrange_workspace(workspace);
	transaction_commit_dirty();
	return true;
}

bool layout_pin_enabled(struct sway_workspace *workspace) {
	if (!workspace->layout.pin.container) {
		return false;
	}
	// Check the pinned container still exists in the workspace
	for (int i = 0; i < workspace->tiling->length; ++i) {
		struct sway_container *con = workspace->tiling->items[i];
		if (workspace->layout.pin.container == con) {
			return true;
		}
	}
	workspace->layout.pin.container = NULL;
	return false;
}

void layout_pin_set(struct sway_workspace *workspace, struct sway_container *container,
		enum sway_layout_pin pos) {
	if (container->pending.parent) {
		// We pin top level containers
		container = container->pending.parent;
	}

	// Toggling logic
	if (workspace->layout.pin.container == container) {
		if (workspace->layout.pin.pos == pos) {
			// Toggling to unpin
			workspace->layout.pin.container = NULL;
		} else {
			// Moving to new position
			workspace->layout.pin.pos = pos;
		}
	} else {
		workspace->layout.pin.container = container;
		workspace->layout.pin.pos = pos;
	}
	arrange_workspace(workspace);
}

void layout_pin_remove(struct sway_workspace *workspace, struct sway_container *container) {
	if (workspace->layout.pin.container == container) {
		workspace->layout.pin.container = NULL;
	}
}

struct sway_container *layout_pin_get_container(struct sway_workspace *workspace) {
	return workspace->layout.pin.container;
}

enum sway_layout_pin layout_pin_get_position(struct sway_workspace *workspace) {
	return workspace->layout.pin.pos;
}

// Selection

// Toggle a selection: depending on mode, chooses a top level or view container
void layout_selection_toggle(struct sway_container *container) {
	struct sway_workspace *workspace = container->pending.workspace;
	if (layout_get_type(workspace) == layout_modifiers_get_mode(workspace) &&
		container->pending.parent) {
		container = container->pending.parent;
	}
	container->selected = !container->selected;
	node_set_dirty(&container->node);
}

// Select every container in the workspace
void layout_selection_workspace(struct sway_workspace *workspace) {
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *con = workspace->floating->items[i];
		con->selected = true;
	}
	for (int i = 0; i < workspace->tiling->length; ++i) {
		struct sway_container *con = workspace->tiling->items[i];
		con->selected = true;
	}
	node_set_dirty(&workspace->node);
}

// Resets the selection
void layout_selection_reset() {
	for (int o = 0; o < root->outputs->length; ++o) {
		struct sway_output *output = root->outputs->items[o];
		for (int w = 0; w < output->current.workspaces->length; ++w) {
			bool selected = false;
			struct sway_workspace *workspace = output->current.workspaces->items[w];
			for (int i = 0; i < workspace->floating->length; ++i) {
				struct sway_container *con = workspace->floating->items[i];
				if (con->selected) {
					con->selected = false;
					selected = true;
				}
				if (con->pending.children) {
					for (int j = 0; j < con->pending.children->length; ++j) {
						struct sway_container *child = con->pending.children->items[j];
						if (child->selected) {
							child->selected = false;
							selected = true;
						}
					}
				}
			}
			for (int i = 0; i < workspace->tiling->length; ++i) {
				struct sway_container *con = workspace->tiling->items[i];
				if (con->selected) {
					con->selected = false;
					selected = true;
				}
				if (con->pending.children) {
					for (int j = 0; j < con->pending.children->length; ++j) {
						struct sway_container *child = con->pending.children->items[j];
						if (child->selected) {
							child->selected = false;
							selected = true;
						}
					}
				}
			}
			if (selected) {
				node_set_dirty(&workspace->node);
			}
		}
	}
}

static struct sway_container *wrap_children_into_container(struct sway_workspace *new_workspace,
		list_t *children, struct sway_container *old_parent, enum sway_container_layout layout) {
	struct sway_container *cont = container_create(NULL);
	cont->current.width = old_parent->current.width;
	cont->current.height = old_parent->current.height;
	cont->pending.width = old_parent->pending.width;
	cont->pending.height = old_parent->pending.height;
	cont->width_fraction = old_parent->width_fraction;
	cont->height_fraction = old_parent->height_fraction;
	cont->current.x = old_parent->current.x;
	cont->current.y = old_parent->current.y;
	cont->pending.x = old_parent->pending.x;
	cont->pending.y = old_parent->pending.y;
	cont->pending.layout = layout;

	cont->pending.children = children;
	cont->current.fullscreen_layout = FULLSCREEN_DISABLED;
	cont->pending.fullscreen_layout = FULLSCREEN_DISABLED;
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *child = children->items[i];
		child->pending.parent = cont;
		child->pending.workspace = new_workspace;
		if (child->current.fullscreen_layout == FULLSCREEN_ENABLED) {
			cont->current.fullscreen_layout = FULLSCREEN_ENABLED;
		}
		if (child->pending.fullscreen_layout == FULLSCREEN_ENABLED) {
			cont->pending.fullscreen_layout = FULLSCREEN_ENABLED;
		}
	}
	cont->pending.workspace = new_workspace;
	container_update_representation(cont);
	container_update_representation(old_parent);
	node_set_dirty(&cont->node);
	return cont;
}

static bool add_selected_children(struct sway_workspace *workspace, list_t *list,
		struct sway_container *container, enum sway_container_layout layout) {
	if (container->selected) {
		// Move floating containers to a position in the new workspace
		if (container->view) {
			double dx = workspace->x - container->pending.workspace->x;
			double dy = workspace->y - container->pending.workspace->y;
			container->pending.x += dx;
			container->pending.y += dy;
		}
		container->pending.workspace = workspace;
		container->pending.layout = layout;
		container->selected = false;
		list_add(list, container);
		return true;
	}
	if (container->pending.children) {
		list_t *selection = create_list();
		int i = 0;
		while (i < container->pending.children->length) {
			struct sway_container *child = container->pending.children->items[i];
			if (child->selected) {
				child->selected = false;
				list_del(container->pending.children, i);
				list_add(selection, child);
			} else {
				++i;
			}
		}
		if (selection->length > 0) {
			struct sway_container *new_parent = wrap_children_into_container(workspace, selection, container, layout);
			list_add(list, new_parent);
			if (container->pending.children->length == 0) {
				return true;
			}
		} else {
			list_free(selection);
		}
	}
	return false;
}

// Move the selection to workspace, with a location given by the current mode modifier
bool layout_selection_move(struct sway_workspace *new_workspace) {
	// All the selected top level containers will be added to a list. In that list
	// there will also be new top level containers for selected non-top level containers.
	// The containers in the list will get their final location and container type
	// depending on the insertion modifier and the target workspace layout type.
	enum sway_container_layout layout = layout_get_type(new_workspace) == L_HORIZ ? L_VERT : L_HORIZ;
	list_t *tiling_selection = create_list();
	list_t *floating_selection = create_list();
	for (int o = 0; o < root->outputs->length; ++o) {
		struct sway_output *output = root->outputs->items[o];
		for (int w = 0; w < output->current.workspaces->length; ++w) {
			int selected = tiling_selection->length + floating_selection->length;
			struct sway_workspace *workspace = output->current.workspaces->items[w];
			list_t *to_delete_floating = create_list();
			for (int i = 0; i < workspace->floating->length; ++i) {
				struct sway_container *con = workspace->floating->items[i];
				if (add_selected_children(new_workspace, floating_selection, con, layout)) {
					list_add(to_delete_floating, con);
				} else {
					arrange_container(con);
					node_set_dirty(&con->node);
				}
			}
			list_t *to_delete_tiling = create_list();
			for (int i = 0; i < workspace->tiling->length; ++i) {
				struct sway_container *con = workspace->tiling->items[i];
				if (add_selected_children(new_workspace, tiling_selection, con, layout)) {
					list_add(to_delete_tiling, con);
				} else {
					arrange_container(con);
					node_set_dirty(&con->node);
				}
			}
			// Remove containers from the workspace lists
			for (int i = 0; i < to_delete_floating->length; ++i) {
				struct sway_container *con = to_delete_floating->items[i];
				list_del(workspace->floating, list_find(workspace->floating, con));
			}
			for (int i = 0; i < to_delete_tiling->length; ++i) {
				struct sway_container *con = to_delete_tiling->items[i];
				list_del(workspace->tiling, list_find(workspace->tiling, con));
			}
			// If there was a selection, re-arrange the workspace
			if (tiling_selection->length + floating_selection->length > selected) {
				arrange_workspace(workspace);
				workspace_update_representation(workspace);
				workspace_consider_destroy(workspace);
				node_set_dirty(&workspace->node);
			}
			// Now finally remove the containers (and possibly the workspace)
			for (int i = 0; i < to_delete_floating->length; ++i) {
				struct sway_container *con = to_delete_floating->items[i];
				if (con->pending.children &&con->pending.children->length == 0) {
					container_reap_empty(con);
				}
			}
			for (int i = 0; i < to_delete_tiling->length; ++i) {
				struct sway_container *con = to_delete_tiling->items[i];
				if (con->pending.children && con->pending.children->length == 0) {
					container_reap_empty(con);
				}
			}
			list_free(to_delete_floating);
			list_free(to_delete_tiling);
		}
	}
	bool changed = tiling_selection->length + floating_selection->length > 0;
	// Insert tiled containers
	struct sway_container *active = new_workspace->current.focused_inactive_child;
	int index = layout_insert_compute_index(new_workspace->tiling, active, layout_modifiers_get_insert(new_workspace));
	for (int i = tiling_selection->length - 1; i >= 0; i--) {
		struct sway_container *con = tiling_selection->items[i];
		workspace_insert_tiling_direct(new_workspace, con, index);
	}
	list_free(tiling_selection);
	// Insert floating containers
	for (int i = 0; i < floating_selection->length; ++i) {
		struct sway_container *con = floating_selection->items[i];
		workspace_add_floating(new_workspace, con);
	}
	list_free(floating_selection);
	// Set focus
	if (changed) {
		arrange_workspace(new_workspace);
		struct sway_seat *seat = input_manager_current_seat();
		bool should_focus = layout_modifiers_get_focus(new_workspace);
		if (should_focus) {
			struct sway_container *focus;
			if (new_workspace->tiling->length > 0) {
				focus = (struct sway_container *) new_workspace->tiling->items[index];
			} else {
				focus =  (struct sway_container *) new_workspace->floating->items[0];
			}
			seat_set_focus_container(seat, focus);
		} else {
			switch_workspace(new_workspace);
		}
		seat_consider_warp_to_focus(seat);
	}
	return changed;
}

bool layout_selection_enabled(struct sway_container *container) {
	if (container->selected) {
		return true;
	}
	if (container->pending.parent && container->pending.parent->selected) {
		return true;
	}
	return false;
}

void layout_selection_set(struct sway_container *container, bool selected) {
	if (container->selected == selected) {
		return;
	}
	container->selected = selected;
	node_set_dirty(&container->node);
}

void layout_selection_to_trail() {
	layout_trail_new();
	for (int o = 0; o < root->outputs->length; ++o) {
		struct sway_output *output = root->outputs->items[o];
		for (int w = 0; w < output->current.workspaces->length; ++w) {
			struct sway_workspace *workspace = output->current.workspaces->items[w];
			for (int i = 0; i < workspace->floating->length; ++i) {
				struct sway_container *con = workspace->floating->items[i];
				bool selected = con->selected;
				if (con->view && con->selected) {
					con->selected = false;
					layout_trailmark_toggle(con->view);
					node_set_dirty(&con->node);
				}
				if (con->pending.children) {
					for (int j = 0; j < con->pending.children->length; ++j) {
						struct sway_container *child = con->pending.children->items[j];
						if (child->view && (child->selected || selected)) {
							child->selected = false;
							layout_trailmark_toggle(child->view);
							node_set_dirty(&child->node);
						}
					}
				}
			}
			for (int i = 0; i < workspace->tiling->length; ++i) {
				struct sway_container *con = workspace->tiling->items[i];
				bool selected = false;
				if (con->selected) {
					con->selected = false;
					selected = true;
				}
				if (con->pending.children) {
					for (int j = 0; j < con->pending.children->length; ++j) {
						struct sway_container *child = con->pending.children->items[j];
						if (child->view && (child->selected || selected)) {
							child->selected = false;
							layout_trailmark_toggle(child->view);
							node_set_dirty(&child->node);
						}
					}
				}
			}
		}
	}
	if (layout_trails_active_length() == 0) {
		layout_trail_delete();
	}
}

static struct sway_trail *trail_create() {
	struct sway_trail *trail = calloc(1, sizeof(struct sway_trail));
	trail->marks = create_list();
	trail->active = 0;
	return trail;
}

static void trail_destroy(struct sway_trail *trail) {
	list_free(trail->marks);
	free(trail);
}

static void trail_clear(struct sway_trail *trail) {
	list_free(trail->marks);
	trail->marks = create_list();
	trail->active = 0;
}

static void trail_to_selection(struct sway_trail *trail) {
	for (int i = 0; i < trail->marks->length; ++i) {
		struct sway_view *view = trail->marks->items[i];
		layout_selection_set(view->container, true);
	}
}

void layout_trail_new() {
	if (trails == NULL) {
		trails = (struct sway_trails *) calloc(1, sizeof(struct sway_trails));
		trails->trails = create_list();
	}
	trails->active = trails->trails->length;
	struct sway_trail *trail = trail_create();
	list_add(trails->trails, trail);
	ipc_event_trails();
}

void layout_trail_next() {
	if (trails == NULL || trails->trails->length <= 1) {
		return;
	}
	trails->active = trails->active == trails->trails->length - 1 ? 0 : trails->active + 1;
	ipc_event_trails();
}

void layout_trail_prev() {
	if (trails == NULL || trails->trails->length <= 1) {
		return;
	}
	trails->active = trails->active == 0 ? trails->trails->length - 1 : trails->active - 1;
	ipc_event_trails();
}

void layout_trail_number(int n) {
	if (trails == NULL || n < 0 || n >= trails->trails->length) {
		return;
	}
	trails->active = n;
	ipc_event_trails();
}

void layout_trail_delete() {
	if (trails == NULL || trails->trails->length == 0) {
		return;
	}
	trail_destroy(trails->trails->items[trails->active]);
	list_del(trails->trails, trails->active);
	if (trails->trails->length == 0) {
		trails->active = 0;
	} else if (trails->active > 0) {
		trails->active--;
	}
	ipc_event_trails();
}

void layout_trail_clear() {
	if (trails == NULL || trails->trails->length == 0) {
		return;
	}
	trail_clear(trails->trails->items[trails->active]);
	ipc_event_trails();
}

void layout_trail_to_selection() {
	if (trails == NULL || trails->trails->length == 0) {
		return;
	}
	trail_to_selection(trails->trails->items[trails->active]);
}

void layout_trailmark_toggle(struct sway_view *view) {
	if (trails == NULL || trails->trails->length == 0) {
		layout_trail_new();
	}
	struct sway_trail *trail = trails->trails->items[trails->active];
	int idx = list_find(trail->marks, view);
	if (idx < 0) {
		list_add(trail->marks, view);
		trail->active = trail->marks->length - 1;
	} else {
		list_del(trail->marks, idx);
		if (trail->marks->length == 0) {
			trail->active = 0;
		} else if (trail->active > 0) {
			trail->active--;
		}
	}
	ipc_event_trails();
	ipc_event_window(view->container, "trailmark");
}

static void trails_focus_view() {
	struct sway_trail *trail = trails->trails->items[trails->active];
	if (trail->marks->length == 0) {
		return;
	}
	struct sway_view *view = trail->marks->items[trail->active];
	struct sway_seat *seat = input_manager_current_seat();
	seat_set_focus_container(seat, view->container);
	seat_consider_warp_to_focus(seat);
}

void layout_trailmark_next() {
	if (trails == NULL || trails->trails->length == 0) {
		return;
	}
	struct sway_trail *trail = trails->trails->items[trails->active];
	if (trail->marks->length == 0) {
		return;
	}
	trail->active = trail->active == trail->marks->length - 1 ? 0 : trail->active + 1;
	trails_focus_view();
}

void layout_trailmark_prev() {
	if (trails == NULL || trails->trails->length == 0) {
		return;
	}
	struct sway_trail *trail = trails->trails->items[trails->active];
	if (trail->marks->length == 0) {
		return;
	}
	trail->active = trail->active == 0 ? trail->marks->length - 1 : trail->active - 1;
	trails_focus_view();
}

void layout_trail_remove_view(struct sway_view *view) {
	if (trails == NULL || trails->trails->length == 0) {
		return;
	}
	bool update = false;
	for (int i = 0; i < trails->trails->length; ++i) {
		struct sway_trail *trail = trails->trails->items[i];
		for (int j = 0; j < trail->marks->length; ++j) {
			struct sway_view *v = trail->marks->items[j];
			if (v == view) {
				list_del(trail->marks, j);
				if (trail->active == j) {
					if (trail->marks->length == 0) {
						trail->active = 0;
					} else if (trail->active > 0) {
						trail->active--;
					}
				}
				update = true;
				break;
			}
		}
	}
	if (update) {
		ipc_event_trails();
	}
}

// For IPC events
int layout_trails_length() {
	if (trails == NULL) {
		return 0;
	}
	return trails->trails->length;
}

int layout_trails_active() {
	if (trails == NULL) {
		return 0;
	}
	return trails->active;
}

int layout_trails_active_length() {
	if (trails == NULL || trails->trails->length == 0) {
		return 0;
	}
	struct sway_trail *trail = trails->trails->items[trails->active];
	return trail->marks->length;
}

bool layout_trails_trailmarked(struct sway_view *view) {
	if (trails == NULL || trails->trails->length == 0 || view == NULL) {
		return false;
	}
	struct sway_trail *trail = trails->trails->items[trails->active];
	for (int i = 0; i < trail->marks->length; ++i) {
		struct sway_view *v = trail->marks->items[i];
		if (v == view) {
			return true;
		}
	}
	return false;
}

static bool find_view(struct sway_container *container, void *data) {
	struct sway_view *view = data;
	return container->view == view;
}

// Find the view. If it exists, detach its container and return it. If it
// doesn't, return NULL
static struct sway_container *find_and_detach_container(struct sway_view *view) {
	struct sway_container *container = root_find_container(find_view, view);
	if (container) {
		if (!container_is_floating(container)) {
			struct sway_container *parent = container->pending.parent;
			list_t *siblings = parent->pending.children;
			list_del(siblings, list_find(siblings, container));
			node_set_dirty(&parent->pending.workspace->node);
			container_update_representation(parent);
			node_set_dirty(&parent->node);
			container_reap_empty(parent);
		}
	}
	return container;
}

static void fill_container(struct sway_space_container *space_container,
		struct sway_container *container) {
	container->width_fraction = space_container->width_fraction;
	container->height_fraction = space_container->height_fraction;
	container->pending.layout = space_container->layout;
	container->pending.x = space_container->x;
	container->pending.y = space_container->y;
	container->pending.width = space_container->width;
	container->pending.height = space_container->height;
}

static struct sway_container *layout_space_container_restore_tiling(struct sway_workspace *workspace,
		struct sway_space_container *space_container,
		struct sway_space_container *focused, struct sway_container *parent) {
	if (space_container->children) {
		struct sway_container *parent = container_create(NULL);
		parent->pending.workspace = workspace;
		parent->pending.layout = space_container->layout;
		parent->pending.focused_inactive_child = NULL;
		//parent->pending.parent = NULL;
		bool has_children = false;
		for (int i = 0; i < space_container->children->length; ++i) {
			struct sway_space_container *space_con = space_container->children->items[i];
			struct sway_container *child = layout_space_container_restore_tiling(workspace,
				space_con, focused, parent);
			if (child) {
				has_children = true;
			}
			if (space_con == space_container->focused_inactive) {
				parent->pending.focused_inactive_child = child;
			}
		}
		if (has_children) {
			fill_container(space_container, parent);
			container_update_representation(parent);
			node_set_dirty(&parent->node);
			list_add(workspace->tiling, parent);
		} else {
			container_begin_destroy(parent);
			container_destroy(parent);
			parent = NULL;
		}
		return parent;
	}
	if (space_container->view) {
		// Find view, detach its container
		struct sway_container *container = find_and_detach_container(space_container->view->view);
		if (container) {
			if (container_is_floating(container)) {
				struct sway_workspace * ws = container->pending.workspace;
				list_del(ws->floating, list_find(ws->floating, container));
				workspace_consider_destroy(ws);
				node_set_dirty(&ws->node);
			}
			container->view->content_scale = space_container->view->content_scale;
			arrange_container(container);
			node_set_dirty(&container->node);
			list_add(parent->pending.children, container);
			container->pending.parent = parent;
			container->pending.workspace = parent->pending.workspace;
			fill_container(space_container, container);
			container_update_representation(container);
			node_set_dirty(&parent->node);
		}
		if (space_container == focused) {
			struct sway_seat *seat = input_manager_current_seat();
			if (container) {
				seat_set_focus_container(seat, container);
			} else {
				seat_set_focus_workspace(seat, workspace);
			}
		}
		return container;
	}
	return NULL;
}

static void layout_space_container_restore_floating(struct sway_workspace *workspace,
		struct sway_space_container *space_container,
		struct sway_space_container *focused) {
	if (space_container->view) {
		// Find view, detach its container
		struct sway_container *container = find_and_detach_container(space_container->view->view);
		if (container) {
			struct sway_output *old_output = container->pending.workspace->output;
			struct sway_workspace *old_workspace = container->pending.workspace;
			if (container_is_floating(container)) {
				list_del(container->pending.workspace->floating,
					list_find(container->pending.workspace->floating, container));
				workspace_consider_destroy(container->pending.workspace);
				node_set_dirty(&container->pending.workspace->node);
			}
			container->view->content_scale = space_container->view->content_scale;
			arrange_container(container);
			node_set_dirty(&container->node);
			list_add(workspace->floating, container);
			container->pending.parent = NULL;
			container->pending.workspace = workspace;
			fill_container(space_container, container);
			// If changing output, adjust the coordinates of the window.
			if (old_output != workspace->output) {
				struct wlr_box workspace_box, old_workspace_box;
				workspace_get_box(workspace, &workspace_box);
				workspace_get_box(old_workspace, &old_workspace_box);
				floating_fix_coordinates(container, &old_workspace_box, &workspace_box);
			}
			container_update_representation(container);
		}
		if (space_container == focused) {
			struct sway_seat *seat = input_manager_current_seat();
			if (container) {
				seat_set_focus_container(seat, container);
			} else {
				seat_set_focus_workspace(seat, workspace);
			}
		}
	}
}

static bool container_find_view(struct sway_space_container *container,
		struct sway_view *view) {
	if (container->children) {
		for (int i = 0; i < container->children->length; ++i) {
			struct sway_space_container *space_con = container->children->items[i];
			if (container_find_view(space_con, view)) {
				return true;
			}
		}
	} else if (container->view->view == view) {
		return true;
	}
	return false;
}

static bool space_find_view(struct sway_space *space, struct sway_view *view) {
	for (int i = 0; i < space->tiling->length; ++i) {
		struct sway_space_container *con = space->tiling->items[i];
		if (container_find_view(con, view)) {
			return true;
		}
	}
	for (int i = 0; i < space->floating->length; ++i) {
		struct sway_space_container *con = space->floating->items[i];
		if (container_find_view(con, view)) {
			return true;
		}
	}
	return false;
}

void apply_reset(struct sway_container *container, void *data) {
	if (container->view) {
		// Search for container->view in space
		struct sway_space *space = data;
		if (!space_find_view(space, container->view)) {
			view_close(container->view);
		}
	}
}

void layout_space_restore(struct sway_space *space, struct sway_workspace *workspace, bool reset) {
	if (workspace->fullscreen && workspace->fullscreen->pending.fullscreen_mode != FULLSCREEN_NONE) {
		container_fullscreen_disable(workspace->fullscreen);
	}
	if (reset) {
		// Close all views in workspace that are not part of space
		workspace_for_each_container(workspace, apply_reset, space);
	}
	for (int i = 0; i < space->tiling->length; ++i) {
		struct sway_space_container *container = space->tiling->items[i];
		layout_space_container_restore_tiling(workspace, container, space->focused, NULL);
	}
	for (int i = 0; i < space->floating->length; ++i) {
		struct sway_space_container *container = space->floating->items[i];
		layout_space_container_restore_floating(workspace, container, space->focused);
	}
	if (space->tiling->length + space->floating->length > 0) {
		arrange_workspace(workspace);
		node_set_dirty(&workspace->node);
	}
}

static void layout_toggle_size_init(struct sway_workspace *workspace) {
	workspace->layout.toggle_size.mode = TOGGLE_SIZE_NONE;
	workspace->layout.toggle_size.container = NULL;
	workspace->layout.toggle_size.width = 0.0;
	workspace->layout.toggle_size.height = 0.0;
}

enum sway_toggle_size layout_toggle_size_mode(struct sway_workspace *workspace) {
	return workspace->layout.toggle_size.mode;
}

static struct sway_container *layout_toggle_size_get_container(struct sway_workspace *workspace) {
	return workspace->layout.toggle_size.container;
}

double layout_toggle_size_width_fraction(struct sway_workspace *workspace) {
	return workspace->layout.toggle_size.width;
}

double layout_toggle_size_height_fraction(struct sway_workspace *workspace) {
	return workspace->layout.toggle_size.height;
}

static void push_sizes(struct sway_container *container) {
	container->toggle_size.saved_width_fraction = container->width_fraction;
	container->toggle_size.saved_height_fraction = container->height_fraction;
}

static void pop_sizes(struct sway_container *container) {
	container->width_fraction = container->toggle_size.saved_width_fraction;
	container->height_fraction = container->toggle_size.saved_height_fraction;
}

static void set_sizes(struct sway_container *container, double width_fraction,
		double height_fraction) {
	container->width_fraction = width_fraction;
	container->height_fraction = height_fraction;
}

static void apply_container_sizes(struct sway_container *container,
		double new_width, double new_height, enum sway_operation op) {
	if (container == NULL) {
		return;
	}

	struct sway_workspace *workspace = container->pending.workspace;
	if (!workspace) {
		return;
	}
	const enum sway_toggle_size mode = layout_toggle_size_mode(workspace);
	bool single = container->pending.parent ?
		container->pending.parent->toggle_size.single : container->toggle_size.single;

	enum sway_toggle_size_state state = container->toggle_size.state;
	enum sway_toggle_size_state ns;
	switch (mode) {
	case TOGGLE_SIZE_NONE:
		switch (state) {
		case TOGGLE_STATE_NONE:
			switch (op) {
			case OPERATION_FOCUS:
			case OPERATION_UNFOCUS:
				ns =  single ? TOGGLE_STATE_TOGGLED : TOGGLE_STATE_NONE;
				if (ns == TOGGLE_STATE_TOGGLED) {
					push_sizes(container);
					set_sizes(container, new_width, new_height);
				}
				break;
			case OPERATION_RESIZE:
				ns =  TOGGLE_STATE_NONE_FORCED;
				set_sizes(container, new_width, new_height);
				break;
			case OPERATION_TOGGLE:
				ns = TOGGLE_STATE_TOGGLED_FORCED;
				push_sizes(container);
				set_sizes(container, new_width, new_height);
				break;
			default:
				return;
			}
			break;
		case TOGGLE_STATE_TOGGLED:
			switch (op) {
			case OPERATION_FOCUS:
			case OPERATION_UNFOCUS:
				ns = single ? TOGGLE_STATE_TOGGLED : TOGGLE_STATE_NONE;
				if (ns == TOGGLE_STATE_NONE) {
					pop_sizes(container);
				}
				break;
			case OPERATION_RESIZE:
				ns = TOGGLE_STATE_NONE_FORCED;
				set_sizes(container, new_width, new_height);
				break;
			case OPERATION_TOGGLE:
				ns =  TOGGLE_STATE_NONE;
				pop_sizes(container);
				break;
			default:
				return;
			}
			break;
		case TOGGLE_STATE_NONE_FORCED:
			switch (op) {
			case OPERATION_FOCUS:
			case OPERATION_UNFOCUS:
				ns = TOGGLE_STATE_NONE_FORCED;
				break;
			case OPERATION_RESIZE:
				ns = TOGGLE_STATE_NONE_FORCED;
				set_sizes(container, new_width, new_height);
				break;
			case OPERATION_TOGGLE:
				ns = TOGGLE_STATE_TOGGLED_FORCED;
				push_sizes(container);
				set_sizes(container, new_width, new_height);
				break;
			default:
				return;
			}
			break;
		case TOGGLE_STATE_TOGGLED_FORCED:
			switch (op) {
			case OPERATION_FOCUS:
			case OPERATION_UNFOCUS:
				ns =  TOGGLE_STATE_TOGGLED_FORCED;
				break;
			case OPERATION_RESIZE:
				ns = TOGGLE_STATE_NONE_FORCED;
				set_sizes(container, new_width, new_height);
				break;
			case OPERATION_TOGGLE:
				ns = TOGGLE_STATE_NONE;
				pop_sizes(container);
				break;
			default:
				return;
			}
			break;
		default:
			return;
		}
		break;
	case TOGGLE_SIZE_ACTIVE:
		switch (state) {
		case TOGGLE_STATE_NONE:
			switch (op) {
			case OPERATION_FOCUS:
				ns = TOGGLE_STATE_TOGGLED;
				push_sizes(container);
				set_sizes(container, new_width, new_height);
				break;
			case OPERATION_UNFOCUS:
				ns = TOGGLE_STATE_NONE;
				break;
			case OPERATION_RESIZE:
				ns = TOGGLE_STATE_NONE_FORCED;
				set_sizes(container, new_width, new_height);
				break;
			case OPERATION_TOGGLE:
				ns = TOGGLE_STATE_TOGGLED_FORCED;
				push_sizes(container);
				set_sizes(container, new_width, new_height);
				break;
			default:
				return;
			}
			break;
		case TOGGLE_STATE_TOGGLED:
			switch (op) {
			case OPERATION_FOCUS:
				ns = TOGGLE_STATE_TOGGLED;
				break;
			case OPERATION_UNFOCUS:
				ns = TOGGLE_STATE_NONE;
				pop_sizes(container);
				break;
			case OPERATION_RESIZE:
				ns = TOGGLE_STATE_NONE_FORCED;
				set_sizes(container, new_width, new_height);
				break;
			case OPERATION_TOGGLE:
				ns = TOGGLE_STATE_NONE;
				pop_sizes(container);
				break;
			default:
				return;
			}
			break;
		case TOGGLE_STATE_NONE_FORCED:
			switch (op) {
			case OPERATION_FOCUS:
			case OPERATION_UNFOCUS:
				ns = TOGGLE_STATE_NONE_FORCED;
				break;
			case OPERATION_RESIZE:
				ns = TOGGLE_STATE_NONE_FORCED;
				set_sizes(container, new_width, new_height);
				break;
			case OPERATION_TOGGLE:
				ns = TOGGLE_STATE_TOGGLED;
				push_sizes(container);
				set_sizes(container, new_width, new_height);
				break;
			default:
				return;
			}
			break;
		case TOGGLE_STATE_TOGGLED_FORCED:
			switch (op) {
			case OPERATION_FOCUS:
			case OPERATION_UNFOCUS:
				ns = TOGGLE_STATE_TOGGLED_FORCED;
				break;
			case OPERATION_RESIZE:
				set_sizes(container, new_width, new_height);
				ns = TOGGLE_STATE_NONE_FORCED;
				break;
			case OPERATION_TOGGLE:
				ns = TOGGLE_STATE_NONE;
				pop_sizes(container);
				break;
			default:
				return;
			}
			break;
		default:
			return;
		}
		break;
	case TOGGLE_SIZE_ALL:
		switch (state) {
		case TOGGLE_STATE_NONE:
			switch (op) {
			case OPERATION_FOCUS:
			case OPERATION_UNFOCUS:
				ns = TOGGLE_STATE_TOGGLED;
				push_sizes(container);
				set_sizes(container, new_width, new_height);
				break;
			case OPERATION_RESIZE:
				ns = TOGGLE_STATE_NONE_FORCED;
				set_sizes(container, new_width, new_height);
				break;
			case OPERATION_TOGGLE:
				ns = TOGGLE_STATE_TOGGLED_FORCED;
				push_sizes(container);
				set_sizes(container, new_width, new_height);
				break;
			default:
				return;
			}
			break;
		case TOGGLE_STATE_TOGGLED:
			switch (op) {
			case OPERATION_FOCUS:
			case OPERATION_UNFOCUS:
				ns = TOGGLE_STATE_TOGGLED;
				break;
			case OPERATION_RESIZE:
				ns = TOGGLE_STATE_NONE_FORCED;
				set_sizes(container, new_width, new_height);
				break;
			case OPERATION_TOGGLE:
				ns = TOGGLE_STATE_NONE;
				pop_sizes(container);
				break;
			default:
				return;
			}
			break;
		case TOGGLE_STATE_NONE_FORCED:
			switch (op) {
			case OPERATION_FOCUS:
			case OPERATION_UNFOCUS:
				ns = TOGGLE_STATE_NONE_FORCED;
				break;
			case OPERATION_RESIZE:
				ns = TOGGLE_STATE_NONE_FORCED;
				set_sizes(container, new_width, new_height);
				break;
			case OPERATION_TOGGLE:
				ns = TOGGLE_STATE_TOGGLED;
				push_sizes(container);
				set_sizes(container, new_width, new_height);
				break;
			default:
				return;
			}
			break;
		case TOGGLE_STATE_TOGGLED_FORCED:
			switch (op) {
			case OPERATION_FOCUS:
			case OPERATION_UNFOCUS:
				ns = TOGGLE_STATE_TOGGLED_FORCED;
				break;
			case OPERATION_RESIZE:
				ns = TOGGLE_STATE_NONE_FORCED;
				set_sizes(container, new_width, new_height);
				break;
			case OPERATION_TOGGLE:
				ns = TOGGLE_STATE_NONE;
				pop_sizes(container);
				break;
			default:
				return;
			}
			break;
		default:
			return;
		}
		break;
	default:
		return;
	}
	container->toggle_size.state = ns;
}

static void apply_container_and_parent_sizes(struct sway_container *container,
		double width_fraction, double height_fraction, enum sway_operation op) {
	if (container->pending.parent) {
		apply_container_sizes(container->pending.parent, width_fraction, height_fraction, op);
	}
	apply_container_sizes(container, width_fraction, height_fraction, op);
}

void layout_toggle_size(struct sway_workspace *workspace,
		struct sway_container *container, enum sway_toggle_size mode,
		double width_fraction, double height_fraction) {
	enum sway_toggle_size old_mode = workspace->layout.toggle_size.mode;
	if (old_mode == mode && mode == TOGGLE_SIZE_NONE) {
		return;
	}
	workspace->layout.toggle_size.mode = mode;
	workspace->layout.toggle_size.container = container;
	workspace->layout.toggle_size.width = width_fraction;
	workspace->layout.toggle_size.height = height_fraction;

	if (mode == TOGGLE_SIZE_ACTIVE) {
		apply_container_and_parent_sizes(container, width_fraction, height_fraction, OPERATION_FOCUS);
	} else {
		for (int i = 0; i < workspace->tiling->length; ++i) {
			struct sway_container *con = workspace->tiling->items[i];
			apply_container_sizes(con, width_fraction, height_fraction, OPERATION_FOCUS);
		}
	}
	arrange_workspace(workspace);
	node_set_dirty(&workspace->node);
}

void layout_tiling_resize_callback(struct sway_container *container) {
	apply_container_sizes(container, container->width_fraction,
		container->height_fraction, OPERATION_RESIZE);

	if (!container->pending.parent) {
		// If it is a top level container, propagate
		for (int i = 0; i < container->pending.children->length; ++i) {
			struct sway_container *child = container->pending.children->items[i];
			apply_container_sizes(child, child->width_fraction,
				child->height_fraction, OPERATION_RESIZE);
		}
	}

	// TODO: add a Lua callback
}

void layout_maximize_if_single(struct sway_workspace *workspace) {
	if (workspace && config->maximize_if_single) {
		bool arrange = false;
		const bool single = workspace->tiling->length == 1;
		for (int i = 0; i < workspace->tiling->length; ++i) {
			struct sway_container *con = workspace->tiling->items[i];
			if (con->toggle_size.single || single) {
				arrange = true;
				con->toggle_size.single = single;
				for (int j = 0; j < con->pending.children->length; ++j) {
					struct sway_container *child = con->pending.children->items[j];
					apply_container_sizes(child, 1.0, 1.0, OPERATION_FOCUS);
					node_set_dirty(&child->node);
				}
				apply_container_sizes(con, 1.0, 1.0, OPERATION_FOCUS);
				node_set_dirty(&con->node);
			}
		}
		if (arrange) {
			arrange_workspace(workspace);
			node_set_dirty(&workspace->node);
		}
	}
}

void layout_toggle_size_change_focus(struct sway_node *last_focus,
		struct sway_container *new_container, struct sway_workspace *new_workspace) {
	if (new_workspace == NULL) {
		return;
	}

	struct sway_workspace *last_workspace = NULL;
	struct sway_container *last_container = NULL;
	if (last_focus) {
		if (last_focus->type == N_WORKSPACE) {
			last_workspace = last_focus->sway_workspace;
		} else {
			last_container = last_focus->sway_container;
			last_workspace = last_focus->sway_container->current.workspace;
		}
	}

	if (new_workspace == last_workspace) {
		layout_maximize_if_single(new_workspace);
		if (last_container) {
			apply_container_and_parent_sizes(last_container,
				layout_toggle_size_width_fraction(new_workspace),
				layout_toggle_size_height_fraction(new_workspace), OPERATION_UNFOCUS);
		}
		if (new_container) {
			apply_container_and_parent_sizes(new_container,
				layout_toggle_size_width_fraction(new_workspace),
				layout_toggle_size_height_fraction(new_workspace), OPERATION_FOCUS);
			node_set_dirty(&new_container->node);
		}
		new_workspace->layout.toggle_size.container = new_container;
	} else {
		if (new_container) {
			struct sway_container *old_active = layout_toggle_size_get_container(new_workspace);
			if (old_active && old_active != new_container) {
				apply_container_and_parent_sizes(old_active,
					layout_toggle_size_width_fraction(new_workspace),
					layout_toggle_size_height_fraction(new_workspace), OPERATION_UNFOCUS);
				node_set_dirty(&old_active->node);
			}
			layout_maximize_if_single(new_workspace);
			apply_container_and_parent_sizes(new_container,
				layout_toggle_size_width_fraction(new_workspace),
				layout_toggle_size_height_fraction(new_workspace), OPERATION_FOCUS);
			new_workspace->layout.toggle_size.container = new_container;
			arrange_workspace(new_workspace);
			node_set_dirty(&new_container->node);
			node_set_dirty(&new_workspace->node);
		} else {
			return;
		}
	}
	arrange_workspace(new_workspace);
	node_set_dirty(&new_workspace->node);
}

void layout_toggle_size_container(struct sway_container *container,
		double width_fraction, double height_fraction) {
	if (container->pending.parent) {
		apply_container_sizes(container->pending.parent, width_fraction, height_fraction, OPERATION_TOGGLE);
		node_set_dirty(&container->pending.parent->node);
	}
	apply_container_sizes(container, width_fraction, height_fraction, OPERATION_TOGGLE);
	struct sway_workspace *workspace = container->pending.workspace;
	arrange_workspace(workspace);
	node_set_dirty(&container->node);
	node_set_dirty(&workspace->node);
}
