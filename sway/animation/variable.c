#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"
#include "sway/log.h"

// Positions are animated only when the change is big enough to be seen
#define ANIMATED_VARIABLE_EPSILON 0.000001
#define ANIMATED_VARIABLE_POSITION_EPSILON 1.0

#define NSEC_PER_SEC 1000000000.0
#define NSEC_PER_MSEC 1000000LL

/**
 * Tracking: the last values set with animated_variable_track(), to know the
 * velocity of the variable when tracking ends.
 */
#define TRACKER_SAMPLES 16
// Only the most recent samples tell the current velocity
#define TRACKER_WINDOW (100 * NSEC_PER_MSEC)
// Without samples for this long, the variable is not moving
#define TRACKER_STALE (60 * NSEC_PER_MSEC)

struct sway_animation_tracker {
	const void *owner;
	struct sway_animated_variable *variable;
	struct wl_list link; // sway_animation_manager.tracking
	int64_t time[TRACKER_SAMPLES];
	double value[TRACKER_SAMPLES];
	int count;
	int next;
};

static void tracker_add(struct sway_animation_tracker *tracker, double value,
		int64_t time) {
	tracker->time[tracker->next] = time;
	tracker->value[tracker->next] = value;
	tracker->next = (tracker->next + 1) % TRACKER_SAMPLES;
	if (tracker->count < TRACKER_SAMPLES) {
		++tracker->count;
	}
}

static double tracker_velocity(struct sway_animation_tracker *tracker, int64_t now) {
	if (tracker->count < 2) {
		return 0.0;
	}
	const int last = (tracker->next + TRACKER_SAMPLES - 1) % TRACKER_SAMPLES;
	if (now - tracker->time[last] > TRACKER_STALE) {
		return 0.0;
	}
	int first = last;
	for (int i = 1; i < tracker->count; ++i) {
		const int idx = (last + TRACKER_SAMPLES - i) % TRACKER_SAMPLES;
		if (tracker->time[last] - tracker->time[idx] > TRACKER_WINDOW) {
			break;
		}
		first = idx;
	}
	const int64_t dt = tracker->time[last] - tracker->time[first];
	if (dt < NSEC_PER_MSEC) {
		return 0.0;
	}
	return (tracker->value[last] - tracker->value[first]) / (dt / NSEC_PER_SEC);
}

static double change_epsilon(const struct sway_animated_variable *av) {
	return av->type == ANIMATED_VARIABLE_POSITION ?
		ANIMATED_VARIABLE_POSITION_EPSILON : ANIMATED_VARIABLE_EPSILON;
}

static double settle_epsilon(const struct sway_animated_variable *av) {
	switch (av->type) {
	case ANIMATED_VARIABLE_POSITION:
	case ANIMATED_VARIABLE_SIZE:
		return 0.5;
	default:
		return 0.001;
	}
}

static void variable_settle(struct sway_animated_variable *av, double value) {
	av->origin = av->target = value;
	av->value = av->type == ANIMATED_VARIABLE_SIZE ? fmax(1.0, value) : value;
	av->offset = 0.0;
}

// Compute the current value of the variable from a sample of its animation
static double variable_sample_value(const struct sway_animated_variable *av,
		const struct sway_animation_sample *sample) {
	double value = sample->value;
	switch (av->type) {
	case ANIMATED_VARIABLE_POSITION:
		value += sample->offset * av->span;
		break;
	case ANIMATED_VARIABLE_FADE:;
		const double min = fmin(av->state.x0, av->state.x1);
		const double max = fmax(av->state.x0, av->state.x1);
		value = fmin(fmax(value, min), max);
		break;
	case ANIMATED_VARIABLE_SIZE:
		value = fmax(1.0, value);
		break;
	case ANIMATED_VARIABLE_NONE:
		break;
	}
	return value;
}

// A variable is running an animation while it has a spec, and it is tracking
// while it has a tracker. It never does both.
static void variable_update_animating(struct sway_animated_variable *av) {
	av->animating = av->spec || av->tracker;
}

static struct sway_animation_clock *live_clock(struct sway_animation_clock *clock) {
	return clock && clock->manager ? clock : NULL;
}

static bool variable_linked(const struct sway_animated_variable *av) {
	return !wl_list_empty(&av->link);
}

// Add a running variable to the list of the clock that samples it
static void variable_link(struct sway_animated_variable *av,
		struct sway_animation_clock *clock) {
	clock = live_clock(clock);
	if (variable_linked(av)) {
		if (av->clock == clock) {
			return;
		}
		animation_manager_variable_stopped(av->manager, av);
	}
	av->clock = clock;
	animation_manager_variable_started(av->manager, av);
}

static void variable_unlink(struct sway_animated_variable *av) {
	if (variable_linked(av)) {
		animation_manager_variable_stopped(av->manager, av);
	}
	av->clock = NULL;
}

// End the animation of the variable, leaving its value where it is
static void variable_stop(struct sway_animated_variable *av) {
	variable_unlink(av);
	animation_spec_unref(av->spec);
	av->spec = NULL;
	struct sway_animation_group *group = av->group;
	av->group = NULL;
	if (group) {
		animation_group_leave(group);
	}
	variable_update_animating(av);
}

// Move the variable to `value` now, ending its animation if it has one
static void variable_jump(struct sway_animated_variable *av, double value) {
	variable_stop(av);
	variable_settle(av, value);
	av->velocity = 0.0;
}

void animated_variable_init(struct sway_animated_variable *av,
		struct sway_animation_manager *manager, double value,
		enum sway_animated_variable_type type) {
	memset(av, 0, sizeof(struct sway_animated_variable));
	av->manager = manager;
	av->type = type;
	variable_settle(av, value);
	wl_list_init(&av->link);
}

// Stop tracking, with no velocity to hand over
static void variable_drop_tracker(struct sway_animated_variable *av) {
	if (!av->tracker) {
		return;
	}
	wl_list_remove(&av->tracker->link);
	free(av->tracker);
	av->tracker = NULL;
}

void animated_variable_release(struct sway_animated_variable *av) {
	if (!av->manager) {
		// It was never initialized
		return;
	}
	variable_drop_tracker(av);
	variable_stop(av);
}

void animated_variable_reset(struct sway_animated_variable *av, double value) {
	if (av->animating) {
		return;
	}
	variable_settle(av, value);
}

void animated_variable_finish(struct sway_animated_variable *av) {
	if (av->spec) {
		variable_jump(av, av->target);
	}
}

static void variable_sample(const struct sway_animated_variable *av,
		int64_t time, struct sway_animation_sample *sample) {
	const int64_t elapsed = time > av->t0 ? time - av->t0 : 0;
	av->spec->impl->sample(&av->state, av->spec, elapsed, sample);
}

bool animated_variable_advance(struct sway_animated_variable *av, int64_t time) {
	struct sway_animation_sample sample;
	variable_sample(av, time, &sample);
	av->sampled_at = time;
	if (sample.done) {
		variable_jump(av, sample.value);
		return false;
	}
	av->value = variable_sample_value(av, &sample);
	av->offset = sample.offset;
	return true;
}

double animated_variable_get_velocity(struct sway_animated_variable *av) {
	if (av->tracker) {
		return tracker_velocity(av->tracker, animation_manager_now(av->manager));
	}
	if (!av->spec) {
		return av->velocity;
	}
	// The slope of the animation at the time of its current value
	const int64_t dt = NSEC_PER_MSEC;
	int64_t t1 = av->sampled_at > av->t0 ? av->sampled_at : av->t0;
	int64_t t0 = t1 - dt;
	if (t0 < av->t0) {
		t0 = av->t0;
		t1 = t0 + dt;
	}
	struct sway_animation_sample s0, s1;
	variable_sample(av, t0, &s0);
	variable_sample(av, t1, &s1);
	return (s1.value - s0.value) / (dt / NSEC_PER_SEC);
}

// The velocity carried from tracking is only good for a while
#define VELOCITY_VALID (100 * NSEC_PER_MSEC)

bool animated_variable_set(struct sway_animated_variable *av, double value,
		const struct sway_animation_options *options) {
	if (av->tracker) {
		// Its owner moves it: this is where it goes when tracking ends
		av->target = value;
		return true;
	}
	const double epsilon = change_epsilon(av);
	struct sway_animation_clock *clock = options ? options->clock : NULL;
	if (av->spec && fabs(value - av->target) <= epsilon) {
		// This running variable doesn't change enough, so it keeps its animation
		// for its remaining duration
		variable_link(av, clock);
		return true;
	}
	struct sway_animation_spec *spec = options ? options->spec : NULL;
	if (!spec || av->manager->stopped || fabs(value - av->value) <= epsilon) {
		// Not animated or not moving: move to the final value now
		variable_jump(av, value);
		return false;
	}

	// New or interrupted animation: continue from the value we have right now
	const int64_t now = options->group ?
		options->group->start : animation_manager_now(av->manager);
	double velocity = 0.0;
	int64_t t0 = now;
	if (av->spec) {
		if (spec->impl->uses_velocity) {
			velocity = animated_variable_get_velocity(av);
		}
		// Continue from the time of the value that is on screen, so there
		// are no jumps, unless the variable stopped having frames.
		if (av->sampled_at > now - VELOCITY_VALID) {
			t0 = av->sampled_at;
		}
	} else if (now - av->sampled_at <= VELOCITY_VALID) {
		velocity = av->velocity;
	}
	animation_spec_ref(spec);
	variable_stop(av);

	av->velocity = 0.0;
	av->origin = av->value;
	av->target = value;
	av->offset = 0.0;
	av->spec = spec;
	av->t0 = t0;
	av->sampled_at = t0;
	av->state = (struct sway_animator_state){
		.x0 = av->value,
		.v0 = velocity,
		.x1 = value,
		.position = av->type == ANIMATED_VARIABLE_POSITION,
		.epsilon = settle_epsilon(av),
	};
	spec->impl->start(&av->state, spec);
	if (options->group) {
		av->group = options->group;
		animation_group_join(av->group);
	}
	variable_update_animating(av);
	variable_link(av, clock);
	return true;
}

void animated_variable_track(struct sway_animated_variable *av, double value,
		struct sway_animation_clock *clock, const void *owner) {
	if (!av->tracker && !av->manager->stopped) {
		av->tracker = calloc(1, sizeof(struct sway_animation_tracker));
		if (av->tracker) {
			av->tracker->variable = av;
			wl_list_insert(&av->manager->tracking, &av->tracker->link);
		}
	}
	if (!av->tracker) {
		variable_jump(av, value);
		return;
	}
	av->tracker->owner = owner;
	variable_stop(av);
	const int64_t now = animation_manager_now(av->manager);
	tracker_add(av->tracker, value, now);
	// The target is still where the variable will rest when tracking ends
	const double target = av->target;
	variable_settle(av, value);
	av->target = target;
	av->velocity = 0.0;
	av->sampled_at = now;
	animation_manager_damage(av->manager, live_clock(clock));
}

static void variable_untrack(struct sway_animated_variable *av) {
	const int64_t now = animation_manager_now(av->manager);
	av->velocity = tracker_velocity(av->tracker, now);
	av->sampled_at = now;
	variable_drop_tracker(av);
	variable_update_animating(av);
}

void animation_manager_untrack(struct sway_animation_manager *manager,
		const void *owner) {
	struct sway_animation_tracker *tracker, *tmp;
	wl_list_for_each_safe(tracker, tmp, &manager->tracking, link) {
		if (tracker->owner == owner) {
			variable_untrack(tracker->variable);
		}
	}
}

void animation_manager_untrack_all(struct sway_animation_manager *manager) {
	while (!wl_list_empty(&manager->tracking)) {
		struct sway_animation_tracker *tracker =
			wl_container_of(manager->tracking.next, tracker, link);
		variable_untrack(tracker->variable);
	}
}

void animated_variable_inherit(struct sway_animated_variable *child,
		struct sway_animated_variable *parent) {
	if (!child || !parent || child == parent) {
		return;
	}
	if (!sway_assert(child->type == parent->type,
		"animated_variable_inherit: cannot inherit a variable of a different type")) {
		return;
	}

	child->value = parent->value;
	child->target = parent->target;
	child->origin = parent->origin;
	child->offset = parent->offset;
	child->velocity = parent->velocity;
	child->span = parent->span;
	child->state = parent->state;
	child->t0 = parent->t0;
	child->sampled_at = parent->sampled_at;
	if (child->spec != parent->spec) {
		animation_spec_unref(child->spec);
		child->spec = parent->spec ? animation_spec_ref(parent->spec) : NULL;
	}
	// The parent is the one in the group
	struct sway_animation_group *group = child->group;
	child->group = NULL;
	if (group) {
		animation_group_leave(group);
	}
	variable_drop_tracker(child);
	if (child->spec) {
		variable_link(child, parent->clock);
	} else {
		variable_unlink(child);
	}
	variable_update_animating(child);
}

void animated_variable_set_span(struct sway_animated_variable *av, double span) {
	if (av->span == span) {
		return;
	}
	if (av->type == ANIMATED_VARIABLE_POSITION) {
		av->value += av->offset * (span - av->span);
	}
	av->span = span;
}

double animated_variable_get_offset(struct sway_animated_variable *av) {
	return av->offset * fabs(av->target - av->origin);
}
