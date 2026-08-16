#pragma once

#include "jni.h"
#include "jni_internals.h"

/*
 * The original APK keeps the game loop in libkatamari.so, but the native
 * code calls a handful of static Java helpers for files and audio.  These
 * classes are the small Linux-side replacement for those helpers; they are
 * deliberately limited to the methods the ARM library actually looks up.
 */
class Katamari : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }

    static jint getAssetFileLength(JNIEnv *env, jclass clazz, jstring name);
    static jint loadAssetFile(JNIEnv *env, jclass clazz, jstring name,
                              jbyteArray buffer, jint length);
    static jint saveUserFile(JNIEnv *env, jclass clazz, jstring name,
                             jbyteArray buffer, jint length);
    static jint loadUserFile(JNIEnv *env, jclass clazz, jstring name,
                             jbyteArray buffer, jint length);
    static void openWebNamcoKT(JNIEnv *env, jclass clazz);
};

class AudioTool : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }

    static void addSound(JNIEnv *env, jclass clazz, jint id);
    static void deallocAll(JNIEnv *env, jclass clazz);
    static void dispose(JNIEnv *env, jclass clazz, jint id);
    static jint isLoop(JNIEnv *env, jclass clazz, jint id);
    static jint isPlaying(JNIEnv *env, jclass clazz, jint id);
    static jint newID(JNIEnv *env, jclass clazz);
    static void pause(JNIEnv *env, jclass clazz, jint id);
    static void play(JNIEnv *env, jclass clazz, jint id);
    static void playSound(JNIEnv *env, jclass clazz, jint id, jint loops);
    static void releaseSound(JNIEnv *env, jclass clazz);
    static void rewind(JNIEnv *env, jclass clazz, jint id);
    static void setLoop(JNIEnv *env, jclass clazz, jint id, jint loop);
    static void setNextPlayer(JNIEnv *env, jclass clazz, jint id, jint next);
    static void setSfxVolume(JNIEnv *env, jclass clazz, jfloat volume);
    static void setVolume(JNIEnv *env, jclass clazz, jint id, jfloat volume);
    static void setup(JNIEnv *env, jclass clazz, jint id, jstring name,
                      jint loop);
    static void stop(JNIEnv *env, jclass clazz, jint id);
    static void stopAll(JNIEnv *env, jclass clazz);
};

