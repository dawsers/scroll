import time
from pathlib import Path
from conftest import ScrollInstance
from test_utils import run_compositor


def test_lua_tilde_expansion(scroll_compositor: ScrollInstance) -> None:
    home_dir = scroll_compositor.temp_dir
    script_path = home_dir / "test_tilde.lua"
    script_path.write_text('scroll.log("TILDE_TEST_SUCCESS")')

    with scroll_compositor.assert_logs_match("TILDE_TEST_SUCCESS"):
        res = scroll_compositor.cmd("lua ~/test_tilde.lua")
        assert res[0]["success"]

    # Test non-existent file
    res = scroll_compositor.cmd("lua ~/nonexistent.lua")
    assert not res[0]["success"]
    assert "Error" in res[0].get("error", "")

    # Test multiple matches (should fail)
    script_path2 = home_dir / "test_tilde2.lua"
    script_path2.write_text('scroll.log("TILDE_TEST_SUCCESS2")')

    res = scroll_compositor.cmd("lua ~/test_tilde*.lua")
    assert not res[0]["success"]
    assert "multiple files" in res[0].get("error", "")


def test_lua_relative_path_config_load(
    scroll_compositor: ScrollInstance, tmp_path: Path
) -> None:
    binary_path = scroll_compositor.proc.args[0]
    script_path = tmp_path / "test_relative.lua"
    script_path.write_text('scroll.log("RELATIVE_LOAD_SUCCESS")')

    config = "workspace 1\nxwayland force\nlua test_relative.lua\n"
    with run_compositor(binary_path, tmp_path, config) as inst:
        time.sleep(1.0)
    assert "RELATIVE_LOAD_SUCCESS" in inst.read_log()


def test_lua_relative_path_subdir_config_load(
    scroll_compositor: ScrollInstance, tmp_path: Path
) -> None:
    binary_path = scroll_compositor.proc.args[0]
    subdir = tmp_path / "scripts"
    subdir.mkdir()
    script_path = subdir / "test_relative2.lua"
    script_path.write_text('scroll.log("RELATIVE_SUBDIR_LOAD_SUCCESS")')

    config = "workspace 1\nxwayland force\nlua scripts/test_relative2.lua\n"
    with run_compositor(binary_path, tmp_path, config) as inst:
        time.sleep(1.0)
    assert "RELATIVE_SUBDIR_LOAD_SUCCESS" in inst.read_log()


def test_lua_relative_glob_config_load(
    scroll_compositor: ScrollInstance, tmp_path: Path
) -> None:
    binary_path = scroll_compositor.proc.args[0]
    subdir = tmp_path / "scripts"
    subdir.mkdir()
    script_path = subdir / "test_glob1.lua"
    script_path.write_text('scroll.log("RELATIVE_GLOB_LOAD_SUCCESS")')

    config = "workspace 1\nxwayland force\nlua scripts/test_glob*.lua\n"
    with run_compositor(binary_path, tmp_path, config) as inst:
        time.sleep(1.0)
    assert "RELATIVE_GLOB_LOAD_SUCCESS" in inst.read_log()


def test_lua_relative_glob_multiple_config_load(
    scroll_compositor: ScrollInstance, tmp_path: Path
) -> None:
    binary_path = scroll_compositor.proc.args[0]
    subdir = tmp_path / "scripts"
    subdir.mkdir()
    (subdir / "test_glob1.lua").write_text('scroll.log("G1")')
    (subdir / "test_glob2.lua").write_text('scroll.log("G2")')

    config = "workspace 1\nxwayland force\nlua scripts/test_glob*.lua\n"
    with run_compositor(binary_path, tmp_path, config) as inst:
        time.sleep(1.0)
    assert "Path expanded to multiple files" in inst.read_log()
