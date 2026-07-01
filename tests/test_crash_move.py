from test_utils import wayland_client, wait_for_client_map, ScrollInstance


def test_move_left_nomode_no_crash(scroll_compositor: ScrollInstance) -> None:
    # Open first window
    with wayland_client(scroll_compositor, "Window 1"):
        wait_for_client_map(scroll_compositor, "Window 1")

        # Open second window
        with wayland_client(scroll_compositor, "Window 2"):
            wait_for_client_map(scroll_compositor, "Window 2")

            # Both windows are open. Now move left nomode.
            res = scroll_compositor.cmd("move left nomode")
            assert res and res[0]["success"], f"Command failed: {res}"

            # Check if compositor process is still alive
            assert scroll_compositor.proc.poll() is None, "Compositor crashed"
