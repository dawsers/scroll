#include <stdbool.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/tree/layout.h"

struct cmd_results *cmd_fit_size(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_container *current = config->handler_context.container;
	if (!current) {
		return cmd_results_new(CMD_INVALID, "Cannot fit_size nothing");
	}

	if (container_is_floating(current)) {
		return cmd_results_new(CMD_INVALID, "Cannot fit_size a floating container");
	}

	if (container_is_scratchpad_hidden_or_child(current)) {
		return cmd_results_new(CMD_FAILURE, "Cannot fit_size a hidden scratchpad container");
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "fit_size", EXPECTED_AT_LEAST, 3))) {
		return error;
	}

	enum sway_layout_fit_group fit;
	if (strcasecmp(argv[1], "active") == 0) {
		fit = FIT_ACTIVE;
	} else if (strcasecmp(argv[1], "visible") == 0) {
		fit = FIT_VISIBLE;
	} else if (strcasecmp(argv[1], "all") == 0) {
		fit = FIT_ALL;
	} else if (strcasecmp(argv[1], "tobeg") == 0) {
		fit = FIT_TOBEG;
	} else if (strcasecmp(argv[1], "toend") == 0) {
		fit = FIT_TOEND;
	} else {
		return cmd_results_new(CMD_INVALID, "fit_size range invalid");
	}

	bool equal;
	if (strcasecmp(argv[2], "proportional") == 0) {
		equal = false;
	} else if (strcasecmp(argv[2], "equal") == 0) {
		equal = true;
	} else {
		return cmd_results_new(CMD_INVALID, "fit_size mode (proportional|equal) invalid");
	}

	struct sway_workspace *workspace = config->handler_context.workspace;

	if (strcasecmp(argv[0], "h") == 0) {
		layout_fit_size(workspace, current, AXIS_HORIZONTAL, fit, equal);
	} else if (strcasecmp(argv[0], "v") == 0) {
		layout_fit_size(workspace, current, AXIS_VERTICAL, fit, equal);
	} else {
		const char usage[] = "Expected 'fit_size <h|v> <active|visible|all|toend|tobeg> <proportional|equal>'";

		return cmd_results_new(CMD_INVALID, "%s", usage);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
