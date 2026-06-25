import time
from conftest import ScrollInstance
from test_utils import wayland_client, wait_for_client_map


def test_workspace_split_uaf_crash(fresh_compositor: ScrollInstance) -> None:
    try:
        # 1. Open Window 1 on Workspace 1 (active on HEADLESS-1)
        with wayland_client(fresh_compositor, "Window 1"):
            wait_for_client_map(fresh_compositor, "Window 1")

            # 2. Split workspace 1. This creates workspace 2 as sibling.
            # Workspace 1 has Window 1, Workspace 2 is empty.
            res = fresh_compositor.cmd("workspace split")
            assert res and res[0]["success"], f"workspace split failed: {res}"

            # 3. Create a second output HEADLESS-2.
            # It should get a default workspace (probably 3).
            res = fresh_compositor.cmd("create_output")
            assert res and res[0]["success"], f"create_output failed: {res}"

            time.sleep(0.5)

            # 4. Unplug HEADLESS-1.
            # Workspaces 1 and 2 should be evacuated.
            # Workspace 1 (non-empty) is moved to HEADLESS-2.
            # Workspace 2 (empty) is destroyed.
            res = fresh_compositor.cmd("output HEADLESS-1 unplug")
            assert res and res[0]["success"], f"unplug failed: {res}"

            time.sleep(0.5)

            # 5. Focus Workspace 3 on HEADLESS-2 (so Workspace 1 becomes inactive)
            res = fresh_compositor.cmd("workspace 3")
            assert res and res[0]["success"], f"workspace 3 failed: {res}"

            time.sleep(0.5)

            # 6. Move Window 1 from Workspace 1 to Workspace 3.
            # Since Window 1 is moved out of Workspace 1, and Workspace 1 is inactive,
            # it should trigger workspace_consider_destroy(Workspace 1).
            # Workspace 1 is empty, and it is split (sibling was Workspace 2).
            # It will try to access Workspace 2 (which is destroyed) -> UAF.
            res = fresh_compositor.cmd(
                '[title="Window 1"] move container to workspace 3'
            )
            assert res and res[0]["success"], f"move container failed: {res}"

            # Check if compositor process is still alive
            log_content = fresh_compositor.read_log()
            print(f"Compositor log:\n{log_content}")
            assert fresh_compositor.proc.poll() is None, "Compositor crashed"
    except Exception as e:
        print(f"Test failed with exception: {e}")
        try:
            print(f"Compositor log:\n{fresh_compositor.read_log()}")
        except Exception as log_err:
            print(f"Failed to read compositor log: {log_err}")
        raise e
