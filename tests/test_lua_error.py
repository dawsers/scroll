import uuid
from contextlib import contextmanager
from pathlib import Path
from typing import Generator
from conftest import ScrollInstance
from test_utils import wayland_client, wait_for_client_map


@contextmanager
def lua_callback(
    scroll_compositor: ScrollInstance, event_name: str, callback_code: str
) -> Generator[str, None, None]:
    cb_key: str = "cb_" + uuid.uuid4().hex

    register_code: str = f"""
    if not _G.test_callbacks then _G.test_callbacks = {{}} end
    _G.test_callbacks["{cb_key}"] = scroll.add_callback("{event_name}", {callback_code}, nil)
    return "{cb_key}"
    """
    res = scroll_compositor.execute_lua(register_code)
    assert res == cb_key

    try:
        yield cb_key
    finally:
        unregister_code: str = f"""
        if _G.test_callbacks and _G.test_callbacks["{cb_key}"] then
            scroll.remove_callback(_G.test_callbacks["{cb_key}"])
            _G.test_callbacks["{cb_key}"] = nil
        end
        """
        scroll_compositor.execute_lua(unregister_code)


def test_workspace_focus_error(scroll_compositor: ScrollInstance) -> None:
    marker: str = "ERR_WORKSPACE_FOCUS"
    with lua_callback(
        scroll_compositor,
        "workspace_focus",
        f"function(ws, data) error('{marker}') end",
    ):
        with scroll_compositor.assert_logs_match(rf"Lua error:.*{marker}"):
            scroll_compositor.cmd("workspace 2")
            scroll_compositor.cmd("workspace 1")

    assert scroll_compositor.proc.poll() is None


def test_workspace_create_error(scroll_compositor: ScrollInstance) -> None:
    marker: str = "ERR_WORKSPACE_CREATE"
    import random

    ws_num: int = random.randint(10, 100)
    with lua_callback(
        scroll_compositor,
        "workspace_create",
        f"function(ws, data) error('{marker}') end",
    ):
        with scroll_compositor.assert_logs_match(rf"Lua error:.*{marker}"):
            scroll_compositor.cmd(f"workspace {ws_num}")
            scroll_compositor.cmd("workspace 1")

    assert scroll_compositor.proc.poll() is None


def test_view_map_error(scroll_compositor: ScrollInstance) -> None:
    marker: str = "ERR_VIEW_MAP"
    with lua_callback(
        scroll_compositor,
        "view_map",
        f"function(con, data) error('{marker}') end",
    ):
        with scroll_compositor.assert_logs_match(rf"Lua error:.*{marker}"):
            with wayland_client(scroll_compositor, "Test Window"):
                wait_for_client_map(scroll_compositor, "Test Window")

    assert scroll_compositor.proc.poll() is None


def test_view_unmap_error(scroll_compositor: ScrollInstance) -> None:
    marker: str = "ERR_VIEW_UNMAP"
    with wayland_client(scroll_compositor, "Test Window") as proc:
        wait_for_client_map(scroll_compositor, "Test Window")
        with lua_callback(
            scroll_compositor,
            "view_unmap",
            f"function(con, data) error('{marker}') end",
        ):
            with scroll_compositor.assert_logs_match(rf"Lua error:.*{marker}"):
                proc.terminate()
                proc.wait(timeout=5)

    assert scroll_compositor.proc.poll() is None


def test_view_focus_error(scroll_compositor: ScrollInstance) -> None:
    marker: str = "ERR_VIEW_FOCUS"
    with wayland_client(scroll_compositor, "Test Window"):
        wait_for_client_map(scroll_compositor, "Test Window")
        scroll_compositor.cmd("workspace 2")

        with lua_callback(
            scroll_compositor,
            "view_focus",
            f"function(con, data) error('{marker}') end",
        ):
            with scroll_compositor.assert_logs_match(rf"Lua error:.*{marker}"):
                scroll_compositor.cmd("workspace 1")

    assert scroll_compositor.proc.poll() is None


def test_view_float_error(scroll_compositor: ScrollInstance) -> None:
    marker: str = "ERR_VIEW_FLOAT"
    with wayland_client(scroll_compositor, "Test Window"):
        wait_for_client_map(scroll_compositor, "Test Window")
        with lua_callback(
            scroll_compositor,
            "view_float",
            f"function(con, data) error('{marker}') end",
        ):
            with scroll_compositor.assert_logs_match(rf"Lua error:.*{marker}"):
                scroll_compositor.cmd("floating toggle")

    assert scroll_compositor.proc.poll() is None


def test_view_urgent_error(scroll_compositor: ScrollInstance) -> None:
    marker: str = "ERR_VIEW_URGENT"
    with wayland_client(scroll_compositor, "Test Window"):
        view_id = wait_for_client_map(scroll_compositor, "Test Window")
        # Switch focus away so the view is not focused (urgent cannot be set on focused view)
        scroll_compositor.cmd("workspace 2")
        try:
            with lua_callback(
                scroll_compositor,
                "view_urgent",
                f"function(con, data) error('{marker}') end",
            ):
                with scroll_compositor.assert_logs_match(rf"Lua error:.*{marker}"):
                    scroll_compositor.execute_lua(
                        f"scroll.view_set_urgent({view_id}, true)"
                    )
        finally:
            scroll_compositor.cmd("workspace 1")

    assert scroll_compositor.proc.poll() is None


def test_ipc_workspace_error(scroll_compositor: ScrollInstance) -> None:
    marker: str = "ERR_IPC_WORKSPACE"
    with lua_callback(
        scroll_compositor,
        "ipc_workspace",
        f"function(old, new, change, data) error('{marker}') end",
    ):
        with scroll_compositor.assert_logs_match(rf"Lua error:.*{marker}"):
            scroll_compositor.cmd("workspace 2")
            scroll_compositor.cmd("workspace 1")

    assert scroll_compositor.proc.poll() is None


def test_ipc_view_error(scroll_compositor: ScrollInstance) -> None:
    marker: str = "ERR_IPC_VIEW"
    with lua_callback(
        scroll_compositor,
        "ipc_view",
        f"function(con, change, data) error('{marker}') end",
    ):
        with scroll_compositor.assert_logs_match(rf"Lua error:.*{marker}"):
            with wayland_client(scroll_compositor, "Test Window"):
                wait_for_client_map(scroll_compositor, "Test Window")

    assert scroll_compositor.proc.poll() is None


def test_command_end_error(scroll_compositor: ScrollInstance) -> None:
    marker: str = "ERR_COMMAND_END"
    with lua_callback(
        scroll_compositor,
        "command_end",
        f"function(cmd, data) error('{marker}') end",
    ):
        with scroll_compositor.assert_logs_match(rf"Lua error:.*{marker}"):
            scroll_compositor.execute_lua('scroll.command(nil, "nop")')

    assert scroll_compositor.proc.poll() is None


def test_lua_load_nonexistent_script(scroll_compositor: ScrollInstance) -> None:
    res = scroll_compositor.cmd("lua /nonexistent/script.lua")
    assert res and not res[0]["success"]
    assert "Error" in res[0]["error"]
    assert scroll_compositor.proc.poll() is None


def test_lua_load_syntax_error(
    scroll_compositor: ScrollInstance, tmp_path: Path
) -> None:
    script: Path = tmp_path / "syntax_error.lua"
    script.write_text("this is not valid lua code")
    res = scroll_compositor.cmd(f"lua {script}")
    assert res and not res[0]["success"]
    assert "Error" in res[0]["error"]
    assert scroll_compositor.proc.poll() is None


def test_lua_runtime_error_top_level(
    scroll_compositor: ScrollInstance, tmp_path: Path
) -> None:
    script: Path = tmp_path / "runtime_error.lua"
    script.write_text("error('top level error')")
    res = scroll_compositor.cmd(f"lua {script}")
    assert res and not res[0]["success"]
    assert "Error" in res[0]["error"]
    assert "top level error" in res[0]["error"]
    assert scroll_compositor.proc.poll() is None


def test_jump_end_error(scroll_compositor: ScrollInstance) -> None:
    marker: str = "ERR_JUMP_END"
    with wayland_client(scroll_compositor, "Test Window"):
        wait_for_client_map(scroll_compositor, "Test Window")
        with lua_callback(
            scroll_compositor,
            "jump_end",
            f"function(con, data) error('{marker}') end",
        ):
            # Enter jump mode
            res = scroll_compositor.cmd("jump")
            assert res and res[0]["success"]

            # Trigger end by clicking
            with scroll_compositor.assert_logs_match(rf"Lua error:.*{marker}"):
                r1 = scroll_compositor.cmd("seat * cursor press button1")
                assert r1 and r1[0]["success"], f"cursor press failed: {r1}"
                r2 = scroll_compositor.cmd("seat * cursor release button1")
                assert r2 and r2[0]["success"], f"cursor release failed: {r2}"

    assert scroll_compositor.proc.poll() is None
