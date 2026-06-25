import time
from typing import Generator
from pathlib import Path
import pytest
from conftest import ScrollInstance
from test_utils import wayland_client, wait_for_client_map, run_compositor


@pytest.fixture(scope="function")
def offset_animating_compositor(
    scroll_compositor_binary: str, tmp_path: Path
) -> Generator[ScrollInstance, None, None]:
    config: str = (
        "workspace 1\n"
        "xwayland force\n"
        "animations enabled yes\n"
        # 5 second animation, 10% offset scale
        "animations window_move yes 5000 var 3 [ 0.215 0.61 0.355 1 ] off 0.1 6 [0 0.6 0.4 0 1 0 0.4 -0.6 1 -0.6]\n"
    )
    with run_compositor(scroll_compositor_binary, tmp_path, config) as inst:
        yield inst


def test_animation_offset_unintended_move(
    offset_animating_compositor: ScrollInstance,
) -> None:
    inst = offset_animating_compositor

    try:
        with wayland_client(inst, "client1"):
            wait_for_client_map(inst, "client1")
            time.sleep(0.5)
            with wayland_client(inst, "client2"):
                wait_for_client_map(inst, "client2")
                time.sleep(0.5)
                with wayland_client(inst, "client3"):
                    wait_for_client_map(inst, "client3")
                    time.sleep(0.5)

                    tree = inst.get_tree()

                    def find_views(node, result):
                        if node.get("type") == "con" and node.get("name"):
                            if (
                                node.get("window")
                                or node.get("app_id")
                                or (
                                    node.get("window_properties")
                                    and node["window_properties"].get("title")
                                )
                            ):
                                result.append(
                                    {
                                        "title": node["name"],
                                        "id": node["id"],
                                        "x": node["rect"]["x"],
                                    }
                                )
                        for child in node.get("nodes", []):
                            find_views(child, result)
                        for child in node.get("floating_nodes", []):
                            find_views(child, result)

                    views = []
                    find_views(tree, views)
                    print("Found views:", views)
                    assert len(views) == 3

                    # Sort by X to identify leftmost, middle, rightmost
                    sorted_views = sorted(views, key=lambda v: v["x"])
                    v1, v2, v3 = sorted_views
                    print(
                        f"Leftmost: {v1['title']}, Middle: {v2['title']}, Rightmost: {v3['title']}"
                    )

                    # Focus rightmost (client3) to avoid scroll during swap
                    print("Focusing rightmost...")
                    res = inst.cmd(f"[con_id={v3['id']}] focus")
                    assert res[0]["success"]
                    time.sleep(1.0)  # wait for focus scroll to settle

                    # Swap leftmost and middle by moving leftmost right, using its ID as context
                    print("Swapping leftmost and middle in background...")
                    # We use execute_lua to run scroll.command with v1['id'] context
                    res_lua = inst.execute_lua(
                        f"return scroll.command({v1['id']}, 'move right')"
                    )
                    print(f"Lua command result: {res_lua}")

                    # Get parent container of client3 to query its actual position
                    parent_id = inst.execute_lua(
                        f"return scroll.container_get_parent({v3['id']})"
                    )
                    query_id = parent_id if parent_id is not None else v3["id"]
                    print(f"Querying container {query_id} (parent of {v3['id']})")

                    # Query position of client3 during animation
                    positions = []
                    start_time = time.time()
                    while time.time() - start_time < 2.0:
                        geom = inst.execute_lua(
                            f"return scroll.container_get_animated_geometry({query_id})"
                        )
                        positions.append(geom)
                        time.sleep(0.05)

                    print(f"Positions of client3: {positions}")

                    # Verify positions are constant
                    x_values = [p["x"] for p in positions]
                    y_values = [p["y"] for p in positions]

                    first_x = x_values[0]
                    first_y = y_values[0]
                    for x in x_values:
                        assert x == pytest.approx(first_x, abs=1.0), (
                            f"client3 moved horizontally: {x_values}"
                        )
                    for y in y_values:
                        assert y == pytest.approx(first_y, abs=1.0), (
                            f"client3 moved vertically: {y_values}"
                        )

                    print("Test passed: static window did not move.")

    except Exception as e:
        print("Scroll Log on failure:")
        print(inst.read_log())
        raise e
