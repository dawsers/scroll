import json
from test_utils import wayland_client, wait_for_client_map, ScrollInstance


def test_focused_inactive_null_crash(fresh_compositor: ScrollInstance) -> None:
    # 1. Set mode to vertical and insert position to before
    fresh_compositor.cmd("set_mode v before")

    # 2. Create first view (client1)
    with wayland_client(fresh_compositor, "client1"):
        wait_for_client_map(fresh_compositor, "client1")
        w1_id = fresh_compositor.execute_lua("return scroll.focused_container()")
        print(f"w1_id: {w1_id}")

        # 3. Create second view (client2)
        with wayland_client(fresh_compositor, "client2"):
            wait_for_client_map(fresh_compositor, "client2")
            w2_id = fresh_compositor.execute_lua("return scroll.focused_container()")
            print(f"w2_id: {w2_id}")

            # Verify they are siblings (same parent column)
            w1_parent = fresh_compositor.execute_lua(
                f"return scroll.container_get_parent({w1_id})"
            )
            w2_parent = fresh_compositor.execute_lua(
                f"return scroll.container_get_parent({w2_id})"
            )
            assert w1_parent == w2_parent, "w1 and w2 should be siblings"
            col_id = w1_parent

            # 4. Focus w1 to make it the focused_inactive_child of the parent column
            fresh_compositor.cmd(f"[con_id={w1_id}] focus")

            # 5. Move w1 to workspace 2 (this detaches it, making col's focused_inactive_child NULL)
            fresh_compositor.cmd(f"[con_id={w1_id}] move container to workspace 2")
            fresh_compositor.wait_for_idle()

            # 6. Focus the parent column
            print(f"col_id: {col_id}")
            fresh_compositor.cmd(f"[con_id={col_id}] focus")
            focused = fresh_compositor.execute_lua("return scroll.focused_container()")
            print(f"focused after focusing col_id: {focused}")

            # 7. Create third view (client3) on workspace 1
            # This should trigger layout_container_add_view on the focused parent column (col_id)
            # with active = col_id (where focused_inactive_child is NULL).
            # This should crash/corrupt memory if index -1 is returned.
            with wayland_client(fresh_compositor, "client3"):
                wait_for_client_map(fresh_compositor, "client3")
                w3_id = fresh_compositor.execute_lua(
                    "return scroll.focused_container()"
                )
                print(f"w3_id: {w3_id}")

                tree = fresh_compositor.get_tree()
                print("Tree layout inside client3 context:")
                print(json.dumps(tree, indent=2))

            # Verify we survived and client3 is mapped
            assert w3_id is not None

            # 8. Exit cleanly and assert returncode is 0 (ASan check)
            try:
                fresh_compositor.cmd("exit")
            except (EOFError, ConnectionError):
                pass
            ret = fresh_compositor.proc.wait(timeout=5)
            print(f"Compositor exit code: {ret}")
            assert ret == 0, f"Compositor crashed or exited with error code {ret}"
