#ifndef _SWAY_LAYERS_H
#define _SWAY_LAYERS_H
#include <stdbool.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include "sway/tree/view.h"

struct sway_layer_surface_state {
	double x, y;
	double width, height;
};

struct sway_layer_surface {
	struct sway_node node;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener surface_commit;
	struct wl_listener node_destroy;
	struct wl_listener new_popup;

	bool mapped;

	struct wlr_scene_tree *popups;
	struct sway_popup_desc desc;

	struct sway_output *output;
	struct wl_list link; // sway_output.layer_surfaces

	struct wlr_scene_layer_surface_v1 *scene;
	struct wlr_scene_tree *tree;
	struct wlr_layer_surface_v1 *layer_surface;

	list_t *layer_popups;
	struct sway_layer_surface_state current;
	struct sway_layer_surface_state pending;

	// Animation variables
	struct {
		double x0, y0, w0, h0;
		double xt, yt, wt, ht;
		double w1, h1;
	} animation;
};

struct sway_layer_popup {
	struct sway_node node;
	struct wlr_xdg_popup *wlr_popup;
	struct wlr_scene_tree *scene;
	struct sway_layer_surface *toplevel;

	struct wl_listener destroy;
	struct wl_listener new_popup;
	struct wl_listener commit;
	struct wl_listener reposition;

	struct sway_layer_surface_state current;
	struct sway_layer_surface_state pending;

	// Animation variables
	struct {
		double x0, y0, w0, h0;
		double xt, yt, wt, ht;
		double w1, h1;
	} animation;
};

struct sway_output;

struct wlr_layer_surface_v1 *toplevel_layer_surface_from_surface(
		struct wlr_surface *surface);

void arrange_layers(struct sway_output *output);

void destroy_layers(struct sway_output *output);

void layer_surface_get_box(struct sway_layer_surface *surface, struct wlr_box *box);

void layer_popup_get_box(struct sway_layer_popup *popup, struct wlr_box *box);

#endif
