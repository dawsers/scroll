from conftest import ScrollInstance
from test_utils import wayland_client, wait_for_client_map


def test_space_restore_uaf_crash(fresh_compositor: ScrollInstance) -> None:
    # 1. Open Window 1 on Workspace 1
    with wayland_client(fresh_compositor, "Window 1"):
        wait_for_client_map(fresh_compositor, "Window 1")

        # Make Window 1 floating
        res = fresh_compositor.cmd("floating enable")
        assert res and res[0]["success"], f"floating enable failed: {res}"

        # Save layout "space1" on Workspace 1
        res = fresh_compositor.cmd("space save space1")
        assert res and res[0]["success"], f"space save failed: {res}"

        # 2. Switch to Workspace 2
        res = fresh_compositor.cmd("workspace 2")
        assert res and res[0]["success"], f"workspace 2 failed: {res}"

        # Move Window 1 to Workspace 2
        # (It should still be floating? Yes, moving floating window to workspace works)
        # Actually, we can just move it.
        # Wait, if we are on Workspace 2, we can't easily move it here unless we focus it.
        # But we switched to Workspace 2, so focus is on Workspace 2 (empty).
        # We should go back to Workspace 1, move it to Workspace 2, then go to Workspace 2.
        res = fresh_compositor.cmd("workspace 1")
        assert res and res[0]["success"], f"workspace 1 failed: {res}"

        res = fresh_compositor.cmd("move container to workspace 2")
        assert res and res[0]["success"], f"move to ws 2 failed: {res}"

        res = fresh_compositor.cmd("workspace 2")
        assert res and res[0]["success"], f"workspace 2 failed: {res}"

        # Now Window 1 is floating on Workspace 2.
        # We must make it tiled on Workspace 2.
        res = fresh_compositor.cmd("floating disable")
        assert res and res[0]["success"], f"floating disable failed: {res}"

        # Set mode to vertical to stack next window
        res = fresh_compositor.cmd("set_mode v")
        assert res and res[0]["success"], f"set_mode v failed: {res}"

        # Open Window 2 on Workspace 2
        with wayland_client(fresh_compositor, "Window 2"):
            wait_for_client_map(fresh_compositor, "Window 2")

            # Now we have on Workspace 2: Split (V) -> [Window 1, Window 2]

            # Move Window 2 to Workspace 3 (so Window 1 is only child of V-split)
            res = fresh_compositor.cmd("move container to workspace 3")
            assert res and res[0]["success"], f"move to ws 3 failed: {res}"

            # Now Window 1 is the only child of V-split on Workspace 2.
            # Workspace 2 has no other windows.

            # 3. Go to Workspace 1
            res = fresh_compositor.cmd("workspace 1")
            assert res and res[0]["success"], f"workspace 1 failed: {res}"

            # 4. Restore layout "space1"
            # This should try to restore Window 1 as floating on Workspace 1.
            # It will detach Window 1 from Workspace 2, reaping V-split and destroying Workspace 2.
            # Then it will call arrange_container with dangling Workspace 2 pointer.
            try:
                res = fresh_compositor.cmd("space restore space1")
                assert res and res[0]["success"], f"space restore failed: {res}"
            except Exception as e:
                print(f"Compositor log:\n{fresh_compositor.read_log()}")
                raise e

            # Check if compositor process is still alive
            log_content = fresh_compositor.read_log()
            print(f"Compositor log:\n{log_content}")
            if (
                fresh_compositor.proc.poll() is not None
                or "node_table not initialized" in log_content
            ):
                pass

            assert fresh_compositor.proc.poll() is None, "Compositor crashed"
