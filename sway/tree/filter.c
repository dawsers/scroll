#include <stdbool.h>
#include <wlr/util/box.h>
#include "sway/criteria.h"
#include "sway/desktop/transaction.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/output.h"
#include "sway/tree/container.h"
#include "sway/tree/filter.h"
#include "sway/tree/layout.h"
#include "sway/tree/node.h"
#include "sway/tree/root.h"
#include "sway/tree/view.h"
#include "sway/tree/virtual_output.h"
#include "sway/tree/workspace.h"
#include "list.h"

static bool workspace_is_active(struct sway_workspace *workspace) {
	struct sway_output *output = workspace->output;
	if (!output || !output->wlr_output->enabled) {
		return false;
	}
	if (workspace->virtual_output) {
		return virtual_output_is_visible(workspace->virtual_output) &&
			virtual_output_get_active_workspace(workspace->virtual_output) == workspace;
	}
	return output->current.active_workspace == workspace;
}

bool filter_workspace_enabled(struct sway_workspace *workspace) {
	const struct sway_root_filters *filters = root->filters;
	if (!filters->criteria) {
		const enum sway_layout_filter_apply apply = filters->apply;
		switch (filters->filter) {
		case LAYOUT_FILTER_TILING:
		case LAYOUT_FILTER_FLOATING:
			if (apply == LAYOUT_FILTER_APPLY_ACTIVE) {
				return workspace_is_active(workspace);
			} else if (apply == LAYOUT_FILTER_APPLY_ALL) {
				return workspace_is_active(workspace) ||
					layout_overview_workspaces_enabled();
			}
			break;
		case LAYOUT_FILTER_TRAILMARK:
		case LAYOUT_FILTER_VISIBLE:
			if (apply == LAYOUT_FILTER_APPLY_ACTIVE) {
				return workspace_is_active(workspace);
			}
			break;
		case LAYOUT_FILTER_SCRATCHPAD:
			return workspace_is_active(workspace);
		case LAYOUT_FILTER_ALL:
		case LAYOUT_FILTER_CONTAINER:
			break;
		}
	}
	return workspace_is_visible(workspace);
}

// Whether the tiling or the floating containers of the workspace are shown
static bool workspace_tiling_or_floating_enabled(struct sway_workspace *workspace, bool floating) {
	const struct sway_root_filters *filters = root->filters;
	if (!filters->criteria) {
		bool hides;
		switch (filters->filter) {
		case LAYOUT_FILTER_TILING:
			hides = floating;
			break;
		case LAYOUT_FILTER_FLOATING:
			hides = !floating;
			break;
		case LAYOUT_FILTER_SCRATCHPAD:
			if (!floating) {
				return false;
			}
			hides = false;
			break;
		default:
			hides = false;
			break;
		}
		if (hides) {
			if (filters->apply == LAYOUT_FILTER_APPLY_ACTIVE_ONLY) {
				return !workspace_is_active(workspace);
			}
			return false;
		}
	}
	const enum sway_layout_overview mode = layout_overview_mode(workspace);
	return mode != (floating ? OVERVIEW_TILING : OVERVIEW_FLOATING);
}

bool filter_workspace_tiling_enabled(struct sway_workspace *workspace) {
	return workspace_tiling_or_floating_enabled(workspace, false);
}

bool filter_workspace_floating_enabled(struct sway_workspace *workspace) {
	return workspace_tiling_or_floating_enabled(workspace, true);
}

struct filter_workspace_data {
	struct sway_workspace *workspace;
	bool applies; // the filter applies to the views of this workspace
	bool otherwise; // apply the filter if applies is false (for ACTIVE/ACTIVE_ONLY)
	struct sway_container *focused; // focused top level container (FILTER_CONTAINER)
	double minx, miny, maxx, maxy; // visible area (FILTER_VISIBLE)
};

static void filter_workspace_visible_area(struct filter_workspace_data *data) {
	struct sway_workspace *workspace = data->workspace;
	struct sway_output *output = workspace->output;
	struct wlr_box *area = workspace_get_output_area(workspace);
	if (output && area) {
		data->minx = area->x;
		data->miny = area->y;
		data->maxx = area->x + area->width;
		data->maxy = area->y + area->height;
	} else if (!output) {
		struct wlr_box *usable = workspace_get_output_usable_area(workspace);
		if (usable) {
			data->minx = usable->x;
			data->miny = usable->y;
			data->maxx = usable->x + usable->width;
			data->maxy = usable->y + usable->height;
		} else {
			data->minx = workspace->x;
			data->miny = workspace->y;
			data->maxx = workspace->x + workspace->width;
			data->maxy = workspace->y + workspace->height;
		}
	} else {
		data->minx = output->lx;
		data->miny = output->ly;
		data->maxx = output->lx + output->width;
		data->maxy = output->ly + output->height;
	}
}

static void filter_workspace_data_init(struct filter_workspace_data *data,
		struct sway_workspace *workspace) {
	const struct sway_root_filters *filters = root->filters;
	data->workspace = workspace;
	data->applies = true;
	data->otherwise = true;
	data->focused = NULL;
	if (filters->criteria) {
		return;
	}
	const bool active = workspace_is_active(workspace);
	switch (filters->filter) {
	case LAYOUT_FILTER_ALL:
	case LAYOUT_FILTER_TILING:
	case LAYOUT_FILTER_FLOATING:
		// These don't filter individual views
		data->applies = false;
		break;
	case LAYOUT_FILTER_CONTAINER:
		switch (filters->apply) {
		case LAYOUT_FILTER_APPLY_ACTIVE:
			data->applies = active;
			data->otherwise = false;
			break;
		case LAYOUT_FILTER_APPLY_ALL:
			break;
		case LAYOUT_FILTER_APPLY_ACTIVE_ONLY:
			data->applies = active;
			break;
		}
		if (data->applies) {
			struct sway_seat *seat = input_manager_current_seat();
			struct sway_node *focus = seat_get_focus_inactive(seat, &workspace->node);
			if (focus && focus->type == N_CONTAINER) {
				struct sway_container *focused = focus->sway_container;
				data->focused = focused->pending.parent ?
					focused->pending.parent : focused;
			}
		}
		break;
	case LAYOUT_FILTER_TRAILMARK:
		data->applies = active || filters->apply == LAYOUT_FILTER_APPLY_ALL;
		break;
	case LAYOUT_FILTER_VISIBLE:
		data->applies = active || filters->apply == LAYOUT_FILTER_APPLY_ALL;
		if (data->applies) {
			// We need to compute the positions now instead of waiting for the
			// transaction to happen
			transaction_workspace_arrange(workspace);
			filter_workspace_visible_area(data);
		}
		break;
	case LAYOUT_FILTER_SCRATCHPAD:
		break;
	}
}

// Whether the active filter shows the view
static bool filter_view(struct filter_workspace_data *data, struct sway_container *con) {
	const struct sway_root_filters *filters = root->filters;
	if (filters->criteria) {
		return criteria_matches_view(filters->criteria, con->view);
	}
	if (!data->applies) {
		return data->otherwise;
	}
	switch (filters->filter) {
	case LAYOUT_FILTER_ALL:
	case LAYOUT_FILTER_TILING:
	case LAYOUT_FILTER_FLOATING:
		break;
	case LAYOUT_FILTER_CONTAINER:
		return data->focused &&
			(con == data->focused || con->pending.parent == data->focused);
	case LAYOUT_FILTER_TRAILMARK:
		return layout_trails_trailmarked(con->view);
	case LAYOUT_FILTER_VISIBLE: {
		// Only the views that are completely visible
		struct wlr_fbox box;
		layout_container_screen_box(con, false, &box);
		return box.x >= data->minx && box.x + box.width <= data->maxx &&
			box.y >= data->miny && box.y + box.height <= data->maxy;
	}
	case LAYOUT_FILTER_SCRATCHPAD:
		return con->scratchpad;
	}
	return true;
}

// Returns true if the container is filtered. A container with children is
// filtered when all of them are.
static bool filter_apply_container(struct filter_workspace_data *data,
		struct sway_container *con, bool layout_enabled) {
	bool filtered = !layout_enabled;
	if (con->pending.children) {
		if (con->pending.children->length > 0) {
			filtered = true;
		}
		for (int i = 0; i < con->pending.children->length; ++i) {
			struct sway_container *child = con->pending.children->items[i];
			if (!filter_apply_container(data, child, layout_enabled)) {
				filtered = false;
			}
		}
	} else if (con->view) {
		filtered = !layout_enabled || !filter_view(data, con);
	}
	if (con->pending.filtered != filtered) {
		con->pending.filtered = filtered;
		node_set_dirty(&con->node);
	}
	return filtered;
}

static void filter_apply_workspace(struct sway_workspace *workspace) {
	struct filter_workspace_data data;
	filter_workspace_data_init(&data, workspace);
	const bool tiling = filter_workspace_tiling_enabled(workspace);
	for (int i = 0; i < workspace->tiling->length; ++i) {
		filter_apply_container(&data, workspace->tiling->items[i], tiling);
	}
	const bool floating = filter_workspace_floating_enabled(workspace);
	for (int i = 0; i < workspace->floating->length; ++i) {
		filter_apply_container(&data, workspace->floating->items[i], floating);
	}
}

void filter_apply(void) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		for (int j = 0; j < output->workspaces->length; ++j) {
			struct sway_workspace *workspace = output->workspaces->items[j];
			if (!workspace || workspace->node.destroying) {
				continue;
			}
			filter_apply_workspace(workspace);
		}
	}
}
