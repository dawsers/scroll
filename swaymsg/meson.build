readline = cc.find_library('readline', required: true)

executable(
	'scrollmsg',
	'main.c',
	include_directories: [sway_inc],
	dependencies: [jsonc, readline],
	link_with: [lib_sway_common],
	install: true
)
