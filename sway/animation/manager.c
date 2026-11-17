#include <stdlib.h>
#include <time.h>
#include "internal.h"

#define NSEC_PER_SEC 1000000000LL

int64_t animation_manager_now(struct sway_animation_manager *manager) {
	if (manager->impl && manager->impl->now) {
		return manager->impl->now(manager->data);
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * NSEC_PER_SEC + now.tv_nsec;
}

static struct sway_animation_spec *create_default_spec(void) {
	double points[] = { 0.215, 0.61, 0.355, 1.0 };
	list_t *default_points = create_list();
	for (uint32_t i = 0; i < sizeof(points) / sizeof(double); ++i) {
		double *val = malloc(sizeof(double));
		*val = points[i];
		list_add(default_points, val);
	}
	struct sway_animation_curve *curve = create_animation_curve(300, 3,
		default_points, false, 0.0, 0, NULL);
	struct sway_animation_path *path = animation_path_create(true);
	animation_path_add_curve(path, curve);
	list_free_items_and_destroy(default_points);
	return animation_spec_create_path(path);
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
	manager->loop = loop;
	wl_list_init(&manager->clocks);
	wl_list_init(&manager->unbound);
	wl_list_init(&manager->finished);
	manager->config.enabled = true;
	manager->config.style = ANIM_STYLE_SCALE;
	manager->config.specs[ANIMATION_DEFAULT] = create_default_spec();
	return manager;
}

void animation_manager_stop(struct sway_animation_manager *manager) {
	manager->stopped = true;
	animation_manager_finish_all(manager);
	manager->needs_apply = false;
	animation_manager_dispatch(manager);
}

void animation_manager_release_loop(struct sway_animation_manager *manager) {
	if (manager->idle) {
		wl_event_source_remove(manager->idle);
		manager->idle = NULL;
	}
	if (manager->watchdog) {
		wl_event_source_remove(manager->watchdog);
		manager->watchdog = NULL;
	}
	manager->watchdog_armed = false;
	manager->loop = NULL;
}

void animation_manager_destroy(struct sway_animation_manager *manager) {
	if (!manager) {
		return;
	}
	animation_manager_stop(manager);
	animation_manager_release_loop(manager);
	struct sway_animation_clock *clock, *tmp;
	wl_list_for_each_safe(clock, tmp, &manager->clocks, link) {
		clock->manager = NULL;
		wl_list_remove(&clock->link);
		wl_list_init(&clock->link);
	}
	for (int i = 0; i < ANIMATION_TYPE_COUNT; ++i) {
		animation_spec_unref(manager->config.specs[i]);
	}
	free(manager);
}

struct sway_animation_config *animation_manager_get_config(
		struct sway_animation_manager *manager) {
	return &manager->config;
}

void animation_manager_set_spec(struct sway_animation_manager *manager,
		enum sway_animation_type type, struct sway_animation_spec *spec) {
	animation_spec_unref(manager->config.specs[type]);
	manager->config.specs[type] = spec;
}

struct sway_animation_spec *animation_manager_get_spec(
		struct sway_animation_manager *manager, enum sway_animation_type type) {
	if (!manager->config.enabled || manager->stopped ||
			type == ANIMATION_DISABLED) {
		return NULL;
	}
	struct sway_animation_spec *spec = manager->config.specs[type];
	if (!spec) {
		spec = manager->config.specs[ANIMATION_DEFAULT];
	}
	return spec && spec->enabled ? spec : NULL;
}

void animation_spec_init(struct sway_animation_spec *spec,
		const struct sway_animator_impl *impl, bool enabled) {
	spec->refs = 1;
	spec->enabled = enabled;
	spec->impl = impl;
}

// The spec of an animator provided by the caller
struct custom_spec {
	struct sway_animation_spec base;
	void *data;
};

struct sway_animation_spec *animation_spec_create_custom(
		const struct sway_animator_impl *impl, void *data) {
	struct custom_spec *custom = calloc(1, sizeof(struct custom_spec));
	if (!custom) {
		return NULL;
	}
	animation_spec_init(&custom->base, impl, true);
	custom->data = data;
	return &custom->base;
}

void *animation_spec_get_data(const struct sway_animation_spec *spec) {
	if (!spec || spec->impl == &animator_bezier ||
			spec->impl == &animator_spring || spec->impl == &animator_decay) {
		return NULL;
	}
	const struct custom_spec *custom = wl_container_of(spec, custom, base);
	return custom->data;
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
	animation_manager_check_stalled(manager);
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
		if (wl_list_empty(&clock->running)) {
			clock->last_tick = animation_manager_now(manager);
		}
		wl_list_insert(&clock->running, &av->link);
		schedule_frame(clock);
	} else {
		if (wl_list_empty(&manager->unbound)) {
			manager->unbound_tick = animation_manager_now(manager);
		}
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
	const int64_t now = animation_manager_now(manager);
	clock->last_tick = now;
	manager->unbound_tick = now;
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

static bool finish_list(struct wl_list *list) {
	bool finished = false;
	while (!wl_list_empty(list)) {
		struct sway_animated_variable *av = wl_container_of(list->next, av, link);
		animated_variable_finish(av);
		finished = true;
	}
	return finished;
}

static void values_changed(struct sway_animation_manager *manager) {
	manager->needs_apply = true;
	animation_manager_schedule_flush(manager);
}

void animation_manager_check_stalled(struct sway_animation_manager *manager) {
	const int64_t now = animation_manager_now(manager);
	bool finished = false;
	int64_t last_tick = manager->unbound_tick;
	struct sway_animation_clock *clock;
	wl_list_for_each(clock, &manager->clocks, link) {
		if (now - clock->last_tick > ANIMATION_STALL_NSEC) {
			finished |= finish_list(&clock->running);
		}
		if (clock->last_tick > last_tick) {
			last_tick = clock->last_tick;
		}
	}
	if (now - last_tick > ANIMATION_STALL_NSEC) {
		finished |= finish_list(&manager->unbound);
	}
	if (finished) {
		values_changed(manager);
	}
}

bool animation_manager_active(struct sway_animation_manager *manager) {
	return manager->running > 0;
}

size_t animation_manager_count(struct sway_animation_manager *manager) {
	return manager->running;
}

void animation_manager_finish_all(struct sway_animation_manager *manager) {
	bool finished = finish_list(&manager->unbound);
	struct sway_animation_clock *clock;
	wl_list_for_each(clock, &manager->clocks, link) {
		finished |= finish_list(&clock->running);
	}
	if (finished) {
		values_changed(manager);
	}
}
