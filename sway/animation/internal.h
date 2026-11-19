#ifndef _SWAY_ANIMATION_INTERNAL_H
#define _SWAY_ANIMATION_INTERNAL_H
#include "sway/desktop/animation.h"

struct sway_animation_manager {
	const struct sway_animation_manager_impl *impl;
	void *data;
	struct wl_event_loop *loop;
	struct wl_listener loop_destroy;
	struct wl_event_source *idle;
	struct wl_event_source *watchdog;
	bool watchdog_armed;
	bool stopped;

	struct wl_list clocks; // sway_animation_clock.link
	struct wl_list unbound; // sway_animated_variable.link, sampled by every clock
	size_t running; // number of variables running an animation
	struct wl_list tracking; // sway_animation_tracker.link

	struct wl_list finished; // sway_animation_group.link
	bool needs_apply;
};

struct sway_animation_group {
	struct sway_animation_manager *manager;
	int refs;
	size_t members;
	int64_t start; // every member starts at this time
	bool committed;
	bool queued;
	sway_animation_group_done_func_t done;
	void *data;
	struct wl_list link; // sway_animation_manager.finished
};

/**
 * Animators: the code that computes the motion of an animated variable.
 */
struct sway_animation_sample {
	double value;
	double offset; // displacement perpendicular to the motion, normalized
	bool done;
};

struct sway_animator_impl {
	bool uses_velocity; // uses the initial velocity
	// Called when an animation starts. Sets state->duration: the animation
	// is done when that time has elapsed.
	void (*start)(struct sway_animator_state *state,
		const struct sway_animation_spec *spec);
	// The value at a given time. It is a pure function of its parameters.
	void (*sample)(const struct sway_animator_state *state,
		const struct sway_animation_spec *spec, int64_t elapsed,
		struct sway_animation_sample *sample);
	// Optional
	void (*destroy)(struct sway_animation_spec *spec);
};

// Every animator needs to have this structure as the first member of its
// spec, so a spec can be freed through this pointer
struct sway_animation_spec {
	int refs;
	const struct sway_animator_impl *impl;
};

void animation_spec_init(struct sway_animation_spec *spec,
	const struct sway_animator_impl *impl);

void animation_group_join(struct sway_animation_group *group);
void animation_group_leave(struct sway_animation_group *group);

// Something has to be done when the event loop is idle
void animation_manager_schedule_flush(struct sway_animation_manager *manager);
// A variable started running
void animation_manager_variable_started(struct sway_animation_manager *manager,
	struct sway_animated_variable *av);
// A variable stopped running
void animation_manager_variable_stopped(struct sway_animation_manager *manager,
	struct sway_animated_variable *av);
// The clock (every clock if NULL) needs a frame to show new values
void animation_manager_damage(struct sway_animation_manager *manager,
	struct sway_animation_clock *clock);

// Sample a running variable at `time`. Returns false if its animation ended.
bool animated_variable_advance(struct sway_animated_variable *av, int64_t time);
// Move a running variable to its target, ending its animation.
void animated_variable_finish(struct sway_animated_variable *av);
// Stop tracking every variable, whoever tracks it
void animation_manager_untrack_all(struct sway_animation_manager *manager);

#endif
