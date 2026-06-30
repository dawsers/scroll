import os
from pathlib import Path
import subprocess
from conftest import ScrollInstance
import pytest
from test_utils import wait_for_client_map


def test_wayland_client(scroll_compositor: ScrollInstance) -> None:
    wayland_display: str | None = scroll_compositor.getenv("WAYLAND_DISPLAY")
    assert wayland_display is not None

    client_path: Path = Path("./build/tests/wayland-test-client").resolve()
    assert client_path.exists(), f"Client not found at {client_path}"

    title: str = "My Wayland Test Window"
    app_id: str = "my_wayland_app_id"

    env: dict = os.environ.copy()
    env["WAYLAND_DISPLAY"] = wayland_display

    proc: subprocess.Popen = subprocess.Popen(
        [str(client_path), title, app_id], env=env
    )

    view_id = wait_for_client_map(scroll_compositor, title)
    app_id_actual = scroll_compositor.execute_lua(
        f"return scroll.view_get_app_id({view_id})"
    )
    assert app_id_actual == app_id
    view_info = {"id": view_id}

    scroll_compositor.execute_lua(f"scroll.view_close({view_info['id']})")

    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        pytest.fail("Client did not exit after view_close")

    assert proc.returncode == 0


def test_x11_client(scroll_compositor: ScrollInstance) -> None:
    display: str | None = scroll_compositor.getenv("DISPLAY")
    if not display:
        pytest.skip("Xwayland is not enabled (no DISPLAY env var in compositor)")

    xauthority: str | None = scroll_compositor.getenv("XAUTHORITY")

    # Wait for Xwayland to be ready
    scroll_compositor.wait_for_log_pattern("Xserver is ready", from_start=True)

    client_path: Path = Path("./build/tests/x11-test-client").resolve()
    if not client_path.exists():
        pytest.skip("X11 test client not built")

    title: str = "My X11 Test Window"
    instance: str = "my_x11_instance"
    class_name: str = "MyX11Class"

    env: dict = os.environ.copy()
    env["DISPLAY"] = display
    if xauthority:
        env["XAUTHORITY"] = xauthority

    proc: subprocess.Popen = subprocess.Popen(
        [str(client_path), title, instance, class_name], env=env
    )

    view_id = wait_for_client_map(scroll_compositor, title)
    class_actual = scroll_compositor.execute_lua(
        f"return scroll.view_get_class({view_id})"
    )
    shell_actual = scroll_compositor.execute_lua(
        f"return scroll.view_get_shell({view_id})"
    )
    assert class_actual == class_name
    assert shell_actual == "xwayland"
    view_info = {"id": view_id}

    scroll_compositor.execute_lua(f"scroll.view_close({view_info['id']})")

    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        pytest.fail("X11 client did not exit after view_close")

    assert proc.returncode == 0
