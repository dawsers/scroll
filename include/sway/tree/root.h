#ifndef _SWAY_ROOT_H
#define _SWAY_ROOT_H
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/config.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/render/wlr_texture.h>
#include "sway/tree/container.h"
#include "sway/tree/scene.h"
#include "sway/tree/node.h"
#include "list.h"

extern struct sway_root *root;

typedef bool (*sway_root_output_filter_func_t)(
	struct sway_output *output, void *data);

typedef bool (*sway_root_workspace_filter_func_t)(
	struct sway_workspace *workspace, void *data);

typedef bool (*sway_root_container_filter_func_t)(
	struct sway_workspace *workspace, struct sway_container *container, void *data);

struct sway_root {
	struct sway_node node;
	struct wlr_output_layout *output_layout;

	// scene node layout:
	// - root
	// 	- staging
	// 	- layer shell stuff
	// 	- tiling
	// 	- floating
	// 	- fullscreen stuff
	// 	- seat stuff
	// 	- ext_session_lock
	struct sway_scene *root_scene;

	// since sway_scene nodes can't be orphaned and must always
	// have a parent, use this staging scene_tree so that a
	// node always have a valid parent. Nothing in this
	// staging node will be visible.
	struct sway_scene_tree *staging;

	// tree containing all layers the compositor will render. Cursor handling
	// will end up iterating this tree.
	struct sway_scene_tree *layer_tree;

	struct {
		struct sway_scene_tree *shell_background;
		struct sway_scene_tree *shell_bottom;
		struct sway_scene_tree *tiling;
		struct sway_scene_tree *floating;
		struct sway_scene_tree *shell_top;
		struct sway_scene_tree *fullscreen;
		struct sway_scene_tree *fullscreen_global;
#if WLR_HAS_XWAYLAND
		struct sway_scene_tree *unmanaged;
#endif
		struct sway_scene_tree *shell_overlay;
		struct sway_scene_tree *popup;
		struct sway_scene_tree *seat;
		struct sway_scene_tree *session_lock;
	} layers;

	// Includes disabled outputs
	struct wl_list all_outputs; // sway_output::link

	double x, y;
	double width, height;

	list_t *outputs; // struct sway_output
	list_t *non_desktop_outputs; // struct sway_output_non_desktop
	list_t *scratchpad; // struct sway_container

	// For when there's no connected outputs
	struct sway_output *fallback_output;

	struct sway_container *fullscreen_global;

	struct {
		struct wl_signal new_node;
	} events;

	// Filters used during arrange_root()
	struct {
		// Function to trigger free animation instead of the default
		sway_root_workspace_filter_func_t free_animation_activation_filter;
		void *free_animation_activation_filter_data;
		// Decide whether to add this output to the scene graph (SG)
		sway_root_output_filter_func_t output_filter;
		void *output_filter_data;
		// Decide whether to add this workspace to the SG
		sway_root_workspace_filter_func_t workspace_filter;
		void *workspace_filter_data;
		// Decide whether to add floating containers of this workspace to the SG
		sway_root_workspace_filter_func_t workspace_floating_filter;
		void *workspace_floating_filter_data;
		// Decide whether to add tiling containers of this workspace to the SG
		sway_root_workspace_filter_func_t workspace_tiling_filter;
		void *workspace_tiling_filter_data;
		// Decide whether to add this container to the SG
		sway_root_container_filter_func_t container_filter;
		void *container_filter_data;
	} filters;

	bool overview;
	bool jumping;

	list_t *spaces;
};

struct sway_root *root_create(struct wl_display *display);

void root_destroy(struct sway_root *root);

/**
 * Move a container to the scratchpad.
 * If a workspace is passed, the container is assumed to have been in
 * the scratchpad before and is shown on the workspace.
 * The ws parameter can safely be NULL.
 */
void root_scratchpad_add_container(struct sway_container *con,
   struct sway_workspace *ws);

/**
 * Remove a container from the scratchpad.
 */
void root_scratchpad_remove_container(struct sway_container *con);

/**
 * Show a single scratchpad container.
 */
void root_scratchpad_show(struct sway_container *con);

/**
 * Hide a single scratchpad container.
 */
void root_scratchpad_hide(struct sway_container *con);

void root_for_each_workspace(void (*f)(struct sway_workspace *ws, void *data),
		void *data);

void root_for_each_container(void (*f)(struct sway_container *con, void *data),
		void *data);

struct sway_output *root_find_output(
		bool (*test)(struct sway_output *output, void *data), void *data);

struct sway_workspace *root_find_workspace(
		bool (*test)(struct sway_workspace *ws, void *data), void *data);

struct sway_container *root_find_container(
		bool (*test)(struct sway_container *con, void *data), void *data);

void root_get_box(struct sway_root *root, struct wlr_box *box);

void root_set_default_filters(struct sway_root *root);

#endif
