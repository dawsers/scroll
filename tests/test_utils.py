import time
import os
import subprocess
import re
import pytest
from typing import Generator, Any
from contextlib import contextmanager
from pathlib import Path
from scrollipc import ScrollIPC

RUNNER_LUA_CONTENT: str = """
local args = ...
local output_path = args[1]
local user_code_path = args[2]

local function escape_str(s)
    return '"' .. s:gsub('\\\\', '\\\\\\\\'):gsub('"', '\\\\"'):gsub('\\n', '\\\\n'):gsub('\\r', '\\\\r'):gsub('\\t', '\\\\t') .. '"'
end

local function serialize(val)
    if val == nil then return "null" end
    if type(val) == "boolean" then return val and "true" or "false" end
    if type(val) == "number" then return tostring(val) end
    if type(val) == "string" then return escape_str(val) end
    if type(val) == "table" then
        local is_list = true
        local max_idx = 0
        local count = 0
        for k, v in pairs(val) do
            count = count + 1
            if type(k) ~= "number" or k < 1 or math.floor(k) ~= k then
                is_list = false
                break
            end
            if k > max_idx then max_idx = k end
        end
        if is_list and max_idx == count then
            local parts = {}
            for i = 1, max_idx do
                table.insert(parts, serialize(val[i]))
            end
            return "[" .. table.concat(parts, ",") .. "]"
        else
            local parts = {}
            for k, v in pairs(val) do
                if type(k) == "string" then
                    table.insert(parts, escape_str(k) .. ":" .. serialize(v))
                end
            end
            return "{" .. table.concat(parts, ",") .. "}"
        end
    end
    return "null"
end

local chunk, err = loadfile(user_code_path)
local success, result
local results
if chunk then
    results = { pcall(chunk) }
    success = results[1]
else
    success = false
    results = { false, "Error loading code: " .. tostring(err) }
end

local f = io.open(output_path, "w")
if success then
    f:write("SUCCESS\\n")
    if #results <= 1 then
        f:write("null")
    elseif #results == 2 then
        f:write(serialize(results[2]))
    else
        local parts = {}
        for i = 2, #results do
            table.insert(parts, serialize(results[i]))
        end
        f:write("[" .. table.concat(parts, ",") .. "]")
    end
else
    f:write("ERROR\\n")
    f:write(tostring(results[2]))
end
f:close()
"""


class ScrollInstance:
    proc: subprocess.Popen
    ipc: ScrollIPC
    log_path: Path
    temp_dir: Path

    def __init__(
        self, proc: subprocess.Popen, ipc: ScrollIPC, log_path: Path, temp_dir: Path
    ):
        self.proc = proc
        self.ipc = ipc
        self.log_path = log_path
        self.temp_dir = temp_dir

    def cmd(self, command: str) -> list:
        return self.ipc.command(command)

    def get_tree(self) -> dict:
        return self.ipc.get_tree()

    def read_log(self) -> str:
        return self.log_path.read_text()

    def execute_lua(self, code: str) -> Any:
        import json

        runner_path = self.temp_dir / "exec_runner.lua"
        if not runner_path.exists():
            runner_path.write_text(RUNNER_LUA_CONTENT)

        if not hasattr(self, "_lua_execute_counter"):
            self._lua_execute_counter = 0
        counter = self._lua_execute_counter
        self._lua_execute_counter += 1

        user_code_path = self.temp_dir / f"user_code_{counter}.lua"
        output_path = self.temp_dir / f"exec_{counter}.out"

        user_code_path.write_text(code)

        res = self.cmd(f"lua {runner_path} {output_path} {user_code_path}")
        assert res[0]["success"], f"Failed to run lua command: {res}"

        assert output_path.exists(), (
            f"Output file not created: {output_path}. Compositor log:\\n{self.read_log()}"
        )
        output_content = output_path.read_text()

        lines = output_content.splitlines()
        if not lines:
            raise RuntimeError(
                f"Lua runner output is empty. Compositor log:\\n{self.read_log()}"
            )
        status = lines[0]
        result_str = "\\n".join(lines[1:])

        if status == "SUCCESS":
            if not result_str:
                return None
            return json.loads(result_str)
        else:
            raise RuntimeError(f"Lua execution failed: {result_str}")

    def getenv(self, var: str) -> str | None:
        return self.execute_lua(f'return os.getenv("{var}")')

    @contextmanager
    def assert_logs_match(
        self, pattern: str, timeout: float = 5.0
    ) -> Generator[None, None, None]:
        initial_log_len: int = len(self.read_log())
        yield
        start_time: float = time.time()
        compiled_pattern = re.compile(pattern)
        while True:
            current_log: str = self.read_log()
            new_log: str = current_log[initial_log_len:]
            if compiled_pattern.search(new_log):
                return
            if time.time() - start_time > timeout:
                raise AssertionError(
                    f"Pattern '{pattern}' not found in new log output within {timeout}s.\nNew log was:\n{new_log}"
                )
            time.sleep(0.1)


@contextmanager
def run_compositor(
    binary_path: str, temp_dir: Path, config_content: str | None = None
) -> Generator[ScrollInstance, None, None]:
    log_path: Path = temp_dir / "scroll.log"
    log_file = open(log_path, "w")

    config_path: Path = temp_dir / "config"
    if config_content is None:
        config_content = "workspace 1\nxwayland force\n"
    config_path.write_text(config_content)

    env = os.environ.copy()
    env["HOME"] = str(temp_dir)
    env["WLR_BACKENDS"] = "headless"

    tests_dir = Path(__file__).parent.resolve()
    supp_path = tests_dir / "lsan.supp"
    if "LSAN_OPTIONS" in env:
        env["LSAN_OPTIONS"] = f"suppressions={supp_path}:{env['LSAN_OPTIONS']}"
    else:
        env["LSAN_OPTIONS"] = f"suppressions={supp_path}"
    if "DISPLAY" in env:
        del env["DISPLAY"]
    if "WAYLAND_DISPLAY" in env:
        del env["WAYLAND_DISPLAY"]

    proc = subprocess.Popen(
        [binary_path, "-c", str(config_path), "-d"],
        env=env,
        stdout=log_file,
        stderr=subprocess.STDOUT,
    )

    xdg_runtime_dir: str = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
    uid: int = os.getuid()
    socket_path: str = os.path.join(
        xdg_runtime_dir, f"scroll-ipc.{uid}.{proc.pid}.sock"
    )

    ipc = None
    tries = 0
    while tries < 100:
        if os.path.exists(socket_path):
            try:
                ipc = ScrollIPC(socket_path)
                break
            except Exception:
                pass
        time.sleep(0.05)
        tries += 1

    if not ipc:
        proc.terminate()
        log_file.close()
        print(f"Scroll log:\n{log_path.read_text()}")
        pytest.exit("Failed to connect to scroll IPC")

    try:
        yield ScrollInstance(proc, ipc, log_path, temp_dir)
    finally:
        # Teardown
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        log_file.close()


@contextmanager
def wayland_client(
    compositor: ScrollInstance,
    title: str,
) -> Generator[subprocess.Popen, None, None]:
    wayland_display: str | None = compositor.getenv("WAYLAND_DISPLAY")
    assert wayland_display is not None
    client_path: Path = Path("./build/tests/wayland-test-client").resolve()
    assert client_path.exists(), f"Client not found at {client_path}"
    env: dict = os.environ.copy()
    env["WAYLAND_DISPLAY"] = wayland_display
    proc: subprocess.Popen = subprocess.Popen(
        [str(client_path), title, "test_app"], env=env
    )
    try:
        yield proc
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()


def wait_for_client_map(compositor: ScrollInstance, title: str) -> int:
    tries: int = 0
    while tries < 50:
        view_id = compositor.execute_lua(f"""
            local view = scroll.focused_view()
            if view and scroll.view_get_title(view) == "{title}" then
                return view
            end
        """)
        if view_id is not None:
            assert isinstance(view_id, int)
            return view_id
        time.sleep(0.05)
        tries += 1
    raise RuntimeError(f"Client '{title}' did not map")
