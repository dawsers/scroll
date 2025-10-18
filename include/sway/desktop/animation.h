#ifndef _SWAY_ANIMATION_H
#define _SWAY_ANIMATION_H
#include <stdint.h>
#include <stdbool.h>
#include "list.h"

/**
 * Animations.
 */

enum sway_animation_style {
	ANIM_STYLE_CLIP,
	ANIM_STYLE_SCALE
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

// Animation
// Set the callbacks for the current animation
void animation_set_callbacks(struct sway_animation_callbacks *callbacks);
struct sway_animation_callbacks *animation_get_callbacks();

// Set the active path for the animation
void animation_set_path(struct sway_animation_path *path);

// Get the current active path for the animation
struct sway_animation_path *animation_get_path();

// Starts animating using the next key (next curve in the animation path)
void animation_next_key();

// Is an animation enabled?
bool animation_enabled();

// Get the current parameters for the active animation
void animation_get_values(double *t, double *x, double *y,
	double *offset_scale);


// Create a 3D animation curve
struct sway_animation_curve *create_animation_curve(uint32_t duration_ms,
		uint32_t var_order, list_t *var_points, bool var_simple,
		double offset_scale, uint32_t off_order, list_t *off_points);
void destroy_animation_curve(struct sway_animation_curve *curve);

#endif
