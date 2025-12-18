#ifndef _SWAY_WORKSPACE_H
#define _SWAY_WORKSPACE_H

#include <stdbool.h>
#include "sway/config.h"
#include "sway/tree/layout.h"
#include "sway/tree/container.h"
#include "sway/tree/scene.h"
#include "sway/tree/node.h"

enum sway_workspace_split {
	WORKSPACE_SPLIT_NONE,
	WORKSPACE_SPLIT_HORIZONTAL,
	WORKSPACE_SPLIT_VERTICAL,

	WORKSPACE_SPLIT_LEFT,
	WORKSPACE_SPLIT_RIGHT,
	WORKSPACE_SPLIT_TOP,
	WORKSPACE_SPLIT_BOTTOM,
};

struct sway_view;

struct sway_workspace_state {
	struct sway_container *fullscreen;
	double x, y;
	int width, height;
	list_t *floating;
	list_t *tiling;

	struct sway_container *focused_inactive_child;
	bool focused;
};

struct sway_workspace {
	struct sway_node node;

	struct {
		struct sway_scene_tree *tiling;
		struct sway_scene_tree *fullscreen;
	} layers;

	struct sway_container *fullscreen;

	char *name;
	char *representation;

	double x, y;
	int width, height;
	struct sway_scroller layout;

	struct side_gaps current_gaps;
	int gaps_inner;
	struct side_gaps gaps_outer;

	struct sway_output *output; // NULL if no outputs are connected
	list_t *floating;           // struct sway_container
	list_t *tiling;             // struct sway_container
	list_t *output_priority;
	bool urgent;

	struct {
		double x, y;
		double width, height;
		double scale;
		struct sway_scene_tree *tree;
		struct sway_text_node *text;
	} jump;

	struct {
		bool scrolling;
		double dx, dy;
		struct sway_container *pin;
		enum sway_layout_pin pin_position;
	} gesture;

	struct sway_workspace_split_data {
		enum sway_workspace_split split;
		double fraction;
		int gap;
		struct sway_workspace *sibling;
		struct wlr_box output_area;
		struct wlr_box usable_area;
	} split;

	struct sway_workspace_state current;
};

struct workspace_config *workspace_find_config(const char *ws_name);

struct sway_output *workspace_get_initial_output(const char *name);

struct sway_workspace *workspace_create(struct sway_output *output,
		const char *name);

void workspace_destroy(struct sway_workspace *workspace);

void workspace_begin_destroy(struct sway_workspace *workspace);

void workspace_consider_destroy(struct sway_workspace *ws);

char *workspace_next_name(const char *output_name);

struct sway_workspace *workspace_auto_back_and_forth(
		struct sway_workspace *workspace);

bool workspace_switch(struct sway_workspace *workspace);

struct sway_workspace *workspace_by_number(const char* name);

struct sway_workspace *workspace_by_name(const char*);

struct sway_workspace *workspace_output_next(struct sway_workspace *current);

struct sway_workspace *workspace_next(struct sway_workspace *current);

struct sway_workspace *workspace_output_prev(struct sway_workspace *current);

struct sway_workspace *workspace_prev(struct sway_workspace *current);

bool workspace_is_visible(struct sway_workspace *ws);

bool workspace_is_empty(struct sway_workspace *ws);

void workspace_output_raise_priority(struct sway_workspace *workspace,
		struct sway_output *old_output, struct sway_output *new_output);

void workspace_output_add_priority(struct sway_workspace *workspace,
		struct sway_output *output);

struct sway_output *workspace_output_get_highest_available(
		struct sway_workspace *ws);

void workspace_detect_urgent(struct sway_workspace *workspace);

void workspace_for_each_container(struct sway_workspace *ws,
		void (*f)(struct sway_container *con, void *data), void *data);

struct sway_container *workspace_find_container(struct sway_workspace *ws,
		bool (*test)(struct sway_container *con, void *data), void *data);

void workspace_detach(struct sway_workspace *workspace);

struct sway_container *workspace_add_tiling(struct sway_workspace *workspace,
		struct sway_container *con);

void workspace_add_floating(struct sway_workspace *workspace,
		struct sway_container *con);

/**
 * Adds a tiling container to the workspace without considering
 * the workspace_layout, so the con will not be split.
 */
void workspace_insert_tiling_direct(struct sway_workspace *workspace,
		struct sway_container *con, int index);

struct sway_container *workspace_insert_tiling(struct sway_workspace *workspace,
		struct sway_container *con, int index);

void workspace_remove_gaps(struct sway_workspace *ws);

void workspace_add_gaps(struct sway_workspace *ws);

void workspace_update_representation(struct sway_workspace *ws);

void workspace_get_box(struct sway_workspace *workspace, struct wlr_box *box);

size_t workspace_num_tiling_views(struct sway_workspace *ws);

size_t workspace_num_sticky_containers(struct sway_workspace *ws);

/**
 * Splits a workspace into two, creating a new one
 */
void workspace_split(struct sway_workspace *workspace, enum sway_workspace_split split,
	double fraction, int gap);

void workspace_split_reset(struct sway_workspace *workspace);

struct wlr_box *workspace_get_output_usable_area(struct sway_workspace *workspace);

/**
 * Swaps two workspaces. If name_only is true, only their names are swapped.
 */
void workspace_swap(struct sway_workspace *first, struct sway_workspace *second,
		bool name_only);

#endif
