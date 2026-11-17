#ifndef _SWAY_ANIMATION_H
#define _SWAY_ANIMATION_H
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <wayland-server-core.h>
#include "list.h"

/**
 * Animations.
 *
 * The animation system is independent of the rest of the compositor: it does
 * not know about transactions, nodes or the scene graph. This header provides
 * the interface to use t.
 *
 * - An animated variable holds the current value of something that can be
 *   animated (a position, a size, an opacity...).
 * - An animation spec describes a kind of motion (a Bezier path, a spring, a
 *   decay...). The code that computes the motion is an animator.
 * - Each ourput owns a clock, which returns the presentation time for a frame.
 *   We sample animated variables at that time.
 * - A group notifies its creator when a set of animations is over.
 * - The manager keeps the running variables, clocks and groups.
 *
 * All times are in nanoseconds, in the CLOCK_MONOTONIC domain.
 */

struct sway_animation_manager;
struct sway_animation_clock;
struct sway_animation_group;
struct sway_animation_spec;
struct sway_animation_tracker;

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

enum sway_animated_variable_type {
	ANIMATED_VARIABLE_NONE,
	ANIMATED_VARIABLE_SIZE,
	ANIMATED_VARIABLE_POSITION,
	ANIMATED_VARIABLE_FADE,
};

/**
 * Animators
 */

struct sway_animation_sample {
	double value;
	double offset; // displacement perpendicular to the motion, normalized
	bool done;
};

// Stores the state of an animated variable through its duration.
struct sway_animator_state {
	double x0, v0, x1;
	bool position; // the variable is a position, it may use an offset
	double epsilon; // distance to the target that is not worth animating
	int64_t duration; // duration of the animation in nanoseconds
};

// Generic interface for an animator.
struct sway_animator_impl {
	const char *name;
	bool uses_velocity;  // uses the initial velocity
	// Optional
	void (*start)(struct sway_animator_state *state,
		const struct sway_animation_spec *spec);
	// Returns the sample value at a given time
	void (*sample)(const struct sway_animator_state *state,
		const struct sway_animation_spec *spec, int64_t elapsed,
		struct sway_animation_sample *sample);
	// Optional
	void (*destroy)(struct sway_animation_spec *spec);
};

struct sway_animation_path;
struct sway_animation_curve;

/**
 * Describes an animation.
 * Specs are immutable and reference counted.
 */
struct sway_animation_spec;

// A path of Bezier curves.
struct sway_animation_spec *animation_spec_create_path(struct sway_animation_path *path);
// A spring that takes about `response_ms` to reach its target, with the
// given damping ratio (1.0 is critically damped, less than 1.0 bounces).
struct sway_animation_spec *animation_spec_create_spring(bool enabled,
	double response_ms, double damping_ratio);
// An exponential decay towards the target. `rate` is the fraction of the
// distance (or of the velocity) lost per second, as an exponent.
struct sway_animation_spec *animation_spec_create_decay(bool enabled, double rate);
// An animator provided by the caller, to compute a value per frame.
struct sway_animation_spec *animation_spec_create_custom(
	const struct sway_animator_impl *impl, void *data);
// Data of a custom spec, NULL for any other.
void *animation_spec_get_data(const struct sway_animation_spec *spec);

struct sway_animation_spec *animation_spec_ref(struct sway_animation_spec *spec);
void animation_spec_unref(struct sway_animation_spec *spec);

// Where something moving at `velocity` from `value` would stop with a decay
double animation_decay_rest(const struct sway_animation_spec *spec,
	double value, double velocity);


// Key Framed Animation System (for Bezier)
struct sway_animation_path *animation_path_create(bool enabled);

void animation_path_destroy(struct sway_animation_path *path);

void animation_path_add_curve(struct sway_animation_path *path,
	struct sway_animation_curve *curve);

// Create a 3D animation curve
struct sway_animation_curve *create_animation_curve(uint32_t duration_ms,
		uint32_t var_order, list_t *var_points, bool var_simple,
		double offset_scale, uint32_t off_order, list_t *off_points);

/**
 * Clocks
 */

struct sway_animation_clock_impl {
	// Ask the owner of the clock for a new frame
	void (*schedule_frame)(struct sway_animation_clock *clock);
};

struct sway_animation_clock {
	const struct sway_animation_clock_impl *impl;
	struct sway_animation_manager *manager;
	void *data;

	bool presented; // there is presentation feedback
	int64_t last_present; // presentation time for the last frame
	int64_t refresh; // interval to the next refresh, 0 if unknown
	int64_t latency; // smoothed time from building a frame to presenting it

	int64_t frame_time; // presentation time of the frame being built
	int64_t frame_wall; // when the frame started being built
	int64_t last_tick; // last time the clock showed signs of life

	bool dirty; // invalidates the clock values
	struct wl_list running; // sway_animated_variable.link
	struct wl_list link; // sway_animation_manager.clocks
};

void animation_clock_init(struct sway_animation_clock *clock,
	struct sway_animation_manager *manager,
	const struct sway_animation_clock_impl *impl, void *data);


// The owner of the clock is gone (for example. output disabled).
// Variables running on it move to other clock, or end if there is none.
void animation_clock_finish(struct sway_animation_clock *clock);

// Call after presentation. A frame was presented at `when` and `refresh` is
// the time to the next refresh, 0 if unknown.
void animation_clock_presented(struct sway_animation_clock *clock,
	int64_t when, int64_t refresh);

// Call when beginning to build a frame. Updates the clock's presentation time.
int64_t animation_clock_begin_frame(struct sway_animation_clock *clock);

// Is there anything to animate for this clock?
bool animation_clock_active(struct sway_animation_clock *clock);

/**
 * Groups
 *
 * A group of animated variables
 * Notifies when their animations are over: every variable has finished its
 * animation, or started a different one.
 */
typedef void (*sway_animation_group_done_func_t)(void *data);

struct sway_animation_group *animation_group_create(struct sway_animation_manager *manager,
	sway_animation_group_done_func_t done, void *data);

// No more variables will join the group. If the group is empty, it is done.
void animation_group_commit(struct sway_animation_group *group);

// Is the group empty?
bool animation_group_empty(struct sway_animation_group *group);

struct sway_animation_group *animation_group_ref(struct sway_animation_group *group);
void animation_group_unref(struct sway_animation_group *group);


/**
 * Animated variables
 *
 * Every variable that takes part in an animation has one of these. Its value
 * is used to place and size nodes in the scene graph.
 *
 * Variables are independent of each other. When a variable gets a new target
 * while it is animating, it continues from the value it has. Variables that
 * keep their target are not affected by what happens to others.
 */
struct sway_animated_variable {
	double value; // current value
	double target; // value when the animation ends
	double origin; // value when the animation started
	double offset; // displacement perpendicular to the motion, normalized
	double velocity; // velocity to start the next animation with
	enum sway_animated_variable_type type;
	double span; // to animate the position of floating and fullscreen containers

	bool animating; // the value is changing: running an animation or tracking
	bool running; // the variable is in a list of running variables
	bool tracking; // the value is set by animated_variable_track()

	struct sway_animation_spec *spec; // of the running animation
	struct sway_animator_state state;
	int64_t t0; // start time of the running animation
	int64_t sampled_at; // time of the current value

	struct sway_animation_tracker *tracker;
	struct sway_animation_manager *manager;
	struct sway_animation_clock *clock; // NULL: sampled by every clock
	struct sway_animation_group *group;
	struct wl_list link; // clock.running or manager.unbound
};

struct sway_animation_options {
	// How to move. NULL or a disabled spec means no animation
	struct sway_animation_spec *spec;
	// Who wants to know when the animation is over. Optional
	struct sway_animation_group *group;
	// The clock of the output where the variable is seen. NULL means any output
	struct sway_animation_clock *clock;
};

/**
 * Initialize an animated variable with `value` default value and type `type`.
 */
void animated_variable_init(struct sway_animated_variable *av,
	struct sway_animation_manager *manager, double value,
	enum sway_animated_variable_type type);

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
 * Move the variable to `value` now, ending its animation if it has one.
 */
void animated_variable_jump(struct sway_animated_variable *av, double value);

/**
 * Set the destination of the variable.
 * 1. If the variable is tracking, only its target changes.
 * 2. If the variable is not affected by the assigned value, it keeps its
 *    animation, so it continues it (if any).
 * 3. If it is affected, start a new animation from the current value of
 *    the variable, toward `value`.
 *
 * Returns true if the variable is animating.
 */
bool animated_variable_set(struct sway_animated_variable *av, double value,
	const struct sway_animation_options *options);

/**
 * Continuous animations: the value is provided by the caller, usually from
 * input events, with the time of the event. There is no origin, target or
 * duration. The variable keeps track of its velocity.
 *
 * `clock` is the clock of the output that shows the variable, NULL for any.
 */
void animated_variable_track(struct sway_animated_variable *av, double value,
	int64_t time, struct sway_animation_clock *clock);

/**
 * Stop tracking. The next animation of the variable starts with the velocity
 * it had.
 */
void animated_variable_untrack(struct sway_animated_variable *av);

/**
 * The velocity of the variable now, in units per second.
 */
double animated_variable_get_velocity(struct sway_animated_variable *av);

/**
 * Inherit the animation state of `parent` into `child`, so both variables
 * animate together with identical values.
 *
 * This is used for tiled containers: a child shares one axis with its
 * parent, so it must move together with it.
 */
void animated_variable_inherit(struct sway_animated_variable *child,
	struct sway_animated_variable *parent);

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
 * Move to destination and end the animation.
 */
void animated_variable_finish(struct sway_animated_variable *av);

/**
 * The value of the variable at any time, without changing it.
 */
double animated_variable_sample_at(const struct sway_animated_variable *av,
	int64_t time);

/**
 * Configuration for animations
 */
struct sway_animation_config {
	bool enabled;
	enum sway_animation_style style;
	struct sway_animation_spec *specs[ANIMATION_TYPE_COUNT];
};

/**
 * The manager
 */
struct sway_animation_manager_impl {
	// The current time
	int64_t (*now)(void *data);
	// Optional.
	// Values changed outside of a frame (animations ended because their
	// output is gone, for example), and whatever depends on them needs to be
	// updated.
	void (*apply)(void *data);
};

/**
 * If loop is NULL, then animation_manager_flush() and
 * animation_manager_check_stalled() need to be called by the owner.
 */
struct sway_animation_manager *animation_manager_create(struct wl_event_loop *loop,
	const struct sway_animation_manager_impl *impl, void *data);

void animation_manager_destroy(struct sway_animation_manager *manager);

/**
 * Stop using the event loop because the server is exiting. From now on the
 * owner has to call animation_manager_flush() itself.
 */
void animation_manager_release_loop(struct sway_animation_manager *manager);

/**
 * End every animation and notify every group, and do not start any more
 * animations. Used at exit, while the owners of the variables still exist.
 */
void animation_manager_stop(struct sway_animation_manager *manager);

// Evaluate the manager's `now` function to get the current time.
int64_t animation_manager_now(struct sway_animation_manager *manager);

// Get a pointer to the animation system configuration.
struct sway_animation_config *animation_manager_get_config(
	struct sway_animation_manager *manager);

// Set the spec for a type of animation. Takes the reference.
void animation_manager_set_spec(struct sway_animation_manager *manager,
	enum sway_animation_type type, struct sway_animation_spec *spec);

// The spec for a type of animation, NULL if it is not animated.
struct sway_animation_spec *animation_manager_get_spec(
	struct sway_animation_manager *manager, enum sway_animation_type type);

/**
 * Sample the variables that run on the clock at its frame time. Returns true
 * if there was anything to sample.
 */
bool animation_manager_advance(struct sway_animation_manager *manager,
	struct sway_animation_clock *clock);

// Notify the groups that are done.
void animation_manager_dispatch(struct sway_animation_manager *manager);

// Apply the changes made outside of a frame, and notify the groups.
void animation_manager_flush(struct sway_animation_manager *manager);

// End the animations of the clocks that stopped having frames.
void animation_manager_check_stalled(struct sway_animation_manager *manager);

// Are there any animations running?
bool animation_manager_active(struct sway_animation_manager *manager);

// The number of variables running an animation
size_t animation_manager_count(struct sway_animation_manager *manager);

// End every animation, moving the variables to their targets.
void animation_manager_finish_all(struct sway_animation_manager *manager);

#endif
