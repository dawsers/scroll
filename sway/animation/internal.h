#ifndef _SWAY_ANIMATION_INTERNAL_H
#define _SWAY_ANIMATION_INTERNAL_H
#include "sway/desktop/animation.h"

// Clocks without a frame for this long have their animations ended
#define ANIMATION_STALL_NSEC 500000000LL
#define ANIMATION_WATCHDOG_MSEC 250

struct sway_animation_manager {
	const struct sway_animation_manager_impl *impl;
	void *data;
	struct wl_event_loop *loop;
	struct wl_event_source *idle;
	struct wl_event_source *watchdog;
	bool watchdog_armed;
	bool stopped;

	struct wl_list clocks; // sway_animation_clock.link
	struct wl_list unbound; // sway_animated_variable.link, sampled by every clock
	int64_t unbound_tick;
	size_t running; // number of variables running an animation

	struct wl_list finished; // sway_animation_group.link
	bool needs_apply;

	struct sway_animation_config config;
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

// Every animator needs to have this structure as its first member, so a spec
// can be freed through this pointer
struct sway_animation_spec {
	int refs;
	bool enabled;
	const struct sway_animator_impl *impl;
};

void animation_spec_init(struct sway_animation_spec *spec,
	const struct sway_animator_impl *impl, bool enabled);

extern const struct sway_animator_impl animator_bezier;
extern const struct sway_animator_impl animator_spring;
extern const struct sway_animator_impl animator_decay;

void animation_group_join(struct sway_animation_group *group);
void animation_group_leave(struct sway_animation_group *group);

// Something has to be done when the event loop is idle
void animation_manager_schedule_flush(struct sway_animation_manager *manager);
// A variable started running
void animation_manager_variable_started(struct sway_animation_manager *manager,
	struct sway_animated_variable *av);
void animation_manager_variable_stopped(struct sway_animation_manager *manager,
	struct sway_animated_variable *av);
// The clock (every clock if NULL) needs a frame to show new values
void animation_manager_damage(struct sway_animation_manager *manager,
	struct sway_animation_clock *clock);

// Sample a running variable at `time`. Returns false if its animation ended.
bool animated_variable_advance(struct sway_animated_variable *av, int64_t time);

#endif
