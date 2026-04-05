#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/tree/workspace.h"
#include "util.h"

struct cmd_results *cmd_workspace_labels_color(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "workspace_labels_color", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	uint32_t color;
	parse_color(argv[0], &color);
	color_to_rgba(config->workspace_labels_color, color);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_workspace_labels_background(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "workspace_labels_background", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	uint32_t color;
	parse_color(argv[0], &color);
	color_to_rgba(config->workspace_labels_background, color);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_workspace_labels_height(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "workspace_labels_height", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	char *inv;
	int value = strtol(argv[0], &inv, 10);
	if (*inv != '\0' || value < 10) {
		return cmd_results_new(CMD_FAILURE, "workspace_labels_height: Invalid size specified");
	}
	config->workspace_labels_height = value;
	return cmd_results_new(CMD_SUCCESS, NULL);
}

/*
 *  We can set the scale, modify or reset it.
 *  If we call overview, it will toggle overview mode:
 *  1. ON: Computes overview_scale and sets overview = true
 *  2. OFF: overview = false
 *
 *  scale_workspace <exact number|increment number|reset|overview>
 */
struct cmd_results *cmd_scale_workspace(int argc, char **argv) {
	if (root->fullscreen_global) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while in global fullscreen mode.");
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_workspace *workspace = config->handler_context.workspace;
	if (!workspace) {
		return cmd_results_new(CMD_INVALID, "Need a workspace to run scale_workspace");
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "scale_workspace", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	int fail = 0;
	if (strcasecmp(argv[0], "exact") == 0) {
		if (argc < 2) {
			fail = 1;
		} else {
			double number = strtod(argv[1], NULL);
			if (number < 0.2f) {
				number = 0.2f;
			} else if (number > 1.0f) {
				number = 1.0f;
			}
			layout_scale_set(workspace, number);
		}
	} else if (strcasecmp(argv[0], "increment") == 0 || strcasecmp(argv[0], "incr") == 0) {
		if (argc < 2) {
			fail = 1;
		} else {
			double number = strtod(argv[1], NULL);
			if (!layout_scale_enabled(workspace)) {
				number += 1.0;
			} else {
				number += layout_scale_get(workspace);
			}
			if (number < 0.2) {
				number = 0.2;
			} else if (number > 1.0) {
				number = 1.0;
			}
			layout_scale_set(workspace, number);
		}
	} else if (strcasecmp(argv[0], "reset") == 0) {
		layout_scale_reset(workspace);
	} else if (strcasecmp(argv[0], "overview") == 0) {
		if (!root->jumping) {
			layout_overview_toggle(workspace, OVERVIEW_ALL);
		}
	} else if (strcasecmp(argv[0], "workspaces") == 0) {
		if (!root->jumping) {
			layout_overview_workspaces_toggle();
		}
	} else {
		fail = 1;
	}

	if (fail) {
		const char usage[] = "Expected 'scale_workspace <exact number|increment number|reset|overview|workspaces>'";
		return cmd_results_new(CMD_INVALID, "%s", usage);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}
