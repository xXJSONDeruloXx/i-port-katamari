#include "so_util.h"

extern DynLibFunction symtable_libc[];
extern DynLibFunction symtable_gles2[];
extern DynLibFunction symtable_egl[];
extern DynLibFunction symtable_log[];
extern DynLibFunction symtable_android[];
extern DynLibFunction symtable_zlib[];
extern DynLibFunction symtable_open_citadel_runtime[];
extern DynLibFunction symtable_libm[];
extern DynLibFunction symtable_time[];
extern DynLibFunction symtable_pthread[];
extern DynLibFunction symtable_sem[];
extern DynLibFunction symtable_stat[];
extern DynLibFunction symtable_io[];
extern DynLibFunction symtable_unwind[];
extern DynLibFunction symtable_bionic[];
extern DynLibFunction symtable_off[];
extern DynLibFunction symtable_setjmp[];

/*
 * Epic Citadel has a much cleaner dependency graph than the Katamari donor:
 * UE3 uses GLES2 and only eglGetProcAddress directly, while libandroid is
 * limited to AAssetManager.
 */
const char *so_builtin_libs[] = {
    "libc.so",
    "libdl.so",
    "libm.so",
    "libstdc++.so",
    "liblog.so",
    "libz.so",
    "libEGL.so",
    "libGLESv2.so",
    "libandroid.so",
    nullptr,
};

DynLibFunction *so_static_patches[] = {
    nullptr,
};

DynLibFunction *so_dynamic_libraries[] = {
    symtable_time,
    symtable_pthread,
    symtable_sem,
    symtable_setjmp,
    symtable_stat,
    symtable_io,
    symtable_off,
    symtable_bionic,
#if defined(__arm__)
    symtable_unwind,
#endif
    symtable_open_citadel_runtime,
    symtable_android,
    symtable_log,
    symtable_egl,
    symtable_zlib,
    symtable_gles2,
    symtable_libm,
    symtable_libc,
    nullptr,
};
