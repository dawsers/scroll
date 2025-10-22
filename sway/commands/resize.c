#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/edges.h>
#include "sway/commands.h"
#include "sway/tree/arrange.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "log.h"
#include "util.h"
#include "sway/desktop/animation.h"

#define AXIS_HORIZONTAL (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)
#define AXIS_VERTICAL   (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)

static uint32_t parse_resize_axis(const char *axis) {
	if (strcasecmp(axis, "width") == 0 || strcasecmp(axis, "horizontal") == 0) {
		return AXIS_HORIZONTAL;
	}
	if (strcasecmp(axis, "height") == 0 || strcasecmp(axis, "vertical") == 0) {
		return AXIS_VERTICAL;
	}
	if (strcasecmp(axis, "up") == 0) {
		return WLR_EDGE_TOP;
	}
	if (strcasecmp(axis, "down") == 0) {
		return WLR_EDGE_BOTTOM;
	}
	if (strcasecmp(axis, "left") == 0) {
		return WLR_EDGE_LEFT;
	}
	if (strcasecmp(axis, "right") == 0) {
		return WLR_EDGE_RIGHT;
	}
	return WLR_EDGE_NONE;
}

static bool is_horizontal(uint32_t axis) {
	return axis & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
}

/**
 * Implement `resize <grow|shrink>` for a floating container.
 */
static struct cmd_results *resize_adjust_floating(uint32_t axis,
		struct movement_amount *amount) {
	struct sway_container *con = config->handler_context.container;
	int grow_width = 0, grow_height = 0;

	if (is_horizontal(axis)) {
		grow_width = amount->amount;
	} else {
		grow_height = amount->amount;
	}

	// Make sure we're not adjusting beyond floating min/max size
	int fmin_width, fmax_width, fmin_height, fmax_height;
	floating_calculate_constraints(&fmin_width, &fmax_width,
			&fmin_height, &fmax_height);
	double min_width, max_width, min_height, max_height;
	view_get_constraints(con->view, &min_width, &max_width,	&min_height, &max_height);
	if (min_width < fmin_width) {
		min_width = fmin_width;
	}
	if (max_width > fmax_width) {
		max_width = fmax_width;
	}
	if (min_height < fmin_height) {
		min_height = fmin_height;
	}
	if (max_height > fmax_height) {
		max_height = fmax_height;
	}

	if (con->pending.width + grow_width < min_width) {
		grow_width = min_width - con->pending.width;
	} else if (con->pending.width + grow_width > max_width) {
		grow_width = max_width - con->pending.width;
	}
	if (con->pending.height + grow_height < min_height) {
		grow_height = min_height - con->pending.height;
	} else if (con->pending.height + grow_height > max_height) {
		grow_height = max_height - con->pending.height;
	}
	int grow_x = 0, grow_y = 0;

	if (axis == AXIS_HORIZONTAL) {
		grow_x = -grow_width / 2;
	} else if (axis == AXIS_VERTICAL) {
		grow_y = -grow_height / 2;
	} else if (axis == WLR_EDGE_TOP) {
		grow_y = -grow_height;
	} else if (axis == WLR_EDGE_LEFT) {
		grow_x = -grow_width;
	}
	if (grow_width == 0 && grow_height == 0) {
		return cmd_results_new(CMD_INVALID, "Cannot resize any further");
	}
	con->pending.x += grow_x;
	con->pending.y += grow_y;
	con->pending.width += grow_width;
	con->pending.height += grow_height;

	con->pending.content_x += grow_x;
	con->pending.content_y += grow_y;
	con->pending.content_width += grow_width;
	con->pending.content_height += grow_height;

	arrange_container(con);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * Implement `resize <grow|shrink>` for a tiled container.
 */
static struct cmd_results *resize_adjust_tiled(uint32_t axis,
		struct movement_amount *amount) {
	struct sway_container *current = config->handler_context.container;
	double min_width, max_width, min_height, max_height;
	view_get_constraints(current->view, &min_width, &max_width, &min_height, &max_height);
	min_width += 2.0 * current->pending.border_thickness;
	max_width += 2.0 * current->pending.border_thickness;
	min_height += 2.0 * current->pending.border_thickness;
	max_height += 2.0 * current->pending.border_thickness;
	enum sway_container_layout layout = layout_get_type(config->handler_context.workspace);
	bool horizontal = is_horizontal(axis);
	if ((layout == L_HORIZ && horizontal) || (layout == L_VERT && !horizontal)) {
		if (current->pending.parent) {
			// Choose parent if not at workspace level yet
			current = current->pending.parent;
		}
	}

	if (container_is_scratchpad_hidden_or_child(current)) {
		return cmd_results_new(CMD_FAILURE, "Cannot resize a hidden scratchpad container");
	}

	if (amount->unit == MOVEMENT_UNIT_DEFAULT) {
		amount->unit = MOVEMENT_UNIT_PPT;
	}
	bool fail = true;
	struct sway_workspace *workspace = current->pending.workspace;
	int gaps = 2 * workspace->gaps_inner;
	if (amount->unit == MOVEMENT_UNIT_PPT) {
		float pct = amount->amount / 100.0f;
		if (horizontal) {
			double new_width = current->width_fraction + pct;
			new_width = MIN(MAX(new_width, 0.05), 1.0);
			double width = workspace->width * new_width - gaps;
			if (width < min_width) {
				if (pct > 0.0f) {
					fail = false;
				}
				width = min_width;
			} else if (width > max_width) {
				if (pct < 0.0) {
					fail = false;
				}
				width = max_width;
			} else if (new_width == current->width_fraction) {
				fail = true;
			} else {
				fail = false;
			}
			current->width_fraction = new_width;
			current->pending.width = width;
			if (layout == L_HORIZ) {
				// If it has children, propagate its width_fraction, overwriting whatever they had
				for (int i = 0; i < current->pending.children->length; ++i) {
					struct sway_container *con = current->pending.children->items[i];
					con->width_fraction = current->width_fraction;
					con->pending.width = workspace->width * con->width_fraction - gaps;
				}
			}
		} else {
			double new_height = current->height_fraction + pct;
			new_height = MIN(MAX(new_height, 0.05), 1.0);
			double height = workspace->height * new_height - gaps;
			if (height < min_height) {
				if (pct > 0.0f) {
					fail = false;
				}
				height = min_height;
			} else if (height > max_height) {
				if (pct < 0.0) {
					fail = false;
				}
				height = max_height;
			} else if (new_height == current->height_fraction) {
				fail = true;
			} else {
				fail = false;
			}
			current->height_fraction = new_height;
			current->pending.height = height;
			if (layout == L_VERT) {
				// If it has children, propagate its height_fraction, overwriting whatever they had
				for (int i = 0; i < current->pending.children->length; ++i) {
					struct sway_container *con = current->pending.children->items[i];
					con->height_fraction = current->height_fraction;
					con->pending.height = workspace->height * con->height_fraction - gaps;
				}
			}
		}
	} else {
		if (horizontal) {
			double new_width = current->pending.width + amount->amount;
			new_width = MIN(MAX(new_width, MIN_SANE_W + gaps), workspace->width - gaps);
			if (new_width < min_width) {
				if (amount->amount >= 0) {
					fail = false;
				}
				new_width = min_width;
			} else if (new_width > max_width) {
				if (amount->amount <= 0) {
					fail = false;
				}
				new_width = max_width;
			} else if (round(new_width) == round(current->pending.width)) {
				fail = true;
			} else {
				fail = false;
			}
			current->pending.width = new_width;
			current->width_fraction = (current->pending.width + gaps) / workspace->width;
			if (layout == L_HORIZ) {
				// If it has children, propagate its width, overwriting whatever they had
				for (int i = 0; i < current->pending.children->length; ++i) {
					struct sway_container *con = current->pending.children->items[i];
					con->pending.width = current->pending.width;
					con->width_fraction = current->width_fraction;
				}
			}
		} else {
			double new_height = current->pending.height + amount->amount;
			new_height = MIN(MAX(new_height, MIN_SANE_H + gaps), workspace->height - gaps);
			if (new_height < min_height) {
				if (amount->amount >= 0) {
					fail = false;
				}
				new_height = min_height;
			} else if (new_height > max_height) {
				if (amount->amount <= 0) {
					fail = false;
				}
				new_height = max_height;
			} else if (round(new_height) == round(current->pending.height)) {
				fail = true;
			} else {
				fail = false;
			}
			current->pending.height = new_height;
			current->height_fraction = (current->pending.height + gaps) / workspace->height;
			if (layout == L_VERT) {
				// If it has children, propagate its height_fraction, overwriting whatever they had
				for (int i = 0; i < current->pending.children->length; ++i) {
					struct sway_container *con = current->pending.children->items[i];
					con->pending.height = current->pending.height;
					con->height_fraction = current->height_fraction;
				}
			}
		}
	}
	if (fail) {
		return cmd_results_new(CMD_INVALID, "Cannot resize any further");
	}

	animation_set_path(config->animations.window_size);

	if (current->pending.parent) {
		arrange_container(current->pending.parent);
	} else {
		arrange_workspace(current->pending.workspace);
	}

	layout_tiling_resize_callback(current);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * Implement `resize set` for a tiled container.
 */
static struct cmd_results *resize_set_tiled(struct sway_container *container,
		struct movement_amount *width, struct movement_amount *height) {

	if (container_is_scratchpad_hidden_or_child(container)) {
		return cmd_results_new(CMD_FAILURE, "Cannot resize a hidden scratchpad container");
	}

	struct sway_workspace *workspace = container->pending.workspace;
	enum sway_container_layout layout = layout_get_type(workspace);

	bool fail = true;
	int gaps = 2 * workspace->gaps_inner;
	if (width->amount) {
		struct sway_container *con = container;
		if (layout == L_HORIZ && con->pending.parent) {
			con = con->pending.parent;
		}
		if (width->unit == MOVEMENT_UNIT_PPT ||
				width->unit == MOVEMENT_UNIT_DEFAULT) {
			if (width->amount <= 1.0 && width->amount >= 0.05) {
				fail = false;
				con->width_fraction = width->amount;
				con->pending.width = workspace->width * con->width_fraction - gaps;
				// If it has children, propagate its width, overwriting whatever they had
				if (con->pending.children) {
					for (int i = 0; i < con->pending.children->length; ++i) {
						struct sway_container *child = con->pending.children->items[i];
						child->width_fraction = con->width_fraction;
						child->pending.width = workspace->width * child->width_fraction - gaps;
					}
				}
			}
		} else {
			if (width->amount <= workspace->width - gaps && width->amount >= MIN_SANE_W + gaps) {
				fail = false;
				con->pending.width = width->amount;
				con->width_fraction = (con->pending.width + gaps) / workspace->width;
				// If it has children, propagate its width, overwriting whatever they had
				if (con->pending.children) {
					for (int i = 0; i < con->pending.children->length; ++i) {
						struct sway_container *child = con->pending.children->items[i];
						child->pending.width = width->amount;
						child->width_fraction = con->width_fraction;
					}
				}
			}
		}
	}
	if (height->amount) {
		struct sway_container *con = container;
		if (layout == L_VERT && con->pending.parent) {
			con = con->pending.parent;
		}
		if (height->unit == MOVEMENT_UNIT_PPT ||
				height->unit == MOVEMENT_UNIT_DEFAULT) {
			if (height->amount <= 1.0 && height->amount >= 0.05) {
				fail = false;
				con->height_fraction = height->amount;
				con->pending.height = con->pending.workspace->height * con->height_fraction - gaps;
				// If it has children, propagate its height, overwriting whatever they had
				if (con->pending.children) {
					for (int i = 0; i < con->pending.children->length; ++i) {
						struct sway_container *child = con->pending.children->items[i];
						child->height_fraction = con->height_fraction;
						child->pending.height = workspace->height * child->height_fraction - gaps;
					}
				}
			}
		} else {
			if (height->amount <= workspace->height - gaps && height->amount >= MIN_SANE_H + gaps) {
				fail = false;
				con->pending.height = height->amount;
				con->height_fraction = (con->pending.height + gaps) / workspace->height;
				// If it has children, propagate its width, overwriting whatever they had
				if (con->pending.children) {
					for (int i = 0; i < con->pending.children->length; ++i) {
						struct sway_container *child = con->pending.children->items[i];
						child->pending.height = height->amount;
						child->height_fraction = con->height_fraction;
					}
				}
			}
		}
	}
	if (fail) {
		return cmd_results_new(CMD_INVALID, "Invalid size");
	}

	animation_set_path(config->animations.window_size);

	if (container->pending.parent) {
		arrange_container(container->pending.parent);
	} else {
		arrange_workspace(container->pending.workspace);
	}

	layout_tiling_resize_callback(container);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * Implement `resize set` for a floating container.
 */
static struct cmd_results *resize_set_floating(struct sway_container *con,
		struct movement_amount *width, struct movement_amount *height) {
	int min_width, max_width, min_height, max_height, grow_width = 0, grow_height = 0;
	floating_calculate_constraints(&min_width, &max_width,
			&min_height, &max_height);

	if (width->amount) {
		switch (width->unit) {
		case MOVEMENT_UNIT_PPT:
			if (container_is_scratchpad_hidden(con)) {
				return cmd_results_new(CMD_FAILURE,
						"Cannot resize a hidden scratchpad container by ppt");
			}
			// Convert to px
			width->amount = con->pending.workspace->width * width->amount / 100;
			width->unit = MOVEMENT_UNIT_PX;
			// Falls through
		case MOVEMENT_UNIT_PX:
		case MOVEMENT_UNIT_DEFAULT:
			width->amount = fmax(min_width, fmin(width->amount, max_width));
			grow_width = width->amount - con->pending.width;
			con->pending.x -= grow_width / 2;
			con->pending.width = width->amount;
			break;
		case MOVEMENT_UNIT_INVALID:
			sway_assert(false, "invalid width unit");
			break;
		}
	}

	if (height->amount) {
		switch (height->unit) {
		case MOVEMENT_UNIT_PPT:
			if (container_is_scratchpad_hidden(con)) {
				return cmd_results_new(CMD_FAILURE,
						"Cannot resize a hidden scratchpad container by ppt");
			}
			// Convert to px
			height->amount = con->pending.workspace->height * height->amount / 100;
			height->unit = MOVEMENT_UNIT_PX;
			// Falls through
		case MOVEMENT_UNIT_PX:
		case MOVEMENT_UNIT_DEFAULT:
			height->amount = fmax(min_height, fmin(height->amount, max_height));
			grow_height = height->amount - con->pending.height;
			con->pending.y -= grow_height / 2;
			con->pending.height = height->amount;
			break;
		case MOVEMENT_UNIT_INVALID:
			sway_assert(false, "invalid height unit");
			break;
		}
	}

	con->pending.content_x -= grow_width / 2;
	con->pending.content_y -= grow_height / 2;
	con->pending.content_width += grow_width;
	con->pending.content_height += grow_height;

	arrange_container(con);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * resize set <args>
 *
 * args: [width] <width> [px|ppt]
 *     : height <height> [px|ppt]
 *     : [width] <width> [px|ppt] [height] <height> [px|ppt]
 */
static struct cmd_results *cmd_resize_set(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "resize", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	const char usage[] = "Expected 'resize set [width] <width> [px|ppt]' or "
		"'resize set height <height> [px|ppt]' or "
		"'resize set [width] <width> [px|ppt] [height] <height> [px|ppt]'";

	// Width
	struct movement_amount width = {0};
	if (argc >= 2 && !strcmp(argv[0], "width") && strcmp(argv[1], "height")) {
		argc--; argv++;
	}
	if (strcmp(argv[0], "height")) {
		int num_consumed_args = parse_movement_amount(argc, argv, &width);
		argc -= num_consumed_args;
		argv += num_consumed_args;
		if (width.unit == MOVEMENT_UNIT_INVALID) {
			return cmd_results_new(CMD_INVALID, "%s", usage);
		}
	}

	// Height
	struct movement_amount height = {0};
	if (argc) {
		if (argc >= 2 && !strcmp(argv[0], "height")) {
			argc--; argv++;
		}
		int num_consumed_args = parse_movement_amount(argc, argv, &height);
		if (argc > num_consumed_args) {
			return cmd_results_new(CMD_INVALID, "%s", usage);
		}
		if (height.unit == MOVEMENT_UNIT_INVALID) {
			return cmd_results_new(CMD_INVALID, "%s", usage);
		}
	}

	// If 0, don't resize that dimension
	struct sway_container *con = config->handler_context.container;
	if (width.amount <= 0) {
		width.amount = con->pending.width;
	}
	if (height.amount <= 0) {
		height.amount = con->pending.height;
	}

	if (container_is_floating(con)) {
		return resize_set_floating(con, &width, &height);
	}
	return resize_set_tiled(con, &width, &height);
}

/**
 * resize <grow|shrink> <args>
 *
 * args: <direction>
 * args: <direction> <amount> <unit>
 * args: <direction> <amount> <unit> or <amount> <other_unit>
 */
static struct cmd_results *cmd_resize_adjust(int argc, char **argv,
		int multiplier) {
	const char usage[] = "Expected 'resize grow|shrink <direction> "
		"[<amount> px|ppt [or <amount> px|ppt]]'";
	uint32_t axis = parse_resize_axis(*argv);
	if (axis == WLR_EDGE_NONE) {
		return cmd_results_new(CMD_INVALID, "%s", usage);
	}
	--argc; ++argv;

	// First amount
	struct movement_amount first_amount;
	if (argc) {
		int num_consumed_args = parse_movement_amount(argc, argv, &first_amount);
		argc -= num_consumed_args;
		argv += num_consumed_args;
		if (first_amount.unit == MOVEMENT_UNIT_INVALID) {
			return cmd_results_new(CMD_INVALID, "%s", usage);
		}
	} else {
		first_amount.amount = 10;
		first_amount.unit = MOVEMENT_UNIT_DEFAULT;
	}

	// "or"
	if (argc) {
		if (strcmp(*argv, "or") != 0) {
			return cmd_results_new(CMD_INVALID, "%s", usage);
		}
		--argc; ++argv;
	}

	// Second amount
	struct movement_amount second_amount;
	if (argc) {
		int num_consumed_args = parse_movement_amount(argc, argv, &second_amount);
		if (argc > num_consumed_args) {
			return cmd_results_new(CMD_INVALID, "%s", usage);
		}
		if (second_amount.unit == MOVEMENT_UNIT_INVALID) {
			return cmd_results_new(CMD_INVALID, "%s", usage);
		}
	} else {
		second_amount.amount = 0;
		second_amount.unit = MOVEMENT_UNIT_INVALID;
	}

	first_amount.amount *= multiplier;
	second_amount.amount *= multiplier;

	struct sway_container *con = config->handler_context.container;
	if (container_is_floating(con)) {
		// Floating containers can only resize in px. Choose an amount which
		// uses px, with fallback to an amount that specified no unit.
		if (first_amount.unit == MOVEMENT_UNIT_PX) {
			return resize_adjust_floating(axis, &first_amount);
		} else if (second_amount.unit == MOVEMENT_UNIT_PX) {
			return resize_adjust_floating(axis, &second_amount);
		} else if (first_amount.unit == MOVEMENT_UNIT_DEFAULT) {
			return resize_adjust_floating(axis, &first_amount);
		} else if (second_amount.unit == MOVEMENT_UNIT_DEFAULT) {
			return resize_adjust_floating(axis, &second_amount);
		} else {
			return cmd_results_new(CMD_INVALID,
					"Floating containers cannot use ppt measurements");
		}
	}

	// For tiling, prefer ppt -> default -> px
	if (first_amount.unit == MOVEMENT_UNIT_PPT) {
		return resize_adjust_tiled(axis, &first_amount);
	} else if (second_amount.unit == MOVEMENT_UNIT_PPT) {
		return resize_adjust_tiled(axis, &second_amount);
	} else if (first_amount.unit == MOVEMENT_UNIT_DEFAULT) {
		return resize_adjust_tiled(axis, &first_amount);
	} else if (second_amount.unit == MOVEMENT_UNIT_DEFAULT) {
		return resize_adjust_tiled(axis, &second_amount);
	} else {
		return resize_adjust_tiled(axis, &first_amount);
	}
}

struct cmd_results *cmd_resize(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_container *current = config->handler_context.container;
	if (!current) {
		return cmd_results_new(CMD_INVALID, "Cannot resize nothing");
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "resize", EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	if (strcasecmp(argv[0], "set") == 0) {
		return cmd_resize_set(argc - 1, &argv[1]);
	}
	if (strcasecmp(argv[0], "grow") == 0) {
		return cmd_resize_adjust(argc - 1, &argv[1], 1);
	}
	if (strcasecmp(argv[0], "shrink") == 0) {
		return cmd_resize_adjust(argc - 1, &argv[1], -1);
	}

	const char usage[] = "Expected 'resize <set|shrink|grow> "
		"<width|height|up|down|left|right> [<amount>] [px|ppt]'";

	return cmd_results_new(CMD_INVALID, "%s", usage);
}
