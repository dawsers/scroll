#include <strings.h>
#include "sway/commands.h"
#include "sway/tree/root.h"
#include "sway/tree/workspace.h"
#include "sway/tree/space.h"

static const char expected_syntax[] =
	"Expected 'space  save|load|restore name'";

struct cmd_results *cmd_space(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "space", EXPECTED_AT_LEAST, 2))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_workspace *workspace = config->handler_context.workspace;
	if (!workspace) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command when there is no active workspace.");
	}

	if (workspace->fullscreen && workspace->fullscreen->pending.fullscreen_mode != FULLSCREEN_NONE) {
		container_fullscreen_disable(workspace->fullscreen);
	}

	if (strcasecmp(argv[0], "load") == 0) {
		space_load(workspace, argv[1], SPACE_RESTORE_LOAD);
	} else if (strcasecmp(argv[0], "save") == 0) {
		space_save(workspace, argv[1]);
	} else if (strcasecmp(argv[0], "restore") == 0) {
		space_load(workspace, argv[1], SPACE_RESTORE_CLOSE);
	} else if (strcasecmp(argv[0], "restore_hide") == 0) {
		space_load(workspace, argv[1], SPACE_RESTORE_HIDE);
	} else {
		return cmd_results_new(CMD_INVALID, "%s", expected_syntax);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
