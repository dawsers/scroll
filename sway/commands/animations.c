#include <strings.h>
#include "sway/desktop/animate.h"
#include "sway/commands.h"
#include "sway/log.h"
#include "util.h"

// must be in order for the bsearch
static const struct cmd_handler animations_config_handlers[] = {
	{ "crossfade", animations_cmd_crossfade },
	{ "default", animations_cmd_default },
	{ "enabled", animations_cmd_enabled },
	{ "fade_in", animations_cmd_fade_in },
	{ "fade_out", animations_cmd_fade_out },
	{ "gesture", animations_cmd_gesture },
	{ "jump", animations_cmd_jump },
	{ "layer_shell", animations_cmd_layer_shell },
	{ "overview", animations_cmd_overview },
	{ "style", animations_cmd_style },
	{ "window_fullscreen", animations_cmd_window_fullscreen },
	{ "window_move", animations_cmd_window_move },
	{ "window_move_float", animations_cmd_window_move_float },
	{ "window_open", animations_cmd_window_open },
	{ "window_size", animations_cmd_window_size },
	{ "workspace_switch", animations_cmd_workspace_switch },
};

struct cmd_results *cmd_animations_enable(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "animations_enable", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	struct sway_animation_config *config = animation_get_config();
	config->enabled = parse_boolean(argv[0], config->enabled);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *animations_cmd_enabled(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "enabled", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	struct sway_animation_config *config = animation_get_config();
	config->enabled = parse_boolean(argv[0], config->enabled);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *animations_cmd_crossfade(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "crossfade", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	struct sway_animation_config *config = animation_get_config();
	config->crossfade = parse_boolean(argv[0], config->crossfade);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *animations_cmd_style(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "style", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	struct sway_animation_config *config = animation_get_config();
	if (strcasecmp(argv[0], "clip") == 0) {
		config->style = ANIM_STYLE_CLIP;
	} else if (strcasecmp(argv[0], "scale") == 0) {
		config->style = ANIM_STYLE_SCALE;
	} else {
		return cmd_results_new(CMD_INVALID, "Expected 'animations style <clip|scale>' ");
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

// Parse an animation description, and assign it to an animation type
static struct cmd_results *parse_animation_spec(int argc, char **argv,
		struct sway_animation_spec **spec) {
	if (argc >= 2 && strcasecmp(argv[1], "spring") == 0) {
		if (argc < 4) {
			return cmd_results_new(CMD_INVALID,
				"Expected '<yes|no> spring <response_ms> <damping_ratio>'");
		}
		const double response = strtod(argv[2], NULL);
		const double damping = strtod(argv[3], NULL);
		if (response <= 0.0 || damping <= 0.0) {
			return cmd_results_new(CMD_INVALID,
				"The response and the damping ratio of a spring must be positive");
		}
		*spec = animation_spec_create_spring(response, damping);
		return NULL;
	}
	if (argc >= 2 && strcasecmp(argv[1], "decay") == 0) {
		if (argc < 3) {
			return cmd_results_new(CMD_INVALID,
				"Expected '<yes|no> decay <rate>'");
		}
		const double rate = strtod(argv[2], NULL);
		if (rate <= 0.0) {
			return cmd_results_new(CMD_INVALID,
				"The rate of a decay must be positive");
		}
		*spec = animation_spec_create_decay(rate);
		return NULL;
	}

	// A duration and Bezier curves, all of them optional
	uint32_t duration = argc >= 2 ? strtol(argv[1], NULL, 10) : 0;
	int arg = 2;
	uint32_t var_order = 0, off_order = 0;
	list_t *var_list = NULL, *off_list = NULL;
	double off_scale = 0.0;
	bool var_simple = false;
	bool error = false;
	while (arg < argc) {
		if (strcasecmp(argv[arg], "var") == 0 && arg + 2 < argc) {
			++arg;
			if (strcasecmp(argv[arg], "simple") == 0) {
				var_simple = true;
				var_order = 3;
			} else {
				var_order = strtol(argv[arg], NULL, 10);
			}
			++arg;
			var_list = parse_double_array(argv[arg++]);
			if (!var_list) {
				error = true;
			}
		} else if (strcasecmp(argv[arg], "off") == 0 && arg + 3 < argc) {
			++arg;
			off_scale = strtod(argv[arg++], NULL);
			off_order = strtol(argv[arg++], NULL, 10);
			off_list = parse_double_array(argv[arg++]);
			if (!off_list) {
				error = true;
			}
		} else {
			++arg;
		}
	}
	struct sway_animation_curve *curve = NULL;
	if (var_list || off_list) {
		curve = create_animation_curve(duration, var_order, var_list, var_simple, off_scale, off_order, off_list);
		if (var_list) {
			list_free_items_and_destroy(var_list);
		}
		if (off_list) {
			list_free_items_and_destroy(off_list);
		}
		if (!curve) {
			return cmd_results_new(CMD_FAILURE, "Invalid animation curve");
		}
	} else if (error) {
		return cmd_results_new(CMD_FAILURE, "Error parsing animations array");
	} else {
		curve = create_animation_curve(duration, 0, NULL, false, 0.0, 0, NULL);
	}
	struct sway_animation_path *path = animation_path_create();
	animation_path_add_curve(path, curve);
	*spec = animation_spec_create_path(path);
	return NULL;
}

// Assign an animation description to an animation type
static struct cmd_results *animations_cmd_type(int argc, char **argv,
		const char *name, enum sway_animation_type type) {
	struct cmd_results *error;
	if ((error = checkarg(argc, name, EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	struct sway_animation_spec *spec = NULL;
	if ((error = parse_animation_spec(argc, argv, &spec))) {
		return error;
	}
	if (!spec) {
		return cmd_results_new(CMD_FAILURE, "Unable to create the animation");
	}
	if (!parse_boolean(argv[0], true)) {
		// The type is not animated
		animation_spec_unref(spec);
		spec = NULL;
	}
	animation_set_spec(type, spec);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *animations_cmd_default(int argc, char **argv) {
	return animations_cmd_type(argc, argv, "default", ANIMATION_DEFAULT);
}

struct cmd_results *animations_cmd_window_open(int argc, char **argv) {
	return animations_cmd_type(argc, argv, "window_open", ANIMATION_WINDOW_OPEN);
}

struct cmd_results *animations_cmd_window_move(int argc, char **argv) {
	return animations_cmd_type(argc, argv, "window_move", ANIMATION_WINDOW_MOVE);
}

struct cmd_results *animations_cmd_window_move_float(int argc, char **argv) {
	return animations_cmd_type(argc, argv, "window_move_float", ANIMATION_WINDOW_MOVE_FLOAT);
}

struct cmd_results *animations_cmd_window_fullscreen(int argc, char **argv) {
	return animations_cmd_type(argc, argv, "window_fullscreen", ANIMATION_WINDOW_FULLSCREEN);
}

struct cmd_results *animations_cmd_window_size(int argc, char **argv) {
	return animations_cmd_type(argc, argv, "window_size", ANIMATION_WINDOW_SIZE);
}

struct cmd_results *animations_cmd_workspace_switch(int argc, char **argv) {
	return animations_cmd_type(argc, argv, "workspace_switch", ANIMATION_WORKSPACE_SWITCH);
}

struct cmd_results *animations_cmd_overview(int argc, char **argv) {
	return animations_cmd_type(argc, argv, "overview", ANIMATION_OVERVIEW);
}

struct cmd_results *animations_cmd_jump(int argc, char **argv) {
	return animations_cmd_type(argc, argv, "jump", ANIMATION_JUMP);
}

struct cmd_results *animations_cmd_layer_shell(int argc, char **argv) {
	return animations_cmd_type(argc, argv, "layer_shell", ANIMATION_LAYER_SHELL);
}

struct cmd_results *animations_cmd_fade_in(int argc, char **argv) {
	return animations_cmd_type(argc, argv, "fade_in", ANIMATION_FADE_IN);
}

struct cmd_results *animations_cmd_fade_out(int argc, char **argv) {
	return animations_cmd_type(argc, argv, "fade_out", ANIMATION_FADE_OUT);
}

struct cmd_results *animations_cmd_gesture(int argc, char **argv) {
	return animations_cmd_type(argc, argv, "gesture", ANIMATION_GESTURE);
}

struct cmd_results *cmd_animations(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "animations", EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	sway_log(SWAY_DEBUG, "entering animations block: %s", argv[0]);

	struct cmd_results *res;

	if (find_handler(argv[0], animations_config_handlers,
			sizeof(animations_config_handlers))) {
		if (config->reading) {
			res = config_subcommand(argv, argc,
				animations_config_handlers, sizeof(animations_config_handlers));
		} else {
			res = cmd_results_new(CMD_FAILURE,
				"Can only be used in config file.");
		}
	} else {
		res = cmd_results_new(CMD_FAILURE,
			"Unknown animations subcommand.");
	}

	return res;
}
