# i-port-katamari

I Love Katamari running its original ARMv5 Android engine on Linux/ARM
handhelds through a native ELF/JNI/GLES host. The repository contains the
loader, SDL input/audio bridge, APK data importer, emulator tools, and
PortMaster packaging. It does not contain the APK or proprietary game data.

## Build

```bash
docker build -t katamari-build -f Dockerfile.build .
docker run --rm -v "$PWD":/src -w /src katamari-build make -j4
docker run --rm -v "$PWD":/src -w /src katamari-build make libs
bash package_portmaster.sh
```

The game-data-free package is written to `build/katamari-portmaster.zip`.

## Supported game version

This port is intended for **I Love Katamari (English)**, the Android/iOS game.
It uses the Android version, whose file must be named
`MMkatamari-englishhack.apk`.

## Install

1. Unzip the release ZIP.
2. Copy the extracted `katamari/` folder into the handheld's `ports/`
   directory.
3. Copy `MMkatamari-englishhack.apk` into `ports/katamari/`.
4. Copy `Katamari.sh` into `ROMS/PORTS/`.
5. Launch I Love Katamari from the frontend. The first launch validates and
   imports the APK data.

Releases are at
<https://github.com/xXJSONDeruloXx/i-port-katamari/releases>.

## Controls

D-pad moves the pointer; A taps; B rapidly taps screen center while held; X
and L2 tap the in-game reverse/turn control; and Select sends Android select.
L1/R1 hold a touch at the left/right screen edge for strafing. Start pauses
through the in-game button. Y and R2 toggle accelerometer mode, where the
D-pad or left analog stick supplies tilt movement.

For the qemu/Mesa harness, see [`emulator/README.md`](emulator/README.md).
