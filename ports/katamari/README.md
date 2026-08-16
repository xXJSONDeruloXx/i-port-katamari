# Katamari Damacy — native PortMaster port

This port runs the original ARMv5 native Katamari game from the Android APK
directly on a Linux/ARM handheld. It does not include Android, an emulator, or
the proprietary game data.

Bring your own copy of the supported Android build:

```text
package: com.namcobandaigames.katamari
native:  lib/armeabi/libkatamari.so
native SHA-256: 86bbb9a6446c7264c6d333f91c2d5ea9cab62dc551eb581862e188460d17cef7
```

## Install

1. Install `katamari-portmaster.zip` through PortMaster's `autoinstall`
   directory and reboot the frontend after the install completes.
2. Put your APK, ZIP, or an extracted APK folder in the installed
   `ports/katamari/` directory. The donor filename does not matter.
3. Launch **Katamari Damacy**. The first launch uses `eapx.py` to discover the
   donor, validate the native library and assets, and publish them atomically.
   Keep at least 200 MiB free while it stages the data.

The tested APK is approximately 38 MiB and publishes about 58 MiB of game
files. Once the import succeeds, the donor can remain in place as a backup or
be removed to reclaim its space. Saves are stored in `ports/katamari/var/`.

## Controls

The game exposes touch and accelerometer entry points rather than an Android
input service. The port maps handheld controls into those same native calls:

- D-pad: move the on-screen pointer
- A or X: tap at the pointer
- B: unbound; Android Back is not sent
- Select: native Android select key
- L1/R1: hold a touch at the left/right screen edge to strafe
- Start: tap the in-game pause button directly and restore the pointer
- L2: tap the in-game reverse/turn control
- R2: toggle accelerometer mode; the D-pad supplies tilt movement
- Analog sticks: unused; the game reads movement from its accelerometer path
- Mouse/touchscreen input, when available, is passed through directly

The pointer is drawn by the host in the game's fixed 640x480 logical space.
`KATAMARI_SCALE=fit|stretch|integer` can be used for other panel sizes.

## Troubleshooting

Each run replaces `ports/katamari/log.txt`; the first-boot importer writes
`ports/katamari/eapx.log`. Include both logs with the device and firmware when
reporting a problem. Common causes are an APK without the expected ARM
library, missing `assets/fat.bin`, no 32-bit GL provider, or insufficient free
space for the transactional first import.

## Credits and licence

Katamari Damacy is © Namco Bandai Games. The original game library is not
distributed by this project. The host and compatibility code are GPL-3.0.

The ARM ELF/JNI/libc loader is based on gmloader-next and Vita so-loader work.
Fixed-function texture support includes the PowerVR decoder. MP3 playback uses
the Debian `libmpg123` runtime carried by the PortMaster package; its license
notice is included under `licenses/`.
