import pytest
import time
from pathlib import Path
from conftest import ScrollInstance
from test_utils import wait_for_client_map


def test_for_exec_window_pid_matching(scroll_compositor: ScrollInstance) -> None:
    client_path: Path = Path("./build/tests/wayland-test-client").resolve()
    assert client_path.exists(), f"Client not found at {client_path}"

    title: str = "PID Matching Test"
    app_id: str = "pid_match_app_id"

    # We use 'env -u XDG_ACTIVATION_TOKEN' to force fallback to PID matching
    # by preventing the client from using the token.
    cmd_line = f"for_exec_window \"floating enable\" env -u XDG_ACTIVATION_TOKEN {client_path} '{title}' '{app_id}'"
    res = scroll_compositor.cmd(cmd_line)
    assert res[0]["success"], f"for_exec_window command failed: {res}"

    view_id = wait_for_client_map(scroll_compositor, title)

    is_floating = scroll_compositor.execute_lua(f"""
        local view = {view_id}
        local container = scroll.view_get_container(view)
        return scroll.container_get_floating(container)
    """)

    try:
        assert is_floating is True
    except AssertionError:
        print("Compositor log:")
        print(scroll_compositor.read_log())
        raise

    scroll_compositor.execute_lua(f"scroll.view_close({view_id})")


def test_for_exec_window_xdg_activation(scroll_compositor: ScrollInstance) -> None:
    client_path: Path = Path("./build/tests/wayland-test-client").resolve()
    assert client_path.exists(), f"Client not found at {client_path}"

    title: str = "XDG Activation Test"
    app_id: str = "xdg_act_app_id"

    # We use 'sh -c "... &"' to double-fork and break PID matching.
    # The client will use XDG activation token from env.
    cmd_line = f'for_exec_window "floating enable" sh -c \'{client_path} "{title}" "{app_id}" &\''
    res = scroll_compositor.cmd(cmd_line)
    assert res[0]["success"], f"for_exec_window command failed: {res}"

    view_id = wait_for_client_map(scroll_compositor, title)

    is_floating = scroll_compositor.execute_lua(f"""
        local view = {view_id}
        local container = scroll.view_get_container(view)
        return scroll.container_get_floating(container)
    """)

    try:
        assert is_floating is True
    except AssertionError:
        print("Compositor log:")
        print(scroll_compositor.read_log())
        raise

    scroll_compositor.execute_lua(f"scroll.view_close({view_id})")


def test_for_exec_window_x11_startup_id(scroll_compositor: ScrollInstance) -> None:
    display: str | None = scroll_compositor.getenv("DISPLAY")
    if not display:
        pytest.skip("Xwayland is not enabled (no DISPLAY env var in compositor)")

    # Wait for Xwayland to be ready
    start_time = time.time()
    while "Xserver is ready" not in scroll_compositor.read_log():
        if time.time() - start_time > 5:
            pytest.fail("Timeout waiting for Xwayland to be ready")
        time.sleep(0.1)

    client_path: Path = Path("./build/tests/x11-test-client").resolve()
    assert client_path.exists(), f"Client not found at {client_path}"

    title: str = "X11 Startup ID Test"
    instance: str = "x11_startup_id_instance"
    class_name: str = "X11StartupIdClass"

    # We use 'sh -c "... &"' to double-fork and break PID matching.
    # The client will use DESKTOP_STARTUP_ID from env.
    cmd_line = f'for_exec_window "floating enable" sh -c \'{client_path} "{title}" "{instance}" "{class_name}" &\''
    res = scroll_compositor.cmd(cmd_line)
    assert res[0]["success"], f"for_exec_window command failed: {res}"

    try:
        view_id = wait_for_client_map(scroll_compositor, title)

        is_floating = scroll_compositor.execute_lua(f"""
            local view = {view_id}
            local container = scroll.view_get_container(view)
            return scroll.container_get_floating(container)
        """)

        assert is_floating is True
    except Exception:
        print("Compositor log:")
        print(scroll_compositor.read_log())
        raise

    scroll_compositor.execute_lua(f"scroll.view_close({view_id})")
