#pragma once

#include <SDL2/SDL.h>

#include "jni.h"

struct so_module;

using KatamariKeyFn = void (*)(JNIEnv *, jobject, int, int);
using KatamariTouchFn = void (*)(JNIEnv *, jobject, int, int, int, int);
using KatamariAccelFn = void (__attribute__((pcs("aapcs"))) *)(
    JNIEnv *, jobject, double, double, double);

void katamari_input_init(so_module *mod, JNIEnv *env, int width, int height);
bool katamari_input_event(const SDL_Event *event);
void katamari_input_tick(long frame);
void katamari_input_cursor_position(float *x, float *y, int *visible);
void katamari_input_cursor_set(float x, float y);
void katamari_input_cursor_press(bool down);
bool katamari_input_inject_control(const char *name, bool down);
