from conftest import ScrollInstance
from test_utils import wayland_client, wait_for_client_map


def test_fullscreen_move_uaf(scroll_compositor: ScrollInstance) -> None:
    # Start on workspace 1
    scroll_compositor.cmd("workspace 1")

    # Open a window
    with wayland_client(scroll_compositor, "client1"):
        wait_for_client_map(scroll_compositor, "client1")

        focused = scroll_compositor.execute_lua("return scroll.focused_container()")
        print(f"Focused container: {focused}")
        if focused:
            print(
                f"Focused type: {scroll_compositor.execute_lua(f'return scroll.node_get_type({focused})')}"
            )
            print(
                f"Is floating: {scroll_compositor.execute_lua(f'return scroll.container_get_floating({focused})')}"
            )
            parent = scroll_compositor.execute_lua(
                f"return scroll.container_get_parent({focused})"
            )
            print(f"Parent: {parent}")
            if parent:
                print(
                    f"Parent type: {scroll_compositor.execute_lua(f'return scroll.node_get_type({parent})')}"
                )

        # Make it fullscreen
        scroll_compositor.cmd("fullscreen")

        # Move it to workspace 2
        scroll_compositor.cmd("move container to workspace 2")

        # Now workspace 1's fullscreen pointer should be dangling

    # client1 is closed, container is destroyed.
    # Workspace 1's fullscreen pointer is now pointing to freed memory.

    # Switch to workspace 1 to trigger arrange/focus logic which might access it
    scroll_compositor.cmd("workspace 1")

    # Open another window on workspace 1 to force more layout activity
    with wayland_client(scroll_compositor, "client2"):
        wait_for_client_map(scroll_compositor, "client2")
        scroll_compositor.cmd("nop")
