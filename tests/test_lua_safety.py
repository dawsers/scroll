from conftest import ScrollInstance


def test_lua_use_after_free_prevented(scroll_compositor: ScrollInstance) -> None:
    # 1. Get current workspace (workspace 1)
    ws1: int = scroll_compositor.execute_lua("return scroll.focused_workspace()")

    # 2. Create and switch to workspace 2
    scroll_compositor.execute_lua('scroll.command(nil, "workspace 2")')
    ws2: int = scroll_compositor.execute_lua("return scroll.focused_workspace()")
    assert ws1 != ws2

    # 3. Switch back to workspace 1 (workspace 2 should be destroyed if empty)
    scroll_compositor.execute_lua('scroll.command(nil, "workspace 1")')

    # 4. Now try to get name of ws2. It should return nil (None in Python), but NOT crash.
    ws2_name = scroll_compositor.execute_lua(f"return scroll.workspace_get_name({ws2})")
    assert ws2_name is None

    # 5. Also try with invalid container ID
    invalid_con_title = scroll_compositor.execute_lua(
        "return scroll.view_get_title(999999)"
    )
    assert invalid_con_title is None

    # Verify it didn't crash
    assert scroll_compositor.proc.poll() is None
