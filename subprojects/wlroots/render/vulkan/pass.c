#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include <wlr/render/color.h>
#include <wlr/render/drm_syncobj.h>

#include "render/color.h"
#include "render/vulkan.h"
#include "util/matrix.h"

static const struct wlr_render_pass_impl render_pass_impl;
static const struct wlr_addon_interface vk_color_transform_impl;

static struct wlr_vk_render_pass *get_render_pass(struct wlr_render_pass *wlr_pass) {
	assert(wlr_pass->impl == &render_pass_impl);
	struct wlr_vk_render_pass *pass = wl_container_of(wlr_pass, pass, base);
	return pass;
}

static struct wlr_vk_color_transform *get_color_transform(
		struct wlr_color_transform *c, struct wlr_vk_renderer *renderer) {
	struct wlr_addon *a = wlr_addon_find(&c->addons, renderer, &vk_color_transform_impl);
	if (!a) {
		return NULL;
	}
	struct wlr_vk_color_transform *transform = wl_container_of(a, transform, addon);
	return transform;
}

static void bind_pipeline(struct wlr_vk_render_pass *pass, VkPipeline pipeline) {
	if (pipeline == pass->bound_pipeline) {
		return;
	}

	vkCmdBindPipeline(pass->command_buffer->vk, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	pass->bound_pipeline = pipeline;
}

static void get_clip_region(struct wlr_vk_render_pass *pass,
		const pixman_region32_t *in, pixman_region32_t *out) {
	if (in != NULL) {
		pixman_region32_init(out);
		pixman_region32_copy(out, in);
	} else {
		struct wlr_buffer *buffer = pass->render_buffer->wlr_buffer;
		pixman_region32_init_rect(out, 0, 0, buffer->width, buffer->height);
	}
}

static void convert_pixman_box_to_vk_rect(const pixman_box32_t *box, VkRect2D *rect) {
	*rect = (VkRect2D){
		.offset = { .x = box->x1, .y = box->y1 },
		.extent = { .width = box->x2 - box->x1, .height = box->y2 - box->y1 },
	};
}

static float color_to_linear(float non_linear) {
	return pow(non_linear, 2.2);
}

static float color_to_linear_premult(float non_linear, float alpha) {
	return (alpha == 0) ? 0 : color_to_linear(non_linear / alpha) * alpha;
}

static void encode_proj_matrix(const float mat3[9], float mat4[4][4]) {
	float result[4][4] = {
		{ mat3[0], mat3[1], 0, mat3[2] },
		{ mat3[3], mat3[4], 0, mat3[5] },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 },
	};

	memcpy(mat4, result, sizeof(result));
}

static void encode_color_matrix(const float mat3[9], float mat4[4][4]) {
	float result[4][4] = {
		{ mat3[0], mat3[1], mat3[2], 0 },
		{ mat3[3], mat3[4], mat3[5], 0 },
		{ mat3[6], mat3[7], mat3[8], 0 },
		{ 0, 0, 0, 0 },
	};

	memcpy(mat4, result, sizeof(result));
}

static void render_pass_destroy(struct wlr_vk_render_pass *pass) {
	struct wlr_vk_render_pass_texture *pass_texture;
	wl_array_for_each(pass_texture, &pass->textures) {
		wlr_drm_syncobj_timeline_unref(pass_texture->wait_timeline);
	}

	wlr_color_transform_unref(pass->color_transform);
	wlr_drm_syncobj_timeline_unref(pass->signal_timeline);
	rect_union_finish(&pass->updated_region);
	wl_array_release(&pass->textures);
	free(pass);
}

static VkSemaphore render_pass_wait_sync_file(struct wlr_vk_render_pass *pass,
		size_t sem_index, int sync_file_fd) {
	struct wlr_vk_renderer *renderer = pass->renderer;
	struct wlr_vk_command_buffer *render_cb = pass->command_buffer;
	VkResult res;

	VkSemaphore *wait_semaphores = render_cb->wait_semaphores.data;
	size_t wait_semaphores_len = render_cb->wait_semaphores.size / sizeof(wait_semaphores[0]);

	VkSemaphore *sem_ptr;
	if (sem_index >= wait_semaphores_len) {
		sem_ptr = wl_array_add(&render_cb->wait_semaphores, sizeof(*sem_ptr));
		if (sem_ptr == NULL) {
			return VK_NULL_HANDLE;
		}
		*sem_ptr = VK_NULL_HANDLE;
	} else {
		sem_ptr = &wait_semaphores[sem_index];
	}

	if (*sem_ptr == VK_NULL_HANDLE) {
		VkSemaphoreCreateInfo semaphore_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};
		res = vkCreateSemaphore(renderer->dev->dev, &semaphore_info, NULL, sem_ptr);
		if (res != VK_SUCCESS) {
			wlr_vk_error("vkCreateSemaphore", res);
			return VK_NULL_HANDLE;
		}
	}

	VkImportSemaphoreFdInfoKHR import_info = {
		.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
		.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
		.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
		.semaphore = *sem_ptr,
		.fd = sync_file_fd,
	};
	res = renderer->dev->api.vkImportSemaphoreFdKHR(renderer->dev->dev, &import_info);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkImportSemaphoreFdKHR", res);
		return VK_NULL_HANDLE;
	}

	return *sem_ptr;
}

static float get_luminance_multiplier(const struct wlr_color_luminances *src_lum,
		const struct wlr_color_luminances *dst_lum) {
	return (dst_lum->reference / src_lum->reference) * (src_lum->max / dst_lum->max);
}

static bool render_pass_submit(struct wlr_render_pass *wlr_pass) {
	struct wlr_vk_render_pass *pass = get_render_pass(wlr_pass);
	struct wlr_vk_renderer *renderer = pass->renderer;
	struct wlr_vk_command_buffer *render_cb = pass->command_buffer;
	struct wlr_vk_render_buffer *render_buffer = pass->render_buffer;
	struct wlr_vk_command_buffer *stage_cb = NULL;
	VkSemaphoreSubmitInfoKHR *render_wait = NULL;
	bool device_lost = false;

	if (pass->failed) {
		goto error;
	}

	if (vulkan_record_stage_cb(renderer) == VK_NULL_HANDLE) {
		goto error;
	}

	stage_cb = renderer->stage.cb;
	assert(stage_cb != NULL);
	renderer->stage.cb = NULL;

	if (pass->two_pass) {
		// Apply output shader to map blend image to actual output image
		vkCmdNextSubpass(render_cb->vk, VK_SUBPASS_CONTENTS_INLINE);

		int width = pass->render_buffer->wlr_buffer->width;
		int height = pass->render_buffer->wlr_buffer->height;

		float final_matrix[9] = {
			width, 0, -1,
			0, height, -1,
			0, 0, 0,
		};
		struct wlr_vk_vert_pcr_data vert_pcr_data = {
			.uv_off = { 0, 0 },
			.uv_size = { 1, 1 },
		};
		encode_proj_matrix(final_matrix, vert_pcr_data.mat4);

		struct wlr_vk_color_transform *transform = NULL;
		size_t dim = 1;
		if (pass->color_transform && pass->color_transform->type != COLOR_TRANSFORM_INVERSE_EOTF) {
			transform = get_color_transform(pass->color_transform, renderer);
			assert(transform);
			dim = transform->lut_3d.dim;
		}

		struct wlr_vk_frag_output_pcr_data frag_pcr_data = {
			.luminance_multiplier = 1,
			.lut_3d_offset = 0.5f / dim,
			.lut_3d_scale = (float)(dim - 1) / dim,
		};

		float matrix[9];
		if (pass->has_primaries) {
			struct wlr_color_primaries srgb;
			wlr_color_primaries_from_named(&srgb, WLR_COLOR_NAMED_PRIMARIES_SRGB);

			float srgb_to_xyz[9];
			wlr_color_primaries_to_xyz(&srgb, srgb_to_xyz);
			float dst_primaries_to_xyz[9];
			wlr_color_primaries_to_xyz(&pass->primaries, dst_primaries_to_xyz);
			float xyz_to_dst_primaries[9];
			matrix_invert(xyz_to_dst_primaries, dst_primaries_to_xyz);

			wlr_matrix_multiply(matrix, xyz_to_dst_primaries, srgb_to_xyz);
		} else {
			wlr_matrix_identity(matrix);
		}
		encode_color_matrix(matrix, frag_pcr_data.matrix);

		VkPipeline pipeline = VK_NULL_HANDLE;
		if (pass->color_transform && pass->color_transform->type != COLOR_TRANSFORM_INVERSE_EOTF) {
			pipeline = render_buffer->two_pass.render_setup->output_pipe_lut3d;
		} else {
			enum wlr_color_transfer_function tf = WLR_COLOR_TRANSFER_FUNCTION_GAMMA22;
			if (pass->color_transform && pass->color_transform->type == COLOR_TRANSFORM_INVERSE_EOTF) {
				struct wlr_color_transform_inverse_eotf *inverse_eotf =
					wlr_color_transform_inverse_eotf_from_base(pass->color_transform);
				tf = inverse_eotf->tf;
			}

			switch (tf) {
			case WLR_COLOR_TRANSFER_FUNCTION_EXT_LINEAR:
				pipeline = render_buffer->two_pass.render_setup->output_pipe_identity;
				break;
			case WLR_COLOR_TRANSFER_FUNCTION_SRGB:
				pipeline = render_buffer->two_pass.render_setup->output_pipe_srgb;
				break;
			case WLR_COLOR_TRANSFER_FUNCTION_ST2084_PQ:
				pipeline = render_buffer->two_pass.render_setup->output_pipe_pq;
				break;
			case WLR_COLOR_TRANSFER_FUNCTION_GAMMA22:
				pipeline = render_buffer->two_pass.render_setup->output_pipe_gamma22;
				break;
			case WLR_COLOR_TRANSFER_FUNCTION_BT1886:
				pipeline = render_buffer->two_pass.render_setup->output_pipe_bt1886;
				break;
			}

			struct wlr_color_luminances srgb_lum, dst_lum;
			wlr_color_transfer_function_get_default_luminance(
				WLR_COLOR_TRANSFER_FUNCTION_SRGB, &srgb_lum);
			wlr_color_transfer_function_get_default_luminance(tf, &dst_lum);
			frag_pcr_data.luminance_multiplier = get_luminance_multiplier(&srgb_lum, &dst_lum);
		}
		bind_pipeline(pass, pipeline);
		vkCmdPushConstants(render_cb->vk, renderer->output_pipe_layout,
			VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vert_pcr_data), &vert_pcr_data);
		vkCmdPushConstants(render_cb->vk, renderer->output_pipe_layout,
			VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vert_pcr_data),
			sizeof(frag_pcr_data), &frag_pcr_data);

		VkDescriptorSet lut_ds;
		if (transform != NULL) {
			lut_ds = transform->lut_3d.ds;
		} else {
			lut_ds = renderer->output_ds_lut3d_dummy;
		}
		VkDescriptorSet ds[] = {
			render_buffer->two_pass.blend_descriptor_set, // set 0
			lut_ds, // set 1
		};
		size_t ds_len = sizeof(ds) / sizeof(ds[0]);
		vkCmdBindDescriptorSets(render_cb->vk,
			VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->output_pipe_layout,
			0, ds_len, ds, 0, NULL);

		const pixman_region32_t *clip = rect_union_evaluate(&pass->updated_region);
		int clip_rects_len;
		const pixman_box32_t *clip_rects = pixman_region32_rectangles(
			clip, &clip_rects_len);
		for (int i = 0; i < clip_rects_len; i++) {
			VkRect2D rect;
			convert_pixman_box_to_vk_rect(&clip_rects[i], &rect);
			vkCmdSetScissor(render_cb->vk, 0, 1, &rect);
			vkCmdDraw(render_cb->vk, 4, 1, 0, 0);
		}
	}

	vkCmdEndRenderPass(render_cb->vk);

	size_t pass_textures_len = pass->textures.size / sizeof(struct wlr_vk_render_pass_texture);
	size_t render_wait_cap = pass_textures_len * WLR_DMABUF_MAX_PLANES;
	render_wait = calloc(render_wait_cap, sizeof(*render_wait));
	if (render_wait == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		goto error;
	}

	uint32_t barrier_count = wl_list_length(&renderer->foreign_textures) + 1;
	VkImageMemoryBarrier *acquire_barriers = calloc(barrier_count, sizeof(*acquire_barriers));
	VkImageMemoryBarrier *release_barriers = calloc(barrier_count, sizeof(*release_barriers));
	if (acquire_barriers == NULL || release_barriers == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		free(acquire_barriers);
		free(release_barriers);
		goto error;
	}

	struct wlr_vk_texture *texture, *tmp_tex;
	size_t idx = 0;
	wl_list_for_each_safe(texture, tmp_tex, &renderer->foreign_textures, foreign_link) {
		if (!texture->transitioned) {
			texture->transitioned = true;
		}

		// acquire
		acquire_barriers[idx] = (VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
			.dstQueueFamilyIndex = renderer->dev->queue_family,
			.image = texture->image,
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcAccessMask = 0, // ignored anyways
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.layerCount = 1,
			.subresourceRange.levelCount = 1,
		};

		// release
		release_barriers[idx] = (VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcQueueFamilyIndex = renderer->dev->queue_family,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
			.image = texture->image,
			.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.dstAccessMask = 0, // ignored anyways
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.layerCount = 1,
			.subresourceRange.levelCount = 1,
		};

		++idx;

		wl_list_remove(&texture->foreign_link);
		texture->owned = false;
	}

	uint32_t render_wait_len = 0;
	struct wlr_vk_render_pass_texture *pass_texture;
	wl_array_for_each(pass_texture, &pass->textures) {
		int sync_file_fds[WLR_DMABUF_MAX_PLANES];
		for (size_t i = 0; i < WLR_DMABUF_MAX_PLANES; i++) {
			sync_file_fds[i] = -1;
		}

		if (pass_texture->wait_timeline) {
			int sync_file_fd = wlr_drm_syncobj_timeline_export_sync_file(pass_texture->wait_timeline, pass_texture->wait_point);
			if (sync_file_fd < 0) {
				wlr_log(WLR_ERROR, "Failed to export wait timeline point as sync_file");
				continue;
			}

			sync_file_fds[0] = sync_file_fd;
		} else {
			struct wlr_vk_texture *texture = pass_texture->texture;
			if (!vulkan_sync_foreign_texture(texture, sync_file_fds)) {
				wlr_log(WLR_ERROR, "Failed to wait for foreign texture DMA-BUF fence");
				continue;
			}
		}

		for (size_t i = 0; i < WLR_DMABUF_MAX_PLANES; i++) {
			if (sync_file_fds[i] < 0) {
				continue;
			}

			VkSemaphore sem = render_pass_wait_sync_file(pass, render_wait_len, sync_file_fds[i]);
			if (sem == VK_NULL_HANDLE) {
				close(sync_file_fds[i]);
				continue;
			}

			render_wait[render_wait_len] = (VkSemaphoreSubmitInfoKHR){
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
				.semaphore = sem,
				.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR,
			};

			render_wait_len++;
		}
	}

	// also add acquire/release barriers for the current render buffer
	VkImageLayout src_layout = VK_IMAGE_LAYOUT_GENERAL;
	if (!pass->render_buffer_out->transitioned) {
		src_layout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		pass->render_buffer_out->transitioned = true;
	}

	if (pass->two_pass) {
		// The render pass changes the blend image layout from
		// color attachment to read only, so on each frame, before
		// the render pass starts, we change it back
		VkImageLayout blend_src_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		if (!render_buffer->two_pass.blend_transitioned) {
			blend_src_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			render_buffer->two_pass.blend_transitioned = true;
		}

		VkImageMemoryBarrier blend_acq_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = render_buffer->two_pass.blend_image,
			.oldLayout = blend_src_layout,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.layerCount = 1,
				.levelCount = 1,
			},
		};
		vkCmdPipelineBarrier(stage_cb->vk, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, NULL, 0, NULL, 1, &blend_acq_barrier);
	}

	// acquire render buffer before rendering
	acquire_barriers[idx] = (VkImageMemoryBarrier){
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
		.dstQueueFamilyIndex = renderer->dev->queue_family,
		.image = render_buffer->image,
		.oldLayout = src_layout,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.srcAccessMask = 0, // ignored anyways
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.layerCount = 1,
		.subresourceRange.levelCount = 1,
	};

	// release render buffer after rendering
	release_barriers[idx] = (VkImageMemoryBarrier){
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcQueueFamilyIndex = renderer->dev->queue_family,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
		.image = render_buffer->image,
		.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = 0, // ignored anyways
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.layerCount = 1,
		.subresourceRange.levelCount = 1,
	};

	++idx;

	vkCmdPipelineBarrier(stage_cb->vk, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0, 0, NULL, 0, NULL, barrier_count, acquire_barriers);

	vkCmdPipelineBarrier(render_cb->vk, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL,
		barrier_count, release_barriers);

	free(acquire_barriers);
	free(release_barriers);

	// No semaphores needed here.
	// We don't need a semaphore from the stage/transfer submission
	// to the render submissions since they are on the same queue
	// and we have a renderpass dependency for that.
	uint64_t stage_timeline_point = vulkan_end_command_buffer(stage_cb, renderer);
	if (stage_timeline_point == 0) {
		goto error;
	}

	VkCommandBufferSubmitInfoKHR stage_cb_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR,
		.commandBuffer = stage_cb->vk,
	};
	VkSemaphoreSubmitInfoKHR stage_signal = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
		.semaphore = renderer->timeline_semaphore,
		.value = stage_timeline_point,
	};
	VkSubmitInfo2KHR stage_submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &stage_cb_info,
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = &stage_signal,
	};

	VkSemaphoreSubmitInfoKHR stage_wait;
	if (renderer->stage.last_timeline_point > 0) {
		stage_wait = (VkSemaphoreSubmitInfoKHR){
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
			.semaphore = renderer->timeline_semaphore,
			.value = renderer->stage.last_timeline_point,
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR,
		};

		stage_submit.waitSemaphoreInfoCount = 1;
		stage_submit.pWaitSemaphoreInfos = &stage_wait;
	}

	renderer->stage.last_timeline_point = stage_timeline_point;

	uint64_t render_timeline_point = vulkan_end_command_buffer(render_cb, renderer);
	if (render_timeline_point == 0) {
		goto error;
	}

	uint32_t render_signal_len = 1;
	VkSemaphoreSubmitInfoKHR render_signal[2] = {0};
	render_signal[0] = (VkSemaphoreSubmitInfoKHR){
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
		.semaphore = renderer->timeline_semaphore,
		.value = render_timeline_point,
	};
	if (renderer->dev->implicit_sync_interop || pass->signal_timeline != NULL) {
		if (render_cb->binary_semaphore == VK_NULL_HANDLE) {
			VkExportSemaphoreCreateInfo export_info = {
				.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
				.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
			};
			VkSemaphoreCreateInfo semaphore_info = {
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
				.pNext = &export_info,
			};
			VkResult res = vkCreateSemaphore(renderer->dev->dev, &semaphore_info,
				NULL, &render_cb->binary_semaphore);
			if (res != VK_SUCCESS) {
				wlr_vk_error("vkCreateSemaphore", res);
				goto error;
			}
		}

		render_signal[render_signal_len++] = (VkSemaphoreSubmitInfoKHR){
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
			.semaphore = render_cb->binary_semaphore,
		};
	}

	VkCommandBufferSubmitInfoKHR render_cb_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR,
		.commandBuffer = render_cb->vk,
	};
	VkSubmitInfo2KHR render_submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,
		.waitSemaphoreInfoCount = render_wait_len,
		.pWaitSemaphoreInfos = render_wait,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &render_cb_info,
		.signalSemaphoreInfoCount = render_signal_len,
		.pSignalSemaphoreInfos = render_signal,
	};

	VkSubmitInfo2KHR submit_info[] = { stage_submit, render_submit };
	VkResult res = renderer->dev->api.vkQueueSubmit2KHR(renderer->dev->queue, 2, submit_info, VK_NULL_HANDLE);

	if (res != VK_SUCCESS) {
		device_lost = res == VK_ERROR_DEVICE_LOST;
		wlr_vk_error("vkQueueSubmit", res);
		goto error;
	}

	free(render_wait);

	struct wlr_vk_shared_buffer *stage_buf, *stage_buf_tmp;
	wl_list_for_each_safe(stage_buf, stage_buf_tmp, &renderer->stage.buffers, link) {
		if (stage_buf->allocs.size == 0) {
			continue;
		}
		wl_list_remove(&stage_buf->link);
		wl_list_insert(&stage_cb->stage_buffers, &stage_buf->link);
	}

	if (!vulkan_sync_render_buffer(renderer, render_buffer, render_cb,
			pass->signal_timeline, pass->signal_point)) {
		wlr_log(WLR_ERROR, "Failed to sync render buffer");
	}

	render_pass_destroy(pass);
	wlr_buffer_unlock(render_buffer->wlr_buffer);
	return true;

error:
	free(render_wait);
	vulkan_reset_command_buffer(stage_cb);
	vulkan_reset_command_buffer(render_cb);
	wlr_buffer_unlock(render_buffer->wlr_buffer);
	render_pass_destroy(pass);

	if (device_lost) {
		wl_signal_emit_mutable(&renderer->wlr_renderer.events.lost, NULL);
	}

	return false;
}

static void render_pass_mark_box_updated(struct wlr_vk_render_pass *pass,
		const struct wlr_box *box) {
	if (!pass->two_pass) {
		return;
	}

	pixman_box32_t pixman_box = {
		.x1 = box->x,
		.x2 = box->x + box->width,
		.y1 = box->y,
		.y2 = box->y + box->height,
	};
	rect_union_add(&pass->updated_region, pixman_box);
}

static void render_pass_add_rect(struct wlr_render_pass *wlr_pass,
		const struct wlr_render_rect_options *options) {
	struct wlr_vk_render_pass *pass = get_render_pass(wlr_pass);
	VkCommandBuffer cb = pass->command_buffer->vk;

	// Input color values are given in sRGB space, shader expects
	// them in linear space. The shader does all computation in linear
	// space and expects in inputs in linear space since it outputs
	// colors in linear space as well (and vulkan then automatically
	// does the conversion for out sRGB render targets).
	float linear_color[] = {
		color_to_linear_premult(options->color.r, options->color.a),
		color_to_linear_premult(options->color.g, options->color.a),
		color_to_linear_premult(options->color.b, options->color.a),
		options->color.a, // no conversion for alpha
	};

	pixman_region32_t clip;
	get_clip_region(pass, options->clip, &clip);

	int clip_rects_len;
	const pixman_box32_t *clip_rects = pixman_region32_rectangles(&clip, &clip_rects_len);
	// Record regions possibly updated for use in second subpass
	for (int i = 0; i < clip_rects_len; i++) {
		struct wlr_box clip_box = {
			.x = clip_rects[i].x1,
			.y = clip_rects[i].y1,
			.width = clip_rects[i].x2 - clip_rects[i].x1,
			.height = clip_rects[i].y2 - clip_rects[i].y1,
		};
		struct wlr_box intersection;
		if (!wlr_box_intersection(&intersection, &options->box, &clip_box)) {
			continue;
		}
		render_pass_mark_box_updated(pass, &intersection);
	}

	struct wlr_box box;
	wlr_render_rect_options_get_box(options, pass->render_buffer->wlr_buffer, &box);

	switch (options->blend_mode) {
	case WLR_RENDER_BLEND_MODE_PREMULTIPLIED:;
		float proj[9], matrix[9];
		wlr_matrix_identity(proj);
		wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL, proj);
		wlr_matrix_multiply(matrix, pass->projection, matrix);

		struct wlr_vk_pipeline *pipe = setup_get_or_create_pipeline(
			pass->render_setup,
			&(struct wlr_vk_pipeline_key) {
				.source = WLR_VK_SHADER_SOURCE_SINGLE_COLOR,
				.layout = { .ycbcr_format = NULL, .shader_layout = WLR_VK_SHADER_LAYOUT_STANDARD },
			});
		if (!pipe) {
			pass->failed = true;
			break;
		}

		struct wlr_vk_vert_pcr_data vert_pcr_data = {
			.uv_off = { 0, 0 },
			.uv_size = { 1, 1 },
		};
		encode_proj_matrix(matrix, vert_pcr_data.mat4);

		bind_pipeline(pass, pipe->vk);
		vkCmdPushConstants(cb, pipe->layout->vk,
			VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vert_pcr_data), &vert_pcr_data);
		vkCmdPushConstants(cb, pipe->layout->vk,
			VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vert_pcr_data), sizeof(float) * 4,
			linear_color);

		for (int i = 0; i < clip_rects_len; i++) {
			VkRect2D rect;
			convert_pixman_box_to_vk_rect(&clip_rects[i], &rect);
			vkCmdSetScissor(cb, 0, 1, &rect);
			vkCmdDraw(cb, 4, 1, 0, 0);
		}
		break;
	case WLR_RENDER_BLEND_MODE_NONE:;
		VkClearAttachment clear_att = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.colorAttachment = 0,
			.clearValue.color.float32 = {
				linear_color[0],
				linear_color[1],
				linear_color[2],
				linear_color[3],
			},
		};
		VkClearRect clear_rect = {
			.layerCount = 1,
		};
		for (int i = 0; i < clip_rects_len; i++) {
			convert_pixman_box_to_vk_rect(&clip_rects[i], &clear_rect.rect);
			vkCmdClearAttachments(cb, 1, &clear_att, 1, &clear_rect);
		}
		break;
	}

	pixman_region32_fini(&clip);
}

static void render_pass_add_texture(struct wlr_render_pass *wlr_pass,
		const struct wlr_render_texture_options *options) {
	struct wlr_vk_render_pass *pass = get_render_pass(wlr_pass);
	struct wlr_vk_renderer *renderer = pass->renderer;
	VkCommandBuffer cb = pass->command_buffer->vk;

	struct wlr_vk_texture *texture = vulkan_get_texture(options->texture);
	assert(texture->renderer == renderer);

	if (texture->dmabuf_imported && !texture->owned) {
		// Store this texture in the list of textures that need to be
		// acquired before rendering and released after rendering.
		// We don't do it here immediately since barriers inside
		// a renderpass are suboptimal (would require additional renderpass
		// dependency and potentially multiple barriers) and it's
		// better to issue one barrier for all used textures anyways.
		texture->owned = true;
		assert(texture->foreign_link.prev == NULL);
		assert(texture->foreign_link.next == NULL);
		wl_list_insert(&renderer->foreign_textures, &texture->foreign_link);
	}

	struct wlr_fbox src_box;
	wlr_render_texture_options_get_src_box(options, &src_box);
	struct wlr_box dst_box;
	wlr_render_texture_options_get_dst_box(options, &dst_box);
	float alpha = wlr_render_texture_options_get_alpha(options);

	float proj[9], matrix[9];
	wlr_matrix_identity(proj);
	wlr_matrix_project_box(matrix, &dst_box, options->transform, proj);
	wlr_matrix_multiply(matrix, pass->projection, matrix);

	struct wlr_vk_vert_pcr_data vert_pcr_data = {
		.uv_off = {
			src_box.x / options->texture->width,
			src_box.y / options->texture->height,
		},
		.uv_size = {
			src_box.width / options->texture->width,
			src_box.height / options->texture->height,
		},
	};
	encode_proj_matrix(matrix, vert_pcr_data.mat4);

	enum wlr_color_transfer_function tf = options->transfer_function;
	if (tf == 0) {
		tf = WLR_COLOR_TRANSFER_FUNCTION_GAMMA22;
	}

	bool srgb_image_view = false;
	enum wlr_vk_texture_transform tex_transform = 0;
	switch (tf) {
	case WLR_COLOR_TRANSFER_FUNCTION_SRGB:
		if (texture->using_mutable_srgb) {
			tex_transform = WLR_VK_TEXTURE_TRANSFORM_IDENTITY;
			srgb_image_view = true;
		} else {
			tex_transform = WLR_VK_TEXTURE_TRANSFORM_SRGB;
		}
		break;
	case WLR_COLOR_TRANSFER_FUNCTION_EXT_LINEAR:
		tex_transform = WLR_VK_TEXTURE_TRANSFORM_IDENTITY;
		break;
	case WLR_COLOR_TRANSFER_FUNCTION_ST2084_PQ:
		tex_transform = WLR_VK_TEXTURE_TRANSFORM_ST2084_PQ;
		break;
	case WLR_COLOR_TRANSFER_FUNCTION_GAMMA22:
		tex_transform = WLR_VK_TEXTURE_TRANSFORM_GAMMA22;
		break;
	case WLR_COLOR_TRANSFER_FUNCTION_BT1886:
		tex_transform = WLR_VK_TEXTURE_TRANSFORM_BT1886;
		break;
	}

	struct wlr_vk_pipeline *pipe = setup_get_or_create_pipeline(
		pass->render_setup,
		&(struct wlr_vk_pipeline_key) {
			.source = WLR_VK_SHADER_SOURCE_TEXTURE,
			.layout = {
				.ycbcr_format = texture->format->is_ycbcr ? texture->format : NULL,
				.filter_mode = options->filter_mode,
				.shader_layout = WLR_VK_SHADER_LAYOUT_STANDARD,
			},
			.texture_transform = tex_transform,
			.blend_mode = !texture->has_alpha && alpha == 1.0 ?
				WLR_RENDER_BLEND_MODE_NONE : options->blend_mode,
		});
	if (!pipe) {
		pass->failed = true;
		return;
	}

	struct wlr_vk_texture_view *view =
		vulkan_texture_get_or_create_view(texture, pipe->layout, srgb_image_view);
	if (!view) {
		pass->failed = true;
		return;
	}

	float color_matrix[9];
	if (options->primaries != NULL) {
		struct wlr_color_primaries srgb;
		wlr_color_primaries_from_named(&srgb, WLR_COLOR_NAMED_PRIMARIES_SRGB);

		float src_primaries_to_xyz[9];
		wlr_color_primaries_to_xyz(options->primaries, src_primaries_to_xyz);
		float srgb_to_xyz[9];
		wlr_color_primaries_to_xyz(&srgb, srgb_to_xyz);
		float xyz_to_srgb[9];
		matrix_invert(xyz_to_srgb, srgb_to_xyz);

		wlr_matrix_multiply(color_matrix, xyz_to_srgb, src_primaries_to_xyz);
	} else {
		wlr_matrix_identity(color_matrix);
	}

	float luminance_multiplier = 1;
	if (tf != WLR_COLOR_TRANSFER_FUNCTION_SRGB
			&& tf != WLR_COLOR_TRANSFER_FUNCTION_GAMMA22) {
		struct wlr_color_luminances src_lum, srgb_lum;
		wlr_color_transfer_function_get_default_luminance(tf, &src_lum);
		wlr_color_transfer_function_get_default_luminance(
			WLR_COLOR_TRANSFER_FUNCTION_SRGB, &srgb_lum);
		luminance_multiplier = get_luminance_multiplier(&src_lum, &srgb_lum);
	}

	struct wlr_vk_frag_texture_pcr_data frag_pcr_data = {
		.alpha = alpha,
		.luminance_multiplier = luminance_multiplier,
		.radius_top = options->radius_top,
		.radius_bottom = options->radius_bottom,
		.box = {
			options->dst_box.x, options->dst_box.y, options->dst_box.width, options->dst_box.height,
		},
	};
	encode_color_matrix(color_matrix, frag_pcr_data.matrix);

	bind_pipeline(pass, pipe->vk);

	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipe->layout->vk, 0, 1, &view->ds, 0, NULL);

	vkCmdPushConstants(cb, pipe->layout->vk,
		VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vert_pcr_data), &vert_pcr_data);
	vkCmdPushConstants(cb, pipe->layout->vk,
		VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vert_pcr_data),
		sizeof(frag_pcr_data), &frag_pcr_data);

	pixman_region32_t clip;
	get_clip_region(pass, options->clip, &clip);

	int clip_rects_len;
	const pixman_box32_t *clip_rects = pixman_region32_rectangles(&clip, &clip_rects_len);
	for (int i = 0; i < clip_rects_len; i++) {
		VkRect2D rect;
		convert_pixman_box_to_vk_rect(&clip_rects[i], &rect);
		vkCmdSetScissor(cb, 0, 1, &rect);
		vkCmdDraw(cb, 4, 1, 0, 0);

		struct wlr_box clip_box = {
			.x = clip_rects[i].x1,
			.y = clip_rects[i].y1,
			.width = clip_rects[i].x2 - clip_rects[i].x1,
			.height = clip_rects[i].y2 - clip_rects[i].y1,
		};
		struct wlr_box intersection;
		if (!wlr_box_intersection(&intersection, &dst_box, &clip_box)) {
			continue;
		}
		render_pass_mark_box_updated(pass, &intersection);
	}

	texture->last_used_cb = pass->command_buffer;

	pixman_region32_fini(&clip);

	if (texture->dmabuf_imported || (options != NULL && options->wait_timeline != NULL)) {
		struct wlr_vk_render_pass_texture *pass_texture =
			wl_array_add(&pass->textures, sizeof(*pass_texture));
		if (pass_texture == NULL) {
			pass->failed = true;
			return;
		}

		struct wlr_drm_syncobj_timeline *wait_timeline = NULL;
		uint64_t wait_point = 0;
		if (options != NULL && options->wait_timeline != NULL) {
			wait_timeline = wlr_drm_syncobj_timeline_ref(options->wait_timeline);
			wait_point = options->wait_point;
		}

		*pass_texture = (struct wlr_vk_render_pass_texture){
			.texture = texture,
			.wait_timeline = wait_timeline,
			.wait_point = wait_point,
		};
	}
}

static void render_pass_add_decoration(struct wlr_render_pass *wlr_pass,
		const struct wlr_render_decoration_options *options) {
	struct wlr_vk_render_pass *pass = get_render_pass(wlr_pass);
	VkCommandBuffer cb = pass->command_buffer->vk;
	pixman_region32_t clip;
	get_clip_region(pass, options->clip, &clip);

	int clip_rects_len;
	const pixman_box32_t *clip_rects = pixman_region32_rectangles(&clip, &clip_rects_len);
	// Record regions possibly updated for use in second subpass
	for (int i = 0; i < clip_rects_len; i++) {
		struct wlr_box clip_box = {
			.x = clip_rects[i].x1,
			.y = clip_rects[i].y1,
			.width = clip_rects[i].x2 - clip_rects[i].x1,
			.height = clip_rects[i].y2 - clip_rects[i].y1,
		};
		struct wlr_box intersection;
		if (!wlr_box_intersection(&intersection, &options->box, &clip_box)) {
			continue;
		}
		render_pass_mark_box_updated(pass, &intersection);
	}

	struct wlr_box box = options->box;

	switch (options->blend_mode) {
	case WLR_RENDER_BLEND_MODE_PREMULTIPLIED:;
		float proj[9], matrix[9];
		wlr_matrix_identity(proj);
		wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL, proj);
		wlr_matrix_multiply(matrix, pass->projection, matrix);

		struct wlr_vk_pipeline *pipe = setup_get_or_create_pipeline(
			pass->render_setup,
			&(struct wlr_vk_pipeline_key) {
				.source = WLR_VK_SHADER_SOURCE_DECORATION,
				.layout = { .ycbcr_format = NULL, .shader_layout = WLR_VK_SHADER_LAYOUT_DECORATION },
			});
		if (!pipe) {
			pass->failed = true;
			break;
		}

		struct wlr_vk_vert_pcr_data vert_pcr_data = {
			.uv_off = { 0, 0 },
			.uv_size = { 1, 1 },
		};
		encode_proj_matrix(matrix, vert_pcr_data.mat4);

		struct wlr_vk_frag_decoration_pcr_data frag_pcr_data = {
			.border = options->border,
			.title_bar = options->title_bar,
			.dim = options->dim,
			.border_width = options->border_width,
			.border_radius = options->border_radius,
			.title_bar_height = options->title_bar_height,
			.title_bar_border_radius = options->title_bar_border_radius,
			.box = {
				options->box.x, options->box.y, options->box.width, options->box.height,
			},
			.border_top = {
				color_to_linear_premult(options->border_top_color.r, options->border_top_color.a),
				color_to_linear_premult(options->border_top_color.g, options->border_top_color.a),
				color_to_linear_premult(options->border_top_color.b, options->border_top_color.a),
				options->border_top_color.a,
			},
			.border_bottom = {
				color_to_linear_premult(options->border_bottom_color.r, options->border_bottom_color.a),
				color_to_linear_premult(options->border_bottom_color.g, options->border_bottom_color.a),
				color_to_linear_premult(options->border_bottom_color.b, options->border_bottom_color.a),
				options->border_bottom_color.a,
			},
			.border_left = {
				color_to_linear_premult(options->border_left_color.r, options->border_left_color.a),
				color_to_linear_premult(options->border_left_color.g, options->border_left_color.a),
				color_to_linear_premult(options->border_left_color.b, options->border_left_color.a),
				options->border_left_color.a,
			},
			.border_right = {
				color_to_linear_premult(options->border_right_color.r, options->border_right_color.a),
				color_to_linear_premult(options->border_right_color.g, options->border_right_color.a),
				color_to_linear_premult(options->border_right_color.b, options->border_right_color.a),
				options->border_right_color.a,
			},
			.title_bar_color = {
				color_to_linear_premult(options->title_bar_color.r, options->title_bar_color.a),
				color_to_linear_premult(options->title_bar_color.g, options->title_bar_color.a),
				color_to_linear_premult(options->title_bar_color.b, options->title_bar_color.a),
				options->title_bar_color.a,
			},
			.dim_color = {
				color_to_linear_premult(options->dim_color.r, options->dim_color.a),
				color_to_linear_premult(options->dim_color.g, options->dim_color.a),
				color_to_linear_premult(options->dim_color.b, options->dim_color.a),
				options->dim_color.a,
			},
		};

		bind_pipeline(pass, pipe->vk);

		struct wlr_vk_object *deco = vulkan_get_object(options->object);
		struct wlr_vk_descriptor_set *ds = vulkan_object_get_ds(deco, pass->command_buffer);
		deco->last_used_cb = pass->command_buffer;

		vkCmdPushConstants(cb, pipe->layout->vk,
			VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vert_pcr_data), &vert_pcr_data);

		memcpy(deco->buffer->cpu_mapping, &frag_pcr_data, sizeof(struct wlr_vk_frag_decoration_pcr_data));
		VkDescriptorBufferInfo binfo = {
			.buffer = deco->buffer->buffer,
			.offset = 0,
			.range = sizeof(struct wlr_vk_frag_decoration_pcr_data),
		};
		VkWriteDescriptorSet write_ds = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = ds->ds,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.pBufferInfo = &binfo,
		};
		vkUpdateDescriptorSets(pass->renderer->dev->dev, 1, &write_ds, 0, NULL);
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipe->layout->vk, 0, 1, &ds->ds, 0, NULL);

		for (int i = 0; i < clip_rects_len; i++) {
			VkRect2D rect;
			convert_pixman_box_to_vk_rect(&clip_rects[i], &rect);
			vkCmdSetScissor(cb, 0, 1, &rect);
			vkCmdDraw(cb, 4, 1, 0, 0);
		}
		break;
	case WLR_RENDER_BLEND_MODE_NONE:;
		break;
	}

	pixman_region32_fini(&clip);
}

static void render_pass_add_shadow(struct wlr_render_pass *wlr_pass,
		const struct wlr_render_shadow_options *options) {
	struct wlr_vk_render_pass *pass = get_render_pass(wlr_pass);
	VkCommandBuffer cb = pass->command_buffer->vk;
	pixman_region32_t clip;
	get_clip_region(pass, options->clip, &clip);

	int clip_rects_len;
	const pixman_box32_t *clip_rects = pixman_region32_rectangles(&clip, &clip_rects_len);
	// Record regions possibly updated for use in second subpass
	for (int i = 0; i < clip_rects_len; i++) {
		struct wlr_box clip_box = {
			.x = clip_rects[i].x1,
			.y = clip_rects[i].y1,
			.width = clip_rects[i].x2 - clip_rects[i].x1,
			.height = clip_rects[i].y2 - clip_rects[i].y1,
		};
		struct wlr_box intersection;
		if (!wlr_box_intersection(&intersection, &options->box, &clip_box)) {
			continue;
		}
		render_pass_mark_box_updated(pass, &intersection);
	}

	struct wlr_box box = options->box;

	switch (options->blend_mode) {
	case WLR_RENDER_BLEND_MODE_PREMULTIPLIED:;
		float proj[9], matrix[9];
		wlr_matrix_identity(proj);
		wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL, proj);
		wlr_matrix_multiply(matrix, pass->projection, matrix);

		struct wlr_vk_pipeline *pipe = setup_get_or_create_pipeline(
			pass->render_setup,
			&(struct wlr_vk_pipeline_key) {
				.source = WLR_VK_SHADER_SOURCE_SHADOW,
				.layout = { .ycbcr_format = NULL, .shader_layout = WLR_VK_SHADER_LAYOUT_SHADOW },
			});
		if (!pipe) {
			pass->failed = true;
			break;
		}

		struct wlr_vk_vert_pcr_data vert_pcr_data = {
			.uv_off = { 0, 0 },
			.uv_size = { 1, 1 },
		};
		encode_proj_matrix(matrix, vert_pcr_data.mat4);

		struct wlr_vk_frag_shadow_pcr_data frag_pcr_data = {
			.enabled = options->enabled,
			.blur = options->blur,
			.radius_top = options->radius_top,
			.radius_bottom = options->radius_bottom,
			.box = {
				options->box.x, options->box.y, options->box.width, options->box.height,
			},
			.color = {
				color_to_linear_premult(options->color.r, options->color.a),
				color_to_linear_premult(options->color.g, options->color.a),
				color_to_linear_premult(options->color.b, options->color.a),
				options->color.a,
			},
		};

		bind_pipeline(pass, pipe->vk);

		struct wlr_vk_object *shadow = vulkan_get_object(options->object);
		struct wlr_vk_descriptor_set *ds = vulkan_object_get_ds(shadow, pass->command_buffer);
		shadow->last_used_cb = pass->command_buffer;

		vkCmdPushConstants(cb, pipe->layout->vk,
			VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vert_pcr_data), &vert_pcr_data);

		memcpy(shadow->buffer->cpu_mapping, &frag_pcr_data, sizeof(struct wlr_vk_frag_shadow_pcr_data));
		VkDescriptorBufferInfo binfo = {
			.buffer = shadow->buffer->buffer,
			.offset = 0,
			.range = sizeof(struct wlr_vk_frag_shadow_pcr_data),
		};
		VkWriteDescriptorSet write_ds = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = ds->ds,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.pBufferInfo = &binfo,
		};
		vkUpdateDescriptorSets(pass->renderer->dev->dev, 1, &write_ds, 0, NULL);
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipe->layout->vk, 0, 1, &ds->ds, 0, NULL);

		for (int i = 0; i < clip_rects_len; i++) {
			VkRect2D rect;
			convert_pixman_box_to_vk_rect(&clip_rects[i], &rect);
			vkCmdSetScissor(cb, 0, 1, &rect);
			vkCmdDraw(cb, 4, 1, 0, 0);
		}
		break;
	case WLR_RENDER_BLEND_MODE_NONE:;
		break;
	}

	pixman_region32_fini(&clip);
}

static const struct wlr_render_pass_impl render_pass_impl = {
	.submit = render_pass_submit,
	.add_rect = render_pass_add_rect,
	.add_texture = render_pass_add_texture,
	.add_decoration = render_pass_add_decoration,
	.add_shadow = render_pass_add_shadow,
};


void vk_color_transform_destroy(struct wlr_addon *addon) {
	struct wlr_vk_renderer *renderer = (struct wlr_vk_renderer *)addon->owner;
	struct wlr_vk_color_transform *transform = wl_container_of(addon, transform, addon);

	VkDevice dev = renderer->dev->dev;
	if (transform->lut_3d.image) {
		vkDestroyImage(dev, transform->lut_3d.image, NULL);
		vkDestroyImageView(dev, transform->lut_3d.image_view, NULL);
		vkFreeMemory(dev, transform->lut_3d.memory, NULL);
		vulkan_free_ds(renderer, transform->lut_3d.ds_pool, transform->lut_3d.ds);
	}

	wl_list_remove(&transform->link);
	wlr_addon_finish(&transform->addon);
	free(transform);
}

static bool create_3d_lut_image(struct wlr_vk_renderer *renderer,
		struct wlr_color_transform *tr, size_t dim_len,
		VkImage *image, VkImageView *image_view,
		VkDeviceMemory *memory, VkDescriptorSet *ds,
		struct wlr_vk_descriptor_pool **ds_pool) {
	VkDevice dev = renderer->dev->dev;
	VkResult res;

	*image = VK_NULL_HANDLE;
	*memory = VK_NULL_HANDLE;
	*image_view = VK_NULL_HANDLE;
	*ds = VK_NULL_HANDLE;
	*ds_pool = NULL;

	// R32G32B32 is not a required Vulkan format
	// TODO: use it when available
	VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;

	VkImageCreateInfo img_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_3D,
		.format = format,
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.extent = (VkExtent3D) { dim_len, dim_len, dim_len },
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	};
	res = vkCreateImage(dev, &img_info, NULL, image);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateImage failed", res);
		return NULL;
	}

	VkMemoryRequirements mem_reqs = {0};
	vkGetImageMemoryRequirements(dev, *image, &mem_reqs);

	int mem_type_index = vulkan_find_mem_type(renderer->dev,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mem_reqs.memoryTypeBits);
	if (mem_type_index == -1) {
		wlr_log(WLR_ERROR, "Failed to find suitable memory type");
		goto fail_image;
	}

	VkMemoryAllocateInfo mem_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.size,
		.memoryTypeIndex = mem_type_index,
	};
	res = vkAllocateMemory(dev, &mem_info, NULL, memory);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkAllocateMemory failed", res);
		goto fail_image;
	}

	res = vkBindImageMemory(dev, *image, *memory, 0);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkBindMemory failed", res);
		goto fail_memory;
	}

	VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.viewType = VK_IMAGE_VIEW_TYPE_3D,
		.format = format,
		.components.r = VK_COMPONENT_SWIZZLE_IDENTITY,
		.components.g = VK_COMPONENT_SWIZZLE_IDENTITY,
		.components.b = VK_COMPONENT_SWIZZLE_IDENTITY,
		.components.a = VK_COMPONENT_SWIZZLE_IDENTITY,
		.subresourceRange = (VkImageSubresourceRange) {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
		.image = *image,
	};
	res = vkCreateImageView(dev, &view_info, NULL, image_view);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateImageView failed", res);
		goto fail_image;
	}

	size_t bytes_per_block = 4 * sizeof(float);
	size_t size = dim_len * dim_len * dim_len * bytes_per_block;
	struct wlr_vk_buffer_span span = vulkan_get_stage_span(renderer,
		size, bytes_per_block);
	if (!span.buffer || span.alloc.size != size) {
		wlr_log(WLR_ERROR, "Failed to retrieve staging buffer");
		goto fail_imageview;
	}

	float sample_range = 1.0f / (dim_len - 1);
	char *map = (char *)span.buffer->cpu_mapping + span.alloc.start;
	float *dst = (float *)map;
	for (size_t b_index = 0; b_index < dim_len; b_index++) {
		for (size_t g_index = 0; g_index < dim_len; g_index++) {
			for (size_t r_index = 0; r_index < dim_len; r_index++) {
				size_t sample_index = r_index + dim_len * g_index + dim_len * dim_len * b_index;
				size_t dst_offset = 4 * sample_index;

				float rgb_in[3] = {
					r_index * sample_range,
					g_index * sample_range,
					b_index * sample_range,
				};
				float rgb_out[3];
				wlr_color_transform_eval(tr, rgb_out, rgb_in);

				dst[dst_offset] = rgb_out[0];
				dst[dst_offset + 1] = rgb_out[1];
				dst[dst_offset + 2] = rgb_out[2];
				dst[dst_offset + 3] = 1.0;
			}
		}
	}

	VkCommandBuffer cb = vulkan_record_stage_cb(renderer);
	vulkan_change_layout(cb, *image,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT);
	VkBufferImageCopy copy = {
		.bufferOffset = span.alloc.start,
		.imageExtent.width = dim_len,
		.imageExtent.height = dim_len,
		.imageExtent.depth = dim_len,
		.imageSubresource.layerCount = 1,
		.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	};
	vkCmdCopyBufferToImage(cb, span.buffer->buffer, *image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	vulkan_change_layout(cb, *image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_SHADER_READ_BIT);

	*ds_pool = vulkan_alloc_texture_ds(renderer,
		renderer->output_ds_lut3d_layout, ds);
	if (!*ds_pool) {
		wlr_log(WLR_ERROR, "Failed to allocate descriptor");
		goto fail_imageview;
	}

	VkDescriptorImageInfo ds_img_info = {
		.imageView = *image_view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkWriteDescriptorSet ds_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.dstSet = *ds,
		.pImageInfo = &ds_img_info,
	};
	vkUpdateDescriptorSets(dev, 1, &ds_write, 0, NULL);

	return true;

fail_imageview:
	vkDestroyImageView(dev, *image_view, NULL);
fail_memory:
	vkFreeMemory(dev, *memory, NULL);
fail_image:
	vkDestroyImage(dev, *image, NULL);
	return false;
}

static struct wlr_vk_color_transform *vk_color_transform_create(
		struct wlr_vk_renderer *renderer, struct wlr_color_transform *transform) {
	struct wlr_vk_color_transform *vk_transform =
		calloc(1, sizeof(*vk_transform));
	if (!vk_transform) {
		return NULL;
	}

	if (transform->type != COLOR_TRANSFORM_INVERSE_EOTF) {
		vk_transform->lut_3d.dim = 33;
		if (!create_3d_lut_image(renderer, transform,
				vk_transform->lut_3d.dim,
				&vk_transform->lut_3d.image,
				&vk_transform->lut_3d.image_view,
				&vk_transform->lut_3d.memory,
				&vk_transform->lut_3d.ds,
				&vk_transform->lut_3d.ds_pool)) {
			free(vk_transform);
			return NULL;
		}
	}

	wlr_addon_init(&vk_transform->addon, &transform->addons,
		renderer, &vk_color_transform_impl);
	wl_list_insert(&renderer->color_transforms, &vk_transform->link);

	return vk_transform;
}


static const struct wlr_addon_interface vk_color_transform_impl = {
	"vk_color_transform",
	.destroy = vk_color_transform_destroy,
};

struct wlr_vk_render_pass *vulkan_begin_render_pass(struct wlr_vk_renderer *renderer,
		struct wlr_vk_render_buffer *buffer, const struct wlr_buffer_pass_options *options) {
	uint32_t inv_eotf;
	if (options != NULL && options->color_transform != NULL) {
		if (options->color_transform->type == COLOR_TRANSFORM_INVERSE_EOTF) {
			struct wlr_color_transform_inverse_eotf *tr =
				wlr_color_transform_inverse_eotf_from_base(options->color_transform);
			inv_eotf = tr->tf;
		} else {
			// Color transform is not an inverse EOTF
			inv_eotf = 0;
		}
	} else {
		// This is the default when unspecified
		inv_eotf = WLR_COLOR_TRANSFER_FUNCTION_GAMMA22;
	}

	bool using_linear_pathway = inv_eotf == WLR_COLOR_TRANSFER_FUNCTION_EXT_LINEAR;
	bool using_srgb_pathway = inv_eotf == WLR_COLOR_TRANSFER_FUNCTION_SRGB &&
		buffer->srgb.out.framebuffer != VK_NULL_HANDLE;
	bool using_two_pass_pathway = !using_linear_pathway && !using_srgb_pathway;

	if (using_linear_pathway && !buffer->linear.out.image_view) {
		struct wlr_dmabuf_attributes attribs;
		wlr_buffer_get_dmabuf(buffer->wlr_buffer, &attribs);
		if (!vulkan_setup_one_pass_framebuffer(buffer, &attribs, false)) {
			wlr_log(WLR_ERROR, "Failed to set up blend image");
			return NULL;
		}
	}

	if (using_two_pass_pathway) {
		if (options != NULL && options->color_transform != NULL &&
				!get_color_transform(options->color_transform, renderer)) {
			/* Try to create a new color transform */
			if (!vk_color_transform_create(renderer, options->color_transform)) {
				wlr_log(WLR_ERROR, "Failed to create color transform");
				return NULL;
			}
		}

		if (!buffer->two_pass.out.image_view) {
			struct wlr_dmabuf_attributes attribs;
			wlr_buffer_get_dmabuf(buffer->wlr_buffer, &attribs);
			if (!vulkan_setup_two_pass_framebuffer(buffer, &attribs)) {
				wlr_log(WLR_ERROR, "Failed to set up blend image");
				return NULL;
			}
		}
	}

	struct wlr_vk_render_format_setup *render_setup;
	struct wlr_vk_render_buffer_out *buffer_out;
	if (using_two_pass_pathway) {
		render_setup = buffer->two_pass.render_setup;
		buffer_out = &buffer->two_pass.out;
	} else if (using_srgb_pathway) {
		render_setup = buffer->srgb.render_setup;
		buffer_out = &buffer->srgb.out;
	} else if (using_linear_pathway) {
		render_setup = buffer->linear.render_setup;
		buffer_out = &buffer->linear.out;
	} else {
		abort(); // unreachable
	}

	struct wlr_vk_render_pass *pass = calloc(1, sizeof(*pass));
	if (pass == NULL) {
		return NULL;
	}

	wlr_render_pass_init(&pass->base, &render_pass_impl);
	pass->renderer = renderer;
	pass->two_pass = using_two_pass_pathway;
	if (options != NULL && options->color_transform != NULL) {
		pass->color_transform = wlr_color_transform_ref(options->color_transform);
	}
	if (options != NULL && options->signal_timeline != NULL) {
		pass->signal_timeline = wlr_drm_syncobj_timeline_ref(options->signal_timeline);
		pass->signal_point = options->signal_point;
	}
	if (options != NULL && options->primaries != NULL) {
		pass->has_primaries = true;
		pass->primaries = *options->primaries;
	}

	rect_union_init(&pass->updated_region);

	struct wlr_vk_command_buffer *cb = vulkan_acquire_command_buffer(renderer);
	if (cb == NULL) {
		free(pass);
		return NULL;
	}

	VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	VkResult res = vkBeginCommandBuffer(cb->vk, &begin_info);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkBeginCommandBuffer", res);
		vulkan_reset_command_buffer(cb);
		free(pass);
		return NULL;
	}

	if (!renderer->dummy3d_image_transitioned) {
		renderer->dummy3d_image_transitioned = true;
		vulkan_change_layout(cb->vk, renderer->dummy3d_image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	int width = buffer->wlr_buffer->width;
	int height = buffer->wlr_buffer->height;
	VkRect2D rect = { .extent = { width, height } };

	VkRenderPassBeginInfo rp_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderArea = rect,
		.clearValueCount = 0,
		.renderPass = render_setup->render_pass,
		.framebuffer = buffer_out->framebuffer,
	};
	vkCmdBeginRenderPass(cb->vk, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdSetViewport(cb->vk, 0, 1, &(VkViewport){
		.width = width,
		.height = height,
		.maxDepth = 1,
	});

	// matrix_projection() assumes a GL coordinate system so we need
	// to pass WL_OUTPUT_TRANSFORM_FLIPPED_180 to adjust it for vulkan.
	matrix_projection(pass->projection, width, height, WL_OUTPUT_TRANSFORM_FLIPPED_180);

	wlr_buffer_lock(buffer->wlr_buffer);
	pass->render_buffer = buffer;
	pass->render_buffer_out = buffer_out;
	pass->render_setup = render_setup;
	pass->command_buffer = cb;
	return pass;
}
