#ifndef _SWAY_TRANSACTION_H
#define _SWAY_TRANSACTION_H
#include <stdint.h>
#include <stdbool.h>
#include <wlr/types/wlr_scene.h>
#include "sway/desktop/animate.h"

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
 * Set the type of animation for the next transaction commit. It is the default
 * animation if not set.
 *
 * A commit may be added to a transaction that is waiting to be committed.
 * That transaction then takes the type, unless it is the default one: the
 * last commit that asks for a type decides.
 *
 * ANIMATION_DISABLED stops the animation of the geometry, and leaves the
 * fades running. transaction_disable_animations() stops both.
 */
void transaction_set_animation(enum sway_animation_type type);

/**
 * If `disable` is true, no commit is animated until called again with
 * `disable` false
 */
void transaction_disable_animations(bool disable);

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
 * For continuous animations, make the animated positions of a floating
 * container follow its pending position instead of animating to it.
 * Call track for every intermediate transition, and untrack before committing
 * the transaction that leaves them in their final positions.
 * Untrack needs to be called for those positions to be released for animation
 * again.
 */
void transaction_track_container(struct sway_container *con);
void transaction_untrack_container(struct sway_container *con);

/**
 * Update the scene graph with the current values of the animated variables of
 * the nodes of an output, or for every output.
 */
void transaction_animate_output(struct sway_output *output);
void transaction_animate_outputs(void);

#endif
