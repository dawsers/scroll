#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/backend.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_object.h>
#include <wlr/types/wlr_color_management_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <types/wlr_output.h>
#include <wlr/util/region.h>
#include <wlr/util/transform.h>
#include "util.h"
#include "log.h"
#include "sway/tree/scene.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/scene_descriptor.h"
#include "sway/tree/debug.h"
#include "sway/output.h"
#include "output.h"
#include "sway/desktop/animation.h"

#include <wlr/config.h>

#if WLR_HAS_XWAYLAND
#include <wlr/xwayland/xwayland.h>
#endif

#define DMABUF_FEEDBACK_DEBOUNCE_FRAMES  30
#define HIGHLIGHT_DAMAGE_FADEOUT_TIME   250

struct sway_scene_tree *sway_scene_tree_from_node(struct sway_scene_node *node) {
	assert(node->type == SWAY_SCENE_NODE_TREE);
	struct sway_scene_tree *tree = wl_container_of(node, tree, node);
	return tree;
}

struct sway_scene_rect *sway_scene_rect_from_node(struct sway_scene_node *node) {
	assert(node->type == SWAY_SCENE_NODE_RECT);
	struct sway_scene_rect *rect = wl_container_of(node, rect, node);
	return rect;
}

struct sway_scene_buffer *sway_scene_buffer_from_node(
		struct sway_scene_node *node) {
	assert(node->type == SWAY_SCENE_NODE_BUFFER);
	struct sway_scene_buffer *buffer = wl_container_of(node, buffer, node);
	return buffer;
}

struct sway_scene_decoration *sway_scene_decoration_from_node(struct sway_scene_node *node) {
	assert(node->type == SWAY_SCENE_NODE_DECORATION);
	struct sway_scene_decoration *decoration = wl_container_of(node, decoration, node);
	return decoration;
}

struct sway_scene_shadow *sway_scene_shadow_from_node(struct sway_scene_node *node) {
	assert(node->type == SWAY_SCENE_NODE_SHADOW);
	struct sway_scene_shadow *shadow = wl_container_of(node, shadow, node);
	return shadow;
}

struct sway_scene *scene_node_get_root(struct sway_scene_node *node) {
	struct sway_scene_tree *tree;
	if (node->type == SWAY_SCENE_NODE_TREE) {
		tree = sway_scene_tree_from_node(node);
	} else {
		tree = node->parent;
	}

	while (tree->node.parent != NULL) {
		tree = tree->node.parent;
	}
	struct sway_scene *scene = wl_container_of(tree, scene, tree);
	return scene;
}

static void scene_node_init(struct sway_scene_node *node,
		enum sway_scene_node_type type, struct sway_scene_tree *parent) {
	*node = (struct sway_scene_node){
		.type = type,
		.parent = parent,
		.enabled = true,
		.info = {
			.scale = -1.0f,
			.wlr_output = NULL,
			.workspace = NULL,
			.output_box = NULL,
			.background = false
		},
	};

	wl_list_init(&node->link);

	wl_signal_init(&node->events.destroy);
	pixman_region32_init(&node->visible);

	if (parent != NULL) {
		wl_list_insert(parent->children.prev, &node->link);
	}

	wlr_addon_set_init(&node->addons);
}

struct highlight_region {
	pixman_region32_t region;
	struct timespec when;
	struct wl_list link;
};

static void scene_buffer_set_buffer(struct sway_scene_buffer *scene_buffer,
	struct wlr_buffer *buffer);
static void scene_buffer_set_texture(struct sway_scene_buffer *scene_buffer,
	struct wlr_texture *texture);

void sway_scene_node_destroy(struct sway_scene_node *node) {
	if (node == NULL) {
		return;
	}

	// We want to call the destroy listeners before we do anything else
	// in case the destroy signal would like to remove children before they
	// are recursively destroyed.
	wl_signal_emit_mutable(&node->events.destroy, NULL);
	wlr_addon_set_finish(&node->addons);

	sway_scene_node_set_enabled(node, false);

	struct sway_scene *scene = scene_node_get_root(node);
	if (node->type == SWAY_SCENE_NODE_BUFFER) {
		struct sway_scene_buffer *scene_buffer = sway_scene_buffer_from_node(node);

		uint64_t active = scene_buffer->active_outputs;
		if (active) {
			struct sway_scene_output *scene_output;
			wl_list_for_each(scene_output, &scene->outputs, link) {
				if (active & (1ull << scene_output->index)) {
					wl_signal_emit_mutable(&scene_buffer->events.output_leave,
						scene_output);
				}
			}
		}

		scene_buffer_set_buffer(scene_buffer, NULL);
		scene_buffer_set_texture(scene_buffer, NULL);
		pixman_region32_fini(&scene_buffer->opaque_region);
		wlr_drm_syncobj_timeline_unref(scene_buffer->wait_timeline);

		assert(wl_list_empty(&scene_buffer->events.output_leave.listener_list));
		assert(wl_list_empty(&scene_buffer->events.output_enter.listener_list));
		assert(wl_list_empty(&scene_buffer->events.outputs_update.listener_list));
		assert(wl_list_empty(&scene_buffer->events.output_sample.listener_list));
		assert(wl_list_empty(&scene_buffer->events.frame_done.listener_list));
	} else if (node->type == SWAY_SCENE_NODE_TREE) {
		struct sway_scene_tree *scene_tree = sway_scene_tree_from_node(node);

		if (scene_tree == &scene->tree) {
			assert(!node->parent);
			struct sway_scene_output *scene_output, *scene_output_tmp;
			wl_list_for_each_safe(scene_output, scene_output_tmp, &scene->outputs, link) {
				sway_scene_output_destroy(scene_output);
			}

			wl_list_remove(&scene->linux_dmabuf_v1_destroy.link);
			wl_list_remove(&scene->gamma_control_manager_v1_destroy.link);
			wl_list_remove(&scene->gamma_control_manager_v1_set_gamma.link);
		} else {
			assert(node->parent);
		}

		struct sway_scene_node *child, *child_tmp;
		wl_list_for_each_safe(child, child_tmp,
				&scene_tree->children, link) {
			sway_scene_node_destroy(child);
		}
	} else if (node->type == SWAY_SCENE_NODE_DECORATION) {
		struct sway_scene_decoration *decoration = sway_scene_decoration_from_node(node);
		wlr_object_destroy(decoration->wlr_object);
	} else if (node->type == SWAY_SCENE_NODE_SHADOW) {
		struct sway_scene_shadow *shadow = sway_scene_shadow_from_node(node);
		wlr_object_destroy(shadow->wlr_object);
	}

	assert(wl_list_empty(&node->events.destroy.listener_list));

	wl_list_remove(&node->link);
	pixman_region32_fini(&node->visible);
	free(node);
}

static void scene_tree_init(struct sway_scene_tree *tree,
		struct sway_scene_tree *parent) {
	*tree = (struct sway_scene_tree){0};
	scene_node_init(&tree->node, SWAY_SCENE_NODE_TREE, parent);
	wl_list_init(&tree->children);
}

struct sway_scene *sway_scene_create(void) {
	struct sway_scene *scene = calloc(1, sizeof(*scene));
	if (scene == NULL) {
		return NULL;
	}

	scene_tree_init(&scene->tree, NULL);

	wl_list_init(&scene->outputs);
	wl_list_init(&scene->linux_dmabuf_v1_destroy.link);
	wl_list_init(&scene->gamma_control_manager_v1_destroy.link);
	wl_list_init(&scene->gamma_control_manager_v1_set_gamma.link);

	const char *debug_damage_options[] = {
		"none",
		"rerender",
		"highlight",
		NULL
	};

	scene->debug_damage_option = env_parse_switch("SWAY_SCENE_DEBUG_DAMAGE", debug_damage_options);
	scene->direct_scanout = !env_parse_bool("SWAY_SCENE_DISABLE_DIRECT_SCANOUT");
	scene->calculate_visibility = !env_parse_bool("SWAY_SCENE_DISABLE_VISIBILITY");
	scene->highlight_transparent_region = env_parse_bool("SWAY_SCENE_HIGHLIGHT_TRANSPARENT_REGION");

	return scene;
}

struct sway_scene_tree *sway_scene_tree_create(struct sway_scene_tree *parent) {
	assert(parent);

	struct sway_scene_tree *tree = calloc(1, sizeof(*tree));
	if (tree == NULL) {
		return NULL;
	}

	scene_tree_init(tree, parent);
	return tree;
}

typedef bool (*scene_node_box_iterator_func_t)(struct sway_scene_node *node,
	double sx, double sy, void *data);

static bool _scene_nodes_in_box(struct sway_scene_node *node, struct wlr_box *box,
		scene_node_box_iterator_func_t iterator, void *user_data, double lx, double ly) {
	if (!node->enabled) {
		return false;
	}

	switch (node->type) {
	case SWAY_SCENE_NODE_TREE:;
		struct sway_scene_tree *scene_tree = sway_scene_tree_from_node(node);
		struct sway_scene_node *child;
		wl_list_for_each_reverse(child, &scene_tree->children, link) {
			if (_scene_nodes_in_box(child, box, iterator, user_data, lx + child->x, ly + child->y)) {
				return true;
			}
		}
		break;
	case SWAY_SCENE_NODE_RECT:
	case SWAY_SCENE_NODE_BUFFER:
	case SWAY_SCENE_NODE_DECORATION:
	case SWAY_SCENE_NODE_SHADOW:;
		double width, height;
		scene_node_get_size(node, &width, &height);
		struct wlr_box node_box = {
			.x = round(lx),
			.y = round(ly),
			.width = round(lx + width) - round(lx),
			.height = round(ly + height) - round(ly)
		};

		if (wlr_box_intersection(&node_box, &node_box, box) &&
				iterator(node, lx, ly, user_data)) {
			return true;
		}
		break;
	}

	return false;
}

static bool scene_nodes_in_box(struct sway_scene_node *node, struct wlr_box *box,
		scene_node_box_iterator_func_t iterator, void *user_data) {
	double x, y;
	sway_scene_node_coords(node, &x, &y);

	return _scene_nodes_in_box(node, box, iterator, user_data, x, y);
}

static void scene_node_opaque_region(struct sway_scene_node *node, int x, int y,
		pixman_region32_t *opaque) {
	double width, height;
	scene_node_get_size(node, &width, &height);

	if (node->type == SWAY_SCENE_NODE_RECT) {
		struct sway_scene_rect *scene_rect = sway_scene_rect_from_node(node);
		if (scene_rect->color[3] != 1) {
			return;
		}
	} else if (node->type == SWAY_SCENE_NODE_BUFFER) {
		struct sway_scene_buffer *scene_buffer = sway_scene_buffer_from_node(node);

		if (!scene_buffer->buffer) {
			return;
		}

		if (scene_buffer->opacity != 1) {
			return;
		}

		if (scene_buffer->radius_top > 0.0f || scene_buffer->radius_bottom > 0.0f) {
			return;
		}

		if (!scene_buffer->buffer_is_opaque) {
			pixman_region32_copy(opaque, &scene_buffer->opaque_region);
			pixman_region32_intersect_rect(opaque, opaque, 0, 0, round(width), round(height));
			pixman_region32_translate(opaque, x, y);
			return;
		}
	} else if (node->type == SWAY_SCENE_NODE_DECORATION || node->type == SWAY_SCENE_NODE_SHADOW) {
		return;
	}

	pixman_region32_fini(opaque);
	pixman_region32_init_rect(opaque, x, y, round(width), round(height));
}

struct scene_update_data {
	pixman_region32_t *visible;
	pixman_region32_t *update_region;
	struct wlr_box update_box;
	struct wl_list *outputs;
	bool calculate_visibility;

#if WLR_HAS_XWAYLAND
	struct wlr_xwayland_surface *restack_above;
#endif
};

static uint32_t region_area(pixman_region32_t *region) {
	uint32_t area = 0;

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(region, &nrects);
	for (int i = 0; i < nrects; ++i) {
		area += (rects[i].x2 - rects[i].x1) * (rects[i].y2 - rects[i].y1);
	}

	return area;
}

static void scale_region(pixman_region32_t *region, float scale, bool round_up) {
	wlr_region_scale(region, region, scale);

	if (round_up && floor(scale) != scale) {
		wlr_region_expand(region, region, 1);
	}
}

struct render_data {
	enum wl_output_transform transform;
	float scale;
	struct wlr_box logical;
	int trans_width, trans_height;

	struct sway_scene_output *output;

	struct wlr_render_pass *render_pass;
	pixman_region32_t damage;
};

static void logical_to_buffer_coords(pixman_region32_t *region, const struct render_data *data,
		bool round_up) {
	enum wl_output_transform transform = wlr_output_transform_invert(data->transform);
	scale_region(region, data->scale, round_up);
	wlr_region_transform(region, region, transform, data->trans_width, data->trans_height);
}

static void output_to_buffer_coords(pixman_region32_t *damage, struct wlr_output *output) {
	int width, height;
	wlr_output_transformed_resolution(output, &width, &height);

	wlr_region_transform(damage, damage,
		wlr_output_transform_invert(output->transform), width, height);
}

static void scale_box(struct wlr_fbox *box, float scale) {
	box->width *= scale;
	box->height *= scale;
	box->x = box->x * scale;
	box->y = box->y * scale;
}

static void transform_output_box(struct wlr_fbox *box, const struct render_data *data) {
	enum wl_output_transform transform = wlr_output_transform_invert(data->transform);
	scale_box(box, data->scale);
	wlr_fbox_transform(box, box, transform, data->trans_width, data->trans_height);
}

static void scene_output_damage(struct sway_scene_output *scene_output,
		const pixman_region32_t *damage) {
	struct wlr_output *output = scene_output->output;

	pixman_region32_t clipped;
	pixman_region32_init(&clipped);
	pixman_region32_intersect_rect(&clipped, damage, 0, 0, output->width, output->height);

	if (!pixman_region32_empty(&clipped)) {
		wlr_output_schedule_frame(scene_output->output);
		wlr_damage_ring_add(&scene_output->damage_ring, &clipped);

		pixman_region32_union(&scene_output->pending_commit_damage,
			&scene_output->pending_commit_damage, &clipped);
	}

	pixman_region32_fini(&clipped);
}

static void scene_output_damage_whole(struct sway_scene_output *scene_output) {
	struct wlr_output *output = scene_output->output;

	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, output->width, output->height);
	scene_output_damage(scene_output, &damage);
	pixman_region32_fini(&damage);
}

static void scene_damage_outputs(struct sway_scene *scene, pixman_region32_t *damage) {
	if (pixman_region32_empty(damage)) {
		return;
	}

	struct sway_scene_output *scene_output;
	wl_list_for_each(scene_output, &scene->outputs, link) {
		pixman_region32_t output_damage;
		pixman_region32_init(&output_damage);
		pixman_region32_copy(&output_damage, damage);
		pixman_region32_translate(&output_damage,
			-scene_output->x, -scene_output->y);
		scale_region(&output_damage, scene_output->output->scale, true);
		output_to_buffer_coords(&output_damage, scene_output->output);
		scene_output_damage(scene_output, &output_damage);
		pixman_region32_fini(&output_damage);
	}
}

static struct wlr_output *scene_node_get_output(struct sway_scene_node *node);

static void update_node_update_outputs(struct sway_scene_node *node,
		struct wl_list *outputs, struct sway_scene_output *ignore,
		struct sway_scene_output *force) {
	if (node->type != SWAY_SCENE_NODE_BUFFER) {
		return;
	}

	struct sway_scene_buffer *scene_buffer = sway_scene_buffer_from_node(node);

	// If tiled, we use the available information and skip all the checks
	struct wlr_output *wlr_output = scene_node_get_output(node);

	uint32_t largest_overlap = 0;
	struct sway_scene_output *old_primary_output = scene_buffer->primary_output;
	scene_buffer->primary_output = NULL;

	size_t count = 0;
	uint64_t active_outputs = scene_buffer->active_outputs;

	uint32_t visible_area = region_area(&node->visible);

	// let's update the outputs in two steps:
	//  - the primary outputs
	//  - the enter/leave signals
	// This ensures that the enter/leave signals can rely on the primary output
	// to have a reasonable value. Otherwise, they may get a value that's in
	// the middle of a calculation.
	struct sway_scene_output *scene_output;
	wl_list_for_each(scene_output, outputs, link) {
		if (scene_output == ignore) {
			continue;
		}

		if (!scene_output->output->enabled) {
			continue;
		}

		if (scene_output->output == wlr_output) {
			active_outputs = 1ull << scene_output->index;
			scene_buffer->primary_output = scene_output;
			count = 1;
			continue;
		}

		if (wlr_output) {
			continue;
		}

		struct wlr_box output_box = {
			.x = scene_output->x,
			.y = scene_output->y,
		};
		wlr_output_effective_resolution(scene_output->output,
			&output_box.width, &output_box.height);

		pixman_region32_t intersection;
		pixman_region32_init(&intersection);
		pixman_region32_intersect_rect(&intersection, &node->visible,
			output_box.x, output_box.y, output_box.width, output_box.height);
		uint32_t overlap = region_area(&intersection);
		pixman_region32_fini(&intersection);

		// If the overlap accounts for less than 10% of the visible node area,
		// ignore this output
		if (overlap >= 0.1 * visible_area) {
			if (overlap >= largest_overlap) {
				largest_overlap = overlap;
				scene_buffer->primary_output = scene_output;
			}

			active_outputs |= 1ull << scene_output->index;
		} else if (overlap <= 0) {
			active_outputs = active_outputs & ~(1ull << scene_output->index);
		}
		if (active_outputs & 1ull << scene_output->index) {
			count++;
		}
	}

	if (old_primary_output != scene_buffer->primary_output) {
		scene_buffer->prev_feedback_options =
			(struct wlr_linux_dmabuf_feedback_v1_init_options){0};
	}

	uint64_t old_active = scene_buffer->active_outputs;
	scene_buffer->active_outputs = active_outputs;

	wl_list_for_each(scene_output, outputs, link) {
		uint64_t mask = 1ull << scene_output->index;
		bool intersects = active_outputs & mask;
		bool intersects_before = old_active & mask;

		if (intersects && !intersects_before) {
			wl_signal_emit_mutable(&scene_buffer->events.output_enter, scene_output);
		} else if (!intersects && intersects_before) {
			wl_signal_emit_mutable(&scene_buffer->events.output_leave, scene_output);
		}
	}

	if (!scene_buffer->primary_output) {
		// When no output contains a big enough fraction of the buffer, it means
		// the primary output has been disconnected or we are exiting.
		return;
	}

	// Skip output update event if nothing was updated
	if (old_active == active_outputs &&
			(!force || ((1ull << force->index) & ~active_outputs)) &&
			old_primary_output == scene_buffer->primary_output) {
		return;
	}

	struct sway_scene_output *outputs_array[64];
	struct sway_scene_outputs_update_event event = {
		.active = outputs_array,
		.size = count,
	};

	size_t i = 0;
	wl_list_for_each(scene_output, outputs, link) {
		if (~active_outputs & (1ull << scene_output->index)) {
			continue;
		}

		assert(i < count);
		outputs_array[i++] = scene_output;
	}

	wl_signal_emit_mutable(&scene_buffer->events.outputs_update, &event);
}

#if WLR_HAS_XWAYLAND
static struct wlr_xwayland_surface *scene_node_try_get_managed_xwayland_surface(
		struct sway_scene_node *node) {
	if (node->type != SWAY_SCENE_NODE_BUFFER) {
		return NULL;
	}

	struct sway_scene_buffer *buffer_node = sway_scene_buffer_from_node(node);
	struct sway_scene_surface *surface_node = sway_scene_surface_try_from_buffer(buffer_node);
	if (!surface_node) {
		return NULL;
	}

	struct wlr_xwayland_surface *xwayland_surface =
		wlr_xwayland_surface_try_from_wlr_surface(surface_node->surface);
	if (!xwayland_surface || xwayland_surface->override_redirect) {
		return NULL;
	}

	return xwayland_surface;
}

static void restack_xwayland_surface(struct sway_scene_node *node,
		struct wlr_box *box, struct scene_update_data *data) {
	struct wlr_xwayland_surface *xwayland_surface =
		scene_node_try_get_managed_xwayland_surface(node);
	if (!xwayland_surface) {
		return;
	}

	// ensure this node is entirely inside the update region. If not, we can't
	// restack this node since we're not considering the whole thing.
	if (wlr_box_contains_box(&data->update_box, box)) {
		if (data->restack_above) {
			wlr_xwayland_surface_restack(xwayland_surface, data->restack_above, XCB_STACK_MODE_BELOW);
		} else {
			wlr_xwayland_surface_restack(xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
		}
	}

	data->restack_above = xwayland_surface;
}

static void restack_xwayland_surface_below(struct sway_scene_node *node) {
	if (node->type == SWAY_SCENE_NODE_TREE) {
		struct sway_scene_tree *scene_tree = sway_scene_tree_from_node(node);
		struct sway_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			restack_xwayland_surface_below(child);
		}
		return;
	}

	struct wlr_xwayland_surface *xwayland_surface =
		scene_node_try_get_managed_xwayland_surface(node);
	if (!xwayland_surface) {
		return;
	}

	wlr_xwayland_surface_restack(xwayland_surface, NULL, XCB_STACK_MODE_BELOW);
}
#endif

static struct wlr_output *scene_node_get_output(struct sway_scene_node *node) {
	struct sway_scene_tree *tree;
	if (node->type == SWAY_SCENE_NODE_TREE) {
		tree = sway_scene_tree_from_node(node);
	} else {
		tree = node->parent;
	}

	while (tree != NULL) {
		if (tree->node.info.wlr_output != NULL) {
			return tree->node.info.wlr_output;
		}
		tree = tree->node.parent;
	}
	return NULL;
}

static struct sway_workspace *scene_node_get_workspace(struct sway_scene_node *node) {
	struct sway_scene_tree *tree;
	if (node->type == SWAY_SCENE_NODE_TREE) {
		tree = sway_scene_tree_from_node(node);
	} else {
		tree = node->parent;
	}

	while (tree != NULL) {
		if (tree->node.info.workspace != NULL) {
			return tree->node.info.workspace;
		}
		tree = tree->node.parent;
	}
	return NULL;
}

static bool scene_node_get_background(struct sway_scene_node *node) {
	struct sway_scene_tree *tree;
	if (node->type == SWAY_SCENE_NODE_TREE) {
		tree = sway_scene_tree_from_node(node);
	} else {
		tree = node->parent;
	}

	while (tree != NULL) {
		if (tree->node.info.background) {
			return true;
		}
		tree = tree->node.parent;
	}
	return false;
}

static struct wlr_box *scene_node_get_workspace_box(struct sway_scene_node *node) {
	struct sway_scene_tree *tree;
	if (node->type == SWAY_SCENE_NODE_TREE) {
		tree = sway_scene_tree_from_node(node);
	} else {
		tree = node->parent;
	}

	while (tree != NULL) {
		if (tree->node.info.output_box != NULL) {
			return tree->node.info.output_box;
		}
		tree = tree->node.parent;
	}
	return NULL;
}

static void scene_node_apply_tiling_visibility(struct sway_scene_node *node,
		struct wl_list *outputs) {
	struct wlr_output *wlr_output = scene_node_get_output(node);
	struct sway_scene_output *scene_output;
	wl_list_for_each(scene_output, outputs, link) {
		if (wlr_output && scene_output->output != wlr_output) {
			// Substract output from visibility
			struct wlr_box output_box = {
				.x = scene_output->x,
				.y = scene_output->y,
			};
			wlr_output_effective_resolution(scene_output->output,
				&output_box.width, &output_box.height);

			pixman_region32_t output_region;
			pixman_region32_init_rect(&output_region, output_box.x, output_box.y, output_box.width, output_box.height);
			pixman_region32_subtract(&node->visible, &node->visible, &output_region);
			pixman_region32_fini(&output_region);
		}
	}
}

static void scene_node_apply_workspace_visibility(struct sway_scene_node *node) {
	struct wlr_box *box = scene_node_get_workspace_box(node);
	if (box) {
		pixman_region32_intersect_rect(&node->visible, &node->visible, box->x, box->y, box->width, box->height);
	}
}

static bool scene_node_update_iterator(struct sway_scene_node *node,
		double lx, double ly, void *_data) {
	struct scene_update_data *data = _data;

	double width, height;
	scene_node_get_size(node, &width, &height);
	struct wlr_box box = {
		.x = round(lx),
		.y = round(ly),
		.width = round(lx + width) - round(lx),
		.height = round(ly + height) - round(ly)
	};

	pixman_region32_subtract(&node->visible, &node->visible, data->update_region);
	pixman_region32_union(&node->visible, &node->visible, data->visible);
	pixman_region32_intersect_rect(&node->visible, &node->visible,
		box.x, box.y, box.width, box.height);

	scene_node_apply_tiling_visibility(node, data->outputs);
	scene_node_apply_workspace_visibility(node);

	if (data->calculate_visibility && !layout_overview_workspaces_enabled()) {
		pixman_region32_t opaque;
		pixman_region32_init(&opaque);
		scene_node_opaque_region(node, box.x, box.y, &opaque);
		pixman_region32_intersect(&opaque, &opaque, &node->visible);
		pixman_region32_subtract(data->visible, data->visible, &opaque);
		pixman_region32_fini(&opaque);
	}

	update_node_update_outputs(node, data->outputs, NULL, NULL);
#if WLR_HAS_XWAYLAND
	restack_xwayland_surface(node, &box, data);
#endif

	return false;
}

static void scene_node_visibility(struct sway_scene_node *node,
		pixman_region32_t *visible) {
	if (!node->enabled) {
		return;
	}

	if (node->type == SWAY_SCENE_NODE_TREE) {
		struct sway_scene_tree *scene_tree = sway_scene_tree_from_node(node);
		struct sway_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_node_visibility(child, visible);
		}
		return;
	}

	pixman_region32_union(visible, visible, &node->visible);
}

static void scene_node_bounds(struct sway_scene_node *node,
		double x, double y, pixman_region32_t *visible) {
	if (!node->enabled) {
		return;
	}

	if (node->type == SWAY_SCENE_NODE_TREE) {
		struct sway_scene_tree *scene_tree = sway_scene_tree_from_node(node);
		struct sway_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_node_bounds(child, x + child->x, y + child->y, visible);
		}
		return;
	}

	double width, height;
	scene_node_get_size(node, &width, &height);
	pixman_region32_union_rect(visible, visible, floor(x), floor(y),
		ceil(x + width) - floor(x), ceil(y + height) - floor(y));
}

static void scene_update_region(struct sway_scene *scene,
		pixman_region32_t *update_region) {
	pixman_region32_t visible;
	pixman_region32_init(&visible);
	pixman_region32_copy(&visible, update_region);

	struct pixman_box32 *region_box = pixman_region32_extents(update_region);
	struct scene_update_data data = {
		.visible = &visible,
		.update_region = update_region,
		.update_box = {
			.x = region_box->x1,
			.y = region_box->y1,
			.width = region_box->x2 - region_box->x1,
			.height = region_box->y2 - region_box->y1,
		},
		.outputs = &scene->outputs,
		.calculate_visibility = scene->calculate_visibility,
	};

	// update node visibility and output enter/leave events
	scene_nodes_in_box(&scene->tree.node, &data.update_box, scene_node_update_iterator, &data);

	pixman_region32_fini(&visible);
}

static void scene_node_update(struct sway_scene_node *node,
		pixman_region32_t *damage) {
	struct sway_scene *scene = scene_node_get_root(node);

	double x, y;
	if (!sway_scene_node_coords(node, &x, &y)) {
#if WLR_HAS_XWAYLAND
		restack_xwayland_surface_below(node);
#endif
		if (damage) {
			scene_update_region(scene, damage);
			scene_damage_outputs(scene, damage);
			pixman_region32_fini(damage);
		}

		return;
	}

	pixman_region32_t visible;
	if (!damage) {
		pixman_region32_init(&visible);
		scene_node_visibility(node, &visible);
		damage = &visible;
	}

	pixman_region32_t update_region;
	pixman_region32_init(&update_region);
	pixman_region32_copy(&update_region, damage);
	scene_node_bounds(node, x, y, &update_region);

	scene_update_region(scene, &update_region);
	pixman_region32_fini(&update_region);

	scene_node_visibility(node, damage);
	scene_damage_outputs(scene, damage);
	pixman_region32_fini(damage);
}

struct sway_scene_rect *sway_scene_rect_create(struct sway_scene_tree *parent,
		double width, double height, const float color[static 4]) {
	assert(parent);
	assert(width >= 0 && height >= 0);

	struct sway_scene_rect *scene_rect = calloc(1, sizeof(*scene_rect));
	if (scene_rect == NULL) {
		return NULL;
	}
	scene_node_init(&scene_rect->node, SWAY_SCENE_NODE_RECT, parent);

	scene_rect->width = width;
	scene_rect->height = height;
	memcpy(scene_rect->color, color, sizeof(scene_rect->color));

	scene_node_update(&scene_rect->node, NULL);

	return scene_rect;
}

void sway_scene_rect_set_size(struct sway_scene_rect *rect, double width, double height) {
	if (rect->width == width && rect->height == height) {
		return;
	}

	assert(width >= 0 && height >= 0);

	rect->width = width;
	rect->height = height;
	scene_node_update(&rect->node, NULL);
}

void sway_scene_rect_set_color(struct sway_scene_rect *rect, const float color[static 4]) {
	if (memcmp(rect->color, color, sizeof(rect->color)) == 0) {
		return;
	}

	memcpy(rect->color, color, sizeof(rect->color));
	scene_node_update(&rect->node, NULL);
}

static struct wlr_object *scene_decoration_get_object(
		struct sway_scene_decoration *scene_decoration, struct wlr_renderer *renderer) {
	if (scene_decoration->wlr_object != NULL) {
		return scene_decoration->wlr_object;
	}

	struct wlr_object *object = wlr_object_with_owner(renderer, WLR_OBJECT_DECORATION,
		scene_decoration);
	scene_decoration->wlr_object = object;
	return object;
}

struct sway_scene_decoration *sway_scene_decoration_create(struct sway_scene_tree *parent,
		struct sway_view *view, double width, double height) {
	assert(parent);
	assert(view);
	assert(width >= 0 && height >= 0);

	struct sway_scene_decoration *decoration = calloc(1, sizeof(*decoration));
	if (decoration == NULL) {
		return NULL;
	}
	scene_node_init(&decoration->node, SWAY_SCENE_NODE_DECORATION, parent);

	decoration->view = view;
	decoration->width = width;
	decoration->height = height;

	scene_node_update(&decoration->node, NULL);

	return decoration;
}

void sway_scene_decoration_set_size(struct sway_scene_decoration *decoration, double width, double height) {
	if (decoration->width == width && decoration->height == height) {
		return;
	}

	assert(width >= 0 && height >= 0);

	decoration->width = width;
	decoration->height = height;
	scene_node_update(&decoration->node, NULL);
}

void sway_scene_decoration_set_border_color(struct sway_scene_decoration *decoration, const float top[static 4],
		const float bottom[static 4], const float left[static 4], const float right[static 4]) {
	decoration->border = true;
	if (memcmp(decoration->border_top_color, top, sizeof(decoration->border_top_color)) == 0 &&
		memcmp(decoration->border_bottom_color, bottom, sizeof(decoration->border_bottom_color)) == 0 &&
		memcmp(decoration->border_left_color, left, sizeof(decoration->border_left_color)) == 0 &&
		memcmp(decoration->border_right_color, right, sizeof(decoration->border_right_color)) == 0) {
		return;
	}

	memcpy(decoration->border_top_color, top, sizeof(decoration->border_top_color));
	memcpy(decoration->border_bottom_color, bottom, sizeof(decoration->border_bottom_color));
	memcpy(decoration->border_left_color, left, sizeof(decoration->border_left_color));
	memcpy(decoration->border_right_color, right, sizeof(decoration->border_right_color));
	scene_node_update(&decoration->node, NULL);
}

void sway_scene_decoration_set_border_radius(struct sway_scene_decoration *decoration, double radius) {
	if (decoration->border_radius == radius) {
		return;
	}
	decoration->border_radius = radius;
	scene_node_update(&decoration->node, NULL);
}

void sway_scene_decoration_set_border_width(struct sway_scene_decoration *decoration, double width) {
	if (!decoration->border) {
		return;
	}
	if (decoration->border_width == width) {
		return;
	}
	decoration->border_width = width;
	scene_node_update(&decoration->node, NULL);
}

void sway_scene_decoration_set_title_bar(struct sway_scene_decoration *decoration, bool enable,
		double height, double border_radius) {
	if (decoration->title_bar == enable &&
		decoration->title_bar_height == height &&
		decoration->title_bar_border_radius == border_radius) {
		return;
	}
	decoration->title_bar = enable;
	decoration->title_bar_height = height;
	decoration->title_bar_border_radius = border_radius;
	scene_node_update(&decoration->node, NULL);
}

void sway_scene_decoration_set_title_bar_color(struct sway_scene_decoration *decoration,
	const float color[static 4]) {
	if (memcmp(decoration->title_bar_color, color, sizeof(decoration->title_bar_color)) == 0) {
		return;
	}
	memcpy(decoration->title_bar_color, color, sizeof(decoration->title_bar_color));
	scene_node_update(&decoration->node, NULL);
}

void sway_scene_decoration_set_dimming(struct sway_scene_decoration *decoration,
		bool dim, const float color[static 4]) {
	if (decoration->dim) {
		if (dim) {
			if (memcmp(decoration->dim_color, color, sizeof(decoration->dim_color)) == 0) {
				return;
			}
			memcpy(decoration->dim_color, color, sizeof(decoration->dim_color));
		} else {
			decoration->dim = dim;
		}
	} else {
		if (dim) {
			decoration->dim = dim;
			memcpy(decoration->dim_color, color, sizeof(decoration->dim_color));
		} else {
			return;
		}
	}
	scene_node_update(&decoration->node, NULL);
}

static struct wlr_object *scene_shadow_get_object(
		struct sway_scene_shadow *scene_shadow, struct wlr_renderer *renderer) {
	if (scene_shadow->wlr_object != NULL) {
		return scene_shadow->wlr_object;
	}

	struct wlr_object *object = wlr_object_with_owner(renderer, WLR_OBJECT_SHADOW,
		scene_shadow);
	scene_shadow->wlr_object = object;
	return object;
}

struct sway_scene_shadow *sway_scene_shadow_create(struct sway_scene_tree *parent,
		struct sway_scene_decoration *decoration) {
	assert(parent);
	assert(decoration);
	assert(decoration->view);

	struct sway_scene_shadow *shadow = calloc(1, sizeof(*shadow));
	if (shadow == NULL) {
		return NULL;
	}
	scene_node_init(&shadow->node, SWAY_SCENE_NODE_SHADOW, parent);

	shadow->decoration = decoration;

	scene_node_update(&shadow->node, NULL);

	return shadow;
}

void sway_scene_shadow_set_properties(struct sway_scene_shadow *shadow,
		double parent_width, double parent_height, bool enabled, bool dynamic,
		double size, double blur, const double offset[static 2], float color[4]) {
	if (shadow->enabled) {
		if (enabled) {
			if (shadow->width == parent_width && shadow->height == parent_height &&
				shadow->dynamic == dynamic && shadow->size == size &&
				shadow->blur == blur &&
				memcmp(shadow->offset, offset, sizeof(shadow->offset)) == 0 &&
				memcmp(shadow->color, color, sizeof(shadow->color)) == 0) {
				return;
			}
		}
	} else {
		if (!enabled) {
			return;
		}
	}
	shadow->enabled = enabled;
	shadow->width = parent_width;
	shadow->height = parent_height;
	shadow->dynamic = dynamic;
	shadow->size = size;
	shadow->blur = blur;
	memcpy(shadow->offset, offset, sizeof(shadow->offset));
	memcpy(shadow->color, color, sizeof(shadow->color));
	scene_node_update(&shadow->node, NULL);
}

static void scene_buffer_handle_buffer_release(struct wl_listener *listener,
		void *data) {
	struct sway_scene_buffer *scene_buffer =
		wl_container_of(listener, scene_buffer, buffer_release);

	scene_buffer->buffer = NULL;
	wl_list_remove(&scene_buffer->buffer_release.link);
	wl_list_init(&scene_buffer->buffer_release.link);
}

static void scene_buffer_set_buffer(struct sway_scene_buffer *scene_buffer,
		struct wlr_buffer *buffer) {
	wl_list_remove(&scene_buffer->buffer_release.link);
	wl_list_init(&scene_buffer->buffer_release.link);
	if (scene_buffer->own_buffer) {
		wlr_buffer_unlock(scene_buffer->buffer);
	}
	scene_buffer->buffer = NULL;
	scene_buffer->own_buffer = false;
	scene_buffer->buffer_width = scene_buffer->buffer_height = 0;
	scene_buffer->buffer_is_opaque = false;

	if (!buffer) {
		return;
	}

	scene_buffer->own_buffer = true;
	scene_buffer->buffer = wlr_buffer_lock(buffer);
	scene_buffer->buffer_width = buffer->width;
	scene_buffer->buffer_height = buffer->height;
	scene_buffer->buffer_is_opaque = wlr_buffer_is_opaque(buffer);

	scene_buffer->buffer_release.notify = scene_buffer_handle_buffer_release;
	wl_signal_add(&buffer->events.release, &scene_buffer->buffer_release);
}

static void scene_buffer_handle_renderer_destroy(struct wl_listener *listener,
		void *data) {
	struct sway_scene_buffer *scene_buffer = wl_container_of(listener, scene_buffer, renderer_destroy);
	scene_buffer_set_texture(scene_buffer, NULL);
}

static void scene_buffer_set_texture(struct sway_scene_buffer *scene_buffer,
		struct wlr_texture *texture) {
	wl_list_remove(&scene_buffer->renderer_destroy.link);
	wlr_texture_destroy(scene_buffer->texture);
	scene_buffer->texture = texture;

	if (texture != NULL) {
		scene_buffer->renderer_destroy.notify = scene_buffer_handle_renderer_destroy;
		wl_signal_add(&texture->renderer->events.destroy, &scene_buffer->renderer_destroy);
	} else {
		wl_list_init(&scene_buffer->renderer_destroy.link);
	}
}

static void scene_buffer_set_wait_timeline(struct sway_scene_buffer *scene_buffer,
		struct wlr_drm_syncobj_timeline *timeline, uint64_t point) {
	wlr_drm_syncobj_timeline_unref(scene_buffer->wait_timeline);
	if (timeline != NULL) {
		scene_buffer->wait_timeline = wlr_drm_syncobj_timeline_ref(timeline);
		scene_buffer->wait_point = point;
	} else {
		scene_buffer->wait_timeline = NULL;
		scene_buffer->wait_point = 0;
	}
}

struct sway_scene_buffer *sway_scene_buffer_create(struct sway_scene_tree *parent,
		struct wlr_buffer *buffer) {
	struct sway_scene_buffer *scene_buffer = calloc(1, sizeof(*scene_buffer));
	if (scene_buffer == NULL) {
		return NULL;
	}
	assert(parent);
	scene_node_init(&scene_buffer->node, SWAY_SCENE_NODE_BUFFER, parent);

	wl_signal_init(&scene_buffer->events.outputs_update);
	wl_signal_init(&scene_buffer->events.output_enter);
	wl_signal_init(&scene_buffer->events.output_leave);
	wl_signal_init(&scene_buffer->events.output_sample);
	wl_signal_init(&scene_buffer->events.frame_done);

	pixman_region32_init(&scene_buffer->opaque_region);
	wl_list_init(&scene_buffer->buffer_release.link);
	wl_list_init(&scene_buffer->renderer_destroy.link);
	scene_buffer->opacity = 1;

	scene_buffer_set_buffer(scene_buffer, buffer);
	scene_node_update(&scene_buffer->node, NULL);

	return scene_buffer;
}

void sway_scene_buffer_set_buffer_with_options(struct sway_scene_buffer *scene_buffer,
		struct wlr_buffer *buffer, const struct sway_scene_buffer_set_buffer_options *options) {
	const struct sway_scene_buffer_set_buffer_options default_options = {0};
	if (options == NULL) {
		options = &default_options;
	}

	// specifying a region for a NULL buffer doesn't make sense. We need to know
	// about the buffer to scale the buffer local coordinates down to scene
	// coordinates.
	assert(buffer || !options->damage);

	bool mapped = buffer != NULL;
	bool prev_mapped = scene_buffer->buffer != NULL || scene_buffer->texture != NULL;

	if (!mapped && !prev_mapped) {
		// unmapping already unmapped buffer - noop
		return;
	}

	// if this node used to not be mapped or its previous displayed
	// buffer region will be different from what the new buffer would
	// produce we need to update the node.
	bool update = mapped != prev_mapped;
	if (buffer != NULL && scene_buffer->dst_width == 0 && scene_buffer->dst_height == 0) {
		update = update || scene_buffer->buffer_width != buffer->width ||
			scene_buffer->buffer_height != buffer->height;
	}

	// If this is a buffer change, check if it's a single pixel buffer.
	// Cache that so we can still apply rendering optimisations even when
	// the original buffer has been freed after texture upload.
	if (buffer != scene_buffer->buffer) {
		scene_buffer->is_single_pixel_buffer = false;
		struct wlr_client_buffer *client_buffer = NULL;
		if (buffer != NULL) {
			client_buffer = wlr_client_buffer_get(buffer);
		}
		if (client_buffer != NULL && client_buffer->source != NULL) {
			struct wlr_single_pixel_buffer_v1 *single_pixel_buffer =
				wlr_single_pixel_buffer_v1_try_from_buffer(client_buffer->source);
			if (single_pixel_buffer != NULL) {
				scene_buffer->is_single_pixel_buffer = true;
				scene_buffer->single_pixel_buffer_color[0] = single_pixel_buffer->r;
				scene_buffer->single_pixel_buffer_color[1] = single_pixel_buffer->g;
				scene_buffer->single_pixel_buffer_color[2] = single_pixel_buffer->b;
				scene_buffer->single_pixel_buffer_color[3] = single_pixel_buffer->a;
			}
		}
	}

	scene_buffer_set_buffer(scene_buffer, buffer);
	scene_buffer_set_texture(scene_buffer, NULL);
	scene_buffer_set_wait_timeline(scene_buffer,
		options->wait_timeline, options->wait_point);

	if (update) {
		scene_node_update(&scene_buffer->node, NULL);
		// updating the node will already damage the whole node for us. Return
		// early to not damage again
		return;
	}

	double lx, ly;
	if (!sway_scene_node_coords(&scene_buffer->node, &lx, &ly)) {
		return;
	}

	pixman_region32_t fallback_damage;
	pixman_region32_init_rect(&fallback_damage, 0, 0, buffer->width, buffer->height);
	const pixman_region32_t *damage = options->damage;
	if (!damage) {
		damage = &fallback_damage;
	}

	struct wlr_fbox box = scene_buffer->src_box;
	if (wlr_fbox_empty(&box)) {
		box.x = 0;
		box.y = 0;
		box.width = buffer->width;
		box.height = buffer->height;
	}

	wlr_fbox_transform(&box, &box, scene_buffer->transform,
		buffer->width, buffer->height);

	double scale_x, scale_y;
	if (scene_buffer->dst_width || scene_buffer->dst_height) {
		scale_x = scene_buffer->dst_width / box.width;
		scale_y = scene_buffer->dst_height / box.height;
	} else {
		scale_x = buffer->width / box.width;
		scale_y = buffer->height / box.height;
	}

	pixman_region32_t trans_damage;
	pixman_region32_init(&trans_damage);
	wlr_region_transform(&trans_damage, damage,
		scene_buffer->transform, buffer->width, buffer->height);
	pixman_region32_intersect_rect(&trans_damage, &trans_damage,
		box.x, box.y, box.width, box.height);
	pixman_region32_translate(&trans_damage, -box.x, -box.y);

	struct sway_scene *scene = scene_node_get_root(&scene_buffer->node);
	struct sway_scene_output *scene_output;
	wl_list_for_each(scene_output, &scene->outputs, link) {
		double output_scale = scene_output->output->scale;
		double output_scale_x = output_scale * scale_x;
		double output_scale_y = output_scale * scale_y;
		pixman_region32_t output_damage;
		pixman_region32_init(&output_damage);
		wlr_region_scale_xy(&output_damage, &trans_damage,
			output_scale_x, output_scale_y);

		// One output pixel will match (buffer_scale_x)x(buffer_scale_y) buffer pixels.
		// If the buffer is upscaled on the given axis (output_scale_* > 1.0,
		// buffer_scale_* < 1.0), its contents will bleed into adjacent
		// (ceil(output_scale_* / 2)) output pixels because of linear filtering.
		// Additionally, if the buffer is downscaled (output_scale_* < 1.0,
		// buffer_scale_* > 1.0), and one output pixel matches a non-integer number of
		// buffer pixels, its contents will bleed into neighboring output pixels.
		// Handle both cases by computing buffer_scale_{x,y} and checking if they are
		// integer numbers; ceilf() is used to ensure that the distance is at least 1.
		double buffer_scale_x = 1.0 / output_scale_x;
		double buffer_scale_y = 1.0 / output_scale_y;
		int dist_x = floor(buffer_scale_x) != buffer_scale_x ?
			(int)ceilf(output_scale_x / 2.0) : 0;
		int dist_y = floor(buffer_scale_y) != buffer_scale_y ?
			(int)ceilf(output_scale_y / 2.0) : 0;
		// TODO: expand with per-axis distances
		wlr_region_expand(&output_damage, &output_damage,
			dist_x >= dist_y ? dist_x : dist_y);

		pixman_region32_t cull_region;
		pixman_region32_init(&cull_region);
		pixman_region32_copy(&cull_region, &scene_buffer->node.visible);
		scale_region(&cull_region, output_scale, true);
		pixman_region32_translate(&cull_region, -lx * output_scale, -ly * output_scale);
		pixman_region32_intersect(&output_damage, &output_damage, &cull_region);
		pixman_region32_fini(&cull_region);

		pixman_region32_translate(&output_damage,
			round((lx - scene_output->x) * output_scale),
			round((ly - scene_output->y) * output_scale));
		output_to_buffer_coords(&output_damage, scene_output->output);
		scene_output_damage(scene_output, &output_damage);
		pixman_region32_fini(&output_damage);
	}

	pixman_region32_fini(&trans_damage);
	pixman_region32_fini(&fallback_damage);
}

void sway_scene_buffer_set_buffer_with_damage(struct sway_scene_buffer *scene_buffer,
		struct wlr_buffer *buffer, const pixman_region32_t *damage) {
	const struct sway_scene_buffer_set_buffer_options options = {
		.damage = damage,
	};
	sway_scene_buffer_set_buffer_with_options(scene_buffer, buffer, &options);
}

void sway_scene_buffer_set_buffer(struct sway_scene_buffer *scene_buffer,
		struct wlr_buffer *buffer)  {
	sway_scene_buffer_set_buffer_with_options(scene_buffer, buffer, NULL);
}

void sway_scene_buffer_set_opaque_region(struct sway_scene_buffer *scene_buffer,
		const pixman_region32_t *region) {
	if (pixman_region32_equal(&scene_buffer->opaque_region, region)) {
		return;
	}

	pixman_region32_copy(&scene_buffer->opaque_region, region);

	double x, y;
	if (!sway_scene_node_coords(&scene_buffer->node, &x, &y)) {
		return;
	}

	pixman_region32_t update_region;
	pixman_region32_init(&update_region);
	scene_node_bounds(&scene_buffer->node, x, y, &update_region);
	scene_update_region(scene_node_get_root(&scene_buffer->node), &update_region);
	pixman_region32_fini(&update_region);
}

void sway_scene_buffer_set_source_box(struct sway_scene_buffer *scene_buffer,
		const struct wlr_fbox *box) {
	if (wlr_fbox_equal(&scene_buffer->src_box, box)) {
		return;
	}

	if (box != NULL) {
		assert(box->x >= 0 && box->y >= 0 && box->width >= 0 && box->height >= 0);
		scene_buffer->src_box = *box;
	} else {
		scene_buffer->src_box = (struct wlr_fbox){0};
	}

	scene_node_update(&scene_buffer->node, NULL);
}

void sway_scene_buffer_set_dest_size(struct sway_scene_buffer *scene_buffer,
		double width, double height) {
	if (scene_buffer->dst_width == width && scene_buffer->dst_height == height) {
		return;
	}

	assert(width >= 0 && height >= 0);
	scene_buffer->dst_width = width;
	scene_buffer->dst_height = height;
	scene_node_update(&scene_buffer->node, NULL);
}

void sway_scene_buffer_set_radius(struct sway_scene_buffer *scene_buffer,
	float radius_top, float radius_bottom) {
	if (scene_buffer->radius_top == radius_top && scene_buffer->radius_bottom == radius_bottom) {
		return;
	}

	assert(radius_top >= 0 && radius_bottom >= 0);
	scene_buffer->radius_top = radius_top;
	scene_buffer->radius_bottom = radius_bottom;
	scene_node_update(&scene_buffer->node, NULL);
}

void sway_scene_buffer_set_transform(struct sway_scene_buffer *scene_buffer,
		enum wl_output_transform transform) {
	if (scene_buffer->transform == transform) {
		return;
	}

	scene_buffer->transform = transform;
	scene_node_update(&scene_buffer->node, NULL);
}

void sway_scene_buffer_send_frame_done(struct sway_scene_buffer *scene_buffer,
		struct sway_scene_frame_done_event *event) {
	if (!pixman_region32_empty(&scene_buffer->node.visible)) {
		wl_signal_emit_mutable(&scene_buffer->events.frame_done, event);
	}
}

void sway_scene_buffer_set_opacity(struct sway_scene_buffer *scene_buffer,
		float opacity) {
	if (scene_buffer->opacity == opacity) {
		return;
	}

	assert(opacity >= 0 && opacity <= 1);
	scene_buffer->opacity = opacity;
	scene_node_update(&scene_buffer->node, NULL);
}

void sway_scene_buffer_set_filter_mode(struct sway_scene_buffer *scene_buffer,
		enum wlr_scale_filter_mode filter_mode) {
	if (scene_buffer->filter_mode == filter_mode) {
		return;
	}

	scene_buffer->filter_mode = filter_mode;
	scene_node_update(&scene_buffer->node, NULL);
}

void sway_scene_buffer_set_transfer_function(struct sway_scene_buffer *scene_buffer,
		enum wlr_color_transfer_function transfer_function) {
	if (scene_buffer->transfer_function == transfer_function) {
		return;
	}

	scene_buffer->transfer_function = transfer_function;
	scene_node_update(&scene_buffer->node, NULL);
}

void sway_scene_buffer_set_primaries(struct sway_scene_buffer *scene_buffer,
		enum wlr_color_named_primaries primaries) {
	if (scene_buffer->primaries == primaries) {
		return;
	}

	scene_buffer->primaries = primaries;
	scene_node_update(&scene_buffer->node, NULL);
}

void sway_scene_buffer_set_color_encoding(struct sway_scene_buffer *scene_buffer,
		enum wlr_color_encoding color_encoding) {
	if (scene_buffer->color_encoding == color_encoding) {
		return;
	}

	scene_buffer->color_encoding = color_encoding;
	scene_node_update(&scene_buffer->node, NULL);
}

void sway_scene_buffer_set_color_range(struct sway_scene_buffer *scene_buffer,
		enum wlr_color_range color_range) {
	if (scene_buffer->color_range == color_range) {
		return;
	}

	scene_buffer->color_range = color_range;
	scene_node_update(&scene_buffer->node, NULL);
}

static struct wlr_texture *scene_buffer_get_texture(
		struct sway_scene_buffer *scene_buffer, struct wlr_renderer *renderer) {
	if (scene_buffer->buffer == NULL || scene_buffer->texture != NULL) {
		return scene_buffer->texture;
	}

	struct wlr_client_buffer *client_buffer =
		wlr_client_buffer_get(scene_buffer->buffer);
	if (client_buffer != NULL) {
		return client_buffer->texture;
	}

	struct wlr_texture *texture =
		wlr_texture_from_buffer(renderer, scene_buffer->buffer);
	if (texture != NULL && scene_buffer->own_buffer) {
		scene_buffer->own_buffer = false;
		wlr_buffer_unlock(scene_buffer->buffer);
	}
	scene_buffer_set_texture(scene_buffer, texture);
	return texture;
}

void scene_node_get_size(struct sway_scene_node *node,
		double *width, double *height) {
	*width = 0;
	*height = 0;

	switch (node->type) {
	case SWAY_SCENE_NODE_TREE:
		return;
	case SWAY_SCENE_NODE_RECT:;
		struct sway_scene_rect *scene_rect = sway_scene_rect_from_node(node);
		*width = scene_rect->width;
		*height = scene_rect->height;
		break;
	case SWAY_SCENE_NODE_BUFFER:;
		struct sway_scene_buffer *scene_buffer = sway_scene_buffer_from_node(node);
		if (scene_buffer->dst_width > 0 && scene_buffer->dst_height > 0) {
			*width = scene_buffer->dst_width;
			*height = scene_buffer->dst_height;
		} else {
			int w = scene_buffer->buffer_width;
			int h = scene_buffer->buffer_height;
			wlr_output_transform_coords(scene_buffer->transform, &w, &h);
			*width = w;
			*height = h;
		}
		break;
	case SWAY_SCENE_NODE_DECORATION:;
		struct sway_scene_decoration *scene_decoration = sway_scene_decoration_from_node(node);
		*width = scene_decoration->width;
		*height = scene_decoration->height;
		break;
	case SWAY_SCENE_NODE_SHADOW:;
		struct sway_scene_shadow *scene_shadow = sway_scene_shadow_from_node(node);
		*width = scene_shadow->width;
		*height = scene_shadow->height;
		break;
	}
}

void sway_scene_node_set_enabled(struct sway_scene_node *node, bool enabled) {
	if (node->enabled == enabled) {
		return;
	}

	double x, y;
	pixman_region32_t visible;
	pixman_region32_init(&visible);
	if (sway_scene_node_coords(node, &x, &y)) {
		scene_node_visibility(node, &visible);
	}

	node->enabled = enabled;

	scene_node_update(node, &visible);
}

void sway_scene_node_set_position(struct sway_scene_node *node, double x, double y) {
	// Check if there is a scaled popup/view parent
	double scale;
	bool is_parent = scene_node_get_parent_total_scale(node, &scale);
	if (scale > 0.0 && !is_parent) {
		// We want to correct the coordinates of nodes descending from a popup/view,
		// but not the popup/view node itself, because it contains the coordinates
		// of the parent view/view
		// For unamanaged Xwayland surfaces (popups), the coordinates are absolute,
		// so we set them as parents in scene_node_get_parent_total_scale() and
		// don't correct them.
		x *= scale;
		y *= scale;
	}
	if (node->x == x && node->y == y) {
		return;
	}

	node->x = x;
	node->y = y;
	scene_node_update(node, NULL);
}

void sway_scene_node_place_above(struct sway_scene_node *node,
		struct sway_scene_node *sibling) {
	assert(node != sibling);
	assert(node->parent == sibling->parent);

	if (node->link.prev == &sibling->link) {
		return;
	}

	wl_list_remove(&node->link);
	wl_list_insert(&sibling->link, &node->link);
	scene_node_update(node, NULL);
}

void sway_scene_node_place_below(struct sway_scene_node *node,
		struct sway_scene_node *sibling) {
	assert(node != sibling);
	assert(node->parent == sibling->parent);

	if (node->link.next == &sibling->link) {
		return;
	}

	wl_list_remove(&node->link);
	wl_list_insert(sibling->link.prev, &node->link);
	scene_node_update(node, NULL);
}

void sway_scene_node_raise_to_top(struct sway_scene_node *node) {
	struct sway_scene_node *current_top = wl_container_of(
		node->parent->children.prev, current_top, link);
	if (node == current_top) {
		return;
	}
	sway_scene_node_place_above(node, current_top);
}

void sway_scene_node_lower_to_bottom(struct sway_scene_node *node) {
	struct sway_scene_node *current_bottom = wl_container_of(
		node->parent->children.next, current_bottom, link);
	if (node == current_bottom) {
		return;
	}
	sway_scene_node_place_below(node, current_bottom);
}

void sway_scene_node_reparent(struct sway_scene_node *node,
		struct sway_scene_tree *new_parent) {
	assert(new_parent != NULL);

	if (node->parent == new_parent) {
		return;
	}

	/* Ensure that a node cannot become its own ancestor */
	for (struct sway_scene_tree *ancestor = new_parent; ancestor != NULL;
			ancestor = ancestor->node.parent) {
		assert(&ancestor->node != node);
	}

	double x, y;
	pixman_region32_t visible;
	pixman_region32_init(&visible);
	if (sway_scene_node_coords(node, &x, &y)) {
		scene_node_visibility(node, &visible);
	}

	wl_list_remove(&node->link);
	node->parent = new_parent;
	wl_list_insert(new_parent->children.prev, &node->link);
	scene_node_update(node, &visible);
}

bool sway_scene_node_coords(struct sway_scene_node *node,
		double *lx_ptr, double *ly_ptr) {
	assert(node);

	double lx = 0, ly = 0;
	bool enabled = true;
	while (true) {
		lx += node->x;
		ly += node->y;
		enabled = enabled && node->enabled;
		if (node->parent == NULL) {
			break;
		}

		node = &node->parent->node;
	}

	*lx_ptr = lx;
	*ly_ptr = ly;
	return enabled;
}

static void scene_node_for_each_scene_buffer(struct sway_scene_node *node,
		double lx, double ly, sway_scene_buffer_iterator_func_t user_iterator,
		void *user_data) {
	if (!node->enabled) {
		return;
	}

	lx += node->x;
	ly += node->y;

	if (node->type == SWAY_SCENE_NODE_BUFFER) {
		struct sway_scene_buffer *scene_buffer = sway_scene_buffer_from_node(node);
		user_iterator(scene_buffer, lx, ly, user_data);
	} else if (node->type == SWAY_SCENE_NODE_TREE) {
		struct sway_scene_tree *scene_tree = sway_scene_tree_from_node(node);
		struct sway_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_node_for_each_scene_buffer(child, lx, ly, user_iterator, user_data);
		}
	}
}

void sway_scene_node_for_each_buffer(struct sway_scene_node *node,
		sway_scene_buffer_iterator_func_t user_iterator, void *user_data) {
	scene_node_for_each_scene_buffer(node, 0, 0, user_iterator, user_data);
}

struct node_at_data {
	double lx, ly;
	double rx, ry;
	struct sway_scene_node *node;
};

static bool scene_node_at_iterator(struct sway_scene_node *node,
		double lx, double ly, void *data) {
	struct node_at_data *at_data = data;

	struct wlr_output *wlr_output = scene_node_get_output(node);
	if (wlr_output) {
		struct sway_output *output = output_for_coords(at_data->lx, at_data->ly);
		if (output && wlr_output != output->wlr_output) {
			return false;
		}
	}
	struct wlr_box *box = scene_node_get_workspace_box(node);
	if (box) {
		if (at_data->lx < box->x || at_data->lx >= box->x + box->width ||
			at_data->ly < box->y || at_data->ly >= box->y + box->height) {
			return false;
		}
	}

	double rx = at_data->lx - lx;
	double ry = at_data->ly - ly;

	if (node->type == SWAY_SCENE_NODE_BUFFER) {
		struct sway_scene_buffer *scene_buffer = sway_scene_buffer_from_node(node);

		double total_scale = 1.0;
		struct sway_scene_surface *scene_surface =
			sway_scene_surface_try_from_buffer(scene_buffer);

		if (scene_surface) {
			struct sway_view *view = view_from_wlr_surface(scene_surface->surface);
			if (view) {
				total_scale = view_get_total_scale(view);
				if (total_scale <= 0.0) {
					total_scale = 1.0;
				}
			}
		}

		// Correct the coordinates to buffer local from desktop logical
		rx /= total_scale;
		ry /= total_scale;
		if (scene_buffer->point_accepts_input &&
				!scene_buffer->point_accepts_input(scene_buffer, &rx, &ry)) {
			return false;
		}
	} else if (node->type == SWAY_SCENE_NODE_DECORATION) {
		struct sway_scene_decoration *scene_decoration = sway_scene_decoration_from_node(node);
		struct sway_view *view = scene_decoration->view;
		struct sway_container *con = view->container;
		if (con) {
			double scale = view_get_total_scale(view);
			if (scale <= 0.0) {
				scale = 1.0;
			}
			const double cx = rx / scale + con->pending.x - con->pending.content_x;
			const double cy = ry / scale + con->pending.y - con->pending.content_y;
			if (cx > 0.0 && cx < con->pending.content_width &&
				cy > 0.0 && cy < con->pending.content_height) {
				return false;
			}
		}
	} else if (node->type == SWAY_SCENE_NODE_SHADOW) {
		return false;
	}

	at_data->rx = rx;
	at_data->ry = ry;
	at_data->node = node;
	return true;
}

struct sway_scene_node *sway_scene_node_at(struct sway_scene_node *node,
		double lx, double ly, double *nx, double *ny) {
	struct wlr_box box = {
		.x = floor(lx),
		.y = floor(ly),
		.width = 1,
		.height = 1
	};

	struct node_at_data data = {
		.lx = lx,
		.ly = ly
	};

	if (scene_nodes_in_box(node, &box, scene_node_at_iterator, &data)) {
		if (nx) {
			*nx = data.rx;
		}
		if (ny) {
			*ny = data.ry;
		}
		return data.node;
	}

	return NULL;
}

static void query_transform(enum wl_output_transform transform, bool *swap_xy,
		bool *flip_x, bool *flip_y) {
	switch (transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
		*swap_xy = false;
		*flip_x = false;
		*flip_y = false;
		break;
	case WL_OUTPUT_TRANSFORM_90:
		*swap_xy = true;
		*flip_x = false;
		*flip_y = true;
		break;
	case WL_OUTPUT_TRANSFORM_180:
		*swap_xy = false;
		*flip_x = true;
		*flip_y = true;
		break;
	case WL_OUTPUT_TRANSFORM_270:
		*swap_xy = true;
		*flip_x = true;
		*flip_y = false;
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED:
		*swap_xy = false;
		*flip_x = true;
		*flip_y = false;
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		*swap_xy = true;
		*flip_x = false;
		*flip_y = false;
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		*swap_xy = false;
		*flip_x = false;
		*flip_y = true;
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		*swap_xy = true;
		*flip_x = true;
		*flip_y = true;
		break;
	default:
		sway_log(SWAY_ERROR, "query_transform(): Unknown transform type %d", transform);
		*swap_xy = false;
		*flip_x = false;
		*flip_y = false;
		break;
	}
}

struct render_list_entry {
	struct sway_scene_node *node;
	bool highlight_transparent_region;
	double x, y;
};

static void scene_entry_render(struct render_list_entry *entry, const struct render_data *data) {
	struct sway_scene_node *node = entry->node;

	struct sway_workspace *workspace = scene_node_get_workspace(node);
	double scale = workspace ? workspace->layout.workspaces.scale : 1.0;
	double dx = workspace ? workspace->layout.workspaces.x : 0;
	double dy = workspace ? workspace->layout.workspaces.y : 0;

	pixman_region32_t render_region;
	pixman_region32_init(&render_region);
	pixman_region32_copy(&render_region, &node->visible);
	pixman_region32_translate(&render_region, -data->logical.x, -data->logical.y);
	logical_to_buffer_coords(&render_region, data, true);
	if (workspace) {
		scale_region(&render_region, scale, false);
		pixman_region32_translate(&render_region, dx, dy);
		// Clip against "mini-workspace"
		pixman_region32_intersect_rect(&render_region, &render_region, dx, dy,
			workspace->layout.workspaces.width, workspace->layout.workspaces.height);
	}
	pixman_region32_intersect(&render_region, &render_region, &data->damage);
	if (pixman_region32_empty(&render_region)) {
		pixman_region32_fini(&render_region);
		return;
	}
	double x = entry->x - data->logical.x;
	double y = entry->y - data->logical.y;

	struct wlr_fbox dst_fbox = {
		.x = x,
		.y = y,
	};
	scene_node_get_size(node, &dst_fbox.width, &dst_fbox.height);
	transform_output_box(&dst_fbox, data);

	pixman_region32_t opaque;
	pixman_region32_init(&opaque);
	scene_node_opaque_region(node, x, y, &opaque);
	logical_to_buffer_coords(&opaque, data, false);
	if (workspace) {
		scale_region(&opaque, scale, false);
		pixman_region32_translate(&opaque, dx, dy);
		pixman_region32_intersect_rect(&opaque, &opaque, dx, dy,
			workspace->layout.workspaces.width, workspace->layout.workspaces.height);
	}
	pixman_region32_subtract(&opaque, &render_region, &opaque);

	if (workspace) {
		dst_fbox.x = ceil(dst_fbox.x * scale + dx);
		dst_fbox.y = ceil(dst_fbox.y * scale + dy);
		dst_fbox.width = ceil(dst_fbox.width * scale);
		dst_fbox.height = ceil(dst_fbox.height * scale);
	}

	struct wlr_box dst_box = {
		.x = round(dst_fbox.x),
		.y = round(dst_fbox.y),
		.width = round(dst_fbox.width),
		.height = round(dst_fbox.height)
	};

	switch (node->type) {
	case SWAY_SCENE_NODE_TREE:
		assert(false);
		break;
	case SWAY_SCENE_NODE_RECT:;
		struct sway_scene_rect *scene_rect = sway_scene_rect_from_node(node);

		wlr_render_pass_add_rect(data->render_pass, &(struct wlr_render_rect_options){
			.box = dst_box,
			.color = {
				.r = scene_rect->color[0],
				.g = scene_rect->color[1],
				.b = scene_rect->color[2],
				.a = scene_rect->color[3],
			},
			.clip = &render_region,
		});
		break;
	case SWAY_SCENE_NODE_BUFFER:;
		struct sway_scene_buffer *scene_buffer = sway_scene_buffer_from_node(node);

		if (scene_buffer->is_single_pixel_buffer) {
			// Render the buffer as a rect, this is likely to be more efficient
			wlr_render_pass_add_rect(data->render_pass, &(struct wlr_render_rect_options){
				.box = dst_box,
				.color = {
					.r = (float)scene_buffer->single_pixel_buffer_color[0] / (float)UINT32_MAX,
					.g = (float)scene_buffer->single_pixel_buffer_color[1] / (float)UINT32_MAX,
					.b = (float)scene_buffer->single_pixel_buffer_color[2] / (float)UINT32_MAX,
					.a = (float)scene_buffer->single_pixel_buffer_color[3] /
						(float)UINT32_MAX * scene_buffer->opacity,
				},
				.clip = &render_region,
			});
			break;
		}

		struct wlr_texture *texture = scene_buffer_get_texture(scene_buffer,
			data->output->output->renderer);
		if (texture == NULL) {
			scene_output_damage(data->output, &render_region);
			break;
		}

		enum wl_output_transform transform =
			wlr_output_transform_invert(scene_buffer->transform);
		transform = wlr_output_transform_compose(transform, data->transform);

		struct wlr_color_primaries primaries = {0};
		if (scene_buffer->primaries != 0) {
			wlr_color_primaries_from_named(&primaries, scene_buffer->primaries);
		}

		bool swap_xy, flip_x, flip_y;
		query_transform(data->transform, &swap_xy, &flip_x, &flip_y);
		wlr_render_pass_add_texture(data->render_pass, &(struct wlr_render_texture_options) {
			.swap_xy = swap_xy,
			.flip_x = flip_x,
			.flip_y = flip_y,
			.radius_top = round(scene_buffer->radius_top * data->scale),
			.radius_bottom = round(scene_buffer->radius_bottom * data->scale),
			.texture = texture,
			.src_box = scene_buffer->src_box,
			.dst_box = dst_box,
			.transform = transform,
			.clip = &render_region,
			.alpha = &scene_buffer->opacity,
			.filter_mode = scene_buffer->filter_mode,
			.blend_mode = !data->output->scene->calculate_visibility ||
					!pixman_region32_empty(&opaque) ?
				WLR_RENDER_BLEND_MODE_PREMULTIPLIED : WLR_RENDER_BLEND_MODE_NONE,
			.transfer_function = scene_buffer->transfer_function,
			.primaries = scene_buffer->primaries != 0 ? &primaries : NULL,
			.color_encoding = scene_buffer->color_encoding,
			.color_range = scene_buffer->color_range,
			.wait_timeline = scene_buffer->wait_timeline,
			.wait_point = scene_buffer->wait_point,
		});

		struct sway_scene_output_sample_event sample_event = {
			.output = data->output,
			.direct_scanout = false,
		};
		wl_signal_emit_mutable(&scene_buffer->events.output_sample, &sample_event);

		if (entry->highlight_transparent_region) {
			wlr_render_pass_add_rect(data->render_pass, &(struct wlr_render_rect_options){
				.box = dst_box,
				.color = { .r = 0, .g = 0.3, .b = 0, .a = 0.3 },
				.clip = &opaque,
			});
		}

		break;
	case SWAY_SCENE_NODE_DECORATION: {
		struct sway_scene_decoration *scene_decoration = sway_scene_decoration_from_node(node);
		struct wlr_object *object = scene_decoration_get_object(scene_decoration,
			data->output->output->renderer);
		bool swap_xy, flip_x, flip_y;
		query_transform(data->transform, &swap_xy, &flip_x, &flip_y);
		wlr_render_pass_add_decoration(data->render_pass, &(struct wlr_render_decoration_options){
			.box = dst_box,
			.clip = &render_region,
			.swap_xy = swap_xy,
			.flip_x = flip_x,
			.flip_y = flip_y,
			.object = object,
			.border = scene_decoration->border,
			.border_radius = round(scene_decoration->border_radius * data->scale),
			.border_width = round(scene_decoration->border_width * data->scale),
			.border_top_color = {
				.r = scene_decoration->border_top_color[0],
				.g = scene_decoration->border_top_color[1],
				.b = scene_decoration->border_top_color[2],
				.a = scene_decoration->border_top_color[3],
			},
			.border_bottom_color = {
				.r = scene_decoration->border_bottom_color[0],
				.g = scene_decoration->border_bottom_color[1],
				.b = scene_decoration->border_bottom_color[2],
				.a = scene_decoration->border_bottom_color[3],
			},
			.border_left_color = {
				.r = scene_decoration->border_left_color[0],
				.g = scene_decoration->border_left_color[1],
				.b = scene_decoration->border_left_color[2],
				.a = scene_decoration->border_left_color[3],
			},
			.border_right_color = {
				.r = scene_decoration->border_right_color[0],
				.g = scene_decoration->border_right_color[1],
				.b = scene_decoration->border_right_color[2],
				.a = scene_decoration->border_right_color[3],
			},
			.title_bar = scene_decoration->title_bar,
			.title_bar_height = scene_decoration->title_bar_height * data->scale,
			.title_bar_border_radius = scene_decoration->title_bar_border_radius * data->scale,
			.title_bar_color = {
				.r = scene_decoration->title_bar_color[0],
				.g = scene_decoration->title_bar_color[1],
				.b = scene_decoration->title_bar_color[2],
				.a = scene_decoration->title_bar_color[3],
			},
			.dim = scene_decoration->dim,
			.dim_color = {
				.r = scene_decoration->dim_color[0],
				.g = scene_decoration->dim_color[1],
				.b = scene_decoration->dim_color[2],
				.a = scene_decoration->dim_color[3],
			},
		});
		break;
	}
	case SWAY_SCENE_NODE_SHADOW: {
		struct sway_scene_shadow *scene_shadow = sway_scene_shadow_from_node(node);
		struct wlr_object *object = scene_shadow_get_object(scene_shadow,
			data->output->output->renderer);
		bool swap_xy, flip_x, flip_y;
		query_transform(data->transform, &swap_xy, &flip_x, &flip_y);
		wlr_render_pass_add_shadow(data->render_pass, &(struct wlr_render_shadow_options){
			.box = dst_box,
			.clip = &render_region,
			.object = object,
			.swap_xy = swap_xy,
			.flip_x = flip_x,
			.flip_y = flip_y,
			.radius_top = round((scene_shadow->decoration->title_bar ?
				scene_shadow->decoration->title_bar_border_radius :
				scene_shadow->decoration->border_radius) * data->scale),
			.radius_bottom = round(scene_shadow->decoration->border_radius * data->scale),
			.enabled = scene_shadow->enabled,
			.blur = scene_shadow->blur,
			.color = {
				.r = scene_shadow->color[0],
				.g = scene_shadow->color[1],
				.b = scene_shadow->color[2],
				.a = scene_shadow->color[3],
			},
		});
		break;
	}
	}

	pixman_region32_fini(&opaque);
	pixman_region32_fini(&render_region);
}

static void scene_handle_linux_dmabuf_v1_destroy(struct wl_listener *listener,
		void *data) {
	struct sway_scene *scene =
		wl_container_of(listener, scene, linux_dmabuf_v1_destroy);
	wl_list_remove(&scene->linux_dmabuf_v1_destroy.link);
	wl_list_init(&scene->linux_dmabuf_v1_destroy.link);
	scene->linux_dmabuf_v1 = NULL;
}

void sway_scene_set_linux_dmabuf_v1(struct sway_scene *scene,
		struct wlr_linux_dmabuf_v1 *linux_dmabuf_v1) {
	assert(scene->linux_dmabuf_v1 == NULL);
	scene->linux_dmabuf_v1 = linux_dmabuf_v1;
	scene->linux_dmabuf_v1_destroy.notify = scene_handle_linux_dmabuf_v1_destroy;
	wl_signal_add(&linux_dmabuf_v1->events.destroy, &scene->linux_dmabuf_v1_destroy);
}

static void scene_handle_gamma_control_manager_v1_set_gamma(struct wl_listener *listener,
		void *data) {
	const struct wlr_gamma_control_manager_v1_set_gamma_event *event = data;
	struct sway_scene *scene =
		wl_container_of(listener, scene, gamma_control_manager_v1_set_gamma);
	struct sway_scene_output *output = sway_scene_get_scene_output(scene, event->output);
	if (!output) {
		// this scene might not own this output.
		return;
	}

	output->gamma_lut_changed = true;
	output->gamma_lut = event->control;
	wlr_color_transform_unref(output->gamma_lut_color_transform);
	output->gamma_lut_color_transform = wlr_gamma_control_v1_get_color_transform(event->control);
	wlr_output_schedule_frame(output->output);
}

static void scene_handle_gamma_control_manager_v1_destroy(struct wl_listener *listener,
		void *data) {
	struct sway_scene *scene =
		wl_container_of(listener, scene, gamma_control_manager_v1_destroy);
	wl_list_remove(&scene->gamma_control_manager_v1_destroy.link);
	wl_list_init(&scene->gamma_control_manager_v1_destroy.link);
	wl_list_remove(&scene->gamma_control_manager_v1_set_gamma.link);
	wl_list_init(&scene->gamma_control_manager_v1_set_gamma.link);
	scene->gamma_control_manager_v1 = NULL;

	struct sway_scene_output *output;
	wl_list_for_each(output, &scene->outputs, link) {
		output->gamma_lut_changed = false;
		output->gamma_lut = NULL;
		wlr_color_transform_unref(output->gamma_lut_color_transform);
		output->gamma_lut_color_transform = NULL;
	}
}

void sway_scene_set_gamma_control_manager_v1(struct sway_scene *scene,
	    struct wlr_gamma_control_manager_v1 *gamma_control) {
	assert(scene->gamma_control_manager_v1 == NULL);
	scene->gamma_control_manager_v1 = gamma_control;

	scene->gamma_control_manager_v1_destroy.notify =
		scene_handle_gamma_control_manager_v1_destroy;
	wl_signal_add(&gamma_control->events.destroy, &scene->gamma_control_manager_v1_destroy);
	scene->gamma_control_manager_v1_set_gamma.notify =
		scene_handle_gamma_control_manager_v1_set_gamma;
	wl_signal_add(&gamma_control->events.set_gamma, &scene->gamma_control_manager_v1_set_gamma);
}

static void scene_handle_color_manager_v1_destroy(struct wl_listener *listener, void *data) {
	struct sway_scene *scene = wl_container_of(listener, scene, color_manager_v1_destroy);
	wl_list_remove(&scene->color_manager_v1_destroy.link);
	wl_list_init(&scene->color_manager_v1_destroy.link);
	scene->color_manager_v1 = NULL;
}

void sway_scene_set_color_manager_v1(struct sway_scene *scene, struct wlr_color_manager_v1 *manager) {
	assert(scene->color_manager_v1 == NULL);
	scene->color_manager_v1 = manager;

	scene->color_manager_v1_destroy.notify = scene_handle_color_manager_v1_destroy;
	wl_signal_add(&manager->events.destroy, &scene->color_manager_v1_destroy);
}

static void scene_output_handle_destroy(struct wlr_addon *addon) {
	struct sway_scene_output *scene_output =
		wl_container_of(addon, scene_output, addon);
	sway_scene_output_destroy(scene_output);
}

static const struct wlr_addon_interface output_addon_impl = {
	.name = "sway_scene_output",
	.destroy = scene_output_handle_destroy,
};

static void scene_node_output_update(struct sway_scene_node *node,
		struct wl_list *outputs, struct sway_scene_output *ignore,
		struct sway_scene_output *force) {
	if (node->type == SWAY_SCENE_NODE_TREE) {
		struct sway_scene_tree *scene_tree = sway_scene_tree_from_node(node);
		struct sway_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_node_output_update(child, outputs, ignore, force);
		}
		return;
	}

	update_node_update_outputs(node, outputs, ignore, force);
}

static void scene_output_update_geometry(struct sway_scene_output *scene_output,
		bool force_update) {
	scene_output_damage_whole(scene_output);

	scene_node_output_update(&scene_output->scene->tree.node,
			&scene_output->scene->outputs, NULL, force_update ? scene_output : NULL);
}

static void scene_output_handle_commit(struct wl_listener *listener, void *data) {
	struct sway_scene_output *scene_output = wl_container_of(listener,
		scene_output, output_commit);
	struct wlr_output_event_commit *event = data;
	const struct wlr_output_state *state = event->state;

	// if the output has been committed with a certain damage, we know that region
	// will be acknowledged by the backend so we don't need to keep track of it
	// anymore
	if (state->committed & WLR_OUTPUT_STATE_BUFFER) {
		if (state->committed & WLR_OUTPUT_STATE_DAMAGE) {
			pixman_region32_subtract(&scene_output->pending_commit_damage,
				&scene_output->pending_commit_damage, &state->damage);
		} else {
			pixman_region32_fini(&scene_output->pending_commit_damage);
			pixman_region32_init(&scene_output->pending_commit_damage);
		}
	}

	bool force_update = state->committed & (
		WLR_OUTPUT_STATE_TRANSFORM |
		WLR_OUTPUT_STATE_SCALE |
		WLR_OUTPUT_STATE_SUBPIXEL);

	if (force_update || state->committed & (WLR_OUTPUT_STATE_MODE |
			WLR_OUTPUT_STATE_ENABLED)) {
		scene_output_update_geometry(scene_output, force_update);
	}

	if (scene_output->scene->debug_damage_option == SWAY_SCENE_DEBUG_DAMAGE_HIGHLIGHT &&
			!wl_list_empty(&scene_output->damage_highlight_regions)) {
		wlr_output_schedule_frame(scene_output->output);
	}

	// Next time the output is enabled, try to re-apply the gamma LUT
	if (scene_output->scene->gamma_control_manager_v1 &&
			(state->committed & WLR_OUTPUT_STATE_ENABLED) &&
			!scene_output->output->enabled) {
		scene_output->gamma_lut_changed = true;
	}
}

static void scene_output_handle_damage(struct wl_listener *listener, void *data) {
	struct sway_scene_output *scene_output = wl_container_of(listener,
		scene_output, output_damage);
	struct wlr_output *output = scene_output->output;
	struct wlr_output_event_damage *event = data;

	int width, height;
	wlr_output_transformed_resolution(output, &width, &height);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_copy(&damage, event->damage);
	wlr_region_transform(&damage, &damage,
		wlr_output_transform_invert(output->transform), width, height);
	scene_output_damage(scene_output, &damage);
	pixman_region32_fini(&damage);
}

static void scene_output_handle_needs_frame(struct wl_listener *listener, void *data) {
	struct sway_scene_output *scene_output = wl_container_of(listener,
		scene_output, output_needs_frame);
	wlr_output_schedule_frame(scene_output->output);
}

struct sway_scene_output *sway_scene_output_create(struct sway_scene *scene,
		struct wlr_output *output) {
	struct sway_scene_output *scene_output = calloc(1, sizeof(*scene_output));
	if (scene_output == NULL) {
		return NULL;
	}

	scene_output->output = output;
	scene_output->scene = scene;
	wlr_addon_init(&scene_output->addon, &output->addons, scene, &output_addon_impl);

	wlr_damage_ring_init(&scene_output->damage_ring);
	pixman_region32_init(&scene_output->pending_commit_damage);
	wl_list_init(&scene_output->damage_highlight_regions);

	int prev_output_index = -1;
	struct wl_list *prev_output_link = &scene->outputs;

	struct sway_scene_output *current_output;
	wl_list_for_each(current_output, &scene->outputs, link) {
		if (prev_output_index + 1 != current_output->index) {
			break;
		}

		prev_output_index = current_output->index;
		prev_output_link = &current_output->link;
	}

	int drm_fd = wlr_backend_get_drm_fd(output->backend);
	if (drm_fd >= 0 && output->backend->features.timeline &&
			output->renderer != NULL && output->renderer->features.timeline) {
		scene_output->in_timeline = wlr_drm_syncobj_timeline_create(drm_fd);
		if (scene_output->in_timeline == NULL) {
			return NULL;
		}
	}

	scene_output->index = prev_output_index + 1;
	assert(scene_output->index < 64);
	wl_list_insert(prev_output_link, &scene_output->link);

	wl_signal_init(&scene_output->events.destroy);

	scene_output->output_commit.notify = scene_output_handle_commit;
	wl_signal_add(&output->events.commit, &scene_output->output_commit);

	scene_output->output_damage.notify = scene_output_handle_damage;
	wl_signal_add(&output->events.damage, &scene_output->output_damage);

	scene_output->output_needs_frame.notify = scene_output_handle_needs_frame;
	wl_signal_add(&output->events.needs_frame, &scene_output->output_needs_frame);

	scene_output_update_geometry(scene_output, false);

	return scene_output;
}

static void highlight_region_destroy(struct highlight_region *damage) {
	wl_list_remove(&damage->link);
	pixman_region32_fini(&damage->region);
	free(damage);
}

void sway_scene_output_destroy(struct sway_scene_output *scene_output) {
	if (scene_output == NULL) {
		return;
	}

	wl_signal_emit_mutable(&scene_output->events.destroy, NULL);

	scene_node_output_update(&scene_output->scene->tree.node,
		&scene_output->scene->outputs, scene_output, NULL);

	assert(wl_list_empty(&scene_output->events.destroy.listener_list));

	struct highlight_region *damage, *tmp_damage;
	wl_list_for_each_safe(damage, tmp_damage, &scene_output->damage_highlight_regions, link) {
		highlight_region_destroy(damage);
	}

	wlr_addon_finish(&scene_output->addon);
	wlr_damage_ring_finish(&scene_output->damage_ring);
	pixman_region32_fini(&scene_output->pending_commit_damage);
	wl_list_remove(&scene_output->link);
	wl_list_remove(&scene_output->output_commit.link);
	wl_list_remove(&scene_output->output_damage.link);
	wl_list_remove(&scene_output->output_needs_frame.link);
	wlr_drm_syncobj_timeline_unref(scene_output->in_timeline);
	wlr_color_transform_unref(scene_output->gamma_lut_color_transform);
	wlr_color_transform_unref(scene_output->prev_gamma_lut_color_transform);
	wlr_color_transform_unref(scene_output->prev_supplied_color_transform);
	wlr_color_transform_unref(scene_output->combined_color_transform);
	wl_array_release(&scene_output->render_list);
	free(scene_output);
}

struct sway_scene_output *sway_scene_get_scene_output(struct sway_scene *scene,
		struct wlr_output *output) {
	struct wlr_addon *addon =
		wlr_addon_find(&output->addons, scene, &output_addon_impl);
	if (addon == NULL) {
		return NULL;
	}
	struct sway_scene_output *scene_output =
		wl_container_of(addon, scene_output, addon);
	return scene_output;
}

void sway_scene_output_set_position(struct sway_scene_output *scene_output,
		int lx, int ly) {
	if (scene_output->x == lx && scene_output->y == ly) {
		return;
	}

	scene_output->x = lx;
	scene_output->y = ly;

	scene_output_update_geometry(scene_output, false);
}

static bool scene_node_invisible(struct sway_scene_node *node) {
	if (node->type == SWAY_SCENE_NODE_TREE) {
		return true;
	} else if (node->type == SWAY_SCENE_NODE_RECT) {
		struct sway_scene_rect *rect = sway_scene_rect_from_node(node);

		return rect->color[3] == 0.f;
	} else if (node->type == SWAY_SCENE_NODE_BUFFER) {
		struct sway_scene_buffer *buffer = sway_scene_buffer_from_node(node);

		return buffer->buffer == NULL && buffer->texture == NULL;
	} else if (node->type == SWAY_SCENE_NODE_DECORATION) {
		struct sway_scene_decoration *decoration = sway_scene_decoration_from_node(node);
		bool title_visible = decoration->title_bar && decoration->title_bar_color[3] != 0.f;
		bool border_visible;
		if (decoration->border) {
			if (decoration->border_width <= 0.0 ||
				(decoration->border_top_color[3] == 0.f && decoration->border_bottom_color[3] == 0.f &&
				 decoration->border_left_color[3] == 0.f && decoration->border_right_color[3] == 0.f)) {
				border_visible = false;
			} else {
				border_visible = true;
			}
		} else {
			border_visible = false;
		}
		if (!title_visible && !border_visible) {
			return true;
		}
	} else if (node->type == SWAY_SCENE_NODE_SHADOW) {
		struct sway_scene_shadow *shadow = sway_scene_shadow_from_node(node);
		if (shadow->color[3] == 0.f || !shadow->enabled) {
			return true;
		}
	}

	return false;
}

struct render_list_constructor_data {
	struct wlr_box box;
	struct wl_array *render_list;
	bool calculate_visibility;
	bool highlight_transparent_region;
	bool fractional_scale;
};

static bool scene_buffer_is_black_opaque(struct sway_scene_buffer *scene_buffer) {
	return scene_buffer->is_single_pixel_buffer &&
		scene_buffer->single_pixel_buffer_color[0] == 0 &&
		scene_buffer->single_pixel_buffer_color[1] == 0 &&
		scene_buffer->single_pixel_buffer_color[2] == 0 &&
		scene_buffer->single_pixel_buffer_color[3] == UINT32_MAX &&
		scene_buffer->opacity == 1.0;
}

static bool construct_render_list_iterator(struct sway_scene_node *node,
		double lx, double ly, void *_data) {
	struct render_list_constructor_data *data = _data;

	if (scene_node_invisible(node)) {
		return false;
	}

	// While rendering, the background should always be black. If we see a
	// black rect, we can ignore rendering everything under the rect, and
	// unless fractional scale is used even the rect itself (to avoid running
	// into issues regarding damage region expansion).
	if (node->type == SWAY_SCENE_NODE_RECT && data->calculate_visibility &&
			(!data->fractional_scale || data->render_list->size == 0)) {
		struct sway_scene_rect *rect = sway_scene_rect_from_node(node);
		float *black = (float[4]){ 0.f, 0.f, 0.f, 1.f };

		if (memcmp(rect->color, black, sizeof(float) * 4) == 0) {
			return false;
		}
	}

	// Apply the same special-case to black opaque single-pixel buffers
	if (node->type == SWAY_SCENE_NODE_BUFFER && data->calculate_visibility &&
			(!data->fractional_scale || data->render_list->size == 0)) {
		struct sway_scene_buffer *scene_buffer = sway_scene_buffer_from_node(node);

		if (scene_buffer_is_black_opaque(scene_buffer)) {
			return false;
		}
	}

	pixman_region32_t intersection;
	pixman_region32_init(&intersection);
	pixman_region32_intersect_rect(&intersection, &node->visible,
			data->box.x, data->box.y,
			round(data->box.width), round(data->box.height));
	if (pixman_region32_empty(&intersection)) {
		pixman_region32_fini(&intersection);
		return false;
	}

	pixman_region32_fini(&intersection);

	struct render_list_entry *entry = wl_array_add(data->render_list, sizeof(*entry));
	if (!entry) {
		return false;
	}

	*entry = (struct render_list_entry){
		.node = node,
		.x = lx,
		.y = ly,
		.highlight_transparent_region = data->highlight_transparent_region,
	};

	return false;
}

static void scene_buffer_send_dmabuf_feedback(const struct sway_scene *scene,
		struct sway_scene_buffer *scene_buffer,
		const struct wlr_linux_dmabuf_feedback_v1_init_options *options) {
	if (!scene->linux_dmabuf_v1) {
		return;
	}

	struct sway_scene_surface *surface = sway_scene_surface_try_from_buffer(scene_buffer);
	if (!surface) {
		return;
	}

	// compare to the previous options so that we don't send
	// duplicate feedback events.
	if (memcmp(options, &scene_buffer->prev_feedback_options, sizeof(*options)) == 0) {
		return;
	}

	scene_buffer->prev_feedback_options = *options;

	struct wlr_linux_dmabuf_feedback_v1 feedback = {0};
	if (!wlr_linux_dmabuf_feedback_v1_init_with_options(&feedback, options)) {
		return;
	}

	enum wl_output_transform preferred_buffer_transform = WL_OUTPUT_TRANSFORM_NORMAL;
	if (options->scanout_primary_output != NULL) {
		preferred_buffer_transform = options->scanout_primary_output->transform;
	}

	// TODO: also send wl_surface.preferred_buffer_transform when running with
	// pure software rendering
	wlr_surface_set_preferred_buffer_transform(surface->surface, preferred_buffer_transform);
	wlr_linux_dmabuf_v1_set_surface_feedback(scene->linux_dmabuf_v1,
		surface->surface, &feedback);

	wlr_linux_dmabuf_feedback_v1_finish(&feedback);
}

static bool color_management_is_scanout_allowed(const struct wlr_output_image_description *img_desc,
		const struct sway_scene_buffer *buffer) {
	// Disallow scanout if the output has colorimetry information but buffer
	// doesn't; allow it only if the output also lacks it.
	if (buffer->transfer_function == 0 && buffer->primaries == 0) {
		return img_desc == NULL;
	}

	// If the output has colorimetry information, the buffer must match it for
	// direct scanout to be allowed.
	if (img_desc != NULL) {
		return img_desc->transfer_function == buffer->transfer_function &&
				img_desc->primaries == buffer->primaries;
	}
	// If the output doesn't have colorimetry image description set, we can only
	// scan out buffers with default colorimetry (gamma2.2 transfer and sRGB
	// primaries) used in wlroots.
	return buffer->transfer_function == WLR_COLOR_TRANSFER_FUNCTION_GAMMA22 &&
			buffer->primaries == WLR_COLOR_NAMED_PRIMARIES_SRGB;
}

enum scene_direct_scanout_result {
	// This scene node is not a candidate for scanout
	SCANOUT_INELIGIBLE,

	// This scene node is a candidate for scanout, but is currently
	// incompatible due to e.g. buffer mismatch, and if possible we'd like to
	// resolve this incompatibility.
	SCANOUT_CANDIDATE,

	// Scanout is successful.
	SCANOUT_SUCCESS,
};

static enum scene_direct_scanout_result scene_entry_try_direct_scanout(
		struct render_list_entry *entry, struct wlr_output_state *state,
		const struct render_data *data) {
	struct sway_scene_output *scene_output = data->output;
	struct sway_scene_node *node = entry->node;

	if (root->fullscreen_global) {
		return SCANOUT_INELIGIBLE;
	}

	if (!scene_output->scene->direct_scanout) {
		return SCANOUT_INELIGIBLE;
	}

	if (node->type != SWAY_SCENE_NODE_BUFFER) {
		return SCANOUT_INELIGIBLE;
	}

	if (state->committed & (WLR_OUTPUT_STATE_MODE |
			WLR_OUTPUT_STATE_ENABLED |
			WLR_OUTPUT_STATE_RENDER_FORMAT)) {
		// Legacy DRM will explode if we try to modeset with a direct scanout buffer
		return SCANOUT_INELIGIBLE;
	}

	if (!wlr_output_is_direct_scanout_allowed(scene_output->output)) {
		return SCANOUT_INELIGIBLE;
	}

	struct sway_scene_buffer *buffer = sway_scene_buffer_from_node(node);
	if (buffer->buffer == NULL) {
		return SCANOUT_INELIGIBLE;
	}

	// The native size of the buffer after any transform is applied
	int default_width = buffer->buffer->width;
	int default_height = buffer->buffer->height;
	wlr_output_transform_coords(buffer->transform, &default_width, &default_height);
	struct wlr_fbox default_box = {
		.width = default_width,
		.height = default_height,
	};

	if (buffer->transform != data->transform) {
		return SCANOUT_INELIGIBLE;
	}

	const struct wlr_output_image_description *img_desc = scene_output_pending_image_description(scene_output->output, state);
	if (!color_management_is_scanout_allowed(img_desc, buffer)) {
		return SCANOUT_INELIGIBLE;
	}

	if (buffer->color_encoding != WLR_COLOR_ENCODING_NONE ||
			buffer->color_range != WLR_COLOR_RANGE_NONE) {
		return SCANOUT_INELIGIBLE;
	}

	// We want to ensure optimal buffer selection, but as direct-scanout can be enabled and disabled
	// on a frame-by-frame basis, we wait for a few frames to send the new format recommendations.
	// Maybe we should only send feedback in this case if tests fail.
	if (scene_output->dmabuf_feedback_debounce >= DMABUF_FEEDBACK_DEBOUNCE_FRAMES
			&& buffer->primary_output == scene_output) {
		struct wlr_linux_dmabuf_feedback_v1_init_options options = {
			.main_renderer = scene_output->output->renderer,
			.scanout_primary_output = scene_output->output,
		};

		scene_buffer_send_dmabuf_feedback(scene_output->scene, buffer, &options);
	}

	struct wlr_output_state pending;
	wlr_output_state_init(&pending);
	if (!wlr_output_state_copy(&pending, state)) {
		return SCANOUT_CANDIDATE;
	}

	if (!wlr_fbox_empty(&buffer->src_box) &&
			!wlr_fbox_equal(&buffer->src_box, &default_box)) {
		pending.buffer_src_box = buffer->src_box;
	}

	// Translate the position from scene coordinates to output coordinates
	struct wlr_fbox buffer_dst_box = {
		.x = entry->x - scene_output->x,
		.y = entry->y - scene_output->y
	};
	scene_node_get_size(node, &buffer_dst_box.width, &buffer_dst_box.height);
	transform_output_box(&buffer_dst_box, data);
	pending.buffer_dst_box.x = round(buffer_dst_box.x);
	pending.buffer_dst_box.y = round(buffer_dst_box.y);
	pending.buffer_dst_box.width = round(buffer_dst_box.x + buffer_dst_box.width) - round(buffer_dst_box.x);
	pending.buffer_dst_box.height = round(buffer_dst_box.y + buffer_dst_box.height) - round (buffer_dst_box.y);

	struct wlr_buffer *wlr_buffer = buffer->buffer;
	struct wlr_client_buffer *client_buffer = wlr_client_buffer_get(wlr_buffer);
	if (client_buffer != NULL && client_buffer->source != NULL && client_buffer->source->n_locks > 0) {
		wlr_buffer = client_buffer->source;
	}

	wlr_output_state_set_buffer(&pending, wlr_buffer);
	if (buffer->wait_timeline != NULL) {
		wlr_output_state_set_wait_timeline(&pending, buffer->wait_timeline, buffer->wait_point);
	}
	if (!wlr_output_test_state(scene_output->output, &pending)) {
		wlr_output_state_finish(&pending);
		return SCANOUT_CANDIDATE;
	}

	wlr_output_state_copy(state, &pending);
	wlr_output_state_finish(&pending);

	struct sway_scene_output_sample_event sample_event = {
		.output = scene_output,
		.direct_scanout = true,
	};
	wl_signal_emit_mutable(&buffer->events.output_sample, &sample_event);
	return SCANOUT_SUCCESS;
}

bool sway_scene_output_needs_frame(struct sway_scene_output *scene_output) {
	return scene_output->output->needs_frame ||
		!pixman_region32_empty(&scene_output->pending_commit_damage) ||
		scene_output->gamma_lut_changed;
}

bool sway_scene_output_commit(struct sway_scene_output *scene_output,
		const struct sway_scene_output_state_options *options) {
	if (!sway_scene_output_needs_frame(scene_output)) {
		return true;
	}

	bool ok = false;
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	if (!sway_scene_output_build_state(scene_output, &state, options)) {
		goto out;
	}

	ok = wlr_output_commit_state(scene_output->output, &state);
	if (!ok) {
		goto out;
	}

out:
	wlr_output_state_finish(&state);
	return ok;
}

static void scene_output_state_attempt_gamma(struct sway_scene_output *scene_output,
		struct wlr_output_state *state) {
	if (!scene_output->gamma_lut_changed) {
		return;
	}

	struct wlr_output_state gamma_pending = {0};
	if (!wlr_output_state_copy(&gamma_pending, state)) {
		return;
	}

	wlr_output_state_set_color_transform(&gamma_pending, scene_output->gamma_lut_color_transform);
	scene_output->gamma_lut_changed = false;

	if (!wlr_output_test_state(scene_output->output, &gamma_pending)) {
		wlr_gamma_control_v1_send_failed_and_destroy(scene_output->gamma_lut);

		scene_output->gamma_lut = NULL;
		wlr_color_transform_unref(scene_output->gamma_lut_color_transform);
		scene_output->gamma_lut_color_transform = NULL;
		wlr_output_state_finish(&gamma_pending);
		return;
	}

	wlr_output_state_copy(state, &gamma_pending);
	wlr_output_state_finish(&gamma_pending);
}

static bool scene_output_combine_color_transforms(
		struct sway_scene_output *scene_output, struct wlr_color_transform *supplied,
		const struct wlr_output_image_description *img_desc, bool render_gamma_lut) {
	struct wlr_color_transform *transforms[3] = {0};
	const size_t transforms_cap = sizeof(transforms) / sizeof(transforms[0]);
	size_t transforms_len = 0;

	if (img_desc != NULL) {
		assert(supplied == NULL);
		struct wlr_color_primaries primaries_srgb;
		wlr_color_primaries_from_named(&primaries_srgb, WLR_COLOR_NAMED_PRIMARIES_SRGB);
		struct wlr_color_primaries primaries;
		wlr_color_primaries_from_named(&primaries, img_desc->primaries);
		float matrix[9];
		wlr_color_primaries_transform_absolute_colorimetric(&primaries_srgb, &primaries, matrix);
		assert(transforms_len < transforms_cap);
		transforms[transforms_len++] =	wlr_color_transform_init_matrix(matrix);
		assert(transforms_len < transforms_cap);
		transforms[transforms_len++] = wlr_color_transform_init_linear_to_inverse_eotf(
			img_desc->transfer_function);
	} else if (supplied != NULL) {
		assert(transforms_len < transforms_cap);
		transforms[transforms_len++] = wlr_color_transform_ref(supplied);
	} else {
		assert(transforms_len < transforms_cap);
		transforms[transforms_len++] = wlr_color_transform_init_linear_to_inverse_eotf(
			WLR_COLOR_TRANSFER_FUNCTION_GAMMA22);
	}

	struct wlr_color_transform *gamma_lut = scene_output->gamma_lut_color_transform;
	if (gamma_lut != NULL && render_gamma_lut) {
		assert(transforms_len < transforms_cap);
		transforms[transforms_len++] = wlr_color_transform_ref(gamma_lut);
	}

	for (size_t i = 0; i < transforms_len; ++i) {
		if (transforms[i] == NULL) {
			goto err_transforms;
		}
	}
	struct wlr_color_transform *combined;
	if (transforms_len == 1) {
		combined = wlr_color_transform_ref(transforms[0]);
	} else {
		combined = wlr_color_transform_init_pipeline(transforms, transforms_len);
	}
	if (combined == NULL) {
		goto err_transforms;
	}
	for (size_t i = 0; i < transforms_len; ++i) {
		wlr_color_transform_unref(transforms[i]);
	}

	wlr_color_transform_unref(scene_output->prev_gamma_lut_color_transform);
	scene_output->prev_gamma_lut_color_transform = gamma_lut ? wlr_color_transform_ref(gamma_lut) : NULL;
	wlr_color_transform_unref(scene_output->prev_supplied_color_transform);
	scene_output->prev_supplied_color_transform = supplied ? wlr_color_transform_ref(supplied) : NULL;
	wlr_color_transform_unref(scene_output->combined_color_transform);
	scene_output->combined_color_transform = combined;

	return true;

err_transforms:
	for (size_t i = 0; i < transforms_len; ++i) {
		wlr_color_transform_unref(transforms[i]);
	}
	return false;
}

bool sway_scene_output_build_state(struct sway_scene_output *scene_output,
		struct wlr_output_state *state, const struct sway_scene_output_state_options *options) {
	struct sway_scene_output_state_options default_options = {0};
	if (!options) {
		options = &default_options;
	}
	struct sway_scene_timer *timer = options->timer;
	struct timespec start_time;
	if (timer) {
		clock_gettime(CLOCK_MONOTONIC, &start_time);
		sway_scene_timer_finish(timer);
		*timer = (struct sway_scene_timer){0};
	}

	if ((state->committed & WLR_OUTPUT_STATE_ENABLED) && !state->enabled) {
		// if the state is being disabled, do nothing.
		return true;
	}

	if (animation_animating(scene_output->output)) {
		animation_animate(scene_output->output);
	}

	struct wlr_output *output = scene_output->output;
	enum sway_scene_debug_damage_option debug_damage =
		scene_output->scene->debug_damage_option;

	bool render_gamma_lut = false;
	if (wlr_output_get_gamma_size(output) == 0 && output->renderer->features.output_color_transform) {
		if (scene_output->gamma_lut_color_transform != scene_output->prev_gamma_lut_color_transform) {
			scene_output_damage_whole(scene_output);
		}
		if (scene_output->gamma_lut_color_transform != NULL) {
			render_gamma_lut = true;
		}
	}

	struct render_data render_data = {
		.transform = output->transform,
		.scale = output->scale,
		.logical = { .x = scene_output->x, .y = scene_output->y },
		.output = scene_output,
	};

	int resolution_width, resolution_height;
	scene_output_pending_resolution(output, state,
		&resolution_width, &resolution_height);

	if (state->committed & WLR_OUTPUT_STATE_TRANSFORM) {
		if (render_data.transform != state->transform) {
			scene_output_damage_whole(scene_output);
		}

		render_data.transform = state->transform;
	}

	if (state->committed & WLR_OUTPUT_STATE_SCALE) {
		if (render_data.scale != state->scale) {
			scene_output_damage_whole(scene_output);
		}

		render_data.scale = state->scale;
	}

	render_data.trans_width = resolution_width;
	render_data.trans_height = resolution_height;
	wlr_output_transform_coords(render_data.transform,
		&render_data.trans_width, &render_data.trans_height);

	render_data.logical.width = render_data.trans_width / render_data.scale;
	render_data.logical.height = render_data.trans_height / render_data.scale;

	struct render_list_constructor_data list_con = {
		.box = render_data.logical,
		.render_list = &scene_output->render_list,
		.calculate_visibility = scene_output->scene->calculate_visibility,
		.highlight_transparent_region = scene_output->scene->highlight_transparent_region,
		.fractional_scale = floor(render_data.scale) != render_data.scale,
	};

	list_con.render_list->size = 0;
	scene_nodes_in_box(&scene_output->scene->tree.node, &list_con.box,
		construct_render_list_iterator, &list_con);
	array_realloc(list_con.render_list, list_con.render_list->size);

	struct render_list_entry *list_data = list_con.render_list->data;
	int list_len = list_con.render_list->size / sizeof(*list_data);

	if (debug_damage == SWAY_SCENE_DEBUG_DAMAGE_RERENDER) {
		scene_output_damage_whole(scene_output);
	}

	struct timespec now;
	if (debug_damage == SWAY_SCENE_DEBUG_DAMAGE_HIGHLIGHT) {
		struct wl_list *regions = &scene_output->damage_highlight_regions;
		clock_gettime(CLOCK_MONOTONIC, &now);

		// add the current frame's damage if there is damage
		if (!pixman_region32_empty(&scene_output->damage_ring.current)) {
			struct highlight_region *current_damage = calloc(1, sizeof(*current_damage));
			if (current_damage) {
				pixman_region32_init(&current_damage->region);
				pixman_region32_copy(&current_damage->region,
					&scene_output->damage_ring.current);
				current_damage->when = now;
				wl_list_insert(regions, &current_damage->link);
			}
		}

		pixman_region32_t acc_damage;
		pixman_region32_init(&acc_damage);
		struct highlight_region *damage, *tmp_damage;
		wl_list_for_each_safe(damage, tmp_damage, regions, link) {
			// remove overlaping damage regions
			pixman_region32_subtract(&damage->region, &damage->region, &acc_damage);
			pixman_region32_union(&acc_damage, &acc_damage, &damage->region);

			// if this damage is too old or has nothing in it, get rid of it
			struct timespec time_diff;
			timespec_sub(&time_diff, &now, &damage->when);
			if (timespec_to_msec(&time_diff) >= HIGHLIGHT_DAMAGE_FADEOUT_TIME ||
					pixman_region32_empty(&damage->region)) {
				highlight_region_destroy(damage);
			}
		}

		scene_output_damage(scene_output, &acc_damage);
		pixman_region32_fini(&acc_damage);
	}

	wlr_output_state_set_damage(state, &scene_output->pending_commit_damage);

	// We only want to try direct scanout if:
	// - There is only one entry in the render list
	// - There are no color transforms that need to be applied
	// - Damage highlight debugging is not enabled
	enum scene_direct_scanout_result scanout_result = SCANOUT_INELIGIBLE;
	if (options->color_transform == NULL && !render_gamma_lut && list_len == 1
			&& debug_damage != SWAY_SCENE_DEBUG_DAMAGE_HIGHLIGHT) {
		scanout_result = scene_entry_try_direct_scanout(&list_data[0], state, &render_data);
	}

	if (scanout_result == SCANOUT_INELIGIBLE) {
		if (scene_output->dmabuf_feedback_debounce > 0) {
			// We cannot scan out, so count down towards sending composition dmabuf feedback
			scene_output->dmabuf_feedback_debounce--;
		}
	} else if (scene_output->dmabuf_feedback_debounce < DMABUF_FEEDBACK_DEBOUNCE_FRAMES) {
		// We either want to scan out or successfully scanned out, so count up towards sending
		// scanout dmabuf feedback
		scene_output->dmabuf_feedback_debounce++;
	}

	bool scanout = scanout_result == SCANOUT_SUCCESS;
	if (scene_output->prev_scanout != scanout) {
		scene_output->prev_scanout = scanout;
		sway_log(SWAY_DEBUG, "Direct scan-out %s",
			scanout ? "enabled" : "disabled");
	}

	if (scanout) {
		scene_output_state_attempt_gamma(scene_output, state);

		if (timer) {
			struct timespec end_time, duration;
			clock_gettime(CLOCK_MONOTONIC, &end_time);
			timespec_sub(&duration, &end_time, &start_time);
			timer->pre_render_duration = timespec_to_nsec(&duration);
		}
		return true;
	}

	struct wlr_swapchain *swapchain = options->swapchain;
	if (!swapchain) {
		if (!wlr_output_configure_primary_swapchain(output, state, &output->swapchain)) {
			return false;
		}

		swapchain = output->swapchain;
	}

	struct wlr_buffer *buffer = wlr_swapchain_acquire(swapchain);
	if (buffer == NULL) {
		return false;
	}

	assert(buffer->width == resolution_width && buffer->height == resolution_height);

	if (timer) {
		timer->render_timer = wlr_render_timer_create(output->renderer);

		struct timespec end_time, duration;
		clock_gettime(CLOCK_MONOTONIC, &end_time);
		timespec_sub(&duration, &end_time, &start_time);
		timer->pre_render_duration = timespec_to_nsec(&duration);
	}

	if ((render_gamma_lut
			&& scene_output->gamma_lut_color_transform != scene_output->prev_gamma_lut_color_transform)
			|| scene_output->prev_supplied_color_transform != options->color_transform
			|| (state->committed & WLR_OUTPUT_STATE_IMAGE_DESCRIPTION)) {
		const struct wlr_output_image_description *output_description =
			output_pending_image_description(output, state);
		if (!scene_output_combine_color_transforms(scene_output, options->color_transform,
				output_description, render_gamma_lut)) {
			wlr_buffer_unlock(buffer);
			return false;
		}
	}

	scene_output->in_point++;
	struct wlr_render_pass *render_pass = wlr_renderer_begin_buffer_pass(output->renderer, buffer,
			&(struct wlr_buffer_pass_options){
		.timer = timer ? timer->render_timer : NULL,
		.color_transform = scene_output->combined_color_transform,
		.signal_timeline = scene_output->in_timeline,
		.signal_point = scene_output->in_point,
	});
	if (render_pass == NULL) {
		wlr_buffer_unlock(buffer);
		return false;
	}

	render_data.render_pass = render_pass;

	pixman_region32_init(&render_data.damage);
	wlr_damage_ring_rotate_buffer(&scene_output->damage_ring, buffer,
		&render_data.damage);

	pixman_region32_t background;
	pixman_region32_init(&background);
	pixman_region32_copy(&background, &render_data.damage);

	// Cull areas of the background that are occluded by opaque regions of
	// scene nodes above. Those scene nodes will just render atop having us
	// never see the background.
	if (scene_output->scene->calculate_visibility) {
		for (int i = list_len - 1; i >= 0; i--) {
			struct render_list_entry *entry = &list_data[i];

			if (layout_overview_workspaces_enabled() && scene_node_get_background(entry->node)) {
				// If we are in workspaces overview and the node is the wallpaper
				// (layer_shell ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND), we want to
				// reset the calculated visibility to avoid having remains of the
				// un-scaled nodes that are part of the workspace.
				pixman_region32_fini(&entry->node->visible);
				pixman_region32_init_rect(&entry->node->visible, render_data.logical.x,
					render_data.logical.y, render_data.logical.width, render_data.logical.height);
			}
			if (!layout_overview_workspaces_enabled()) {
				// We must only cull opaque regions that are visible by the node.
				// The node's visibility will have the knowledge of a black rect
				// that may have been omitted from the render list via the black
				// rect optimization. In order to ensure we don't cull background
				// rendering in that black rect region, consider the node's visibility.
				pixman_region32_t opaque;
				pixman_region32_init(&opaque);
				scene_node_opaque_region(entry->node, entry->x, entry->y, &opaque);
				pixman_region32_intersect(&opaque, &opaque, &entry->node->visible);

				pixman_region32_translate(&opaque, -scene_output->x, -scene_output->y);
				logical_to_buffer_coords(&opaque, &render_data, false);
				pixman_region32_subtract(&background, &background, &opaque);
				pixman_region32_fini(&opaque);
			}
		}

		if (floor(render_data.scale) != render_data.scale) {
			wlr_region_expand(&background, &background, 1);

			// reintersect with the damage because we never want to render
			// outside of the damage region
			pixman_region32_intersect(&background, &background, &render_data.damage);
		}
	}

	wlr_render_pass_add_rect(render_pass, &(struct wlr_render_rect_options){
		.box = { .width = buffer->width, .height = buffer->height },
		.color = { .r = 0, .g = 0, .b = 0, .a = 1 },
		.clip = &background,
	});
	pixman_region32_fini(&background);

	for (int i = list_len - 1; i >= 0; i--) {
		struct render_list_entry *entry = &list_data[i];
		scene_entry_render(entry, &render_data);

		if (entry->node->type == SWAY_SCENE_NODE_BUFFER) {
			struct sway_scene_buffer *buffer = sway_scene_buffer_from_node(entry->node);

			// Direct scanout counts up to DMABUF_FEEDBACK_DEBOUNCE_FRAMES before sending new dmabuf
			// feedback, and on composition we wait until it hits zero again. If we knew that an
			// entry could never be a scanout candidate, we could send feedback to it
			// unconditionally without debounce, but for now it is all or nothing
			if (scene_output->dmabuf_feedback_debounce == 0 && buffer->primary_output == scene_output) {
				struct wlr_linux_dmabuf_feedback_v1_init_options options = {
					.main_renderer = output->renderer,
					.scanout_primary_output = NULL,
				};

				scene_buffer_send_dmabuf_feedback(scene_output->scene, buffer, &options);
			}
		}
	}

	if (debug_damage == SWAY_SCENE_DEBUG_DAMAGE_HIGHLIGHT) {
		struct highlight_region *damage;
		wl_list_for_each(damage, &scene_output->damage_highlight_regions, link) {
			struct timespec time_diff;
			timespec_sub(&time_diff, &now, &damage->when);
			int64_t time_diff_ms = timespec_to_msec(&time_diff);
			float alpha = 1.0 - (double)time_diff_ms / HIGHLIGHT_DAMAGE_FADEOUT_TIME;

			wlr_render_pass_add_rect(render_pass, &(struct wlr_render_rect_options){
				.box = { .width = buffer->width, .height = buffer->height },
				.color = { .r = alpha * 0.5, .g = 0, .b = 0, .a = alpha * 0.5 },
				.clip = &damage->region,
			});
		}
	}

	wlr_output_add_software_cursors_to_render_pass(output, render_pass, &render_data.damage);
	pixman_region32_fini(&render_data.damage);

	if (!wlr_render_pass_submit(render_pass)) {
		wlr_buffer_unlock(buffer);

		// if we failed to render the buffer, it will have undefined contents
		// Trash the damage ring
		wlr_damage_ring_add_whole(&scene_output->damage_ring);
		return false;
	}

	wlr_output_state_set_buffer(state, buffer);
	wlr_buffer_unlock(buffer);

	if (scene_output->in_timeline != NULL) {
		wlr_output_state_set_wait_timeline(state, scene_output->in_timeline,
			scene_output->in_point);
	}

	if (!render_gamma_lut) {
		scene_output_state_attempt_gamma(scene_output, state);
	}

	return true;
}

int64_t sway_scene_timer_get_duration_ns(struct sway_scene_timer *timer) {
	int64_t pre_render = timer->pre_render_duration;
	if (!timer->render_timer) {
		return pre_render;
	}
	int64_t render = wlr_render_timer_get_duration_ns(timer->render_timer);
	return render != -1 ? pre_render + render : -1;
}

void sway_scene_timer_finish(struct sway_scene_timer *timer) {
	if (timer->render_timer) {
		wlr_render_timer_destroy(timer->render_timer);
	}
}

static void scene_node_send_frame_done(struct sway_scene_node *node,
		struct sway_scene_output *scene_output, struct timespec *now) {
	if (!node->enabled) {
		return;
	}

	if (node->type == SWAY_SCENE_NODE_BUFFER) {
		struct sway_scene_buffer *scene_buffer =
			sway_scene_buffer_from_node(node);

		struct sway_scene_frame_done_event event = {
			.output = scene_output,
			.when = *now,
		};
		sway_scene_buffer_send_frame_done(scene_buffer, &event);
	} else if (node->type == SWAY_SCENE_NODE_TREE) {
		struct sway_scene_tree *scene_tree = sway_scene_tree_from_node(node);
		struct sway_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_node_send_frame_done(child, scene_output, now);
		}
	}
}

void sway_scene_output_send_frame_done(struct sway_scene_output *scene_output,
		struct timespec *now) {
	scene_node_send_frame_done(&scene_output->scene->tree.node,
		scene_output, now);
}

static void scene_output_for_each_scene_buffer(const struct wlr_box *output_box,
		struct sway_scene_node *node, double lx, double ly,
		sway_scene_buffer_iterator_func_t user_iterator, void *user_data) {
	if (!node->enabled) {
		return;
	}

	lx += node->x;
	ly += node->y;

	if (node->type == SWAY_SCENE_NODE_BUFFER) {
		double width, height;
		scene_node_get_size(node, &width, &height);
		struct wlr_box node_box = {
			.x = round(lx),
			.y = round(ly),
			.width = round(lx + width) - round(lx),
			.height = round(ly + height) - round(ly)
		};

		struct wlr_box intersection;
		if (wlr_box_intersection(&intersection, output_box, &node_box)) {
			struct sway_scene_buffer *scene_buffer =
				sway_scene_buffer_from_node(node);
			user_iterator(scene_buffer, lx, ly, user_data);
		}
	} else if (node->type == SWAY_SCENE_NODE_TREE) {
		struct sway_scene_tree *scene_tree = sway_scene_tree_from_node(node);
		struct sway_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_output_for_each_scene_buffer(output_box, child, lx, ly,
				user_iterator, user_data);
		}
	}
}

void sway_scene_output_for_each_buffer(struct sway_scene_output *scene_output,
		sway_scene_buffer_iterator_func_t iterator, void *user_data) {
	struct wlr_box box = { .x = scene_output->x, .y = scene_output->y };
	wlr_output_effective_resolution(scene_output->output,
		&box.width, &box.height);
	scene_output_for_each_scene_buffer(&box, &scene_output->scene->tree.node, 0, 0,
		iterator, user_data);
}

bool scene_node_get_parent_total_scale(struct sway_scene_node *node, double *scale) {
	struct sway_scene_tree *tree;
	if (node->type == SWAY_SCENE_NODE_TREE) {
		tree = sway_scene_tree_from_node(node);
	} else {
		tree = node->parent;
	}

	while (tree) {
		// Check scene descriptor
		struct sway_view *view = scene_descriptor_try_get(&tree->node,SWAY_SCENE_DESC_VIEW);
		if (view && view->container) {
			*scale = view_get_total_scale(view);
			return &tree->node == node;
		}
		struct sway_popup_desc *desc = scene_descriptor_try_get(&tree->node,SWAY_SCENE_DESC_POPUP);
		if (desc && desc->view) {
			*scale = view_get_total_scale(desc->view);
			return &tree->node == node;
		}
		tree = tree->node.parent;
	}
	if (node->type == SWAY_SCENE_NODE_BUFFER) {
		struct sway_scene_buffer *scene_buffer = sway_scene_buffer_from_node(node);
		double total_scale = -1.0;
		struct sway_scene_surface *scene_surface =
			sway_scene_surface_try_from_buffer(scene_buffer);

		if (scene_surface) {
			struct sway_view *view = view_from_wlr_surface(scene_surface->surface);
			if (view) {
				total_scale = view_get_total_scale(view);
			}
		}
		*scale = total_scale;
		return true;
	}
	*scale = -1.0;
	return false;
}
