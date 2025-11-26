scroll(5)

# NAME

scroll - configuration file and commands

# DESCRIPTION

A scroll configuration file is a list of scroll commands that are executed by scroll
on startup.  These commands usually consist of setting your preferences and
setting key bindings. An example config is likely present in /etc/scroll/config
for you to check out.

Lines in the configuration file might be extended through multiple lines by
adding a '\\' character at the end of line. e.g.:

```
bindsym Shift+XF86AudioRaiseVolume exec \\
	pactl set-sink-volume @DEFAULT_SINK@ -1%
```

Commands can also be given as a block in the form *command { <subcommands...>
}*. Anything before the opening *{* will be prepended to the lines inside the
block. For example:

```
output eDP-1 {
	background ~/wallpaper.png fill
	resolution 1920x1080
}
```

is identical to

```
output eDP-1 background ~/wallpaper.png fill
output eDP-1 resolution 1920x1080
```

These commands can be executed in your config file, via *scrollmsg*(1), or via
the bindsym command.

# COMMAND CONVENTIONS

Commands are split into several arguments using spaces. You can enclose
arguments with quotation marks (*"..."* or *'...'*) to add spaces to a single
argument. You may also run several commands in order by separating each with
*,* or *;*. Criteria is retained across commands separated by *,*, but will be
reset (and allow for new criteria, if desired) for commands separated by a *;*.

Throughout the documentation, *|* is used to distinguish between arguments for
which you may only select one. *[...]* is used for optional arguments, and
*<...>* for arguments where you are expected to supply some value.

# COMMANDS

This section only lists general commands. For input and output commands, refer
to *scroll-input*(5) and *scroll-output*(5).

## Config only commands
The following commands may only be used in the configuration file.

*align_reset_auto* <true|false>
	The default value is _true_. If _true_, alignment is reset when changing
	focus, otherwise the user needs to do it manually through *align reset*.

*animations* <animations-subcommands...>
	Subcommands are:

	*enabled* <yes|no>
	Default value is _yes_. Enables/disables animations globally.

	*frequency_ms* <number>
	Default is _16_. Number of milliseconds for each animation step. The
	default is approximately the refresh rate of a monitor that works at 60 Hz.
	If you need smoother animations, reduce this value. Use with caution to
	avoid crashes or performance issues. The default should work for most cases
	unless you have a very high refresh rate monitor.

	*style* <clip|scale>
	Default is _scale_. _clip_ keeps the resolution of the client and clips the
	container while animating. This leads to a part of the container showing the
	background when resizing containers to a smaller size, but at the same time,
	the content is always the same, making it easier to read. _scale_, instead,
	scales the content of the container to the animated size of the container.
	This makes the transitions smoother, but the content may be deformed
	while animating.

	*default* enabled [duration] [var animation curve] [off animation curve]
	Default is default is _yes 300 var 3 [ 0.215 0.61 0.355 1 ]_.
	Defines the default animation curve. Follows the format explained below.

	*window_move* enabled [duration] [var animation curve] [off animation curve]
	Default is not defined, so it uses _default_ unless you set one up in your
	config. It is the curve for windows movement (`move` command).

	*window_open* enabled [duration] [var animation curve] [off animation curve]
	Default is not defined, so it uses _default_ unless you set one up in your
	config. It is the curve for windows opening.

	*window_size* enabled [duration] [var animation curve] [off animation curve]
	Default is not defined, so it uses _default_ unless you set one up in your
	config. It is the curve for windows resizing (_cycle_size_, _set_size_,
	_fit_size_, and _resize_ commands).

	*window_update* enabled [duration] [var animation curve] [off animation curve]
	Default is not defined, so it uses _default_ unless you set one up in your
	config. It is the curve for a client window requesting an update.

	*workspace_switch* enabled [duration] [var animation curve] [off animation curve]
	Default is not defined, so it uses _default_ unless you set one up in your
	config. It is the curve for workspace switching animations.

	Format of an animation curve:

	The first field is _enabled_. It can be _yes_ or _no_. It enables or
	disables animations for that operation. If set to _no_, you don't need to
	define the curve. If set to _yes_, you can set the _duration_ if you
	want to use the _default_ curve, or define the curves using the following
	options.

	_duration_ms_. Duration of the animation in milliseconds.

	_var_. Defines the animation curve for the variable changing in the
	operation/command. The fields are _order_ and _control_points_. _order_
	defines the order of the Bezier curve that follows, and _control_points_ is
	an array defining its control points except for the first, that is _(0, 0)_,
	and the last, _(1, 1)_. The number of values in the array will be
	_2 x (order - 1)_. A Bezier curve of _order_ needs _order + 1_ control
	points to be defined, but we set the first and last to _(0, 0)_ and _(1, 1)_.
	We use 2D Bezier curves, so the array needs _2 x (order - 1)_ numbers in it.

	*scroll* uses a different parametrization for its 2D Bezier curves than other
	compositors that only support cubic Bezier curves with two user-defined
	control points. This is because *scroll* supports Bezier curves of any _order_,
	and this may produce curves with more than one y value per x value. So
	*scroll* uses the fraction of the curve's length as parameter, to ensure
	curve points can be defined in an unique way, while those other compositors
	use the curve's x value because their curves cannot contain loops. For
	compatibility with curves from other compositors and CSS, the _var_ animation
	curve can have an optional argument, _simple_, that replaces _order_. This
	creates a compatible cubic Bezier. For example:

```
    default yes 300 var simple [ 0.6 -0.28 0.735 0.045 ]
```

	instead of:

```
    default yes 300 var 3 [ 0.6 -0.28 0.735 0.045 ]
```

	_off_. Defines the animation curve for the variable that doesn't change in the
	operation/command. The fields are _scale_ _order_ and _control_points_.
	_scale_ is the fraction of the workspace size used to scale the curve. As
	the curve is defined from _(0, 0)_ to _(1, 0)_, we need a parameter to scale
	the offset value. This parameter does that. For example, using a small parameter
	like _0.05_, creates offsets of an order of 5% of the size of the workspace.
	_order_ and _control_points_ work as in the _var_ case. But this time, the
	points you will not include are: _(0, 0)_ (first) and _(1, 0)_ (last).

*bar* [<bar-id>] <bar-subcommands...>
	For details on bar subcommands, see *scroll-bar*(5).

*cursor_shake_magnify* <true|false>
	Default value is _false_. If _true_, shaking the cursor will magnify it. It
	can be useful to find the location of the cursor on very big displays.

*cycle_size_wrap* <true|false>
	Default value is _false_. Enables wrapping sizes in the *cycle_size* command.
	Incrementing from the last available size will start again from the
	beginning, and decrementing the first one will move to the last.

*default_orientation* <horizontal|vertical|auto>
	Sets the default container layout for tiled containers.

*fullscreen_movefocus* <true|false> [follow|nofollow]
	Default value is _true_ and _nofollow_.

	_true_. Allows you to change focus while in full screen mode.

	_false_. Focusing other containers in the same workspace is not allowed
	until exiting full screen mode.

	_follow_. Full screen will follow focus. You can still use overview/jump to
	select a new window, and full screen state will follow.

	_nofollow_. You can focus other windows, but they will not become full
	screen. However, those in full screen mode will recover that mode when
	focusing on them again.

*gesture_scroll_enable* <true|false>
	Default value is _true_. Enables trackpad layout scrolling.

*gesture_scroll_fingers* <number>
	Default is _3_. Number of fingers assigned to the scrolling gesture.

*gesture_scroll_sensitivity* <number>
	Default is _1.0_. Increase if you want more sensitivity.

*include* <paths...>
	Include files from _paths_. _paths_ can include either a full path or a
	path relative to the parent config, and expands shell syntax (see
	*wordexp*(3) for details). The same include file can only be included once;
	subsequent attempts will be ignored.

*jump_labels_background* <color>
	Default is _#00000000_ (transparent). Color of the background of the jump
	labels.

*jump_labels_color* <color>
	Default is _#159E3080_. Color of the jump labels.

*jump_labels_keys* <string>
	Default is _1234_. Keys that will be used to generate possible jump labels.

*jump_labels_scale* <number>
	Default is _0.5_. Scale of the label within the window.

*layout_default_height* <number>
	Default is _1.0_. Sets the default height for new windows
	("horizontal" layouts) or rows ("vertical" layouts).

*layout_default_width* <number>
	Default is _0.5_. Sets the default width for new columns
	("horizontal" layout) or windows ("vertical" layouts).

*layout_heights* <array of numbers>
	Default is _[0.33333333 0.5 0.66666667 1.0]_. These are the fractions
	_cycle_size_ will use when resizing the height of columns/windows.

*layout_widths* <array of numbers>
	Default is _[0.33333333 0.5 0.66666667 1.0]_. These are the fractions
	_cycle_size_ will use when resizing the width of columns/windows.

*maximize_if_single* <true|false>
	Default value is _false_. If _true_, whenever there is a single tiling
	window in the workspace, it will be maximized, unless you explicitly resize
	it.

*swaybg_command* <command>
	Executes custom background _command_. Default is _swaybg_. Refer to
	*scroll-output*(5) for more information.

	It can be disabled by setting the command to a single dash:
	_swaybg\_command -_

*scrollnag_command* <command>
	Executes custom command for _scrollnag_. Default is _scrollnag_. Additional
	arguments may be appended to the end. This should only be used to either
	direct scroll to call scrollnag from a custom path or to provide additional
	arguments. This should be placed at the top of the config for the best
	results.

	It can be disabled by setting the command to a single dash:
	_scrollnag\_command -_

*workspace_layout* default|horizontal|vertical
	Specifies the initial layout for new containers in an empty workspace.

*workspace_next_on_output_create_empty* <true|false>
	The default is _true_. If _true_, when calling *workspace next_on_output*,
	if the current workspace is the last one, it will create a new workspace
	instead	of wrapping to the first one. The new workspace will be the first
	unused one for that output. If _false_, instead of creating a new workspace,
	it will focus on the first one.

*xwayland* enable|disable|force
	Enables or disables Xwayland support, which allows X11 applications to be
	used. _enable_ will lazily load Xwayland so Xwayland will not be launched
	until the first client attempts to connect. In some cases, such as slower
	machines, it may be desirable to have Xwayland started immediately by
	using _force_ instead of _enable_.

*xwayland_output_scale* <true|false>
	The default is _true_. If _true_, XWayland windows get fractional scaling.
	Make it _false_ if you want XWayland windows to ignore the output's
	fractional scaling value. This option is equivalent to applying to each
	XWayland window a content scale with value the inverse of the output's
	fractional scale.

## Runtime only commands
The following commands cannot be used directly in the configuration file.
They are expected to be used with *bindsym* or at runtime through *scrollmsg*(1).

*align* <left|right|center|up|down|middle|reset>
	Columns are generally aligned in automatic mode, always making the active
	one visible, and trying to make at least the previously focused one visible
	too if it fits the viewport, if not, the one adjacent on the other side.
	However, you can always align any column to the center, left or right
	of the monitor (in horizontal mode), or up (top), down (bottom) or to
	the center in vertical mode. For example center a column for easier
	reading, regardless of what happens to the other columns. If you want to
	go back to automatic mode, you need to use the _set_mode_ modifier
	reorder_auto or call _align_ reset. The only time alignment is lost is if
	you open a new window.

	See *align_reset_auto* for a configuration command to also reset alignment
	when changing focus.

	You can also center a window on your workspace using _middle_, it will
	center its column, and also the window within the column.

*border* none|normal|csd|pixel [<n>]
	Set border style for focused window. _normal_ includes a border of
	thickness _n_ and a title bar. _pixel_ is a border without title bar _n_
	pixels thick. The title bar always shows in stacking or tabbed layouts.
	_csd_ is short for client-side-decorations, which allows the client to draw
	its own decorations. Default is _normal_ with border thickness 2.

*border* toggle
	Cycles through the available border styles.

*cycle_size* <h|v> <next|prev>
	Cycles the widths/heights of windows/columns/rows in horizontal or vertical
	modes. See the *cycle_size_wrap* option if you want to wrap from the last
	available size to the first one when incrementing, and from the first to the
	last one when decrementing.

*decoration* [border_radius <n>] [shadow true|false] [shadow_dynamic true|false] \
[shadow_size <n>] [shadow_blur <n>] [shadow_offset x y] [shadow_color #RRGGBBAA] \
[dim true|false] [dim_color #RRGGBBAA]
	Sets the focused window's decoration parameters

	_border radius_: Radius of the border, in pixels.
	_shadow_: Turns on/off shadow casting for the window.
	_shadow_dynamic_: Turns of/off dynamic shadows. Dynamic shadows make the
		shadow offset variable depending on the location of the window.
	_shadow_size_: size in pixels of the shadow.
	_shadow_blur_: Value of the blur radius in pixels, where 0 means sharp shadows.
	_shadow_offset_: Shadow's offset from the window.
	_shadow_color_: Color of the shadow.
	_dim_: Turns on/off dimming of the content when the window is not in focus.
	_dim_color_: Color used to dim the content when the window is not in focus.

*exit*
	Exit scroll and end your Wayland session.

*fit_size* <h|v> <active|visible|all|toend|tobeg> <proportional|equal>
	Allows re-fitting the columns (horizontal mode) or windows (vertical mode)
	you want to the screen extents. It accepts an argument related to the
	columns/windows it will try to fit. The new width/height of each
	column/window will be _proportional_ to its previous width or height,
	relative to the other columns or windows affected, or _equal_ for all of them.

	_active_: It will maximize the active column/window.
	_visible_: All the currently fully or partially visible columns/windows
	will be resized to fit in the screen.
	_all_: All the columns in the row or windows in the column will be resized
	to fit in the screen.
	_toend_: All the columns or windows from the focused one to the end of the
	row/column will be affected.
	_tobeg_ or _tobeginning_: All the columns/windows from the focused one to
	the beginning of the row/column will now fit in screen.

*floating* enable|disable|toggle
	Make focused window floating, non-floating, or the opposite of what it is now.

<criteria> *focus*
	Moves focus to the container that matches the specified criteria.

*focus* up|right|down|left|begin|end
	Moves focus to the next container in the specified direction.

*focus* prev|next [sibling]
	Moves focus to the previous or next container in the current layout. By default,
	the last active child of the newly focused container will be focused. The _sibling_
	option indicates not to immediately focus a child of the container.

*focus* child
	Moves focus to the last-focused child of the focused container.

*focus* parent
	Moves focus to the parent of the focused container.

*focus* output up|right|down|left
	Moves focus to the next output in the specified direction.

*focus* output <name>
	Moves focus to the named output.

*focus* tiling
	Sets focus to the last focused tiling container.

*focus* floating
	Sets focus to the last focused floating container.

*focus* mode_toggle
	Moves focus between the floating and tiled layers.

*fullscreen* [enable|disable|toggle] [global|application|layout]
	Makes the focused window fullscreen, non-fullscreen, or the opposite of what it
	is now. If no argument is given, it does the same as _toggle_.

	_global_: the window will be fullscreen across all outputs.

	_application_: the window will be in fullscreen UI mode, while still fitting
	its container. Decouples the application's UI full screen mode with the
	compositor's full screen container mode. With different combinations of
	_fullscreen workspace_ and _fullscreen application_ you can have several
	"fake" full screen modes.

	_layout_: applies only to tiled windows. The focused window will be in full
	screen mode, but as part of the tiling layout. If you scroll away from it,
	it will still keep full screen resolution.

*gaps* inner|outer|horizontal|vertical|top|right|bottom|left all|current
set|plus|minus|toggle <amount>
	Changes the _inner_ or _outer_ gaps for either _all_ workspaces or the
	_current_ workspace. _outer_ gaps can be altered per side with _top_,
	_right_, _bottom_, and _left_ or per direction with _horizontal_ and
	_vertical_.

*inhibit_idle* focus|fullscreen|open|none|visible
	Set/unset an idle inhibitor for the window. _focus_ will inhibit idle when
	the window is focused by any seat. _fullscreen_ will inhibit idle when the
	view is fullscreen (or a descendant of a fullscreen container) and is
	visible. _open_ will inhibit idle until the window is closed (or the
	inhibitor is unset/changed). _visible_ will inhibit idle when the window is
	visible on any output. _none_ will remove any existing idle inhibitor for
	the window.

	This can also be used with criteria to set an idle inhibitor for any
	existing window or with _for_window_ to set idle inhibitors for future windows.

*jump* [container|workspaces|floating|tiling]
	*jump* with no arguments starts an overview mode where all the tiling
	windows of the visible workspaces fit in their monitors. Each window will have
	an overlay with the combination of keys that will change focus to each of them
	and exit jump mode. Pressing any other combination of keys exits jump mode
	with no change of focus. Clicking on an item will also select it. There are
	several options to modify the look and content of the labels.

	Using _tiling_ does the same as using no arguments.

	The argument _workspaces_, instead creates an overview of the workspaces
	available on each monitor for a quick preview and navigation to any workspace.

	_container_ creates an overview of the windows of the active column
	(horizontal layout) or row (vertical layout), so you can quickly jump to
	any of them. It is a good replacement to tabbed navigation, because it also
	shows the content of the windows.

	_floating_ starts a similar jump mode showing all the floating windows of
	the	visible workspaces. Windows move so there are no overlaps for a better
	preview.

*layout_transpose*
	Allows you to change from one type of layout to the other at runtime. For
	example, you want to move your current workspace from a landscape monitor
	to one in portrait mode: *move* the workspace, and then call
	*layout_transpose*; it will change your existing layout from a row of
	columns to a column of rows, keeping all your windows in the same relative
	positions. You can undo it by calling *layout_transpose* again.

*max_render_time* off|<msec>
	Controls when the relevant application is told to render this window, as a
	positive number of milliseconds before the next time scroll composites the
	output. A smaller number leads to fresher rendered frames being composited
	by scroll and lower perceived input latency, but if set too low, the
	application may not finish rendering before scroll composites the output,
	leading to delayed frames.

	When set to off, the relevant application is told to render this window
	immediately after display refresh. How much time is left for rendering
	before scroll composites the output at that point depends on the output
	*max_render_time* setting.

	To set this up for optimal latency:
	. Set up *output max_render_time* (see *scroll-output*(5)).
	. Put the target application in _full-screen_ and have it continuously
	  render something.
	. Start by setting *max_render_time 1*. If the application drops
	  frames, increment by *1*.

	This setting only has an effect if a per-output *max_render_time* is in
	effect on the output the window is currently on. See *scroll-output*(5) for
	further details.

*allow_tearing* yes|no
	Allows or disallows screen tearing as a result of immediate page flips
	for a fullscreen application.

	When this option is not set, the tearing hints provided by the application
	determine whether tearing is allowed. When _yes_ is specified,
	the application allows tearing regardless of the tearing hints.
	When _no_ is specified, tearing will never be allowed on the application,
	regardless of the tearing hints.

	This setting only has an effect if tearing is allowed on the output through
	the per-output *allow_tearing* setting. See *scroll-output*(5) for further
	details.

*move* left|right|up|down|beinning|end [<px> px] [nomode]
	Moves the focused container in the direction specified. The optional _px_
	argument specifies how many pixels to move the container if it is floating.
	If unspecified,	the default is 10 pixels. Pixels are ignored when moving
	tiled containers.

	For tiled containers, depending on the mode and layout type, you will be
	moving columns/rows or windows. You can move them in any direction, or to
	the end/beginning of the row/column.

	Aside from that, you can use _nomode_. With that argument, windows will
	move freely. Movement will only move the active window, leaving its column
	intact, regardless of which mode (h/v) you are currently in. For a
	horizontal layout, the movement will be like this:

	If the window is in some column with other windows, any left or right
	movement will expel it to that side, creating a new column with just that
	window. Moving it again will insert it in the column in the direction of
	movement, and so on. Moving the window up or down will simply move it in
	its current column. You can still use beginning/end to move it to the
	edges of the row if it is alone in a column, or the edges of the column
	if it has siblings in that column.

*move* [absolute] position <pos_x> [px|ppt] <pos_y> [px|ppt]
	Moves the focused container to the specified position in the workspace.
	The position can be specified in pixels or percentage points, omitting
	the unit defaults to pixels. If _absolute_ is used, the position is
	relative to all outputs. _absolute_ can not be used with percentage points.

*move* [absolute] position center
	Moves the focused container to be centered on the workspace. If _absolute_
	is used, it is moved to the center of all outputs.

*move* position cursor|mouse|pointer
	Moves the focused container to be centered on the cursor.

*move* [container|window] [to] mark <mark>
	Moves the focused container to the specified mark.

*move* [--no-auto-back-and-forth] [container|window] [to] workspace [number] <name>
	Moves the focused container to the specified workspace. The string _number_
	is optional and is used to match a workspace with the same number, even if
	it has a different name.

*move* [container|window] [to] workspace prev|next|current
	Moves the focused container to the previous, next or current workspace on
	this output, or if no workspaces remain, the previous or next output.

*move* [container|window] [to] workspace prev_on_output|next_on_output
	Moves the focused container to the previous or next workspace on this
	output, wrapping around if already at the first or last workspace.

*move* [container|window] [to] workspace back_and_forth
	Moves the focused container to previously focused workspace.

*move* [container|window] [to] output <name-or-id>|current
	Moves the focused container to the specified output.

*move* [container|window] [to] output up|right|down|left
	Moves the focused container to next output in the specified
	direction.

*move* [container|window] [to] scratchpad
	Moves the focused container to the scratchpad.

*move* workspace [to] output <name-or-id>|current
	Moves the focused workspace to the specified output.

*move* workspace to [output] <name-or-id>|current
	Moves the focused workspace to the specified output.

*move* workspace [to] output up|right|down|left
	Moves the focused workspace to next output in the specified direction.

*move* workspace to [output] up|right|down|left
	Moves the focused workspace to next output in the specified direction.

*nop* <comment>
	A no operation command that can be used to override default behaviour. The
	optional comment argument is ignored, but logged for debugging purposes.

*pin* top-level container to <beginning|end>
	Pins a tiled top level container to either edge of the workspace. This may
	be useful when you have a very wide monitor, or you want to keep a column
	visible at all times. You may want to have some documentation or terminal
	always visible.

	It will *pin* the active top level container. For horizontal layouts, it
	will pin it to the left (_beginning_) or right (_end_) edge of the monitor.
	For	vertical layouts, to the top (_beginning_) or bottom (_end_) edge.
	*pin* works as a toggle, and there can only be one pin per workspace. The
	logic is as follows when you call *pin*:

	1. If the current container is already pinned: if you call *pin* with the
	same argument of the current pin, it will be unset and the container freed
	from its pin. If the argument is different, it will move the pinned
	container to the other position.
	2. If the current container is not pinned yet: it will replace the pinned
	container, if any.

*reload*
	Reloads the scroll config file and applies any changes. The config file is
	located at path specified by the command line arguments when started,
	otherwise according to the priority stated in *scroll*(1).

*rename* workspace [<old_name>] to <new_name>
	Rename either <old_name> or the focused workspace to the <new_name>

*resize* shrink|grow width|height [<amount> [px|ppt]]
	Resizes the currently focused container by _amount_, specified in pixels or
	percentage points. If the units are omitted, floating containers are resized
	in px and tiled containers by ppt. _amount_ will default to 10 if omitted.

*resize* set height <height> [px|ppt]
	Sets the height of the container to _height_, specified in pixels or
	percentage points. If the units are omitted, floating containers are
	resized in px and tiled containers by ppt. If _height_ is 0, the container
	will not be resized.

*resize* set [width] <width> [px|ppt]
	Sets the width of the container to _width_, specified in pixels or
	percentage points. If the units are omitted, floating containers are
	resized in px and tiled containers by ppt. If _width_ is 0, the container
	will not be resized.

*resize* set [width] <width> [px|ppt] [height] <height> [px|ppt]
	Sets the width and height of the container to _width_ and _height_,
	specified in pixels or percentage points. If the units are omitted,
	floating containers are resized in px and tiled containers by ppt. If
	_width_ or _height_ is 0, the container will not be resized on that axis.

*scale_content* <exact number|increment number|reset>
	Scales the content of a window independently of the current monitor scale
	or fractional scale. You can have several copies of the same application
	with different scales. This works well for Wayland and XWayland windows.
	Use _exact_ and a scale to set the scale.
	Use _increment_ with a delta value (for example 0.05 or -0.05) to increment
	or decrement the current scale. Use _reset_ to recover the original scale.

*scale_workspace* <exact number|increment number|reset|overview>
	This command will scale the workspace to an _exact_ scale, or
	incrementally by a delta value. If you want a useful automatic scale, use
	the _overview_ argument which will fit all the windows of the workspace
	in the current viewport.

	You will still be able to interact with the windows normally (change focus,
	move windows, create or destroy them, type in them etc.). Use it as a way
	to see where things are and move the active focus, or reposition windows.

*scratchpad* show|jump
	_show_ shows a window from the scratchpad. Repeatedly using this command will
	cycle through the windows in the scratchpad.

	_jump_ will show all the scratchpad windows in a jump overview mode. As with
	the *jump* command, you can press the keys shown on a label, or click on the
	window to change focus.

*selection* toggle|reset|workspace|move|to_trail
	This command manages window/container selections. You can select several
	windows or containers at a time, even in different workspaces and/or
	from overview mode. Those windows will change the border color to the one
	specified in the option _client.selected_.

	Use _toggle_ to select/deselect a window (in window mode) or a full
	container (in top-level mode).

	If you want to clear a selection, instead of "toggling" each
	window/container, you can call _reset_, which will clear all the selected
	items.

	Once you have made a selection, you can move those windows to a different
	workspace or location in the same workspace using _move_.
	The selection order and column/window configuration will be maintained.

	_workspace_ will add every window of the current workspace to the
	selection. You can use this when you want to move one workspace to a
	different one, but keeping windows positions and sizes. Use
	*selection workspace*, and then *selection move* where you want the
	windows to appear.

	_move_ uses the current mode modifiers to locate the new containers. So
	you can place the new containers _before_, _after_, at the
	beginning (_beg_) or _end_.

	If your new location has a different layout type (for example, vertical
	instead of horizontal), your containers and windows will adapt, transposing
	their positions to better fit their new destination.

	_to_trail_ creates a new trail with the contents of the selection and makes
	it active. You can create that trail to navigate through those windows
	quickly, or as storage for the selection (_trail to_selection_ can recover
	the selection from a stored trail).

*set_mode* [<h|v|t> <after|before|end|beg> <focus|nofocus>
<center_horiz|nocenter_horiz> <center_vert|nocenter_vert>
<reorder_auto|noreorder_auto>]
	Modes and mode modifiers are per workspace.
	At window creation time, scroll can apply several modifiers to the current
	working mode (h/v). It supports extra arguments:

	_<h|v|t>_: set horizontal, vertical, or toggle the current mode.

	_position_: It is one of after (default), before, end, beg. This parameter
	decides the position of new windows: after the current one (default value),
	before the current one, at the end of the row/column, or at the beginning
	of the row/column.

	_focus_: One of focus (default) or nofocus. When creating a new window,
	this parameter controls whether it will get focus or not.

	_reorder_ automatic mode: reorder_auto (default) or noreorder_auto.
	By default, scroll will reorder windows every time you change focus,
	move or create new windows. But sometimes you want to keep the current
	window in a certain position, for example when using _align_. _align_ turns
	reordering mode to noreorder_auto, and the window will keep its position
	regardless of what you do, until you set reorder_auto again.

	_center_horiz/nocenter_horiz_: It will keep the active column centered
	(or not) on the screen.

	_center_vert/nocenter_vert_: It will keep the active window centered
	(or not) in its column.

	You can skip any number of parameters when calling the command, and
	their order doesn't matter.

*set_size* <h|v> <fraction>
	It is similar to _cycle_size_, but instead of cycling the array of values,
	it sets the width (_h_) or height (_v_) to _fraction_ directly.

*shortcuts_inhibitor* enable|disable
	Enables or disables the ability of clients to inhibit keyboard
	shortcuts for a window. This is primarily useful for virtualization and
	remote desktop software. It affects either the currently focused window
	or a set of windows selected by criteria. Subcommand _disable_
	additionally deactivates any active inhibitors for the given window(s).
	Criteria are particularly useful with the *for_window* command to
	configure a class of windows differently from the per-seat defaults
	established by the *seat* subcommand of the same name. See
	*scroll-input*(5) for more ways to affect inhibitors.

*space* <save|load|restore> <name>
	A _space_ is a configuration of existing windows.

	_save_ stores the configuration of the current workspace, including
	window/container geometry and positions, content scale etc. under _name_.

	_load_ recovers the windows that still exist from that space and
	applies the configuration stored under _name_ to them. This only applies
	to windows that still exist, if you have closed any windows from the space,
	they won't be recovered. You can load a space in any workspace, it doesn't
	have to be the original one. Loading the space will gather all the windows
	in the saved space from any workspace where they may currently be, and set
	them in the current workspace using the configuration under _name_.

	_restore_ is like loading a space, but any window in the workspace where
	loading happens that doesn't belong to the space, will be closed. You can
	use this if you have opened several disposable applications in your
	workspace and want to remove the clutter by restoring your original
	configuration/space.

*sticky* enable|disable|toggle
	"Sticks" a floating window to the current output so that it shows up on all
	workspaces.

*swap* container with id|con_id|mark <arg>
	Swaps the position, geometry, and fullscreen status of two containers. The
	first container can be selected either by criteria or focus. The second
	container can be selected by _id_, _con_id_, or _mark_. _id_ can only be
	used with xwayland windows. If the first container has focus, it will retain
	focus unless it is moved to a different workspace or the second container
	becomes fullscreen on the same workspace as the first container. In either
	of those cases, the second container will gain focus.

*title_format* <format>
	Sets the format of window titles. The following placeholders may be used:

	*%title*
		The title supplied by the window

	*%app_id*
		The wayland app ID (applicable to wayland windows only)

	*%class*
		The X11 classname (applicable to xwayland windows only)

	*%instance*
		The X11 instance (applicable to xwayland windows only)

	*%shell*
		The protocol the window is using (typically xwayland or xdg_shell)

	*%sandbox_engine*
		The associated sandbox engine

	*%sandbox_app_id*
		The app ID provided by the associated sandbox engine

	*%sandbox_instance_id*
		The instance ID provided by the associated sandbox engine

	This command is typically used with *for_window* criteria. For example:

		for_window [title="."] title_format "<b>%title</b> (%app_id)"

	Note that markup requires pango to be enabled via the *font* command.

	The default format is "%title".

*toggle_size* <this|active|all|reset> <width_fraction> <height_fraction>
	Toggles the size for tiling windows in the current workspace. There are
	three modes of operation, and they affect the whole workspace:

	_normal_: *scroll*'s usual working mode. Only windows that call
	*toggle_size* manually (using _this_) are affected by the temporary sizing.

	_active_: set by calling *toggle_size* _active_, the _active_ (currently
	focused) window will be affected every time it changes, unless the user
	resized it explicitly while in *toggle_size* mode.

	_all_: set by calling *toggle_size* _all_, it affects all the windows in
	the workspace except for those that have been resized by the user during
	*toggle_size* mode.

	When you move a window from a workspace to another, it will be affected
	by the new workspace's mode regardless of its previous _toggle_size_ state.

	The _this_ argument doesn't set any mode, it just overrides the current
	window's state.

	_reset_ will return to _normal_ mode regardless of the current state.

	_width_fraction_ and _height_fraction_ are the new fractions to toggle. For
	example, if you wanted to always have the active window maximized, you
	could use:

```
	toggle_size active 1.0 1.0
```

	If you just want to have the current window maximized, you can call:

```
	toggle_size this 1.0 1.0
```

	*toggle_size* saves the sizes of the affected windows when you call the
	command so you can go back to them when toggling to normal mode again.

*trail* new|prev|next|delete|clear|to_selection|number
	Manages trails. A _trail_ is a path of trailmarks. A _trailmark_ is an
	anonymous mark. You can create as many trailmarks as you want in a trail,
	and as many trails as you want. A view can belong to any number of trails.

	_new_: creates a new trail and makes it the active one.
	_prev_: makes the previous trail on the list the active one.
	_next_: makes the next trail on the list the active one.
	_number_ <n>: makes trail number _n_ on the list the active one.
	_delete_: deletes the active trail from the list.
	_clear_: clears all the trailmarks in the active trail.
	_to_selection_: creates a _selection_ with the trailmarks of the active
	trail. See *selection* to learn what you can do with them.

*trailmark* toggle|prev|next|jump
	When you want to navigate to certain windows frequently, you can assign them
	a trailmark. A trailmark will become an anonymous mark assigned to a trail,
	and you can navigate the trailmarks of a trail using _prev_ and _next_. This
	way you can move between frequently used application windows.

	_toggle_: assigns/removes the trailmark from the active window.
	_prev_: focus the previous trailmarked window on the active trail.
	_next_: focus the next trailmarked window on the active trail.
	_jump_: enter jump mode for the windows in the active trail.


## Config or runtime commands
The following commands may be used either in the configuration file or at
runtime.

*assign* <criteria> [→] [workspace] [number] <workspace>
	Assigns windows matching _criteria_ (see *CRITERIA* for details) to
	_workspace_. The → (U+2192) is optional and cosmetic. This command is
	equivalent to:

		for_window <criteria> move container to workspace <workspace>

*assign* <criteria> [→] output left|right|up|down|<name>
	Assigns windows matching _criteria_ (see *CRITERIA* for details) to the
	specified output. The → (U+2192) is optional and cosmetic. This command is
	equivalent to:

		for_window <criteria> move container to output <output>

*bindsym* [--whole-window] [--border] [--exclude-titlebar] [--release] [--locked] \
[--to-code] [--input-device=<device>] [--no-warn] [--no-repeat] [--inhibited] \
[Group<1-4>+]<key combo> <command>
	Binds _key combo_ to execute the scroll command _command_ when pressed. You
	may use XKB key names here (*wev*(1) is a good tool for discovering these).
	With the flag _--release_, the command is executed when the key combo is
	released. If _input-device_ is given, the binding will only be executed for
	that input device and will be executed instead of any binding that is
	generic to all devices. If a group number is given, then the binding will
	only be available for that group. By default, if you overwrite a binding,
	scrollnag will give you a warning. To silence this, use the _--no-warn_ flag.

	For specifying modifier keys, you can use the XKB modifier names _Shift_,
	_Lock_ (for Caps Lock), _Control_, _Mod1_ (for Alt), _Mod2_ (for Num Lock),
	_Mod3_ (for XKB modifier Mod3), _Mod4_ (for the Logo key), and _Mod5_ (for
	AltGr). In addition, you can use the aliases  _Ctrl_ (for Control), _Alt_
	(for Alt), and _Super_ (for the Logo key).

	Unless the flag _--locked_ is set, the command will not be run when a
	screen locking program is active. If there is a matching binding with
	and without _--locked_, the one with will be preferred when locked and the
	one without will be preferred when unlocked. If there are matching bindings
	and one has both _--input-device_ and _--locked_ and the other has neither,
	the former will be preferred even when unlocked.

	Unless the flag _--inhibited_ is set, the command will not be run when
	a keyboard shortcuts inhibitor is active for the currently focused
	window. Such inhibitors are usually requested by remote desktop and
	virtualization software to enable the user to send keyboard shortcuts
	to the remote or virtual session. The _--inhibited_ flag allows one to
	define bindings which will be exempt from pass-through to such
	software. The same preference logic as for _--locked_ applies.

	Unless the flag _--no-repeat_ is set, the command will be run
	repeatedly when the key is held, according to the repeat
	settings specified in the input configuration.

	Bindings to keysyms are layout-dependent. This can be changed with the
	_--to-code_ flag. In this case, the keysyms will be translated into the
	corresponding keycodes in the first configured layout.

	Mouse bindings operate on the container under the cursor instead of the
	container that has focus. Mouse buttons can either be specified in the form
	_button[1-9]_ or by using the name of the event code (ex _BTN\_LEFT_ or
	_BTN\_RIGHT_). For the former option, the buttons will be mapped to their
	values in X11 (1=left, 2=middle, 3=right, 4=scroll up, 5=scroll down,
	6=scroll left, 7=scroll right, 8=back, 9=forward). For the latter option,
	you can find the event names using _libinput debug-events_.

	The priority for matching bindings is as follows: input device, group,
	and locked state.

	_--whole-window_, _--border_, and _--exclude-titlebar_ are mouse-only options
	which affect the region in which the mouse bindings can be triggered.  By
	default, mouse bindings are only triggered when over the title bar. With the
	_--border_ option, the border of the window will be included in this region.
	With the _--whole-window_ option, the cursor can be anywhere over a window
	including the title, border, and content. _--exclude-titlebar_ can be used in
	conjunction with any other option to specify that the titlebar should be
	excluded from the region of consideration.

	If _--whole-window_ is given, the command can be triggered when the cursor
	is over an empty workspace. Using a mouse binding over a layer surface's
	exclusive region is not currently possible.

	Example:
```
		# Execute firefox when alt, shift, and f are pressed together
		bindsym Mod1+Shift+f exec firefox
```

	*bindcode* [--whole-window] [--border] [--exclude-titlebar] [--release] \
[--locked] [--input-device=<device>] [--no-warn] [--no-repeat] [--inhibited] \
[Group<1-4>+]<code> <command>
	is also available for binding with key/button codes instead of key/button names.

*bindswitch* [--locked] [--no-warn] [--reload] <switch>:<state> <command>
	Binds <switch> to execute the scroll command _command_ on state changes.
	Supported switches are _lid_ (laptop lid) and _tablet_ (tablet mode)
	switches. Valid values for _state_ are _on_, _off_ and _toggle_. These
	switches are on when the device lid is shut and when tablet mode is active
	respectively. _toggle_ is also supported to run a command both when the
	switch is toggled on or off.

	Unless the flag _--locked_ is set, the command will not be run when a
	screen locking program is active. If there is a matching binding with
	and without _--locked_, the one with will be preferred when locked and the
	one without will be preferred when unlocked.

	If the _--reload_ flag is given, the binding will also be executed when
	the config is reloaded. _toggle_ bindings will not be executed on reload.
	The _--locked_ flag will operate as normal so if the config is reloaded
	while locked and _--locked_ is not given, the binding will not be executed.

	By default, if you overwrite a binding, scrollnag will give you a warning. To
	silence this, use the _--no-warn_ flag.

	Example:
```
		# Show the virtual keyboard when tablet mode is entered.
		bindswitch tablet:on busctl call --user sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 SetVisible b true

		# Log a message when the laptop lid is opened or closed.
		bindswitch lid:toggle exec echo "Lid moved"
```

*bindgesture* [--exact] [--input-device=<device>] [--no-warn] \
<gesture>[:<fingers>][:directions] <command>
	Binds _gesture_ to execute the scroll command _command_ when detected.
	Currently supports the _hold_, _pinch_ or _swipe_ gesture. Optionally
	can be limited to bind to a certain number of _fingers_ or, for a
	_pinch_ or _swipe_ gesture, to certain _directions_.

[[ *type*
:[ *fingers*
:< *direction*
|  hold
:- 1 - 5
:  none
|  swipe
:  3 - 5
:  up, down, left, right
|  pinch
:  2 - 5
:  all above + inward, outward, clockwise, counterclockwise

	The _fingers_ can be limited to any sensible number or left empty to accept
	any finger counts.
	Valid directions are _up_, _down_, _left_ and _right_, as well as _inward_,
	_outward_, _clockwise_, _counterclockwise_ for the _pinch_ gesture.
	Multiple directions can be combined by a plus.

	If a _input-device_ is given, the binding will only be executed for
	that input device and will be executed instead of any binding that is
	generic to all devices. By default, if you overwrite a binding,
	scrollnag will give you a warning. To silence this, use the _--no-warn_ flag.

	The _--exact_ flag can be used to ensure a binding only matches when exactly
	all specified directions are matched and nothing more. If there is matching
	binding with _--exact_, it will be preferred.

	The priority for matching bindings is as follows: input device, then
	exact matches followed by matches with the highest number of matching
	directions.

	Gestures executed while the pointer is above a bar are not handled by scroll.
	See the respective documentation, e.g. *bindgesture* in *scroll-bar*(5).

	Example:
```
		# Allow switching between workspaces with left and right swipes
		bindgesture swipe:right workspace prev
		bindgesture swipe:left workspace next

		# Allow container movements by pinching them
		bindgesture pinch:inward+up move up
		bindgesture pinch:inward+down move down
		bindgesture pinch:inward+left move left
		bindgesture pinch:inward+right move right

```

*client.background* <color>
	This command is ignored and is only present for i3 compatibility.

*client.<class>* <border> <background> <text> [<indicator> [<child_border>]]
	Configures the color of window borders and title bars. The first three
	colors are required. When omitted _indicator_ will use a sane default and
	_child_border_ will use the color set for _background_. Colors may be
	specified in hex, either as _#RRGGBB_ or _#RRGGBBAA_.

	The available classes are:

	*client.focused*
		The window that has focus.

	*client.focused_inactive*
		The most recently focused window within a container which is not focused.

	*client.focused_tab_title*
		A window that has focused descendant container.
		Tab or stack container title that is the parent of the focused container
		but is not directly focused. Defaults to focused_inactive if not
		specified and does not use the indicator and child_border colors.

	*client.pinned*
		A pinned column/row.

	*client.pinned_focused*
		A pinned column/row that is currently focused.

	*client.selected*
		A selected view/container. See the command *selection*.

	*client.selected_focused*
		A selected view/container that is currently focused. See the command
		*selection*.

	*client.placeholder*
		Ignored (present for i3 compatibility).

	*client.unfocused*
		A window that does not have focus.

	*client.urgent*
		A window with an urgency hint. *Note*: Native Wayland windows do not
		support urgency. Urgency only works for Xwayland windows.

	The meaning of each color is:

	_border_
		The border around the title bar.

	_background_
		The background of the title bar.

	_text_
		The text color of the title bar.

	_indicator_
		The color used to indicate where a new window will open. In a tiled
		container, depending on the mode (_horizontal_ or _vertical_) and the
		insert mode modifier (_before_, _after_, _begin_, _end_), the
		corresponding border will be painted with this color, overriding the
		title border color too if there is one. This only applies to the
		currently focused window.

	_child_border_
		The border around the window itself.

The default colors are:

[- *class*
:[ _border_
:[ _background_
:[ _text_
:[ _indicator_
:[ _child_border_
|[ *background*
:  n/a
:  #ffffff
:  n/a
:  n/a
:  n/a
|  *focused*
:  #4c7899
:  #285577
:  #ffffff
:  #2e9ef4
:  #285577
|  *focused_inactive*
:  #333333
:  #5f676a
:  #ffffff
:  #484e50
:  #5f676a
|  *focused_tab_title*
:  #333333
:  #5f676a
:  #ffffff
:  n/a
:  n/a
|  *unfocused*
:  #333333
:  #222222
:  #888888
:  #292d2e
:  #222222
|  *urgent*
:  #2f343a
:  #900000
:  #ffffff
:  #900000
:  #900000
|  *pinned*
:  #beeeef
:  #000000
:  #ffffff
:  #efeebe
:  #beeeef
|  *pinned_focused*
:  #6e8eef
:  #000000
:  #ffffff
:  #efeebe
:  #6e8eef
|  *selected*
:  #009000
:  #000000
:  #ffffff
:  #00b000
:  #00b000
|  *selected_focused*
:  #009090
:  #000000
:  #ffffff
:  #00b000
:  #009090
|  *placeholder*
:  #000000
:  #0c0c0c
:  #ffffff
:  #000000
:  #0c0c0c


*default_border* normal|none|pixel [<n>]
	Set default border style for new tiled windows. Config reload won't affect
	existing windows, only newly created ones after the reload.

*default_decoration* [border_radius <n>] [shadow true|false] \
[shadow_dynamic true|false]	[shadow_size <n>] [shadow_blur <n>] \
[shadow_offset x y]	[shadow_color #RRGGBBAA] [dim true|false] \
[dim_color #RRGGBBAA]
	Sets the default values for window decorations.

	_border radius_: Radius of the border, in pixels. Default is 0.
	_shadow_: Turns on/off shadow casting for the window. Default is off.
	_shadow_dynamic_: Turns of/off dynamic shadows. Dynamic shadows make the
		shadow offset variable depending on the location of the window. Default
		is off.
	_shadow_size_: size in pixels of the shadow. Default is 40
	_shadow_blur_: Value of the blur radius in pixelz, where 0 means sharp
		shadows. Default is	30.
	_shadow_offset_: Shadow's offset from the window. Default is 40 40
	_shadow_color_: Color of the shadow. Default is #00000070.
	_dim_: Turns on/off dimming of the content when the window is not in focus.
		Default is off.
	_dim_color_: Color used to dim the content when the window is not in focus.
		Default is #00000040.

*default_floating_border* normal|none|pixel [<n>]
	Set default border style for new floating windows. This only applies to
	windows that are spawned in floating mode, not windows that become floating
	afterwards.

*exec* <shell command>
	Executes _shell command_ with sh.

*exec_always* <shell command>
	Like *exec*, but the shell command will be executed _again_ after *reload*.

*floating_maximum_size* <width> x <height>
	Specifies the maximum size of floating windows. -1 x -1 removes the upper
	limit. The default is 0 x 0, which will use the width and height of the
	entire output layout as the maximums

*floating_minimum_size* <width> x <height>
	Specifies the minimum size of floating windows. The default is 75 x 50.

*floating_modifier* <modifier> [normal|inverse]
	When the _modifier_ key is held down, you may hold left click to move
	windows, and right click to resize them. Setting _modifier_ to _none_
	disables this feature. If _inverse_ is specified, left click is used for
	resizing and right click for moving.

*focus_follows_mouse* yes|no|always|full
	If set to _yes_ (default value), moving your mouse over a window will focus
	that window. If	set to _always_, the window under the cursor will always be
	focused, even after switching between workspaces. If set to _full_,focusing
	will only happen if the tiling window under the cursor is fully in the
	viewport.

*focus_on_window_activation* smart|urgent|focus|none
	This option determines what to do when a client requests window activation.
	If set to _urgent_, the urgent state will be set for that window. If set to
	_focus_, the window will become focused. If set to _smart_, the window will
	become focused only if it is already visible, otherwise the urgent state
	will be set. Default is _focus_. See also *xdg_activation_force* to force
	activation when clients don't implement the full protocol.

*focus_wrapping* yes|no|force|workspace
	This option determines what to do when attempting to focus over the edge
	of a container. If set to _no_, the focused container will retain focus,
	if there are no other containers in the direction. If set to _yes_, focus
	will be wrapped to the opposite edge of the container, if there are no
	other containers in the direction. If set to _force_, focus will be wrapped
	to the opposite edge of the container, even if there are other containers
	in the direction. If set to _workspace_, focus will wrap like in the _yes_
	case and additionally wrap when moving outside of workspaces boundaries.
	Default is _no_.

*font* [pango:]<font>
	Sets font to use for the title bars. To enable support for pango markup,
	preface the font name with _pango:_. For example, _monospace 10_ is the
	default font. To enable support for pango markup, _pango:monospace 10_
	should be used instead. Regardless of whether pango markup is enabled,
	_font_ should be specified as a pango font description. For more
	information on pango font descriptions, see
	https://docs.gtk.org/Pango/type_func.FontDescription.from_string.html#description

*fullscreen_on_request* default|layout
	This command/option controls what scroll does when an application requests
	full screen mode for a container. _default_ is the default value, and
	uses _fullscreen workspace_, which is the best mode for performance. scroll
	only has to display one buffer per output, so this is probably the option
	to choose for applications that require the best performance and are GPU
	bound, like games. _layout_ fulfills the request using _fullscreen layout_,
	creating a full screen window in the tiling layout that scrolls with the
	rest. See the _fullscreen_ command for more details.

*force_display_urgency_hint* <timeout> [ms]
	If an application on another workspace sets an urgency hint, switching to this
	workspace may lead to immediate focus of the application, which also means the
	window decoration color would be immediately reset to *client.focused*. This
	may make it unnecessarily hard to tell which window originally raised the
	event. This option allows one to set a _timeout_ in ms to delay the urgency hint reset.

*titlebar_border_radius* <radius>
	Radii of the top titlebar borders in pixels. Default is 0.

*titlebar_padding* <horizontal> [<vertical>]
	Padding of the text in the titlebar. _horizontal_ value affects horizontal
	padding of the text while _vertical_ value affects vertical padding (space
	above and below text). Padding includes titlebar borders so their value
	should be greater than titlebar_border_thickness. If _vertical_ value is
	not specified it is set to the _horizontal_ value.

*for_window* <criteria> <command>
	Whenever a window that matches _criteria_ appears, run list of commands.
	See *CRITERIA* for more details.

*gaps* inner|outer|horizontal|vertical|top|right|bottom|left <amount>
	Sets default _amount_ pixels of _inner_ or _outer_ gap, where the inner
	affects spacing around each window and outer affects the spacing around each
	workspace. Outer gaps are in addition to inner gaps. To reduce or remove
	outer gaps, outer gaps can be set to a negative value. _outer_ gaps can
	also be specified per side with _top_, _right_, _bottom_, and _left_ or
	per direction with _horizontal_ and _vertical_.

	This affects new workspaces only, and is used when the workspace doesn't
	have its own gaps settings (see: workspace <ws> gaps ...).

*hide_edge_borders* [--i3] none|vertical|horizontal|both|smart|smart_no_gaps
	Hides window borders adjacent to the screen edges. Default is _none_. The
	_--i3_ option enables i3-compatible behavior to hide the title bar on
	tabbed and stacked containers with one child. The _smart_|_smart_no_gaps_
	options are equivalent to setting _smart_borders_ smart|no_gaps and
	_hide_edge_borders_ none.

*input* <input_device> <input-subcommands...>
	For details on input subcommands, see *scroll-input*(5).

	\* may be used in lieu of a specific device name to configure all input
	devices. A list of input device names may be obtained via *scrollmsg -t
	get_inputs*.

*seat* <seat> <seat-subcommands...>
	For details on seat subcommands, see *scroll-input*(5).

*kill* <focused>|unfocused|all
	_focused_ or no argument kills (closes) the currently focused container
	and all of its children.
	_unfocused_ kills (closes) every container in the workspace except the
	currently focused one.
	_all_ kills every container from every workspace, including the scratchpad.

*smart_borders* on|no_gaps|off
	If smart_borders are _on_, borders will only be enabled if the workspace
	has more than one visible child. If smart_borders is set to _no_gaps_,
	borders will only be enabled if the workspace has more than one visible
	child and gaps equal to zero.

*smart_gaps* on|off|toggle|inverse_outer
	If smart_gaps are _on_ gaps will only be enabled if a workspace has more
	than one child. If smart_gaps are _inverse_outer_ outer gaps will only
	be enabled if a workspace has exactly one child.

*mark* --add|--replace [--toggle] <identifier>
	Marks are arbitrary labels that can be used to identify certain windows and
	then jump to them at a later time. Each _identifier_ can only be set on a
	single window at a time since they act as a unique identifier. By default,
	*mark* sets _identifier_ as the only mark on a window. _--add_ will instead
	add _identifier_ to the list of current marks for that window. If _--toggle_
	is specified mark will remove _identifier_ if it is already marked.

*mode* <mode>
	Switches to the specified mode. The default mode is _default_.

*mode* [--pango_markup] <mode> <mode-subcommands...>
	The only valid _mode-subcommands..._ are *bindsym*, *bindcode*,
	*bindswitch*, and *set*. If _--pango_markup_ is given, then _mode_ will be
	interpreted as pango markup.

*mouse_warping* output|container|none
	If _output_ is specified, the mouse will be moved to new outputs as you
	move focus between them. If _container_ is specified, the mouse will be
	moved to the middle of the container on switch. Default is _output_.

*no_focus* <criteria>
	Prevents windows matching <criteria> from being focused automatically when
	they're created. This has no effect on the first window in a workspace.

*output* <output_name> <output-subcommands...>
	For details on output subcommands, see *scroll-output*(5).

	\* may be used in lieu of a specific output name to configure all outputs.
	A list of output names may be obtained via *scrollmsg -t get_outputs*.

*popup_during_fullscreen* smart|ignore|leave_fullscreen
	Determines what to do when a fullscreen window opens a dialog.
	If _smart_ (the default), the dialog will be displayed. If _ignore_, the
	dialog will not be rendered. If _leave_fullscreen_, the window will exit
	fullscreen mode and the dialog will be rendered.

*primary_selection* enabled|disabled
	Enable or disable the primary selection clipboard. May only be configured
	at launch. Default is _enabled_.

*send_shortcut* [modifiers]<mouse button|key>
	It will send the modifiers/key/mouse button combination to the currently
	focused	application.

	Example:
	bindsym --whole-window button9 send_shortcut button2
	will send a middle mouse button click to the application when you press
	the back mouse button.

*set* $<name> <value>
	Sets variable $_name_ to _value_. You can use the new variable in the
	arguments of future commands. When the variable is used, it can be escaped
	with an additional $ (ie $$_name_) to have the replacement happen at run
	time instead of when reading the config. However, it does not always make
	sense for the variable to be replaced at run time since some arguments do
	need to be known at config time.

*show_marks* yes|no
	If *show_marks* is yes, marks will be displayed in the window borders.
	Any mark that starts with an underscore will not be drawn even if
	*show_marks* is yes. The default is _yes_.

*opacity* [set|plus|minus] <value>
	Adjusts the opacity of the window between 0 (completely transparent) and
	1 (completely opaque). If the operation is omitted, _set_ will be used.

*tiling_drag*  enable|disable|toggle
	Sets whether or not tiling containers can be dragged with the mouse. If
	_enabled_ (default), the _floating_mod_ can be used to drag tiling, as well
	as floating, containers. Using the left mouse button on title bars without
	the _floating_mod_ will also allow the container to be dragged. _toggle_
	should not be used in the config file.

*tiling_drag_threshold* <threshold>
	Sets the threshold that must be exceeded for a container to be dragged by
	its titlebar. This has no effect if _floating_mod_ is used or if
	_tiling_drag_ is set to _disable_.  Once the threshold has been exceeded
	once, the drag starts and the cursor can come back inside the threshold
	without stopping the drag.  _threshold_ is multiplied by the scale of the
	output that the cursor on.  The default is 9.

*title_align* left|center|right
	Sets the title alignment. If _right_ is selected and _show_marks_ is set
	to _yes_, the marks will be shown on the _left_ side instead of the
	_right_ side.

*unbindswitch* <switch>:<state>
	Removes a binding for when <switch> changes to <state>.

*unbindgesture* [--exact] [--input-device=<device>] \
<gesture>[:<fingers>][:directions]
	Removes a binding for the specified _gesture_, _fingers_
	and _directions_ combination.

*unbindsym* [--whole-window] [--border] [--exclude-titlebar] [--release] [--locked] \
[--to-code] [--input-device=<device>] <key combo>
	Removes the binding for _key combo_ that was previously bound with the
	given flags.  If _input-device_ is given, only the binding for that
	input device will be unbound.

	*unbindcode* [--whole-window] [--border] [--exclude-titlebar] [--release] \
[--locked] [--input-device=<device>] <code>
	is also available for unbinding with key/button codes instead of key/button names.

*unmark* [<identifier>]
	*unmark* will remove _identifier_ from the list of current marks on a
	window. If _identifier_ is omitted, all marks are removed.

*urgent* enable|disable|allow|deny
	Using _enable_ or _disable_ manually sets or unsets the window's urgent
	state. Using _allow_ or _deny_ controls the window's ability to set itself
	as urgent. By default, windows are allowed to set their own urgency.

*workspace* [--no-auto-back-and-forth] [number] <[num:]name>
	Switches to the specified workspace. The _num:_ portion of the name is
	optional and will be used for ordering. If _num:_ is not given and
	_name_ is a number, then it will be also be used for ordering.

	If the _no-auto-back-and-forth_ option is given, then this command will
	not perform a back-and-forth operation when the workspace is already
	focused and _workspace_auto_back_and_forth_ is enabled.

	If the _number_ keyword is specified and a workspace with the number
	already exists, then the workspace with the number will be used. If a
	workspace with the number does not exist, a new workspace will be created
	with the name _name_.

*workspace* prev|next
	Switches to the next workspace on the current output or on the next output
	if currently on the last workspace.

*workspace* prev_on_output|next_on_output
	Switches to the next workspace on the current output.

*workspace* back_and_forth
	Switches to the previously focused workspace.

*workspace* <name> gaps inner|outer|horizontal|vertical|top|right|bottom|left
<amount>
	Specifies that workspace _name_ should have the given gaps settings when it
	is created.

	This command does not affect existing workspaces. To alter the gaps of an
	existing workspace, use the _gaps_ command.

*workspace* <name> output <outputs...>
	Specifies that workspace _name_ should be shown on the specified _outputs_.
	Multiple outputs can be listed and the first available will be used. If the
	workspace gets placed on an output further down the list and an output that
	is higher on the list becomes available, the workspace will be moved to the
	higher priority output.

	This command does not affect existing workspaces. To move an existing
	workspace, use the _move_ command in combination with the _workspace_
	criteria (non-empty workspaces only) or _workspace_ command (to switch
	to the workspace before moving).

*workspace* <name> exec <command>
	Specifies a command to execute when a workspace with _name_ is created. You
	can use it to open applications automatically when you create workspace
	_name_.

*workspace* swap <name>
	Swaps the workspace with <name> and the currently focused one. They can be
	on different outputs.

*workspace* split [h|v|reset] [<fraction>] [<gap>]
	Splits the current workspace horizontally (_h_) or vertically (_v_), and the
	original workspace retains a _fraction_ (0.1-0.9) of the total size, leaving
	an empty gap of integer _gap_ pixels between the two new workspaces.
	The two workspaces have independent layouts.
	A workspace can only be split once, so there can only be at most two visible
	workspaces per output.
	_reset_ undoes the split, making each of the workspaces use the full output
	again.

*workspace_auto_back_and_forth* yes|no
	When _yes_, repeating a workspace switch command will switch back to the
	prior workspace. For example, if you are currently on workspace 1,
	switch to workspace 2, then invoke the *workspace 2* command again, you
	will be returned to workspace 1. Default is _no_.

*xdg_activation_force* <true|false>
	The default is _false_. When _true_, clients that don't implement the
	xdg-activation protocol correctly will be able to activate focus on other
	windows. See *focus_on_window_activation* to set the state of the activated
	window to _urgent_ or _focused_.

*lua* script
	Loads and executes the lua script. See *LUA* for details about the API and
	some examples.

# CRITERIA

A criteria is a string in the form of, for example:

```
[app_id="some-application" title="[Rr]egex.*"]
```

The string contains one or more (space separated) attribute/value pairs. They
are used by some commands to choose which windows to execute actions on. All
attributes must match for the criteria to match. Criteria is retained across
commands separated by a *,*, but will be reset (and allow for new criteria, if
desired) for commands separated by a *;*.

Criteria may be used with either the *for_window* or *assign* commands to
specify operations to perform on new windows. A criteria may also be used to
perform specific commands (ones that normally act upon one window) on all windows
that match that criteria. For example:

Focus on a window with the mark "IRC":

```
[con_mark="IRC"] focus
```

Kill all windows where the title contains "Emacs":

```
[title="Emacs"] kill
```

Several attributes allow regular expressions. These use Perl-compatible regular
expressions (PCRE2), which are documented in *pcre2pattern*(3) and summarized in
*pcre2syntax*(3). For example, this moves all windows with titles ending in
"scroll" or "Scroll" to workspace 1:

```
[title="[Ss]croll$"] move workspace 1
```

You may like to use scrollmsg -t get_tree for finding the values of these
properties in practice for your applications.

The following attributes may be matched with:

*all*
	Matches all windows.

*app_id*
	Compare value against the app id. Can be a regular expression. If value is
	\_\_focused\_\_, then the app id must be the same as that of the currently
	focused window. _app_id_ are specific to Wayland applications.

*class*
	Compare value against the window class. Can be a regular expression. If
	value is \_\_focused\_\_, then the window class must be the same as that of
	the currently focused window. _class_ are specific to X11 applications and
	require XWayland.

*con_id*
	Compare against the internal container ID, which you can find via IPC. If
	value is \_\_focused\_\_, then the id must be the same as that of the
	currently focused window.

*con_mark*
	Compare against the window marks. Can be a regular expression.

*floating*
	Matches floating windows.

*id*
	Compare value against the X11 window ID. Must be numeric. id is specific to
	X11 applications and requires XWayland.

*instance*
	Compare value against the window instance. Can be a regular expression. If
	value is \_\_focused\_\_, then the window instance must be the same as that
	of the currently focused window. instance is specific to X11 applications and
	requires XWayland.

*pid*
	Compare value against the window's process ID. Must be numeric.

*shell*
	Compare value against the window shell, such as "xdg_shell" or "xwayland".
	Can be a regular expression. If value is \_\_focused\_\_, then the shell
	must be the same as that of the currently focused window.

*tag*
	Compare value against the tag. _tag_ is specific to Wayland applications.

*tiling*
	Matches tiling windows.

*title*
	Compare against the window title. Can be a regular expression. If value is
	\_\_focused\_\_, then the window title must be the same as that of the
	currently focused window.

*urgent*
	Compares the urgent state of the window. Can be _first_, _last_, _latest_,
	_newest_, _oldest_ or _recent_.

*window_role*
	Compare against the window role (WM_WINDOW_ROLE). Can be a regular
	expression. If value is \_\_focused\_\_, then the window role must be the
	same as that of the currently focused window. window_role is specific to X11
	applications and requires XWayland.

*window_type*
	Compare against the window type (\_NET_WM_WINDOW_TYPE). Possible values
	are normal, dialog, utility, toolbar, splash, menu, dropdown_menu,
	popup_menu, tooltip and notification. window_type is specific to X11
	applications and requires XWayland.

*workspace*
	Compare against the workspace name for this window. Can be a regular
	expression. If the value is \_\_focused\_\_, then all the windows on the
	currently focused workspace matches.

*sandbox_engine*
	Compare against the associated sandbox engine. Can be a regular expression.
	If the value is \_\_focused\_\_, then the sandbox engine must be the same as
	that of the currently focused window.

*sandbox_app_id*
	Compare against the app ID provided by the associated sandbox engine. Can be
	a regular expression. If the value is \_\_focused\_\_, then the sandbox app
	ID must be the same as that of the currently focused window.

*sandbox_instance_id*
	Compare against the instance ID provided by the associated sandbox engine.
	Can be a regular expression. If the value is \_\_focused\_\_, then the
	sandbox instance ID must be the same as that of the currently focused
	window.

# LUA

scroll provides a Lua API to enable scripting of the window manager. You can
access properties of the window manager, execute commands or add callbacks to
window events.

You can assign scripts to keyboard bindings, or add them to your configuration
file for execution when it loads.

The module is called *scroll*.

A script should start by receiving its arguments and state, like:

```
local args, state = ...
```

You can call the same script many times with the same or different arguments,
and you can check the arguments passed to the script on each invokation using
the variable _args_ if you store it at startup.

_state_ is another special variable that allows you to store script-local data
preserved between script runs. These data are global to different runs of the
same script (even if using different arguments), but local to that script (not
shared with other scripts).

```
local args, state = ...

for _, arg in ipairs(args) do
  print(arg)
end

local val = scroll.state_get_value(state, "test")

if val == nil then
  scroll.state_set_value(state, "test", 1)
else
  scroll.state_set_value(state, "test", val + 1)
end

print(val)
```

Each time you run the script, it will increment and print the value for "test".

This is the current supported API:

*log(message)*
	Add _message_ to the DEBUG log of scroll.

*state_set_value(state, key, value)*
	Sets _key_ = _value_ in the script's _state_. This value will be preserved
	between script runs.

*state_get_value(state, key)*
	Returns the value stored at _key_ in the script's _state_.

*command(container|workspace|nil, command)*
	Execute a scroll _command_ with _container_ or _workspace_ as its context.
	If the first parameter is _nil_, use the default context, usually the
	focused container or workspace. Returns all the results/errors in an array.

```
scroll.command(nil, "set_size v 0.33333333; move left nomode")
```

*focused_view()*
	Returns the focused view pointer or nil if none

*focused_container()*
	Returns the focused container pointer or nil if none

*focused_workspace()*
	Returns the focused workspace pointer or nil if none

*urgent_view()*
	Returns the current urgent view pointer or nil if none.

*view_mapped(view)*
	Returns _true_ if the _view_ is mapped (exists), otherwise returns _false_.

*view_get_container(view)*
	Returns the container pointer associated to _view_.

*view_get_app_id(view)*
	Returns the _app_id_ string for _view_, or nil if any error happens.

*view_get_title(view)*
	Returns the _title_ string for _view_, or nil if any error happens.

*view_get_pid(view)*
	Returns the _pid_ number for the program running in _view_, or nil if any
	error happens.

*view_get_parent_view(view)*
	Returns the view whose pid is the parent of the application running in
	_view_, or nil if it has no parent view.

*view_get_urgent(view)*
	If the _view_ has the _urgent_ attribute set, return _true_, else _false_.

*view_set_urgent(view, true|false)*
	Sets the _view's_ _urgent_ attribute.

*view_get_shell(view)*
	Returns the _xdg_shell_ string if the application running in _view_ is a
	Wayland application. If it is an X windows application, returns _xwayland_.

*view_get_tag(view)*
	Returns the _tag_ string property for _view_, or nil if any error happens.

*view_close(view)*
	Close/kill _view_.

*container_get_workspace(container)*
	Returns the _container's_ parent workspace pointer, or nil if none.

*container_get_marks(container)*
	Returns an array with all the marks associated to _container_.

*container_get_floating(container)*
	Returns _true_ if the _container_ is floating, _false_ if it is tiled.

*container_get_opacity(container)*
	Returns the numerical value for the _container's_ opacity.

*container_get_sticky(container)*
	Returns _true_ if the container is sticky, otherwise _false_.

*container_get_width_fraction(container)*
	Returns the value for the container's width fraction. This value is used
	to compute the width of the container.

*container_get_height_fraction(container)*
	Returns the value for the container's height fraction. This value is used
	to compute the height of the container.

*container_get_width(container)*
	Returns a floating point value with the _container_'s width in pixels.

*container_get_height(container)*
	Returns a floating point value with the _container_'s height in pixels.

*container_get_fullscreen_mode(container)*
	Returns a string with the _fullscreen_ state for _container_. Values can be
	"none", "workspace" (covers only its workspace extents) or "global" (covers
	all outputs).

*container_get_fullscreen_app_mode(container)*
	Returns a string with the _fullscreen_application_ state for _container_.
	Values can be "disabled" or "enabled". See the
	*fullscreen application* command for details.

*container_get_fullscreen_view_mode(container)*
	Returns a string with the _fullscreen_view_ state for _container_.
	Values can be "disabled" or "enabled". This is the internal state of the
	container. A container can be displayed in full screen mode because an
	application requested it, but this state can still be "disabled", so the
	compositor knows the container should become non-full screen when the
	request ends.

*container_get_fullscreen_layout_mode(container)*
	Returns a string with the _fullscreen_layout_ state for _container_.
	Values can be "disabled" or "enabled". See the
	*fullscreen layout* command for details.

*container_get_pin_mode(container)*
	Returns a string with the _pin_ mode for _container_.
	Values can be "none", "beginning" or "end". See the
	*pin* command for details.

*container_get_parent(container)*
	Returns the container parent of _container_ or nil if it is a top level
	container.

*container_get_children(container)*
	Returns an array with all the children containers of _container_, if any.
	Only top level containers have children. Only bottom level containers have
	views.

*container_get_views(container)*
	Returns an array with all the views inside the _container_.

*container_get_id(container)*
	Returns an integer value with the unique container id, or nil if error.

*workspace_get_name(workspace)*
	Returns a string with the name of the _workspace_, or nil if error.

*workspace_get_tiling(workspace)*
	Returns an array with all the tiling containers inside the _workspace_.

*workspace_get_floating(workspace)*
	Returns an array with all the floating containers inside the _workspace_.

*workspace_get_mode(workspace)*
	Returns a table with all the current mode modifiers for _workspace_. The
	keys and values of that table are:
	
	_mode_: "horizontal"|"vertical"|"none"

	_insert_: "before"|"after"|"beginning"|"end"

	_reorder_: "auto"|"lazy"

	_focus_: true|false

	_center_horizontal_: true|false

	_center_vertical_: true|false

*workspace_set_mode(workspace, modifiers)*
	Sets the mode _modifiers_ for _workspace_. The _modifiers_ table can
	override any number of modifiers. The keys and values that table can include
	are at most:
	
	_mode_: "horizontal"|"vertical"

	_insert_: "before"|"after"|"beginning"|"end"

	_reorder_: "auto"|"lazy"

	_focus_: true|false

	_center_horizontal_: true|false

	_center_vertical_: true|false

*workspace_get_layout_type(workspace)*
	Returns the layout type, _"horizontal"_ or _"vertical"_.

*workspace_set_layout_type(workspace, layout_type)*
	Sets the workspace's layout type to _layout_type_, which can be
	_"horizontal"_ or _"vertical"_.

*workspace_get_width(workspace)*
	Returns an integer number with the _workspace_'s width in pixels.

*workspace_get_height(workspace)*
	Returns an integer number with the _workspace_'s height in pixels.

*workspace_get_pin(workspace)*
	Returns the _workspace's_ pinned container pointer, or nil if none.

*workspace_get_output(workspace)*
	Returns the _workspace's_ output pointer, or nil if none.

*output_get_enabled(output)*
	Returns _true_ if the output (display) is enabled, or _false_ otherwise.

*output_get_name(output)*
	Returns the name of the _output_'s interface. For example 'DP-3'.

*output_get_active_workspace(output)*
	Returns the workspace currently active on _output_.

*output_get_workspaces(output)*
	Returns an array with all the existing workspaces assigned to _output_.

*root_get_outputs()*
	Returns an array with all the outputs (displays).

*scratchpad_get_containers()*
	Returns an array with all the containers in the scratchpad.

*add_callback(event, cb_func, cb_data)*
	Sets a _cb_func_ callback function for _event_, passing _cb_data_ data to
	it.

	_event_ can be "view_map" (application's window creation), "view_unmap"
	(application's window destruction), "view_urgent" (a window gets the
	urgent attribute set), "view_focus" (a window gets focus), "view_float"
	(a window becomes floating or goes back to tiled), or "workspace_create"
	(called when a workspace is created).

	_cb_func_ is a Lua function with two parameters, _view_ (the view triggering
	the event) and _data_, the _cb_data_ value passed when creating the
	callback.

	_cb_data_ can be any Lua variable, including a table with multiple values.

	This function returns an _id_ you need to store if you want to be able to
	remove the callback later.

*remove__callback(id)*
	Removes a callback set earlier using *add_callback*. _id_ is the unique
	identifier returned by *add_callback*.

Examples:

Calling this script from the configuration file, you will get focus on every
window that gets the *urgent* attribute:

```
local function on_urgent(view, _)
  local container = scroll.view_get_container(view)
  local workspace = scroll.container_get_workspace(container)
  scroll.command(nil, "workspace " .. scroll.workspace_get_name(workspace))
  scroll.command(container, "focus")
end

scroll.add_callback("view_urgent", on_urgent, nil)
```

If the current focused window is a neovim instance, when called, this script
resizes neovim's window height to 0.66667, and opens a kitty terminal under
it with height 0.33333. When you close the neovim window, the terminal is
also automatically closed.

```
local id_map
local id_unmap
local data = {}

local on_create = function (cbview, cbdata)
  if scroll.view_get_app_id(cbview) == "kitty" then
    cbdata.view = cbview
    scroll.command(nil, "set_size v 0.33333333; move left nomode")
  end
  scroll.remove_callback(id_map)
end

local on_destroy = function (cbview, cbdata)
  if scroll.view_get_pid(cbview) == cbdata.pid then
    scroll.view_close(cbdata.view)
  end
  scroll.remove_callback(id_unmap)
end

local view = scroll.focused_view()

data.pid = scroll.view_get_pid(view)

id_map = scroll.add_callback("view_map", on_create, data)
id_unmap = scroll.add_callback("view_unmap", on_destroy, data)

if view then
  if string.find(scroll.view_get_title(view), "^nvim") then
    scroll.command(nil, 'set_size v 0.66666667; exec kitty')
  end
end
```

Select and move every tiling kitty terminal from the focused workspace to
workspace number 2.

```
local workspace = scroll.focused_workspace()
local containers = scroll.workspace_get_tiling(workspace)

for _, container in ipairs(containers) do
  local views = scroll.container_get_views(container)
  for _, view in ipairs(views) do
    local app_id = scroll.view_get_app_id(view)
    if app_id == "kitty" then
      local con = scroll.view_get_container(view)
      scroll.command(con, "selection toggle")
    end
  end
end

scroll.command(nil, "workspace number 2; selection move")
```

# SEE ALSO

*scroll*(1) *scroll-input*(5) *scroll-output*(5) *scroll-bar*(5) *scroll-ipc*(7)
*pcre2pattern*(3) *pcre2syntax*(3)
