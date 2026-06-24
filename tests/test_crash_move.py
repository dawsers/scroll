from conftest import ScrollInstance
from test_utils import wayland_client, wait_for_client_map


def test_move_left_nomode_no_crash(scroll_compositor: ScrollInstance) -> None:
    # Open first window
    with wayland_client(scroll_compositor, "Window 1"):
        wait_for_client_map(scroll_compositor, "Window 1")

        # Open second window
        with wayland_client(scroll_compositor, "Window 2"):
            wait_for_client_map(scroll_compositor, "Window 2")

            # Both windows are open. Now move left nomode.
            try:
                res = scroll_compositor.cmd("move left nomode")
                assert res and res[0]["success"], f"Command failed: {res}"
            except Exception as e:
                print(f"Compositor log:\n{scroll_compositor.read_log()}")
                raise e

            # Check if compositor process is still alive
            assert scroll_compositor.proc.poll() is None, "Compositor crashed"
