import json
from pathlib import Path
import socket
import struct
from test_utils import (
    run_compositor,
    wait_for_client_map,
    wayland_client,
    ScrollCompositorFactory,
)


def test_shutdown_events_verification(
    scroll_compositor_binary: str, tmp_path: Path
) -> None:
    config_path: Path = Path(__file__).parent.parent / "config.in"
    config_content: str = config_path.read_text()

    with run_compositor(scroll_compositor_binary, tmp_path, config_content) as inst:
        factory = ScrollCompositorFactory(inst)
        with factory() as fc:
            # Connect a second socket for events subscription
            event_socket: socket.socket = socket.socket(
                socket.AF_UNIX, socket.SOCK_STREAM
            )
            event_socket.connect(fc.ipc.socket_path)

        # Helper functions for the custom subscription socket
        def send_msg(msg_type: int, payload: str) -> None:
            payload_bytes: bytes = payload.encode("utf-8")
            length: int = len(payload_bytes)
            header: bytes = struct.pack("<6sII", b"i3-ipc", length, msg_type)
            event_socket.sendall(header + payload_bytes)

        def recv_msg() -> tuple[int, str]:
            header_data: bytes = b""
            while len(header_data) < 14:
                chunk: bytes = event_socket.recv(14 - len(header_data))
                if not chunk:
                    raise EOFError("Socket closed")
                header_data += chunk
            magic: bytes
            length: int
            msg_type: int
            magic, length, msg_type = struct.unpack("<6sII", header_data)
            payload_data: bytes = b""
            while len(payload_data) < length:
                chunk = event_socket.recv(length - len(payload_data))
                if not chunk:
                    raise EOFError("Socket closed")
                payload_data += chunk
            return msg_type, payload_data.decode("utf-8")

        # Subscribe to workspace, window, and shutdown events
        # 2 is IPC_SUBSCRIBE
        send_msg(2, json.dumps(["workspace", "window", "shutdown"]))
        msg_type, payload = recv_msg()
        assert msg_type == 2
        assert json.loads(payload)["success"] is True

        # Start two clients to have some active views/workspaces
        with wayland_client(fc, "client1"):
            wait_for_client_map(fc, "client1")
            with wayland_client(fc, "client2"):
                wait_for_client_map(fc, "client2")

                # Drain all pending events on subscription socket before terminating
                event_socket.setblocking(False)
                try:
                    while True:
                        header_data: bytes = event_socket.recv(14)
                        if len(header_data) == 14:
                            magic, length, msg_type = struct.unpack(
                                "<6sII", header_data
                            )
                            payload_data = b""
                            event_socket.setblocking(True)
                            while len(payload_data) < length:
                                chunk = event_socket.recv(length - len(payload_data))
                                if not chunk:
                                    break
                                payload_data += chunk
                            event_socket.setblocking(False)
                except BlockingIOError:
                    pass

                event_socket.setblocking(True)

                # Now terminate the compositor by sending the exit command
                try:
                    fc.cmd("exit")
                except (EOFError, BrokenPipeError, ConnectionResetError):
                    # The socket might close immediately during exit processing, which is fine
                    pass

                # Read all events sent during shutdown until socket EOF
                shutdown_events: list[tuple[int, dict]] = []
                try:
                    while True:
                        msg_type, payload = recv_msg()
                        shutdown_events.append((msg_type, json.loads(payload)))
                except (EOFError, BrokenPipeError, ConnectionResetError):
                    pass  # EOF or connection error is expected when compositor exits

                print(f"Events received during shutdown: {shutdown_events}")

                shutdown_msg_type: int = (1 << 31) | 6
                # Verify that if any events are received, they are only shutdown events and nothing unexpected
                unexpected_events: list[tuple[int, dict]] = []
                for mtype, p in shutdown_events:
                    if mtype == shutdown_msg_type:
                        assert p["change"] == "exit"
                    else:
                        unexpected_events.append((mtype, p))

                assert not unexpected_events, (
                    f"Received unexpected events: {unexpected_events}"
                )
