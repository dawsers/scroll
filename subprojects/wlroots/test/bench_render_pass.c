#include <assert.h>
#include <drm_fourcc.h>
#include <math.h>
#include <pixman.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/color.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/render/pass.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/util/log.h>

// Switch to e.g., XRGB2101010 for the Vulkan two-pass path
#define OUTPUT_FORMAT  DRM_FORMAT_XRGB8888
#define TARGET_NS      100000000
#define OUTPUT_WIDTH   1920
#define OUTPUT_HEIGHT  1080
#define TEXTURE_SIZE   500
#define STACKED_SIZE   500
#define CLIP_MANY_ROWS 200
#define MAX_ITER       10000
#define MIN_ITER       10
#define WARMUP_ITER    2

enum primitive_type {
	RECT,
	TEXTURE,
};

enum layout_type {
	STACKED,
	GRID,
};

struct bench_case {
	enum primitive_type primitive;
	enum layout_type layout;
	int clips;
	int count;
};

struct bench_result {
	int iters;
	int64_t cpu_ns;
	int64_t gpu_ns;
};

struct bench_ctx {
	struct wl_event_loop *ev;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_swapchain *swapchain;
	struct wlr_drm_syncobj_timeline *timeline;
	struct wlr_color_transform *color_transform;
	struct wlr_render_timer *timer;
	struct wlr_texture *texture;
	uint64_t signal_point;
};

struct render_wait {
	struct wlr_drm_syncobj_timeline_waiter waiter;
	bool ready;
};

static void handle_render_ready(struct wlr_drm_syncobj_timeline_waiter *waiter) {
	struct render_wait *wait = wl_container_of(waiter, wait, waiter);
	wait->ready = true;
}

static int64_t timespec_to_ns(const struct timespec *ts) {
	return (int64_t)ts->tv_sec * 1000000000L + ts->tv_nsec;
}

static int64_t timespec_diff_ns(const struct timespec *start,
		const struct timespec *end) {
	return timespec_to_ns(end) - timespec_to_ns(start);
}

static void bench_ctx_init(struct bench_ctx *ctx) {
	ctx->ev = wl_event_loop_create();
	assert(ctx->ev);

	wlr_log_init(WLR_ERROR, NULL);

	ctx->backend = wlr_headless_backend_create(ctx->ev);
	assert(ctx->backend);

	ctx->renderer = wlr_renderer_autocreate(ctx->backend);
	assert(ctx->renderer);

	if (ctx->renderer->features.timeline) {
		int drm_fd = wlr_renderer_get_drm_fd(ctx->renderer);
		assert(drm_fd >= 0);

		ctx->timeline = wlr_drm_syncobj_timeline_create(drm_fd);
		assert(ctx->timeline);
	}

	ctx->color_transform = wlr_color_transform_init_linear_to_inverse_eotf(
		WLR_COLOR_TRANSFER_FUNCTION_SRGB);
	assert(ctx->color_transform);

	ctx->allocator = wlr_allocator_autocreate(ctx->backend, ctx->renderer);
	assert(ctx->allocator);

	const struct wlr_drm_format_set *formats =
		wlr_renderer_get_texture_formats(ctx->renderer,
			ctx->allocator->buffer_caps);

	ctx->swapchain = wlr_swapchain_create(ctx->allocator,
		OUTPUT_WIDTH, OUTPUT_HEIGHT,
		wlr_drm_format_set_get(formats, OUTPUT_FORMAT));
	assert(ctx->swapchain);

	ctx->timer = wlr_render_timer_create(ctx->renderer);

	size_t stride = TEXTURE_SIZE * 4;
	size_t size = stride * TEXTURE_SIZE;
	uint8_t *data = malloc(size);
	assert(data);
	for (size_t i = 0; i < size; i++) {
		data[i] = i & 0xFF;
	}
	ctx->texture = wlr_texture_from_pixels(ctx->renderer,
		DRM_FORMAT_ARGB8888, stride, TEXTURE_SIZE, TEXTURE_SIZE, data);
	assert(ctx->texture);
	free(data);
}

static void bench_ctx_finish(struct bench_ctx *ctx) {
	wlr_texture_destroy(ctx->texture);
	wlr_render_timer_destroy(ctx->timer);
	wlr_swapchain_destroy(ctx->swapchain);
	wlr_allocator_destroy(ctx->allocator);
	wlr_color_transform_unref(ctx->color_transform);
	wlr_drm_syncobj_timeline_unref(ctx->timeline);
	wlr_renderer_destroy(ctx->renderer);
	wlr_backend_destroy(ctx->backend);
	wl_event_loop_destroy(ctx->ev);
}

static void run_one(struct bench_ctx *ctx, const struct bench_case *bc,
		const pixman_region32_t *clip, int64_t *out_cpu_ns,
		int64_t *out_gpu_ns) {
	struct wlr_buffer *buffer = wlr_swapchain_acquire(ctx->swapchain);
	assert(buffer);

	uint64_t point = ctx->signal_point++;

	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);

	struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(
			ctx->renderer, buffer, &(struct wlr_buffer_pass_options){
		.timer = ctx->timer,
		.color_transform = ctx->color_transform,
		.signal_timeline = ctx->timeline,
		.signal_point = point,
	});
	assert(pass);

	for (int i = 0; i < bc->count; i++) {
		struct wlr_box box;
		if (bc->layout == STACKED) {
			box = (struct wlr_box){
				.x = 0, .y = 0,
				.width = STACKED_SIZE,
				.height = STACKED_SIZE,
			};
		} else {
			int cols = ceil(sqrt(bc->count));
			int rows = (bc->count + cols - 1) / cols;
			int tile_w = OUTPUT_WIDTH / cols;
			int tile_h = OUTPUT_HEIGHT / rows;
			box = (struct wlr_box){
				.x = (i % cols) * tile_w,
				.y = (i / cols) * tile_h,
				.width = tile_w,
				.height = tile_h,
			};
		}

		if (bc->primitive == RECT) {
			wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
				.box = box,
				.color = { .r = 0.5, .g = 0.25, .b = 0.05, .a = 0.5 },
				.clip = clip,
			});
		} else {
			wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
				.texture = ctx->texture,
				.dst_box = box,
				.clip = clip,
			});
		}
	}

	bool ok = wlr_render_pass_submit(pass);
	assert(ok);

	clock_gettime(CLOCK_MONOTONIC, &end);
	*out_cpu_ns = timespec_diff_ns(&start, &end);

	if (ctx->renderer->features.timeline) {
		struct render_wait wait = { 0 };
		assert(wlr_drm_syncobj_timeline_waiter_init(&wait.waiter, ctx->timeline,
				point, 0, ctx->ev, handle_render_ready));
		while (!wait.ready) {
			int ret = wl_event_loop_dispatch(ctx->ev, -1);
			assert(ret >= 0);
		}
		wlr_drm_syncobj_timeline_waiter_finish(&wait.waiter);
	}

	wlr_buffer_unlock(buffer);

	if (ctx->timer) {
		*out_gpu_ns = wlr_render_timer_get_duration_ns(ctx->timer);
	}
}

static int64_t run(struct bench_ctx *ctx, const struct bench_case *bc,
		const pixman_region32_t *clip, int64_t *out_cpu_ns,
		int64_t *out_gpu_ns, int64_t iters) {
	struct timespec wall_start, wall_end;
	clock_gettime(CLOCK_MONOTONIC, &wall_start);

	for (int64_t i = 0; i < iters; i++) {
		int64_t cpu = 0, gpu = 0;
		run_one(ctx, bc, clip, &cpu, &gpu);
		*out_cpu_ns += cpu;
		*out_gpu_ns += gpu;
	}

	clock_gettime(CLOCK_MONOTONIC, &wall_end);
	return timespec_diff_ns(&wall_start, &wall_end);
}

static struct bench_result run_benchmark(struct bench_ctx *ctx,
		const struct bench_case *bc) {
	pixman_region32_t clip;

	if (bc->clips == 1) {
		pixman_region32_init_rect(&clip, 0, 0, OUTPUT_WIDTH, OUTPUT_HEIGHT);
	} else {
		// Varying width ensures that pixman does not merge adjacent rows.
		pixman_region32_init(&clip);
		for (int row = 0; row < bc->clips; row++) {
			pixman_region32_union_rect(&clip, &clip,
				0, row, row + 1, 1);
		}
	}

	int64_t iters = WARMUP_ITER, discard;
	int64_t wall_ns = run(ctx, bc, &clip, &discard, &discard, iters);

	struct bench_result result = {0};
	for (;;) {
		// To avoid being slightly below target we aim for 10% over
		assert(wall_ns > 0);
		iters = iters * TARGET_NS * 1.1 / wall_ns + 1;
		if (iters < MIN_ITER) {
			iters = MIN_ITER;
		}

		int64_t total_cpu = 0;
		int64_t total_gpu = 0;

		wall_ns = run(ctx, bc, &clip, &total_cpu, &total_gpu, iters);
		if (wall_ns >= TARGET_NS || iters >= MAX_ITER) {
			// The test either ran long enough or we're giving up
			result.iters = iters;
			result.cpu_ns = total_cpu;
			result.gpu_ns = total_gpu;
			break;
		}
	}

	pixman_region32_fini(&clip);
	return result;
}

// print_result outputs a benchmark measurement in the Go Benchmark Data Format
// used by the `go test -bench`, which can be digested by tools like benchstat.
//
// See: https://go.googlesource.com/proposal/+/master/design/14313-benchmark-format.md
static void print_result(const struct bench_case *bc,
		const struct bench_result *r) {
	int64_t cpu_per_op = r->cpu_ns / r->iters;
	int64_t gpu_per_op = r->gpu_ns / r->iters;
	const char *primitive_name = bc->primitive == RECT ? "Rect" : "Texture";
	const char *layout_name = bc->layout == STACKED ? "stacked" : "grid";

	char name[64];
	snprintf(name, sizeof(name), "Benchmark%s/%s/clip%d/%d",
		primitive_name, layout_name, bc->clips, bc->count);

	printf("%-40s %8d %12lld cpu-ns/op",
		name, r->iters, (long long)cpu_per_op);
	if (r->gpu_ns > 0) {
		printf(" %12lld gpu-ns/op", (long long)gpu_per_op);
	}
	printf("\n");
	fflush(stdout);
}

int main(int argc, char *argv[]) {
	int reruns = 1;

	int opt;
	while ((opt = getopt(argc, argv, "c:")) != -1) {
		switch (opt) {
		case 'c':
			reruns = atoi(optarg);
			if (reruns <= 0) {
				fprintf(stderr, "count must be positive\n");
				return 1;
			}
			break;
		default:
			fprintf(stderr, "Usage: %s [-c N]\n", argv[0]);
			return 1;
		}
	}

	if (optind != argc) {
		fprintf(stderr, "Usage: %s [-c N]\n", argv[0]);
		return 1;
	}

	struct bench_ctx ctx = {0};
	bench_ctx_init(&ctx);

	static const int primitives[] = { RECT, TEXTURE, -1 };
	static const int layouts[] = { STACKED, GRID, -1 };
	static const int clips[] = { 1, 200, -1 };
	static const int counts[] = { 1, 4, 64, 1024, -1 };

	// *art*.
	for (int pi = 0; primitives[pi] != -1; pi++) {
		for (int li = 0; layouts[li] != -1; li++) {
			for (int ci = 0; clips[ci] != -1; ci++) {
				for (int ni = 0; counts[ni] != -1; ni++) {
					for (int ri = 0; ri < reruns; ri++) {
						struct bench_case bc = {
							.primitive = primitives[pi],
							.layout = layouts[li],
							.clips = clips[ci],
							.count = counts[ni],
						};
						struct bench_result result =
							run_benchmark(&ctx, &bc);
						print_result(&bc, &result);
					}
				}
			}
		}
	}

	bench_ctx_finish(&ctx);
	return 0;
}
