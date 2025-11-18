#include <limits.h>
#include <stdbool.h>
#include <strings.h>
#include <wlr/util/edges.h>
#include "sway/commands.h"
#include "sway/tree/arrange.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/output.h"
#include "sway/desktop/animation.h"
#include "util.h"

#define AXIS_HORIZONTAL (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)
#define AXIS_VERTICAL   (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)

static bool is_horizontal(uint32_t axis) {
	return axis & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
}

static const double EPSILON = 0.001;

static double get_closest_fraction(double cur, list_t *sizes, int inc) {
	if (inc > 0) {
		for (int i = 0; i < sizes->length; ++i) {
			double *size = sizes->items[i];
			if (*size > cur + EPSILON) {
				return *size;
			}
		}
		if (config->cycle_size_wrap) {
			double *size = sizes->items[0];
			return *size;
		}
	} else {
		for (int i = sizes->length - 1; i >= 0; i--) {
			double *size = sizes->items[i];
			if (*size < cur - EPSILON) {
				return *size;
			}
		}
		if (config->cycle_size_wrap) {
			double *size = sizes->items[sizes->length - 1];
			return *size;
		}
	}
	return cur;
}

struct cmd_results *cmd_cycle_size_wrap(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "cycle_size_wrap", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	config->cycle_size_wrap = parse_boolean(argv[0], config->cycle_size_wrap);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * Implement `cycle_size <incr>` for a tiled container.
 */
static struct cmd_results *cycle_size_tiled(uint32_t axis, int inc) {
	struct sway_container *current = config->handler_context.container;
	struct sway_workspace *workspace = config->handler_context.workspace;
	// Only allow layout->type axis resizing of a parent container, not individual windows
	enum sway_container_layout layout = layout_get_type(workspace);
	bool horizontal = is_horizontal(axis);
	if ((layout == L_HORIZ && horizontal) || (layout == L_VERT && !horizontal)) {
		if (current->pending.parent) {
			// Choose parent if not at workspace level yet
			current = current->pending.parent;
		}
	}

	if (container_is_scratchpad_hidden_or_child(current)) {
		return cmd_results_new(CMD_FAILURE, "Cannot cycle_size a hidden scratchpad container");
	}

	struct sway_output *output = config->handler_context.workspace->output;
	if (horizontal) {
		if (current->width_fraction <= 0.0) {
			current->width_fraction = output->scroller_options.default_width;
		}
		double fraction = current->width_fraction;
		current->width_fraction = get_closest_fraction(fraction, layout_get_widths(output), inc);
		if (layout == L_HORIZ) {
			// If it has children, propagate its width_fraction, overwriting whatever they had
			for (int i = 0; i < current->pending.children->length; ++i) {
				struct sway_container *con = current->pending.children->items[i];
				con->width_fraction = current->width_fraction;
			}
		}
	} else {
		if (current->height_fraction <= 0.0) {
			current->height_fraction = output->scroller_options.default_height;
		}
		double fraction = current->height_fraction;
		current->height_fraction = get_closest_fraction(fraction, layout_get_heights(output), inc);
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

struct cmd_results *cmd_cycle_size(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_container *current = config->handler_context.container;
	if (!current) {
		return cmd_results_new(CMD_INVALID, "Cannot cycle_size nothing");
	}
	if (container_is_floating(current)) {
		return cmd_results_new(CMD_INVALID, "Cannot cycle_size floating containers");
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "cycle_size", EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	int inc, fail = 0;
	if (strcasecmp(argv[1], "next") == 0) {
		inc = 1;
	} else if (strcasecmp(argv[1], "prev") == 0) {
		inc = -1;
	} else {
		fail = 1;
	}

	if (!fail) {
		if (strcasecmp(argv[0], "h") == 0) {
			return cycle_size_tiled(AXIS_HORIZONTAL, inc);
		} else if (strcasecmp(argv[0], "v") == 0) {
			return cycle_size_tiled(AXIS_VERTICAL, inc);
		}
	}

	const char usage[] = "Expected 'cycle_size <h|v> <next}prev>'";

	return cmd_results_new(CMD_INVALID, "%s", usage);
}
