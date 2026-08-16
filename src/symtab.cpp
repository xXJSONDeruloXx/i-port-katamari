/*
 *
 *
 * so_resolve_link() walks these in order and takes the first match, which is
 * why the port's own tables come before the generic bionic libc one.
 *
 * A symbol that is in none of them stays unresolved. The loader points it at a
 * PLT stub that aborts on first call, and src/main.cpp reports it by name
 * before anything runs - see report_unresolved_symbols().
 */
#include "so_util.h"
#include "thunk_gen.h"

extern DynLibFunction symtable_libc[];      /* thunks/libc/libc_table.cpp   */
extern DynLibFunction symtable_gles2[];     /* thunks/khronos/gles2.cpp     */
extern DynLibFunction symtable_gles1[];     /* thunks/khronos/gles1.cpp     */
extern DynLibFunction symtable_egl[];       /* android/egl_shim.cpp         */
extern DynLibFunction symtable_log[];       /* android/log.cpp              */
extern DynLibFunction symtable_libm[];      /* src/symtab_libm.cpp          */
extern DynLibFunction symtable_time[];      /* src/symtab_time.cpp          */
extern DynLibFunction symtable_pthread[];   /* src/symtab_pthread.cpp       */
extern DynLibFunction symtable_sem[];       /* src/symtab_sem.cpp           */
extern DynLibFunction symtable_stat[];      /* src/symtab_stat.cpp          */
extern DynLibFunction symtable_io[];        /* src/symtab_io.cpp            */
extern DynLibFunction symtable_glprobe[];   /* src/symtab_glprobe.cpp       */
extern DynLibFunction symtable_unwind[];    /* src/symtab_unwind.cpp        */
extern DynLibFunction symtable_bionic[];    /* src/symtab_bionic.cpp        */
extern DynLibFunction symtable_off[];       /* src/symtab_off.cpp           */
extern DynLibFunction symtable_setjmp[];    /* src/symtab_setjmp.cpp        */

/*
 * Every DT_NEEDED entry the loader answers itself instead of mapping.
 *
 *
 * libstdc++.so is listed but unused: the game statically links its own C++
 * runtime, and its dynamic table contains no mangled C++ import at all.
 *
 * libGLESv1_CM.so is the one that had to be added. so_load_module() walks
 * DT_NEEDED and treats anything not in this list as another module to map, so
 * without the entry it goes looking for lib/armeabi-v7a/libGLESv1_CM.so inside
 * the game tree - which is of course not there, it comes from the system image
 * on a device - and the load fails before a single relocation is applied.
 */
const char *so_builtin_libs[] = {
    "libc.so",
    "libdl.so",
    "libm.so",
    "libstdc++.so",
    "liblog.so",
    "libEGL.so",
    "libGLESv2.so",
    "libGLESv1_CM.so",
    NULL,
};

/* No binary patching is needed so far; the game's code runs as shipped. */
DynLibFunction *so_static_patches[] = {
    NULL,
};

DynLibFunction *so_dynamic_libraries[] = {
    /* Override the generated libc table where the host disagrees with bionic
     * about the size or layout of a type; must therefore come first. */
    symtable_time,
    symtable_pthread,
    /* bionic's sem_t is one word and glibc's is four; sem_init bound straight
     * through writes twelve bytes past the end of every semaphore the engine
     * owns. Must come before symtable_libc for the same reason as the mutex
     * bridge above it. */
    symtable_sem,
    /* bionic's arm jmp_buf is 64 words; glibc's is those 64 plus a
     * __mask_was_saved flag and a 128-byte signal mask. Bound straight through,
     * setjmp writes a 1 exactly 256 bytes into a buffer the game sized at 256,
     * onto whatever field follows. Same reason as the three above it. */
    symtable_setjmp,
    symtable_stat,
    /* Every libc entry point that takes a *path*. The engine addresses its
     * assets as "appbundle:/..." - a scheme, not a filename - so these have to
     * shadow the generated table or the names reach the kernel verbatim and
     * ENOENT. See src/symtab_io.cpp for the strace that showed it. */
    symtable_io,
    /* off_t is 8 bytes on this host and 4 in the game; mmap/lseek/ftruncate
     * therefore disagree about register layout, not just field width. */
    symtable_off,
    /* The imports the generated bionic table left commented out: bionic-only
     * atomics, three OBJECT symbols, variadic fcntl, and readdir_r with its
     * own dirent layout. Ahead of symtable_libc because readdir_r's layout
     * disagreement is exactly the kind the generated entry gets wrong. */
    symtable_bionic,
    /* Shadows the host libgcc's __gnu_Unwind_Find_exidx, which cannot see a
     * module this loader mapped; must therefore come before symtable_libc. */
    symtable_unwind,
    symtable_log,
    symtable_egl,
    /* Counts draw calls for the content milestone and, for GLES2 games, reads
     * compile/link status. It must precede both GL tables; its draw wrappers
     * forward through the live GLES1 table used by this fixed-function game. */
    symtable_glprobe,
    /* Fixed function, and the only GL table this game needs: it imports 190
     * GLES 1.1 entry points and not one shader call. symtable_gles2 stays in
     * the list below it for ports that also carry GLES2 imports; shared names
     * such as glDrawElements are intercepted by symtable_glprobe above. */
    symtable_gles1,
    symtable_gles2,
    symtable_libm,
    symtable_libc,
    NULL,
};
