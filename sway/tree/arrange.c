#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/server.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/filter.h"
#include "sway/tree/virtual_output.h"
#include "sway/output.h"
#include "sway/tree/workspace.h"
#include "sway/tree/view.h"
#include "sway/tree/layout.h"
#include "list.h"
#include "sway/log.h"
#include "util.h"

void fit_children(list_t *children, enum sway_container_layout layout) {
	const int ncons = children->length;
	if (layout == L_HORIZ) {
		double total = 0.0;
		for (int i = 0; i < ncons; ++i) {
			struct sway_container *con = children->items[i];
			total += con->width_fraction;
		}
		for (int i = 0; i < ncons; ++i) {
			struct sway_container *con = children->items[i];
			con->width_fraction /= total;
		}
	} else {
		double total = 0.0;
		for (int i = 0; i < ncons; ++i) {
			struct sway_container *con = children->items[i];
			total += con->height_fraction;
		}
		for (int i = 0; i < ncons; ++i) {
			struct sway_container *con = children->items[i];
			con->height_fraction /= total;
		}
	}
}

void fit_workspace(struct sway_workspace *workspace) {
	const int ncons = workspace->tiling->length;
	if (ncons == 0) {
		return;
	}
	if (workspace_is_fullscreen(workspace) || layout_overview_mode(workspace) != OVERVIEW_DISABLED) {
		// Silently return if full screen or overview mode
		return;
	}
	enum sway_container_layout layout = layout_get_type(workspace);
	fit_children(workspace->tiling, layout);

	return;
}

void fit_container(struct sway_container *container) {
	if (container->pending.fullscreen_mode != FULLSCREEN_NONE ||
		container->pending.fullscreen_layout != FULLSCREEN_DISABLED) {
		return;
	}
	struct sway_workspace *workspace = container->pending.workspace;
	if (!workspace || layout_overview_mode(workspace) != OVERVIEW_DISABLED) {
		return;
	}
	list_t *children = container->pending.children;
	const int ncons = children->length;
	if (ncons == 0) {
		return;
	}
	enum sway_container_layout layout = container->pending.layout;
	fit_children(children, layout);

	return;
}

// For us, gaps_inner is applied to each container on both sides, regardless
// of its position. So an edge container will have the same content size
// than an inner container. Otherwise, moving containers may produce a content
// resize, which is annoying in some applications. i3wm and sway divide the
// total gap by the number of containers, so each has the same gap, but we
// cannot do this with scroll, or the content would be resized if we add or
// remove containers.
static void apply_horiz_layout(list_t *children, struct wlr_box *box) {
	if (!children->length) {
		return;
	}

	// Calculate gap size
	double inner_gap = 0;
	struct sway_container *child = children->items[0];
	struct sway_workspace *ws = child->pending.workspace;
	struct sway_container *parent = child->pending.parent;
	if (ws) {
		inner_gap = ws->gaps_inner;
	}
	double height = parent ? parent->height_fraction : layout_get_default_height(ws);

	// Resize windows
	// box has already applied outer and inner gaps
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *child = children->items[i];
		if (child->pending.fullscreen_layout == FULLSCREEN_ENABLED && ws) {
			struct wlr_box *area = workspace_get_output_area(ws);
			const double w = area ? area->width : ws->output->width;
			const double h = area ? area->height : ws->output->height;
			child->pending.width = w;
			child->pending.height = h;
			if (parent) {
				parent->pending.width = w;
				parent->pending.height = h;
			}
			continue;
		}
		child->pending.width = child->width_fraction * box->width - 2 * inner_gap;
		if (parent) {
			child->pending.height = height * box->height - 2 * inner_gap;
		} else {
			child->pending.height = child->height_fraction * box->height - 2 * inner_gap;
		}
	}
}

static void apply_vert_layout(list_t *children, struct wlr_box *box) {
	if (!children->length) {
		return;
	}

	// Calculate gap size
	double inner_gap = 0;
	struct sway_container *child = children->items[0];
	struct sway_workspace *ws = child->pending.workspace;
	struct sway_container *parent = child->pending.parent;
	if (ws) {
		inner_gap = ws->gaps_inner;
	}
	double width = parent ? parent->width_fraction : layout_get_default_width(ws);

	// Resize windows
	// box has already applied outer and inner gaps
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *child = children->items[i];
		if (child->pending.fullscreen_layout == FULLSCREEN_ENABLED && ws) {
			struct wlr_box *area = workspace_get_output_area(ws);
			const double w = area ? area->width : ws->output->width;
			const double h = area ? area->height : ws->output->height;
			child->pending.width = w;
			child->pending.height = h;
			if (parent) {
				parent->pending.width = w;
				parent->pending.height = h;
			}
			continue;
		}
		child->pending.height = child->height_fraction * box->height - 2 * inner_gap;
		if (parent) {
			child->pending.width = width * box->width - 2 * inner_gap;
		} else {
			child->pending.width = child->width_fraction * box->width - 2 * inner_gap;
		}
	}
}

static void arrange_container_sizes(struct sway_container *container);

static void arrange_floating(list_t *floating) {
	for (int i = 0; i < floating->length; ++i) {
		struct sway_container *floater = floating->items[i];
		arrange_container_sizes(floater);
	}
}

static void arrange_children(list_t *children,
		enum sway_container_layout layout, struct wlr_box *parent) {
	// Calculate x, y, width and height of children
	switch (layout) {
	case L_HORIZ:
		apply_horiz_layout(children, parent);
		break;
	case L_VERT:
		apply_vert_layout(children, parent);
		break;
	case L_NONE:
		apply_horiz_layout(children, parent);
		break;
	}

	// Recurse into child containers
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *child = children->items[i];
		arrange_container_sizes(child);
	}
}

// Arrange positions functions

static double get_active_position_pin(struct sway_workspace *workspace,
		const struct wlr_fbox *vp, enum sway_container_layout layout,
		list_t *children, int active_idx, int gaps, struct sway_container *pin) {
	// We consider all the possible positions where each container is next to
	// the pin. We choose the one that shows the active container and makes it
	// move as little as possible.
	struct sway_container *active = children->items[active_idx];
	int pin_idx = list_find(children, pin);
	if (layout == L_HORIZ) {
		// Add/substract 1 to account for rounding errors due to widths/heights
		// computed using layout fractions. The extra pixel will be absorbed by
		// the gaps.
		const double workspace_beg = vp->x - 1;
		const double workspace_end = vp->x + vp->width + 1;
		const double a_x = active->pending.x;
		if (layout_pin_get_position(workspace) == PIN_BEGINNING) {
			if (active == pin) {
				return vp->x + gaps;
			}
			// Set the active to the right of the pin, and test how many before it
			// fit while keeping the active inside of the viewport. Choose the one
			// that moves the active column the least.
			double cx0 = vp->x + (pin->pending.width + 2.0 * gaps);
			int c = active_idx;
			double best_movement = cx0 + gaps - a_x;
			const double caw = active->pending.width + 2.0 * gaps;
			for (int i = active_idx - 1; i >= 0; i--) {
				struct sway_container *con = children->items[i];
				if (con != pin) {
					double cw = con->pending.width +  2.0 * gaps;
					cx0 += cw;
					if (cx0 + caw > workspace_end) {
						break;
					}
					const double a_x0 = cx0 + gaps;
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
				return vp->x + vp->width - (pin->pending.width + gaps);
			}
			double cx1 = vp->x + vp->width - (pin->pending.width + 2.0 * gaps);
			int c = active_idx;
			const double caw = active->pending.width + 2.0 * gaps;
			double best_movement = cx1 - caw + gaps - a_x;
			for (int i = active_idx + 1; i < children->length; ++i) {
				struct sway_container *con = children->items[i];
				if (con != pin) {
					double cw = con->pending.width +  2.0 * gaps;
					cx1 -= cw;
					if (cx1 - caw < workspace_beg) {
						break;
					}
					const double a_x0 = cx1 - caw + gaps;
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
		const double workspace_beg = vp->y - 1;
		const double workspace_end = vp->y + vp->height + 1;
		const double a_y = active->pending.y;
		if (layout_pin_get_position(workspace) == PIN_BEGINNING) {
			if (active == pin) {
				return vp->y + gaps;
			}
			double cy0 = vp->y + (pin->pending.height + 2.0 * gaps);
			int c = active_idx;
			double best_movement = cy0 + gaps - a_y;
			const double cah = active->pending.height + 2.0 * gaps;
			for (int i = active_idx - 1; i >= 0; i--) {
				struct sway_container *con = children->items[i];
				if (con != pin) {
					double ch = con->pending.height +  2.0 * gaps;
					cy0 += ch;
					if (cy0 + cah > workspace_end) {
						break;
					}
					const double a_y0 = cy0 + gaps;
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
				return vp->y + vp->height - (pin->pending.height + gaps);
			}
			double cy1 = vp->y + vp->height - (pin->pending.height + 2.0 * gaps);
			int c = active_idx;
			const double cah = active->pending.height + 2.0 * gaps;
			double best_movement = cy1 - cah + gaps - a_y;
			for (int i = active_idx + 1; i < children->length; ++i) {
				struct sway_container *con = children->items[i];
				if (con != pin) {
					double ch = con->pending.height +  2.0 * gaps;
					cy1 -= ch;
					if (cy1 - cah < workspace_beg) {
						break;
					}
					const double a_y0 = cy1 - cah + gaps;
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
		const struct wlr_fbox *vp, enum sway_container_layout layout,
		list_t *children, int active_idx, int gaps) {
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
		const double workspace_beg = vp->x - 1;
		const double workspace_end = vp->x + vp->width + 1;
		const double a_x = active->pending.x;
		// Set the active at the left/top edge and test how many after it
		// fit within the viewport fully
		double x1 = vp->x + gaps;
		for (int c = active_idx; c < children->length; ++c) {
			struct sway_container *con = children->items[c];
			x1 += con->pending.width + gaps;
			// For those that fit, locate each one at the end of the viewport,
			// and check the previous ones don't leave any empty space.
			if (x1 <= workspace_end) {
				c_r = c;
				// Test from c_r to the beginning
				double movement = DBL_MAX;
				double cx0 = vp->x + vp->width;
				bool space = true;
				bool fits = false;
				for (int i = c_r; i >= 0; i--) {
					struct sway_container *con = children->items[i];
					cx0 -= con->pending.width + 2.0 * gaps;
					if (i == active_idx) {
						movement = (cx0 + gaps) - a_x;
					}
					if (cx0 <= workspace_beg) {
						space = false;
						if (cx0 < workspace_beg) {
							break;
						}
					}
					if (c_r == children->length - 1 && i == 0) {
						fits = true;
					}
				}
				if ((!space || fits) && fabs(movement) < fabs(best_movement)) {
					move = true;
					best_movement = movement;
				}
			} else {
				break;
			}
		}
		// Set the active at the right/bottom edge and test how many before it
		// fit within the viewport fully
		double x0 = vp->x + vp->width - gaps;
		for (int c = active_idx; c >= 0; --c) {
			struct sway_container *con = children->items[c];
			x0 -= con->pending.width + gaps;
			// For those that fit, locate each one at the beginning of the viewport,
			// and check the next ones don't leave any empty space.
			if (x0 >= workspace_beg) {
				c_l = c;
				// Test from c_l to the end
				double movement = DBL_MAX;
				double cx1 = vp->x;
				bool space = true;
				bool fits = false;
				for (int i = c_l; i < children->length; ++i) {
					if (i == active_idx) {
						movement = cx1 + gaps - a_x;
					}
					struct sway_container *con = children->items[i];
					cx1 += con->pending.width + 2.0 * gaps;
					if (cx1 >= workspace_end) {
						space = false;
						if (cx1 > workspace_end) {
							break;
						}
					}
					if (c_l == 0 && i == children->length - 1) {
						fits = true;
					}
				}
				if ((!space || fits) && fabs(movement) < fabs(best_movement)) {
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
		const double workspace_beg = vp->y - 1;
		const double workspace_end = vp->y + vp->height + 1;
		const double a_y = active->pending.y;
		double y0 = vp->y + vp->height - gaps;
		for (int c = active_idx; c >= 0; --c) {
			struct sway_container *con = children->items[c];
			y0 -= con->pending.height + gaps;
			if (y0 >= workspace_beg) {
				c_l = c;
				// Test from c_l to the end
				double movement = DBL_MAX;
				double cy1 = vp->y;
				bool space = true;
				bool fits = false;
				for (int i = c_l; i < children->length; ++i) {
					if (i == active_idx) {
						movement = cy1 + gaps - a_y;
					}
					struct sway_container *con = children->items[i];
					cy1 += con->pending.height + 2.0 * gaps;
					if (cy1 >= workspace_end) {
						space = false;
						if (cy1 > workspace_end) {
							break;
						}
					}
					if (c_l == 0 && i == children->length - 1) {
						fits = true;
					}
				}
				if ((!space || fits) &&	fabs(movement) < fabs(best_movement)) {
					move = true;
					best_movement = movement;
				}
			} else {
				break;
			}
		}
		double y1 = vp->y + gaps;
		for (int c = active_idx; c < children->length; ++c) {
			struct sway_container *con = children->items[c];
			y1 += con->pending.height + gaps;
			if (y1 <= workspace_end) {
				c_r = c;
				// Test from c_r to the beginning
				double movement = DBL_MAX;
				double cy0 = vp->y + vp->height;
				bool space = true;
				bool fits = false;
				for (int i = c_r; i >= 0; i--) {
					struct sway_container *con = children->items[i];
					cy0 -= con->pending.height + 2.0 * gaps;
					if (i == active_idx) {
						movement = (cy0 + gaps) - a_y;
					}
					if (cy0 <= workspace_beg) {
						space = false;
						if (cy0 < workspace_beg) {
							break;
						}
					}
					if (c_r == children->length - 1 && i == 0) {
						fits = true;
					}
				}
				if ((!space || fits) &&	fabs(movement) < fabs(best_movement)) {
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

// The viewport vp is the workspace box (the usable area minus gaps_out) in
// layout coordinates, which are unscaled: for a scaled workspace it is the
// workspace box scaled down around the center of the output area, so it covers
// what is shown on screen. So if we want to place a container on the leftmost
// possible position, we should use gaps_in, and it will include both gaps, out
// and the container's in. Positions and sizes are unscaled too.
static double compute_active_offset(struct sway_workspace *workspace,
		const struct wlr_fbox *vp, enum sway_container_layout layout,
		list_t *children, int active_idx, int gaps, struct sway_container *pin) {
	struct sway_container *active = children->items[active_idx];
	// Only the position of the active container is considered: it is where
	// the container was after the previous arrangement, or where the caller
	// wants it to be. The rest are chained from it.
	if (layout == L_HORIZ) {
		bool center = layout_modifiers_get_center_horizontal(workspace);
		if (center) {
			return vp->x + 0.5 * (vp->width - active->pending.width);
		}
		// Center row if space available
        double lwidth = 0, rwidth = 0;
        for (int i = 0; i < active_idx; ++i) {
			struct sway_container *con = children->items[i];
            lwidth += con->pending.width + 2 * gaps;
        }
		lwidth = round(lwidth);
        for (int i = active_idx; i < children->length; ++i) {
			struct sway_container *con = children->items[i];
            rwidth += con->pending.width + 2 * gaps;
        }
		rwidth = round(rwidth);
        double twidth = lwidth + rwidth;
        if (twidth <= vp->width + 1) {
			if (config->center_horizontal_if_fits) {
	            double start = 0.5 * (vp->width - twidth);
		        return vp->x + start + lwidth + gaps;
			}
        }
	} else {
		bool center = layout_modifiers_get_center_vertical(workspace);
		if (center) {
			return vp->y + 0.5 * (vp->height - active->pending.height);
		}
		// Center row if space available
        double lheight = 0, rheight = 0;
        for (int i = 0; i < active_idx; ++i) {
			struct sway_container *con = children->items[i];
            lheight += con->pending.height + 2 * gaps;
        }
		lheight = round(lheight);
        for (int i = active_idx; i < children->length; ++i) {
			struct sway_container *con = children->items[i];
            rheight += con->pending.height + 2 * gaps;
        }
		rheight = round(rheight);
        double theight = lheight + rheight;
        if (theight <= vp->height + 1) {
			if (config->center_vertical_if_fits) {
	            double start = 0.5 * (vp->height - theight);
		        return vp->y + start + lheight + gaps;
			}
        }
	}
	if (pin) {
		return get_active_position_pin(workspace, vp, layout, children, active_idx, gaps, pin);
	} else {
		return get_active_position(workspace, vp, layout, children, active_idx, gaps);
	}
}

static void arrange_container_position(struct sway_workspace *workspace,
		struct sway_container *con, int gaps);

static enum sway_layout_align_axis layout_alignment_axis(struct sway_workspace *workspace,
		struct sway_container *container, enum sway_container_layout workspace_layout,
		enum sway_container_layout container_layout) {
	enum sway_layout_align_axis axis = container_layout == L_HORIZ ? ALIGN_AXIS_H : ALIGN_AXIS_V;
	if ((layout_workspace_get_align_axes(workspace) & axis) == ALIGN_AXIS_NONE) {
		return ALIGN_AXIS_NONE;
	}
	if (container_layout == workspace_layout) {
		return axis;
	}
	struct sway_container *aligned = layout_workspace_get_align_container(workspace);
	return aligned && container->pending.parent == aligned->pending.parent ?
		axis : ALIGN_AXIS_NONE;
}

static void arrange_children_positions(struct sway_workspace *workspace,
		enum sway_container_layout layout, list_t *children,
		struct sway_container *active, int gaps) {

	if (children->length == 0) {
		return;
	}

	int active_idx = -1;
	bool jumping = layout_overview_mode(workspace) == OVERVIEW_JUMP;
	if (jumping) {
		for (int i = 0; i < children->length; ++i) {
			struct sway_container *child = children->items[i];
			if (child->pending.filtered) {
				continue;
			}
			active_idx = i;
			active = children->items[active_idx];
			break;
		}
	} else {
		active_idx = list_find(children, active);
		if (active_idx == -1) {
			active_idx = 0;
			active = children->items[active_idx];
		}
	}
	if (active_idx < 0) {
		return;
	}

	// Positions are in layout coordinates: the workspace scale is applied when
	// the containers are placed in the scene.
	struct wlr_fbox viewport;
	layout_vp_box(workspace, &viewport);

	struct sway_container *pin = layout_pin_enabled(workspace) ?
		layout_pin_get_container(workspace) : NULL;

	enum sway_container_layout workspace_layout = layout_get_type(workspace);
	if (layout != workspace_layout) {
		pin = NULL;
	}

	double offset;
	if (workspace->gesture.scrolling ||	layout_modifiers_get_reorder(workspace) == REORDER_LAZY || jumping) {
		offset = layout == L_HORIZ ? active->pending.x : active->pending.y;
	} else {
		uint32_t aligned = layout_alignment_axis(workspace, active, workspace_layout, layout);
		if (aligned != ALIGN_AXIS_NONE) {
			offset = aligned == ALIGN_AXIS_H ? active->pending.x : active->pending.y;
		} else if (active->pending.fullscreen_layout == FULLSCREEN_ENABLED && !layout_scale_enabled(workspace)) {
			struct wlr_box *area = workspace_get_output_area(workspace);
			offset = layout == L_HORIZ ?
				(area ? area->x : workspace->output->lx) :
				(area ? area->y : workspace->output->ly);
		} else {
			offset = compute_active_offset(workspace, &viewport, layout, children,
				active_idx, gaps, pin);
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

	if (layout == L_VERT) {
		double off = offset;
		for (int i = active_idx; i < children->length; ++i) {
			struct sway_container *child = children->items[i];
			if (jumping && child->pending.filtered) {
				continue;
			}
			struct sway_container *parent = child->pending.parent;
			child->pending.y = off;
			if (parent) {
				child->pending.x = parent->pending.x;
			} else {
				child->pending.x = viewport.x + gaps;
			}
			arrange_container_position(workspace, child, gaps);
			off += child->pending.height + 2 * gaps;
		}
		off = offset;
		for (int i = active_idx - 1; i >= 0; i--) {
			struct sway_container *child = children->items[i];
			if (jumping && child->pending.filtered) {
				continue;
			}
			struct sway_container *parent = child->pending.parent;
			off -= child->pending.height + 2 * gaps;
			child->pending.y = off;
			if (parent) {
				child->pending.x = parent->pending.x;
			} else {
				child->pending.x = viewport.x + gaps;
			}
			arrange_container_position(workspace, child, gaps);
		}
	} else if (layout == L_HORIZ) {
		double off = offset;
		for (int i = active_idx; i < children->length; ++i) {
			struct sway_container *child = children->items[i];
			if (jumping && child->pending.filtered) {
				continue;
			}
			struct sway_container *parent = child->pending.parent;
			child->pending.x = off;
			if (parent) {
				child->pending.y = parent->pending.y;
			} else {
				child->pending.y = viewport.y + gaps;
			}
			arrange_container_position(workspace, child, gaps);
			off += child->pending.width + 2 * gaps;
		}
		off = offset;
		for (int i = active_idx - 1; i >= 0; i--) {
			struct sway_container *child = children->items[i];
			if (jumping && child->pending.filtered) {
				continue;
			}
			struct sway_container *parent = child->pending.parent;
			off -= child->pending.width + 2 * gaps;
			child->pending.x = off;
			if (parent) {
				child->pending.y = parent->pending.y;
			} else {
				child->pending.y = viewport.y + gaps;
			}
			arrange_container_position(workspace, child, gaps);
		}
	} else {
		sway_assert(false, "unreachable");
	}
}

static void arrange_container_position(struct sway_workspace *workspace,
		struct sway_container *con, int gaps) {
	if (con->view) {
		// The position of the content depends on the position of the container
		view_autoconfigure(con->view);
	} else {
		// See arrange_active_children()
		arrange_children_positions(workspace, con->pending.layout,
			con->pending.children, con->pending.focused_inactive_child, gaps);
	}
	node_set_dirty(&con->node);
}

static void arrange_active_children_reset(struct sway_container *con) {
	if (con->view) {
		return;
	}
	con->pending.focused_inactive_child = NULL;
	for (int i = 0; i < con->pending.children->length; ++i) {
		arrange_active_children_reset(con->pending.children->items[i]);
	}
}

// Set the active child of every container of the workspace that has children,
// walking the focus stack only once
static void arrange_active_children(struct sway_workspace *workspace,
		struct sway_seat *seat) {
	for (int i = 0; i < workspace->tiling->length; ++i) {
		arrange_active_children_reset(workspace->tiling->items[i]);
	}
	for (int i = 0; i < workspace->floating->length; ++i) {
		arrange_active_children_reset(workspace->floating->items[i]);
	}
	struct sway_seat_node *current;
	wl_list_for_each(current, &seat->focus_stack, link) {
		struct sway_node *node = current->node;
		if (node->type != N_CONTAINER) {
			continue;
		}
		struct sway_container *parent = node->sway_container->pending.parent;
		if (parent && parent->pending.workspace == workspace &&
				!parent->pending.focused_inactive_child) {
			parent->pending.focused_inactive_child = node->sway_container;
		}
	}
}

static void arrange_workspace_positions(struct sway_workspace *workspace) {
	if (!workspace->output) {
		return;
	}
	struct sway_seat *seat = input_manager_current_seat();
	arrange_active_children(workspace, seat);
	const int gaps = workspace->gaps_inner;
	struct sway_container *fs = workspace->fullscreen;
	if (fs) {
		// When there is a full screen container, don't arrange the tiling
		// ones, or its coordinates will change.
		if (!fs->view) {
			arrange_container_position(workspace, fs, gaps);
		}
	} else if (workspace->tiling->length > 0 &&
			filter_workspace_tiling_enabled(workspace)) {
		struct sway_container *active =
			seat_get_focus_inactive_tiling(seat, workspace);
		if (active) {
			while (active->pending.parent) {
				active = active->pending.parent;
			}
		}
		arrange_children_positions(workspace, layout_get_type(workspace),
			workspace->tiling, active, gaps);
	}
	if (filter_workspace_floating_enabled(workspace)) {
		for (int i = 0; i < workspace->floating->length; ++i) {
			struct sway_container *floater = workspace->floating->items[i];
			if (!floater->view &&
					floater->pending.fullscreen_mode == FULLSCREEN_NONE) {
				arrange_container_position(workspace, floater, gaps);
			}
		}
	}
}

static void arrange_container_sizes(struct sway_container *container) {
	if (container->view) {
		view_autoconfigure(container->view);
		node_set_dirty(&container->node);
		return;
	}
	if (container->pending.children->length == 0) {
		return;
	}
	struct wlr_box box;
	workspace_get_box(container->pending.workspace, &box);
	// Keep workspace width/height because our sizes are a fraction of that,
	// but we need the correct coordinates of the parent
	box.x = container->pending.x;
	box.y = container->pending.y;

	enum sway_layout_fit fit = layout_modifiers_get_fit(container->pending.workspace);
	if (fit != FIT_NONE) {
		fit_container(container);
	}

	arrange_children(container->pending.children, container->pending.layout, &box);
	node_set_dirty(&container->node);
}

void arrange_container(struct sway_container *container) {
	if (config->reloading) {
		return;
	}
	arrange_container_sizes(container);

	// The size of a tiling container moves the ones next to it
	struct sway_workspace *workspace = container->pending.workspace;
	if (workspace && !container_is_floating_or_child(container)) {
		arrange_workspace_positions(workspace);
	}
}

void arrange_workspace(struct sway_workspace *workspace) {
	if (config->reloading) {
		return;
	}
	if (!workspace->output) {
		// Happens when there are no outputs connected
		return;
	}
	struct sway_output *output = workspace->output;
	output_virtual_outputs_update_geometry(output);
	struct wlr_box *area = workspace_get_output_usable_area(workspace);
	if (!area) {
		return;
	}
	sway_log(SWAY_DEBUG, "Usable area for ws: %dx%d@%d,%d",
			area->width, area->height, area->x, area->y);

	bool first_arrange = workspace->width == 0 && workspace->height == 0;
	struct wlr_fbox prev_vp;
	layout_vp_box(workspace, &prev_vp);

	double prev_x = workspace->x - workspace->current_gaps.left;
	double prev_y = workspace->y - workspace->current_gaps.top;
	workspace->width = area->width;
	workspace->height = area->height;
	workspace->x = output->lx + area->x;
	workspace->y = output->ly + area->y;

	// Adjust any floating containers
	double diff_x = workspace->x - prev_x;
	double diff_y = workspace->y - prev_y;
	if (!first_arrange && (diff_x != 0 || diff_y != 0)) {
		for (int i = 0; i < workspace->floating->length; ++i) {
			struct sway_container *floater = workspace->floating->items[i];
			struct wlr_fbox vp;
			layout_vp_box(workspace, &vp);
			floating_fix_coordinates(floater, &prev_vp, &vp);
			// Set transformation for scratchpad windows.
			if (floater->scratchpad) {
				struct wlr_box output_box;
				output_get_box(output, &output_box);
				floater->transform = output_box;
			}
		}
	}

	workspace_add_gaps(workspace);
	node_set_dirty(&workspace->node);
	sway_log(SWAY_DEBUG, "Arranging workspace '%s' at %f, %f", workspace->name,
			workspace->x, workspace->y);
	if (workspace->fullscreen) {
		struct sway_container *fs = workspace->fullscreen;
		struct wlr_box *box = workspace_get_output_area(workspace);
		fs->pending.x = box ? box->x : output->lx;
		fs->pending.y = box ? box->y : output->ly;
		fs->pending.width = box ? box->width : output->width;
		fs->pending.height = box ? box->height : output->height;
		arrange_container_sizes(fs);
	} else {
		struct wlr_box box;
		workspace_get_box(workspace, &box);

		enum sway_layout_fit fit = layout_modifiers_get_fit(workspace);
		if (fit != FIT_NONE) {
			fit_workspace(workspace);
		}

		arrange_children(workspace->tiling, layout_get_type(workspace), &box);
		arrange_floating(workspace->floating);
	}
	arrange_workspace_positions(workspace);
}

void arrange_output(struct sway_output *output) {
	if (config->reloading) {
		return;
	}
	if (!output->wlr_output->enabled) {
		return;
	}
	output_virtual_outputs_update_geometry(output);
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct sway_workspace *workspace = output->workspaces->items[i];
		arrange_workspace(workspace);
	}
}

void arrange_root(void) {
	if (config->reloading) {
		return;
	}
	struct wlr_box layout_box;
	wlr_output_layout_get_box(root->output_layout, NULL, &layout_box);
	root->x = layout_box.x;
	root->y = layout_box.y;
	root->width = layout_box.width;
	root->height = layout_box.height;

	if (root->fullscreen_global) {
		struct sway_container *fs = root->fullscreen_global;
		fs->pending.x = root->x;
		fs->pending.y = root->y;
		fs->pending.width = root->width;
		fs->pending.height = root->height;
		arrange_container_sizes(fs);
		if (!fs->view && fs->pending.workspace) {
			arrange_active_children_reset(fs);
			arrange_active_children(fs->pending.workspace,
				input_manager_current_seat());
			arrange_container_position(fs->pending.workspace, fs,
				fs->pending.workspace->gaps_inner);
		}
	} else {
		for (int i = 0; i < root->outputs->length; ++i) {
			struct sway_output *output = root->outputs->items[i];
			arrange_output(output);
		}
	}
}

void arrange_node(struct sway_node *node) {
	switch (node->type) {
	case N_ROOT:
		arrange_root();
		break;
	case N_OUTPUT:
		arrange_output(node->sway_output);
		break;
	case N_WORKSPACE:
		arrange_workspace(node->sway_workspace);
		break;
	case N_CONTAINER:
		arrange_container(node->sway_container);
		break;
	case N_LAYER_SURFACE:
	case N_LAYER_POPUP:
		break;
	}
}
