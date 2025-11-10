/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_WLR_OBJECT_H
#define WLR_RENDER_WLR_OBJECT_H

#include <wayland-server-core.h>

struct wlr_renderer;
struct wlr_object_impl;

enum wlr_object_type {
	WLR_OBJECT_DECORATION,
	WLR_OBJECT_SHADOW,
};

struct wlr_object {
	const struct wlr_object_impl *impl;
	const void *owner;
	struct wlr_renderer *renderer;
	enum wlr_object_type type;
};

/**
 * Destroys the object.
 */
void wlr_object_destroy(struct wlr_object *object);

/**
 * Create a new object with an owner and type.
 */
struct wlr_object *wlr_object_with_owner(struct wlr_renderer *renderer,
	enum wlr_object_type type, const void *owner);

#endif
