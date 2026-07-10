from pathlib import Path
import subprocess
import time
from test_utils import (
    run_compositor,
    wait_for_client_map,
    wayland_client,
    ScrollCompositorFactory,
)


def test_leak_two_clients(scroll_compositor_binary: str, tmp_path: Path) -> None:
    config_path = Path(__file__).parent.parent / "config.in"
    config_content = config_path.read_text()

    with run_compositor(scroll_compositor_binary, tmp_path, config_content) as inst:
        factory = ScrollCompositorFactory(inst)
        with factory() as fresh_compositor:
            # Start two clients and keep them running
            with wayland_client(fresh_compositor, "client1"):
                wait_for_client_map(fresh_compositor, "client1")

                with wayland_client(fresh_compositor, "client2"):
                    wait_for_client_map(fresh_compositor, "client2")

                    # Let them run a bit
                    time.sleep(0.5)

                    # Terminate compositor while clients are still running
                    fresh_compositor.proc.terminate()
                    try:
                        ret = fresh_compositor.proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        fresh_compositor.proc.kill()
                        ret = fresh_compositor.proc.wait()

                    assert ret == 0, f"Compositor exited with code {ret}"
