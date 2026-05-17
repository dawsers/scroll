#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_pointer_warp_v1.h>
#include <wlr/types/wlr_seat.h>
#include "pointer-warp-v1-protocol.h"

#define POINTER_WARP_VERSION 1

static const struct wp_pointer_warp_v1_interface pointer_warp_impl;

static struct wlr_pointer_warp_v1 *pointer_warp_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wp_pointer_warp_v1_interface,
		&pointer_warp_impl));
	return wl_resource_get_user_data(resource);
}

static void resource_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void pointer_warp_handle_warp_pointer(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *surface_resource,
		struct wl_resource *pointer_resource, wl_fixed_t sx, wl_fixed_t sy,
		uint32_t serial) {
	struct wlr_pointer_warp_v1 *pointer_warp =
		pointer_warp_from_resource(resource);
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_pointer_resource(pointer_resource);
	if (seat_client == NULL) {
		return;
	}

	struct wlr_pointer_warp_v1_event_warp event = {
		.surface = surface,
		.seat_client = seat_client,
		.x = wl_fixed_to_double(sx),
		.y = wl_fixed_to_double(sy),
		.serial = serial,
	};

	wl_signal_emit_mutable(&pointer_warp->events.warp, &event);
}

static const struct wp_pointer_warp_v1_interface pointer_warp_impl = {
	.destroy = resource_destroy,
	.warp_pointer = pointer_warp_handle_warp_pointer,
};

static void pointer_warp_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_pointer_warp_v1 *pointer_warp = data;
	struct wl_resource *resource = wl_resource_create(client,
		&wp_pointer_warp_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &pointer_warp_impl, pointer_warp, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_pointer_warp_v1 *pointer_warp =
		wl_container_of(listener, pointer_warp, display_destroy);

	wl_signal_emit_mutable(&pointer_warp->events.destroy, NULL);

	assert(wl_list_empty(&pointer_warp->events.destroy.listener_list));
	assert(wl_list_empty(&pointer_warp->events.warp.listener_list));

	wl_list_remove(&pointer_warp->display_destroy.link);
	wl_global_destroy(pointer_warp->global);
	free(pointer_warp);
}

struct wlr_pointer_warp_v1 *wlr_pointer_warp_v1_create(struct wl_display *display,
		uint32_t version) {
	assert(version <= POINTER_WARP_VERSION);

	struct wlr_pointer_warp_v1 *pointer_warp = calloc(1, sizeof(*pointer_warp));
	if (pointer_warp == NULL) {
		return NULL;
	}

	pointer_warp->global = wl_global_create(display, &wp_pointer_warp_v1_interface,
		version, pointer_warp, pointer_warp_bind);
	if (pointer_warp->global == NULL) {
		free(pointer_warp);
		return NULL;
	}

	wl_signal_init(&pointer_warp->events.destroy);
	wl_signal_init(&pointer_warp->events.warp);

	pointer_warp->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &pointer_warp->display_destroy);

	return pointer_warp;
}
