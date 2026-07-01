#!/bin/sh -e

# Find project root
SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

cd "$PROJECT_ROOT"

# Ensure we are in a nix develop shell or similar if needed
if ! command -v meson >/dev/null 2>&1; then
    echo "Error: meson not found." >&2
    echo "Please run this script inside 'nix develop' or ensure development dependencies are installed." >&2
    exit 1
fi

echo "Building scroll with ASan enabled..."
if [ ! -d build ]; then
    meson setup build -Dwerror=false -Db_sanitize=address
else
    meson configure build -Db_sanitize=address
fi
ninja -C build

# Put built binaries in PATH
BUILD_DIR="$PROJECT_ROOT/build"
export PATH="$BUILD_DIR/sway:$BUILD_DIR/swaymsg:$BUILD_DIR/swaybar:$BUILD_DIR/swaynag:$PATH"

DEFAULT_CONFIG="$PROJECT_ROOT/config.in"

# Check if user provided a config file via -c or --config
has_config=false
for arg in "$@"; do
    case "$arg" in
        -c|--config)
            has_config=true
            break
            ;;
    esac
done

if [ "$has_config" = "false" ]; then
    CONFIG="$DEFAULT_CONFIG"
    if [ -n "${SCROLL_CONFIG:-}" ]; then
        CONFIG="$SCROLL_CONFIG"
    fi
    echo "Using default config: $CONFIG"
    # Prepend -c <config> to arguments
    set -- -c "$CONFIG" "$@"
fi

# Disable leak detection to suppress leak errors in the interactive test.
export ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
export LSAN_OPTIONS="suppressions=$PROJECT_ROOT/tests/lsan.supp${LSAN_OPTIONS:+:$LSAN_OPTIONS}"

echo "Starting scroll..."
echo "PATH is set to: $PATH"
exec scroll "$@"
