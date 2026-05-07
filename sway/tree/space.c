#include "sway/tree/space.h"
#include "sway/tree/workspace.h"
#include "sway/tree/arrange.h"
#include "sway/output.h"

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
	if (container_is_floating(container) && container->pending.workspace) {
		struct sway_output *output = container->pending.workspace->output;
		container->pending.x = output->width * space_container->x + output->lx;
		container->pending.y = output->height * space_container->y + output->ly;
		container->pending.width = space_container->width * output->width;
		container->pending.height = space_container->height * output->height;
	} else {
		container->pending.x = space_container->x;
		container->pending.y = space_container->y;
		container->pending.width = space_container->width;
		container->pending.height = space_container->height;
	}
}

static struct sway_container *layout_space_container_restore_tiling(struct sway_workspace *workspace,
		struct sway_space_container *space_container,
		struct sway_space_container *focused, struct sway_container *parent) {
	if (space_container->children) {
		struct sway_container *parent = container_create(NULL);
		parent->pending.workspace = workspace;
		parent->pending.layout = space_container->layout;
		parent->pending.focused_inactive_child = NULL;
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
			parent = NULL;
		}
		return parent;
	}
	if (space_container->view) {
		// Find view, detach its container
		struct sway_container *container = find_and_detach_container(space_container->view->view);
		if (container) {
			if (container->scratchpad) {
				root_scratchpad_show(container);
				root_scratchpad_remove_container(container);
			}
			if (container_is_floating(container)) {
				struct sway_workspace * ws = container->pending.workspace;
				list_del(ws->floating, list_find(ws->floating, container));
				workspace_consider_destroy(ws);
				node_set_dirty(&ws->node);
			}
			container->view->content_scale = space_container->view->content_scale;
			container->pending.workspace = parent->pending.workspace;
			arrange_container(container);
			node_set_dirty(&container->node);
			list_add(parent->pending.children, container);
			container->pending.parent = parent;
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
			if (container->scratchpad) {
				root_scratchpad_show(container);
				root_scratchpad_remove_container(container);
			}
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

struct space_data {
	struct sway_space *space;
	list_t *views;
};

static void get_views(struct sway_container *container, void *data) {
	if (container->view) {
		// Search for container->view in space
		struct space_data *space_data = data;
		if (!space_find_view(space_data->space, container->view)) {
			list_add(space_data->views, container->view);
		}
	}
}

void layout_space_restore(struct sway_space *space, struct sway_workspace *workspace, enum sway_space_restore restore) {
	if (workspace->fullscreen && workspace->fullscreen->pending.fullscreen_mode != FULLSCREEN_NONE) {
		container_fullscreen_disable(workspace->fullscreen);
	}
	if (restore == SPACE_RESTORE_CLOSE || restore == SPACE_RESTORE_HIDE) {
		struct space_data data = {
			.space = space,
			.views = create_list(),
		};
		workspace_for_each_container(workspace, get_views, &data);
		if (restore == SPACE_RESTORE_CLOSE) {
			// Close all views in workspace that are not part of space
			for (int i = 0; i < data.views->length; ++i) {
				struct sway_view *view = data.views->items[i];
				view_close(view);
			}
		} else {
			for (int i = 0; i < data.views->length; ++i) {
				struct sway_view *view = data.views->items[i];
				struct sway_container *container = view->container;
				if (container->scratchpad) {
					root_scratchpad_hide(container);
				} else {
					root_scratchpad_add_container(container, NULL);
				}
			}
		}
		list_free(data.views);
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

static struct sway_space_view *space_view_create(struct sway_view *sway_view,
		struct sway_space_container *container, float content_scale) {
	struct sway_space_view *view = malloc(sizeof(struct sway_space_view));
	view->view = sway_view;
	view->container = container;
	view->content_scale = content_scale;
	return view;
}

static void space_view_destroy(struct sway_space_view *view) {
	free(view);
}

static struct sway_space_container *space_container_create(struct sway_container *container,
			struct sway_container *focused, struct sway_space *space) {
	struct sway_space_container *space_container = malloc(sizeof(struct sway_space_container));
	if (container->pending.children) {
		space_container->children = create_list();
		space_container->focused_inactive = NULL;
		for (int i = 0; i < container->pending.children->length; ++i) {
			struct sway_container *con = container->pending.children->items[i];
			struct sway_space_container *space_con = space_container_create(con, focused, space);
			if (con == container->current.focused_inactive_child) {
				space_container->focused_inactive = space_con;
			}
			if (con == focused) {
				space->focused = space_con;
			}
			list_add(space_container->children, space_con);
		}
	} else {
		space_container->children = NULL;
	}
	if (container->view) {
		space_container->view = space_view_create(container->view, space_container,
			container->view->content_scale);
	} else {
		space_container->view = NULL;
	}
	if (container_is_floating(container) && container->pending.workspace) {
		struct sway_output *output = container->pending.workspace->output;
		space_container->x = (container->pending.x - output->lx) / output->width;
		space_container->y = (container->pending.y - output->ly) / output->height;
		space_container->width = container->pending.width / output->width;
		space_container->height = container->pending.height / output->height;
	} else {
		space_container->x = container->pending.x;
		space_container->y = container->pending.y;
		space_container->width = container->pending.width;
		space_container->height = container->pending.height;
	}
	space_container->width_fraction = container->width_fraction;
	space_container->height_fraction = container->height_fraction;
	space_container->layout = container->pending.layout;
	return space_container;
}

static void space_container_destroy(struct sway_space_container *container) {
	if (container->children) {
		for (int i = 0; i < container->children->length; ++i) {
			struct sway_space_container *con = container->children->items[i];
			space_container_destroy(con);
		}
		list_free(container->children);
	}
	if (container->view) {
		space_view_destroy(container->view);
	}
	free(container);
}

static struct sway_space *space_create(const char *name) {
	struct sway_space *space = malloc(sizeof(struct sway_space));
	space->name = strdup(name);
	space->tiling = create_list();
	space->floating = create_list();
	list_add(root->spaces, space);
	return space;
}

static void space_destroy(struct sway_space *space) {
	int idx = list_find(root->spaces, space);
	if (idx >= 0) {
		list_del(root->spaces, idx);
	}
	free(space->name);
	for (int i = 0; i < space->tiling->length; ++i) {
		struct sway_space_container *con = space->tiling->items[i];
		space_container_destroy(con);
	}
	list_free(space->tiling);
	for (int i = 0; i < space->floating->length; ++i) {
		struct sway_space_container *con = space->floating->items[i];
		space_container_destroy(con);
	}
	list_free(space->floating);
	free(space);
}

static struct sway_space *find_space(const char *name) {
	for (int i = 0; i < root->spaces->length; ++i) {
		struct sway_space *space = root->spaces->items[i];
		if (strcmp(space->name, name) == 0) {
			return space;
		}
	}
	return NULL;
}

// Save the current workspace configuration into a space with name
void space_save(struct sway_workspace *workspace, const char *name) {
	struct sway_space *space = find_space(name);
	if (space) {
		space_destroy(space);
	}
	space = space_create(name);

	struct sway_seat *seat = input_manager_current_seat();
	struct sway_container *focused = seat_get_focused_container(seat);
	if (focused && focused->pending.workspace != workspace) {
		focused = NULL;
	}

	for (int i = 0; i < workspace->tiling->length; ++i) {
		struct sway_container *container = workspace->tiling->items[i];
		struct sway_space_container *space_container = space_container_create(container, focused, space);
		list_add(space->tiling, space_container);
	}
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *container = workspace->floating->items[i];
		struct sway_space_container *space_container = space_container_create(container, focused, space);
		list_add(space->floating, space_container);
		if (container == focused) {
			space->focused = space_container;
		}
	}
}

// Load the space with name into the current workspace.
// If restore is SPACCE_RESTORE_LOAD, add the space data to the workspace.
// If it is SPACE_RESTORE_CLOSE, close any views not belonging to the space.
// If it is SPACE_RESTORE_HIDE, hide any views not belonging to the space in the
// scratchpad.
void space_load(struct sway_workspace *workspace, const char *name, enum sway_space_restore restore) {
	struct sway_space *space = find_space(name);
	if (!space) {
		return;
	}
	layout_space_restore(space, workspace, restore);
}
