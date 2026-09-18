#ifndef _SWAY_SPACE_TEMPLATE_APPLY_H
#define _SWAY_SPACE_TEMPLATE_APPLY_H

#include "list.h"
#include "sway/tree/space_template.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"

// One caller-resolved slot -> live view binding. The apply engine never
// performs matching/launching/filling/retrying itself (that is userland's
// job, per #384 and D-02/D-06): callers (commands/IPC in G4, Lua in G5)
// resolve `slot` names to already-mapped `view`s -- via an explicit con_id
// or via criteria_matches_view() -- before calling space_template_apply().
//
// Both `slot` and `view` are borrowed: the apply engine neither copies nor
// frees `slot`, and never takes ownership of `view` (a live compositor
// object it does not manage the lifetime of).
struct sway_space_template_binding {
	const char *slot;
	struct sway_view *view;
};

enum sway_space_template_apply_error {
	SPACE_TEMPLATE_APPLY_OK = 0,
	SPACE_TEMPLATE_APPLY_ERROR_NO_WORKSPACE,
	SPACE_TEMPLATE_APPLY_ERROR_INVALID_TEMPLATE, // fails space_template_validate()
	SPACE_TEMPLATE_APPLY_ERROR_UNSUPPORTED_TEMPLATE, // e.g. a non-leaf floating root (v1 limitation)
	SPACE_TEMPLATE_APPLY_ERROR_UNKNOWN_SLOT, // a binding names a slot the template does not have
	SPACE_TEMPLATE_APPLY_ERROR_MISSING_SLOT, // a template slot has no binding
	SPACE_TEMPLATE_APPLY_ERROR_DUPLICATE_SLOT, // the same slot is bound more than once
	SPACE_TEMPLATE_APPLY_ERROR_DUPLICATE_VIEW, // the same view is bound to more than one slot (ambiguous)
	SPACE_TEMPLATE_APPLY_ERROR_STALE_VIEW, // a bound view is NULL, unmapped, or destroying
	SPACE_TEMPLATE_APPLY_ERROR_ARRANGE_FAILED, // defensive: see space_template_apply()'s doc comment
};

// Result of space_template_apply(). `slots` names the affected template
// slot(s) for slot-specific errors (UNKNOWN_SLOT, MISSING_SLOT -- possibly
// several, DUPLICATE_SLOT, DUPLICATE_VIEW -- exactly two, STALE_VIEW); NULL
// for template/workspace-level errors and on success. Entries are
// heap-allocated copies; the caller owns `slots` and must free it (and its
// entries) via space_template_apply_result_destroy().
struct sway_space_template_apply_result {
	enum sway_space_template_apply_error error;
	list_t *slots; // char *
};

const char *space_template_apply_error_str(enum sway_space_template_apply_error error);
void space_template_apply_result_destroy(struct sway_space_template_apply_result *result);

// Applies `template` to `workspace` using exactly the bindings supplied,
// in one atomic compositor operation: validate -> sweep -> arrange ->
// dissolve.
//
//  1. Validate (zero mutation): `template` must itself already pass
//     space_template_validate(); every template slot must have exactly one
//     binding, every binding must name a slot the template actually has,
//     no two bindings may share a slot or a view, and every bound view
//     must currently be live (non-NULL, mapped: view->container != NULL,
//     not destroying). Any failure returns immediately, naming the
//     offending slot(s), having touched nothing.
//  2. Sweep (H-01/restore_hide parity): every view currently on
//     `workspace` that is *not* one of the bound views is moved to the
//     scratchpad (root_scratchpad_add_container()/root_scratchpad_hide()
//     as appropriate) -- never closed.
//  3. Arrange: the bound views are detached from wherever they currently
//     live and rebuilt into `workspace`'s tiling/floating trees exactly as
//     `template` describes (layout, split fractions, floating geometry),
//     the workspace's scroller modifiers are set from `template->scroller`
//     via the existing layout_modifiers_set_*() API, and focus is set to
//     the view bound to `template->focused_slot`, if any.
//  4. Dissolve: `template` and `bindings` are read-only inputs. Nothing
//     from either is retained by the live tree; the workspace that results
//     is an ordinary workspace like any other.
//
// v1 only supports leaf nodes at the top level of `template->floating`
// (matching the live tree's own floating model, which has no nested
// floating groups); a non-leaf floating root fails validation with
// SPACE_TEMPLATE_APPLY_ERROR_UNSUPPORTED_TEMPLATE before any mutation.
//
// SPACE_TEMPLATE_APPLY_ERROR_ARRANGE_FAILED is a defensive-only outcome:
// with all bindings pre-validated as live and the compositor single
// threaded, step 3 cannot fail through this codebase's normal control
// flow, but each step re-checks its view before touching it and treats an
// unexpected failure as this error rather than crashing or leaving a
// partial tree. On this path the sweep from step 2 is undone via
// root_scratchpad_show(), returning every swept container to the current
// workspace -- the plan's documented "single-command rollback window",
// not a full transaction log: a container that was tiled before the sweep
// may come back floating (an inherent limitation of the scratchpad
// show/hide primitives this reuses, matching legacy sway_space's own
// restore_hide, which never attempts to undo at all).
struct sway_space_template_apply_result space_template_apply(
		const struct sway_space_template *template,
		struct sway_workspace *workspace,
		list_t *bindings);

#endif // _SWAY_SPACE_TEMPLATE_APPLY_H
