from test_utils import wayland_client, wait_for_client_map, ScrollCompositorFactory


def test_workspace_split_uaf_crash(
    scroll_compositor_factory: ScrollCompositorFactory,
) -> None:
    config = "workspace 1\nxwayland force\nanimations enabled off\n"
    with scroll_compositor_factory(config) as scroll_compositor:
        # 1. Open Window 1 on Workspace 1 (active on HEADLESS-1)
        with wayland_client(scroll_compositor, "Window 1"):
            wait_for_client_map(scroll_compositor, "Window 1")

            # 2. Split workspace 1. This creates workspace 2 as sibling.
            # Workspace 1 has Window 1, Workspace 2 is empty.
            res = scroll_compositor.cmd("workspace split")
            assert res and res[0]["success"], f"workspace split failed: {res}"

            # 3. Create a second output HEADLESS-2.
            # It should get a default workspace (probably 3).
            res = scroll_compositor.cmd("create_output")
            assert res and res[0]["success"], f"create_output failed: {res}"
            scroll_compositor.wait_for_idle()

            # 4. Unplug HEADLESS-1.
            # Workspaces 1 and 2 should be evacuated.
            # Workspace 1 (non-empty) is moved to HEADLESS-2.
            # Workspace 2 (empty) is destroyed.
            res = scroll_compositor.cmd("output HEADLESS-1 unplug")
            assert res and res[0]["success"], f"unplug failed: {res}"
            scroll_compositor.wait_for_idle()

            # 5. Focus Workspace 3 on HEADLESS-2 (so Workspace 1 becomes inactive)
            res = scroll_compositor.cmd("workspace 3")
            assert res and res[0]["success"], f"workspace 3 failed: {res}"
            scroll_compositor.wait_for_idle()

            # 6. Move Window 1 from Workspace 1 to Workspace 3.
            # Since Window 1 is moved out of Workspace 1, and Workspace 1 is inactive,
            # it should trigger workspace_consider_destroy(Workspace 1).
            # Workspace 1 is empty, and it is split (sibling was Workspace 2).
            # It will try to access Workspace 2 (which is destroyed) -> UAF.
            res = scroll_compositor.cmd(
                '[title="Window 1"] move container to workspace 3'
            )
            assert res and res[0]["success"], f"move container failed: {res}"

            # Check if compositor process is still alive
            assert scroll_compositor.proc.poll() is None, "Compositor crashed"
