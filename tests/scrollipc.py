import socket
import struct
import json

MAGIC: bytes = b"i3-ipc"
HEADER_FORMAT: str = "<6sII"  # 6 bytes magic, 4 bytes length, 4 bytes type
HEADER_SIZE: int = struct.calcsize(HEADER_FORMAT)

# Command types from include/ipc.h
IPC_COMMAND: int = 0
IPC_GET_WORKSPACES: int = 1
IPC_SUBSCRIBE: int = 2
IPC_GET_VERSION: int = 7
IPC_MINT_ACTIVATION_TOKEN: int = 102


class ScrollIPC:
    socket_path: str
    sock: socket.socket

    def __init__(self, socket_path: str):
        self.socket_path = socket_path
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(socket_path)

    def close(self) -> None:
        self.sock.close()

    def _send(self, msg_type: int, payload: str) -> None:
        payload_bytes: bytes = payload.encode("utf-8")
        length: int = len(payload_bytes)
        header: bytes = struct.pack(HEADER_FORMAT, MAGIC, length, msg_type)
        self.sock.sendall(header + payload_bytes)

    def _recv(self) -> tuple[int, str]:
        header_data: bytes = self._recv_all(HEADER_SIZE)
        magic: bytes
        length: int
        msg_type: int
        magic, length, msg_type = struct.unpack(HEADER_FORMAT, header_data)
        if magic != MAGIC:
            raise ValueError(f"Invalid magic string: {magic}")
        payload_data: bytes = self._recv_all(length)
        return msg_type, payload_data.decode("utf-8")

    def _recv_all(self, size: int) -> bytes:
        data: bytes = b""
        while len(data) < size:
            chunk: bytes = self.sock.recv(size - len(data))
            if not chunk:
                raise EOFError("Socket closed prematurely")
            data += chunk
        return data

    def command(self, cmd_string: str) -> list:
        self._send(IPC_COMMAND, cmd_string)
        reply_type: int
        reply_payload: str
        reply_type, reply_payload = self._recv()
        if reply_type != IPC_COMMAND:
            raise ValueError(f"Unexpected reply type: {reply_type}")
        result = json.loads(reply_payload)
        assert isinstance(result, list)
        return result

    def get_tree(self) -> dict:
        self._send(4, "")
        reply_type, reply_payload = self._recv()
        if reply_type != 4:
            raise ValueError(f"Unexpected reply type: {reply_type}")
        return json.loads(reply_payload)

    def mint_activation_token(self) -> dict:
        self._send(IPC_MINT_ACTIVATION_TOKEN, "")
        reply_type, reply_payload = self._recv()
        if reply_type != IPC_MINT_ACTIVATION_TOKEN:
            raise ValueError(f"Unexpected reply type: {reply_type}")
        result = json.loads(reply_payload)
        assert isinstance(result, dict)
        return result
