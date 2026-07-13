#include "sway/tree/focus_ring.h"
#include "sway/input/seat.h"
#include "log.h"

struct sway_focus_ring *focus_ring_create() {
	struct sway_focus_ring *focus_ring = calloc(1, sizeof(struct sway_focus_ring));
	if (focus_ring) {
		focus_ring->ring = create_list();
		focus_ring->index = -1;
	} else {
		sway_log(SWAY_ERROR, "Couldn't allocate memory for focus ring");
	}
	return focus_ring;
}

void focus_ring_destroy(struct sway_focus_ring *focus_ring) {
	list_free(focus_ring->ring);
	free(focus_ring);
}

static void focus_container(struct sway_seat *seat,
		struct sway_container *container) {
	if (container_is_scratchpad_hidden(container)) {
		root_scratchpad_show(container);
	}
	seat_set_focus_container(seat, container);
}

void focus_ring_next(struct sway_focus_ring *focus_ring,
		struct sway_seat *seat) {
	int index = focus_ring->index + 1;
	if (index < focus_ring->ring->length) {
		struct sway_view *view = focus_ring->ring->items[index];
		focus_container(seat, view->container);
		++focus_ring->index;
	}
}

void focus_ring_prev(struct sway_focus_ring *focus_ring,
		struct sway_seat *seat) {
	int index = focus_ring->index - 1;
	int len = config->focus_ring_length > 0 ? config->focus_ring_length :
		focus_ring->ring->length;
	if (index >= focus_ring->ring->length - len) {
		struct sway_view *view = focus_ring->ring->items[index];
		focus_container(seat, view->container);
		focus_ring->index--;
	}
}

void focus_ring_first(struct sway_focus_ring *focus_ring,
		struct sway_seat *seat) {
	int len = config->focus_ring_length > 0 ? config->focus_ring_length :
		focus_ring->ring->length;
	focus_ring->index = focus_ring->ring->length - len;
	struct sway_view *view = focus_ring->ring->items[focus_ring->index];
	focus_container(seat, view->container);
}

void focus_ring_last(struct sway_focus_ring *focus_ring,
		struct sway_seat *seat) {
	focus_ring->index = focus_ring->ring->length - 1;
	struct sway_view *view = focus_ring->ring->items[focus_ring->index];
	focus_container(seat, view->container);
}

void focus_ring_set(struct sway_focus_ring *focus_ring, struct sway_seat *seat,
		struct sway_view *view) {
	int idx = list_find(focus_ring->ring, view);
	sway_assert(idx >= 0, "Error in focus_ring, focused view is not included");
	list_del(focus_ring->ring, idx);
	list_add(focus_ring->ring, view);
	focus_ring->index = focus_ring->ring->length - 1;
}

void focus_ring_add_view(struct sway_focus_ring *focus_ring,
		struct sway_view *view) {
	int idx = list_find(focus_ring->ring, view);
	if (idx < 0) {
		++focus_ring->index;
		list_insert(focus_ring->ring, focus_ring->index, view);
	}
}

void focus_ring_remove_view(struct sway_focus_ring *focus_ring,
		struct sway_view *view) {
	int idx = list_find(focus_ring->ring, view);
	if (idx >= 0) {
		list_del(focus_ring->ring, idx);
		if (idx <= focus_ring->index) {
			focus_ring->index--;
		}
	}
}
