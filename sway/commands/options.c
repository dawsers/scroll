#include "sway/commands.h"
#include "sway/config.h"
#include "util.h"

struct cmd_results *cmd_maximize_if_single(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "maximize_if_single", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	config->maximize_if_single = parse_boolean(argv[0], config->maximize_if_single);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_center_horizontal_if_fits(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "center_horizontal_if_fits", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	config->center_horizontal_if_fits = parse_boolean(argv[0], config->center_horizontal_if_fits);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_center_vertical_if_fits(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "center_vertical_if_fits", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	config->center_vertical_if_fits = parse_boolean(argv[0], config->center_vertical_if_fits);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

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

struct cmd_results *cmd_mouse_resize_tiling_limit(int argc, char **argv) {
	struct cmd_results *error = checkarg(argc, "mouse_resize_tiling_limit", EXPECTED_AT_LEAST, 1);
	if (error) {
		return error;
	}
	config->mouse_resize_tiling_limit = parse_boolean(argv[0], config->mouse_resize_tiling_limit);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_snap_window_gap(int argc, char **argv) {
	struct cmd_results *error = checkarg(argc, "snap_window_gap", EXPECTED_EQUAL_TO, 1);
	if (error) {
		return error;
	}
	long snap_window_gap;
	if (!parse_integer(argv[0], &snap_window_gap)) {
		return cmd_results_new(CMD_INVALID, "Invalid parameter '%s'", argv[1]);
	}

	config->snap_window_gap = snap_window_gap;
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_snap_workspace_gap(int argc, char **argv) {
	struct cmd_results *error = checkarg(argc, "snap_workspace_gap", EXPECTED_EQUAL_TO, 1);
	if (error) {
		return error;
	}
	long snap_workspace_gap;
	if (!parse_integer(argv[0], &snap_workspace_gap)) {
		return cmd_results_new(CMD_INVALID, "Invalid parameter '%s'", argv[1]);
	}

	config->snap_workspace_gap = snap_workspace_gap;
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_snap_respect_gaps_inner(int argc, char **argv) {
	struct cmd_results *error = checkarg(argc, "snap_respect_gaps_inner", EXPECTED_AT_LEAST, 1);
	if (error) {
		return error;
	}
	config->snap_respect_gaps_inner = parse_boolean(argv[0], config->snap_respect_gaps_inner);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_snap_respect_gaps_outer(int argc, char **argv) {
	struct cmd_results *error = checkarg(argc, "snap_respect_gaps_outer", EXPECTED_AT_LEAST, 1);
	if (error) {
		return error;
	}
	config->snap_respect_gaps_outer = parse_boolean(argv[0], config->snap_respect_gaps_outer);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_snap_border_overlap(int argc, char **argv) {
	struct cmd_results *error = checkarg(argc, "snap_border_overlap", EXPECTED_AT_LEAST, 1);
	if (error) {
		return error;
	}
	config->snap_border_overlap = parse_boolean(argv[0], config->snap_border_overlap);
	return cmd_results_new(CMD_SUCCESS, NULL);
}
