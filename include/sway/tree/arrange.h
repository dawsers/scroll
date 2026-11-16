#ifndef _SWAY_ARRANGE_H
#define _SWAY_ARRANGE_H

struct sway_output;
struct sway_workspace;
struct sway_container;
struct sway_node;

/**
 * The arrange functions set the sizes and the positions of the pending state
 * so the nodes are ready to be committed in a transaction.
 */
void arrange_container(struct sway_container *container);

void arrange_workspace(struct sway_workspace *workspace);

void arrange_output(struct sway_output *output);

void arrange_root(void);

void arrange_node(struct sway_node *node);

#endif
