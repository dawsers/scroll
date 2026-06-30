from conftest import ScrollInstance


def test_lua_eval_command(scroll_compositor: ScrollInstance) -> None:
    # Test successful inline execution
    res: list = scroll_compositor.cmd("lua_eval \"scroll.command(nil, 'nop')\"")
    assert res[0]["success"] is True

    # Test execution failure with error propagation
    res = scroll_compositor.cmd("lua_eval \"error('my_eval_test_error')\"")
    assert res[0]["success"] is False
    assert "my_eval_test_error" in res[0]["error"]
