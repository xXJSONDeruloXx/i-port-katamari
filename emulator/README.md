# Katamari local emulator control

This is the interactive qemu-arm/Mesa development harness for the Katamari
native host. It keeps the original ARM game loop alive and exposes a small
file-backed control channel for touch, buttons, accelerometer mode, screenshots,
and quit.
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

The runner builds with the `katamari-build` Docker image by default. Set
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
./emulator/send.sh button l2 down   # flip 180
./emulator/send.sh button l2 up
./emulator/send.sh button r2 down   # D-pad accelerometer mode
./emulator/send.sh button left down
./emulator/send.sh button left up
./emulator/send.sh button r2 up
./emulator/send.sh screenshot gameplay
./emulator/send.sh quit
```

The loader consumes at most one command per game frame, so a down/up pair
cannot collapse into a zero-duration event. The same protocol is used by the
MCP server in `mcp_server.py`.

The bridge maps controls as follows:

- D-pad: move the software touch pointer
- A: touch at the pointer
- B: rapidly tap at the pointer while held; Android Back is not sent
- X/L2: tap the in-game reverse/turn control
- Y/R2: toggle accelerometer mode
- Select: native Android select key
- L1/R1: hold a touch at the left/right screen edge to strafe
- R2/Y: toggle accelerometer mode; the D-pad or left analog stick supplies
  tilt movement
- Start: tap the in-game pause button directly and show the pointer
- Mouse events: direct touch input when a windowed SDL driver is available

The logical game panel is 640x480. `KATAMARI_AUTOPILOT=1` runs a conservative
boot/menu/input smoke sequence. `KATAMARI_INPUTTRACE=1` logs every synthetic
touch event and the native touch queue while diagnosing input. Use
`KATAMARI_AUTO_SCREENSHOT_FRAMES=...` for frame checkpoints.

## MCP smoke test

The server uses only Python's standard library and speaks MCP JSON-RPC over
stdio. Its tools are `start_emulator`, `stop_emulator`, `emulator_status`,
`move_cursor`, `click`, `press_control`, `capture_screen`, and
`read_emulator_log`.

Run the protocol check with `KATAMARI_GAMEDIR` pointing at a writable extracted
donor:

```bash
KATAMARI_GAMEDIR=/path/to/katamari ./emulator/smoke_test_mcp.py
```

The smoke test starts qemu, exercises the D-pad accelerometer mode and edge
touches, captures a PNG, and stops the process it created.
