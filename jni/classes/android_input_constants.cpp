#include "jni_internals.h"

/*
 * Tiny read-only framework classes used by the original UE3 Android glue.
 *
 * UE3 intentionally asks JNI for android.view.MotionEvent/KeyEvent constants
 * instead of compiling their numeric values into the native library.  On
 * Android these are static int fields.  The fake JNI represents primitive
 * static fields by storing the value directly in FieldId::offset (its
 * GetStaticIntField implementation returns that slot as the value).
 */

namespace {

#define STATIC_INT_FIELD(clazz, name, value) \
    { .clazz=&clazz, .name=#name, .signature="I", \
      .offset=(uintptr_t)(value), .is_static=1 }

struct MotionEventClass {
    static Class clazz;
};

const FieldId motion_fields[] = {
    STATIC_INT_FIELD(MotionEventClass::clazz, ACTION_DOWN, 0),
    STATIC_INT_FIELD(MotionEventClass::clazz, ACTION_UP, 1),
    STATIC_INT_FIELD(MotionEventClass::clazz, ACTION_MOVE, 2),
    STATIC_INT_FIELD(MotionEventClass::clazz, ACTION_CANCEL, 3),
    STATIC_INT_FIELD(MotionEventClass::clazz, ACTION_OUTSIDE, 4),
    STATIC_INT_FIELD(MotionEventClass::clazz, ACTION_POINTER_DOWN, 5),
    STATIC_INT_FIELD(MotionEventClass::clazz, ACTION_POINTER_UP, 6),
    {},
};

Class MotionEventClass::clazz = {
    .classpath = "android/view/MotionEvent",
    .classname = "MotionEvent",
    .managed_methods = nullptr,
    .native_methods = nullptr,
    .fields = motion_fields,
    .instance_size = 0,
};

struct KeyEventClass {
    static Class clazz;
};

/* Android KeyEvent values have remained stable for these legacy API-1 keys. */
const FieldId key_fields[] = {
    STATIC_INT_FIELD(KeyEventClass::clazz, ACTION_DOWN, 0),
    STATIC_INT_FIELD(KeyEventClass::clazz, ACTION_UP, 1),
    STATIC_INT_FIELD(KeyEventClass::clazz, ACTION_MULTIPLE, 2),

    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_HOME, 3),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_BACK, 4),

    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_0, 7),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_1, 8),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_2, 9),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_3, 10),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_4, 11),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_5, 12),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_6, 13),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_7, 14),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_8, 15),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_9, 16),

    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_STAR, 17),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_DPAD_UP, 19),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_DPAD_DOWN, 20),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_DPAD_LEFT, 21),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_DPAD_RIGHT, 22),

    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_A, 29),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_B, 30),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_C, 31),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_D, 32),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_E, 33),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_F, 34),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_G, 35),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_H, 36),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_I, 37),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_J, 38),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_K, 39),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_L, 40),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_M, 41),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_N, 42),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_O, 43),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_P, 44),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_Q, 45),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_R, 46),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_S, 47),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_T, 48),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_U, 49),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_V, 50),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_W, 51),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_X, 52),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_Y, 53),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_Z, 54),

    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_COMMA, 55),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_PERIOD, 56),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_ALT_LEFT, 57),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_ALT_RIGHT, 58),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_SHIFT_LEFT, 59),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_SHIFT_RIGHT, 60),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_TAB, 61),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_SPACE, 62),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_ENTER, 66),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_DEL, 67),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_GRAVE, 68),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_MINUS, 69),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_EQUALS, 70),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_LEFT_BRACKET, 71),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_RIGHT_BRACKET, 72),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_BACKSLASH, 73),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_SEMICOLON, 74),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_APOSTROPHE, 75),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_SLASH, 76),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_NUM, 78),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_PLUS, 81),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_MENU, 82),
    STATIC_INT_FIELD(KeyEventClass::clazz, KEYCODE_SEARCH, 84),
    {},
};

Class KeyEventClass::clazz = {
    .classpath = "android/view/KeyEvent",
    .classname = "KeyEvent",
    .managed_methods = nullptr,
    .native_methods = nullptr,
    .fields = key_fields,
    .instance_size = 0,
};

const int registered_motion = ClassRegistry::register_class(MotionEventClass::clazz);
const int registered_key = ClassRegistry::register_class(KeyEventClass::clazz);

} // namespace
