// Atomic space-template apply engine (validate -> sweep -> arrange ->
// dissolve). This is a slot-keyed generalization of the named-Space restore
// path in sway/tree/space.c (see layout_space_restore() and its helpers),
// adapted to: (a) look views up by template slot via caller-resolved
// bindings instead of a saved space's embedded view pointers, and (b)
// require the full binding set to validate before any mutation happens,
// per issue #3 (no partial/pending apply in v1).
#include "sway/tree/space_template_apply.h"

#include <string.h>

#include "list.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/layout.h"
#include "sway/tree/node.h"
#include "sway/tree/root.h"
#include "sway/tree/workspace.h"
#include "sway/lua.h"

// --- result plumbing -----------------------------------------------------

const char *space_template_apply_error_str(enum sway_space_template_apply_error error) {
	switch (error) {
	case SPACE_TEMPLATE_APPLY_OK:
		return "ok";
	case SPACE_TEMPLATE_APPLY_ERROR_NO_WORKSPACE:
		return "no current workspace";
	case SPACE_TEMPLATE_APPLY_ERROR_INVALID_TEMPLATE:
		return "template failed structural validation";
	case SPACE_TEMPLATE_APPLY_ERROR_UNSUPPORTED_TEMPLATE:
		return "template uses an unsupported shape (non-leaf floating root)";
	case SPACE_TEMPLATE_APPLY_ERROR_UNKNOWN_SLOT:
		return "binding names a slot the template does not have";
	case SPACE_TEMPLATE_APPLY_ERROR_MISSING_SLOT:
		return "template slot has no binding";
	case SPACE_TEMPLATE_APPLY_ERROR_DUPLICATE_SLOT:
		return "slot is bound more than once";
	case SPACE_TEMPLATE_APPLY_ERROR_DUPLICATE_VIEW:
		return "view is bound to more than one slot";
	case SPACE_TEMPLATE_APPLY_ERROR_STALE_VIEW:
		return "bound view is not live";
	case SPACE_TEMPLATE_APPLY_ERROR_ARRANGE_FAILED:
		return "apply failed after the sweep; sweep was rolled back";
	}
	return "unknown error";
}

void space_template_apply_result_destroy(struct sway_space_template_apply_result *result) {
	if (!result || !result->slots) {
		return;
	}
	for (int i = 0; i < result->slots->length; ++i) {
		free(result->slots->items[i]);
	}
	list_free(result->slots);
	result->slots = NULL;
}

static struct sway_space_template_apply_result apply_result(
		enum sway_space_template_apply_error error, list_t *slots) {
	struct sway_space_template_apply_result result = { .error = error, .slots = slots };
	return result;
}

static list_t *one_slot(const char *slot) {
	list_t *slots = create_list();
	list_add(slots, strdup(slot));
	return slots;
}

// --- enum mapping (template -> live compositor enums) -------------------

static enum sway_container_layout template_layout_to_container_layout(
		enum sway_space_template_layout layout) {
	switch (layout) {
	case SPACE_TEMPLATE_LAYOUT_HORIZONTAL:
		return L_HORIZ;
	case SPACE_TEMPLATE_LAYOUT_VERTICAL:
		return L_VERT;
	case SPACE_TEMPLATE_LAYOUT_NONE:
	default:
		return L_NONE;
	}
}

static enum sway_layout_insert template_insert_to_layout_insert(
		enum sway_space_template_insert insert) {
	switch (insert) {
	case SPACE_TEMPLATE_INSERT_AFTER:
		return INSERT_AFTER;
	case SPACE_TEMPLATE_INSERT_BEGINNING:
		return INSERT_BEGINNING;
	case SPACE_TEMPLATE_INSERT_END:
		return INSERT_END;
	case SPACE_TEMPLATE_INSERT_BEFORE:
	default:
		return INSERT_BEFORE;
	}
}

static enum sway_layout_fit template_fit_to_layout_fit(enum sway_space_template_fit fit) {
	switch (fit) {
	case SPACE_TEMPLATE_FIT_FITSPLIT:
		return FIT_SPLIT;
	case SPACE_TEMPLATE_FIT_FITFRACTION:
		return FIT_FRACTION;
	case SPACE_TEMPLATE_FIT_NOFIT:
	default:
		return FIT_NONE;
	}
}

static enum sway_container_layout template_scroller_mode_to_container_layout(
		enum sway_space_template_scroller_mode mode) {
	switch (mode) {
	case SPACE_TEMPLATE_SCROLLER_HORIZONTAL:
		return L_HORIZ;
	case SPACE_TEMPLATE_SCROLLER_VERTICAL:
		return L_VERT;
	case SPACE_TEMPLATE_SCROLLER_NONE:
	default:
		return L_NONE;
	}
}

static void apply_scroller_modifiers(struct sway_workspace *workspace,
		const struct sway_space_template_scroller *scroller) {
	layout_modifiers_set_mode(workspace, template_scroller_mode_to_container_layout(scroller->mode));
	layout_modifiers_set_insert(workspace, template_insert_to_layout_insert(scroller->insert));
	layout_modifiers_set_fit(workspace, template_fit_to_layout_fit(scroller->fit));
	layout_modifiers_set_focus(workspace, scroller->focus);
	layout_modifiers_set_center_horizontal(workspace, scroller->center_horizontal);
	layout_modifiers_set_center_vertical(workspace, scroller->center_vertical);
	layout_modifiers_set_reorder(workspace, scroller->reorder);
}

// --- slot bookkeeping -----------------------------------------------------

static void collect_slots(list_t *nodes, list_t *out) {
	if (!nodes) {
		return;
	}
	for (int i = 0; i < nodes->length; ++i) {
		struct sway_space_template_node *node = nodes->items[i];
		if (node->children) {
			collect_slots(node->children, out);
		} else if (node->slot) {
			list_add(out, node->slot); // borrowed from the template
		}
	}
}

static struct sway_view *find_binding_view(list_t *bindings, const char *slot) {
	for (int i = 0; i < bindings->length; ++i) {
		struct sway_space_template_binding *binding = bindings->items[i];
		if (binding->slot && strcmp(binding->slot, slot) == 0) {
			return binding->view;
		}
	}
	return NULL;
}

static bool view_is_live(struct sway_view *view) {
	return view && view->container && !view->destroying;
}

// --- validation (zero mutation) ------------------------------------------

static struct sway_space_template_apply_result validate_floating_shape(
		const struct sway_space_template *template) {
	if (template->floating) {
		for (int i = 0; i < template->floating->length; ++i) {
			struct sway_space_template_node *node = template->floating->items[i];
			if (node->children) {
				return apply_result(SPACE_TEMPLATE_APPLY_ERROR_UNSUPPORTED_TEMPLATE, NULL);
			}
		}
	}
	return apply_result(SPACE_TEMPLATE_APPLY_OK, NULL);
}

static struct sway_space_template_apply_result validate_bindings(
		const struct sway_space_template *template, list_t *bindings) {
	list_t *template_slots = create_list(); // char*, borrowed
	collect_slots(template->tiling, template_slots);
	collect_slots(template->floating, template_slots);

	list_t *bound_slots = create_list(); // char*, borrowed

	for (int i = 0; i < bindings->length; ++i) {
		struct sway_space_template_binding *binding = bindings->items[i];

		if (!binding->slot || !binding->slot[0]) {
			list_free(template_slots);
			list_free(bound_slots);
			return apply_result(SPACE_TEMPLATE_APPLY_ERROR_UNKNOWN_SLOT, create_list());
		}

		bool known = false;
		for (int j = 0; j < template_slots->length; ++j) {
			if (strcmp((const char *)template_slots->items[j], binding->slot) == 0) {
				known = true;
				break;
			}
		}
		if (!known) {
			list_t *slots = one_slot(binding->slot);
			list_free(template_slots);
			list_free(bound_slots);
			return apply_result(SPACE_TEMPLATE_APPLY_ERROR_UNKNOWN_SLOT, slots);
		}

		for (int j = 0; j < bound_slots->length; ++j) {
			if (strcmp((const char *)bound_slots->items[j], binding->slot) == 0) {
				list_t *slots = one_slot(binding->slot);
				list_free(template_slots);
				list_free(bound_slots);
				return apply_result(SPACE_TEMPLATE_APPLY_ERROR_DUPLICATE_SLOT, slots);
			}
		}
		list_add(bound_slots, (void *)binding->slot);

		if (!view_is_live(binding->view)) {
			list_t *slots = one_slot(binding->slot);
			list_free(template_slots);
			list_free(bound_slots);
			return apply_result(SPACE_TEMPLATE_APPLY_ERROR_STALE_VIEW, slots);
		}
	}

	for (int i = 0; i < bindings->length; ++i) {
		struct sway_space_template_binding *a = bindings->items[i];
		for (int j = i + 1; j < bindings->length; ++j) {
			struct sway_space_template_binding *b = bindings->items[j];
			if (a->view == b->view) {
				list_t *slots = create_list();
				list_add(slots, strdup(a->slot));
				list_add(slots, strdup(b->slot));
				list_free(template_slots);
				list_free(bound_slots);
				return apply_result(SPACE_TEMPLATE_APPLY_ERROR_DUPLICATE_VIEW, slots);
			}
		}
	}

	list_t *missing = create_list();
	for (int i = 0; i < template_slots->length; ++i) {
		const char *slot = template_slots->items[i];
		bool found = false;
		for (int j = 0; j < bound_slots->length; ++j) {
			if (strcmp((const char *)bound_slots->items[j], slot) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			list_add(missing, strdup(slot));
		}
	}
	list_free(template_slots);
	list_free(bound_slots);

	if (missing->length > 0) {
		return apply_result(SPACE_TEMPLATE_APPLY_ERROR_MISSING_SLOT, missing);
	}
	list_free(missing);

	return apply_result(SPACE_TEMPLATE_APPLY_OK, NULL);
}

// --- sweep (H-01: restore_hide parity) ------------------------------------

struct sweep_data {
	list_t *bound_views; // struct sway_view *, borrowed
	list_t *unbound_views; // struct sway_view *, collected
};

static void collect_unbound_views(struct sway_container *container, void *data) {
	struct sweep_data *sweep = data;
	if (container->view && list_find(sweep->bound_views, container->view) == -1) {
		list_add(sweep->unbound_views, container->view);
	}
}

// Returns the list of containers that were moved to the scratchpad, so the
// caller can undo the sweep (root_scratchpad_show() on each) if arrange
// fails afterward.
static list_t *sweep_unrelated_views(struct sway_workspace *workspace, list_t *bindings) {
	list_t *bound_views = create_list();
	for (int i = 0; i < bindings->length; ++i) {
		struct sway_space_template_binding *binding = bindings->items[i];
		list_add(bound_views, binding->view);
	}

	struct sweep_data data = { .bound_views = bound_views, .unbound_views = create_list() };
	workspace_for_each_container(workspace, collect_unbound_views, &data);
	list_free(bound_views);

	list_t *swept = create_list();
	for (int i = 0; i < data.unbound_views->length; ++i) {
		struct sway_view *view = data.unbound_views->items[i];
		struct sway_container *container = view->container;
		if (!container) {
			continue;
		}
		if (container->scratchpad) {
			root_scratchpad_hide(container);
		} else {
			root_scratchpad_add_container(container, NULL);
		}
		list_add(swept, container);
	}
	list_free(data.unbound_views);
	return swept;
}

// See space_template_apply_h's doc comment: a "single-command rollback
// window", not a full transaction log.
static void undo_sweep(list_t *swept) {
	for (int i = 0; i < swept->length; ++i) {
		root_scratchpad_show(swept->items[i]);
	}
}

// --- arrange ---------------------------------------------------------

static void fill_node_tiling_geometry(const struct sway_space_template_node *node,
		struct sway_container *container) {
	container->width_fraction = node->width_fraction;
	container->height_fraction = node->height_fraction;
}

static void fill_node_floating_geometry(const struct sway_space_template_node *node,
		struct sway_container *container) {
	if (container_is_floating(container) && container->pending.workspace) {
		struct sway_output *output = container->pending.workspace->output;
		container->pending.x = output->width * node->x + output->lx;
		container->pending.y = output->height * node->y + output->ly;
		container->pending.width = node->width * output->width;
		container->pending.height = node->height * output->height;
	} else {
		container->pending.x = node->x;
		container->pending.y = node->y;
		container->pending.width = node->width;
		container->pending.height = node->height;
	}
}

static void detach_from_current_position(struct sway_container *container) {
	if (!container_is_floating(container)) {
		struct sway_container *parent = container->pending.parent;
		container_detach(container);
		if (parent) {
			container_reap_empty(parent);
		}
	}
}

// Detaches `container` (the live container backing a bound view) from
// wherever it currently lives -- scratchpad, floating, or elsewhere in the
// tiling tree -- so it can be re-homed into the tree apply is building.
// `view_float` collects views that transition from tiling to floating, for
// the Lua float-callback pass apply() runs once arrange completes (see
// lua_execute_view_float_cbs(), matching legacy sway_space's behavior).
static void detach_bound_container(struct sway_container *container, list_t *view_float) {
	detach_from_current_position(container);
	if (container->scratchpad) {
		root_scratchpad_show(container);
		root_scratchpad_remove_container(container);
	}
	if (container_is_floating(container)) {
		struct sway_workspace *ws = container->pending.workspace;
		if (ws) {
			int idx = list_find(ws->floating, container);
			if (idx != -1) {
				list_del(ws->floating, idx);
			}
			workspace_consider_destroy(ws);
			node_set_dirty(&ws->node);
		}
	} else {
		list_add(view_float, container->view);
	}
}

static struct sway_container *apply_node_tiling(struct sway_workspace *workspace,
		const struct sway_space_template_node *node, const char *focused_slot,
		list_t *bindings, struct sway_container *parent, list_t *view_float) {
	if (node->children) {
		struct sway_container *inner = container_create(NULL);
		inner->pending.workspace = workspace;
		inner->pending.parent = parent;
		inner->pending.layout = template_layout_to_container_layout(node->layout);
		inner->pending.focused_inactive_child = NULL;

		bool has_children = false;
		for (int i = 0; i < node->children->length; ++i) {
			struct sway_space_template_node *child_node = node->children->items[i];
			struct sway_container *child =
				apply_node_tiling(workspace, child_node, focused_slot, bindings, inner, view_float);
			if (child) {
				has_children = true;
			}
		}

		if (has_children) {
			fill_node_tiling_geometry(node, inner);
			container_update_representation(inner);
			node_set_dirty(&inner->node);
			if (parent) {
				list_add(parent->pending.children, inner);
			} else {
				list_add(workspace->tiling, inner);
			}
		} else {
			// Defensive only: every slot was validated bound, so an inner
			// node should never end up childless here.
			container_begin_destroy(inner);
			inner = NULL;
		}
		return inner;
	}

	struct sway_view *view = find_binding_view(bindings, node->slot);
	if (!view_is_live(view)) {
		// Defensive only (see space_template_apply_error_str's
		// ARRANGE_FAILED doc): validate_bindings() already required this
		// exact view to be live.
		return NULL;
	}
	struct sway_container *container = view->container;

	detach_bound_container(container, view_float);

	if (parent) {
		container->pending.workspace = parent->pending.workspace;
		container->pending.parent = parent;
	} else {
		container->pending.workspace = workspace;
		container->pending.parent = NULL;
	}
	arrange_container(container);
	node_set_dirty(&container->node);
	if (parent) {
		list_add(parent->pending.children, container);
	} else {
		list_add(workspace->tiling, container);
	}
	fill_node_tiling_geometry(node, container);
	container_update_representation(container);
	if (parent) {
		node_set_dirty(&parent->node);
	}

	if (focused_slot && focused_slot[0] && strcmp(node->slot, focused_slot) == 0) {
		seat_set_focus_container(input_manager_current_seat(), container);
	}

	return container;
}

static bool apply_node_floating(struct sway_workspace *workspace,
		const struct sway_space_template_node *node, const char *focused_slot,
		list_t *bindings, list_t *view_float) {
	struct sway_view *view = find_binding_view(bindings, node->slot);
	if (!view_is_live(view)) {
		return false; // defensive only; see apply_node_tiling()
	}
	struct sway_container *container = view->container;

	detach_bound_container(container, view_float);

	container->pending.parent = NULL;
	container->pending.workspace = workspace;
	if (!container_is_floating(container)) {
		container_set_floating(container, true);
	}
	arrange_container(container);
	node_set_dirty(&container->node);
	list_add(workspace->floating, container);
	fill_node_floating_geometry(node, container);
	container_update_representation(container);

	if (focused_slot && focused_slot[0] && strcmp(node->slot, focused_slot) == 0) {
		seat_set_focus_container(input_manager_current_seat(), container);
	}

	return true;
}

// --- top-level apply -------------------------------------------------

struct sway_space_template_apply_result space_template_apply(
		const struct sway_space_template *template,
		struct sway_workspace *workspace,
		list_t *bindings) {
	if (!workspace) {
		return apply_result(SPACE_TEMPLATE_APPLY_ERROR_NO_WORKSPACE, NULL);
	}

	struct sway_space_template_validation_result validation = space_template_validate(template);
	if (validation.error != SPACE_TEMPLATE_VALID) {
		free(validation.path);
		return apply_result(SPACE_TEMPLATE_APPLY_ERROR_INVALID_TEMPLATE, NULL);
	}

	struct sway_space_template_apply_result shape_check = validate_floating_shape(template);
	if (shape_check.error != SPACE_TEMPLATE_APPLY_OK) {
		return shape_check;
	}

	struct sway_space_template_apply_result binding_check = validate_bindings(template, bindings);
	if (binding_check.error != SPACE_TEMPLATE_APPLY_OK) {
		return binding_check;
	}

	if (workspace->fullscreen && workspace->fullscreen->pending.fullscreen_mode != FULLSCREEN_NONE) {
		container_fullscreen_disable(workspace->fullscreen);
	}

	list_t *swept = sweep_unrelated_views(workspace, bindings);

	list_t *view_float = create_list();
	bool ok = true;

	for (int i = 0; i < template->tiling->length && ok; ++i) {
		struct sway_space_template_node *node = template->tiling->items[i];
		struct sway_container *result =
			apply_node_tiling(workspace, node, template->focused_slot, bindings, NULL, view_float);
		if (!result) {
			ok = false;
		}
	}
	for (int i = 0; i < template->floating->length && ok; ++i) {
		struct sway_space_template_node *node = template->floating->items[i];
		if (!apply_node_floating(workspace, node, template->focused_slot, bindings, view_float)) {
			ok = false;
		}
	}

	if (!ok) {
		undo_sweep(swept);
		list_free(swept);
		list_free(view_float);
		return apply_result(SPACE_TEMPLATE_APPLY_ERROR_ARRANGE_FAILED, NULL);
	}

	apply_scroller_modifiers(workspace, &template->scroller);

	if (template->tiling->length + template->floating->length > 0) {
		arrange_workspace(workspace);
		node_set_dirty(&workspace->node);
	}

	for (int i = 0; i < view_float->length; ++i) {
		lua_execute_view_float_cbs(view_float->items[i]);
	}
	list_free(view_float);
	list_free(swept);

	return apply_result(SPACE_TEMPLATE_APPLY_OK, NULL);
}
