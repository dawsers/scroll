// Focused model tests for the plain-data space template (issue #1, G1).
//
// These tests exercise only include/sway/tree/space_template.h and
// sway/tree/space_template.c: construction, deep copy, structural
// validation, and partial-tree-safe destruction. They do not start a
// compositor and never touch a live view/container, matching the ownership
// boundary the model is required to hold.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sway/tree/space_template.h"

static int failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

#define CHECK_VALID(template) \
	do { \
		struct sway_space_template_validation_result _r = space_template_validate(template); \
		if (_r.error != SPACE_TEMPLATE_VALID) { \
			fprintf(stderr, "FAIL %s:%d: expected valid, got %s at %s\n", \
				__FILE__, __LINE__, space_template_validation_error_str(_r.error), \
				_r.path ? _r.path : "(none)"); \
			failures++; \
		} \
		free(_r.path); \
	} while (0)

#define CHECK_ERROR(template, expected_error) \
	do { \
		struct sway_space_template_validation_result _r = space_template_validate(template); \
		if (_r.error != (expected_error)) { \
			fprintf(stderr, "FAIL %s:%d: expected %s, got %s at %s\n", \
				__FILE__, __LINE__, space_template_validation_error_str(expected_error), \
				space_template_validation_error_str(_r.error), _r.path ? _r.path : "(none)"); \
			failures++; \
		} \
		free(_r.path); \
	} while (0)

// A well-formed two-slot tiling template: an inner horizontal split whose
// two leaves are "editor" and "terminal", focused on "editor".
static struct sway_space_template *build_valid_template(void) {
	struct sway_space_template *template = space_template_create("dev");

	struct sway_space_template_node *root =
		space_template_node_create_inner(SPACE_TEMPLATE_LAYOUT_HORIZONTAL);
	space_template_node_set_tiling_geometry(root, 1.0, 1.0);

	struct sway_space_template_node *editor = space_template_node_create_leaf("editor");
	space_template_node_set_tiling_geometry(editor, 0.6, 1.0);
	space_template_node_set_hints(editor, "code", NULL, NULL);
	CHECK(space_template_node_add_child(root, editor));

	struct sway_space_template_node *terminal = space_template_node_create_leaf("terminal");
	space_template_node_set_tiling_geometry(terminal, 0.4, 1.0);
	CHECK(space_template_node_add_child(root, terminal));

	space_template_add_tiling(template, root);
	space_template_set_focused_slot(template, "editor");

	return template;
}

static void test_round_trip_and_copy(void) {
	struct sway_space_template *template = build_valid_template();
	CHECK_VALID(template);

	CHECK(template->tiling->length == 1);
	struct sway_space_template_node *root = template->tiling->items[0];
	CHECK(!space_template_node_is_leaf(root));
	CHECK(root->children->length == 2);

	struct sway_space_template_node *editor = root->children->items[0];
	CHECK(space_template_node_is_leaf(editor));
	CHECK(strcmp(editor->slot, "editor") == 0);
	CHECK(editor->hint_app_id && strcmp(editor->hint_app_id, "code") == 0);
	CHECK(editor->hint_class == NULL);

	// Deep copy must be structurally identical and independently owned.
	struct sway_space_template *copy = space_template_copy(template);
	CHECK_VALID(copy);
	CHECK(copy != template);
	CHECK(copy->tiling->items[0] != template->tiling->items[0]);
	struct sway_space_template_node *copy_root = copy->tiling->items[0];
	struct sway_space_template_node *copy_editor = copy_root->children->items[0];
	CHECK(copy_editor != editor);
	CHECK(strcmp(copy_editor->slot, "editor") == 0);
	CHECK(strcmp(copy_editor->hint_app_id, "code") == 0);
	CHECK(strcmp(copy->focused_slot, "editor") == 0);

	// Mutating the copy must not affect the original (independent ownership).
	space_template_set_focused_slot(copy, "terminal");
	CHECK(strcmp(template->focused_slot, "editor") == 0);

	space_template_destroy(template);
	space_template_destroy(copy);
}

static void test_ambiguous_and_empty_node(void) {
	// A node with both children and a slot is invalid.
	struct sway_space_template *template = space_template_create("bad");
	struct sway_space_template_node *node = space_template_node_create_inner(SPACE_TEMPLATE_LAYOUT_NONE);
	node->slot = strdup("oops"); // deliberately malformed, bypassing the leaf/inner API
	struct sway_space_template_node *child = space_template_node_create_leaf("child");
	space_template_node_set_tiling_geometry(child, 1.0, 1.0);
	space_template_node_add_child(node, child);
	space_template_add_tiling(template, node);
	CHECK_ERROR(template, SPACE_TEMPLATE_ERROR_AMBIGUOUS_NODE);
	space_template_destroy(template);

	// A node with neither children nor a slot is invalid. create_leaf(NULL)
	// produces exactly this state directly (children stays NULL, slot stays
	// NULL), without needing to hand-poke the struct.
	template = space_template_create("bad");
	struct sway_space_template_node *empty = space_template_node_create_leaf(NULL);
	space_template_add_tiling(template, empty);
	CHECK_ERROR(template, SPACE_TEMPLATE_ERROR_EMPTY_NODE);
	space_template_destroy(template);
}

static void test_empty_and_duplicate_slots(void) {
	struct sway_space_template *template = space_template_create("dup");
	struct sway_space_template_node *root = space_template_node_create_inner(SPACE_TEMPLATE_LAYOUT_HORIZONTAL);
	struct sway_space_template_node *a = space_template_node_create_leaf("editor");
	struct sway_space_template_node *b = space_template_node_create_leaf("editor");
	space_template_node_add_child(root, a);
	space_template_node_add_child(root, b);
	space_template_add_tiling(template, root);
	CHECK_ERROR(template, SPACE_TEMPLATE_ERROR_DUPLICATE_SLOT);
	space_template_destroy(template);

	template = space_template_create("empty-slot");
	struct sway_space_template_node *leaf = space_template_node_create_leaf("");
	space_template_add_tiling(template, leaf);
	CHECK_ERROR(template, SPACE_TEMPLATE_ERROR_EMPTY_SLOT);
	space_template_destroy(template);
}

static void test_inner_without_children(void) {
	struct sway_space_template *template = space_template_create("no-children");
	struct sway_space_template_node *root = space_template_node_create_inner(SPACE_TEMPLATE_LAYOUT_VERTICAL);
	space_template_add_tiling(template, root);
	CHECK_ERROR(template, SPACE_TEMPLATE_ERROR_EMPTY_CHILDREN);
	space_template_destroy(template);
}

static void test_invalid_geometry(void) {
	// Tiling: negative fraction.
	struct sway_space_template *template = space_template_create("bad-fraction");
	struct sway_space_template_node *leaf = space_template_node_create_leaf("solo");
	space_template_node_set_tiling_geometry(leaf, -0.1, 1.0);
	space_template_add_tiling(template, leaf);
	CHECK_ERROR(template, SPACE_TEMPLATE_ERROR_INVALID_GEOMETRY);
	space_template_destroy(template);

	// Floating: zero width is not a usable window size.
	template = space_template_create("bad-floating");
	leaf = space_template_node_create_leaf("solo");
	space_template_node_set_floating_geometry(leaf, 0.1, 0.1, 0.0, 0.4);
	space_template_add_floating(template, leaf);
	CHECK_ERROR(template, SPACE_TEMPLATE_ERROR_INVALID_GEOMETRY);
	space_template_destroy(template);

	// Floating: valid, positive, finite geometry passes.
	template = space_template_create("good-floating");
	leaf = space_template_node_create_leaf("solo");
	space_template_node_set_floating_geometry(leaf, 0.1, 0.1, 0.3, 0.4);
	space_template_add_floating(template, leaf);
	CHECK_VALID(template);
	space_template_destroy(template);
}

static void test_invalid_focused_slot(void) {
	struct sway_space_template *template = space_template_create("bad-focus");
	struct sway_space_template_node *leaf = space_template_node_create_leaf("solo");
	space_template_node_set_tiling_geometry(leaf, 1.0, 1.0);
	space_template_add_tiling(template, leaf);
	space_template_set_focused_slot(template, "does-not-exist");
	CHECK_ERROR(template, SPACE_TEMPLATE_ERROR_INVALID_FOCUSED_SLOT);
	space_template_destroy(template);
}

static void test_empty_name(void) {
	struct sway_space_template *template = space_template_create("");
	CHECK_ERROR(template, SPACE_TEMPLATE_ERROR_EMPTY_NAME);
	space_template_destroy(template);
}

static void test_leaf_rejects_hints_and_children(void) {
	struct sway_space_template_node *inner =
		space_template_node_create_inner(SPACE_TEMPLATE_LAYOUT_HORIZONTAL);
	CHECK(!space_template_node_set_hints(inner, "x", NULL, NULL));

	struct sway_space_template_node *leaf = space_template_node_create_leaf("solo");
	struct sway_space_template_node *other = space_template_node_create_leaf("other");
	CHECK(!space_template_node_add_child(leaf, other));
	space_template_node_destroy(other);
	space_template_node_destroy(leaf);
	space_template_node_destroy(inner);
}

static void test_partial_tree_destroy_is_safe(void) {
	// Destroy must tolerate a template/tree that was only partially built
	// (never validated, never fully wired up) without crashing or leaking.
	struct sway_space_template *template = space_template_create("partial");
	struct sway_space_template_node *root =
		space_template_node_create_inner(SPACE_TEMPLATE_LAYOUT_HORIZONTAL);
	// Intentionally add zero children before destroying.
	space_template_add_tiling(template, root);

	struct sway_space_template_node *dangling_leaf = space_template_node_create_leaf("orphan");
	// Never attached to any template/tree; destroyed directly.
	space_template_node_destroy(dangling_leaf);

	space_template_destroy(template); // frees `root` with an empty children list
	space_template_destroy(NULL); // must be a no-op
	space_template_node_destroy(NULL); // must be a no-op
}

int main(void) {
	test_round_trip_and_copy();
	test_ambiguous_and_empty_node();
	test_empty_and_duplicate_slots();
	test_inner_without_children();
	test_invalid_geometry();
	test_invalid_focused_slot();
	test_empty_name();
	test_leaf_rejects_hints_and_children();
	test_partial_tree_destroy_is_safe();

	if (failures > 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("all space_template checks passed\n");
	return 0;
}
