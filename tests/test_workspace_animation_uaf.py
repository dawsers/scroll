from pathlib import Path
from typing import Generator
from conftest import ScrollInstance
import pytest
from test_utils import run_compositor, wait_for_client_map, wayland_client


@pytest.fixture(scope="function")
def animating_compositor(
    scroll_compositor_binary: str, tmp_path: Path
) -> Generator[ScrollInstance, None, None]:
    config: str = (
        "workspace 1\n"
        "xwayland force\n"
        "animations enabled yes\n"
        "animations workspace_switch yes 5000\n"
    )
    with run_compositor(scroll_compositor_binary, tmp_path, config) as inst:
        yield inst


def test_workspace_switch_active_uaf(
    animating_compositor: ScrollInstance,
) -> None:
    # 1. Create w1 on Workspace 1
    with wayland_client(animating_compositor, "client1") as client1:
        wait_for_client_map(animating_compositor, "client1")
        w1_id = animating_compositor.execute_lua("return scroll.focused_container()")
        print(f"w1_id: {w1_id}")

        # Enable manual stepping
        animating_compositor.set_manual_stepping(True)

        # 2. Switch to Workspace 2.
        # This starts a 5s animation. WS 1 is NOT empty, so data->from = WS 1.
        animating_compositor.cmd("workspace 2")

        # Step 100ms to ensure animation has started and is ongoing
        animating_compositor.animation_step(100)

        # 3. Kill w1 (on WS 1) during animation.
        # WS 1 should become empty and be destroyed/freed.
        animating_compositor.cmd(f"[con_id={w1_id}] kill")

        # Wait for client1 to exit
        client1.wait(timeout=5)

        # Wait for destroy transactions to complete, but animation is still ongoing
        animating_compositor.wait_for_transactions()

        # 4. Switch back to Workspace 1.
        # This will cancel the ongoing animation, triggering workspace_switch_callback_end.
        # It should crash here if WS 1 was freed!
        animating_compositor.cmd("workspace 1")

        animating_compositor.set_manual_stepping(False)
