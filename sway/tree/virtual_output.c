#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_ext_workspace_v1.h>
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/node.h"
#include "sway/tree/root.h"
#include "sway/tree/virtual_output.h"
#include "sway/tree/workspace.h"
#include "sway/tree/layout.h"
#include "list.h"

static int virtual_output_next_id = 0;

static struct sway_virtual_output *virtual_output_create(
		struct sway_output *output) {
	struct sway_virtual_output *vo = calloc(1, sizeof(*vo));
	if (!vo) {
		return NULL;
	}
	vo->output = output;
	vo->workspaces = create_list();
	if (!vo->workspaces) {
		free(vo);
		return NULL;
	}
	char name[256];
	snprintf(name, sizeof(name), "%s-VO%d",
		output->wlr_output ? output->wlr_output->name : "OUTPUT",
		++virtual_output_next_id);
	vo->name = strdup(name);
	list_add(output->virtual_outputs, vo);
	return vo;
}

static void virtual_output_free(struct sway_virtual_output *vo) {
	free(vo->name);
	list_free(vo->workspaces);
	free(vo);
}

static void virtual_output_remove(struct sway_virtual_output *vo) {
	struct sway_output *output = vo->output;
	if (output && output->virtual_outputs) {
		int index = list_find(output->virtual_outputs, vo);
		if (index != -1) {
			list_del(output->virtual_outputs, index);
		}
	}
	virtual_output_free(vo);
}

void output_virtual_outputs_create(struct sway_output *output) {
	output->virtual_outputs = create_list();
	if (!output->virtual_outputs) {
		return;
	}
	output->root_virtual_output = virtual_output_create(output);
}

void output_virtual_outputs_destroy(struct sway_output *output) {
	if (!output->virtual_outputs) {
		return;
	}
	// Workspaces should have been detached already, but make sure no scene
	// node keeps a pointer to any of the virtual outputs we are freeing
	for (int i = 0; i < output->virtual_outputs->length; ++i) {
		struct sway_virtual_output *vo = output->virtual_outputs->items[i];
		for (int j = 0; j < vo->workspaces->length; ++j) {
			struct sway_workspace *ws = vo->workspaces->items[j];
			if (ws->virtual_output == vo) {
				ws->virtual_output = NULL;
				if (ws->layers.tiling) {
					ws->layers.tiling->node.info.output_box = NULL;
				}
			}
		}
		virtual_output_free(vo);
	}
	list_free(output->virtual_outputs);
	output->virtual_outputs = NULL;
	output->root_virtual_output = NULL;
}

struct sway_virtual_output *output_virtual_outputs_get_default(
		struct sway_output *output) {
	if (!output || !output->root_virtual_output) {
		return NULL;
	}
	struct sway_virtual_output *vo = output->root_virtual_output;
	while (vo->children[0]) {
		vo = vo->children[0];
	}
	return vo;
}

struct sway_virtual_output *output_virtual_outputs_get_active(
		struct sway_output *output) {
	if (!output || !output->root_virtual_output) {
		return NULL;
	}
	if (server.input) {
		struct sway_workspace *ws = output_get_active_workspace(output);
		if (ws && ws->virtual_output && ws->virtual_output->output == output) {
			return ws->virtual_output;
		}
	}
	return output_virtual_outputs_get_default(output);
}

struct sway_virtual_output *virtual_output_by_name(const char *name) {
	if (!name) {
		return NULL;
	}
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		if (!output->virtual_outputs) {
			continue;
		}
		for (int j = 0; j < output->virtual_outputs->length; ++j) {
			struct sway_virtual_output *vo = output->virtual_outputs->items[j];
			if (vo->children[0] == NULL && vo->name &&
					strcasecmp(vo->name, name) == 0) {
				return vo;
			}
		}
	}
	return NULL;
}

bool virtual_output_is_visible(struct sway_virtual_output *vo) {
	if (!vo || !vo->output || vo->children[0]) {
		return false;
	}
	if (!vo->output->maximize_virtual_output) {
		return true;
	}
	return vo == output_virtual_outputs_get_active(vo->output);
}

struct sway_workspace *virtual_output_get_active_workspace(
		struct sway_virtual_output *vo) {
	if (!vo) {
		return NULL;
	}
	struct sway_seat *seat = server.input ? input_manager_current_seat() : NULL;
	if (seat) {
		struct sway_seat_node *current;
		wl_list_for_each(current, &seat->focus_stack, link) {
			struct sway_node *node = current->node;
			struct sway_workspace *ws = NULL;
			if (!node) {
				continue;
			}
			if (node->type == N_WORKSPACE) {
				ws = node->sway_workspace;
			} else if (node->type == N_CONTAINER) {
				ws = node->sway_container->pending.workspace;
			}
			if (ws && !ws->node.destroying && ws->virtual_output == vo) {
				return ws;
			}
		}
	}
	if (vo->active_workspace && !vo->active_workspace->node.destroying &&
			list_find(vo->workspaces, vo->active_workspace) != -1) {
		return vo->active_workspace;
	}
	return vo->workspaces->length ? vo->workspaces->items[0] : NULL;
}

bool output_has_multiple_visible_virtual_outputs(struct sway_output *output) {
	if (!output || !output->virtual_outputs) {
		return false;
	}
	int visible = 0;
	for (int i = 0; i < output->virtual_outputs->length; ++i) {
		struct sway_virtual_output *vo = output->virtual_outputs->items[i];
		if (virtual_output_is_visible(vo)) {
			++visible;
		}
	}
	return visible > 1;
}

bool output_fullscreen_needs_background(struct sway_output *output) {
	if (!output_has_multiple_visible_virtual_outputs(output)) {
		return false;
	}
	for (int i = 0; i < output->virtual_outputs->length; ++i) {
		struct sway_virtual_output *vo = output->virtual_outputs->items[i];
		if (virtual_output_is_visible(vo)) {
			struct sway_workspace *ws =	virtual_output_get_active_workspace(vo);
			if (ws && ws->current.fullscreen) {
				return true;
			}
		}
	}
	return false;
}

static void virtual_output_compute(struct sway_virtual_output *vo,
		struct wlr_box area, struct wlr_box usable, struct sway_output *output) {
	vo->split_area = area;
	vo->output_area = area;
	vo->usable_area = usable;
	if (!vo->children[0] || !vo->children[1]) {
		return;
	}

	const int gap_w = vo->gap / 2;
	const int gap_left = usable.x - (area.x - output->lx);
	const int gap_right = (area.x - output->lx + area.width) - (usable.x + usable.width);
	const int gap_top = usable.y - (area.y - output->ly);
	const int gap_bottom = (area.y - output->ly + area.height) - (usable.y + usable.height);

	struct wlr_box area_0 = area, area_1 = area;
	struct wlr_box usable_0 = usable, usable_1 = usable;
	if (vo->split == VIRTUAL_OUTPUT_SPLIT_VERTICAL) {
		int area_w = round(vo->fraction * area.width);
		area_0.width = area_w - gap_w;
		area_1.x = area.x + area_w - gap_w + vo->gap;
		area_1.width = area.width - area_w - vo->gap + gap_w;
		usable_0.width = area_0.width - gap_left;
		usable_1.x = area_1.x - output->lx;
		usable_1.width = area_1.width - gap_right;
	} else {
		int area_h = round(vo->fraction * area.height);
		area_0.height = area_h - gap_w;
		area_1.y = area.y + area_h - gap_w + vo->gap;
		area_1.height = area.height - area_h - vo->gap + gap_w;
		usable_0.height = area_0.height - gap_top;
		usable_1.y = area_1.y - output->ly;
		usable_1.height = area_1.height - gap_bottom;
	}
	if (area_0.width < 0) {
		area_0.width = 0;
	}
	if (area_0.height < 0) {
		area_0.height = 0;
	}
	if (area_1.width < 0) {
		area_1.width = 0;
	}
	if (area_1.height < 0) {
		area_1.height = 0;
	}
	if (usable_0.width < 0) {
		usable_0.width = 0;
	}
	if (usable_0.height < 0) {
		usable_0.height = 0;
	}
	if (usable_1.width < 0) {
		usable_1.width = 0;
	}
	if (usable_1.height < 0) {
		usable_1.height = 0;
	}

	virtual_output_compute(vo->children[0], area_0, usable_0, output);
	virtual_output_compute(vo->children[1], area_1, usable_1, output);
}

void output_virtual_outputs_update_geometry(struct sway_output *output) {
	if (!output || !output->root_virtual_output) {
		return;
	}
	struct wlr_box area = {
		.x = output->lx,
		.y = output->ly,
		.width = output->width,
		.height = output->height,
	};
	virtual_output_compute(output->root_virtual_output, area,
			output->usable_area, output);

	if (output->maximize_virtual_output) {
		struct sway_virtual_output *active = output_virtual_outputs_get_active(output);
		if (active && active->children[0] == NULL) {
			active->output_area = area;
			active->usable_area = output->usable_area;
		}
	}
}

static int virtual_output_sort_workspace_cmp(const void *_a, const void *_b) {
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

void virtual_output_sort_workspaces(struct sway_virtual_output *vo) {
	if (vo && vo->workspaces->length > 1) {
		list_stable_sort(vo->workspaces, virtual_output_sort_workspace_cmp);
	}
}

void virtual_output_attach_workspace(struct sway_virtual_output *vo,
		struct sway_workspace *ws) {
	if (!vo || !ws) {
		return;
	}
	if (ws->virtual_output && ws->virtual_output != vo) {
		virtual_output_detach_workspace(ws);
	}
	if (list_find(vo->workspaces, ws) == -1) {
		list_add(vo->workspaces, ws);
	}
	ws->virtual_output = vo;
	if (!vo->active_workspace) {
		vo->active_workspace = ws;
	}
	virtual_output_sort_workspaces(vo);
	if (ws->layers.tiling) {
		// Cover for the fallback output, which doesn't have geometry
		ws->layers.tiling->node.info.output_box =
			(vo->output_area.width > 0 && vo->output_area.height > 0) ?
			&vo->output_area : NULL;
	}
}

void virtual_output_detach_workspace(struct sway_workspace *ws) {
	if (!ws || !ws->virtual_output) {
		return;
	}
	struct sway_virtual_output *vo = ws->virtual_output;
	int index = list_find(vo->workspaces, ws);
	if (index != -1) {
		list_del(vo->workspaces, index);
	}
	ws->virtual_output = NULL;
	if (vo->active_workspace == ws) {
		vo->active_workspace = vo->workspaces->length ? vo->workspaces->items[0] : NULL;
	}
	if (ws->layers.tiling) {
		ws->layers.tiling->node.info.output_box = NULL;
	}
}

void virtual_output_set_workspace(struct sway_workspace *ws,
		struct sway_virtual_output *vo) {
	if (!ws || !vo || ws->virtual_output == vo) {
		return;
	}
	if (!ws->output || ws->output != vo->output) {
		output_add_workspace(vo->output, ws);
	}
	virtual_output_detach_workspace(ws);
	virtual_output_attach_workspace(vo, ws);
	output_virtual_outputs_update_geometry(vo->output);
	arrange_workspace(ws);
	node_set_dirty(&ws->node);
	node_set_dirty(&vo->output->node);
	layout_overview_workspaces_recompute_scale();
}

struct sway_workspace *virtual_output_create_workspace(
		struct sway_virtual_output *vo) {
	if (!vo || !vo->output) {
		return NULL;
	}
	char *name = workspace_next_name(vo->output->wlr_output->name);
	if (!name) {
		return NULL;
	}
	struct sway_workspace *ws = workspace_create(vo->output, name);
	free(name);
	if (ws) {
		virtual_output_set_workspace(ws, vo);
	}
	return ws;
}

void output_virtual_outputs_arrange(struct sway_output *output) {
	if (!output) {
		return;
	}
	output_virtual_outputs_update_geometry(output);
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct sway_workspace *ws = output->workspaces->items[i];
		if (ws->virtual_output && ws->layers.tiling) {
			struct wlr_box *area = &ws->virtual_output->output_area;
			ws->layers.tiling->node.info.output_box =
				(area->width > 0 && area->height > 0) ? area : NULL;
		}
		arrange_workspace(ws);
	}
	node_set_dirty(&output->node);
	layout_overview_workspaces_recompute_scale();
	if (output->enabled && output->scene_output) {
		output_damage_whole(output);
	}
}

void virtual_output_split(struct sway_virtual_output *vo,
		enum sway_virtual_output_split direction, double fraction, int gap) {
	if (!vo || !vo->output || vo->children[0] ||
			(direction != VIRTUAL_OUTPUT_SPLIT_HORIZONTAL &&
			 direction != VIRTUAL_OUTPUT_SPLIT_VERTICAL)) {
		return;
	}
	struct sway_output *output = vo->output;
	struct sway_virtual_output *node = virtual_output_create(output);
	struct sway_virtual_output *child = virtual_output_create(output);
	if (!node || !child) {
		if (node) {
			virtual_output_remove(node);
		}
		if (child) {
			virtual_output_remove(child);
		}
		return;
	}

	// The leaf becomes the first child of a new split node, which takes its
	// place in the tree. The new virtual output becomes the second child
	node->parent = vo->parent;
	if (vo->parent) {
		if (vo->parent->children[0] == vo) {
			vo->parent->children[0] = node;
		} else {
			vo->parent->children[1] = node;
		}
	} else {
		output->root_virtual_output = node;
	}
	node->children[0] = vo;
	node->children[1] = child;
	node->split = direction;
	node->fraction = fraction;
	node->gap = gap;
	vo->parent = node;
	child->parent = node;

	// A virtual output needs to have at least one workspace
	virtual_output_create_workspace(child);
	output_virtual_outputs_arrange(output);
}

static void virtual_output_merge(struct sway_virtual_output *node,
		struct sway_virtual_output *survivor) {
	if (node == survivor) {
		return;
	}
	while (node->workspaces->length) {
		struct sway_workspace *ws = node->workspaces->items[0];
		bool focused = false;
		struct sway_seat *seat = server.input ? input_manager_current_seat() : NULL;
		if (seat) {
			focused = seat_get_focused_workspace(seat) == ws;
		}
		if (workspace_is_empty(ws) && !focused) {
			workspace_begin_destroy(ws);
		} else {
			virtual_output_detach_workspace(ws);
			virtual_output_attach_workspace(survivor, ws);
		}
		if (node->workspaces->length && node->workspaces->items[0] == ws) {
			int index = list_find(node->workspaces, ws);
			list_del(node->workspaces, index);
			ws->virtual_output = NULL;
			if (ws->layers.tiling) {
				ws->layers.tiling->node.info.output_box = NULL;
			}
		}
	}
	struct sway_virtual_output *first = node->children[0];
	struct sway_virtual_output *second = node->children[1];
	node->children[0] = node->children[1] = NULL;
	if (first) {
		virtual_output_merge(first, survivor);
	}
	if (second) {
		virtual_output_merge(second, survivor);
	}
	virtual_output_remove(node);
}

void virtual_output_split_reset(struct sway_virtual_output *vo) {
	if (!vo || !vo->parent || !vo->output) {
		return;
	}
	struct sway_output *output = vo->output;
	struct sway_virtual_output *node = vo->parent;
	struct sway_virtual_output *grandparent = node->parent;

	// Replace the split node with the surviving leaf
	if (grandparent) {
		if (grandparent->children[0] == node) {
			grandparent->children[0] = vo;
		} else {
			grandparent->children[1] = vo;
		}
	} else {
		output->root_virtual_output = vo;
	}
	vo->parent = grandparent;

	virtual_output_merge(node, vo);
	output_virtual_outputs_arrange(output);
}

void virtual_output_toggle_maximize(struct sway_output *output) {
	if (!output) {
		return;
	}
	output->maximize_virtual_output = !output->maximize_virtual_output;
	output_virtual_outputs_arrange(output);
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct sway_workspace *ws = output->workspaces->items[i];
		if (ws->ext_workspace) {
			wlr_ext_workspace_handle_v1_set_active(ws->ext_workspace,
					workspace_is_visible(ws));
		}
	}
}

struct sway_virtual_output *virtual_output_get_in_direction(
		struct sway_virtual_output *from, enum wlr_direction direction) {
	if (!from || !from->output || !from->output->virtual_outputs) {
		return NULL;
	}
	const double ref_x = from->split_area.x + from->split_area.width / 2.0;
	const double ref_y = from->split_area.y + from->split_area.height / 2.0;
	struct sway_virtual_output *best = NULL;
	double best_distance = DBL_MAX;
	for (int i = 0; i < from->output->virtual_outputs->length; ++i) {
		struct sway_virtual_output *vo = from->output->virtual_outputs->items[i];
		if (vo == from || vo->children[0]) {
			continue;
		}
		double dx = vo->split_area.x + vo->split_area.width / 2.0 - ref_x;
		double dy = vo->split_area.y + vo->split_area.height / 2.0 - ref_y;
		double distance;
		switch (direction) {
		case WLR_DIRECTION_LEFT:
			if (dx >= 0 || fabs(dx) < fabs(dy)) { continue; }
			distance = -dx + fabs(dy) * 0.5;
			break;
		case WLR_DIRECTION_RIGHT:
			if (dx <= 0 || fabs(dx) < fabs(dy)) { continue; }
			distance = dx + fabs(dy) * 0.5;
			break;
		case WLR_DIRECTION_UP:
			if (dy >= 0 || fabs(dy) < fabs(dx)) { continue; }
			distance = -dy + fabs(dx) * 0.5;
			break;
		case WLR_DIRECTION_DOWN:
			if (dy <= 0 || fabs(dy) < fabs(dx)) { continue; }
			distance = dy + fabs(dx) * 0.5;
			break;
		default:
			continue;
		}
		if (distance < best_distance) {
			best_distance = distance;
			best = vo;
		}
	}
	return best;
}

struct sway_virtual_output *output_virtual_output_at_coords(
		struct sway_output *output, double lx, double ly) {
	if (!output || !output->virtual_outputs) {
		return NULL;
	}
	for (int i = 0; i < output->virtual_outputs->length; ++i) {
		struct sway_virtual_output *vo = output->virtual_outputs->items[i];
		if (virtual_output_is_visible(vo) &&
				wlr_box_contains_point(&vo->output_area, lx, ly)) {
			return vo;
		}
	}
	return output_virtual_outputs_get_active(output);
}
