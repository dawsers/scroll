#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/edges.h>
#include "sway/commands.h"
#include "sway/tree/arrange.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/tree/layout.h"
#include "sway/desktop/animation.h"

#define AXIS_HORIZONTAL (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)
#define AXIS_VERTICAL   (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)

static bool is_horizontal(uint32_t axis) {
	return axis & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
}

/**
 * Implement `set_size <fraction>` for a floating container.
 */
static struct cmd_results *set_size_floating(uint32_t axis, double fraction) {
	struct sway_container *current = config->handler_context.container;

	if (container_is_scratchpad_hidden_or_child(current)) {
		return cmd_results_new(CMD_FAILURE, "Cannot set_size a hidden scratchpad container");
	}

	int min_width, max_width, min_height, max_height;
	floating_calculate_constraints(&min_width, &max_width,
			&min_height, &max_height);

	struct sway_workspace *workspace = config->handler_context.workspace;
	bool horizontal = is_horizontal(axis);

	if (horizontal) {
		const double width = fmax(min_width, fmin(fraction * workspace->width, max_width));
		const double grow_width = width - current->pending.width;
		current->pending.x -= 0.5 * grow_width;
		current->pending.width = width;
		current->pending.content_x -= 0.5 * grow_width;
		current->pending.content_width += grow_width;
	} else {
		const double height = fmax(min_height, fmin(fraction * workspace->height, max_height));
		const double grow_height = height - current->pending.height;
		current->pending.y -= 0.5 * grow_height;
		current->pending.height = height;
		current->pending.content_y -= 0.5 * grow_height;
		current->pending.content_height += grow_height;
	}

	animation_set_type(ANIMATION_WINDOW_SIZE);
	arrange_container(current);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * Implement `set_size <fraction>` for a tiled container.
 */
static struct cmd_results *set_size_tiled(uint32_t axis, double fraction) {
	struct sway_container *current = config->handler_context.container;

	if (container_is_scratchpad_hidden_or_child(current)) {
		return cmd_results_new(CMD_FAILURE, "Cannot set_size a hidden scratchpad container");
	}

	enum sway_container_layout layout = layout_get_type(config->handler_context.workspace);
	bool horizontal = is_horizontal(axis);
	if ((layout == L_HORIZ && horizontal) || (layout == L_VERT && !horizontal)) {
		if (current->pending.parent) {
			// Choose parent if not at workspace level yet
			current = current->pending.parent;
		}
	}

	if (horizontal) {
		current->width_fraction = fraction;
		if (layout == L_HORIZ) {
			// If it has children, propagate its width_fraction, overwriting whatever they had
			for (int i = 0; i < current->pending.children->length; ++i) {
				struct sway_container *con = current->pending.children->items[i];
				con->width_fraction = current->width_fraction;
			}
		}
	} else {
		current->height_fraction = fraction;
		if (layout == L_VERT) {
			// If it has children, propagate its width_fraction, overwriting whatever they had
			for (int i = 0; i < current->pending.children->length; ++i) {
				struct sway_container *con = current->pending.children->items[i];
				con->height_fraction = current->height_fraction;
			}
		}
	}

	animation_set_type(ANIMATION_WINDOW_SIZE);
	if (current->pending.parent) {
		arrange_container(current->pending.parent);
	} else {
		arrange_workspace(current->pending.workspace);
	}

	layout_tiling_resize_callback(current);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_set_size(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_container *current = config->handler_context.container;
	if (!current) {
		return cmd_results_new(CMD_INVALID, "Cannot set_size nothing");
	}
	bool floating = container_is_floating(current);

	struct cmd_results *error;
	if ((error = checkarg(argc, "set_size", EXPECTED_AT_LEAST, 2))) {
		return error;
	}
	double fraction = strtod(argv[1], NULL);

	if (strcasecmp(argv[0], "h") == 0) {
		return floating ? set_size_floating(AXIS_HORIZONTAL, fraction) :
			set_size_tiled(AXIS_HORIZONTAL, fraction);
	} else if (strcasecmp(argv[0], "v") == 0) {
		return floating ? set_size_floating(AXIS_VERTICAL, fraction) :
			set_size_tiled(AXIS_VERTICAL, fraction);
	}

	const char usage[] = "Expected 'set_size <h|v> <fraction>'";

	return cmd_results_new(CMD_INVALID, "%s", usage);
}
