#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/edges.h>
#include "sway/commands.h"
#include "sway/tree/arrange.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/tree/layout.h"
#include "util.h"
#include "sway/desktop/animation.h"

#define AXIS_HORIZONTAL (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)
#define AXIS_VERTICAL   (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)

static bool is_horizontal(uint32_t axis) {
	return axis & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
}

static void get_visible_from_active(enum sway_container_layout layout,
		list_t *children, int active_idx, int *from, int *to, float scale) {
	struct sway_container *active = children->items[active_idx];
	struct sway_workspace *workspace = active->pending.workspace;
	if (layout == L_HORIZ) {
		double offset = active->pending.x;
		for (int i = active_idx; i < children->length; ++i) {
			struct sway_container *con = children->items[i];
			double x0 = offset;
			double x1 = offset + scale * con->pending.width;
			if ((x0 >= workspace->x && x0 < workspace->x + workspace->width) ||
				(x1 >= workspace->x && x1 < workspace->x + workspace->width) ||
				(x0 < workspace->x && x1 >= workspace->x + workspace->width)) {
				if (from && i < *from) {
					*from = i;
				}
				if (to && i > *to) {
					*to = i;
				}
			}
			offset = x1 + scale * 2.0 * workspace->gaps_inner;
		}
	} else {
		double offset = active->pending.y;
		for (int i = active_idx; i < children->length; ++i) {
			struct sway_container *con = children->items[i];
			double y0 = offset;
			double y1 = offset + scale * con->pending.height;
			if ((y0 >= workspace->y && y0 < workspace->y + workspace->height) ||
				(y1 >= workspace->y && y1 < workspace->y + workspace->height) ||
				(y0 < workspace->y && y1 >= workspace->y + workspace->height)) {
				if (from && i < *from) {
					*from = i;
				}
				if (to && i > *to) {
					*to = i;
				}
			}
			offset = y1 + scale * 2.0 * workspace->gaps_inner;
		}
	}
}

static void get_visible_to_active(enum sway_container_layout layout,
		list_t *children, int active_idx, int *from, int *to, float scale) {
	struct sway_container *active = children->items[active_idx];
	struct sway_workspace *workspace = active->pending.workspace;
	if (layout == L_HORIZ) {
		double offset = active->pending.x - scale * 2.0 * workspace->gaps_inner;
		for (int i = active_idx - 1; i >= 0; i--) {
			struct sway_container *con = children->items[i];
			double x1 = offset;
			double x0 = offset - scale * con->pending.width;
			if ((x0 >= workspace->x && x0 < workspace->x + workspace->width) ||
				(x1 >= workspace->x && x1 < workspace->x + workspace->width) ||
				(x0 < workspace->x && x1 >= workspace->x + workspace->width)) {
				if (from && i < *from) {
					*from = i;
				}
				if (to && i > *to) {
					*to = i;
				}
			}
			offset = x0 - scale * 2.0 * workspace->gaps_inner;
		}
	} else {
		double offset = active->pending.y - scale * 2.0 * workspace->gaps_inner;
		for (int i = active_idx - 1; i >= 0; i--) {
			struct sway_container *con = children->items[i];
			double y1 = offset;
			double y0 = offset - scale * con->pending.height;
			if ((y0 >= workspace->y && y0 < workspace->y + workspace->height) ||
				(y1 >= workspace->y && y1 < workspace->y + workspace->height) ||
				(y0 < workspace->y && y1 >= workspace->y + workspace->height)) {
				if (from && i < *from) {
					*from = i;
				}
				if (to && i > *to) {
					*to = i;
				}
			}
			offset = y0 - scale * 2.0 * workspace->gaps_inner;
		}
	}
}

static void get_visible(enum sway_container_layout layout, list_t *children, int active_idx, int *from, int *to, float scale) {
	*from = children->length;
	*to = -1;
	get_visible_from_active(layout, children, active_idx, from, to, scale);
	get_visible_to_active(layout, children, active_idx, from, to, scale);
}


// Set from position. It needs to be set by setting active so from
// is at the edge. And positions are scaled.
void set_from_position(struct sway_workspace *workspace, enum sway_container_layout layout,
		list_t *children, float scale, int from, int active_idx, int gap) {
	if (layout == L_HORIZ) {
		double offset = workspace->x + scale * gap;
		if (from <= active_idx) {
			for (int i = from; i <= active_idx; ++i) {
				struct sway_container *con = children->items[i];
				con->pending.x = offset;
				offset += scale * (con->pending.width + 2.0 * gap);
			}
		} else {
			for (int i = from - 1; i >= active_idx; i--) {
				struct sway_container *con = children->items[i];
				offset -= scale * (con->pending.width + 2.0 * gap);
				con->pending.x = offset;
			}
		}
	} else {
		double offset = workspace->y + scale * gap;
		if (from <= active_idx) {
			for (int i = from; i <= active_idx; ++i) {
				struct sway_container *con = children->items[i];
				con->pending.y = offset;
				offset += scale * (con->pending.height + 2.0 * gap);
			}
		} else {
			for (int i = from - 1; i >= active_idx; i--) {
				struct sway_container *con = children->items[i];
				offset -= scale * (con->pending.height + 2.0 * gap);
				con->pending.y = offset;
			}
		}
	}
}

static void fit_size_workspace(struct sway_workspace *workspace, enum sway_layout_fit_group fit, bool equal) {
	if (workspace->tiling->length == 0) {
		return;
	}
	int from, to;
	int active_idx = max(list_find(workspace->tiling, workspace->current.focused_inactive_child), 0);
	float scale = layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0f;
	enum sway_container_layout layout = layout_get_type(workspace);

	switch (fit) {
	case FIT_ACTIVE:
		from = to = active_idx;
		break;
	case FIT_VISIBLE:
		get_visible(layout, workspace->tiling, active_idx, &from, &to, scale);
		break;
	case FIT_ALL:
		from = 0;
		to = workspace->tiling->length - 1;
		break;
	case FIT_TOEND:
		from = active_idx;
		to = workspace->tiling->length - 1;
		break;
	case FIT_TOBEG:
		from = 0;
		to = active_idx;
		break;
	default:
		return;
	}

	// Now split the width/height among all the containers (from, to), and realign
	// from to the edge. Note: widths/heights are not stored scaled, so operate in
	// unscaled mode.
	int gap = workspace->gaps_inner;
	if (layout == L_HORIZ) {
		double share = workspace->width - (to - from + 1) * 2.0 * workspace->gaps_inner;
		if (!equal) {
			double total = 0.0;
			for (int i = from; i <= to; ++i) {
				struct sway_container *con = workspace->tiling->items[i];
				total += con->pending.width;
			}
			for (int i = from; i <= to; ++i) {
				struct sway_container *con = workspace->tiling->items[i];
				con->pending.width *= share / total;
				con->width_fraction = (con->pending.width + 2.0 * gap) / workspace->width;
			}
		} else {
			double width = share / (to - from + 1);
			for (int i = from; i <= to; ++i) {
				struct sway_container *con = workspace->tiling->items[i];
				con->pending.width = width;
				con->width_fraction = 1.0 / (to - from + 1);
			}
		}
	} else {
		double share = workspace->height - (to - from + 1) * 2.0 * workspace->gaps_inner;
		if (!equal) {
			double total = 0.0;
			for (int i = from; i <= to; ++i) {
				struct sway_container *con = workspace->tiling->items[i];
				total += con->pending.height;
			}
			for (int i = from; i <= to; ++i) {
				struct sway_container *con = workspace->tiling->items[i];
				con->pending.height *= share / total;
				con->height_fraction = (con->pending.height + 2.0 * gap) / workspace->height;
			}
		} else {
			double height = share / (to - from + 1);
			for (int i = from; i <= to; ++i) {
				struct sway_container *con = workspace->tiling->items[i];
				con->pending.height = height;
				con->height_fraction = 1.0 / (to - from + 1);
			}
		}
	}
	set_from_position(workspace, layout, workspace->tiling, scale, from, active_idx, workspace->gaps_inner);
	
	arrange_workspace(workspace);

	for (int i = from; i <= to; ++i) {
		struct sway_container *con = workspace->tiling->items[i];
		layout_tiling_resize_callback(con);
	}
}

static void fit_size_container(struct sway_container *container, enum sway_layout_fit_group fit, bool equal) {
	list_t *children = container->pending.children;
	if (children->length == 0) {
		return;
	}
	int from, to;
	int active_idx = max(list_find(children, container->current.focused_inactive_child), 0);
	struct sway_workspace * workspace = container->pending.workspace;
	float scale = layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0f;
	enum sway_container_layout layout = container->pending.layout;
	
	switch (fit) {
	case FIT_ACTIVE:
		from = to = active_idx;
		break;
	case FIT_VISIBLE:
		get_visible(layout, children, active_idx, &from, &to, scale);
		break;
	case FIT_ALL:
		from = 0;
		to = children->length - 1;
		break;
	case FIT_TOEND:
		from = active_idx;
		to = children->length - 1;
		break;
	case FIT_TOBEG:
		from = 0;
		to = active_idx;
		break;
	default:
		return;
	}

	// Now split the width/height among all the containers (from, to), and realign
	// from to the edge. Note: widths/heights are not stored scaled, so operate in
	// unscaled mode.
	int gap = workspace->gaps_inner;
	if (layout == L_HORIZ) {
		double share = workspace->width - (to - from + 1) * 2.0 * workspace->gaps_inner;
		if (!equal) {
			double total = 0.0;
			for (int i = from; i <= to; ++i) {
				struct sway_container *con = children->items[i];
				total += con->pending.width;
			}
			for (int i = from; i <= to; ++i) {
				struct sway_container *con = children->items[i];
				con->pending.width *= share / total;
				con->width_fraction = (con->pending.width + 2.0 * gap) / workspace->width;
			}
		} else {
			double width = share / (to - from + 1);
			for (int i = from; i <= to; ++i) {
				struct sway_container *con = children->items[i];
				con->pending.width = width;
				con->width_fraction = 1.0 / (to - from + 1);
			}
		}
	} else {
		double share = workspace->height - (to - from + 1) * 2.0 * workspace->gaps_inner;
		if (!equal) {
			double total = 0.0;
			for (int i = from; i <= to; ++i) {
				struct sway_container *con = children->items[i];
				total += con->pending.height;
			}
			for (int i = from; i <= to; ++i) {
				struct sway_container *con = children->items[i];
				con->pending.height *= share / total;
				con->height_fraction = (con->pending.height + 2.0 * gap) / workspace->height;
			}
		} else {
			double height = share / (to - from + 1);
			for (int i = from; i <= to; ++i) {
				struct sway_container *con = children->items[i];
				con->pending.height = height;
				con->height_fraction = 1.0 / (to - from + 1);
			}
		}
	}
	set_from_position(workspace, layout, children, scale, from, active_idx, workspace->gaps_inner);
	
	arrange_container(container);

	layout_tiling_resize_callback(container);
}

static struct cmd_results *fit_size(uint32_t axis, enum sway_layout_fit_group fit, bool equal) {
	struct sway_container *current = config->handler_context.container;

	if (container_is_scratchpad_hidden_or_child(current)) {
		return cmd_results_new(CMD_FAILURE, "Cannot fit_size a hidden scratchpad container");
	}

	struct sway_workspace *workspace = config->handler_context.workspace;

	if (current->pending.fullscreen_mode != FULLSCREEN_NONE || layout_overview_mode(workspace) != OVERVIEW_DISABLED) {
		// Silently return if full screen or overview mode
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	enum sway_container_layout layout = layout_get_type(workspace);

	if (is_horizontal(axis)) {
		if (layout == L_HORIZ) {
			fit_size_workspace(workspace, fit, equal);
		} else {
			fit_size_container(current->pending.parent ? current->pending.parent : current, fit, equal);
		}
	} else {
		if (layout == L_VERT) {
			fit_size_workspace(workspace, fit, equal);
		} else {
			fit_size_container(current->pending.parent ? current->pending.parent : current, fit, equal);
		}
	}
	animation_set_type(ANIMATION_WINDOW_SIZE);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_fit_size(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_container *current = config->handler_context.container;
	if (!current) {
		return cmd_results_new(CMD_INVALID, "Cannot fit_size nothing");
	}

	if (container_is_floating(current)) {
		return cmd_results_new(CMD_INVALID, "Cannot fit_size a floating container");
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "fit_size", EXPECTED_AT_LEAST, 3))) {
		return error;
	}

	enum sway_layout_fit_group fit;
	if (strcasecmp(argv[1], "active") == 0) {
		fit = FIT_ACTIVE;
	} else if (strcasecmp(argv[1], "visible") == 0) {
		fit = FIT_VISIBLE;
	} else if (strcasecmp(argv[1], "all") == 0) {
		fit = FIT_ALL;
	} else if (strcasecmp(argv[1], "tobeg") == 0) {
		fit = FIT_TOBEG;
	} else if (strcasecmp(argv[1], "toend") == 0) {
		fit = FIT_TOEND;
	} else {
		return cmd_results_new(CMD_INVALID, "fit_size range invalid");
	}

	bool equal;
	if (strcasecmp(argv[2], "proportional") == 0) {
		equal = false;
	} else if (strcasecmp(argv[2], "equal") == 0) {
		equal = true;
	} else {
		return cmd_results_new(CMD_INVALID, "fit_size mode (proportional|equal) invalid");
	}

	if (strcasecmp(argv[0], "h") == 0) {
		return fit_size(AXIS_HORIZONTAL, fit, equal);
	} else if (strcasecmp(argv[0], "v") == 0) {
		return fit_size(AXIS_VERTICAL, fit, equal);
	}

	const char usage[] = "Expected 'fit_size <h|v> <active|visible|all|toend|tobeg> <proportional|equal>'";

	return cmd_results_new(CMD_INVALID, "%s", usage);
}
