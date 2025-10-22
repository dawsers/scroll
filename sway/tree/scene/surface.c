#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include "sway/config.h"
#include "sway/tree/scene.h"
#include "sway/tree/view.h"
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/util/transform.h>
#include "util.h"

static void handle_scene_buffer_outputs_update(
		struct wl_listener *listener, void *data) {
	struct sway_scene_surface *surface =
		wl_container_of(listener, surface, outputs_update);

	if (surface->buffer->primary_output == NULL) {
		return;
	}
	double scale = surface->buffer->primary_output->output->scale;
	wlr_fractional_scale_v1_notify_scale(surface->surface, scale);
	wlr_surface_set_preferred_buffer_scale(surface->surface, ceil(scale));
	wlr_surface_set_preferred_buffer_transform(surface->surface,
		surface->buffer->primary_output->output->transform);
}

static void handle_scene_buffer_output_enter(
		struct wl_listener *listener, void *data) {
	struct sway_scene_surface *surface =
		wl_container_of(listener, surface, output_enter);
	struct sway_scene_output *output = data;

	wlr_surface_send_enter(surface->surface, output->output);
}

static void handle_scene_buffer_output_leave(
		struct wl_listener *listener, void *data) {
	struct sway_scene_surface *surface =
		wl_container_of(listener, surface, output_leave);
	struct sway_scene_output *output = data;

	wlr_surface_send_leave(surface->surface, output->output);
}

static void handle_scene_buffer_output_sample(
		struct wl_listener *listener, void *data) {
	struct sway_scene_surface *surface =
		wl_container_of(listener, surface, output_sample);
	const struct sway_scene_output_sample_event *event = data;
	struct sway_scene_output *scene_output = event->output;
	if (surface->buffer->primary_output != scene_output) {
		return;
	}

	if (event->direct_scanout) {
		wlr_presentation_surface_scanned_out_on_output(surface->surface, scene_output->output);
	} else {
		wlr_presentation_surface_textured_on_output(surface->surface, scene_output->output);
	}
}

static void handle_scene_buffer_frame_done(
		struct wl_listener *listener, void *data) {
	struct sway_scene_surface *surface =
		wl_container_of(listener, surface, frame_done);
	struct timespec *now = data;

	wlr_surface_send_frame_done(surface->surface, now);
}

static void scene_surface_handle_surface_destroy(
		struct wl_listener *listener, void *data) {
	struct sway_scene_surface *surface =
		wl_container_of(listener, surface, surface_destroy);

	sway_scene_node_destroy(&surface->buffer->node);
}

// This is used for sway_scene where it unconditionally locks buffers preventing
// reuse of the existing texture for shm clients. With the usage pattern of
// sway_scene surface handling, we can mark its locked buffer as safe
// for mutation.
static void client_buffer_mark_next_can_damage(struct wlr_client_buffer *buffer) {
	buffer->WLR_PRIVATE.n_ignore_locks++;
}

static void scene_buffer_unmark_client_buffer(struct sway_scene_buffer *scene_buffer) {
	if (!scene_buffer->buffer) {
		return;
	}

	struct wlr_client_buffer *buffer = wlr_client_buffer_get(scene_buffer->buffer);
	if (!buffer) {
		return;
	}

	// If the buffer was a single-pixel buffer where we cached its color
	// then it won't have been marked as damage-allowed.
	if (buffer->WLR_PRIVATE.n_ignore_locks > 0) {
		buffer->WLR_PRIVATE.n_ignore_locks--;
	}
}

void sway_scene_surface_reconfigure(struct sway_scene_surface *scene_surface) {
	struct sway_scene_buffer *scene_buffer = scene_surface->buffer;
	struct wlr_surface *surface = scene_surface->surface;
	struct wlr_surface_state *state = &surface->current;

	struct wlr_fbox src_box;
	wlr_surface_get_buffer_source_box(surface, &src_box);

	pixman_region32_t opaque;
	pixman_region32_init(&opaque);
	pixman_region32_copy(&opaque, &surface->opaque_region);

	int width = state->width;
	int height = state->height;

	if (!wlr_box_empty(&scene_surface->clip)) {
		struct wlr_box *clip = &scene_surface->clip;

		int buffer_width = state->buffer_width;
		int buffer_height = state->buffer_height;
		width = min(clip->width, width - clip->x);
		height = min(clip->height, height - clip->y);

		wlr_fbox_transform(&src_box, &src_box, state->transform,
			buffer_width, buffer_height);
		wlr_output_transform_coords(state->transform, &buffer_width, &buffer_height);

		src_box.x += (clip->x * src_box.width) / state->width;
		src_box.y += (clip->y * src_box.height) / state->height;
		src_box.width *= (double)width / state->width;
		src_box.height *= (double)height / state->height;
		src_box.x = round(src_box.x);
		src_box.y = round(src_box.y);
		src_box.width = round(src_box.width);
		src_box.height = round(src_box.height);

		wlr_fbox_transform(&src_box, &src_box, wlr_output_transform_invert(state->transform),
			buffer_width, buffer_height);

		pixman_region32_translate(&opaque, -clip->x, -clip->y);
		pixman_region32_intersect_rect(&opaque, &opaque, 0, 0, width, height);
	}

	if (width <= 0 || height <= 0 || (src_box.width <= 1.0 && src_box.height <= 1.0)) {
		sway_scene_buffer_set_buffer(scene_buffer, NULL);
		pixman_region32_fini(&opaque);
		return;
	}

	float opacity = 1.0;
	const struct wlr_alpha_modifier_surface_v1_state *alpha_modifier_state =
		wlr_alpha_modifier_v1_get_surface_state(surface);
	if (alpha_modifier_state != NULL) {
		opacity = (float)alpha_modifier_state->multiplier;
	}

	double wscale, hscale, total_scale;
	struct sway_view *view = view_from_wlr_surface(surface);
	if (view) {
		total_scale = view_get_total_scale(view);
		if (total_scale < 0.0) {
			total_scale = 1.0;
		}
		view_get_animation_scales(view, &wscale, &hscale);
	} else {
		wscale = hscale = total_scale = 1.0;
	}
	// Compute a dst that can perfectly fit an aligned buffer in logical space
	double dst_width = width * total_scale * wscale;
	double dst_height = height * total_scale * hscale;
	sway_scene_buffer_set_opaque_region(scene_buffer, &opaque);
	sway_scene_buffer_set_source_box(scene_buffer, &src_box);
	sway_scene_buffer_set_dest_size(scene_buffer, MAX(1, dst_width),
		MAX(1, dst_height));
	sway_scene_buffer_set_transform(scene_buffer, state->transform);
	sway_scene_buffer_set_opacity(scene_buffer, opacity);

	scene_buffer_unmark_client_buffer(scene_buffer);

	if (surface->buffer) {
		// If we've cached the buffer's single-pixel buffer color
		// then any in-place updates to the texture wouldn't be
		// reflected in rendering. So only allow in-place texture
		// updates if it's not a single pixel buffer.  Note that we
		// can't use the cached scene_buffer->is_single_pixel_buffer
		// because that's only set later on.
		bool is_single_pixel_buffer = false;
		if (surface->buffer->source != NULL) {
			struct wlr_single_pixel_buffer_v1 *spb =
				wlr_single_pixel_buffer_v1_try_from_buffer(surface->buffer->source);
			is_single_pixel_buffer = spb != NULL;
		}
		if (!is_single_pixel_buffer) {
			client_buffer_mark_next_can_damage(surface->buffer);
		}

		struct wlr_linux_drm_syncobj_surface_v1_state *syncobj_surface_state =
			wlr_linux_drm_syncobj_v1_get_surface_state(surface);

		struct wlr_drm_syncobj_timeline *wait_timeline = NULL;
		uint64_t wait_point = 0;
		if (syncobj_surface_state != NULL) {
			wait_timeline = syncobj_surface_state->acquire_timeline;
			wait_point = syncobj_surface_state->acquire_point;
		}

		struct sway_scene_buffer_set_buffer_options options = {
			.damage = &surface->buffer_damage,
			.wait_timeline = wait_timeline,
			.wait_point = wait_point,
		};
		sway_scene_buffer_set_buffer_with_options(scene_buffer,
			&surface->buffer->base, &options);

		if (syncobj_surface_state != NULL &&
				(surface->current.committed & WLR_SURFACE_STATE_BUFFER &&
				 surface->buffer->source != NULL)) {
			wlr_linux_drm_syncobj_v1_state_signal_release_with_buffer(syncobj_surface_state,
				surface->buffer->source);
		}
	} else {
		sway_scene_buffer_set_buffer(scene_buffer, NULL);
	}

	pixman_region32_fini(&opaque);
}
static void surface_reconfigure(struct sway_scene_surface *scene_surface) {
	sway_scene_surface_reconfigure(scene_surface);
}

static void handle_scene_surface_surface_commit(
		struct wl_listener *listener, void *data) {
	struct sway_scene_surface *surface =
		wl_container_of(listener, surface, surface_commit);
	struct sway_scene_buffer *scene_buffer = surface->buffer;

	surface_reconfigure(surface);

	// If the surface has requested a frame done event, honour that. The
	// frame_callback_list will be populated in this case. We should only
	// schedule the frame however if the node is enabled and there is an
	// output intersecting, otherwise the frame done events would never reach
	// the surface anyway.
	double lx, ly;
	bool enabled = sway_scene_node_coords(&scene_buffer->node, &lx, &ly);

	if (!wl_list_empty(&surface->surface->current.frame_callback_list) &&
			surface->buffer->primary_output != NULL && enabled) {
		wlr_output_schedule_frame(surface->buffer->primary_output->output);
	}
}

static bool scene_buffer_point_accepts_input(struct sway_scene_buffer *scene_buffer,
		double *sx, double *sy) {
	struct sway_scene_surface *scene_surface =
		sway_scene_surface_try_from_buffer(scene_buffer);

	*sx += scene_surface->clip.x;
	*sy += scene_surface->clip.y;

	return wlr_surface_point_accepts_input(scene_surface->surface, *sx, *sy);
}

static void surface_addon_destroy(struct wlr_addon *addon) {
	struct sway_scene_surface *surface = wl_container_of(addon, surface, addon);

	scene_buffer_unmark_client_buffer(surface->buffer);

	wlr_addon_finish(&surface->addon);

	wl_list_remove(&surface->outputs_update.link);
	wl_list_remove(&surface->output_enter.link);
	wl_list_remove(&surface->output_leave.link);
	wl_list_remove(&surface->output_sample.link);
	wl_list_remove(&surface->frame_done.link);
	wl_list_remove(&surface->surface_destroy.link);
	wl_list_remove(&surface->surface_commit.link);

	free(surface);
}

static const struct wlr_addon_interface surface_addon_impl = {
	.name = "sway_scene_surface",
	.destroy = surface_addon_destroy,
};

struct sway_scene_surface *sway_scene_surface_try_from_buffer(
		struct sway_scene_buffer *scene_buffer) {
	struct wlr_addon *addon = wlr_addon_find(&scene_buffer->node.addons,
		scene_buffer, &surface_addon_impl);
	if (!addon) {
		return NULL;
	}

	struct sway_scene_surface *surface = wl_container_of(addon, surface, addon);
	return surface;
}

struct sway_scene_surface *sway_scene_surface_create(struct sway_scene_tree *parent,
		struct wlr_surface *wlr_surface) {
	struct sway_scene_surface *surface = calloc(1, sizeof(*surface));
	if (surface == NULL) {
		return NULL;
	}

	struct sway_scene_buffer *scene_buffer = sway_scene_buffer_create(parent, NULL);
	if (!scene_buffer) {
		free(surface);
		return NULL;
	}

	surface->buffer = scene_buffer;
	surface->surface = wlr_surface;
	scene_buffer->point_accepts_input = scene_buffer_point_accepts_input;

	surface->outputs_update.notify = handle_scene_buffer_outputs_update;
	wl_signal_add(&scene_buffer->events.outputs_update, &surface->outputs_update);

	surface->output_enter.notify = handle_scene_buffer_output_enter;
	wl_signal_add(&scene_buffer->events.output_enter, &surface->output_enter);

	surface->output_leave.notify = handle_scene_buffer_output_leave;
	wl_signal_add(&scene_buffer->events.output_leave, &surface->output_leave);

	surface->output_sample.notify = handle_scene_buffer_output_sample;
	wl_signal_add(&scene_buffer->events.output_sample, &surface->output_sample);

	surface->frame_done.notify = handle_scene_buffer_frame_done;
	wl_signal_add(&scene_buffer->events.frame_done, &surface->frame_done);

	surface->surface_destroy.notify = scene_surface_handle_surface_destroy;
	wl_signal_add(&wlr_surface->events.destroy, &surface->surface_destroy);

	surface->surface_commit.notify = handle_scene_surface_surface_commit;
	wl_signal_add(&wlr_surface->events.commit, &surface->surface_commit);

	wlr_addon_init(&surface->addon, &scene_buffer->node.addons,
		scene_buffer, &surface_addon_impl);

	surface_reconfigure(surface);

	return surface;
}

void scene_surface_set_clip(struct sway_scene_surface *surface, struct wlr_box *clip) {
	if (wlr_box_equal(clip, &surface->clip)) {
		return;
	}

	if (clip) {
		surface->clip = *clip;
	} else {
		surface->clip = (struct wlr_box){0};
	}

	surface_reconfigure(surface);
}
