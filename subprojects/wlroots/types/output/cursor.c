#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/allocator.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/util/transform.h>
#include "render/color.h"
#include "types/wlr_buffer.h"
#include "types/wlr_output.h"

static bool output_set_hardware_cursor(struct wlr_output *output,
		struct wlr_buffer *buffer, int hotspot_x, int hotspot_y) {
	if (!output->impl->set_cursor) {
		return false;
	}

	if (!output->impl->set_cursor(output, buffer, hotspot_x, hotspot_y)) {
		return false;
	}

	wlr_output_update_needs_frame(output);

	wlr_buffer_unlock(output->cursor_front_buffer);
	output->cursor_front_buffer = NULL;

	if (buffer != NULL) {
		output->cursor_front_buffer = wlr_buffer_lock(buffer);
	}

	return true;
}

static bool output_move_hardware_cursor(struct wlr_output *output, int x, int y) {
	assert(output->impl->move_cursor);
	if (!output->impl->move_cursor(output, x, y)) {
		return false;
	}
	wlr_output_update_needs_frame(output);
	return true;
}

static void output_cursor_damage_whole(struct wlr_output_cursor *cursor);

static void output_disable_hardware_cursor(struct wlr_output *output) {
	if (output->hardware_cursor == NULL) {
		return;
	}

	output_set_hardware_cursor(output, NULL, 0, 0);
	output_cursor_damage_whole(output->hardware_cursor);
	output->hardware_cursor = NULL;
}

void wlr_output_lock_software_cursors(struct wlr_output *output, bool lock) {
	if (lock) {
		++output->software_cursor_locks;
	} else {
		assert(output->software_cursor_locks > 0);
		--output->software_cursor_locks;
	}
	wlr_log(WLR_DEBUG, "%s hardware cursors on output '%s' (locks: %d)",
		lock ? "Disabling" : "Enabling", output->name,
		output->software_cursor_locks);

	if (output->software_cursor_locks > 0) {
		output_disable_hardware_cursor(output);
	}

	// If it's possible to use hardware cursors again, don't switch immediately
	// since a recorder is likely to lock software cursors for the next frame
	// again.
}

/**
 * Returns the cursor box, scaled for its output.
 */
static void output_cursor_get_box(struct wlr_output_cursor *cursor,
		struct wlr_box *box) {
	box->x = cursor->x - cursor->hotspot_x;
	box->y = cursor->y - cursor->hotspot_y;
	box->width = cursor->width;
	box->height = cursor->height;
}

void wlr_output_add_software_cursors_to_render_pass(struct wlr_output *output,
		struct wlr_render_pass *render_pass, const pixman_region32_t *damage) {
	int width, height;
	wlr_output_transformed_resolution(output, &width, &height);

	struct wlr_output_cursor *cursor;
	wl_list_for_each(cursor, &output->cursors, link) {
		if (!cursor->enabled || !cursor->visible ||
				output->hardware_cursor == cursor) {
			continue;
		}

		struct wlr_texture *texture = cursor->texture;
		if (texture == NULL) {
			continue;
		}

		struct wlr_box box;
		output_cursor_get_box(cursor, &box);
		wlr_box_transform(&box, &box,
			wlr_output_transform_invert(output->transform), width, height);

		pixman_region32_t cursor_damage;
		pixman_region32_init_rect(&cursor_damage,
			box.x, box.y, box.width, box.height);
		if (damage != NULL) {
			pixman_region32_intersect(&cursor_damage, &cursor_damage, damage);
		}

		if (pixman_region32_empty(&cursor_damage)) {
			pixman_region32_fini(&cursor_damage);
			continue;
		}

		wlr_render_pass_add_texture(render_pass, &(struct wlr_render_texture_options) {
			.texture = texture,
			.src_box = cursor->src_box,
			.dst_box = box,
			.clip = &cursor_damage,
			.transform = output->transform,
		});

		pixman_region32_fini(&cursor_damage);
	}
}

static void output_cursor_damage_whole(struct wlr_output_cursor *cursor) {
	struct wlr_box box;
	output_cursor_get_box(cursor, &box);

	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, box.x, box.y, box.width, box.height);

	struct wlr_output_event_damage event = {
		.output = cursor->output,
		.damage = &damage,
	};
	wl_signal_emit_mutable(&cursor->output->events.damage, &event);

	pixman_region32_fini(&damage);
}

static void output_cursor_update_visible(struct wlr_output_cursor *cursor) {
	struct wlr_box output_box;
	output_box.x = output_box.y = 0;
	wlr_output_transformed_resolution(cursor->output, &output_box.width,
		&output_box.height);

	struct wlr_box cursor_box;
	output_cursor_get_box(cursor, &cursor_box);

	cursor->visible =
		wlr_box_intersects(&output_box, &cursor_box);
}

static bool output_pick_cursor_format(struct wlr_output *output,
		struct wlr_drm_format *format) {
	struct wlr_allocator *allocator = output->allocator;
	assert(allocator != NULL);

	const struct wlr_drm_format_set *display_formats = NULL;
	if (output->impl->get_cursor_formats) {
		display_formats =
			output->impl->get_cursor_formats(output, allocator->buffer_caps);
		if (display_formats == NULL) {
			wlr_log(WLR_DEBUG, "Failed to get cursor display formats");
			return false;
		}
	}

	return output_pick_format(output, display_formats, format, DRM_FORMAT_ARGB8888);
}

/**
 * Converts between wlr_output_cursor's orientation and the orientation of the
 * buffer passed to output->impl->set_cursor.
 *
 * The conversion is the same in both directions.
 */
static void output_cursor_transform_coords(const struct wlr_output_cursor *cursor,
		int *width, int *height) {
	wlr_output_transform_coords(cursor->output->transform, width, height);
}

/**
 * Converts from wlr_output_cursor's hotspot/width/height coordinate space to
 * the coordinate space of the buffer passed to output->impl->set_cursor.
 */
static void output_cursor_to_hardware_buffer_coords(const struct wlr_output_cursor *cursor,
		const struct wlr_buffer *hardware_buffer, struct wlr_box *box) {
	int width = hardware_buffer->width;
	int height = hardware_buffer->height;
	// wlr_box_transform expects pre-transform dimensions
	output_cursor_transform_coords(cursor, &width, &height);

	wlr_box_transform(box, box, wlr_output_transform_invert(cursor->output->transform),
		width, height);
}

static struct wlr_buffer *render_cursor_buffer(struct wlr_output_cursor *cursor) {
	struct wlr_output *output = cursor->output;

	struct wlr_texture *texture = cursor->texture;
	if (texture == NULL) {
		return NULL;
	}

	struct wlr_allocator *allocator = output->allocator;
	struct wlr_renderer *renderer = output->renderer;
	assert(allocator != NULL && renderer != NULL);

	// buffer dimensions
	int width = cursor->width;
	int height = cursor->height;
	output_cursor_transform_coords(cursor, &width, &height);

	if (output->impl->get_cursor_sizes) {
		// Apply hardware limitations on buffer size
		size_t sizes_len = 0;
		const struct wlr_output_cursor_size *sizes =
			output->impl->get_cursor_sizes(cursor->output, &sizes_len);
		if (sizes_len == 0) {
			wlr_log(WLR_DEBUG, "Hardware cursor not supported");
			return NULL;
		}

		bool found = false;
		for (size_t i = 0; i < sizes_len; i++) {
			struct wlr_output_cursor_size size = sizes[i];
			if (width <= size.width && height <= size.height) {
				width = size.width;
				height = size.height;
				found = true;
				break;
			}
		}

		if (!found) {
			wlr_log(WLR_DEBUG, "Cursor too large (%dx%d), "
				"exceeds hardware limitations", width, height);
			return NULL;
		}
	}

	if (output->cursor_swapchain == NULL ||
			output->cursor_swapchain->width != width ||
			output->cursor_swapchain->height != height) {
		struct wlr_drm_format format = {0};
		if (!output_pick_cursor_format(output, &format)) {
			wlr_log(WLR_DEBUG, "Failed to pick cursor format");
			return NULL;
		}

		wlr_swapchain_destroy(output->cursor_swapchain);
		output->cursor_swapchain = wlr_swapchain_create(allocator,
			width, height, &format);
		wlr_drm_format_finish(&format);
		if (output->cursor_swapchain == NULL) {
			wlr_log(WLR_ERROR, "Failed to create cursor swapchain");
			return NULL;
		}
	}

	struct wlr_buffer *buffer = wlr_swapchain_acquire(output->cursor_swapchain);
	if (buffer == NULL) {
		return NULL;
	}

	struct wlr_box dst_box = {
		.width = cursor->width,
		.height = cursor->height,
	};
	output_cursor_to_hardware_buffer_coords(cursor, buffer, &dst_box);

	struct wlr_buffer_pass_options options = {
		.color_transform = cursor->color_transform,
	};
	struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(renderer, buffer, &options);
	if (pass == NULL) {
		wlr_buffer_unlock(buffer);
		return NULL;
	}

	enum wl_output_transform transform = wlr_output_transform_invert(cursor->transform);
	transform = wlr_output_transform_compose(transform, output->transform);

	wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
		.box = { .width = buffer->width, .height = buffer->height },
		.blend_mode = WLR_RENDER_BLEND_MODE_NONE,
	});
	wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
		.texture = texture,
		.src_box = cursor->src_box,
		.dst_box = dst_box,
		.transform = transform,
		.wait_timeline = cursor->wait_timeline,
		.wait_point = cursor->wait_point,
	});

	if (!wlr_render_pass_submit(pass)) {
		wlr_buffer_unlock(buffer);
		return NULL;
	}

	return buffer;
}

static bool output_cursor_attempt_hardware(struct wlr_output_cursor *cursor) {
	struct wlr_output *output = cursor->output;

	if (!output->impl->set_cursor || output->software_cursor_locks > 0) {
		return false;
	}

	struct wlr_texture *texture = cursor->texture;

	// If the cursor was hidden or was a software cursor, the hardware
	// cursor position is outdated
	output_move_hardware_cursor(cursor->output, (int)cursor->x, (int)cursor->y);

	struct wlr_buffer *buffer = NULL;
	if (texture != NULL) {
		buffer = render_cursor_buffer(cursor);
		if (buffer == NULL) {
			wlr_log(WLR_DEBUG, "Failed to render cursor buffer");
			return false;
		}
	}

	struct wlr_box hotspot = {0};
	if (buffer != NULL) {
		hotspot.x = cursor->hotspot_x;
		hotspot.y = cursor->hotspot_y;
		output_cursor_to_hardware_buffer_coords(cursor, buffer, &hotspot);
	}

	bool ok = output_set_hardware_cursor(output, buffer, hotspot.x, hotspot.y);
	wlr_buffer_unlock(buffer);
	if (ok) {
		output->hardware_cursor = cursor;
	}
	return ok;
}

bool output_cursor_set_buffer_scaled(struct wlr_output_cursor *cursor,
		struct wlr_buffer *buffer, int32_t hotspot_x, int32_t hotspot_y,
		float image_scale) {
	struct wlr_renderer *renderer = cursor->output->renderer;
	assert(renderer != NULL);

	struct wlr_texture *texture = NULL;
	struct wlr_fbox src_box = {0};
	// The buffer has image_scale times the resolution of a buffer for the
	// output scale, so its logical size and hotspot are reduced accordingly
	double scale = cursor->output->scale * image_scale;
	double dst_width = 0, dst_height = 0;
	if (buffer != NULL) {
		texture = wlr_texture_from_buffer(renderer, buffer);
		if (texture == NULL) {
			return false;
		}

		src_box = (struct wlr_fbox){
			.width = texture->width,
			.height = texture->height,
		};

		dst_width = (double)texture->width / scale;
		dst_height = (double)texture->height / scale;
	}

	double dst_hotspot_x = (double)hotspot_x / scale;
	double dst_hotspot_y = (double)hotspot_y / scale;

	return output_cursor_set_texture(cursor, texture, true, &src_box,
		dst_width, dst_height, WL_OUTPUT_TRANSFORM_NORMAL, dst_hotspot_x, dst_hotspot_y,
		NULL, 0);
}

bool wlr_output_cursor_set_buffer(struct wlr_output_cursor *cursor,
		struct wlr_buffer *buffer, int32_t hotspot_x, int32_t hotspot_y) {
	return output_cursor_set_buffer_scaled(cursor, buffer, hotspot_x, hotspot_y, 1.0f);
}

static void output_cursor_handle_renderer_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_output_cursor *cursor = wl_container_of(listener, cursor, renderer_destroy);
	output_cursor_set_texture(cursor, NULL, false, NULL, 0, 0,
		WL_OUTPUT_TRANSFORM_NORMAL, 0, 0, NULL, 0);
}

bool output_cursor_set_texture(struct wlr_output_cursor *cursor,
		struct wlr_texture *texture, bool own_texture, const struct wlr_fbox *src_box,
		double dst_width, double dst_height, enum wl_output_transform transform,
		double hotspot_x, double hotspot_y,
		struct wlr_drm_syncobj_timeline *wait_timeline, uint64_t wait_point) {
	if (texture == NULL && !cursor->enabled) {
		// Cursor is still disabled, do nothing
		return true;
	}

	struct wlr_output *output = cursor->output;

	if (cursor->output->hardware_cursor != cursor) {
		output_cursor_damage_whole(cursor);
	}

	cursor->enabled = texture != NULL;
	if (texture != NULL) {
		cursor->width = (int)round(dst_width * output->scale * cursor->scale);
		cursor->height = (int)round(dst_height * output->scale * cursor->scale);
		cursor->src_box = *src_box;
		cursor->transform = transform;
	} else {
		cursor->width = 0;
		cursor->height = 0;
	}

	cursor->hotspot_x = (int)round(hotspot_x * output->scale * cursor->scale);
	cursor->hotspot_y = (int)round(hotspot_y * output->scale * cursor->scale);

	output_cursor_update_visible(cursor);

	if (cursor->own_texture) {
		wlr_texture_destroy(cursor->texture);
	}
	cursor->texture = texture;
	cursor->own_texture = own_texture;

	wlr_drm_syncobj_timeline_unref(cursor->wait_timeline);
	if (wait_timeline != NULL) {
		cursor->wait_timeline = wlr_drm_syncobj_timeline_ref(wait_timeline);
		cursor->wait_point = wait_point;
	} else {
		cursor->wait_timeline = NULL;
		cursor->wait_point = 0;
	}

	wl_list_remove(&cursor->renderer_destroy.link);
	if (texture != NULL) {
		cursor->renderer_destroy.notify = output_cursor_handle_renderer_destroy;
		wl_signal_add(&texture->renderer->events.destroy, &cursor->renderer_destroy);
	} else {
		wl_list_init(&cursor->renderer_destroy.link);
	}

	if (output->hardware_cursor == NULL || output->hardware_cursor == cursor) {
		if (output_cursor_attempt_hardware(cursor)) {
			return true;
		}

		wlr_log(WLR_DEBUG, "Falling back to software cursor on output '%s'", output->name);
		output_disable_hardware_cursor(output);
	}

	output_cursor_damage_whole(cursor);
	return true;
}

bool wlr_output_cursor_move(struct wlr_output_cursor *cursor,
		double x, double y) {
	// Scale coordinates for the output
	x *= cursor->output->scale;
	y *= cursor->output->scale;

	if (cursor->x == x && cursor->y == y) {
		return true;
	}

	if (cursor->output->hardware_cursor != cursor) {
		output_cursor_damage_whole(cursor);
	}

	cursor->x = x;
	cursor->y = y;
	bool was_visible = cursor->visible;
	output_cursor_update_visible(cursor);

	if (!was_visible && !cursor->visible) {
		// Cursor is still hidden, do nothing
		return true;
	}

	if (cursor->output->hardware_cursor != cursor) {
		output_cursor_damage_whole(cursor);
		return true;
	}

	return output_move_hardware_cursor(cursor->output, (int)x, (int)y);
}

void wlr_output_cursor_set_scale(struct wlr_output_cursor *cursor, float scale) {
	if (cursor->scale == scale) {
		return;
	}

	if (cursor->output->hardware_cursor != cursor) {
		output_cursor_damage_whole(cursor);
	}

	cursor->scale = scale;
	bool was_visible = cursor->visible;
	output_cursor_update_visible(cursor);

	if (!was_visible && !cursor->visible) {
		// Cursor is still hidden, do nothing
		return;
	}

	if (cursor->output->hardware_cursor != cursor) {
		output_cursor_damage_whole(cursor);
		return;
	}

	wlr_output_update_needs_frame(cursor->output);
}

struct wlr_output_cursor *wlr_output_cursor_create(struct wlr_output *output) {
	struct wlr_output_cursor *cursor = calloc(1, sizeof(*cursor));
	if (cursor == NULL) {
		return NULL;
	}
	cursor->output = output;
	cursor->scale = 1.0f;
	wl_list_insert(&output->cursors, &cursor->link);
	cursor->visible = true; // default position is at (0, 0)
	wl_list_init(&cursor->renderer_destroy.link);
	output_cursor_refresh_color_transform(cursor, output->image_description);
	return cursor;
}

void wlr_output_cursor_destroy(struct wlr_output_cursor *cursor) {
	if (cursor == NULL) {
		return;
	}
	if (cursor->output->hardware_cursor == cursor) {
		// If this cursor was the hardware cursor, disable it
		output_disable_hardware_cursor(cursor->output);
	} else {
		output_cursor_damage_whole(cursor);
	}
	wl_list_remove(&cursor->renderer_destroy.link);
	if (cursor->own_texture) {
		wlr_texture_destroy(cursor->texture);
	}
	wlr_drm_syncobj_timeline_unref(cursor->wait_timeline);
	wl_list_remove(&cursor->link);
	wlr_color_transform_unref(cursor->color_transform);
	free(cursor);
}

bool output_cursor_refresh_color_transform(struct wlr_output_cursor *output_cursor,
		const struct wlr_output_image_description *img_desc) {
	wlr_color_transform_unref(output_cursor->color_transform);
	output_cursor->color_transform = NULL;
	if (img_desc == NULL) {
		return true;
	}

	struct wlr_color_primaries primaries_srgb;
	wlr_color_primaries_from_named(&primaries_srgb, WLR_COLOR_NAMED_PRIMARIES_SRGB);
	struct wlr_color_primaries primaries;
	wlr_color_primaries_from_named(&primaries, img_desc->primaries);
	float matrix[9];
	wlr_color_primaries_transform_absolute_colorimetric(&primaries_srgb, &primaries, matrix);

	// Source is sRGB, which has reference == max
	struct wlr_color_luminances dst_lum;
	wlr_color_transfer_function_get_default_luminance(img_desc->transfer_function, &dst_lum);
	float luminance_multiplier = dst_lum.reference / dst_lum.max;
	for (int i = 0; i < 9; ++i) {
		matrix[i] *= luminance_multiplier;
	}

	struct wlr_color_transform *transforms[] = {
		wlr_color_transform_init_matrix(matrix),
		wlr_color_transform_init_linear_to_inverse_eotf(img_desc->transfer_function),
	};
	if (transforms[0] == NULL || transforms[1] == NULL) {
		wlr_color_transform_unref(transforms[0]);
		wlr_color_transform_unref(transforms[1]);
		return false;
	}
	output_cursor->color_transform = wlr_color_transform_init_pipeline(transforms,
		sizeof(transforms) / sizeof(transforms[0]));
	wlr_color_transform_unref(transforms[0]);
	wlr_color_transform_unref(transforms[1]);
	return output_cursor->color_transform != NULL;
}
