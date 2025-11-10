#include "sway/commands.h"
#include "sway/config.h"
#include "log.h"

struct cmd_results *cmd_titlebar_border_thickness(int argc, char **argv) {
	sway_log(SWAY_INFO, "Warning: titlebar_border_thickness is deprecated.");
	if (config->reading) {
		config_add_swaynag_warning("titlebar_border_thickness is deprecated. "
			"Title bars can be rounded (see titlebar_border_radius), but don't have visible borders any more. ");
	}
	return cmd_results_new(CMD_FAILURE, "Invalid command");
}
