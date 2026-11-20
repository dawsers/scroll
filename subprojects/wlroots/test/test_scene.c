#include <wlr/types/wlr_scene.h>
#include <types/wlr_scene.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

static bool check_extents(struct wlr_scene_node *node,
		int x_min, int y_min, int x_max, int y_max) {
	struct wlr_box extents;
	if (!scene_node_get_extents(node, &extents)) {
		fprintf(stderr, "Expected non-empty extents\n");
		return false;
	}

	int ext_x_max = extents.x + extents.width;
	int ext_y_max = extents.y + extents.height;

	if (extents.x != x_min || extents.y != y_min ||
			ext_x_max != x_max || ext_y_max != y_max) {
		fprintf(stderr, "Expected extents (%d, %d); (%d, %d), but got (%d, %d); (%d, %d)\n",
				x_min, y_min, x_max, y_max,
				extents.x, extents.y, ext_x_max, ext_y_max);
		return false;
	}

	return true;
}

static void test_scene_node_extents(struct wlr_scene_node *(*create_node)(
			struct wlr_scene_tree *parent, int width, int height)) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	// An empty tree should have no extents
	struct wlr_box extents;
	assert(!scene_node_get_extents(&scene->tree.node, &extents));

	// Adding a node should update extents
	struct wlr_scene_node *node = create_node(&scene->tree, 5, 7);
	assert(check_extents(&scene->tree.node, 0, 0, 5, 7));

	// Changing the node's position should update extents
	wlr_scene_node_set_position(node, 1, 2);
	assert(check_extents(&scene->tree.node, 1, 2, 6, 9));

	// Adding another node should combine the extents
	struct wlr_scene_node *second_node = create_node(&scene->tree, 11, 23);
	assert(check_extents(&scene->tree.node, 0, 0, 11, 23));

	// Moving the second node so that it doesn't overlap should grow the extents
	wlr_scene_node_set_position(second_node, 10, 10);
	assert(check_extents(&scene->tree.node, 1, 2, 21, 33));

	// Removing the second node should restore the previous extents
	wlr_scene_node_destroy(second_node);
	assert(check_extents(&scene->tree.node, 1, 2, 6, 9));

	// Disabling the first node should clear the extents
	wlr_scene_node_set_enabled(node, false);
	assert(!scene_node_get_extents(&scene->tree.node, &extents));

	// Enabling the first node again should restore the extents
	wlr_scene_node_set_enabled(node, true);
	assert(check_extents(&scene->tree.node, 1, 2, 6, 9));

	// Removing the first node should clear the extents
	wlr_scene_node_destroy(node);
	assert(!scene_node_get_extents(&scene->tree.node, &extents));

	wlr_scene_node_destroy(&scene->tree.node);
}

static struct wlr_scene_node *create_scene_rect(
		struct wlr_scene_tree *parent, int width, int height) {
	return &wlr_scene_rect_create(parent, width, height, (float[4]){0})->node;
}

static struct wlr_scene_node *create_scene_tree_with_rect(
		struct wlr_scene_tree *parent, int width, int height) {
	struct wlr_scene_tree *tree = wlr_scene_tree_create(parent);
	wlr_scene_rect_create(tree, width, height, (float[4]){0});
	return &tree->node;
}

static struct wlr_scene_node *create_deep_scene_tree_with_rect(
		struct wlr_scene_tree *parent, int width, int height) {
	struct wlr_scene_tree *tree1 = wlr_scene_tree_create(parent);
	struct wlr_scene_tree *tree2 = wlr_scene_tree_create(tree1);
	wlr_scene_rect_create(tree2, width, height, (float[4]){0});
	return &tree1->node;
}

static void test_scene_rect_extents(void) {
	test_scene_node_extents(create_scene_rect);
}

static void test_scene_tree_extents(void) {
	test_scene_node_extents(create_scene_tree_with_rect);
}

static void test_deep_scene_tree_extents(void) {
	test_scene_node_extents(create_deep_scene_tree_with_rect);
}

static void test_scene_extents_negative_coords(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	create_scene_rect(&scene->tree, 10, 10);

	struct wlr_scene_node *left_top = create_scene_rect(&scene->tree, 10, 10);
	wlr_scene_node_set_position(left_top, -5, -8);

	struct wlr_scene_node *right_bottom = create_scene_rect(&scene->tree, 10, 10);
	wlr_scene_node_set_position(right_bottom, 20, 25);

	assert(check_extents(&scene->tree.node, -5, -8, 30, 35));

	wlr_scene_node_destroy(&scene->tree.node);
}

// Resizing a rect should update the extents.
static void test_scene_extents_rect_set_size(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	struct wlr_scene_rect *rect = wlr_scene_rect_create(&scene->tree, 5, 5, (float[4]){0});
	assert(check_extents(&scene->tree.node, 0, 0, 5, 5));

	wlr_scene_rect_set_size(rect, 20, 30);
	assert(check_extents(&scene->tree.node, 0, 0, 20, 30));

	wlr_scene_rect_set_size(rect, 2, 3);
	assert(check_extents(&scene->tree.node, 0, 0, 2, 3));

	wlr_scene_node_destroy(&scene->tree.node);
}

// Reparenting a node should update the extents of both the old and the new
// parent
static void test_scene_extents_reparent(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	struct wlr_scene_tree *tree_a = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_set_position(&tree_a->node, 100, 100);

	struct wlr_scene_tree *tree_b = wlr_scene_tree_create(&scene->tree);

	struct wlr_scene_node *rect = create_scene_rect(tree_a, 10, 10);
	assert(check_extents(&scene->tree.node, 100, 100, 110, 110));
	assert(check_extents(&tree_a->node, 100, 100, 110, 110));

	wlr_scene_node_reparent(rect, tree_b);
	assert(check_extents(&scene->tree.node, 0, 0, 10, 10));
	assert(check_extents(&tree_b->node, 0, 0, 10, 10));
	assert(!scene_node_get_extents(&tree_a->node, &(struct wlr_box){0}));

	wlr_scene_node_destroy(&scene->tree.node);
}

// Extents are reported in layout coordinates
static void test_scene_extents_nested_offset(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	struct wlr_scene_tree *tree = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_set_position(&tree->node, 10, 20);

	struct wlr_scene_node *rect = create_scene_rect(tree, 5, 5);
	wlr_scene_node_set_position(rect, 3, 4);

	assert(check_extents(&scene->tree.node, 13, 24, 18, 29));
	assert(check_extents(&tree->node, 13, 24, 18, 29));

	// Moving the intermediate tree shifts the layout-space extents.
	wlr_scene_node_set_position(&tree->node, 0, 0);
	assert(check_extents(&scene->tree.node, 3, 4, 8, 9));

	wlr_scene_node_destroy(&scene->tree.node);
}

// Toggling the enabled state of an intermediate tree updates the ancestors.
static void test_scene_extents_toggle_nested(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	struct wlr_scene_tree *outer = wlr_scene_tree_create(&scene->tree);
	struct wlr_scene_tree *inner = wlr_scene_tree_create(outer);
	create_scene_rect(&scene->tree, 3, 3);
	create_scene_rect(inner, 50, 60);

	assert(check_extents(&scene->tree.node, 0, 0, 50, 60));

	wlr_scene_node_set_enabled(&inner->node, false);
	assert(check_extents(&scene->tree.node, 0, 0, 3, 3));
	assert(!scene_node_get_extents(&outer->node, &(struct wlr_box){0}));

	wlr_scene_node_set_enabled(&inner->node, true);
	assert(check_extents(&scene->tree.node, 0, 0, 50, 60));

	wlr_scene_node_destroy(&scene->tree.node);
}

// scroll: node positions and sizes are fractional; extents are rounded
// outwards to integer layout coordinates.
static void test_scene_extents_fractional(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	struct wlr_scene_rect *rect =
		wlr_scene_rect_create(&scene->tree, 2.2, 3.6, (float[4]){0});
	wlr_scene_node_set_position(&rect->node, 0.5, -0.5);
	assert(check_extents(&scene->tree.node, 0, -1, 3, 4));

	// Two fractional children must be combined without accumulating rounding
	struct wlr_scene_rect *rect2 =
		wlr_scene_rect_create(&scene->tree, 0.4, 0.4, (float[4]){0});
	wlr_scene_node_set_position(&rect2->node, 2.8, 3.2);
	assert(check_extents(&scene->tree.node, 0, -1, 4, 4));

	wlr_scene_node_destroy(&scene->tree.node);
}

#define MAX_VISITS 8

struct visit {
	struct wlr_scene_node *node;
	double sx, sy;
};

struct visit_data {
	struct visit visits[MAX_VISITS];
	size_t len;
	// If non-NULL, the iterator stops once this node has been visited
	struct wlr_scene_node *stop_at;
};

static bool collect_iterator(struct wlr_scene_node *node, double sx, double sy, void *data) {
	struct visit_data *visit_data = data;

	if (visit_data->len < MAX_VISITS) {
		visit_data->visits[visit_data->len] = (struct visit){
			.node = node,
			.sx = sx,
			.sy = sy,
		};
	}
	visit_data->len++;

	return visit_data->stop_at == node;
}

static bool check_visit(struct visit_data *visit_data, size_t index,
		struct wlr_scene_node *node, double sx, double sy) {
	if (index >= visit_data->len || index >= MAX_VISITS) {
		fprintf(stderr, "Expected at least %zu visits, but got %zu\n",
			index + 1, visit_data->len);
		return false;
	}

	struct visit *visit = &visit_data->visits[index];
	if (visit->node != node || visit->sx != sx || visit->sy != sy) {
		fprintf(stderr, "Expected visit %zu to be node %p at (%f, %f), "
			"but got node %p at (%f, %f)\n", index, (void *)node, sx, sy,
			(void *)visit->node, visit->sx, visit->sy);
		return false;
	}

	return true;
}

// Only nodes intersecting the box are visited, in layout coordinates
static void test_nodes_in_box_intersection(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	struct wlr_scene_node *inside = create_scene_rect(&scene->tree, 10, 10);
	struct wlr_scene_node *outside = create_scene_rect(&scene->tree, 10, 10);
	wlr_scene_node_set_position(outside, 100, 100);

	struct visit_data visit_data = {0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 0, 0, 10, 10 }, collect_iterator, &visit_data));
	assert(visit_data.len == 1);
	assert(check_visit(&visit_data, 0, inside, 0, 0));

	// A box touching the right edge of a node doesn't intersect it
	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 10, 0, 10, 10 }, collect_iterator, &visit_data));
	assert(visit_data.len == 0);

	// A single pixel overlap is enough
	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 9, 9, 1, 1 }, collect_iterator, &visit_data));
	assert(visit_data.len == 1);
	assert(check_visit(&visit_data, 0, inside, 0, 0));

	// An empty box never intersects anything
	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 0, 0, 0, 0 }, collect_iterator, &visit_data));
	assert(visit_data.len == 0);

	// A box large enough covers both nodes
	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 0, 0, 200, 200 }, collect_iterator, &visit_data));
	assert(visit_data.len == 2);

	wlr_scene_node_destroy(&scene->tree.node);
}

// Zero-sized nodes are never visited
static void test_nodes_in_box_empty_node(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	create_scene_rect(&scene->tree, 0, 0);

	struct visit_data visit_data = {0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 0, 0, 10, 10 }, collect_iterator, &visit_data));
	assert(visit_data.len == 0);

	wlr_scene_node_destroy(&scene->tree.node);
}

// Buffer nodes are visited, using their destination size
static void test_nodes_in_box_buffer(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	struct wlr_scene_buffer *buffer = wlr_scene_buffer_create(&scene->tree, NULL);
	assert(buffer);

	// A buffer without a buffer nor a destination size has no size
	struct visit_data visit_data = {0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 0, 0, 100, 100 }, collect_iterator, &visit_data));
	assert(visit_data.len == 0);

	wlr_scene_buffer_set_dest_size(buffer, 10, 10);
	wlr_scene_node_set_position(&buffer->node, 5, 5);

	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 0, 0, 100, 100 }, collect_iterator, &visit_data));
	assert(visit_data.len == 1);
	assert(check_visit(&visit_data, 0, &buffer->node, 5, 5));

	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 0, 0, 5, 5 }, collect_iterator, &visit_data));
	assert(visit_data.len == 0);

	wlr_scene_node_destroy(&scene->tree.node);
}

// Nodes are visited from top to bottom
static void test_nodes_in_box_order(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	struct wlr_scene_node *bottom = create_scene_rect(&scene->tree, 10, 10);
	struct wlr_scene_node *middle = create_scene_rect(&scene->tree, 10, 10);
	struct wlr_scene_node *top = create_scene_rect(&scene->tree, 10, 10);

	struct visit_data visit_data = {0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 0, 0, 10, 10 }, collect_iterator, &visit_data));
	assert(visit_data.len == 3);
	assert(check_visit(&visit_data, 0, top, 0, 0));
	assert(check_visit(&visit_data, 1, middle, 0, 0));
	assert(check_visit(&visit_data, 2, bottom, 0, 0));

	// Restacking changes the order
	wlr_scene_node_raise_to_top(bottom);

	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 0, 0, 10, 10 }, collect_iterator, &visit_data));
	assert(visit_data.len == 3);
	assert(check_visit(&visit_data, 0, bottom, 0, 0));
	assert(check_visit(&visit_data, 1, top, 0, 0));
	assert(check_visit(&visit_data, 2, middle, 0, 0));

	wlr_scene_node_destroy(&scene->tree.node);
}

// Tree nodes are descended into, but never passed to the iterator themselves
static void test_nodes_in_box_nested(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	struct wlr_scene_tree *outer = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_set_position(&outer->node, 10, 20);

	struct wlr_scene_tree *inner = wlr_scene_tree_create(outer);
	wlr_scene_node_set_position(&inner->node, 3, 4);

	struct wlr_scene_node *rect = create_scene_rect(inner, 5, 5);
	wlr_scene_node_set_position(rect, 1, 1);

	struct visit_data visit_data = {0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 0, 0, 100, 100 }, collect_iterator, &visit_data));
	assert(visit_data.len == 1);
	assert(check_visit(&visit_data, 0, rect, 14, 25));

	// The box is in layout coordinates, not relative to the node
	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 0, 0, 14, 25 }, collect_iterator, &visit_data));
	assert(visit_data.len == 0);

	// Starting from a nested tree offsets the coordinates the same way
	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(&inner->node,
		&(struct wlr_box){ 0, 0, 100, 100 }, collect_iterator, &visit_data));
	assert(visit_data.len == 1);
	assert(check_visit(&visit_data, 0, rect, 14, 25));

	wlr_scene_node_destroy(&scene->tree.node);
}

// Only the nodes below the starting node are visited
static void test_nodes_in_box_subtree(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	create_scene_rect(&scene->tree, 10, 10);

	struct wlr_scene_tree *tree = wlr_scene_tree_create(&scene->tree);
	struct wlr_scene_node *child = create_scene_rect(tree, 10, 10);

	struct visit_data visit_data = {0};
	assert(!scene_nodes_in_box(&tree->node,
		&(struct wlr_box){ 0, 0, 10, 10 }, collect_iterator, &visit_data));
	assert(visit_data.len == 1);
	assert(check_visit(&visit_data, 0, child, 0, 0));

	// A leaf node can be used as the starting node too
	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(child,
		&(struct wlr_box){ 0, 0, 10, 10 }, collect_iterator, &visit_data));
	assert(visit_data.len == 1);
	assert(check_visit(&visit_data, 0, child, 0, 0));

	wlr_scene_node_destroy(&scene->tree.node);
}

// Disabled nodes and the children of disabled trees are skipped
static void test_nodes_in_box_disabled(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	struct wlr_scene_node *rect = create_scene_rect(&scene->tree, 10, 10);

	struct wlr_scene_tree *tree = wlr_scene_tree_create(&scene->tree);
	struct wlr_scene_node *child = create_scene_rect(tree, 10, 10);

	struct wlr_box box = { 0, 0, 10, 10 };

	wlr_scene_node_set_enabled(rect, false);

	struct visit_data visit_data = {0};
	assert(!scene_nodes_in_box(&scene->tree.node, &box, collect_iterator, &visit_data));
	assert(visit_data.len == 1);
	assert(check_visit(&visit_data, 0, child, 0, 0));

	// Disabling the tree hides its children
	wlr_scene_node_set_enabled(&tree->node, false);

	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(&scene->tree.node, &box, collect_iterator, &visit_data));
	assert(visit_data.len == 0);

	// The starting node itself is checked as well
	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(&tree->node, &box, collect_iterator, &visit_data));
	assert(visit_data.len == 0);

	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(rect, &box, collect_iterator, &visit_data));
	assert(visit_data.len == 0);

	wlr_scene_node_set_enabled(&tree->node, true);

	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(&scene->tree.node, &box, collect_iterator, &visit_data));
	assert(visit_data.len == 1);
	assert(check_visit(&visit_data, 0, child, 0, 0));

	wlr_scene_node_destroy(&scene->tree.node);
}

// An iterator returning true stops the iteration
static void test_nodes_in_box_early_exit(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	struct wlr_scene_tree *tree = wlr_scene_tree_create(&scene->tree);
	struct wlr_scene_node *bottom = create_scene_rect(tree, 10, 10);
	struct wlr_scene_node *middle = create_scene_rect(tree, 10, 10);
	struct wlr_scene_node *top = create_scene_rect(tree, 10, 10);

	struct wlr_box box = { 0, 0, 10, 10 };

	// Stopping at the topmost node visits nothing else
	struct visit_data visit_data = { .stop_at = top };
	assert(scene_nodes_in_box(&scene->tree.node, &box, collect_iterator, &visit_data));
	assert(visit_data.len == 1);
	assert(check_visit(&visit_data, 0, top, 0, 0));

	// Stopping in the middle unwinds through the parent trees
	visit_data = (struct visit_data){ .stop_at = middle };
	assert(scene_nodes_in_box(&scene->tree.node, &box, collect_iterator, &visit_data));
	assert(visit_data.len == 2);
	assert(check_visit(&visit_data, 0, top, 0, 0));
	assert(check_visit(&visit_data, 1, middle, 0, 0));

	// Stopping at the last node still reports that the iteration was stopped
	visit_data = (struct visit_data){ .stop_at = bottom };
	assert(scene_nodes_in_box(&scene->tree.node, &box, collect_iterator, &visit_data));
	assert(visit_data.len == 3);

	wlr_scene_node_destroy(&scene->tree.node);
}

// scroll: with fractional positions, pruning a tree by its bounding box must
// never skip a leaf whose (rounded) box intersects the query box.
static void test_nodes_in_box_fractional(void) {
	struct wlr_scene *scene = wlr_scene_create();
	assert(scene);

	struct wlr_scene_tree *tree = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_set_position(&tree->node, 0.5, 0.5);
	struct wlr_scene_node *rect = create_scene_rect(tree, 10, 10);

	// The leaf's rounded box is (1, 1, 10, 10), so the last pixel column/row
	// is at 10, past a truncated tree box of (0, 0, 10, 10).
	struct visit_data visit_data = {0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 10, 10, 1, 1 }, collect_iterator, &visit_data));
	assert(visit_data.len == 1);
	assert(check_visit(&visit_data, 0, rect, 0.5, 0.5));

	// Outside of the rounded leaf box, nothing is visited
	visit_data = (struct visit_data){0};
	assert(!scene_nodes_in_box(&scene->tree.node,
		&(struct wlr_box){ 11, 11, 1, 1 }, collect_iterator, &visit_data));
	assert(visit_data.len == 0);

	wlr_scene_node_destroy(&scene->tree.node);
}

int main(void) {
#ifdef NDEBUG
	fprintf(stderr, "NDEBUG must be disabled for tests\n");
	return 1;
#endif

	test_scene_rect_extents();
	test_scene_tree_extents();
	test_deep_scene_tree_extents();
	test_scene_extents_negative_coords();
	test_scene_extents_rect_set_size();
	test_scene_extents_reparent();
	test_scene_extents_nested_offset();
	test_scene_extents_toggle_nested();
	test_scene_extents_fractional();
	test_nodes_in_box_intersection();
	test_nodes_in_box_empty_node();
	test_nodes_in_box_buffer();
	test_nodes_in_box_order();
	test_nodes_in_box_nested();
	test_nodes_in_box_subtree();
	test_nodes_in_box_disabled();
	test_nodes_in_box_early_exit();
	test_nodes_in_box_fractional();

	return 0;
}
