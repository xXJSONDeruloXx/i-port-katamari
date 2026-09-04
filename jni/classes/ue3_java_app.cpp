#include "ue3_java_app.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>

#include "android/asset_manager.h"
#include "trace.h"

namespace {

SDL_Window *g_window = nullptr;
SDL_GLContext g_context = nullptr;
std::string g_game_dir;
std::string g_main_obb;
std::string g_patch_obb;
std::atomic<int> g_shutdown{0};
UE3JavaApp g_activity;
std::mutex g_prefs_lock;
std::unordered_map<std::string, std::string> g_prefs;

static const char *str_value(jstring value)
{
    String *s = reinterpret_cast<String *>(value);
    return s && s->str ? s->str : "";
}

static jstring jstr(const char *value)
{
    return reinterpret_cast<jstring>(new String(value ? value : ""));
}

static jstring cb_get_app_command_line(JNIEnv *, jobject)
{
    const char *override_line = std::getenv("OPEN_CITADEL_COMMAND_LINE");
    if (override_line && *override_line)
        return jstr(override_line);

    /*
     * The donor's assets/UE3CommandLine.txt is:
     *   EpicCitadel.udk -installed -Exec=UnrealFrontend_TmpExec.txt
     *
     * Start without OpenSL so graphics/filesystem bring-up cannot be blocked by
     * Android audio. Set OPEN_CITADEL_COMMAND_LINE to opt into audio or test
     * another UE3 switch set without rebuilding.
     */
    return jstr("EpicCitadel.udk -installed -Exec=UnrealFrontend_TmpExec.txt -nosound");
}

static jobject cb_get_asset_manager(JNIEnv *, jobject)
{
    return open_citadel_asset_manager_java_object();
}

static jint cb_get_depth_size(JNIEnv *, jobject)
{
    int depth = 24;
    if (g_window)
        SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depth);
    return depth;
}

static jstring cb_get_device_model(JNIEnv *, jobject)
{
    return jstr("OpenCitadel Linux");
}

static jstring cb_get_main_expansion(JNIEnv *, jobject)
{
    return jstr(g_main_obb.c_str());
}

static jstring cb_get_patch_expansion(JNIEnv *, jobject)
{
    return jstr(g_patch_obb.c_str());
}

static jint cb_get_performance_level(JNIEnv *, jobject)
{
    const char *value = std::getenv("OPEN_CITADEL_PERFORMANCE_LEVEL");
    return value ? std::atoi(value) : 2;
}

static jfloat cb_get_resolution_scale(JNIEnv *, jobject)
{
    const char *value = std::getenv("OPEN_CITADEL_RESOLUTION_SCALE");
    return value ? static_cast<jfloat>(std::atof(value)) : 1.0f;
}

static jint cb_get_sdk_version(JNIEnv *, jobject)
{
    /* Epic Citadel shipped in 2013; API 17 is new enough for its code paths
     * while avoiding newer Android-only behaviour. */
    return 17;
}

static jboolean cb_is_expansion_in_apk(JNIEnv *, jobject)
{
    /* The APKPure donor stores the expansion externally in Android/obb. */
    return JNI_FALSE;
}

static void cb_noop(JNIEnv *, jobject) {}

static void cb_hide_keyboard(JNIEnv *, jobject, jboolean) {}

static jboolean cb_is_video_playing(JNIEnv *, jobject)
{
    return JNI_FALSE;
}

static jint cb_load_sound_file(JNIEnv *, jobject, jstring)
{
    return -1;
}

static void cb_set_fixed_size_scale(JNIEnv *, jobject, jfloat) {}

static void cb_set_max_performance(JNIEnv *, jobject, jint) {}

static void cb_show_web_page(JNIEnv *, jobject, jstring url)
{
    trace("OpenCitadel JavaCallback_ShowWebPage ignored: %s", str_value(url));
}

static void cb_shutdown(JNIEnv *, jobject)
{
    trace("OpenCitadel Java requested shutdown");
    g_shutdown.store(1, std::memory_order_release);
}

static void cb_show_keyboard(JNIEnv *, jobject, jstring, jfloat, jfloat,
                             jfloat, jfloat, jboolean) {}

static void cb_start_video(JNIEnv *, jobject, jobject, jlong, jlong, jboolean)
{
    /* Video is deliberately skipped during initial native bring-up. */
}

static void cb_video_text(JNIEnv *, jobject, jstring) {}

static void cb_play_song(JNIEnv *, jobject, jobject, jlong, jlong, jstring) {}

static void cb_stop_song(JNIEnv *, jobject) {}

static jint cb_play_sound(JNIEnv *, jobject, jint, jboolean)
{
    return -1;
}

static void cb_sound_id(JNIEnv *, jobject, jint) {}

static void cb_set_volume(JNIEnv *, jobject, jint, jfloat) {}

static void cb_update_song(JNIEnv *, jobject, jfloat) {}

static void cb_apsalar_event(JNIEnv *, jobject, jstring) {}

static void cb_apsalar_event_param(JNIEnv *, jobject, jstring, jstring,
                                   jstring) {}

static void cb_apsalar_event_array(JNIEnv *, jobject, jstring, jobjectArray) {}

static void cb_apsalar_start(JNIEnv *, jobject, jstring, jstring) {}

static void cb_apsalar_engine_data(JNIEnv *, jobject, jstring, jint) {}

static jboolean cb_init_egl(JNIEnv *, jobject, jobject)
{
    return (g_window && g_context) ? JNI_TRUE : JNI_FALSE;
}

static jboolean cb_make_current(JNIEnv *, jobject)
{
    if (!g_window || !g_context)
        return JNI_FALSE;
    int rc = SDL_GL_MakeCurrent(g_window, g_context);
    if (rc != 0)
        trace("OpenCitadel SDL_GL_MakeCurrent failed: %s", SDL_GetError());
    return rc == 0 ? JNI_TRUE : JNI_FALSE;
}

static jboolean cb_swap_buffers(JNIEnv *, jobject)
{
    if (!g_window)
        return JNI_FALSE;
    SDL_GL_SwapWindow(g_window);
    return JNI_TRUE;
}

static jboolean cb_unmake_current(JNIEnv *, jobject)
{
    if (!g_window)
        return JNI_FALSE;
    int rc = SDL_GL_MakeCurrent(g_window, nullptr);
    return rc == 0 ? JNI_TRUE : JNI_FALSE;
}

static jboolean cb_has_local_value(JNIEnv *, jobject, jstring key)
{
    std::lock_guard<std::mutex> lock(g_prefs_lock);
    return g_prefs.count(str_value(key)) ? JNI_TRUE : JNI_FALSE;
}

static jstring cb_get_local_value(JNIEnv *, jobject, jstring key)
{
    std::lock_guard<std::mutex> lock(g_prefs_lock);
    auto it = g_prefs.find(str_value(key));
    return jstr(it == g_prefs.end() ? "" : it->second.c_str());
}

static void cb_set_local_value(JNIEnv *, jobject, jstring key, jstring value)
{
    std::lock_guard<std::mutex> lock(g_prefs_lock);
    g_prefs[str_value(key)] = str_value(value);
}

static void egl_config_ctor(JNIEnv *, jobject obj, jclass)
{
    if (obj)
        new (obj) UE3EGLConfigParms();
}

static const ManagedMethod UE3JavaMethods[] = {
    ManagedMethod::Register<&cb_get_app_command_line>(
        UE3JavaApp::clazz, "JavaCallback_GetAppCommandLine", "()Ljava/lang/String;"),
    ManagedMethod::Register<&cb_get_asset_manager>(
        UE3JavaApp::clazz, "JavaCallback_GetAssetManager",
        "()Landroid/content/res/AssetManager;"),
    ManagedMethod::Register<&cb_get_depth_size>(
        UE3JavaApp::clazz, "JavaCallback_GetDepthSize", "()I"),
    ManagedMethod::Register<&cb_get_device_model>(
        UE3JavaApp::clazz, "JavaCallback_GetDeviceModel", "()Ljava/lang/String;"),
    ManagedMethod::Register<&cb_get_main_expansion>(
        UE3JavaApp::clazz, "JavaCallback_GetMainAPKExpansionName",
        "()Ljava/lang/String;"),
    ManagedMethod::Register<&cb_get_patch_expansion>(
        UE3JavaApp::clazz, "JavaCallback_GetPatchAPKExpansionName",
        "()Ljava/lang/String;"),
    ManagedMethod::Register<&cb_get_performance_level>(
        UE3JavaApp::clazz, "JavaCallback_GetPerformanceLevel", "()I"),
    ManagedMethod::Register<&cb_get_resolution_scale>(
        UE3JavaApp::clazz, "JavaCallback_GetResolutionScale", "()F"),
    ManagedMethod::Register<&cb_get_sdk_version>(
        UE3JavaApp::clazz, "JavaCallback_GetSDKVersion", "()I"),
    ManagedMethod::Register<&cb_hide_keyboard>(
        UE3JavaApp::clazz, "JavaCallback_HideKeyBoard", "(Z)V"),
    ManagedMethod::Register<&cb_noop>(
        UE3JavaApp::clazz, "JavaCallback_HideReloader", "()V"),
    ManagedMethod::Register<&cb_noop>(
        UE3JavaApp::clazz, "JavaCallback_HideSplash", "()V"),
    ManagedMethod::Register<&cb_is_expansion_in_apk>(
        UE3JavaApp::clazz, "JavaCallback_IsExpansionInAPK", "()Z"),
    ManagedMethod::Register<&cb_is_video_playing>(
        UE3JavaApp::clazz, "JavaCallback_IsVideoPlaying", "()Z"),
    ManagedMethod::Register<&cb_load_sound_file>(
        UE3JavaApp::clazz, "JavaCallback_LoadSoundFile", "(Ljava/lang/String;)I"),
    ManagedMethod::Register<&cb_noop>(
        UE3JavaApp::clazz, "JavaCallback_OpenSettingsMenu", "()V"),
    ManagedMethod::Register<&cb_play_song>(
        UE3JavaApp::clazz, "JavaCallback_PlaySong",
        "(Ljava/io/FileDescriptor;JJLjava/lang/String;)V"),
    ManagedMethod::Register<&cb_play_sound>(
        UE3JavaApp::clazz, "JavaCallback_PlaySound", "(IZ)I"),
    ManagedMethod::Register<&cb_noop>(
        UE3JavaApp::clazz, "JavaCallback_RestoreMedia", "()V"),
    ManagedMethod::Register<&cb_set_fixed_size_scale>(
        UE3JavaApp::clazz, "JavaCallback_SetFixedSizeScale", "(F)V"),
    ManagedMethod::Register<&cb_set_max_performance>(
        UE3JavaApp::clazz, "JavaCallback_SetMaxPerformanceLevel", "(I)V"),
    ManagedMethod::Register<&cb_set_volume>(
        UE3JavaApp::clazz, "JavaCallback_SetVolume", "(IF)V"),
    ManagedMethod::Register<&cb_noop>(
        UE3JavaApp::clazz, "JavaCallback_ShowExitDialog", "()V"),
    ManagedMethod::Register<&cb_show_keyboard>(
        UE3JavaApp::clazz, "JavaCallback_ShowKeyBoard",
        "(Ljava/lang/String;FFFFZ)V"),
    ManagedMethod::Register<&cb_show_web_page>(
        UE3JavaApp::clazz, "JavaCallback_ShowWebPage", "(Ljava/lang/String;)V"),
    ManagedMethod::Register<&cb_shutdown>(
        UE3JavaApp::clazz, "JavaCallback_ShutDownApp", "()V"),
    ManagedMethod::Register<&cb_start_video>(
        UE3JavaApp::clazz, "JavaCallback_StartVideo",
        "(Ljava/io/FileDescriptor;JJZ)V"),
    ManagedMethod::Register<&cb_stop_song>(
        UE3JavaApp::clazz, "JavaCallback_StopSong", "()V"),
    ManagedMethod::Register<&cb_sound_id>(
        UE3JavaApp::clazz, "JavaCallback_StopSound", "(I)V"),
    ManagedMethod::Register<&cb_noop>(
        UE3JavaApp::clazz, "JavaCallback_StopVideo", "()V"),
    ManagedMethod::Register<&cb_sound_id>(
        UE3JavaApp::clazz, "JavaCallback_UnloadSoundID", "(I)V"),
    ManagedMethod::Register<&cb_update_song>(
        UE3JavaApp::clazz, "JavaCallback_UpdateSongPlayer", "(F)V"),
    ManagedMethod::Register<&cb_video_text>(
        UE3JavaApp::clazz, "JavaCallback_VideoAddTextOverlay",
        "(Ljava/lang/String;)V"),

    ManagedMethod::Register<&cb_noop>(
        UE3JavaApp::clazz, "JavaCallback_ApsalarEndSession", "()V"),
    ManagedMethod::Register<&cb_apsalar_event>(
        UE3JavaApp::clazz, "JavaCallback_ApsalarEvent", "(Ljava/lang/String;)V"),
    ManagedMethod::Register<&cb_apsalar_event_param>(
        UE3JavaApp::clazz, "JavaCallback_ApsalarEventParam",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
    ManagedMethod::Register<&cb_apsalar_event_array>(
        UE3JavaApp::clazz, "JavaCallback_ApsalarEventParamArray",
        "(Ljava/lang/String;[Ljava/lang/String;)V"),
    ManagedMethod::Register<&cb_noop>(
        UE3JavaApp::clazz, "JavaCallback_ApsalarInit", "()V"),
    ManagedMethod::Register<&cb_apsalar_engine_data>(
        UE3JavaApp::clazz, "JavaCallback_ApsalarLogEngineData",
        "(Ljava/lang/String;I)V"),
    ManagedMethod::Register<&cb_apsalar_start>(
        UE3JavaApp::clazz, "JavaCallback_ApsalarStartSession",
        "(Ljava/lang/String;Ljava/lang/String;)V"),

    ManagedMethod::Register<&cb_has_local_value>(
        UE3JavaApp::clazz, "JavaCallback_hasAppLocalValue",
        "(Ljava/lang/String;)Z"),
    ManagedMethod::Register<&cb_get_local_value>(
        UE3JavaApp::clazz, "JavaCallback_getAppLocalValue",
        "(Ljava/lang/String;)Ljava/lang/String;"),
    ManagedMethod::Register<&cb_set_local_value>(
        UE3JavaApp::clazz, "JavaCallback_setAppLocalValue",
        "(Ljava/lang/String;Ljava/lang/String;)V"),

    ManagedMethod::Register<&cb_init_egl>(
        UE3JavaApp::clazz, "JavaCallback_initEGL",
        "(Lcom/epicgames/EpicCitadel/UE3JavaApp$EGLConfigParms;)Z"),
    ManagedMethod::Register<&cb_make_current>(
        UE3JavaApp::clazz, "JavaCallback_makeCurrent", "()Z"),
    ManagedMethod::Register<&cb_swap_buffers>(
        UE3JavaApp::clazz, "JavaCallback_swapBuffers", "()Z"),
    ManagedMethod::Register<&cb_unmake_current>(
        UE3JavaApp::clazz, "JavaCallback_unMakeCurrent", "()Z"),
    {nullptr},
};

static const ManagedMethod EGLConfigMethods[] = {
    ManagedMethod::RegisterNonVirtual<&egl_config_ctor>(
        UE3EGLConfigParms::clazz, "<init>", "()V"),
    {nullptr},
};

static const FieldId EGLConfigFields[] = {
    {.clazz=&UE3EGLConfigParms::clazz, .name="alphaSize", .signature="I",
     .offset=(uintptr_t)&(((UE3EGLConfigParms*)0)->alphaSize), .is_static=0},
    {.clazz=&UE3EGLConfigParms::clazz, .name="blueSize", .signature="I",
     .offset=(uintptr_t)&(((UE3EGLConfigParms*)0)->blueSize), .is_static=0},
    {.clazz=&UE3EGLConfigParms::clazz, .name="depthSize", .signature="I",
     .offset=(uintptr_t)&(((UE3EGLConfigParms*)0)->depthSize), .is_static=0},
    {.clazz=&UE3EGLConfigParms::clazz, .name="greenSize", .signature="I",
     .offset=(uintptr_t)&(((UE3EGLConfigParms*)0)->greenSize), .is_static=0},
    {.clazz=&UE3EGLConfigParms::clazz, .name="redSize", .signature="I",
     .offset=(uintptr_t)&(((UE3EGLConfigParms*)0)->redSize), .is_static=0},
    {.clazz=&UE3EGLConfigParms::clazz, .name="sampleBuffers", .signature="I",
     .offset=(uintptr_t)&(((UE3EGLConfigParms*)0)->sampleBuffers), .is_static=0},
    {.clazz=&UE3EGLConfigParms::clazz, .name="sampleSamples", .signature="I",
     .offset=(uintptr_t)&(((UE3EGLConfigParms*)0)->sampleSamples), .is_static=0},
    {.clazz=&UE3EGLConfigParms::clazz, .name="stencilSize", .signature="I",
     .offset=(uintptr_t)&(((UE3EGLConfigParms*)0)->stencilSize), .is_static=0},
    {.clazz=&UE3EGLConfigParms::clazz, .name="this$0",
     .signature="Lcom/epicgames/EpicCitadel/UE3JavaApp;",
     .offset=(uintptr_t)&(((UE3EGLConfigParms*)0)->this0), .is_static=0},
    {.clazz=&UE3EGLConfigParms::clazz, .name="validConfig", .signature="I",
     .offset=(uintptr_t)&(((UE3EGLConfigParms*)0)->validConfig), .is_static=0},
    {},
};

} // namespace

Class UE3JavaApp::clazz = {
    .classpath = "com/epicgames/EpicCitadel/UE3JavaApp",
    .classname = "UE3JavaApp",
    .managed_methods = UE3JavaMethods,
    .native_methods = nullptr,
    .fields = nullptr,
    .instance_size = sizeof(UE3JavaApp),
};

Class UE3EGLConfigParms::clazz = {
    .classpath = "com/epicgames/EpicCitadel/UE3JavaApp$EGLConfigParms",
    .classname = "EGLConfigParms",
    .managed_methods = EGLConfigMethods,
    .native_methods = nullptr,
    .fields = EGLConfigFields,
    .instance_size = sizeof(UE3EGLConfigParms),
};

static const int registered_ue3_java =
    ClassRegistry::register_class(UE3JavaApp::clazz);
static const int registered_ue3_egl =
    ClassRegistry::register_class(UE3EGLConfigParms::clazz);

extern "C" void open_citadel_java_configure(
    SDL_Window *window, SDL_GLContext context, const char *game_dir,
    const char *main_obb, const char *patch_obb)
{
    g_window = window;
    g_context = context;
    g_game_dir = game_dir ? game_dir : "";
    g_main_obb = main_obb ? main_obb : "";
    g_patch_obb = patch_obb ? patch_obb : "";
    open_citadel_asset_manager_configure(g_game_dir.c_str());
    g_shutdown.store(0, std::memory_order_release);
}

extern "C" jobject open_citadel_java_activity(void)
{
    return reinterpret_cast<jobject>(&g_activity);
}

extern "C" int open_citadel_java_shutdown_requested(void)
{
    return g_shutdown.load(std::memory_order_acquire);
}

extern "C" void open_citadel_java_clear_shutdown(void)
{
    g_shutdown.store(0, std::memory_order_release);
}
