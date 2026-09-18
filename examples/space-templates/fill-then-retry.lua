-- Fill-then-retry example for space templates (issue #5).
--
-- The canonical userland pattern: try to apply a saved template; if it
-- fails because some slots have no live window bound yet, launch a
-- default program for each named slot and apply again once it has mapped.
-- This is *plain scripting* against the structured error
-- scroll.space_template_apply() already returns -- there is no
-- compositor-side fallback/retry/staging feature to configure, and this
-- script adds no persistent state of its own beyond one view_map callback
-- at a time (removed as soon as it fires or the template is fully
-- applied).
--
-- Requires a template already saved with e.g. `space_template save dev
-- --with-hints` (or hand-authored JSON) whose slots have meaningful names.
-- See examples/space-templates/templates/dev.json for a matching example.

-- Map each slot name in your template to the command that should fill it
-- when nothing is bound to it yet. Edit this table to match your own
-- templates -- scroll has no idea what "editor" or "terminal" should mean.
local DEFAULT_PROGRAM = {
	editor = "foot -a space-template-editor -e nvim",
	terminal = "foot -a space-template-terminal",
}

local function log(fmt, ...)
	scroll.log(2, string.format(fmt, ...))
end

-- Applies `template_name` with the given slot->con_id/criteria `mappings`.
-- Returns true on success, or false plus the array of slots that still
-- need a window (may be nil if the failure wasn't slot-specific, e.g. an
-- unknown template name).
local function try_apply(template_name, mappings)
	local ok, err, slots = scroll.space_template_apply(template_name, mappings)
	if ok then
		return true, nil
	end
	log("space_template_apply(%s) failed: %s", template_name, tostring(err))
	return false, slots
end

-- Launches `program`, waits for the next view whose app_id matches
-- `app_id_pattern`, then calls `on_view(view)` exactly once and removes
-- its own callback.
--
-- All of a fill_then_retry() call's placeholders are launched up front
-- (see below) and therefore have their view_map callbacks registered
-- *before* any of them can fire -- deliberately, not registered one at a
-- time from inside another callback's handler. scroll's callback dispatch
-- iterates its callback list by re-reading its current length on every
-- step, so a callback registered *while* a view_map event is already
-- being dispatched can end up invoked immediately, in that same dispatch,
-- with that same (wrong) view. Registering everything up front sidesteps
-- that hazard entirely; matching by each placeholder's own app_id tag
-- (set via DEFAULT_PROGRAM below) is what makes it safe for more than one
-- callback to be live at once, since each one simply ignores any view_map
-- event that isn't its own.
local function launch_and_wait(program, app_id_pattern, on_view)
	scroll.exec_process(program)
	local callback_id
	callback_id = scroll.add_callback("view_map", function(view, _data)
		if not view then
			return
		end
		local app_id = scroll.view_get_app_id(view)
		if app_id and app_id:match(app_id_pattern) then
			scroll.remove_callback(callback_id)
			on_view(view)
		end
	end, nil)
end

-- DEFAULT_PROGRAM[slot] is expected to tag its window with the app_id
-- "space-template-<slot>" (see the -a flags above) so this can find the
-- right window without help; keep that convention if you edit
-- DEFAULT_PROGRAM, or change this pattern to match whatever you use.
local function placeholder_app_id_pattern(slot)
	return "^space%-template%-" .. slot .. "$"
end

--- Applies `template_name`. `initial_mappings` (optional) is a list of
--- {slot=..., con_id=...|criteria=...} you already know how to satisfy,
--- e.g. windows that were already open. For every remaining slot named in
--- the first failed attempt, launches DEFAULT_PROGRAM[slot] -- one at a
--- time, waiting for each to map before launching the next -- then retries
--- once all of them have. Gives up (logging and returning false) if a
--- missing slot has no configured default; this script never invents a
--- fallback on its own.
function space_template_fill_then_retry(template_name, initial_mappings)
	local mappings = {}
	for _, m in ipairs(initial_mappings or {}) do
		table.insert(mappings, m)
	end

	local ok, slots = try_apply(template_name, mappings)
	if ok then
		return true
	end
	if not slots or #slots == 0 then
		log("space_template_fill_then_retry(%s): apply failed with no slot list, giving up", template_name)
		return false
	end

	for _, slot in ipairs(slots) do
		if not DEFAULT_PROGRAM[slot] then
			log("space_template_fill_then_retry(%s): no default program configured for slot '%s'",
				template_name, slot)
			return false
		end
	end

	local pending = #slots
	for _, slot in ipairs(slots) do
		launch_and_wait(DEFAULT_PROGRAM[slot], placeholder_app_id_pattern(slot), function(view)
			local container = scroll.view_get_container(view)
			table.insert(mappings, { slot = slot, con_id = scroll.container_get_id(container) })
			pending = pending - 1
			if pending == 0 then
				local ok2, remaining = try_apply(template_name, mappings)
				if not ok2 then
					log("space_template_fill_then_retry(%s): still incomplete after filling: %s",
						template_name, table.concat(remaining or { "?" }, ", "))
				end
			end
		end)
	end

	return false -- completes asynchronously; see the retry inside the callback above
end
