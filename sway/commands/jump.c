#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/tree/workspace.h"
#include "util.h"

struct cmd_results *cmd_jump_labels_color(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "jump_labels_color", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	uint32_t color;
	parse_color(argv[0], &color);
	color_to_rgba(config->jump_labels_color, color);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_jump_labels_background(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "jump_labels_background", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	uint32_t color;
	parse_color(argv[0], &color);
	color_to_rgba(config->jump_labels_background, color);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_jump_labels_scale(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "jump_labels_scale", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	config->jump_labels_scale = fmin(fmax(strtod(argv[0], NULL), 0.1), 1.0);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_jump_labels_keys(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "jump_labels_keys", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	list_free_items_and_destroy(config->jump_labels_keys_text);
	config->jump_labels_keys_text = create_list();
	for (uint32_t i = 0; i < strlen(argv[0]); ++i) {
		char label[2] = { argv[0][i], 0 };
		list_add(config->jump_labels_keys_text, strdup(label));
	}
	if (argc > 1) {
		list_t *list = parse_string_array(argv[1]);
		if (list) {
			list_free_items_and_destroy(config->jump_labels_keys);
			config->jump_labels_keys = list;
		} else {
			return cmd_results_new(CMD_FAILURE, "Error parsing jump_labels_keys symbols array");
		}
	} else {
		list_free_items_and_destroy(config->jump_labels_keys);
		config->jump_labels_keys = create_list();
		for (int i = 0; i < config->jump_labels_keys_text->length; ++i) {
			list_add(config->jump_labels_keys, strdup(config->jump_labels_keys_text->items[i]));
		}
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

/*
 *  jump
 */
struct cmd_results *cmd_jump(int argc, char **argv) {
	if (root->fullscreen_global) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while in global fullscreen mode.");
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there are no outputs connected.");
	}

	if (argc > 0) {
		if(strcasecmp(argv[0], "workspaces") == 0) {
			layout_jump_workspaces();
		} else if (strcasecmp(argv[0], "container") == 0) {
			struct sway_container *con = config->handler_context.container;
			if (!con) {
				return cmd_results_new(CMD_INVALID, "No container selected.");
			}
			layout_jump_container(con);
		} else if(strcasecmp(argv[0], "all") == 0) {
			layout_jump_all();
		} else if(strcasecmp(argv[0], "floating") == 0) {
			layout_jump_floating();
		} else if(strcasecmp(argv[0], "tiling") == 0) {
			layout_jump();
		} else {
			return cmd_results_new(CMD_INVALID, "Invalid argument %s for command 'jump'.", argv[0]);
		}
	} else {
		layout_jump();
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
