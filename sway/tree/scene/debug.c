#include "debug.h"
#include "log.h"
#include <wlr/types/wlr_scene.h>
#include "sway/tree/view.h"
#include "sway/scene_descriptor.h"

void scene_node_debug_print_info(struct wlr_scene_node *node, int x, int y) {
	bool enabled = true;
	if (enabled) {
		static const char *names[3] = { "TREE", "RECT", "BUFFER" };
		sway_log(SWAY_INFO, "Node type %s %d %d", names[node->type], x, y);
		// Debug graph
		if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_BUFFER_TIMER)) {
			sway_log(SWAY_INFO, "Node %d %d TIMER", x, y);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_NON_INTERACTIVE)) {
			sway_log(SWAY_INFO, "Node %d %d NON_INTERACTIVE", x, y);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_CONTAINER)) {
			sway_log(SWAY_INFO, "Node %d %d CONTAINER", x, y);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_VIEW)) {
			struct sway_view *view = scene_descriptor_try_get(node, SWAY_SCENE_DESC_VIEW);
			float scale = -1.0f;
			if (view) {
				if (view_is_content_scaled(view)) {
					scale = view_get_content_scale(view);
				}
			}
			sway_log(SWAY_INFO, "Node %d %d VIEW scale %f", x, y, scale);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_LAYER_SHELL)) {
			sway_log(SWAY_INFO, "Node %d %d LAYER_SHELL", x, y);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_XWAYLAND_UNMANAGED)) {
			sway_log(SWAY_INFO, "Node %d %d XWAYLAND", x, y);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_POPUP)) {
			struct sway_popup_desc *desc = scene_descriptor_try_get(node, SWAY_SCENE_DESC_POPUP);
			float scale = -1.0f;
			if (desc && desc->view) {
				if (view_is_content_scaled(desc->view)) {
					scale = view_get_content_scale(desc->view);
				}
			}
			sway_log(SWAY_INFO, "Node %d %d DESC_POPUP scale %f", x, y, scale);
		} else if (scene_descriptor_try_get(node, SWAY_SCENE_DESC_DRAG_ICON)) {
			sway_log(SWAY_INFO, "Node %d %d DRAG_ICON", x, y);
		}
	}
}

