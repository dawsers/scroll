---@meta
---
---
local scroll = {}

---
--- Add message to the DEBUG log of scroll
---
--- @param message string
---
--- @return integer
function scroll.log(message) end

---
--- Sets key = value in the script's state.
--- This value will be preserved between script runs
---
--- @param state userdata
--- @param key string
--- @param value any
---
--- @return integer
function scroll.state_set_value(state, key, value) end

---
--- Returns the value stored at key in the script's state.
---
--- @param state userdata
--- @param key string
---
--- @return any
function scroll.state_get_value(state, key) end

---
--- Encodes and sends 'data' (a Lua table as complex as needed) as a json
--- object through the IPC interface, generating a new LUA event with 'id'
--- (string)
---
--- @param id string
--- @param data table
---
--- @return integer
function scroll.ipc_send(id, data) end

---
--- Execute a scroll command with container or workspace as its context.
--- If the first paramenter is nil, use the default context, usually the
--- focused container or workspace.
---
--- Returns all the results/errors in an array.
---
--- @param context userdata|nil
--- @param command string
---
--- @return string[]
function scroll.command(context, command) end

---
--- Returns the focused view pointer or nil if none.
---
--- @return userdata|nil
function scroll.focused_view() end

---
--- Returns the focused container pointer or nil if none.
---
--- @return userdata|nil
function scroll.focused_container() end

---
--- Returns the focused workspace pointer or nil if none.
---
--- @return userdata|nil
function scroll.focused_workspace() end

---
--- Returns the current urgent view pointer or nil if none.
---
--- @return userdata|nil
function scroll.urgent_view() end

---
--- Returns true if view is mapped (exists), otherwise returns false
---
--- @param view userdata
---
--- @return boolean
function scroll.view_mapped(view) end

---
--- Returns a pointer to the container associated to view, or nil if none.
---
--- @param view userdata
---
--- @return userdata|nil
function scroll.view_get_container(view) end

---
--- Returns the app_id string for view, or nil if any error happens
---
--- @param view userdata
---
--- @return string|nil
function scroll.view_get_app_id(view) end

---
--- Returns the title string for view, or nil if any error happens
---
--- @param view userdata
---
--- @return string|nil
function scroll.view_get_title(view) end

---
--- Returns the pid number for view, or nil if any error happens
---
--- @param view userdata
---
--- @return integer|nil
function scroll.view_get_pid(view) end

---
--- Returns the view whose pid is the parent of the application running
--- in view, or nil if it has no parent view.
---
--- @param view userdata
---
--- @return userdata|nil
function scroll.view_get_parent_view(view) end

---
--- If the view has the urgent attribute set, return true, otherwise false.
---
--- @param view userdata
---
--- @return boolean
function scroll.view_get_urgent(view) end

---
--- Sets the view urgent attribute
---
--- @param view userdata
--- @param urgent boolean
---
--- @return integer
function scroll.view_set_urgent(view, urgent) end

---
--- Returns `xdg_shell` if the application running in view is a Wayland
--- application. If it is an X windows application, returns `xwayland`.
---
--- @param view userdata
---
--- @return string|nil
function scroll.view_get_shell(view) end

---
--- Returns the tag string property for view, or nil if any error happens.
---
--- @param view userdata
---
--- @return string|nil
function scroll.view_get_tag(view) end

---
--- Close/kill view
---
--- @param view userdata
---
--- @return number
function scroll.view_close(view) end

---
--- Sets the focus on container.
---
--- @param container userdata
---
--- @return integer
function scroll.container_set_focus(container) end

---
--- Returns the container's parent workspace pointer, or nil if none.
---
--- @param container userdata
---
--- @return userdata|nil
function scroll.container_get_workspace(container) end

---
--- Returns an array with all the marks associated to container.
---
--- @param container userdata
---
--- @return string[]
function scroll.container_get_marks(container) end

---
--- Returns true if the container is floating, false if it is tiled.
---
--- @param container userdata
---
--- @return boolean
function scroll.container_get_floating(container) end

---
--- Returns the numerical value for the container's opacity.
---
--- @param container userdata
---
--- @return number
function scroll.container_get_opacity(container) end

---
--- Returns true if the container is sticky, otherwise false.
---
--- @param container userdata
---
--- @return boolean
function scroll.container_get_sticky(container) end

---
--- Returns the value for the container's width fraction.
--- This value is used to compute the width of the container.
---
--- @param container userdata
---
--- @return number
function scroll.container_get_width_fraction(container) end

---
--- Returns the value for the container's height fraction.
--- This value is used to compute the height of the container.
---
--- @param container userdata
---
--- @return number
function scroll.container_get_height_fraction(container) end

---
--- Returns a floating point value with the container's width in pixels.
---
--- @param container userdata
---
--- @return number
function scroll.container_get_width(container) end

---
--- Returns a floating point value with the container's height in pixels.
---
--- @param container userdata
---
--- @return number
function scroll.container_get_height(container) end

---
--- Returns a string with the fullscreen state for container.
--- Values can be `none`, `workspace` (covers only its workspace extents)
--- or `global` (covers all outputs).
---
--- @param container userdata
---
--- @return string
function scroll.container_get_fullscreen_mode(container) end

---
--- Returns a string with the fullscreen_application state for container.
--- Values can be `disabled` or `enabled`.
--- See the `fullscreen application` command for details.
---
--- @param container userdata
---
--- @return string
function scroll.container_get_fullscreen_app_mode(container) end

---
--- Returns a string with the fullscreen_view state for container.
--- Values can be `disabled` or `enabled`.
--- This is the internal state of the container. A container
--- can be displayed in full screen mode because an application requested it,
--- but this state can still be "disabled", so the compositor knows the
--- container should become non-full screen when the request ends.
---
--- @param container userdata
---
--- @return string
function scroll.container_get_fullscreen_view_mode(container) end

---
--- Returns a string with the fullscreen_layout state for container.
--- Values can be `disabled` or `enabled`.
--- See the `fullscreen layout` command for details.
---
--- @param container userdata
---
--- @return string
function scroll.container_get_fullscreen_layout_mode(container) end

---
--- Returns a string with the pin mode for container.
--- Values can be `none`, `beginning` or `end`.
--- See the pin command for details.
---
--- @param container userdata
---
--- @return string
function scroll.container_get_pin_mode(container) end

---
--- Returns the container parent of container or nil if it is a top
--- level container.
---
--- @param container userdata
---
--- @return userdata|nil
function scroll.container_get_parent(container) end

---
--- Returns an array with all the children containers of container, if any.
--- Only top level containers have children. Only bottom level containers
--- have views.
---
--- @params container userdata
---
--- @return userdata[]
function scroll.container_get_children(container) end

---
--- Returns an array with all the views inside container.
--- If a top level container, it will return the views of all its children,
--- if a bottom level container, its only view.
---
--- @param container userdata
---
--- @return userdata[]
function scroll.container_get_views(container) end

---
--- Returns an integer value with the unique container id, or nil if error.
---
--- @param container userdata
---
--- @return integer|nil
function scroll.container_get_id(container) end

---
--- Sets the focus on the last active container of workspace.
---
--- @param workspace userdata
---
--- @return integer
function scroll.workspace_set_focus(workspace) end

---
--- Returns a string with the name of the workspace, or nil if error.
---
--- @param workspace userdata
---
--- @return string|nil
function scroll.workspace_get_name(workspace) end

---
--- Returns an array with all the tiling containers inside the workspace.
---
--- @param workspace userdata
---
--- @return userdata[]
function scroll.workspace_get_tiling(workspace) end

---
--- Returns an array with all the floating containers inside the workspace.
---
--- @param workspace userdata
---
--- @return userdata[]
function scroll.workspace_get_floating(workspace) end

---
--- Returns a table with all the current mode modifiers for workspace.
--- The keys and values of that table are:
---
---   mode: "horizontal"|"vertical"|"none"
---   insert: "before"|"after"|"beginning"|"end"
---   reorder: "auto"|"lazy"
---   focus: true|false
---   center_horizontal: true|false
---   center_vertical: true|false
---
--- @param workspace userdata
---
--- @return table
function scroll.workspace_get_mode(workspace) end

---
--- Sets the mode modifiers for workspace.
--- The modifiers table can override any number of modifiers.
--- The keys and values that table can include are at most:
---
---   mode: "horizontal"|"vertical"
---   insert: "before"|"after"|"beginning"|"end"
---   reorder: "auto"|"lazy"
---   focus: true|false
---   center_horizontal: true|false
---   center_vertical: true|false
---
--- @param workspace userdata
--- @param modifiers table
---
--- @return integer
function scroll.workspace_set_mode(workspace, modifiers) end

---
--- Returns the layout type, `horizontal` or `vertical`.
---
--- @param workspace userdata
---
--- @return string|nil
function scroll.workspace_get_layout_type(workspace) end

---
--- Sets the workspace's layout type to layout_type,
--- which can be `horizontal` or `vertical`.
---
--- @param workspace userdata
--- @param layout_type string
---
--- @return integer
function scroll.workspace_set_layout_type(workspace, layout_type) end

---
--- Returns an integer number with the workspace's width in pixels.
---
--- @param workspace userdata
---
--- @return integer|nil
function scroll.workspace_get_width(workspace) end

---
--- Returns an integer number with the workspace's height in pixels.
---
--- @param workspace userdata
---
--- @return integer|nil
function scroll.workspace_get_height(workspace) end

---
--- Returns the workspace's pinned container pointer, or nil if none.
---
--- @param workspace userdata
---
--- @return userdata|nil
function scroll.workspace_get_pin(workspace) end

---
--- Returns a table with the split state for workspace.
--- The keys and values of that table are:
---
---   split: "none"|"top"|"bottom"|"left"|"right"
---   fraction: number
---   gap: integer
---   sibling: the sibling workspace's pointer
---
--- @param workspace userdata
---
--- @return table
function scroll.workspace_get_split(workspace) end

---
--- Returns the workspace's output pointer, or nil if none.
---
--- @param workspace userdata
---
--- @return userdata|nil
function scroll.workspace_get_output(workspace) end

---
--- Returns true if the output (display) is enabled, or false otherwise.
---
--- @param output userdata
---
--- @return boolean
function scroll.output_get_enabled(output) end

---
--- Returns the name of the output's interface. For example 'DP-3'.
---
--- @param output userdata
---
--- @return string|nil
function scroll.output_get_name(output) end

---
--- Returns the workspace currently active on output.
---
--- @param output userdata
---
--- @return userdata|nil
function scroll.output_get_active_workspace(output) end

---
--- Returns an array with all the existing workspaces assigned to output.
---
--- @praam output userdata
---
--- @return userdata[]
function scroll.output_get_workspaces(output) end

---
--- Returns an array with all the outputs (displays).
---
--- @return userdata[]
function scroll.root_get_outputs() end

---
--- Returns an array with all the containers in the scratchpad.
---
--- @return userdata[]
function scroll.scratchpad_get_containers() end

---
--- Sets a cb_func callback function for event, passing cb_data data to
--- it.
---
--- event can be:
---
--- view events: cb_func is a Lua function with two parameters, view
--- (the view triggering the event) and data, the cb_data value passed when
--- creating the callback.
---
---   "view_map": application's window creation.
---   "view_unmap": right before an application's window destruction.
---   "view_urgent": a window gets the urgent attribute set.
---   "view_focus": a window gets focus.
---   "view_float" a window becomes floating or goes back to tiled.
---
--- workspace events: cb_func is Lua function with two parameters, workspace
--- (the workspace triggering the event) and  data , the  cb_data  value passed
--- when creating the callback.
---
---   "workspace_create": called when a workspace is created.
---   "workspace_focus": called when focusing a workspace.
---
--- ipc events:
---
---   "ipc_view": called every time an IPC event for a window happens. cb_func
---     is Lua function with three parameters: view  (the view triggering the event),
---     change (a string with the event name), and data, the cb_data value
---     passed when creating the callback.
---   "ipc_workspace": called every time an IPC event for a workspace happens.
---     cb_func is Lua function with four parameters: old_ws (the old workspace),
---     new_ws (the new workspace), change (a string with the event name), and
---     data, the cb_data value	passed when creating the callback.
---
--- cb_data can be any Lua variable, including a table with multiple values.
---
--- This function returns an id you need to store if you want to be able to
--- remove the callback later.
---
--- @param event string
--- @param cb_func function
--- @param cb_data any
---
--- @return lightuserdata|nil
function scroll.add_callback(event, cb_func, cb_data) end

---
--- Removes a callback set earlier using add_callback.
--- id is the unique identifier returned by add_callback.
---
--- @param id lightuserdata
---
--- @return integer
function scroll.remove_callback(id) end

return scroll
