#include <stdlib.h>
#include <time.h>
#include "internal.h"

#define NSEC_PER_SEC 1000000000LL

// An animation that should have ended this long ago did not have frames to
// end it (its output is off, for example). The watchdog ends it.
#define ANIMATION_OVERDUE_NSEC 500000000LL
#define ANIMATION_WATCHDOG_MSEC 250

int64_t animation_manager_now(struct sway_animation_manager *manager) {
	if (manager->impl && manager->impl->now) {
		return manager->impl->now(manager->data);
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * NSEC_PER_SEC + now.tv_nsec;
}

static void release_loop(struct sway_animation_manager *manager) {
	if (!manager->loop) {
		return;
	}
	if (manager->idle) {
		wl_event_source_remove(manager->idle);
		manager->idle = NULL;
	}
	if (manager->watchdog) {
		wl_event_source_remove(manager->watchdog);
		manager->watchdog = NULL;
	}
	manager->watchdog_armed = false;
	wl_list_remove(&manager->loop_destroy.link);
	manager->loop = NULL;
}

static void handle_loop_destroy(struct wl_listener *listener, void *data) {
	struct sway_animation_manager *manager =
		wl_container_of(listener, manager, loop_destroy);
	release_loop(manager);
}

struct sway_animation_manager *animation_manager_create(
		struct wl_event_loop *loop,
		const struct sway_animation_manager_impl *impl, void *data) {
	struct sway_animation_manager *manager =
		calloc(1, sizeof(struct sway_animation_manager));
	if (!manager) {
		return NULL;
	}
	manager->impl = impl;
	manager->data = data;
	wl_list_init(&manager->clocks);
	wl_list_init(&manager->unbound);
	wl_list_init(&manager->tracking);
	wl_list_init(&manager->finished);
	if (loop) {
		manager->loop = loop;
		manager->loop_destroy.notify = handle_loop_destroy;
		wl_event_loop_add_destroy_listener(loop, &manager->loop_destroy);
	}
	return manager;
}

void animation_manager_stop(struct sway_animation_manager *manager) {
	manager->stopped = true;
	animation_manager_untrack_all(manager);
	animation_manager_finish_all(manager);
	manager->needs_apply = false;
	animation_manager_dispatch(manager);
}

void animation_manager_destroy(struct sway_animation_manager *manager) {
	if (!manager) {
		return;
	}
	animation_manager_stop(manager);
	release_loop(manager);
	struct sway_animation_clock *clock, *tmp;
	wl_list_for_each_safe(clock, tmp, &manager->clocks, link) {
		clock->manager = NULL;
		wl_list_remove(&clock->link);
		wl_list_init(&clock->link);
	}
	free(manager);
}

void animation_spec_init(struct sway_animation_spec *spec,
		const struct sway_animator_impl *impl) {
	spec->refs = 1;
	spec->impl = impl;
}

struct sway_animation_spec *animation_spec_ref(struct sway_animation_spec *spec) {
	++spec->refs;
	return spec;
}

void animation_spec_unref(struct sway_animation_spec *spec) {
	if (!spec) {
		return;
	}
	if (--spec->refs == 0) {
		if (spec->impl->destroy) {
			spec->impl->destroy(spec);
		}
		free(spec);
	}
}

static void handle_idle(void *data) {
	struct sway_animation_manager *manager = data;
	manager->idle = NULL;
	animation_manager_flush(manager);
}

void animation_manager_schedule_flush(struct sway_animation_manager *manager) {
	if (!manager->loop || manager->idle) {
		return;
	}
	manager->idle = wl_event_loop_add_idle(manager->loop, handle_idle, manager);
}

void animation_manager_dispatch(struct sway_animation_manager *manager) {
	while (!wl_list_empty(&manager->finished)) {
		struct sway_animation_group *group =
			wl_container_of(manager->finished.next, group, link);
		wl_list_remove(&group->link);
		wl_list_init(&group->link);
		if (group->done) {
			group->done(group->data);
		}
		animation_group_unref(group);
	}
}

void animation_manager_flush(struct sway_animation_manager *manager) {
	if (manager->needs_apply) {
		manager->needs_apply = false;
		if (manager->impl && manager->impl->apply) {
			manager->impl->apply(manager->data);
		}
	}
	animation_manager_dispatch(manager);
}

static void schedule_frame(struct sway_animation_clock *clock) {
	if (clock->impl && clock->impl->schedule_frame) {
		clock->impl->schedule_frame(clock);
	}
}

void animation_manager_damage(struct sway_animation_manager *manager,
		struct sway_animation_clock *clock) {
	if (clock) {
		clock->dirty = true;
		schedule_frame(clock);
		return;
	}
	wl_list_for_each(clock, &manager->clocks, link) {
		clock->dirty = true;
		schedule_frame(clock);
	}
}

static int handle_watchdog(void *data) {
	struct sway_animation_manager *manager = data;
	animation_manager_check_expired(manager);
	if (manager->running > 0) {
		wl_event_source_timer_update(manager->watchdog, ANIMATION_WATCHDOG_MSEC);
	} else {
		manager->watchdog_armed = false;
	}
	return 0;
}

static void arm_watchdog(struct sway_animation_manager *manager) {
	if (!manager->loop || manager->watchdog_armed) {
		return;
	}
	if (!manager->watchdog) {
		manager->watchdog = wl_event_loop_add_timer(manager->loop,
			handle_watchdog, manager);
		if (!manager->watchdog) {
			return;
		}
	}
	manager->watchdog_armed = true;
	wl_event_source_timer_update(manager->watchdog, ANIMATION_WATCHDOG_MSEC);
}

void animation_manager_variable_started(struct sway_animation_manager *manager,
		struct sway_animated_variable *av) {
	++manager->running;
	struct sway_animation_clock *clock = av->clock;
	if (clock) {
		wl_list_insert(&clock->running, &av->link);
		schedule_frame(clock);
	} else {
		wl_list_insert(&manager->unbound, &av->link);
		wl_list_for_each(clock, &manager->clocks, link) {
			schedule_frame(clock);
		}
	}
	arm_watchdog(manager);
}

void animation_manager_variable_stopped(struct sway_animation_manager *manager,
		struct sway_animated_variable *av) {
	--manager->running;
	wl_list_remove(&av->link);
	wl_list_init(&av->link);
}

bool animation_manager_advance(struct sway_animation_manager *manager,
		struct sway_animation_clock *clock) {
	const int64_t time = clock->frame_time;
	clock->dirty = false;

	bool sampled = false;
	struct sway_animated_variable *av, *tmp;
	wl_list_for_each_safe(av, tmp, &clock->running, link) {
		sampled = true;
		animated_variable_advance(av, time);
	}
	bool finished = false;
	wl_list_for_each_safe(av, tmp, &manager->unbound, link) {
		sampled = true;
		if (!animated_variable_advance(av, time)) {
			finished = true;
		}
	}
	if (finished) {
		// The other outputs have to show the final values too
		struct sway_animation_clock *other;
		wl_list_for_each(other, &manager->clocks, link) {
			if (other != clock) {
				other->dirty = true;
				schedule_frame(other);
			}
		}
	}
	return sampled;
}

static void values_changed(struct sway_animation_manager *manager) {
	manager->needs_apply = true;
	animation_manager_schedule_flush(manager);
}

// End the animations of the list that had to end before `time`. Returns true
// if any ended.
static bool finish_list(struct wl_list *list, int64_t time) {
	bool finished = false;
	struct sway_animated_variable *av, *tmp;
	wl_list_for_each_safe(av, tmp, list, link) {
		if (av->t0 + av->state.duration < time) {
			animated_variable_finish(av);
			finished = true;
		}
	}
	return finished;
}

static void finish_before(struct sway_animation_manager *manager, int64_t time) {
	bool finished = finish_list(&manager->unbound, time);
	struct sway_animation_clock *clock;
	wl_list_for_each(clock, &manager->clocks, link) {
		finished |= finish_list(&clock->running, time);
	}
	if (finished) {
		values_changed(manager);
	}
}

void animation_manager_check_expired(struct sway_animation_manager *manager) {
	finish_before(manager,
		animation_manager_now(manager) - ANIMATION_OVERDUE_NSEC);
}

void animation_manager_finish_all(struct sway_animation_manager *manager) {
	finish_before(manager, INT64_MAX);
}

bool animation_manager_active(struct sway_animation_manager *manager) {
	return manager->running > 0;
}
