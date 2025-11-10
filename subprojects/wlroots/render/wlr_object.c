#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_object.h>


void wlr_object_init(struct wlr_object *object, struct wlr_renderer *renderer,
	const struct wlr_object_impl *impl, enum wlr_object_type type, const void *owner) {
	assert(renderer);

	*object = (struct wlr_object){
		.renderer = renderer,
		.impl = impl,
		.owner = owner,
		.type = type,
	};
}

void wlr_object_destroy(struct wlr_object *object) {
	if (object && object->impl && object->impl->destroy) {
		object->impl->destroy(object);
	} else {
		free(object);
	}
}

struct wlr_object *wlr_object_with_owner(struct wlr_renderer *renderer,
		enum wlr_object_type type, const void *owner) {
	assert(owner);

	if (!renderer->impl->object_with_owner) {
		return NULL;
	}
	return renderer->impl->object_with_owner(renderer, type, owner);
}
