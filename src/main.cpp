/*
 * Katamari native ARM host.
 *
 * The APK is a small Java shell around an unstripped ARMv5 C++ game.  The
 * original Java activity owns the lifecycle, so this process supplies the
 * lifecycle calls directly while the existing bionic ELF/JNI/GLES loader maps
 * libkatamari.so.  No Android runtime or emulator is involved.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "so_util.h"
#include "khronos/gles2.h"

#include "jni.h"
#include "classes/katamari.h"

#include "android/cursor_draw.h"
#include "android/emulator_control.h"
#include "android/fb_probe.h"
#include "android/katamari_input.h"
#include "crash.h"
#include "fix_path.h"
#include "gl_diag.h"
#include "gl_probe.h"
#include "port_version.h"
#include "sdl_info.h"
#include "trace.h"
#include "viewport_scale.h"

extern "C" long android_io_assets_opened(void);
extern "C" long katamari_asset_files_loaded(void);
extern "C" long android_gl_textures_uploaded(void);
extern "C" long android_gl_draw_calls(void);
extern "C" long android_gl_rgba_uploaded(void);
extern "C" long android_gl_subimages_uploaded(void);
extern "C" long android_gl_atc_decoded(void);
extern "C" long android_gl_pvrtc_native(void);
extern "C" long android_gl_pvrtc_decoded(void);
extern "C" long android_gl_compressed_passthrough(void);
extern "C" long android_gl_decode_failed(void);

static const char *kNativeLib = "libkatamari.so";
static const char *kNativeLibDir = "lib/armeabi";
static const int kWidth = 640;
static const int kHeight = 480;

static so_module *g_module = NULL;
so_module *katamari_module(void) { return g_module; }

static int report_unresolved_symbols(so_module *mod)
{
    int missing = 0;
    for (int i = 0; i < mod->num_dynsym; i++) {
        Elf_Sym *sym = &mod->dynsym[i];
        if (sym->st_shndx != SHN_UNDEF)
            continue;

        const char *name = mod->dynstr + sym->st_name;
        if (!name || !*name)
            continue;
        if (so_resolve_link(mod, name))
            continue;
        if (ELF32_ST_BIND(sym->st_info) == STB_WEAK) {
            trace("weak import left null: %s", name);
            continue;
        }
        fprintf(stderr, "unresolved symbol: %s\n", name);
        missing++;
    }
    fflush(stderr);
    return missing;
}
/* Katamari's ARMv5 soft-float code is loaded exactly as shipped. */
extern "C" int so_after_relocate(so_module *mod)
{
    g_module = mod;
    trace("module loaded: %s", mod->soname ? mod->soname : kNativeLib);

    int missing = report_unresolved_symbols(mod);
    if (missing != 0) {
        fatal("%d import(s) of %s have no implementation", missing,
              mod->soname ? mod->soname : kNativeLib);
        return 1;
    }
    trace("all native imports resolved");
    return 0;
}

static void trace_probe_line(void *ctx, const char *line)
{
    (void)ctx;
    trace("  %s", line);
}

static void log_window_failure(const char *stage)
{
    trace("window failure (%s): %s", stage, SDL_GetError());
    const char *driver = SDL_GetCurrentVideoDriver();
    trace("SDL video driver: %s", driver ? driver : "(none)");
    const char *egl = getenv("SDL_VIDEO_EGL_DRIVER");
    if (!egl || !*egl)
        egl = "libEGL.so.1";
    gl_probe_init(egl, trace_probe_line, NULL);
    gl_probe_deps(egl, trace_probe_line, NULL);
}

static bool create_gl_window(SDL_Window **window_out, SDL_GLContext *context_out)
{
    *window_out = NULL;
    *context_out = NULL;

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    SDL_Window *window = SDL_CreateWindow(
        "Katamari Damacy", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kWidth, kHeight, SDL_WINDOW_OPENGL);
    if (window) {
        SDL_GLContext context = SDL_GL_CreateContext(window);
        if (context) {
            *window_out = window;
            *context_out = context;
            trace("using native GLES 1.1 context");
            return true;
        }
        trace("native GLES 1.1 context unavailable: %s", SDL_GetError());
        SDL_DestroyWindow(window);
    } else {
        log_window_failure("GLES 1.1 window");
    }

    /* Mesa's software stack often exposes fixed-function GL through a desktop
     * compatibility profile rather than a GLES1 EGL config. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    window = SDL_CreateWindow(
        "Katamari Damacy", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kWidth, kHeight, SDL_WINDOW_OPENGL);
    if (!window) {
        log_window_failure("desktop compatibility window");
        return false;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        log_window_failure("desktop compatibility context");
        SDL_DestroyWindow(window);
        return false;
    }

    *window_out = window;
    *context_out = context;
    trace("using desktop compatibility context for GLES1 fixed function");
    return true;
}

static void log_gl_info(void)
{
    auto get_string = (const GLubyte *(*)(GLenum))SDL_GL_GetProcAddress("glGetString");
    if (!get_string)
        return;
    trace("GL_VERSION=%s | GL_RENDERER=%s",
          get_string(GL_VERSION) ? (const char *)get_string(GL_VERSION) : "?",
          get_string(GL_RENDERER) ? (const char *)get_string(GL_RENDERER) : "?");
}

static bool copy_activity_path(JNIEnv *env, const char *game_dir,
                               jbyteArray *array_out)
{
    size_t length = strlen(game_dir);
    if (length > INT_MAX - 1)
        return false;

    jbyteArray array = env->NewByteArray((jsize)length);
    if (!array)
        return false;
    if (length != 0)
        env->SetByteArrayRegion(array, 0, (jsize)length,
                                (const jbyte *)game_dir);
    *array_out = array;
    return true;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (argc >= 2 && strcmp(argv[1], "--gl-probe") == 0)
        return gl_probe_main(argc - 2, argv + 2);
    if (argc >= 3 && strcmp(argv[1], "--gl-probe-init") == 0)
        return gl_probe_init(argv[2], gl_probe_report_stdout, NULL);
    if (argc >= 3 && strcmp(argv[1], "--gl-probe-deps") == 0)
        return gl_probe_deps(argv[2], gl_probe_report_stdout, NULL);
    if (argc >= 2 && strcmp(argv[1], "--sdl-info") == 0)
        return sdl_info_main();
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        printf("%s\n", KATAMARI_PORT_VERSION);
        return 0;
    }

    trace("Katamari port v%s", KATAMARI_PORT_VERSION);
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <katamari-directory>\n"
                "\n"
                "Supply an extracted MMkatamari APK tree containing\n"
                "lib/armeabi/libkatamari.so, assets/ and res/.\n",
                argv[0]);
        return 2;
    }

    const char *game_dir = argv[1];
    io_set_game_dir(game_dir);

    char lib_dir[PATH_MAX];
    char lib_path[PATH_MAX];
    snprintf(lib_dir, sizeof(lib_dir), "%s/%s", game_dir, kNativeLibDir);
    snprintf(lib_path, sizeof(lib_path), "%s/%s", lib_dir, kNativeLib);

    struct stat st;
    if (stat(lib_path, &st) != 0) {
        fatal("native library is missing: %s (%s)", lib_path, strerror(errno));
        return 1;
    }
    trace("native library found: %s (%lld bytes)", lib_path,
          (long long)st.st_size);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fatal("SDL initialization failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = NULL;
    SDL_GLContext gl = NULL;
    if (!create_gl_window(&window, &gl)) {
        fatal("could not create a fixed-function GL context");
        return 1;
    }

    load_gles1_funcs();
    log_gl_info();

    int window_w = 0;
    int window_h = 0;
    int drawable_w = 0;
    int drawable_h = 0;
    SDL_GetWindowSize(window, &window_w, &window_h);
    SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);
    trace("window geometry: logical=%dx%d drawable=%dx%d",
          window_w, window_h, drawable_w, drawable_h);
    viewport_scale_init(drawable_w, drawable_h, kWidth, kHeight);

    if (getenv("KATAMARI_FAST_FORWARD"))
        SDL_GL_SetSwapInterval(0);

    JavaVM *vm = NULL;
    JNIEnv *env = NULL;
    if (JNI_CreateJavaVM(&vm, &env, NULL) != JNI_OK || !vm || !env) {
        fatal("could not create the fake JNI environment");
        return 1;
    }

    so_set_options(NULL, lib_dir);
    so_module *mod = so_load_module(kNativeLib, NULL, NULL);
    if (!mod) {
        fatal("could not load %s from %s", kNativeLib, lib_dir);
        return 1;
    }
    crash_report_init(mod, kNativeLib);

    using InitActivityFn = void (*)(JNIEnv *, jclass, jbyteArray);
    using RendererInitFn = void (*)(JNIEnv *, jobject);
    using RendererResizeFn = void (*)(JNIEnv *, jobject, jint, jint);
    using RendererDoneFn = void (*)(JNIEnv *, jobject);
    using RendererRenderFn = jint (*)(JNIEnv *, jobject);
    using LifecycleFn = void (*)(JNIEnv *, jobject);

    InitActivityFn native_init_activity = (InitActivityFn)so_symbol(
        mod, "Java_com_namcobandaigames_katamari_Katamari_nativeInitActivity");
    RendererInitFn native_init = (RendererInitFn)so_symbol(
        mod, "Java_com_namcobandaigames_katamari_AppRenderer_nativeInit");
    RendererResizeFn native_resize = (RendererResizeFn)so_symbol(
        mod, "Java_com_namcobandaigames_katamari_AppRenderer_nativeResize");
    RendererDoneFn native_done = (RendererDoneFn)so_symbol(
        mod, "Java_com_namcobandaigames_katamari_AppRenderer_nativeDone");
    RendererRenderFn native_render = (RendererRenderFn)so_symbol(
        mod, "Java_com_namcobandaigames_katamari_AppRenderer_nativeRender");
    LifecycleFn native_resume = (LifecycleFn)so_symbol(
        mod, "Java_com_namcobandaigames_katamari_Katamari_nativeResume");
    LifecycleFn native_release = (LifecycleFn)so_symbol(
        mod, "Java_com_namcobandaigames_katamari_Katamari_nativeDoneActivity");

    if (!native_init_activity || !native_init || !native_resize ||
        !native_render || !native_done || !native_release) {
        fatal("libkatamari.so is missing a lifecycle export: initActivity=%p "
              "init=%p resize=%p render=%p done=%p release=%p",
              (void *)native_init_activity, (void *)native_init,
              (void *)native_resize, (void *)native_render,
              (void *)native_done, (void *)native_release);
        return 1;
    }

    jbyteArray activity_path = NULL;
    if (!copy_activity_path(env, game_dir, &activity_path)) {
        fatal("could not construct nativeInitActivity path byte array");
        return 1;
    }

    trace("calling Katamari.nativeInitActivity");
    native_init_activity(env, (jclass)&Katamari::clazz, activity_path);
    trace("calling AppRenderer.nativeInit");
    native_init(env, (jobject)(uintptr_t)0x42424242);
    trace("calling AppRenderer.nativeResize(%d,%d)", kWidth, kHeight);
    native_resize(env, (jobject)(uintptr_t)0x42424242, kWidth, kHeight);
    if (native_resume)
        native_resume(env, (jobject)(uintptr_t)0x42424242);

    katamari_input_init(mod, env, kWidth, kHeight);
    emulator_control_init();

    const char *limit_env = getenv("KATAMARI_FRAME_LIMIT");
    const long frame_limit = limit_env ? atol(limit_env) : 0;
    long frames = 0;
    bool running = true;
    while (running && (frame_limit == 0 || frames < frame_limit)) {
        if (!emulator_control_tick(frames)) {
            running = false;
            break;
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (!katamari_input_event(&event)) {
                running = false;
                break;
            }
        }
        if (!running)
            break;

        katamari_input_tick(frames);
        if (frames < 5)
            trace("-> AppRenderer.nativeRender #%ld", frames + 1);
        jint alive = native_render(env, (jobject)(uintptr_t)0x42424242);
        frames++;
        if (frames <= 5)
            trace("<- AppRenderer.nativeRender #%ld (alive=%d)", frames, alive);

        if (window) {
            android_cursor_draw(drawable_w, drawable_h);
            emulator_control_after_draw(frames, drawable_w, drawable_h);
            SDL_GL_SwapWindow(window);
        }
        if (!alive)
            running = false;
        if (frames <= 5 || frames % 60 == 0)
            trace("frames=%ld", frames);
    }

    trace("frames=%ld", frames);
    emulator_control_shutdown(frames);
    trace("summary assets=%ld fixed_paths=%ld textures=%ld draws=%ld",
          katamari_asset_files_loaded(), android_io_assets_opened(),
          android_gl_textures_uploaded(),
          android_gl_draw_calls());
    trace("texture summary atc_decoded=%ld pvrtc_native=%ld pvrtc_decoded=%ld "
          "rgba=%ld subimage=%ld passthrough=%ld failed=%ld gl_errors=%ld",
          android_gl_atc_decoded(), android_gl_pvrtc_native(),
          android_gl_pvrtc_decoded(), android_gl_rgba_uploaded(),
          android_gl_subimages_uploaded(), android_gl_compressed_passthrough(),
          android_gl_decode_failed(), gl_diag_error_count());

    native_done(env, (jobject)(uintptr_t)0x42424242);
    native_release(env, (jobject)(uintptr_t)0x42424242);
    trace("run finished: %ld frames", frames);

    /* The original image may have worker threads alive after release. Let the
     * process tear down atomically instead of destroying their GL context. */
    fflush(NULL);
    _exit(0);
}
