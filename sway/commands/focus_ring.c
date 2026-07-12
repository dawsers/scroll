#include <strings.h>
#include <wayland-util.h>
#include "sway/commands.h"
#include "sway/input/seat.h"
#include "sway/tree/node.h"
#include "sway/tree/focus_ring.h"
#include "util.h"

static struct cmd_results *ring_next(struct sway_seat *seat) {
	focus_ring_next(root->focus_ring, seat);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *ring_prev(struct sway_seat *seat) {
	focus_ring_prev(root->focus_ring, seat);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *ring_first(struct sway_seat *seat) {
	focus_ring_first(root->focus_ring, seat);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *ring_last(struct sway_seat *seat) {
	focus_ring_last(root->focus_ring, seat);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *ring_set(struct sway_seat *seat) {
	struct sway_container *container = config->handler_context.container;
	if (container && container->view) {
		focus_ring_set(root->focus_ring, seat, container->view);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_focus_ring_length(int argc, char **argv) {
	struct cmd_results *error = checkarg(argc, "focus_ring_length", EXPECTED_EQUAL_TO, 1);
	if (error) {
		return error;
	}
	long focus_ring_length;
	if (!parse_integer(argv[0], &focus_ring_length)) {
		return cmd_results_new(CMD_INVALID, "Invalid parameter '%s'", argv[1]);
	}

	config->focus_ring_length = focus_ring_length;
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_focus_ring(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "focus_ring", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	struct sway_seat *seat = config->handler_context.seat;

	if (strcasecmp(argv[0], "next") == 0) {
		return ring_next(seat);
	} else if (strcasecmp(argv[0], "prev") == 0) {
		return ring_prev(seat);
	} else if (strcasecmp(argv[0], "first") == 0) {
		return ring_first(seat);
	} else if (strcasecmp(argv[0], "last") == 0) {
		return ring_last(seat);
	} else if (strcasecmp(argv[0], "set") == 0) {
		return ring_set(seat);
	} else {
		return cmd_results_new(CMD_INVALID, "Expected 'focus_ring prev|next|first|last|set'");
	}
}
