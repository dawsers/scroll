import os
import subprocess
import time
from pathlib import Path
from typing import Any, Dict, Optional
import pytest
from conftest import ScrollInstance
from test_utils import wait_for_client_map


def find_node(node: Dict[str, Any], title: str) -> Optional[Dict[str, Any]]:
    if node.get("name") == title:
        return node
    for child in node.get("nodes", []):
        res = find_node(child, title)
        if res:
            return res
    for child in node.get("floating_nodes", []):
        res = find_node(child, title)
        if res:
            return res
    return None


def test_activation_token(scroll_compositor: ScrollInstance, tmp_path: Path) -> None:
    client_path: Path = Path("./build/tests/wayland-test-client").resolve()
    assert client_path.exists(), f"Client not found at {client_path}"

    token_file: Path = tmp_path / "token.txt"
    title: str = "Activation Token Test"
    app_id: str = "act_token_test"

    # Launch sh which writes token to file and exits immediately
    cmd_line: str = f"exec sh -c 'echo $XDG_ACTIVATION_TOKEN > {token_file}'"
    res: list = scroll_compositor.cmd(cmd_line)
    assert res[0]["success"], f"exec failed: {res}"

    # Wait for token file to be written
    start_time: float = time.time()
    while not token_file.exists():
        if time.time() - start_time > 5:
            pytest.fail("Timeout waiting for token file")
        time.sleep(0.1)

    token: str = token_file.read_text().strip()
    assert token, "Token is empty"

    # Register for_window rule with initial_activation_token
    cmd_line2: str = f'for_window [initial_activation_token="{token}"] floating enable'
    res2: list = scroll_compositor.cmd(cmd_line2)
    assert res2[0]["success"], f"for_window failed: {res2}"

    # Launch client directly from python with token in env
    wayland_display: Optional[str] = scroll_compositor.getenv("WAYLAND_DISPLAY")
    assert wayland_display is not None

    env: Dict[str, str] = os.environ.copy()
    env["WAYLAND_DISPLAY"] = wayland_display
    env["XDG_ACTIVATION_TOKEN"] = token

    proc: Optional[subprocess.Popen] = None
    try:
        proc = subprocess.Popen([str(client_path), title, app_id], env=env)

        # Wait for client to map
        view_id: int = wait_for_client_map(scroll_compositor, title)

        # Verify it is floating
        is_floating: bool = scroll_compositor.execute_lua(f"""
            local view = {view_id}
            local container = scroll.view_get_container(view)
            return scroll.container_get_floating(container)
        """)
        assert is_floating is True

        # Verify initial_activation_token is exposed in IPC (get_tree)
        tree: dict = scroll_compositor.get_tree()

        node: Optional[Dict[str, Any]] = find_node(tree, title)
        assert node is not None, f"Node '{title}' not found in tree"
        assert node.get("initial_activation_token") == token, (
            f"Expected token {token}, got {node.get('initial_activation_token')}"
        )

        # Clean up
        scroll_compositor.execute_lua(f"scroll.view_close({view_id})")
    finally:
        if proc:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill()


def test_activation_token_invalid_combination(
    scroll_compositor: ScrollInstance,
) -> None:
    # Try to combine initial_activation_token with class
    cmd_line = 'for_window [initial_activation_token="test-token" class="Kitty"] floating enable'
    res = scroll_compositor.cmd(cmd_line)
    assert not res[0]["success"]
    assert (
        "initial_activation_token criteria cannot be combined with other criteria"
        in res[0]["error"]
    )


def test_ipc_mint_activation_token(
    scroll_compositor: ScrollInstance,
) -> None:
    client_path: Path = Path("./build/tests/wayland-test-client").resolve()
    assert client_path.exists(), f"Client not found at {client_path}"

    title: str = "IPC Activation Token Test"
    app_id: str = "ipc_act_token_test"

    # Mint token via IPC
    res = scroll_compositor.ipc.mint_activation_token()
    assert res["success"]
    token = res["token"]
    assert token

    # Register for_window rule with initial_activation_token
    cmd_line = f'for_window [initial_activation_token="{token}"] floating enable'
    res2 = scroll_compositor.cmd(cmd_line)
    assert res2[0]["success"], f"for_window failed: {res2}"

    # Launch client directly from python with token in env
    wayland_display = scroll_compositor.getenv("WAYLAND_DISPLAY")
    assert wayland_display is not None

    env = os.environ.copy()
    env["WAYLAND_DISPLAY"] = wayland_display
    env["XDG_ACTIVATION_TOKEN"] = token

    proc = None
    try:
        proc = subprocess.Popen([str(client_path), title, app_id], env=env)

        # Wait for client to map
        view_id = wait_for_client_map(scroll_compositor, title)

        # Verify it is floating
        is_floating = scroll_compositor.execute_lua(f"""
            local view = {view_id}
            local container = scroll.view_get_container(view)
            return scroll.container_get_floating(container)
        """)
        assert is_floating is True

        # Verify initial_activation_token is exposed in IPC (get_tree)
        tree = scroll_compositor.get_tree()

        node = find_node(tree, title)
        assert node is not None, f"Node '{title}' not found in tree"
        assert node.get("initial_activation_token") == token, (
            f"Expected token {token}, got {node.get('initial_activation_token')}"
        )

        # Clean up
        scroll_compositor.execute_lua(f"scroll.view_close({view_id})")
    finally:
        if proc:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill()


def test_scrollmsg_mint_activation_token(scroll_compositor: ScrollInstance) -> None:
    socket_path = scroll_compositor.ipc.socket_path
    scrollmsg_bin = "./build/swaymsg/scrollmsg"
    if not Path(scrollmsg_bin).exists():
        scrollmsg_bin = "scrollmsg"
    res = subprocess.run(
        [scrollmsg_bin, "-s", socket_path, "-t", "mint_activation_token"],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0
    import json

    data = json.loads(res.stdout)
    assert data["success"] is True
    assert "token" in data
    assert data["token"]
