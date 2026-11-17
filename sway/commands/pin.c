#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/edges.h>
#include "sway/commands.h"
#include "sway/tree/arrange.h"
#include "sway/tree/layout.h"
#include "sway/tree/workspace.h"
#include "sway/output.h"
#include "sway/desktop/animate.h"

struct cmd_results *cmd_pin(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_container *current = config->handler_context.container;
	if (!current) {
		return cmd_results_new(CMD_INVALID, "Need a container to pin");
	}
	if (container_is_floating(current)) {
		return cmd_results_new(CMD_INVALID, "Cannot pin floating containers, use sticky for that");
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "pin", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	enum sway_layout_pin pos;
	if (strcasecmp(argv[0], "beg") == 0 || strcasecmp(argv[0], "beginning") == 0) {
		pos = PIN_BEGINNING;
	} else if (strcasecmp(argv[0], "end") == 0) {
		pos = PIN_END;
	} else {
		return cmd_results_new(CMD_INVALID, "Unknown argument %s, expected 'pin <beginning|end>'", argv[0]);
	}

	layout_pin_set(config->handler_context.workspace, current, pos);


	return cmd_results_new(CMD_SUCCESS, NULL);
}
