#ifndef _SWAY_SCENE_H
#define _SWAY_SCENE_H

struct sway_workspace;
struct wlr_box;
struct wlr_output;

/**
 * Contains data gathered on the top to bottom scene graph traversal.
 * At each point in the traversal, we know the inherited data for the current
 * node, so we don't need to recurse upward to gather it.
 */
struct sway_scene_node_at_inherited {
	struct wlr_output *output; // may be NULL
	void *workspace; // may be NULL
	const struct wlr_box *output_box; // may be NULL
};

/**
 * When traversing the scene graph top to bottom to search for the node at a
 * certain position on screen, we keep scene data from the nodes above it
 * to be able to discard whole scene graph trees quickly, and avoid having to
 * recurse upward to gather data again.
 */
struct sway_scene_node_at_query {
	// For workspaces overview, if not NULL, consider only the nodes that belong
	// to this workspace or to none
	struct sway_workspace *workspace;
	// output at the query point, or NULL if there is none
	struct wlr_output *wlr_output;

	// Cached data from nodes above, affecting the current one
	struct sway_scene_node_at_inherited inherited;
};

#endif
