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

static int16_t g_lx = 0;
static int16_t g_ly = 0;
static int16_t g_rx = 0;
static int16_t g_ry = 0;
static bool g_left_active = false;
static bool g_right_active = false;

static float g_cursor_x = 320.0f;
static float g_cursor_y = 240.0f;
static bool g_cursor_visible = true;
static bool g_cursor_down = false;
static int g_cursor_dx = 0;
static int g_cursor_dy = 0;
static Uint32 g_cursor_last_ms = 0;

static bool g_mouse_down = false;
static int g_mouse_id = 2;
static bool g_autopilot = false;
static int g_autopilot_step = 0;
static long g_autopilot_next = 0;

static float axis_value(int16_t value)
{
    float scaled = (float)value / 32767.0f;
    if (fabsf(scaled) < 0.16f)
        return 0.0f;
    return std::max(-1.0f, std::min(1.0f, scaled));
}

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

static void set_virtual_stick(bool left, float x, float y)
{
    const int id = left ? 0 : 1;
    const float base_x = left ? g_width * 0.22f : g_width * 0.78f;
    const float base_y = g_height * 0.72f;
    const float radius = std::min(g_width, g_height) * 0.18f;
    const float magnitude = std::sqrt(x * x + y * y);
    bool &active = left ? g_left_active : g_right_active;

    if (magnitude < 0.16f) {
        if (active) {
            send_touch(ACTION_UP, clamp_coordinate(base_x, g_width),
                       clamp_coordinate(base_y, g_height), id);
            active = false;
        }
        return;
    }

    float length = std::min(1.0f, magnitude);
    float px = base_x + x / (magnitude ? magnitude : 1.0f) * radius * length;
    float py = base_y + y / (magnitude ? magnitude : 1.0f) * radius * length;
    int ix = clamp_coordinate(px, g_width);
    int iy = clamp_coordinate(py, g_height);

    send_touch(active ? ACTION_MOVE : ACTION_DOWN, ix, iy, id);
    active = true;
}

static void release_virtual_touches(void)
{
    if (g_left_active) {
        send_touch(ACTION_UP, (int)(g_width * 0.22f), (int)(g_height * 0.72f), 0);
        g_left_active = false;
    }
    if (g_right_active) {
        send_touch(ACTION_UP, (int)(g_width * 0.78f), (int)(g_height * 0.72f), 1);
        g_right_active = false;
    }
}

static void autopilot_tick(long frame)
{
    if (!g_autopilot || frame < g_autopilot_next)
        return;

    /* The script is intentionally conservative: it exercises the complete
     * JNI touch path without assuming a particular menu language or layout. */
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
        set_virtual_stick(true, 0.75f, 0.0f);
        set_virtual_stick(false, 0.0f, -0.75f);
        g_autopilot_next = frame + 90;
        break;
    default:
        set_virtual_stick(true, 0.0f, 0.0f);
        set_virtual_stick(false, 0.0f, 0.0f);
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
    g_left_active = false;
    g_right_active = false;

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
        release_virtual_touches();
        return false;

    case SDL_CONTROLLERDEVICEADDED:
        open_controller();
        break;

    case SDL_CONTROLLERAXISMOTION:
        if (event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
            g_lx = event->caxis.value;
        else if (event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
            g_ly = event->caxis.value;
        else if (event->caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX)
            g_rx = event->caxis.value;
        else if (event->caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY)
            g_ry = event->caxis.value;
        break;

    case SDL_CONTROLLERBUTTONDOWN:
        switch (event->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_X:
            tap_cursor();
            break;
        case SDL_CONTROLLER_BUTTON_B:
            send_key(4, 0);
            send_key(4, 1);
            break;
        case SDL_CONTROLLER_BUTTON_START:
            show_cursor();
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            move_cursor_direction(0, -1);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            move_cursor_direction(0, 1);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            move_cursor_direction(-1, 0);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            move_cursor_direction(1, 0);
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            show_cursor();
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            send_accel(-6.0, 0.0, 0.0);
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            send_accel(6.0, 0.0, 0.0);
            break;
        default:
            break;
        }
        break;

    case SDL_CONTROLLERBUTTONUP:
        if (event->cbutton.button >= SDL_CONTROLLER_BUTTON_DPAD_UP &&
            event->cbutton.button <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            g_cursor_dx = 0;
            g_cursor_dy = 0;
        }
        break;

    case SDL_KEYDOWN:
        if (event->key.repeat)
            break;
        switch (event->key.keysym.sym) {
        case SDLK_ESCAPE:
            release_virtual_touches();
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
    if (g_controller) {
        g_lx = SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_LEFTX);
        g_ly = SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_LEFTY);
        g_rx = SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_RIGHTX);
        g_ry = SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_RIGHTY);
    }

    set_virtual_stick(true, axis_value(g_lx), axis_value(g_ly));
    set_virtual_stick(false, axis_value(g_rx), axis_value(g_ry));

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

    /* A stationary sample keeps the game's gravity state initialized on
     * handhelds that do not have an Android sensor service. */
    send_accel(0.0, 0.0, 9.8);
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
        if (down)
            send_key(4, ACTION_DOWN);
        return true;
    }
    if (!strcmp(name, "start")) {
        if (down)
            show_cursor();
        return true;
    }
    if (!strcmp(name, "up") || !strcmp(name, "down") ||
        !strcmp(name, "left") || !strcmp(name, "right")) {
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
    if (!strcmp(name, "l1") || !strcmp(name, "l2")) {
        if (down)
            send_accel(-6.0, 0.0, 0.0);
        return true;
    }
    if (!strcmp(name, "r1") || !strcmp(name, "r2")) {
        if (down)
            send_accel(6.0, 0.0, 0.0);
        return true;
    }
    if (!strcmp(name, "select") || !strcmp(name, "y") ||
        !strcmp(name, "l3") || !strcmp(name, "r3"))
        return true;
    return false;
}

bool katamari_input_inject_stick(const char *name, float x, float y)
{
    if (!name)
        return false;
    if (!strcmp(name, "left")) {
        g_lx = (int16_t)std::lround(std::max(-1.0f, std::min(1.0f, x)) * 32767.0f);
        g_ly = (int16_t)std::lround(std::max(-1.0f, std::min(1.0f, y)) * 32767.0f);
        return true;
    }
    if (!strcmp(name, "right")) {
        g_rx = (int16_t)std::lround(std::max(-1.0f, std::min(1.0f, x)) * 32767.0f);
        g_ry = (int16_t)std::lround(std::max(-1.0f, std::min(1.0f, y)) * 32767.0f);
        return true;
    }
    return false;
}
