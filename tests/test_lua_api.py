from conftest import ScrollInstance


def test_lua_comprehensive_api(scroll_compositor: ScrollInstance) -> None:
    # Get focused workspace
    ws: int = scroll_compositor.execute_lua("return scroll.focused_workspace()")
    assert isinstance(ws, int)

    # Get workspace name
    ws_name: str = scroll_compositor.execute_lua(
        f"return scroll.workspace_get_name({ws})"
    )
    assert ws_name == "1"

    # Get workspace output
    output: int = scroll_compositor.execute_lua(
        f"return scroll.workspace_get_output({ws})"
    )
    assert isinstance(output, int)

    # Get output name
    output_name: str = scroll_compositor.execute_lua(
        f"return scroll.output_get_name({output})"
    )
    assert isinstance(output_name, str)

    # Get output enabled
    output_enabled: bool = scroll_compositor.execute_lua(
        f"return scroll.output_get_enabled({output})"
    )
    assert output_enabled is True

    # Get root outputs
    outputs: list = scroll_compositor.execute_lua("return scroll.root_get_outputs()")
    assert isinstance(outputs, list)
    assert len(outputs) > 0
    assert outputs[0] == output

    # Get output workspaces
    ws_list: list = scroll_compositor.execute_lua(
        f"return scroll.output_get_workspaces({output})"
    )
    assert isinstance(ws_list, list)
    assert len(ws_list) > 0
    assert ws_list[0] == ws

    # Get node types
    ws_type: str = scroll_compositor.execute_lua(f"return scroll.node_get_type({ws})")
    assert ws_type == "workspace"

    output_type: str = scroll_compositor.execute_lua(
        f"return scroll.node_get_type({output})"
    )
    assert output_type == "output"

    # Test invalid IDs
    invalid_ws: int = 999999
    assert (
        scroll_compositor.execute_lua(f"return scroll.node_get_type({invalid_ws})")
        is None
    )
    assert (
        scroll_compositor.execute_lua(f"return scroll.workspace_get_name({invalid_ws})")
        is None
    )
    assert (
        scroll_compositor.execute_lua(
            f"return scroll.workspace_get_output({invalid_ws})"
        )
        is None
    )

    invalid_output: int = 999998
    assert (
        scroll_compositor.execute_lua(
            f"return scroll.output_get_name({invalid_output})"
        )
        is None
    )
    assert (
        scroll_compositor.execute_lua(
            f"return scroll.output_get_enabled({invalid_output})"
        )
        is False
    )

    invalid_output_ws: list = scroll_compositor.execute_lua(
        f"return scroll.output_get_workspaces({invalid_output})"
    )
    assert isinstance(invalid_output_ws, list)
    assert len(invalid_output_ws) == 0

    assert scroll_compositor.proc.poll() is None
