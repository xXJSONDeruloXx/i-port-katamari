#!/bin/bash
# PORTMASTER: katamari-portmaster.zip, Katamari.sh

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
