-- Launcher-placeholder example for space templates (issue #5).
--
-- Stages every slot of a saved template with a real terminal window
-- running a user-supplied launcher script, applies the template so every
-- placeholder is visible immediately, then swaps each placeholder out for
-- the real application the user picks -- using only existing view/move/
-- close primitives (add_callback, exec_process, view_get_app_id,
-- container move via re-apply). Scroll never creates a compositor
-- placeholder of its own: every container in the tree always holds a real
-- window, before, during, and after this script runs. This is the H-02
-- pattern end to end.
--
-- The "swap" itself needs no special move command: space_template_apply()
-- always sweeps whatever isn't in the mapping to the scratchpad and
-- rebuilds the tree fresh, so replacing a placeholder's con_id with the
-- real app's con_id in the mapping and re-applying *is* the swap. The
-- vacated placeholder ends up in the scratchpad (never closed by scroll,
-- per H-01) -- this script closes it explicitly once it's done its job.
--
-- The launcher script that runs inside each placeholder terminal (letting
-- the user pick an app, e.g. via dmenu/rofi/fzf) is not part of this
-- example; it is ordinary userland shell scripting. This file only owns
-- the scroll-side staging/apply/swap orchestration. Your launcher script
-- is expected to exec/replace itself with (or otherwise hand off to) the
-- chosen real application once the user picks one.

-- Edit to match your own launcher and template. %s is replaced with the
-- slot name, so your launcher script can tell which slot it's staging for
-- (e.g. to preselect a relevant app list).
local LAUNCHER_COMMAND = "foot -a space-template-placeholder-%s -- your-launcher-script.sh %s"

local PLACEHOLDER_PREFIX = "space-template-placeholder-"

local function is_placeholder_app_id(app_id)
	return app_id ~= nil and app_id:sub(1, #PLACEHOLDER_PREFIX) == PLACEHOLDER_PREFIX
end

-- Default predicate for "the user picked a real app": anything that isn't
-- one of our own placeholder terminals. Lua patterns have no negation, so
-- this is a plain predicate function, not a pattern string -- pass your
-- own `real_app_predicate(app_id) -> boolean` to
-- space_template_launcher_placeholders() if you need something more
-- specific (e.g. only accept a known set of app_ids).
local function default_real_app_predicate(app_id)
	return app_id ~= nil and not is_placeholder_app_id(app_id)
end

local slot_con_id = {}      -- slot name -> current con_id bound to it
local slot_placeholder_con_id = {} -- slot name -> the placeholder's own con_id, while staged

local function log(fmt, ...)
	scroll.log(2, string.format(fmt, ...))
end

local function current_mappings()
	local mappings = {}
	for slot, con_id in pairs(slot_con_id) do
		table.insert(mappings, { slot = slot, con_id = con_id })
	end
	return mappings
end

local function reapply(template_name)
	local ok, err, slots = scroll.space_template_apply(template_name, current_mappings())
	if not ok then
		log("space_template_apply(%s) failed during launcher-placeholder staging: %s (%s)",
			template_name, tostring(err), table.concat(slots or {}, ", "))
	end
	return ok
end

-- Waits for the next view whose app_id satisfies `predicate(app_id)`,
-- then calls on_view(view) exactly once and removes its own callback. See
-- launch_and_wait's equivalent in fill-then-retry.lua for why every
-- registration below happens either up front or behind a predicate
-- specific enough to safely ignore an out-of-order/same-dispatch firing.
local function on_next_view(predicate, on_view)
	local id
	id = scroll.add_callback("view_map", function(view, _data)
		if not view then
			return
		end
		local app_id = scroll.view_get_app_id(view)
		if predicate(app_id) then
			scroll.remove_callback(id)
			on_view(view)
		end
	end, nil)
end

local function exact_app_id_predicate(expected)
	return function(app_id)
		return app_id == expected
	end
end

local function collect_slots(template)
	local slots = {}
	local function walk(node)
		if node.children then
			for _, child in ipairs(node.children) do
				walk(child)
			end
		else
			table.insert(slots, node.slot)
		end
	end
	for _, root in ipairs(template.tiling or {}) do
		walk(root)
	end
	for _, root in ipairs(template.floating or {}) do
		walk(root)
	end
	return slots
end

-- Waits for `target_view` (a specific, already-known view) to unmap, then
-- calls on_unmap() exactly once and removes its own callback. Safe to
-- call from inside a currently-firing view_map handler: view_unmap is a
-- separate callback list from view_map in scroll's dispatcher, so
-- registering here can never be invoked "for free" within a view_map
-- dispatch pass the way registering another view_map callback could be
-- (see on_next_view's comment above).
local function on_view_unmap(target_view, on_unmap)
	local id
	id = scroll.add_callback("view_unmap", function(view, _data)
		if view ~= target_view then
			return
		end
		scroll.remove_callback(id)
		on_unmap()
	end, nil)
end

-- Once the real app for `slot` has mapped: bind it in place of the
-- placeholder, re-apply, and close the now-vacated placeholder (which
-- landed in the scratchpad when it fell out of the mapping).
local function swap_in_real_app(template_name, slot, real_view)
	local real_container = scroll.view_get_container(real_view)
	local placeholder_con_id = slot_placeholder_con_id[slot]
	slot_con_id[slot] = scroll.container_get_id(real_container)
	slot_placeholder_con_id[slot] = nil

	if not reapply(template_name) then
		return
	end

	if placeholder_con_id then
		scroll.command(placeholder_con_id, "kill")
	end
end

--- Stages every slot of `template_name` with a placeholder terminal
--- running LAUNCHER_COMMAND, applies the template, and swaps each
--- placeholder for the real application once the user picks one (detected
--- as the next mapped view for which `real_app_predicate(app_id)` returns
--- true; defaults to default_real_app_predicate(), i.e. "anything that
--- isn't one of our own placeholders").
function space_template_launcher_placeholders(template_name, real_app_predicate)
	real_app_predicate = real_app_predicate or default_real_app_predicate

	local template, err = scroll.space_template_get(template_name)
	if not template then
		log("space_template_launcher_placeholders: could not load %s: %s", template_name, tostring(err))
		return false
	end

	local slots = collect_slots(template)
	local pending = #slots

	for _, slot in ipairs(slots) do
		on_next_view(exact_app_id_predicate(PLACEHOLDER_PREFIX .. slot), function(placeholder_view)
			local placeholder_container = scroll.view_get_container(placeholder_view)
			local placeholder_con_id = scroll.container_get_id(placeholder_container)
			slot_con_id[slot] = placeholder_con_id
			slot_placeholder_con_id[slot] = placeholder_con_id

			pending = pending - 1
			if pending == 0 then
				reapply(template_name)
			end

			-- Once *this* placeholder's own window unmaps (your launcher
			-- script hands off after the user picks something in it), the
			-- next view to map is -- causally -- that same handoff's real
			-- application, so bind it to this slot. This correctly
			-- distinguishes which slot a selection happened in even though
			-- real_app_predicate alone can't (it can't tell two ordinary
			-- application windows apart by app_id/class alone). It is not
			-- airtight if the user makes selections in two different
			-- placeholders within the same instant -- an inherent limit of
			-- matching by "the next window to map" rather than a real
			-- handoff protocol, which is outside scroll's scope.
			on_view_unmap(placeholder_view, function()
				on_next_view(real_app_predicate, function(real_view)
					swap_in_real_app(template_name, slot, real_view)
				end)
			end)
		end)

		scroll.exec_process(string.format(LAUNCHER_COMMAND, slot, slot))
	end

	return true
end
