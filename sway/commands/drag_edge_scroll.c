#include <stdlib.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "util.h"

static struct cmd_results *parse_value(const char *arg, int min, int *value) {
	char *inv;
	long v = strtol(arg, &inv, 10);
	if (*arg == '\0' || *inv != '\0' || v < min || v > 100000) {
		return cmd_results_new(CMD_INVALID, "Invalid value '%s'", arg);
	}
	*value = v;
	return NULL;
}

struct cmd_results *cmd_drag_edge_scroll(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "drag_edge_scroll", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	if (strcasecmp(argv[0], "zone") == 0 || strcasecmp(argv[0], "speed") == 0) {
		if ((error = checkarg(argc, "drag_edge_scroll", EXPECTED_EQUAL_TO, 2))) {
			return error;
		}
		int *value = strcasecmp(argv[0], "zone") == 0 ?
			&config->drag_edge_scroll_zone : &config->drag_edge_scroll_speed;
		if ((error = parse_value(argv[1], 1, value))) {
			return error;
		}
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if ((error = checkarg(argc, "drag_edge_scroll", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	config->drag_edge_scroll = parse_boolean(argv[0], config->drag_edge_scroll);

	return cmd_results_new(CMD_SUCCESS, NULL);
}
