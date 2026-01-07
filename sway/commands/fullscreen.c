#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"
#include "util.h"
#include "sway/desktop/animation.h"

struct cmd_results *cmd_fullscreen_on_request(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "fullscreen_on_request", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	if (strcasecmp(argv[0], "default") == 0) {
		config->fullscreen_on_request = FULLSCREEN_REQUEST_DEFAULT;
	} else if (strcasecmp(argv[0], "layout") == 0) {
		config->fullscreen_on_request = FULLSCREEN_REQUEST_LAYOUT;
	} else {
		return cmd_results_new(CMD_INVALID,
			"Expected fullscreen_on_request default|layout.");
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

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

// fullscreen [enable|disable|toggle] [global|application|layout]
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

	bool global = false;
	bool layout = false;
	bool application = false;
	bool enable = container->pending.fullscreen_mode == FULLSCREEN_NONE;
	bool enable_application = container->pending.fullscreen_application == FULLSCREEN_DISABLED;
	bool enable_layout = container->pending.fullscreen_layout == FULLSCREEN_DISABLED;

	if (argc >= 1) {
		if (strcasecmp(argv[0], "global") == 0) {
			global = true;
		} else if (strcasecmp(argv[0], "application") == 0) {
			application = true;
		} else if (strcasecmp(argv[0], "layout") == 0) {
			layout = true;
		} else {
			enable = parse_boolean(argv[0], !enable);
			enable_application = parse_boolean(argv[0], !enable_application);
			enable_layout = parse_boolean(argv[0], !enable_layout);
		}
	}

	if (argc >= 2) {
		if (strcasecmp(argv[1], "global") == 0) {
			global = true;
		} else if (strcasecmp(argv[1], "application") == 0) {
			application = true;
		} else if (strcasecmp(argv[1], "layout") == 0) {
			layout = true;
		}
	}

	if (layout) {
		if (container_is_floating(container) || container->pending.fullscreen_mode != FULLSCREEN_NONE) {
			return cmd_results_new(CMD_INVALID,
				"Can't call fullscreen layout on a floating or fullscreen container.");
		}
		container_set_fullscreen_layout(container, enable_layout ? FULLSCREEN_ENABLED : FULLSCREEN_DISABLED);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (container->pending.fullscreen_layout == FULLSCREEN_ENABLED) {
		return cmd_results_new(CMD_INVALID,
			"Can't call fullscreen on a fullscreen layout container.");
	}

	if (application) {
		container_set_fullscreen_application(container, enable_application ? FULLSCREEN_ENABLED : FULLSCREEN_DISABLED);
		return cmd_results_new(CMD_SUCCESS, NULL);
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
