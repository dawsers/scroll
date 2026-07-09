import time
import os
import subprocess
import re
import pytest
from typing import Generator, Any, Dict, Optional
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

    def reload_config(self, config_content: str) -> None:
        config_path = self.temp_dir / "config"
        config_path.write_text(config_content)
        self.cmd("reload")
        self.wait_for_idle()

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

    def wait_for_idle(self, timeout: float = 5.0) -> None:
        start = time.time()
        while time.time() - start < timeout:
            pending = self.execute_lua(
                "return scroll.pending_transactions() or scroll.animating()"
            )
            if not pending:
                return
            time.sleep(0.005)
        raise TimeoutError("Timeout waiting for compositor to become idle")

    def wait_for_transactions(self, timeout: float = 5.0) -> None:
        start = time.time()
        while time.time() - start < timeout:
            if not self.execute_lua("return scroll.pending_transactions()"):
                return
            time.sleep(0.005)
        raise TimeoutError("Timeout waiting for transactions")

    def reset(self) -> None:
        # 1. Kill all views to clean up leftover windows
        try:
            self.cmd("kill all")
        except Exception:
            pass

        # 2. Clean up extra outputs
        try:
            tree = self.get_tree()
            outputs: list[str] = []
            for child in tree.get("nodes", []):
                if child.get("type") == "output" and child.get("name") != "__i3":
                    outputs.append(child["name"])

            if "HEADLESS-1" in outputs:
                for out in outputs:
                    if out != "HEADLESS-1" and out.startswith("HEADLESS-"):
                        self.cmd(f"output {out} unplug")
            self.wait_for_idle()
        except Exception:
            pass

        # 3. Reload config to reset defaults
        try:
            config_path = self.temp_dir / "config"
            config_path.write_text("workspace 1\nxwayland force\n")
            self.cmd("reload")
            self.wait_for_idle()
        except Exception:
            pass

        # 4. Reset workspaces (recreate workspace 1)
        try:
            self.cmd("workspace __temp")
            self.cmd("workspace 1")
            self.wait_for_idle()
        except Exception:
            pass

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

    def wait_for_log_pattern(
        self, pattern: str, timeout: float = 5.0, from_start: bool = False
    ) -> None:
        compiled_pattern = re.compile(pattern)
        start_time = time.time()
        initial_log_len = 0 if from_start else len(self.read_log())
        while True:
            current_log: str = self.read_log()
            log_to_search = current_log if from_start else current_log[initial_log_len:]
            if compiled_pattern.search(log_to_search):
                return
            if time.time() - start_time > timeout:
                raise AssertionError(
                    f"Pattern '{pattern}' not found in log output within"
                    f" {timeout}s.\nLog searched was:\n{log_to_search}"
                )
            time.sleep(0.1)


class ScrollCompositorFactory:
    _session: ScrollInstance
    _active_context: Optional["ScrollCompositorContextManager"]

    def __init__(self, session: ScrollInstance):
        self._session = session
        self._active_context = None

    def print_log(self) -> None:
        try:
            print(
                f"Compositor log (PID {self._session.proc.pid}):\n{self._session.read_log()}"
            )
        except Exception as log_err:
            print(f"Failed to read compositor log: {log_err}")

    def __call__(self, config: str | None = None) -> "ScrollCompositorContextManager":
        return ScrollCompositorContextManager(self, self._session, config)


class ScrollCompositorContextManager:
    _factory: ScrollCompositorFactory
    _session: ScrollInstance
    _config: Optional[str]

    def __init__(
        self,
        factory: ScrollCompositorFactory,
        session: ScrollInstance,
        config: Optional[str] = None,
    ):
        self._factory = factory
        self._session = session
        self._config = config

    def __enter__(self) -> ScrollInstance:
        if self._factory._active_context is not None:
            raise RuntimeError(
                "ScrollInstance is already active in another context manager"
            )
        self._factory._active_context = self

        if self._config is not None:
            config_path = self._session.temp_dir / "config"
            config_path.write_text(self._config)
            self._session.cmd("reload")
            self._session.wait_for_idle()

        return self._session

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        try:
            if exc_type is not None:
                self._factory.print_log()
            self._session.reset()
        finally:
            self._factory._active_context = None


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
        env["LSAN_OPTIONS"] = (
            f"detect_leaks=0:suppressions={supp_path}:{env['LSAN_OPTIONS']}"
        )
    else:
        env["LSAN_OPTIONS"] = f"detect_leaks=0:suppressions={supp_path}"
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
            local function find_view(title)
                -- Check scratchpad
                for _, con in ipairs(scroll.scratchpad_get_containers()) do
                    for _, view in ipairs(scroll.container_get_views(con)) do
                        if scroll.view_get_title(view) == title then
                            return view
                        end
                    end
                end
                -- Check all outputs and workspaces
                for _, output in ipairs(scroll.root_get_outputs()) do
                    for _, ws in ipairs(scroll.output_get_workspaces(output)) do
                        for _, con in ipairs(scroll.workspace_get_tiling(ws)) do
                            for _, view in ipairs(scroll.container_get_views(con)) do
                                if scroll.view_get_title(view) == title then
                                    return view
                                end
                            end
                        end
                        for _, con in ipairs(scroll.workspace_get_floating(ws)) do
                            for _, view in ipairs(scroll.container_get_views(con)) do
                                if scroll.view_get_title(view) == title then
                                    return view
                                end
                            end
                        end
                    end
                end
                return nil
            end
            return find_view("{title}")
        """)
        if view_id is not None:
            assert isinstance(view_id, int)
            return view_id
        time.sleep(0.05)
        tries += 1
    raise RuntimeError(f"Client '{title}' did not map")


def find_node_by_title_contains(
    node: Dict[str, Any], title_sub: str
) -> Optional[Dict[str, Any]]:
    name = node.get("name")
    if name and title_sub in name:
        return node
    for child in node.get("nodes", []):
        res = find_node_by_title_contains(child, title_sub)
        if res:
            return res
    for child in node.get("floating_nodes", []):
        res = find_node_by_title_contains(child, title_sub)
        if res:
            return res
    return None


def wait_for_title_contains_map(
    compositor: ScrollInstance, title_sub: str, timeout: float = 15.0
) -> Dict[str, Any]:
    start = time.time()
    while time.time() - start < timeout:
        tree = compositor.get_tree()
        node = find_node_by_title_contains(tree, title_sub)
        if node:
            return node
        time.sleep(0.2)
    raise RuntimeError(f"Client with title containing '{title_sub}' did not map.")
