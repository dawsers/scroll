from pathlib import Path
import re
from test_utils import (
    run_compositor,
    wait_for_client_map,
    wayland_client,
    ScrollCompositorFactory,
)


def test_shutdown_lua_callbacks_verification(
    scroll_compositor_binary: str, tmp_path: Path
) -> None:
    config_path: Path = Path(__file__).parent.parent / "config.in"
    config_content: str = config_path.read_text()

    with run_compositor(scroll_compositor_binary, tmp_path, config_content) as inst:
        factory = ScrollCompositorFactory(inst)
        with factory() as fc:
            # Register callbacks for all events, logging them to the scroll debug log
            res = fc.execute_lua("""
                scroll.add_callback("view_map", function(view, data)
                    scroll.log("LUA_CALLBACK: view_map " .. tostring(view))
                end, nil)
                scroll.add_callback("view_unmap", function(view, data)
                    scroll.log("LUA_CALLBACK: view_unmap " .. tostring(view))
                end, nil)
                scroll.add_callback("view_focus", function(view, data)
                    scroll.log("LUA_CALLBACK: view_focus " .. tostring(view))
                end, nil)
                scroll.add_callback("workspace_create", function(ws, data)
                    scroll.log("LUA_CALLBACK: workspace_create " .. tostring(ws))
                end, nil)
                scroll.add_callback("workspace_focus", function(ws, data)
                    scroll.log("LUA_CALLBACK: workspace_focus " .. tostring(ws))
                end, nil)
            """)
            print(f"execute_lua result: {res}")

            # Start two clients to populate windows and workspaces
            with wayland_client(fc, "client1"):
                wait_for_client_map(fc, "client1")
                with wayland_client(fc, "client2"):
                    wait_for_client_map(fc, "client2")

                    # Record the log length before exit command
                    log_before_exit: str = fc.read_log()
                    log_len_before_exit: int = len(log_before_exit)

                    # Now terminate the compositor via the exit command
                    try:
                        fc.cmd("exit")
                    except (EOFError, BrokenPipeError, ConnectionResetError):
                        pass

                    # Wait for compositor to exit
                    fc.proc.wait(timeout=5)

                    # Read all new log lines generated during shutdown
                    full_log: str = fc.read_log()
                    shutdown_log: str = full_log[log_len_before_exit:]

                    # Extract all callback events triggered during shutdown
                    callback_pattern = re.compile(r"LUA_CALLBACK: (\w+)")
                    triggered_callbacks: list[str] = callback_pattern.findall(
                        shutdown_log
                    )

                    print(f"Triggered callbacks during shutdown: {triggered_callbacks}")

                    # During shutdown, we unmap the existing views, so 'view_unmap' callbacks are expected.
                    # However, we should NOT see any new focus events ('view_focus', 'workspace_focus')
                    # or creation events ('workspace_create') being invoked.
                    unexpected_callbacks: list[str] = [
                        cb
                        for cb in triggered_callbacks
                        if cb
                        in (
                            "view_map",
                            "view_focus",
                            "workspace_create",
                            "workspace_focus",
                        )
                    ]

                    assert not unexpected_callbacks, (
                        "Unexpected Lua callbacks invoked during shutdown:"
                        f" {unexpected_callbacks}"
                    )
