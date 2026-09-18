#ifndef _SWAY_SPACE_TEMPLATE_IPC_H
#define _SWAY_SPACE_TEMPLATE_IPC_H

#include <json.h>
#include "sway/tree/workspace.h"

// Builds the canonical `get_space_template` reply for `name`: the
// template's canonical JSON (space_template_to_json()) on success, or
// {"success":false,"error":"...","path":"...","detail":"..."} (path/detail
// omitted when not applicable) on failure -- unsafe name, not found,
// malformed file, etc. Caller owns the result and must json_object_put()
// it. Never mutates anything; legacy `get_spaces` is untouched.
struct json_object *space_template_ipc_get(const char *name);

// Builds the canonical `apply_space_template` reply for a request object
// shaped like:
//   {"name": "...", "workspace": "current",
//    "mappings": [{"slot": "...", "con_id": N} | {"slot": "...", "criteria": "..."}]}
//
// "workspace" must be omitted or exactly "current" -- v1 operates on the
// current workspace only (D-05); any other value is rejected. Each mapping
// resolves to a live view: "con_id" is looked up via node_by_id() and must
// name a mapped container's view; "criteria" is parsed with
// criteria_parse() and must match exactly one live-viewed container
// (criteria_get_containers()) -- zero or multiple matches is an error
// naming that slot, resolved *before* space_template_apply() ever runs.
// The named template is then loaded (space_template_load()) and applied to
// `workspace` via space_template_apply().
//
// On success returns {"success":true}. On any failure -- malformed
// request, unknown/duplicate slot, ambiguous or no-match criteria, a
// missing/invalid template, or an apply-time error (missing/duplicate/
// stale/ambiguous binding) -- returns
// {"success":false,"error":"...","slots":["..."]} (slots omitted when not
// applicable) and performs no mutation.
//
// The request has no fallback/retry/staging/pending/timeout/cancel/commit
// field, and this function implements no such behavior: incompleteness is
// reported once, via the structured error, exactly per D-02/D-06. Matching
// itself (which candidate wins, launching, filling, retrying) is entirely
// the caller's job; this function only resolves the *mapping the caller
// already decided on* to a live view.
//
// `workspace` is the resolved "current workspace" for this caller's
// context (config->handler_context.workspace for the `space_template`
// command; the focused seat's workspace for a bare IPC connection) --
// resolving it is the caller's responsibility so this function stays
// context-agnostic. Caller owns the result and must json_object_put() it.
struct json_object *space_template_ipc_apply(struct json_object *request,
		struct sway_workspace *workspace);

#endif // _SWAY_SPACE_TEMPLATE_IPC_H
