from typing import Any, Dict, Optional
from conftest import ScrollInstance
import pytest
from test_utils import wait_for_client_map, wayland_client


def find_node(
    node: Dict[str, Any], type_name: str, name: Optional[str] = None
) -> Optional[Dict[str, Any]]:
    if node.get("type") == type_name and (name is None or node.get("name") == name):
        return node
    for child in node.get("nodes", []):
        n = find_node(child, type_name, name)
        if n:
            return n
    for child in node.get("floating_nodes", []):
        n = find_node(child, type_name, name)
        if n:
            return n
    return None


def get_workspace_rect(inst: ScrollInstance, name: str = "1") -> Dict[str, int]:
    tree = inst.get_tree()
    ws = find_node(tree, "workspace", name)
    assert ws is not None
    return ws["rect"]


def get_container_geometry(inst: ScrollInstance, con_id: int) -> Dict[str, float]:
    geom = inst.execute_lua(f"return scroll.container_get_geometry({con_id})")
    assert geom is not None
    return geom


def test_default_alignment(scroll_compositor: ScrollInstance) -> None:
    inst = scroll_compositor
    ws_rect = get_workspace_rect(inst)

    with wayland_client(inst, "client1"):
        v1 = wait_for_client_map(inst, "client1")
        c1 = inst.execute_lua(f"return scroll.view_get_container({v1})")
        inst.wait_for_idle()

        geom1 = get_container_geometry(inst, c1)
        # By default, a single window should be centered.
        # Expected x: ws_x + 0.5 * (ws_width - con_width)
        expected_x = ws_rect["x"] + 0.5 * (ws_rect["width"] - geom1["width"])
        assert abs(geom1["x"] - expected_x) < 1.0


def test_horizontal_alignment_left(scroll_compositor: ScrollInstance) -> None:
    inst = scroll_compositor
    ws_rect = get_workspace_rect(inst)

    inst.cmd("set_mode align_horiz_left")

    with wayland_client(inst, "client1"):
        v1 = wait_for_client_map(inst, "client1")
        c1 = inst.execute_lua(f"return scroll.view_get_container({v1})")
        inst.wait_for_idle()

        geom1 = get_container_geometry(inst, c1)
        # Left aligned: x should be ws_x + gaps
        assert geom1["x"] < ws_rect["x"] + 0.1 * ws_rect["width"]


@pytest.fixture(scope="function")
def no_gaps_compositor(
    scroll_compositor: ScrollInstance,
) -> ScrollInstance:
    scroll_compositor.cmd("gaps inner 0")
    scroll_compositor.cmd("gaps outer 0")
    scroll_compositor.cmd("output * layout_default_height 0.5")
    scroll_compositor.wait_for_idle()
    return scroll_compositor


def test_horizontal_alignment_values(
    no_gaps_compositor: ScrollInstance,
) -> None:
    inst = no_gaps_compositor
    try:
        ws_rect = get_workspace_rect(inst)

        with wayland_client(inst, "client1"):
            v1 = wait_for_client_map(inst, "client1")
            c1 = inst.execute_lua(f"return scroll.view_get_container({v1})")
            inst.wait_for_idle()
            geom_center = get_container_geometry(inst, c1)
            expected_center_x = ws_rect["x"] + 0.5 * (
                ws_rect["width"] - geom_center["width"]
            )
            assert abs(geom_center["x"] - expected_center_x) < 1.0

            # Change to left
            inst.cmd("set_mode align_horiz_left")
            inst.wait_for_idle()
            geom_left = get_container_geometry(inst, c1)
            assert abs(geom_left["x"] - ws_rect["x"]) < 1.0

            # Change to right
            inst.cmd("set_mode align_horiz_right")
            inst.wait_for_idle()
            geom_right = get_container_geometry(inst, c1)
            expected_right_x = ws_rect["x"] + ws_rect["width"] - geom_right["width"]
            assert abs(geom_right["x"] - expected_right_x) < 1.0
    except AssertionError as e:
        print("Compositor log:")
        print(inst.read_log())
        raise e


def test_horizontal_policy_initial(no_gaps_compositor: ScrollInstance) -> None:
    inst = no_gaps_compositor
    ws_rect = get_workspace_rect(inst)

    # Set policy to initial, align left
    inst.cmd("set_mode align_horiz_left align_horiz_initial")

    with wayland_client(inst, "client1") as c1_client:
        v1 = wait_for_client_map(inst, "client1")
        c1 = inst.execute_lua(f"return scroll.view_get_container({v1})")
        inst.wait_for_idle()

        geom1 = get_container_geometry(inst, c1)
        # First window should be left-aligned (x = 0)
        assert abs(geom1["x"] - ws_rect["x"]) < 1.0

        with wayland_client(inst, "client2"):
            v2 = wait_for_client_map(inst, "client2")
            c2 = inst.execute_lua(f"return scroll.view_get_container({v2})")
            inst.wait_for_idle()

            geom1_after = get_container_geometry(inst, c1)
            geom2 = get_container_geometry(inst, c2)

            assert geom2["x"] > geom1_after["x"]

            geom2_before_close = geom2

            c1_client.terminate()
            c1_client.wait()
            inst.wait_for_idle()

            geom2_after_close = get_container_geometry(inst, c2)
            assert abs(geom2_after_close["x"] - geom2_before_close["x"]) < 1.0
            # Confirm it did NOT center (centered would be 320)
            assert (
                abs(
                    geom2_after_close["x"]
                    - (
                        ws_rect["x"]
                        + 0.5 * (ws_rect["width"] - geom2_after_close["width"])
                    )
                )
                > 10.0
            )


def test_vertical_alignment_values(no_gaps_compositor: ScrollInstance) -> None:
    inst = no_gaps_compositor
    ws_rect = get_workspace_rect(inst)

    # Change to vertical mode
    inst.cmd("set_mode v")
    inst.wait_for_idle()

    with wayland_client(inst, "client1"):
        v1 = wait_for_client_map(inst, "client1")
        c1 = inst.execute_lua(f"return scroll.view_get_container({v1})")
        inst.wait_for_idle()
        geom_middle = get_container_geometry(inst, c1)
        expected_middle_y = ws_rect["y"] + 0.5 * (
            ws_rect["height"] - geom_middle["height"]
        )
        assert abs(geom_middle["y"] - expected_middle_y) < 1.0

        # Change to top
        inst.cmd("set_mode align_vert_top")
        inst.wait_for_idle()
        geom_top = get_container_geometry(inst, c1)
        assert abs(geom_top["y"] - ws_rect["y"]) < 1.0

        # Change to bottom
        inst.cmd("set_mode align_vert_bottom")
        inst.wait_for_idle()
        geom_bottom = get_container_geometry(inst, c1)
        expected_bottom_y = ws_rect["y"] + ws_rect["height"] - geom_bottom["height"]
        assert abs(geom_bottom["y"] - expected_bottom_y) < 1.0


def test_vertical_policy_initial(no_gaps_compositor: ScrollInstance) -> None:
    inst = no_gaps_compositor
    ws_rect = get_workspace_rect(inst)

    # Change to vertical mode, set policy to initial, align top
    inst.cmd("set_mode v align_vert_top align_vert_initial")
    inst.wait_for_idle()

    with wayland_client(inst, "client1") as c1_client:
        v1 = wait_for_client_map(inst, "client1")
        c1 = inst.execute_lua(f"return scroll.view_get_container({v1})")
        inst.wait_for_idle()

        geom1 = get_container_geometry(inst, c1)
        assert abs(geom1["y"] - ws_rect["y"]) < 1.0

        with wayland_client(inst, "client2"):
            v2 = wait_for_client_map(inst, "client2")
            c2 = inst.execute_lua(f"return scroll.view_get_container({v2})")
            inst.wait_for_idle()

            geom1_after = get_container_geometry(inst, c1)
            geom2 = get_container_geometry(inst, c2)

            assert geom2["y"] > geom1_after["y"]

            geom2_before_close = geom2

            c1_client.terminate()
            c1_client.wait()
            inst.wait_for_idle()

            geom2_after_close = get_container_geometry(inst, c2)
            assert abs(geom2_after_close["y"] - geom2_before_close["y"]) < 1.0
            # Confirm it did NOT center (middle)
            assert (
                abs(
                    geom2_after_close["y"]
                    - (
                        ws_rect["y"]
                        + 0.5 * (ws_rect["height"] - geom2_after_close["height"])
                    )
                )
                > 10.0
            )


def test_lua_api(scroll_compositor: ScrollInstance) -> None:
    inst = scroll_compositor
    ws_id = inst.execute_lua("return scroll.focused_workspace()")

    # Get default mode
    mode = inst.execute_lua(f"return scroll.workspace_get_mode({ws_id})")
    assert mode["align_horiz"] == "center"
    assert mode["align_vert"] == "middle"
    assert mode["align_horiz_policy"] == "if_fits"
    assert mode["align_vert_policy"] == "if_fits"

    # Set via Lua
    inst.execute_lua(f"""
        scroll.workspace_set_mode({ws_id}, {{
            align_horiz = "left",
            align_vert = "top",
            align_horiz_policy = "initial",
            align_vert_policy = "initial"
        }})
    """)

    mode = inst.execute_lua(f"return scroll.workspace_get_mode({ws_id})")
    assert mode["align_horiz"] == "left"
    assert mode["align_vert"] == "top"
    assert mode["align_horiz_policy"] == "initial"
    assert mode["align_vert_policy"] == "initial"


def test_vertical_alignment_in_column(
    no_gaps_compositor: ScrollInstance,
) -> None:
    inst = no_gaps_compositor
    try:
        ws_rect = get_workspace_rect(inst)

        # Set default height to 0.3 to make sure they fit when stacked
        res = inst.cmd("output * layout_default_height 0.3")
        assert res[0]["success"], f"Failed to set layout_default_height: {res}"

        with wayland_client(inst, "client1"):
            v1 = wait_for_client_map(inst, "client1")
            c1 = inst.execute_lua(f"return scroll.view_get_container({v1})")
            inst.wait_for_idle()
            inst.cmd("set_mode v")
            inst.wait_for_idle()

            with wayland_client(inst, "client2"):
                v2 = wait_for_client_map(inst, "client2")
                c2 = inst.execute_lua(f"return scroll.view_get_container({v2})")
                inst.wait_for_idle()
                # They should both be in the same column.
                # Let's verify they share x.
                fraction1 = inst.execute_lua(
                    f"return scroll.container_get_height_fraction({c1})"
                )
                fraction2 = inst.execute_lua(
                    f"return scroll.container_get_height_fraction({c2})"
                )
                print(f"Fractions: {fraction1}, {fraction2}")
                geom1 = get_container_geometry(inst, c1)
                geom2 = get_container_geometry(inst, c2)
                assert abs(geom1["x"] - geom2["x"]) < 1.0

                # Total height of children: geom1["height"] + geom2["height"]
                # They both have height_fraction 0.3.
                h1 = 0.3 * ws_rect["height"]
                h2 = 0.3 * ws_rect["height"]
                total_h = h1 + h2

                # By default, align_vert is 'middle'.
                # So the column should be centered in workspace.
                expected_y1 = ws_rect["y"] + 0.5 * (ws_rect["height"] - total_h)
                expected_y2 = expected_y1 + h1
                assert abs(geom1["y"] - expected_y1) < 1.0
                assert abs(geom2["y"] - expected_y2) < 1.0

                # Change align_vert to top
                inst.cmd("set_mode align_vert_top")
                inst.wait_for_idle()
                geom1_top = get_container_geometry(inst, c1)
                geom2_top = get_container_geometry(inst, c2)
                # Should be at top: geom1 y = ws_y, geom2 y = ws_y + h1
                assert abs(geom1_top["y"] - ws_rect["y"]) < 1.0
                assert abs(geom2_top["y"] - (ws_rect["y"] + h1)) < 1.0

                # Change align_vert to bottom
                inst.cmd("set_mode align_vert_bottom")
                inst.wait_for_idle()
                geom1_bottom = get_container_geometry(inst, c1)
                geom2_bottom = get_container_geometry(inst, c2)
                # Should be at bottom: geom1 y = ws_y + ws_height - total_h, geom2 y = geom1_y + h1
                expected_bottom_y1 = ws_rect["y"] + ws_rect["height"] - total_h
                expected_bottom_y2 = expected_bottom_y1 + h1
                assert abs(geom1_bottom["y"] - expected_bottom_y1) < 1.0
                assert abs(geom2_bottom["y"] - expected_bottom_y2) < 1.0
    except AssertionError as e:
        print("Compositor log:")
        print(inst.read_log())
        raise e


def test_shifting_bug(no_gaps_compositor: ScrollInstance) -> None:
    inst = no_gaps_compositor
    try:
        ws_rect = get_workspace_rect(inst)

        # Set align_horiz to left and policy to initial, and enable gaps
        inst.cmd("set_mode align_horiz_left align_horiz_initial")
        inst.cmd("output * layout_default_width 0.3")
        inst.cmd("gaps inner all set 10")
        inst.wait_for_idle()

        gaps = 10

        with wayland_client(inst, "client1"):
            v1 = wait_for_client_map(inst, "client1")
            c1 = inst.execute_lua(f"return scroll.view_get_container({v1})")
            inst.wait_for_idle()

            geom1 = get_container_geometry(inst, c1)
            print(f"DEBUG: W1 initial: {geom1}")
            # W1 should be at left edge + gaps
            assert abs(geom1["x"] - (ws_rect["x"] + gaps)) < 1.0

            with wayland_client(inst, "client2") as c2_client:
                v2 = wait_for_client_map(inst, "client2")
                c2 = inst.execute_lua(f"return scroll.view_get_container({v2})")
                inst.wait_for_idle()

                geom1_after_open = get_container_geometry(inst, c1)
                geom2 = get_container_geometry(inst, c2)
                print(f"DEBUG: W1 after open W2: {geom1_after_open}, W2: {geom2}")
                # W1 should STILL be at left edge + gaps
                assert abs(geom1_after_open["x"] - (ws_rect["x"] + gaps)) < 1.0, (
                    f"W1 shifted to {geom1_after_open['x']} after opening W2"
                )

                # Close W2
                c2_client.terminate()
                c2_client.wait()
                inst.wait_for_idle()

                geom1_after_close = get_container_geometry(inst, c1)
                print(f"DEBUG: W1 after close W2: {geom1_after_close}")
                # W1 should STILL be at left edge + gaps
                assert abs(geom1_after_close["x"] - (ws_rect["x"] + gaps)) < 1.0, (
                    f"W1 shifted to {geom1_after_close['x']} after closing W2"
                )

                # Open W3
                with wayland_client(inst, "client3"):
                    v3 = wait_for_client_map(inst, "client3")
                    c3 = inst.execute_lua(f"return scroll.view_get_container({v3})")
                    inst.wait_for_idle()

                    geom1_after_open3 = get_container_geometry(inst, c1)
                    geom3 = get_container_geometry(inst, c3)
                    print(f"DEBUG: W1 after open W3: {geom1_after_open3}, W3: {geom3}")
                    # W1 should STILL be at left edge + gaps
                    assert abs(geom1_after_open3["x"] - (ws_rect["x"] + gaps)) < 1.0, (
                        f"W1 shifted to {geom1_after_open3['x']} after opening W3"
                    )
    except AssertionError as e:
        print("Compositor log:")
        print(inst.read_log())
        raise e


def test_initial_policy_offscreen(no_gaps_compositor: ScrollInstance) -> None:
    inst = no_gaps_compositor
    ws_rect = get_workspace_rect(inst)

    # Set policy to initial, align left, default width 0.4 (so they fit with space)
    inst.cmd("set_mode align_horiz_left align_horiz_initial")
    inst.cmd("output * layout_default_width 0.4")
    inst.wait_for_idle()

    with wayland_client(inst, "client1") as c1_client:
        v1 = wait_for_client_map(inst, "client1")
        c1 = inst.execute_lua(f"return scroll.view_get_container({v1})")
        inst.wait_for_idle()

        geom1 = get_container_geometry(inst, c1)
        assert abs(geom1["x"] - ws_rect["x"]) < 1.0

        with wayland_client(inst, "client2"):
            v2 = wait_for_client_map(inst, "client2")
            c2 = inst.execute_lua(f"return scroll.view_get_container({v2})")
            inst.wait_for_idle()

            geom2 = get_container_geometry(inst, c2)
            assert abs(geom2["x"] - (ws_rect["x"] + geom1["width"])) < 1.0

            # Close W1, W2 should stay at its position (512)
            c1_client.terminate()
            c1_client.wait()
            inst.wait_for_idle()

            geom2_after = get_container_geometry(inst, c2)
            assert abs(geom2_after["x"] - (ws_rect["x"] + geom1["width"])) < 1.0

            # Open W3. It should be placed next to W2 (at 1024).
            # W3 ends at 1024 + 512 = 1536 (offscreen).
            # It should scroll to be visible.
            # Min scroll to bring W3 into view is 256.
            # So W3 should be at 1024 - 256 = 768.
            # W2 should be at 512 - 256 = 256.
            with wayland_client(inst, "client3"):
                v3 = wait_for_client_map(inst, "client3")
                c3 = inst.execute_lua(f"return scroll.view_get_container({v3})")
                inst.wait_for_idle()

                geom3 = get_container_geometry(inst, c3)
                geom2_after_w3 = get_container_geometry(inst, c2)

                # W3 should be visible
                assert (
                    geom3["x"] + geom3["width"] <= ws_rect["x"] + ws_rect["width"] + 1.0
                )
                # W3 should be at 768
                assert abs(geom3["x"] - (ws_rect["x"] + 768)) < 1.0
                # W2 should be at 256
                assert abs(geom2_after_w3["x"] - (ws_rect["x"] + 256)) < 1.0
