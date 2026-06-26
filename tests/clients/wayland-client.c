#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-activation-v1-client-protocol.h"

struct client_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *xdg_wm_base;
	struct xdg_activation_v1 *xdg_activation;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct wl_buffer *buffer;
	int width, height;
	void *shm_data;
};

static void noop() {}

static void shm_format(void *data, struct wl_shm *wl_shm, uint32_t format) {}
static const struct wl_shm_listener shm_listener = { shm_format };

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
	xdg_wm_base_pong(xdg_wm_base, serial);
}
static const struct xdg_wm_base_listener xdg_wm_base_listener = { xdg_wm_base_ping };

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	struct client_state *state = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
		wl_shm_add_listener(state->shm, &shm_listener, state);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		state->xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
	} else if (strcmp(interface, xdg_activation_v1_interface.name) == 0) {
		state->xdg_activation = wl_registry_bind(registry, name, &xdg_activation_v1_interface, 1);
	}
}
static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = { registry_global, registry_global_remove };

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
	struct client_state *state = data;
	xdg_surface_ack_configure(xdg_surface, serial);

	if (!state->buffer) {
		int stride = state->width * 4;
		int size = stride * state->height;

		int fd = memfd_create("scroll-test-shm", MFD_CLOEXEC);
		if (fd < 0) {
			perror("memfd_create");
			exit(1);
		}
		if (ftruncate(fd, size) < 0) {
			perror("ftruncate");
			exit(1);
		}
		state->shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (state->shm_data == MAP_FAILED) {
			perror("mmap");
			exit(1);
		}
		struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
		state->buffer = wl_shm_pool_create_buffer(pool, 0, state->width, state->height, stride, WL_SHM_FORMAT_XRGB8888);
		wl_shm_pool_destroy(pool);
		close(fd);

		uint32_t *pixels = state->shm_data;
		for (int i = 0; i < state->width * state->height; ++i) {
			pixels[i] = 0xFF0000FF; // Blue
		}
	}

	wl_surface_attach(state->surface, state->buffer, 0, 0);
	wl_surface_damage_buffer(state->surface, 0, 0, state->width, state->height);
	wl_surface_commit(state->surface);
}
static const struct xdg_surface_listener xdg_surface_listener = { xdg_surface_configure };

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {}
static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
	exit(0);
}
static const struct xdg_toplevel_listener xdg_toplevel_listener = { xdg_toplevel_configure, xdg_toplevel_close, noop, noop };

int main(int argc, char **argv) {
	struct client_state state = {0};
	state.width = 100;
	state.height = 100;

	const char *title = "Test Wayland Window";
	const char *app_id = "test_app_id";
	if (argc > 1) title = argv[1];
	if (argc > 2) app_id = argv[2];

	state.display = wl_display_connect(NULL);
	if (!state.display) {
		fprintf(stderr, "Failed to connect to Wayland display\n");
		return 1;
	}

	state.registry = wl_display_get_registry(state.display);
	wl_registry_add_listener(state.registry, &registry_listener, &state);
	wl_display_roundtrip(state.display);

	if (!state.compositor || !state.shm || !state.xdg_wm_base) {
		fprintf(stderr, "Missing globals\n");
		return 1;
	}

	state.surface = wl_compositor_create_surface(state.compositor);
	state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.surface);
	xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);

	state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
	xdg_toplevel_add_listener(state.xdg_toplevel, &xdg_toplevel_listener, &state);

	xdg_toplevel_set_title(state.xdg_toplevel, title);
	xdg_toplevel_set_app_id(state.xdg_toplevel, app_id);

	wl_surface_commit(state.surface);

	char *token = getenv("XDG_ACTIVATION_TOKEN");
	if (token && state.xdg_activation) {
		xdg_activation_v1_activate(state.xdg_activation, token, state.surface);
	}

	while (wl_display_dispatch(state.display) != -1) {
		// Loop
	}

	if (state.buffer) wl_buffer_destroy(state.buffer);
	if (state.shm_data) munmap(state.shm_data, state.width * 4 * state.height);
	if (state.xdg_toplevel) xdg_toplevel_destroy(state.xdg_toplevel);
	if (state.xdg_surface) xdg_surface_destroy(state.xdg_surface);
	if (state.surface) wl_surface_destroy(state.surface);
	if (state.compositor) wl_compositor_destroy(state.compositor);
	if (state.shm) wl_shm_destroy(state.shm);
	if (state.xdg_wm_base) xdg_wm_base_destroy(state.xdg_wm_base);
	if (state.xdg_activation)
		xdg_activation_v1_destroy(state.xdg_activation);
	if (state.registry) wl_registry_destroy(state.registry);
	if (state.display) wl_display_disconnect(state.display);

	return 0;
}
