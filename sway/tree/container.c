#include <assert.h>
#include <drm_fourcc.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_subcompositor.h>
#include "sway/config.h"
#include "sway/desktop/transaction.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/scene_descriptor.h"
#include "sway/sway_text_node.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/arrange.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/xdg_decoration.h"
#include "list.h"
#include "pango.h"
#include "log.h"
#include "stringop.h"
#include "util.h"

static void handle_output_enter(
		struct wl_listener *listener, void *data) {
	struct sway_container *con = wl_container_of(
			listener, con, output_enter);
	struct sway_scene_output *output = data;

	if (con->view->foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_output_enter(
			con->view->foreign_toplevel, output->output);
	}
}

static void handle_output_leave(
		struct wl_listener *listener, void *data) {
	struct sway_container *con = wl_container_of(
			listener, con, output_leave);
	struct sway_scene_output *output = data;

	if (con->view->foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_output_leave(
			con->view->foreign_toplevel, output->output);
	}
}

static void handle_destroy(
		struct wl_listener *listener, void *data) {
	struct sway_container *con = wl_container_of(
			listener, con, output_handler_destroy);

	container_begin_destroy(con);
}

static bool handle_point_accepts_input(
		struct sway_scene_buffer *buffer, double *x, double *y) {
	return false;
}

static struct sway_scene_decoration *alloc_decoration_node(struct sway_scene_tree *parent,
		struct sway_view *view, bool *failed) {
	if (*failed) {
		return NULL;
	}

	// just pass in random values. These will be overwritten when
	// they need to be used.
	struct sway_scene_decoration *decoration = sway_scene_decoration_create(parent, view, 0, 0);
	if (!decoration) {
		sway_log(SWAY_ERROR, "Failed to allocate a sway_scene_decoration");
		*failed = true;
	}
	if (!failed && !scene_descriptor_assign(&decoration->node,
			SWAY_SCENE_DESC_NON_INTERACTIVE, (void *)1)) {
		*failed = true;
	}
	return decoration;
}

struct sway_container *container_create(struct sway_view *view) {
	struct sway_container *c = calloc(1, sizeof(struct sway_container));
	if (!c) {
		sway_log(SWAY_ERROR, "Unable to allocate sway_container");
		return NULL;
	}
	node_init(&c->node, N_CONTAINER, c);

	// Container tree structure
	// - scene tree
	//   - decoration
	//     - title bar
	//       - title text
	//       - marks text
	//     - full
	//     - content_tree (we put the content node here so when we disable the
	//       decoration everything gets disabled. We only render the content iff there
	//       is a decoration as well)
	//     - buffer used for output enter/leave events for foreign_toplevel
	//   - jump
	//     - text
	bool failed = false;
	c->scene_tree = alloc_scene_tree(root->staging, &failed);

	c->decoration.tree = alloc_scene_tree(c->scene_tree, &failed);
	c->title_bar.tree = alloc_scene_tree(c->decoration.tree, &failed);
	c->content_tree = alloc_scene_tree(c->decoration.tree, &failed);

	c->jump.text = NULL;
	c->jump.tree = alloc_scene_tree(c->scene_tree, &failed);
	sway_scene_node_set_enabled(&c->jump.tree->node, false);

	if (view) {
		// only containers with views can have borders
		c->decoration.full = alloc_decoration_node(c->decoration.tree, view, &failed);
		c->shadow = sway_scene_shadow_create(c->decoration.tree, c->decoration.full);
		sway_scene_node_set_enabled(&c->shadow->node, false);
		// We set the title bar at the top and the shadow at the bottom
		sway_scene_node_lower_to_bottom(&c->shadow->node);
		sway_scene_node_raise_to_top(&c->title_bar.tree->node);
		c->output_handler = sway_scene_buffer_create(c->decoration.tree, NULL);
		if (!c->output_handler) {
			sway_log(SWAY_ERROR, "Failed to allocate a scene node");
			failed = true;
		}

		if (!failed) {
			c->output_enter.notify = handle_output_enter;
			wl_signal_add(&c->output_handler->events.output_enter,
					&c->output_enter);
			c->output_leave.notify = handle_output_leave;
			wl_signal_add(&c->output_handler->events.output_leave,
					&c->output_leave);
			c->output_handler_destroy.notify = handle_destroy;
			wl_signal_add(&c->output_handler->node.events.destroy,
					&c->output_handler_destroy);
			c->output_handler->point_accepts_input = handle_point_accepts_input;
		}
	}

	if (!failed && !scene_descriptor_assign(&c->scene_tree->node,
			SWAY_SCENE_DESC_CONTAINER, c)) {
		failed = true;
	}

	if (failed) {
		sway_scene_node_destroy(&c->scene_tree->node);
		free(c);
		return NULL;
	}

	if (!view) {
		c->pending.children = create_list();
		c->current.children = create_list();
	}

	c->pending.layout = L_NONE;
	c->view = view;
	c->alpha = 1.0f;
	c->marks = create_list();
	c->toggle_size.single = false;
	c->toggle_size.state = TOGGLE_STATE_NONE;
	c->fullscreen = false;

	wl_signal_init(&c->events.destroy);
	wl_signal_emit_mutable(&root->events.new_node, &c->node);

	container_update(c);

	return c;
}

static bool container_is_focused(struct sway_container *con, void *data) {
	return con->current.focused;
}

static bool container_has_focused_child(struct sway_container *con) {
	return container_find_child(con, container_is_focused, NULL);
}

static bool container_is_current_parent_focused(struct sway_container *con) {
	if (con->current.parent) {
		struct sway_container *parent = con->current.parent;
		return parent->current.focused || container_is_current_parent_focused(parent);
	} else if (con->current.workspace) {
		struct sway_workspace *ws = con->current.workspace;
		return ws->current.focused;
	}

	return false;
}

static struct border_colors *container_get_current_colors(
		struct sway_container *con) {
	struct border_colors *colors;

	bool urgent = con->view ?
		view_is_urgent(con->view) : container_has_urgent_child(con);

	struct sway_workspace *workspace = con->pending.workspace;
	bool pinned = false;
	if (workspace) {
		if (layout_pin_enabled(workspace)) {
			struct sway_container *parent = con->pending.parent ? con->pending.parent : con;
			pinned = layout_pin_get_container(workspace) == parent;
		}
	}

	struct sway_container *active_child;

	if (con->current.parent) {
		active_child = con->current.parent->current.focused_inactive_child;
	} else if (con->current.workspace) {
		active_child = con->current.workspace->current.focused_inactive_child;
	} else {
		active_child = NULL;
	}

	if (urgent) {
		colors = &config->border_colors.urgent;
	} else if (layout_selection_enabled(con)) {
		if (con->current.focused || container_is_current_parent_focused(con)) {
			colors = &config->border_colors.selected_focused;
		} else {
			colors = &config->border_colors.selected;
		}
	} else if (pinned) {
		if (con->current.focused || container_is_current_parent_focused(con)) {
			colors = &config->border_colors.pinned_focused;
		} else {
			colors = &config->border_colors.pinned;
		}
	} else if (con->current.focused || container_is_current_parent_focused(con)) {
		colors = &config->border_colors.focused;
	} else if (config->has_focused_tab_title && container_has_focused_child(con)) {
		colors = &config->border_colors.focused_tab_title;
	} else if (con == active_child) {
		colors = &config->border_colors.focused_inactive;
	} else {
		colors = &config->border_colors.unfocused;
	}

	return colors;
}

static bool container_is_current_floating(struct sway_container *container) {
	if (!container->current.parent && container->current.workspace &&
			list_find(container->current.workspace->floating, container) != -1) {
		return true;
	}
	if (container->scratchpad) {
		return true;
	}
	return false;
}

// scene border wants premultiplied colors
static void scene_border_set_colors(struct sway_scene_decoration *decoration,
		const float top[4], const float bottom[4], const float left[4],
		const float right[4], float opacity) {
	const float pre_top[] = {
		top[0] * top[3] * opacity,
		top[1] * top[3] * opacity,
		top[2] * top[3] * opacity,
		top[3] * opacity,
	};
	const float pre_bottom[] = {
		bottom[0] * bottom[3] * opacity,
		bottom[1] * bottom[3] * opacity,
		bottom[2] * bottom[3] * opacity,
		bottom[3] * opacity,
	};
	const float pre_left[] = {
		left[0] * left[3] * opacity,
		left[1] * left[3] * opacity,
		left[2] * left[3] * opacity,
		left[3] * opacity,
	};
	const float pre_right[] = {
		right[0] * right[3] * opacity,
		right[1] * right[3] * opacity,
		right[2] * right[3] * opacity,
		right[3] * opacity,
	};

	sway_scene_decoration_set_border_color(decoration, pre_top, pre_bottom, pre_left, pre_right);
}

void container_update(struct sway_container *con) {
	struct border_colors *colors = container_get_current_colors(con);
	float alpha = con->alpha;

	float top[4], bottom[4], left[4], right[4], title_border[4];
	memcpy(top, colors->child_border, sizeof(top));
	memcpy(bottom, colors->child_border, sizeof(bottom));
	memcpy(left, colors->child_border, sizeof(left));
	memcpy(right, colors->child_border, sizeof(right));
	memcpy(title_border, colors->border, sizeof(title_border));

	if (!container_is_current_floating(con)) {
		struct sway_workspace *workspace = con->current.workspace;
		if (workspace && con->current.focused) {
			enum sway_container_layout mode = layout_modifiers_get_mode(workspace);
			enum sway_layout_insert insert = layout_modifiers_get_insert(workspace);
			switch (insert) {
			case INSERT_BEFORE:
			case INSERT_BEGINNING:
				if (mode == L_HORIZ) {
					memcpy(left, colors->indicator, sizeof(left));
				} else if (mode == L_VERT) {
					memcpy(top, colors->indicator, sizeof(top));
					memcpy(title_border, colors->indicator, sizeof(title_border));
				}
				break;
			case INSERT_AFTER:
			case INSERT_END:
				if (mode == L_HORIZ) {
					memcpy(right, colors->indicator, sizeof(right));
				} else if (mode == L_VERT) {
					memcpy(bottom, colors->indicator, sizeof(bottom));
				}
				break;
				break;
			}
		}
	}

	if (con->view) {
		scene_border_set_colors(con->decoration.full, top, bottom, left, right, alpha);
		if (con->pending.decoration.dim && !(con->current.focused || container_is_current_parent_focused(con))) {
			float color[4] = {
				con->pending.decoration.dim_color_r,
				con->pending.decoration.dim_color_g,
				con->pending.decoration.dim_color_b,
				con->pending.decoration.dim_color_a
			};
			sway_scene_decoration_set_dimming(con->decoration.full, con->pending.decoration.dim, color);
		} else {
			float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			sway_scene_decoration_set_dimming(con->decoration.full, false, color);
		}
	}

	if (con->title_bar.title_text) {
		sway_text_node_set_color(con->title_bar.title_text, colors->text);
		sway_text_node_set_background(con->title_bar.title_text, colors->background);
	}

	if (con->title_bar.marks_text) {
		sway_text_node_set_color(con->title_bar.marks_text, colors->text);
		sway_text_node_set_background(con->title_bar.marks_text, colors->background);
	}
	if (con->decoration.full) {
		const float premultiplied[] = {
			colors->background[0] * colors->background[3] * alpha,
			colors->background[1] * colors->background[3] * alpha,
			colors->background[2] * colors->background[3] * alpha,
			colors->background[3] * alpha,
		};
		sway_scene_decoration_set_title_bar_color(con->decoration.full, premultiplied);
	}
}

void container_update_itself_and_parents(struct sway_container *con) {
	container_update(con);

	if (con->current.parent) {
		container_update_itself_and_parents(con->current.parent);
	}
}

void container_arrange_title_bar(struct sway_container *con) {
	if (con->pending.border != B_NORMAL) {
		return;
	}

	enum alignment title_align = config->title_align;
	int marks_buffer_width = 0;
	double width = con->title_width;
	int height = container_titlebar_height();

	struct sway_workspace *workspace = con->pending.workspace;
	double scale = workspace ? (layout_scale_enabled(workspace) ? layout_scale_get(workspace) : 1.0) : 1.0;

	if (con->title_bar.marks_text) {
		struct sway_text_node *node = con->title_bar.marks_text;
		marks_buffer_width = node->width;

		double h_padding;
		if (title_align == ALIGN_RIGHT) {
			h_padding = config->titlebar_h_padding;
		} else {
			h_padding = width - config->titlebar_h_padding - marks_buffer_width;
		}

		h_padding = MAX(h_padding, config->titlebar_h_padding);

		double alloc_width = MIN(node->width,
			width - h_padding - config->titlebar_h_padding);
		alloc_width = MAX(alloc_width, 0);

		sway_text_node_set_max_width(node, round(alloc_width));
		sway_scene_node_set_position(node->node,
			scale * h_padding, scale * (height - node->height) * 0.5);
		sway_text_node_scale(node, scale);
	}

	if (con->title_bar.title_text) {
		struct sway_text_node *node = con->title_bar.title_text;

		double h_padding;
		if (title_align == ALIGN_RIGHT) {
			h_padding = width - config->titlebar_h_padding - node->width;
		} else if (title_align == ALIGN_CENTER) {
			h_padding = (width - marks_buffer_width - node->width) * 0.5;
		} else {
			h_padding = config->titlebar_h_padding;
		}

		h_padding = MAX(h_padding, config->titlebar_h_padding);

		double alloc_width = MIN(node->width,
			width - h_padding - config->titlebar_h_padding);
		alloc_width = MAX(alloc_width, 0);

		sway_text_node_set_max_width(node, round(alloc_width));
		sway_scene_node_set_position(node->node,
			scale * h_padding, scale * (height - node->height) * 0.5);
		sway_text_node_scale(node, scale);
	}

	if (width <= 0 || height <= 0) {
		return;
	}

	sway_scene_decoration_set_title_bar(con->decoration.full, true, scale * height, scale * config->titlebar_border_radius);

	container_update(con);
}

void container_update_marks(struct sway_container *con) {
	char *buffer = NULL;

	if (config->show_marks && con->marks->length) {
		size_t len = 0;
		for (int i = 0; i < con->marks->length; ++i) {
			char *mark = con->marks->items[i];
			if (mark[0] != '_') {
				len += strlen(mark) + 2;
			}
		}
		buffer = calloc(len + 1, 1);
		char *part = malloc(len + 1);

		if (!sway_assert(buffer && part, "Unable to allocate memory")) {
			free(buffer);
			return;
		}

		for (int i = 0; i < con->marks->length; ++i) {
			char *mark = con->marks->items[i];
			if (mark[0] != '_') {
				snprintf(part, len + 1, "[%s]", mark);
				strcat(buffer, part);
			}
		}
		free(part);
	}

	if (!buffer) {
		if (con->title_bar.marks_text) {
			sway_scene_node_destroy(con->title_bar.marks_text->node);
			con->title_bar.marks_text = NULL;
		}
	} else if (!con->title_bar.marks_text) {
		struct border_colors *colors = container_get_current_colors(con);

		con->title_bar.marks_text = sway_text_node_create(con->title_bar.tree,
			buffer, colors->text, false);
	} else {
		sway_text_node_set_text(con->title_bar.marks_text, buffer);
	}

	container_arrange_title_bar(con);
	free(buffer);
}

void container_update_title_bar(struct sway_container *con) {
	if (!con->formatted_title) {
		return;
	}

	struct border_colors *colors = container_get_current_colors(con);

	if (con->title_bar.title_text) {
		sway_scene_node_destroy(con->title_bar.title_text->node);
		con->title_bar.title_text = NULL;
	}

	con->title_bar.title_text = sway_text_node_create(con->title_bar.tree,
		con->formatted_title, colors->text, config->pango_markup);

	// we always have to remake these text buffers completely for text font
	// changes etc...
	if (con->title_bar.marks_text) {
		sway_scene_node_destroy(con->title_bar.marks_text->node);
		con->title_bar.marks_text = NULL;
	}

	container_update_marks(con);
	container_arrange_title_bar(con);
}

void container_destroy(struct sway_container *con) {
	if (!sway_assert(con->node.destroying,
				"Tried to free container which wasn't marked as destroying")) {
		return;
	}
	if (!sway_assert(con->node.ntxnrefs == 0, "Tried to free container "
				"which is still referenced by transactions")) {
		return;
	}
	free(con->title);
	free(con->formatted_title);
	free(con->title_format);
	list_free(con->pending.children);
	list_free(con->current.children);

	list_free_items_and_destroy(con->marks);

	if (con->view && con->view->container == con) {
		con->view->container = NULL;
		sway_scene_node_destroy(&con->output_handler->node);
		if (con->view->destroying) {
			view_destroy(con->view);
		}
	}

	scene_node_disown_children(con->content_tree);
	sway_scene_node_destroy(&con->scene_tree->node);
	free(con);
}

void container_begin_destroy(struct sway_container *con) {
	if (con->view) {
		ipc_event_window(con, "close");
	}
	// The workspace must have the fullscreen pointer cleared so that the
	// seat code can find an appropriate new focus.
	if (con->pending.fullscreen_mode == FULLSCREEN_WORKSPACE && con->pending.workspace) {
		con->pending.workspace->fullscreen = NULL;
	}

	// If the container was stored in the layout structure for the workspace, clear it.
	if (con->pending.workspace) {
		if (con->pending.workspace->layout.fullscreen == con) {
			con->pending.workspace->layout.fullscreen = NULL;
		}
		if (con->pending.workspace->layout.pin.container == con) {
			con->pending.workspace->layout.pin.container = NULL;
		}
		if (con->pending.workspace->layout.toggle_size.container == con) {
			con->pending.workspace->layout.toggle_size.container = NULL;
		}
	}

	if (con->scratchpad && con->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		container_fullscreen_disable(con);
	}

	wl_signal_emit_mutable(&con->node.events.destroy, &con->node);

	container_end_mouse_operation(con);

	node_set_dirty(&con->node);
	con->node.destroying = true;

	if (con->scratchpad) {
		root_scratchpad_remove_container(con);
	}

	if (con->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		container_fullscreen_disable(con);
	}

	if (con->pending.parent || con->pending.workspace) {
		container_detach(con);
	}

	if (con->view && con->view->container == con) {
		wl_list_remove(&con->output_enter.link);
		wl_list_remove(&con->output_leave.link);
		wl_list_remove(&con->output_handler_destroy.link);
	}
}

void container_reap_empty(struct sway_container *con) {
	if (con->view) {
		return;
	}
	struct sway_workspace *ws = con->pending.workspace;
	while (con) {
		if (con->pending.children->length) {
			return;
		}
		struct sway_container *parent = con->pending.parent;
		container_begin_destroy(con);
		con = parent;
	}
	if (ws) {
		workspace_consider_destroy(ws);
	}
}

struct sway_container *container_find_child(struct sway_container *container,
		bool (*test)(struct sway_container *con, void *data), void *data) {
	if (!container->pending.children) {
		return NULL;
	}
	for (int i = 0; i < container->pending.children->length; ++i) {
		struct sway_container *child = container->pending.children->items[i];
		if (test(child, data)) {
			return child;
		}
		struct sway_container *res = container_find_child(child, test, data);
		if (res) {
			return res;
		}
	}
	return NULL;
}

void container_for_each_child(struct sway_container *container,
		void (*f)(struct sway_container *container, void *data),
		void *data) {
	if (container->pending.children)  {
		for (int i = 0; i < container->pending.children->length; ++i) {
			struct sway_container *child = container->pending.children->items[i];
			f(child, data);
			container_for_each_child(child, f, data);
		}
	}
}

struct sway_container *container_obstructing_fullscreen_container(struct sway_container *container)
{
	struct sway_workspace *workspace = container->pending.workspace;

	if (workspace && workspace->fullscreen && !container_is_fullscreen_or_child(container)) {
		if (container_is_transient_for(container, workspace->fullscreen)) {
			return NULL;
		}
		return workspace->fullscreen;
	}

	struct sway_container *fullscreen_global = root->fullscreen_global;
	if (fullscreen_global && container != fullscreen_global && !container_has_ancestor(container, fullscreen_global)) {
		if (container_is_transient_for(container, fullscreen_global)) {
			return NULL;
		}
		return fullscreen_global;
	}

	return NULL;
}

bool container_has_ancestor(struct sway_container *descendant,
		struct sway_container *ancestor) {
	while (descendant) {
		descendant = descendant->pending.parent;
		if (descendant == ancestor) {
			return true;
		}
	}
	return false;
}

static char *escape_pango_markup(const char *buffer) {
	size_t length = escape_markup_text(buffer, NULL);
	char *escaped_title = calloc(length + 1, sizeof(char));
	escape_markup_text(buffer, escaped_title);
	return escaped_title;
}

static size_t append_prop(char *buffer, const char *value) {
	if (!value) {
		return 0;
	}
	// If using pango_markup in font, we need to escape all markup chars
	// from values to make sure tags are not inserted by clients
	if (config->pango_markup) {
		char *escaped_value = escape_pango_markup(value);
		lenient_strcat(buffer, escaped_value);
		size_t len = strlen(escaped_value);
		free(escaped_value);
		return len;
	} else {
		lenient_strcat(buffer, value);
		return strlen(value);
	}
}

/**
 * Calculate and return the length of the formatted title.
 * If buffer is not NULL, also populate the buffer with the formatted title.
 */
size_t parse_title_format(struct sway_container *container, char *buffer) {
	if (!container->title_format || strcmp(container->title_format, "%title") == 0) {
		if (container->view) {
			return append_prop(buffer, view_get_title(container->view));
		} else {
			return container_build_representation(container->pending.layout, container->pending.children, buffer);
		}
	}

	size_t len = 0;
	char *format = container->title_format;
	char *next = strchr(format, '%');
	while (next) {
		// Copy everything up to the %
		lenient_strncat(buffer, format, next - format);
		len += next - format;
		format = next;

		if (has_prefix(next, "%title")) {
			if (container->view) {
				len += append_prop(buffer, view_get_title(container->view));
			} else {
				len += container_build_representation(container->pending.layout, container->pending.children, buffer);
			}
			format += strlen("%title");
		} else if (container->view) {
			if (has_prefix(next, "%app_id")) {
				len += append_prop(buffer, view_get_app_id(container->view));
				format += strlen("%app_id");
			} else if (has_prefix(next, "%class")) {
				len += append_prop(buffer, view_get_class(container->view));
				format += strlen("%class");
			} else if (has_prefix(next, "%instance")) {
				len += append_prop(buffer, view_get_instance(container->view));
				format += strlen("%instance");
			} else if (has_prefix(next, "%shell")) {
				len += append_prop(buffer, view_get_shell(container->view));
				format += strlen("%shell");
			} else if (has_prefix(next, "%sandbox_engine")) {
				len += append_prop(buffer, view_get_sandbox_engine(container->view));
				format += strlen("%sandbox_engine");
			} else if (has_prefix(next, "%sandbox_app_id")) {
				len += append_prop(buffer, view_get_sandbox_app_id(container->view));
				format += strlen("%sandbox_app_id");
			} else if (has_prefix(next, "%sandbox_instance_id")) {
				len += append_prop(buffer, view_get_sandbox_instance_id(container->view));
				format += strlen("%sandbox_instance_id");
			} else {
				lenient_strcat(buffer, "%");
				++format;
				++len;
			}
		} else {
			lenient_strcat(buffer, "%");
			++format;
			++len;
		}
		next = strchr(format, '%');
	}
	lenient_strcat(buffer, format);
	len += strlen(format);

	return len;
}

/**
 * Calculate and return the length of the tree representation.
 * An example tree representation is: V[Terminal, Firefox]
 * If buffer is not NULL, also populate the buffer with the representation.
 */
size_t container_build_representation(enum sway_container_layout layout,
		list_t *children, char *buffer) {
	size_t len = 2;
	switch (layout) {
	case L_VERT:
		lenient_strcat(buffer, "V[");
		break;
	case L_HORIZ:
		lenient_strcat(buffer, "H[");
		break;
	case L_NONE:
		lenient_strcat(buffer, "D[");
		break;
	}
	for (int i = 0; i < children->length; ++i) {
		if (i != 0) {
			++len;
			lenient_strcat(buffer, " ");
		}
		struct sway_container *child = children->items[i];
		const char *identifier = NULL;
		if (child->view) {
			identifier = view_get_class(child->view);
			if (!identifier) {
				identifier = view_get_app_id(child->view);
			}
		} else {
			identifier = child->formatted_title;
		}
		if (identifier) {
			len += strlen(identifier);
			lenient_strcat(buffer, identifier);
		} else {
			len += strlen("(null)");
			lenient_strcat(buffer, "(null)");
		}
	}
	++len;
	lenient_strcat(buffer, "]");
	return len;
}

void container_update_representation(struct sway_container *con) {
	if (!con->view) {
		size_t len = parse_title_format(con, NULL);
		free(con->formatted_title);
		con->formatted_title = calloc(len + 1, sizeof(char));
		if (!sway_assert(con->formatted_title,
					"Unable to allocate title string")) {
			return;
		}
		parse_title_format(con, con->formatted_title);

		if (con->title_bar.title_text) {
			sway_text_node_set_text(con->title_bar.title_text, con->formatted_title);
			container_arrange_title_bar(con);
		} else {
			container_update_title_bar(con);
		}
	}
	if (con->pending.parent) {
		container_update_representation(con->pending.parent);
	} else if (con->pending.workspace) {
		workspace_update_representation(con->pending.workspace);
	}
}

size_t container_titlebar_height(void) {
	return config->font_height + config->titlebar_v_padding * 2;
}

void floating_calculate_constraints(int *min_width, int *max_width,
		int *min_height, int *max_height) {
	if (config->floating_minimum_width == -1) { // no minimum
		*min_width = 0;
	} else if (config->floating_minimum_width == 0) { // automatic
		*min_width = 75;
	} else {
		*min_width = config->floating_minimum_width;
	}

	if (config->floating_minimum_height == -1) { // no minimum
		*min_height = 0;
	} else if (config->floating_minimum_height == 0) { // automatic
		*min_height = 50;
	} else {
		*min_height = config->floating_minimum_height;
	}

	struct wlr_box box;
	wlr_output_layout_get_box(root->output_layout, NULL, &box);

	if (config->floating_maximum_width == -1) { // no maximum
		*max_width = INT_MAX;
	} else if (config->floating_maximum_width == 0) { // automatic
		*max_width = box.width;
	} else {
		*max_width = config->floating_maximum_width;
	}

	if (config->floating_maximum_height == -1) { // no maximum
		*max_height = INT_MAX;
	} else if (config->floating_maximum_height == 0) { // automatic
		*max_height = box.height;
	} else {
		*max_height = config->floating_maximum_height;
	}

}

void floating_fix_coordinates(struct sway_container *con, struct wlr_box *old, struct wlr_box *new) {
	if (!old->width || !old->height) {
		// Fall back to centering on the workspace.
		container_floating_move_to_center(con);
	} else {
		double rel_x = con->pending.x - old->x + (con->pending.width / 2);
		double rel_y = con->pending.y - old->y + (con->pending.height / 2);

		con->pending.x = new->x + (rel_x * new->width) / old->width - (con->pending.width / 2);
		con->pending.y = new->y + (rel_y * new->height) / old->height - (con->pending.height / 2);

		sway_log(SWAY_DEBUG, "Transformed container %p to coords (%f, %f)", con, con->pending.x, con->pending.y);
	}
}

static void floating_natural_resize(struct sway_container *con) {
	int min_width, max_width, min_height, max_height;
	floating_calculate_constraints(&min_width, &max_width,
			&min_height, &max_height);
	if (!con->view) {
		con->pending.width = fmax(min_width, fmin(con->pending.width, max_width));
		con->pending.height = fmax(min_height, fmin(con->pending.height, max_height));
	} else {
		struct sway_view *view = con->view;
		con->pending.content_width =
			fmax(min_width, fmin(view->natural_width, max_width));
		con->pending.content_height =
			fmax(min_height, fmin(view->natural_height, max_height));
		container_set_geometry_from_content(con);
	}
}

void container_floating_resize_and_center(struct sway_container *con) {
	struct sway_workspace *ws = con->pending.workspace;
	if (!ws) {
		// On scratchpad, just resize
		floating_natural_resize(con);
		return;
	}

	struct wlr_box ob;
	wlr_output_layout_get_box(root->output_layout, ws->output->wlr_output, &ob);
	if (wlr_box_empty(&ob)) {
		// On NOOP output. Will be called again when moved to an output
		con->pending.x = 0;
		con->pending.y = 0;
		con->pending.width = 0;
		con->pending.height = 0;
		return;
	}

	floating_natural_resize(con);
	if (!con->view) {
		if (con->pending.width > ws->width || con->pending.height > ws->height) {
			con->pending.x = ob.x + (ob.width - con->pending.width) / 2;
			con->pending.y = ob.y + (ob.height - con->pending.height) / 2;
		} else {
			con->pending.x = ws->x + (ws->width - con->pending.width) / 2;
			con->pending.y = ws->y + (ws->height - con->pending.height) / 2;
		}
	} else {
		if (con->pending.content_width > ws->width
				|| con->pending.content_height > ws->height) {
			con->pending.content_x = ob.x + (ob.width - con->pending.content_width) / 2;
			con->pending.content_y = ob.y + (ob.height - con->pending.content_height) / 2;
		} else {
			con->pending.content_x = ws->x + (ws->width - con->pending.content_width) / 2;
			con->pending.content_y = ws->y + (ws->height - con->pending.content_height) / 2;
		}

		// If the view's border is B_NONE then these properties are ignored.
		con->pending.border_top = con->pending.border_bottom = true;
		con->pending.border_left = con->pending.border_right = true;

		container_set_geometry_from_content(con);
	}
}

void container_floating_set_default_size(struct sway_container *con) {
	if (!sway_assert(con->pending.workspace, "Expected a container on a workspace")) {
		return;
	}

	int min_width, max_width, min_height, max_height;
	floating_calculate_constraints(&min_width, &max_width,
			&min_height, &max_height);
	struct wlr_box box;
	workspace_get_box(con->pending.workspace, &box);

	double width = fmax(min_width, fmin(box.width * 0.5, max_width));
	double height = fmax(min_height, fmin(box.height * 0.75, max_height));
	if (!con->view) {
		con->pending.width = width;
		con->pending.height = height;
	} else {
		con->pending.content_width = width;
		con->pending.content_height = height;
		container_set_geometry_from_content(con);
	}
}


/**
 * Indicate to clients in this container that they are participating in (or
 * have just finished) an interactive resize
 */
void container_set_resizing(struct sway_container *con, bool resizing) {
	if (!con) {
		return;
	}

	if (con->view) {
		if (con->view->impl->set_resizing) {
			con->view->impl->set_resizing(con->view, resizing);
		}
	} else {
		for (int i = 0; i < con->pending.children->length; ++i ) {
			struct sway_container *child = con->pending.children->items[i];
			container_set_resizing(child, resizing);
		}
	}
}

void container_set_floating(struct sway_container *container, bool enable) {
	if (container_is_floating(container) == enable) {
		return;
	}

	struct sway_seat *seat = input_manager_current_seat();
	struct sway_workspace *workspace = container->pending.workspace;
	struct sway_container *focus = seat_get_focused_container(seat);
	bool set_focus = focus == container;

	if (enable) {
		struct sway_container *old_parent = container->pending.parent;
		if (layout_selection_enabled(old_parent)) {
			layout_selection_set(container, true);
		}
		container_detach(container);
		workspace_add_floating(workspace, container);
		if (container->view) {
			view_set_tiled(container->view, false);
			if (container->view->using_csd) {
				container->saved_border = container->pending.border;
				container->pending.border = B_CSD;
				if (container->view->xdg_decoration) {
					struct sway_xdg_decoration *deco = container->view->xdg_decoration;
					wlr_xdg_toplevel_decoration_v1_set_mode(deco->wlr_xdg_decoration,
							WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
				}
			}
		}
		container_floating_set_default_size(container);
		container_floating_resize_and_center(container);
		if (old_parent) {
			if (set_focus) {
				seat_set_raw_focus(seat, &old_parent->node);
				seat_set_raw_focus(seat, &container->node);
			}
			container_reap_empty(old_parent);
		}
	} else {
		// Returning to tiled
		if (container->scratchpad) {
			root_scratchpad_remove_container(container);
		}
		container_detach(container);
		struct sway_container *reference =
			seat_get_focus_inactive_tiling(seat, workspace);
		layout_add_view(workspace, reference, container);
		if (container->view) {
			view_set_tiled(container->view, true);
			if (container->view->using_csd) {
				container->pending.border = container->saved_border;
				if (container->view->xdg_decoration) {
					struct sway_xdg_decoration *deco = container->view->xdg_decoration;
					wlr_xdg_toplevel_decoration_v1_set_mode(deco->wlr_xdg_decoration,
							WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
				}
			}
		}
	}

	layout_maximize_if_single(workspace);

	container_end_mouse_operation(container);

	ipc_event_window(container, "floating");

	// Lua callbacks
	for (int i = 0; i < config->lua.cbs_view_float->length; ++i) {
		struct sway_lua_closure *closure = config->lua.cbs_view_float->items[i];
		lua_rawgeti(config->lua.state, LUA_REGISTRYINDEX, closure->cb_function);
		lua_pushlightuserdata(config->lua.state, container->view);
		lua_rawgeti(config->lua.state, LUA_REGISTRYINDEX, closure->cb_data);
		lua_call(config->lua.state, 2, 0);
	}
}

void container_set_geometry_from_content(struct sway_container *con) {
	if (!sway_assert(con->view, "Expected a view")) {
		return;
	}
	if (!sway_assert(container_is_floating(con), "Expected a floating view")) {
		return;
	}
	size_t border_width = 0;
	size_t top = 0;

	if (con->pending.border != B_CSD && !con->pending.fullscreen_mode) {
		border_width = con->pending.border_thickness * (con->pending.border != B_NONE);
		top = con->pending.border == B_NORMAL ?
			container_titlebar_height() : border_width;
	}
	float scale = view_is_content_scaled(con->view) ? view_get_content_scale(con->view) : 1.0f;
	con->pending.x = con->pending.content_x - border_width;
	con->pending.y = con->pending.content_y - top;
	con->pending.width = scale * con->pending.content_width + border_width * 2;
	con->pending.height = top + scale * con->pending.content_height + border_width;
	node_set_dirty(&con->node);
}

bool container_is_floating(struct sway_container *container) {
	if (!container->pending.parent && container->pending.workspace &&
			list_find(container->pending.workspace->floating, container) != -1) {
		return true;
	}
	if (container->scratchpad) {
		return true;
	}
	return false;
}

void container_get_box(struct sway_container *container, struct wlr_box *box) {
	box->x = container->pending.x;
	box->y = container->pending.y;
	box->width = container->pending.width;
	box->height = container->pending.height;
}

/**
 * Translate the container's position as well as all children.
 */
void container_floating_translate(struct sway_container *con,
		double x_amount, double y_amount) {
	con->pending.x += x_amount;
	con->pending.y += y_amount;
	con->pending.content_x += x_amount;
	con->pending.content_y += y_amount;

	if (con->pending.children) {
		for (int i = 0; i < con->pending.children->length; ++i) {
			struct sway_container *child = con->pending.children->items[i];
			container_floating_translate(child, x_amount, y_amount);
		}
	}

	node_set_dirty(&con->node);
}

/**
 * Choose an output for the floating container's new position.
 *
 * If the center of the container intersects an output then we'll choose that
 * one, otherwise we'll choose whichever output is closest to the container's
 * center.
 */
struct sway_output *container_floating_find_output(struct sway_container *con) {
	double center_x = con->pending.x + con->pending.width / 2;
	double center_y = con->pending.y + con->pending.height / 2;
	struct sway_output *closest_output = NULL;
	double closest_distance = DBL_MAX;
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		struct wlr_box output_box;
		double closest_x, closest_y;
		output_get_box(output, &output_box);
		wlr_box_closest_point(&output_box, center_x, center_y,
				&closest_x, &closest_y);
		if (center_x == closest_x && center_y == closest_y) {
			// The center of the floating container is on this output
			return output;
		}
		double x_dist = closest_x - center_x;
		double y_dist = closest_y - center_y;
		double distance = x_dist * x_dist + y_dist * y_dist;
		if (distance < closest_distance) {
			closest_output = output;
			closest_distance = distance;
		}
	}
	return closest_output;
}

void container_floating_move_to(struct sway_container *con,
		double lx, double ly) {
	if (!sway_assert(container_is_floating(con),
			"Expected a floating container")) {
		return;
	}
	container_floating_translate(con, lx - con->pending.x, ly - con->pending.y);
	if (container_is_scratchpad_hidden(con)) {
		return;
	}
	struct sway_workspace *old_workspace = con->pending.workspace;
	struct sway_output *new_output = container_floating_find_output(con);
	if (!sway_assert(new_output, "Unable to find any output")) {
		return;
	}
	struct sway_workspace *new_workspace =
		output_get_active_workspace(new_output);
	if (new_workspace && old_workspace != new_workspace) {
		container_detach(con);
		workspace_add_floating(new_workspace, con);
		arrange_workspace(old_workspace);
		arrange_workspace(new_workspace);
		// If the moved container was a visible scratchpad container, then
		// update its transform.
		if (con->scratchpad) {
			struct wlr_box output_box;
			output_get_box(new_output, &output_box);
			con->transform = output_box;
		}
		workspace_detect_urgent(old_workspace);
		workspace_detect_urgent(new_workspace);
	}
}

void container_floating_move_to_center(struct sway_container *con) {
	if (!sway_assert(container_is_floating(con),
			"Expected a floating container")) {
		return;
	}
	struct sway_workspace *ws = con->pending.workspace;
	double new_lx = ws->x + (ws->width - con->pending.width) / 2;
	double new_ly = ws->y + (ws->height - con->pending.height) / 2;
	container_floating_translate(con, new_lx - con->pending.x, new_ly - con->pending.y);
}

static bool find_urgent_iterator(struct sway_container *con, void *data) {
	return con->view && view_is_urgent(con->view);
}

bool container_has_urgent_child(struct sway_container *container) {
	return container_find_child(container, find_urgent_iterator, NULL);
}

void container_end_mouse_operation(struct sway_container *container) {
	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		seatop_unref(seat, container);
	}
}

static void set_fullscreen(struct sway_container *con, bool enable) {
	if (!con->view) {
		return;
	}
	if (con->view->impl->set_fullscreen) {
		con->view->impl->set_fullscreen(con->view, enable);
		if (con->view->foreign_toplevel) {
			wlr_foreign_toplevel_handle_v1_set_fullscreen(
				con->view->foreign_toplevel, enable);
		}
	}
}

static void container_fullscreen_workspace(struct sway_container *con) {
	if (!sway_assert(con->pending.fullscreen_mode == FULLSCREEN_NONE,
				"Expected a non-fullscreen container")) {
		return;
	}
	con->pending.fullscreen_mode = FULLSCREEN_WORKSPACE;
	con->fullscreen = true;

	con->saved_x = con->pending.x;
	con->saved_y = con->pending.y;
	con->saved_width = con->pending.width;
	con->saved_height = con->pending.height;

	if (con->pending.workspace) {
		con->pending.workspace->fullscreen = con;
		struct sway_seat *seat;
		struct sway_workspace *focus_ws;
		wl_list_for_each(seat, &server.input->seats, link) {
			focus_ws = seat_get_focused_workspace(seat);
			if (focus_ws == con->pending.workspace) {
				seat_set_focus_container(seat, con);
			} else {
				struct sway_node *focus =
					seat_get_focus_inactive(seat, &root->node);
				seat_set_raw_focus(seat, &con->node);
				seat_set_raw_focus(seat, focus);
			}
		}
	}

	container_end_mouse_operation(con);
	ipc_event_window(con, "fullscreen_mode");
}

static void container_fullscreen_global(struct sway_container *con) {
	if (!sway_assert(con->pending.fullscreen_mode == FULLSCREEN_NONE,
				"Expected a non-fullscreen container")) {
		return;
	}

	root->fullscreen_global = con;
	con->saved_x = con->pending.x;
	con->saved_y = con->pending.y;
	con->saved_width = con->pending.width;
	con->saved_height = con->pending.height;

	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		struct sway_container *focus = seat_get_focused_container(seat);
		if (focus && focus != con) {
			seat_set_focus_container(seat, con);
		}
	}

	con->pending.fullscreen_mode = FULLSCREEN_GLOBAL;
	container_end_mouse_operation(con);
	ipc_event_window(con, "fullscreen_mode");
}

void container_fullscreen_disable(struct sway_container *con) {
	if (!sway_assert(con->pending.fullscreen_mode != FULLSCREEN_NONE,
				"Expected a fullscreen container")) {
		return;
	}

	if (container_is_floating(con)) {
		con->pending.x = con->saved_x;
		con->pending.y = con->saved_y;
		con->pending.width = con->saved_width;
		con->pending.height = con->saved_height;
	}

	if (con->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
		if (con->pending.workspace) {
			con->pending.workspace->fullscreen = NULL;
			if (container_is_floating(con)) {
				struct sway_output *output =
					container_floating_find_output(con);
				if (con->pending.workspace->output != output) {
					container_floating_move_to_center(con);
				}
			}
		}
	} else {
		root->fullscreen_global = NULL;
	}

	// If the container was mapped as fullscreen and set as floating by
	// criteria, it needs to be reinitialized as floating to get the proper
	// size and location
	if (container_is_floating(con) && (con->pending.width == 0 || con->pending.height == 0)) {
		container_floating_resize_and_center(con);
	}

	con->pending.fullscreen_mode = FULLSCREEN_NONE;
	container_end_mouse_operation(con);
	ipc_event_window(con, "fullscreen_mode");

	if (con->scratchpad) {
		struct sway_seat *seat;
		wl_list_for_each(seat, &server.input->seats, link) {
			struct sway_container *focus = seat_get_focused_container(seat);
			if (focus == con || container_has_ancestor(focus, con)) {
				seat_set_focus(seat,
						seat_get_focus_inactive(seat, &root->node));
			}
		}
	}
}

void container_set_fullscreen(struct sway_container *con,
		enum sway_fullscreen_mode mode) {
	if (con->pending.fullscreen_mode == mode) {
		return;
	}

	struct sway_workspace *workspace = con->pending.workspace;
	switch (mode) {
	case FULLSCREEN_NONE:
		container_fullscreen_disable(con);
		con->fullscreen = false;
		break;
	case FULLSCREEN_WORKSPACE:
		if (workspace && layout_overview_mode(workspace) == OVERVIEW_DISABLED) {
			if (root->fullscreen_global) {
				container_fullscreen_disable(root->fullscreen_global);
			}
			if (con->pending.workspace && con->pending.workspace->fullscreen) {
				container_fullscreen_disable(con->pending.workspace->fullscreen);
			}
			container_fullscreen_workspace(con);
		}
		break;
	case FULLSCREEN_GLOBAL:
		if (workspace && layout_overview_mode(workspace) == OVERVIEW_DISABLED) {
			if (root->fullscreen_global) {
				container_fullscreen_disable(root->fullscreen_global);
			}
			if (con->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
				container_fullscreen_disable(con);
			}
			container_fullscreen_global(con);
		}
		break;
	}
}

void container_pass_fullscreen(struct sway_container *src, struct sway_container *dst) {
	if (src->pending.fullscreen_mode == dst->pending.fullscreen_mode) {
		return;
	}
	enum sway_fullscreen_mode mode = src->pending.fullscreen_mode;

	switch (mode) {
	case FULLSCREEN_NONE:
		container_fullscreen_disable(dst);
		break;
	case FULLSCREEN_WORKSPACE:
		if (root->fullscreen_global) {
			container_fullscreen_disable(root->fullscreen_global);
		}
		if (src->pending.workspace && src->pending.workspace->fullscreen) {
			container_fullscreen_disable(src->pending.workspace->fullscreen);
		}
		arrange_root();
		container_fullscreen_workspace(dst);
		arrange_root();
		break;
	case FULLSCREEN_GLOBAL:
		if (root->fullscreen_global) {
			container_fullscreen_disable(root->fullscreen_global);
		}
		if (src->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
			container_fullscreen_disable(src);
		}
		arrange_root();
		container_fullscreen_global(dst);
		arrange_root();
		break;
	}
}

void container_set_fullscreen_container(struct sway_container *con,
		enum sway_fullscreen_state mode) {
	con->pending.fullscreen_container = mode;
}

void container_set_fullscreen_application(struct sway_container *con,
		enum sway_fullscreen_state mode) {
	if (con->pending.fullscreen_application == mode) {
		return;
	}
	switch (mode) {
	case FULLSCREEN_DISABLED:
		con->pending.fullscreen_application = mode;
		set_fullscreen(con, false);
		break;
	case FULLSCREEN_ENABLED:
		con->pending.fullscreen_application = mode;
		set_fullscreen(con, true);
		break;
	}
}

void container_set_fullscreen_layout(struct sway_container *con,
		enum sway_fullscreen_state mode) {
	struct sway_workspace *workspace = con->pending.workspace;
	if (!workspace) {
		return;
	}
	if (layout_scale_enabled(workspace)) {
		return;
	}
	container_set_fullscreen_application(con, mode);
	con->pending.fullscreen_layout = mode;
	if (con->pending.parent) {
		node_set_dirty(&con->node);
		con = con->pending.parent;
	}
	con->pending.fullscreen_layout = mode;
	output_layer_shell_enable(workspace->output, LAYER_SHELL_OVERLAY);
	arrange_workspace(workspace);
	node_set_dirty(&workspace->node);
	node_set_dirty(&con->node);
}

void container_handle_fullscreen_request(struct sway_container *con, bool enable) {
	enum sway_fullscreen_request_mode mode = config->fullscreen_on_request;
	// Override the request mode if the container is already in some FS mode
	if (con->pending.fullscreen_mode != FULLSCREEN_NONE ||
		container_is_floating(con)) {
		mode = FULLSCREEN_REQUEST_DEFAULT;
	} else if (con->pending.fullscreen_layout == FULLSCREEN_ENABLED) {
		mode = FULLSCREEN_REQUEST_LAYOUT;
	}
	if (enable) {
		switch (mode) {
		case FULLSCREEN_REQUEST_DEFAULT:
		default:
			// Enable app FS
			container_set_fullscreen_application(con, FULLSCREEN_ENABLED);
			if (con->pending.fullscreen_mode == FULLSCREEN_NONE) {
				container_set_fullscreen(con, enable);
			}
			break;
		case FULLSCREEN_REQUEST_LAYOUT:
			container_set_fullscreen_layout(con, FULLSCREEN_ENABLED);
			break;
		}
	} else {
		switch (mode) {
		case FULLSCREEN_REQUEST_DEFAULT:
		default:
			// Disable app FS
			container_set_fullscreen_application(con, FULLSCREEN_DISABLED);
			if (con->pending.fullscreen_container != FULLSCREEN_ENABLED) {
				container_set_fullscreen(con, enable);
			}
			break;
		case FULLSCREEN_REQUEST_LAYOUT:
			container_set_fullscreen_layout(con, FULLSCREEN_DISABLED);
			break;
		}
	}
}

struct sway_container *container_toplevel_ancestor(
		struct sway_container *container) {
	while (container->pending.parent) {
		container = container->pending.parent;
	}

	return container;
}

bool container_is_floating_or_child(struct sway_container *container) {
	return container_is_floating(container_toplevel_ancestor(container));
}

bool container_is_fullscreen_or_child(struct sway_container *container) {
	do {
		if (container->pending.fullscreen_mode) {
			return true;
		}
		container = container->pending.parent;
	} while (container);

	return false;
}

enum sway_container_layout container_parent_layout(struct sway_container *con) {
	if (con->pending.parent) {
		return con->pending.parent->pending.layout;
	}
	if (con->pending.workspace) {
		return layout_modifiers_get_mode(con->pending.workspace);
	}
	return L_NONE;
}

list_t *container_get_siblings(struct sway_container *container) {
	if (container->pending.parent) {
		return container->pending.parent->pending.children;
	}
	if (!container->pending.workspace) {
		return NULL;
	}
	if (list_find(container->pending.workspace->tiling, container) != -1) {
		return container->pending.workspace->tiling;
	}
	return container->pending.workspace->floating;
}

int container_sibling_index(struct sway_container *child) {
	return list_find(container_get_siblings(child), child);
}

void container_handle_fullscreen_reparent(struct sway_container *con) {
	if (con->pending.fullscreen_mode != FULLSCREEN_WORKSPACE || !con->pending.workspace ||
			con->pending.workspace->fullscreen == con) {
		return;
	}
	if (con->pending.workspace->fullscreen) {
		container_fullscreen_disable(con->pending.workspace->fullscreen);
	}
	con->pending.workspace->fullscreen = con;

	arrange_workspace(con->pending.workspace);
}

static void set_workspace(struct sway_container *container, void *data) {
	container->pending.workspace = container->pending.parent->pending.workspace;
}

void container_insert_update_parent_fullscreen_layout(struct sway_container *parent,
		struct sway_container *child) {
	if (child->pending.fullscreen_layout == FULLSCREEN_ENABLED) {
		parent->pending.fullscreen_layout = FULLSCREEN_ENABLED;
	}
}

void container_detach_update_parent_fullscreen_layout(struct sway_container *parent,
		struct sway_container *child) {
	if (child->pending.fullscreen_layout == FULLSCREEN_ENABLED) {
		parent->pending.fullscreen_layout = FULLSCREEN_DISABLED;
		list_t *siblings = parent->pending.children;
		for (int i = 0; i < siblings->length; ++i) {
			struct sway_container *sibling = siblings->items[i];
			if (sibling->pending.fullscreen_layout == FULLSCREEN_ENABLED) {
				parent->pending.fullscreen_layout = FULLSCREEN_ENABLED;
				return;
			}
		}
	}
}

void container_insert_child(struct sway_container *parent,
		struct sway_container *child, int i) {
	if (child->pending.workspace) {
		container_detach(child);
	}
	list_insert(parent->pending.children, i, child);
	container_insert_update_parent_fullscreen_layout(parent, child);
	child->pending.parent = parent;
	child->pending.workspace = parent->pending.workspace;
	container_for_each_child(child, set_workspace, NULL);
	container_handle_fullscreen_reparent(child);
	container_update_representation(parent);
}

void container_add_sibling(struct sway_container *fixed,
		struct sway_container *active, bool after) {
	if (active->pending.workspace) {
		container_detach(active);
	}
	list_t *siblings = container_get_siblings(fixed);
	int index = list_find(siblings, fixed);
	list_insert(siblings, index + after, active);
	active->pending.parent = fixed->pending.parent;
	active->pending.workspace = fixed->pending.workspace;
	if (active->pending.parent) {
		container_insert_update_parent_fullscreen_layout(active->pending.parent,
			active);
	}
	container_for_each_child(active, set_workspace, NULL);
	container_handle_fullscreen_reparent(active);
	container_update_representation(active);
}

void container_add_child(struct sway_container *parent,
		struct sway_container *child) {
	if (child->pending.workspace) {
		container_detach(child);
	}
	list_add(parent->pending.children, child);
	container_insert_update_parent_fullscreen_layout(parent, child);
	child->pending.parent = parent;
	child->pending.workspace = parent->pending.workspace;
	container_for_each_child(child, set_workspace, NULL);
	container_handle_fullscreen_reparent(child);
	container_update_representation(parent);
	node_set_dirty(&child->node);
	node_set_dirty(&parent->node);
}

void container_detach(struct sway_container *child) {
	if (child->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
		child->pending.workspace->fullscreen = NULL;
	}
	if (child->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		root->fullscreen_global = NULL;
	}

	struct sway_container *old_parent = child->pending.parent;
	struct sway_workspace *old_workspace = child->pending.workspace;
	list_t *siblings = container_get_siblings(child);
	if (siblings) {
		int index = list_find(siblings, child);
		if (index != -1) {
			list_del(siblings, index);
		}
	}
	child->pending.parent = NULL;
	child->pending.workspace = NULL;
	container_for_each_child(child, set_workspace, NULL);

	if (old_parent) {
		container_detach_update_parent_fullscreen_layout(old_parent, child);
		container_update_representation(old_parent);
		node_set_dirty(&old_parent->node);
	} else if (old_workspace) {
		workspace_update_representation(old_workspace);
		node_set_dirty(&old_workspace->node);
	}
	node_set_dirty(&child->node);
}

void container_replace(struct sway_container *container,
		struct sway_container *replacement) {
	enum sway_fullscreen_mode fullscreen = container->pending.fullscreen_mode;
	bool scratchpad = container->scratchpad;
	struct sway_workspace *ws = NULL;
	if (fullscreen != FULLSCREEN_NONE) {
		container_fullscreen_disable(container);
	}
	if (scratchpad) {
		ws = container->pending.workspace;
		root_scratchpad_show(container);
		root_scratchpad_remove_container(container);
	}
	if (container->pending.parent || container->pending.workspace) {
		float width_fraction = container->width_fraction;
		float height_fraction = container->height_fraction;
		container_add_sibling(container, replacement, 1);
		container_detach(container);
		replacement->width_fraction = width_fraction;
		replacement->height_fraction = height_fraction;
	}
	if (scratchpad) {
		root_scratchpad_add_container(replacement, ws);
	}
	switch (fullscreen) {
	case FULLSCREEN_WORKSPACE:
		container_fullscreen_workspace(replacement);
		break;
	case FULLSCREEN_GLOBAL:
		container_fullscreen_global(replacement);
		break;
	case FULLSCREEN_NONE:
		// noop
		break;
	}
}

struct sway_container *container_split(struct sway_container *child,
		enum sway_container_layout layout) {
#if 0
	// i3 doesn't split singleton H/V containers
	// https://github.com/i3/i3/blob/3cd1c45eba6de073bc4300eebb4e1cc1a0c4479a/src/tree.c#L354
	if (child->pending.parent || child->pending.workspace) {
		list_t *siblings = container_get_siblings(child);
		if (siblings->length == 1) {
			enum sway_container_layout current = container_parent_layout(child);
			if (container_is_floating(child)) {
				current = L_NONE;
			}
			if (current == L_HORIZ || current == L_VERT) {
				if (child->pending.parent) {
					child->pending.parent->pending.layout = layout;
					container_update_representation(child->pending.parent);
				} else {
					layout_modifiers_set_mode(&child->pending.workspace->layout, layout);
					workspace_update_representation(child->pending.workspace);
				}
				return child;
			}
		}
	}
#endif
	struct sway_seat *seat = input_manager_get_default_seat();
	bool set_focus = (seat_get_focus(seat) == &child->node);

	if (container_is_floating(child) && child->view) {
		view_set_tiled(child->view, true);
		if (child->view->using_csd) {
			child->pending.border = child->saved_border;
		}
	}

	struct sway_container *cont = container_create(NULL);
	cont->pending.width = child->pending.width;
	cont->pending.height = child->pending.height;
	cont->width_fraction = child->width_fraction;
	cont->height_fraction = child->height_fraction;
	cont->pending.x = child->pending.x;
	cont->pending.y = child->pending.y;
	cont->pending.layout = layout;

	container_replace(child, cont);
	container_add_child(cont, child);

	if (set_focus) {
		seat_set_raw_focus(seat, &cont->node);
		if (cont->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
			seat_set_focus(seat, &child->node);
		} else {
			seat_set_raw_focus(seat, &child->node);
		}
	}

	return cont;
}

bool container_is_transient_for(struct sway_container *child,
		struct sway_container *ancestor) {
	return config->popup_during_fullscreen == POPUP_SMART &&
		child->view && ancestor->view &&
		view_is_transient_for(child->view, ancestor->view);
}

static bool find_by_mark_iterator(struct sway_container *con, void *data) {
	char *mark = data;
	return container_has_mark(con, mark);
}

struct sway_container *container_find_mark(char *mark) {
	return root_find_container(find_by_mark_iterator, mark);
}

bool container_find_and_unmark(char *mark) {
	struct sway_container *con = root_find_container(
		find_by_mark_iterator, mark);
	if (!con) {
		return false;
	}

	for (int i = 0; i < con->marks->length; ++i) {
		char *con_mark = con->marks->items[i];
		if (strcmp(con_mark, mark) == 0) {
			free(con_mark);
			list_del(con->marks, i);
			container_update_marks(con);
			ipc_event_window(con, "mark");
			return true;
		}
	}
	return false;
}

void container_clear_marks(struct sway_container *con) {
	for (int i = 0; i < con->marks->length; ++i) {
		free(con->marks->items[i]);
	}
	con->marks->length = 0;
	ipc_event_window(con, "mark");
}

bool container_has_mark(struct sway_container *con, char *mark) {
	for (int i = 0; i < con->marks->length; ++i) {
		char *item = con->marks->items[i];
		if (strcmp(item, mark) == 0) {
			return true;
		}
	}
	return false;
}

void container_add_mark(struct sway_container *con, char *mark) {
	list_add(con->marks, strdup(mark));
	ipc_event_window(con, "mark");
}

void container_raise_floating(struct sway_container *con) {
	// Bring container to front by putting it at the end of the floating list.
	struct sway_container *floater = container_toplevel_ancestor(con);
	if (container_is_floating(floater) && floater->pending.workspace) {
		// it's okay to just raise the scene directly instead of waiting
		// for the transaction to go through. We won't be reconfiguring
		// surfaces
		sway_scene_node_raise_to_top(&floater->scene_tree->node);

		list_move_to_end(floater->pending.workspace->floating, floater);
		node_set_dirty(&floater->pending.workspace->node);
	}
}

bool container_is_scratchpad_hidden(struct sway_container *con) {
	return con->scratchpad && !con->pending.workspace;
}

bool container_is_scratchpad_hidden_or_child(struct sway_container *con) {
	con = container_toplevel_ancestor(con);
	return con->scratchpad && !con->pending.workspace;
}

bool container_is_sticky(struct sway_container *con) {
	return con->is_sticky && container_is_floating(con);
}

bool container_is_sticky_or_child(struct sway_container *con) {
	return container_is_sticky(container_toplevel_ancestor(con));
}

static void swap_places(struct sway_container *con1,
		struct sway_container *con2) {
	struct sway_container *temp = malloc(sizeof(struct sway_container));
	temp->pending.x = con1->pending.x;
	temp->pending.y = con1->pending.y;
	temp->pending.width = con1->pending.width;
	temp->pending.height = con1->pending.height;
	temp->width_fraction = con1->width_fraction;
	temp->height_fraction = con1->height_fraction;
	temp->pending.parent = con1->pending.parent;
	temp->pending.workspace = con1->pending.workspace;
	bool temp_floating = container_is_floating(con1);

	con1->pending.x = con2->pending.x;
	con1->pending.y = con2->pending.y;
	con1->pending.width = con2->pending.width;
	con1->pending.height = con2->pending.height;
	con1->width_fraction = con2->width_fraction;
	con1->height_fraction = con2->height_fraction;

	con2->pending.x = temp->pending.x;
	con2->pending.y = temp->pending.y;
	con2->pending.width = temp->pending.width;
	con2->pending.height = temp->pending.height;
	con2->width_fraction = temp->width_fraction;
	con2->height_fraction = temp->height_fraction;

	int temp_index = container_sibling_index(con1);
	if (con2->pending.parent) {
		container_insert_child(con2->pending.parent, con1,
				container_sibling_index(con2));
	} else if (container_is_floating(con2)) {
		workspace_add_floating(con2->pending.workspace, con1);
	} else {
		workspace_insert_tiling(con2->pending.workspace, con1,
				container_sibling_index(con2));
	}
	if (temp->pending.parent) {
		container_insert_child(temp->pending.parent, con2, temp_index);
	} else if (temp_floating) {
		workspace_add_floating(temp->pending.workspace, con2);
	} else {
		workspace_insert_tiling(temp->pending.workspace, con2, temp_index);
	}

	free(temp);
}

static void swap_focus(struct sway_container *con1,
		struct sway_container *con2, struct sway_seat *seat,
		struct sway_container *focus) {
	if (focus == con1 || focus == con2) {
		struct sway_workspace *ws1 = con1->pending.workspace;
		struct sway_workspace *ws2 = con2->pending.workspace;
		if (ws1 != ws2) {
			seat_set_focus_container(seat, focus == con1 ? con2 : con1);
		} else {
			seat_set_focus_container(seat, focus);
		}
	} else {
		seat_set_focus_container(seat, focus);
	}

	if (root->fullscreen_global) {
		seat_set_focus(seat,
				seat_get_focus_inactive(seat, &root->fullscreen_global->node));
	}
}

void container_swap(struct sway_container *con1, struct sway_container *con2) {
	if (!sway_assert(con1 && con2, "Cannot swap with nothing")) {
		return;
	}
	if (!sway_assert(con1->view && con2->view, "Cannot swap parent containers")) {
		return;
	}
	if (!sway_assert(!container_has_ancestor(con1, con2)
				&& !container_has_ancestor(con2, con1),
				"Cannot swap ancestor and descendant")) {
		return;
	}

	sway_log(SWAY_DEBUG, "Swapping containers %zu and %zu",
			con1->node.id, con2->node.id);

	bool scratch1 = con1->scratchpad;
	bool hidden1 = container_is_scratchpad_hidden(con1);
	bool scratch2 = con2->scratchpad;
	bool hidden2 = container_is_scratchpad_hidden(con2);
	if (scratch1) {
		if (hidden1) {
			root_scratchpad_show(con1);
		}
		root_scratchpad_remove_container(con1);
	}
	if (scratch2) {
		if (hidden2) {
			root_scratchpad_show(con2);
		}
		root_scratchpad_remove_container(con2);
	}

	enum sway_fullscreen_mode fs1 = con1->pending.fullscreen_mode;
	if (fs1) {
		container_fullscreen_disable(con1);
	}
	enum sway_fullscreen_mode fs2 = con2->pending.fullscreen_mode;
	if (fs2) {
		container_fullscreen_disable(con2);
	}

	struct sway_seat *seat = input_manager_current_seat();
	struct sway_container *focus = seat_get_focused_container(seat);
	struct sway_workspace *vis1 =
		output_get_active_workspace(con1->pending.workspace->output);
	struct sway_workspace *vis2 =
		output_get_active_workspace(con2->pending.workspace->output);
	if (!sway_assert(vis1 && vis2, "con1 or con2 are on an output without a"
				"workspace. This should not happen")) {
		return;
	}

	char *stored_prev_name = NULL;
	if (seat->prev_workspace_name) {
		stored_prev_name = strdup(seat->prev_workspace_name);
	}

	swap_places(con1, con2);

	if (!workspace_is_visible(vis1)) {
		seat_set_focus(seat, seat_get_focus_inactive(seat, &vis1->node));
	}
	if (!workspace_is_visible(vis2)) {
		seat_set_focus(seat, seat_get_focus_inactive(seat, &vis2->node));
	}

	swap_focus(con1, con2, seat, focus);

	if (stored_prev_name) {
		free(seat->prev_workspace_name);
		seat->prev_workspace_name = stored_prev_name;
	}

	if (scratch1) {
		root_scratchpad_add_container(con2, NULL);
		if (!hidden1) {
			root_scratchpad_show(con2);
		}
	}
	if (scratch2) {
		root_scratchpad_add_container(con1, NULL);
		if (!hidden2) {
			root_scratchpad_show(con1);
		}
	}

	if (fs1) {
		container_set_fullscreen(con2, fs1);
	}
	if (fs2) {
		container_set_fullscreen(con1, fs2);
	}
}
