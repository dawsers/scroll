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

static const double EPSILON = 0.0001;

static struct cmd_results *toggle_size(enum sway_toggle_size mode, double width_fraction,
		double height_fraction) {
	struct sway_container * current = config->handler_context.container;
	if (current && container_is_floating(current)) {
		return cmd_results_new(CMD_INVALID, "Cannot toggle_size floating containers");
	}
	struct sway_workspace *workspace = config->handler_context.workspace;
	enum sway_toggle_size old_mode = layout_toggle_size_mode(workspace);
	double old_width = layout_toggle_size_width_fraction(workspace);
	double old_height = layout_toggle_size_height_fraction(workspace);

	layout_toggle_size(workspace, current, TOGGLE_SIZE_NONE, 0.0, 0.0);
	if (mode != TOGGLE_SIZE_NONE) {
		// If the mode is the same and width and height too, toggle
		if (mode != old_mode ||
			(fabs(old_width - width_fraction) > EPSILON &&
			 fabs(old_height - height_fraction) > EPSILON)) {
			layout_toggle_size(workspace, current, mode, width_fraction, height_fraction);
		}
	}

	animation_set_type(ANIMATION_WINDOW_SIZE);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

// Toggles the size for tiling windows in the current workspace.
// It can affect the active (currently focused) window every time it changes,
// or all of them in the workspace.

// toggle_size <this|active|all|reset> <width_fraction> <height_fraction>
struct cmd_results *cmd_toggle_size(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "toggle_size", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	if (strcasecmp(argv[0], "reset") == 0) {
		return toggle_size(TOGGLE_SIZE_NONE, 0.0, 0.0);
	}

	if ((error = checkarg(argc, "toggle_size", EXPECTED_AT_LEAST, 3))) {
		return error;
	}

	double width_fraction = strtod(argv[1], NULL);
	double height_fraction = strtod(argv[2], NULL);

	if (strcasecmp(argv[0], "active") == 0) {
		return toggle_size(TOGGLE_SIZE_ACTIVE, width_fraction, height_fraction);
	} else if (strcasecmp(argv[0], "all") == 0) {
		return toggle_size(TOGGLE_SIZE_ALL, width_fraction, height_fraction);
	} else if (strcasecmp(argv[0], "this") == 0) {
		struct sway_container * current = config->handler_context.container;
		if (!current) {
			return cmd_results_new(CMD_INVALID, "toggle_size this needs an active container");
		}
		if (container_is_floating(current)) {
			return cmd_results_new(CMD_INVALID, "Cannot toggle_size floating containers");
		}
		layout_toggle_size_container(current, width_fraction, height_fraction);
	}
	const char usage[] = "Expected 'toggle_size <this|active|all|reset> <width_fraction> <height_fraction>'";

	return cmd_results_new(CMD_INVALID, "%s", usage);
}
