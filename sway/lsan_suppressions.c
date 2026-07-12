const char *__lsan_default_suppressions() {
	return
		"leak:seatop_begin_default\n"
		"leak:workspace_create\n"
		"leak:layer_surface_destroy\n"
		"leak:finalize_output_config\n"
		"leak:pango_font_description_from_string\n"
		"leak:output_create\n"
		"leak:seat_configure_keyboard\n"
		"leak:sway_keyboard_destroy\n"
		"leak:init_signals\n"
		"leak:FcInit\n";
}
