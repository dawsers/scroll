from conftest import ScrollInstance
from test_utils import wayland_client, wait_for_client_map


def test_static_geometry(scroll_compositor: ScrollInstance) -> None:
    inst = scroll_compositor
    with wayland_client(inst, "client1"):
        view_id = wait_for_client_map(inst, "client1")
        con_id = inst.execute_lua(f"return scroll.view_get_container({view_id})")
        assert con_id is not None

        # Wait for any map animation to settle
        inst.wait_for_idle()

        geom = inst.execute_lua(f"return scroll.container_get_geometry({con_id})")
        actual_geom = inst.execute_lua(
            f"return scroll.container_get_animated_geometry({con_id})"
        )

        print("Static Geometry:", geom)
        print("Static Actual Geometry:", actual_geom)

        tree = inst.get_tree()
        print("Tree:", tree)

        assert geom == actual_geom
        assert "x" in geom
        assert "y" in geom
        assert "width" in geom
        assert "height" in geom

        # Check if they match tree rect

        def find_node(node, target_id):
            if node.get("id") == target_id:
                return node
            for child in node.get("nodes", []):
                n = find_node(child, target_id)
                if n:
                    return n
            for child in node.get("floating_nodes", []):
                n = find_node(child, target_id)
                if n:
                    return n
            return None

        target_node = find_node(tree, con_id)
        assert target_node is not None
        assert "surface_content_rect" in target_node
        view_geom = target_node["surface_content_rect"]
        assert "x" in view_geom
        assert "y" in view_geom
        assert "width" in view_geom
        assert "height" in view_geom

        rect = target_node["rect"]
        deco_rect = target_node.get("deco_rect", {"height": 0})

        expected_y = rect["y"]
        expected_height = rect["height"]
        # If the container has a titlebar, the 'rect' in get_tree excludes it.
        # But container_get_geometry includes it.
        if deco_rect.get("height", 0) > 0:
            expected_y -= deco_rect["height"]
            expected_height += deco_rect["height"]

        assert geom["x"] == rect["x"]
        assert geom["y"] == expected_y
        assert geom["width"] == rect["width"]
        assert geom["height"] == expected_height


def test_invalid_geometry(scroll_compositor: ScrollInstance) -> None:
    inst = scroll_compositor
    # Invalid ID
    geom = inst.execute_lua("return scroll.container_get_geometry(99999)")
    animated_geom = inst.execute_lua(
        "return scroll.container_get_animated_geometry(99999)"
    )
    assert geom is None
    assert animated_geom is None

    # No arguments
    geom_no_args = inst.execute_lua("return scroll.container_get_geometry()")
    animated_geom_no_args = inst.execute_lua(
        "return scroll.container_get_animated_geometry()"
    )
    assert geom_no_args is None
    assert animated_geom_no_args is None
