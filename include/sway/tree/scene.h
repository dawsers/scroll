/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef _SWAY_SCENE_H
#define _SWAY_SCENE_H

/**
 * The scene-graph API provides a declarative way to display surfaces. The
 * compositor creates a scene, adds surfaces, then renders the scene on
 * outputs.
 *
 * The scene-graph API only supports basic 2D composition operations (like the
 * KMS API or the Wayland protocol does). For anything more complicated,
 * compositors need to implement custom rendering logic.
 */

#include <pixman.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>

struct wlr_output;
struct wlr_output_layout;
struct wlr_output_layout_output;
struct wlr_xdg_surface;
struct wlr_layer_surface_v1;
struct wlr_drag_icon;
struct wlr_surface;

struct sway_scene_node;
struct sway_scene_buffer;
struct sway_scene_output_layout;

struct wlr_presentation;
struct wlr_linux_dmabuf_v1;
struct wlr_gamma_control_manager_v1;
struct wlr_color_manager_v1;
struct wlr_output_state;

typedef bool (*sway_scene_buffer_point_accepts_input_func_t)(
	struct sway_scene_buffer *buffer, double *sx, double *sy);

typedef void (*sway_scene_buffer_iterator_func_t)(
	struct sway_scene_buffer *buffer, int sx, int sy, void *user_data);

enum sway_scene_node_type {
	SWAY_SCENE_NODE_TREE,
	SWAY_SCENE_NODE_RECT,
	SWAY_SCENE_NODE_BUFFER,
	SWAY_SCENE_NODE_DECORATION,
	SWAY_SCENE_NODE_SHADOW,
};

struct sway_scene_node_info {
	double scale;			// scale for everything below
	struct wlr_output *wlr_output;	// wlr_output the node belongs to (if tiled, otherwise NULL)
	struct sway_workspace *workspace;
	struct wlr_box *output_box;
	bool background;	// bakground layer shell, usually the wallpaper
};

/** A node is an object in the scene. */
struct sway_scene_node {
	enum sway_scene_node_type type;
	struct sway_scene_tree *parent;
	struct sway_scene_tree *old_parent;  // for full screen focusing

	struct wl_list link; // sway_scene_tree.children

	bool enabled;
	double x, y; // relative to parent

	struct {
		struct wl_signal destroy;
	} events;

	void *data;

	struct wlr_addon_set addons;

	struct {
		pixman_region32_t visible;
	};

	struct sway_scene_node_info info;
};

enum sway_scene_debug_damage_option {
	SWAY_SCENE_DEBUG_DAMAGE_NONE,
	SWAY_SCENE_DEBUG_DAMAGE_RERENDER,
	SWAY_SCENE_DEBUG_DAMAGE_HIGHLIGHT
};

/** A sub-tree in the scene-graph. */
struct sway_scene_tree {
	struct sway_scene_node node;

	struct wl_list children; // sway_scene_node.link
};

/** The root scene-graph node. */
struct sway_scene {
	struct sway_scene_tree tree;

	struct wl_list outputs; // sway_scene_output.link

	// May be NULL
	struct wlr_linux_dmabuf_v1 *linux_dmabuf_v1;
	struct wlr_gamma_control_manager_v1 *gamma_control_manager_v1;
	struct wlr_color_manager_v1 *color_manager_v1;

	struct {
		struct wl_listener linux_dmabuf_v1_destroy;
		struct wl_listener gamma_control_manager_v1_destroy;
		struct wl_listener gamma_control_manager_v1_set_gamma;
		struct wl_listener color_manager_v1_destroy;

		enum sway_scene_debug_damage_option debug_damage_option;
		bool direct_scanout;
		bool calculate_visibility;
		bool highlight_transparent_region;
	};
};

/** A scene-graph node displaying a single surface. */
struct sway_scene_surface {
	struct sway_scene_buffer *buffer;
	struct wlr_surface *surface;

	struct {
		struct wlr_box clip;

		// Output used for frame pacing (surface frame callbacks, presentation
		// time feedback, etc), may be NULL
		struct wlr_output *frame_pacing_output;

		struct wlr_addon addon;

		struct wl_listener outputs_update;
		struct wl_listener output_enter;
		struct wl_listener output_leave;
		struct wl_listener output_sample;
		struct wl_listener frame_done;
		struct wl_listener surface_destroy;
		struct wl_listener surface_commit;
	};
};

/** A scene-graph node displaying a solid-colored rectangle */
struct sway_scene_rect {
	struct sway_scene_node node;
	double width, height;
	float color[4];
};

struct sway_scene_decoration {
	struct sway_scene_node node;
	struct sway_view *view;
	struct wlr_object *wlr_object;
	double width, height;
	bool title_bar;
	double title_bar_height;
	double title_bar_border_radius;
	float title_bar_color[4];
	bool border;
	double border_radius;
	double border_width;
	float border_top_color[4], border_bottom_color[4], border_left_color[4], border_right_color[4];
	bool dim;
	float dim_color[4];
};

struct sway_scene_shadow {
	struct sway_scene_node node;
	struct sway_scene_decoration *decoration;
	struct wlr_object *wlr_object;
	double width, height;
	bool enabled;
	bool dynamic;
	double size;
	double blur;
	double offset[2];
	float color[4];
};

struct sway_scene_outputs_update_event {
	struct sway_scene_output **active;
	size_t size;
};

struct sway_scene_output_sample_event {
	struct sway_scene_output *output;
	bool direct_scanout;
};

struct sway_scene_frame_done_event {
	struct sway_scene_output *output;
	struct timespec when;
};

/** A scene-graph node displaying a buffer */
struct sway_scene_buffer {
	struct sway_scene_node node;

	// May be NULL
	struct wlr_buffer *buffer;

	struct {
		struct wl_signal outputs_update; // struct sway_scene_outputs_update_event
		struct wl_signal output_enter; // struct sway_scene_output
		struct wl_signal output_leave; // struct sway_scene_output
		struct wl_signal output_sample; // struct sway_scene_output_sample_event
		struct wl_signal frame_done; // struct sway_scene_frame_done_event
	} events;

	// May be NULL
	sway_scene_buffer_point_accepts_input_func_t point_accepts_input;

	/**
	 * The output that the largest area of this buffer is displayed on.
	 * This may be NULL if the buffer is not currently displayed on any
	 * outputs.
	 */
	struct sway_scene_output *primary_output;

	float opacity;
	enum wlr_scale_filter_mode filter_mode;
	struct wlr_fbox src_box;
	double dst_width, dst_height;
	enum wl_output_transform transform;
	pixman_region32_t opaque_region;
	enum wlr_color_transfer_function transfer_function;
	enum wlr_color_named_primaries primaries;
	enum wlr_color_encoding color_encoding;
	enum wlr_color_range color_range;

	struct {
		uint64_t active_outputs;
		struct wlr_texture *texture;
		struct wlr_linux_dmabuf_feedback_v1_init_options prev_feedback_options;

		bool own_buffer;
		int buffer_width, buffer_height;
		bool buffer_is_opaque;

		struct wlr_drm_syncobj_timeline *wait_timeline;
		uint64_t wait_point;

		struct wl_listener buffer_release;
		struct wl_listener renderer_destroy;

		// True if the underlying buffer is a wlr_single_pixel_buffer_v1
		bool is_single_pixel_buffer;
		// If is_single_pixel_buffer is set, contains the color of the buffer
		// as {R, G, B, A} where the max value of each component is UINT32_MAX
		uint32_t single_pixel_buffer_color[4];
	};

	// Radii of the two top and two bottom corners
	float radius_top;
	float radius_bottom;
};

/** A viewport for an output in the scene-graph */
struct sway_scene_output {
	struct wlr_output *output;
	struct wl_list link; // sway_scene.outputs
	struct sway_scene *scene;
	struct wlr_addon addon;

	struct wlr_damage_ring damage_ring;

	int x, y;

	struct {
		struct wl_signal destroy;
	} events;

	struct {
		pixman_region32_t pending_commit_damage;

		uint8_t index;

		/**
		 * When scanout is applicable, we increment this every time a frame is rendered until
		 * DMABUF_FEEDBACK_DEBOUNCE_FRAMES is hit to debounce the scanout dmabuf feedback. Likewise,
		 * when scanout is no longer applicable, we decrement this until zero is hit to debounce
		 * composition dmabuf feedback.
		 */
		uint8_t dmabuf_feedback_debounce;
		bool prev_scanout;

		bool gamma_lut_changed;
		struct wlr_gamma_control_v1 *gamma_lut;
		struct wlr_color_transform *gamma_lut_color_transform;

		struct wlr_color_transform *prev_gamma_lut_color_transform;
		struct wlr_color_transform *prev_supplied_color_transform;
		struct wlr_color_transform *prev_combined_color_transform;

		struct wl_listener output_commit;
		struct wl_listener output_damage;
		struct wl_listener output_needs_frame;

		struct wl_list damage_highlight_regions;

		struct wl_array render_list;

		struct wlr_drm_syncobj_timeline *in_timeline;
		uint64_t in_point;
	};
};

struct sway_scene_timer {
	int64_t pre_render_duration;
	struct wlr_render_timer *render_timer;
};

/** A layer shell scene helper */
struct sway_scene_layer_surface_v1 {
	struct sway_scene_tree *tree;
	struct wlr_layer_surface_v1 *layer_surface;

	struct {
		struct wl_listener tree_destroy;
		struct wl_listener layer_surface_destroy;
		struct wl_listener layer_surface_map;
		struct wl_listener layer_surface_unmap;
	};
};

/**
 * Immediately destroy the scene-graph node.
 */
void sway_scene_node_destroy(struct sway_scene_node *node);
/**
 * Enable or disable this node. If a node is disabled, all of its children are
 * implicitly disabled as well.
 */
void sway_scene_node_set_enabled(struct sway_scene_node *node, bool enabled);
/**
 * Set the position of the node relative to its parent.
 */
void sway_scene_node_set_position(struct sway_scene_node *node, double x, double y);
/**
 * Move the node right above the specified sibling.
 * Asserts that node and sibling are distinct and share the same parent.
 */
void sway_scene_node_place_above(struct sway_scene_node *node,
	struct sway_scene_node *sibling);
/**
 * Move the node right below the specified sibling.
 * Asserts that node and sibling are distinct and share the same parent.
 */
void sway_scene_node_place_below(struct sway_scene_node *node,
	struct sway_scene_node *sibling);
/**
 * Move the node above all of its sibling nodes.
 */
void sway_scene_node_raise_to_top(struct sway_scene_node *node);
/**
 * Move the node below all of its sibling nodes.
 */
void sway_scene_node_lower_to_bottom(struct sway_scene_node *node);
/**
 * Move the node to another location in the tree.
 */
void sway_scene_node_reparent(struct sway_scene_node *node,
	struct sway_scene_tree *new_parent);
/**
 * Get the node's layout-local coordinates.
 *
 * True is returned if the node and all of its ancestors are enabled.
 */
bool sway_scene_node_coords(struct sway_scene_node *node, double *lx, double *ly);
/**
 * Call `iterator` on each buffer in the scene-graph, with the buffer's
 * position in layout coordinates. The function is called from root to leaves
 * (in rendering order).
 */
void sway_scene_node_for_each_buffer(struct sway_scene_node *node,
	sway_scene_buffer_iterator_func_t iterator, void *user_data);
/**
 * Find the topmost node in this scene-graph that contains the point at the
 * given layout-local coordinates. (For surface nodes, this means accepting
 * input events at that point.) Returns the node and coordinates relative to the
 * returned node, or NULL if no node is found at that location.
 */
struct sway_scene_node *sway_scene_node_at(struct sway_scene_node *node,
	double lx, double ly, double *nx, double *ny);

/**
 * Create a new scene-graph.
 *
 * The graph is also a struct sway_scene_node. Associated resources can be
 * destroyed through sway_scene_node_destroy().
 */
struct sway_scene *sway_scene_create(void);

/**
 * Handles linux_dmabuf_v1 feedback for all surfaces in the scene.
 *
 * Asserts that a struct wlr_linux_dmabuf_v1 hasn't already been set for the scene.
 */
void sway_scene_set_linux_dmabuf_v1(struct sway_scene *scene,
	struct wlr_linux_dmabuf_v1 *linux_dmabuf_v1);

/**
 * Handles gamma_control_v1 for all outputs in the scene.
 *
 * Asserts that a struct wlr_gamma_control_manager_v1 hasn't already been set
 * for the scene.
 */
void sway_scene_set_gamma_control_manager_v1(struct sway_scene *scene,
	struct wlr_gamma_control_manager_v1 *gamma_control);

/**
 * Handles color_management_v1 feedback for all surfaces in the scene.
 *
 * Asserts that a struct wlr_color_manager_v1 hasn't already been set for the scene.
 */
void sway_scene_set_color_manager_v1(struct sway_scene *scene, struct wlr_color_manager_v1 *manager);

/**
 * Add a node displaying nothing but its children.
 */
struct sway_scene_tree *sway_scene_tree_create(struct sway_scene_tree *parent);

/**
 * Add a node displaying a single surface to the scene-graph.
 *
 * The child sub-surfaces are ignored. See sway_scene_subsurface_tree_create()
 *
 * Note that this helper does multiple things on behalf of the compositor. Some
 * of these include protocol implementations where compositors just need to enable
 * the protocols:
 *  - wp_viewporter
 *  - wp_presentation_time
 *  - wp_fractional_scale_v1
 *  - wp_alpha_modifier_v1
 *  - wp_linux_drm_syncobj_v1
 *  - zwp_linux_dmabuf_v1 presentation feedback with sway_scene_set_linux_dmabuf_v1()
 *
 * This helper will also transparently:
 *  - Send preferred buffer scale¹
 *  - Send preferred buffer transform¹
 *  - Restack xwayland surfaces. See wlr_xwayland_surface_restack()²
 *  - Send output enter/leave events.
 *
 * ¹ Note that scale and transform sent to the surface will be based on the output
 * which has the largest visible surface area. Intelligent visibility calculations
 * influence this.
 * ² xwayland stacking order is undefined when the xwayland surfaces do not
 * intersect.
 */
struct sway_scene_surface *sway_scene_surface_create(struct sway_scene_tree *parent,
	struct wlr_surface *surface);

/**
 * If this node represents a sway_scene_buffer, that buffer will be returned. It
 * is not legal to feed a node that does not represent a sway_scene_buffer.
 */
struct sway_scene_buffer *sway_scene_buffer_from_node(struct sway_scene_node *node);

/**
 * If this node represents a sway_scene_tree, that tree will be returned. It
 * is not legal to feed a node that does not represent a sway_scene_tree.
 */
struct sway_scene_tree *sway_scene_tree_from_node(struct sway_scene_node *node);

/**
 * If this node represents a sway_scene_rect, that rect will be returned. It
 * is not legal to feed a node that does not represent a sway_scene_rect.
 */
struct sway_scene_rect *sway_scene_rect_from_node(struct sway_scene_node *node);

/**
 * If this buffer is backed by a surface, then the struct sway_scene_surface is
 * returned. If not, NULL will be returned.
 */
struct sway_scene_surface *sway_scene_surface_try_from_buffer(
	struct sway_scene_buffer *scene_buffer);

/**
 * Call wlr_surface_send_frame_done() if the surface is visible.
 */
void sway_scene_surface_send_frame_done(struct sway_scene_surface *scene_surface,
	const struct timespec *when);

/**
 * Add a node displaying a solid-colored rectangle to the scene-graph.
 *
 * The color argument must be a premultiplied color value.
 */
struct sway_scene_rect *sway_scene_rect_create(struct sway_scene_tree *parent,
		double width, double height, const float color[static 4]);

/**
 * Change the width and height of an existing rectangle node.
 */
void sway_scene_rect_set_size(struct sway_scene_rect *rect, double width, double height);

/**
 * Change the color of an existing rectangle node.
 *
 * The color argument must be a premultiplied color value.
 */
void sway_scene_rect_set_color(struct sway_scene_rect *rect, const float color[static 4]);

/**
 * Add a decoration node for a view to the scene-graph.
 */
struct sway_scene_decoration *sway_scene_decoration_create(struct sway_scene_tree *parent,
		struct sway_view *view, double width, double height);

/**
 * Change the width and height of an existing decoration node.
 */
void sway_scene_decoration_set_size(struct sway_scene_decoration *deco, double width, double height);

/**
 * Add a border to an existing decoration node with the given colors.
 *
 * The color argument must be a premultiplied color value.
 */
void sway_scene_decoration_set_border_color(struct sway_scene_decoration *deco,
	const float border_top[static 4], const float border_bottom[static 4],
	const float border_left[static 4], const float border_right[static 4]);

/**
 * Change the radius of an existing decoration node.
 */
void sway_scene_decoration_set_border_radius(struct sway_scene_decoration *deco, double radius);

/**
 * Change the width of the border of an existing decoration node.
 */
void sway_scene_decoration_set_border_width(struct sway_scene_decoration *deco, double width);

/**
 * Add a title bar to an existing decoration node with the given height and border radius.
 */
void sway_scene_decoration_set_title_bar(struct sway_scene_decoration *deco, bool enable,
		double height, double border_radius);

/**
 * Set the color of the title bar of an existing decoration node.
 * The color argument must be a premultiplied color value.
 */
void sway_scene_decoration_set_title_bar_color(struct sway_scene_decoration *deco,
	const float color[static 4]);

/**
 * Dims the view for an existing decoration node.
 * The color argument must be a premultiplied color value.
 */
void sway_scene_decoration_set_dimming(struct sway_scene_decoration *deco,
	bool dim, const float color[static 4]);

/**
 * Add a shadow node for a view to the scene graph
 */
struct sway_scene_shadow *sway_scene_shadow_create(struct sway_scene_tree *parent,
		struct sway_scene_decoration *decoration);

/**
 * Change the shadow properties for an existing shadow node.
 */
void sway_scene_shadow_set_properties(struct sway_scene_shadow *shadow,
	double width, double height, bool enabled, bool dynamic,
	double size, double blur, const double offset[static 2], float color[4]);

/**
 * Add a node displaying a buffer to the scene-graph.
 *
 * If the buffer is NULL, this node will not be displayed.
 */
struct sway_scene_buffer *sway_scene_buffer_create(struct sway_scene_tree *parent,
	struct wlr_buffer *buffer);

/**
 * Sets the buffer's backing buffer.
 *
 * If the buffer is NULL, the buffer node will not be displayed.
 */
void sway_scene_buffer_set_buffer(struct sway_scene_buffer *scene_buffer,
	struct wlr_buffer *buffer);

/**
 * Sets the buffer's backing buffer with a custom damage region.
 *
 * The damage region is in buffer-local coordinates. If the region is NULL,
 * the whole buffer node will be damaged.
 */
void sway_scene_buffer_set_buffer_with_damage(struct sway_scene_buffer *scene_buffer,
	struct wlr_buffer *buffer, const pixman_region32_t *region);

/**
 * Options for sway_scene_buffer_set_buffer_with_options().
 */
struct sway_scene_buffer_set_buffer_options {
	// The damage region is in buffer-local coordinates. If the region is NULL,
	// the whole buffer node will be damaged.
	const pixman_region32_t *damage;

	// Wait for a timeline synchronization point before reading from the buffer.
	struct wlr_drm_syncobj_timeline *wait_timeline;
	uint64_t wait_point;
};

/**
 * Sets the buffer's backing buffer.
 *
 * If the buffer is NULL, the buffer node will not be displayed. If options is
 * NULL, empty options are used.
 */
void sway_scene_buffer_set_buffer_with_options(struct sway_scene_buffer *scene_buffer,
	struct wlr_buffer *buffer, const struct sway_scene_buffer_set_buffer_options *options);

/**
 * Sets the buffer's opaque region. This is an optimization hint used to
 * determine if buffers which reside under this one need to be rendered or not.
 */
void sway_scene_buffer_set_opaque_region(struct sway_scene_buffer *scene_buffer,
	const pixman_region32_t *region);

/**
 * Set the source rectangle describing the region of the buffer which will be
 * sampled to render this node. This allows cropping the buffer.
 *
 * If NULL, the whole buffer is sampled. By default, the source box is NULL.
 */
void sway_scene_buffer_set_source_box(struct sway_scene_buffer *scene_buffer,
	const struct wlr_fbox *box);

/**
 * Set the destination size describing the region of the scene-graph the buffer
 * will be painted onto. This allows scaling the buffer.
 *
 * If zero, the destination size will be the buffer size. By default, the
 * destination size is zero.
 */
void sway_scene_buffer_set_dest_size(struct sway_scene_buffer *scene_buffer,
	double width, double height);

/**
 * Sets the two top and two bottom radii for the buffer.
 */
void sway_scene_buffer_set_radius(struct sway_scene_buffer *scene_buffer,
	float radius_top, float radius_bottom);

/**
 * Set a transform which will be applied to the buffer.
 */
void sway_scene_buffer_set_transform(struct sway_scene_buffer *scene_buffer,
	enum wl_output_transform transform);

/**
* Sets the opacity of this buffer
*/
void sway_scene_buffer_set_opacity(struct sway_scene_buffer *scene_buffer,
	float opacity);

/**
* Sets the filter mode to use when scaling the buffer
*/
void sway_scene_buffer_set_filter_mode(struct sway_scene_buffer *scene_buffer,
	enum wlr_scale_filter_mode filter_mode);

void sway_scene_buffer_set_transfer_function(struct sway_scene_buffer *scene_buffer,
	enum wlr_color_transfer_function transfer_function);

void sway_scene_buffer_set_primaries(struct sway_scene_buffer *scene_buffer,
	enum wlr_color_named_primaries primaries);

void sway_scene_buffer_set_color_encoding(struct sway_scene_buffer *scene_buffer,
	enum wlr_color_encoding encoding);

void sway_scene_buffer_set_color_range(struct sway_scene_buffer *scene_buffer,
	enum wlr_color_range range);

/**
 * Calls the buffer's frame_done signal.
 */
void sway_scene_buffer_send_frame_done(struct sway_scene_buffer *scene_buffer,
	struct sway_scene_frame_done_event *event);

/**
 * Add a viewport for the specified output to the scene-graph.
 *
 * An output can only be added once to the scene-graph.
 */
struct sway_scene_output *sway_scene_output_create(struct sway_scene *scene,
	struct wlr_output *output);
/**
 * Destroy a scene-graph output.
 */
void sway_scene_output_destroy(struct sway_scene_output *scene_output);
/**
 * Set the output's position in the scene-graph.
 */
void sway_scene_output_set_position(struct sway_scene_output *scene_output,
	int lx, int ly);

struct sway_scene_output_state_options {
	struct sway_scene_timer *timer;

	/**
	 * Color transform to apply before the output's color transform. Cannot be
	 * used when the output has a non-NULL image description set.
	 */
	struct wlr_color_transform *color_transform;

	/**
	 * Allows use of a custom swapchain. This can be useful when trying out an
	 * output configuration. The swapchain dimensions must match the respective
	 * wlr_output_state or output size if not specified.
	 */
	struct wlr_swapchain *swapchain;
};

/**
 * Returns true if scene wants to render a new frame. False, if no new frame
 * is needed and an output commit can be skipped for the current frame.
 */
bool sway_scene_output_needs_frame(struct sway_scene_output *scene_output);

/**
 * Render and commit an output.
 */
bool sway_scene_output_commit(struct sway_scene_output *scene_output,
	const struct sway_scene_output_state_options *options);

/**
 * Render and populate given output state.
 */
bool sway_scene_output_build_state(struct sway_scene_output *scene_output,
	struct wlr_output_state *state, const struct sway_scene_output_state_options *options);

/**
 * Retrieve the duration in nanoseconds between the last sway_scene_output_commit() call and the end
 * of its operations, including those on the GPU that may have finished after the call returned.
 *
 * Returns -1 if the duration is unavailable.
 */
int64_t sway_scene_timer_get_duration_ns(struct sway_scene_timer *timer);
void sway_scene_timer_finish(struct sway_scene_timer *timer);

/**
 * Call wlr_surface_send_frame_done() on all surfaces in the scene rendered by
 * sway_scene_output_commit() for which sway_scene_surface.primary_output
 * matches the given scene_output.
 */
void sway_scene_output_send_frame_done(struct sway_scene_output *scene_output,
	struct timespec *now);
/**
 * Call `iterator` on each buffer in the scene-graph visible on the output,
 * with the buffer's position in layout coordinates. The function is called
 * from root to leaves (in rendering order).
 */
void sway_scene_output_for_each_buffer(struct sway_scene_output *scene_output,
	sway_scene_buffer_iterator_func_t iterator, void *user_data);
/**
 * Get a scene-graph output from a struct wlr_output.
 *
 * If the output hasn't been added to the scene-graph, returns NULL.
 */
struct sway_scene_output *sway_scene_get_scene_output(struct sway_scene *scene,
	struct wlr_output *output);

/**
 * Attach an output layout to a scene.
 *
 * The resulting scene output layout allows to synchronize the positions of scene
 * outputs with the positions of corresponding layout outputs.
 *
 * It is automatically destroyed when the scene or the output layout is destroyed.
 */
struct sway_scene_output_layout *sway_scene_attach_output_layout(struct sway_scene *scene,
	struct wlr_output_layout *output_layout);

/**
 * Add an output to the scene output layout.
 *
 * When the layout output is repositioned, the scene output will be repositioned
 * accordingly.
 */
void sway_scene_output_layout_add_output(struct sway_scene_output_layout *sol,
	struct wlr_output_layout_output *lo, struct sway_scene_output *so);

/**
 * Add a node displaying a surface and all of its sub-surfaces to the
 * scene-graph.
 */
struct sway_scene_tree *sway_scene_subsurface_tree_create(
	struct sway_scene_tree *parent, struct wlr_surface *surface);

/**
 * Sets a cropping region for any subsurface trees that are children of this
 * scene node. The clip coordinate space will be that of the root surface of
 * the subsurface tree.
 *
 * A NULL or empty clip will disable clipping
 */
void sway_scene_subsurface_tree_set_clip(struct sway_scene_node *node,
	const struct wlr_box *clip);

/**
 * Add a node displaying an xdg_surface and all of its sub-surfaces to the
 * scene-graph.
 *
 * The origin of the returned scene-graph node will match the top-left corner
 * of the xdg_surface window geometry.
 */
struct sway_scene_tree *sway_scene_xdg_surface_create(
	struct sway_scene_tree *parent, struct wlr_xdg_surface *xdg_surface);

/**
 * Add a node displaying a layer_surface_v1 and all of its sub-surfaces to the
 * scene-graph.
 *
 * The origin of the returned scene-graph node will match the top-left corner
 * of the layer surface.
 */
struct sway_scene_layer_surface_v1 *sway_scene_layer_surface_v1_create(
	struct sway_scene_tree *parent, struct wlr_layer_surface_v1 *layer_surface);

/**
 * Configure a layer_surface_v1, position its scene node in accordance to its
 * current state, and update the remaining usable area.
 *
 * full_area represents the entire area that may be used by the layer surface
 * if its exclusive_zone is -1, and is usually the output dimensions.
 * usable_area represents what remains of full_area that can be used if
 * exclusive_zone is >= 0. usable_area is updated if the surface has a positive
 * exclusive_zone, so that it can be used for the next layer surface.
 */
void sway_scene_layer_surface_v1_configure(
	struct sway_scene_layer_surface_v1 *scene_layer_surface,
	const struct wlr_box *full_area, struct wlr_box *usable_area);

/**
 * Add a node displaying a drag icon and all its sub-surfaces to the
 * scene-graph.
 */
struct sway_scene_tree *sway_scene_drag_icon_create(
	struct sway_scene_tree *parent, struct wlr_drag_icon *drag_icon);

struct sway_scene *scene_node_get_root(struct sway_scene_node *node);

void scene_node_get_size(struct sway_scene_node *node, double *width, double *height);

void scene_surface_set_clip(struct sway_scene_surface *surface, struct wlr_box *clip);

/**
 * Find a parent of the current node that is a popup or view. If it finds one,
 * fill scale (content scale and workspace scale)
 * Returns: true if the current node is a popup or view (parent view), else false
 * (children surfaces or popups)
 */
bool scene_node_get_parent_total_scale(struct sway_scene_node *node, double *scale);

void sway_scene_surface_reconfigure(struct sway_scene_surface *scene_surface);

#endif // _SWAY_SCENE_H
