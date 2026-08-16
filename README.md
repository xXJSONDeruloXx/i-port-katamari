# Katamari Damacy — native ARM PortMaster port

This branch adapts the reusable ARM/JNI/GLES loader in this repository for the
Katamari Android build. It loads the original ARMv5 native engine directly on
Linux handhelds; no Android runtime or emulator is involved.

The project contains the loader, host lifecycle, filesystem-backed JNI helpers,
SDL controller bridge, fixed-function GLES path, and PortMaster packaging. It
does not contain the game APK or any proprietary game data.

## Supported donor

The tested donor is the user-supplied `MMkatamari-englishhack.apk`:

```text
package: com.namcobandaigames.katamari
version: 1.0.0
APK SHA-256: b364519c270300065e9f4fd8b9b214e571c74d166b3e8620ade03cdf2122bed2
native: lib/armeabi/libkatamari.so
native SHA-256: 86bbb9a6446c7264c6d333f91c2d5ea9cab62dc551eb581862e188460d17cef7
```

The first launch uses `tools/eapx.py` and the recipe in
`ports/katamari/katamari.eapx.json` to find an APK, ZIP, or extracted folder by
its contents. It validates the ARM library and the game-data tree before
publishing `lib/`, `assets/`, and optional `res/` files atomically.

## Build

The pinned Docker image is the supported build and qemu test environment:

```bash
docker build -t deadspace-build -f Dockerfile.build .
docker run --rm -v "$PWD":/src -w /src deadspace-build make -j4
docker run --rm -v "$PWD":/src -w /src deadspace-build make libs
bash package_portmaster.sh
```

The release ZIP is written to `build/katamari-portmaster.zip`. It contains the
host binary, ARM support libraries, extractor, metadata, and licenses only.
The user's APK is never copied into the package.

## Local run

Extract the donor to a directory with this shape:

```text
katamari/
├── assets/
│   ├── fat.bin
│   └── sound/
├── lib/armeabi/libkatamari.so
└── res/raw/                 # optional resources from the APK
```

Then run the ARM binary in the build container:

```bash
docker run --rm \
  -e SDL_VIDEODRIVER=offscreen \
  -e SDL_AUDIODRIVER=dummy \
  -e LOADER_TRACE=1 \
  -e KATAMARI_AUTOPILOT=1 \
  -e KATAMARI_FRAME_LIMIT=1000 \
  -v "$PWD":/src -v /path/to/katamari:/katamari -w /src \
  deadspace-build ./build/katamari /katamari
```

The harness reaches the original activity lifecycle, relocates every native
import, loads the real title/menu data, renders GLES 1.1 textures and draw
calls, writes saves, and decodes both the WAV effects and MP3 music through the
SDL mixer. The qemu test is a boot/runtime check; a complete hardware
play-through still needs validation on a target handheld.

## PortMaster install

1. Put `build/katamari-portmaster.zip` in PortMaster's `autoinstall/`
   directory and wait for the install to finish.
2. Reboot the frontend through the firmware menu.
3. Put your APK, ZIP, or extracted donor in the installed
   `ports/katamari/` directory.
4. Launch **Katamari Damacy**. The first launch imports and validates the donor.

Keep at least 200 MiB free during the first import. Saves are kept in
`ports/katamari/var/`. See `ports/katamari/README.md` for the full user-facing
instructions and troubleshooting notes.

## Controls

The Android shell exposes touch and accelerometer JNI methods directly. The
host maps those calls as follows:

- D-pad: move the software pointer
- A/X: tap at the pointer
- B: Android back
- Left/right sticks: virtual touch sticks
- L1/R1: simulated tilt
- Start: restore the pointer
- Mouse/touchscreen events: direct touch input when available

The logical game panel is 640x480. Set `KATAMARI_SCALE=fit`, `stretch`, or
`integer` for a different display shape.

## Repository layout

- `loader/`, `thunks/`, `jni/`: reusable ARM ELF and JNI compatibility layer
- `src/main.cpp`: Katamari lifecycle host and render loop
- `jni/classes/katamari.cpp`: asset/save JNI and SDL audio bridge
- `android/katamari_input.cpp`: controller, pointer, and accelerometer bridge
- `ports/katamari/`: PortMaster launcher, recipe, metadata, and documentation
- `package_portmaster.sh`: game-data-free release builder

The original Dead Space-specific sources remain in the historical loader base
because several low-level components are shared; the active entry point and
release packaging on this branch are Katamari-specific.
