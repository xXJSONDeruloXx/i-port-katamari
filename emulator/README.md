# Katamari local emulator control

This is the interactive qemu-arm/Mesa development harness for the Katamari
native host. It keeps the original ARM game loop alive and exposes a small
file-backed control channel for touch, buttons, sticks, screenshots, and quit.
The APK and game data are never included in the repository or sent through the
control channel.

## Start

Extract the donor APK into a writable directory with this shape:

```text
katamari/
├── assets/fat.bin
└── lib/armeabi/libkatamari.so
```

Then start the emulator:

```bash
./emulator/run.sh --game-dir /path/to/katamari
```

The default game directory is `../NeededFiles/data/katamari` relative to the
repository. `KATAMARI_GAMEDIR` and `KATAMARI_CONTROL_DIR` override the defaults.
The game directory is mounted writable because the native save code creates
`var/savedata.dat` during normal navigation.

The runner builds with the `deadspace-build` Docker image by default. Set
`KATAMARI_BUILD_IMAGE` to use another local image. Set
`KATAMARI_EMULATOR_SKIP_BUILD=1` when the current `build/katamari` is already
available.

## Manual controls

The default control directory is `emulator/runtime/`. It contains:

- `commands`: newline-delimited input commands;
- `status.json`: current state and frame number;
- `screenshots/*.png`: real framebuffer captures;
- `emulator.log`: created by the MCP wrapper.

Examples:

```bash
./emulator/send.sh cursor 320 240
./emulator/send.sh click down
./emulator/send.sh click up
./emulator/send.sh button start down
./emulator/send.sh button start up
./emulator/send.sh button l2 down   # simulated tilt
./emulator/send.sh button l2 up
./emulator/send.sh stick left 1 0
./emulator/send.sh stick left 0 0
./emulator/send.sh screenshot gameplay
./emulator/send.sh quit
```

The loader consumes at most one command per game frame, so a down/up pair
cannot collapse into a zero-duration event. The same protocol is used by the
MCP server in `mcp_server.py`.

The bridge maps controls as follows:

- D-pad: move the software touch pointer
- A/X: touch at the pointer
- B: Android back
- Select: native Android select key
- Left/right sticks: virtual touch sticks for rolling
- L1/R1: simulated accelerometer tilt
- L2/R2: toggle digital rolling mode; D-pad becomes the left virtual stick and
  X/B/A/Y become the right virtual stick (up/right/down/left)
- Start: show the pointer
- Mouse events: direct touch input when a windowed SDL driver is available

The logical game panel is 640x480. `KATAMARI_AUTOPILOT=1` runs a conservative
boot/menu/input smoke sequence. `KATAMARI_INPUTTRACE=1` logs every synthetic
touch event and the native touch queue while diagnosing input. Use
`KATAMARI_AUTO_SCREENSHOT_FRAMES=...` for frame checkpoints.

## MCP smoke test

The server uses only Python's standard library and speaks MCP JSON-RPC over
stdio. Its tools are `start_emulator`, `stop_emulator`, `emulator_status`,
`move_cursor`, `click`, `press_control`, `set_stick`, `capture_screen`, and
`read_emulator_log`.

Run the protocol check with `KATAMARI_GAMEDIR` pointing at a writable extracted
donor:

```bash
KATAMARI_GAMEDIR=/path/to/katamari ./emulator/smoke_test_mcp.py
```

The smoke test starts qemu, sends a stick command, captures a PNG, and stops
the process it created.
