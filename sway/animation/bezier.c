#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"
#include "sway/log.h"

/**
 * Animator: paths of Bezier curves with a fixed duration
 */

#define NDIM 2

// Size of lookup table
#define NINTERVALS 100

struct bezier_curve {
	uint32_t n;
	double *b[NDIM];
	double u[NINTERVALS + 1];
	 // if true, this is a cubic Bezier in compatibility mode for other compositors.
	bool simple;
};

// Curves of an animation are queried once per animated variable and frame, and
// variables usually share curves and are evaluated for the same input values,
// so we cache some values.
#define ANIMATION_CURVE_CACHE_SIZE 4

struct curve_cache_entry {
	bool valid;
	uint64_t used;
	double u;
	double t, x, y;
};

struct curve_cache {
	struct curve_cache_entry entry[ANIMATION_CURVE_CACHE_SIZE];
	uint64_t clock;
};

struct sway_animation_curve {
	uint32_t duration_ms;
	struct bezier_curve var;
	struct bezier_curve off;
	struct curve_cache cache;
};

// Control points are stored multiplied by their binomial coefficients, so
// we can compute using Horner's rule for the Bernstein form of the curve
// B(t) = sum(b[i] * t^i * (1 - t)^(n - i))
static void bezier(struct bezier_curve *curve, double t, double (*B)[NDIM]) {
	const double u = 1.0 - t;
	double res[NDIM];
	for (uint32_t d = 0; d < NDIM; ++d) {
		res[d] = curve->b[d][0];
	}
	double tn = 1.0;
	for (uint32_t i = 1; i <= curve->n; ++i) {
		tn *= t;
		for (uint32_t d = 0; d < NDIM; ++d) {
			res[d] = res[d] * u + curve->b[d][i] * tn;
		}
	}
	for (uint32_t d = 0; d < NDIM; ++d) {
		(*B)[d] = res[d];
	}
}

static void fill_lookup(struct bezier_curve *curve, double t0, double x0,
		double t1, double x1) {
	double t = 0.5 * (t0 + t1);
	double B[NDIM];
	bezier(curve, t, &B);
	if (x0 * NINTERVALS <= floor(x1 * NINTERVALS)) {
		if (x1 - x0 < 0.001) {
			curve->u[(uint32_t)floor(x1 * NINTERVALS)] = B[1];
			return;
		}
		fill_lookup(curve, t0, x0, t, B[0]);
		fill_lookup(curve, t, B[0], t1, x1);
	}
}

static void create_lookup_simple(struct bezier_curve *curve) {
	fill_lookup(curve, 0.0, 0.0, 1.0, 1.0);
}

static void create_lookup_length(struct bezier_curve *curve) {
	double B0[NDIM], length = 0.0;
	for (int i = 0; i < NDIM; ++i) {
		B0[i] = 0.0;
	}
	double *T = (double *) malloc(sizeof(double) * (NINTERVALS + 1));
	for (int i = 0; i < NINTERVALS + 1; ++i) {
		double u = (double) i / NINTERVALS;
		double B[NDIM];
		bezier(curve, u, &B);
		double t = 0.0;
		for (int d = 0; d < NDIM; ++d) {
			t += (B[d] - B0[d]) * (B[d] - B0[d]);
		}
		T[i] = sqrt(t);
		length += T[i];
		for (int d = 0; d < NDIM; ++d) {
			B0[d] = B[d];
		}
	}
	// Fill U
	int last = 0;
	double len0 = 0.0, len1 = 0.0;
	double u0 = 0.0;
	for (int i = 0; i < NINTERVALS + 1; ++i) {
		double t = fmin((double) i / NINTERVALS * length, length);
		while (t > len1) {
			len0 = len1;
			u0 = (double) last / NINTERVALS;
			++last;
			if (last < NINTERVALS) {
				len1 += T[last];
			} else {
				len1 = length;
			}
		}
		// Interpolate
		if (last == 0) {
			curve->u[i] = 0;
		} else {
			double u1 = (double) last / NINTERVALS;
			double k = (t - len0) / (len1 - len0);
			curve->u[i] = (1.0 - k) * u0 + k * u1;
		}
	}
	free(T);
}

struct sway_animation_path {
	list_t *curves;	// struct sway_animation_curve
};

struct sway_animation_path *animation_path_create(void) {
	struct sway_animation_path *path = malloc(sizeof(struct sway_animation_path));
	path->curves = create_list();
	return path;
}

static void destroy_animation_curve(struct sway_animation_curve *curve);

static void animation_path_destroy(struct sway_animation_path *path) {
	if (path) {
		for (int i = 0; i < path->curves->length; ++i) {
			struct sway_animation_curve *curve = path->curves->items[i];
			destroy_animation_curve(curve);
		}
		list_free(path->curves);
		free(path);
	}
}

void animation_path_add_curve(struct sway_animation_path *path,
		struct sway_animation_curve *curve) {
	list_add(path->curves, curve);
}

static void lookup_xy(struct bezier_curve *curve, double t, double *x, double *y) {
	// Interpolate in the lookup table
	const double pos = t * NINTERVALS;
	const uint32_t i = (uint32_t) pos;
	const double k = pos - i;
	double u = curve->u[i];
	if (k > 0.0) {
		u = (1.0 - k) * u + k * curve->u[i + 1];
	}
	if (curve->simple) {
		*x = t; *y = u;
		return;
	}
	double B[NDIM];
	// I could create another lookup table for B
	bezier(curve, u, &B);
	*x = B[0]; *y = B[1];
}

static void animation_curve_cache_init(struct sway_animation_curve *curve) {
	memset(&curve->cache, 0, sizeof(struct curve_cache));
}

static bool animation_curve_cache_query(struct sway_animation_curve *curve, double u,
		double *t, double *x, double *y) {
	for (int i = 0; i < ANIMATION_CURVE_CACHE_SIZE; ++i) {
		struct curve_cache_entry *entry = &curve->cache.entry[i];
		if (entry->valid && entry->u == u) {
			entry->used = ++curve->cache.clock;
			*t = entry->t;
			*x = entry->x;
			*y = entry->y;
			return true;
		}
	}
	return false;
}

static void animation_curve_cache_update(struct sway_animation_curve *curve, double u,
		double t, double x, double y) {
	struct curve_cache_entry *lru = &curve->cache.entry[0];
	for (int i = 0; i < ANIMATION_CURVE_CACHE_SIZE; ++i) {
		struct curve_cache_entry *entry = &curve->cache.entry[i];
		if (!entry->valid) {
			lru = entry;
			break;
		}
		if (entry->used < lru->used) {
			lru = entry;
		}
	}
	lru->valid = true;
	lru->used = ++curve->cache.clock;
	lru->u = u;
	lru->t = t;
	lru->x = x;
	lru->y = y;
}

static void animation_curve_get_values(struct sway_animation_curve *curve, double u,
		double *t, double *x, double *y) {
	if (animation_curve_cache_query(curve, u, t, x, y)) {
		return;
	}
	if (u >= 1.0) {
		*t = 1.0;
		*x = 1.0; *y = 0.0;
		animation_curve_cache_update(curve, u, *t, *x, *y);
		return;
	}
	double t_off;
	if (curve->var.n > 0) {
		lookup_xy(&curve->var, u, &t_off, t);
	} else {
		*t = t_off = u;
	}

	// Now use t_off to get offset
	if (t_off >= 1.0) {
		*x = 1.0; *y = 0.0;
		animation_curve_cache_update(curve, u, *t, *x, *y);
		return;
	} else if (t_off < 0.0) {
		t_off = 0.0;
	}
	if (curve->off.n > 0) {
		lookup_xy(&curve->off, t_off, x, y);
	} else {
		*x = *t; *y = 0.0;
	}
	animation_curve_cache_update(curve, u, *t, *x, *y);
}

#define NSEC_PER_MSEC 1000000.0

struct bezier_spec {
	struct sway_animation_spec base;
	struct sway_animation_path *path;
};

static struct sway_animation_path *bezier_path(const struct sway_animation_spec *spec) {
	const struct bezier_spec *bezier = wl_container_of(spec, bezier, base);
	return bezier->path;
}

static void bezier_start(struct sway_animator_state *state,
		const struct sway_animation_spec *spec) {
	uint32_t duration = 0;
	const struct sway_animation_path *path = bezier_path(spec);
	for (int i = 0; i < path->curves->length; ++i) {
		struct sway_animation_curve *curve = path->curves->items[i];
		if (curve) {
			duration += curve->duration_ms;
		}
	}
	state->duration = duration * (int64_t)NSEC_PER_MSEC;
}

static void bezier_sample(const struct sway_animator_state *state,
		const struct sway_animation_spec *spec, int64_t elapsed,
		struct sway_animation_sample *sample) {
	const double ms = elapsed / NSEC_PER_MSEC;
	double start = 0.0;
	const struct sway_animation_path *path = bezier_path(spec);
	for (int i = 0; i < path->curves->length; ++i) {
		struct sway_animation_curve *curve = path->curves->items[i];
		if (!curve) {
			break;
		}
		if (ms - start <= curve->duration_ms) {
			double u = curve->duration_ms ?
				(ms - start) / curve->duration_ms : 1.0;
			double t, x, y;
			animation_curve_get_values(curve, u, &t, &x, &y);
			if (state->position) {
				sample->value = state->x0 + (state->x1 - state->x0) * x;
				sample->offset = y;
			} else {
				sample->value = state->x0 + (state->x1 - state->x0) * t;
				sample->offset = 0.0;
			}
			sample->done = false;
			return;
		}
		// Move to the next curve of the path
		start += curve->duration_ms;
	}
	sample->value = state->x1;
	sample->offset = 0.0;
	sample->done = true;
}

static void bezier_destroy(struct sway_animation_spec *spec) {
	animation_path_destroy(bezier_path(spec));
}

static const struct sway_animator_impl animator_bezier = {
	.uses_velocity = false,
	.start = bezier_start,
	.sample = bezier_sample,
	.destroy = bezier_destroy,
};

struct sway_animation_spec *animation_spec_create_path(
		struct sway_animation_path *path) {
	struct bezier_spec *bezier = calloc(1, sizeof(struct bezier_spec));
	if (!bezier) {
		animation_path_destroy(path);
		return NULL;
	}
	animation_spec_init(&bezier->base, &animator_bezier);
	bezier->path = path;
	return &bezier->base;
}

static void create_bezier(struct bezier_curve *curve, uint32_t order, list_t *points,
		double end[NDIM], bool simple) {
	if (points && points->length > 0) {
		curve->n = order;
		curve->simple = simple;

		for (int d = 0; d < NDIM; ++d) {
		    curve->b[d] = (double *) malloc(sizeof(double) * (curve->n + 1));
			// Set starting point (0, 0,...)
			curve->b[d][0] = 0.0;
		}

		// For faster evaluation we store each control point multiplied
		// by its binomial coefficient
		double comb = 1.0;
		for (uint32_t i = 1, idx = 0; i < curve->n; ++i) {
			comb = comb * (curve->n - i + 1) / i;
			for (int d = 0; d < NDIM; ++d) {
				double *x = points->items[idx++];
				curve->b[d][i] = comb * *x;
			}
		}
		// Set end points
		for (int d = 0; d < NDIM; ++d) {
			curve->b[d][curve->n] = end[d];
		}
		if (simple) {
			create_lookup_simple(curve);
		} else {
			create_lookup_length(curve);
		}
	} else {
		// Use linear parameter
		curve->n = 0;
		curve->simple = false;
	}
}

struct sway_animation_curve *create_animation_curve(uint32_t duration_ms,
		uint32_t var_order, list_t *var_points, bool var_simple, double offset_scale,
		uint32_t off_order, list_t *off_points) {
	if (var_points && (uint32_t) var_points->length != NDIM * (var_order - 1)) {
		sway_log(SWAY_ERROR, "Animation curve mismatch: var curve provided %d points, need %d for curve of order %d",
			var_points->length, NDIM * (var_order - 1), var_order);
		return NULL;
	}
	if (off_points && (uint32_t) off_points->length != NDIM * (off_order - 1)) {
		sway_log(SWAY_ERROR, "Animation curve mismatch: off curve provided %d points, need %d for curve of order %d",
			off_points->length, NDIM * (off_order - 1), off_order);
		return NULL;
	}
	if (var_simple && var_order != 3) {
		sway_log(SWAY_ERROR, "Animation curve mismatch: simple curves need to be cubic Beziers with two user-set control points");
		return NULL;

	}
	struct sway_animation_curve *curve = (struct sway_animation_curve *) malloc(sizeof(struct sway_animation_curve));
	curve->duration_ms = duration_ms;
	animation_curve_cache_init(curve);

	double end_var[2] = { 1.0, 1.0 };
	create_bezier(&curve->var, var_order, var_points, end_var, var_simple);
	double end_off[2] = { 1.0, 0.0 };
	if (off_points && off_points->length > 0) {
		const double xc = 0.5, yc = 0;
		int i = 0;
		while (i < off_points->length) {
			double *x = off_points->items[i++];
			double *y = off_points->items[i++];
			*x = xc + offset_scale * (*x - xc);
			*y = yc + offset_scale * (*y - yc);
		}
	}
	create_bezier(&curve->off, off_order, off_points, end_off, false);

	return curve;
}

static void destroy_animation_curve(struct sway_animation_curve *curve) {
	if (!curve) {
		return;
	}
	for (int i = 0; i < NDIM; ++i) {
		if (curve->var.n > 0) {
			free(curve->var.b[i]);
		}
		if (curve->off.n > 0) {
			free(curve->off.b[i]);
		}
	}
	free(curve);
}
