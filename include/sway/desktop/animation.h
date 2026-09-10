#ifndef _SWAY_ANIMATION_H
#define _SWAY_ANIMATION_H
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <wayland-server-core.h>
#include "list.h"

/**
 * Animations.
 */

struct wlr_output;
struct sway_transaction;

enum sway_animation_style {
	ANIM_STYLE_CLIP,
	ANIM_STYLE_SCALE
};

enum sway_animation_type {
	ANIMATION_DISABLED,
	ANIMATION_DEFAULT,
	ANIMATION_WINDOW_OPEN,
	ANIMATION_WINDOW_SIZE,
	ANIMATION_WINDOW_MOVE,
	ANIMATION_WINDOW_MOVE_FLOAT,
	ANIMATION_WINDOW_FULLSCREEN,
	ANIMATION_WORKSPACE_SWITCH,
	ANIMATION_OVERVIEW,
	ANIMATION_JUMP,
	ANIMATION_LAYER_SHELL,
	ANIMATION_FADE_IN,
	ANIMATION_FADE_OUT,
};

enum sway_animated_variable_type {
	ANIMATED_VARIABLE_NONE,
	ANIMATED_VARIABLE_SIZE,
	ANIMATED_VARIABLE_POSITION,
	ANIMATED_VARIABLE_FADE,
};

/**
 * Every variable that takes part in an animation (the x position of a
 * container, its width, its opacity, the scale of a workspace, ...) has one of
 * these. The variable holds its origin (x0), destination (x1) and current
 * value (xt), plus the animation it follows. The value used to place and size
 * nodes in the scene graph is always the current value of the animated
 * variable (xt).
 *
 * Variables are independent of each other: within the same transaction, two
 * variables of the same element may follow different animations. When a new
 * transaction interrupts an ongoing animation, variables affected by the new
 * transaction get a new destination and adopt its animation type, continuing
 * from the value they had at the time of the interruption (xt). Variables not
 * affected by the new transaction continue their old animation for its
 * remaining duration.
 */
struct sway_animated_variable {
	bool animating; // the variable is in the list of animating variables
	double x0, x1, xt; // origin, destrination, current value
	enum sway_animation_type animation;
	enum sway_animated_variable_type type; // sway_animated_variable_type
	struct timespec start;  // start time for the animation curve
	int curve;  // index of the curve in the animation path
	double span; // to animate the position of floating and fullscreen containers
	double ct, cx, cy; // values of the curve at the current time
	// The animated variable values have been updated, but shouldn't be considered
	// yet part of the already animating variables until its own animation begins.
	bool pending_start;
	struct wl_list link; // link to currently animating variables
};

/**
 * Initialize an animated variable with `value` default value and type `type`.
 */
void animated_variable_init(struct sway_animated_variable *av,
		double value, enum sway_animated_variable_type type);

/**
 * Call when destroying the owner of the animated variable.
 */
void animated_variable_release(struct sway_animated_variable *av);

/**
 * Move an idle variable to `value`, with no animation. Does nothing if the
 * variable is animating, so that ongoing animations are not affected.
 */
void animated_variable_reset(struct sway_animated_variable *av, double value);

/**
 * Set the destination value of the variable to its original one. It does nothing
 * if the animation has already begun, or if the variable is not animating.
 */
void animated_variable_cancel(struct sway_animated_variable *av);

/**
 * Set the destination of the variable.
 * 1. If the animation is ending, the variable moves to its final value.
 * 2. If the variable is not affected by the assigned value, it keeps its
 *    animation status and type, so it continues its animation (if any).
 * 3. If it is affected, start a new animation from the current value of
 *    the variable, toward `value`.
 *
 * Returns true if the variable is animating.
 */
bool animated_variable_set(struct sway_animated_variable *av, double value,
	enum sway_animation_type type);

/**
 * Set the span value, meaningful for floating or full screen containers,
 * zero tor tiled ones.
 */
void animated_variable_set_span(struct sway_animated_variable *av, double span);

/**
 * Get the displacement perpendicular to the direction of the layout.
 */
double animated_variable_get_offset(struct sway_animated_variable *av);

/**
 * Move to destination and remove it from the animated variables list.
 */
void animated_variable_finish(struct sway_animated_variable *av);

/**
 * Advance all animating variables one step, stopping those that reach their
 * destrination.
 *
 * Returns the number of variables that are still animating.
 */
size_t animated_variables_update(void);

/**
 * The number of variables that are currently animating.
 */
size_t animated_variables_count(void);

/**
 * Move all animating variables to their destinations, ending their animations.
 */
void animated_variables_finish(void);

/**
 * Configuration for animations
 */
struct sway_animation_config {
	bool enabled;
	enum sway_animation_style style;
	struct sway_animation_path *anim_disabled;
	struct sway_animation_path *anim_default;
	struct sway_animation_path *window_open;
	struct sway_animation_path *window_size;
	struct sway_animation_path *window_move;
	struct sway_animation_path *window_move_float;
	struct sway_animation_path *window_fullscreen;
	struct sway_animation_path *workspace_switch;
	struct sway_animation_path *overview;
	struct sway_animation_path *jump;
	struct sway_animation_path *layer_shell;
	struct sway_animation_path *fade_in;
	struct sway_animation_path *fade_out;
};

// Animation callback
typedef void (*sway_animation_callback_func_t)(void *data);

// callback_begin is the function used to prepare anything the animation needs.
//   it will be called before the animation begins, just once, and only if the
//   animation is enabled. The function and data parameter can be NULL if not
//   needed.
//
// callback_step is the function that will be called at each step of the
//   animation, or once if the animation is disabled. This function cannot be
//   NULL, though its data parameter can be if not needed..
//
// callback_end is the function called when the animation ends. The function
//   and data can be NULL if not needed.

struct sway_animation_callbacks {
	sway_animation_callback_func_t callback_begin;
	void *callback_begin_data;
	sway_animation_callback_func_t callback_step;
	void *callback_step_data;
	sway_animation_callback_func_t callback_end;
	void *callback_end_data;
};

// Key Framed Animation System
struct sway_animation_curve;
struct sway_animation_path;

// Animation Path
struct sway_animation_path *animation_path_create(bool enabled);

void animation_path_destroy(struct sway_animation_path *path);

void animation_path_add_curve(struct sway_animation_path *path,
	struct sway_animation_curve *curve);

bool animation_path_enabled(enum sway_animation_type anim);

// Animation System create/destroy
void animation_create();
void animation_destroy();

// Set the default callbacks
void animation_set_default_callbacks(struct sway_animation_callbacks *callbacks);

// Set/Get the callbacks for the pending animation
void animation_set_callbacks(struct sway_animation_callbacks *callbacks);
struct sway_animation_callbacks *animation_get_callbacks();

// Get a pointer to the animation system configuration
struct sway_animation_config *animation_get_config();

// Set the transaction for the animation state
void animation_set_transaction(struct sway_transaction *transaction);

// Set the pending animation
void animation_set_type(enum sway_animation_type anim);

// Get the animation of the pending transaction, to prepare animated variables
// for it
enum sway_animation_type animation_get_pending_type(void);

// Starts the pending animation if pending is true, otherwise reuse the
// current path (for client-side transactions)
void animation_begin();

// Ends the current animation, moving all animated variables to their final
// values
void animation_end();

// Ends the current animation without moving the animated variables to their
// final values: variables not affected by the new transaction continue their
// animations
void animation_interrupt(void);

// Animates one frame for output
void animation_animate(struct wlr_output *output);

// Returns the output currently being animated, or NULL if not in a
// per-output animation call. Used by callbacks to scope work to a
// single output instead of processing all outputs.
struct wlr_output *animation_get_current_output(void);

// Is an animation enabled?
bool animation_enabled();

// Reset the list of outputs for the animation
void animation_reset_outputs();

/**
 * When an output is disabled or destroyed, we need to reorgainze any possible
 * animations that were happening there, passing them to another output, or
 * ending them if no output is left.
 */
void animation_output_gone(struct wlr_output *output);

// Adds every enabled output to the current animation
void animation_add_all_outputs();

// Are we in the middle of an animation?
bool animation_animating();
// Are we in the middle of an animation for output?
bool animation_animating_output(struct wlr_output *output);

// Create a 3D animation curve
struct sway_animation_curve *create_animation_curve(uint32_t duration_ms,
		uint32_t var_order, list_t *var_points, bool var_simple,
		double offset_scale, uint32_t off_order, list_t *off_points);

#endif
