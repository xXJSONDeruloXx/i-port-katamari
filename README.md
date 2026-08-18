# i-port-katamari

I Love Katamari ported for sbc handhelds via portmaster, with controller first optimizations.

This port is intended for **I Love Katamari (English)**, the Android/iOS game.
It uses the Android version, whose file must be named
`MMkatamari-englishhack.apk`.

I can not help you find this file, you're on your own

## Install

1. Unzip the release ZIP.
2. Copy the extracted `katamari/` folder into the handheld's `ports/`
   directory.
3. Copy `MMkatamari-englishhack.apk` into `ports/katamari/`.
4. Copy `Katamari.sh` into `ROMS/PORTS/`.
5. Launch I Love Katamari from the frontend.


## Controls
| Control | Action |
|---|---|
| D-pad | Move the on-screen pointer |
| A | Tap at the pointer |
| B | Tap the screen center once per press |
| X | Tap the reverse/turn control |
| L2 | Tap the reverse/turn control |
| Y | Toggle accelerometer mode |
| R2 | Toggle accelerometer mode |
| D-pad in accelerometer mode | Roll/tilt the katamari |
| L1 | Strafe left while held |
| R1 | Strafe right while held |
| Start | Pause through the in-game pause button |


D-pad moves the pointer; A taps; B taps screen center once per press; X
and L2 tap the in-game reverse/turn control; and Select sends Android select.
L1/R1 hold a touch at the left/right screen edge for strafing. Start pauses
through the in-game button. Y and R2 toggle accelerometer mode, where the
D-pad or left analog stick supplies tilt movement.

Tested on an RG28XX running MuOS, leveraging mode shift for switching D pad from cursor to acting as accelerometer. YMMV on devices with an analogue stick
