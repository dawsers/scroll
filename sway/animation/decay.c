#include <math.h>
#include <stdlib.h>
#include "internal.h"

/**
 * Animator: exponential decay, the motion of something that slows down by
 * friction.
 *
 *   x(t) = x1 + (x0 - x1) exp(-rate t)
 *
 * Something moving at v0 from x0 stops at x0 + v0 / rate. Animating a variable
 * to that target gives it the initial velocity v0, so the same animator is
 * used for a free decay (a flick) and for a decay towards a given target.
 */

#define NSEC_PER_SEC 1000000000.0

struct decay_spec {
	struct sway_animation_spec base;
	double rate; // 1/s
};

static double decay_rate(const struct sway_animation_spec *spec) {
	const struct decay_spec *decay = wl_container_of(spec, decay, base);
	return decay->rate;
}
#define DECAY_MAX_DURATION 10.0

static void decay_start(struct sway_animator_state *state,
		const struct sway_animation_spec *spec) {
	const double distance = fabs(state->x1 - state->x0);
	double duration = distance > state->epsilon ?
		log(distance / state->epsilon) / decay_rate(spec) : 0.0;
	if (!isfinite(duration) || duration > DECAY_MAX_DURATION) {
		duration = DECAY_MAX_DURATION;
	}
	state->duration = (int64_t)(duration * NSEC_PER_SEC);
}

static void decay_sample(const struct sway_animator_state *state,
		const struct sway_animation_spec *spec, int64_t elapsed,
		struct sway_animation_sample *sample) {
	sample->offset = 0.0;
	if (elapsed >= state->duration) {
		sample->value = state->x1;
		sample->done = true;
		return;
	}
	const double t = elapsed / NSEC_PER_SEC;
	sample->value = state->x1 +
		(state->x0 - state->x1) * exp(-decay_rate(spec) * t);
	sample->done = false;
}

const struct sway_animator_impl animator_decay = {
	.uses_velocity = false,
	.start = decay_start,
	.sample = decay_sample,
};

struct sway_animation_spec *animation_spec_create_decay(double rate) {
	struct decay_spec *decay = calloc(1, sizeof(struct decay_spec));
	if (!decay) {
		return NULL;
	}
	animation_spec_init(&decay->base, &animator_decay);
	decay->rate = rate > 0.01 ? rate : 0.01;
	return &decay->base;
}
