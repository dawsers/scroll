#include "sway/tree/space_template.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "stringop.h"

// --- node lifecycle -------------------------------------------------------

static struct sway_space_template_node *node_alloc(void) {
	return calloc(1, sizeof(struct sway_space_template_node));
}

struct sway_space_template_node *space_template_node_create_leaf(const char *slot) {
	struct sway_space_template_node *node = node_alloc();
	node->slot = slot ? strdup(slot) : NULL;
	return node;
}

struct sway_space_template_node *space_template_node_create_inner(
		enum sway_space_template_layout layout) {
	struct sway_space_template_node *node = node_alloc();
	node->layout = layout;
	node->children = create_list();
	return node;
}

void space_template_node_destroy(struct sway_space_template_node *node) {
	if (!node) {
		return;
	}
	if (node->children) {
		for (int i = 0; i < node->children->length; ++i) {
			space_template_node_destroy(node->children->items[i]);
		}
		list_free(node->children);
	}
	free(node->slot);
	free(node->hint_app_id);
	free(node->hint_class);
	free(node->hint_title);
	free(node);
}

struct sway_space_template_node *space_template_node_copy(
		const struct sway_space_template_node *node) {
	if (!node) {
		return NULL;
	}
	struct sway_space_template_node *copy = node_alloc();
	copy->layout = node->layout;
	copy->slot = node->slot ? strdup(node->slot) : NULL;
	copy->hint_app_id = node->hint_app_id ? strdup(node->hint_app_id) : NULL;
	copy->hint_class = node->hint_class ? strdup(node->hint_class) : NULL;
	copy->hint_title = node->hint_title ? strdup(node->hint_title) : NULL;
	copy->width_fraction = node->width_fraction;
	copy->height_fraction = node->height_fraction;
	copy->x = node->x;
	copy->y = node->y;
	copy->width = node->width;
	copy->height = node->height;
	if (node->children) {
		copy->children = create_list();
		for (int i = 0; i < node->children->length; ++i) {
			list_add(copy->children, space_template_node_copy(node->children->items[i]));
		}
	}
	return copy;
}

bool space_template_node_is_leaf(const struct sway_space_template_node *node) {
	return node && node->children == NULL;
}

bool space_template_node_add_child(struct sway_space_template_node *parent,
		struct sway_space_template_node *child) {
	if (!parent || !parent->children || !child) {
		return false;
	}
	list_add(parent->children, child);
	return true;
}

bool space_template_node_set_hints(struct sway_space_template_node *node,
		const char *app_id, const char *class, const char *title) {
	if (!node || node->children) {
		return false;
	}
	free(node->hint_app_id);
	free(node->hint_class);
	free(node->hint_title);
	node->hint_app_id = app_id ? strdup(app_id) : NULL;
	node->hint_class = class ? strdup(class) : NULL;
	node->hint_title = title ? strdup(title) : NULL;
	return true;
}

void space_template_node_set_tiling_geometry(struct sway_space_template_node *node,
		double width_fraction, double height_fraction) {
	if (!node) {
		return;
	}
	node->width_fraction = width_fraction;
	node->height_fraction = height_fraction;
}

void space_template_node_set_floating_geometry(struct sway_space_template_node *node,
		double x, double y, double width, double height) {
	if (!node) {
		return;
	}
	node->x = x;
	node->y = y;
	node->width = width;
	node->height = height;
}

// --- template lifecycle ----------------------------------------------------

struct sway_space_template *space_template_create(const char *name) {
	struct sway_space_template *template = calloc(1, sizeof(struct sway_space_template));
	template->version = SPACE_TEMPLATE_VERSION;
	template->name = name ? strdup(name) : NULL;
	template->tiling = create_list();
	template->floating = create_list();
	return template;
}

void space_template_destroy(struct sway_space_template *template) {
	if (!template) {
		return;
	}
	free(template->name);
	free(template->focused_slot);
	if (template->tiling) {
		for (int i = 0; i < template->tiling->length; ++i) {
			space_template_node_destroy(template->tiling->items[i]);
		}
		list_free(template->tiling);
	}
	if (template->floating) {
		for (int i = 0; i < template->floating->length; ++i) {
			space_template_node_destroy(template->floating->items[i]);
		}
		list_free(template->floating);
	}
	free(template);
}

struct sway_space_template *space_template_copy(const struct sway_space_template *template) {
	if (!template) {
		return NULL;
	}
	struct sway_space_template *copy = calloc(1, sizeof(struct sway_space_template));
	copy->version = template->version;
	copy->name = template->name ? strdup(template->name) : NULL;
	copy->scroller = template->scroller;
	copy->focused_slot = template->focused_slot ? strdup(template->focused_slot) : NULL;
	copy->tiling = create_list();
	if (template->tiling) {
		for (int i = 0; i < template->tiling->length; ++i) {
			list_add(copy->tiling, space_template_node_copy(template->tiling->items[i]));
		}
	}
	copy->floating = create_list();
	if (template->floating) {
		for (int i = 0; i < template->floating->length; ++i) {
			list_add(copy->floating, space_template_node_copy(template->floating->items[i]));
		}
	}
	return copy;
}

void space_template_set_focused_slot(struct sway_space_template *template, const char *slot) {
	if (!template) {
		return;
	}
	free(template->focused_slot);
	template->focused_slot = slot ? strdup(slot) : NULL;
}

void space_template_add_tiling(struct sway_space_template *template,
		struct sway_space_template_node *node) {
	if (!template || !node) {
		return;
	}
	list_add(template->tiling, node);
}

void space_template_add_floating(struct sway_space_template *template,
		struct sway_space_template_node *node) {
	if (!template || !node) {
		return;
	}
	list_add(template->floating, node);
}

// --- validation --------------------------------------------------------

const char *space_template_validation_error_str(enum sway_space_template_validation_error error) {
	switch (error) {
	case SPACE_TEMPLATE_VALID:
		return "valid";
	case SPACE_TEMPLATE_ERROR_EMPTY_NAME:
		return "template name is empty";
	case SPACE_TEMPLATE_ERROR_AMBIGUOUS_NODE:
		return "node has both children and a slot";
	case SPACE_TEMPLATE_ERROR_EMPTY_NODE:
		return "node has neither children nor a slot";
	case SPACE_TEMPLATE_ERROR_EMPTY_CHILDREN:
		return "inner node has no children";
	case SPACE_TEMPLATE_ERROR_EMPTY_SLOT:
		return "leaf slot is empty";
	case SPACE_TEMPLATE_ERROR_DUPLICATE_SLOT:
		return "duplicate slot";
	case SPACE_TEMPLATE_ERROR_INVALID_GEOMETRY:
		return "invalid geometry";
	case SPACE_TEMPLATE_ERROR_INVALID_FOCUSED_SLOT:
		return "focused_slot does not name a slot in the template";
	case SPACE_TEMPLATE_ERROR_TOO_DEEP:
		return "template tree exceeds the maximum depth";
	case SPACE_TEMPLATE_ERROR_TOO_MANY_NODES:
		return "template tree exceeds the maximum node count";
	}
	return "unknown error";
}

struct validate_state {
	list_t *seen_slots; // const char *, borrowed from template nodes
	int node_count;
};

static struct sway_space_template_validation_result validate_result(
		enum sway_space_template_validation_error error, char *path) {
	struct sway_space_template_validation_result result = { .error = error, .path = path };
	return result;
}

static bool slot_seen(struct validate_state *state, const char *slot) {
	for (int i = 0; i < state->seen_slots->length; ++i) {
		if (strcmp((const char *)state->seen_slots->items[i], slot) == 0) {
			return true;
		}
	}
	return false;
}

static struct sway_space_template_validation_result validate_node(
		struct sway_space_template_node *node, char *path, int depth,
		bool is_floating, struct validate_state *state) {
	if (depth > SPACE_TEMPLATE_MAX_DEPTH) {
		return validate_result(SPACE_TEMPLATE_ERROR_TOO_DEEP, path);
	}
	if (++state->node_count > SPACE_TEMPLATE_MAX_NODES) {
		return validate_result(SPACE_TEMPLATE_ERROR_TOO_MANY_NODES, path);
	}

	bool has_children = node->children != NULL;
	bool has_slot = node->slot != NULL;

	if (has_children && has_slot) {
		return validate_result(SPACE_TEMPLATE_ERROR_AMBIGUOUS_NODE, path);
	}
	if (!has_children && !has_slot) {
		return validate_result(SPACE_TEMPLATE_ERROR_EMPTY_NODE, path);
	}

	if (is_floating) {
		if (!isfinite(node->x) || !isfinite(node->y) ||
				!isfinite(node->width) || !isfinite(node->height) ||
				node->width <= 0 || node->height <= 0) {
			return validate_result(SPACE_TEMPLATE_ERROR_INVALID_GEOMETRY, path);
		}
	} else {
		if (!isfinite(node->width_fraction) || node->width_fraction < 0 ||
				!isfinite(node->height_fraction) || node->height_fraction < 0) {
			return validate_result(SPACE_TEMPLATE_ERROR_INVALID_GEOMETRY, path);
		}
	}

	if (!has_children) {
		// Leaf.
		if (node->slot[0] == '\0') {
			return validate_result(SPACE_TEMPLATE_ERROR_EMPTY_SLOT, path);
		}
		if (slot_seen(state, node->slot)) {
			return validate_result(SPACE_TEMPLATE_ERROR_DUPLICATE_SLOT, path);
		}
		list_add(state->seen_slots, node->slot);
		free(path);
		return validate_result(SPACE_TEMPLATE_VALID, NULL);
	}

	// Inner node.
	if (node->children->length == 0) {
		return validate_result(SPACE_TEMPLATE_ERROR_EMPTY_CHILDREN, path);
	}
	for (int i = 0; i < node->children->length; ++i) {
		struct sway_space_template_node *child = node->children->items[i];
		char *child_path = format_str("%s.children[%d]", path, i);
		struct sway_space_template_validation_result result =
			validate_node(child, child_path, depth + 1, is_floating, state);
		if (result.error != SPACE_TEMPLATE_VALID) {
			free(path);
			return result;
		}
	}
	free(path);
	return validate_result(SPACE_TEMPLATE_VALID, NULL);
}

struct sway_space_template_validation_result space_template_validate(
		const struct sway_space_template *template) {
	if (!template || !template->name || template->name[0] == '\0') {
		return validate_result(SPACE_TEMPLATE_ERROR_EMPTY_NAME, NULL);
	}

	struct validate_state state = {
		.seen_slots = create_list(),
		.node_count = 0,
	};

	struct sway_space_template_validation_result result = validate_result(SPACE_TEMPLATE_VALID, NULL);

	if (template->tiling) {
		for (int i = 0; i < template->tiling->length && result.error == SPACE_TEMPLATE_VALID; ++i) {
			struct sway_space_template_node *node = template->tiling->items[i];
			char *path = format_str("tiling[%d]", i);
			result = validate_node(node, path, 1, false, &state);
		}
	}
	if (result.error == SPACE_TEMPLATE_VALID && template->floating) {
		for (int i = 0; i < template->floating->length && result.error == SPACE_TEMPLATE_VALID; ++i) {
			struct sway_space_template_node *node = template->floating->items[i];
			char *path = format_str("floating[%d]", i);
			result = validate_node(node, path, 1, true, &state);
		}
	}

	if (result.error == SPACE_TEMPLATE_VALID && template->focused_slot &&
			template->focused_slot[0] != '\0' && !slot_seen(&state, template->focused_slot)) {
		result = validate_result(SPACE_TEMPLATE_ERROR_INVALID_FOCUSED_SLOT, NULL);
	}

	list_free(state.seen_slots);
	return result;
}
