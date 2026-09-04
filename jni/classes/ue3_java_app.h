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

    jint redSize = 8;
    jint greenSize = 8;
    jint blueSize = 8;
    jint alphaSize = 8;
    jint depthSize = 24;
    jint stencilSize = 8;
    jint sampleBuffers = 0;
    jint sampleSamples = 0;
    jint validConfig = 1;
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

#ifdef __cplusplus
}
#endif
