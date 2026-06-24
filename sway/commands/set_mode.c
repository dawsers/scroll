#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/edges.h>

#include "sway/commands.h"
#include "sway/desktop/transaction.h"
#include "sway/tree/arrange.h"
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

		if (strcasecmp(argv[i], "align_horiz_initial") == 0) {
			layout_modifiers_set_align_horiz_policy(current, ALIGN_POLICY_INITIAL);
			success = true;
		} else if (strcasecmp(argv[i], "align_horiz_if_fits") == 0) {
			layout_modifiers_set_align_horiz_policy(current, ALIGN_POLICY_IF_FIT);
			success = true;
		}
		if (strcasecmp(argv[i], "align_vert_initial") == 0) {
			layout_modifiers_set_align_vert_policy(current, ALIGN_POLICY_INITIAL);
			success = true;
		} else if (strcasecmp(argv[i], "align_vert_if_fits") == 0) {
			layout_modifiers_set_align_vert_policy(current, ALIGN_POLICY_IF_FIT);
			success = true;
		}

		if (strcasecmp(argv[i], "align_horiz_left") == 0) {
			layout_modifiers_set_align_horiz(current, ALIGN_HORIZ_LEFT);
			success = true;
		} else if (strcasecmp(argv[i], "align_horiz_center") == 0) {
			layout_modifiers_set_align_horiz(current, ALIGN_HORIZ_CENTER);
			success = true;
		} else if (strcasecmp(argv[i], "align_horiz_right") == 0) {
			layout_modifiers_set_align_horiz(current, ALIGN_HORIZ_RIGHT);
			success = true;
		}

		if (strcasecmp(argv[i], "align_vert_top") == 0) {
			layout_modifiers_set_align_vert(current, ALIGN_VERT_TOP);
			success = true;
		} else if (strcasecmp(argv[i], "align_vert_middle") == 0) {
			layout_modifiers_set_align_vert(current, ALIGN_VERT_MIDDLE);
			success = true;
		} else if (strcasecmp(argv[i], "align_vert_bottom") == 0) {
			layout_modifiers_set_align_vert(current, ALIGN_VERT_BOTTOM);
			success = true;
		}
	}

	if (success) {
		if (update_container && container) {
			container_update(container);
		}
		arrange_workspace(current);
		transaction_commit_dirty();
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	const char usage[] = "Expected 'set_mode [<h|v|t> <after|before|end|beg> "
						 "<focus|nofocus> "
						 "<center_horiz|nocenter_horiz> <center_vert|nocenter_vert> "
						 "<reorder_auto|noreorder_auto> "
						 "<align_horiz_initial|align_horiz_if_fits> "
						 "<align_vert_initial|align_vert_if_fits> "
						 "<align_horiz_left|align_horiz_center|align_horiz_right> "
						 "<align_vert_top|align_vert_middle|align_vert_bottom>]'";

	return cmd_results_new(CMD_INVALID, "%s", usage);
}
