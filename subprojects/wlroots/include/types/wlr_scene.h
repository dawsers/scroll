#ifndef TYPES_WLR_SCENE_H
#define TYPES_WLR_SCENE_H

#include <wlr/types/wlr_scene.h>

struct wlr_scene *scene_node_get_root(struct wlr_scene_node *node);

bool scene_node_get_extents(struct wlr_scene_node *node, struct wlr_box *box);

void scene_node_get_size(struct wlr_scene_node *node, double *width, double *height);

typedef bool (*scene_node_box_iterator_func_t)(struct wlr_scene_node *node,
	double sx, double sy, void *data);

bool scene_nodes_in_box(struct wlr_scene_node *node, struct wlr_box *box,
	scene_node_box_iterator_func_t iterator, void *user_data);

void scene_surface_set_clip(struct wlr_scene_surface *surface, struct wlr_box *clip);

#endif
