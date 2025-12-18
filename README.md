# scroll

<img width="256" height="256" src="https://github.com/dawsers/scroll/blob/master/scroll.png" />

[scroll](https://github.com/dawsers/scroll) is a Wayland compositor forked
from [sway](https://github.com/swaywm/sway). The main difference is *scroll*
only supports one layout, a scrolling layout similar to
[PaperWM](https://github.com/paperwm/PaperWM), [niri](https://github.com/YaLTeR/niri)
or [hyprscroller](https://github.com/dawsers/hyprscroller).

[video.mp4](https://github.com/user-attachments/assets/ef244c68-8376-4ba0-a793-198a5a697922)

*scroll* works very similarly to *hyprscroller*, and it is also mostly
compatible with *sway* configurations aside from the window layout. It
supports some added features:

* Animations: *scroll* supports very customizable animations.

* *scroll* supports rounded borders and title bars, dimming of inactive
  windows, and dynamic shadows with blur.

* Content scaling: The content of individual windows can be scaled
  independently of the general output scale.

* Overview and Jump modes: You can see an overview of the desktop and work
  with the windows at that scale. Jump allows you to move to any window with
  just some key presses, like easymotion in some editors. There is also a jump
  mode to preview and switch to any available workspace.

* Workspace scaling: Apart from overview, you can scale the workspace to any
  scale, and continue working.

* Lua scripting: scroll provides a lua API to script the window manager.

* Several full screen modes: `workspace`, `global`, `application` and `layout`.

* Trackpad/Mouse scrolling: You can use the trackpad or mouse dragging to
  navigate/scroll the workspace windows.

* Portrait and Landscape monitor support: The layout works and adapts to both
  portrait or landscape monitors. You can define the layout orientation per
  output (monitor).

* For ultra-wide displays, you can split a workspace in two and show them both
  at the same time (`workspace split` command).

For more videos explaining the different features, check the
[TUTORIAL](https://github.com/dawsers/scroll/blob/master/TUTORIAL.md).

## Documentation

This README explains the basic differences between *sway/i3* and *scroll*. For
people unfamiliar with *i3* or *sway*, it is advised to read their
documentation, as compatibility is very high.

[Documentation for i3](https://i3wm.org/docs/userguide.html)

[Documentation for sway](https://github.com/swaywm/sway/wiki)

[scroll TUTORIAL](https://github.com/dawsers/scroll/blob/master/TUTORIAL.md)

[Example configuration](https://github.com/dawsers/scroll/blob/master/config.in)

*scroll's* man pages are the best source of documentation for details on all
the commands. They are up to date. Read `man 5 scroll` for reference on any
command or the Lua API. If you haven't installed scroll yet, you can find the
source for the manual
[here](https://github.com/dawsers/scroll/blob/master/sway/scroll.5.scd).

``` bash
man 5 scroll
man 1 scroll
man 1 scrollmsg
man 7 scroll-ipc
man scroll-output
man scroll-bar
man scrollnag
```

The [example configuration](https://github.com/dawsers/scroll/blob/master/config.in)
includes key bindings for most commands, so it is a good source of documentation
to see what *scroll* can do. If you have time, read it carefully with the manual
side by side (`man 5 scroll`), and experiment while customizing it.

## Building and Installing

*scroll* is a stable fork of *sway*; the build tree is basically the
same and the executables are renamed to *scroll*, "scrollmsg", "scrollnag" and
"scrollbar".

*scroll* uses a modified version of [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots)
which is included in the source tree and linked statically. So if you want
to build *scroll*, you will need to install its dependencies plus wlroots's
dependencies.

### Arch Linux

If you are using Arch Linux, there are two AUR package you can install:

* Stable version: `sway-scroll`. Currently at 1.12. It corresponds to the
  development version of sway. You should be able to have any version of `sway`
  and `scroll` installed on the same system and start any of them without problems,
  as `scroll` doesn't need any version of `wlroots` installed on the system,
  and its executable and file names are different (*scroll* vs *sway*).

``` sh
paru -S sway-scroll
```

* Unstable, development version: `sway-scroll-git`. This has all the newest
  changes and features.

``` sh
paru -S sway-scroll-git
```

**NOTE**: The package `sway-scroll-stable` has been discontinued and renamed
`sway-scroll`. Please uninstall it and install `sway-scroll` if you want the
stable version.

### Fedora Linux

Thanks to @mecattaf, [scroll stable is also available on the Fedora Copr](https://copr.fedorainfracloud.org/coprs/scrollwm/packages/).

### NixOS

Thanks to @AsahiRocks for creating a nix flake of scroll stable in his repository,
[here](https://github.com/AsahiRocks/scroll-flake)

### Artix Linux and Arch-derived Distributions not Using `systemd`

Artix uses `elogind` instead of `systemd`'s login daemon to provide some
services. That means the AUR packages for *scroll* won't work out of the box.
Artix doesn't have a package for *scroll* in their repository, so if you want
to install it, you can either do it manually, or modify the AUR package as
explained [here](https://github.com/dawsers/scroll/issues/164).

``` bash
git clone https://aur.archlinux.org/sway-scroll.git
cd sway-scroll
```

Apply this patch:

``` diff
--- a/PKGBUILD
+++ b/PKGBUILD
@@ -19,7 +19,7 @@
 	"libliftoff"
 	"libglvnd"
 	"lcms2"
-	"systemd-libs"
+	"elogind"
 	"opengl-driver"
 	"xcb-util-errors"
 	"xcb-util-renderutil"
@@ -62,7 +62,7 @@

 build() {
   mkdir -p build
-  arch-meson build scroll -D sd-bus-provider=libsystemd -D werror=false -D b_ndebug=true
+  arch-meson build scroll -D sd-bus-provider=libelogind -D werror=false -D b_ndebug=true
   ninja -C build
 }
```

And make and install the package.

``` bash
makepkg -si
```

### Void Linux

Thanks to @brihadeesh for setting up a Void linux template for the stable version of
scroll on his repo, [here](https://git.sr.ht/~peregrinator/scroll-void-template).

### Post-Installation

After installing either package, prepare a configuration file
`~/.config/scroll/config` using the provided example (`/etc/scroll/config`),
and you can start *scroll* from a tty. You can also start *scroll* from your
display manager using the provided `/usr/share/wayland-sessions/scroll.desktop`.

### Building Requirements

If you want to compile *scroll* yourself, [sway compiling instructions](https://github.com/swaywm/sway#compiling-from-source)
apply to *scroll*. You will also need to install the lua package (version >= 5.4)
to enable lua scripting. `wlroots` is no longer a requirement, as the
source is included in the scroll source tree, but you will need its
dependencies to be able to build *scroll*.

``` sh
meson setup build/
ninja -C build/
sudo ninja -C build/ install
```

## Useful Tips after Installation

Aside from the dependencies listed in the package for your distribution,
*scroll* works best with some additional software to make your life easier,
and some extra system configuration for the best possible experience.

### XDG Portals

Wayland compositors use *portals* for added functionality. It is recommended
to install `xdg-desktop-portal`, `xdg-desktop-portal-gtk` and
`xdg-desktop-portal-wlr` to use with *scroll*. The last one adds specific
functionality for screencasting and video recording. You should also install
`pipewire` if you plan on recording desktop videos or do screencasting.

*scroll*'s AUR package by default installs the file
`/usr/share/xdg-desktop-portal/scroll-portals.conf` which configures what
portal to use for each situation. If you install *scroll* manually, or your
distribution package doesn't include that file, you can create it either
in the location mentioned above, or in
`~/.config/xdg-desktop-portal/scroll-portals.conf`, with the following content:

```
[preferred]
default=gtk
org.freedesktop.impl.portal.ScreenCast=wlr
org.freedesktop.impl.portal.Screenshot=wlr
org.freedesktop.impl.portal.Inhibit=none
```

### Environment Variables

Some application frameworks use special environment variables to decide
whether to run in Wayland or X11 mode. Even though *scroll* is compatible with
X11 applications out of the box, running Wayland applications should always be
preferred; they will provide a better experience. Try to find Wayland
alternatives to your favorite X11 applications, and if you have doubts, use
the [Discussions board](https://github.com/dawsers/scroll/discussions) to
ask for good alternatives. Any application that works well with *sway* will
also work with *scroll*.

*scroll* doesn't let you define environment variables in its configuration
file, so you will need to set them before launching *scroll*, for example in
your `~/.bash_profile` or the one your shell uses.

``` sh
# Sway/Scroll needs its environment variables here
# Set GTK theme
export GTK_THEME=Adwaita-dark
# Tell QT, GDK and others to use the Wayland backend by default, X11 if not available
export QT_QPA_PLATFORM="wayland;xcb"
export GDK_BACKEND="wayland,x11"
export SDL_VIDEODRIVER=wayland
export CLUTTER_BACKEND=wayland

# XDG desktop variables to set scroll as the desktop
export XDG_CURRENT_DESKTOP=scroll
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=scroll

# Configure Electron to use Wayland instead of X11
export ELECTRON_OZONE_PLATFORM_HINT=wayland

export QT_WAYLAND_DISABLE_WINDOWDECORATION=1 # Disables window decorations on Qt applications
export QT_QPA_PLATFORMTHEME=qt6ct
# This is to (temporarily) fix font rendering on QWebEngineView 6
# (qutebrowser, goldendict etc.)
# https://bugreports.qt.io/browse/QTBUG-113574
export QT_SCALE_FACTOR_ROUNDING_POLICY=RoundPreferFloor

# If you use a Nvidia card
# NVIDIA environment variables
export LIBVA_DRIVER_NAME=nvidia
export GBM_BACKEND=nvidia-drm
export __GLX_VENDOR_LIBRARY_NAME=nvidia

export XCURSOR_THEME=Adwaita
export XCURSOR_SIZE=24
```

### Starting from a tty

*scroll* can be started from your session manager, the distribution provides a
`scroll.desktop` file. But if you want to skip the installation of a session
manager, you can start *scroll* directly from a TTY.

Modifying your `~/.bash_profile` like the following will let you start
*scroll* every time you login from tty1, without the need for a login manager.

``` sh

[[ -f ~/.bashrc ]] && . ~/.bashrc

# If you want to use the vulkan renderer instead of the gles2 one (more
# features, experimental, better performance) uncomment this line
# export WLR_RENDERER=vulkan

# Start scroll directly if you login from tty1
if [ "$(tty)" = "/dev/tty1" ];then
    exec scroll
fi

```

### Starting From a Display Manager

*scroll* includes `/usr/share/wayland-sessions/scroll.desktop`, so when you
install it, *scroll* should be included in the sessions menu of your display
manager of choice.

### Choosing a Desktop Bar

Most bars that support *sway* will work using their configuration for *sway*,
for example [DankMaterialShell](https://github.com/AvengeMedia/DankMaterialShell)
or [Waybar](https://github.com/Alexays/Waybar). Configure your bar as if you
were goingto use *sway*, and it will work.

Bars for *sway* that are not aware of *scroll* will work, but will be missing
features *scroll* provides, like showing the current *mode modifiers*, trails
or spaces.

I wrote a bar, [gtkshell](https://github.com/dawsers/gtkshell) which shows
a special *scroll* module that could be ported to other bars, and is aware
of *scroll* specific features. See `man 7 scroll-ipc`.

The very basic included `scrollbar` is also *scroll*-aware and shows mode
modifiers, trail information etc.


## Configuration

*scroll* is very compatible with *i3* and *sway*, but because it uses a
scrolling layout, to use its extra features you will need to spend some time
configuring key bindings and options. *scroll* includes an example configuration
file that will usually be installed in `/etc/scroll/config`. Copy it to
`~/.config/scroll/config` and make your changes.

The configuration file may include other files by using the `include <file>`
command. You can use this to split the configuration in more manageable pieces,
or when you have several systems that share some parts of the configuration,
but not all of it.

Another useful feature are *variables*. You can set variables that apply to
the configuration, and use them in any command. For example:

```
# Logo key. Use Mod1 for Alt.
set $mod Mod4

# Home row direction keys, like vim
# set $left h
# set $down j
# set $up k
# set $right l
# Arrow keys like Emacs
set $left Left
set $down Down
set $up Up
set $right Right

bindsym $mod+$left focus left
bindsym $mod+$down focus down
bindsym $mod+$up focus up
bindsym $mod+$right focus right
bindsym $mod+home focus beginning
bindsym $mod+end focus end
```

### Configuring Your Monitors

You will need to configure your monitors (*outputs*) first. Run `scrollmsg -t
get_outputs` to get a list of output names. You will use the connector name
to identify each output. Each name will correspond to a block in your
configuration file where you will set the output's properties.

For example:

```
output HDMI-A-1 {
    background #202020 solid_color
    scale 1.25
    position 0 0
}
output DP-2 {
    background /usr/share/backgrounds/scroll/Sway_Wallpaper_Blue_1920x1080.png fill
    scale 1.25
    # hdr on
}
```

Monitors will auto-arrange, so you only need to set the `position` parameter
if the auto-arrangement doesn't do what you want.

You can also set the default layout options per monitor. For example if one of
your monitors is in portrait mode and you prefer a vertical layout type for
it, set `layout_type vertical` (the default is horizontal).

For all the details about how to configure a monitor and the available options,
read `man 5 scroll-output`

#### HDR

*scroll* supports HDR like the latest version of *sway*.

*scroll* can use two different renderers: gles2 (default) and vulkan. The
vulkan renderer is experimental, but has better performance and supports more
features, like advanced color profiles and **HDR**.

So, to enable HDR and advanced color profiles, you will have to use the vulkan
renderer. Simply set `export WLR_RENDERER=vulkan` in your `~/.bash_profile` to
choose the vulkan renderer over the default gles2 one, and then set `hdr on`
in your monitor configuration as explained in `man 5 scroll-output`


### Configuring Your Keyboard

By default, *scroll* will configure your keyboard and mouse to make them
usable, but if you have some special needs (like specific or additional layouts),
you should spend some time reading `man 5 scroll-input`

For example, to simply configure a keyboard to have two layouts available, you
can do it like this:

```
input type:keyboard {
    xkb_layout "us,us"
    xkb_variant ",intl"
    xkb_numlock enabled
}
```

It will set two layouts for the default keyboard, `us` and `us-international`,
and you can change from one to the other using `$mod+Space` like this:

```
    bindsym $mod+Space input type:keyboard xkb_switch_layout next
```

### Starting Some Applications

After configuring the keyboard, you should start some applications, for
example:

```
### Exec startup apps

# Execute your favorite apps at launch
# It is convenient to start the DBUS environment here if you login from a tty
exec dbus-update-activation-environment --systemd WAYLAND_DISPLAY XDG_CURRENT_DESKTOP
# Start your favorite desktop bar
exec waybar
# Start the gnome authentication daemon to be able to run sudo apps
exec /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1
```

### Window Defaults

Here, you will configure the look of windows.

```
### Windows defaults
# Use a default window without title bars, only a 2 pixel border
default_border pixel 2
# windows with a title bar will have it with rounded corners or radius 8px
titlebar_border_radius 8
# the default decoration for windows has rounded borders of 12px radius
default_decoration border_radius 12
# These are all the options you can tweak for the default decoration
# default_decoration border_radius 12 shadow true shadow_dynamic true shadow_size 40 shadow_blur 30 shadow_offset 40 40 shadow_color #00000070 dim false dim_color #00000040

# Use an inner gap of 4 and an outer gap of 20
gaps inner 4
gaps outer 20

# Colors for focused and unfocused windows, including their title bars if any
client.focused #15439e #4b4b4b #e0e0e0 #2e9ef4 #15439e
client.focused_inactive #595959 #3b3b3b #e0e0e0 #2e9ef4 #595959
client.unfocused #595959 #1b1b1b #e0e0e0 #2e9ef4 #595959
```

### Layout Settings

*scroll* only supports one type of tiled layout, a scrolling layout. In this
section we will set the default values for this layout. These values can be
changed per monitor too.

`layout_widths` and `layout_heights` affect your preferred fractions, those
you select with `cycle_size`, but you can still use any fraction using other
commands like `set_size` or `resize`.

```
# Layout settings
layout_default_width 0.5
layout_default_height 1.0
layout_widths [0.33333333 0.5 0.666666667 1.0]
layout_heights [0.33333333 0.5 0.666666667 1.0]
```

### Setting Some Options

In this section we will configure some of the most common options for
*scroll*. They are explained [here](#options-specific-to-scroll)

```
# These are the default values for some useful scroll options, consult
# `man 5 scroll` and change them to your preference

# align_reset_auto yes
# cursor_shake_magnify false
# cycle_size_wrap false
# fullscreen_movefocus true nofollow
# fullscreen_on_request default
# maximize_if_single false
# workspace_next_on_output_create_empty true
# xwayland_output_scale true
# focus_wrapping no
# focus_on_window_activation focus
# xdg_activation_force false
```

### configuring Animations

The next section defines which animations will be used by *scroll*. For more
details about animations, check the manual or read [this](#animation-options)

```
# Animations
animations {
    enabled yes
    default yes 300 var 3 [ 0.215 0.61 0.355 1 ]
    window_open yes 300 var 3 [ 0 0 1 1 ]
    window_move yes 300 var 3 [ 0.215 0.61 0.355 1 ] off 0.05 6 [0 0.6 0.4 0 1 0 0.4 -0.6 1 -0.6]
    window_size yes 300 var 3 [ -0.35 0 0 0.5 ]
    workspace_switch yes 500 var simple [ 0.215 0.61 0.355 1 ]
    window_fullscreen yes 500 var simple [ 0.3 0.5 0.4 1 ]
    # You can also define curves for the following events
    # window_move_float
    # overview
    # jump
}

```

### Key Bindings

*scroll* provides a quite powerful key binding system where you can use
keys, mouse buttons or trackpad gestures to run different actions. You can also
use *modes* which allow you to have modal key bindings, where several actions
are available when you type a prefix. You can read all about this in `man 5 scroll`.

The main way to set key bindings is through `bindsym`. `bindsym` uses the
symbols associated to each key in the active keyboard layout. For example,
to call `cycle_size h prev` when pressing `Mod+minus` use:

```
bindsym $mod+minus cycle_size h prev
```

However, people who need to use more than one layout in their system may find
the problem some keys don't always correspond to the same symbol, for example
on a `us` keyboard that uses a QWERTY and an AZERTY layout, hitting `q` will
type a `q` under the QWERTY layout, but an `a` under the other one. For this
reason, `bindsym` accepts the `--to-code` option. This option sets the key
binding's symbol to be translated by the first layout defined for the
keyboard, instead of the current layout. This makes bindings permanent, no
matter which layout is currently being used.

There is also `bindcode` to exactly match a key code to a binding. Each key of
your keyboard has a unique key code. Use `wev` to know the number
corresponding to any key, and set the binding. For example, the `t` key in
many cases corresponds to the code `28`. You can set a binding like this:

```
bindcode $mod+28 toggle_size this 1.0 1.0
```

or easier to read:

```
set $KEY_T 28
bindcode $mod+$KEY_T toggle_size this 1.0 1.0
```

Mouse buttons are named `buttonX`, with `X` being a numeber: `1` left mouse
button, `2`, right mouse button etc. You can also use `wev` to identify mouse
button codes instead: `272`, `273`, etc.

*scroll* also supports the command `send_shortcut` which allows you to send
modifiers/key/mouse button combinations to the currently focused application.
For example:

```
# Use mouse->back as middle mouse button to close Firefox tabs
bindsym --whole-window button9 send_shortcut button2
```

#### Key Binding Modes

*scroll* has a lot of commands, and the default configuration has an
overwhelming number of key bindings. This is only possible because of keyboard
*modes*. A *mode* is a special state where a keyboard prefix triggers a
sub-map. This allows you to have groups of related commands that can be
triggered after a leader key binding. There is a parent mode, called
"default", from which all the others hang. For example:

```
# Mode modifiers
mode "modifiers" {
    bindsym $right set_mode after; mode default
    bindsym $left set_mode before; mode default
    bindsym home set_mode beginning; mode default
    bindsym end set_mode end; mode default
    bindsym $up set_mode focus; mode default
    bindsym $down set_mode nofocus; mode default
    bindsym h set_mode center_horiz; mode default
    bindsym Shift+h set_mode nocenter_horiz; mode default
    bindsym v set_mode center_vert; mode default
    bindsym Shift+v set_mode nocenter_vert; mode default
    bindsym r set_mode reorder_auto; mode default
    bindsym Shift+r set_mode noreorder_auto; mode default

    # Return to default mode
    bindsym Escape mode "default"
}
bindsym $mod+backslash mode "modifiers"
```

Pressing `mod+backslash` will enter a special "modifiers" mode. In that mode,
*scroll* only listens to certain bindings defined for the mode. In this case,
the ones within the mode block. You can exit the mode by adding `mode default`
to the end of the command, as in the example, or pressing `Escape`, which
simply calls `mode default` and returns to the main, parent default mode.

If your desktop bar supports showing the *sway/scroll* keyboard mode, you can
use the name of the mode to add interesting information. Read
[this section](#screenshots) for an example. It sets the mode name to a
string that shows the different key bindings for the mode, so you have
"documentation" on your desktop bar.

### Lock and Suspend

`swaylock` and `swayidle` are simple applications you can use for locking and
suspending your session if you still don't have any favorites.

In your config:

```
set $lock_cmd "pidof swaylock || swaylock"
exec swayidle -w
bindsym $mod+l exec $lock_cmd
```

Then create the following file `~/.config/swayidle/config`

```
timeout 600 'swaylock -f'
timeout 605 'scrollmsg "output * power off"' resume 'scrollmsg "output * power on"'
timeout 30 'if pgrep swaylock; then scrollmsg "output * power off"; fi' resume 'if pgrep swaylock; then scrollmsg "output * power on"; fi'
before-sleep 'swaylock -f'
lock 'swaylock -f; sleep 5s; scrollmsg "output * power off"'
```

and `~/.config/swaylock/config`

```
daemonize
show-failed-attempts
color=000000
ignore-empty-password
```

This configuration will lock your system when you press `mod+l` or after 10
minutes of staying idle. After locking it, monitors will enter a suspended
state until you try to enter your password.

### Screenshots

*scroll* can use any Wayland screenshot tool. The following example is just
for people who don't have a favorite one, or are installing their first
Wayland compositor. It uses `swappy`, `grim` and `slurp`.

```
set $satty swappy -f -
set $printscreen_mode 'printscreen (r:region f:full o:output w:window)'
mode $printscreen_mode {
    bindsym r exec grim -g "$(slurp -d)" - | $satty; mode default
    bindsym f exec grim - | $satty; mode default
    bindsym o exec grim -o "$(scrollmsg -t get_outputs | jq -r '.[] | select(.focused) | .name')" - | $satty; mode default
    bindsym w exec scrollmsg -t get_tree | jq -r '.. | select(.focused?) | .rect | "\(.x),\(.y) \(.width)x\(.height)"' | grim -g - - | $satty; mode default

    bindsym Escape mode "default"
}
bindsym $mod+Print mode $printscreen_mode
```



## Commands and Quirks Specific to *scroll*

### Layout types

You can set the layout type per output (monitor). There are two types:
"horizontal" and "vertical". If the monitor is in landscape orientation, the
default will be "horizontal", and if it is in portrait mode, it will be
"vertical". But you can force any mode per output, like this:

``` config
output DP-2 {
    ...
    layout_type horizontal
    ...
}
```

The *horizontal* layout will create columns of windows, and the "vertical"
layout will create rows of windows. You can still add any number of columns or
rows, and any number of windows to each row/column.

The command `layout_transpose` allows you to change from one type of layout to
the other at runtime. For example, you want to move your current workspace
from a landscape monitor to one in portrait mode: `move` the workspace and then
call `layout_transpose`; it will change your existing layout from a row of
columns to a column of rows, keeping all your windows in the same relative
positions. You can undo it by calling `layout_transpose` again.

### Modes

You can set the mode with the `set_mode <h|v>` command. *scroll* works in any
of two modes that can be changed at any moment.

1. "horizontal (h)" mode: In horizontal layouts, it creates a new column per new
   window. In vertical layouts, it adds the new window to the current row.
   For horizontal layouts, `cyclesize h` and `setsize h` affect the width of
   the active column. `align` aligns the active column according to the
   argument received. `fit_size h` fits the selected columns to the width of
   the monitor. For vertical layouts, those commands affect the active window
   in the active row.

2. "vertical (v)" mode: In horizontal layouts, it adds the new window to the
   active column. In vertical layouts, it adds the new window to a new row.
   For horizontal layouts, `cyclesize v` and `setsize v` affect the height of
   the active window. `align` aligns the active window within its column
   according to the argument received. `fit_size v` fits the selected column
   windows to the height of the monitor. For vertical layouts, those commands
   affect the active row.


### Mode Modifiers

Modes and mode modifiers are per workspace.

At window creation time, *scroll* can apply several modifiers to the
current working mode (*h/v*). `set_mode` supports extra arguments:

``` config
set_mode [<h|v|t> <after|before|end|beg> <focus|nofocus> <center_horiz|nocenter_horiz> <center_vert|nocenter_vert> <reorder_auto|noreorder_auto>]
```

1. `<h|v|t>`: set horizontal, vertical, or toggle the current mode.
2. `position`: It is one of `after` (default), `before`, `end`, `beg`.
This parameter decides the position of new windows: *after* the current one
(default value), *before* the current one, at the *end* of the row/column, or
at the *beginning* of the row/column. The currently focused window will have
the corresponding border painted with the `indicator` color to show where the
new window would open.
3. `focus`: One of `focus` (default) or `nofocus`. When creating a new window,
this parameter decides whether it will get focus or not.
4. Reorder automatic mode: `reorder_auto` (default) or `noreorder_auto`. By
default, *scroll* will reorder windows every time you change focus, move or
create new windows. But sometimes you want to keep the current window in a
certain position, for example when using `align`. `align` turns reordering
mode to `noreorder_auto`, and the window will keep its position regardless of
what you do, until you set `reorder_auto` again.
5. `center_horiz/nocenter_horiz`: It will keep the active column centered
(or not) on the screen. The default value is the one in your configuration.
4. `center_vert/nocenter_vert`: It will keep the active window centered
(or not) in its column. The default value is the one in your configuration.

You can skip any number of parameters when calling the dispatcher, and their
order doesn't matter.


### Focus

Focus also supports `beginning` and `end`. Those arguments move you to the end
or beginning of a column/row depending on the current mode.


### Window Movement

*scroll* adds movement options specific to the tiled scrolling layout.

``` config
move left|right|up|down|beginning|end [nomode]
```

Depending on the mode and layout type, you will be moving columns/rows or
windows. You can move them in any direction, or to the end/beginning of the
row/column.

Aside from that, you can work in `nomode` "mode". With that argument, windows
will move freely. Movement will **only** move the active window, leaving its
column intact, regardless of which *mode* (h/v) you are currently in. For a
horizontal layout, the movement will be like this:

If the window is in some column with other windows, any `left` or `right`
movement will expel it to that side, creating a new column with just that
window. Moving it again will insert it in the column in the direction of
movement, and so on. Moving the window `up` or `down` will simply move it in
its current column. You can still use `beginning/end` to move it to the edges
of the row if it is alone in a column, or the edges of the column if it has
siblings in that column.


### Resizing

``` config
cycle_size <h|v> <next|prev>
```

`cycle_size <h|v>` cycles forward or backward through a number of column
widths (in *horizontal* mode), or window heights (in *vertical* mode). Those widths or
heights are a fraction of the width or height of the monitor, and are
configurable globally and per monitor. However, using the `resize` command, you
can still modify the width or height of any window freely.

`set_size` is similar to their `cycle_size`, but instead of cycling, it allows
the width or height to be set directly.


### Aligning

Columns are generally aligned in automatic mode, always making the active one
visible, and trying to make at least the previously focused one visible too if
it fits the viewport, if not, the one adjacent on the other side. However, you
can always align any column to the *center*, *left* or *right* of the monitor
(in *horizontal* mode), or *up* (top), *down* (bottom) or to the *center* in
*vertical* mode. For example center a column for easier reading, regardless of
what happens to the other columns. If you want to go back to automatic mode,
you need to use the mode modifier `reorder_auto` or call `align reset`. The
only time when alignment will be lost is if you open a new window. However,
there is also a configuration option, `align_reset_auto yes|no`. It is `no` by
default. If set to `yes`, every time you change focus, the alignment will be
reset automatically, for a behavior similar to hyprscroller's.

You can also center a window on your workspace using *middle*, it
will center its column, and also the window within the column.

``` config
align <left|right|center|up|down|middle|reset>
```

### Fitting the Screen

When you have a ultra-wide monitor, one in a vertical position, or the default
column widths or window heights don't fit your workflow, you can use manual
resizing, but it is sometimes slow and tricky.

``` config
fit_size <h|v> <active|visible|all|toend|tobeg> <proportional|equal>
```

`fit_size` allows you to re-fit the columns (*horizontal* mode) or windows
(*vertical* mode) you want to the screen extents. It accepts an argument related to the
columns/windows it will try to fit. The new width/height of each column/window
will be *proportional* to its previous width or height, relative to the other
columns or windows affected, or *equal* for all of them.

1. `active`: It will maximize the active column/window.
2. `visible`: All the currently fully or partially visible columns/windows will
   be resized to fit the screen.
3. `all`: All the columns in the row or windows in the column will be resized
   to fit.
4. `toend`: All the columns or windows from the focused one to the end of the
   row/column will be affected.
5. `tobeg` or `tobeginning`: All the columns/windows from the focused one to
   the beginning of the row/column will now fit the screen.


### Workspace Scaling Commands: Overview

In *scroll* you can work at any scale. The workspace can be scaled using

``` config
scale_workspace <exact number|increment number|reset|overview>
```

This command will scale the workspace to an exact scale, or incrementally by a
delta value. If you want a useful automatic scale, use the `overview` argument
which will fit all the windows of the workspace in the current viewport.

Note that You will still be able to interact with the windows normally (change
focus, move windows, create or destroy them, type in them etc.). Use it as a
way to see where things are and move the active focus, or reposition windows.

Example configuration:

``` config
# Scaling
    # Workspace
    # Mod + Shift + comma/period will incremmentally scale the workspace
    # down/up. Using Mod + Shift and the mouse scrollwheel will do the same.
    bindsym $mod+Shift+comma scale_workspace incr -0.05
    bindsym --whole-window $mod+Shift+button4 scale_workspace incr -0.05
    bindsym $mod+Shift+period scale_workspace incr 0.05
    bindsym --whole-window $mod+Shift+button5 scale_workspace incr 0.05
    bindsym $mod+Shift+Ctrl+period scale_workspace reset

    # Overview
    # Mod + Tab or a lateral mouse button will toggle overview.
    bindsym --no-repeat $mod+tab scale_workspace overview
    bindsym --whole-window button8 scale_workspace overview
```


### Jump

`jump` provides a shortcut-based quick focus mode for any tiled window on
the active workspaces, similar to [vim-easymotion](https://github.com/easymotion/vim-easymotion)
and variations.

It shows all the windows on your monitors' active workspaces in overview, and
waits for a combination of key presses (overlaid on each window) to quickly
change focus to the selected window. Pressing any key that is not on the list or
a combination that doesn't exist, exits jump mode without changes.

Depending on the total number of windows and keys you set on your list, you
will have to press more or less keys. Each window has its full trigger combination
on the overlaid label.

You can call `jump` from any mode: overview or normal mode.

There are also three special `jump` modes:

`jump workspaces` will show you a preview of all the available workspaces on
their respective monitor. You can use this mode to preview and quickly jump
to any workspace.

`jump floating` will show you an overview of all the floating windows on the
active workspaces of each monitor. The windows will move so they all show
without overlap. You can use this mode to preview and quickly jump to any
of them.

`jump container` will show you all the windows in the active column
(horizontal layout) or all the windows in the active row (vertical layout), so
you can quickly jump to any of them. It is a good substitute for tabs, because
you also see the content of the windows.

``` config
    bindsym --no-repeat $mod+slash jump
    bindsym --no-repeat $mod+Shift+slash jump container
    bindsym --no-repeat $mod+Ctrl+slash jump workspaces
    bindsym --no-repeat $mod+Alt+slash jump floating
```

You can also click on the item (container, workspace etc.) to exit jump mode
and focus on that element.

There are also special jump modes for the scratchpad and trailmarks.

### Scratchpad

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


### Content Scaling

*scroll* lets you scale the content of a window independently of the current
monitor scale or fractional scale. You can have several copies of the same
application with different scales. This works well for both Wayland and
XWayland windows.

Add these key bindings to your config:

``` config
    # Content
    # Mod + period/comma scale the active window's content incremmentally.
    # Mod + scroll wheel do the same.
    bindsym $mod+comma scale_content incr -0.05
    bindsym --whole-window $mod+button4 scale_content incr -0.05
    bindsym $mod+period scale_content incr 0.05
    bindsym --whole-window $mod+button5 scale_content incr 0.05
    bindsym $mod+Ctrl+period scale_content reset
```

### Full Screen Modes

Aside from sway's full screen workspace and full screen global modes, *scroll*
also supports the command `fullscreen application`. You can toggle an application's
interface to be in full screen mode, while the content still fits in a container.

`fullscreen application` basically decouples the synchronization between the
application's UI full screen mode and the container's. With different combinations
of `fullscreen` and `fullscreen application` you can have several "fake" full
screen modes, like full screen YouTube videos within a container, full screen
UI within a container, or a regular UI in a full screen container.

`fullscreen layout` applies a special full screen mode to the active tiled
window. The window becomes full screen but it is still part of the tiling
layout, so you can scroll it like the rest. You can have as many of these
full screen windows as you want at the same time.

Regarding the base `fullscreen workspace` command, you can also change focus
while in full screen mode, and the new window will still be in full screen mode.
See the `fullscreen_movefocus` option. This way
you can work normally in full screen mode, even using `jump` to move to
different containers quickly.

*scroll* also supports a *maximize* mode, through the `toggle_size` command.
This command lets you toggle back and forth any size for the *active* window in
the current workspace, or *all* of them. See the manual for details.

### Touchpad and Mouse Drag Scrolling

The default for scrolling is swiping with three fingers to scroll left, right,
up or down.

When swiping vertically (a column), *scroll* will scroll the column
that contains the mouse pointer. This allows you to scroll columns that are
not the active one if your configuration is set to focus following the mouse.

You can also use the mouse (Mod + dragging with the center button pressed) to
scroll.

### Animations

*scroll* supports very customizable animations using N-order Bezier curves.
You can use specific animation curves for each operation, and each curve is
composed of two additional curves. One controls the timing for the animation
of the changing variable, and another an offset for the non-changing one.
This means you can animate the speed of the effect, like in most compositors,
but you can also animate the offset that isn't changing. For example, when
a window is moving, you can animate the coordinate that is moving, but also
define an oscillation for the coordinate that doesn't change.

Using N-order Bezier curves, the curves can be practically anything. 

There are two optional curves to define:

- `var` defines the timing/position for the main animated variable, and should
always start at (0, 0) and end at (1, 1). You only define the points in-between.
(0, 0) is `time = 0` and initial `x` position. (1, 1) is `time = 1` (end of
animation) and `x` at the final animation position.

*scroll* uses a different parametrization for its 2D Bezier curves than other
compositors that only support cubic Bezier curves with two user-defined control
points. This is because *scroll* supports Bezier curves of any order, and this
may produce curves with more than one y value per x value. So *scroll* uses the
fraction of the curve's length as parameter, to ensure curve points can be
defined in an unique way, while those other compositors use the curve's x value
because their curves cannot contain loops. For compatibility with curves from
other compositors and CSS, the var animation curve can have an optional argument,
`simple`, that replaces order. This creates a compatible cubic Bezier.
For example:

``` config
default yes 300 var simple [ 0.6 -0.28 0.735 0.045 ]

# instead of:

default yes 300 var 3 [ 0.6 -0.28 0.735 0.045 ]
```

- `off` defines the positional offset for the variable that is static (for
example, if moving on the x direction, `var` defines the timing of the x
coordinate and `off` the offset for y). This curve starts at (0, 0) (initial
`x` and `y` positions) and ends at (1, 0) (final `x` position, and final
offset for `y` which is 0 because that is the variable that doesn't move. To
avoid mistakes, you only define the points in-between.

[This](https://nurbscalculator.in/) page has a Bezier curve design applet you
can use to customize any curve. Don't forget to select `Bezier` as the type of
the curve instead of the default `NURBS`. Here, you will need to add the
origin and end points to be able to design the curve, but then you don't
add them in your config.

If you want some simpler (order 3) curves for the `var` curve, you can copy them
from [here](https://www.cssportal.com/css-cubic-bezier-generator/). You can
copy them directly, as they don't include the initial and last points either.

Read the man pages and the section of this document on *Animation Options* for
details, and try these example curves in your `config` to see it in action:

``` config
animations {
    default yes 300 var 3 [ 0.215 0.61 0.355 1 ]
    window_open yes 300 var 3 [ 0 0 1 1 ]
    window_move yes 300 var 3 [ 0.215 0.61 0.355 1 ] off 0.05 6 [0 0.6 0.4 0 1 0 0.4 -0.6 1 -0.6]
    window_size yes 300 var 3 [ -0.35 0 0 0.5 ]
}
```

### Spaces

A *space* is a configuration of existing windows. You can control spaces with
the command `space load|save|restore name`.

Saving a *space* stores the current configuration of the workspace, including
window geometry and positions, content scale etc.

Loading a *space* recovers the windows that still exist from that space and
applies the stored configuration to them. This only applies to windows that
still exist, if you have closed any windows from the space, they won't be
recovered. You can load a space in any workspace, it doesn't have to be the
original one. Loading the space will gather all the windows in the saved space
from any workspace where they may currently be.

Restoring a *space* is like loading a space, but any window in the workspace
where loading happens that doesn't belong to the space, will be closed. You
can use this if you have opened several disposable applications in your
workspace and want to remove the clutter by restoring your original
configuration/space.

You can use the keybindings in the default configuration to manage spaces
named from 0 to 9, or you can assign key bindings to the following script to
name and manage your own space names which can be arbitrary, multiword strings.

``` config
bindsym $mod+g exec scroll-spaces.sh load
bindsym $mod+Shift+g exec scroll-spaces.sh save
bindsym $mod+Ctrl+g exec scroll-spaces.sh restore
```

`scroll-spaces.sh`

``` bash
#!/bin/bash

if [ $1 == "save" ]; then
    space=$(scrollmsg -t get_spaces | jq -r '.[]' | rofi -p "Save a new or overwrite an existing space" -dmenu)
    scrollmsg "space save" "\"$space\""
elif [ $1 == "load" ]; then
    space=$(scrollmsg -t get_spaces | jq -r '.[]' | rofi -p "Load a space" -dmenu)
    scrollmsg "space load" "\"$space\""
elif [ $1 == "restore" ]; then
    space=$(scrollmsg -t get_spaces | jq -r '.[]' | rofi -p "Restore a space - Windows not belonging to the space will be closed" -dmenu)
    scrollmsg "space restore" "\"$space\""
fi
```


### Pins

*scroll* supports *pinning* a tiled top level container to either edge of the
workspace. This may be useful when you have a very wide monitor, or you want
to keep a column visible at all times. You may want to have some documentation
or terminal always visible.

The command is `pin <beginning|end>`.

It will *pin* the active top level container. For horizontal layouts, it will
pin it to the left (`beginning`) or right (`end`) edge of the monitor. For
vertical layouts, to the top (`beginning`) or bottom (`end`) edge.

`pin` works as a toggle, and there can only be one pin per workspace. The
logic is as follows when you call `pin`:

1. If the current container is already pinned: if you call `pin` with the same
   argument of the current pin, it will be unset and the container freed from
   its pin. If the argument is different, it will move the pinned container to
   the other position.
2. If the current container is not pinned yet: it will replace the pinned
   container, if any.

### Window Selection/Moving

The command `selection` manages window/container selections. You can select
several windows or containers at a time, even in different workspaces and/or
from overview mode. Those windows will change the border color to the one
specified in the option `client.selected`.

Use `selection toggle` to select/deselect a window (in window mode)
or a full container (in top-level mode).

If you want to clear a selection, instead of "toggling" each window/container,
you can call `selection reset`, which will clear all the selected items.

Once you have made a selection, you can move those windows to a different
workspace or location in the same workspace using `selection move`.
The selection order and column/window configuration will be maintained.

If your new location has a different layout type (for example, vertical
instead of horizontal), your containers and windows will adapt, transposing
their positions to better fit the new destination.

`selection workspace` will add every window of the current workspace
to the selection. You can use this when you want to move one workspace to a
different one, but keeping windows positions and sizes. Use
`selection workspace`, and then `selection move` where you want the windows
to appear.

`selection move` uses the current mode modifier to locate the new containers.
So you can place the new containers `before`, `after`, at the beginning
(`beg`) or `end` depending on the current mode.

`selection to_trail` creates a new trail from the contents of the current
selection list, and resets the selection. You can use that trail to navigate
through those windows quickly, or as storage for the selection
(`trail to_selection` can recover the selection from a stored trail).


```
selection <toggle|reset|workspace|move|to_trail>
```


### Trails and Trailmarks

Trails and Trailmarks are a concept borrowed from [trailblazer.nvim](https://github.com/LeonHeidelbach/trailblazer.nvim).

A **trailmark** is like an anonymous mark on a window, and a **trail** is a
collection of trailmarks. You can have as many trails as you want, and as many
trailmarks as you want in any trail. Each window can be in as many trails
as you want, too.

Creating your first trailmark (`trailmark toggle`) will create a trail. From
then on, every trailmark you create will be assigned to that trail. You can
navigate back (`trailmark prev`) and forth (`trailmark next`) within the
collection of trailmarks contained in the trail. You can also use
`trailmark jump` to enable jump mode for the windows on a trail. This is quite
powerful, as it allows you to create groups of windows you can access very
quickly, regardless of which workspace they belong to.

To create a new trail, use `trail new`. With `trail prev` and `trail next`
you can navigate trails, changing the active one. The active trail will be
the one used for the trailmark command (`toggle`, `next`, `prev` and `jump`).

Clear all the trailmarks of the active trail using `trail clear`, or delete
the trail from the list with `trail delete`.

`trail to_selection` creates a selection list from the trailmarks in the active
trail. You can use that selection for example to move all the windows to a new
workspace using `selection move`.

*scroll* generates IPC signals for trail/trailmark events. See the man page
or the example implementation in *scrollbar* if you want to use these signals
to display information on your desktop bar.

There is also a reference implementation for the *scroll* modules in
[gtkshell](https://github.com/dawsers/gtkshell).

Read the example config for an example on how to set bindings for the trail
and trailmark commands.

``` config
mode "trailmark" {
    bindsym bracketright trailmark next
    bindsym bracketleft trailmark prev
    bindsym semicolon trailmark toggle; mode default
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


### Tips for Using Marks

Even though trails and trailmarks are in general more useful because of the
`jump` functionality, *scroll* also supports sway's mark based navigation. I use
these scripts and key bindings:

``` config
    # Marks
    bindsym $mod+m exec scroll-mark-toggle.sh
    bindsym $mod+Shift+m exec scroll-mark-remove.sh
    bindsym $mod+apostrophe exec scroll-mark-switch.sh
```

`scroll-mark-toggle.sh`


``` bash
#!/bin/bash
    
marks=($(scrollmsg -t get_tree | jq -c 'recurse(.nodes[]?) | recurse(.floating_nodes[]?) | select(.focused==true) | {marks} | .[]' | jq -r '.[]'))

generate_marks() {
    for mark in "${marks[@]}"; do
        echo "$mark"
    done
}

mark=$( (generate_marks) | rofi -p "Toggle a mark" -dmenu)
if [[ -z $mark ]]; then
    exit
fi
scrollmsg "mark --add --toggle" "$mark"
```

`scroll-mark-remove.sh`

``` bash
#!/bin/bash
    
marks=($(scrollmsg -t get_tree | jq -c 'recurse(.nodes[]?) | recurse(.floating_nodes[]?) | select(.focused==true) | {marks} | .[]' | jq -r '.[]'))

generate_marks() {
    for mark in "${marks[@]}"; do
        echo "$mark"
    done
}

remove_marks() {
    echo $marks
    for mark in "${marks[@]}"; do
        scrollmsg unmark "$mark"
    done
}

mark=$( (generate_marks) | rofi -p "Remove a mark (leave empty to clear all)" -dmenu)
if [[ -z $mark ]]; then
    remove_marks
    exit
fi
scrollmsg unmark "$mark"
```

`scroll-mark-switch.sh`

``` bash
#!/bin/bash

marks=($(scrollmsg -t get_marks | jq -r '.[]'))

generate_marks() {
    for mark in "${marks[@]}"; do
        echo "$mark"
    done
}

mark=$( (generate_marks) | rofi -p "Switch to mark" -dmenu)
[[ -z $mark ]] && exit

scrollmsg "[con_mark=\b$mark\b]" focus
```


## Workspace Rules

*scroll* supports Lua scripts, so you can do very complex things. A simple
example of the Lua API is this script that implements "workspace rules":

``` lua
-- workspace_exec <name> <commands>
local args, state = ...

local scroll = require("scroll")

local function on_create_ws(workspace, _)
  local name = scroll.workspace_get_name(workspace)
  if name == args[1] then
    local cmd = table.concat(args, " ", 2)
    scroll.command(workspace, cmd)
  end
end

scroll.add_callback("workspace_create", on_create_ws, nil)
```

Examples:

```
lua $lua_scripts/workspace_exec.lua 1 workspace split v 0.5
lua $lua_scripts/workspace_exec.lua 3 exec kitty yazi
lua $lua_scripts/workspace_exec.lua 1 exec kitty
```


## Options Specific to Scroll

Aside from i3/sway options, *scroll* supports a few additional ones to manage
its layout/resizing/jump etc.

### Layout Options

The following options are supported globally and per output:

`layout_default_width`: default is `0.5`. Set the default width for new
columns ("horizontal" layout) or windows ("vertical" layouts).

`layout_default_height`: default is `1.0`. Set the default height for new
windows ("horizontal" layouts) or rows ("vertical" layouts).

`layout_widths`: it is an array of floating point values. The default
is `[0.33333333, 0.5, 0.66666667, 1.0]`. These are the fractions `cycle_size`
will use when resizing the width of columns/windows.

`layout_heights`: it is an array of floating point values. The default
is `[0.33333333, 0.5, 0.66666667, 1.0]`. These are the fractions `cycle_size`
will use when resizing the height of windows/rows.


### Per Output Options

`layout_type`: `<horizontal|vertical>`. You can set it per output, or let
*scroll* decide depending on the aspect ratio of your monitor.

Aside from that, you can also set layout options per output, as in:

``` config
output DP-2 {
    background #000000 solid_color
    scale 1
    resolution 1920x1080
    position 0 0
    layout_type horizontal
    layout_default_width 0.5
    layout_default_height 1.0
    layout_widths [0.33333333 0.5 0.666666667 1.0]
    layout_heights [0.33333333 0.5 0.666666667 1.0]
}
```

### Jump Options

`jump_labels_color`: default is  `#159E3080`. Color of the jump labels.

`jump_labels_background`: default is  `#00000000`. Color of the background of
the jump labels.

`jump_labels_scale`: default is `0.5`. Scale of the label within the window.

`jump_labels_keys`: default is `1234`. Keys that will be used to generate
possible jump labels.

### General Options

`align_reset_auto`: default is `yes` (`true`). If `true`, every time you
change focus, any active alignment (`align` command) will be reset automatically,
without any need to call `align reset`, for a behavior similar to hyprscroller's.

`cursor_shake_magnify <true|false>`: Default value is `false`. If `true`,
shaking the cursor will magnify it. It can be useful to find the location of
the cursor on very big displays.

`cycle_size_wrap <true|false>`: Default value is `false`. Enables wrapping
sizes in the `cycle_size` command. Incrementing from the last available size
will start again from the beginning, and decrementing the first one will move
to the last.

`fullscreen_movefocus <true|false> [follow|nofollow]`: Default value is `true`
and `nofollow`.
* `true` allows you to change focus while in full screen mode.
* `false`. Focusing other containers in the same workspace is not allowed until
exiting full screen mode.
* `follow`. Full screen will follow focus. You can still use overview/jump to
select a new window, and full screen state will follow.
* `nofollow`. You can focus other windows, but they will not become full screen.
However, those in full screen mode will recover that mode when focusing on them
again.

`focus_follows_mouse full`: `full` is a new argument for `focus_follows_mouse`
which makes *scroll* change focus to a window when hovering over it, only when
it is fully inside the viewport.

`fullscreen_on_request default|layout`: Default is `default`.
This command/option controls what scroll does when an application requests
full screen mode for a container.
* `default`	uses `fullscreen workspace`, which is the best mode for performance.
*scroll* only has to display one buffer peroutput, so this is probably the
option to choose for applications that require the best performance and are
GPU bound, like games.
* `layout` fulfills the request using `fullscreen layout`, creating a full
screen window in the tiling layout that scrolls with the rest. See the
`fullscreen` command for more details.

`maximize_if_single <true|false>`: Default value is `false`. If `true`,
whenever there is a single tiling window in the workspace, it will be
maximized, unless you explicitly resize	it.

`workspace_next_on_output_create_empty <true|false>`: The default is `true`.
If `true`, when calling `workspace next_on_output`,	if the current workspace
is the last one, it will create a new workspace	instead	of wrapping to the
first one. The new workspace will be the first unused one for that output.
If `false`, instead of creating a new workspace, it will focus on the first one.

`xwayland_output_scale <true|false>`: The default is `true`. If `true`,
XWayland windows get fractional scaling. Make it `false` if you want XWayland
windows to ignore the output's fractional scaling value and keep a scale of 1.
This option is equivalent to applying to each XWayland window a content scale
with value the inverse of the output's fractional scale.

`xdg_activation_force <true|false>`: The default is `false`. When `true`,
clients that don't implement the xdg-activation protocol correctly will be able
to activate focus on other windows. See `focus_on_window_activation` to set the
state of the activated window to `urgent` or `focused`.

### Decoration Options

*scroll* uses different decorations (borders, title bars etc.) than *sway*. It
supports rounded corners and title bars, shadows, dimming of inactive windows
etc. The following options affect their properties.

`default_decoration [border_radius <n>] [shadow true|false]
[shadow_dynamic true|false]	[shadow_size <n>] [shadow_blur <n>]
[shadow_offset x y]	[shadow_color #RRGGBBAA] [dim true|false]
[dim_color #RRGGBBAA]`: Sets the default values for window decorations.

* `border radius`: Radius of the border, in pixels. Default is 0.
* `shadow`: Turns on/off shadow casting for the window. Default is off.
* `shadow_dynamic`: Turns of/off dynamic shadows. Dynamic shadows make the
shadow offset variable depending on the location of the window. Default	is off.
* `shadow_size`: size in pixels of the shadow. Default is 40.
* `shadow_blur`: Value of the blur radius in pixelz, where 0 means sharp
shadows. Default is	30.
* `shadow_offset`: Shadow's offset from the window. Default is 40 40.
* `shadow_color`: Color of the shadow. Default is #00000070.
* `dim`: Turns on/off dimming of the content when the window is not in focus.
Default is off.
* `dim_color`: Color used to dim the content when the window is not in focus.
Default is #00000040.

`titlebar_border_radius`: When title bars are enabled, this will set the
radius of the top title bar borders. Default is 0.

### Gesture Options

`gesture_scroll_enable`: default is `true`. Enables the trackapad scrolling gesture.

`gesture_scroll_fingers`: default is `3`. Number of fingers to use for the
scrolling gesture.

`gesture_scroll_sentitivity`: default is `1.0`. Increase if you want more
sensitivity.

### Animation Options

You can define a block in your `config` file for animations. The block is
called `animations`. Within that block there are several options allowed:

`enabled`: (boolean) default is `yes`. Enables/disables animations globally.

`frequency_ms`: (integer) default is `16`. Number of milliseconds for each
animation step. The default is approximately the refresh rate of a monitor
that works at 60 Hz. If you need smoother animations, reduce this value. Use
with caution to avoid crashes or performance issues. The default should work
for most cases unless you have a very high refresh rate monitor.

`style <clip|scale>`: Default is `clip`. `clip` keeps the resolution of
the client and clips the container while animating. This leads to a part
of the container showing the background when resizing containers to a
smaller size, but at the same time, the content is always the same, making it
easier to read. `scale`, instead, scales the content of the container to the
animated size of the container.	This makes transitions smoother, but the
content may be deformed	while animating.

`default`: defines the default animation curve. Follows the format explained
below. default is `yes 300 var 3 [ 0.215 0.61 0.355 1 ]`

The following curves are not defined by default, so they use the `default`
curve unless you add one to your config.

`window_move`: defines the curve for windows movement (`move` command).

`window_move_float`: It is the curve for floating windows movement
(`move` command for	floating windows).

`window_open`: defines the curve for windows opening.

`window_fullscreen`: It is the curve for (un)setting windows full
screen.

`window_size`: defines the curve for windows resizing (`cycle_size`,
`set_size`, `fit_size`, `resize` commands)..

`workspace_switch`: curve for workspace switching animations.

`overview`: used when turning on/off overview mode.

`jump`:  used when starting/ending jump mode (except `jump
workspaces`).

Format of an animation curve:

- The first field is `enabled`. It can be `yes` or `no`. It enables or
  disables animations for that operation. If set to `no`, you don't need to
  define the curve. If set to `yes`, you can set the `duration` if you
  want to use the `default` curve, or define the curves using the following
  options.

- `duration_ms`. Duration of the animation in milliseconds.

- `var`. Defines the animation curve for the variable changing in the
  operation/command. The fields are `order` and `control_points`. `order`
  defines the order of the Bezier curve that follows, and `control_points` is
  an array defining its control points except for the first, that is `(0, 0)`,
  and the last, `(1, 1)`. The number of values in the array will be `2 * (order - 1)`.
  A Bezier curve of `order` needs `order + 1` control points to be
  defined, but we set the first and last to `(0, 0)` and `(1, 1)`.
  We use 2D Bezier curves, so the array needs `2 * (order -1)` numbers in it.

- `off`. Defines the animation curve for the variable that doesn't change in the
  operation/command. The fields are `scale` `order` and `control_points`.
  `scale` is the fraction of the workspace size used to scale the curve. As
  the curve is defined from `(0, 0)` to `(1, 0)`, we need a parameter to scale
  the offset value. This parameter does that. For example, using a small parameter
  like `0.05`, creates offsets of an order of `5%` of the size of the workspace.
  `order` and `control_points` work as in the `var` case. But this time, the
  points you will not include are: `(0, 0)` (first) and `(1, 0)` (last).

Example:

``` config
animations {
    default yes 300 var 3 [ 0.215 0.61 0.355 1 ]
    window_open yes 300 var 3 [ 0 0 1 1 ]
    window_move yes 300 var 3 [ 0.215 0.61 0.355 1 ] off 0.05 6 [0 0.6 0.4 0 1 0 0.4 -0.6 1 -0.6]
    window_size yes 300 var 3 [ -0.35 0 0 0.5 ]
}
```

## X11 Aplications and Xwayland

*scroll*, like *sway*, supports the Xwayland compatibility layer for X11
applications. Xwayland works in *rootless* mode, where each X11 window is a
first-class resident among the native Wayland windows, allowing full interaction
between them.

*scroll* adds the ability to run Xwayland applications at their native scale
instead of the fractional scale defined in your monitor configuration. This is
to avoid blurriness problems, because X11 doesn't support HiDPI properly. Set
`xwayland_output_scale false` in your configuration, and Xwayland applications
will always run at scale 1.

## Lua Scripting

scroll provides a Lua API to enable scripting of the window manager. The API
is evolving. Read the manual's (`man 5 scroll`) Lua section for all the
details.

Using the command `lua` you can run Lua scripts that access window manager
properties, execute commands or add callbacks to window events.

You can assign scripts to keyboard bindings, or add them to your
configuration for execution when the configuration loads.

For example, if you wanted to focus on every window that gets the *urgent*
attribute, you could call this script from your `config` file:

``` lua
local function on_urgent(view, _)
  local container = scroll.view_get_container(view)
  local workspace = scroll.container_get_workspace(container)
  scroll.command(nil, "workspace " .. scroll.workspace_get_name(workspace))
  scroll.command(container, "focus")
end

scroll.add_callback("view_urgent", on_urgent, nil)
```

If the current focused window is a *neovim* instance, when called, this script
resizes *neovim's* window height to 0.66667, and opens a *kitty* terminal under
it with height 0.33333. When you close the *neovim* window, the terminal is
also automatically closed.

``` lua
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

Select and move every tiling *kitty* terminal from the focused workspace to
workspace number 2.

``` lua
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

These are just a few examples. Read the manual to know more about the
API, or check [this](https://github.com/dawsers/scroll/issues/35) issue
for more examples and API requests.

## Screenshots and Video Recording/Streaming

*scroll* is compatible with the latest development version of *sway*, so all
the tools you could use with *sway* are available to *scroll*.

For screenshots, you can use `grim` and `slurp`. `swappy` or `satty` can add
a UI to the screenshot capture. For example:

``` config
#set $satty satty -f - --initial-tool=crop --copy-command=wl-copy --actions-on-escape="save-to-clipboard,exit" --brush-smooth-history-size=5 --disable-notifications
set $satty swappy -f -
set $printscreen_mode 'printscreen (r:region f:full o:output w:window)'
mode $printscreen_mode {
    bindsym r exec grim -g "$(slurp -d)" - | $satty; mode default
    bindsym f exec grim - | $satty; mode default
    bindsym o exec grim -o "$(scrollmsg -t get_outputs | jq -r '.[] | select(.focused) | .name')" - | $satty; mode default
    bindsym w exec scrollmsg -t get_tree | jq -r '.. | select(.focused?) | .rect | "\(.x),\(.y) \(.width)x\(.height)"' | grim -g - - | $satty; mode default
    # The image is scaled like using fractional scaling
    #bindsym w exec grim -T "$(scrollmsg -t get_tree | jq -j '.. | select(.type?) | select(.focused).foreign_toplevel_identifier')" - | $satty; mode default

    bindsym Escape mode "default"
}
bindsym $mod+Print mode $printscreen_mode
```

For video recording/streaming, you should install [xdg-desktop-portal-wlr](https://github.com/emersion/xdg-desktop-portal-wlr).
and `pipewire`. With those two dependencies, [OBS Studio](https://obsproject.com/)
works out of the box.

For application compatibility read [this](https://github.com/emersion/xdg-desktop-portal-wlr/wiki/Screencast-Compatibility)
and the [FAQ](https://github.com/emersion/xdg-desktop-portal-wlr/wiki/FAQ).

## IPC

*scroll* adds IPC events you can use to create a module for your favorite
desktop bar.

See `include/ipc.h` for `IPG_GET_SCROLLER`, `IPC_EVENT_SCROLLER`,
`IPG_GET_TRAILS` and `IPC_EVENT_TRAILS`.

You can get data for mode/mode modifiers, overview and scale mode as well as
trails and whether a view has an active trailmark.

For anyone interested in creating modules for popular desktop bars, there is a
reference implementation for the *scroll* modules in
[gtkshell](https://github.com/dawsers/gtkshell).


``` c
json_object *ipc_json_describe_scroller(struct sway_workspace *workspace) {
	if (!(sway_assert(workspace, "Workspace must not be null"))) {
		return NULL;
	}
	json_object *object = json_object_new_object();

	json_object_object_add(object, "workspace", json_object_new_string(workspace->name));
	json_object_object_add(object, "overview", json_object_new_boolean(layout_overview_mode(workspace) != OVERVIEW_DISABLED));
	json_object_object_add(object, "scaled", json_object_new_boolean(layout_scale_enabled(workspace)));
	json_object_object_add(object, "scale", json_object_new_double(layout_scale_get(workspace)));

	enum sway_container_layout layout = layout_modifiers_get_mode(workspace);
	json_object_object_add(object, "mode",
		json_object_new_string(layout == L_HORIZ ? "horizontal" : "vertical"));

	enum sway_layout_insert insert = layout_modifiers_get_insert(workspace);
	switch (insert) {
	case INSERT_BEFORE:
		json_object_object_add(object, "insert", json_object_new_string("before"));
		break;
	case INSERT_AFTER:
		json_object_object_add(object, "insert", json_object_new_string("after"));
		break;
	case INSERT_BEGINNING:
		json_object_object_add(object, "insert", json_object_new_string("beginning"));
		break;
	case INSERT_END:
		json_object_object_add(object, "insert", json_object_new_string("end"));
		break;
	}
	bool focus = layout_modifiers_get_focus(workspace);
	json_object_object_add(object, "focus", json_object_new_boolean(focus));
	bool center_horizontal = layout_modifiers_get_center_horizontal(workspace);
	json_object_object_add(object, "center_horizontal", json_object_new_boolean(center_horizontal));
	bool center_vertical = layout_modifiers_get_center_vertical(workspace);
	json_object_object_add(object, "center_vertical", json_object_new_boolean(center_vertical));
	enum sway_layout_reorder reorder = layout_modifiers_get_reorder(workspace);
	json_object_object_add(object, "reorder",
		json_object_new_string(reorder == REORDER_AUTO ? "auto" : "lazy"));

	return object;
}

void ipc_event_scroller(const char *change, struct sway_workspace *workspace) {
	if (!ipc_has_event_listeners(IPC_EVENT_SCROLLER)) {
		return;
	}
	sway_log(SWAY_DEBUG, "Sending scroller event");

	json_object *json = json_object_new_object();
	json_object_object_add(json, "change", json_object_new_string(change));
	json_object_object_add(json, "scroller", ipc_json_describe_scroller(workspace));

	const char *json_string = json_object_to_json_string(json);
	ipc_send_event(json_string, IPC_EVENT_SCROLLER);
	json_object_put(json);
}

json_object *ipc_json_describe_trails() {
	json_object *object = json_object_new_object();

	json_object_object_add(object, "length", json_object_new_int(layout_trails_length()));
	json_object_object_add(object, "active", json_object_new_int(layout_trails_active()));
	json_object_object_add(object, "trail_length", json_object_new_int(layout_trails_active_length()));

	return object;
}

void ipc_event_trails() {
	if (!ipc_has_event_listeners(IPC_EVENT_TRAILS)) {
		return;
	}
	sway_log(SWAY_DEBUG, "Sending trails event");

	json_object *json = json_object_new_object();
	json_object_object_add(json, "trails", ipc_json_describe_trails());

	const char *json_string = json_object_to_json_string(json);
	ipc_send_event(json_string, IPC_EVENT_TRAILS);
	json_object_put(json);
}

static void ipc_json_describe_view(struct sway_container *c, json_object *object) {
...
	json_object_object_add(object, "trailmark", json_object_new_boolean(layout_trails_trailmarked(c->view)));
}
```

## What are the Differences between *sway* and *scroll*?

They share most commands and configuration syntax, but *scroll* only supports a
scrolling layout similar to [PaperWM](https://github.com/paperwm/PaperWM),
[niri](https://github.com/YaLTeR/niri) or [hyprscroller](https://github.com/dawsers/hyprscroller).

*scroll* removes all the original *sway*/*i3* layouts, but adds many new
features, among them:

* Animations: *scroll* supports very customizable animations.

* *scroll* supports rounded borders and title bars, dimming of inactive
  windows, and dynamic shadows with blur.

* Content scaling: The content of individual windows can be scaled
  independently of the general output scale.

* Overview and Jump modes: You can see an overview of the desktop and work
  with the windows at that scale. Jump allows you to move to any window with
  just some key presses, like easymotion in some editors. There is also a jump
  mode to preview and switch to any available workspace.

* Workspace scaling: Apart from overview, you can scale the workspace to any
  scale, and continue working.

* Lua scripting: scroll provides a lua API to script the window manager.

* Several full screen modes: `workspace`, `global`, `application` and `layout`.

* Trackpad/Mouse scrolling: You can use the trackpad or mouse dragging to
  navigate/scroll the workspace windows.

* Portrait and Landscape monitor support: The layout works and adapts to both
  portrait or landscape monitors. You can define the layout orientation per
  output (monitor).

* For ultra-wide displays, you can split a workspace in two and show them both
  at the same time (`workspace split` command).

Aside from the differences mentioned above, *scroll* adds many new commands
and configuration options. For those familiar with *sway*, this is a list that
includes most of the new commands; check the manual for more information about
each one of them:

1. No need for `--unsupported-gpu` if you are using a Nvidia card.

### Config-Only Commands

`align_reset_auto`, `animations`, `cursor_shake_magnify`, `cycle_size_wrap`,
`fullscreen_movefocus`, `gesture_scroll_enable`, `gesture_scroll_fingers`,
`gesture_scroll_sensitivity`,  `jump_labels_background`, `jump_labels_color`,
`jump_labels_keys`, `jump_labels_scale`, `layout_default_height`,
`layout_default_width`, `layout_heights`, `layout_widths`, `maximize_if_single`,
`workspace_next_on_output_create_empty`, `xwayland_output_scale`

### Runtime Only Commands

`align`, `animations_enable`, `cycle_size`, `decoration`, `fit_size`,
`focus begin/end`, `fullscreen application|layout`, `jump`, `layout_transpose`,
`move beginning|end nomode`, `pin`, `scale_content`, `scale_workspace`,
`scratchpad jump`, `selection`, `set_mode`, `set_size`, `space`,
`toggle_size`, `trail`, `trailmark`

### Config or Runtime Commands

Colors for pinned and selected containers. Indicator changes to show next
insertion location.

`default_decoration`, `focus_follows_mouse full`,
`fullscreen_on_request default|layout`, `titlebar_border_radius`,
`kill focused|unfocused|all`, `send_shortcut`, `workspace swap`,
`workspace split`, `xdg_activation_force`

`lua` and API

### scroll-bar

`mode top`, `scroller_indicator`, `trails_indicator`

Colors: `scroller`

### IPC Protocol

commands: `GET_SCROLLER`, `GET_TRAILS`, `GET_SPACES`

events: `scroller`, `trails`

`fully_visible` attribute for windows

### Outputs

`scale <factor> [force]`, `layout_type`, `layout_default_height`,
`layout_default_width`, `layout_heights`, `layout_widths`

### scrollnag

`--edge center`, `--width`
