#ifndef _SWAY_SCENE_DEBUG_H
#define _SWAY_SCENE_DEBUG_H

struct wlr_scene_node;

void wlr_scene_node_debug_print_info(struct wlr_scene_node *node, double x, double y);
void wlr_scene_node_recurse_debug_print_info(struct wlr_scene_node *node);

#endif
