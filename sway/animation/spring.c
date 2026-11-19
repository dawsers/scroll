#include <math.h>
#include <stdlib.h>
#include "internal.h"

/**
 * Animator: a damped spring. It keeps the velocity the variable has when the
 * animation starts, so it is a good choice for animations that follow a
 * gesture or interrupt another animation.
 *
 * With A = x0 - x1, w the natural frequency and z the damping ratio:
 *
 *   z < 1:  x(t) = x1 + exp(-z w t) (A cos(wd t) + B sin(wd t))
 *           wd = w sqrt(1 - z^2), B = (v0 + z w A) / wd
 *   z = 1:  x(t) = x1 + (A + B t) exp(-w t), B = v0 + w A
 *   z > 1:  x(t) = x1 + c1 exp(r1 t) + c2 exp(r2 t)
 *           r1,2 = -w (z -+ sqrt(z^2 - 1))
 *           c1 = (v0 - r2 A) / (r1 - r2), c2 = A - c1
 *
 * The animation ends when the motion that is left is smaller than epsilon.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NSEC_PER_SEC 1000000000.0
// Never run for longer than this
#define SPRING_MAX_DURATION 10.0

struct spring_spec {
	struct sway_animation_spec base;
	double omega; // natural frequency, rad/s
	double zeta; // damping ratio
};

// The two coefficients of the solution for a run: A and B, or c1 and c2
static void spring_coefficients(const struct sway_animator_state *state,
		double w, double z, double c[2]) {
	const double A = state->x0 - state->x1;
	if (z < 1.0) {
		const double wd = w * sqrt(1.0 - z * z);
		c[0] = A;
		c[1] = (state->v0 + z * w * A) / wd;
	} else if (z == 1.0) {
		c[0] = A;
		c[1] = state->v0 + w * A;
	} else {
		const double s = sqrt(z * z - 1.0);
		const double r1 = -w * (z - s);
		const double r2 = -w * (z + s);
		c[0] = (state->v0 - r2 * A) / (r1 - r2);
		c[1] = A - c[0];
	}
}

static void spring_start(struct sway_animator_state *state,
		const struct sway_animation_spec *spec) {
	const struct spring_spec *spring = wl_container_of(spec, spring, base);
	const double w = spring->omega;
	const double z = spring->zeta;
	const double eps = state->epsilon;
	double c[2];
	spring_coefficients(state, w, z, c);
	double duration;
	if (z < 1.0) {
		const double amplitude = fabs(c[0]) + fabs(c[1]);
		duration = amplitude > eps ? log(amplitude / eps) / (z * w) : 0.0;
	} else if (z == 1.0) {
		// (|A| + |B| t) exp(-w t) = eps
		duration = 0.0;
		if (fabs(c[0]) > eps || fabs(c[1]) > eps * w) {
			for (int i = 0; i < 16; ++i) {
				const double amplitude = fabs(c[0]) + fabs(c[1]) * duration;
				duration = amplitude > eps ? log(amplitude / eps) / w : 0.0;
			}
		}
	} else {
		const double s = sqrt(z * z - 1.0);
		const double r1 = -w * (z - s);
		const double r2 = -w * (z + s);
		const double d1 = 2.0 * fabs(c[0]) > eps ? log(2.0 * fabs(c[0]) / eps) / -r1 : 0.0;
		const double d2 = 2.0 * fabs(c[1]) > eps ? log(2.0 * fabs(c[1]) / eps) / -r2 : 0.0;
		duration = fmax(d1, d2);
	}
	if (!isfinite(duration) || duration > SPRING_MAX_DURATION) {
		duration = SPRING_MAX_DURATION;
	}
	state->duration = (int64_t)(duration * NSEC_PER_SEC);
}

static void spring_sample(const struct sway_animator_state *state,
		const struct sway_animation_spec *spec, int64_t elapsed,
		struct sway_animation_sample *sample) {
	sample->offset = 0.0;
	if (elapsed >= state->duration) {
		sample->value = state->x1;
		sample->done = true;
		return;
	}
	const struct spring_spec *spring = wl_container_of(spec, spring, base);
	const double t = elapsed / NSEC_PER_SEC;
	const double w = spring->omega;
	const double z = spring->zeta;
	double c[2];
	spring_coefficients(state, w, z, c);
	double x;
	if (z < 1.0) {
		const double wd = w * sqrt(1.0 - z * z);
		x = exp(-z * w * t) * (c[0] * cos(wd * t) + c[1] * sin(wd * t));
	} else if (z == 1.0) {
		x = (c[0] + c[1] * t) * exp(-w * t);
	} else {
		const double s = sqrt(z * z - 1.0);
		x = c[0] * exp(-w * (z - s) * t) + c[1] * exp(-w * (z + s) * t);
	}
	sample->value = state->x1 + x;
	sample->done = false;
}

static const struct sway_animator_impl animator_spring = {
	.uses_velocity = true,
	.start = spring_start,
	.sample = spring_sample,
};

struct sway_animation_spec *animation_spec_create_spring(double response_ms,
		double damping_ratio) {
	struct spring_spec *spring = calloc(1, sizeof(struct spring_spec));
	if (!spring) {
		return NULL;
	}
	animation_spec_init(&spring->base, &animator_spring);
	if (response_ms < 1.0) {
		response_ms = 1.0;
	}
	if (damping_ratio < 0.05) {
		damping_ratio = 0.05;
	}
	// The response is the period of the spring without damping
	spring->omega = 2.0 * M_PI / (response_ms / 1000.0);
	spring->zeta = damping_ratio;
	return &spring->base;
}
