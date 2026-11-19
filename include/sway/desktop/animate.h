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
	ANIMATION_GESTURE,
	ANIMATION_TYPE_COUNT,
};

/**
 * Configuration for animations. It belongs to the configuration of the
 * compositor: config->animations.
 */
struct sway_animation_config {
	bool enabled;
	enum sway_animation_style style;
	struct {
		struct sway_animation_spec *spec;
		bool disabled; // set to not animate, instead of using the default
	} types[ANIMATION_TYPE_COUNT];
};

// Create the default configuration: enabled, with only the default type set
struct sway_animation_config *animation_config_create(void);
void animation_config_destroy(struct sway_animation_config *animations);

// Create/destroy the animation manager of the server
void animation_create(void);
void animation_destroy(void);

/**
 * End every animation and release whatever was waiting for them, to be used
 * before destroying the tree at exit.
 */
void animation_stop(void);

struct sway_animation_config *animation_get_config(void);

/**
 * Set the spec for a type of animation, or disable the type if spec is
 * NULL. Takes a reference to spec.
 */
void animation_set_spec(enum sway_animation_type type,
	struct sway_animation_spec *spec);

/**
 * The spec to use for a type of animation, NULL if it is not animated.
 */
struct sway_animation_spec *animation_get_spec(enum sway_animation_type type);

// Are animations enabled?
bool animation_enabled(void);
// Is an animation type enabled?
bool animation_type_enabled(enum sway_animation_type type);

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

#endif
