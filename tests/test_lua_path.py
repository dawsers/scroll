from test_utils import ScrollInstance, ScrollCompositorFactory


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
    scroll_compositor_factory: ScrollCompositorFactory,
) -> None:
    with scroll_compositor_factory() as scroll_compositor:
        home_dir = scroll_compositor.temp_dir
        script_path = home_dir / "test_relative.lua"
        script_path.write_text('scroll.log("RELATIVE_LOAD_SUCCESS")')

        config = "workspace 1\nxwayland force\nlua test_relative.lua\n"
        with scroll_compositor.assert_logs_match("RELATIVE_LOAD_SUCCESS"):
            scroll_compositor.reload_config(config)


def test_lua_relative_path_subdir_config_load(
    scroll_compositor_factory: ScrollCompositorFactory,
) -> None:
    with scroll_compositor_factory() as scroll_compositor:
        home_dir = scroll_compositor.temp_dir
        subdir = home_dir / "scripts"
        subdir.mkdir(exist_ok=True)
        script_path = subdir / "test_relative2.lua"
        script_path.write_text('scroll.log("RELATIVE_SUBDIR_LOAD_SUCCESS")')

        config = "workspace 1\nxwayland force\nlua scripts/test_relative2.lua\n"
        with scroll_compositor.assert_logs_match("RELATIVE_SUBDIR_LOAD_SUCCESS"):
            scroll_compositor.reload_config(config)


def test_lua_relative_glob_config_load(
    scroll_compositor_factory: ScrollCompositorFactory,
) -> None:
    with scroll_compositor_factory() as scroll_compositor:
        home_dir = scroll_compositor.temp_dir
        subdir = home_dir / "scripts"
        subdir.mkdir(exist_ok=True)
        script_path = subdir / "test_glob1.lua"
        script_path.write_text('scroll.log("RELATIVE_GLOB_LOAD_SUCCESS")')

        config = "workspace 1\nxwayland force\nlua scripts/test_glob*.lua\n"
        with scroll_compositor.assert_logs_match("RELATIVE_GLOB_LOAD_SUCCESS"):
            scroll_compositor.reload_config(config)


def test_lua_relative_glob_multiple_config_load(
    scroll_compositor_factory: ScrollCompositorFactory,
) -> None:
    with scroll_compositor_factory() as scroll_compositor:
        home_dir = scroll_compositor.temp_dir
        subdir = home_dir / "scripts"
        subdir.mkdir(exist_ok=True)
        (subdir / "test_glob1.lua").write_text('scroll.log("G1")')
        (subdir / "test_glob2.lua").write_text('scroll.log("G2")')

        config = "workspace 1\nxwayland force\nlua scripts/test_glob*.lua\n"
        with scroll_compositor.assert_logs_match("Path expanded to multiple files"):
            scroll_compositor.reload_config(config)
