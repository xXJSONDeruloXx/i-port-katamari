# Third-party code and licensing

This Katamari PortMaster host is released under **GPL-3.0**; see `LICENSE`.
The repository contains compatibility code derived from the projects below.
Their notices remain applicable to the corresponding source files.

| Component | Origin | Licence |
|---|---|---|
| `loader/`, `thunks/libc/` | gmloader-next and the Vita so-loader by Andy Nguyen | GPL; see bundled notice |
| `thunks/libc/time64.cpp` | y2038 by Michael G Schwern | MIT / Artistic |
| `thunks/libc/fortify.cpp`, `jni/jni.h` | Android Open Source Project | Apache-2.0 / BSD |
| `loader/leb128.h` | GNU binutils | GPL |
| `thunks/khronos/` | glad and Khronos headers | MIT / Apache-2.0 |
| `third_party/powervr/PVRTDecompress.*` | Imagination Technologies PowerVR SDK | MIT |
| `src/vfp_vector_patch.cpp` | VFPVector by Bythos14 | MIT |
| `tools/eapx.py` | written for this project | GPL-3.0 |

The ARM shared libraries under `libs.armhf/` are copied from Debian armhf
packages by `tools/collect_libs.sh`. The release ZIP includes the exact Debian
copyright file for each bundled SONAME under `licenses/libraries/`, including
the `libmpg123` MP3 decoder used by Katamari's audio bridge.

## Game data

Katamari Damacy and its Android native library are © Namco Bandai Games. No APK,
native game library, music, texture, or other proprietary game data is shipped
by this repository. The PortMaster launcher asks the user to provide a donor
they are entitled to use and extracts it locally on first launch.
