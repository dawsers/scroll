#include "sway/commands.h"
#include "sway/log.h"
#include "stringop.h"

struct cmd_results *bar_cmd_swaybar_command(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "scrollbar_command", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	free(config->current_bar->swaybar_command);
	config->current_bar->swaybar_command = join_args(argv, argc);
	sway_log(SWAY_DEBUG, "Using custom scrollbar command: %s",
			config->current_bar->swaybar_command);
	return cmd_results_new(CMD_SUCCESS, NULL);
}
