tray_files = have_tray ? [
	'tray/host.c',
	'tray/icon.c',
	'tray/item.c',
	'tray/tray.c',
	'tray/watcher.c'
] : []

swaybar_deps = [
	cairo,
	gdk_pixbuf,
	jsonc,
	math,
	pango,
	pangocairo,
	rt,
	wayland_client,
	wayland_cursor
]
if have_tray
	swaybar_deps += sdbus
endif

executable(
	'scrollbar', [
		'bar.c',
		'config.c',
		'i3bar.c',
		'image.c',
		'input.c',
		'ipc.c',
		'main.c',
		'render.c',
		'status_line.c',
		tray_files,
		wl_protos_src,
	],
	include_directories: [sway_inc],
	dependencies: swaybar_deps,
	link_with: [lib_sway_common, lib_sway_client],
	install: true
)
