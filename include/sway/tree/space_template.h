#ifndef _SWAY_SPACE_TEMPLATE_H
#define _SWAY_SPACE_TEMPLATE_H

#include <stdbool.h>
#include "list.h"

// On-disk/wire schema version for space templates. Bumped by G2
// (JSON/persistence); the in-memory model itself has no version-specific
// behavior yet.
#define SPACE_TEMPLATE_VERSION 1

// Structural limits enforced by space_template_validate(). A template will
// eventually be parsed from an untrusted JSON file (G2); these bound
// worst-case recursion/allocation for that parser. They have no effect on
// live workspace trees.
#define SPACE_TEMPLATE_MAX_DEPTH 32
#define SPACE_TEMPLATE_MAX_NODES 512

enum sway_space_template_layout {
	SPACE_TEMPLATE_LAYOUT_NONE,
	SPACE_TEMPLATE_LAYOUT_HORIZONTAL,
	SPACE_TEMPLATE_LAYOUT_VERTICAL,
};

enum sway_space_template_scroller_mode {
	SPACE_TEMPLATE_SCROLLER_NONE,
	SPACE_TEMPLATE_SCROLLER_HORIZONTAL,
	SPACE_TEMPLATE_SCROLLER_VERTICAL,
};

enum sway_space_template_insert {
	SPACE_TEMPLATE_INSERT_BEFORE,
	SPACE_TEMPLATE_INSERT_AFTER,
	SPACE_TEMPLATE_INSERT_BEGINNING,
	SPACE_TEMPLATE_INSERT_END,
};

enum sway_space_template_fit {
	SPACE_TEMPLATE_FIT_NOFIT,
	SPACE_TEMPLATE_FIT_FITSPLIT,
	SPACE_TEMPLATE_FIT_FITFRACTION,
};

// Plain-data scroller/layout modifiers captured at save time. These mirror
// the meaning of the compositor's live scroller settings (see
// include/sway/tree/layout.h) but are intentionally a separate, storage-only
// value type: a template never points at a live sway_workspace/container.
struct sway_space_template_scroller {
	enum sway_space_template_scroller_mode mode;
	enum sway_space_template_insert insert;
	enum sway_space_template_fit fit;
	bool focus;
	bool center_horizontal;
	bool center_vertical;
	bool reorder;
};

// One node in a template's tiling or floating tree.
//
// Inner nodes have a non-empty `children` list and `slot == NULL`.
// Leaf nodes have `children == NULL` and a non-empty, template-unique `slot`.
// A leaf may optionally carry regex hints used later (G3/apply) to match a
// live view; hints are plain strings here, never compiled/cached.
//
// This struct owns strings, an optional child list, enums, and geometry
// only. It never holds a view/container pointer, listener, PID/con_id,
// timer, rollback, pending, or session field -- see issue #1's ownership
// boundary.
struct sway_space_template_node {
	enum sway_space_template_layout layout; // meaningful for inner nodes only
	list_t *children; // struct sway_space_template_node *, NULL for leaves

	char *slot; // non-empty, template-unique; leaves only

	// Optional view-matching hints (leaves only). Any/all may be NULL.
	char *hint_app_id;
	char *hint_class;
	char *hint_title;

	// Tiling geometry: fraction of the parent split this node occupies.
	// Meaningful for nodes reachable from sway_space_template.tiling.
	double width_fraction;
	double height_fraction;

	// Floating geometry: normalized (fraction-of-output) position/size.
	// Meaningful for nodes reachable from sway_space_template.floating.
	double x, y;
	double width, height;
};

// A named, versioned, durable workspace layout. Owns only strings, lists,
// enums, and geometry -- no view/container pointers, listeners, PID/con_id,
// timers, rollback, pending, or session state.
struct sway_space_template {
	int version;
	char *name;
	struct sway_space_template_scroller scroller;
	list_t *tiling; // struct sway_space_template_node *
	list_t *floating; // struct sway_space_template_node *
	char *focused_slot; // NULL/empty means nothing is focused
};

struct sway_space_template *space_template_create(const char *name);
void space_template_destroy(struct sway_space_template *template);
struct sway_space_template *space_template_copy(const struct sway_space_template *template);

void space_template_set_focused_slot(struct sway_space_template *template, const char *slot);

// Appends to the template's tiling/floating root list and takes ownership of
// `node`. Root entries have no leaf/inner restriction of their own; a root
// is validated like any other node by space_template_validate().
void space_template_add_tiling(struct sway_space_template *template, struct sway_space_template_node *node);
void space_template_add_floating(struct sway_space_template *template, struct sway_space_template_node *node);

struct sway_space_template_node *space_template_node_create_leaf(const char *slot);
struct sway_space_template_node *space_template_node_create_inner(enum sway_space_template_layout layout);
void space_template_node_destroy(struct sway_space_template_node *node);
struct sway_space_template_node *space_template_node_copy(const struct sway_space_template_node *node);

bool space_template_node_is_leaf(const struct sway_space_template_node *node);

// Appends `child` to `parent`'s children and takes ownership of it. Fails
// (returns false, does not take ownership) if `parent` is a leaf
// (parent->children == NULL).
bool space_template_node_add_child(struct sway_space_template_node *parent,
		struct sway_space_template_node *child);

// Sets optional view-matching hints on a leaf. Fails (returns false, changes
// nothing) if `node` is an inner node.
bool space_template_node_set_hints(struct sway_space_template_node *node,
		const char *app_id, const char *class, const char *title);

void space_template_node_set_tiling_geometry(struct sway_space_template_node *node,
		double width_fraction, double height_fraction);
void space_template_node_set_floating_geometry(struct sway_space_template_node *node,
		double x, double y, double width, double height);

enum sway_space_template_validation_error {
	SPACE_TEMPLATE_VALID = 0,
	SPACE_TEMPLATE_ERROR_EMPTY_NAME,
	SPACE_TEMPLATE_ERROR_AMBIGUOUS_NODE, // has both children and a slot
	SPACE_TEMPLATE_ERROR_EMPTY_NODE, // has neither children nor a slot
	SPACE_TEMPLATE_ERROR_EMPTY_CHILDREN, // inner node with no children
	SPACE_TEMPLATE_ERROR_EMPTY_SLOT,
	SPACE_TEMPLATE_ERROR_DUPLICATE_SLOT,
	SPACE_TEMPLATE_ERROR_INVALID_GEOMETRY,
	SPACE_TEMPLATE_ERROR_INVALID_FOCUSED_SLOT,
	SPACE_TEMPLATE_ERROR_TOO_DEEP,
	SPACE_TEMPLATE_ERROR_TOO_MANY_NODES,
};

// Result of space_template_validate(). `path` is a heap-allocated,
// dot/bracket-separated locator (e.g. "tiling[0].children[1]") pointing at
// the first offending node, in a stable depth-first tiling-then-floating
// order. `path` is NULL when error == SPACE_TEMPLATE_VALID or the error is
// not node-specific (empty name, invalid focused_slot). Caller owns `path`
// and must free() it.
struct sway_space_template_validation_result {
	enum sway_space_template_validation_error error;
	char *path;
};

const char *space_template_validation_error_str(enum sway_space_template_validation_error error);

// Structural validation only (G1 scope): leaf/inner node shape, unique
// non-empty slots, finite/sane geometry, focused_slot membership, and
// depth/node-count limits. Does not touch JSON, disk, or any live
// view/container -- see G2 (persistence) and G3 (apply).
struct sway_space_template_validation_result space_template_validate(
		const struct sway_space_template *template);

#endif // _SWAY_SPACE_TEMPLATE_H
