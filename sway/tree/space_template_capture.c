// Captures a live workspace tree into a plain-data sway_space_template.
// Mirrors space_container_create()/space_save() (sway/tree/space.c) for the
// legacy Space model, but produces the durable, slot-keyed template shape
// from G1/G2 instead of a live-view-owning sway_space_container tree.
#include "sway/tree/space_template_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/output.h"
#include "sway/tree/container.h"
#include "sway/tree/layout.h"
#include "sway/tree/view.h"

struct capture_state {
	int next_slot;
};

static enum sway_space_template_layout container_layout_to_template_layout(
		enum sway_container_layout layout) {
	switch (layout) {
	case L_HORIZ:
		return SPACE_TEMPLATE_LAYOUT_HORIZONTAL;
	case L_VERT:
		return SPACE_TEMPLATE_LAYOUT_VERTICAL;
	case L_NONE:
	default:
		return SPACE_TEMPLATE_LAYOUT_NONE;
	}
}

static enum sway_space_template_scroller_mode container_layout_to_template_scroller_mode(
		enum sway_container_layout layout) {
	switch (layout) {
	case L_HORIZ:
		return SPACE_TEMPLATE_SCROLLER_HORIZONTAL;
	case L_VERT:
		return SPACE_TEMPLATE_SCROLLER_VERTICAL;
	case L_NONE:
	default:
		return SPACE_TEMPLATE_SCROLLER_NONE;
	}
}

static enum sway_space_template_insert layout_insert_to_template_insert(
		enum sway_layout_insert insert) {
	switch (insert) {
	case INSERT_AFTER:
		return SPACE_TEMPLATE_INSERT_AFTER;
	case INSERT_BEGINNING:
		return SPACE_TEMPLATE_INSERT_BEGINNING;
	case INSERT_END:
		return SPACE_TEMPLATE_INSERT_END;
	case INSERT_BEFORE:
	default:
		return SPACE_TEMPLATE_INSERT_BEFORE;
	}
}

static enum sway_space_template_fit layout_fit_to_template_fit(enum sway_layout_fit fit) {
	switch (fit) {
	case FIT_SPLIT:
		return SPACE_TEMPLATE_FIT_FITSPLIT;
	case FIT_FRACTION:
		return SPACE_TEMPLATE_FIT_FITFRACTION;
	case FIT_NONE:
	default:
		return SPACE_TEMPLATE_FIT_NOFIT;
	}
}

static char *generate_slot(struct capture_state *state) {
	char buf[32];
	snprintf(buf, sizeof(buf), "slot-%d", ++state->next_slot);
	return strdup(buf);
}

static void maybe_capture_hints(struct sway_space_template_node *node,
		struct sway_container *container, bool with_hints) {
	if (!with_hints || !container->view) {
		return;
	}
	space_template_node_set_hints(node, view_get_app_id(container->view),
		view_get_class(container->view), view_get_title(container->view));
}

static struct sway_space_template_node *capture_tiling_container(
		struct sway_container *container, struct sway_container *focused,
		char **out_focused_slot, bool with_hints, struct capture_state *state) {
	if (container->pending.children) {
		struct sway_space_template_node *node =
			space_template_node_create_inner(container_layout_to_template_layout(container->pending.layout));
		space_template_node_set_tiling_geometry(node, container->width_fraction, container->height_fraction);
		for (int i = 0; i < container->pending.children->length; ++i) {
			struct sway_container *child = container->pending.children->items[i];
			struct sway_space_template_node *child_node =
				capture_tiling_container(child, focused, out_focused_slot, with_hints, state);
			space_template_node_add_child(node, child_node);
		}
		return node;
	}

	char *slot = generate_slot(state);
	struct sway_space_template_node *node = space_template_node_create_leaf(slot);
	space_template_node_set_tiling_geometry(node, container->width_fraction, container->height_fraction);
	maybe_capture_hints(node, container, with_hints);
	if (container == focused) {
		free(*out_focused_slot);
		*out_focused_slot = strdup(slot);
	}
	free(slot);
	return node;
}

static struct sway_space_template_node *capture_floating_container(
		struct sway_container *container, struct sway_container *focused,
		char **out_focused_slot, bool with_hints, struct capture_state *state) {
	char *slot = generate_slot(state);
	struct sway_space_template_node *node = space_template_node_create_leaf(slot);

	if (container->pending.workspace && container->pending.workspace->output) {
		struct sway_output *output = container->pending.workspace->output;
		double x = (container->pending.x - output->lx) / output->width;
		double y = (container->pending.y - output->ly) / output->height;
		double width = container->pending.width / output->width;
		double height = container->pending.height / output->height;
		space_template_node_set_floating_geometry(node, x, y, width, height);
	} else {
		space_template_node_set_floating_geometry(node, container->pending.x, container->pending.y,
			container->pending.width, container->pending.height);
	}

	maybe_capture_hints(node, container, with_hints);
	if (container == focused) {
		free(*out_focused_slot);
		*out_focused_slot = strdup(slot);
	}
	free(slot);
	return node;
}

struct sway_space_template *space_template_capture(
		struct sway_workspace *workspace, const char *name, bool with_hints) {
	struct sway_space_template *template = space_template_create(name);
	if (!workspace) {
		return template; // empty tiling/floating; still structurally valid
	}

	struct sway_seat *seat = input_manager_current_seat();
	struct sway_container *focused = seat_get_focused_container(seat);
	if (focused && focused->pending.workspace != workspace) {
		focused = NULL;
	}

	struct capture_state state = { .next_slot = 0 };
	char *focused_slot = NULL;

	for (int i = 0; i < workspace->tiling->length; ++i) {
		struct sway_container *container = workspace->tiling->items[i];
		struct sway_space_template_node *node =
			capture_tiling_container(container, focused, &focused_slot, with_hints, &state);
		space_template_add_tiling(template, node);
	}
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *container = workspace->floating->items[i];
		struct sway_space_template_node *node =
			capture_floating_container(container, focused, &focused_slot, with_hints, &state);
		space_template_add_floating(template, node);
	}

	if (focused_slot) {
		space_template_set_focused_slot(template, focused_slot);
		free(focused_slot);
	}

	template->scroller.mode = container_layout_to_template_scroller_mode(layout_modifiers_get_mode(workspace));
	template->scroller.insert = layout_insert_to_template_insert(layout_modifiers_get_insert(workspace));
	template->scroller.fit = layout_fit_to_template_fit(layout_modifiers_get_fit(workspace));
	template->scroller.focus = layout_modifiers_get_focus(workspace);
	template->scroller.center_horizontal = layout_modifiers_get_center_horizontal(workspace);
	template->scroller.center_vertical = layout_modifiers_get_center_vertical(workspace);
	template->scroller.reorder = layout_modifiers_get_reorder(workspace);

	return template;
}
