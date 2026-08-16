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

D-pad moves the pointer; A/X taps; B backs out; Start taps the in-game pause
button directly and restores the pointer;
Select sends Android select; L1/R1 simulate tilt; and the analog sticks roll
normally. L2 taps the in-game reverse/turn control; R2 toggles digital rolling
mode, where the D-pad becomes the left virtual stick and X/B/A/Y become the
right stick (up/right/down/left).

For the qemu/Mesa harness, see [`emulator/README.md`](emulator/README.md).
