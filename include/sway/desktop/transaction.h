#ifndef _SWAY_TRANSACTION_H
#define _SWAY_TRANSACTION_H
#include <stdint.h>
#include <stdbool.h>
#include <wlr/types/wlr_scene.h>
#include "sway/desktop/animation.h"

/**
 * Transactions enable us to perform atomic layout updates.
 *
 * A transaction contains a list of containers and their new state.
 * A state might contain a new size, or new border settings, or new parent/child
 * relationships.
 *
 * Committing a transaction makes sway notify of all the affected clients with
 * their new sizes. We then wait for all the views to respond with their new
 * surface sizes. When all are ready, or when a timeout has passed, we apply the
 * updates all at the same time.
 *
 * When we want to make adjustments to the layout, we change the pending state
 * in containers, mark them as dirty and call transaction_commit_dirty(). This
 * create and commits a transaction from the dirty containers.
 */

struct sway_transaction;
struct sway_transaction_instruction;
struct sway_view;
struct sway_container;
struct sway_output;
struct sway_workspace;

/**
 * Find all dirty containers, create and commit a transaction containing them,
 * and unmark them as dirty.
 */
void transaction_commit_dirty(void);

/*
 * Same as transaction_commit_dirty, but signalling that this is a
 * client-initiated change has already taken effect.
 */
void transaction_commit_dirty_client(void);

/**
 * Set the type of animation for the next transaction. It is
 * the default animation if not set.
 */
void transaction_set_animation(enum sway_animation_type type);

/**
 * The same as transaction_commit_dirty(), but disabling all animations for
 * the transaction
 */
void transaction_commit_dirty_disable_animations(void);

/**
 * Notify the transaction system that a view is ready for the new layout.
 *
 * When all views in the transaction are ready, the layout will be applied.
 *
 * A success boolean is returned denoting that this part of the transaction is
 * ready.
 */
bool transaction_notify_view_ready_by_serial(struct sway_view *view,
		uint32_t serial);

/**
 * Notify the transaction system that a view is ready for the new layout, but
 * identifying the instruction by geometry rather than by serial.
 *
 * This is used by xwayland views, as they don't have serials.
 *
 * A success boolean is returned denoting that this part of the transaction is
 * ready.
 */
bool transaction_notify_view_ready_by_geometry(struct sway_view *view,
		double x, double y, int width, int height);

void arrange_popups(struct wlr_scene_tree *popups);

/**
 * Returns the animation group associated with the transaction that is being
 * applied, or NULL otherwise.
 */
struct sway_animation_group *transaction_get_animation_group(void);

/**
 * For continuous animations, make the animated positions of a floating
 * container, or of the tiling containers of a workspace follow their
 * pending positions instead of animating to them.
 * Call track for every intermediate transition, and untrack before committing
 * the transaction that leaves them in their final positions.
 */
void transaction_track_container(struct sway_container *con);
void transaction_untrack_container(struct sway_container *con);
void transaction_track_workspace(struct sway_workspace *ws);
void transaction_untrack_workspace(struct sway_workspace *ws);

/**
 * Transactions are reference counted, releasing their nodes when losing their
 * last reference.
 */
struct sway_transaction *transaction_ref(struct sway_transaction *transaction);
void transaction_unref(struct sway_transaction *transaction);

/**
 * Render the scebe graph for the current values of the animated variables of
 * the nodes of an output, or for every output.
 */
void transaction_animate_output(struct sway_output *output);
void transaction_animate_outputs(void);

#endif
