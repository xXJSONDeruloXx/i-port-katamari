#include "katamari_input.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdint.h>
#include <string.h>

#include "so_util.h"
#include "trace.h"

namespace {

enum {
    ACTION_DOWN = 0,
    ACTION_UP = 1,
    ACTION_MOVE = 2,
    ACTION_CANCEL = 3,
    KEYCODE_BUTTON_SELECT = 109,
};

static JNIEnv *g_env = NULL;
static KatamariKeyFn g_key = NULL;
static KatamariTouchFn g_touch = NULL;
static KatamariAccelFn g_accel = NULL;
using KatamariTriggerFn = int (*)();
static KatamariTriggerFn g_trigger = NULL;
static bool g_trace_input = false;
static uint32_t *g_touch_ptr_f = NULL;
static uint32_t *g_touch_ptr_b = NULL;
static uint32_t *g_touch_event_count = NULL;
static int g_width = 640;
static int g_height = 480;
static SDL_GameController *g_controller = NULL;

static bool g_accel_mode = false;
static bool g_accel_dpad_up = false;
static bool g_accel_dpad_down = false;
static bool g_accel_dpad_left = false;
static bool g_accel_dpad_right = false;
static bool g_l2_trigger_down = false;
static bool g_r2_trigger_down = false;
static Uint32 g_last_accel_toggle_ms = 0;

static float g_cursor_x = 320.0f;
static float g_cursor_y = 240.0f;
static bool g_cursor_visible = true;
static bool g_cursor_down = false;
static int g_cursor_dx = 0;
static int g_cursor_dy = 0;
static Uint32 g_cursor_last_ms = 0;

static bool g_mouse_down = false;
static int g_mouse_id = 2;
static int g_pause_id = 3;
static int g_reverse_id = 4;
static int g_strafe_left_id = 5;
static int g_strafe_right_id = 6;
static bool g_strafe_left_down = false;
static bool g_strafe_right_down = false;
static bool g_autopilot = false;
static int g_autopilot_step = 0;
static long g_autopilot_next = 0;

static constexpr double kNeutralAccelX = 6.0;
static constexpr double kNeutralAccelY = 0.0;
static constexpr double kNeutralAccelZ = 9.8;
static constexpr double kAccelDpadStep = 6.0;

static int clamp_coordinate(float value, int maximum)
{
    if (maximum <= 0)
        return 0;
    return std::max(0, std::min(maximum - 1, (int)std::lround(value)));
}

static void send_touch(int action, int x, int y, int id)
{
    if (g_trace_input)
        trace("input: send touch action=%d x=%d y=%d id=%d fn=%p",
              action, x, y, id, (void *)g_touch);
    if (g_touch)
        g_touch(g_env, (jobject)(uintptr_t)0x42424242, x, y, id, action);
    if (g_trace_input && g_touch_ptr_f && g_touch_ptr_b &&
        g_touch_event_count)
        trace("input: touch action=%d x=%d y=%d id=%d queue=%u/%u count=%u",
              action, x, y, id, *g_touch_ptr_f, *g_touch_ptr_b,
              *g_touch_event_count);
}

static void send_key(int code, int action)
{
    if (g_key)
        g_key(g_env, (jobject)(uintptr_t)0x42424242, code, action);
}

static void send_accel(double x, double y, double z)
{
    if (g_accel)
        g_accel(g_env, (jobject)(uintptr_t)0x42424242, x, y, z);
}

static void open_controller(void)
{
    if (g_controller)
        return;

    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i))
            continue;
        g_controller = SDL_GameControllerOpen(i);
        if (g_controller) {
            trace("input: controller opened: %s",
                  SDL_GameControllerName(g_controller) ?: "unknown");
            return;
        }
    }
}

static void show_cursor(void)
{
    g_cursor_visible = true;
    g_cursor_last_ms = SDL_GetTicks();
}

static void hide_cursor(void)
{
    if (g_cursor_down) {
        send_touch(ACTION_UP, clamp_coordinate(g_cursor_x, g_width),
                    clamp_coordinate(g_cursor_y, g_height), g_mouse_id);
        g_cursor_down = false;
    }
    g_cursor_visible = false;
    g_cursor_dx = 0;
    g_cursor_dy = 0;
}

static void move_cursor_direction(int dx, int dy)
{
    show_cursor();
    g_cursor_dx = dx;
    g_cursor_dy = dy;
}

static void tap_cursor(void)
{
    if (!g_cursor_visible)
        show_cursor();
    int x = clamp_coordinate(g_cursor_x, g_width);
    int y = clamp_coordinate(g_cursor_y, g_height);
    send_touch(ACTION_DOWN, x, y, g_mouse_id);
    send_touch(ACTION_UP, x, y, g_mouse_id);
    trace("input: virtual tap at %d,%d", x, y);
}

static void tap_game_pause(void)
{
    int x = clamp_coordinate((float)g_width * 0.5f, g_width);
    int y = clamp_coordinate(23.0f, g_height);
    send_touch(ACTION_DOWN, x, y, g_pause_id);
    send_touch(ACTION_UP, x, y, g_pause_id);
    trace("input: virtual pause tap at %d,%d", x, y);
}

static void tap_game_reverse(void)
{
    int x = clamp_coordinate((float)g_width * 0.5f, g_width);
    int y = clamp_coordinate((float)g_height - 23.0f, g_height);
    send_touch(ACTION_DOWN, x, y, g_reverse_id);
    send_touch(ACTION_UP, x, y, g_reverse_id);
    trace("input: virtual reverse tap at %d,%d", x, y);
}

static int side_touch_x(bool left)
{
    return left ? clamp_coordinate(1.0f, g_width)
                : clamp_coordinate((float)g_width - 2.0f, g_width);
}

static int side_touch_y(void)
{
    return clamp_coordinate((float)g_height * 0.5f, g_height);
}

static void set_strafe_touch(bool left, bool down)
{
    bool &active = left ? g_strafe_left_down : g_strafe_right_down;
    const int id = left ? g_strafe_left_id : g_strafe_right_id;
    const int x = side_touch_x(left);
    const int y = side_touch_y();

    if (down && !active) {
        send_touch(ACTION_DOWN, x, y, id);
        active = true;
        trace("input: strafe %s touch down at %d,%d",
              left ? "left" : "right", x, y);
    } else if (!down && active) {
        send_touch(ACTION_UP, x, y, id);
        active = false;
        trace("input: strafe %s touch up", left ? "left" : "right");
    }
}

static void update_strafe_touches(void)
{
    if (g_strafe_left_down)
        send_touch(ACTION_MOVE, side_touch_x(true), side_touch_y(),
                   g_strafe_left_id);
    if (g_strafe_right_down)
        send_touch(ACTION_MOVE, side_touch_x(false), side_touch_y(),
                   g_strafe_right_id);
}

static void release_strafe_touches(void)
{
    set_strafe_touch(true, false);
    set_strafe_touch(false, false);
}

static float digital_axis(bool negative, bool positive)
{
    if (negative == positive)
        return 0.0f;
    return positive ? 1.0f : -1.0f;
}

static void clear_accel_directions(void)
{
    g_accel_dpad_up = false;
    g_accel_dpad_down = false;
    g_accel_dpad_left = false;
    g_accel_dpad_right = false;
}

static void toggle_accel_mode(void)
{
    Uint32 now = SDL_GetTicks();
    if (g_last_accel_toggle_ms && now - g_last_accel_toggle_ms < 200)
        return;
    g_last_accel_toggle_ms = now;

    release_strafe_touches();
    clear_accel_directions();
    g_accel_mode = !g_accel_mode;
    if (g_accel_mode)
        hide_cursor();
    else
        show_cursor();
    trace("input: accelerometer D-pad mode %s",
          g_accel_mode ? "on" : "off");
}

static void set_accel_dpad(const char *name, bool down)
{
    if (!strcmp(name, "up"))
        g_accel_dpad_up = down;
    else if (!strcmp(name, "down"))
        g_accel_dpad_down = down;
    else if (!strcmp(name, "left"))
        g_accel_dpad_left = down;
    else if (!strcmp(name, "right"))
        g_accel_dpad_right = down;
}

static void autopilot_tick(long frame)
{
    if (!g_autopilot || frame < g_autopilot_next)
        return;

    switch (g_autopilot_step++) {
    case 0:
        tap_cursor();
        g_autopilot_next = frame + 45;
        break;
    case 1:
        move_cursor_direction(1, 0);
        g_autopilot_next = frame + 20;
        break;
    case 2:
        g_cursor_dx = 0;
        tap_cursor();
        g_autopilot_next = frame + 45;
        break;
    case 3:
        toggle_accel_mode();
        set_accel_dpad("right", true);
        g_autopilot_next = frame + 90;
        break;
    default:
        clear_accel_directions();
        if (g_accel_mode)
            toggle_accel_mode();
        g_autopilot_next = frame + 120;
        break;
    }
}

}

void katamari_input_init(so_module *mod, JNIEnv *env, int width, int height)
{
    g_env = env;
    g_width = width > 0 ? width : 640;
    g_height = height > 0 ? height : 480;
    g_cursor_x = g_width * 0.5f;
    g_cursor_y = g_height * 0.5f;
    g_cursor_visible = true;
    g_cursor_last_ms = SDL_GetTicks();
    g_accel_mode = false;
    clear_accel_directions();
    g_l2_trigger_down = false;
    g_r2_trigger_down = false;
    g_last_accel_toggle_ms = 0;
    g_strafe_left_down = false;
    g_strafe_right_down = false;

    g_key = (KatamariKeyFn)so_symbol(
        mod, "Java_com_namcobandaigames_katamari_AppGLSurfaceView_nativeOnKeyEvent");
    g_touch = (KatamariTouchFn)so_symbol(
        mod, "Java_com_namcobandaigames_katamari_AppGLSurfaceView_nativeOnTouchEvent");
    g_accel = (KatamariAccelFn)so_symbol(
        mod, "Java_com_namcobandaigames_katamari_Katamari_nativeOnAccelerate");
    g_trigger = (KatamariTriggerFn)so_symbol(mod,
                                              "_ZN6NTouch10getTriggerEv");
    g_touch_ptr_f = (uint32_t *)so_symbol(mod, "_ZN6NTouch9touchPtrFE");
    g_touch_ptr_b = (uint32_t *)so_symbol(mod, "_ZN6NTouch9touchPtrBE");
    g_touch_event_count =
        (uint32_t *)so_symbol(mod, "_ZN6NTouch13touchEventCntE");

    const char *auto_env = getenv("KATAMARI_AUTOPILOT");
    g_autopilot = auto_env && *auto_env && strcmp(auto_env, "0") != 0;
    const char *trace_env = getenv("KATAMARI_INPUTTRACE");
    g_trace_input = trace_env && *trace_env && strcmp(trace_env, "0") != 0;
    g_autopilot_step = 0;
    g_autopilot_next = 30;

    open_controller();
    trace("input: touch=%p key=%p accelerometer=%p trigger=%p cursor=%s "
          "autopilot=%s",
          (void *)g_touch, (void *)g_key, (void *)g_accel,
          (void *)g_trigger,
          g_cursor_visible ? "visible" : "hidden",
          g_autopilot ? "on" : "off");
}

bool katamari_input_event(const SDL_Event *event)
{
    if (!event)
        return true;

    switch (event->type) {
    case SDL_QUIT:
        release_strafe_touches();
        return false;

    case SDL_CONTROLLERDEVICEADDED:
        open_controller();
        break;

    case SDL_CONTROLLERAXISMOTION:
        if (event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            bool down = event->caxis.value > 16000;
            if (down && !g_l2_trigger_down)
                tap_game_reverse();
            g_l2_trigger_down = down;
        } else if (event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
            bool down = event->caxis.value > 16000;
            if (down && !g_r2_trigger_down)
                toggle_accel_mode();
            g_r2_trigger_down = down;
        }
        break;

    case SDL_CONTROLLERBUTTONDOWN:
        switch (event->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A:
            tap_cursor();
            break;
        case SDL_CONTROLLER_BUTTON_X:
            tap_cursor();
            break;
        case SDL_CONTROLLER_BUTTON_B:
            trace("input: B ignored (Android back is unbound)");
            break;
        case SDL_CONTROLLER_BUTTON_BACK:
            send_key(KEYCODE_BUTTON_SELECT, ACTION_DOWN);
            send_key(KEYCODE_BUTTON_SELECT, ACTION_UP);
            break;
        case SDL_CONTROLLER_BUTTON_START:
            release_strafe_touches();
            if (g_accel_mode) {
                clear_accel_directions();
                g_accel_mode = false;
            }
            tap_game_pause();
            show_cursor();
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            if (g_accel_mode)
                set_accel_dpad("up", true);
            else
                move_cursor_direction(0, -1);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            if (g_accel_mode)
                set_accel_dpad("down", true);
            else
                move_cursor_direction(0, 1);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            if (g_accel_mode)
                set_accel_dpad("left", true);
            else
                move_cursor_direction(-1, 0);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            if (g_accel_mode)
                set_accel_dpad("right", true);
            else
                move_cursor_direction(1, 0);
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            set_strafe_touch(true, true);
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            set_strafe_touch(false, true);
            break;
        default:
            break;
        }
        break;

    case SDL_CONTROLLERBUTTONUP:
        switch (event->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            if (g_accel_mode)
                set_accel_dpad("up", false);
            else {
                g_cursor_dy = 0;
            }
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            if (g_accel_mode)
                set_accel_dpad("down", false);
            else {
                g_cursor_dy = 0;
            }
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            if (g_accel_mode)
                set_accel_dpad("left", false);
            else {
                g_cursor_dx = 0;
            }
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            if (g_accel_mode)
                set_accel_dpad("right", false);
            else {
                g_cursor_dx = 0;
            }
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            set_strafe_touch(true, false);
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            set_strafe_touch(false, false);
            break;
        default:
            break;
        }
        break;

    case SDL_KEYDOWN:
        if (event->key.repeat)
            break;
        switch (event->key.keysym.sym) {
        case SDLK_ESCAPE:
            release_strafe_touches();
            return false;
        case SDLK_RETURN:
        case SDLK_SPACE:
            tap_cursor();
            break;
        case SDLK_UP:
            move_cursor_direction(0, -1);
            break;
        case SDLK_DOWN:
            move_cursor_direction(0, 1);
            break;
        case SDLK_LEFT:
            move_cursor_direction(-1, 0);
            break;
        case SDLK_RIGHT:
            move_cursor_direction(1, 0);
            break;
        default:
            break;
        }
        break;

    case SDL_KEYUP:
        if (event->key.keysym.sym == SDLK_UP || event->key.keysym.sym == SDLK_DOWN)
            g_cursor_dy = 0;
        if (event->key.keysym.sym == SDLK_LEFT || event->key.keysym.sym == SDLK_RIGHT)
            g_cursor_dx = 0;
        break;

    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            g_mouse_down = true;
            hide_cursor();
            g_cursor_x = event->button.x;
            g_cursor_y = event->button.y;
            send_touch(ACTION_DOWN, event->button.x, event->button.y, g_mouse_id);
        }
        break;

    case SDL_MOUSEMOTION:
        if (g_mouse_down)
            send_touch(ACTION_MOVE, event->motion.x, event->motion.y, g_mouse_id);
        break;

    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT && g_mouse_down) {
            g_mouse_down = false;
            send_touch(ACTION_UP, event->button.x, event->button.y, g_mouse_id);
        }
        break;

    default:
        break;
    }

    return true;
}

void katamari_input_tick(long frame)
{
    open_controller();
    update_strafe_touches();

    Uint32 now = SDL_GetTicks();
    float dt = (now - g_cursor_last_ms) / 1000.0f;
    g_cursor_last_ms = now;
    if (g_cursor_visible && (g_cursor_dx || g_cursor_dy)) {
        g_cursor_x += g_cursor_dx * 420.0f * dt;
        g_cursor_y += g_cursor_dy * 420.0f * dt;
        g_cursor_x = std::max(0.0f, std::min(g_cursor_x, (float)g_width - 1.0f));
        g_cursor_y = std::max(0.0f, std::min(g_cursor_y, (float)g_height - 1.0f));
        if (g_cursor_down)
            send_touch(ACTION_MOVE, (int)g_cursor_x, (int)g_cursor_y, g_mouse_id);
    }

    send_accel(
        kNeutralAccelX +
            digital_axis(g_accel_dpad_left, g_accel_dpad_right) *
                kAccelDpadStep,
        kNeutralAccelY +
            digital_axis(g_accel_dpad_up, g_accel_dpad_down) *
                kAccelDpadStep,
        kNeutralAccelZ);
    if (g_trace_input && g_trigger && g_trigger())
        trace("input: native touch trigger is active at frame %ld", frame);
    autopilot_tick(frame);
}

void katamari_input_cursor_position(float *x, float *y, int *visible)
{
    if (x)
        *x = g_cursor_x;
    if (y)
        *y = g_cursor_y;
    if (visible)
        *visible = g_cursor_visible ? 1 : 0;
}

void katamari_input_cursor_set(float x, float y)
{
    show_cursor();
    g_cursor_x = std::max(0.0f, std::min(x, (float)g_width - 1.0f));
    g_cursor_y = std::max(0.0f, std::min(y, (float)g_height - 1.0f));
    if (g_cursor_down)
        send_touch(ACTION_MOVE, (int)g_cursor_x, (int)g_cursor_y, g_mouse_id);
}

void katamari_input_cursor_press(bool down)
{
    int x = clamp_coordinate(g_cursor_x, g_width);
    int y = clamp_coordinate(g_cursor_y, g_height);
    if (down) {
        if (!g_cursor_down) {
            send_touch(ACTION_DOWN, x, y, g_mouse_id);
            g_cursor_down = true;
        }
    } else if (g_cursor_down) {
        send_touch(ACTION_UP, x, y, g_mouse_id);
        g_cursor_down = false;
    }
}

bool katamari_input_inject_control(const char *name, bool down)
{
    if (!name)
        return false;

    if (!strcmp(name, "a") || !strcmp(name, "x")) {
        katamari_input_cursor_press(down);
        return true;
    }
    if (!strcmp(name, "b")) {
        trace("input: B ignored (Android back is unbound)");
        return true;
    }
    if (!strcmp(name, "start")) {
        if (down) {
            release_strafe_touches();
            if (g_accel_mode) {
                clear_accel_directions();
                g_accel_mode = false;
            }
            tap_game_pause();
            show_cursor();
        }
        return true;
    }
    if (!strcmp(name, "select")) {
        if (down) {
            send_key(KEYCODE_BUTTON_SELECT, ACTION_DOWN);
            send_key(KEYCODE_BUTTON_SELECT, ACTION_UP);
        }
        return true;
    }
    if (!strcmp(name, "y") || !strcmp(name, "l3") ||
        !strcmp(name, "r3")) {
        return true;
    }
    if (!strcmp(name, "up") || !strcmp(name, "down") ||
        !strcmp(name, "left") || !strcmp(name, "right")) {
        if (g_accel_mode) {
            set_accel_dpad(name, down);
            return true;
        }
        if (!down) {
            if ((!strcmp(name, "up") || !strcmp(name, "down")) &&
                g_cursor_dy != 0)
                g_cursor_dy = 0;
            if ((!strcmp(name, "left") || !strcmp(name, "right")) &&
                g_cursor_dx != 0)
                g_cursor_dx = 0;
            return true;
        }
        int dx = 0;
        int dy = 0;
        if (!strcmp(name, "left"))
            dx = -1;
        else if (!strcmp(name, "right"))
            dx = 1;
        else if (!strcmp(name, "up"))
            dy = -1;
        else
            dy = 1;
        move_cursor_direction(dx, dy);
        return true;
    }
    if (!strcmp(name, "l2")) {
        if (down)
            tap_game_reverse();
        return true;
    }
    if (!strcmp(name, "r2")) {
        if (down)
            toggle_accel_mode();
        return true;
    }
    if (!strcmp(name, "l1")) {
        set_strafe_touch(true, down);
        return true;
    }
    if (!strcmp(name, "r1")) {
        set_strafe_touch(false, down);
        return true;
    }
    return false;
}
