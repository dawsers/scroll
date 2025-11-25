#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_buffer.h>
#include "sway/config.h"
#include "sway/scene_descriptor.h"
#include "sway/desktop/idle_inhibit_v1.h"
#include "sway/desktop/transaction.h"
#include "sway/desktop/animation.h"
#include "sway/input/cursor.h"
#include "sway/input/input-manager.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/container.h"
#include "sway/tree/node.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/tree/layout.h"
#include "list.h"
#include "log.h"
#include "util.h"

struct sway_transaction {
	struct wl_event_source *timer;
	list_t *instructions;   // struct sway_transaction_instruction *
	size_t num_waiting;
	size_t num_configures;
	struct timespec commit_time;
};

struct sway_transaction_instruction {
	struct sway_transaction *transaction;
	struct sway_node *node;
	union {
		struct sway_output_state output_state;
		struct sway_workspace_state workspace_state;
		struct sway_container_state container_state;
	};
	uint32_t serial;
	bool server_request;
	bool waiting;
};

static struct sway_transaction *transaction_create(void) {
	struct sway_transaction *transaction =
		calloc(1, sizeof(struct sway_transaction));
	if (!sway_assert(transaction, "Unable to allocate transaction")) {
		return NULL;
	}
	transaction->instructions = create_list();
	return transaction;
}

static void transaction_destroy(struct sway_transaction *transaction) {
	// Free instructions
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct sway_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct sway_node *node = instruction->node;
		node->ntxnrefs--;
		if (node->instruction == instruction) {
			node->instruction = NULL;
		}
		if (node->destroying && node->ntxnrefs == 0) {
			switch (node->type) {
			case N_ROOT:
				sway_assert(false, "Never reached");
				break;
			case N_OUTPUT:
				output_destroy(node->sway_output);
				break;
			case N_WORKSPACE:
				workspace_destroy(node->sway_workspace);
				break;
			case N_CONTAINER:
				container_destroy(node->sway_container);
				break;
			}
		}
		free(instruction);
	}
	list_free(transaction->instructions);

	if (transaction->timer) {
		wl_event_source_remove(transaction->timer);
	}
	free(transaction);
}

static void copy_output_state(struct sway_output *output,
		struct sway_transaction_instruction *instruction) {
	struct sway_output_state *state = &instruction->output_state;
	if (state->workspaces) {
		state->workspaces->length = 0;
	} else {
		state->workspaces = create_list();
	}
	list_cat(state->workspaces, output->workspaces);

	state->active_workspace = output_get_active_workspace(output);
}

static void copy_workspace_state(struct sway_workspace *ws,
		struct sway_transaction_instruction *instruction) {
	struct sway_workspace_state *state = &instruction->workspace_state;

	state->fullscreen = ws->fullscreen;
	state->x = ws->x;
	state->y = ws->y;
	state->width = ws->width;
	state->height = ws->height;

	if (state->floating) {
		state->floating->length = 0;
	} else {
		state->floating = create_list();
	}
	if (state->tiling) {
		state->tiling->length = 0;
	} else {
		state->tiling = create_list();
	}
	list_cat(state->floating, ws->floating);
	list_cat(state->tiling, ws->tiling);

	struct sway_seat *seat = input_manager_current_seat();
	state->focused = seat_get_focus(seat) == &ws->node;

	// Set focused_inactive_child to the direct tiling child
	struct sway_container *focus = seat_get_focus_inactive_tiling(seat, ws);
	if (focus) {
		while (focus->pending.parent) {
			focus = focus->pending.parent;
		}
	}
	state->focused_inactive_child = focus;
}

static void copy_container_state(struct sway_container *container,
		struct sway_transaction_instruction *instruction) {
	struct sway_container_state *state = &instruction->container_state;

	if (state->children) {
		list_free(state->children);
	}

	memcpy(state, &container->pending, sizeof(struct sway_container_state));

	if (!container->view) {
		// We store a copy of the child list to avoid having it mutated after
		// we copy the state.
		state->children = create_list();
		list_cat(state->children, container->pending.children);
	} else {
		state->children = NULL;
	}

	struct sway_seat *seat = input_manager_current_seat();
	state->focused = seat_get_focus(seat) == &container->node;

	if (!container->view) {
		struct sway_node *focus =
			seat_get_active_tiling_child(seat, &container->node);
		state->focused_inactive_child = focus ? focus->sway_container : NULL;
	}
}

static void transaction_add_node(struct sway_transaction *transaction,
		struct sway_node *node, bool server_request) {
	struct sway_transaction_instruction *instruction = NULL;

	// Check if we have an instruction for this node already, in which case we
	// update that instead of creating a new one.
	if (node->ntxnrefs > 0) {
		for (int idx = 0; idx < transaction->instructions->length; idx++) {
			struct sway_transaction_instruction *other =
				transaction->instructions->items[idx];
			if (other->node == node) {
				instruction = other;
				break;
			}
		}
	}

	if (!instruction) {
		instruction = calloc(1, sizeof(struct sway_transaction_instruction));
		if (!sway_assert(instruction, "Unable to allocate instruction")) {
			return;
		}
		instruction->transaction = transaction;
		instruction->node = node;
		instruction->server_request = server_request;

		list_add(transaction->instructions, instruction);
		node->ntxnrefs++;
	} else if (server_request) {
		instruction->server_request = true;
	}

	switch (node->type) {
	case N_ROOT:
		break;
	case N_OUTPUT:
		copy_output_state(node->sway_output, instruction);
		break;
	case N_WORKSPACE:
		copy_workspace_state(node->sway_workspace, instruction);
		break;
	case N_CONTAINER:
		copy_container_state(node->sway_container, instruction);
		break;
	}
}

static void apply_output_state(struct sway_output *output,
		struct sway_output_state *state) {
	list_free(output->current.workspaces);
	memcpy(&output->current, state, sizeof(struct sway_output_state));
}

static void apply_workspace_state(struct sway_workspace *ws,
		struct sway_workspace_state *state) {
	list_free(ws->current.floating);
	list_free(ws->current.tiling);
	memcpy(&ws->current, state, sizeof(struct sway_workspace_state));
}

static void apply_container_state(struct sway_container *container,
		struct sway_container_state *state) {
	struct sway_view *view = container->view;
	// There are separate children lists for each instruction state, the
	// container's current state and the container's pending state
	// (ie. con->children). The list itself needs to be freed here.
	// Any child containers which are being deleted will be cleaned up in
	// transaction_destroy().
	list_free(container->current.children);

	memcpy(&container->current, state, sizeof(struct sway_container_state));

	if (view) {
		if (view->saved_surface_tree) {
			if (!container->node.destroying || container->node.ntxnrefs == 1) {
				view_remove_saved_buffer(view);
			}
		}

		// If the view hasn't responded to the configure, center it within
		// the container. This is important for fullscreen views which
		// refuse to resize to the size of the output.
		if (view->surface) {
			view_center_and_clip_surface(view);
		}
	}
}

static void arrange_title_bar(struct sway_container *con,
		double x, double y, double width, double height) {
	container_update(con);

	bool has_title_bar = height > 0;
	sway_scene_node_set_enabled(&con->title_bar.tree->node, has_title_bar);
	if (!has_title_bar) {
		return;
	}

	sway_scene_node_set_position(&con->title_bar.tree->node, x, y);

	con->title_width = width;
	container_arrange_title_bar(con);
}

static void disable_container(struct sway_container *con) {
	if (con->view) {
		sway_scene_node_reparent(&con->view->scene_tree->node, con->content_tree);
	} else {
		for (int i = 0; i < con->current.children->length; i++) {
			struct sway_container *child = con->current.children->items[i];

			sway_scene_node_reparent(&child->scene_tree->node, con->content_tree);

			disable_container(child);
		}
	}
}

static void arrange_container(struct sway_container *con,
		double dwidth, double dheight, bool title_bar, int gaps,
		struct sway_workspace *workspace);

static double get_active_position_pin(struct sway_workspace *workspace,
		enum sway_container_layout layout, list_t *children, int active_idx,
		int gaps, float scale, struct sway_container *pin) {
	// We consider all the possible positions where each container is next to
	// the pin. We choose the one that shows the active container and makes it
	// move as little as possible.
	struct sway_container *active = children->items[active_idx];
	int pin_idx = list_find(children, pin);
	if (layout == L_HORIZ) {
		// Add/substract 1 to account for rounding errors due to widths/heights
		// computed using layout fractions. The extra pixel will be absorbed by
		// the gaps.
		const double workspace_beg = workspace->x - 1;
		const double workspace_end = workspace->x + workspace->width + 1;
		const double a_x = active->pending.x;
		if (layout_pin_get_position(workspace) == PIN_BEGINNING) {
			if (active == pin) {
				return workspace->x + scale * gaps;
			}
			// Set the active to the right of the pin, and test how many before it
			// fit while keeping the active inside of the viewport. Choose the one
			// that moves the active column the least.
			double cx0 = workspace->x + scale * (pin->pending.width + 2.0 * gaps);
			int c = active_idx;
			double best_movement = cx0 + scale * gaps - a_x;
			const double caw = scale * (active->pending.width + 2.0 * gaps);
			for (int i = active_idx - 1; i >= 0; i--) {
				struct sway_container *con = children->items[i];
				if (con != pin) {
					double cw = scale * (con->current.width +  2.0 * gaps);
					cx0 += cw;
					if (cx0 + caw > workspace_end) {
						break;
					}
					const double a_x0 = cx0 + scale * gaps;
					if (fabs(a_x0 - a_x) < fabs(best_movement)) {
						best_movement = a_x0 - a_x;
						c = i;
					}
				}
			}
			if (pin_idx >= c) {
				list_move_to(children, c, pin);
			} else {
				list_move_to(children, c - 1, pin);
			}
			return best_movement + a_x;
		} else {
			if (active == pin) {
				return workspace->x + workspace->width - scale * (pin->pending.width + gaps);
			}
			double cx1 = workspace->x + workspace->width - scale * (pin->pending.width + 2.0 * gaps);
			int c = active_idx;
			const double caw = scale * (active->pending.width + 2.0 * gaps);
			double best_movement = cx1 - caw + scale * gaps - a_x;
			for (int i = active_idx + 1; i < children->length; ++i) {
				struct sway_container *con = children->items[i];
				if (con != pin) {
					double cw = scale * (con->current.width +  2.0 * gaps);
					cx1 -= cw;
					if (cx1 - caw < workspace_beg) {
						break;
					}
					const double a_x0 = cx1 - caw + scale * gaps;
					if (fabs(a_x0 - a_x) < fabs(best_movement)) {
						best_movement = a_x0 - a_x;
						c = i;
					}
				}
			}
			if (pin_idx >= c) {
				list_move_to(children, c + 1, pin);
			} else {
				list_move_to(children, c, pin);
			}
			return best_movement + a_x;
		}
	} else {
		const double workspace_beg = workspace->y - 1;
		const double workspace_end = workspace->y + workspace->height + 1;
		const double a_y = active->pending.y;
		if (layout_pin_get_position(workspace) == PIN_BEGINNING) {
			if (active == pin) {
				return workspace->y + scale * gaps;
			}
			double cy0 = workspace->y + scale * (pin->pending.height + 2.0 * gaps);
			int c = active_idx;
			double best_movement = cy0 + scale * gaps - a_y;
			const double cah = scale * (active->pending.height + 2.0 * gaps);
			for (int i = active_idx - 1; i >= 0; i--) {
				struct sway_container *con = children->items[i];
				if (con != pin) {
					double ch = scale * (con->current.height +  2.0 * gaps);
					cy0 += ch;
					if (cy0 + cah > workspace_end) {
						break;
					}
					const double a_y0 = cy0 + scale * gaps;
					if (fabs(a_y0 - a_y) < fabs(best_movement)) {
						best_movement = a_y0 - a_y;
						c = i;
					}
				}
			}
			if (pin_idx >= c) {
				list_move_to(children, c, pin);
			} else {
				list_move_to(children, c - 1, pin);
			}
			return best_movement + a_y;
		} else {
			if (active == pin) {
				return workspace->y + workspace->height - scale * (pin->pending.height + gaps);
			}
			double cy1 = workspace->y + workspace->height - scale * (pin->pending.height + 2.0 * gaps);
			int c = active_idx;
			const double cah = scale * (active->pending.height + 2.0 * gaps);
			double best_movement = cy1 - cah + scale * gaps - a_y;
			for (int i = active_idx + 1; i < children->length; ++i) {
				struct sway_container *con = children->items[i];
				if (con != pin) {
					double ch = scale * (con->current.height +  2.0 * gaps);
					cy1 -= ch;
					if (cy1 - cah < workspace_beg) {
						break;
					}
					const double a_y0 = cy1 - cah + scale * gaps;
					if (fabs(a_y0 - a_y) < fabs(best_movement)) {
						best_movement = a_y0 - a_y;
						c = i;
					}
				}
			}
			if (pin_idx >= c) {
				list_move_to(children, c + 1, pin);
			} else {
				list_move_to(children, c, pin);
			}
			return best_movement + a_y;
		}
	}
}

static double get_active_position(struct sway_workspace *workspace,
		enum sway_container_layout layout, list_t *children, int active_idx,
		int gaps, float scale) {
	// We consider all the possible positions where each container is at the
	// left/top edge and at the right/bottom edge. We choose the one that leaves
	// the active container inside the viewport, moves the active as little as
	// possible, and leaves no empty space in the viewport.

	// First, find the range of containers that being on each edge of the viewport,
	// allow the active one to be seen completely.
	bool move = false;
	double best_movement = DBL_MAX;
	int c_l = 0, c_r = children->length - 1;
	struct sway_container *active = children->items[active_idx];
	if (layout == L_HORIZ) {
		// Add/substract 1 to account for rounding errors due to widths/heights
		// computed using layout fractions. The extra pixel will be absorbed by
		// the gaps.
		const double workspace_beg = workspace->x - 1;
		const double workspace_end = workspace->x + workspace->width + 1;
		const double a_x = active->pending.x;
		// Set the active at the left/top edge and test how many after it
		// fit within the viewport fully
		double x1 = workspace->x + scale * gaps;
		for (int c = active_idx; c < children->length; ++c) {
			struct sway_container *con = children->items[c];
			x1 += scale * (con->current.width + gaps);
			// For those that fit, locate each one at the end of the viewport,
			// and check the previous ones don't leave any empty space.
			if (x1 <= workspace_end) {
				c_r = c;
				// Test from c_r to the beginning
				double movement = DBL_MAX;
				double cx0 = workspace->x + workspace->width;
				bool space = true;
				for (int i = c_r; i >= 0; i--) {
					struct sway_container *con = children->items[i];
					cx0 -= scale * (con->current.width + 2.0 * gaps);
					if (i == active_idx) {
						movement = (cx0 + scale * gaps) - a_x;
					}
					if (cx0 <= workspace_beg) {
						space = false;
						if (cx0 < workspace_beg) {
							break;
						}
					}
				}
				if (!space && fabs(movement) < fabs(best_movement)) {
					move = true;
					best_movement = movement;
				}
			} else {
				break;
			}
		}
		// Set the active at the right/bottom edge and test how many before it
		// fit within the viewport fully
		double x0 = workspace->x + workspace->width - scale * gaps;
		for (int c = active_idx; c >= 0; --c) {
			struct sway_container *con = children->items[c];
			x0 -= scale * (con->current.width + gaps);
			// For those that fit, locate each one at the beginning of the viewport,
			// and check the next ones don't leave any empty space.
			if (x0 >= workspace_beg) {
				c_l = c;
				// Test from c_l to the end
				double movement = DBL_MAX;
				double cx1 = workspace->x;
				bool space = true;
				for (int i = c_l; i < children->length; ++i) {
					if (i == active_idx) {
						movement = (cx1 + scale * gaps) - a_x;
					}
					struct sway_container *con = children->items[i];
					cx1 += scale * (con->current.width + 2.0 * gaps);
					if (cx1 >= workspace_end) {
						space = false;
						if (cx1 > workspace_end) {
							break;
						}
					}
				}
				if (!space && fabs(movement) < fabs(best_movement)) {
					move = true;
					best_movement = movement;
				}
			} else {
				break;
			}
		}
		if (!move) {
			return a_x;
		}
		return a_x + best_movement;
	} else {
		const double workspace_beg = workspace->y - 1;
		const double workspace_end = workspace->y + workspace->height + 1;
		const double a_y = active->pending.y;
		double y0 = workspace->y + workspace->height - scale * gaps;
		for (int c = active_idx; c >= 0; --c) {
			struct sway_container *con = children->items[c];
			y0 -= scale * (con->current.height + gaps);
			if (y0 >= workspace_beg) {
				c_l = c;
				// Test from c_l to the end
				double movement = DBL_MAX;
				double cy1 = workspace->y;
				bool space = true;
				for (int i = c_l; i < children->length; ++i) {
					if (i == active_idx) {
						movement = (cy1 + scale * gaps) - a_y;
					}
					struct sway_container *con = children->items[i];
					cy1 += scale * (con->current.height + 2.0 * gaps);
					if (cy1 >= workspace_end) {
						space = false;
						if (cy1 > workspace_end) {
							break;
						}
					}
				}
				if (!space && fabs(movement) < fabs(best_movement)) {
					move = true;
					best_movement = movement;
				}
			} else {
				break;
			}
		}
		double y1 = workspace->y + scale * gaps;
		for (int c = active_idx; c < children->length; ++c) {
			struct sway_container *con = children->items[c];
			y1 += scale * (con->current.height + gaps);
			if (y1 <= workspace_end) {
				c_r = c;
				// Test from c_r to the beginning
				double movement = DBL_MAX;
				double cy0 = workspace->y + workspace->height;
				bool space = true;
				for (int i = c_r; i >= 0; i--) {
					struct sway_container *con = children->items[i];
					cy0 -= scale * (con->current.height + 2.0 * gaps);
					if (i == active_idx) {
						movement = (cy0 + scale * gaps) - a_y;
					}
					if (cy0 <= workspace_beg) {
						space = false;
						if (cy0 < workspace_beg) {
							break;
						}
					}
				}
				if (!space && fabs(movement) < fabs(best_movement)) {
					move = true;
					best_movement = movement;
				}
			} else {
				break;
			}
		}
		if (!move) {
			return a_y;
		}
		return a_y + best_movement;
	}
}

// Workspace stores in x, y the logical coordinate that will be applied to a node
// with x, y = 0, and it includes gaps_out. So if we want to place
// a container on the leftmost possible position, we should use gaps_in, and it will
// include both gaps, out and the container's in. Workspace also stores its width
// and height, and they are the effective, available space: viewport resolution
// minus 2 * gaps_out.
static double compute_active_offset(struct sway_workspace *workspace,
		enum sway_container_layout layout, list_t *children, int active_idx,
		int width, int height, int gaps, struct sway_container *pin) {
	struct sway_container *active = children->items[active_idx];
	// Offsets may be wrong, so consider only the active position and all the
	// widths and order are valid. Also, when the workspace is scaled, offsets
	// and sizes are not.
	double scale = layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0;
	if (layout == L_HORIZ) {
		bool center = layout_modifiers_get_center_horizontal(workspace);
		if (center) {
			return workspace->x + 0.5 * (width - scale * active->current.width);
		}
		// Center row if space available
        double lwidth = 0, rwidth = 0;
        for (int i = 0; i < active_idx; ++i) {
			struct sway_container *con = children->items[i];
            lwidth += scale * (con->current.width + 2 * gaps);
        }
		lwidth = round(lwidth);
        for (int i = active_idx; i < children->length; ++i) {
			struct sway_container *con = children->items[i];
            rwidth += scale * (con->current.width + 2 * gaps);
        }
		rwidth = round(rwidth);
        double twidth = lwidth + rwidth;
        if (twidth <= width + 1) {
            double start = 0.5 * (width - twidth);
            return workspace->x + start + lwidth + scale * gaps;
        }
	} else {
		bool center = layout_modifiers_get_center_vertical(workspace);
		if (center) {
			return workspace->y + 0.5 * (height - scale * active->pending.height);
		}
		// Center row if space available
        double lheight = 0, rheight = 0;
        for (int i = 0; i < active_idx; ++i) {
			struct sway_container *con = children->items[i];
            lheight += scale * (con->current.height + 2 * gaps);
        }
		lheight = round(lheight);
        for (int i = active_idx; i < children->length; ++i) {
			struct sway_container *con = children->items[i];
            rheight += scale * (con->current.height + 2 * gaps);
        }
		rheight = round(rheight);
        double theight = lheight + rheight;
        if (theight <= height + 1) {
            double start = 0.5 * (height - theight);
            return workspace->y + start + lheight + scale * gaps;
        }
	}
	if (pin) {
		return get_active_position_pin(workspace, layout, children, active_idx, gaps, scale, pin);
	} else {
		return get_active_position(workspace, layout, children, active_idx, gaps, scale);
	}
}

static void default_arrange_children(struct sway_workspace *workspace,
		enum sway_container_layout layout, list_t *children,
		struct sway_container *active, struct sway_scene_tree *content,
		int width, int height, int gaps) {

	if (children->length == 0) {
		return;
	}

	int active_idx = list_find(children, active);
	if (active_idx == -1) {
		active_idx = 0;
		active = children->items[active_idx];
	}
	float scale = layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0f;

	struct sway_container *pin = layout_pin_enabled(workspace) ?
		layout_pin_get_container(workspace) : NULL;

	if (layout != layout_get_type(workspace)) {
		pin = NULL;
	}

	double offset;
	if (workspace->gesture.scrolling || layout_modifiers_get_reorder(workspace) == REORDER_LAZY) {
		offset = layout == L_HORIZ ? active->pending.x : active->pending.y;
	} else {
		if (active->pending.fullscreen_layout == FULLSCREEN_ENABLED && !layout_scale_enabled(workspace)) {
			if (layout == L_HORIZ) {
				offset = workspace->split.split != WORKSPACE_SPLIT_NONE ? workspace->split.output_area.x : workspace->output->lx;
			} else {
				offset = workspace->split.split != WORKSPACE_SPLIT_NONE ? workspace->split.output_area.y : workspace->output->ly;
			}
		} else {
			offset = compute_active_offset(workspace, layout, children, active_idx,
				workspace->width, workspace->height, gaps, pin);
			if (pin) {
				// active may have moved because of pin, recompute
				active_idx = list_find(children, active);
				if (active_idx == -1) {
					active_idx = 0;
				}
				active = children->items[active_idx];
			}
		}
	}

	double t, x, y, anim_scale;
	animation_get_values(&t, &x, &y, &anim_scale);
	if (layout == L_VERT) {
		double off = offset;
		for (int i = active_idx; i < children->length; ++i) {
			struct sway_container *child = children->items[i];
			struct sway_container *parent = child->pending.parent;
			if (parent && parent->jump.jumping) {
				off = child->pending.y;
			}
			child->animation.ht = fmax(1, linear_scale(child->animation.h0, child->animation.h1, t));
			animation_set_animation_enabled(child->animation.h1 != child->animation.h0);
			sway_scene_node_set_enabled(&child->decoration.tree->node, true);
			double movement = fabs(off - child->animation.y0);
			if (movement > 0.0) {
				child->animation.yt = linear_scale(child->animation.y0, off, x);
				animation_set_animation_enabled(true);
			} else if (fabs(child->pending.x - child->animation.x0) > 0.0) {
				child->animation.yt = child->animation.y0 + y * anim_scale * workspace->height;
			} else {
				child->animation.yt = child->animation.y0;
				animation_set_animation_enabled(false);
			}
			sway_scene_node_set_position(&child->scene_tree->node, 0, child->animation.yt - workspace->y);
			child->current.y = off;
			child->pending.y = off;
			if (parent) {
				child->current.x = parent->current.x;
				child->pending.x = parent->pending.x;
			} else {
				child->current.x = workspace->x + scale * gaps;
				child->pending.x = workspace->x + scale * gaps;
			}
			sway_scene_node_reparent(&child->scene_tree->node, content);
			child->animation.wt = fmax(1, linear_scale(child->animation.w0, child->animation.w1, t));
			animation_set_animation_enabled(child->animation.w1 != child->animation.w0);
			arrange_container(child, child->animation.wt, child->animation.ht, true, gaps, workspace);
			off += scale * (child->pending.height + 2 * gaps);
		}
		off = offset;
		for (int i = active_idx - 1; i >= 0; i--) {
			struct sway_container *child = children->items[i];
			struct sway_container *parent = child->pending.parent;
			child->animation.ht = fmax(1, linear_scale(child->animation.h0, child->animation.h1, t));
			animation_set_animation_enabled(child->animation.h1 != child->animation.h0);
			off -= scale * (child->pending.height + 2 * gaps);
			if (parent && parent->jump.jumping) {
				off = child->pending.y;
			}
			child->current.y = off;
			child->pending.y = off;
			if (parent) {
				child->current.x = parent->current.x;
				child->pending.x = parent->pending.x;
			} else {
				child->current.x = workspace->x + scale * gaps;
				child->pending.x = workspace->x + scale * gaps;
			}
			sway_scene_node_set_enabled(&child->decoration.tree->node, true);
			double movement = fabs(off - child->animation.y0);
			if (movement > 0.0) {
				child->animation.yt = linear_scale(child->animation.y0, off, x);
				animation_set_animation_enabled(true);
			} else if (fabs(child->pending.x - child->animation.x0) > 0.0) {
				child->animation.yt = child->animation.y0 + y * anim_scale * workspace->height;
			} else {
				child->animation.yt = child->animation.y0;
				animation_set_animation_enabled(false);
			}
			sway_scene_node_set_position(&child->scene_tree->node, 0, child->animation.yt - workspace->y);
			sway_scene_node_reparent(&child->scene_tree->node, content);
			child->animation.wt = fmax(1, linear_scale(child->animation.w0, child->animation.w1, t));
			animation_set_animation_enabled(child->animation.w1 != child->animation.w0);
			arrange_container(child, child->animation.wt, child->animation.ht, true, gaps, workspace);
		}
	} else if (layout == L_HORIZ) {
		double off = offset;
		for (int i = active_idx; i < children->length; ++i) {
			struct sway_container *child = children->items[i];
			struct sway_container *parent = child->pending.parent;
			if (parent && parent->jump.jumping) {
				off = child->pending.x;
			}
			child->animation.wt = fmax(1, linear_scale(child->animation.w0, child->animation.w1, t));
			animation_set_animation_enabled(child->animation.w1 != child->animation.w0);
			double movement = fabs(off - child->animation.x0);
			if (movement > 0.0) {
				child->animation.xt = linear_scale(child->animation.x0, off, x);
				animation_set_animation_enabled(true);
			} else if (fabs(child->pending.y - child->animation.y0) > 0.0) {
				child->animation.xt = child->animation.x0 + y * anim_scale * workspace->width;
			} else {
				child->animation.xt = child->animation.x0;
				animation_set_animation_enabled(false);
			}
			sway_scene_node_set_enabled(&child->decoration.tree->node, true);
			sway_scene_node_set_position(&child->scene_tree->node, child->animation.xt - workspace->x, 0);
			// Update child for next iteration. Transactions don't re-arrange
			// the layout (arrange.c:apply_xxx()), so we need to set it here,
			// otherwise the next call will have the positions wrong and the
			// offset won't be optimal.
			child->current.x = off;
			child->pending.x = off;
			if (parent) {
				child->current.y = parent->current.y;
				child->pending.y = parent->pending.y;
			} else {
				child->current.y = workspace->y + scale * gaps;
				child->pending.y = workspace->y + scale * gaps;
			}
			sway_scene_node_reparent(&child->scene_tree->node, content);
			child->animation.ht = fmax(1, linear_scale(child->animation.h0, child->animation.h1, t));
			animation_set_animation_enabled(child->animation.h1 != child->animation.h0);
			arrange_container(child, child->animation.wt, child->animation.ht, true, gaps, workspace);
			off += scale * (child->pending.width + 2 * gaps);
		}
		off = offset;
		for (int i = active_idx - 1; i >= 0; i--) {
			struct sway_container *child = children->items[i];
			struct sway_container *parent = child->pending.parent;
			child->animation.wt = fmax(1, linear_scale(child->animation.w0, child->animation.w1, t));
			animation_set_animation_enabled(child->animation.w1 != child->animation.w0);
			off -= scale * (child->pending.width + 2 * gaps);
			if (parent && parent->jump.jumping) {
				off = child->pending.x;
			}
			child->current.x = off;
			child->pending.x = off;
			if (parent) {
				child->current.y = parent->current.y;
				child->pending.y = parent->pending.y;
			} else {
				child->current.y = workspace->y + scale * gaps;
				child->pending.y = workspace->y + scale * gaps;
			}
			sway_scene_node_set_enabled(&child->decoration.tree->node, true);
			double movement = fabs(off - child->animation.x0);
			if (movement > 0.0) {
				child->animation.xt = linear_scale(child->animation.x0, off, x);
				animation_set_animation_enabled(true);
			} else if (fabs(child->pending.y - child->animation.y0) > 0.0) {
				child->animation.xt = child->animation.x0 + y * anim_scale * workspace->width;
			} else {
				child->animation.xt = child->animation.x0;
				animation_set_animation_enabled(false);
			}
			sway_scene_node_set_position(&child->scene_tree->node, child->animation.xt - workspace->x, 0);
			sway_scene_node_reparent(&child->scene_tree->node, content);
			child->animation.ht = fmax(1, linear_scale(child->animation.h0, child->animation.h1, t));
			animation_set_animation_enabled(child->animation.h1 != child->animation.h0);
			arrange_container(child, child->animation.wt, child->animation.ht, true, gaps, workspace);
		}
	} else {
		sway_assert(false, "unreachable");
	}
}

static void map_to_configure(double content_x, double content_y, double content_width,
		double content_height, int *cx, int *cy, int *cw, int *ch) {
	// We need to match what we did in view_configure()
	*cx = round(content_x);
	*cy = round(content_y);
	int ex = round(content_x + content_width);
	int ey = round(content_y + content_height);
	*cw = ex - *cx;
	*ch = ey - *cy;
}

static void animation_arrange_children(struct sway_workspace *workspace,
		enum sway_container_layout layout, list_t *children,
		struct sway_scene_tree *content);

static void arrange_container(struct sway_container *con,
		double dwidth, double dheight, bool title_bar, int gaps,
		struct sway_workspace *workspace) {
	if (!root->filters.container_filter(workspace, con, root->filters.container_filter_data)) {
		sway_scene_node_set_enabled(&con->scene_tree->node, false);
		return;
	}
	// this container might have previously been in the scratchpad,
	// make sure it's enabled for viewing
	sway_scene_node_set_enabled(&con->scene_tree->node, true);

	if (con->output_handler) {
		sway_scene_buffer_set_dest_size(con->output_handler, dwidth, dheight);
	}

	if (con->view) {
		if (con->pending.fullscreen_layout == FULLSCREEN_ENABLED) {
			sway_scene_node_set_position(&con->view->scene_tree->node, 0, 0);
			sway_scene_node_set_enabled(&con->title_bar.tree->node, false);
			sway_scene_node_set_enabled(&con->decoration.full->node, false);
			return;
		}
		double scale = layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0;
		double border_top = container_titlebar_height() * scale;
		double border_width = MAX(1, con->current.border_thickness * scale);
		double width = scale * dwidth;
		double height = scale * dheight;

		if (title_bar && con->current.border != B_NORMAL) {
			sway_scene_node_set_enabled(&con->title_bar.tree->node, false);
		}

		if (con->current.border == B_NORMAL) {
			if (title_bar) {
				arrange_title_bar(con, 0, 0, dwidth, border_top);
			} else {
				border_top = 0;
				// should be handled by the parent container
			}
			border_top += border_width;
		} else if (con->current.border == B_PIXEL) {
			container_update(con);
			border_top = title_bar && con->current.border_top ? border_width : 0;
		} else if (con->current.border == B_NONE) {
			container_update(con);
			border_top = 0;
			border_width = 0;
		} else if (con->current.border == B_CSD) {
			border_top = 0;
			border_width = 0;
			con->decoration.full->title_bar = false;
		} else {
			sway_assert(false, "unreachable");
		}

		if (con->current.border != B_NORMAL || !title_bar) {
			sway_scene_decoration_set_title_bar(con->decoration.full, false, 0, 0);
		}

		double border_left = con->current.border_left ? border_width : 0;
		sway_scene_decoration_set_size(con->decoration.full, width, height);
		sway_scene_decoration_set_border_width(con->decoration.full, border_width);
		sway_scene_decoration_set_border_radius(con->decoration.full, con->pending.decoration.border_radius * scale);
		double shadow_offset[2] = {
			con->pending.decoration.shadow_offset_x,
			con->pending.decoration.shadow_offset_y
		};
		sway_scene_node_set_enabled(&con->shadow->node, false);
		struct sway_scene_decoration * decoration = con->decoration.full;
		struct sway_scene_shadow *shadow = con->shadow;
		double sx, sy, sw, sh;
		const double size = con->pending.decoration.shadow_size;
		if (con->pending.decoration.shadow_dynamic) {
			// For a height (or width) of 0.5 root->height, compute ratio d/D so 
			// height projected is height + 2 * size
			double w = 0.5 * root->width * scale;
			double h = 0.5 * root->height * scale;
			double ratio = root->width >= root->height ?
				h / (h + 2 * size * scale) :
				w / (w + 2 * size * scale);
			// Coordinates relative to center
			double x = 0.5 * root->width - con->pending.x;
			double y = 0.5 * root->height - con->pending.y;
			double proj_x = 0.5 * root->width - x / ratio;
			double proj_y = 0.5 * root->height - y / ratio;
			sx = decoration->node.x + proj_x - con->pending.x;
			sy = decoration->node.y + proj_y - con->pending.y;
			sw = width / ratio;
			sh = height / ratio;
		} else {
			sx = decoration->node.x + (shadow_offset[0] - size) * scale;
			sy = decoration->node.y + (shadow_offset[1] - size) * scale;
			sw = width + 2.0 * scale * (size);
			sh = height + 2.0 * scale * (size);
		}
		sway_scene_node_set_position(&shadow->node, sx, sy);

		float shadow_color[4] = {
			con->pending.decoration.shadow_color_r,
			con->pending.decoration.shadow_color_g,
			con->pending.decoration.shadow_color_b,
			con->pending.decoration.shadow_color_a,
		};
		sway_scene_shadow_set_properties(shadow, sw, sh,
			con->pending.decoration.shadow, con->pending.decoration.shadow_dynamic,
			con->pending.decoration.shadow_size,
			con->pending.decoration.shadow_blur, shadow_offset, shadow_color);
		sway_scene_node_set_enabled(&con->decoration.full->node, true);

		// make sure to reparent, it's possible that the client just came out of
		// fullscreen mode where the parent of the surface is not the container
		sway_scene_node_reparent(&con->view->scene_tree->node, con->content_tree);
		sway_scene_node_set_position(&con->view->scene_tree->node,
			border_left, border_top);

		sway_scene_node_set_enabled(&shadow->node, shadow->enabled);

		// Update content geometry
		view_autoconfigure(con->view);
#if WLR_HAS_XWAYLAND
		// Re-configure Xwayland views that change position. The reason is unlike
		// sway, we update the positions of containers when the transaction is
		// committed (instead of every time a arrange.c:arrange_XXX() happens.
		// Views are configured before the transaction re-arranges containers,
		// so their position may be wrong. This is especially important for
		// Xwayland windows, because their buffers use absolute positions, and
		// popup positions could then be wrong..
		// Here we configure any view that has changed position.
		if(con->view->type == SWAY_VIEW_XWAYLAND) {
			// Only update the view at the end of the animation to avoid stress
			double t, x, y, off;
			animation_get_values(&t, &x, &y, &off);
			int pcx, pcy, pcw, pch;
			map_to_configure(con->pending.content_x, con->pending.content_y,
				con->pending.content_width, con->pending.content_height,
				&pcx, &pcy, &pcw, &pch);
			int ocx, ocy, ocw, och;
			map_to_configure(con->old_content.x, con->old_content.y,
				con->old_content.width, con->old_content.height,
				&ocx, &ocy, &ocw, &och);
			if (t >= 1.0 &&
				(pcx != ocx || pcy != ocy || pcw != ocw || pch != och)) {
				view_configure(con->view, con->pending.content_x, con->pending.content_y,
					con->pending.content_width, con->pending.content_height);
				con->current.content_x = con->pending.content_x;
				con->current.content_y = con->pending.content_y;
				con->current.content_width = con->pending.content_width;
				con->current.content_height = con->pending.content_height;
			}
		}
#endif
		view_reconfigure(con->view);
	} else {
		// make sure to disable the title bar if the parent is not managing it
		if (title_bar) {
			sway_scene_node_set_enabled(&con->title_bar.tree->node, false);
		}

		if (!root->filters.free_animation_activation_filter(workspace, root->filters.free_animation_activation_filter_data)) {
			default_arrange_children(workspace, con->current.layout,
				con->current.children, con->current.focused_inactive_child,
				con->content_tree, dwidth, dheight, gaps);
		} else {
			animation_arrange_children(workspace, con->current.layout,
				con->current.children, con->content_tree);
		}
	}
}

static int container_get_gaps(struct sway_container *con) {
	struct sway_workspace *ws = con->current.workspace;
	return ws->gaps_inner;
}

static void arrange_fullscreen(struct sway_scene_tree *tree,
		struct sway_container *fs, struct sway_workspace *ws,
		int width, int height) {
	struct sway_scene_node *fs_node;
	if (fs->view) {
		fs_node = &fs->view->scene_tree->node;

		// if we only care about the view, disable any decorations
		sway_scene_node_set_enabled(&fs->scene_tree->node, false);
	} else {
		fs_node = &fs->scene_tree->node;
		arrange_container(fs, width, height, true, container_get_gaps(fs), fs->current.workspace);
	}

	if (ws) {
		// When we change focus in workspace full screen mode, we avoid disabling and enabling
		// full screen mode by calling container_pass_fullscreen(). This only calls
		// transaction_commit_dirty() at the end, so the old node may be in the
		// list of full screen nodes when we insert the new one. Remove any old
		// nodes before adding the new one (there should be just one).
		struct sway_scene_node *node, *tmp;
		wl_list_for_each_safe(node, tmp, &tree->children, link) {
			sway_scene_node_reparent(node, node->old_parent);
		}

		fs_node->old_parent = fs_node->parent;
	}
	sway_scene_node_reparent(fs_node, tree);
	sway_scene_node_lower_to_bottom(fs_node);
	struct sway_output *output = ws->output;
	if (output && ws->split.split != WORKSPACE_SPLIT_NONE) {
		sway_scene_node_set_position(fs_node, ws->split.output_area.x - output->lx,
			ws->split.output_area.y - output->ly);
	} else {
		sway_scene_node_set_position(fs_node, 0, 0);
	}
}

static void arrange_workspace_floating(struct sway_workspace *ws) {
	enum sway_layout_overview mode = layout_overview_mode(ws);
	if (mode == OVERVIEW_ALL || mode == OVERVIEW_FLOATING) {
		layout_overview_recompute_scale(ws, ws->gaps_inner);
	}

	for (int i = 0; i < ws->current.floating->length; i++) {
		struct sway_container *floater = ws->current.floating->items[i];
		struct sway_scene_tree *layer = root->layers.floating;

		if (floater->current.fullscreen_mode != FULLSCREEN_NONE) {
			continue;
		}

		if (root->fullscreen_global) {
			if (container_is_transient_for(floater, root->fullscreen_global)) {
				layer = root->layers.fullscreen_global;
			}
		} else {
			for (int i = 0; i < root->outputs->length; i++) {
				struct sway_output *output = root->outputs->items[i];
				struct sway_workspace *active = output->current.active_workspace;

				if (active && active->fullscreen &&
						container_is_transient_for(floater, active->fullscreen)) {
					layer = root->layers.fullscreen;
				}
			}
		}

		sway_scene_node_reparent(&floater->scene_tree->node, layer);
		if (layout_scale_enabled(ws)) {
			const float scale = layout_scale_get(ws);
			const double minx = ws->output->lx + 0.5 * ws->output->width * (1.0 - scale);
			const double miny = ws->output->ly + 0.5 * ws->output->height * (1.0 - scale);
			const double x = minx + scale * (floater->current.x - ws->output->lx);
			const double y = miny + scale * (floater->current.y - ws->output->ly);
			sway_scene_node_set_position(&floater->scene_tree->node, x, y);

		} else {
			sway_scene_node_set_position(&floater->scene_tree->node,
				floater->current.x, floater->current.y);
		}
		sway_scene_node_set_enabled(&floater->scene_tree->node, true);
		sway_scene_node_set_enabled(&floater->decoration.tree->node, true);

		arrange_container(floater, floater->current.width, floater->current.height,
			true, ws->gaps_inner, ws);
	}
}

static void arrange_workspace_tiling(struct sway_workspace *ws,
		int width, int height) {
	if (ws->tiling->length == 0) {
		return;
	}
	enum sway_layout_overview mode = layout_overview_mode(ws);
	if (mode == OVERVIEW_ALL || mode == OVERVIEW_TILING) {
		layout_overview_recompute_scale(ws, ws->gaps_inner);
	}
	if (!root->filters.free_animation_activation_filter(ws, root->filters.free_animation_activation_filter_data)) {
		default_arrange_children(ws, layout_get_type(ws), ws->tiling,
			ws->current.focused_inactive_child, ws->layers.tiling,
			width, height, ws->gaps_inner);
	} else {
		animation_arrange_children(ws, layout_get_type(ws), ws->tiling,
			ws->layers.tiling);
	}
}

static void disable_workspace(struct sway_workspace *ws) {
	// if any containers were just moved to a disabled workspace it will
	// have the parent of the old workspace. Move the workspace so that it won't
	// be shown.
	for (int i = 0; i < ws->current.tiling->length; i++) {
		struct sway_container *child = ws->current.tiling->items[i];

		sway_scene_node_reparent(&child->scene_tree->node, ws->layers.tiling);
		disable_container(child);
	}

	for (int i = 0; i < ws->current.floating->length; i++) {
		struct sway_container *floater = ws->current.floating->items[i];
		sway_scene_node_reparent(&floater->scene_tree->node, root->layers.floating);
		disable_container(floater);
		sway_scene_node_set_enabled(&floater->scene_tree->node, false);
	}
}

static void arrange_output(struct sway_output *output, int width, int height) {
	for (int i = 0; i < output->current.workspaces->length; i++) {
		struct sway_workspace *child = output->current.workspaces->items[i];

		bool activated = root->filters.workspace_filter(child, root->filters.workspace_filter_data);

		sway_scene_node_reparent(&child->layers.tiling->node, output->layers.tiling);
		sway_scene_node_reparent(&child->layers.fullscreen->node, output->layers.fullscreen);

		bool floating = root->filters.workspace_floating_filter(child, root->filters.workspace_floating_filter_data);
		bool tiling = root->filters.workspace_tiling_filter(child, root->filters.workspace_tiling_filter_data);

		for (int i = 0; i < child->current.floating->length; i++) {
			struct sway_container *floater = child->current.floating->items[i];
			sway_scene_node_reparent(&floater->scene_tree->node, root->layers.floating);
			sway_scene_node_set_enabled(&floater->scene_tree->node, activated && floating);
		}

		if (activated) {
			struct sway_container *fs = child->current.fullscreen;
			if (!fs && child->split.split != WORKSPACE_SPLIT_NONE) {
				fs = child->split.sibling->current.fullscreen;
			}
			sway_scene_node_set_enabled(&child->layers.tiling->node, !fs && tiling);
			sway_scene_node_set_enabled(&child->layers.fullscreen->node, fs);

			sway_scene_node_set_enabled(&output->layers.shell_background->node, !fs);
			sway_scene_node_set_enabled(&output->layers.shell_bottom->node, !fs);
			sway_scene_node_set_enabled(&output->layers.fullscreen->node, fs);

			if (fs) {
				disable_workspace(child);

				if (child->current.fullscreen) {
					sway_scene_rect_set_size(output->fullscreen_background, width, height);

					if (floating) {
						arrange_workspace_floating(child);
					}
					arrange_fullscreen(child->layers.fullscreen, fs, child,
						width, height);
				}
			} else {
				struct wlr_box *area = workspace_get_output_usable_area(child);
				struct side_gaps *gaps = &child->current_gaps;

				sway_scene_node_set_position(&child->layers.tiling->node,
					gaps->left + area->x, gaps->top + area->y);

				if (tiling) {
					arrange_workspace_tiling(child,
						area->width - gaps->left - gaps->right,
						area->height - gaps->top - gaps->bottom);
				}
				if (floating) {
					arrange_workspace_floating(child);
				}
			}
		} else {
			sway_scene_node_set_enabled(&child->layers.tiling->node, false);
			sway_scene_node_set_enabled(&child->layers.fullscreen->node, false);

			disable_workspace(child);
		}
	}
}

void arrange_popups(struct sway_scene_tree *popups) {
	struct sway_scene_node *node;
	wl_list_for_each(node, &popups->children, link) {
		struct sway_popup_desc *popup = scene_descriptor_try_get(node,
			SWAY_SCENE_DESC_POPUP);

		if (popup) {
			double lx, ly;
			sway_scene_node_coords(popup->relative, &lx, &ly);
			sway_scene_node_set_position(node, lx, ly);
		}
	}
}

static void arrange_root(struct sway_root *root) {
	struct sway_container *fs = root->fullscreen_global;

	sway_scene_node_set_enabled(&root->layers.shell_background->node, !fs);
	sway_scene_node_set_enabled(&root->layers.shell_bottom->node, !fs);
	sway_scene_node_set_enabled(&root->layers.tiling->node, !fs);
	sway_scene_node_set_enabled(&root->layers.floating->node, !fs);
	sway_scene_node_set_enabled(&root->layers.shell_top->node, !fs);
	sway_scene_node_set_enabled(&root->layers.fullscreen->node, !fs);

	// hide all contents in the scratchpad
	for (int i = 0; i < root->scratchpad->length; i++) {
		struct sway_container *con = root->scratchpad->items[i];

		// When a container is moved to a scratchpad, it's possible that it
		// was moved into a floating container as part of the same transaction.
		// In this case, we need to make sure we reparent all the container's
		// children so that disabling the container will disable all descendants.
		if (!con->view) for (int ii = 0; ii < con->current.children->length; ii++) {
			struct sway_container *child = con->current.children->items[ii];
			sway_scene_node_reparent(&child->scene_tree->node, con->content_tree);
		}

		sway_scene_node_set_enabled(&con->scene_tree->node, false);
	}

	if (fs) {
		for (int i = 0; i < root->outputs->length; i++) {
			struct sway_output *output = root->outputs->items[i];
			bool activated = root->filters.output_filter(output, root->filters.output_filter_data);
			if (!output->enabled || !output->wlr_output->enabled || !activated) {
				continue;
			}
			struct sway_workspace *ws = output->current.active_workspace;

			sway_scene_output_set_position(output->scene_output, output->lx, output->ly);

			// disable all workspaces to get to a known state
			for (int j = 0; j < output->current.workspaces->length; j++) {
				struct sway_workspace *workspace = output->current.workspaces->items[j];
				disable_workspace(workspace);
			}

			// arrange the active workspace
			if (ws) {
				arrange_workspace_floating(ws);
			}
		}

		arrange_fullscreen(root->layers.fullscreen_global, fs, NULL,
			root->width, root->height);
	} else {
		for (int i = 0; i < root->outputs->length; i++) {
			struct sway_output *output = root->outputs->items[i];
			bool activated = root->filters.output_filter(output, root->filters.output_filter_data);
			if (!output->enabled || !output->wlr_output->enabled || !activated) {
				continue;
			}

			sway_scene_output_set_position(output->scene_output, output->lx, output->ly);

			sway_scene_node_reparent(&output->layers.shell_background->node, root->layers.shell_background);
			sway_scene_node_reparent(&output->layers.shell_bottom->node, root->layers.shell_bottom);
			sway_scene_node_reparent(&output->layers.tiling->node, root->layers.tiling);
			sway_scene_node_reparent(&output->layers.shell_top->node, root->layers.shell_top);
			sway_scene_node_reparent(&output->layers.shell_overlay->node, root->layers.shell_overlay);
			sway_scene_node_reparent(&output->layers.fullscreen->node, root->layers.fullscreen);
			sway_scene_node_reparent(&output->layers.session_lock->node, root->layers.session_lock);

			sway_scene_node_set_position(&output->layers.shell_background->node, output->lx, output->ly);
			sway_scene_node_set_position(&output->layers.shell_bottom->node, output->lx, output->ly);
			sway_scene_node_set_position(&output->layers.tiling->node, output->lx, output->ly);
			sway_scene_node_set_position(&output->layers.fullscreen->node, output->lx, output->ly);
			sway_scene_node_set_position(&output->layers.shell_top->node, output->lx, output->ly);
			sway_scene_node_set_position(&output->layers.shell_overlay->node, output->lx, output->ly);
			sway_scene_node_set_position(&output->layers.session_lock->node, output->lx, output->ly);

			sway_scene_node_set_enabled(&output->layers.shell_background->node, output->layer_shell_mask & LAYER_SHELL_BACKGROUND);
			sway_scene_node_set_enabled(&output->layers.shell_bottom->node, output->layer_shell_mask & LAYER_SHELL_BOTTOM);
			sway_scene_node_set_enabled(&output->layers.shell_top->node, output->layer_shell_mask & LAYER_SHELL_TOP);
			sway_scene_node_set_enabled(&output->layers.shell_overlay->node, output->layer_shell_mask & LAYER_SHELL_OVERLAY);

			arrange_output(output, output->width, output->height);
		}
	}

	arrange_popups(root->layers.popup);
}

static void set_animation_data(struct sway_transaction *transaction) {
	animation_reset_outputs();
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct sway_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct sway_node *node = instruction->node;

		switch (node->type) {
		case N_ROOT:
			break;
		case N_OUTPUT:
			animation_add_output(node->sway_output->wlr_output);
			break;
		case N_WORKSPACE: {
			struct sway_workspace *workspace = node->sway_workspace;
			if (workspace->output) {
				animation_add_output(workspace->output->wlr_output);
			}
			break;
			}
		case N_CONTAINER: {
			struct sway_container *container = node->sway_container;
			struct sway_workspace *current = container->current.workspace;
			if (current && current->output) {
				animation_add_output(current->output->wlr_output);
			}
			struct sway_workspace *pending = container->pending.workspace;
			if (pending && pending->output) {
				animation_add_output(pending->output->wlr_output);
			}
			break;
			}
		}
	}
}

/**
 * Apply a transaction to the "current" state of the tree.
 */
static void transaction_apply(struct sway_transaction *transaction) {
	sway_log(SWAY_DEBUG, "Applying transaction %p", transaction);
	if (debug.txn_timings) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		struct timespec *commit = &transaction->commit_time;
		float ms = (now.tv_sec - commit->tv_sec) * 1000 +
			(now.tv_nsec - commit->tv_nsec) / 1000000.0;
		sway_log(SWAY_DEBUG, "Transaction %p: %.1fms waiting "
				"(%.1f frames if 60Hz)", transaction, ms, ms / (1000.0f / 60));
	}

	// Apply the instruction state to the node's current state
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct sway_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct sway_node *node = instruction->node;

		switch (node->type) {
		case N_ROOT:
			break;
		case N_OUTPUT:
			apply_output_state(node->sway_output, &instruction->output_state);
			break;
		case N_WORKSPACE:
			apply_workspace_state(node->sway_workspace,
					&instruction->workspace_state);
			break;
		case N_CONTAINER:
			apply_container_state(node->sway_container,
					&instruction->container_state);
			break;
		}

		node->instruction = NULL;
	}
}

static void animation_arrange_children(struct sway_workspace *workspace,
		enum sway_container_layout layout, list_t *children,
		struct sway_scene_tree *content) {
	if (children->length == 0) {
		return;
	}

	double t, x, y, anim_scale;
	animation_get_values(&t, &x, &y, &anim_scale);
	if (layout == L_VERT) {
		for (int i = 0; i < children->length; ++i) {
			struct sway_container *child = children->items[i];
			const double off = child->pending.y;
			struct sway_container *parent = child->pending.parent;
			child->animation.ht = fmax(1, linear_scale(child->animation.h0, child->animation.h1, t));
			animation_set_animation_enabled(child->animation.h1 != child->animation.h0);
			sway_scene_node_set_enabled(&child->decoration.tree->node, true);
			double movement = fabs(off - child->animation.y0);
			if (movement > 0.0) {
				child->animation.yt = linear_scale(child->animation.y0, off, x);
				animation_set_animation_enabled(true);
			} else if (fabs(child->pending.x - child->animation.x0) > 0.0) {
				child->animation.yt = child->animation.y0 + y * anim_scale * workspace->height;
			} else {
				child->animation.yt = child->animation.y0;
				animation_set_animation_enabled(false);
			}
			sway_scene_node_set_position(&child->scene_tree->node, 0, child->animation.yt - workspace->y);
			child->current.y = off;
			child->pending.y = off;
			if (parent) {
				child->current.x = parent->current.x;
				child->pending.x = parent->pending.x;
			}
			sway_scene_node_reparent(&child->scene_tree->node, content);
			child->animation.wt = fmax(1, linear_scale(child->animation.w0, child->animation.w1, t));
			animation_set_animation_enabled(child->animation.w1 != child->animation.w0);
			arrange_container(child, child->animation.wt, child->animation.ht, true, 0, workspace);
		}
	} else if (layout == L_HORIZ) {
		for (int i = 0; i < children->length; ++i) {
			struct sway_container *child = children->items[i];
			const double off = child->pending.x;
			struct sway_container *parent = child->pending.parent;
			animation_set_animation_enabled(child->animation.w1 != child->animation.w0);
			child->animation.wt = fmax(1, linear_scale(child->animation.w0, child->animation.w1, t));
			double movement = fabs(off - child->animation.x0);
			if (movement > 0.0) {
				child->animation.xt = linear_scale(child->animation.x0, off, x);
				animation_set_animation_enabled(true);
			} else if (fabs(child->pending.y - child->animation.y0) > 0.0) {
				child->animation.xt = child->animation.x0 + y * anim_scale * workspace->width;
			} else {
				child->animation.xt = child->animation.x0;
				animation_set_animation_enabled(false);
			}
			sway_scene_node_set_enabled(&child->decoration.tree->node, true);
			sway_scene_node_set_position(&child->scene_tree->node, child->animation.xt - workspace->x, 0);
			// Update child for next iteration. Transactions don't re-arrange
			// the layout (arrange.c:apply_xxx()), so we need to set it here,
			// otherwise the next call will have the positions wrong and the
			// offset won't be optimal.
			child->current.x = off;
			child->pending.x = off;
			if (parent) {
				child->current.y = parent->current.y;
				child->pending.y = parent->pending.y;
			}
			sway_scene_node_reparent(&child->scene_tree->node, content);
			animation_set_animation_enabled(child->animation.h1 != child->animation.h0);
			child->animation.ht = fmax(1, linear_scale(child->animation.h0, child->animation.h1, t));
			arrange_container(child, child->animation.wt, child->animation.ht, true, 0, workspace);
		}
	} else {
		sway_assert(false, "unreachable");
	}
}

static void animation_callback(void *data) {
	arrange_root(root);
}

static void animation_callback_end(void *data) {
	cursor_rebase_all();
}

void config_default_animation_callbacks() {
	struct sway_animation_callbacks callbacks;
	callbacks.callback_begin = NULL;
	callbacks.callback_begin_data = NULL;
	callbacks.callback_step = animation_callback;
	callbacks.callback_step_data = NULL;
	callbacks.callback_end = animation_callback_end;
	callbacks.callback_end_data = NULL;
	animation_set_default_callbacks(&callbacks);
}

static void transaction_commit_pending(void);

static void transaction_progress(void) {
	if (!server.queued_transaction) {
		return;
	}
	if (server.queued_transaction->num_waiting > 0) {
		return;
	}
	set_animation_data(server.queued_transaction);
	transaction_apply(server.queued_transaction);
	animation_begin();
	cursor_rebase_all();
	transaction_destroy(server.queued_transaction);
	server.queued_transaction = NULL;

	if (!server.pending_transaction) {
		struct sway_seat *seat = input_manager_get_default_seat();
		struct sway_node *node = seat_get_focus(seat);
		if (node && node->type == N_CONTAINER) {
			switch (node_get_focus_warp(node)) {
			case FOCUS_WARP_NONE:
				break;
			case FOCUS_WARP_DEFAULT:
				cursor_warp_to_container(seat->cursor, node->sway_container, false);
				node_set_focus_warp(node, FOCUS_WARP_NONE);
				break;
			case FOCUS_WARP_FORCE:
				cursor_warp_to_container(seat->cursor, node->sway_container, true);
				node_set_focus_warp(node, FOCUS_WARP_NONE);
				break;
			}
		}
		sway_idle_inhibit_v1_check_active();
		return;
	}

	transaction_commit_pending();
}

static int handle_timeout(void *data) {
	struct sway_transaction *transaction = data;
	sway_log(SWAY_DEBUG, "Transaction %p timed out (%zi waiting)",
			transaction, transaction->num_waiting);
	transaction->num_waiting = 0;
	transaction_progress();
	return 0;
}

static bool should_configure(struct sway_node *node,
		struct sway_transaction_instruction *instruction) {
	if (!node_is_view(node)) {
		return false;
	}
	if (node->destroying) {
		return false;
	}
	if (!instruction->server_request) {
		return false;
	}
	struct sway_container_state *cstate = &node->sway_container->current;
	struct sway_container_state *istate = &instruction->container_state;
#if WLR_HAS_XWAYLAND
	// Xwayland views are position-aware and need to be reconfigured
	// when their position changes.
	// For scroll, they all need to be reconfigured, not just the dirty ones,
	// because focusing or moving may change their positions, so we do it in
	// arrange_container(), which goes over every window. We only configure
	// here those that come in the transaction.
	if (node->sway_container->view->type == SWAY_VIEW_XWAYLAND) {
		int cx, cy, cw, ch;
		map_to_configure(cstate->content_x, cstate->content_y, cstate->content_width,
			cstate->content_height, &cx, &cy, &cw, &ch);
		int ix, iy, iw, ih;
		map_to_configure(istate->content_x, istate->content_y, istate->content_width,
			istate->content_height, &ix, &iy, &iw, &ih);
		if (cx != ix || cy != iy || cw != iw || ch != ih) {
			// Update old_content so we don't re-configure in arrange_container()
			node->sway_container->old_content.x = istate->content_x;
			node->sway_container->old_content.y = istate->content_y;
			node->sway_container->old_content.width = istate->content_width;
			node->sway_container->old_content.height = istate->content_height;
			return true;
		}
		return false;
	}
#endif
	if (cstate->content_width == istate->content_width &&
			cstate->content_height == istate->content_height) {
		return false;
	}
	return true;
}

static void transaction_commit(struct sway_transaction *transaction) {
	sway_log(SWAY_DEBUG, "Transaction %p committing with %i instructions",
			transaction, transaction->instructions->length);
	transaction->num_waiting = 0;
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct sway_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct sway_node *node = instruction->node;
		bool hidden = node_is_view(node) && !node->destroying &&
			!view_is_visible(node->sway_container->view);
		if (should_configure(node, instruction)) {
			instruction->serial = view_configure(node->sway_container->view,
					instruction->container_state.content_x,
					instruction->container_state.content_y,
					instruction->container_state.content_width,
					instruction->container_state.content_height);
			if (!hidden) {
				instruction->waiting = true;
				++transaction->num_waiting;
			}

			view_send_frame_done(node->sway_container->view);
		}
		if (!hidden && node_is_view(node) &&
				!node->sway_container->view->saved_surface_tree) {
			view_save_buffer(node->sway_container->view);
		}
		node->instruction = instruction;
	}
	transaction->num_configures = transaction->num_waiting;
	if (debug.txn_timings) {
		clock_gettime(CLOCK_MONOTONIC, &transaction->commit_time);
	}
	if (debug.noatomic) {
		transaction->num_waiting = 0;
	} else if (debug.txn_wait) {
		// Force the transaction to time out even if all views are ready.
		// We do this by inflating the waiting counter.
		transaction->num_waiting += 1000000;
	}

	if (transaction->num_waiting) {
		// Set up a timer which the views must respond within
		transaction->timer = wl_event_loop_add_timer(server.wl_event_loop,
				handle_timeout, transaction);
		if (transaction->timer) {
			wl_event_source_timer_update(transaction->timer,
					server.txn_timeout_ms);
		} else {
			sway_log_errno(SWAY_ERROR, "Unable to create transaction timer "
					"(some imperfect frames might be rendered)");
			transaction->num_waiting = 0;
		}
	}
}

static void transaction_commit_pending(void) {
	if (server.queued_transaction) {
		return;
	}
	struct sway_transaction *transaction = server.pending_transaction;
	server.pending_transaction = NULL;
	server.queued_transaction = transaction;
	transaction_commit(transaction);
	transaction_progress();
}

static void set_instruction_ready(
		struct sway_transaction_instruction *instruction) {
	struct sway_transaction *transaction = instruction->transaction;

	if (debug.txn_timings) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		struct timespec *start = &transaction->commit_time;
		float ms = (now.tv_sec - start->tv_sec) * 1000 +
			(now.tv_nsec - start->tv_nsec) / 1000000.0;
		sway_log(SWAY_DEBUG, "Transaction %p: %zi/%zi ready in %.1fms (%s)",
				transaction,
				transaction->num_configures - transaction->num_waiting + 1,
				transaction->num_configures, ms,
				instruction->node->sway_container->title);
	}

	// If the transaction has timed out then its num_waiting will be 0 already.
	if (instruction->waiting && transaction->num_waiting > 0 &&
			--transaction->num_waiting == 0) {
		sway_log(SWAY_DEBUG, "Transaction %p is ready", transaction);
		wl_event_source_timer_update(transaction->timer, 0);
	}

	instruction->node->instruction = NULL;
	animation_set_type(ANIMATION_WINDOW_UPDATE);
	transaction_progress();
}

bool transaction_notify_view_ready_by_serial(struct sway_view *view,
		uint32_t serial) {
	struct sway_transaction_instruction *instruction =
		view->container->node.instruction;
	if (instruction != NULL && instruction->serial == serial) {
		set_instruction_ready(instruction);
		return true;
	}
	return false;
}

bool transaction_notify_view_ready_by_geometry(struct sway_view *view,
		double x, double y, int width, int height) {
	struct sway_transaction_instruction *instruction =
		view->container->node.instruction;
	int ccx, ccy, ccw, cch;
	map_to_configure(instruction->container_state.content_x, instruction->container_state.content_y,
		instruction->container_state.content_width, instruction->container_state.content_height,
		&ccx, &ccy, &ccw, &cch);
	if (instruction != NULL &&
			ccx == (int)x && ccy == (int)y &&
			ccw == width && cch == height) {
		set_instruction_ready(instruction);
		return true;
	}
	return false;
}

static void children_save_animation_variables(list_t *children) {
	if (!children) {
		return;
	}
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *child = children->items[i];
		child->animation.x0 = child->current.x;
		child->animation.y0 = child->current.y;
		child->animation.w0 = child->current.width;
		child->animation.h0 = child->current.height;
		child->animation.w1 = child->pending.width;
		child->animation.h1 = child->pending.height;
#if WLR_HAS_XWAYLAND
		if (child->view && child->view->type == SWAY_VIEW_XWAYLAND) {
			child->old_content.x = child->current.content_x;
			child->old_content.y = child->current.content_y;
			child->old_content.width = child->current.content_width;
			child->old_content.height = child->current.content_height;
		}
#endif
		children_save_animation_variables(child->pending.children);
	}
}

static void workspace_save_animation_variables(struct sway_workspace *ws) {
	if (ws->tiling->length == 0 && ws->floating->length == 0) {
		return;
	}
	children_save_animation_variables(ws->tiling);
	children_save_animation_variables(ws->floating);
}

static void save_animation_variables() {
	struct sway_container *fs = root->fullscreen_global;

	if (!fs) {
		for (int j = 0; j < root->outputs->length; j++) {
			struct sway_output *output = root->outputs->items[j];

			for (int i = 0; i < output->workspaces->length; i++) {
				struct sway_workspace *child = output->workspaces->items[i];
				workspace_save_animation_variables(child);
			}
		}
	}
}

static void _transaction_commit_dirty(bool server_request) {
	if (!server.dirty_nodes->length) {
		return;
	}

	if (!server.pending_transaction) {
		server.pending_transaction = transaction_create();
		if (!server.pending_transaction) {
			return;
		}
	}

	for (int i = 0; i < server.dirty_nodes->length; ++i) {
		struct sway_node *node = server.dirty_nodes->items[i];
		transaction_add_node(server.pending_transaction, node, server_request);
		node->dirty = false;
	}
	server.dirty_nodes->length = 0;

	// Save states to animation variables and old_content
	save_animation_variables();

	transaction_commit_pending();
}

void transaction_commit_dirty(void) {
	_transaction_commit_dirty(true);
}

void transaction_commit_dirty_client(void) {
	_transaction_commit_dirty(false);
}
