#include <strings.h>
#include "util.h"
#include "sway/commands.h"
#include "sway/config.h"

struct cmd_results *output_cmd_layout_default_mode(int argc, char **argv) {
	if (!config->handler_context.output_config) {
		return cmd_results_new(CMD_FAILURE, "Missing output config");
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, "Missing layout_type argument.");
	}

	int success = parse_into_modifiers(argc, argv, &config->handler_context.output_config->layout_default_modifiers);
	if (success > 0) {
		config->handler_context.leftovers.argc = argc - success;
		config->handler_context.leftovers.argv = argv + success;
		return NULL;
	}
	return cmd_results_new(CMD_INVALID, "Invalid layout_default_mode.");
}

struct cmd_results *output_cmd_layout_type(int argc, char **argv) {
	if (!config->handler_context.output_config) {
		return cmd_results_new(CMD_FAILURE, "Missing output config");
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, "Missing layout_type argument.");
	}
	
	if (strcasecmp(argv[0], "horizontal") == 0) {
		config->handler_context.output_config->layout_type = L_HORIZ;
	} else if (strcasecmp(argv[0], "vertical") == 0) {
		config->handler_context.output_config->layout_type = L_VERT;
	} else {
		config->handler_context.output_config->layout_type = L_HORIZ;
	}

	config->handler_context.leftovers.argc = argc - 1;
	config->handler_context.leftovers.argv = argv + 1;
	return NULL;
}

struct cmd_results *output_cmd_layout_default_height(int argc, char **argv) {
	if (!config->handler_context.output_config) {
		return cmd_results_new(CMD_FAILURE, "Missing output config");
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, "Missing layout_default_height argument.");
	}

	char *end;
	config->handler_context.output_config->layout_default_height = strtod(*argv, &end);
	if (*end) {
		return cmd_results_new(CMD_INVALID, "Invalid layout_default_height.");
	}

	config->handler_context.leftovers.argc = argc - 1;
	config->handler_context.leftovers.argv = argv + 1;
	return NULL;
}

struct cmd_results *output_cmd_layout_default_width(int argc, char **argv) {
	if (!config->handler_context.output_config) {
		return cmd_results_new(CMD_FAILURE, "Missing output config");
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, "Missing layout_default_width argument.");
	}

	char *end;
	config->handler_context.output_config->layout_default_width = strtod(*argv, &end);
	if (*end) {
		return cmd_results_new(CMD_INVALID, "Invalid layout_default_width.");
	}

	config->handler_context.leftovers.argc = argc - 1;
	config->handler_context.leftovers.argv = argv + 1;
	return NULL;
}

struct cmd_results *output_cmd_layout_heights(int argc, char **argv) {
	if (!config->handler_context.output_config) {
		return cmd_results_new(CMD_FAILURE, "Missing output config");
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, "Missing layout_heights argument.");
	}

	list_t *list = parse_double_array(argv[0]);
	if (list) {
		list_free_items_and_destroy(config->handler_context.output_config->layout_heights);
		config->handler_context.output_config->layout_heights = list;
		config->handler_context.leftovers.argc = argc - 1;
		config->handler_context.leftovers.argv = argv + 1;
		return NULL;
	} else {
		return cmd_results_new(CMD_FAILURE, "Error parsing layout_heights array");
	}
}

struct cmd_results *output_cmd_layout_widths(int argc, char **argv) {
	if (!config->handler_context.output_config) {
		return cmd_results_new(CMD_FAILURE, "Missing output config");
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, "Missing layout_widths argument.");
	}

	list_t *list = parse_double_array(argv[0]);
	if (list) {
		list_free_items_and_destroy(config->handler_context.output_config->layout_widths);
		config->handler_context.output_config->layout_widths = list;
		config->handler_context.leftovers.argc = argc - 1;
		config->handler_context.leftovers.argv = argv + 1;
		return NULL;
	} else {
		return cmd_results_new(CMD_FAILURE, "Error parsing layout_widths array");
	}
}
