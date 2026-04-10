/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_EXT_BACKGROUND_EFFECT_V1_H
#define WLR_TYPES_WLR_EXT_BACKGROUND_EFFECT_V1_H

#include <pixman.h>
#include <wayland-server-core.h>
#include <wayland-protocols/ext-background-effect-v1-enum.h>

struct wlr_surface;

struct wlr_ext_background_effect_surface_v1_state {
	pixman_region32_t blur_region;
};

struct wlr_ext_background_effect_manager_v1 {
	struct wl_global *global;
	uint32_t capabilities; // bitmask of enum ext_background_effect_manager_v1_capability

	struct {
		struct wl_signal destroy;
	} events;

	void *data;

	struct {
		struct wl_list resources; // wl_resource_get_link()
		struct wl_listener display_destroy;
	} WLR_PRIVATE;
};

struct wlr_ext_background_effect_manager_v1 *wlr_ext_background_effect_manager_v1_create(
	struct wl_display *display, uint32_t version, uint32_t capabilities);

/**
 * Get the committed background effect state for a surface.
 *
 * Returns NULL if the client has not attached a background effect object to
 * the surface.
 */
const struct wlr_ext_background_effect_surface_v1_state *
wlr_ext_background_effect_v1_get_surface_state(struct wlr_surface *surface);

#endif
