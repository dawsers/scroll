#include <float.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/tree/arrange.h"
#include "sway/tree/workspace.h"
#include "sway/tree/layout.h"
#include "sway/desktop/transaction.h"
#include "util.h"

struct cmd_results *cmd_align_reset_auto(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "align_reset_auto", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	config->align_reset_auto = parse_boolean(argv[0], config->align_reset_auto);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static bool parse_direction(const char *name,
		enum sway_layout_direction *out) {
	if (strcasecmp(name, "left") == 0) {
		*out = DIR_LEFT;
	} else if (strcasecmp(name, "right") == 0) {
		*out = DIR_RIGHT;
	} else if (strcasecmp(name, "up") == 0 || strcasecmp(name, "top") == 0) {
		*out = DIR_UP;
	} else if (strcasecmp(name, "down") == 0 || strcasecmp(name, "bottom") == 0) {
		*out = DIR_DOWN;
	} else if (strcasecmp(name, "center") == 0) {
		*out = DIR_CENTER;
	} else if (strcasecmp(name, "middle") == 0) {
		*out = DIR_MIDDLE;
	} else {
		*out = DIR_INVALID;
		return false;
	}
	return true;
}

static void set_parent_x(struct sway_container *parent, double x) {
	parent->pending.x = x;
	node_set_dirty(&parent->node);
	arrange_workspace(parent->pending.workspace);
	transaction_commit_dirty();
}

static void set_parent_y(struct sway_container *parent, double y) {
	parent->pending.y = y;
	node_set_dirty(&parent->node);
	arrange_workspace(parent->pending.workspace);
	transaction_commit_dirty();
}

static void set_container_x(struct sway_container *container, double x) {
	container->pending.x = x;
	node_set_dirty(&container->node);
	arrange_container(container);
	transaction_commit_dirty();
}

static void set_container_y(struct sway_container *container, double y) {
	container->pending.y = y;
	node_set_dirty(&container->node);
	arrange_container(container);
	transaction_commit_dirty();
}

struct cmd_results *cmd_align(int argc, char **argv) {
	if (config->reading || !config->active) {
		return cmd_results_new(CMD_DEFER, NULL);
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_node *node = config->handler_context.node;
	struct sway_container *container = config->handler_context.container;
	if (node->type != N_CONTAINER) {
		return cmd_results_new(CMD_FAILURE,
			"Command 'align' needs a container to align.");
	}

	if (container_is_floating(container) || container->pending.fullscreen_mode != FULLSCREEN_NONE) {
		return cmd_results_new(CMD_FAILURE,
			"Command 'align' needs a tiled, non full-screen container to align.");
	}

	struct sway_workspace *workspace = container->pending.workspace;
	if (layout_overview_mode(workspace) != OVERVIEW_DISABLED) {
		// Do nothing if in overview mode
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "align", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	if (strcasecmp(argv[0], "reset") == 0) {
		layout_modifiers_set_reorder(workspace, REORDER_AUTO);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	enum sway_layout_direction direction = DIR_INVALID;
	if (!parse_direction(argv[0], &direction)) {
		return cmd_results_new(
				CMD_INVALID, "Expected 'align <left|right|center|top|bottom|middle|reset>' ");
	}

	enum sway_container_layout layout = layout_get_type(workspace);
	enum sway_container_layout mode = layout_modifiers_get_mode(workspace);
	struct sway_container *parent = container->pending.parent ? container->pending.parent : container;
	float scale = layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0f;
	layout_modifiers_set_reorder(workspace, REORDER_LAZY);
	int gap = workspace->gaps_inner;

	switch (direction) {
	case DIR_LEFT: {
		if (layout == L_HORIZ) {
			set_parent_x(parent, workspace->x + scale * gap);
		} else {
			set_container_x(container, workspace->x + scale * gap);
		}
		break;
	}
	case DIR_RIGHT: {
		if (layout == L_HORIZ) {
			set_parent_x(parent, workspace->x + workspace->width - scale * (parent->pending.width + gap));
		} else {
			set_container_x(container, workspace->x + workspace->width - scale * (container->pending.width + gap));
		}
		break;
	}
	case DIR_CENTER: {
		if (layout == L_HORIZ) {
			if (mode == L_HORIZ) {
				set_parent_x(parent, workspace->x + 0.5 * (workspace->width - scale * parent->pending.width));
			} else {
				set_container_y(container, workspace->y + 0.5 * (workspace->height - scale * container->pending.height));
			}
		} else {
			if (mode == L_VERT) {
				set_parent_y(parent, workspace->y + 0.5 * (workspace->height - scale * parent->pending.height));
			} else {
				set_container_x(container, workspace->x + 0.5 * (workspace->width - scale * container->pending.width));
			}
		}
		break;
	}
	case DIR_UP: {
		if (layout == L_HORIZ) {
			set_container_y(container, workspace->y + scale * gap);
		} else {
			set_parent_y(parent, workspace->y + scale * gap);
		}
		break;
	}
	case DIR_DOWN: {
		if (layout == L_HORIZ) {
			set_container_y(container, workspace->y + workspace->height - scale * (container->pending.height + gap));
		} else {
			set_parent_y(parent, workspace->y + workspace->height - scale * (parent->pending.height + gap));
		}
		break;
	}
	case DIR_MIDDLE: {
		if (layout == L_HORIZ) {
			set_parent_x(parent, workspace->x + 0.5 * (workspace->width - scale * parent->pending.width));
			set_container_y(container, workspace->y + 0.5 * (workspace->height - scale * container->pending.height));
		} else {
			set_parent_y(parent, workspace->y + 0.5 * (workspace->height - scale * parent->pending.height));
			set_container_x(container, workspace->x + 0.5 * (workspace->width - scale * container->pending.width));
		}
		break;
	}
	default:
		break;
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
