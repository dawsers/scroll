import os
import subprocess
import time
from pathlib import Path
import pytest
from conftest import ScrollInstance


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

    view_info: dict | None = None
    tries: int = 0
    while tries < 50:
        view_info = scroll_compositor.execute_lua("""
            local view = scroll.focused_view()
            if view then
                return {
                    id = view,
                    title = scroll.view_get_title(view),
                    app_id = scroll.view_get_app_id(view)
                }
            end
        """)
        if (
            view_info
            and view_info.get("title") == title
            and view_info.get("app_id") == app_id
        ):
            break

        time.sleep(0.1)
        tries += 1

    assert tries < 50, "Timed out waiting for client to map or verify"
    assert view_info is not None

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
    xwayland_ready_tries: int = 0
    while xwayland_ready_tries < 50:
        if "Xserver is ready" in scroll_compositor.read_log():
            break
        time.sleep(0.1)
        xwayland_ready_tries += 1
    assert xwayland_ready_tries < 50, "Timed out waiting for Xwayland to be ready"

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

    view_info: dict | None = None
    tries: int = 0
    while tries < 50:
        view_info = scroll_compositor.execute_lua("""
            local view = scroll.focused_view()
            if view then
                return {
                    id = view,
                    title = scroll.view_get_title(view),
                    class = scroll.view_get_class(view),
                    shell = scroll.view_get_shell(view)
                }
            end
        """)
        if (
            view_info
            and view_info.get("title") == title
            and view_info.get("class") == class_name
        ):
            assert view_info.get("shell") == "xwayland"
            break

        time.sleep(0.1)
        tries += 1

    if tries >= 50:
        Path("build/test_x11_compositor.log").write_text(scroll_compositor.read_log())
        print("Wrote compositor log to build/test_x11_compositor.log")
    assert tries < 50, "Timed out waiting for X11 client to map or verify"
    assert view_info is not None

    scroll_compositor.execute_lua(f"scroll.view_close({view_info['id']})")

    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        pytest.fail("X11 client did not exit after view_close")

    assert proc.returncode == 0
