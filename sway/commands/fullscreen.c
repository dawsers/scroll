#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"
#include "util.h"

struct cmd_results *cmd_fullscreen_movefocus(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "fullscreen_movefocus", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	bool enabled = parse_boolean(argv[0], config->fullscreen_movefocus != FULLSCREEN_MOVEFOCUS_NONE);
	if (enabled) {
		if (argc > 1) {
			if (strcasecmp(argv[1], "follow") == 0) {
				config->fullscreen_movefocus = FULLSCREEN_MOVEFOCUS_FOLLOW;
			} else if (strcasecmp(argv[1], "nofollow") == 0) {
				config->fullscreen_movefocus = FULLSCREEN_MOVEFOCUS_NOFOLLOW;
			} else {
				return cmd_results_new(CMD_INVALID,
					"Expected fullscreen_movefocus <true|false> [follow|nofollow].");
			}
		} else {
			config->fullscreen_movefocus = FULLSCREEN_MOVEFOCUS_FOLLOW;
		}
	} else {
		config->fullscreen_movefocus = FULLSCREEN_MOVEFOCUS_NONE;
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

// fullscreen [enable|disable|toggle] [global]
struct cmd_results *cmd_fullscreen(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "fullscreen", EXPECTED_AT_MOST, 2))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_FAILURE,
				"Can't run this command while there are no outputs connected.");
	}
	struct sway_container *container = config->handler_context.container;

	if (!container) {
		// If the focus is not a container, do nothing successfully
		return cmd_results_new(CMD_SUCCESS, NULL);
	} else if (!container->pending.workspace) {
		// If in the scratchpad, operate on the highest container
		while (container->pending.parent) {
			container = container->pending.parent;
		}
	}

	bool is_fullscreen = container->pending.fullscreen_mode != FULLSCREEN_NONE;
	bool global = false;
	bool enable = !is_fullscreen;

	if (argc >= 1) {
		if (strcasecmp(argv[0], "global") == 0) {
			global = true;
		} else {
			enable = parse_boolean(argv[0], is_fullscreen);
		}
	}

	if (argc >= 2) {
		global = strcasecmp(argv[1], "global") == 0;
	}

	enum sway_fullscreen_mode mode = FULLSCREEN_NONE;
	if (enable) {
		mode = global ? FULLSCREEN_GLOBAL : FULLSCREEN_WORKSPACE;
	}

	container_set_fullscreen(container, mode);
	container_set_fullscreen_container(container, mode != FULLSCREEN_NONE);
	arrange_root();

	return cmd_results_new(CMD_SUCCESS, NULL);
}

// fullscreen_application [enable|disable|toggle]
struct cmd_results *cmd_fullscreen_application(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "fullscreen_application", EXPECTED_AT_MOST, 1))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_FAILURE,
				"Can't run this command while there are no outputs connected.");
	}
	struct sway_container *container = config->handler_context.container;

	if (!container) {
		// If the focus is not a container, do nothing successfully
		return cmd_results_new(CMD_SUCCESS, NULL);
	} else if (!container->pending.workspace) {
		// If in the scratchpad, operate on the highest container
		while (container->pending.parent) {
			container = container->pending.parent;
		}
	}

	bool is_fullscreen;
	enum sway_fullscreen_state mode = container->pending.fullscreen_application;
	if (mode == FULLSCREEN_ENABLED) {
		is_fullscreen = true;
		mode = FULLSCREEN_DISABLED;
	} else {
		is_fullscreen = false;
		mode = FULLSCREEN_ENABLED;
	}

	if (argc >= 1) {
		bool enable = parse_boolean(argv[0], is_fullscreen);
		mode = enable ? FULLSCREEN_ENABLED : FULLSCREEN_DISABLED;
	}

	container_set_fullscreen_application(container, mode);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

