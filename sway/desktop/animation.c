#include "sway/desktop/animation.h"
#include "sway/server.h"
#include "log.h"
#include <wayland-server-core.h>
#include "sway/output.h"
#include "sway/desktop/transaction.h"

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

enum sway_animation_enabled {
	ANIMATION_ENABLED_UNKNOWN,
	ANIMATION_ENABLED_YES,
	ANIMATION_ENABLED_NO,
};

struct sway_animation {
	bool animating;
	struct timespec start;
	double time;
	struct wl_event_source *timer;

	list_t *outputs;
	enum sway_animation_enabled enabled;
	struct {
		struct sway_animation_path *path;
		struct sway_animation_callbacks callbacks;
	} current;
	struct {
		struct sway_animation_path *path;
		struct sway_animation_callbacks callbacks;
	} pending;

	struct sway_animation_callbacks default_callbacks;

	struct sway_animation_config config;
};

static struct sway_animation *animation = NULL;

void animation_create() {
	if (animation) {
		animation_destroy();
	}
	animation = calloc(1, sizeof(struct sway_animation));

	animation->config.frequency_ms = 16; // ~60 Hz
	animation->config.enabled = true;
	animation->config.style = ANIM_STYLE_SCALE;
	animation->config.anim_disabled = animation_path_create(false);
	double points[] = { 0.215, 0.61, 0.355, 1.0 };
	list_t *default_points = create_list();
	for (uint32_t i = 0; i < sizeof(points) / sizeof(double); ++i) {
		double *val = malloc(sizeof(double));
		*val = points[i];
		list_add(default_points, val);
	}
	struct sway_animation_curve *curve = create_animation_curve(300, 3, default_points, false, 0.0, 0, NULL);
	animation->config.anim_default = animation_path_create(true);
	animation_path_add_curve(animation->config.anim_default, curve);
	animation_set_type(ANIMATION_DEFAULT);
	list_free_items_and_destroy(default_points);
	animation->config.window_open = NULL;
	animation->config.window_move = NULL;
	animation->config.window_move_float = NULL;
	animation->config.window_fullscreen = NULL;
	animation->config.window_size = NULL;
	animation->config.workspace_switch = NULL;
	animation->config.overview = NULL;
	animation->config.jump = NULL;

	config_default_animation_callbacks();
	animation->current.callbacks = animation->default_callbacks;
	animation->pending.callbacks = animation->default_callbacks;
	animation->outputs = create_list();
}

void animation_destroy() {
	if (animation) {
		if (root && animation->timer) {
			wl_event_source_remove(animation->timer);
		}
		if (animation->outputs) {
			list_free(animation->outputs);
		}
		if (animation->config.jump) {
			animation_path_destroy(animation->config.jump);
		}
		if (animation->config.overview) {
			animation_path_destroy(animation->config.overview);
		}
		if (animation->config.workspace_switch) {
			animation_path_destroy(animation->config.workspace_switch);
		}
		if (animation->config.window_size) {
			animation_path_destroy(animation->config.window_size);
		}
		if (animation->config.window_move) {
			animation_path_destroy(animation->config.window_move);
		}
		if (animation->config.window_move_float) {
			animation_path_destroy(animation->config.window_move_float);
		}
		if (animation->config.window_fullscreen) {
			animation_path_destroy(animation->config.window_fullscreen);
		}
		if (animation->config.window_open) {
			animation_path_destroy(animation->config.window_open);
		}
		if (animation->config.anim_default) {
			animation_path_destroy(animation->config.anim_default);
		}
		if (animation->config.anim_disabled) {
			animation_path_destroy(animation->config.anim_disabled);
		}
		free(animation);
		animation = NULL;
	}
}

struct sway_animation_config *animation_get_config() {
	return &animation->config;
}

static int get_animating_index(struct wlr_output *output) {
	return list_find(animation->outputs, output);
}

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

// Set the callbacks for the pending animation
void animation_set_default_callbacks(struct sway_animation_callbacks *callbacks) {
	animation->default_callbacks.callback_begin = callbacks->callback_begin;
	animation->default_callbacks.callback_begin_data = callbacks->callback_begin_data;
	animation->default_callbacks.callback_step = callbacks->callback_step;
	animation->default_callbacks.callback_step_data = callbacks->callback_step_data;
	animation->default_callbacks.callback_end = callbacks->callback_end;
	animation->default_callbacks.callback_end_data = callbacks->callback_end_data;
}

void animation_set_callbacks(struct sway_animation_callbacks *callbacks) {
	animation->pending.callbacks.callback_begin = callbacks->callback_begin;
	animation->pending.callbacks.callback_begin_data = callbacks->callback_begin_data;
	animation->pending.callbacks.callback_step = callbacks->callback_step;
	animation->pending.callbacks.callback_step_data = callbacks->callback_step_data;
	animation->pending.callbacks.callback_end = callbacks->callback_end;
	animation->pending.callbacks.callback_end_data = callbacks->callback_end_data;
}

struct sway_animation_callbacks *animation_get_callbacks() {
	return &animation->pending.callbacks;
}

// Set the type of the pending animation
void animation_set_type(enum sway_animation_type anim) {
	switch (anim) {
	case ANIMATION_DISABLED:
		animation->pending.path = animation->config.anim_disabled;
		break;
	case ANIMATION_DEFAULT:
	default:
		animation->pending.path = animation->config.anim_default;
		break;
	case ANIMATION_WINDOW_OPEN:
		animation->pending.path = animation->config.window_open;
		break;
	case ANIMATION_WINDOW_SIZE:
		animation->pending.path = animation->config.window_size;
		break;
	case ANIMATION_WINDOW_MOVE:
		animation->pending.path = animation->config.window_move;
		break;
	case ANIMATION_WINDOW_MOVE_FLOAT:
		animation->pending.path = animation->config.window_move_float;
		break;
	case ANIMATION_WINDOW_FULLSCREEN:
		animation->pending.path = animation->config.window_fullscreen;
		break;
	case ANIMATION_WORKSPACE_SWITCH:
		animation->pending.path = animation->config.workspace_switch;
		break;
	case ANIMATION_OVERVIEW:
		animation->pending.path = animation->config.overview;
		break;
	case ANIMATION_JUMP:
		animation->pending.path = animation->config.jump;
		break;
	}
}

static struct sway_animation_path *get_path() {
	if (!animation->config.enabled || config->reloading) {
		return NULL;
	}
	struct sway_animation_path *path;
	if (!animation->current.path) {
		path = animation->config.anim_default;
	} else {
		path = animation->current.path;
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

static void animation_reset_path(struct sway_animation_path *path) {
	path->idx = 0;
}

static uint32_t difftime_ms(struct timespec *t0, struct timespec *t1) {
	struct timespec diff = {
		.tv_sec = t1->tv_sec - t0->tv_sec,
		.tv_nsec = t1->tv_nsec - t0->tv_nsec
	};
	if (diff.tv_nsec < 0) {
		diff.tv_nsec += 1000000000; // nsec/sec
		diff.tv_sec--;
	}
	return diff.tv_sec * 1000 + diff.tv_nsec / 1000000;
}

static void addtime_ms(struct timespec *time, uint32_t ms) {
	struct timespec added = {
		.tv_sec = time->tv_sec + ms / 1000,
		.tv_nsec = time->tv_nsec + (ms % 1000) * 1000000,
	};
	added.tv_sec += added.tv_nsec / 1000000000;
	added.tv_nsec = added.tv_nsec % 1000000000;
	*time = added;
}

static void schedule_frames() {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		int idx = get_animating_index(output->wlr_output);
		if (idx >= 0) {
			wlr_output_schedule_frame(output->wlr_output);
		}
	}
}

static bool is_animating() {
	if (animation->enabled == ANIMATION_ENABLED_NO || animation->outputs->length == 0) {
		return false;
	}
	return true;
}

static int timer_callback(void *data) {
	struct sway_animation *animation = data;
	if (animation->animating) {
		schedule_frames();
		wl_event_source_timer_update(animation->timer, animation->config.frequency_ms);
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

bool animation_animating(struct wlr_output *output) {
	if (animation->enabled == ANIMATION_ENABLED_NO) {
		return false;
	}
	int idx = get_animating_index(output);
	return idx >= 0;
}

void animation_add_output(struct wlr_output *output) {
	int idx = get_animating_index(output);
	if (idx < 0) {
		list_add(animation->outputs, output);
	}
}

void animation_add_all_outputs() {
	animation_reset_outputs();
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		if (output->enabled && output->wlr_output->enabled) {
			list_add(animation->outputs, output->wlr_output);
		}
	}
}

void animation_reset_outputs() {
	if (animation->outputs) {
		list_reset(animation->outputs);
	}
}

void animation_set_animation_enabled(bool enable) {
	switch (animation->enabled) {
	case ANIMATION_ENABLED_UNKNOWN:
		animation->enabled = enable ? ANIMATION_ENABLED_YES : ANIMATION_ENABLED_NO;
		break;
	case ANIMATION_ENABLED_YES:
		break;
	case ANIMATION_ENABLED_NO:
		if (enable) {
			animation->enabled = ANIMATION_ENABLED_YES;
		}
		break;
	}
}

static void stop_animation() {
	if (animation->animating) {
		animation->animating = false;
		if (animation->timer) {
			wl_event_source_remove(animation->timer);
			animation->timer = NULL;
		}
		if (animation->current.callbacks.callback_end) {
			animation->current.callbacks.callback_end(animation->current.callbacks.callback_end_data);
		}
	}
}

void animation_end() {
	stop_animation();
}

// Begin the pending animation
void animation_begin() {
	stop_animation();
	animation->current.path = animation->pending.path;
	animation->current.callbacks = animation->pending.callbacks;
	animation->pending.path = animation->config.anim_default;
	animation->pending.callbacks = animation->default_callbacks;
	struct sway_animation_path *path = get_path();
	if (path) {
		animation_reset_path(path);
		animation->animating = true;
		animation->enabled = ANIMATION_ENABLED_UNKNOWN;
		clock_gettime(CLOCK_MONOTONIC, &animation->start);
		if (animation->current.callbacks.callback_begin) {
			animation->current.callbacks.callback_begin(animation->current.callbacks.callback_begin_data);
		}
		schedule_frames();
		if (animation->timer) {
			wl_event_source_remove(animation->timer);
		}
		animation->timer = wl_event_loop_add_timer(server.wl_event_loop,
			timer_callback, animation);
		if (animation->timer) {
			wl_event_source_timer_update(animation->timer, animation->config.frequency_ms);
		} else {
			sway_log_errno(SWAY_ERROR, "Unable to create animation timer");
		}
		return;
	}
	animation->current.callbacks.callback_step(animation->current.callbacks.callback_step_data);
}

static bool animation_output_filter(struct sway_output *output, void *data) {
	return list_find(animation->outputs, output->wlr_output) >= 0;
}

// Returns true if the animation path ended
static bool animation_set_time(struct timespec *time) {
	struct sway_animation_path *path = get_path();
	if (!path) {
		goto last;
	}
	while (true) {
		struct sway_animation_curve *curve = path->curves->items[path->idx];
		if (!curve) {
			goto last;
		}
		uint32_t diff = difftime_ms(&animation->start, time);
		uint32_t duration = curve->duration_ms;
		animation->time = (double) diff / duration;
		if (animation->time <= 1.0) {
			break;
		}
		++path->idx;
		if (path->idx >= path->curves->length) {
			path->idx = path->curves->length - 1;
			goto last;
		} else {
			addtime_ms(&animation->start, duration);
		}
	}
	return false;

last:
	animation->time = 1.0;
	return true;
}

void animation_animate(struct wlr_output *output) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	bool ended = animation_set_time(&now);

	// Save old filters and push new
	sway_root_output_filter_func_t old_filter = root->filters.output_filter;
	void *old_filter_data = root->filters.output_filter_data;
	root->filters.output_filter = animation_output_filter;
	root->filters.output_filter_data = NULL;

	animation->current.callbacks.callback_step(animation->current.callbacks.callback_step_data);

	// Restore old filters
	root->filters.output_filter = old_filter;
	root->filters.output_filter_data = old_filter_data;

	if (ended) {
		int idx = get_animating_index(output);
		if (idx >= 0) {
			list_del(animation->outputs, idx);
		}
	}

	if (!is_animating()) {
		stop_animation();
	}
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
	} else if (t_off < 0.0) {
		t_off = 0.0;
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
	double u = animation->time;
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
