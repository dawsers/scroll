#include <wlr/types/wlr_cursor.h>
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/tree/layout.h"

struct seatop_scroll_tiling_event {
	struct sway_container *con;
	double x, y; // last cursor position
};

static void finalize_move(struct sway_seat *seat) {
	struct seatop_scroll_tiling_event *e = seat->seatop_data;

	struct wlr_cursor *cursor = seat->cursor->cursor;
	layout_scroll_update(seat, cursor->x - e->x, cursor->y - e->y, config->gesture_scroll_sentitivity_mouse);

	layout_scroll_end(seat);

	seatop_begin_default(seat);
}

static void handle_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wl_pointer_button_state state) {
	if (seat->cursor->pressed_button_count == 0) {
		finalize_move(seat);
	}
}

static void handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
	struct seatop_scroll_tiling_event *e = seat->seatop_data;
	struct wlr_cursor *cursor = seat->cursor->cursor;

	layout_scroll_update(seat, cursor->x - e->x, cursor->y - e->y, config->gesture_scroll_sentitivity_mouse);
	e->x = cursor->x;
	e->y = cursor->y;
}

static void handle_unref(struct sway_seat *seat, struct sway_container *con) {
	struct seatop_scroll_tiling_event *e = seat->seatop_data;
	if (e->con == con) {
		seatop_begin_default(seat);
	}
}

static const struct sway_seatop_impl seatop_impl = {
	.button = handle_button,
	.pointer_motion = handle_pointer_motion,
	.unref = handle_unref,
};

void seatop_begin_scroll_tiling(struct sway_seat *seat,
		struct sway_container *con) {
	seatop_end(seat);

	struct sway_cursor *cursor = seat->cursor;
	struct seatop_scroll_tiling_event *e =
		calloc(1, sizeof(struct seatop_scroll_tiling_event));
	if (!e) {
		return;
	}
	e->con = con;
	e->x = cursor->cursor->x;
	e->y = cursor->cursor->y;

	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	if (layout_scroll_begin(seat) == false) {
		seatop_begin_default(seat);
		return;
	}

	cursor_set_image(cursor, "grab", NULL);
	wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
