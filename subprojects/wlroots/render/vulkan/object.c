#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_object.h>
#include <wlr/util/log.h>
#include "render/vulkan.h"

static const struct wlr_object_impl object_impl;

static bool wlr_object_is_vk(struct wlr_object *wlr_object) {
	return wlr_object->impl == &object_impl;
}

struct wlr_vk_object *vulkan_get_object(struct wlr_object *wlr_object) {
	assert(wlr_object_is_vk(wlr_object));
	struct wlr_vk_object *object = wl_container_of(wlr_object, object, wlr_object);
	return object;
}

static void vk_object_destroy_instance(struct wlr_vk_object *object,
		struct wlr_vk_object_instance *instance) {
	vulkan_free_ds(object->renderer, instance->ds.ds_pool, instance->ds.ds);
	vulkan_destroy_mapped_uniform_buffer(object->renderer, instance->buffer);
	wl_list_remove(&instance->link);
	free(instance);
}

void vulkan_object_destroy(struct wlr_vk_object *object) {
	struct wlr_vk_object_instance *instance, *tmp_instance;
	wl_list_for_each_safe(instance, tmp_instance, &object->instances, link) {
		vk_object_destroy_instance(object, instance);
	}
	wl_list_remove(&object->link);
	free(object);
}

static void handle_vk_object_destroy(struct wlr_object *wlr_object) {
	struct wlr_vk_object *object = vulkan_get_object(wlr_object);
	// the object can be destroyed when there are no more command buffers
	// referencing it.
	if (object->last_used_cb != NULL) {
		assert(object->destroy_link.next == NULL); // not already inserted
		wl_list_insert(&object->last_used_cb->destroy_objects,
			&object->destroy_link);
		return;
	}

	vulkan_object_destroy(object);
}

static const struct wlr_object_impl object_impl = {
	.destroy = handle_vk_object_destroy,
};

static struct wlr_vk_object *vk_object_create(struct wlr_vk_renderer *renderer,
		enum wlr_object_type type, const void *owner) {
	struct wlr_vk_object *object;
	wl_list_for_each(object, &renderer->objects, link) {
		if (object->wlr_object.owner == owner) {
			return object;
		}
	}

	object = calloc(1, sizeof(*object));
	if (object == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	wlr_object_init(&object->wlr_object, &renderer->wlr_renderer,
		&object_impl, type, owner);
	object->renderer = renderer;

	wl_list_init(&object->instances);

	wl_list_insert(&renderer->objects, &object->link);
	return object;
}

struct wlr_object *vulkan_object_with_owner(struct wlr_renderer *wlr_renderer,
		enum wlr_object_type type, const void *owner) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	struct wlr_vk_object *object = vk_object_create(renderer, type, owner);
	if (object == NULL) {
		return NULL;
	}
	return &object->wlr_object;
}

struct wlr_vk_object_instance *vulkan_object_get_instance(struct wlr_vk_object *object,
		struct wlr_vk_command_buffer *cb) {
	struct wlr_vk_object_instance *instance;
	wl_list_for_each(instance, &object->instances, link) {
		if (instance->cb == NULL) {
			instance->cb = cb;
			return instance;
		}
	}

	struct wlr_vk_renderer *renderer = object->renderer;
	instance = calloc(1, sizeof(*instance));
	if (instance == NULL) {
		wlr_log_errno(WLR_ERROR, "vulkan_object_get_instance(): instance allocation failed");
		return NULL;
	}

	instance->cb = cb;

	VkDeviceSize size;
	switch (object->wlr_object.type) {
	case WLR_OBJECT_DECORATION:
		instance->ds.ds_pool = vulkan_alloc_object_ds(renderer, renderer->deco_ds_layout, &instance->ds.ds);
		size = sizeof(struct wlr_vk_frag_decoration_pcr_data);
		break;
	case WLR_OBJECT_SHADOW:
		instance->ds.ds_pool = vulkan_alloc_object_ds(renderer, renderer->shadow_ds_layout, &instance->ds.ds);
		size = sizeof(struct wlr_vk_frag_shadow_pcr_data);
		break;
	default:
		wlr_log(WLR_ERROR, "vulkan_object_get_instance(): unknown object type %d", object->wlr_object.type);
		free(instance);
		return NULL;
	}
	if (instance->ds.ds_pool == NULL) {
		free(instance);
		return NULL;
	}
	instance->buffer = vulkan_create_mapped_uniform_buffer(renderer, size);
	if (instance->buffer == NULL) {
		vulkan_free_ds(renderer, instance->ds.ds_pool, instance->ds.ds);
		wlr_log_errno(WLR_ERROR, "vulkan_object_get_instance(): buffer allocation failed");
		free(instance);
		return NULL;
	}

	wl_list_insert(&object->instances, &instance->link);
	return instance;
}
