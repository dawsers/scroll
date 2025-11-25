#include <float.h>
#include <strings.h>
#include <wlr/types/wlr_output_layout.h>
#include "log.h"
#include "sway/commands.h"
#include "sway/input/input-manager.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/root.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/tree/layout.h"
#include "stringop.h"
#include "util.h"
#include "sway/desktop/transaction.h"

static bool get_direction_from_next_prev(struct sway_container *container,
		struct sway_seat *seat, const char *name, enum sway_layout_direction *out) {
	enum sway_container_layout parent_layout = L_NONE;
	if (container) {
		parent_layout = container_parent_layout(container);
	}

	if (strcasecmp(name, "prev") == 0) {
		switch (parent_layout) {
		case L_HORIZ:
			*out = DIR_LEFT;
			break;
		case L_VERT:
			*out = DIR_UP;
			break;
		case L_NONE:
			return true;
		default:
			return false;
		}
	} else if (strcasecmp(name, "next") == 0) {
		switch (parent_layout) {
		case L_HORIZ:
			*out = DIR_RIGHT;
			break;
		case L_VERT:
			*out = DIR_DOWN;
			break;
		case L_NONE:
			return true;
		default:
			return false;
		}
	} else {
		return false;
	}

	return true;
}

static bool parse_direction(const char *name,
		enum sway_layout_direction *out) {
	if (strcasecmp(name, "left") == 0) {
		*out = DIR_LEFT;
	} else if (strcasecmp(name, "right") == 0) {
		*out = DIR_RIGHT;
	} else if (strcasecmp(name, "up") == 0) {
		*out = DIR_UP;
	} else if (strcasecmp(name, "down") == 0) {
		*out = DIR_DOWN;
	} else if (strcasecmp(name, "beginning") == 0) {
		*out = DIR_BEGIN;
	} else if (strcasecmp(name, "end") == 0) {
		*out = DIR_END;
	} else {
		*out = DIR_INVALID;
		return false;
	}

	return true;
}

static struct sway_node *get_node_in_workspace_direction(
		struct sway_workspace *ws, enum sway_layout_direction dir) {
	struct sway_seat *seat = config->handler_context.seat;
	if (ws->fullscreen) {
		return seat_get_focus_inactive(seat, &ws->fullscreen->node);
	}
	struct sway_container *container = NULL;

	if (ws->tiling->length > 0) {
		enum sway_container_layout layout = layout_modifiers_get_mode(ws);
		switch (dir) {
		case DIR_LEFT:
			if (layout == L_HORIZ) {
				// get most right child of new output
				container = ws->tiling->items[ws->tiling->length-1];
			} else {
				container = seat_get_focus_inactive_tiling(seat, ws);
			}
			break;
		case DIR_RIGHT:
			if (layout == L_HORIZ) {
				// get most left child of new output
				container = ws->tiling->items[0];
			} else {
				container = seat_get_focus_inactive_tiling(seat, ws);
			}
			break;
		case DIR_UP:
			if (layout == L_VERT) {
				// get most bottom child of new output
				container = ws->tiling->items[ws->tiling->length-1];
			} else {
				container = seat_get_focus_inactive_tiling(seat, ws);
			}
			break;
		case DIR_DOWN:
			if (layout == L_VERT) {
				// get most top child of new output
				container = ws->tiling->items[0];
			} else {
				container = seat_get_focus_inactive_tiling(seat, ws);
			}
			break;
		default:
			break;
		}
	}

	if (container) {
		container = seat_get_focus_inactive_view(seat, &container->node);
		return &container->node;
	}

	return &ws->node;
}

/**
 * Get node in the direction of newly entered output.
 */
static struct sway_node *get_node_in_output_direction(
		struct sway_output *output, enum sway_layout_direction dir) {
	struct sway_workspace *ws = output_get_active_workspace(output);
	if (!sway_assert(ws, "Expected output to have a workspace")) {
		return NULL;
	}
	return get_node_in_workspace_direction(ws, dir);
}

static struct sway_node *node_get_in_direction_tiling(
		struct sway_container *container, struct sway_seat *seat,
		enum sway_layout_direction dir, bool descend) {
	struct sway_container *wrap_candidate = NULL;
	struct sway_container *current = container;
	while (current) {
		if (config->fullscreen_movefocus == FULLSCREEN_MOVEFOCUS_NONE) {
			if (current->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
				// Fullscreen container with a direction - go straight to outputs
				struct sway_output *output = current->pending.workspace->output;
				struct sway_output *new_output =
					output_get_in_direction(output, layout_to_wlr_direction(dir));
				if (!new_output) {
					return NULL;
				}
				return get_node_in_output_direction(new_output, dir);
			}
			if (current->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
				return NULL;
			}
		}

		bool can_move = false;
		int desired;
		int idx = container_sibling_index(current);
		enum sway_container_layout parent_layout = current->pending.parent ?
			container_parent_layout(current) : layout_get_type(current->pending.workspace);
		list_t *siblings = container_get_siblings(current);

		if (dir == DIR_LEFT || dir == DIR_RIGHT) {
			if (parent_layout == L_HORIZ) {
				can_move = true;
				desired = idx + (dir == DIR_LEFT ? -1 : 1);
			}
		} else if (dir == DIR_UP || dir == DIR_DOWN) {
			if (parent_layout == L_VERT) {
				can_move = true;
				desired = idx + (dir == DIR_UP ? -1 : 1);
			}
		} else if (dir == DIR_BEGIN || dir == DIR_END) {
			// We move focus within this container if the layout mode is the
			// same as the container's
			struct sway_workspace *workspace = config->handler_context.workspace;
			enum sway_container_layout mode = layout_modifiers_get_mode(workspace);
			if (mode == parent_layout) {
				can_move = true;
				desired = dir == DIR_BEGIN ? 0 : siblings->length - 1;
			}
		}

		if (can_move) {
			if (desired < 0 || desired >= siblings->length) {
				int len = siblings->length;
				if (config->focus_wrapping != WRAP_NO && !wrap_candidate
						&& len > 1) {
					if (desired < 0) {
						wrap_candidate = siblings->items[len-1];
					} else {
						wrap_candidate = siblings->items[0];
					}
					if (config->focus_wrapping == WRAP_FORCE) {
						struct sway_container *c = seat_get_focus_inactive_view(
								seat, &wrap_candidate->node);
						return &c->node;
					}
				}
			} else {
				struct sway_container *desired_con = siblings->items[desired];
				if (!descend) {
					return &desired_con->node;
				} else {
					struct sway_container *c = seat_get_focus_inactive_view(
							seat, &desired_con->node);
					return &c->node;
				}
			}
		}

		current = current->pending.parent;
	}

	// Check if this is a split workspace and we can move focus between the two siblings
	if (container->pending.workspace) {
		struct sway_workspace *ws = container->pending.workspace;
		if ((dir == DIR_LEFT && ws->split.split == WORKSPACE_SPLIT_RIGHT) ||
			(dir == DIR_RIGHT && ws->split.split == WORKSPACE_SPLIT_LEFT)) {
			return get_node_in_workspace_direction(ws->split.sibling, dir);
		}
		if ((dir == DIR_UP && ws->split.split == WORKSPACE_SPLIT_BOTTOM) ||
			(dir == DIR_DOWN && ws->split.split == WORKSPACE_SPLIT_TOP)) {
			return get_node_in_workspace_direction(ws->split.sibling, dir);
		}
	}
	// Check a different output
	struct sway_output *output = container->pending.workspace->output;
	struct sway_output *new_output = output_get_in_direction(output, layout_to_wlr_direction(dir));
	if ((config->focus_wrapping != WRAP_WORKSPACE ||
				container->node.type == N_WORKSPACE) && new_output) {
		return get_node_in_output_direction(new_output, dir);
	}

	// If there is a wrap candidate, return its focus inactive view
	if (wrap_candidate) {
		struct sway_container *wrap_inactive = seat_get_focus_inactive_view(
				seat, &wrap_candidate->node);
		return &wrap_inactive->node;
	}

	return NULL;
}

static struct sway_node *node_get_in_direction_floating(
		struct sway_container *con, struct sway_seat *seat,
		enum sway_layout_direction dir) {
	double ref_lx = con->pending.x + con->pending.width / 2;
	double ref_ly = con->pending.y + con->pending.height / 2;
	double closest_distance = DBL_MAX;
	struct sway_container *closest_con = NULL;

	if (!con->pending.workspace) {
		return NULL;
	}

	for (int i = 0; i < con->pending.workspace->floating->length; i++) {
		struct sway_container *floater = con->pending.workspace->floating->items[i];
		if (floater == con) {
			continue;
		}
		float distance = dir == DIR_LEFT || dir == DIR_RIGHT
			? (floater->pending.x + floater->pending.width / 2) - ref_lx
			: (floater->pending.y + floater->pending.height / 2) - ref_ly;
		if (dir == DIR_LEFT || dir == DIR_UP) {
			distance = -distance;
		}
		if (distance < 0) {
			continue;
		}
		if (distance < closest_distance) {
			closest_distance = distance;
			closest_con = floater;
		}
	}

	return closest_con ? &closest_con->node : NULL;
}

static struct cmd_results *focus_mode(struct sway_workspace *ws,
		struct sway_seat *seat, bool floating) {
	struct sway_container *new_focus = NULL;
	if (floating) {
		new_focus = seat_get_focus_inactive_floating(seat, ws);
	} else {
		new_focus = seat_get_focus_inactive_tiling(seat, ws);
	}
	if (new_focus) {
		struct sway_container *new_focus_view =
			seat_get_focus_inactive_view(seat, &new_focus->node);
		if (new_focus_view) {
			new_focus = new_focus_view;
		}
		seat_set_focus_container(seat, new_focus);

		// If we're on the floating layer and the floating container area
		// overlaps the position on the tiling layer that would be warped to,
		// `seat_consider_warp_to_focus` would decide not to warp, but we need
		// to anyway.
		if (config->mouse_warping == WARP_CONTAINER) {
			node_set_focus_warp(&new_focus->node, FOCUS_WARP_FORCE);
		} else {
			seat_consider_warp_to_focus(seat);
		}
	} else {
		return cmd_results_new(CMD_FAILURE,
				"Failed to find a %s container in workspace.",
				floating ? "floating" : "tiling");
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *focus_output(struct sway_seat *seat,
		int argc, char **argv) {
	if (!argc) {
		return cmd_results_new(CMD_INVALID,
			"Expected 'focus output <direction|name>'.");
	}
	char *identifier = join_args(argv, argc);
	struct sway_output *output = output_by_name_or_id(identifier);

	if (!output) {
		enum sway_layout_direction direction = DIR_INVALID;
		if (!parse_direction(identifier, &direction)) {
			free(identifier);
			return cmd_results_new(CMD_INVALID,
				"There is no output with that name.");
		}
		struct sway_workspace *ws = seat_get_focused_workspace(seat);
		if (!ws) {
			free(identifier);
			return cmd_results_new(CMD_FAILURE,
				"No focused workspace to base directions off of.");
		}
		output = output_get_in_direction(ws->output, layout_to_wlr_direction(direction));

		if (!output) {
			int center_lx = ws->output->lx + ws->output->width / 2;
			int center_ly = ws->output->ly + ws->output->height / 2;
			struct wlr_output *target = wlr_output_layout_farthest_output(
					root->output_layout, opposite_direction(layout_to_wlr_direction(direction)),
					ws->output->wlr_output, center_lx, center_ly);
			if (target) {
				output = output_from_wlr_output(target);
			}
		}
	}

	free(identifier);
	if (output) {
		seat_set_focus(seat, seat_get_focus_inactive(seat, &output->node));
		seat_consider_warp_to_focus(seat);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *focus_parent(void) {
	struct sway_seat *seat = config->handler_context.seat;
	struct sway_container *con = config->handler_context.container;
	if (!con || con->pending.fullscreen_mode) {
		return cmd_results_new(CMD_SUCCESS, NULL);
	}
	struct sway_node *parent = node_get_parent(&con->node);
	if (parent) {
		seat_set_focus(seat, parent);
		seat_consider_warp_to_focus(seat);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *focus_child(void) {
	struct sway_seat *seat = config->handler_context.seat;
	struct sway_node *node = config->handler_context.node;
	struct sway_node *focus = seat_get_active_tiling_child(seat, node);
	if (focus) {
		seat_set_focus(seat, focus);
		seat_consider_warp_to_focus(seat);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_focus(int argc, char **argv) {
	if (config->reading || !config->active) {
		return cmd_results_new(CMD_DEFER, NULL);
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_node *node = config->handler_context.node;
	struct sway_container *container = config->handler_context.container;
	struct sway_workspace *workspace = config->handler_context.workspace;
	struct sway_seat *seat = config->handler_context.seat;
	if (node->type < N_WORKSPACE) {
		return cmd_results_new(CMD_FAILURE,
			"Command 'focus' cannot be used above the workspace level.");
	}

	if (argc == 0) {
		if (!container) {
			return cmd_results_new(CMD_FAILURE, "No container to focus was specified.");
		}

		if (container_is_scratchpad_hidden_or_child(container)) {
			root_scratchpad_show(container);
		}
		// if we are switching to a container under a fullscreen window, we first
		// need to exit fullscreen so that the newly focused container becomes visible
		struct sway_container *obstructing = container_obstructing_fullscreen_container(container);
		if (obstructing) {
			container_fullscreen_disable(obstructing);
			arrange_root();
		}
		seat_set_focus_container(seat, container);
		seat_consider_warp_to_focus(seat);
		container_raise_floating(container);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (strcmp(argv[0], "floating") == 0) {
		return focus_mode(workspace, seat, true);
	} else if (strcmp(argv[0], "tiling") == 0) {
		return focus_mode(workspace, seat, false);
	} else if (strcmp(argv[0], "mode_toggle") == 0) {
		bool floating = container && container_is_floating_or_child(container);
		return focus_mode(workspace, seat, !floating);
	}

	if (strcmp(argv[0], "output") == 0) {
		argc--; argv++;
		return focus_output(seat, argc, argv);
	}

	if (strcasecmp(argv[0], "parent") == 0) {
		return focus_parent();
	}
	if (strcasecmp(argv[0], "child") == 0) {
		return focus_child();
	}

	enum sway_layout_direction direction = DIR_INVALID;
	bool descend = true;
	if (!parse_direction(argv[0], &direction)) {
		if (!get_direction_from_next_prev(container, seat, argv[0], &direction)) {
			return cmd_results_new(CMD_INVALID,
				"Expected 'focus <direction|next|prev|parent|child|mode_toggle|floating|tiling>' "
				"or 'focus output <direction|name>'");
		} else if (argc == 2 && strcasecmp(argv[1], "sibling") == 0) {
			descend = false;
		}
	}

	if (direction == DIR_INVALID) {
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (node->type == N_WORKSPACE) {
		// Jump to the next output
		struct sway_output *new_output =
			output_get_in_direction(workspace->output, layout_to_wlr_direction(direction));
		if (!new_output) {
			return cmd_results_new(CMD_SUCCESS, NULL);
		}

		struct sway_node *node =
			get_node_in_output_direction(new_output, direction);
		seat_set_focus(seat, node);
		seat_consider_warp_to_focus(seat);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (!sway_assert(container, "Expected container to be non null")) {
		return cmd_results_new(CMD_FAILURE, "");
	}
	struct sway_node *next_focus = NULL;
	if (container_is_floating(container) &&
			container->pending.fullscreen_mode == FULLSCREEN_NONE) {
		next_focus = node_get_in_direction_floating(container, seat, direction);
	} else {
		next_focus = node_get_in_direction_tiling(container, seat, direction, descend);
	}
	if (next_focus) {
		if (next_focus->type == N_CONTAINER) {
			if (config->fullscreen_movefocus != FULLSCREEN_MOVEFOCUS_NONE) {
				if (container->pending.workspace == next_focus->sway_container->pending.workspace) {
					// Deal with full screen
					enum sway_fullscreen_mode fullscreen_mode = container->pending.fullscreen_mode;
					// Allow fullscreen_movefocus for WORKSPACE type full screen
					if (fullscreen_mode == FULLSCREEN_WORKSPACE) {
						if (config->fullscreen_movefocus == FULLSCREEN_MOVEFOCUS_FOLLOW) {
							container_pass_fullscreen(container, next_focus->sway_container);
						} else {
							container_fullscreen_disable(container);
							node_set_dirty(&container->node);
						}
					}
					if (config->fullscreen_movefocus == FULLSCREEN_MOVEFOCUS_NOFOLLOW) {
						if (next_focus->sway_container->fullscreen) {
							container_set_fullscreen(next_focus->sway_container, FULLSCREEN_WORKSPACE);
							node_set_dirty(&next_focus->sway_container->node);
						}
						node_set_dirty(&container->pending.workspace->node);
					}
				}
			} else {
				if (container->pending.workspace == next_focus->sway_container->pending.workspace) {
					if (next_focus->sway_container->fullscreen) {
						container_set_fullscreen(next_focus->sway_container, FULLSCREEN_WORKSPACE);
						node_set_dirty(&next_focus->sway_container->node);
						node_set_dirty(&container->pending.workspace->node);
					}
				}
			}
			arrange_workspace(next_focus->sway_container->pending.workspace);
			container_raise_floating(next_focus->sway_container);
		}
		seat_set_focus(seat, next_focus);
		seat_consider_warp_to_focus(seat);
		transaction_commit_dirty();
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
