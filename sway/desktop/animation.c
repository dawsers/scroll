#include <math.h>
#include <string.h>
#include "sway/desktop/animation.h"
#include "sway/server.h"
#include "sway/log.h"
#include <wayland-server-core.h>
#include "sway/output.h"
#include "sway/desktop/transaction.h"
#include "util.h"

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
	bool enabled;
	list_t *curves;	// struct sway_animation_curve
};

struct sway_animation_state {
	enum sway_animation_type type;
	struct sway_animation_path *path;
	struct sway_animation_callbacks callbacks;
	struct sway_transaction *transaction;
};

struct sway_animation {
	bool animating;
	bool finishing;  // running the last step of an animation
	bool stepping;  // we are within an animation step (frame)
	// Outputs taking part in the animation have this animation_id. Resetting
	// outputs simply changes this id, so the output's values become invalid.
	// It is never 0; 0 is also an animation_id of outputs that are not animating.
	uint32_t id;
	// Time of the current step, common to all animated variables
	struct timespec frame_time;

	// The output currently being rendered. When set, animation callbacks
	// should only process this output instead of looping all outputs.
	// NULL when called from a non-per-output path (e.g. disabled animations).
	struct wlr_output *current_output;
	struct sway_animation_state current, pending;
	// List of transactions that cannot be destroyed yet because they hold some
	// animating node.
	list_t * transactions;  // struct sway_transaction

	// List of active animated variables
	struct wl_list variables;  // sway_animated_variable.link

	struct sway_animation_callbacks default_callbacks;

	struct sway_animation_config config;
};

static struct sway_animation *animation = NULL;

static void animated_variables_clear(void);
static void animated_variable_restart(struct sway_animated_variable *av,
		struct timespec *now);

void animation_create() {
	if (animation) {
		animation_destroy();
	}
	animation = calloc(1, sizeof(struct sway_animation));
	wl_list_init(&animation->variables);

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
	animation->config.layer_shell = NULL;
	animation->config.fade_in = NULL;
	animation->config.fade_out = NULL;

	config_default_animation_callbacks();
	animation->id = 1;
	animation->current.type = ANIMATION_DEFAULT;
	animation->current.callbacks = animation->default_callbacks;
	animation->pending.callbacks = animation->default_callbacks;
	animation->transactions = create_list();
}

void animation_destroy() {
	if (animation) {
		animated_variables_clear();
		if (animation->transactions) {
			for (int i = 0; i < animation->transactions->length; ++i) {
				transaction_destroy(animation->transactions->items[i]);
			}
			list_free(animation->transactions);
		}
		if (animation->config.fade_out) {
			animation_path_destroy(animation->config.fade_out);
		}
		if (animation->config.fade_in) {
			animation_path_destroy(animation->config.fade_in);
		}
		if (animation->config.layer_shell) {
			animation_path_destroy(animation->config.layer_shell);
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

// Does this output take part in the animation?
static bool is_animating_output(struct wlr_output *output) {
	struct sway_output *sway_output = output ? output->data : NULL;
	return sway_output && sway_output->animation_id == animation->id;
}

static int animating_output_count(void) {
	int count = 0;
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		count += is_animating_output(output->wlr_output);
	}
	return count;
}

struct sway_animation_path *animation_path_create(bool enabled) {
	struct sway_animation_path *path = malloc(sizeof(struct sway_animation_path));
	path->enabled = enabled;
	path->curves = create_list();
	return path;
}

static void destroy_animation_curve(struct sway_animation_curve *curve);

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

static struct sway_animation_path *path_for_type(enum sway_animation_type anim) {
	struct sway_animation_path *path;
	switch (anim) {
	case ANIMATION_DISABLED:
		path = animation->config.anim_disabled;
		break;
	case ANIMATION_DEFAULT:
	default:
		path = animation->config.anim_default;
		break;
	case ANIMATION_WINDOW_OPEN:
		path = animation->config.window_open;
		break;
	case ANIMATION_WINDOW_SIZE:
		path = animation->config.window_size;
		break;
	case ANIMATION_WINDOW_MOVE:
		path =  animation->config.window_move;
		break;
	case ANIMATION_WINDOW_MOVE_FLOAT:
		path =  animation->config.window_move_float;
		break;
	case ANIMATION_WINDOW_FULLSCREEN:
		path = animation->config.window_fullscreen;
		break;
	case ANIMATION_WORKSPACE_SWITCH:
		path = animation->config.workspace_switch;
		break;
	case ANIMATION_OVERVIEW:
		path = animation->config.overview;
		break;
	case ANIMATION_JUMP:
		path = animation->config.jump;
		break;
	case ANIMATION_LAYER_SHELL:
		path = animation->config.layer_shell;
		break;
	case ANIMATION_FADE_IN:
		path = animation->config.fade_in;
		break;
	case ANIMATION_FADE_OUT:
		path = animation->config.fade_out;
		break;
	}
	if (!path) {
		path = animation->config.anim_default;
	}
	return path;
}

// The enabled path of an animation type, or NULL if it is not animated.
static struct sway_animation_path *enabled_path_for_type(enum sway_animation_type anim) {
	if (!animation->config.enabled || config->reloading) {
		return NULL;
	}
	struct sway_animation_path *path = path_for_type(anim);
	return path->enabled ? path : NULL;
}

bool animation_path_enabled(enum sway_animation_type anim) {
	return enabled_path_for_type(anim) != NULL;
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

// Set the transaction for the pending state
void animation_set_transaction(struct sway_transaction *transaction) {
	animation->pending.transaction = transaction;
}

// Set the type of the pending animation
void animation_set_type(enum sway_animation_type anim) {
	animation->pending.type = anim;
}

enum sway_animation_type animation_get_pending_type(void) {
	return animation->pending.type;
}

static struct sway_animation_path *get_path() {
	return enabled_path_for_type(animation->current.type);
}

static struct sway_animation_path *animated_variable_path(struct sway_animated_variable *av) {
	return enabled_path_for_type(av->animation);
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

static bool schedule_frames(void) {
	bool scheduled = false;
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		if (is_animating_output(output->wlr_output)) {
			wlr_output_schedule_frame(output->wlr_output);
			scheduled = true;
		}
	}
	return scheduled;
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

struct wlr_output *animation_get_current_output(void) {
	return animation ? animation->current_output : NULL;
}

bool animation_animating() {
	return animation->animating;
}

bool animation_animating_output(struct wlr_output *output) {
	return animation && animation->animating && is_animating_output(output);
}

void animation_add_all_outputs() {
	animation_reset_outputs();
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		output->animation_id =
			(output->enabled && output->wlr_output->enabled) ? animation->id : 0;
	}
}

void animation_reset_outputs() {
	if (++animation->id == 0) {
		animation->id = 1;
	}
}

void animation_output_gone(struct wlr_output *output) {
	if (!output) {
		return;
	}
	if (is_animating_output(output)) {
		((struct sway_output *)output->data)->animation_id = 0;
	}
	if (animating_output_count()) {
		return;
	}
	animation_add_all_outputs();
	if (schedule_frames()) {
		return;
	}
	if (animation->animating) {
		// No outputs left, end the animation or its variables would stay
		// active forever, and commit a possible delayed transaction that could
		// be waiting for this animation to end.
		animation_end();
		transaction_commit_delayed();
	}
}

// Delay the destruction of transactions that still have nodes in the middle of
// an animation, keeping them on a list.
static void animation_keep_or_destroy_transaction(struct sway_transaction *transaction) {
	if (transaction_delays_destruction(transaction)) {
		list_add(animation->transactions, transaction);
	} else {
		transaction_destroy(transaction);
	}
}

// Free the transactions that were kept for unfinished animations.
static void animation_sweep_transactions(void) {
	for (int i = animation->transactions->length - 1; i >= 0; --i) {
		struct sway_transaction *transaction = animation->transactions->items[i];
		if (!transaction_delays_destruction(transaction)) {
			list_del(animation->transactions, i);
			transaction_destroy(transaction);
		}
	}
}

static void stop_animation() {
	if (animation->animating) {
		animation->animating = false;
		struct sway_animation_state state = animation->current;
		if (state.callbacks.callback_end) {
			state.callbacks.callback_end(state.callbacks.callback_end_data);
		}
		if (state.transaction) {
			animation_keep_or_destroy_transaction(state.transaction);
		}
		animation->current.transaction = NULL;
	}
	animation->current_output = NULL;
}

void animation_end() {
	if (animation->animating) {
		// Apply the final values of the animation to the scene graph.
		animation->finishing = true;
		animation->stepping = true;
		animation->current.callbacks.callback_step(animation->current.callbacks.callback_step_data);
		animation->stepping = false;
		animation->finishing = false;
	}
	animated_variables_finish();
	stop_animation();
	animation_sweep_transactions();
}

// End the animation, but leaving animated variables at their current state,
// so those not affected by the new transaction can continue their animations
// from where they were.
void animation_interrupt(void) {
	stop_animation();
}

void animation_begin() {
	stop_animation();
	animation->current = animation->pending;
	animation->pending.type = ANIMATION_DEFAULT;
	animation->pending.callbacks = animation->default_callbacks;
	animation->pending.transaction = NULL;

	clock_gettime(CLOCK_MONOTONIC, &animation->frame_time);
	// Variables affected by this transaction start their new animations now.
	// The others keep the clock they had, so they run for their remaining
	// duration.
	struct sway_animated_variable *av;
	wl_list_for_each(av, &animation->variables, link) {
		if (av->pending_start) {
			animated_variable_restart(av, &animation->frame_time);
		}
	}

	// A transaction whose type has an animation path is animated even when it
	// animates nothing itself, until a frame finds out there is nothing to do.
	animation->animating = animated_variables_count() > 0 || get_path() != NULL;
	if (animating_output_count() == 0 && animated_variables_count() > 0) {
		// There are animations running in other outputs.
		animation_add_all_outputs();
	}
	if (animation->animating) {
		if (schedule_frames()) {
			if (get_path() && animation->current.callbacks.callback_begin) {
				animation->current.callbacks.callback_begin(
					animation->current.callbacks.callback_begin_data);
			}
			return;
		}
		// There were no outputs with scheduled frames for this transaction,
		// so apply their final values directly.
		animation->animating = false;
		animated_variables_finish();
	}
	animation->stepping = true;
	animation->current.callbacks.callback_step(animation->current.callbacks.callback_step_data);
	animation->stepping = false;
	animation_sweep_transactions();
}

static bool animation_output_filter(struct sway_output *output, void *data) {
	return is_animating_output(output->wlr_output);
}

void animation_animate(struct wlr_output *output) {
	animated_variables_update();

	// Save old filters and push new
	sway_root_output_filter_func_t old_filter = root->filters->output_filter;
	void *old_filter_data = root->filters->output_filter_data;
	root->filters->output_filter = animation_output_filter;
	root->filters->output_filter_data = NULL;

	// Set current output so callback_step only processes this output
	// instead of looping all animating outputs (avoids N² work)
	animation->current_output = output;
	animation->stepping = true;
	animation->current.callbacks.callback_step(animation->current.callbacks.callback_step_data);
	animation->stepping = false;
	animation->current_output = NULL;

	// Restore old filters
	root->filters->output_filter = old_filter;
	root->filters->output_filter_data = old_filter_data;

	if (animated_variables_count() == 0) {
		// All the animated variables reached their destinations, so there is
		// nothing left to animate in any output. callback_step only applies
		// to the animating output, but variables may belong to any output, so
		// we need to step every output one last time to make sure the owner
		// of the variable gets its final value.
		animation->stepping = true;
		animation->current.callbacks.callback_step(
			animation->current.callbacks.callback_step_data);
		animation->stepping = false;
		stop_animation();
		animation_reset_outputs();
		transaction_commit_delayed();
	}
	animation_sweep_transactions();
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

// Animated variables
// Positions are animated only when the change is big enough to be seen: the
// layout computes positions with fractions and rounding, so containers may end
// up in a position that differs from the previous one in a fraction of a pixel.
#define ANIMATED_VARIABLE_EPSILON 0.000001
#define ANIMATED_VARIABLE_POSITION_EPSILON 1.0

// Compute the current value of the variable from its origin and destination
static void animated_variable_apply(struct sway_animated_variable *av) {
	double progress = av->ct;
	if (av->type == ANIMATED_VARIABLE_POSITION) {
		av->xt = av->x0 + (av->x1 - av->x0) * av->cx + av->cy * av->span;
		return;
	}
	if (av->type == ANIMATED_VARIABLE_FADE) {
		progress = fmin(fmax(progress, 0.0), 1.0);
	}
	av->xt = linear_scale(av->x0, av->x1, progress);
	if (av->type == ANIMATED_VARIABLE_SIZE) {
		av->xt = fmax(1.0, av->xt);
	}
}

void animated_variable_init(struct sway_animated_variable *av, double value,
		enum sway_animated_variable_type type) {
	memset(av, 0, sizeof(struct sway_animated_variable));
	av->type = type;
	av->animation = ANIMATION_DEFAULT;
	av->ct = av->cx = 1.0;
	av->x0 = av->x1 = value;
	animated_variable_apply(av);
	wl_list_init(&av->link);
}

void animated_variable_finish(struct sway_animated_variable *av) {
	av->ct = av->cx = 1.0;
	av->cy = 0.0;
	av->pending_start = false;
	animated_variable_apply(av);
	if (av->animating) {
		av->animating = false;
		wl_list_remove(&av->link);
		wl_list_init(&av->link);
	}
}

void animated_variable_release(struct sway_animated_variable *av) {
	if (av->animating) {
		av->animating = false;
		wl_list_remove(&av->link);
	}
	wl_list_init(&av->link);
}

void animated_variable_reset(struct sway_animated_variable *av, double value) {
	if (av->animating) {
		return;
	}
	av->x0 = av->x1 = value;
	animated_variable_finish(av);
}

void animated_variable_cancel(struct sway_animated_variable *av) {
	if (!av->animating || !av->pending_start) {
		// The variable is not animating, or the animation has already begun
		return;
	}
	av->x1 = av->x0;
	animated_variable_finish(av);
}

// Start a new animation for the variable, from its current value to `value`.
static void animated_variable_start(struct sway_animated_variable *av,
		double value) {
	av->x0 = av->xt;
	av->x1 = value;
	av->ct = av->cx = 0.0;
	av->cy = 0.0;
	av->curve = 0;
	// Only modify the clock if the animation starts
	av->pending_start = !animation->stepping;
	if (!av->pending_start) {
		av->start = animation->frame_time;
	}
	animated_variable_apply(av);
	if (!av->animating) {
		av->animating = true;
		wl_list_insert(&animation->variables, &av->link);
	}
}

// Start the clock of a variable with a new animation from the current transaction
static void animated_variable_restart(struct sway_animated_variable *av,
		struct timespec *now) {
	av->pending_start = false;
	av->curve = 0;
	av->ct = av->cx = 0.0;
	av->cy = 0.0;
	av->start = *now;
	av->x0 = av->xt;
	animated_variable_apply(av);
}

// Advance the clock of the variable and update its value. Returns false when
// the variable has reached its destination.
static bool animated_variable_advance(struct sway_animated_variable *av,
		struct timespec *now) {
	struct sway_animation_path *path = animated_variable_path(av);
	if (!path) {
		return false;
	}
	if (av->pending_start) {
		// Stay at the origin until the animation begins
		av->ct = av->cx = 0.0;
		av->cy = 0.0;
		animated_variable_apply(av);
		return true;
	}
	while (av->curve < path->curves->length) {
		struct sway_animation_curve *curve = path->curves->items[av->curve];
		if (!curve) {
			return false;
		}
		uint32_t diff = difftime_ms(&av->start, now);
		if (diff <= curve->duration_ms) {
			double u = curve->duration_ms ?
				(double) diff / curve->duration_ms : 1.0;
			animation_curve_get_values(curve, u, &av->ct, &av->cx, &av->cy);
			animated_variable_apply(av);
			return true;
		}
		// Move to the next curve of the path
		addtime_ms(&av->start, curve->duration_ms);
		++av->curve;
	}
	return false;
}

bool animated_variable_set(struct sway_animated_variable *av, double value,
		enum sway_animation_type type) {
	double epsilon = (av->type == ANIMATED_VARIABLE_POSITION) ?
		ANIMATED_VARIABLE_POSITION_EPSILON : ANIMATED_VARIABLE_EPSILON;
	if (animation->finishing) {
		// The animation is ending: the variable moves to its final value.
		av->x0 = av->x1 = value;
		animated_variable_finish(av);
		return false;
	}
	if (av->animating && fabs(value - av->x1) <= epsilon) {
		// This variable is not affected by the new transaction: keep its
		// animation and its animation type, so it runs for its remaining
		// duration.
		return true;
	}
	av->animation = type;
	if (!animated_variable_path(av) || fabs(value - av->xt) <= epsilon) {
		// Not animated, or nowhere to go; move to the final value now.
		av->x0 = av->x1 = value;
		animated_variable_finish(av);
		return false;
	}
	// New or interrupted animation: continue from the value we have right now.
	animated_variable_start(av, value);
	return true;
}

void animated_variable_set_span(struct sway_animated_variable *av, double span) {
	if (av->span == span) {
		return;
	}
	av->span = span;
	animated_variable_apply(av);
}

double animated_variable_get_offset(struct sway_animated_variable *av) {
	return av->cy * fabs(av->x1 - av->x0);
}

size_t animated_variables_count(void) {
	return animation ? wl_list_length(&animation->variables) : 0;
}

size_t animated_variables_update(void) {
	if (!animation) {
		return 0;
	}
	clock_gettime(CLOCK_MONOTONIC, &animation->frame_time);
	struct sway_animated_variable *av, *tmp;
	wl_list_for_each_safe(av, tmp, &animation->variables, link) {
		if (!animated_variable_advance(av, &animation->frame_time)) {
			animated_variable_finish(av);
		}
	}
	return wl_list_length(&animation->variables);
}

void animated_variables_finish(void) {
	if (!animation) {
		return;
	}
	struct sway_animated_variable *av, *tmp;
	wl_list_for_each_safe(av, tmp, &animation->variables, link) {
		animated_variable_finish(av);
	}
}

static void animated_variables_clear(void) {
	if (!animation) {
		return;
	}
	struct sway_animated_variable *av, *tmp;
	wl_list_for_each_safe(av, tmp, &animation->variables, link) {
		av->animating = false;
		wl_list_init(&av->link);
	}
	wl_list_init(&animation->variables);
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
