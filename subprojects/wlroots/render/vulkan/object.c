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

void vulkan_object_destroy(struct wlr_vk_object *object) {
	vulkan_destroy_mapped_uniform_buffer(object->renderer, object->buffer);
	struct wlr_vk_descriptor_set *ds, *tmp_ds;
	wl_list_for_each_safe(ds, tmp_ds, &object->dss, link) {
		switch (object->wlr_object.type) {
		case WLR_OBJECT_DECORATION:
			vulkan_free_ds(object->renderer, ds->ds_pool, ds->ds);
			break;
		case WLR_OBJECT_SHADOW:
			vulkan_free_ds(object->renderer, ds->ds_pool, ds->ds);
			break;
		}
		free(ds);
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

	wl_list_init(&object->dss);

	VkDeviceSize size;
	switch (type) {
	case WLR_OBJECT_DECORATION:
		size = sizeof(struct wlr_vk_frag_decoration_pcr_data);
		break;
	case WLR_OBJECT_SHADOW:
		size = sizeof(struct wlr_vk_frag_shadow_pcr_data);
		break;
	default:
		wlr_log(WLR_ERROR, "vk_object_create(): unknown object type %d", type);
		free(object);
		return NULL;
	}

	object->buffer = vulkan_create_mapped_uniform_buffer(renderer, size);
	if (object->buffer == NULL) {
		free(object);
		return NULL;
	}

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

struct wlr_vk_descriptor_set *vulkan_object_get_ds(struct wlr_vk_object *object,
		struct wlr_vk_command_buffer *cb) {
	struct wlr_vk_descriptor_set *ds;
	wl_list_for_each(ds, &object->dss, link) {
		if (ds->cb == cb) {
			return ds;
		}
	}

	struct wlr_vk_renderer *renderer = object->renderer;
	ds = calloc(1, sizeof(*ds));
	if (ds == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return 0;
	}

	switch (object->wlr_object.type) {
	case WLR_OBJECT_DECORATION:
		ds->ds_pool = vulkan_alloc_decoration_ds(renderer, renderer->deco_ds_layout, &ds->ds);
		break;
	case WLR_OBJECT_SHADOW:
		ds->ds_pool = vulkan_alloc_shadow_ds(renderer, renderer->shadow_ds_layout, &ds->ds);
		break;
	}

	wl_list_insert(&object->dss, &ds->link);
	return ds;
}
