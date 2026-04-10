#include "debug.h"
#include "log.h"
#include <wlr/types/wlr_scene.h>
#include "sway/tree/view.h"
#include "sway/scene_descriptor.h"

void wlr_scene_node_debug_print_info(struct wlr_scene_node *node, double x, double y) {
	bool enabled = true;
	if (enabled) {
		static const char *names[5] = { "TREE", "RECT", "BUFFER", "DECORATION", "SHADOW" };
		sway_log(SWAY_INFO, "Node type %s %g %g", names[node->type], x, y);
		// Debug graph
		if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_BUFFER_TIMER)) {
			sway_log(SWAY_INFO, "Node %g %g TIMER", x, y);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_NON_INTERACTIVE)) {
			sway_log(SWAY_INFO, "Node %g %g NON_INTERACTIVE", x, y);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_CONTAINER)) {
			sway_log(SWAY_INFO, "Node %g %g CONTAINER", x, y);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_VIEW)) {
			struct sway_view *view = scene_descriptor_try_get(node, SWAY_SCENE_DESC_VIEW);
			float scale = -1.0f;
			if (view) {
				if (view_is_content_scaled(view)) {
					scale = view_get_content_scale(view);
				}
			}
			sway_log(SWAY_INFO, "Node %g %g VIEW scale %f", x, y, scale);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_LAYER_SHELL)) {
			sway_log(SWAY_INFO, "Node %g %g LAYER_SHELL", x, y);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_XWAYLAND_UNMANAGED)) {
			sway_log(SWAY_INFO, "Node %g %g XWAYLAND", x, y);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_POPUP)) {
			struct sway_popup_desc *desc = scene_descriptor_try_get(node, SWAY_SCENE_DESC_POPUP);
			float scale = -1.0f;
			if (desc && desc->view) {
				if (view_is_content_scaled(desc->view)) {
					scale = view_get_content_scale(desc->view);
				}
			}
			sway_log(SWAY_INFO, "Node %g %g DESC_POPUP scale %f", x, y, scale);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_DRAG_ICON)) {
			sway_log(SWAY_INFO, "Node %g %g DRAG_ICON", x, y);
		}
	}
}

void wlr_scene_node_recurse_debug_print_info(struct wlr_scene_node *node) {
	if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each_reverse(child, &scene_tree->children, link) {
			wlr_scene_node_recurse_debug_print_info(child);
		}
		return;
	}
	if (!node->enabled) {
		return;
	}
	wlr_scene_node_debug_print_info(node, node->x, node->y);
}
