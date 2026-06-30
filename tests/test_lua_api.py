from conftest import ScrollInstance
from test_utils import wayland_client, wait_for_client_map


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


def test_lua_context_container(scroll_compositor: ScrollInstance) -> None:
    # 1. Without criteria, context_container should return nil (None in Python)
    ctx_con_glob: int | None = scroll_compositor.execute_lua(
        "return scroll.context_container()"
    )
    assert ctx_con_glob is None

    # 2. With criteria, context_container should return the matching container ID
    with wayland_client(scroll_compositor, "client1"):
        view_id = wait_for_client_map(scroll_compositor, "client1")
        con_id = scroll_compositor.execute_lua(
            f"return scroll.view_get_container({view_id})"
        )
        assert con_id is not None

        # Execute lua command with criteria matching client1
        scroll_compositor.cmd(
            f'[con_id={con_id}] lua_eval "_G.ctx_con_crit = scroll.context_container()"'
        )
        ctx_con_crit = scroll_compositor.execute_lua("return _G.ctx_con_crit")
        assert ctx_con_crit == con_id

        # 3. Nested calls with different contexts
        with wayland_client(scroll_compositor, "client2"):
            wait_for_client_map(scroll_compositor, "client2")
            con2_id = scroll_compositor.execute_lua("return scroll.focused_container()")
            assert con2_id is not None
            assert con2_id != con_id

            # Execute outer script with con_id criteria matching con_id
            # Using lua_eval for both outer and nested calls!
            scroll_compositor.cmd(
                f'[con_id={con_id}] lua_eval "'
                f"_G.nested_context = nil; "
                f"_G.outer_context = scroll.context_container(); "
                f"scroll.command({con2_id}, [[lua_eval '_G.nested_context = scroll.context_container()']]); "
                f'_G.outer_context_post = scroll.context_container()"'
            )

            # Retrieve results
            outer_context = scroll_compositor.execute_lua("return _G.outer_context")
            nested_context = scroll_compositor.execute_lua("return _G.nested_context")
            outer_context_post = scroll_compositor.execute_lua(
                "return _G.outer_context_post"
            )

            assert outer_context == con_id
            assert nested_context == con2_id
            assert outer_context_post == con_id
