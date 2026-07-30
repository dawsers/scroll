#include <strings.h>
#include "sway/commands.h"
#include "sway/log.h"
#include "util.h"

struct cmd_results *bar_cmd_scroller_indicator(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc,
			"scroller_indicator", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	config->current_bar->scroller_indicator =
		parse_boolean(argv[0], config->current_bar->scroller_indicator);
	if (config->current_bar->scroller_indicator) {
		sway_log(SWAY_DEBUG, "Enabling scroller indicator on bar: %s",
				config->current_bar->id);
	} else {
		sway_log(SWAY_DEBUG, "Disabling scroller indicator on bar: %s",
				config->current_bar->id);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *bar_cmd_trails_indicator(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc,
			"trails_indicator", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	config->current_bar->trails_indicator =
		parse_boolean(argv[0], config->current_bar->trails_indicator);
	if (config->current_bar->trails_indicator) {
		sway_log(SWAY_DEBUG, "Enabling trails indicator on bar: %s",
				config->current_bar->id);
	} else {
		sway_log(SWAY_DEBUG, "Disabling trails indicator on bar: %s",
				config->current_bar->id);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}
