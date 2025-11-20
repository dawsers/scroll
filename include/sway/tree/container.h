#ifndef _SWAY_CONTAINER_H
#define _SWAY_CONTAINER_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_compositor.h>
#include "list.h"
#include "sway/tree/scene.h"
#include "sway/tree/node.h"

struct sway_view;
struct sway_seat;

enum sway_container_layout {
	L_NONE,
	L_HORIZ,
	L_VERT,
};

enum sway_container_border {
	B_NONE,
	B_PIXEL,
	B_NORMAL,
	B_CSD,
};

enum sway_fullscreen_mode {
	FULLSCREEN_NONE,
	FULLSCREEN_WORKSPACE,
	FULLSCREEN_GLOBAL,
};

enum sway_fullscreen_request_mode {
	FULLSCREEN_REQUEST_DEFAULT,
	FULLSCREEN_REQUEST_LAYOUT,
};

enum sway_fullscreen_state {
	FULLSCREEN_DISABLED,
	FULLSCREEN_ENABLED,
};

enum sway_fullscreen_movefocus {
	FULLSCREEN_MOVEFOCUS_NONE,
	FULLSCREEN_MOVEFOCUS_FOLLOW,
	FULLSCREEN_MOVEFOCUS_NOFOLLOW
};

enum sway_toggle_size_state {
	TOGGLE_STATE_NONE,
	TOGGLE_STATE_TOGGLED,
	TOGGLE_STATE_NONE_FORCED,
	TOGGLE_STATE_TOGGLED_FORCED,
};

struct sway_root;
struct sway_output;
struct sway_workspace;
struct sway_view;

enum wlr_direction;

struct sway_container_state {
	// Container properties
	enum sway_container_layout layout;
	double x, y;
	double width, height;

	enum sway_fullscreen_mode fullscreen_mode;
	enum sway_fullscreen_state fullscreen_container;
	enum sway_fullscreen_state fullscreen_application;
	enum sway_fullscreen_state fullscreen_layout;

	struct sway_workspace *workspace; // NULL when hidden in the scratchpad
	struct sway_container *parent;    // NULL if container in root of workspace
	list_t *children;                 // struct sway_container

	struct sway_container *focused_inactive_child;
	bool focused;

	enum sway_container_border border;
	int border_thickness;
	bool border_top;
	bool border_bottom;
	bool border_left;
	bool border_right;
	struct {
		int border_radius;
		bool shadow;
		bool shadow_dynamic;
		double shadow_size;
		double shadow_blur;
		double shadow_offset_x, shadow_offset_y;
		float shadow_color_r, shadow_color_g, shadow_color_b, shadow_color_a;
		bool dim;
		float dim_color_r, dim_color_g, dim_color_b, dim_color_a;
	} decoration;

	// These are in layout coordinates.
	double content_x, content_y;
	double content_width, content_height;
};

struct sway_container {
	struct sway_node node;
	struct sway_view *view;

	struct sway_scene_tree *scene_tree;

	struct {
		struct sway_scene_tree *tree;

		struct sway_text_node *title_text;
		struct sway_text_node *marks_text;
	} title_bar;

	struct {
		struct sway_scene_tree *tree;
		struct sway_scene_decoration *full;
	} decoration;

	struct sway_scene_shadow *shadow;

	struct {
		struct sway_scene_tree *tree;

		struct sway_text_node *text;
		bool jumping;
		double x, y;	// original positions for floating windows
	} jump;

	struct sway_scene_tree *content_tree;
	struct sway_scene_buffer *output_handler;

	struct wl_listener output_enter;
	struct wl_listener output_leave;
	struct wl_listener output_handler_destroy;

	struct sway_container_state current;
	struct sway_container_state pending;

	char *title;           // The view's title (unformatted)
	char *formatted_title; // The title displayed in the title bar
	double title_width;

	char *title_format;

	enum sway_container_layout prev_split_layout;

	// Whether stickiness has been enabled on this container. Use
	// `container_is_sticky_[or_child]` rather than accessing this field
	// directly; it'll also check that the container is floating.
	bool is_sticky;

	// For C_ROOT, this has no meaning
	// For other types, this is the position in layout coordinates
	// Includes borders
	double saved_x, saved_y;
	double saved_width, saved_height;

	// Used when the view changes to CSD unexpectedly. This will be a non-B_CSD
	// border which we use to restore when the view returns to SSD.
	enum sway_container_border saved_border;

	// Fraction of the viewport size this container occupies
	double width_fraction;
	double height_fraction;

	struct {
		bool single;
		enum sway_toggle_size_state state;
		double saved_width_fraction;
		double saved_height_fraction;
	} toggle_size;

	// Animation variables
	struct {
		double x0, y0, w0, h0;
		double xt, yt, wt, ht;
		double w1, h1;
	} animation;

	bool selected;	// for selection/cut/move

	// Indicates that the container is a scratchpad container.
	// Both hidden and visible scratchpad containers have scratchpad=true.
	// Hidden scratchpad containers have a NULL parent.
	bool scratchpad;

	// Stores last output size and position for adjusting coordinates of
	// scratchpad windows.
	// Unused for non-scratchpad windows.
	struct wlr_box transform;

	float alpha;

	list_t *marks; // char *

	bool fullscreen; // container needs to recover fs mode when refocused

	// For XWayland cotainers, because they need to be reconfigured when
	// changing positions, and some containes will change position without
	// being in the transaction (change of focus, move container, etc.)
	struct {
		double x, y;
		double width, height;
	} old_content;

	struct {
		struct wl_signal destroy;
	} events;
};

struct sway_container *container_create(struct sway_view *view);

void container_destroy(struct sway_container *con);

void container_begin_destroy(struct sway_container *con);

/**
 * Search a container's descendants a container based on test criteria. Returns
 * the first container that passes the test.
 */
struct sway_container *container_find_child(struct sway_container *container,
		bool (*test)(struct sway_container *view, void *data), void *data);

void container_for_each_child(struct sway_container *container,
		void (*f)(struct sway_container *container, void *data), void *data);

/**
 * Returns the fullscreen container obstructing this container if it exists.
 */
struct sway_container *container_obstructing_fullscreen_container(struct sway_container *container);

/**
 * Returns true if the given container is an ancestor of this container.
 */
bool container_has_ancestor(struct sway_container *container,
		struct sway_container *ancestor);

void container_reap_empty(struct sway_container *con);

void container_update_title_bar(struct sway_container *container);

void container_update_marks(struct sway_container *container);

size_t parse_title_format(struct sway_container *container, char *buffer);

size_t container_build_representation(enum sway_container_layout layout,
		list_t *children, char *buffer);

void container_update_representation(struct sway_container *container);

/**
 * Return the height of a regular title bar.
 */
size_t container_titlebar_height(void);

void floating_calculate_constraints(int *min_width, int *max_width,
		int *min_height, int *max_height);

void floating_fix_coordinates(struct sway_container *con,
		struct wlr_box *old, struct wlr_box *new);

void container_floating_resize_and_center(struct sway_container *con);

void container_floating_set_default_size(struct sway_container *con);

void container_set_resizing(struct sway_container *con, bool resizing);

void container_set_floating(struct sway_container *container, bool enable);

void container_set_geometry_from_content(struct sway_container *con);

/**
 * Determine if the given container is itself floating.
 * This will return false for any descendants of a floating container.
 *
 * Uses pending container state.
 */
bool container_is_floating(struct sway_container *container);

/**
 * Get a container's box in layout coordinates.
 */
void container_get_box(struct sway_container *container, struct wlr_box *box);

/**
 * Move a floating container by the specified amount.
 */
void container_floating_translate(struct sway_container *con,
		double x_amount, double y_amount);

/**
 * Choose an output for the floating container's new position.
 */
struct sway_output *container_floating_find_output(struct sway_container *con);

/**
 * Move a floating container to a new layout-local position.
 */
void container_floating_move_to(struct sway_container *con,
		double lx, double ly);

/**
 * Move a floating container to the center of the workspace.
 */
void container_floating_move_to_center(struct sway_container *con);

bool container_has_urgent_child(struct sway_container *container);

/**
 * If the container is involved in a drag or resize operation via a mouse, this
 * ends the operation.
 */
void container_end_mouse_operation(struct sway_container *container);

void container_set_fullscreen(struct sway_container *con,
		enum sway_fullscreen_mode mode);

void container_set_fullscreen_container(struct sway_container *con,
		enum sway_fullscreen_state mode);

void container_set_fullscreen_application(struct sway_container *con,
		enum sway_fullscreen_state mode);

void container_set_fullscreen_layout(struct sway_container *con,
		enum sway_fullscreen_state mode);

void container_handle_fullscreen_request(struct sway_container *con, bool enable);

/**
 * Convenience function.
 */
void container_fullscreen_disable(struct sway_container *con);

/**
 * Pass the src container's full screen mode to dst. Use when changing focus
 * and want to keep full screen mode (fullscreen_movefocus option)
 */
void container_pass_fullscreen(struct sway_container *src, struct sway_container *dst);

/**
 * Walk up the container tree branch starting at the given container, and return
 * its earliest ancestor.
 */
struct sway_container *container_toplevel_ancestor(
		struct sway_container *container);

/**
 * Return true if the container is floating, or a child of a floating split
 * container.
 */
bool container_is_floating_or_child(struct sway_container *container);

/**
 * Return true if the container is fullscreen, or a child of a fullscreen split
 * container.
 */
bool container_is_fullscreen_or_child(struct sway_container *container);

enum sway_container_layout container_parent_layout(struct sway_container *con);

list_t *container_get_siblings(struct sway_container *container);

int container_sibling_index(struct sway_container *child);

void container_handle_fullscreen_reparent(struct sway_container *con);

void container_add_child(struct sway_container *parent,
		struct sway_container *child);

void container_insert_child(struct sway_container *parent,
		struct sway_container *child, int i);

/**
 * Side should be 0 to add before, or 1 to add after.
 */
void container_add_sibling(struct sway_container *parent,
		struct sway_container *child, bool after);

void container_detach(struct sway_container *child);

void container_replace(struct sway_container *container,
		struct sway_container *replacement);

void container_swap(struct sway_container *con1, struct sway_container *con2);

struct sway_container *container_split(struct sway_container *child,
		enum sway_container_layout layout);

bool container_is_transient_for(struct sway_container *child,
		struct sway_container *ancestor);

/**
 * Find any container that has the given mark and return it.
 */
struct sway_container *container_find_mark(char *mark);

/**
 * Find any container that has the given mark and remove the mark from the
 * container. Returns true if it matched a container.
 */
bool container_find_and_unmark(char *mark);

/**
 * Remove all marks from the container.
 */
void container_clear_marks(struct sway_container *container);

bool container_has_mark(struct sway_container *container, char *mark);

void container_add_mark(struct sway_container *container, char *mark);

void container_raise_floating(struct sway_container *con);

bool container_is_scratchpad_hidden(struct sway_container *con);

bool container_is_scratchpad_hidden_or_child(struct sway_container *con);

bool container_is_sticky(struct sway_container *con);

bool container_is_sticky_or_child(struct sway_container *con);

void container_arrange_title_bar(struct sway_container *con);

void container_update(struct sway_container *con);

void container_update_itself_and_parents(struct sway_container *con);

/**
 * Helper functions to update a parent when a container in fullscreen layout mode
 * gets detached or inserted from/to another container
 */
void container_insert_update_parent_fullscreen_layout(struct sway_container *parent,
		struct sway_container *child);

void container_detach_update_parent_fullscreen_layout(struct sway_container *parent,
		struct sway_container *child);

#endif
