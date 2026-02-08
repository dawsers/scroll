#include <stdbool.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/tree/container.h"
#include "sway/tree/layout.h"
#include "util.h"

/*
 *  Trailmarks
 */
struct cmd_results *cmd_trailmark(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there are no outputs connected.");
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "trailmark", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	if (strcasecmp(argv[0], "toggle") == 0) {
		struct sway_container *con = config->handler_context.container;
		struct sway_view *view;
		if (con->view) {
			view = con->view;
		} else {
			con = con->current.focused_inactive_child;
			if (con) {
				view = con->view;
			} else {
				con = con->current.children->items[0];
				view = con->view;
			}
		}
		layout_trailmark_toggle(view);
	} else if (strcasecmp(argv[0], "next") == 0) {
		layout_trailmark_next();
	} else if (strcasecmp(argv[0], "prev") == 0) {
		layout_trailmark_prev();
	} else if (strcasecmp(argv[0], "jump") == 0) {
		if (root->fullscreen_global) {
			return cmd_results_new(CMD_INVALID,
				"Can't run this command while in global fullscreen mode.");
		}
		layout_jump_trailmark(config->handler_context.workspace);
	} else {
		return cmd_results_new(CMD_INVALID, "Unknown argument %s for command 'trailmark", argv[0]);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}

/*
 *  Trail
 */
struct cmd_results *cmd_trail(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there are no outputs connected.");
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "trail", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	if (strcasecmp(argv[0], "number") == 0) {
		if (argc < 2) {
			return cmd_results_new(CMD_INVALID, "Expected trail number");
		}
		long n;
		if (!parse_integer(argv[1], &n)) {
			return cmd_results_new(CMD_INVALID, "Invalid trail number '%s'", argv[1]);
		}
		layout_trail_number(n);
	} else if (strcasecmp(argv[0], "new") == 0) {
		layout_trail_new();
	} else if (strcasecmp(argv[0], "next") == 0) {
		layout_trail_next();
	} else if (strcasecmp(argv[0], "prev") == 0) {
		layout_trail_prev();
	} else if (strcasecmp(argv[0], "delete") == 0) {
		layout_trail_delete();
	} else if (strcasecmp(argv[0], "clear") == 0) {
		layout_trail_clear();
	} else if (strcasecmp(argv[0], "to_selection") == 0) {
		layout_trail_to_selection();
	} else {
		return cmd_results_new(CMD_INVALID, "Unknown argument %s for command 'trail", argv[0]);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
