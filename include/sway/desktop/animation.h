#ifndef _SWAY_ANIMATION_H
#define _SWAY_ANIMATION_H
#include <stdint.h>
#include <stdbool.h>
#include "list.h"

/**
 * Animations.
 */

struct wlr_output;

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
};

/**
 * Configuration for animations
 */
struct sway_animation_config {
	bool enabled;
	uint32_t frequency_ms;
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

// Set the pending animation
void animation_set_type(enum sway_animation_type anim);

// Starts the pending animation
void animation_begin();

// Ends the current animation
void animation_end();

// Animates one frame for output
void animation_animate(struct wlr_output *output);

// Is an animation enabled?
bool animation_enabled();

// Reset the list of outputs for the animation
void animation_reset_outputs();
// Adds the output to the current animation
void animation_add_output(struct wlr_output *output);
// Adds every enabled output to the current animation
void animation_add_all_outputs();
// Hints the animation system about a possible early cancel of a running animation
void animation_set_animation_enabled(bool enable);

// Are we in the middle of an animation?
bool animation_animating(struct wlr_output *output);

// Get the current parameters for the active animation
void animation_get_values(double *t, double *x, double *y,
	double *offset_scale);


// Create a 3D animation curve
struct sway_animation_curve *create_animation_curve(uint32_t duration_ms,
		uint32_t var_order, list_t *var_points, bool var_simple,
		double offset_scale, uint32_t off_order, list_t *off_points);
void destroy_animation_curve(struct sway_animation_curve *curve);

#endif
