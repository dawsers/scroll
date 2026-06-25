import time
from typing import Generator
from pathlib import Path
import pytest
from conftest import ScrollInstance
from test_utils import wayland_client, wait_for_client_map, run_compositor


@pytest.fixture(scope="function")
def animating_compositor(
    scroll_compositor_binary: str, tmp_path: Path
) -> Generator[ScrollInstance, None, None]:
    config: str = (
        "workspace 1\n"
        "xwayland force\n"
        "animations enabled yes\n"
        # 2 second animation for window_move
        "animations window_move yes 2000 linear\n"
    )
    with run_compositor(scroll_compositor_binary, tmp_path, config) as inst:
        yield inst


def test_static_geometry(scroll_compositor: ScrollInstance) -> None:
    inst = scroll_compositor
    with wayland_client(inst, "client1"):
        view_id = wait_for_client_map(inst, "client1")
        con_id = inst.execute_lua(f"return scroll.view_get_container({view_id})")
        assert con_id is not None

        # Wait for any map animation to settle
        time.sleep(2.0)

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


def test_animating_geometry(animating_compositor: ScrollInstance) -> None:
    inst = animating_compositor
    with wayland_client(inst, "client1"):
        v1 = wait_for_client_map(inst, "client1")
        c1 = inst.execute_lua(f"return scroll.view_get_container({v1})")

        with wayland_client(inst, "client2"):
            wait_for_client_map(inst, "client2")

            # Wait for initial map animations to settle
            time.sleep(0.5)

            geom_before = inst.execute_lua(
                f"return scroll.container_get_geometry({c1})"
            )
            actual_geom_before = inst.execute_lua(
                f"return scroll.container_get_animated_geometry({c1})"
            )
            assert geom_before == actual_geom_before

            print("Before move:", geom_before)

            # Trigger move
            inst.execute_lua(f"scroll.command({c1}, 'move right')")

            # Immediately query geometry
            geom_after_trigger = inst.execute_lua(
                f"return scroll.container_get_geometry({c1})"
            )
            actual_geom_after_trigger = inst.execute_lua(
                f"return scroll.container_get_animated_geometry({c1})"
            )

            print("Immediately after trigger (target):", geom_after_trigger)
            print("Immediately after trigger (actual):", actual_geom_after_trigger)

            # The target geometry (geom) should have jumped to the final position.
            # The actual geometry should still be close to the initial position.
            assert geom_after_trigger != geom_before
            assert actual_geom_after_trigger["x"] == pytest.approx(
                actual_geom_before["x"], abs=10.0
            )

            # Monitor animation
            actual_xs = []
            target_xs = []
            start_time = time.time()
            while time.time() - start_time < 2.5:  # Animation is 2s
                g = inst.execute_lua(f"return scroll.container_get_geometry({c1})")
                ag = inst.execute_lua(
                    f"return scroll.container_get_animated_geometry({c1})"
                )
                target_xs.append(g["x"])
                actual_xs.append(ag["x"])
                time.sleep(0.1)

            print("Target Xs:", target_xs)
            print("Actual Xs:", actual_xs)

            # Target Xs should all be equal to the final position
            final_x = geom_after_trigger["x"]
            for tx in target_xs:
                assert tx == final_x

            # Actual Xs should start near before_x and end at final_x
            assert actual_xs[0] < final_x  # Assuming it moved right
            assert actual_xs[-1] == pytest.approx(final_x, abs=1.0)

            # Verify it is monotonically increasing (if it moved right)
            for i in range(1, len(actual_xs)):
                assert actual_xs[i] >= actual_xs[i - 1] - 0.1


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
