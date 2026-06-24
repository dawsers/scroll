import subprocess
import os
import pytest
from typing import Generator
from pathlib import Path
from test_utils import ScrollInstance, run_compositor


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--scroll", help="the scroll binary to test", default=None)


@pytest.fixture(scope="session")
def scroll_compositor_binary(request: pytest.FixtureRequest) -> str:
    binary_path: str = request.config.getoption("scroll")
    if not binary_path:
        # Auto-build using Meson/Ninja
        print("\nBuilding scroll with Meson/Ninja...")
        build_dir = os.path.abspath("./build")
        if not os.path.exists(build_dir):
            res = subprocess.run(
                ["meson", "setup", "build", "-Dwerror=false", "-Db_sanitize=address"],
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
                ["meson", "configure", "build", "-Db_sanitize=address"],
                capture_output=True,
                text=True,
            )
            if res.returncode != 0:
                pytest.exit(
                    f"Failed to configure build with ASan:\nStdout: {res.stdout}\nStderr: {res.stderr}"
                )

        # Run ninja to compile (incremental build)
        res = subprocess.run(["ninja", "-C", "build"], capture_output=True, text=True)
        if res.returncode != 0:
            pytest.exit(
                f"Failed to build scroll:\nStdout: {res.stdout}\nStderr: {res.stderr}"
            )

        binary_path = os.path.join(build_dir, "sway", "scroll")
    else:
        binary_path = os.path.abspath(binary_path)

    assert os.path.exists(binary_path), f"Binary not found at {binary_path}"
    return binary_path


@pytest.fixture(scope="session")
def scroll_compositor(
    scroll_compositor_binary: str, tmp_path_factory: pytest.TempPathFactory
) -> Generator[ScrollInstance, None, None]:
    temp_dir: Path = tmp_path_factory.mktemp("scroll")
    with run_compositor(scroll_compositor_binary, temp_dir) as inst:
        yield inst


@pytest.fixture(scope="function")
def fresh_compositor(
    scroll_compositor_binary: str, tmp_path: Path
) -> Generator[ScrollInstance, None, None]:
    with run_compositor(scroll_compositor_binary, tmp_path) as inst:
        yield inst
