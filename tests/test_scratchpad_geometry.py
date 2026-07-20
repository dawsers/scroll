from conftest import ScrollInstance
from test_utils import wait_for_client_map, wayland_client


def test_scratchpad_geometry_rules(scroll_compositor: ScrollInstance) -> None:
    inst = scroll_compositor
    title = "Scratchpad Test"

    # Register for_window rule
    res = inst.cmd(
        f'for_window [title="{title}"] "move scratchpad, resize set 500 px 500 px, move position 100 100"'
    )

    assert res[0]["success"], f"for_window command failed: {res}"

    try:
        with wayland_client(inst, title):
            view_id = wait_for_client_map(inst, title)
            assert view_id is not None

            con_id = inst.execute_lua(f"return scroll.view_get_container({view_id})")
            assert con_id is not None

            # Wait for any animation to settle (though scratchpad hidden windows might not animate)
            inst.wait_for_idle()

            geom = inst.execute_lua(f"return scroll.container_get_geometry({con_id})")
            print("Scratchpad Geometry:", geom)

            # Assert geometry
            assert geom is not None
            assert geom["width"] == 500
            assert geom["height"] == 500
            assert geom["x"] == 100
            assert geom["y"] == 100

            # Clean up
            inst.execute_lua(f"scroll.view_close({view_id})")
    except Exception:
        print("Compositor log:")
        print(inst.read_log())
        raise
