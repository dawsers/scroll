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

	return 0;
}
