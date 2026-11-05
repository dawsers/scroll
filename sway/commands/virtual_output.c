#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/output.h"
#include "sway/tree/root.h"
#include "sway/tree/virtual_output.h"
#include "sway/tree/workspace.h"
#include "sway/log.h"
#include "stringop.h"

static const char expected[] =
	"Expected 'virtual_output split [h|v|reset] [<fraction>] [<gap>]' or "
	"'virtual_output toggle'";

struct cmd_results *cmd_virtual_output(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "virtual_output", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	// At startup there are no outputs yet, so defer the command until they
	// appear. The validation pass of a reload must not modify the live tree.
	// A reload of a live tree can run it immediately.
	if (!config->active || config->validating) {
		return cmd_results_new(CMD_DEFER, NULL);
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_DEFER, NULL);
	}

	struct sway_seat *seat = config->handler_context.seat;
	if (!seat) {
		seat = input_manager_current_seat();
	}
	struct sway_workspace *workspace = config->handler_context.workspace;
	if (!workspace) {
		workspace = seat_get_focused_workspace(seat);
	}
	if (!workspace || !workspace->virtual_output) {
		return cmd_results_new(CMD_FAILURE,
				"This command only works when called from a workspace");
	}
	struct sway_virtual_output *vo = workspace->virtual_output;
	struct sway_output *output = vo->output;

	if (strcasecmp(argv[0], "toggle") == 0) {
		if (argc > 2) {
			return cmd_results_new(CMD_INVALID, "%s", expected);
		}
		if (argc == 2) {
			const char *value = argv[1];
			bool enable = strcasecmp(value, "on") == 0 ||
				strcasecmp(value, "yes") == 0 ||
				strcasecmp(value, "true") == 0;
			bool disable = strcasecmp(value, "off") == 0 ||
				strcasecmp(value, "no") == 0 ||
				strcasecmp(value, "false") == 0;
			if (!enable && !disable) {
				return cmd_results_new(CMD_INVALID, "%s", expected);
			}
			if (output->maximize_virtual_output != enable) {
				virtual_output_toggle_maximize(output);
			}
		} else {
			virtual_output_toggle_maximize(output);
		}
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (strcasecmp(argv[0], "split") != 0 || argc > 4) {
		return cmd_results_new(CMD_INVALID, "%s", expected);
	}

	enum sway_virtual_output_split split = VIRTUAL_OUTPUT_SPLIT_VERTICAL;
	double fraction = 0.5;
	int gap = 0;
	if (argc > 1) {
		if (strcasecmp(argv[1], "h") == 0 ||
				strcasecmp(argv[1], "horizontal") == 0 ||
				strcasecmp(argv[1], "up") == 0 ||
				strcasecmp(argv[1], "down") == 0) {
			split = VIRTUAL_OUTPUT_SPLIT_HORIZONTAL;
		} else if (strcasecmp(argv[1], "v") == 0 ||
				strcasecmp(argv[1], "vertical") == 0 ||
				strcasecmp(argv[1], "left") == 0 ||
				strcasecmp(argv[1], "right") == 0) {
			split = VIRTUAL_OUTPUT_SPLIT_VERTICAL;
		} else if (strcasecmp(argv[1], "reset") == 0) {
			virtual_output_split_reset(vo);
			return cmd_results_new(CMD_SUCCESS, NULL);
		} else {
			return cmd_results_new(CMD_INVALID, "%s", expected);
		}
		if (argc > 2) {
			char *end;
			fraction = strtod(argv[2], &end);
			if (*end) {
				return cmd_results_new(CMD_INVALID, "%s", expected);
			}
			fraction = fmin(fmax(0.1, fraction), 0.9);
			if (argc > 3) {
				gap = strtol(argv[3], &end, 10);
				if (*end) {
					return cmd_results_new(CMD_INVALID, "%s", expected);
				}
				if (gap < 0) {
					gap = 0;
				}
			}
		}
	}
	virtual_output_split(vo, split, fraction, gap);
	return cmd_results_new(CMD_SUCCESS, NULL);
}
