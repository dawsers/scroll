#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_object.h>
#include <wlr/util/log.h>
#include "render/gles2.h"

static const struct wlr_object_impl object_impl;

static bool wlr_object_is_gles2(struct wlr_object *wlr_object) {
	return wlr_object->impl == &object_impl;
}

static struct wlr_gles2_object *gles2_get_object(
		struct wlr_object *wlr_object) {
	assert(wlr_object_is_gles2(wlr_object));
	struct wlr_gles2_object *object = wl_container_of(wlr_object, object, wlr_object);
	return object;
}

void gles2_object_destroy(struct wlr_gles2_object *object) {
	wl_list_remove(&object->link);
	free(object);
}

static void handle_gles2_object_destroy(struct wlr_object *object) {
	gles2_object_destroy(gles2_get_object(object));
}

static const struct wlr_object_impl object_impl = {
	.destroy = handle_gles2_object_destroy,
};

static struct wlr_gles2_object *gles2_object_create(struct wlr_gles2_renderer *renderer,
		enum wlr_object_type type, const void *owner) {
	struct wlr_gles2_object *object = calloc(1, sizeof(*object));
	if (object == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	wlr_object_init(&object->wlr_object, &renderer->wlr_renderer,
		&object_impl, type, owner);
	object->renderer = renderer;
	wl_list_insert(&renderer->objects, &object->link);
	return object;
}

struct wlr_object *gles2_object_with_owner(struct wlr_renderer *wlr_renderer,
		enum wlr_object_type type, const void *owner) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);
	struct wlr_gles2_object *object = gles2_object_create(renderer, type, owner);
	if (object == NULL) {
		return NULL;
	}
	return &object->wlr_object;
}
