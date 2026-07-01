import subprocess
import os
import pytest
from typing import Generator
from pathlib import Path
from test_utils import ScrollInstance, run_compositor, ScrollCompositorFactory


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--scroll", help="the scroll binary to test", default=None)


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(
    item: pytest.Item, call: pytest.CallInfo
) -> Generator[None, None, None]:
    outcome = yield
    rep = outcome.get_result()
    setattr(item, "result_" + rep.when, rep)


def _build_scroll() -> str:
    # Auto-build using Meson/Ninja
    print("\nBuilding scroll with Meson/Ninja...")
    build_dir = os.path.abspath("./build")
    if not os.path.exists(build_dir):
        res = subprocess.run(
            [
                "meson",
                "setup",
                "build",
                "-Dwerror=false",
                "-Db_sanitize=address",
                "-Dbuildtype=debugoptimized",
            ],
            capture_output=True,
            text=True,
        )
        if res.returncode != 0:
            pytest.exit(
                f"Failed to setup build:\nStdout: {res.stdout}\nStderr: {res.stderr}"
            )
    else:
        # Ensure ASan is enabled
        res = subprocess.run(
            [
                "meson",
                "configure",
                "build",
                "-Db_sanitize=address",
                "-Dbuildtype=debugoptimized",
            ],
            capture_output=True,
            text=True,
        )
        if res.returncode != 0:
            pytest.exit(
                "Failed to configure build with ASan:\nStdout:"
                f" {res.stdout}\nStderr: {res.stderr}"
            )

    # Run ninja to compile (incremental build)
    res = subprocess.run(["ninja", "-C", "build"], capture_output=True, text=True)
    if res.returncode != 0:
        pytest.exit(
            f"Failed to build scroll:\nStdout: {res.stdout}\nStderr: {res.stderr}"
        )

    return os.path.join(build_dir, "sway", "scroll")


@pytest.fixture(scope="session")
def scroll_compositor_binary(request: pytest.FixtureRequest) -> str:
    binary_path: str = request.config.getoption("scroll")
    if not binary_path:
        # Check if we are running under xdist
        try:
            worker_id = request.getfixturevalue("worker_id")
        except Exception:
            worker_id = "master"

        if worker_id == "master":
            binary_path = _build_scroll()
        else:
            tmp_path_factory = request.getfixturevalue("tmp_path_factory")
            shared_dir = tmp_path_factory.getbasetemp().parent
            lock_path = shared_dir / "scroll_build.lock"
            status_path = shared_dir / "scroll_build.status"

            import fcntl

            # Open with 'a' to avoid truncating while another process might have it locked
            with open(lock_path, "a") as lock_file:
                fcntl.flock(lock_file, fcntl.LOCK_EX)
                try:
                    if status_path.exists():
                        binary_path = status_path.read_text().strip()
                    else:
                        binary_path = _build_scroll()
                        status_path.write_text(binary_path)
                finally:
                    fcntl.flock(lock_file, fcntl.LOCK_UN)
    else:
        binary_path = os.path.abspath(binary_path)

    assert os.path.exists(binary_path), f"Binary not found at {binary_path}"

    # Set up PATH to include build directories so that the compositor can find
    # our newly built scrollbar, swaymsg, swaynag, etc.
    build_dir = Path(binary_path).parent.parent
    old_path = os.environ.get("PATH", "")
    build_paths = [
        str(build_dir / "sway"),
        str(build_dir / "swaymsg"),
        str(build_dir / "swaybar"),
        str(build_dir / "swaynag"),
    ]
    os.environ["PATH"] = ":".join(build_paths) + ":" + old_path

    return binary_path


@pytest.fixture(scope="session")
def scroll_compositor_factory(
    scroll_compositor_binary: str, tmp_path_factory: pytest.TempPathFactory
) -> Generator[ScrollCompositorFactory, None, None]:
    temp_dir: Path = tmp_path_factory.mktemp("scroll")
    with run_compositor(scroll_compositor_binary, temp_dir) as inst:
        yield ScrollCompositorFactory(inst)


@pytest.fixture(scope="function")
def scroll_compositor(
    request: pytest.FixtureRequest,
    scroll_compositor_factory: ScrollCompositorFactory,
) -> Generator[ScrollInstance, None, None]:
    ctx = scroll_compositor_factory()
    inst = ctx.__enter__()
    try:
        yield inst
    finally:
        failed = (
            hasattr(request.node, "result_call") and request.node.result_call.failed
        )
        if failed:
            scroll_compositor_factory.print_log()
        ctx.__exit__(None, None, None)


@pytest.fixture(scope="function")
def fresh_compositor_factory(
    scroll_compositor_binary: str, tmp_path: Path
) -> Generator[ScrollCompositorFactory, None, None]:
    with run_compositor(scroll_compositor_binary, tmp_path) as inst:
        yield ScrollCompositorFactory(inst)


@pytest.fixture(scope="function")
def fresh_compositor(
    request: pytest.FixtureRequest,
    fresh_compositor_factory: ScrollCompositorFactory,
) -> Generator[ScrollInstance, None, None]:
    ctx = fresh_compositor_factory()
    inst = ctx.__enter__()
    try:
        yield inst
    finally:
        failed = (
            hasattr(request.node, "result_call") and request.node.result_call.failed
        )
        if failed:
            fresh_compositor_factory.print_log()
        ctx.__exit__(None, None, None)
