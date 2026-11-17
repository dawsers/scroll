#ifndef _SWAY_ANIMATE_H
#define _SWAY_ANIMATE_H
#include "sway/desktop/animation.h"

/**
 * Maps the animation needs of the compositor to functions in the animation
 * library. The animation system is independent of the compositor, these
 * functions create interfaces that are compositor-aware.
 */

struct sway_output;
struct wlr_output;

// Create/destroy the animation manager of the server
void animation_create(void);
void animation_destroy(void);

/**
 * The event loop of the server is going to be destroyed: animations cannot
 * use it anymore.
 */
void animation_release_loop(void);

/**
 * End every animation and release whatever was waiting for them, to be used
 * before destroying the tree at exit.
 */
void animation_stop(void);

struct sway_animation_config *animation_get_config(void);

/**
 * The spec to use for a type of animation, NULL if it is not animated.
 */
struct sway_animation_spec *animation_get_spec(enum sway_animation_type type);

// Are animations enabled?
bool animation_enabled(void);
// Is an animation type enabled?
bool animation_path_enabled(enum sway_animation_type type);

// Is there any animation running?
bool animation_animating(void);
// Is there anything to animate for output?
bool animation_animating_output(struct wlr_output *output);

// Ends every animation, moving all animated variables to their final values
void animation_end(void);

// The clock for the animations in output, NULL if it has none
struct sway_animation_clock *output_animation_clock(struct sway_output *output);

void output_animation_clock_init(struct sway_output *output);
void output_animation_clock_finish(struct sway_output *output);

// Animates one frame for output
void animation_animate(struct wlr_output *output);

// The time to use for the values set with animated_variable_track()
int64_t animation_now(void);

#endif
