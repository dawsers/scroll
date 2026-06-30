import time
from test_utils import ScrollCompositorFactory


def test_workspace_switch_empty_uaf(
    scroll_compositor_factory: ScrollCompositorFactory,
) -> None:
    config = (
        "workspace 1\n"
        "xwayland force\n"
        "animations enabled yes\n"
        "animations workspace_switch yes 5000\n"
    )
    with scroll_compositor_factory(config) as scroll_compositor:
        scroll_compositor.cmd("workspace 2; workspace 1")
        # Sleep a bit to let the animation start and run a few frames, triggering UAF
        time.sleep(0.5)

        # Check if the compositor process crashed
        if scroll_compositor.proc.poll() is not None:
            print("=== COMPOSITOR LOG (ASAN) ===")
            print(scroll_compositor.read_log())
            print("=============================")
            assert False, "Compositor crashed"
