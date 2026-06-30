executable(
	'wayland-test-client',
	files('clients/wayland-client.c'),
	dependencies: [wayland_client],
	sources: wl_protos_src,
	install: false,
)

xcb_dep = dependency('xcb', required: false)
if xcb_dep.found()
	executable(
		'x11-test-client',
		files('clients/x11-client.c'),
		dependencies: [xcb_dep],
		install: false,
	)
endif



