/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_POINTER_WARP_V1_H
#define WLR_TYPES_WLR_POINTER_WARP_V1_H

#include <wayland-server-core.h>

struct wlr_pointer_warp_v1 {
	struct wl_global *global;

	struct {
		struct wl_signal destroy;
		struct wl_signal warp; // struct wlr_pointer_warp_v1_event_warp
	} events;

	void *data;

	struct {
		struct wl_listener display_destroy;
	} WLR_PRIVATE;
};

struct wlr_pointer_warp_v1_event_warp {
	struct wlr_surface *surface;
	struct wlr_seat_client *seat_client;
	double x, y;
	uint32_t serial;
};

struct wlr_pointer_warp_v1 *wlr_pointer_warp_v1_create(struct wl_display *display,
		uint32_t version);
#endif
