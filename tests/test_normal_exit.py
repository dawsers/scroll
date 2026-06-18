import time
from conftest import ScrollInstance


def test_normal_exit_no_errors(fresh_compositor: ScrollInstance) -> None:
    # Send exit command
    try:
        res = fresh_compositor.cmd("exit")
        assert res and res[0]["success"], f"Exit command failed: {res}"
    except EOFError:
        # Expected if compositor exits before flushing reply
        pass
    except Exception as e:
        print(f"Compositor log:\n{fresh_compositor.read_log()}")
        raise e

    # Wait for compositor to exit
    tries = 0
    poll = None
    while tries < 50:
        poll = fresh_compositor.proc.poll()
        if poll is not None:
            break
        time.sleep(0.1)
        tries += 1

    assert poll is not None, "Compositor did not exit"

    log_content = fresh_compositor.read_log()
    if poll != 0 or "node_table not initialized" in log_content:
        print(f"Compositor log:\n{log_content}")

    assert poll == 0, f"Compositor exited with non-zero code: {poll}"
    assert "node_table not initialized" not in log_content
