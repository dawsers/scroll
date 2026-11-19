#ifndef _SWAY_ANIMATION_H
#define _SWAY_ANIMATION_H
#include <stdint.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include "list.h"

/**
 * Animations.
 *
 * The animation system is independent of the rest of the compositor: it does
 * not know about transactions, nodes, the scene graph or the configuration.
 * This header provides the interface to use it.
 *
 * - An animated variable holds the current value of something that can be
 *   animated (a position, a size, an opacity...).
 * - An animation spec describes a kind of motion (a Bezier path, a spring, a
 *   decay...).
 * - Each output owns a clock, which returns the presentation time for a frame.
 *   We sample animated variables at that time.
 * - A group notifies its creator when a set of animations is over.
 * - The manager keeps the running variables, clocks and groups.
 *
 * All times are in nanoseconds, in the CLOCK_MONOTONIC domain.
 */

struct sway_animation_manager;
struct sway_animation_clock;
struct sway_animation_group;
struct sway_animation_tracker;

/**
 * Animation specs
 *
 * Describes how a variable moves to its target. Specs are immutable and
 * reference counted. A NULL spec means no animation.
 */
struct sway_animation_spec;
struct sway_animation_path;
struct sway_animation_curve;

// A path of Bezier curves, with a fixed duration. Takes ownership of path.
struct sway_animation_spec *animation_spec_create_path(struct sway_animation_path *path);
// A spring that takes about `response_ms` to reach its target, with the
// given damping ratio (1.0 is critically damped, less than 1.0 bounces).
struct sway_animation_spec *animation_spec_create_spring(double response_ms,
	double damping_ratio);
// An exponential decay towards the target. `rate` is the fraction of the
// distance lost per second, as an exponent.
struct sway_animation_spec *animation_spec_create_decay(double rate);

struct sway_animation_spec *animation_spec_ref(struct sway_animation_spec *spec);
void animation_spec_unref(struct sway_animation_spec *spec);

// Key Framed Animation System (for Bezier)
struct sway_animation_path *animation_path_create(void);

// Takes ownership of curve
void animation_path_add_curve(struct sway_animation_path *path,
	struct sway_animation_curve *curve);

// Create a 3D animation curve
struct sway_animation_curve *create_animation_curve(uint32_t duration_ms,
		uint32_t var_order, list_t *var_points, bool var_simple,
		double offset_scale, uint32_t off_order, list_t *off_points);

/**
 * Clocks
 *
 * A clock computes when to display the frame that is being built for an output.
 * At its next refresh, if the output reports its refresh interval, or now if
 * it does not.
 */
struct sway_animation_clock_impl {
	// Ask the owner of the clock for a new frame
	void (*schedule_frame)(struct sway_animation_clock *clock);
};

struct sway_animation_clock {
	const struct sway_animation_clock_impl *impl;
	struct sway_animation_manager *manager; // NULL if the clock is not in use
	void *data;

	int64_t last_present; // presentation time for the last frame
	int64_t refresh; // interval to the next refresh, 0 if unknown
	int64_t frame_time; // presentation time of the frame being built

	bool dirty; // values changed outside of a frame, display them
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

// Call when beginning to build a frame. Returns the clock's frame time.
int64_t animation_clock_begin_frame(struct sway_animation_clock *clock);

// Is there anything to animate for this clock?
bool animation_clock_active(struct sway_animation_clock *clock);

/**
 * Groups
 *
 * A group of animated variables
 * Notifies when their animations are over: every variable has finished its
 * animation, or started a different one. The notification happens in
 * animation_manager_dispatch()
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
enum sway_animated_variable_type {
	ANIMATED_VARIABLE_NONE,
	ANIMATED_VARIABLE_SIZE, // never smaller than 1
	ANIMATED_VARIABLE_POSITION, // may be displaced perpendicularly to its motion
	ANIMATED_VARIABLE_FADE, // never leaves the range of its animation
};

// What an animation needs to know about the variable it moves
struct sway_animator_state {
	double x0, v0, x1; // initial value and velocity, final value
	bool position; // the variable is a position, it may use an offset
	double epsilon; // distance to the target that is not worth animating
	int64_t duration; // time the animation takes, in nanoseconds
};

struct sway_animated_variable {
	double value; // current value
	double target; // value when the animation ends
	double origin; // value when the animation started
	double offset; // displacement perpendicular to the motion, normalized
	double span; // to animate the position of floating and fullscreen containers
	enum sway_animated_variable_type type;
	bool animating; // the value is changing: running an animation or tracking

	// Private to the animation system
	struct sway_animation_manager *manager;
	struct sway_animation_spec *spec; // of the running animation
	struct sway_animator_state state;
	int64_t t0; // start time of the running animation
	int64_t sampled_at; // time of the current value
	double velocity; // velocity at the next animation beginning

	struct sway_animation_tracker *tracker; // NULL while not tracking
	struct sway_animation_clock *clock; // NULL means sampled by every clock
	struct sway_animation_group *group;
	struct wl_list link; // clock.running or manager.unbound
};

struct sway_animation_options {
	// How to move. NULL means no animation
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
 * input events. There is no origin, target or duration. The variable keeps
 * track of its velocity.
 *
 * `clock` is the clock of the output that shows the variable, NULL for any.
 * `owner` identifies who is tracking: a gesture, a drag. The variable tracks
 * until its owner calls animation_manager_untrack(), and nothing else can
 * animate it until then.
 */
void animated_variable_track(struct sway_animated_variable *av, double value,
	struct sway_animation_clock *clock, const void *owner);

/**
 * Stop tracking every variable `owner` is tracking. The next animation of
 * each of them starts with the velocity it had.
 */
void animation_manager_untrack(struct sway_animation_manager *manager,
	const void *owner);

/**
 * The clock (every clock if NULL) needs a frame to show new values.
 * Used for variables that changed without an animation.
 */
void animation_manager_damage(struct sway_animation_manager *manager,
	struct sway_animation_clock *clock);

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
 * zero for tiled ones.
 */
void animated_variable_set_span(struct sway_animated_variable *av, double span);

/**
 * Get the displacement perpendicular to the direction of the layout.
 */
double animated_variable_get_offset(struct sway_animated_variable *av);

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
 * The manager uses the event loop to schedule tasks that cannot happen
 * immediately. See animation_manager_flush() and
 * animation_manager_check_expired(). It stops using the loop when the loop
 * is destroyed. If loop is NULL, the owner has to call those functions.
 */
struct sway_animation_manager *animation_manager_create(struct wl_event_loop *loop,
	const struct sway_animation_manager_impl *impl, void *data);

void animation_manager_destroy(struct sway_animation_manager *manager);

/**
 * End every animation and notify every group, and do not start any more
 * animations. Used at exit, while the owners of the variables still exist.
 */
void animation_manager_stop(struct sway_animation_manager *manager);

// The current time
int64_t animation_manager_now(struct sway_animation_manager *manager);

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

// End the animations that should have ended a while ago, but had no frames
// to do it (their output is off, for example).
void animation_manager_check_expired(struct sway_animation_manager *manager);

// Are there any animations running?
bool animation_manager_active(struct sway_animation_manager *manager);

// End every animation, moving the variables to their targets.
void animation_manager_finish_all(struct sway_animation_manager *manager);

#endif
