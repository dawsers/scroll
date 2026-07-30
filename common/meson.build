lib_sway_common = static_library(
	'scroll-common',
	files(
		'cairo.c',
		'gesture.c',
		'ipc-client.c',
		'log.c',
		'loop.c',
		'list.c',
		'pango.c',
		'stringop.c',
		'util.c'
	),
	dependencies: [
		cairo,
		pango,
		pangocairo,
		wayland_client.partial_dependency(compile_args: true)
	],
	include_directories: sway_inc
)

lib_sway_common_nolog = static_library(
	'scroll-common-nolog',
	files(
		'cairo.c',
		'gesture.c',
		'ipc-client.c',
		'loop.c',
		'list.c',
		'pango.c',
		'stringop.c',
		'util.c'
	),
	dependencies: [
		cairo,
		pango,
		pangocairo,
		wayland_client.partial_dependency(compile_args: true)
	],
	include_directories: sway_inc
)
