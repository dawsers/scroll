#include "sway/commands.h"
#include "sway/config.h"
#include "util.h"

struct cmd_results *cmd_cursor_shake_magnify(int argc, char **argv) {
	struct cmd_results *error =
		checkarg(argc, "cursor_shake_magnify", EXPECTED_EQUAL_TO, 1);
	if (error) {
		return error;
	}
	config->cursor_shake_magnify = parse_boolean(argv[0], config->cursor_shake_magnify);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_cursor_shake_magnify_sensitivity(int argc, char **argv) {
	struct cmd_results *error =
		checkarg(argc, "cursor_shake_magnify_sensitivity", EXPECTED_EQUAL_TO, 1);
	if (error) {
		return error;
	}
	const char *expected = "Expected 'cursor_shake_magnify_sensitivity <number (0 to 1)>'";
	char *end;
	const double sensitivity = strtod(argv[0], &end);
	if (*end || sensitivity < 0.0 || sensitivity > 1.0) {
		return cmd_results_new(CMD_INVALID, "%s", expected);
	}
	config->cursor_shake_magnify_sensitivity = sensitivity;
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_workspace_next_on_output_create_empty(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "workspace_next_on_output_create_empty", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	config->workspace_next_on_output_create_empty = parse_boolean(argv[0], config->workspace_next_on_output_create_empty);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_xdg_activation_force(int argc, char **argv) {
	struct cmd_results *error =
		checkarg(argc, "xdg_activation_force", EXPECTED_EQUAL_TO, 1);
	if (error) {
		return error;
	}
	config->xdg_activation_force = parse_boolean(argv[0], config->xdg_activation_force);
	return cmd_results_new(CMD_SUCCESS, NULL);
}
