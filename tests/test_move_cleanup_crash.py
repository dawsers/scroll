import time
from conftest import ScrollInstance
from test_utils import wayland_client, wait_for_client_map


def test_move_cleanup_uaf_crash(fresh_compositor: ScrollInstance) -> None:
    # 1. Open Window 1 on Workspace 1
    with wayland_client(fresh_compositor, "Window 1"):
        wait_for_client_map(fresh_compositor, "Window 1")

        # Make Window 1 floating
        res = fresh_compositor.cmd("floating enable")
        assert res and res[0]["success"], f"floating enable failed: {res}"

        # 2. Switch to Workspace 3 (so Workspace 1 becomes inactive)
        res = fresh_compositor.cmd("workspace 3")
        assert res and res[0]["success"], f"workspace 3 failed: {res}"

        time.sleep(0.5)

        # 3. Use criteria to move Window 1 from Workspace 1 to Workspace 2.
        # This should make Workspace 1 empty and inactive, so it gets destroyed.
        # Then the move command cleanup code should UAF on Workspace 1.
        try:
            res = fresh_compositor.cmd(
                '[title="Window 1"] move container to workspace 2'
            )
            assert res and res[0]["success"], f"move failed: {res}"
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
