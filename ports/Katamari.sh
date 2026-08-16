#!/bin/bash
# PORTMASTER: katamari-portmaster.zip, Katamari.sh
#
# Katamari Damacy (Android 1.0.0) — PortMaster launcher.
#
# The release contains the native loader and its open-source runtime only.
# The user's own APK is imported on first launch by the bundled eapx engine:
#
#   ports/katamari/MMkatamari-englishhack.apk
#
# The imported tree is kept in the same directory so saves and the donor data
# remain on the user's SD card, independent of the PortMaster ZIP itself.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source "$controlfolder/control.txt"

export PORT_32BIT="Y"
[ -f "$controlfolder/tasksetter" ]          && source "$controlfolder/tasksetter"
[ -f "$controlfolder/device_info.txt" ]     && source "$controlfolder/device_info.txt"
[ -f "$controlfolder/mod_${CFW_NAME}.txt" ] && source "$controlfolder/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR="/$directory/ports/katamari"
cd "$GAMEDIR" || exit 1

: > "$GAMEDIR/log.txt"
exec > "$GAMEDIR/log.txt" 2>&1

GAME="$GAMEDIR/katamari"

# muOS on the H700 handhelds keeps the usable 32-bit SDL/GL stack in
# /usr/lib32.  Inheriting PortMaster's full library path can put frontend
# libraries ahead of that stack and crash before the native loader starts.
# Restrict this workaround to muOS; other CFWs keep the old search order.
PORT_DEVICE_LIBDIR=""
case "${CFW_NAME:-}" in
  muOS|muos|MuOS)
    if [ -d /usr/lib32 ]; then
      PORT_DEVICE_LIBDIR="/usr/lib32"
      export LD_LIBRARY_PATH="$GAMEDIR/libs.armhf:/usr/lib32"
      echo "runtime: using muOS 32-bit libraries from /usr/lib32"
    fi
    ;;
esac

if [ -z "$PORT_DEVICE_LIBDIR" ]; then
  export LD_LIBRARY_PATH="$GAMEDIR/libs.armhf${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
export SDL_GAMECONTROLLERCONFIG="${sdl_controllerconfig:-}"
export KATAMARI_SCALE="${KATAMARI_SCALE:-fit}"
export LOADER_TRACE="${LOADER_TRACE:-1}"

$ESUDO chmod +x "$GAME" 2>/dev/null

# Some Mali-based CFWs ship a vendor SDL video backend instead of a usable
# KMSDRM default.  Ask the exact SDL linked by this binary before selecting it;
# an unconditional SDL_VIDEODRIVER=mali breaks devices whose SDL lacks it.
if [ -n "$PORT_DEVICE_LIBDIR" ]; then
  SDL_INFO=$("$GAME" --sdl-info 2>&1)
  SDL_INFO_RC=$?
  printf '%s\n' "$SDL_INFO" | sed 's/^/SDL: /'
  if [ "$SDL_INFO_RC" -eq 0 ] && \
     printf '%s\n' "$SDL_INFO" | grep -q '^sdl: video driver: mali$'; then
    export SDL_VIDEODRIVER="mali"

    if [ -e "$PORT_DEVICE_LIBDIR/libEGL.so" ]; then
      export SDL_VIDEO_EGL_DRIVER="$PORT_DEVICE_LIBDIR/libEGL.so"
    elif [ -e "$PORT_DEVICE_LIBDIR/libEGL.so.1" ]; then
      export SDL_VIDEO_EGL_DRIVER="$PORT_DEVICE_LIBDIR/libEGL.so.1"
    fi
    if [ -e "$PORT_DEVICE_LIBDIR/libGLESv2.so" ]; then
      export SDL_VIDEO_GL_DRIVER="$PORT_DEVICE_LIBDIR/libGLESv2.so"
    elif [ -e "$PORT_DEVICE_LIBDIR/libGLESv2.so.2" ]; then
      export SDL_VIDEO_GL_DRIVER="$PORT_DEVICE_LIBDIR/libGLESv2.so.2"
    fi

    echo "video: selected SDL mali backend"
    echo "video: EGL=${SDL_VIDEO_EGL_DRIVER:-default} GL=${SDL_VIDEO_GL_DRIVER:-default}"
  else
    echo "video: SDL mali backend unavailable; keeping the CFW default"
  fi
fi

if [ ! -f "$GAMEDIR/lib/armeabi/libkatamari.so" ] || \
   [ ! -f "$GAMEDIR/assets/fat.bin" ]; then
  if ! command -v python3 >/dev/null 2>&1; then
    echo "Katamari data import failed: python3 is unavailable"
    pm_finish
    exit 1
  fi
  mkdir -p "$GAMEDIR/gamedata"
  if ! python3 "$GAMEDIR/eapx.py" install \
       --recipe "$GAMEDIR/katamari.eapx.json" \
       --game-dir "$GAMEDIR" --tty none; then
    echo "Katamari data import failed; put your own APK, ZIP, or extracted" \
         "game tree in $GAMEDIR and see eapx.log"
    pm_finish
    exit 1
  fi
fi

mkdir -p "$GAMEDIR/var"

if [ -n "${GPTOKEYB:-}" ] && [ -f "$GAMEDIR/katamari.gptk" ]; then
  $GPTOKEYB "$GAME" -c "$GAMEDIR/katamari.gptk" &
  GPTOKEYB_PID=$!
fi

if command -v pm_platform_helper >/dev/null 2>&1; then
  pm_platform_helper "$GAME"
fi

if [ -n "${TASKSET:-}" ]; then
  $TASKSET "$GAME" "$GAMEDIR"
else
  "$GAME" "$GAMEDIR"
fi
GAME_RC=$?

if [ -n "${GPTOKEYB_PID:-}" ]; then
  $ESUDO kill -9 "$GPTOKEYB_PID" 2>/dev/null
fi

unset LD_LIBRARY_PATH SDL_GAMECONTROLLERCONFIG SDL_VIDEODRIVER \
      SDL_VIDEO_EGL_DRIVER SDL_VIDEO_GL_DRIVER
pm_finish
exit "$GAME_RC"
