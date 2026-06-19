import time
from conftest import ScrollInstance
from test_utils import wayland_client, wait_for_client_map


def test_space_aba(fresh_compositor: ScrollInstance) -> None:
    # 1. Create client1 on WS 1
    with wayland_client(fresh_compositor, "client1"):
        wait_for_client_map(fresh_compositor, "client1")

        # Save space "sp1"
        fresh_compositor.cmd("space_save sp1")

    # client1 is closed now.
    # Wait for it to be fully destroyed.
    time.sleep(0.2)

    # 2. Create client2 on WS 1.
    # Hopefully it reuses client1's view struct address.
    with wayland_client(fresh_compositor, "client2"):
        wait_for_client_map(fresh_compositor, "client2")

        # Switch to WS 2
        fresh_compositor.cmd("workspace 2")

        # Load space "sp1" on WS 2.
        # If ABA bug occurs, it might find client2 (matching old client1 address)
        # and move it to WS 2.
        fresh_compositor.cmd("space_load sp1 load")

        # Check if client2 is visible on WS 2.
        # If it was moved to WS 2, it should be focused because space_load focuses restored containers.
        focused_title = fresh_compositor.execute_lua("""
            local view = scroll.focused_view()
            return view and scroll.view_get_title(view)
        """)
        print(f"Focused title after space_load: {focused_title}")

        assert focused_title != "client2", (
            "ABA bug: client2 was incorrectly moved to WS 2!"
        )
