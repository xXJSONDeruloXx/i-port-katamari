# I Love Katamari — native PortMaster port

This port runs the original ARMv5 native Katamari game from the Android APK
directly on a Linux/ARM handheld. It does not include Android, an emulator, or
the proprietary game data.

Bring your own copy of the supported **I Love Katamari (English)** Android/iOS
game. This port uses its Android build, whose APK filename must be
`MMkatamari-englishhack.apk`:

```text
package: com.namcobandaigames.katamari
native:  lib/armeabi/libkatamari.so
native SHA-256: 86bbb9a6446c7264c6d333f91c2d5ea9cab62dc551eb581862e188460d17cef7
```

## Install

1. Unzip the release ZIP.
2. Copy the extracted `katamari/` folder into the handheld's `ports/`
   directory.
3. Copy `MMkatamari-englishhack.apk` into `ports/katamari/`.
4. Copy `Katamari.sh` into `ROMS/PORTS/`.
5. Launch **I Love Katamari**. The first launch validates the native library
   and assets, then imports the APK data atomically.

Keep at least 200 MiB free while it stages the data.

The tested APK is approximately 38 MiB and publishes about 58 MiB of game
files. Once the import succeeds, the donor can remain in place as a backup or
be removed to reclaim its space. Saves are stored in `ports/katamari/var/`.

## Controls

The game exposes touch and accelerometer entry points rather than an Android
input service. The port maps handheld controls into those same native calls:

- D-pad: move the on-screen pointer
- A: tap at the pointer
- B: rapidly tap at the pointer while held; Android Back is not sent
- X or L2: tap the in-game reverse/turn control
- Y or R2: toggle accelerometer mode
- Select: native Android select key
- L1/R1: hold a touch at the left/right screen edge to strafe
- Start: tap the in-game pause button directly and restore the pointer
- R2/Y: toggle accelerometer mode; the D-pad or left analog stick supplies
  tilt movement
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

I Love Katamari is © Namco Bandai Games. The original game library is not
distributed by this project. The host and compatibility code are GPL-3.0.

The ARM ELF/JNI/libc loader is based on gmloader-next and Vita so-loader work.
Fixed-function texture support includes the PowerVR decoder. MP3 playback uses
the Debian `libmpg123` runtime carried by the PortMaster package; its license
notice is included under `licenses/`.
