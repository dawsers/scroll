import time
from conftest import ScrollInstance
from test_utils import wayland_client, wait_for_client_map


def test_focused_inactive_uaf(fresh_compositor: ScrollInstance) -> None:
    # 1. Set mode to vertical to stack windows
    fresh_compositor.cmd("set_mode v")

    # 2. Create first view
    with wayland_client(fresh_compositor, "client1") as client1:
        wait_for_client_map(fresh_compositor, "client1")
        w1_id = fresh_compositor.execute_lua("return scroll.focused_container()")
        print(f"w1_id: {w1_id}")

        # 3. Create second view
        with wayland_client(fresh_compositor, "client2"):
            wait_for_client_map(fresh_compositor, "client2")
            w2_id = fresh_compositor.execute_lua("return scroll.focused_container()")
            print(f"w2_id: {w2_id}")

            # Verify they are siblings (same parent)
            w1_parent = fresh_compositor.execute_lua(
                f"return scroll.container_get_parent({w1_id})"
            )
            w2_parent = fresh_compositor.execute_lua(
                f"return scroll.container_get_parent({w2_id})"
            )
            print(f"w1_parent: {w1_parent}, w2_parent: {w2_parent}")
            assert w1_parent == w2_parent, "w1 and w2 should be siblings"

            # Get Col1 ID (parent ID)
            col1_id = fresh_compositor.execute_lua("""
                local outputs = scroll.root_get_outputs()
                local workspaces = scroll.output_get_workspaces(outputs[1])
                local ws1
                for i, ws in ipairs(workspaces) do
                    if scroll.workspace_get_name(ws) == "1" then
                        ws1 = ws
                        break
                    end
                end
                local tiling = scroll.workspace_get_tiling(ws1)
                return tiling[1]
            """)
            print(f"Col1 ID: {col1_id}")
            assert col1_id == w1_parent, "Col1 ID should match parent ID"

            # 4. Focus w1 to make it the focused_inactive_child of the parent
            fresh_compositor.cmd(f"[con_id={w1_id}] focus")
            focused = fresh_compositor.execute_lua("return scroll.focused_container()")
            assert focused == w1_id, (
                f"w1 ({w1_id}) should be focused, but got {focused}"
            )

            # 5. Switch to workspace 2
            fresh_compositor.cmd("workspace 2")

            # 6. Kill w1 (which is on workspace 1)
            fresh_compositor.cmd(f"[con_id={w1_id}] kill")

            # Wait for client1 to exit (destruction complete)
            client1.wait(timeout=5)
            time.sleep(0.1)

            # 7. Switch back to workspace 1.
            fresh_compositor.cmd("workspace 1")

            # 8. Run move command targeted at Col1 (which has dangling focused_inactive_child)
            # This should trigger UAF in container_get_active_view!
            fresh_compositor.cmd(f"[con_id={col1_id}] move left nomode")

            # If we survive, let's verify w2 is still there
            focused = fresh_compositor.execute_lua("return scroll.focused_container()")
            print(f"Focused after move: {focused}")
