#ifndef _SWAY_FILTER_H
#define _SWAY_FILTER_H
#include <stdbool.h>

struct sway_workspace;

/**
 * Filters show or hide parts of the tree. The active filter, stored in
 * root->filters, is a description of what should be shown (see struct
 * sway_root_filters).
 *
 * Containers: filter_apply() evaluates the active filter for every
 * container and stores the result in container->pending.filtered.
 * A container that changes that flag fades in or out.
 *
 * Workspaces: filters are evaluated on demand
 */

/**
 * Apply the active filter to the pending tree, updating
 * container->pending.filtered and marking containers that change dirty.
 *
 * Needs to be called when the active filter changes and before committing a
 * transaction.
 */
void filter_apply(void);

/**
 * Whether the workspace is part of the scene graph.
 */
bool filter_workspace_enabled(struct sway_workspace *workspace);

/**
 * Whether the tiling/floating containers of the workspace are part of the
 * scene graph
 */
bool filter_workspace_tiling_enabled(struct sway_workspace *workspace);
bool filter_workspace_floating_enabled(struct sway_workspace *workspace);

#endif
