#include "log.h"
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/tree/container.h"

struct cmd_results *cmd_default_floating_border(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "default_floating_border",
					EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	if (strcmp(argv[0], "none") == 0) {
		config->floating_border = B_NONE;
	} else if (strcmp(argv[0], "normal") == 0) {
		config->floating_border = B_NORMAL;
	} else if (strcmp(argv[0], "pixel") == 0) {
		config->floating_border = B_PIXEL;
	} else if (strcmp(argv[0], "csd") == 0) {
		config->floating_border = B_CSD;
	} else {
		return cmd_results_new(CMD_INVALID,
				"Expected 'default_floating_border <none|normal|pixel|csd>' "
				"or 'default_floating_border <normal|pixel> <px>'");
	}
	if (argc == 2) {
		config->floating_border_thickness = atoi(argv[1]);
	}
	if (config->border == B_CSD || config->border == B_NONE) {
		config->border_thickness = 0;
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
