#pragma once

extern "C" {
long open_citadel_gl_draws(void);
long open_citadel_gl_textures(void);
long open_citadel_gl_atc_decoded(void);
int open_citadel_gl_shaders_ok(void);
int open_citadel_gl_shaders_failed(void);
int open_citadel_gl_programs_ok(void);
int open_citadel_gl_programs_failed(void);
}
