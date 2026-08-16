# i-port-katamari

Katamari Damacy running its original ARMv5 Android engine on Linux/ARM
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

## Install

Install the ZIP with PortMaster, reboot the frontend, then place your own
compatible APK, ZIP, or extracted tree in `ports/katamari/`. Launch Katamari;
the first run validates and imports the donor. Releases are at
<https://github.com/xXJSONDeruloXx/i-port-katamari/releases>.

## Controls

D-pad moves the pointer; A taps; B rapidly taps at the pointer while held; X
and L2 tap the in-game reverse/turn control; and Select sends Android select.
L1/R1 hold a touch at the left/right screen edge for strafing. Start pauses
through the in-game button. Y and R2 toggle accelerometer mode, where the
D-pad or left analog stick supplies tilt movement.

For the qemu/Mesa harness, see [`emulator/README.md`](emulator/README.md).
