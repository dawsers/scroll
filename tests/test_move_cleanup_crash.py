from test_utils import wayland_client, wait_for_client_map, ScrollInstance


def test_move_cleanup_uaf_crash(scroll_compositor: ScrollInstance) -> None:
    # 1. Open Window 1 on Workspace 1
    with wayland_client(scroll_compositor, "Window 1"):
        wait_for_client_map(scroll_compositor, "Window 1")

        # Make Window 1 floating
        res = scroll_compositor.cmd("floating enable")
        assert res and res[0]["success"], f"floating enable failed: {res}"

        # 2. Switch to Workspace 3 (so Workspace 1 becomes inactive)
        res = scroll_compositor.cmd("workspace 3")
        assert res and res[0]["success"], f"workspace 3 failed: {res}"

        scroll_compositor.wait_for_idle()

        # 3. Use criteria to move Window 1 from Workspace 1 to Workspace 2.
        # This should make Workspace 1 empty and inactive, so it gets destroyed.
        # Then the move command cleanup code should UAF on Workspace 1.
        res = scroll_compositor.cmd('[title="Window 1"] move container to workspace 2')
        assert res and res[0]["success"], f"move failed: {res}"

        # Check if compositor process is still alive
        assert scroll_compositor.proc.poll() is None, "Compositor crashed"
