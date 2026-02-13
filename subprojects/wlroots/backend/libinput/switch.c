#include <assert.h>
#include <libinput.h>
#include <wlr/interfaces/wlr_switch.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"
#include "config.h"

const struct wlr_switch_impl libinput_switch_impl = {
	.name = "libinput-switch",
};

void init_device_switch(struct wlr_libinput_input_device *dev) {
	const char *name = get_libinput_device_name(dev->handle);
	struct wlr_switch *wlr_switch = &dev->switch_device;
	wlr_switch_init(wlr_switch, &libinput_switch_impl, name);
}

struct wlr_libinput_input_device *device_from_switch(
		struct wlr_switch *wlr_switch) {
	assert(wlr_switch->impl == &libinput_switch_impl);

	struct wlr_libinput_input_device *dev =
		wl_container_of(wlr_switch, dev, switch_device);
	return dev;
}

static bool switch_type_from_libinput(enum libinput_switch type, enum wlr_switch_type *out) {
	switch (type) {
	case LIBINPUT_SWITCH_LID:
		*out = WLR_SWITCH_TYPE_LID;
		return true;
	case LIBINPUT_SWITCH_TABLET_MODE:
		*out = WLR_SWITCH_TYPE_TABLET_MODE;
		return true;
#if HAVE_LIBINPUT_SWITCH_KEYPAD_SLIDE
	case LIBINPUT_SWITCH_KEYPAD_SLIDE:
		*out = WLR_SWITCH_TYPE_KEYPAD_SLIDE;
		return true;
#endif
	}
	return false;
}

static bool switch_state_from_libinput(enum libinput_switch_state state, enum wlr_switch_state *out) {
	switch (state) {
	case LIBINPUT_SWITCH_STATE_OFF:
		*out = WLR_SWITCH_STATE_OFF;
		return true;
	case LIBINPUT_SWITCH_STATE_ON:
		*out = WLR_SWITCH_STATE_ON;
		return true;
	}
	return false;
}

void handle_switch_toggle(struct libinput_event *event,
		struct wlr_switch *wlr_switch) {
	struct libinput_event_switch *sevent =
		libinput_event_get_switch_event(event);
	struct wlr_switch_toggle_event wlr_event = {
		.time_msec = usec_to_msec(libinput_event_switch_get_time_usec(sevent)),
	};
	if (!switch_type_from_libinput(libinput_event_switch_get_switch(sevent), &wlr_event.switch_type)) {
		wlr_log(WLR_DEBUG, "Unhandled libinput switch type");
		return;
	}
	if (!switch_state_from_libinput(libinput_event_switch_get_switch_state(sevent), &wlr_event.switch_state)) {
		wlr_log(WLR_DEBUG, "Unhandled libinput switch state");
		return;
	}
	wl_signal_emit_mutable(&wlr_switch->events.toggle, &wlr_event);
}
