#ifndef _SWAY_LAYOUT_H
#define _SWAY_LAYOUT_H

#include "sway/tree/container.h"
#include <wlr/util/box.h>
#include <wlr/util/edges.h>

enum sway_layout_reorder {
	REORDER_AUTO,
	REORDER_LAZY,
};

enum sway_layout_direction {
	DIR_INVALID,
	DIR_LEFT,
	DIR_RIGHT,
	DIR_UP,
	DIR_DOWN,
	DIR_BEGIN,
	DIR_END,
	DIR_CENTER,
	DIR_MIDDLE,
};

enum sway_layout_axis {
	AXIS_HORIZONTAL = (WLR_EDGE_LEFT | WLR_EDGE_RIGHT),
	AXIS_VERTICAL = (WLR_EDGE_TOP | WLR_EDGE_BOTTOM),
};

enum sway_layout_fit_group {
	FIT_ACTIVE,
	FIT_VISIBLE,
	FIT_ALL,
	FIT_TOEND,
	FIT_TOBEG,
};

enum sway_layout_insert {
	INSERT_BEFORE,
	INSERT_AFTER,
	INSERT_BEGINNING,
	INSERT_END
};

enum sway_layout_align {
	ALIGN_NONE,
	ALIGN_AUTO,
	ALIGN_ALWAYS,
};

enum sway_layout_align_axis {
	ALIGN_AXIS_NONE = 0,
	ALIGN_AXIS_H = (1 << 0),
	ALIGN_AXIS_V = (1 << 1)
};

enum sway_layout_pin {
	PIN_BEGINNING,
	PIN_END
};

enum sway_layout_overview {
	OVERVIEW_DISABLED,
	OVERVIEW_ALL,
	OVERVIEW_JUMP
};

enum sway_toggle_size {
	TOGGLE_SIZE_NONE,
	TOGGLE_SIZE_ACTIVE,
	TOGGLE_SIZE_ALL
};

enum sway_layout_filter_apply {
	LAYOUT_FILTER_APPLY_ACTIVE,
	LAYOUT_FILTER_APPLY_ALL,
	LAYOUT_FILTER_APPLY_ACTIVE_ONLY
};

enum sway_layout_filter {
	LAYOUT_FILTER_ALL,
	LAYOUT_FILTER_TILING,
	LAYOUT_FILTER_FLOATING,
	LAYOUT_FILTER_CONTAINER,
	LAYOUT_FILTER_TRAILMARK,
	LAYOUT_FILTER_VISIBLE,
	LAYOUT_FILTER_SCRATCHPAD,
};

enum sway_layout_fit {
	FIT_NONE,
	FIT_SPLIT,
	FIT_FRACTION
};

struct sway_scroller {
	enum sway_container_layout type;

	struct {
		enum sway_layout_reorder reorder;
		enum sway_container_layout mode;
		enum sway_layout_insert insert;
		enum sway_layout_fit fit;
		bool focus;
		bool center_horizontal;
		bool center_vertical;
		bool floating;
		bool infinite;
	} modifiers;

	bool locked; // locked status of the infinite canvas. Use layout_locked() to read it

	struct {
		enum sway_layout_overview mode;
		double mem_scale; // stores the current workspace scale when calling overview, so it can be restored later
		double mem_pan_x, mem_pan_y; // the same, for the workspace pan
		double tiling_scale;
		struct wlr_fbox content; // empty if it needs a recompute
		int width, height; // size of the workspace
		struct sway_container *fullscreen;  // stores the full screen container when calling overview, so it can be restored later
	} overview;

	struct {
		struct sway_container *container;
		enum sway_layout_pin pos;
	} pin;

	struct {
		enum sway_toggle_size mode;
		struct sway_container *container;
		double width, height;
	} toggle_size;

	enum sway_layout_align align;
	uint32_t align_axes;
	struct sway_container *align_container;
};

struct sway_scroller_modifiers {
	bool set;
	bool reorder_set;
	enum sway_layout_reorder reorder;
	bool mode_set;
	enum sway_container_layout mode;
	bool insert_set;
	enum sway_layout_insert insert;
	bool fit_set;
	enum sway_layout_fit fit;
	bool focus_set;
	bool focus;
	bool center_horizontal_set;
	bool center_horizontal;
	bool center_vertical_set;
	bool center_vertical;
	bool floating_set;
	bool floating;
	bool infinite_set;
	bool infinite;
};

struct sway_scroller_output_options {
	enum sway_container_layout type;
	double default_width;
	double default_height;
	list_t *widths;
	list_t *heights;
	struct sway_scroller_modifiers default_modifiers;
};

// Global functions
enum wlr_direction layout_to_wlr_direction(enum sway_layout_direction dir);

double layout_get_default_width(struct sway_workspace *workspace);
double layout_get_default_height(struct sway_workspace *workspace);
list_t *layout_get_widths(struct sway_output *output);
list_t *layout_get_heights(struct sway_output *output);

// Functions Specific to layout handling
void layout_init(struct sway_workspace *workspace);
void layout_set_type(struct sway_workspace *workspace, enum sway_container_layout type);
enum sway_container_layout layout_get_type(struct sway_workspace *workspace);

// Toggle overview will only trigger a workspace arrangement where it will call
// layout_overview_recompute_scale()
void layout_overview_set(struct sway_workspace *workspace, enum sway_layout_overview mode);
// This can be called by the layout to recalculate the overview scale every
// transaction that re-arranges the workspace. It will set it too
void layout_overview_recompute_scale(struct sway_workspace *workspace);
// After arranging the workspace, compute the scale and pan that fit every window
void layout_overview_recompute_camera(struct sway_workspace *workspace);
// Return current overview mode
enum sway_layout_overview layout_overview_mode(struct sway_workspace *workspace);

bool layout_overview_workspaces_enabled();
void layout_overview_workspaces(bool on);

// Global and screen coordinates are only different in workspaces overview mode.
// Global includes the workspaces overview transform, screen is just the position
// on the screen, untransformed.

// Screen to global when the workspace is not known. It finds the workspace under
// the cursor when in workspaces overview mode, false if there is none
bool layout_overview_workspaces_map_coordinates(struct sway_workspace **workspace, double *x, double *y);
void layout_overview_workspaces_global_to_screen(struct sway_workspace *workspace, double *x, double *y);
void layout_overview_workspaces_screen_to_global(struct sway_workspace *workspace, double *x, double *y);
// The scale of workspace in the workspaces overview transform (1.0 when not enabled)
double layout_overview_workspaces_scale(struct sway_workspace *workspace);
void layout_overview_workspaces_recompute_scale();

void layout_scale_set(struct sway_workspace *workspace, double scale);
void layout_scale_reset(struct sway_workspace *workspace);
double layout_scale_get(struct sway_workspace *workspace);
bool layout_scale_enabled(struct sway_workspace *workspace);

// Mode modifiers
void layout_modifiers_init(struct sway_workspace *workspace);
void layout_modifiers_set_mode(struct sway_workspace *workspace, enum sway_container_layout mode);
void layout_modifiers_set_insert(struct sway_workspace *workspace, enum sway_layout_insert ins);
void layout_modifiers_set_fit(struct sway_workspace *workspace, enum sway_layout_fit fit);
void layout_modifiers_set_focus(struct sway_workspace *workspace, bool focus);
void layout_modifiers_set_center_horizontal(struct sway_workspace *workspace, bool center);
void layout_modifiers_set_center_vertical(struct sway_workspace *workspace, bool center);
void layout_modifiers_set_reorder(struct sway_workspace *workspace, enum sway_layout_reorder reorder);
void layout_modifiers_set_floating(struct sway_workspace *workspace, bool floating);
void layout_modifiers_set_infinite(struct sway_workspace *workspace, bool infinite);

enum sway_container_layout layout_modifiers_get_mode(struct sway_workspace *workspace);
enum sway_layout_insert layout_modifiers_get_insert(struct sway_workspace *workspace);
enum sway_layout_fit layout_modifiers_get_fit(struct sway_workspace *workspace);
bool layout_modifiers_get_focus(struct sway_workspace *workspace);
bool layout_modifiers_get_center_horizontal(struct sway_workspace *workspace);
bool layout_modifiers_get_center_vertical(struct sway_workspace *workspace);
enum sway_layout_reorder layout_modifiers_get_reorder(struct sway_workspace *workspace);
bool layout_modifiers_get_floating(struct sway_workspace *workspace);
bool layout_modifiers_get_infinite(struct sway_workspace *workspace);

void layout_workspace_set_align(struct sway_workspace *workspace, enum sway_layout_align align);
enum sway_layout_align layout_workspace_get_align(struct sway_workspace *workspace);
uint32_t layout_workspace_get_align_axes(struct sway_workspace *workspace);
// Sets the new aligned container and its axes of alignment. If the container
// is already aligned, axes add to the current ones.
void layout_workspace_set_align_container(struct sway_workspace *workspace,
		struct sway_container *container, uint32_t axes);
struct sway_container *layout_workspace_get_align_container(struct sway_workspace *workspace);
// Should focusing container keep the workspace's current alignment?
bool layout_alignment_keep(struct sway_workspace *workspace, struct sway_container *container);

// Layout API
void layout_add_view(struct sway_workspace *workspace, struct sway_container *active, struct sway_container *view);
void layout_move_container_to_workspace(struct sway_container *container, struct sway_workspace *workspace);
void layout_drag_container_to_workspace(struct sway_container *container, struct sway_workspace *workspace);
void layout_drag_container_to_container(struct sway_container *container, struct sway_container *target,
		struct sway_workspace *workspace, enum wlr_edges edge);

bool layout_move_container(struct sway_container *container, enum sway_layout_direction dir, bool nomode);

void layout_jump();
void layout_jump_workspaces();
void layout_jump_container(struct sway_container *container);
void layout_jump_tiling(bool all);
void layout_jump_floating(bool all);
void layout_jump_scratchpad(void);
void layout_jump_trailmark(bool all);
void layout_jump_all(bool all);
struct criteria;
void layout_jump_criteria(struct criteria *critera);

void layout_container_jump_decoration_apply_scale(struct sway_container *con);

void layout_filter_reset();
void layout_filter(enum sway_layout_filter filter, enum sway_layout_filter_apply apply);

// Gestures
// Begin scrolling swipe gesture. Return true if scrolling, false if there are
// no conditions to scroll (total width of windows is smaller than viewport).
// The gesture scrolls the workspace until layout_scroll_end() or
// layout_scroll_cancel().
bool layout_scroll_begin(struct sway_seat *seat, struct sway_workspace *workspace);
// Update scrolling swipe gesture
void layout_scroll_update(struct sway_seat *seat, double dx, double dy, float sensitivity);
// Finish scrolling swipe and return true if scrolling, else false
bool layout_scroll_end(struct sway_seat *seat);
// Interrupt scrolling and reset viewport. Does nothing if the seat is not scrolling
void layout_scroll_cancel(struct sway_seat *seat);
// In infintie canvas mode, a gesture that scrolls an unlocked viewport, only
// pans it. Nothing takes focus, even if focus_follows_mouse is on, because it
// would move the viewport to the window it hovers. So a window needs to be
// clicked, or get focus some other way, to release it.
bool layout_scroll_holds_focus(struct sway_workspace *workspace);
void layout_scroll_release_focus(struct sway_workspace *workspace);

// Infinite canvas
//
// The workspace is a camera over an infinite plane. Containers keep their
// layout coordinates and the camera moves.
//
// Tiled containers are always arranged and focused with no pan. Floating
// containers can be anywhere on the canvas (which pans to focus them.
//
// While a workspace is locked, the viewport has zero pan (except during a
// scrolling gesture). While it is unlocked, the viewport pans to follow the
// pointer in the outer gaps and to show the container that gets focus.

// Is the viewport of the workspace locked? Use this function instead of the
// flag, because it verifies the overrides (infinite and floating modes, dragging).
bool layout_locked(struct sway_workspace *workspace);
// Set the lock flag of the workspace for a container that gets focus.
void layout_locked_update(struct sway_workspace *workspace,
		struct sway_container *focus);
// The container changed in a way the lock depends on (floating or tiled,
// sticky, scratchpad). If is is focused, set the lock flag again.
void layout_locked_refresh(struct sway_container *container);
// If the workspace is not locked, pan its viewport to show the container.
void layout_show_container(struct sway_container *container);
// The same for the focused container in `workspace`
void layout_show_focus(struct sway_workspace *workspace);

// Returns true is a dragging operation is happening: dragging a window, or 
// something from a client (drag and drop). Unlocks every workspace until
// it ends.
bool layout_dragging(void);
// Set the dragging flag to mark the beginning of a drag operation
void layout_drag_set(struct sway_seat *seat, bool *flag, bool dragging);

// Edge panning
// While the pointer is in the outer gaps of a workspace that is not locked,
// its viewport pans to follow it.
//
// Call when the pointer moves, to start or stop panning
void layout_edge_pan_update(struct sway_seat *seat);
// Stop panning for the seat
void layout_edge_pan_cancel(struct sway_seat *seat);
// Is the seat panning a workspace?
bool layout_edge_pan_active(struct sway_seat *seat);
// Call before rendering a frame for output to advance the pan
void layout_edge_pan_frame(struct sway_output *output);

// Pin

// Returns true if a pin is enabled for the workspace. Instead of chasing the pinned
// container to make sure the pin is valid (view unmapped, container changing
// workspace etc.), we verify the pin is still valid using this function in the
// very few places we need to check.
bool layout_pin_enabled(struct sway_workspace *workspace);
void layout_pin_set(struct sway_workspace *workspace, struct sway_container *container, enum sway_layout_pin pos);
struct sway_container *layout_pin_get_container(struct sway_workspace *workspace);
enum sway_layout_pin layout_pin_get_position(struct sway_workspace *workspace);

// Selection

// Toggle a selection: depending on mode, chooses a top level or view container
void layout_selection_toggle(struct sway_container *container);
// Select every container in the workspace
void layout_selection_workspace(struct sway_workspace *workspace);
// Resets the selection
void layout_selection_reset();
// Move the selection to workspace, with a location given by the current mode modifier
bool layout_selection_move(struct sway_workspace *workspace);
// Create a new, active, trail with the contents of the current selection, and reset selection.
void layout_selection_to_trail();

// Returns true if the container is selected
bool layout_selection_enabled(struct sway_container *container);
void layout_selection_set(struct sway_container *container, bool selected);

// Trails and Trailmarks

void layout_trail_new();
void layout_trail_next();
void layout_trail_prev();
void layout_trail_number(int n);
void layout_trail_delete();
void layout_trail_clear();
void layout_trail_to_selection();
void layout_trailmark_toggle(struct sway_view *view);
void layout_trailmark_next();
void layout_trailmark_prev();

// Call when unmapping a view to avoid having dangling pointers
void layout_trail_remove_view(struct sway_view *view);

// For TRAIL IPC event
int layout_trails_length();
int layout_trails_active();
int layout_trails_active_length();
bool layout_trails_trailmarked(struct sway_view *view);

// Toggle size
//
// Return the current toggle_size mode for the workspace
enum sway_toggle_size layout_toggle_size_mode(struct sway_workspace *workspace);
double layout_toggle_size_width_fraction(struct sway_workspace *workspace);
double layout_toggle_size_height_fraction(struct sway_workspace *workspace);
// Set the toggle_size mode for the workspace
void layout_toggle_size(struct sway_workspace *workspace, struct sway_container *container,
	enum sway_toggle_size mode, double width_fraction, double height_fraction);
// Called when changing focus to update toggle_size
void layout_toggle_size_change_focus(struct sway_node *last_focus,
	struct sway_container *new_container, struct sway_workspace *new_workspace);

// Maximize if single option
void layout_maximize_if_single(struct sway_workspace *workspace);

void layout_toggle_size_container(struct sway_container *container,
		double width_fraction, double height_fraction);

// Call after any resize. This function calls any needed functions for
// internal management of options etc.
void layout_tiling_resize_callback(struct sway_container *container);

// Fit container/workspace size
void layout_fit_size_workspace(struct sway_workspace *workspace,
	enum sway_layout_fit_group fit, bool equal);
void layout_fit_size_container(struct sway_container *container,
	enum sway_layout_fit_group fit, bool equal);

// Set the fraction of `container` along `axis` and rebalance its siblings
void layout_fit_set_fraction(struct sway_container *container,
	enum sway_layout_axis axis, double fraction);

// Layout Coordinate Transforms.
//
// There are three coordinate systems:
//
// * layout: coordinate system for unscaled positions and sizes of containers
//   (canvas of the workspace). A transform function with no coordinate
//   system in its name operates in layout coordinates.
// * global: output layout coordinates. The scene is arranged in this coordinate
//   system.
// * screen: real position on the outputs (cursor coordinate system). The same
//   as global, except in workspaces overview.
//
// Layout to global. Consider A to be the global anchor (center of the
// workspace's output area), and C the layout point. If the workspace scale
// is s, the transform is:
//
//           global = A + s * (layout - C)
//
// We keep A and C separate instead of simplifying, so we can easily do
// viewport panning (scrolling gestures, infinite canvas...)
//
// The transform of a workspace has no pan: C = A. Tiling containers are
// arranged with zero pan. The pan of a workspace changes what is drawn, so
// it is part of the transform of each container: C = A - pan. This means
// layout coordinates are not global coordinates even when the workspace is
// not scaled. Use the following functions to convert.
//
// Global to screen: layout_overview_workspaces_global_to_screen() and
// layout_overview_workspaces_screen_to_global().
//
// Layout to screen: layout_container_layout_to_screen() and
// layout_container_screen_to_layout().
struct sway_layout_transform {
	double scale;
	double x, y;
};

double layout_transform_workspace_scale(struct sway_workspace *workspace,
		bool animated);

// 1.0 if it is full screen or has no workspace/output
double layout_transform_container_scale(struct sway_container *container,
		bool animated);

// The workspace transform without any pan. It is the viewport where tiling
// layout arrangement happens.
void layout_transform_workspace_get(struct sway_workspace *workspace,
		bool animated, struct sway_layout_transform *transform);

// Does the pan of the workspace move the container? Full screen, sticky and
// scratchpad containers stay where they are on screen
bool layout_container_pans(struct sway_container *container);

// Identity if it is full screen or it has no workspace/output. Includes
// workspace pan if needed
void layout_transform_container_get(struct sway_container *container,
		bool animated, struct sway_layout_transform *transform);

// A point in the layout coordinates of the container, as if it was in
// `workspace`, to where it is on screen.
void layout_container_layout_to_screen(struct sway_container *container,
		struct sway_workspace *workspace, bool animated, double *x, double *y);
void layout_container_screen_to_layout(struct sway_container *container,
		struct sway_workspace *workspace, bool animated, double *x, double *y);

// Apply a transform: layout to global, and `_inv` for global to layout
static inline double layout_transform_x(const struct sway_layout_transform *t, double x) {
	return t->scale * x + t->x;
}

static inline double layout_transform_y(const struct sway_layout_transform *t, double y) {
	return t->scale * y + t->y;
}

// Layout to global
static inline void layout_transform_point(const struct sway_layout_transform *t,
		double *x, double *y) {
	*x = t->scale * *x + t->x;
	*y = t->scale * *y + t->y;
}

// Global to layout
static inline void layout_transform_point_inv(const struct sway_layout_transform *t,
		double *x, double *y) {
	*x = (*x - t->x) / t->scale;
	*y = (*y - t->y) / t->scale;
}

static inline void layout_transform_box(const struct sway_layout_transform *t,
		struct wlr_fbox *box) {
	layout_transform_point(t, &box->x, &box->y);
	box->width *= t->scale;
	box->height *= t->scale;
}

static inline void layout_transform_box_inv(const struct sway_layout_transform *t,
		struct wlr_fbox *box) {
	layout_transform_point_inv(t, &box->x, &box->y);
	box->width /= t->scale;
	box->height /= t->scale;
}

// The workspace box (usable area minus outer gaps) in layout coordinates
// without any pan. The tiling layout is arranged there
void layout_workspace_box(struct sway_workspace *workspace, struct wlr_fbox *box);

// The workspace box in layout coordinates, panned unless the container does
// not pan (sticky, scratchpad)
void layout_viewport_box(struct sway_container *container,
		struct wlr_fbox *box);

// Workspace pan as it applies to `container` (nothing if the container does
// not pan (sticky, scratchpad))
void layout_container_pan_get(struct sway_container *container,
		struct sway_workspace *workspace, double *x, double *y);

// Translates `box in global coordinates by the pan of the container's workspace
void layout_floating_box_from_global(struct sway_container *container, struct wlr_fbox *box);

// The box of the container in global coordinates with or without animation
void layout_container_global_box(struct sway_container *container, bool animated,
		struct wlr_fbox *box);

// The same, rounded to integer coordinates
void layout_container_global_wlr_box(struct sway_container *container,
		bool animated, struct wlr_box *box);

#endif // _SWAY_LAYOUT_H
