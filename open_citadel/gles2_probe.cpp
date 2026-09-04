/*
 * GLES2 observation and compatibility layer for Epic Citadel.
 *
 * Keep this separate from Katamari's symtab_glprobe.cpp: that probe forwards
 * through a GLES1 table, while UE3 is a shader-era GLES2 renderer. These
 * wrappers sit before symtable_gles2 in the resolver and forward through the
 * already-populated GLAD GLES2 pointers.
 */
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

#include "atc_decompress.h"
#include "khronos/glad.h"
#include "so_util.h"
#include "thunk_gen.h"
#include "trace.h"

#ifndef GL_ATC_RGB_AMD
#define GL_ATC_RGB_AMD 0x8C92
#endif
#ifndef GL_ATC_RGBA_EXPLICIT_ALPHA_AMD
#define GL_ATC_RGBA_EXPLICIT_ALPHA_AMD 0x8C93
#endif
#ifndef GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD
#define GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD 0x87EE
#endif

static std::atomic<int> g_shaders_ok{0};
static std::atomic<int> g_shaders_failed{0};
static std::atomic<int> g_programs_ok{0};
static std::atomic<int> g_programs_failed{0};
static std::atomic<long> g_draws{0};
static std::atomic<long> g_textures{0};
static std::atomic<long> g_atc_decoded{0};
static std::atomic<long> g_compressed_passthrough{0};

static void report_shader_log(GLuint object, bool shader)
{
    GLint length = 0;
    if (shader)
        glad_glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
    else
        glad_glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1)
        return;

    std::vector<GLchar> log(static_cast<std::size_t>(length) + 1);
    if (shader)
        glad_glGetShaderInfoLog(object, length, nullptr, log.data());
    else
        glad_glGetProgramInfoLog(object, length, nullptr, log.data());
    trace("OpenCitadel GL: %s log: %s", shader ? "shader" : "program",
          log.data());
}

extern "C" void open_citadel_glCompileShader(GLuint shader)
{
    glad_glCompileShader(shader);
    GLint ok = 0;
    glad_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) {
        ++g_shaders_ok;
    } else {
        ++g_shaders_failed;
        report_shader_log(shader, true);
    }
}

extern "C" void open_citadel_glLinkProgram(GLuint program)
{
    glad_glLinkProgram(program);
    GLint ok = 0;
    glad_glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok) {
        ++g_programs_ok;
    } else {
        ++g_programs_failed;
        report_shader_log(program, false);
    }
}

extern "C" void open_citadel_glDrawArrays(GLenum mode, GLint first,
                                            GLsizei count)
{
    ++g_draws;
    glad_glDrawArrays(mode, first, count);
}

extern "C" void open_citadel_glDrawElements(GLenum mode, GLsizei count,
                                              GLenum type,
                                              const void *indices)
{
    ++g_draws;
    glad_glDrawElements(mode, count, type, indices);
}

extern "C" void open_citadel_glTexImage2D(
    GLenum target, GLint level, GLint internalformat, GLsizei width,
    GLsizei height, GLint border, GLenum format, GLenum type,
    const void *pixels)
{
    ++g_textures;
    glad_glTexImage2D(target, level, internalformat, width, height, border,
                      format, type, pixels);
}

enum class AtcKind { none, rgb, explicit_alpha, interpolated_alpha };

static AtcKind atc_kind(GLenum format)
{
    switch (format) {
    case GL_ATC_RGB_AMD: return AtcKind::rgb;
    case GL_ATC_RGBA_EXPLICIT_ALPHA_AMD: return AtcKind::explicit_alpha;
    case GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD:
        return AtcKind::interpolated_alpha;
    default: return AtcKind::none;
    }
}

static bool decode_atc(AtcKind kind, GLsizei width, GLsizei height,
                       GLsizei image_size, const void *data,
                       std::vector<std::uint8_t> &rgba)
{
    if (kind == AtcKind::none || width <= 0 || height <= 0 ||
        image_size < 0 || !data)
        return false;

    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    if (w > SIZE_MAX / h || w * h > SIZE_MAX / 4)
        return false;

    try {
        rgba.resize(w * h * 4);
    } catch (const std::bad_alloc &) {
        return false;
    }

    bool ok = false;
    switch (kind) {
    case AtcKind::rgb:
        ok = atc::decode_rgb(data, static_cast<std::size_t>(image_size),
                             width, height, rgba.data());
        break;
    case AtcKind::explicit_alpha:
        ok = atc::decode_rgba_explicit(
            data, static_cast<std::size_t>(image_size),
            width, height, rgba.data());
        break;
    case AtcKind::interpolated_alpha:
        ok = atc::decode_rgba_interpolated(
            data, static_cast<std::size_t>(image_size),
            width, height, rgba.data());
        break;
    default:
        break;
    }
    if (!ok)
        rgba.clear();
    return ok;
}

extern "C" void open_citadel_glCompressedTexImage2D(
    GLenum target, GLint level, GLenum internalformat, GLsizei width,
    GLsizei height, GLint border, GLsizei image_size, const void *data)
{
    ++g_textures;
    const AtcKind kind = atc_kind(internalformat);
    if (kind != AtcKind::none) {
        std::vector<std::uint8_t> rgba;
        if (decode_atc(kind, width, height, image_size, data, rgba)) {
            glad_glTexImage2D(target, level, GL_RGBA, width, height, border,
                              GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            const long count = ++g_atc_decoded;
            if (count <= 16 || (count % 128) == 0)
                trace("OpenCitadel ATC: decoded upload #%ld level=%d "
                      "format=0x%04x %dx%d (%d -> %zu bytes)",
                      count, level, internalformat, width, height, image_size,
                      rgba.size());
            return;
        }
        trace("OpenCitadel ATC: decode failed; passing format 0x%04x "
              "%dx%d bytes=%d to driver",
              internalformat, width, height, image_size);
    }

    ++g_compressed_passthrough;
    glad_glCompressedTexImage2D(target, level, internalformat, width, height,
                                border, image_size, data);
}

extern "C" void open_citadel_glCompressedTexSubImage2D(
    GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
    GLsizei height, GLenum format, GLsizei image_size, const void *data)
{
    /* The AMD ATC extension explicitly disallows compressed sub-image updates.
     * Forward unchanged so the driver's GL error remains observable if UE3
     * unexpectedly attempts one. */
    glad_glCompressedTexSubImage2D(target, level, xoffset, yoffset, width,
                                   height, format, image_size, data);
}

extern "C" long open_citadel_gl_draws(void) { return g_draws.load(); }
extern "C" long open_citadel_gl_textures(void) { return g_textures.load(); }
extern "C" long open_citadel_gl_atc_decoded(void) { return g_atc_decoded.load(); }
extern "C" int open_citadel_gl_shaders_ok(void) { return g_shaders_ok.load(); }
extern "C" int open_citadel_gl_shaders_failed(void) { return g_shaders_failed.load(); }
extern "C" int open_citadel_gl_programs_ok(void) { return g_programs_ok.load(); }
extern "C" int open_citadel_gl_programs_failed(void) { return g_programs_failed.load(); }

DynLibFunction symtable_open_citadel_gles2_probe[] = {
    THUNK_SPECIFIC("glCompileShader", open_citadel_glCompileShader),
    THUNK_SPECIFIC("glLinkProgram", open_citadel_glLinkProgram),
    THUNK_SPECIFIC("glDrawArrays", open_citadel_glDrawArrays),
    THUNK_SPECIFIC("glDrawElements", open_citadel_glDrawElements),
    THUNK_SPECIFIC("glTexImage2D", open_citadel_glTexImage2D),
    THUNK_SPECIFIC("glCompressedTexImage2D",
                   open_citadel_glCompressedTexImage2D),
    THUNK_SPECIFIC("glCompressedTexSubImage2D",
                   open_citadel_glCompressedTexSubImage2D),
    { nullptr, 0 },
};
