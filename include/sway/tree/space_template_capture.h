#ifndef _SWAY_SPACE_TEMPLATE_CAPTURE_H
#define _SWAY_SPACE_TEMPLATE_CAPTURE_H

#include <stdbool.h>
#include "sway/tree/space_template.h"
#include "sway/tree/workspace.h"

// Captures the live tiling/floating tree of `workspace` into a new,
// structurally valid sway_space_template named `name`. This is the mirror
// image of space_template_apply(): read-only, never mutates the live tree.
//
// Every leaf gets a freshly generated, template-unique slot name
// ("slot-1", "slot-2", ... in tree-traversal order): v1 has no semantic
// slot-naming inference (see docs/research/space-templates-final/plan.md,
// "generated slot-N names are structural only"). A human or script renames
// slots by hand-editing the saved JSON afterward.
//
// When `with_hints` is true, each leaf's view_hint is populated from the
// live view's current app_id/class/title; when false (the default for a
// plain `save`), no hints are captured -- matching the documented meaning
// of the `--with-hints` command flag: it controls hint capture, not slot
// existence.
//
// Scroller modifiers are captured from the workspace's current layout
// settings (layout_modifiers_get_*()). `focused_slot` is set to whichever
// slot's view is the workspace's currently focused container, if any, else
// left unset.
//
// The result always passes space_template_validate() (an empty workspace
// captures to a template with empty tiling/floating lists, which is
// valid). The caller owns it and must space_template_destroy() it.
struct sway_space_template *space_template_capture(
		struct sway_workspace *workspace, const char *name, bool with_hints);

#endif // _SWAY_SPACE_TEMPLATE_CAPTURE_H
