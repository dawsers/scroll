#include "sway/config.h"
#include "sway/commands.h"
#include "util.h"

struct cmd_results *cmd_xwayland_output_scale(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "xwayland_output_scale", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
#ifdef WLR_HAS_XWAYLAND
	bool xwayland_output_scale = parse_boolean(argv[0], config->xwayland_output_scale);

	// config->xwayland_output_scale is reset to the previous value on reload in
	// load_main_config()
	if (config->reloading && config->xwayland_output_scale != xwayland_output_scale) {
		return cmd_results_new(CMD_FAILURE,
				"xwayland_output_scale can only be set at launch");
	}
	config->xwayland_output_scale = xwayland_output_scale;
#else
	sway_log(SWAY_INFO, "Ignoring `xwayland_output_scale` command, "
		"scroll hasn't been built with Xwayland support");
#endif
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_xwayland(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "xwayland", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

#ifdef WLR_HAS_XWAYLAND
	enum xwayland_mode xwayland;
	if (strcmp(argv[0], "force") == 0) {
		xwayland = XWAYLAND_MODE_IMMEDIATE;
	} else if (parse_boolean(argv[0], true)) {
		xwayland = XWAYLAND_MODE_LAZY;
	} else {
		xwayland = XWAYLAND_MODE_DISABLED;
	}

	// config->xwayland is reset to the previous value on reload in
	// load_main_config()
	if (config->reloading && config->xwayland != xwayland) {
		return cmd_results_new(CMD_FAILURE,
				"xwayland can only be enabled/disabled at launch");
	}
	config->xwayland = xwayland;
#else
	sway_log(SWAY_INFO, "Ignoring `xwayland` command, "
		"scroll hasn't been built with Xwayland support");
#endif

	return cmd_results_new(CMD_SUCCESS, NULL);
}
