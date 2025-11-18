#include <wlr/util/edges.h>
#include "sway/commands.h"
#include "sway/tree/arrange.h"
#include "sway/tree/layout.h"
#include "sway/tree/workspace.h"
#include "sway/desktop/animation.h"

/**
 * Implement `layout_transpose` for a tiled workspace.
 */

struct cmd_results *cmd_layout_transpose(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_workspace *workspace = config->handler_context.workspace;

	if (!workspace) {
		return cmd_results_new(CMD_INVALID, "layout_transpose needs a workspace to transpose its layout");
	}

	enum sway_container_layout mode = layout_get_type(workspace);
	if (mode == L_HORIZ) {
		layout_set_type(workspace, L_VERT);
		for (int i = 0; i < workspace->tiling->length; ++i) {
			struct sway_container *con = workspace->tiling->items[i];
			con->pending.layout = L_HORIZ;
			container_update_representation(con);
		}
	} else if (mode == L_VERT) {
		layout_set_type(workspace, L_HORIZ);
		for (int i = 0; i < workspace->tiling->length; ++i) {
			struct sway_container *con = workspace->tiling->items[i];
			con->pending.layout = L_VERT;
			container_update_representation(con);
		}
	} else {
		return cmd_results_new(CMD_INVALID, "The current workspace has an unknown layout type");
	}
	arrange_workspace(workspace);
	workspace_update_representation(workspace);
	animation_set_type(ANIMATION_WINDOW_MOVE);

	return cmd_results_new(CMD_SUCCESS, NULL);
}
