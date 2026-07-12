#ifndef _SWAY_TREE_FOCUS_RING_H
#define _SWAY_TREE_FOCUS_RING_H

#include "sway/tree/view.h"

struct sway_focus_ring {
	list_t *ring; // struct sway_view
	int index;
};

// Create a focus ring
struct sway_focus_ring *focus_ring_create();

// Destroy focus ring
void focus_ring_destroy(struct sway_focus_ring *focus_ring);

// Set focus on the next view of the focus ring
void focus_ring_next(struct sway_focus_ring *focus_ring,
		struct sway_seat *seat);

// Set focus on the previous view of the focus ring
void focus_ring_prev(struct sway_focus_ring *focus_ring,
		struct sway_seat *seat);

// Set focus on the first view of the ring
void focus_ring_first(struct sway_focus_ring *focus_ring,
		struct sway_seat *seat);
//
// Set focus on the last view of the ring
void focus_ring_last(struct sway_focus_ring *focus_ring,
		struct sway_seat *seat);

// Set the current view as the last of the focus ring
void focus_ring_set(struct sway_focus_ring *focus_ring, struct sway_seat *seat,
		struct sway_view *view);

// Add a view to the focus ring if not present
void focus_ring_add_view(struct sway_focus_ring *focus_ring,
		struct sway_view *view);

// Remove a view from the focus ring (view unmap event)
void focus_ring_remove_view(struct sway_focus_ring *focus_ring,
		struct sway_view *view);

#endif // _SWAY_TREE_FOCUS_RING_H
