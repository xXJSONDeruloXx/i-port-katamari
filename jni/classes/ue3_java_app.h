#pragma once

#include <SDL2/SDL.h>

#include "jni.h"
#include "jni_internals.h"

class UE3JavaApp : public Object {
public:
    static Class clazz;
    Class *_getClass() override { return &clazz; }
};

class UE3EGLConfigParms : public Object {
public:
    static Class clazz;

    jint redSize = 5;
    jint greenSize = 6;
    jint blueSize = 5;
    jint alphaSize = 0;
    jint depthSize = 16;
    jint stencilSize = 0;
    jint sampleBuffers = 0;
    jint sampleSamples = 0;
    jint validConfig = 0;
    jobject this0 = nullptr;

    Class *_getClass() override { return &clazz; }
};

#ifdef __cplusplus
extern "C" {
#endif

/* Configure the fake Java activity before JNI_OnLoad/NativeCallback_Initialize. */
void open_citadel_java_configure(SDL_Window *window, SDL_GLContext context,
                                 const char *game_dir, const char *main_obb,
                                 const char *patch_obb);

jobject open_citadel_java_activity(void);
int open_citadel_java_shutdown_requested(void);
void open_citadel_java_clear_shutdown(void);
long open_citadel_java_frames_presented(void);

#ifdef __cplusplus
}
#endif
