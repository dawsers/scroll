#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/output.h"
#include "sway/tree/workspace.h"
#include "sway/tree/view.h"
#include "sway/tree/layout.h"
#include "list.h"
#include "log.h"
#include "util.h"

// For us, gaps_inner is applied to each container on both sides, regardless
// of its position. So an edge container will have the same content size
// than an inner container. Otherwise, moving containers may produce a content
// resize, which is annoying in some applications. i3wm and sway divide the
// total gap by the number of containers, so each has the same gap, but we
// cannot do this with scroll, or the content would be resized if we add or
// remove containers.
static void apply_horiz_layout(list_t *children, struct sway_container *active, struct wlr_box *box) {
	if (!children->length) {
		return;
	}

	// Calculate gap size
	double inner_gap = 0;
	struct sway_container *child = children->items[0];
	struct sway_workspace *ws = child->pending.workspace;
	struct sway_container *parent = child->pending.parent;
	if (ws) {
		inner_gap = ws->gaps_inner;
	}
	double height = parent ? parent->height_fraction : layout_get_default_height(ws);

	// Resize windows
	// box has already applied outer and inner gaps
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *child = children->items[i];
		if (child->pending.fullscreen_layout == FULLSCREEN_ENABLED && ws) {
			const double w = ws->output->width;
			const double h = ws->output->height;
			child->pending.width = w;
			child->pending.height = h;
			if (parent) {
				parent->pending.width = w;
				parent->pending.height = h;
			}
			continue;
		}
		child->pending.width = child->width_fraction * box->width - 2 * inner_gap;
		if (parent) {
			child->pending.height = height * box->height - 2 * inner_gap;
		} else {
			child->pending.height = child->height_fraction * box->height - 2 * inner_gap;
		}
	}
}

static void apply_vert_layout(list_t *children, struct sway_container *active, struct wlr_box *box) {
	if (!children->length) {
		return;
	}

	// Calculate gap size
	double inner_gap = 0;
	struct sway_container *child = children->items[0];
	struct sway_workspace *ws = child->pending.workspace;
	struct sway_container *parent = child->pending.parent;
	if (ws) {
		inner_gap = ws->gaps_inner;
	}
	double width = parent ? parent->width_fraction : layout_get_default_width(ws);

	// Resize windows
	// box has already applied outer and inner gaps
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *child = children->items[i];
		if (child->pending.fullscreen_layout == FULLSCREEN_ENABLED && ws) {
			const double w = ws->output->width;
			const double h = ws->output->height;
			child->pending.width = w;
			child->pending.height = h;
			if (parent) {
				parent->pending.width = w;
				parent->pending.height = h;
			}
			continue;
		}
		child->pending.height = child->height_fraction * box->height - 2 * inner_gap;
		if (parent) {
			child->pending.width = width * box->width - 2 * inner_gap;
		} else {
			child->pending.width = child->width_fraction * box->width - 2 * inner_gap;
		}
	}
}

static void arrange_floating(list_t *floating) {
	for (int i = 0; i < floating->length; ++i) {
		struct sway_container *floater = floating->items[i];
		arrange_container(floater);
	}
}

static void arrange_children(list_t *children, struct sway_container *active,
		enum sway_container_layout layout, struct wlr_box *parent) {
	// Calculate x, y, width and height of children
	switch (layout) {
	case L_HORIZ:
		apply_horiz_layout(children, active, parent);
		break;
	case L_VERT:
		apply_vert_layout(children, active, parent);
		break;
	case L_NONE:
		apply_horiz_layout(children, active, parent);
		break;
	}

	// Recurse into child containers
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *child = children->items[i];
		arrange_container(child);
	}
}

void arrange_container(struct sway_container *container) {
	if (config->reloading) {
		return;
	}
	if (container->view) {
		view_autoconfigure(container->view);
		node_set_dirty(&container->node);
		return;
	}
	if (container->pending.children->length == 0) {
		return;
	}
	struct wlr_box box;
	workspace_get_box(container->pending.workspace, &box);
	// Keep workspace width/height because our sizes are a fraction of that,
	// but we need the correct coordinates of the parent
	box.x = container->pending.x;
	box.y = container->pending.y;
	struct sway_container *active = container->pending.focused_inactive_child ?
		container->pending.focused_inactive_child : container->current.focused_inactive_child;
	arrange_children(container->pending.children, active, container->pending.layout, &box);
	node_set_dirty(&container->node);
}

void arrange_workspace(struct sway_workspace *workspace) {
	if (config->reloading) {
		return;
	}
	if (!workspace->output) {
		// Happens when there are no outputs connected
		return;
	}
	struct sway_output *output = workspace->output;
	struct wlr_box *area = &output->usable_area;
	sway_log(SWAY_DEBUG, "Usable area for ws: %dx%d@%d,%d",
			area->width, area->height, area->x, area->y);

	bool first_arrange = workspace->width == 0 && workspace->height == 0;
	struct wlr_box prev_box;
	workspace_get_box(workspace, &prev_box);

	double prev_x = workspace->x - workspace->current_gaps.left;
	double prev_y = workspace->y - workspace->current_gaps.top;
	workspace->width = area->width;
	workspace->height = area->height;
	workspace->x = output->lx + area->x;
	workspace->y = output->ly + area->y;

	// Adjust any floating containers
	double diff_x = workspace->x - prev_x;
	double diff_y = workspace->y - prev_y;
	if (!first_arrange && (diff_x != 0 || diff_y != 0)) {
		for (int i = 0; i < workspace->floating->length; ++i) {
			struct sway_container *floater = workspace->floating->items[i];
			struct wlr_box workspace_box;
			workspace_get_box(workspace, &workspace_box);
			floating_fix_coordinates(floater, &prev_box, &workspace_box);
			// Set transformation for scratchpad windows.
			if (floater->scratchpad) {
				struct wlr_box output_box;
				output_get_box(output, &output_box);
				floater->transform = output_box;
			}
		}
	}

	workspace_add_gaps(workspace);
	node_set_dirty(&workspace->node);
	sway_log(SWAY_DEBUG, "Arranging workspace '%s' at %f, %f", workspace->name,
			workspace->x, workspace->y);
	if (workspace->fullscreen) {
		struct sway_container *fs = workspace->fullscreen;
		fs->pending.x = output->lx;
		fs->pending.y = output->ly;
		fs->pending.width = output->width;
		fs->pending.height = output->height;
		arrange_container(fs);
	} else {
		struct wlr_box box;
		workspace_get_box(workspace, &box);
		arrange_children(workspace->tiling, workspace->current.focused_inactive_child, layout_get_type(workspace), &box);
		arrange_floating(workspace->floating);
	}
}

void arrange_output(struct sway_output *output) {
	if (config->reloading) {
		return;
	}
	if (!output->wlr_output->enabled) {
		return;
	}
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct sway_workspace *workspace = output->workspaces->items[i];
		arrange_workspace(workspace);
	}
}

void arrange_root(void) {
	if (config->reloading) {
		return;
	}
	struct wlr_box layout_box;
	wlr_output_layout_get_box(root->output_layout, NULL, &layout_box);
	root->x = layout_box.x;
	root->y = layout_box.y;
	root->width = layout_box.width;
	root->height = layout_box.height;

	if (root->fullscreen_global) {
		struct sway_container *fs = root->fullscreen_global;
		fs->pending.x = root->x;
		fs->pending.y = root->y;
		fs->pending.width = root->width;
		fs->pending.height = root->height;
		arrange_container(fs);
	} else {
		for (int i = 0; i < root->outputs->length; ++i) {
			struct sway_output *output = root->outputs->items[i];
			arrange_output(output);
		}
	}
}

void arrange_node(struct sway_node *node) {
	switch (node->type) {
	case N_ROOT:
		arrange_root();
		break;
	case N_OUTPUT:
		arrange_output(node->sway_output);
		break;
	case N_WORKSPACE:
		arrange_workspace(node->sway_workspace);
		break;
	case N_CONTAINER:
		arrange_container(node->sway_container);
		break;
	}
}
