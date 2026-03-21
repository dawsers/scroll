#include "wlr/types/wlr_scene.h"
#include "sway/tree/root.h"
#include "sway/tree/layout.h"
#include "sway/output.h"
#include "sway/tree/workspace.h"
#include "sway/scene_descriptor.h"
#include "sway/desktop/animation.h"

static bool scene_fullscreen_global_enabled() {
	return root->fullscreen_global;	
}

static bool scene_overview_workspaces_enabled() {
	return layout_overview_workspaces_enabled();	
}

static bool scene_node_at(struct wlr_scene_node *node, double lx, double ly,
		struct wlr_scene_node_at_data *data) {
	struct wlr_output *wlr_output = wlr_scene_node_info_get_output(node);
	if (wlr_output) {
		struct sway_output *output = output_for_coords(data->lx, data->ly);
		if (output && wlr_output != output->wlr_output) {
			return false;
		}
	}
	struct wlr_box *box = wlr_scene_node_info_get_workspace_box(node);
	if (box) {
		if (data->lx < box->x || data->lx >= box->x + box->width ||
			data->ly < box->y || data->ly >= box->y + box->height) {
			return false;
		}
	}

	double rx = data->lx - lx;
	double ry = data->ly - ly;

	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

		double total_scale = 1.0;
		struct wlr_scene_surface *scene_surface =
			wlr_scene_surface_try_from_buffer(scene_buffer);

		if (scene_surface) {
			struct sway_view *view = view_from_wlr_surface(scene_surface->surface);
			if (view) {
				total_scale = view_get_total_scale(view);
				if (total_scale <= 0.0) {
					total_scale = 1.0;
				}
			}
		}

		// Correct the coordinates to buffer local from desktop logical
		rx /= total_scale;
		ry /= total_scale;
		if (scene_buffer->point_accepts_input &&
				!scene_buffer->point_accepts_input(scene_buffer, &rx, &ry)) {
			return false;
		}
	} else if (node->type == WLR_SCENE_NODE_DECORATION) {
		struct wlr_scene_decoration *scene_decoration = wlr_scene_decoration_from_node(node);
		struct sway_view *view = scene_decoration->view;
		struct sway_container *con = view->container;
		if (con) {
			double scale = view_get_total_scale(view);
			if (scale <= 0.0) {
				scale = 1.0;
			}
			const double cx = rx / scale + con->pending.x - con->pending.content_x;
			const double cy = ry / scale + con->pending.y - con->pending.content_y;
			if (cx > 0.0 && cx < con->pending.content_width &&
				cy > 0.0 && cy < con->pending.content_height) {
				return false;
			}
		}
	} else if (node->type == WLR_SCENE_NODE_SHADOW) {
		return false;
	}

	data->rx = rx;
	data->ry = ry;
	data->node = node;
	return true;
}

static void *scene_node_get_workspace(struct wlr_scene_node *node) {
	struct wlr_scene_tree *tree;
	if (node->type == WLR_SCENE_NODE_TREE) {
		tree = wlr_scene_tree_from_node(node);
	} else {
		tree = node->parent;
	}

	while (tree != NULL) {
		if (tree->node.info.workspace != NULL) {
			return tree->node.info.workspace;
		}
		tree = tree->node.parent;
	}
	return NULL;
}

static bool scene_workspace_data(struct wlr_scene_node *node, struct wlr_scene_workspace_data *data) {
	struct sway_workspace *workspace = scene_node_get_workspace(node);
	if (workspace) {
		data->x = workspace->jump.x;
		data->y = workspace->jump.y;
		data->width = workspace->jump.width;
		data->height = workspace->jump.height;
		data->scale = workspace->jump.scale;
		return true;
	}
	return false;
}

static bool scene_view_data(struct wlr_surface *surface, struct wlr_scene_view_data *data) {
	struct sway_view *view = view_from_wlr_surface(surface);
	data->radius_top = data->radius_bottom = 0.0f;
	if (view) {
		data->total_scale = view_get_total_scale(view);
		if (data->total_scale < 0.0) {
			data->total_scale = 1.0;
		}
		view_get_animation_scales(view, &data->wscale, &data->hscale);
		if (view->container && !container_is_fullscreen_or_child(view->container) &&
			view->container->pending.fullscreen_layout == FULLSCREEN_DISABLED) {
			if (!view->container->decoration.full->title_bar) {
				data->radius_top = view->container->decoration.full->border_radius;
			}
			data->radius_bottom = view->container->decoration.full->border_radius;
		}
		return true;
	} else {
		data->total_scale = data->wscale = data->hscale = 1.0;
	}
	return false;
}

/**
 * Find a parent of the current node that is a popup or view. If it finds one,
 * fill scale (content scale and workspace scale)
 * Returns: true if the current node is a popup or view (parent view), else false
 * (children surfaces or popups)
 */
static bool scene_node_get_parent_total_scale(struct wlr_scene_node *node, double *scale) {
	struct wlr_scene_tree *tree;
	if (node->type == WLR_SCENE_NODE_TREE) {
		tree = wlr_scene_tree_from_node(node);
	} else {
		tree = node->parent;
	}

	while (tree) {
		// Check scene descriptor
		struct sway_view *view = scene_descriptor_try_get(&tree->node, SWAY_SCENE_DESC_VIEW);
		if (view && view->container) {
			*scale = view_get_total_scale(view);
			return &tree->node == node;
		}
		struct sway_popup_desc *desc = scene_descriptor_try_get(&tree->node, SWAY_SCENE_DESC_POPUP);
		if (desc && desc->view) {
			*scale = view_get_total_scale(desc->view);
			return &tree->node == node;
		}
		tree = tree->node.parent;
	}
	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
		double total_scale = -1.0;
		struct wlr_scene_surface *scene_surface =
			wlr_scene_surface_try_from_buffer(scene_buffer);

		if (scene_surface) {
			struct sway_view *view = view_from_wlr_surface(scene_surface->surface);
			if (view) {
				total_scale = view_get_total_scale(view);
			}
		}
		*scale = total_scale;
		return true;
	}
	*scale = -1.0;
	return false;
}

static void animate(struct wlr_output *output) {
	if (animation_animating(output)) {
		animation_animate(output);
	}
}

const struct wlr_scene_callbacks scroll_scene_cbs = {
	.fullscreen_global_enabled = scene_fullscreen_global_enabled,
	.overview_workspaces_enabled = scene_overview_workspaces_enabled,
	.node_at = scene_node_at,
	.workspace_data = scene_workspace_data,
	.view_data = scene_view_data,
	.node_get_parent_total_scale = scene_node_get_parent_total_scale,
	.animate = animate,
};
