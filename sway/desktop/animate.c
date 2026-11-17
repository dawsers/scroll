#include <wlr/types/wlr_output.h>
#include "sway/config.h"
#include "sway/desktop/animate.h"
#include "sway/desktop/transaction.h"
#include "sway/output.h"
#include "sway/server.h"

static void handle_apply(void *data) {
	transaction_animate_outputs();
}

static const struct sway_animation_manager_impl manager_impl = {
	.apply = handle_apply,
};

void animation_create(void) {
	if (server.animation_manager) {
		animation_destroy();
	}
	server.animation_manager = animation_manager_create(server.wl_event_loop,
		&manager_impl, NULL);
}

void animation_release_loop(void) {
	if (server.animation_manager) {
		animation_manager_release_loop(server.animation_manager);
	}
}

void animation_stop(void) {
	if (server.animation_manager) {
		animation_manager_stop(server.animation_manager);
	}
}

void animation_destroy(void) {
	animation_manager_destroy(server.animation_manager);
	server.animation_manager = NULL;
}

struct sway_animation_config *animation_get_config(void) {
	return animation_manager_get_config(server.animation_manager);
}

struct sway_animation_spec *animation_get_spec(enum sway_animation_type type) {
	if (config->reloading) {
		return NULL;
	}
	return animation_manager_get_spec(server.animation_manager, type);
}

bool animation_enabled(void) {
	return animation_get_spec(ANIMATION_DEFAULT) != NULL;
}

bool animation_path_enabled(enum sway_animation_type type) {
	return animation_get_spec(type) != NULL;
}

bool animation_animating(void) {
	return server.animation_manager &&
		animation_manager_active(server.animation_manager);
}

bool animation_animating_output(struct wlr_output *wlr_output) {
	struct sway_output *output = wlr_output ? wlr_output->data : NULL;
	return output && animation_clock_active(&output->animation_clock);
}

void animation_end(void) {
	animation_manager_finish_all(server.animation_manager);
	animation_manager_flush(server.animation_manager);
}

int64_t animation_now(void) {
	return animation_manager_now(server.animation_manager);
}

static void clock_schedule_frame(struct sway_animation_clock *clock) {
	struct sway_output *output = clock->data;
	if (output->wlr_output && output->wlr_output->enabled) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

static const struct sway_animation_clock_impl clock_impl = {
	.schedule_frame = clock_schedule_frame,
};

struct sway_animation_clock *output_animation_clock(struct sway_output *output) {
	return output && output->animation_clock.manager ?
		&output->animation_clock : NULL;
}

void output_animation_clock_init(struct sway_output *output) {
	if (output->animation_clock.manager || !server.animation_manager) {
		return;
	}
	animation_clock_init(&output->animation_clock, server.animation_manager,
		&clock_impl, output);
}

void output_animation_clock_finish(struct sway_output *output) {
	animation_clock_finish(&output->animation_clock);
}

void animation_animate(struct wlr_output *wlr_output) {
	struct sway_output *output = wlr_output->data;
	if (!output || !animation_clock_active(&output->animation_clock)) {
		return;
	}
	struct sway_animation_manager *manager = server.animation_manager;
	// Compute the clock data
	animation_clock_begin_frame(&output->animation_clock);
	// Sample variables
	animation_manager_advance(manager, &output->animation_clock);
	// Evaluate the scene graph with the new values
	transaction_animate_output(output);
	// Notify the groups the animation has ended
	animation_manager_dispatch(manager);
}
