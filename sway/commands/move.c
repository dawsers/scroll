#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include "sway/commands.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"
#include "sway/tree/workspace.h"
#include "stringop.h"
#include "list.h"
#include "log.h"
#include "util.h"
#include "sway/desktop/animation.h"

static const char expected_syntax[] =
	"Expected 'move <left|right|up|down> <[px] px>' or "
	"'move [--no-auto-back-and-forth] <container|window> [to] workspace <name>' or "
	"'move <container|window|workspace> [to] output <name|direction>' or "
	"'move <container|window> [to] mark <mark>'";

static struct sway_output *output_in_direction(const char *direction_string,
		struct sway_output *reference, int ref_lx, int ref_ly) {
	if (strcasecmp(direction_string, "current") == 0) {
		struct sway_workspace *active_ws =
			seat_get_focused_workspace(config->handler_context.seat);
		if (!active_ws) {
			return NULL;
		}
		return active_ws->output;
	}

	struct {
		char *name;
		enum wlr_direction direction;
	} names[] = {
		{ "up", WLR_DIRECTION_UP },
		{ "down", WLR_DIRECTION_DOWN },
		{ "left", WLR_DIRECTION_LEFT },
		{ "right", WLR_DIRECTION_RIGHT },
	};

	enum wlr_direction direction = 0;

	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		if (strcasecmp(names[i].name, direction_string) == 0) {
			direction = names[i].direction;
			break;
		}
	}

	if (reference && direction) {
		struct wlr_output *target = wlr_output_layout_adjacent_output(
				root->output_layout, direction, reference->wlr_output,
				ref_lx, ref_ly);

		if (!target) {
			target = wlr_output_layout_farthest_output(
					root->output_layout, opposite_direction(direction),
					reference->wlr_output, ref_lx, ref_ly);
		}

		if (target) {
			return target->data;
		}
	}

	return output_by_name_or_id(direction_string);
}

/**
 * Ensures all seats focus the fullscreen container if needed.
 */
static void workspace_focus_fullscreen(struct sway_workspace *workspace) {
	if (!workspace->fullscreen) {
		return;
	}
	struct sway_seat *seat;
	struct sway_workspace *focus_ws;
	wl_list_for_each(seat, &server.input->seats, link) {
		focus_ws = seat_get_focused_workspace(seat);
		if (focus_ws == workspace) {
			struct sway_node *new_focus =
				seat_get_focus_inactive(seat, &workspace->fullscreen->node);
			seat_set_raw_focus(seat, new_focus);
		}
	}
}

static void container_move_to_workspace(struct sway_container *container,
		struct sway_workspace *workspace) {
	if (container->pending.workspace == workspace) {
		return;
	}
	struct sway_workspace *old_workspace = container->pending.workspace;
	if (container_is_floating(container)) {
		struct sway_output *old_output = container->pending.workspace->output;
		container_detach(container);
		workspace_add_floating(workspace, container);
		container_handle_fullscreen_reparent(container);
		// If changing output, adjust the coordinates of the window.
		if (old_output != workspace->output && !container->pending.fullscreen_mode) {
			struct wlr_box workspace_box, old_workspace_box;
			workspace_get_box(workspace, &workspace_box);
			workspace_get_box(old_workspace, &old_workspace_box);
			floating_fix_coordinates(container, &old_workspace_box, &workspace_box);
			if (container->scratchpad && workspace->output) {
				struct wlr_box output_box;
				output_get_box(workspace->output, &output_box);
				container->transform = workspace_box;
			}
		}
	} else {
		layout_move_container_to_workspace(container, workspace);
	}
	if (container->view) {
		ipc_event_window(container, "move");
	}
	workspace_detect_urgent(old_workspace);
	workspace_detect_urgent(workspace);
	workspace_focus_fullscreen(workspace);
}

static void container_move_to_container(struct sway_container *container,
		struct sway_container *destination) {
	if (container == destination
			|| container_has_ancestor(destination, container)) {
		return;
	}
	if (container_is_floating(container)) {
		container_move_to_workspace(container, destination->pending.workspace);
		return;
	}
	struct sway_workspace *old_workspace = container->pending.workspace;

	container_detach(container);
	container->pending.width = container->pending.height = 0;
	container->width_fraction = container->height_fraction = 0;

	if (destination->view) {
		container_add_sibling(destination, container, 1);
	} else {
		container_add_child(destination, container);
	}

	if (container->view) {
		ipc_event_window(container, "move");
	}

	if (destination->pending.workspace) {
		workspace_focus_fullscreen(destination->pending.workspace);
		workspace_detect_urgent(destination->pending.workspace);
	}

	if (old_workspace && old_workspace != destination->pending.workspace) {
		workspace_detect_urgent(old_workspace);
	}
}

// Returns true if moved
static bool container_move_in_direction(struct sway_container *container,
		enum sway_layout_direction move_dir, bool nomode) {
	// If moving a fullscreen view, only consider outputs
	switch (container->pending.fullscreen_mode) {
	case FULLSCREEN_NONE:
		break;
	case FULLSCREEN_WORKSPACE:
	case FULLSCREEN_GLOBAL:
		return false;
	}
	return layout_move_container(container, move_dir, nomode);
}

static struct cmd_results *cmd_move_to_scratchpad(void);

static struct cmd_results *cmd_move_container(bool no_auto_back_and_forth,
		int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "move container/window",
				EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	struct sway_node *node = config->handler_context.node;
	struct sway_workspace *workspace = config->handler_context.workspace;
	struct sway_container *container = config->handler_context.container;
	if (node->type == N_WORKSPACE) {
		if (workspace->tiling->length == 0) {
			return cmd_results_new(CMD_FAILURE,
					"Can't move an empty workspace");
		}
		container = workspace_wrap_children(workspace);
	}

	if (container->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		return cmd_results_new(CMD_FAILURE,
				"Can't move fullscreen global container");
	}

	struct sway_seat *seat = config->handler_context.seat;
	struct sway_container *old_parent = container->pending.parent;
	struct sway_workspace *old_ws = container->pending.workspace;
	struct sway_output *old_output = old_ws ? old_ws->output : NULL;
	struct sway_node *destination = NULL;

	// determine destination
	if (strcasecmp(argv[0], "workspace") == 0) {
		// move container to workspace x
		struct sway_workspace *ws = NULL;
		char *ws_name = NULL;
		if (strcasecmp(argv[1], "next") == 0 ||
				strcasecmp(argv[1], "prev") == 0 ||
				strcasecmp(argv[1], "next_on_output") == 0 ||
				strcasecmp(argv[1], "prev_on_output") == 0 ||
				strcasecmp(argv[1], "current") == 0) {
			ws = workspace_by_name(argv[1]);
		} else if (strcasecmp(argv[1], "back_and_forth") == 0) {
			if (!(ws = workspace_by_name(argv[1]))) {
				if (seat->prev_workspace_name) {
					ws_name = strdup(seat->prev_workspace_name);
				} else {
					return cmd_results_new(CMD_FAILURE,
							"No workspace was previously active.");
				}
			}
		} else {
			if (strcasecmp(argv[1], "number") == 0) {
				// move [window|container] [to] "workspace number x"
				if (argc < 3) {
					return cmd_results_new(CMD_INVALID, "%s", expected_syntax);
				}
				if (!isdigit(argv[2][0])) {
					return cmd_results_new(CMD_INVALID,
							"Invalid workspace number '%s'", argv[2]);
				}
				ws_name = join_args(argv + 2, argc - 2);
				ws = workspace_by_number(ws_name);
			} else {
				ws_name = join_args(argv + 1, argc - 1);
				ws = workspace_by_name(ws_name);
			}

			if (!no_auto_back_and_forth && config->auto_back_and_forth &&
					seat->prev_workspace_name) {
				// auto back and forth move
				if (old_ws && old_ws->name &&
						strcmp(old_ws->name, ws_name) == 0) {
					// if target workspace is the current one
					free(ws_name);
					ws_name = strdup(seat->prev_workspace_name);
					ws = workspace_by_name(ws_name);
				}
			}
		}
		if (!ws) {
			// We have to create the workspace, but if the container is
			// sticky and the workspace is going to be created on the same
			// output, we'll bail out first.
			if (container_is_sticky_or_child(container)) {
				struct sway_output *new_output =
					workspace_get_initial_output(ws_name);
				if (old_output == new_output) {
					free(ws_name);
					return cmd_results_new(CMD_FAILURE,
							"Can't move sticky container to another workspace "
							"on the same output");
				}
			}
			ws = workspace_create(NULL, ws_name);
			arrange_workspace(ws);
		}
		free(ws_name);
		destination = &ws->node;
	} else if (strcasecmp(argv[0], "output") == 0) {
		struct sway_output *new_output = output_in_direction(argv[1],
				old_output, container->pending.x, container->pending.y);
		if (!new_output) {
			return cmd_results_new(CMD_FAILURE,
				"Can't find output with name/direction '%s'", argv[1]);
		}
		destination = &new_output->current.active_workspace->node;
	} else if (strcasecmp(argv[0], "mark") == 0) {
		struct sway_container *dest_con = container_find_mark(argv[1]);
		if (dest_con == NULL) {
			return cmd_results_new(CMD_FAILURE,
					"Mark '%s' not found", argv[1]);
		}
		destination = &dest_con->pending.workspace->node;
	} else {
		return cmd_results_new(CMD_INVALID, "%s", expected_syntax);
	}

	if (destination->type == N_CONTAINER &&
			container_is_scratchpad_hidden(destination->sway_container)) {
		return cmd_move_to_scratchpad();
	}

	if (container_is_sticky_or_child(container) && old_output &&
			node_has_ancestor(destination, &old_output->node)) {
		return cmd_results_new(CMD_FAILURE, "Can't move sticky "
				"container to another workspace on the same output");
	}

	struct sway_output *new_output = node_get_output(destination);
	struct sway_workspace *new_output_last_ws = NULL;
	if (new_output && old_output != new_output) {
		new_output_last_ws = output_get_active_workspace(new_output);
	}

	// save focus, in case it needs to be restored
	struct sway_node *focus = seat_get_focus(seat);

	// move container
	if (container_is_scratchpad_hidden_or_child(container)) {
		container_detach(container);
		root_scratchpad_show(container);
	}
	switch (destination->type) {
	case N_WORKSPACE:
		container_move_to_workspace(container, destination->sway_workspace);
		break;
	case N_OUTPUT: {
			struct sway_output *output = destination->sway_output;
			struct sway_workspace *ws = output_get_active_workspace(output);
			if (!sway_assert(ws, "Expected output to have a workspace")) {
				return cmd_results_new(CMD_FAILURE,
						"Expected output to have a workspace");
			}
			container_move_to_workspace(container, ws);
		}
		break;
	case N_CONTAINER:
		container_move_to_container(container, destination->sway_container);
		break;
	case N_ROOT:
		break;
	}

	// restore focus on destination output back to its last active workspace
	struct sway_workspace *new_workspace = new_output ?
		output_get_active_workspace(new_output) : NULL;
	if (new_output &&
			!sway_assert(new_workspace, "Expected output to have a workspace")) {
		return cmd_results_new(CMD_FAILURE,
				"Expected output to have a workspace");
	}

	if (new_output_last_ws && new_output_last_ws != new_workspace) {
		struct sway_node *new_output_last_focus =
			seat_get_focus_inactive(seat, &new_output_last_ws->node);
		seat_set_raw_focus(seat, new_output_last_focus);
	}

	// restore focus
	if (focus == &container->node) {
		focus = NULL;
		if (old_parent != container->pending.parent) {
			focus = seat_get_focus_inactive(seat, &old_parent->node);
		}
		if (!focus && old_ws) {
			focus = seat_get_focus_inactive(seat, &old_ws->node);
		}
	}
	seat_set_focus(seat, focus);

	// clean-up, destroying parents if the container was the last child
	if (old_parent) {
		container_reap_empty(old_parent);
	} else if (old_ws) {
		workspace_consider_destroy(old_ws);
	}

	// arrange windows
	if (root->fullscreen_global) {
		arrange_root();
	} else {
		if (old_ws && !old_ws->node.destroying) {
			arrange_workspace(old_ws);
		}
		arrange_node(node_get_parent(destination));
	}
	animation_create(ANIM_WINDOW_MOVE);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

static void workspace_move_to_output(struct sway_workspace *workspace,
		struct sway_output *output) {
	if (workspace->output == output) {
		return;
	}
	struct sway_output *old_output = workspace->output;
	workspace_detach(workspace);
	struct sway_workspace *new_output_old_ws =
		output_get_active_workspace(output);
	if (!sway_assert(new_output_old_ws, "Expected output to have a workspace")) {
		return;
	}

	output_add_workspace(output, workspace);

	// If moving the last workspace from the old output, create a new workspace
	// on the old output
	struct sway_seat *seat = config->handler_context.seat;
	if (old_output->workspaces->length == 0) {
		char *ws_name = workspace_next_name(old_output->wlr_output->name);
		struct sway_workspace *ws = workspace_create(old_output, ws_name);
		free(ws_name);
		seat_set_raw_focus(seat, &ws->node);
	}

	workspace_consider_destroy(new_output_old_ws);

	output_sort_workspaces(output);
	struct sway_node *focus = seat_get_focus_inactive(seat, &workspace->node);
	seat_set_focus(seat, focus);
	workspace_output_raise_priority(workspace, old_output, output);
	ipc_event_workspace(NULL, workspace, "move");
}

static struct cmd_results *cmd_move_workspace(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "move workspace", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	if (strcasecmp(argv[0], "output") == 0) {
		--argc; ++argv;
	}

	if (!argc) {
		return cmd_results_new(CMD_INVALID,
				"Expected 'move workspace to [output] <output>'");
	}

	struct sway_workspace *workspace = config->handler_context.workspace;
	if (!workspace) {
		return cmd_results_new(CMD_FAILURE, "No workspace to move");
	}

	struct sway_output *old_output = workspace->output;
	int center_x = workspace->width / 2 + workspace->x,
		center_y = workspace->height / 2 + workspace->y;
	struct sway_output *new_output = output_in_direction(argv[0],
			old_output, center_x, center_y);
	if (!new_output) {
		return cmd_results_new(CMD_FAILURE,
			"Can't find output with name/direction '%s'", argv[0]);
	}
	workspace_move_to_output(workspace, new_output);

	arrange_output(old_output);
	arrange_output(new_output);

	struct sway_seat *seat = config->handler_context.seat;
	seat_consider_warp_to_focus(seat);

	animation_create(ANIM_WINDOW_MOVE);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *cmd_move_in_direction(
		enum sway_layout_direction direction, int argc, char **argv) {
	int move_amt = 10;
	bool nomode = false;
	if (argc) {
		if (strcasecmp(argv[0], "nomode") == 0) {
			nomode = true;
		} else {
			char *inv;
			move_amt = (int)strtol(argv[0], &inv, 10);
			if (*inv != '\0' && strcasecmp(inv, "px") != 0) {
				return cmd_results_new(CMD_FAILURE, "Invalid distance specified");
			}
		}
	}

	struct sway_container *container = config->handler_context.container;
	if (!container) {
		return cmd_results_new(CMD_FAILURE,
				"Cannot move workspaces in a direction");
	}
	if (container_is_floating(container)) {
		if (container->pending.fullscreen_mode) {
			return cmd_results_new(CMD_FAILURE,
					"Cannot move fullscreen floating container");
		}
		double lx = container->pending.x;
		double ly = container->pending.y;
		switch (direction) {
		case DIR_LEFT:
			lx -= move_amt;
			break;
		case DIR_RIGHT:
			lx += move_amt;
			break;
		case DIR_UP:
			ly -= move_amt;
			break;
		case DIR_DOWN:
			ly += move_amt;
			break;
		default:
			return cmd_results_new(CMD_FAILURE, "Invalid direction for floating container");
		}
		container_floating_move_to(container, lx, ly);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}
	struct sway_workspace *old_ws = container->pending.workspace;
	struct sway_container *old_parent = container->pending.parent;

	if (!container_move_in_direction(container, direction, nomode)) {
		// Container didn't move
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	// clean-up, destroying parents if the container was the last child
	if (old_parent) {
		container_reap_empty(old_parent);
	} else if (old_ws) {
		workspace_consider_destroy(old_ws);
	}

	struct sway_workspace *new_ws = container->pending.workspace;

	if (root->fullscreen_global) {
		arrange_root();
	} else {
		arrange_workspace(old_ws);
		if (new_ws != old_ws) {
			arrange_workspace(new_ws);
		}
	}

	if (container->view) {
		ipc_event_window(container, "move");
	}

	container_end_mouse_operation(container);

	struct sway_seat *seat = config->handler_context.seat;
	seat_consider_warp_to_focus(seat);

	animation_create(ANIM_WINDOW_MOVE);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *cmd_move_to_position_pointer(
		struct sway_container *container) {
	struct sway_seat *seat = config->handler_context.seat;
	if (!seat->cursor) {
		return cmd_results_new(CMD_FAILURE, "No cursor device");
	}
	struct wlr_cursor *cursor = seat->cursor->cursor;
	/* Determine where to put the window. */
	double lx = cursor->x - container->pending.width / 2;
	double ly = cursor->y - container->pending.height / 2;

	/* Correct target coordinates to be in bounds (on screen). */
	struct wlr_output *output = wlr_output_layout_output_at(
			root->output_layout, cursor->x, cursor->y);
	if (output) {
		struct wlr_box box;
		wlr_output_layout_get_box(root->output_layout, output, &box);
		lx = fmax(lx, box.x);
		ly = fmax(ly, box.y);
		if (lx + container->pending.width > box.x + box.width) {
			lx = box.x + box.width - container->pending.width;
		}
		if (ly + container->pending.height > box.y + box.height) {
			ly = box.y + box.height - container->pending.height;
		}
	}

	/* Actually move the container. */
	container_floating_move_to(container, lx, ly);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static const char expected_position_syntax[] =
	"Expected 'move [absolute] position <x> [px] <y> [px]' or "
	"'move [absolute] position center' or "
	"'move position cursor|mouse|pointer'";

static struct cmd_results *cmd_move_to_position(int argc, char **argv) {
	struct sway_container *container = config->handler_context.container;
	if (!container || !container_is_floating(container)) {
		return cmd_results_new(CMD_FAILURE, "Only floating containers "
				"can be moved to an absolute position");
	}

	if (!argc) {
		return cmd_results_new(CMD_INVALID, "%s", expected_position_syntax);
	}

	bool absolute = false;
	if (strcmp(argv[0], "absolute") == 0) {
		absolute = true;
		--argc;
		++argv;
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, "%s", expected_position_syntax);
	}
	if (strcmp(argv[0], "position") == 0) {
		--argc;
		++argv;
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, "%s", expected_position_syntax);
	}
	if (strcmp(argv[0], "cursor") == 0 || strcmp(argv[0], "mouse") == 0 ||
			strcmp(argv[0], "pointer") == 0) {
		if (absolute) {
			return cmd_results_new(CMD_INVALID, "%s", expected_position_syntax);
		}
		return cmd_move_to_position_pointer(container);
	} else if (strcmp(argv[0], "center") == 0) {
		double lx, ly;
		if (absolute) {
			lx = root->x + (root->width - container->pending.width) / 2;
			ly = root->y + (root->height - container->pending.height) / 2;
		} else {
			struct sway_workspace *ws = container->pending.workspace;
			if (!ws) {
				struct sway_seat *seat = config->handler_context.seat;
				ws = seat_get_focused_workspace(seat);
			}
			lx = ws->x + (ws->width - container->pending.width) / 2;
			ly = ws->y + (ws->height - container->pending.height) / 2;
		}
		container_floating_move_to(container, lx, ly);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (argc < 2) {
		return cmd_results_new(CMD_FAILURE, "%s", expected_position_syntax);
	}

	struct movement_amount lx = { .amount = 0, .unit = MOVEMENT_UNIT_INVALID };
	// X direction
	int num_consumed_args = parse_movement_amount(argc, argv, &lx);
	argc -= num_consumed_args;
	argv += num_consumed_args;
	if (lx.unit == MOVEMENT_UNIT_INVALID) {
		return cmd_results_new(CMD_INVALID, "Invalid x position specified");
	}

	if (argc < 1) {
		return cmd_results_new(CMD_FAILURE, "%s", expected_position_syntax);
	}

	struct movement_amount ly = { .amount = 0, .unit = MOVEMENT_UNIT_INVALID };
	// Y direction
	num_consumed_args = parse_movement_amount(argc, argv, &ly);
	argc -= num_consumed_args;
	argv += num_consumed_args;
	if (argc > 0) {
		return cmd_results_new(CMD_INVALID, "%s", expected_position_syntax);
	}
	if (ly.unit == MOVEMENT_UNIT_INVALID) {
		return cmd_results_new(CMD_INVALID, "Invalid y position specified");
	}

	struct sway_workspace *ws = container->pending.workspace;
	if (!ws) {
		struct sway_seat *seat = config->handler_context.seat;
		ws = seat_get_focused_workspace(seat);
	}

	switch (lx.unit) {
	case MOVEMENT_UNIT_PPT:
		if (container_is_scratchpad_hidden(container)) {
			return cmd_results_new(CMD_FAILURE,
					"Cannot move a hidden scratchpad container by ppt");
		}
		if (absolute) {
			return cmd_results_new(CMD_FAILURE,
					"Cannot move to absolute positions by ppt");
		}
		// Convert to px
		lx.amount = ws->width * lx.amount / 100;
		lx.unit = MOVEMENT_UNIT_PX;
		// Falls through
	case MOVEMENT_UNIT_PX:
	case MOVEMENT_UNIT_DEFAULT:
		break;
	case MOVEMENT_UNIT_INVALID:
		sway_assert(false, "invalid x unit");
		break;
	}

	switch (ly.unit) {
	case MOVEMENT_UNIT_PPT:
		if (container_is_scratchpad_hidden(container)) {
			return cmd_results_new(CMD_FAILURE,
					"Cannot move a hidden scratchpad container by ppt");
		}
		if (absolute) {
			return cmd_results_new(CMD_FAILURE,
					"Cannot move to absolute positions by ppt");
		}
		// Convert to px
		ly.amount = ws->height * ly.amount / 100;
		ly.unit = MOVEMENT_UNIT_PX;
		// Falls through
	case MOVEMENT_UNIT_PX:
	case MOVEMENT_UNIT_DEFAULT:
		break;
	case MOVEMENT_UNIT_INVALID:
		sway_assert(false, "invalid y unit");
		break;
	}
	if (!absolute) {
		lx.amount += ws->x;
		ly.amount += ws->y;
	}
	container_floating_move_to(container, lx.amount, ly.amount);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *cmd_move_to_scratchpad(void) {
	struct sway_node *node = config->handler_context.node;
	struct sway_container *con = config->handler_context.container;
	struct sway_workspace *ws = config->handler_context.workspace;
	if (node->type == N_WORKSPACE && ws->tiling->length == 0) {
		return cmd_results_new(CMD_INVALID,
				"Can't move an empty workspace to the scratchpad");
	}
	if (node->type == N_WORKSPACE) {
		// Wrap the workspace's children in a container
		con = workspace_wrap_children(ws);
		layout_modifiers_set_mode(ws, L_HORIZ);
	}

	// If the container is in a floating split container,
	// operate on the split container instead of the child.
	if (container_is_floating_or_child(con)) {
		while (con->pending.parent) {
			con = con->pending.parent;
		}
	}

	if (!con->scratchpad) {
		root_scratchpad_add_container(con, NULL);
	} else if (con->pending.workspace) {
		root_scratchpad_hide(con);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static const char expected_full_syntax[] = "Expected "
	"'move left|right|up|down|beginning|end [<amount> [px]|nomode]'"
	" or 'move [--no-auto-back-and-forth] [window|container] [to] workspace"
	"  <name>|next|prev|next_on_output|prev_on_output|current|(number <num>)'"
	" or 'move [window|container] [to] output <name/id>|left|right|up|down'"
	" or 'move [window|container] [to] mark <mark>'"
	" or 'move [window|container] [to] scratchpad'"
	" or 'move workspace to [output] <name/id>|left|right|up|down'"
	" or 'move [window|container] [to] [absolute] position <x> [px] <y> [px]'"
	" or 'move [window|container] [to] [absolute] position center'"
	" or 'move [window|container] [to] position mouse|cursor|pointer'";

struct cmd_results *cmd_move(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "move", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}

	if (strcasecmp(argv[0], "left") == 0) {
		return cmd_move_in_direction(DIR_LEFT, --argc, ++argv);
	} else if (strcasecmp(argv[0], "right") == 0) {
		return cmd_move_in_direction(DIR_RIGHT, --argc, ++argv);
	} else if (strcasecmp(argv[0], "up") == 0) {
		return cmd_move_in_direction(DIR_UP, --argc, ++argv);
	} else if (strcasecmp(argv[0], "down") == 0) {
		return cmd_move_in_direction(DIR_DOWN, --argc, ++argv);
	} else if (strcasecmp(argv[0], "beginning") == 0) {
		return cmd_move_in_direction(DIR_BEGIN, --argc, ++argv);
	} else if (strcasecmp(argv[0], "end") == 0) {
		return cmd_move_in_direction(DIR_END, --argc, ++argv);
	} else if (strcasecmp(argv[0], "workspace") == 0 && argc >= 2
			&& (strcasecmp(argv[1], "to") == 0 ||
				strcasecmp(argv[1], "output") == 0)) {
		argc -= 2; argv += 2;
		return cmd_move_workspace(argc, argv);
	}

	bool no_auto_back_and_forth = false;
	if (strcasecmp(argv[0], "--no-auto-back-and-forth") == 0) {
		no_auto_back_and_forth = true;
		--argc; ++argv;
	}

	if (argc > 0 && (strcasecmp(argv[0], "window") == 0 ||
			strcasecmp(argv[0], "container") == 0)) {
		--argc; ++argv;
	}

	if (argc > 0 && strcasecmp(argv[0], "to") == 0) {
		--argc;	++argv;
	}

	if (!argc) {
		return cmd_results_new(CMD_INVALID, "%s", expected_full_syntax);
	}

	// Only `move [window|container] [to] workspace` supports
	// `--no-auto-back-and-forth` so treat others as invalid syntax
	if (no_auto_back_and_forth && strcasecmp(argv[0], "workspace") != 0) {
		return cmd_results_new(CMD_INVALID, "%s", expected_full_syntax);
	}

	if (strcasecmp(argv[0], "workspace") == 0 ||
			strcasecmp(argv[0], "output") == 0 ||
			strcasecmp(argv[0], "mark") == 0) {
		return cmd_move_container(no_auto_back_and_forth, argc, argv);
	} else if (strcasecmp(argv[0], "scratchpad") == 0) {
		return cmd_move_to_scratchpad();
	} else if (strcasecmp(argv[0], "position") == 0 ||
			(argc > 1 && strcasecmp(argv[0], "absolute") == 0 &&
			strcasecmp(argv[1], "position") == 0)) {
		return cmd_move_to_position(argc, argv);
	}
	return cmd_results_new(CMD_INVALID, "%s", expected_full_syntax);
}
