#include <strings.h>
#include "sway/tree/container.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/output.h"
#include "sway/commands.h"

static void close_container_iterator(struct sway_container *con, void *data) {
	if (data) {
		struct sway_container *focused = data;
		if (con == focused) {
			return;
		}
	}
	if (con->view) {
		view_close(con->view);
	}
}

// kill <focused>
// kill unfocused
// kill all (every workspace and scratchpad)
struct cmd_results *cmd_kill(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	if (argc == 0 || (argc == 1 && strcasecmp(argv[0], "focused") == 0)) {
		// kill focused
		struct sway_container *con = config->handler_context.container;
		if (con) {
			close_container_iterator(con, NULL);
			container_for_each_child(con, close_container_iterator, NULL);
		}
	} else {
		if (strcasecmp(argv[0], "unfocused") == 0) {
			struct sway_container *con = config->handler_context.container;
			if (con->view) {
				struct sway_workspace *ws = config->handler_context.workspace;
				if (ws) {
					workspace_for_each_container(ws, close_container_iterator, con);
				}
			}
		} else if (strcasecmp(argv[0], "all") == 0) {
			for (int i = 0; i < root->outputs->length; ++i) {
				struct sway_output *output = root->outputs->items[i];
				for (int j = 0; j < output->workspaces->length; ++j) {
					struct sway_workspace *ws = output->workspaces->items[j];
					workspace_for_each_container(ws, close_container_iterator, NULL);
				}
			}
			for (int i = 0; i < root->scratchpad->length; ++i) {
				struct sway_container *con = root->scratchpad->items[i];
				close_container_iterator(con, NULL);
			}
		} else {
			return cmd_results_new(CMD_INVALID, "Unknown command: kill %s", argv[0]);
		}
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
