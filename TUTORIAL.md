# Tutorial

[scroll](https://github.com/dawsers/scroll) is a Wayland compositor forked from
[sway](https://github.com/swaywm/sway). It creates a window layout similar to
[PaperWM](https://github.com/paperwm/PaperWM).

Consult the [README](https://github.com/dawsers/scroll/blob/master/README.md)
for more details beyond this quick tutorial.

This tutorial uses the default key bindings you can find in
`/etc/scroll/config` or in this repo in
[config.in](https://github.com/dawsers/scroll/blob/master/config.in).

## Rows and Columns

*scroll* supports multiple workspaces per monitor. Each workspace is a
*row*: a scrollable set of columns, where each *column* may contain one or
more windows.

Your workspace is not limited by the size of the monitor; windows will have a
standard size and will scroll in and out of the view as they gain or lose focus.
This is an important difference with respect to other tiling window managers,
you always control the size of your windows, and if they are too big and not
in focus, they will just scroll out of view. There are ways to view all
windows at the same time, through overview mode, but more on that later.

There are two working modes that can be changed at any time:

1. *horizontal*: it is the default. For each new window you open, it creates a new
   column containing only that window.
2. *vertical*: a new window will be placed in the current *column*,
   right below the active window.

``` config
    bindsym $mod+bracketleft set_mode h
    bindsym $mod+bracketright set_mode v
```

If your monitor is in landscape mode (the usual one), you will probably work
mostly in *horizontal* mode. If you are using a monitor in portrait mode, *scroll*
will adapt and instead of having a row with columns of windows, you will have
a column with rows of windows. You can configure each monitor independently through
available options.

If you have an ultra-wide display, you can also show two workspaces at the
same time using `workspace split`. It will split the current workspace in two,
and both will be shown at the same time.

[Rows and Columns](https://github.com/user-attachments/assets/853d8117-99a0-4910-baa7-7a99e6acfe1e)


## Window Sizes

There are options to define the default width and height of windows. These
sizes are usually a fraction of the available monitor space so it is easier to
fit an integer number of windows fully on screen at a time. If you use a regular
monitor, you may use fractions like `0.5` or `0.333333` to fit 2 or 3
windows. If you have an ultra-wide monitor, you may decide to use smaller
fractions to fit more windows in the viewport at a time.

It is convenient to define complementary fractions like `0.6666667`, `0.75` etc.,
so for example you can fit a `0.333333` and a `0.666667` window for the full
size of the monitor. Depending on the size and resolution of your monitor you
can decide which fractions are best suited for you, and configure them
through options.

You can toggle different window widths and heights in real-time using
`cycle_size`. It will go through your list of selected fractions defined in the
configuration, and resize the active window accordingly.

``` config
bindsym $mod+minus cycle_size h prev
bindsym $mod+equal cycle_size h next
bindsym $mod+Shift+minus cycle_size v prev
bindsym $mod+Shift+equal cycle_size v next
```

Of course you can also manually define exactly the size of a window with
`resize` or use `set_size` to directly choose a fraction without having to
cycle through them.

``` config
mode "setsizeh" {
    bindsym 1 set_size h 0.125; mode default
    bindsym 2 set_size h 0.1666666667; mode default
    bindsym 3 set_size h 0.25; mode default
    bindsym 4 set_size h 0.333333333; mode default
    bindsym 5 set_size h 0.375; mode default
    bindsym 6 set_size h 0.5; mode default
    bindsym 7 set_size h 0.625; mode default
    bindsym 8 set_size h 0.6666666667; mode default
    bindsym 9 set_size h 0.75; mode default
    bindsym 0 set_size h 0.833333333; mode default
    bindsym minus set_size h 0.875; mode default
    bindsym equal set_size h 1.0; mode default

    # Return to default mode
    #bindsym Return mode "default"
    bindsym Escape mode "default"
}
bindsym $mod+b mode "setsizeh"

mode "setsizev" {
    bindsym 1 set_size v 0.125; mode default
    bindsym 2 set_size v 0.1666666667; mode default
    bindsym 3 set_size v 0.25; mode default
    bindsym 4 set_size v 0.333333333; mode default
    bindsym 5 set_size v 0.375; mode default
    bindsym 6 set_size v 0.5; mode default
    bindsym 7 set_size v 0.625; mode default
    bindsym 8 set_size v 0.6666666667; mode default
    bindsym 9 set_size v 0.75; mode default
    bindsym 0 set_size v 0.833333333; mode default
    bindsym minus set_size v 0.875; mode default
    bindsym equal set_size v 1.0; mode default

    # Return to default mode
    #bindsym Return mode "default"
    bindsym Escape mode "default"
}
bindsym $mod+Shift+b mode "setsizev"

mode "resize" {
    # left will shrink the containers width
    # right will grow the containers width
    # up will shrink the containers height
    # down will grow the containers height
    bindsym $left resize shrink width 100px
    bindsym $down resize grow height 100px
    bindsym $up resize shrink height 100px
    bindsym $right resize grow width 100px

    # Return to default mode
    #bindsym Return mode "default"
    bindsym Escape mode "default"
}
bindsym $mod+Shift+r mode "resize"

```

If you want to quickly fit a series of windows in a column or columns in a
row, you can use `fit_size`. It will ignore fractions, and re-scale all the
affected windows to fit in the screen.

[cycle_size and fit_view](https://github.com/user-attachments/assets/3a034369-5246-4868-a696-bfeb044c7c0d)

``` config
mode "fit_size" {
    bindsym w fit_size h visible proportional; mode default
    bindsym Shift+w fit_size v visible proportional; mode default
    bindsym Ctrl+w fit_size h visible equal; mode default
    bindsym Ctrl+Shift+w fit_size v visible equal; mode default

    bindsym $right fit_size h toend proportional; mode default
    bindsym Shift+$right fit_size v toend proportional; mode default
    bindsym Ctrl+$right fit_size h toend equal; mode default
    bindsym Ctrl+Shift+$right fit_size v toend equal; mode default

    bindsym $left fit_size h tobeg proportional; mode default
    bindsym Shift+$left fit_size v tobeg proportional; mode default
    bindsym Ctrl+$left fit_size h tobeg equal; mode default
    bindsym Ctrl+Shift+$left fit_size v tobeg equal; mode default

    bindsym $up fit_size h active proportional; mode default
    bindsym Shift+$up fit_size v active proportional; mode default
    #bindsym Ctrl+$up fit_size h active equal; mode default
    #bindsym Ctrl+Shift+$up fit_size v active equal; mode default

    bindsym $down fit_size h all proportional; mode default
    bindsym Shift+$down fit_size v all proportional; mode default
    bindsym Ctrl+$down fit_size h all equal; mode default
    bindsym Ctrl+Shift+$down fit_size v all equal; mode default
}
bindsym $mod+w mode "fit_size"
```


## Focusing and Alignment

When you change window focus, *scroll* will always show the active
(focused) window on screen, and intelligently try to fit another adjacent
window if it can. However, in some cases you may want to decide to align a
window manually; `align` does exactly that.

[align](https://github.com/user-attachments/assets/974f0916-0a0e-453a-bdb2-09e2674c9a7a)

``` config
mode "align" {
    bindsym c align center; mode default
    bindsym m align middle; mode default
    bindsym r align reset; mode default
    bindsym $left align left; mode default
    bindsym $right align right; mode default
    bindsym $up align up; mode default
    bindsym $down align down; mode default
    bindsym Escape mode "default"
}
bindsym $mod+c mode "align"
```


## Moving Columns/Windows Around.

There are several ways to move windows and columns:

``` config
    bindsym $mod+Ctrl+$left move left
    bindsym $mod+Ctrl+$down move down
    bindsym $mod+Ctrl+$up move up
    bindsym $mod+Ctrl+$right move right
    bindsym $mod+Ctrl+home move beginning
    bindsym $mod+Ctrl+end move end
    # nomode
    bindsym $mod+Alt+$left move left nomode
    bindsym $mod+Alt+$down move down nomode
    bindsym $mod+Alt+$up move up nomode
    bindsym $mod+Alt+$right move right nomode
    bindsym $mod+Alt+home move beginning nomode
    bindsym $mod+Alt+end move end nomode
```

When using `mod+Ctrl+cursor_keys` you move a window, a row or a column
depending on which mode you are on. Instead, `mod+Alt+cursor_keys` uses
`nomode`, which bypasses the mode, and moves individual windows, extracting
them from the row/column or inserting them when needed.

[Movement](https://github.com/user-attachments/assets/c2ede07a-f94a-4f88-be66-8da469a65d0c)

To be able to re-organize windows and columns more easily, you can use
`selection toggle` to select and deselect windows. Those windows can be in
any workspace, and can also be selected from overview mode. After you are
done with your selection, go to the place where you want to move them (same
or different workspace), and call `selection move`. All the selected
windows/containers will move to your current location. If you want to select
a full workspace, use `selection workspace`, or `selection reset` to undo a
multiple selection. You can control more precisely where the selection will
end up by changing mode modifiers (after, before, end,...)

``` config
    # Selection
    bindsym --no-repeat $mod+Insert selection toggle
    bindsym --no-repeat $mod+Ctrl+Insert selection reset
    bindsym --no-repeat $mod+Shift+Insert selection move
    bindsym --no-repeat $mod+Ctrl+Shift+Insert selection workspace
    bindsym --no-repeat $mod+Alt+Insert selection to_trail
```


## Overview Mode

When you have too many windows in a workspace, it can be hard to know where
things are. `scale_workspace overview` helps with that by creating a bird's eye view
of the whole workspace.

``` config
    bindsym --no-repeat $mod+tab scale_workspace overview
```

[Overview](https://github.com/user-attachments/assets/618fa129-f3db-4970-8dff-4d2ed7ed5ae2)


## Jump Mode

Jump mode takes advantage of overview mode to provide a very quick way to focus on
any window or workspace on your monitors. Similar to [vim-easymotion](https://github.com/easymotion/vim-easymotion),
it overlays a label on each window (or workspace if you use `jump workspaces`.
Pressing all the numbers of a label, changes focus to the selected window or
workspace.

1. Assign key bindings to `jump`, for this tutorial:

``` config
    bindsym --no-repeat $mod+slash jump
    bindsym --no-repeat $mod+Shift+slash jump container
    bindsym --no-repeat $mod+Ctrl+slash jump workspaces
    bindsym --no-repeat $mod+Alt+slash jump floating
```

2. Pressing your `mod` key and `/` will show an overview of your windows on
   each monitor.
3. Now press the numbers shown on the overlay for the window you want to change
   focus to, or click on it.
4. `jump` will exit, and the focus will be on the window you selected.

If instead you press `mod`, `Ctrl` and `/`, you will see a bird's eye view of
all your workspaces. Pressing a combination of keys, like in the example
above, will take you to the chosen workspace.

If your focus is on a column with more than one window, you can press `mod`,
`Shift` and `/`, and you will get a jump overview of only all the windows in that
column instead.

If you use floating windows, `mod+Alt+/` will show you an overview of
all your floating windows without overlaps, so you can select one even if it
was hidden behind.

[Jump](https://github.com/user-attachments/assets/33bec595-148f-4449-bcd6-e1cc9e5b9a1a)


## Spaces

A space is a configuration of existing windows. You can set your workspace and
save a named "space" with that configuration. You can later on recall that
configuration on the same or any other workspace. scroll will gather the
windows from their location and set them up as they were when you saved the
space.

``` config
mode "spaces" {
    bindsym 1 space load 1; mode default
    bindsym 2 space load 2; mode default
    bindsym 3 space load 3; mode default
    bindsym 4 space load 4; mode default
    bindsym 5 space load 5; mode default
    bindsym 6 space load 6; mode default
    bindsym 7 space load 7; mode default
    bindsym 8 space load 8; mode default
    bindsym 9 space load 9; mode default
    bindsym 0 space load 0; mode default
    bindsym Shift+1 space save 1; mode default
    bindsym Shift+2 space save 2; mode default
    bindsym Shift+3 space save 3; mode default
    bindsym Shift+4 space save 4; mode default
    bindsym Shift+5 space save 5; mode default
    bindsym Shift+6 space save 6; mode default
    bindsym Shift+7 space save 7; mode default
    bindsym Shift+8 space save 8; mode default
    bindsym Shift+9 space save 9; mode default
    bindsym Shift+0 space save 0; mode default
    bindsym Ctrl+1 space restore 1; mode default
    bindsym Ctrl+2 space restore 2; mode default
    bindsym Ctrl+3 space restore 3; mode default
    bindsym Ctrl+4 space restore 4; mode default
    bindsym Ctrl+5 space restore 5; mode default
    bindsym Ctrl+6 space restore 6; mode default
    bindsym Ctrl+7 space restore 7; mode default
    bindsym Ctrl+8 space restore 8; mode default
    bindsym Ctrl+9 space restore 9; mode default
    bindsym Ctrl+0 space restore 0; mode default
    bindsym Escape mode "default"
}
bindsym $mod+g mode "spaces"
```

[Spaces](https://github.com/user-attachments/assets/32a076db-5cf1-49d1-8132-3822a137d231)


## Scratchpad

*scroll* adds some functionality to *sway*'s scratchpad. By using `scratchpad jump`
you will see an overview similar to `jump floating` for your scratchpad windows.
You can use this to quickly select one of them instead of having to cycle.

``` config
    # Move the currently focused window to the scratchpad
    bindsym $mod+Shift+z move scratchpad

    # Show the next scratchpad window or hide the focused scratchpad window.
    # If there are multiple scratchpad windows, this command cycles through them.
    bindsym $mod+z scratchpad show
    # Show all the scratchpad windows to quickly change focus to one of them
    bindsym --no-repeat $mod+Alt+z scratchpad jump
```


## Content Scaling

You can set the scale of the content of any window independently. Add these
bindings and pressing Mod and moving the scroll wheel of you mouse will scale
the content of the active window.

``` config
bindsym --whole-window $mod+button4 scale_content incr -0.05
bindsym --whole-window $mod+button5 scale_content incr 0.05
```

## Workspace Scaling

Apart from overview and jump, you can scale your workspace to an arbitrary
scale value. Add these bindings to your configuration, and pressing Mod +
Shift and moving the scroll wheel of your mouse will scale the workspace.

``` config
bindsym --whole-window $mod+Shift+button4 scale_workspace incr -0.05
bindsym --whole-window $mod+Shift+button5 scale_workspace incr 0.05
```

[scale](https://github.com/user-attachments/assets/a222e9f9-1d60-4c39-aaee-ca68aeabd1ea)


## Pins

Sometimes you want to keep one window static at all times, for example an
editor with a file you are working on, while letting other windows scroll
in the rest of the available space. These windows may include terminals or
documentation browser windows. You can pin a column to the right or left
(top or bottom) of the monitor, and it will stay at that location until you
call pin again.

``` config
    # Toggle pin
    bindsym --no-repeat $mod+a pin beginning
    bindsym --no-repeat $mod+Shift+a pin end
```

[Pin](https://github.com/user-attachments/assets/5d0ea318-832e-441c-be16-e0e6af278fc4)


## Trails and Trailmarks

Aside from sway's marks, *scroll* supports a special kind of anonymous mark
called a trailmark. You can create trails and trailmarks and use them to
navigate your favorite windows. See the README and the man page for more
details.

``` config
mode "trailmark" {
    bindsym bracketright trailmark next
    bindsym bracketleft trailmark prev
    bindsym semicolon trailmark toggle; mode default
    bindsym --no-repeat slash trailmark jump; mode default
    bindsym Escape mode "default"
}
bindsym $mod+semicolon mode "trailmark"

mode "trail" {
    bindsym bracketright trail next
    bindsym bracketleft trail prev
    bindsym semicolon trail new; mode default
    bindsym d trail delete; mode default
    bindsym c trail clear; mode default
    bindsym insert trail to_selection; mode default
    bindsym Escape mode "default"
}
bindsym $mod+Shift+semicolon mode "trail"
```

You can create selections from trails, and a new trail for the current
selection. This also provides memory for selections, and a way to quickly
navigate favorite windows by using `trailmark jump`.

[Trailmarks](https://github.com/user-attachments/assets/abb75af4-9208-48c3-8c72-45de8db4b374)


## Working in Full Screen Mode

Overview and jump can be very helpful when you work in fullscreen mode. You
can keep all your windows maximized or in full screen mode, and toggle
overview on/off to see your full desktop, and quickly focus or change the
position to a different application.

[Full Screen](https://github.com/user-attachments/assets/fed977a0-8e87-4f94-bd68-7159578499ba)

You can also use `fullscreen application` to toggle a full screen UI for any
application, while still having its content fit in the assigned container.

With different combinations of `fullscreen` and `fullscreen application` you
can have several "fake" full screen modes, like full screen YouTube videos
within a container, full screen UI within a container, or a regular UI in a
full screen container.

`fullscreen layout` applies a special full screen mode to the active tiled
window. The window becomes full screen but it is still part of the tiling
layout, so you can scroll it like the rest. You can have as many of these
full screen windows as you want at the same time.

*scroll* also supports a *maximize* mode, through the `toggle_size` command.
This command lets you toggle back and forth any size for the *active* window in
the current workspace, or *all* of them. See the manual for details.

``` config
    bindsym $mod+f fullscreen
    bindsym $mod+Ctrl+f fullscreen layout
    bindsym $mod+Alt+f fullscreen application
    bindsym $mod+Ctrl+Alt+f fullscreen global

    # Toggle Size (and maximize)
    bindsym $mod+t toggle_size this 1.0 1.0
    bindsym $mod+Shift+t toggle_size active 1.0 1.0

mode "togglesizeh" {
    bindsym 1 toggle_size all 0.125 1.0; mode default
    bindsym 2 toggle_size all 0.1666666667 1.0; mode default
    bindsym 3 toggle_size all 0.25 1.0; mode default
    bindsym 4 toggle_size all 0.333333333 1.0; mode default
    bindsym 5 toggle_size all 0.375 1.0; mode default
    bindsym 6 toggle_size all 0.5 1.0; mode default
    bindsym 7 toggle_size all 0.625 1.0; mode default
    bindsym 8 toggle_size all 0.6666666667 1.0; mode default
    bindsym 9 toggle_size all 0.75 1.0; mode default
    bindsym 0 toggle_size all 0.833333333 1.0; mode default
    bindsym minus toggle_size all 0.875 1.0; mode default
    bindsym equal toggle_size all 1.0 1.0; mode default
    bindsym r toggle_size reset; mode default

    # Return to default mode
    bindsym Escape mode "default"
}
bindsym $mod+Ctrl+t mode "togglesizeh"

mode "togglesizev" {
    bindsym 1 toggle_size all 1.0 0.125; mode default
    bindsym 2 toggle_size all 1.0 0.1666666667; mode default
    bindsym 3 toggle_size all 1.0 0.25; mode default
    bindsym 4 toggle_size all 1.0 0.333333333; mode default
    bindsym 5 toggle_size all 1.0 0.375; mode default
    bindsym 6 toggle_size all 1.0 0.5; mode default
    bindsym 7 toggle_size all 1.0 0.625; mode default
    bindsym 8 toggle_size all 1.0 0.6666666667; mode default
    bindsym 9 toggle_size all 1.0 0.75; mode default
    bindsym 0 toggle_size all 1.0 0.833333333; mode default
    bindsym minus toggle_size all 1.0 0.875; mode default
    bindsym equal toggle_size all 1.0 1.0; mode default
    bindsym r toggle_size reset; mode default

    # Return to default mode
    bindsym Escape mode "default"
}
bindsym $mod+Ctrl+Shift+t mode "togglesizev"
```

## Touchpad Gestures

By default, *scroll* supports three and four finger touchpad swipe
gestures to scroll windows, call *overview* and switch workspaces.

You can also scroll windows with the mouse. Press `mod` and the middle mouse
button, and you can drag/scroll columns and windows.


## Lua API

scroll is programmable through an exposed Lua API. You can write scripts that
access the internals of the window manager, listen to events and execute
commands. Follow this [link](https://github.com/dawsers/scroll/issues/35) for
examples and requests.

``` lua
local function candidate(view)
  local app_id = scroll.view_get_app_id(view)
  if app_id == "mpv" then
    local pview = scroll.view_get_parent_view(view)
    if pview ~= nil and pview ~= view then
      local papp_id = scroll.view_get_app_id(pview)
      if papp_id == "kitty" then
        return scroll.view_get_container(pview)
      end
    end
  end
  return nil
end

local function on_create(view, _)
  local parent = candidate(view)
  if parent ~= nil then
    scroll.command(parent, "move scratchpad")
  end
end

local function on_destroy(view, _)
  local parent = candidate(view)
  if parent ~= nil then
    scroll.command(nil, "scratchpad show; floating toggle")
  end
end

scroll.add_callback("view_map", on_create, nil)
scroll.add_callback("view_unmap", on_destroy, nil)
```

## Example Configuration

*scroll* includes an example [configuration](https://github.com/dawsers/scroll/blob/master/config.in)
with most of the key bindings recommended for a standard setup.
