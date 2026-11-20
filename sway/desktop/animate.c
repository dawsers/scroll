#include <stdlib.h>
#include <wlr/types/wlr_output.h>
#include "sway/config.h"
#include "sway/desktop/animate.h"
#include "sway/desktop/transaction.h"
#include "sway/output.h"
#include "sway/server.h"

/**
 * Configuration: which spec, if any, animates each type of animation. It is
 * part of the configuration of the compositor (config->animations), so
 * reloading the configuration starts from the defaults again.
 */
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
	struct sway_animation_path *path = animation_path_create();
	animation_path_add_curve(path, curve);
	list_free_items_and_destroy(default_points);
	return animation_spec_create_path(path);
}

struct sway_animation_config *animation_config_create(void) {
	struct sway_animation_config *animations =
		calloc(1, sizeof(struct sway_animation_config));
	if (!animations) {
		return NULL;
	}
	animations->enabled = true;
	animations->crossfade = true;
	animations->style = ANIM_STYLE_SCALE;
	animations->types[ANIMATION_DEFAULT].spec = create_default_spec();
	return animations;
}

void animation_config_destroy(struct sway_animation_config *animations) {
	if (!animations) {
		return;
	}
	for (int i = 0; i < ANIMATION_TYPE_COUNT; ++i) {
		animation_spec_unref(animations->types[i].spec);
	}
	free(animations);
}

struct sway_animation_config *animation_get_config(void) {
	return config->animations;
}

void animation_set_spec(enum sway_animation_type type,
		struct sway_animation_spec *spec) {
	struct sway_animation_config *animations = config->animations;
	animation_spec_unref(animations->types[type].spec);
	animations->types[type].spec = spec;
	animations->types[type].disabled = spec == NULL;
}

bool animation_enabled(void) {
	return config->animations->enabled && !config->reloading;
}

struct sway_animation_spec *animation_get_spec(enum sway_animation_type type) {
	if (!animation_enabled() || type == ANIMATION_DISABLED) {
		return NULL;
	}
	const struct sway_animation_config *animations = config->animations;
	if (!animations->types[type].spec && !animations->types[type].disabled) {
		type = ANIMATION_DEFAULT;
	}
	return animations->types[type].spec;
}

bool animation_type_enabled(enum sway_animation_type type) {
	return animation_get_spec(type) != NULL;
}

/**
 * animation manager interface
 */
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

void animation_stop(void) {
	if (server.animation_manager) {
		animation_manager_stop(server.animation_manager);
	}
}

void animation_destroy(void) {
	animation_manager_destroy(server.animation_manager);
	server.animation_manager = NULL;
}

bool animation_animating(void) {
	return server.animation_manager &&
		animation_manager_active(server.animation_manager);
}

void animation_end(void) {
	animation_manager_finish_all(server.animation_manager);
	animation_manager_flush(server.animation_manager);
}

/*
 * Output clock interface
 */

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

bool animation_animating_output(struct wlr_output *wlr_output) {
	struct sway_output *output = wlr_output ? wlr_output->data : NULL;
	return output && animation_clock_active(&output->animation_clock);
}

void animation_animate(struct wlr_output *wlr_output) {
	struct sway_animation_clock *clock = output_animation_clock(wlr_output->data);
	if (!clock || !animation_clock_active(clock)) {
		return;
	}
	struct sway_output *output = wlr_output->data;
	struct sway_animation_manager *manager = server.animation_manager;
	// Compute the presentation time of the frame
	animation_clock_begin_frame(clock);
	// Sample variables
	animation_manager_advance(manager, clock);
	// Evaluate the scene graph with the new values
	transaction_animate_output(output);
	// Notify the groups the animation has ended
	animation_manager_dispatch(manager);
}
