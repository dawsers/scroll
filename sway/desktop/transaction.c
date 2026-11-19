#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include "sway/config.h"
#include "sway/scene_descriptor.h"
#include "sway/desktop/idle_inhibit_v1.h"
#include "sway/desktop/transaction.h"
#include "sway/desktop/animate.h"
#include "sway/input/cursor.h"
#include "sway/input/input-manager.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/layers.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/filter.h"
#include "sway/tree/node.h"
#include "sway/tree/virtual_output.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/tree/layout.h"
#include "list.h"
#include "sway/log.h"

struct sway_transaction {
	struct wl_event_source *timer;
	list_t *instructions;   // struct sway_transaction_instruction *
	list_t *workspaces;     // struct sway_workspace * that were dirty for this transaction
	size_t num_waiting;
	size_t num_configures;
	struct timespec commit_time;
	enum sway_animation_type animation;
	bool disable_animations;
};

struct sway_transaction_instruction {
	struct sway_transaction *transaction;
	struct sway_node *node;
	union {
		struct sway_output_state output_state;
		struct sway_workspace_state workspace_state;
		struct sway_container_state container_state;
		struct sway_layer_surface_state layer_state;
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
	transaction->workspaces = create_list();
	transaction->animation = ANIMATION_DEFAULT;
	return transaction;
}

static void transaction_destroy(struct sway_transaction *transaction) {
	// Free instructions
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct sway_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct sway_node *node = instruction->node;
		if (node->instruction == instruction) {
			node->instruction = NULL;
		}
		if (node->type == N_CONTAINER) {
			free(instruction->container_state.formatted_title);
		}
		// Nodes that are being destroyed are freed when their last reference is gone
		node_unref(node);
		free(instruction);
	}
	list_free(transaction->instructions);
	list_free(transaction->workspaces);

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

	if (output->workspace_switch_animated) {
		state->workspace_switch_animated = true;
		output->workspace_switch_animated = false;
	}
}

static void copy_workspace_state(struct sway_workspace *ws,
		struct sway_transaction_instruction *instruction) {
	struct sway_workspace_state *state = &instruction->workspace_state;

	state->fullscreen = ws->fullscreen;
	state->x = ws->x;
	state->y = ws->y;
	state->width = ws->width;
	state->height = ws->height;
	state->scale = ws->scale;

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
	if (state->formatted_title) {
		free(state->formatted_title);
	}

	memcpy(state, &container->pending, sizeof(struct sway_container_state));
	state->formatted_title = NULL;

	if (container->pending.formatted_title) {
		state->formatted_title = strdup(container->pending.formatted_title);
		sway_assert(state->formatted_title,
				"Unable to allocate formatted title");
	}

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

static void copy_layer_surface_state(struct sway_layer_surface *surface,
		struct sway_transaction_instruction *instruction) {
	struct sway_layer_surface_state *state = &instruction->layer_state;
	memcpy(state, &surface->pending, sizeof(struct sway_layer_surface_state));
}

static void copy_layer_popup_state(struct sway_layer_popup *popup,
		struct sway_transaction_instruction *instruction) {
	struct sway_layer_surface_state *state = &instruction->layer_state;
	memcpy(state, &popup->pending, sizeof(struct sway_layer_surface_state));
}

static void transaction_add_workspace(struct sway_transaction *transaction,
		struct sway_workspace *workspace) {
	if (workspace && list_find(transaction->workspaces, workspace) == -1) {
		list_add(transaction->workspaces, workspace);
	}
}

static void transaction_add_node(struct sway_transaction *transaction,
		struct sway_node *node, bool server_request) {
	struct sway_transaction_instruction *instruction = NULL;

	// Check if we have an instruction for this node already, in which case we
	// update that instead of creating a new one.
	if (node->refs > 0) {
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
		node_ref(node);
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
		transaction_add_workspace(transaction, node->sway_workspace);
		copy_workspace_state(node->sway_workspace, instruction);
		break;
	case N_CONTAINER:
		transaction_add_workspace(transaction,
			node->sway_container->current.workspace);
		transaction_add_workspace(transaction,
			node->sway_container->pending.workspace);
		copy_container_state(node->sway_container, instruction);
		break;
	case N_LAYER_SURFACE:
		copy_layer_surface_state(node->sway_layer_surface, instruction);
		break;
	case N_LAYER_POPUP:
		copy_layer_popup_state(node->sway_layer_popup, instruction);
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
	free(container->current.formatted_title);

	memcpy(&container->current, state, sizeof(struct sway_container_state));

	state->formatted_title = NULL;

	if (view) {
		// Keep the last frame of a closing view, so the fade out
		// animation has something to fade. It is destroyed together
		// with the container or when the view is mapped again.
		if (view->saved_surface_tree && !container->node.destroying) {
			view_remove_saved_buffer(view);
		}

		// If the view hasn't responded to the configure, center it within
		// the container. This is important for fullscreen views which
		// refuse to resize to the size of the output.
		if (view->surface) {
			view_center_and_clip_surface(view);
		}
	}

	if (!container->node.destroying) {
		container_update_title_text(container);
	}
}

static void apply_layer_surface_state(struct sway_layer_surface *surface,
		struct sway_layer_surface_state *state) {
	memcpy(&surface->current, state, sizeof(struct sway_layer_surface_state));
}

static void apply_layer_popup_state(struct sway_layer_popup *popup,
		struct sway_layer_surface_state *state) {
	memcpy(&popup->current, state, sizeof(struct sway_layer_surface_state));
}

static void arrange_title_bar(struct sway_container *con,
		double x, double y, double width, double height) {
	container_update(con);

	bool has_title_bar = height > 0;
	wlr_scene_node_set_enabled(&con->title_bar.tree->node, has_title_bar);
	if (!has_title_bar) {
		return;
	}

	wlr_scene_node_set_position(&con->title_bar.tree->node, x, y);

	con->title_width = width;
	container_arrange_title_bar(con);
}

static void disable_container(struct sway_container *con) {
	if (con->view) {
		wlr_scene_node_reparent(&con->view->scene_tree->node, con->content_tree);
	} else {
		for (int i = 0; i < con->current.children->length; i++) {
			struct sway_container *child = con->current.children->items[i];

			wlr_scene_node_reparent(&child->scene_tree->node, con->content_tree);

			disable_container(child);
		}
	}
}

static void scene_arrange_container(struct sway_container *con,
		bool title_bar, struct sway_workspace *workspace);

static void scene_arrange_children(struct sway_workspace *workspace,
		list_t *children) {
	const bool jumping = layout_overview_mode(workspace) == OVERVIEW_JUMP;
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *child = children->items[i];
		if (jumping && child->current.filtered) {
			continue;
		}
		scene_arrange_container(child, true, workspace);
	}
}

static void map_to_configure(struct sway_view *view, double content_x, double content_y, double content_width,
		double content_height, int *cx, int *cy, int *cw, int *ch) {
	// We need to match what we did in view_configure()
#if WLR_HAS_XWAYLAND
	// X11 clients are screen position dependent
	if (view->type == SWAY_VIEW_XWAYLAND && view->container) {
		struct sway_layout_transform transform;
		layout_transform_container_get(view->container, false, &transform);
		layout_transform_point(&transform, &content_x, &content_y);
	}
#endif
	*cx = round(content_x);
	*cy = round(content_y);
	int ex = round(content_x + content_width);
	int ey = round(content_y + content_height);
	*cw = ex - *cx;
	*ch = ey - *cy;
#if WLR_HAS_XWAYLAND
	if (!config->xwayland_output_scale && view->container && view->container->pending.workspace) {
		struct sway_output *output = view->container->pending.workspace->output;
		if (output) {
			struct wlr_output_layout_output *layout_o = wlr_output_layout_get(root->output_layout, output->wlr_output);
			*cx = round(layout_o->p_x + (*cx - layout_o->x) * layout_o->output->scale);
			*cy = round(layout_o->p_y + (*cy - layout_o->y) * layout_o->output->scale);
		}
	}
#endif
}

static void animation_update_container(struct sway_container *con) {
	wlr_scene_node_set_enabled(&con->decoration.tree->node, true);
	con->current.x = con->pending.x;
	con->current.y = con->pending.y;
}

static void set_children_positions(struct sway_container *parent, list_t *children,
		enum sway_container_layout layout,
		const struct sway_animation_options *options) {
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *child = children->items[i];
		if (layout == L_VERT) {
			animated_variable_set(&child->animation.y, child->current.y, options);
			animated_variable_set_span(&child->animation.y, 0.0);
			if (parent) {
				animated_variable_inherit(&child->animation.x, &parent->animation.x);
			} else {
				animated_variable_reset(&child->animation.x, child->current.x);
			}
		} else {
			animated_variable_set(&child->animation.x, child->current.x, options);
			animated_variable_set_span(&child->animation.x, 0.0);
			if (parent) {
				animated_variable_inherit(&child->animation.y, &parent->animation.y);
			} else {
				animated_variable_reset(&child->animation.y, child->current.y);
			}
		}
		if (!child->view) {
			set_children_positions(child, child->current.children,
				child->current.layout, options);
		}
	}
}

static void set_container_positions(struct sway_container *con, double width,
		double height, const struct sway_animation_options *options) {
	animated_variable_set(&con->animation.x, con->pending.x, options);
	animated_variable_set(&con->animation.y, con->pending.y, options);
	animated_variable_set_span(&con->animation.x, width);
	animated_variable_set_span(&con->animation.y, height);
}

static void set_workspace_positions(struct sway_workspace *ws,
		const struct sway_animation_options *options) {
	struct sway_container *fs = ws->current.fullscreen;
	if (fs) {
		set_container_positions(fs, fs->animation.w.target, fs->animation.h.target, options);
	} else {
		set_children_positions(NULL, ws->tiling, layout_get_type(ws), options);
	}
	for (int i = 0; i < ws->floating->length; ++i) {
		struct sway_container *child = ws->floating->items[i];
		if (child->current.fullscreen_mode != FULLSCREEN_NONE) {
			continue;
		}
		set_container_positions(child, ws->width, ws->height, options);
	}
}

// A filtered container is not part of the scene graph, but it is kept there
// until its fade out animation ends.
static bool container_filtered(struct sway_container *con) {
	return con->current.filtered && !con->animation.a.animating;
}

// Whether the container should be left out of the scene graph
static bool container_hidden(struct sway_workspace *workspace,
		struct sway_container *con) {
	if (container_filtered(con)) {
		return true;
	}
	// While the workspace is panning, only show what is in its viewport,
	// regardless of the active filter.
	return workspace && workspace_is_panning(workspace) &&
		!workspace_container_visible(workspace, con);
}

// The opacity of a container: filtered containers fade out
static float container_state_alpha(const struct sway_container_state *state) {
	return state->filtered ? 0.0f : state->alpha;
}

static bool container_animating(struct sway_container *con) {
	return con->animation.x.animating || con->animation.y.animating ||
		con->animation.w.animating || con->animation.h.animating ||
		con->animation.a.animating;
}

static void animate_children(struct sway_workspace *workspace,
		enum sway_container_layout layout, list_t *children,
		struct wlr_scene_tree *content);

static void animate_view(struct sway_container *con,
		double dwidth, double dheight, bool title_bar, int gaps,
		struct sway_workspace *workspace) {
	// this container might have previously been in the scratchpad,
	// make sure it's enabled for viewing
	wlr_scene_node_set_enabled(&con->scene_tree->node, true);

	double scale = layout_transform_workspace_scale(workspace, true);
	double width = scale * dwidth;
	double height = scale * dheight;

	if (con->pending.fullscreen_layout == FULLSCREEN_ENABLED) {
		wlr_scene_node_set_position(&con->view->scene_tree->node, 0, 0);
		wlr_scene_node_set_enabled(&con->title_bar.tree->node, false);
		wlr_scene_node_set_enabled(&con->decoration.full->node, false);
		wlr_scene_node_set_enabled(&con->shadow->node, false);
		wlr_scene_node_reparent(&con->view->scene_tree->node, con->content_tree);
		wlr_scene_node_set_position(&con->view->output_handler->node, 0, 0);
		wlr_scene_buffer_set_dest_size(con->view->output_handler, width, height);
		view_reconfigure(con->view);
		return;
	}
	double border_top = container_titlebar_height() * scale;
	double border_width = con->current.border_thickness > 0 ?
		fmax(1.0, con->current.border_thickness * scale) : 0.0;

	if (title_bar && con->current.border != B_NORMAL) {
		wlr_scene_node_set_enabled(&con->title_bar.tree->node, false);
	}

	if (con->current.border == B_NORMAL) {
		if (title_bar) {
			arrange_title_bar(con, 0, 0, dwidth, border_top);
		} else {
			border_top = 0;
			// should be handled by the parent container
		}
		border_top += con->current.border_top ? border_width : 0.0;
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
		wlr_scene_decoration_set_title_bar(con->decoration.full, false, 0, 0);
	}

	double border_left = con->current.border_left ? border_width : 0;
	wlr_scene_decoration_set_size(con->decoration.full, width, height);
	wlr_scene_decoration_set_border_width(con->decoration.full, border_width);
	wlr_scene_decoration_set_border_radius(con->decoration.full, con->view->using_csd ?
		0.0 : con->pending.decoration.border_radius * scale);
	double shadow_offset[2] = {
		con->pending.decoration.shadow_offset_x,
		con->pending.decoration.shadow_offset_y
	};
	wlr_scene_node_set_enabled(&con->shadow->node, false);
	struct wlr_scene_decoration * decoration = con->decoration.full;
	struct wlr_scene_shadow *shadow = con->shadow;
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
		double x = 0.5 * root->width - con->animation.x.value;
		double y = 0.5 * root->height - con->animation.y.value;
		double proj_x = 0.5 * root->width - x / ratio;
		double proj_y = 0.5 * root->height - y / ratio;
		sx = decoration->node.x + proj_x - con->animation.x.value;
		sy = decoration->node.y + proj_y - con->animation.y.value;
		sw = width / ratio;
		sh = height / ratio;
	} else {
		sx = decoration->node.x + (shadow_offset[0] - size) * scale;
		sy = decoration->node.y + (shadow_offset[1] - size) * scale;
		sw = width + 2.0 * scale * (size);
		sh = height + 2.0 * scale * (size);
	}
	wlr_scene_node_set_position(&shadow->node, sx, sy);

	// The shadow is a separate scene node, it needs to fade with a container
	// that is appearing or disappearing.
	const struct sway_animated_variable *fade = &con->animation.a;
	const double opaque = fmax(fade->origin, fade->target);
	const float shadow_alpha = fade->animating && opaque > 0.0 &&
		(fade->origin == 0.0 || fade->target == 0.0) ?
		fmin(fmax(fade->value / opaque, 0.0), 1.0) : 1.0f;
	float shadow_color[4] = {
		con->pending.decoration.shadow_color_r * shadow_alpha,
		con->pending.decoration.shadow_color_g * shadow_alpha,
		con->pending.decoration.shadow_color_b * shadow_alpha,
		con->pending.decoration.shadow_color_a * shadow_alpha,
	};
	wlr_scene_shadow_set_properties(shadow, sw, sh,
		con->pending.decoration.shadow, con->pending.decoration.shadow_dynamic,
		con->pending.decoration.shadow_size,
		con->pending.decoration.shadow_blur, shadow_offset, shadow_color);
	wlr_scene_node_set_enabled(&con->decoration.full->node, true);

	// make sure to reparent, it's possible that the client just came out of
	// fullscreen mode where the parent of the surface is not the container
	wlr_scene_node_reparent(&con->view->scene_tree->node, con->content_tree);
	wlr_scene_node_set_position(&con->view->scene_tree->node,
		border_left, border_top);

	// the output handler for the view wants to detect events for the entire
	// container so give it negative coordinates to move it back over the
	// decorations
	wlr_scene_node_set_position(&con->view->output_handler->node,
		-border_left, -border_top);
	wlr_scene_buffer_set_dest_size(con->view->output_handler, width, height);

	wlr_scene_node_set_enabled(&shadow->node, shadow->enabled);

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
		int pcx, pcy, pcw, pch;
		map_to_configure(con->view, con->pending.content_x, con->pending.content_y,
			con->pending.content_width, con->pending.content_height,
			&pcx, &pcy, &pcw, &pch);
		int ocx, ocy, ocw, och;
		map_to_configure(con->view, con->old_content.x, con->old_content.y,
			con->old_content.width, con->old_content.height,
			&ocx, &ocy, &ocw, &och);
		// The position on screen also changes when the workspace scale changes
		struct wlr_xwayland_surface *xsurface = con->view->wlr_xwayland_surface;
		const bool moved = xsurface && (pcx != xsurface->x || pcy != xsurface->y);
		if (!container_animating(con) &&
			(pcx != ocx || pcy != ocy || pcw != ocw || pch != och || moved)) {
			view_configure(con->view, con->pending.content_x, con->pending.content_y,
				con->pending.content_width, con->pending.content_height);
			con->current.content_x = con->pending.content_x;
			con->current.content_y = con->pending.content_y;
			con->current.content_width = con->pending.content_width;
			con->current.content_height = con->pending.content_height;
		}
	}
#endif

	struct sway_animated_variable *a = &con->animation.a;
	if (a->origin != a->target) {
		const float old_alpha = con->pending.alpha;
		con->pending.alpha = a->value;
		output_configure_scene(NULL, &con->scene_tree->node, con->pending.alpha);
		container_update(con);
		con->pending.alpha = old_alpha;
	}

	view_reconfigure(con->view);
}

static void scene_arrange_container(struct sway_container *con,
		bool title_bar, struct sway_workspace *workspace) {
	if (con->view && container_hidden(workspace, con)) {
		wlr_scene_node_set_enabled(&con->scene_tree->node, false);
		return;
	}
	// this container might have previously been in the scratchpad,
	// make sure it's enabled for viewing
	wlr_scene_node_set_enabled(&con->scene_tree->node, true);

	if (con->view == NULL) {
		// make sure to disable the title bar if the parent is not managing it
		if (title_bar) {
			wlr_scene_node_set_enabled(&con->title_bar.tree->node, false);
		}

		scene_arrange_children(workspace, con->current.children);
	} else if (layout_overview_mode(workspace) == OVERVIEW_JUMP) {
		layout_container_jump_decoration_apply_scale(con);
	}
}

static void animate_container(struct sway_container *con,
		double dwidth, double dheight, bool title_bar, int gaps,
		struct sway_workspace *workspace) {
	if (container_hidden(workspace, con)) {
		wlr_scene_node_set_enabled(&con->scene_tree->node, false);
		return;
	}

	if (con->view) {
		animate_view(con, dwidth, dheight, title_bar, gaps, workspace);
	} else {
		animate_children(workspace, con->current.layout, con->current.children, con->content_tree);
	}
}

static void arrange_fullscreen(struct wlr_scene_tree *tree,
		struct sway_container *fs, struct sway_workspace *ws) {
	struct wlr_scene_node *fs_node;
	if (fs->view) {
		fs_node = &fs->view->scene_tree->node;

		// if we only care about the view, disable any decorations
		wlr_scene_node_set_enabled(&fs->scene_tree->node, false);
		wlr_scene_node_set_enabled(&fs->shadow->node, false);
	} else {
		fs_node = &fs->scene_tree->node;
		scene_arrange_container(fs, true, fs->current.workspace);
	}

	if (ws) {
		// When we change focus in workspace full screen mode, we avoid disabling and enabling
		// full screen mode by calling container_pass_fullscreen(). This only calls
		// transaction_commit_dirty() at the end, so the old node may be in the
		// list of full screen nodes when we insert the new one. Remove any old
		// nodes before adding the new one (there should be just one).
		struct wlr_scene_node *node, *tmp;
		wl_list_for_each_safe(node, tmp, &tree->children, link) {
			wlr_scene_node_reparent(node, node->old_parent);
		}

		fs_node->old_parent = fs_node->parent;
	}
	wlr_scene_node_reparent(fs_node, tree);
	wlr_scene_node_lower_to_bottom(fs_node);
}

static void animate_fullscreen(struct wlr_scene_tree *tree,
		struct sway_container *fs, struct sway_workspace *ws) {
	struct wlr_scene_node *fs_node;
	if (fs->view) {
		fs_node = &fs->view->scene_tree->node;

		// if we only care about the view, disable any decorations
		wlr_scene_node_set_enabled(&fs->scene_tree->node, false);
	} else {
		// We shouldn't be here
		sway_assert(false, "Unreachable");
		return;
	}
	if (ws && !ws->output) {
		return;
	}
	animation_update_container(fs);
	const double fx = fs->animation.x.value;
	const double fy = fs->animation.y.value;
	if (ws) {
		struct sway_output *output = ws->output;
		// The container moves with its workspace layer, but the background of
		// the output does not, so pan it too.
		const double pan = workspace_switch_offset(ws);
		wlr_scene_node_set_position(&output->fullscreen_background->node, fx - output->lx, fy + pan - output->ly);
		wlr_scene_rect_set_size(output->fullscreen_background, fs->animation.w.value, fs->animation.h.value);
		wlr_scene_node_set_position(fs_node, fx - output->lx, fy - output->ly);
		wlr_scene_node_set_position(&fs->view->output_handler->node, fx - output->lx, fy - output->ly);
	} else {
		wlr_scene_node_set_position(fs_node, fx, fy);
		wlr_scene_node_set_position(&fs->view->output_handler->node, fx, fy);
	}
	wlr_scene_buffer_set_dest_size(fs->view->output_handler, fs->animation.w.value, fs->animation.h.value);
	view_reconfigure(fs->view);
}

// Apply the switch animation offset directly to the layers that hold the
// tiled and full screen containers.
static void workspace_place_layers(struct sway_workspace *ws) {
	struct wlr_box *area = workspace_get_output_usable_area(ws);
	struct side_gaps *gaps = &ws->current_gaps;
	const double pan = workspace_switch_offset(ws);
	const double scale = layout_transform_workspace_scale(ws, true);
	wlr_scene_node_set_position(&ws->layers.tiling->node,
		gaps->left + area->x + scale * ws->animation.pan_x.value,
		gaps->top + area->y + pan + scale * ws->animation.pan_y.value);
	wlr_scene_node_set_position(&ws->layers.fullscreen->node, 0.0, pan);
}

// Floating containers are not children of any workspace layer, so they need
// to be panned one by one.
static double workspace_floating_pan(struct sway_workspace *ws,
		struct sway_container *con) {
	// Sticky containers are shown in every workspace: they do not move with it.
	if (container_is_sticky_or_child(con)) {
		return 0.0;
	}
	return workspace_switch_offset(ws);
}

// Place a floating container in the scene, at its animated position
static void floating_set_position(struct sway_workspace *ws, struct sway_container *con) {
	const double pan = workspace_floating_pan(ws, con);
	struct sway_layout_transform transform;
	layout_transform_container_get(con, true, &transform);
	wlr_scene_node_set_position(&con->scene_tree->node,
		layout_transform_x(&transform, con->animation.x.value),
		layout_transform_y(&transform, con->animation.y.value) + pan);
}

static void arrange_workspace_floating(struct sway_workspace *ws) {
	for (int i = 0; i < ws->current.floating->length; i++) {
		struct sway_container *floater = ws->current.floating->items[i];
		struct wlr_scene_tree *layer = root->layers.floating;

		if (floater->current.fullscreen_mode != FULLSCREEN_NONE) {
			continue;
		}
		if (container_hidden(ws, floater)) {
			wlr_scene_node_set_enabled(&floater->scene_tree->node, false);
			continue;
		}

		if (root->fullscreen_global) {
			if (container_is_transient_for(floater, root->fullscreen_global)) {
				layer = root->layers.fullscreen_global;
			}
		} else {
			for (int i = 0; i < root->outputs->length; i++) {
				struct sway_output *output = root->outputs->items[i];
				if (!output->virtual_outputs) {
					continue;
				}
				for (int j = 0; j < output->virtual_outputs->length; j++) {
					struct sway_virtual_output *vo =
						output->virtual_outputs->items[j];
					if (!virtual_output_is_visible(vo)) {
						continue;
					}
					struct sway_workspace *active =
						virtual_output_get_active_workspace(vo);
					if (active && active->fullscreen &&
							container_is_transient_for(floater, active->fullscreen)) {
						layer = root->layers.fullscreen;
						break;
					}
				}
			}
		}

		wlr_scene_node_reparent(&floater->scene_tree->node, layer);
		wlr_scene_node_set_enabled(&floater->scene_tree->node, true);
		wlr_scene_node_set_enabled(&floater->decoration.tree->node, true);

		scene_arrange_container(floater, true, ws);
		floating_set_position(ws, floater);
	}
}

static void animate_workspace_floating(struct sway_workspace *ws) {
	if (ws->current.floating->length == 0) {
		return;
	}

	for (int i = 0; i < ws->current.floating->length; i++) {
		struct sway_container *child = ws->current.floating->items[i];
		if (child->current.fullscreen_mode != FULLSCREEN_NONE) {
			continue;
		}
		// If the workspaces overview is enabled, make sure floating windows only
		// show in the output corresponding to their workspace.
		if (layout_overview_workspaces_enabled()) {
			child->scene_tree->node.info.wlr_output = ws->output->wlr_output;
		} else {
			child->scene_tree->node.info.wlr_output = NULL;
		}
		animation_update_container(child);
		floating_set_position(ws, child);
		animate_container(child, child->animation.w.value, child->animation.h.value,
			true, ws->gaps_inner, ws);
	}
}

static void arrange_workspace_tiling(struct sway_workspace *ws,
		int width, int height) {
	if (ws->tiling->length == 0) {
		return;
	}
	if (ws->current.fullscreen) {
		// The tiling containers are not shown when there is a full screen one
		return;
	}
	scene_arrange_children(ws, ws->tiling);
	struct sway_container *pin = layout_pin_enabled(ws) ? layout_pin_get_container(ws) : NULL;
	if (pin) {
		wlr_scene_node_raise_to_top(&pin->scene_tree->node);
	}
}

static void animate_workspace_tiling(struct sway_workspace *ws) {
	if (ws->tiling->length == 0) {
		return;
	}
	animate_children(ws, layout_get_type(ws), ws->tiling, ws->layers.tiling);
}

static void disable_workspace(struct sway_workspace *ws) {
	// if any containers were just moved to a disabled workspace it will
	// have the parent of the old workspace. Move the workspace so that it won't
	// be shown.
	for (int i = 0; i < ws->current.tiling->length; i++) {
		struct sway_container *child = ws->current.tiling->items[i];

		wlr_scene_node_reparent(&child->scene_tree->node, ws->layers.tiling);
		disable_container(child);
	}

	for (int i = 0; i < ws->current.floating->length; i++) {
		struct sway_container *floater = ws->current.floating->items[i];
		wlr_scene_node_reparent(&floater->scene_tree->node, root->layers.floating);
		disable_container(floater);
		wlr_scene_node_set_enabled(&floater->scene_tree->node, false);
	}
}

static void layer_surface_resize_iterator(struct wlr_scene_buffer *buffer,
		int sx, int sy, void *user_data) {
	struct sway_layer_surface *surface = user_data;
	const double total_scale = 1.0;
	const double wscale = surface->animation.w.value > 0.0 ? surface->animation.w.value / fmax(1.0, surface->animation.w.target) : 0.0;
	const double hscale = surface->animation.h.value > 0.0 ? surface->animation.h.value / fmax(1.0, surface->animation.h.target) : 0.0;
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	wlr_scene_surface_resize(scene_surface, total_scale, wscale, hscale, 0.0f, 0.0f);
}

static void layer_popup_resize_iterator(struct wlr_scene_buffer *buffer,
		int sx, int sy, void *user_data) {
	struct sway_layer_popup *popup = user_data;
	const double total_scale = 1.0;
	const double width = fmax(1.0, popup->animation.w.value);
	const double height = fmax(1.0, popup->animation.h.value);
	const double wscale = width / fmax(1.0, popup->animation.w.target);
	const double hscale = height / fmax(1.0, popup->animation.h.target);
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	wlr_scene_surface_resize(scene_surface, total_scale, wscale, hscale, 0.0f, 0.0f);
	double x, y;
	if (popup->toplevel->layer_surface->current.desired_height == 0) {
		x = popup->pending.x >= popup->toplevel->pending.x ? 0.0: popup->animation.w.target - width;
	} else {
		x = 0.5 * (popup->animation.w.target - width);
	}
	if (popup->toplevel->layer_surface->current.desired_width == 0) {
		y = popup->pending.y >= popup->toplevel->pending.y ? 0.0 : popup->animation.h.target - height;
	} else {
		y = 0.5 * (popup->animation.h.target - height);
	}
	wlr_scene_node_set_position(&buffer->node, x, y);
}

static void animate_layer(struct wlr_scene_tree *tree,
		const struct wlr_box *full_area, struct wlr_box *usable_area, bool exclusive) {
	struct wlr_scene_node *node;
	wl_list_for_each(node, &tree->children, link) {
		struct sway_layer_surface *surface = scene_descriptor_try_get(node,
			SWAY_SCENE_DESC_LAYER_SHELL);
		// surface could be null during destruction
		if (!surface) {
			continue;
		}

		if (!surface->scene->layer_surface->initialized) {
			continue;
		}

		if ((surface->scene->layer_surface->current.exclusive_zone > 0) != exclusive) {
			continue;
		}

		if (surface->animation.w.target != 0.0 && surface->animation.h.target != 0.0) {
			wlr_scene_node_for_each_buffer(&surface->tree->node, layer_surface_resize_iterator, surface);
		} else {
			surface->animation.w.value = surface->layer_surface->current.desired_width;
			surface->animation.h.value = surface->layer_surface->current.desired_height;
		}
		struct wlr_box box = {0};
		wlr_scene_layer_surface_v1_get_box(surface->scene, full_area, usable_area,
			surface->animation.w.value, surface->animation.h.value, &box);
		wlr_scene_node_set_position(&surface->scene->tree->node, box.x, box.y);

		for (int i = 0; i < surface->layer_popups->length; ++i) {
			struct sway_layer_popup *popup = surface->layer_popups->items[i];
			wlr_scene_node_for_each_buffer(&popup->scene->node, layer_popup_resize_iterator, popup);
		}
	}
}

static void animate_layers(struct sway_output *output) {
	struct wlr_box usable_area = { 0 };
	wlr_output_effective_resolution(output->wlr_output,
			&usable_area.width, &usable_area.height);
	const struct wlr_box full_area = usable_area;

	animate_layer(output->layers.shell_overlay, &full_area, &usable_area, true);
	animate_layer(output->layers.shell_top, &full_area, &usable_area, true);
	animate_layer(output->layers.shell_bottom, &full_area, &usable_area, true);
	animate_layer(output->layers.shell_background, &full_area, &usable_area, true);

	animate_layer(output->layers.shell_overlay, &full_area, &usable_area, false);
	animate_layer(output->layers.shell_top, &full_area, &usable_area, false);
	animate_layer(output->layers.shell_bottom, &full_area, &usable_area, false);
	animate_layer(output->layers.shell_background, &full_area, &usable_area, false);
}

// Enable fullscreen mode and its background regardless of the fullscreen state
// of the active workspace: a workspace switch animation needs them when any of
// the workspaces is in full screen mode.
static bool output_fullscreen_panning(struct sway_output *output) {
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct sway_workspace *workspace = output->workspaces->items[i];
		if (workspace_is_panning(workspace) && workspace->current.fullscreen) {
			return true;
		}
	}
	return false;
}

static void scene_arrange_output(struct sway_output *output) {
	arrange_layers(output);
	bool output_fs = output_fullscreen_panning(output);
	bool split_fs = output_fullscreen_needs_background(output);
	for (int i = 0; i < output->current.workspaces->length; i++) {
		struct sway_workspace *child = output->current.workspaces->items[i];

		if (!child || child->node.destroying) {
			continue;
		}
		bool activated = filter_workspace_enabled(child);

		wlr_scene_node_reparent(&child->layers.tiling->node, output->layers.tiling);
		wlr_scene_node_reparent(&child->layers.fullscreen->node, output->layers.fullscreen);

		// Containers that are not enabled are all filtered: they don't need
		// to be arranged, and they hide when they finish fading out.
		bool floating = filter_workspace_floating_enabled(child);
		bool tiling = filter_workspace_tiling_enabled(child);

		for (int i = 0; i < child->current.floating->length; i++) {
			struct sway_container *floater = child->current.floating->items[i];
			wlr_scene_node_reparent(&floater->scene_tree->node, root->layers.floating);
			wlr_scene_node_set_enabled(&floater->scene_tree->node,
				activated && !container_filtered(floater));
		}

		if (activated) {
			struct sway_container *fs = child->current.fullscreen;
			wlr_scene_node_set_enabled(&child->layers.tiling->node, !fs);
			wlr_scene_node_set_enabled(&child->layers.fullscreen->node, fs);

			if (split_fs || output_fs) {
				wlr_scene_node_set_enabled(&output->layers.shell_background->node, true);
				wlr_scene_node_set_enabled(&output->layers.shell_bottom->node, true);
				wlr_scene_node_set_enabled(&output->layers.fullscreen->node, true);
			} else {
				wlr_scene_node_set_enabled(&output->layers.shell_background->node, !fs);
				wlr_scene_node_set_enabled(&output->layers.shell_bottom->node, !fs);
				wlr_scene_node_set_enabled(&output->layers.fullscreen->node, fs);
			}

			if (fs) {
				disable_workspace(child);

				if (floating) {
					arrange_workspace_floating(child);
				}
				arrange_fullscreen(child->layers.fullscreen, fs, child);
			} else {
				struct wlr_box *area = workspace_get_output_usable_area(child);
				struct side_gaps *gaps = &child->current_gaps;

				workspace_place_layers(child);

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
			workspace_hide(child);

			struct sway_transaction *transaction = server.queued_transaction;
			if (transaction && list_find(transaction->workspaces, child) != -1) {
				if (tiling) {
					struct wlr_box *area = workspace_get_output_usable_area(child);
					struct side_gaps *gaps = &child->current_gaps;
					arrange_workspace_tiling(child,
						area->width - gaps->left - gaps->right,
						area->height - gaps->top - gaps->bottom);
				}
				if (floating) {
					arrange_workspace_floating(child);
				}
			}

			disable_workspace(child);
		}
	}
}

static void animate_output(struct sway_output *output) {
	animate_layers(output);

	for (int i = 0; i < output->current.workspaces->length; i++) {
		struct sway_workspace *child = output->current.workspaces->items[i];

		if (!child || child->node.destroying) {
			continue;
		}
		bool activated = filter_workspace_enabled(child);

		// Se the positions of every workspace layers in case there is a switch
		workspace_place_layers(child);

		if (!activated) {
			// Hide the workspace in case a workspace switch showed it
			workspace_hide(child);
			continue;
		}

		// Filtered containers hide themselves
		struct sway_container *fs = child->current.fullscreen;
		if (fs) {
			animate_workspace_floating(child);
			animate_fullscreen(child->layers.fullscreen, fs, child);
		} else {
			animate_workspace_tiling(child);
			animate_workspace_floating(child);
		}
	}
}

void arrange_popups(struct wlr_scene_tree *popups) {
	struct wlr_scene_node *node;
	wl_list_for_each(node, &popups->children, link) {
		struct sway_popup_desc *popup = scene_descriptor_try_get(node,
			SWAY_SCENE_DESC_POPUP);

		if (popup) {
			double lx, ly;
			wlr_scene_node_coords(popup->relative, &lx, &ly);
			wlr_scene_node_set_position(node, lx, ly);
		}
	}
}

static void scene_arrange_root(struct sway_root *root) {
	struct sway_container *fs = root->fullscreen_global;

	wlr_scene_node_set_enabled(&root->layers.shell_background->node, !fs);
	wlr_scene_node_set_enabled(&root->layers.shell_bottom->node, !fs);
	wlr_scene_node_set_enabled(&root->layers.tiling->node, !fs);
	wlr_scene_node_set_enabled(&root->layers.floating->node, !fs);
	wlr_scene_node_set_enabled(&root->layers.shell_top->node, !fs);
	wlr_scene_node_set_enabled(&root->layers.fullscreen->node, !fs);

	// hide all contents in the scratchpad
	for (int i = 0; i < root->scratchpad->length; i++) {
		struct sway_container *con = root->scratchpad->items[i];

		disable_container(con);
		wlr_scene_node_set_enabled(&con->scene_tree->node, false);
	}

	if (fs) {
		for (int i = 0; i < root->outputs->length; i++) {
			struct sway_output *output = root->outputs->items[i];
			if (!output->enabled || !output->wlr_output->enabled) {
				continue;
			}
			struct sway_workspace *ws = output->current.active_workspace;

			wlr_scene_output_set_position(output->scene_output, output->lx, output->ly);

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

		arrange_fullscreen(root->layers.fullscreen_global, fs, NULL);
	} else {
		for (int i = 0; i < root->outputs->length; i++) {
			struct sway_output *output = root->outputs->items[i];
			if (!output->enabled || !output->wlr_output->enabled) {
				continue;
			}

			wlr_scene_output_set_position(output->scene_output, output->lx, output->ly);

			wlr_scene_node_reparent(&output->layers.shell_background->node, root->layers.shell_background);
			wlr_scene_node_reparent(&output->layers.shell_bottom->node, root->layers.shell_bottom);
			wlr_scene_node_reparent(&output->layers.tiling->node, root->layers.tiling);
			wlr_scene_node_reparent(&output->layers.shell_top->node, root->layers.shell_top);
			wlr_scene_node_reparent(&output->layers.shell_overlay->node, root->layers.shell_overlay);
			wlr_scene_node_reparent(&output->layers.fullscreen->node, root->layers.fullscreen);
			wlr_scene_node_reparent(&output->layers.session_lock->node, root->layers.session_lock);

			wlr_scene_node_set_position(&output->layers.shell_background->node, output->lx, output->ly);
			wlr_scene_node_set_position(&output->layers.shell_bottom->node, output->lx, output->ly);
			wlr_scene_node_set_position(&output->layers.tiling->node, output->lx, output->ly);
			wlr_scene_node_set_position(&output->layers.fullscreen->node, output->lx, output->ly);
			wlr_scene_node_set_position(&output->layers.shell_top->node, output->lx, output->ly);
			wlr_scene_node_set_position(&output->layers.shell_overlay->node, output->lx, output->ly);
			wlr_scene_node_set_position(&output->layers.session_lock->node, output->lx, output->ly);

			wlr_scene_node_set_enabled(&output->layers.shell_background->node, output->layer_shell_mask & LAYER_SHELL_BACKGROUND);
			wlr_scene_node_set_enabled(&output->layers.shell_bottom->node, output->layer_shell_mask & LAYER_SHELL_BOTTOM);
			wlr_scene_node_set_enabled(&output->layers.shell_top->node, output->layer_shell_mask & LAYER_SHELL_TOP);
			wlr_scene_node_set_enabled(&output->layers.shell_overlay->node, output->layer_shell_mask & LAYER_SHELL_OVERLAY);

			scene_arrange_output(output);
		}
	}

	for (int i = 0; i < root->unmapped_views->length; ++i) {
		struct sway_view *view = root->unmapped_views->items[i];
		struct sway_container *container = view->container;
		if (container) {
			wlr_scene_node_set_enabled(&container->scene_tree->node, true);
			output_configure_scene(NULL, &container->scene_tree->node, 1);
		}
	}
	arrange_popups(root->layers.popup);
}

static void animate_unmapped_views(void) {
	for (int i = 0; i < root->unmapped_views->length; ++i) {
		struct sway_view *view = root->unmapped_views->items[i];
		struct sway_container *container = view->container;
		if (!container) {
			continue;
		}
		struct sway_animated_variable *a = &container->animation.a;
		const float old_alpha = container->pending.alpha;
		if (a->origin != a->target) {
			container->pending.alpha = a->value;
		}
		const float alpha = container->pending.alpha;
		container_update(container);
		// Fade the saved content of the view
		output_configure_scene(NULL, &container->scene_tree->node, alpha);
		container->pending.alpha = old_alpha;
		// container_update() fades the borders, but the shadow is a separate
		// scene node, so it needs to be faded too.
		struct wlr_scene_shadow *shadow = container->shadow;
		if (shadow && shadow->enabled) {
			float color[4] = {
				container->pending.decoration.shadow_color_r * alpha,
				container->pending.decoration.shadow_color_g * alpha,
				container->pending.decoration.shadow_color_b * alpha,
				container->pending.decoration.shadow_color_a * alpha,
			};
			wlr_scene_shadow_set_properties(shadow, shadow->width,
				shadow->height, true, shadow->dynamic, shadow->size,
				shadow->blur, shadow->offset, color);
		}
		view_reconfigure(view);
	}
}

void transaction_animate_output(struct sway_output *output) {
	struct sway_container *fs = root->fullscreen_global;
	if (fs) {
		animate_fullscreen(root->layers.fullscreen_global, fs, NULL);
	} else if (output->enabled && output->wlr_output->enabled) {
		animate_output(output);
	}
	animate_unmapped_views();
	arrange_popups(root->layers.popup);
}

void transaction_animate_outputs(void) {
	struct sway_container *fs = root->fullscreen_global;
	if (fs) {
		animate_fullscreen(root->layers.fullscreen_global, fs, NULL);
	} else {
		for (int i = 0; i < root->outputs->length; i++) {
			struct sway_output *output = root->outputs->items[i];
			if (!output->enabled || !output->wlr_output->enabled) {
				continue;
			}
			animate_output(output);
		}
	}
	animate_unmapped_views();
	arrange_popups(root->layers.popup);
}

/**
 * Apply a transaction to the "current" state of the tree. Its animations
 * join `group`.
 */
static void transaction_apply(struct sway_transaction *transaction,
		struct sway_animation_group *group) {
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

	// Outputs that animate a workspace switch, each followed by "from" workspace
	list_t *switches = create_list();

	// Apply the instruction state to the node's current state
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct sway_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct sway_node *node = instruction->node;

		switch (node->type) {
		case N_ROOT:
			break;
		case N_OUTPUT:
			if (instruction->output_state.workspace_switch_animated) {
				list_add(switches, node->sway_output);
				list_add(switches, node->sway_output->current.active_workspace);
			}
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
		case N_LAYER_SURFACE:
			apply_layer_surface_state(node->sway_layer_surface,
					&instruction->layer_state);
			break;
		case N_LAYER_POPUP:
			apply_layer_popup_state(node->sway_layer_popup,
					&instruction->layer_state);
			break;
		}

		node->instruction = NULL;
	}

	for (int i = 0; i < switches->length; i += 2) {
		struct sway_output *output = switches->items[i];
		struct sway_workspace *from = switches->items[i + 1];
		workspace_switch_animate(output, from, output->current.active_workspace,
			transaction->disable_animations ? NULL :
				animation_get_spec(ANIMATION_WORKSPACE_SWITCH), group);
	}
	list_free(switches);
}

static void animate_children(struct sway_workspace *workspace,
		enum sway_container_layout layout, list_t *children,
		struct wlr_scene_tree *content) {
	if (children->length == 0) {
		return;
	}

	struct sway_layout_transform transform;
	layout_transform_workspace_get(workspace, true, &transform);
	const double scale = transform.scale;
	if (layout == L_VERT) {
		for (int i = 0; i < children->length; ++i) {
			struct sway_container *child = children->items[i];
			struct sway_animated_variable *y = &child->animation.y;
			wlr_scene_node_set_enabled(&child->decoration.tree->node, true);
			const double x = scale * animated_variable_get_offset(y);
			wlr_scene_node_reparent(&child->scene_tree->node, content);
			wlr_scene_node_set_position(&child->scene_tree->node, x,
				layout_transform_y(&transform, y->value) - workspace->y);
			animated_variable_reset(&child->animation.x, child->current.x);
			animate_container(child, child->animation.w.value, child->animation.h.value,
				true, 0, workspace);
		}
	} else if (layout == L_HORIZ) {
		for (int i = 0; i < children->length; ++i) {
			struct sway_container *child = children->items[i];
			struct sway_animated_variable *x = &child->animation.x;
			wlr_scene_node_set_enabled(&child->decoration.tree->node, true);
			const double y = scale * animated_variable_get_offset(x);
			wlr_scene_node_reparent(&child->scene_tree->node, content);
			wlr_scene_node_set_position(&child->scene_tree->node,
				layout_transform_x(&transform, x->value) - workspace->x, y);
			animated_variable_reset(&child->animation.y, child->current.y);
			animate_container(child, child->animation.w.value, child->animation.h.value,
				true, 0, workspace);
		}
	} else {
		sway_assert(false, "unreachable");
	}
}

static void transaction_commit_pending(void);

static void sync_animation_variables(bool animate);
static void animate_transaction(struct sway_transaction *transaction,
	struct sway_animation_group *group);
static void transaction_animations_done(void *data);

static void transaction_progress(void) {
	if (!server.queued_transaction) {
		return;
	}
	if (server.queued_transaction->num_waiting > 0) {
		return;
	}
	struct sway_transaction *transaction = server.queued_transaction;
	// The group tells when every animation started by the transaction is over
	struct sway_animation_group *group = animation_group_create(
		server.animation_manager, transaction_animations_done, NULL);
	// Variables that are not animating start from where their nodes are
	sync_animation_variables(!transaction->disable_animations);
	transaction_apply(transaction, group);
	scene_arrange_root(root);
	// Start the animations for the changes of the transaction, and render
	// with the initial values. Future frames will be scheduled by the outputs
	animate_transaction(transaction, group);
	if (!animation_group_empty(group)) {
		animation_group_commit(group);
	}
	animation_group_unref(group);
	transaction_animate_outputs();
	cursor_rebase_all();
	server.queued_transaction = NULL;
	transaction_destroy(transaction);
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

static double workspace_state_scale(double scale) {
	return scale > 0.0 ? scale : 1.0;
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
	struct sway_workspace *workspace = node->sway_container->pending.workspace;
	if (workspace &&
		(workspace_state_scale(workspace->current.scale) != workspace_state_scale(workspace->scale) ||
			workspace->animation.s.origin != workspace->animation.s.target)) {
		return true;
	}
	struct sway_container_state *cstate = &node->sway_container->current;
	struct sway_container_state *istate = &instruction->container_state;
#if WLR_HAS_XWAYLAND
	// Xwayland views are position-aware and need to be reconfigured
	// when their position changes.
	// For scroll, they all need to be reconfigured, not just the dirty ones,
	// because focusing or moving may change their positions, so we do it in
	// scene_arrange_container(), which goes over every window. We only configure
	// here those that come in the transaction.
	if (node->sway_container->view->type == SWAY_VIEW_XWAYLAND) {
		int cx, cy, cw, ch;
		map_to_configure(node->sway_container->view, cstate->content_x, cstate->content_y,
			cstate->content_width, cstate->content_height, &cx, &cy, &cw, &ch);
		int ix, iy, iw, ih;
		map_to_configure(node->sway_container->view, istate->content_x, istate->content_y,
			istate->content_width, istate->content_height, &ix, &iy, &iw, &ih);
		if (cx != ix || cy != iy || cw != iw || ch != ih) {
			// Update old_content so we don't re-configure in scene_arrange_container()
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

static void set_surface_preferred_buffer_scale(struct sway_view *view) {
	double scale = 1;
	double content_scale = view_is_content_scaled(view) ? view_get_content_scale(view) : 1.0;
	if (wl_list_empty(&view->surface->current_outputs)) {
		struct sway_workspace *workspace = view->container->pending.workspace;
		if (workspace && workspace->output) {
			scale = content_scale * workspace->output->wlr_output->scale;
		}
	} else {
		struct wlr_surface_output *surface_output;
		wl_list_for_each(surface_output, &view->surface->current_outputs, link) {
			if (surface_output->output->scale * content_scale > scale) {
				scale = surface_output->output->scale * content_scale;
			}
		}
	}
	wlr_fractional_scale_v1_notify_scale(view->surface, scale);
	wlr_surface_set_preferred_buffer_scale(view->surface, ceil(scale));
}

static void transaction_commit(struct sway_transaction *transaction) {
	sway_log(SWAY_DEBUG, "Transaction %p committing with %i instructions",
			transaction, transaction->instructions->length);
	transaction->num_waiting = 0;
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct sway_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct sway_node *node = instruction->node;
		if (should_configure(node, instruction)) {
			bool hidden = !node->destroying &&
				!view_is_visible(node->sway_container->view);
			set_surface_preferred_buffer_scale(node->sway_container->view);
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

	// Save the buffers of the views, so they keep showing their content while
	// the transaction waits for the clients. If it does not have to wait, it
	// is applied right away and there is nothing to keep, except for the views
	// that are closing: their last frame is used by the fade out animation.
	// Every container of a workspace is in the transaction when it is
	// arranged, so this avoids saving and removing the buffers of all of them
	// when they only move.
	const bool waiting = transaction->num_waiting > 0;
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct sway_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct sway_node *node = instruction->node;
		if (!node_is_view(node)) {
			continue;
		}
		struct sway_view *view = node->sway_container->view;
		if (view->saved_surface_tree || view->container != node->sway_container) {
			// It already has it, or the view was mapped again and belongs to
			// a different container
			continue;
		}
		if (node->destroying || (waiting && view_is_visible(view))) {
			view_save_buffer(view);
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
		view->container ? view->container->node.instruction : NULL;
	if (!instruction) {
		// The view is not part of the transaction being applied
		return false;
	}
	int ccx, ccy, ccw, cch;
	map_to_configure(view, instruction->container_state.content_x, instruction->container_state.content_y,
		instruction->container_state.content_width, instruction->container_state.content_height,
		&ccx, &ccy, &ccw, &cch);
	if (ccx == (int)x && ccy == (int)y && ccw == width && cch == height) {
		set_instruction_ready(instruction);
		return true;
	}
	return false;
}

/**
 * Animations
 *
 * When a transaction is applied, the animated variables of the nodes get the
 * state of the nodes as their targets, and those that change start an
 * animation. The transaction is not needed after that: the animation
 * manager runs the animations, and the frames of the outputs show them.
 */

// A container that is closing is kept alive by its fade out animation
static void container_exit_done(void *data) {
	struct sway_container *con = data;
	struct sway_container *parent = con->exiting_parent;
	con->exiting_parent = NULL;
	node_unref(&con->node);
	if (parent) {
		node_unref(&parent->node);
	}
}

static void container_begin_exit(struct sway_container *con, bool animate) {
	if (con->exiting) {
		return;
	}
	con->exiting = true;
	node_ref(&con->node);
	// The scene tree of the container is in its parent's. Destroying the parent
	// would move the container's scene tree out of the graph
	con->exiting_parent = con->current.parent;
	if (con->exiting_parent) {
		node_ref(&con->exiting_parent->node);
	}
	struct sway_workspace *ws = con->current.workspace;
	struct sway_animation_group *group = animation_group_create(
		server.animation_manager, container_exit_done, con);
	const struct sway_animation_options options = {
		.spec = animate ? animation_get_spec(ANIMATION_FADE_OUT) : NULL,
		.group = group,
		.clock = ws ? output_animation_clock(ws->output) : NULL,
	};
	animated_variable_set(&con->animation.a, con->pending.alpha, &options);
	// If there is nothing to animate, the group is done already
	animation_group_commit(group);
	animation_group_unref(group);
}

// Before applying a transaction: idle variables take the values of the
// current state of their nodes, which are the origin of the animations.
static void container_sync_animation_variables(struct sway_container *child,
		bool animate) {
	if (!child) {
		return;
	}
	if (child->node.destroying) {
		// It is still in the tree for the last time
		container_begin_exit(child, animate);
		return;
	}
	animated_variable_reset(&child->animation.x, child->current.x);
	animated_variable_reset(&child->animation.y, child->current.y);
	animated_variable_reset(&child->animation.w, child->current.width);
	animated_variable_reset(&child->animation.h, child->current.height);
	animated_variable_reset(&child->animation.a, container_state_alpha(&child->current));
#if WLR_HAS_XWAYLAND
	if (child->view && child->view->type == SWAY_VIEW_XWAYLAND) {
		child->old_content.x = child->current.content_x;
		child->old_content.y = child->current.content_y;
		child->old_content.width = child->current.content_width;
		child->old_content.height = child->current.content_height;
	}
#endif
}

static void children_sync_animation_variables(list_t *children, bool animate) {
	if (!children) {
		return;
	}
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *child = children->items[i];
		container_sync_animation_variables(child, animate);
		children_sync_animation_variables(child->pending.children, animate);
	}
}

static void sync_animation_variables(bool animate) {
	if (root->fullscreen_global) {
		return;
	}
	for (int j = 0; j < root->unmapped_views->length; ++j) {
		struct sway_view *view = root->unmapped_views->items[j];
		container_sync_animation_variables(view->container, animate);
	}
	for (int j = 0; j < root->outputs->length; j++) {
		struct sway_output *output = root->outputs->items[j];
		for (int i = 0; i < output->workspaces->length; i++) {
			struct sway_workspace *ws = output->workspaces->items[i];
			animated_variable_reset(&ws->animation.s,
				workspace_state_scale(ws->current.scale));
			children_sync_animation_variables(ws->tiling, animate);
			children_sync_animation_variables(ws->floating, animate);
		}
	}
}

// After applying a transaction: the new state is the target of the variables
static void container_animate_variables(struct sway_container *child,
		const struct sway_animation_options *options, bool fade_enabled) {
	if (!child || child->node.destroying) {
		return;
	}
	animated_variable_set(&child->animation.w, child->current.width, options);
	animated_variable_set(&child->animation.h, child->current.height, options);
	const float alpha = container_state_alpha(&child->current);
	// Opacity uses its own animation independent of the one assigned to the transaction.
	struct sway_animation_options fade = *options;
	fade.spec = fade_enabled ? animation_get_spec(
		alpha >= child->animation.a.value ?
			ANIMATION_FADE_IN : ANIMATION_FADE_OUT) : NULL;
	animated_variable_set(&child->animation.a, alpha, &fade);
}

static void children_animate_variables(list_t *children,
		const struct sway_animation_options *options, bool fade) {
	if (!children) {
		return;
	}
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *child = children->items[i];
		container_animate_variables(child, options, fade);
		children_animate_variables(child->pending.children, options, fade);
	}
}

static void layer_animate_variables(struct wlr_scene_tree *tree,
		const struct sway_animation_options *options) {
	struct wlr_scene_node *node;
	wl_list_for_each(node, &tree->children, link) {
		struct sway_layer_surface *surface = scene_descriptor_try_get(node,
			SWAY_SCENE_DESC_LAYER_SHELL);
		// surface could be null during destruction
		if (!surface) {
			continue;
		}

		if (!surface->scene->layer_surface->initialized) {
			continue;
		}

		animated_variable_reset(&surface->animation.x, surface->pending.x);
		animated_variable_reset(&surface->animation.y, surface->pending.y);
		animated_variable_set(&surface->animation.w, surface->pending.width, options);
		animated_variable_set(&surface->animation.h, surface->pending.height, options);

		for (int i = 0; i < surface->layer_popups->length; ++i) {
			struct sway_layer_popup *popup = surface->layer_popups->items[i];
			animated_variable_reset(&popup->animation.x, popup->pending.x);
			animated_variable_reset(&popup->animation.y, popup->pending.y);
			animated_variable_set(&popup->animation.w, popup->pending.width, options);
			animated_variable_set(&popup->animation.h, popup->pending.height, options);
		}
	}
}

static void output_animate_variables(struct sway_output *output,
		const struct sway_animation_options *options, bool fade) {
	layer_animate_variables(output->layers.shell_overlay, options);
	layer_animate_variables(output->layers.shell_top, options);
	layer_animate_variables(output->layers.shell_bottom, options);
	layer_animate_variables(output->layers.shell_background, options);

	for (int i = 0; i < output->workspaces->length; i++) {
		struct sway_workspace *ws = output->workspaces->items[i];
		animated_variable_set(&ws->animation.s,
			workspace_state_scale(ws->scale), options);
		// Update workspace pan
		animated_variable_set(&ws->animation.pan_x, ws->pan.x, options);
		animated_variable_set(&ws->animation.pan_y, ws->pan.y, options);
		children_animate_variables(ws->tiling, options, fade);
		children_animate_variables(ws->floating, options, fade);
	}
}

// The animations of a transaction ended
static void transaction_animations_done(void *data) {
	cursor_rebase_all();
}

static void animate_transaction(struct sway_transaction *transaction,
		struct sway_animation_group *group) {
	struct sway_animation_options options = {
		.spec = transaction->disable_animations ?
			NULL : animation_get_spec(transaction->animation),
		.group = group,
	};

	struct sway_container *fs = root->fullscreen_global;
	if (fs) {
		set_container_positions(fs, fs->animation.w.target,
			fs->animation.h.target, &options);
	} else {
		for (int i = 0; i < root->outputs->length; ++i) {
			struct sway_output *output = root->outputs->items[i];
			options.clock = output_animation_clock(output);
			output_animate_variables(output, &options,
				!transaction->disable_animations);
			if (!output->enabled) {
				continue;
			}
			for (int j = 0; j < output->workspaces->length; ++j) {
				struct sway_workspace *ws = output->workspaces->items[j];
				if (ws->node.destroying) {
					continue;
				}
				set_workspace_positions(ws, &options);
			}
		}
	}
}

static void overview_recompute_scales() {
	struct sway_container *fs = root->fullscreen_global;

	if (!fs) {
		for (int j = 0; j < root->outputs->length; j++) {
			struct sway_output *output = root->outputs->items[j];
			if (!output->enabled || !output->wlr_output->enabled) {
				continue;
			}
			for (int i = 0; i < output->workspaces->length; i++) {
				struct sway_workspace *child = output->workspaces->items[i];
				if (!child || child->node.destroying) {
					continue;
				}
				if (!filter_workspace_enabled(child)) {
					continue;
				}
				enum sway_layout_overview mode = layout_overview_mode(child);
				if (mode != OVERVIEW_DISABLED) {
					layout_overview_recompute_scale(child);
					arrange_workspace(child);
				}
			}
		}
	}
}

/**
 * Continuous animations
 *
 * While something moves a container following the input (dragging a window),
 * its position is not animated, it is set by the caller. The animated
 * variables track it, and when the caller untracks them, the animation that
 * settles the values starts with the container's velocity.
 *
 * The container owns the tracking: untracking it stops every variable that
 * was tracked for it.
 */
static struct sway_animation_clock *workspace_animation_clock(
		struct sway_workspace *ws) {
	return ws ? output_animation_clock(ws->output) : NULL;
}

void transaction_track_container(struct sway_container *con) {
	struct sway_animation_clock *clock =
		workspace_animation_clock(con->pending.workspace);
	animated_variable_track(&con->animation.x, con->pending.x, clock, con);
	animated_variable_track(&con->animation.y, con->pending.y, clock, con);
}

void transaction_untrack_container(struct sway_container *con) {
	animation_manager_untrack(server.animation_manager, con);
}

// The type of animation for the next commit
static enum sway_animation_type next_animation = ANIMATION_DEFAULT;

void transaction_set_animation(enum sway_animation_type type) {
	next_animation = type;
}

// While it is not zero, no commit is animated
static int animations_disabled = 0;

void transaction_disable_animations(bool disable) {
	animations_disabled += disable ? 1 : -1;
}

static void _transaction_commit_dirty(bool server_request) {
	const enum sway_animation_type animation = next_animation;
	next_animation = ANIMATION_DEFAULT;
	if (!server.dirty_nodes->length) {
		return;
	}

	filter_apply();
	overview_recompute_scales();

	if (!server.pending_transaction) {
		server.pending_transaction = transaction_create();
		if (!server.pending_transaction) {
			return;
		}
	}
	struct sway_transaction *transaction = server.pending_transaction;
	if (animation != ANIMATION_DEFAULT) {
		// Override the animation type unless it is not specified
		transaction->animation = animation;
	}
	transaction->disable_animations |= animations_disabled > 0;

	for (int i = 0; i < server.dirty_nodes->length; ++i) {
		struct sway_node *node = server.dirty_nodes->items[i];
		transaction_add_node(server.pending_transaction, node, server_request);
		node->dirty = false;
	}
	server.dirty_nodes->length = 0;

	transaction_commit_pending();
}

void transaction_commit_dirty(void) {
	_transaction_commit_dirty(true);
}

void transaction_commit_dirty_client(void) {
	_transaction_commit_dirty(false);
}
