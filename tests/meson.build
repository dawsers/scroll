executable(
	'wayland-test-client',
	files('clients/wayland-client.c'),
	dependencies: [wayland_client],
	sources: wl_protos_src,
	install: false,
)

# G1 focused model tests (issue #1): plain-data template construction, deep
# copy, structural validation, and partial-tree-safe destruction. No
# wlroots/Wayland dependency -- links only the common list/string helpers.
space_template_unit_test = executable(
	'space_template_unit_test',
	files('unit/test_space_template.c', '../sway/tree/space_template.c'),
	include_directories: [sway_inc],
	# lib_sway_common (not the -nolog variant): common/list.c and
	# common/stringop.c call sway_assert()/sway_log(), whose symbols only
	# live in log.c. The nolog variant deliberately omits log.c for
	# consumers that provide those symbols another way (the real `scroll`
	# executable compiles sway/log.c directly into its own sources) -- a
	# standalone test binary has no other source of them. Confirmed by an
	# actual link failure (undefined reference to _sway_assert/_sway_log)
	# on a real Linux build before this was corrected.
	link_with: [lib_sway_common],
	install: false,
)
test('space_template', space_template_unit_test)

xcb_dep = dependency('xcb', required: false)
if xcb_dep.found()
	executable(
		'x11-test-client',
		files('clients/x11-client.c'),
		dependencies: [xcb_dep],
		install: false,
	)
endif



