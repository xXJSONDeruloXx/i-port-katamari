#pragma once

/*
 */
void emulator_control_init(void);
bool emulator_control_tick(long frame);
void emulator_control_after_draw(long frame, int width, int height);
void emulator_control_shutdown(long frame);
