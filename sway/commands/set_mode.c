#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/edges.h>
#include "sway/commands.h"
#include "sway/tree/workspace.h"

struct cmd_results *cmd_set_mode(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_workspace *current = config->handler_context.workspace;
	if (!current) {
		return cmd_results_new(CMD_INVALID, "Need a workspace for set_mode to work");
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "set_mode", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	bool success = false;
	bool update_container = false;
	struct sway_container *container =  config->handler_context.container;
	for (int i = 0; i < argc; ++i) {
		if (strcasecmp(argv[i], "h") == 0) {
			layout_modifiers_set_mode(current, L_HORIZ);
			success = true;
			update_container = true;
		} else if (strcasecmp(argv[i], "v") == 0) {
			layout_modifiers_set_mode(current, L_VERT);
			success = true;
			update_container = true;
		} else if (strcasecmp(argv[i], "t") == 0) {
			if (layout_modifiers_get_mode(current) == L_HORIZ) {
				layout_modifiers_set_mode(current, L_VERT);
				success = true;
				update_container = true;
			} else {
				layout_modifiers_set_mode(current, L_HORIZ);
				success = true;
				update_container = true;
			}
		}
        if (strcasecmp(argv[i], "after") == 0) {
			layout_modifiers_set_insert(current, INSERT_AFTER);
			success = true;
			update_container = true;
		} else if (strcasecmp(argv[i], "before") == 0) {
			layout_modifiers_set_insert(current, INSERT_BEFORE);
			success = true;
			update_container = true;
		} else if (strcasecmp(argv[i], "end") == 0) {
			layout_modifiers_set_insert(current, INSERT_END);
			success = true;
			update_container = true;
		} else if (strcasecmp(argv[i], "beginning") == 0 || strcasecmp(argv[i], "beg") == 0) {
			layout_modifiers_set_insert(current, INSERT_BEGINNING);
			success = true;
			update_container = true;
		}

		if (strcasecmp(argv[i], "nofit") == 0) {
			layout_modifiers_set_fit(current, FIT_NONE);
			success = true;
			update_container = true;
		} else if (strcasecmp(argv[i], "fitsplit") == 0) {
			layout_modifiers_set_fit(current, FIT_SPLIT);
			success = true;
			update_container = true;
		} else if (strcasecmp(argv[i], "fitfraction") == 0) {
			layout_modifiers_set_fit(current, FIT_FRACTION);
			success = true;
			update_container = true;
		}

        if (strcasecmp(argv[i], "focus") == 0) {
			layout_modifiers_set_focus(current, true);
			success = true;
		} else if (strcasecmp(argv[i], "nofocus") == 0) {
			layout_modifiers_set_focus(current, false);
			success = true;
		}

        if (strcasecmp(argv[i], "center_horiz") == 0) {
			layout_modifiers_set_center_horizontal(current, true);
			success = true;
		} else if (strcasecmp(argv[i], "nocenter_horiz") == 0) {
			layout_modifiers_set_center_horizontal(current, false);
			success = true;
		}
        if (strcasecmp(argv[i], "center_vert") == 0) {
			layout_modifiers_set_center_vertical(current, true);
			success = true;
		} else if (strcasecmp(argv[i], "nocenter_vert") == 0) {
			layout_modifiers_set_center_vertical(current, false);
			success = true;
		}

        if (strcasecmp(argv[i], "reorder_auto") == 0) {
			layout_modifiers_set_reorder(current, REORDER_AUTO);
			success = true;
		} else if (strcasecmp(argv[i], "noreorder_auto") == 0) {
			layout_modifiers_set_reorder(current, REORDER_LAZY);
			success = true;
		}
	}

	if (success) {
		if (update_container && container) {
			container_update(container);
		}
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	const char usage[] = "Expected 'set_mode [<h|v|t> <after|before|end|beg> <nofit|fitsplit|fitfraction> <focus|nofocus> <center_horiz|nocenter_horiz> <center_vert|nocenter_vert> <reorder_auto|noreorder_auto>]'";

	return cmd_results_new(CMD_INVALID, "%s", usage);
}
