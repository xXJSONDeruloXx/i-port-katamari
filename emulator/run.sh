#!/usr/bin/env bash
#
# Interactive, host-controlled Katamari emulator.
#
# The game still runs through qemu-arm and Mesa/llvmpipe, as in the immutable
# verifier, but it has no fixed frame limit and mounts a bidirectional control
# directory. emulator_control.cpp consumes commands from that directory and
# writes PNG screenshots/status back to it.
set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME_DIR="${KATAMARI_GAMEDIR:-$PORT_DIR/../NeededFiles/data/katamari}"
CONTROL_DIR="${KATAMARI_CONTROL_DIR:-$PORT_DIR/emulator/runtime}"
IMAGE="${KATAMARI_BUILD_IMAGE:-deadspace-build}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --game-dir)
            GAME_DIR="${2:?--game-dir needs a path}"
            shift 2
            ;;
        --control-dir)
            CONTROL_DIR="${2:?--control-dir needs a path}"
            shift 2
            ;;
        *)
            echo "unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

if [ ! -d "$GAME_DIR" ]; then
    echo "game directory not found at $GAME_DIR" >&2
    exit 2
fi
GAME_DIR="$(cd "$GAME_DIR" && pwd -P)"
mkdir -p "$CONTROL_DIR"
CONTROL_DIR="$(cd "$CONTROL_DIR" && pwd -P)"

# Docker Desktop on macOS does not share /tmp with its VM coherently: the bind
# mount is accepted, the container writes into it, and the host sees an empty
# directory. Every byte of the control channel - commands, status.json,
# screenshots - is a file in this directory, so the symptom is an emulator that
# starts, runs, and never becomes ready, with nothing anywhere saying why. It
# cost an afternoon once; refusing is cheaper than diagnosing it twice.
if [ "$(uname -s)" = "Darwin" ]; then
    _probe="$CONTROL_DIR"
    while [ -n "$_probe" ] && [ ! -d "$_probe" ]; do
        _probe="$(dirname "$_probe")"
    done
    _resolved="$(cd "$_probe" 2>/dev/null && pwd -P || echo "$CONTROL_DIR")"
    case "$_resolved" in
        /tmp|/tmp/*|/private/tmp|/private/tmp/*)
            echo "refusing to use a control dir under /tmp: $CONTROL_DIR" >&2
            echo "  Docker Desktop on macOS does not share /tmp with the container," >&2
            echo "  so the emulator would write status files this side never sees." >&2
            echo "  Use a path inside the port tree, e.g. $PORT_DIR/emulator/runtime." >&2
            exit 2
            ;;
    esac
fi

if [ ! -f "$GAME_DIR/lib/armeabi/libkatamari.so" ] ||
   [ ! -f "$GAME_DIR/assets/fat.bin" ]; then
    echo "game tree not found at $GAME_DIR" >&2
    exit 2
fi

mkdir -p "$CONTROL_DIR/screenshots"
: > "$CONTROL_DIR/commands"
rm -f "$CONTROL_DIR/status.json" "$CONTROL_DIR/status.json.tmp"

if [ "${KATAMARI_EMULATOR_SKIP_BUILD:-0}" != "1" ]; then
    docker run --rm \
        -v "$PORT_DIR":/src \
        -w /src \
        "$IMAGE" make -j4
fi

# Katamari writes var/savedata.dat while entering a stage, so the donor tree
# must be writable for an interactive run.
exec docker run --rm \
    -v "$PORT_DIR":/src \
    -v "$GAME_DIR":/game \
    -v "$CONTROL_DIR":/control \
    -w /src \
    -e SDL_VIDEODRIVER=offscreen \
    -e SDL_AUDIODRIVER=dummy \
    -e LIBGL_ALWAYS_SOFTWARE=1 \
    -e GALLIUM_DRIVER=llvmpipe \
    -e EGL_PLATFORM=surfaceless \
    -e LOADER_TRACE=1 \
    -e KATAMARI_AUTOPILOT="${KATAMARI_AUTOPILOT:-}" \
    -e KATAMARI_INPUTTRACE="${KATAMARI_INPUTTRACE:-}" \
    -e KATAMARI_FRAME_LIMIT="${KATAMARI_FRAME_LIMIT:-}" \
    -e KATAMARI_CONTROL_DIR=/control \
    -e KATAMARI_AUTO_SCREENSHOT_FRAME="${KATAMARI_AUTO_SCREENSHOT_FRAME:-}" \
    -e KATAMARI_AUTO_SCREENSHOT_FRAMES="${KATAMARI_AUTO_SCREENSHOT_FRAMES:-}" \
    "$IMAGE" \
    qemu-arm -L /usr/arm-linux-gnueabihf \
        ./build/katamari /game
