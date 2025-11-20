#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/tree/view.h"

/*
 *  We can set the scale, modify or reset it.
 *  scale_content <exact number|increment number|reset>
 */
struct cmd_results *cmd_scale_content(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_container *container = config->handler_context.container;
	if (!container || !container->view) {
		return cmd_results_new(CMD_INVALID, "Need a window to run scale_content");
	}

	if (container->pending.fullscreen_mode != FULLSCREEN_NONE ||
		container->pending.fullscreen_layout == FULLSCREEN_ENABLED) {
		return cmd_results_new(CMD_INVALID, "Can't run scale_content on fullscreen containers");
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "scale_content", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	int fail = 0;
	if (strcasecmp(argv[0], "exact") == 0) {
		if (argc < 2) {
			fail = 1;
		} else {
			double number = strtod(argv[1], NULL);
			view_set_content_scale(container->view, number);
		}
	} else if (strcasecmp(argv[0], "increment") == 0 || strcasecmp(argv[0], "incr") == 0) {
		if (argc < 2) {
			fail = 1;
		} else {
			double number = strtod(argv[1], NULL);
			view_increment_content_scale(container->view, number);
		}
	} else if (strcasecmp(argv[0], "reset") == 0) {
		view_reset_content_scale(container->view);
	} else {
		fail = 1;
	}

	if (fail) {
		const char usage[] = "Expected 'scale_content <exact number|increment number|reset>'";
		return cmd_results_new(CMD_INVALID, "%s", usage);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}
