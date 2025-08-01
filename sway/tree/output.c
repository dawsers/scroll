#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include "sway/ipc-server.h"
#include "sway/layers.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/workspace.h"
#include "sway/server.h"
#include "log.h"
#include "util.h"

enum wlr_direction opposite_direction(enum wlr_direction d) {
	switch (d) {
	case WLR_DIRECTION_UP:
		return WLR_DIRECTION_DOWN;
	case WLR_DIRECTION_DOWN:
		return WLR_DIRECTION_UP;
	case WLR_DIRECTION_RIGHT:
		return WLR_DIRECTION_LEFT;
	case WLR_DIRECTION_LEFT:
		return WLR_DIRECTION_RIGHT;
	}
	assert(false);
	return 0;
}

static void restore_workspaces(struct sway_output *output) {
	// Workspace output priority
	for (int i = 0; i < root->outputs->length; i++) {
		struct sway_output *other = root->outputs->items[i];
		if (other == output) {
			continue;
		}

		for (int j = 0; j < other->workspaces->length; j++) {
			struct sway_workspace *ws = other->workspaces->items[j];
			struct sway_output *highest =
				workspace_output_get_highest_available(ws);
			if (highest == output) {
				workspace_detach(ws);
				output_add_workspace(output, ws);
				ipc_event_workspace(NULL, ws, "move");
				j--;
			}
		}

		if (other->workspaces->length == 0) {
			char *next = workspace_next_name(other->wlr_output->name);
			struct sway_workspace *ws = workspace_create(other, next);
			free(next);
			ipc_event_workspace(NULL, ws, "init");
		}
	}

	// Saved workspaces
	while (root->fallback_output->workspaces->length) {
		struct sway_workspace *ws = root->fallback_output->workspaces->items[0];
		workspace_detach(ws);
		output_add_workspace(output, ws);

		// If the floater was made floating while on the NOOP output, its width
		// and height will be zero and it should be reinitialized as a floating
		// container to get the appropriate size and location. Additionally, if
		// the floater is wider or taller than the output or is completely
		// outside of the output's bounds, do the same as the output layout has
		// likely changed and the maximum size needs to be checked and the
		// floater re-centered
		for (int i = 0; i < ws->floating->length; i++) {
			struct sway_container *floater = ws->floating->items[i];
			if (floater->pending.width == 0 || floater->pending.height == 0 ||
					floater->pending.width > output->width ||
					floater->pending.height > output->height ||
					floater->pending.x > output->lx + output->width ||
					floater->pending.y > output->ly + output->height ||
					floater->pending.x + floater->pending.width < output->lx ||
					floater->pending.y + floater->pending.height < output->ly) {
				container_floating_resize_and_center(floater);
			}
		}

		ipc_event_workspace(NULL, ws, "move");
	}

	output_sort_workspaces(output);
}

static void destroy_scene_layers(struct sway_output *output) {
	sway_scene_node_destroy(&output->fullscreen_background->node);

	scene_node_disown_children(output->layers.tiling);
	scene_node_disown_children(output->layers.fullscreen);

	sway_scene_node_destroy(&output->layers.shell_background->node);
	sway_scene_node_destroy(&output->layers.shell_bottom->node);
	sway_scene_node_destroy(&output->layers.tiling->node);
	sway_scene_node_destroy(&output->layers.fullscreen->node);
	sway_scene_node_destroy(&output->layers.shell_top->node);
	sway_scene_node_destroy(&output->layers.shell_overlay->node);
	sway_scene_node_destroy(&output->layers.session_lock->node);
}

struct sway_output *output_create(struct wlr_output *wlr_output) {
	struct sway_output *output = calloc(1, sizeof(struct sway_output));
	node_init(&output->node, N_OUTPUT, output);

	bool failed = false;
	output->layers.shell_background = alloc_scene_tree(root->staging, &failed);
	output->layers.shell_bottom = alloc_scene_tree(root->staging, &failed);
	output->layers.tiling = alloc_scene_tree(root->staging, &failed);
	output->layers.tiling->node.info.wlr_output = wlr_output;
	output->layers.fullscreen = alloc_scene_tree(root->staging, &failed);
	output->layers.shell_top = alloc_scene_tree(root->staging, &failed);
	output->layers.shell_overlay = alloc_scene_tree(root->staging, &failed);
	output->layers.session_lock = alloc_scene_tree(root->staging, &failed);

	if (!failed) {
		output->fullscreen_background = sway_scene_rect_create(
			output->layers.fullscreen, 0, 0, (float[4]){0.f, 0.f, 0.f, 1.f});

		if (!output->fullscreen_background) {
			sway_log(SWAY_ERROR, "Unable to allocate a background rect");
			failed = true;
		}
	}

	if (failed) {
		destroy_scene_layers(output);
		sway_scene_output_destroy(output->scene_output);
		free(output);
		return NULL;
	}

	output->wlr_output = wlr_output;
	wlr_output->data = output;
	output->detected_subpixel = wlr_output->subpixel;
	output->scale_filter = SCALE_FILTER_NEAREST;

	wl_list_insert(&root->all_outputs, &output->link);

	output->workspaces = create_list();
	output->current.workspaces = create_list();
	wl_list_init(&output->layer_surfaces);

	return output;
}

void output_enable(struct sway_output *output) {
	if (!sway_assert(!output->enabled, "output is already enabled")) {
		return;
	}
	struct wlr_output *wlr_output = output->wlr_output;
	output->enabled = true;
	list_add(root->outputs, output);

	restore_workspaces(output);

	struct sway_workspace *ws = NULL;
	if (!output->workspaces->length) {
		// Create workspace
		char *ws_name = workspace_next_name(wlr_output->name);
		sway_log(SWAY_DEBUG, "Creating default workspace %s", ws_name);
		ws = workspace_create(output, ws_name);
		// Set each seat's focus if not already set
		struct sway_seat *seat = NULL;
		wl_list_for_each(seat, &server.input->seats, link) {
			if (!seat->has_focus) {
				seat_set_focus_workspace(seat, ws);
			}
		}
		free(ws_name);
		ipc_event_workspace(NULL, ws, "init");
	}

	if (ws && config->default_orientation == L_NONE) {
		// Since the output transformation and resolution could have changed
		// due to applying the output config, the previously set layout for the
		// created workspace may not be correct for `default_orientation auto`
		layout_set_type(ws, output_get_default_layout(output));
	}

	wl_signal_emit_mutable(&root->events.new_node, &output->node);
}

static void evacuate_sticky(struct sway_workspace *old_ws,
		struct sway_output *new_output) {
	struct sway_workspace *new_ws = output_get_active_workspace(new_output);
	if (!sway_assert(new_ws, "New output does not have a workspace")) {
		return;
	}
	while(old_ws->floating->length) {
		struct sway_container *sticky = old_ws->floating->items[0];
		container_detach(sticky);
		workspace_add_floating(new_ws, sticky);
		container_handle_fullscreen_reparent(sticky);
		container_floating_move_to_center(sticky);
		ipc_event_window(sticky, "move");
	}
	workspace_detect_urgent(new_ws);
}

static void output_evacuate(struct sway_output *output) {
	if (!output->workspaces->length) {
		return;
	}
	struct sway_output *fallback_output = NULL;
	if (root->outputs->length > 0) {
		fallback_output = root->outputs->items[0];
	}

	while (output->workspaces->length) {
		struct sway_workspace *workspace = output->workspaces->items[0];

		workspace_detach(workspace);

		struct sway_output *new_output =
			workspace_output_get_highest_available(workspace);
		if (!new_output) {
			new_output = fallback_output;
		}
		if (!new_output) {
			new_output = root->fallback_output;
		}

		struct sway_workspace *new_output_ws =
			output_get_active_workspace(new_output);

		if (workspace_is_empty(workspace)) {
			// If the new output has an active workspace (the noop output may
			// not have one), move all sticky containers to it
			if (new_output_ws &&
					workspace_num_sticky_containers(workspace) > 0) {
				evacuate_sticky(workspace, new_output);
			}

			if (workspace_num_sticky_containers(workspace) == 0) {
				workspace_begin_destroy(workspace);
				continue;
			}
		}

		workspace_output_add_priority(workspace, new_output);
		output_add_workspace(new_output, workspace);
		output_sort_workspaces(new_output);
		ipc_event_workspace(NULL, workspace, "move");

		// If there is an old workspace (the noop output may not have one),
		// check to see if it is empty and should be destroyed.
		if (new_output_ws) {
			workspace_consider_destroy(new_output_ws);
		}
	}
}

void output_destroy(struct sway_output *output) {
	if (!sway_assert(output->node.destroying,
				"Tried to free output which wasn't marked as destroying")) {
		return;
	}
	if (!sway_assert(output->wlr_output == NULL,
				"Tried to free output which still had a wlr_output")) {
		return;
	}
	if (!sway_assert(output->node.ntxnrefs == 0, "Tried to free output "
				"which is still referenced by transactions")) {
		return;
	}

	destroy_scene_layers(output);
	list_free(output->workspaces);
	list_free(output->current.workspaces);
	list_free_items_and_destroy(output->scroller_options.heights);
	list_free_items_and_destroy(output->scroller_options.widths);
	wlr_color_transform_unref(output->color_transform);
	free(output);
}

void output_disable(struct sway_output *output) {
	if (!sway_assert(output->enabled, "Expected an enabled output")) {
		return;
	}
	int index = list_find(root->outputs, output);
	if (!sway_assert(index >= 0, "Output not found in root node")) {
		return;
	}

	sway_log(SWAY_DEBUG, "Disabling output '%s'", output->wlr_output->name);

	// Remove the output now to avoid interacting with it during e.g.,
	// transactions, as the output might be physically removed with the scene
	// output destroyed.
	list_del(root->outputs, index);
	output->enabled = false;

	destroy_layers(output);
	output_evacuate(output);
}

void output_begin_destroy(struct sway_output *output) {
	if (!sway_assert(!output->enabled, "Expected a disabled output")) {
		return;
	}
	sway_log(SWAY_DEBUG, "Destroying output '%s'", output->wlr_output->name);
	wl_signal_emit_mutable(&output->node.events.destroy, &output->node);

	node_set_dirty(&output->node);
	output->node.destroying = true;
}

struct sway_output *output_from_wlr_output(struct wlr_output *output) {
	return output->data;
}

struct sway_output *output_get_in_direction(struct sway_output *reference,
		enum wlr_direction direction) {
	if (!sway_assert(direction, "got invalid direction: %d", direction)) {
		return NULL;
	}
	struct wlr_box output_box;
	wlr_output_layout_get_box(root->output_layout, reference->wlr_output, &output_box);
	int lx = output_box.x + output_box.width / 2;
	int ly = output_box.y + output_box.height / 2;
	struct wlr_output *wlr_adjacent = wlr_output_layout_adjacent_output(
			root->output_layout, direction, reference->wlr_output, lx, ly);
	if (!wlr_adjacent) {
		return NULL;
	}
	return output_from_wlr_output(wlr_adjacent);
}

void output_add_workspace(struct sway_output *output,
		struct sway_workspace *workspace) {
	if (workspace->output) {
		workspace_detach(workspace);
	}
	list_add(output->workspaces, workspace);
	workspace->output = output;
	node_set_dirty(&output->node);
	node_set_dirty(&workspace->node);
}

void output_for_each_workspace(struct sway_output *output,
		void (*f)(struct sway_workspace *ws, void *data), void *data) {
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct sway_workspace *workspace = output->workspaces->items[i];
		f(workspace, data);
	}
}

void output_for_each_container(struct sway_output *output,
		void (*f)(struct sway_container *con, void *data), void *data) {
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct sway_workspace *workspace = output->workspaces->items[i];
		workspace_for_each_container(workspace, f, data);
	}
}

struct sway_workspace *output_find_workspace(struct sway_output *output,
		bool (*test)(struct sway_workspace *ws, void *data), void *data) {
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct sway_workspace *workspace = output->workspaces->items[i];
		if (test(workspace, data)) {
			return workspace;
		}
	}
	return NULL;
}

struct sway_container *output_find_container(struct sway_output *output,
		bool (*test)(struct sway_container *con, void *data), void *data) {
	struct sway_container *result = NULL;
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct sway_workspace *workspace = output->workspaces->items[i];
		if ((result = workspace_find_container(workspace, test, data))) {
			return result;
		}
	}
	return NULL;
}

static int sort_workspace_cmp_qsort(const void *_a, const void *_b) {
	struct sway_workspace *a = *(void **)_a;
	struct sway_workspace *b = *(void **)_b;

	if (isdigit(a->name[0]) && isdigit(b->name[0])) {
		int a_num = strtol(a->name, NULL, 10);
		int b_num = strtol(b->name, NULL, 10);
		return (a_num < b_num) ? -1 : (a_num > b_num);
	} else if (isdigit(a->name[0])) {
		return -1;
	} else if (isdigit(b->name[0])) {
		return 1;
	}
	return 0;
}

void output_sort_workspaces(struct sway_output *output) {
	list_stable_sort(output->workspaces, sort_workspace_cmp_qsort);
}

void output_get_box(struct sway_output *output, struct wlr_box *box) {
	box->x = output->lx;
	box->y = output->ly;
	box->width = output->width;
	box->height = output->height;
}

static void handle_destroy_non_desktop(struct wl_listener *listener, void *data) {
	struct sway_output_non_desktop *output =
		wl_container_of(listener, output, destroy);

	sway_log(SWAY_DEBUG, "Destroying non-desktop output '%s'", output->wlr_output->name);

	int index = list_find(root->non_desktop_outputs, output);
	list_del(root->non_desktop_outputs, index);

	wl_list_remove(&output->destroy.link);

	free(output);
}

struct sway_output_non_desktop *output_non_desktop_create(
		struct wlr_output *wlr_output) {
	struct sway_output_non_desktop *output =
		calloc(1, sizeof(struct sway_output_non_desktop));

	output->wlr_output = wlr_output;

	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	output->destroy.notify = handle_destroy_non_desktop;

	return output;
}

enum sway_container_layout output_get_default_layout(
		struct sway_output *output) {
	if (output->scroller_options.type != L_NONE) {
		return output->scroller_options.type;
	}
	if (config->default_orientation != L_NONE) {
		return config->default_orientation;
	}
	if (output->height > output->width) {
		return L_VERT;
	}
	return L_HORIZ;
}
