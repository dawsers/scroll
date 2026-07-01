from test_utils import wayland_client, wait_for_client_map, ScrollInstance


def test_space_restore_uaf_crash(scroll_compositor: ScrollInstance) -> None:
    inst = scroll_compositor
    # 1. Open Window 1 on Workspace 1
    with wayland_client(inst, "Window 1"):
        wait_for_client_map(inst, "Window 1")

        # Make Window 1 floating
        res = inst.cmd("floating enable")
        assert res and res[0]["success"], f"floating enable failed: {res}"

        # Save layout "space1" on Workspace 1
        res = inst.cmd("space save space1")
        assert res and res[0]["success"], f"space save failed: {res}"

        # 2. Switch to Workspace 2
        res = inst.cmd("workspace 2")
        assert res and res[0]["success"], f"workspace 2 failed: {res}"

        # Move Window 1 to Workspace 2
        res = inst.cmd("workspace 1")
        assert res and res[0]["success"], f"workspace 1 failed: {res}"

        res = inst.cmd("move container to workspace 2")
        assert res and res[0]["success"], f"move to ws 2 failed: {res}"

        res = inst.cmd("workspace 2")
        assert res and res[0]["success"], f"workspace 2 failed: {res}"

        # Now Window 1 is floating on Workspace 2.
        # We must make it tiled on Workspace 2.
        res = inst.cmd("floating disable")
        assert res and res[0]["success"], f"floating disable failed: {res}"

        # Set mode to vertical to stack next window
        res = inst.cmd("set_mode v")
        assert res and res[0]["success"], f"set_mode v failed: {res}"

        # Open Window 2 on Workspace 2
        with wayland_client(inst, "Window 2"):
            wait_for_client_map(inst, "Window 2")

            # Now we have on Workspace 2: Split (V) -> [Window 1, Window 2]

            # Move Window 2 to Workspace 3 (so Window 1 is only child of V-split)
            res = inst.cmd("move container to workspace 3")
            assert res and res[0]["success"], f"move to ws 3 failed: {res}"

            # Now Window 1 is the only child of V-split on Workspace 2.
            # Workspace 2 has no other windows.

            # 3. Go to Workspace 1
            res = inst.cmd("workspace 1")
            assert res and res[0]["success"], f"workspace 1 failed: {res}"

            # 4. Restore layout "space1"
            res = inst.cmd("space restore space1")
            assert res and res[0]["success"], f"space restore failed: {res}"

            # Check if compositor process is still alive
            assert inst.proc.poll() is None, "Compositor crashed"

            # Clean up space
            inst.cmd("space delete space1")
