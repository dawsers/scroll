/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_BACKEND_LIBINPUT_H
#define WLR_BACKEND_LIBINPUT_H

#include <libinput.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>

struct wlr_input_device;
struct wlr_tablet_tool;

struct wlr_backend *wlr_libinput_backend_create(struct wlr_session *session);
/**
 * Gets the underlying struct libinput_device handle for the given input device.
 */
struct libinput_device *wlr_libinput_get_device_handle(
		struct wlr_input_device *dev);
/**
 * Gets the underlying struct libinput_tablet_tool handle for the given tablet tool.
 */
struct libinput_tablet_tool *wlr_libinput_get_tablet_tool_handle(
		struct wlr_tablet_tool *wlr_tablet_tool);

bool wlr_backend_is_libinput(struct wlr_backend *backend);
bool wlr_input_device_is_libinput(struct wlr_input_device *device);

#endif
