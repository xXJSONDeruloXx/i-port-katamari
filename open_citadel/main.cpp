#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>

#include <algorithm>
#include <cmath>

#include <SDL2/SDL.h>

#include "io_util.h"
#include "fix_path.h"
#include "so_util.h"
#include "khronos/gles2.h"
#include "jni.h"
#include "classes/ue3_java_app.h"
#include "trace.h"
#include "crash.h"
#include "gles2_probe.h"

extern "C" void android_egl_init(SDL_Window *window, SDL_GLContext gl);

static so_module *g_module = nullptr;

/* Existing bionic pthread compatibility uses this accessor to determine
 * whether a pthread entry point belongs to the mapped Android module. Keep the
 * ABI while the shared loader is being generalized beyond Katamari. */
so_module *katamari_module(void)
{
    return g_module;
}

static int report_unresolved_symbols(so_module *mod)
{
    int missing = 0;
    for (int i = 0; i < mod->num_dynsym; ++i) {
        Elf_Sym *sym = &mod->dynsym[i];
        if (sym->st_shndx != SHN_UNDEF)
            continue;
        const char *name = mod->dynstr + sym->st_name;
        if (!name || !*name || so_resolve_link(mod, name))
            continue;
        if (ELF32_ST_BIND(sym->st_info) == STB_WEAK)
            continue;
        fprintf(stderr, "unresolved symbol: %s\n", name);
        ++missing;
    }
    return missing;
}

extern "C" int so_after_relocate(so_module *mod)
{
    g_module = mod;

    /* Called before the loader executes .init_array, so constructor faults are
     * reported with guest-relative PCs instead of disappearing into a bare
     * SIGSEGV. */
    crash_report_init(mod, "libUnrealEngine3.so");

    const int missing = report_unresolved_symbols(mod);
    if (missing)
        fprintf(stderr, "OpenCitadel: %d unresolved import(s)\n", missing);
    else
        fprintf(stderr, "OpenCitadel: all native imports resolved\n");
    return missing ? 1 : 0;
}

static int env_int(const char *name, int fallback)
{
    const char *value = getenv(name);
    if (!value || !*value)
        return fallback;
    char *end = nullptr;
    long parsed = strtol(value, &end, 10);
    return end && !*end && parsed > 0 && parsed <= INT_MAX
        ? (int)parsed : fallback;
}

static bool exists(const char *path)
{
    struct stat st {};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static SDL_Window *create_window(int width, int height, SDL_GLContext *out_gl)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (getenv("OPEN_CITADEL_HIDDEN"))
        flags |= SDL_WINDOW_HIDDEN;

    SDL_Window *window = SDL_CreateWindow(
        "Open Citadel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, flags);
    if (!window)
        return nullptr;

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        SDL_DestroyWindow(window);
        return nullptr;
    }
    *out_gl = gl;
    return window;
}

template <typename T>
static T guest_symbol(so_module *mod, const char *name)
{
    uintptr_t value = so_symbol(mod, name);
    if (!value)
        fprintf(stderr, "OpenCitadel: missing guest export %s\n", name);
    return reinterpret_cast<T>(value);
}


static int android_keycode(SDL_Keycode key)
{
    if (key >= SDLK_0 && key <= SDLK_9)
        return 7 + (int)(key - SDLK_0);
    if (key >= SDLK_a && key <= SDLK_z)
        return 29 + (int)(key - SDLK_a);

    switch (key) {
    case SDLK_HOME: return 3;
    case SDLK_ESCAPE: return 4; /* KEYCODE_BACK */
    case SDLK_LEFT: return 21;
    case SDLK_RIGHT: return 22;
    case SDLK_UP: return 19;
    case SDLK_DOWN: return 20;
    case SDLK_TAB: return 61;
    case SDLK_SPACE: return 62;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return 66;
    case SDLK_BACKSPACE:
    case SDLK_DELETE: return 67;
    case SDLK_BACKQUOTE: return 68;
    case SDLK_MINUS:
    case SDLK_KP_MINUS: return 69;
    case SDLK_EQUALS: return 70;
    case SDLK_LEFTBRACKET: return 71;
    case SDLK_RIGHTBRACKET: return 72;
    case SDLK_BACKSLASH: return 73;
    case SDLK_SEMICOLON: return 74;
    case SDLK_QUOTE: return 75;
    case SDLK_SLASH:
    case SDLK_KP_DIVIDE: return 76;
    case SDLK_MENU: return 82;
    case SDLK_LALT: return 57;
    case SDLK_RALT: return 58;
    case SDLK_LSHIFT: return 59;
    case SDLK_RSHIFT: return 60;
    case SDLK_COMMA: return 55;
    case SDLK_PERIOD: return 56;
    case SDLK_KP_MULTIPLY: return 17;
    case SDLK_KP_PLUS: return 81;
    default: return 0;
    }
}

static int android_gamepad_key(SDL_GameControllerButton button)
{
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return 96;
    case SDL_CONTROLLER_BUTTON_B: return 97;
    case SDL_CONTROLLER_BUTTON_X: return 99;
    case SDL_CONTROLLER_BUTTON_Y: return 100;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return 102;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return 103;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return 106;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return 107;
    case SDL_CONTROLLER_BUTTON_START: return 108;
    case SDL_CONTROLLER_BUTTON_BACK: return 109;
    case SDL_CONTROLLER_BUTTON_GUIDE: return 110;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return 19;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return 20;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return 21;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return 22;
    default: return 0;
    }
}

static int android_axis(SDL_GameControllerAxis axis)
{
    switch (axis) {
    case SDL_CONTROLLER_AXIS_LEFTX: return 0;   /* AXIS_X */
    case SDL_CONTROLLER_AXIS_LEFTY: return 1;   /* AXIS_Y */
    case SDL_CONTROLLER_AXIS_RIGHTX: return 11; /* AXIS_Z */
    case SDL_CONTROLLER_AXIS_RIGHTY: return 14; /* AXIS_RZ */
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT: return 17;  /* AXIS_LTRIGGER */
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return 18; /* AXIS_RTRIGGER */
    default: return -1;
    }
}

static float android_axis_value(SDL_GameControllerAxis axis, Sint16 raw)
{
    if (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
        axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
        return std::clamp((float)raw / 32767.0f, 0.0f, 1.0f);
    return std::clamp((float)raw / 32767.0f, -1.0f, 1.0f);
}

static int unicode_for_key(const SDL_KeyboardEvent &event)
{
    SDL_Keycode key = event.keysym.sym;
    if (key >= SDLK_a && key <= SDLK_z) {
        int ch = 'a' + (int)(key - SDLK_a);
        if (event.keysym.mod & KMOD_SHIFT)
            ch -= 'a' - 'A';
        return ch;
    }
    if (key >= SDLK_0 && key <= SDLK_9)
        return '0' + (int)(key - SDLK_0);
    if (key >= 0x20 && key <= 0x7e)
        return (int)key;
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    if (argc != 2) {
        fprintf(stderr, "usage: %s <imported-open-citadel-directory>\n", argv[0]);
        return 2;
    }

    const char *game_dir = argv[1];
#if defined(__i386__)
    const char *abi_dir = "lib/x86";
#elif defined(__arm__)
    const char *abi_dir = "lib/armeabi-v7a";
#else
#error Open Citadel bring-up currently supports i386 and ARMv7
#endif

    char lib_dir[PATH_MAX];
    char lib_path[PATH_MAX];
    char main_obb[PATH_MAX];
    snprintf(lib_dir, sizeof(lib_dir), "%s/%s", game_dir, abi_dir);
    snprintf(lib_path, sizeof(lib_path), "%s/libUnrealEngine3.so", lib_dir);
    snprintf(main_obb, sizeof(main_obb),
             "%s/obb/main.903107.com.epicgames.EpicCitadel.obb", game_dir);

    if (!exists(lib_path)) {
        fprintf(stderr, "OpenCitadel: missing engine %s\n", lib_path);
        return 2;
    }
    if (!exists(main_obb)) {
        fprintf(stderr, "OpenCitadel: missing donor OBB %s\n", main_obb);
        return 2;
    }

    io_set_game_dir(game_dir);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "OpenCitadel: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    const int width = env_int("OPEN_CITADEL_WIDTH", 1280);
    const int height = env_int("OPEN_CITADEL_HEIGHT", 720);
    SDL_GLContext gl = nullptr;
    SDL_Window *window = create_window(width, height, &gl);
    if (!window) {
        fprintf(stderr, "OpenCitadel: GLES2 context creation failed: %s\n",
                SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(0);
    load_gles2_funcs();
    android_egl_init(window, gl);
    open_citadel_java_configure(window, gl, game_dir, main_obb, nullptr);

    const GLubyte *version = glGetString(GL_VERSION);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    fprintf(stderr, "OpenCitadel: GL_VERSION=%s GL_RENDERER=%s\n",
            version ? (const char *)version : "?",
            renderer ? (const char *)renderer : "?");

    JavaVM *vm = nullptr;
    JNIEnv *env = nullptr;
    if (JNI_CreateJavaVM(&vm, &env, nullptr) != JNI_OK || !vm || !env) {
        fprintf(stderr, "OpenCitadel: fake JVM creation failed\n");
        return 1;
    }

    so_set_options(nullptr, lib_dir);
    so_module *mod = so_load_module("libUnrealEngine3.so", nullptr, nullptr);
    if (!mod) {
        fprintf(stderr, "OpenCitadel: failed to map libUnrealEngine3.so\n");
        return 1;
    }
    g_module = mod;

    using OnLoad = jint (*)(JavaVM *, void *);
    OnLoad on_load = guest_symbol<OnLoad>(mod, "JNI_OnLoad");
    if (!on_load)
        return 1;
    jint jni_version = on_load(vm, nullptr);
    fprintf(stderr, "OpenCitadel: JNI_OnLoad -> 0x%x\n", jni_version);
    if (jni_version != JNI_VERSION_1_4 && jni_version != JNI_VERSION_1_6) {
        fprintf(stderr, "OpenCitadel: JNI_OnLoad failed\n");
        return 1;
    }

    using InitEGL = jboolean (*)(JNIEnv *, jobject);
    using Initialize = jboolean (*)(JNIEnv *, jobject, jint, jint, jfloat,
                                    jboolean, jobject, jboolean);
    using Cleanup = void (*)(JNIEnv *, jobject);
    using PostInit = void (*)(JNIEnv *, jobject, jint, jint);
    using Back = void (*)(void);
    using Input = jboolean (*)(JNIEnv *, jobject, jint, jint, jint, jint, jlong);
    using Keyboard = jboolean (*)(JNIEnv *, jobject, jint, jint, jint, jint);
    using JoyAxis = jboolean (*)(JNIEnv *, jobject, jint, jint, jint, jfloat, jlong);
    using JoyButton = jboolean (*)(JNIEnv *, jobject, jint, jint, jint, jlong);
    using Interrupt = jboolean (*)(JNIEnv *, jobject, jboolean);
    using Network = void (*)(JNIEnv *, jobject, jboolean, jboolean);
    using Language = void (*)(JNIEnv *, jobject, jstring);
    using KeyPad = void (*)(JNIEnv *, jobject, jboolean);
    using SystemStats = jboolean (*)(JNIEnv *, jobject, jlong, jfloat);

    InitEGL native_init_egl = guest_symbol<InitEGL>(
        mod, "_Z30NativeCallback_InitEGLCallbackP7_JNIEnvP8_jobject");
    Initialize native_initialize = guest_symbol<Initialize>(
        mod, "_Z25NativeCallback_InitializeP7_JNIEnvP8_jobjectiifhS2_h");
    Cleanup native_cleanup = guest_symbol<Cleanup>(
        mod, "_Z22NativeCallback_CleanupP7_JNIEnvP8_jobject");
    PostInit native_post_init = guest_symbol<PostInit>(
        mod, "_Z29NativeCallback_PostInitUpdateP7_JNIEnvP8_jobjectii");
    Back native_back = guest_symbol<Back>(
        mod, "_Z38NativeCallback_HandleBackButtonPressedv");
    Input native_input = guest_symbol<Input>(
        mod, "_Z25NativeCallback_InputEventP7_JNIEnvP8_jobjectiiiix");
    Keyboard native_keyboard = guest_symbol<Keyboard>(
        mod, "_Z28NativeCallback_KeyboardEventP7_JNIEnvP8_jobjectiiii");
    JoyAxis native_joy_axis = guest_symbol<JoyAxis>(
        mod, "_Z32NativeCallback_JoystickAxisEventP7_JNIEnvP8_jobjectiiifx");
    JoyButton native_joy_button = guest_symbol<JoyButton>(
        mod, "_Z34NativeCallback_JoystickButtonEventP7_JNIEnvP8_jobjectiiix");
    Interrupt native_interrupt = guest_symbol<Interrupt>(
        mod, "_Z34NativeCallback_InterruptionChangedP7_JNIEnvP8_jobjecth");
    Network native_network = guest_symbol<Network>(
        mod, "_Z28NativeCallback_NetworkUpdateP7_JNIEnvP8_jobjecthh");
    Language native_language = guest_symbol<Language>(
        mod, "_Z26NativeCallback_LanguageSetP7_JNIEnvP8_jobjectP8_jstring");
    KeyPad native_keypad = guest_symbol<KeyPad>(
        mod, "_Z27NativeCallback_KeyPadChangeP7_JNIEnvP8_jobjecth");
    SystemStats native_stats = guest_symbol<SystemStats>(
        mod, "_Z26NativeCallback_SystemStatsP7_JNIEnvP8_jobjectxf");

    if (!native_init_egl || !native_initialize || !native_cleanup ||
        !native_post_init || !native_input || !native_keyboard ||
        !native_joy_axis || !native_joy_button || !native_interrupt ||
        !native_network || !native_language || !native_keypad || !native_stats)
        return 1;

    jobject activity = open_citadel_java_activity();

    /* Match UE3JavaApp.systemStartupCheck/PostStartup before the rendering
     * thread is launched. The newer 1.07 donor passes display density as the
     * second SystemStats argument; 1.0 corresponds to Android's baseline
     * density and is overrideable for diagnostics. */
    struct sysinfo system_info {};
    jlong available_memory = 512LL * 1024LL * 1024LL;
    if (sysinfo(&system_info) == 0)
        available_memory = (jlong)system_info.freeram *
                           (jlong)system_info.mem_unit;
    float density_scale = 1.0f;
    if (const char *density = getenv("OPEN_CITADEL_DENSITY_SCALE"))
        density_scale = std::max(0.1f, (float)atof(density));
    native_stats(env, activity, available_memory, density_scale);
    native_keypad(env, activity, JNI_FALSE);
    native_network(env, activity, JNI_TRUE, JNI_FALSE);
    jstring locale = env->NewStringUTF(
        getenv("OPEN_CITADEL_LANGUAGE") ? getenv("OPEN_CITADEL_LANGUAGE") : "en");
    native_language(env, activity, locale);
    env->DeleteLocalRef(locale);

    if (!native_init_egl(env, activity)) {
        fprintf(stderr, "OpenCitadel: NativeCallback_InitEGLCallback failed\n");
        return 1;
    }
    fprintf(stderr, "OpenCitadel: EGL callback initialized\n");

    /* Android hands the context from the UI thread to UE3GameThread. Do the
     * same explicitly: JavaCallback_makeCurrent will acquire it there. */
    if (SDL_GL_MakeCurrent(window, nullptr) != 0)
        fprintf(stderr, "OpenCitadel: context release warning: %s\n",
                SDL_GetError());

    /* The exported C++ helper carries two legacy parameters that are not
     * present in the Java declaration (IIFZ)Z. The 1.07 implementation only
     * consumes env/activity/width/height, but match its actual native ABI so
     * both i386 cdecl and ARM softfp calls remain well-defined. */
    if (!native_initialize(env, activity, width, height, 1.0f, JNI_FALSE,
                           nullptr, JNI_FALSE)) {
        fprintf(stderr, "OpenCitadel: NativeCallback_Initialize failed\n");
        return 1;
    }
    native_post_init(env, activity, width, height);
    fprintf(stderr, "OpenCitadel: UE3GameThread launched (%dx%d)\n",
            width, height);

    const long frame_limit = getenv("OPEN_CITADEL_FRAME_LIMIT")
        ? atol(getenv("OPEN_CITADEL_FRAME_LIMIT")) : 0;
    const long run_seconds = getenv("OPEN_CITADEL_RUN_SECONDS")
        ? atol(getenv("OPEN_CITADEL_RUN_SECONDS")) : 0;
    const Uint64 start = SDL_GetTicks64();
    long last_reported = -1;
    bool running = true;
    bool interrupted = false;

    SDL_GameController *controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) {
                fprintf(stderr, "OpenCitadel: controller=%s\n",
                        SDL_GameControllerName(controller));
                break;
            }
        }
    }

    while (running && !open_citadel_java_shutdown_requested()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            const jlong event_time = (jlong)event.common.timestamp;
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    native_input(env, activity,
                                 event.type == SDL_MOUSEBUTTONDOWN ? 0 : 1,
                                 event.button.x, event.button.y, 0, event_time);
                }
                break;
            case SDL_MOUSEMOTION:
                if (event.motion.state & SDL_BUTTON_LMASK)
                    native_input(env, activity, 2, event.motion.x,
                                 event.motion.y, 0, event_time);
                break;
            case SDL_FINGERDOWN:
            case SDL_FINGERUP:
            case SDL_FINGERMOTION: {
                int dw = 0, dh = 0;
                SDL_GetWindowSize(window, &dw, &dh);
                const int action = event.type == SDL_FINGERDOWN ? 0 :
                                   event.type == SDL_FINGERUP ? 1 : 2;
                native_input(env, activity, action,
                             (int)std::lround(event.tfinger.x * dw),
                             (int)std::lround(event.tfinger.y * dh),
                             (jint)(event.tfinger.fingerId & 0x7fffffff),
                             event_time);
                break;
            }

            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                if (event.key.repeat)
                    break;
                const int code = android_keycode(event.key.keysym.sym);
                if (code)
                    native_keyboard(env, activity, 0,
                                    event.type == SDL_KEYDOWN ? 0 : 1,
                                    code, unicode_for_key(event.key));
                if (event.type == SDL_KEYUP &&
                    event.key.keysym.sym == SDLK_ESCAPE)
                    native_back();
                break;
            }

            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP: {
                const int code = android_gamepad_key(
                    (SDL_GameControllerButton)event.cbutton.button);
                if (code)
                    native_joy_button(
                        env, activity, event.cbutton.which,
                        event.type == SDL_CONTROLLERBUTTONDOWN ? 0 : 1,
                        code, event_time);
                break;
            }
            case SDL_CONTROLLERAXISMOTION: {
                const auto axis =
                    (SDL_GameControllerAxis)event.caxis.axis;
                const int aaxis = android_axis(axis);
                if (aaxis >= 0)
                    native_joy_axis(env, activity, event.caxis.which,
                                    2, aaxis,
                                    android_axis_value(axis, event.caxis.value),
                                    event_time);
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                if (!controller && SDL_IsGameController(event.cdevice.which))
                    controller = SDL_GameControllerOpen(event.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (controller &&
                    SDL_JoystickInstanceID(
                        SDL_GameControllerGetJoystick(controller)) ==
                        event.cdevice.which) {
                    SDL_GameControllerClose(controller);
                    controller = nullptr;
                }
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    native_post_init(env, activity,
                                     event.window.data1, event.window.data2);
                } else if ((event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                            event.window.event == SDL_WINDOWEVENT_MINIMIZED) &&
                           !interrupted) {
                    native_interrupt(env, activity, JNI_TRUE);
                    interrupted = true;
                } else if ((event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                            event.window.event == SDL_WINDOWEVENT_RESTORED) &&
                           interrupted) {
                    native_interrupt(env, activity, JNI_FALSE);
                    interrupted = false;
                }
                break;
            default:
                break;
            }
        }

        const long frames = open_citadel_java_frames_presented();
        if (frames != last_reported && (frames <= 5 || frames % 60 == 0)) {
            fprintf(stderr,
                    "OpenCitadel: frames=%ld draws=%ld textures=%ld atc=%ld "
                    "shaders=%d/%d programs=%d/%d\n",
                    frames, open_citadel_gl_draws(),
                    open_citadel_gl_textures(), open_citadel_gl_atc_decoded(),
                    open_citadel_gl_shaders_ok(),
                    open_citadel_gl_shaders_failed(),
                    open_citadel_gl_programs_ok(),
                    open_citadel_gl_programs_failed());
            last_reported = frames;
        }
        if (frame_limit > 0 && frames >= frame_limit)
            break;
        if (run_seconds > 0 &&
            SDL_GetTicks64() - start >= (Uint64)run_seconds * 1000)
            break;

        SDL_Delay(8);
    }

    if (controller)
        SDL_GameControllerClose(controller);
    if (interrupted)
        native_interrupt(env, activity, JNI_FALSE);

    fprintf(stderr,
            "OpenCitadel: cleanup frames=%ld draws=%ld textures=%ld atc=%ld "
            "shaders=%d/%d programs=%d/%d\n",
            open_citadel_java_frames_presented(), open_citadel_gl_draws(),
            open_citadel_gl_textures(), open_citadel_gl_atc_decoded(),
            open_citadel_gl_shaders_ok(), open_citadel_gl_shaders_failed(),
            open_citadel_gl_programs_ok(), open_citadel_gl_programs_failed());
    native_cleanup(env, activity);

    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
