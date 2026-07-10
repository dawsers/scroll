from pathlib import Path
import subprocess
import time
from test_utils import run_compositor, ScrollCompositorFactory, ScrollInstance


def test_normal_exit_no_errors(fresh_compositor: ScrollInstance) -> None:
    # Send exit command
    try:
        res = fresh_compositor.cmd("exit")
        assert res and res[0]["success"], f"Exit command failed: {res}"
    except EOFError:
        # Expected if compositor exits before flushing reply
        pass

    # Wait for compositor to exit
    try:
        poll = fresh_compositor.proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        poll = None

    assert poll is not None, "Compositor did not exit"

    log_content = fresh_compositor.read_log()
    assert poll == 0, f"Compositor exited with non-zero code: {poll}"
    assert "node_table not initialized" not in log_content


def test_normal_exit_with_bar(scroll_compositor_binary: str, tmp_path: Path) -> None:
    config_content = """
workspace 1
xwayland force
animations enabled no
bar {
    scrollbar_command scrollbar
}
"""
    with run_compositor(scroll_compositor_binary, tmp_path, config_content) as inst:
        factory = ScrollCompositorFactory(inst)
        with factory() as fresh_compositor:
            # Let it run a bit to ensure swaybar starts
            time.sleep(0.5)

            # Send exit command
            try:
                res = fresh_compositor.cmd("exit")
                assert res and res[0]["success"], f"Exit command failed: {res}"
            except EOFError:
                pass

            ret = fresh_compositor.proc.wait(timeout=5)
            assert ret == 0, f"Compositor exited with code {ret}"

            log_content = fresh_compositor.read_log()
            assert "ERROR: LeakSanitizer" not in log_content, (
                "Leak detected in compositor or helper process"
            )
