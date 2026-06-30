import json
import os
import subprocess
import tempfile
from conftest import ScrollInstance


def test_lua_eval_command(scroll_compositor: ScrollInstance) -> None:
    # Test successful inline execution
    res: list = scroll_compositor.cmd("lua_eval \"scroll.command(nil, 'nop')\"")
    assert res[0]["success"] is True

    # Test execution failure with error propagation
    res = scroll_compositor.cmd("lua_eval \"error('my_eval_test_error')\"")
    assert res[0]["success"] is False
    assert "my_eval_test_error" in res[0]["error"]


def test_lua_ipc_eval(scroll_compositor: ScrollInstance) -> None:
    socket_path: str = scroll_compositor.ipc.socket_path

    # Test basic evaluation
    res: subprocess.CompletedProcess = subprocess.run(
        ["scrollmsg", "-s", socket_path, "-t", "lua_eval", "return 1 + 2"],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0
    data: dict = json.loads(res.stdout)
    assert data["success"] is True
    assert data["result"] == 3

    # Test passing number arguments
    res = subprocess.run(
        [
            "scrollmsg",
            "-s",
            socket_path,
            "-t",
            "lua_eval",
            "local args = ...; return args[1] + args[2]",
            "10",
            "20",
        ],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0
    data = json.loads(res.stdout)
    assert data["success"] is True
    assert data["result"] == 30

    # Test passing JSON object argument
    res = subprocess.run(
        [
            "scrollmsg",
            "-s",
            socket_path,
            "-t",
            "lua_eval",
            "local args = ...; return args[1].x",
            '{"x": 42}',
        ],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0
    data = json.loads(res.stdout)
    assert data["success"] is True
    assert data["result"] == 42

    # Test multiple return values
    res = subprocess.run(
        ["scrollmsg", "-s", socket_path, "-t", "lua_eval", "return 1, 2, 'hello'"],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0
    data = json.loads(res.stdout)
    assert data["success"] is True
    assert data["result"] == [1, 2, "hello"]

    # Test error handling
    res = subprocess.run(
        ["scrollmsg", "-s", socket_path, "-t", "lua_eval", "error('ipc_eval_error')"],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 2
    data = json.loads(res.stdout)
    assert data["success"] is False
    assert "ipc_eval_error" in data["error"]


def test_lua_ipc_file(scroll_compositor: ScrollInstance) -> None:
    socket_path: str = scroll_compositor.ipc.socket_path

    # Create a temporary lua file to execute
    with tempfile.NamedTemporaryFile(suffix=".lua", delete=False) as f:
        f.write(b"local args = ...; return (args[1] or 0) * 2")
        temp_file_path: str = f.name

    try:
        res: subprocess.CompletedProcess = subprocess.run(
            ["scrollmsg", "-s", socket_path, "-t", "lua", temp_file_path, "21"],
            capture_output=True,
            text=True,
        )
        assert res.returncode == 0
        data: dict = json.loads(res.stdout)
        assert data["success"] is True
        assert data["result"] == 42
    finally:
        os.unlink(temp_file_path)
