#include "sway/desktop/animation.h"
#include "sway/server.h"
#include "log.h"
#include "util.h"
#include <wayland-server-core.h>


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

struct sway_animation_curve {
	uint32_t duration_ms;
	double offset_scale;
	struct bezier_curve var;
	struct bezier_curve off;
};

static uint32_t comb_n_i(uint32_t n, uint32_t i) {
	if (i > n) {
		return 0;
	}
	int comb = 1;
	for (uint32_t j = n; j > i; --j) {
		comb *= j;
	}
	for (uint32_t j = n - i; j > 1; --j) {
		comb /= j;
	}
	return comb;
}

static double bernstein(uint32_t n, uint32_t i, double t) {
	if (n == 0) {
		return 1.0;
	}
	double B = comb_n_i(n, i) * pow(t, i) * pow(1.0 - t, n - i);
	return B;
}

static void bezier(struct bezier_curve *curve, double t, double (*B)[NDIM]) {
	for (uint32_t d = 0; d < NDIM; ++d) {
		(*B)[d] = 0.0;
	}
	for (uint32_t i = 0; i <= curve->n; ++i) {
		double b = bernstein(curve->n, i, t);
		for (uint32_t d = 0; d < NDIM; ++d) {
			(*B)[d] += curve->b[d][i] * b;
		}
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
		double t = i * length / NINTERVALS;
		while (t > len1) {
			len0 = len1;
			u0 = (double) last / NINTERVALS;
			len1 += T[++last];
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
	bool enabled;
	int idx;
	list_t *curves;	// struct sway_animation_curve
};

struct sway_animation {
	uint32_t nsteps;
	uint32_t step;
	struct wl_event_source *timer;

	struct sway_animation_path *path;
	struct sway_animation_callbacks callbacks;
};

static struct sway_animation animation = {
	.timer = NULL,
	.path = NULL,
};

struct sway_animation_path *animation_path_create(bool enabled) {
	struct sway_animation_path *path = malloc(sizeof(struct sway_animation_path));
	path->enabled = enabled;
	path->idx = 0;
	path->curves = create_list();
	return path;
}

void animation_path_destroy(struct sway_animation_path *path) {
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

// Set the callbacks for the current animation
void animation_set_callbacks(struct sway_animation_callbacks *callbacks) {
	animation.callbacks.callback_begin = callbacks->callback_begin;
	animation.callbacks.callback_begin_data = callbacks->callback_begin_data;
	animation.callbacks.callback_step = callbacks->callback_step;
	animation.callbacks.callback_step_data = callbacks->callback_step_data;
	animation.callbacks.callback_end = callbacks->callback_end;
	animation.callbacks.callback_end_data = callbacks->callback_end_data;
}

struct sway_animation_callbacks *animation_get_callbacks() {
	return &animation.callbacks;
}

// Set the active path for the animation
void animation_set_path(struct sway_animation_path *path) {
	animation.path = path;
}

struct sway_animation_path *animation_get_path() {
	return animation.path;
}

static struct sway_animation_path *get_path() {
	if (!config->animations.enabled || config->reloading) {
		return NULL;
	}
	struct sway_animation_path *path;
	if (!animation.path) {
		path = config->animations.anim_default;
	} else {
		path = animation.path;
	}
	if (path->enabled) {
		return path;
	}
	return NULL;
}

static struct sway_animation_curve *get_curve() {
	struct sway_animation_path *path = get_path();
	if (path) {
		struct sway_animation_curve *curve = path->curves->items[path->idx];
		return curve;
	}
	return NULL;
}

static int timer_callback(void *data) {
	struct sway_animation *animation = data;
	++animation->step;
	if (animation->step <= animation->nsteps) {
		if (animation->callbacks.callback_step) {
			animation->callbacks.callback_step(animation->callbacks.callback_step_data);
		}
		wl_event_source_timer_update(animation->timer, config->animations.frequency_ms);
	} else {
		// This is where we set the one in config if disabled or if not, default
		struct sway_animation_path *path = get_path();
		if (path) {
			path->idx++;
			if (path->idx >= path->curves->length) {
				path->idx = 0;
			}
		}
		animation->path = config->animations.anim_default;
		if (animation->callbacks.callback_end) {
			animation->callbacks.callback_end(animation->callbacks.callback_end_data);
		}
	}
	return 0;
}

// Is an animation enabled?
bool animation_enabled() {
	struct sway_animation_path *path = get_path();
	if (!path) {
		return false;
	} else {
		return path->enabled;
	}
}

static uint32_t animation_get_duration_ms() {
	struct sway_animation_curve *curve = get_curve();
	if (!curve) {
		return 0;
	} else {
		return curve->duration_ms;
	}
}

// Select the next key
void animation_next_key() {
	struct sway_animation_path *path = get_path();
	if (path) {
		animation.nsteps = max(1, animation_get_duration_ms() / config->animations.frequency_ms);
		if (animation.nsteps > 0) {
			animation.step = 1;
			if (animation.callbacks.callback_begin) {
				animation.callbacks.callback_begin(animation.callbacks.callback_begin_data);
			}
			animation.callbacks.callback_step(animation.callbacks.callback_step_data);
			if (animation.timer) {
				wl_event_source_remove(animation.timer);
				path->idx = 0;
			}
			animation.timer = wl_event_loop_add_timer(server.wl_event_loop,
				timer_callback, &animation);
			if (animation.timer) {
				wl_event_source_timer_update(animation.timer, config->animations.frequency_ms);
			} else {
				sway_log_errno(SWAY_ERROR, "Unable to create animation timer");
			}
			return;
		}
	}
	animation.callbacks.callback_step(animation.callbacks.callback_step_data);
}

static void lookup_xy(struct bezier_curve *curve, double t, double *x, double *y) {
	// Interpolate in the lookup table
	double t0 = floor(t * NINTERVALS);
	double t1 = ceil(t * NINTERVALS);
	double u;
	if (t0 != t1) {
		double u0 = curve->u[(uint32_t) t0];
		double u1 = curve->u[(uint32_t) t1];
		double k = (t * NINTERVALS - t0) / (t1 - t0);
		u = (1.0 - k) * u0 + k * u1;
	} else {
		u = curve->u[(uint32_t) t0];
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

static void animation_curve_get_values(struct sway_animation_curve *curve, double u,
		double *t, double *x, double *y, double *scale) {
	if (u >= 1.0) {
		*t = 1.0;
		*x = 1.0; *y = 0.0;
		*scale = 0.0;
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
		*scale = 0.0;
		return;
	}
	if (curve->off.n > 0) {
		lookup_xy(&curve->off, t_off, x, y);
		*scale = curve->offset_scale;
	} else {
		*x = *t; *y = 0.0;
		*scale = 0.0;
	}
}

// Get the current parameters for the active animation
void animation_get_values(double *t, double *x, double *y,
		double *offset_scale) {
	struct sway_animation_curve *curve = get_curve();
	if (!curve) {
		*t = 1.0; *x = 1.0, *y = 0.0, *offset_scale = 0.0;
		return;
	}
	double u = animation.step / (double)animation.nsteps;
	animation_curve_get_values(curve, u, t, x, y, offset_scale);
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

		for (uint32_t i = 1, idx = 0; i < curve->n; ++i) {
			for (int d = 0; d < NDIM; ++d) {
				double *x = points->items[idx++];
				curve->b[d][i] = *x;
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
	curve->offset_scale = offset_scale;

	double end_var[2] = { 1.0, 1.0 };
	create_bezier(&curve->var, var_order, var_points, end_var, var_simple);
	double end_off[2] = { 1.0, 0.0 };
	create_bezier(&curve->off, off_order, off_points, end_off, false);

	return curve;
}

void destroy_animation_curve(struct sway_animation_curve *curve) {
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
