#include <stdio.h>
#include <time.h>
#include <wlr/types/wlr_scene.h>

struct tree_spec {
	// Parameters for the tree we'll construct
	int depth;
	int branching;
	int rect_size;
	int spread;

	// Stats around the tree we built
	int tree_count;
	int rect_count;
	int max_x;
	int max_y;
};

static int max(int a, int b) {
	return a > b ? a : b;
}

static double timespec_diff_msec(struct timespec *start, struct timespec *end) {
	return (double)(end->tv_sec - start->tv_sec) * 1e3 +
		(double)(end->tv_nsec - start->tv_nsec) / 1e6;
}

static bool build_tree(struct wlr_scene_tree *parent, struct tree_spec *spec,
		int depth, int x, int y) {

	if (depth == spec->depth) {
		float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		struct wlr_scene_rect *rect =
			wlr_scene_rect_create(parent, spec->rect_size, spec->rect_size, color);
		if (rect == NULL) {
			fprintf(stderr, "wlr_scene_rect_create failed\n");
			return false;
		}
		wlr_scene_node_set_position(&rect->node, x, y);
		spec->max_x = max(spec->max_x, x + spec->rect_size);
		spec->max_y = max(spec->max_y, y + spec->rect_size);
		spec->rect_count++;
		return true;
	}

	for (int i = 0; i < spec->branching; i++) {
		struct wlr_scene_tree *child = wlr_scene_tree_create(parent);
		if (child == NULL) {
			fprintf(stderr, "wlr_scene_tree_create failed\n");
			return false;
		}
		spec->tree_count++;
		int offset = i * spec->spread;
		wlr_scene_node_set_position(&child->node, offset, offset);
		if (!build_tree(child, spec, depth + 1, x + offset, y + offset)) {
			return false;
		}
	}
	return true;
}

static bool bench_create_tree(struct wlr_scene *scene, struct tree_spec *spec) {
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);
	if (!build_tree(&scene->tree, spec, 0, 0, 0)) {
		fprintf(stderr, "build_tree failed\n");
		return false;
	}
	clock_gettime(CLOCK_MONOTONIC, &end);

	printf("Built tree with %d tree nodes, %d rect nodes\n\n",
		spec->tree_count, spec->rect_count);

	double elapsed = timespec_diff_msec(&start, &end);
	int nodes = spec->tree_count + spec->rect_count;
	printf("create test tree:               %d nodes, %.3f ms, %.0f nodes/ms\n",
		nodes, elapsed, nodes / elapsed);
	return true;
}

static void bench_scene_node_at(struct wlr_scene *scene, struct tree_spec *spec) {
	struct timespec start, end;
	int iters = 10000;
	int hits = 0;

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (int i = 0; i < iters; i++) {
		// Spread lookups across the tree extent
		double lx = (double)(i * 97 % spec->max_x);
		double ly = (double)(i * 53 % spec->max_y);
		double nx, ny;
		struct wlr_scene_node *node =
			wlr_scene_node_at(&scene->tree.node, lx, ly, &nx, &ny);
		if (node != NULL) {
			hits++;
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &end);

	double elapsed = timespec_diff_msec(&start, &end);
	int nodes = (spec->tree_count + spec->rect_count) * iters;
	printf("wlr_scene_node_at:              %d iters, %.3f ms, %.0f nodes/ms (hits: %d/%d)\n",
		iters, elapsed, nodes / elapsed, hits, iters);
}

static void noop_iterator(struct wlr_scene_buffer *buffer,
		int sx, int sy, void *user_data) {
	(void)buffer;
	(void)sx;
	(void)sy;
	int *cnt = user_data;
	(*cnt)++;
}

static void bench_scene_node_for_each_buffer(struct wlr_scene *scene, struct tree_spec *spec) {
	struct timespec start, end;
	int iters = 10000;
	int hits = 0;

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (int i = 0; i < iters; i++) {
		wlr_scene_node_for_each_buffer(&scene->tree.node,
			noop_iterator, &hits);
	}
	clock_gettime(CLOCK_MONOTONIC, &end);

	double elapsed = timespec_diff_msec(&start, &end);
	int nodes = (spec->tree_count + spec->rect_count) * iters;
	printf("wlr_scene_node_for_each_buffer: %d iters, %.3f ms, %.0f nodes/ms (hits: %d/%d)\n",
		iters, elapsed, nodes / elapsed, hits, iters);
}

int main(void) {
	struct wlr_scene *scene = wlr_scene_create();
	if (scene == NULL) {
		fprintf(stderr, "wlr_scene_create failed\n");
		return 99;
	}

	struct tree_spec spec = {
		.depth = 5,
		.branching = 5,
		.rect_size = 10,
		.spread = 100,
	};
	if (!bench_create_tree(scene, &spec)) {
		return 99;
	}
	bench_scene_node_at(scene, &spec);
	bench_scene_node_for_each_buffer(scene, &spec);

	wlr_scene_node_destroy(&scene->tree.node);
	return 0;
}
