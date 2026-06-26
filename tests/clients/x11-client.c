#include <xcb/xcb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

int main(int argc, char **argv) {
	xcb_connection_t *conn = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(conn)) {
		fprintf(stderr, "Failed to connect to X11 display\n");
		return 1;
	}

	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	xcb_window_t win = xcb_generate_id(conn);

	uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	uint32_t values[2] = {
		screen->white_pixel,
		XCB_EVENT_MASK_EXPOSURE
	};

	xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, screen->root,
					  0, 0, 150, 150, 10,
					  XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
					  mask, values);

	const char *title = "Test X11 Window";
	if (argc > 1) title = argv[1];
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
						XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
						strlen(title), title);

	const char *instance = "test_instance";
	const char *class = "TestClass";
	if (argc > 2) instance = argv[2];
	if (argc > 3) class = argv[3];
	size_t class_len = strlen(instance) + 1 + strlen(class) + 1;
	char *class_str = malloc(class_len);
	if (!class_str) {
		perror("malloc");
		return 1;
	}
	strcpy(class_str, instance);
	strcpy(class_str + strlen(instance) + 1, class);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
						XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
						class_len, class_str);
	free(class_str);

	char *startup_id = getenv("DESKTOP_STARTUP_ID");
	if (startup_id) {
		xcb_intern_atom_cookie_t cookie =
				xcb_intern_atom(conn, 0, strlen("_NET_STARTUP_ID"), "_NET_STARTUP_ID");
		xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);
		if (reply) {
			xcb_atom_t startup_id_atom = reply->atom;
			free(reply);
			xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, startup_id_atom, XCB_ATOM_STRING,
					8, strlen(startup_id), startup_id);
		}
	}

	xcb_map_window(conn, win);
	xcb_flush(conn);

	while (1) {
		xcb_generic_event_t *ev = xcb_wait_for_event(conn);
		if (!ev) break;
		free(ev);
	}

	xcb_disconnect(conn);
	return 0;
}
