#ifndef _SWAY_VIRTUAL_OUTPUT_H
#define _SWAY_VIRTUAL_OUTPUT_H

#include <stdbool.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/box.h>
#include "list.h"

struct sway_output;
struct sway_workspace;

/*
 * A physical output is divided into one or more virtual outputs. Every virtual
 * output owns a list of workspaces, just like a real output does. Virtual
 * outputs are created by splitting either the whole physical output or another
 * virtual output, so the union of the areas of the visible virtual outputs is
 * always exactly the area of the physical output.
 *
 * Splits form a binary tree. Inner nodes only describe the split (direction,
 * fraction and gap) and have no workspaces; leaves hold the workspaces.
 */
enum sway_virtual_output_split {
	VIRTUAL_OUTPUT_SPLIT_NONE,
	VIRTUAL_OUTPUT_SPLIT_HORIZONTAL,
	VIRTUAL_OUTPUT_SPLIT_VERTICAL,
};

struct sway_virtual_output {
	struct sway_output *output; // always set
	struct sway_virtual_output *parent;
	struct sway_virtual_output *children[2]; // NULL for leaves

	enum sway_virtual_output_split split;
	double fraction;
	int gap;

	char *name;
	list_t *workspaces; // struct sway_workspace (leaves only)
	struct sway_workspace *active_workspace; // fallback when never focused

	// Effective geometry (layout coordinates / output relative coordinates).
	struct wlr_box output_area;
	struct wlr_box usable_area;
	// Geometry without the maximize_virtual_output override.
	struct wlr_box split_area;
};

/* Called from output_create/output_destroy. */
void output_virtual_outputs_create(struct sway_output *output);
void output_virtual_outputs_destroy(struct sway_output *output);

/* Recompute every virtual output's geometry for output. */
void output_virtual_outputs_update_geometry(struct sway_output *output);

/* Recompute geometry, then re-arrange every workspace of output. */
void output_virtual_outputs_arrange(struct sway_output *output);

struct sway_virtual_output *output_virtual_outputs_get_default(struct sway_output *output);
struct sway_virtual_output *output_virtual_outputs_get_active(struct sway_output *output);
struct sway_virtual_output *virtual_output_by_name(const char *name);

bool virtual_output_is_visible(struct sway_virtual_output *vo);
struct sway_workspace *virtual_output_get_active_workspace(struct sway_virtual_output *vo);

/* Move an existing workspace into a virtual output. */
void virtual_output_set_workspace(struct sway_workspace *ws, struct sway_virtual_output *vo);

/* Create a new workspace in a virtual output. */
struct sway_workspace *virtual_output_create_workspace(struct sway_virtual_output *vo);

/* Used by output.c/workspace.c */
void virtual_output_attach_workspace(struct sway_virtual_output *vo, struct sway_workspace *ws);
void virtual_output_detach_workspace(struct sway_workspace *ws);
void virtual_output_sort_workspaces(struct sway_virtual_output *vo);

/* Commands */
void virtual_output_split(struct sway_virtual_output *vo,
		enum sway_virtual_output_split split, double fraction, int gap);
void virtual_output_split_reset(struct sway_virtual_output *vo);
void virtual_output_toggle_maximize(struct sway_output *output);

/* Helpers */
bool output_has_multiple_visible_virtual_outputs(struct sway_output *output);
bool output_fullscreen_needs_background(struct sway_output *output);

struct sway_virtual_output *virtual_output_get_in_direction(struct sway_virtual_output *from,
		enum wlr_direction direction);
struct sway_virtual_output *output_virtual_output_at_coords(struct sway_output *output,
		double lx, double ly);

#endif
