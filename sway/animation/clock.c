#include <stdlib.h>
#include "internal.h"

/**
 * Clocks
 *
 * A clock predicts when the frame that is going to be rendered for an output
 * will be displayed, using the presentation feedback of previous frames.
 * Animations are sampled at that time, instead of the time the frame is
 * rendered, so every output shows the animation as it has to be at the
 * moment its frame reaches the screen, regardless of its refresh rate.
 */

#define NSEC_PER_SEC 1000000000LL

void animation_clock_init(struct sway_animation_clock *clock,
		struct sway_animation_manager *manager,
		const struct sway_animation_clock_impl *impl, void *data) {
	*clock = (struct sway_animation_clock){0};
	clock->impl = impl;
	clock->manager = manager;
	clock->data = data;
	wl_list_init(&clock->running);
	wl_list_insert(manager->clocks.prev, &clock->link);
}

void animation_clock_finish(struct sway_animation_clock *clock) {
	struct sway_animation_manager *manager = clock->manager;
	if (!manager) {
		return;
	}
	clock->manager = NULL;
	wl_list_remove(&clock->link);
	wl_list_init(&clock->link);

	// Move the variables of this clock to another one
	const bool had_running = !wl_list_empty(&clock->running);
	struct sway_animated_variable *av, *tmp;
	wl_list_for_each_safe(av, tmp, &clock->running, link) {
		wl_list_remove(&av->link);
		wl_list_insert(&manager->unbound, &av->link);
		av->clock = NULL;
	}
	if (wl_list_empty(&manager->clocks)) {
		// Nobody is left to animate anything
		animation_manager_finish_all(manager);
	} else if (had_running) {
		animation_manager_damage(manager, NULL);
	}
}

void animation_clock_presented(struct sway_animation_clock *clock,
		int64_t when, int64_t refresh) {
	clock->last_present = when;
	clock->refresh = refresh > 0 ? refresh : 0;
}

int64_t animation_clock_begin_frame(struct sway_animation_clock *clock) {
	const int64_t now = animation_manager_now(clock->manager);
	int64_t time;
	if (clock->refresh == 0) {
		// We don't know the refresh rate, use current time
		time = now;
	} else if (now >= clock->last_present &&
			now - clock->last_present < NSEC_PER_SEC) {
		// The first refresh after now
		const int64_t n = (now - clock->last_present) / clock->refresh + 1;
		time = clock->last_present + n * clock->refresh;
	} else {
		// The phase of the refresh is not known
		time = now + clock->refresh;
	}
	// A frame is never presented before the previous one
	if (time <= clock->frame_time) {
		time = clock->frame_time + (clock->refresh > 0 ? clock->refresh : 1);
	}
	clock->frame_time = time;
	return time;
}

bool animation_clock_active(struct sway_animation_clock *clock) {
	if (!clock->manager) {
		return false;
	}
	return clock->dirty || !wl_list_empty(&clock->running) ||
		!wl_list_empty(&clock->manager->unbound);
}
