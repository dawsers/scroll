#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/tree/workspace.h"

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
	} else {
		fail = 1;
	}

	if (fail) {
		const char usage[] = "Expected 'scale_workspace <exact number|increment number|reset|overview>'";
		return cmd_results_new(CMD_INVALID, "%s", usage);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}
