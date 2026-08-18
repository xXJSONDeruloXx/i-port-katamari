#include "katamari.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <stdint.h>
#include <vector>

#include <SDL2/SDL.h>
#include <mpg123.h>
#include <vorbis/vorbisfile.h>

#include "fix_path.h"
#include "trace.h"

namespace {

static const char *string_value(jstring value)
{
    return value ? ((String *)value)->str : NULL;
}

static bool safe_relative(const char *path)
{
    if (!path || !*path || path[0] == '/')
        return false;

    const char *cursor = path;
    while (*cursor) {
        const char *end = strchr(cursor, '/');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (length == 2 && cursor[0] == '.' && cursor[1] == '.')
            return false;
        if (!end)
            break;
        cursor = end + 1;
    }
    return true;
}

static void trim_asset_name(const char *input, char *output, size_t size)
{
    if (!input) {
        output[0] = '\0';
        return;
    }

    const char *name = input;
    const char *scheme = strstr(name, "appbundle:/");
    if (scheme)
        name = scheme + strlen("appbundle:/");

    while (*name == '/')
        name++;
    if (strncmp(name, "assets/", 7) == 0)
        name += 7;

    snprintf(output, size, "%s", name);
}

static bool regular_file(const char *path, off_t *length)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return false;
    if (length)
        *length = st.st_size;
    return true;
}

/*
 * AssetManager names in this APK are mostly assets-relative, while Android's
 * res/raw sounds are addressed by their bare resource filename.  Keep both
 * layouts in the extracted donor so the first successful candidate is the
 * same file Android would have served.
 */
static bool resolve_asset(const char *input, char *path, size_t path_size)
{
    char name[PATH_MAX];
    trim_asset_name(input, name, sizeof(name));
    if (!safe_relative(name))
        return false;

    const char *root = io_game_dir();
    off_t ignored = 0;

    snprintf(path, path_size, "%s/assets/%s", root, name);
    if (regular_file(path, &ignored))
        return true;

    snprintf(path, path_size, "%s/%s", root, name);
    if (regular_file(path, &ignored))
        return true;

    snprintf(path, path_size, "%s/res/raw/%s", root, name);
    if (regular_file(path, &ignored))
        return true;

    /* A few Android callers pass a resource path with its directory. */
    if (strncmp(name, "res/raw/", 8) == 0) {
        snprintf(path, path_size, "%s/%s", root, name);
        if (regular_file(path, &ignored))
            return true;
    }

    return false;
}

static bool resolve_user_file(const char *input, char *path, size_t path_size)
{
    char name[PATH_MAX];
    trim_asset_name(input, name, sizeof(name));
    if (!safe_relative(name))
        return false;

    snprintf(path, path_size, "%s/var/%s", io_game_dir(), name);
    return true;
}

static void make_parent_dirs(const char *path)
{
    char work[PATH_MAX];
    snprintf(work, sizeof(work), "%s", path);

    char *slash = strrchr(work, '/');
    if (!slash)
        return;
    *slash = '\0';

    for (char *cursor = work + 1; *cursor; cursor++) {
        if (*cursor != '/')
            continue;
        *cursor = '\0';
        (void)mkdir(work, 0700);
        *cursor = '/';
    }
    (void)mkdir(work, 0700);
}

static jint array_capacity(jbyteArray buffer)
{
    ArrayObject *array = (ArrayObject *)buffer;
    if (!array || array->element_size != sizeof(jbyte) || array->count < 0)
        return 0;
    return array->count;
}

static jint read_file(const char *path, jbyteArray buffer, jint requested)
{
    ArrayObject *array = (ArrayObject *)buffer;
    jint capacity = array_capacity(buffer);
    if (!array || !array->elements || requested <= 0 || capacity <= 0)
        return 0;

    jint total = std::min(requested, capacity);
    int fd = ::open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    jint done = 0;
    while (done < total) {
        ssize_t count = ::read(fd, (char *)array->elements + done,
                               (size_t)(total - done));
        if (count <= 0)
            break;
        done += (jint)count;
    }
    ::close(fd);
    return done;
}

static jint write_file(const char *path, jbyteArray buffer, jint requested)
{
    ArrayObject *array = (ArrayObject *)buffer;
    jint capacity = array_capacity(buffer);
    if (!array || !array->elements || requested <= 0 || capacity <= 0)
        return 0;

    jint total = std::min(requested, capacity);
    make_parent_dirs(path);

    int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return 0;

    jint done = 0;
    while (done < total) {
        ssize_t count = ::write(fd, (const char *)array->elements + done,
                                (size_t)(total - done));
        if (count <= 0)
            break;
        done += (jint)count;
    }
    ::close(fd);
    return done;
}

static void trace_asset(const char *operation, const char *name,
                        const char *path, jint result)
{
    static int shown = 0;
    if (shown >= 32)
        return;
    shown++;
    trace("katamari %s '%s' -> '%s' (%d)", operation,
          name ? name : "(null)", path ? path : "(missing)", result);
}

struct AudioSlot {
    bool allocated = false;
    bool decoded = false;
    bool playing = false;
    bool loop = false;
    int next = -1;
    float volume = 1.0f;
    size_t position = 0;
    char name[128] = {};
    std::vector<int16_t> pcm;
};

static constexpr int kAudioSlotCount = 128;
static constexpr int kRawSoundCount = 52;
static constexpr int kDynamicAudioBase = 64;

/* AudioTool.m_sndIDList in the donor APK, in its original index order.
 * This is intentionally not the resource declaration order. */
static const char *const kRawSoundNames[] = {
    "kd_se_common_00.ogg",
    "kd_se_common_01.ogg",
    "kd_se_common_02.ogg",
    "kd_se_game_21.ogg",
    "kd_se_oujih_00.ogg",
    "kd_se_oujih_01.ogg",
    "kd_se_game_00.ogg",
    "kd_se_game_01.ogg",
    "kd_se_game_02.ogg",
    "kd_se_game_03.ogg",
    "kd_se_game_04.ogg",
    "kd_se_game_05.ogg",
    "kd_se_game_06.ogg",
    "kd_se_game_07.ogg",
    "kd_se_game_08.ogg",
    "kd_se_game_09.ogg",
    "kd_se_game_10.ogg",
    "kd_se_game_11.ogg",
    "kd_se_game_12.ogg",
    "kd_se_game_13.ogg",
    "kd_se_game_14.ogg",
    "kd_se_game_15.ogg",
    "kd_se_game_16.ogg",
    "kd_se_game_17.ogg",
    "kd_se_game_18.ogg",
    "kd_se_game_19.ogg",
    "kd_se_game_20.ogg",
    "kd_se_game_27.ogg",
    "kd_se_game_28.ogg",
    "kd_se_game_29.ogg",
    "kd_se_game_30.ogg",
    "kd_se_res_00.ogg",
    "kd_se_res_01.ogg",
    "kd_se_over_00.ogg",
    "kd_se_over_00_2.ogg",
    "kd_se_over_01.ogg",
    "kd_se_mono_00.ogg",
    "kd_se_mono_10.ogg",
    "kd_se_mono_11.ogg",
    "kd_se_mono_12.ogg",
    "kd_se_mono_13.ogg",
    "kd_se_mono_14.ogg",
    "kd_se_mono_15.ogg",
    "kd_se_mono_16.ogg",
    "kd_se_mono_17.ogg",
    "kd_se_mono_18.ogg",
    "kd_se_mono_19.ogg",
    "kd_se_mono_20.ogg",
    "kd_se_mono_21.ogg",
    "kd_se_mono_22.ogg",
    "kd_se_mono_23.ogg",
    "kd_se_mono_24.ogg",
};

static_assert((int)(sizeof(kRawSoundNames) / sizeof(kRawSoundNames[0])) ==
              kRawSoundCount);

static AudioSlot g_audio[kAudioSlotCount];
static int g_next_audio_id = kDynamicAudioBase;
static float g_sfx_volume = 1.0f;
static long g_asset_reads = 0;
static unsigned int g_audio_events = 0;
static SDL_AudioDeviceID g_audio_device = 0;
static SDL_AudioSpec g_audio_spec;
static SDL_mutex *g_audio_mutex = NULL;
static bool g_audio_initialized = false;
static bool g_mpg_initialized = false;

static void trace_audio(const char *operation, jint id, const char *name = NULL)
{
    if (g_audio_events >= 64)
        return;
    g_audio_events++;
    trace("katamari audio %s id=%d%s%s%s", operation, id,
          name ? " name='" : "", name ? name : "",
          name ? "'" : "");
}

static AudioSlot *audio_slot(jint id)
{
    if (id < 0 || id >= kAudioSlotCount)
        return NULL;
    return &g_audio[id];
}

static const char *raw_sound_name(jint id)
{
    if (id < 0 || id >= kRawSoundCount)
        return NULL;
    return kRawSoundNames[id];
}

static void audio_lock(void)
{
    if (g_audio_mutex)
        SDL_LockMutex(g_audio_mutex);
}

static void audio_unlock(void)
{
    if (g_audio_mutex)
        SDL_UnlockMutex(g_audio_mutex);
}

static int16_t clamp_sample(int value)
{
    if (value > 32767)
        return 32767;
    if (value < -32768)
        return -32768;
    return (int16_t)value;
}

static void audio_callback(void *userdata, Uint8 *stream, int length)
{
    (void)userdata;
    memset(stream, 0, (size_t)length);
    if (!g_audio_mutex || g_audio_spec.channels != 2 ||
        g_audio_spec.format != AUDIO_S16SYS)
        return;

    int16_t *output = (int16_t *)stream;
    const size_t frames = (size_t)length / (sizeof(int16_t) * 2);
    audio_lock();
    for (AudioSlot &slot : g_audio) {
        if (!slot.playing || slot.pcm.empty())
            continue;

        for (size_t frame = 0; frame < frames; frame++) {
            if (slot.position + 1 >= slot.pcm.size()) {
                if (slot.loop) {
                    slot.position = 0;
                } else {
                    slot.playing = false;
                    AudioSlot *next = audio_slot(slot.next);
                    if (next && next->allocated && !next->pcm.empty()) {
                        next->position = 0;
                        next->playing = true;
                    }
                    break;
                }
            }

            const float volume = slot.volume;
            const int left = (int)output[frame * 2] +
                             (int)((float)slot.pcm[slot.position] * volume);
            const int right = (int)output[frame * 2 + 1] +
                              (int)((float)slot.pcm[slot.position + 1] * volume);
            output[frame * 2] = clamp_sample(left);
            output[frame * 2 + 1] = clamp_sample(right);
            slot.position += 2;
        }
    }
    audio_unlock();
}

static void ensure_audio(void)
{
    if (g_audio_initialized)
        return;
    g_audio_initialized = true;

    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) &&
        SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        trace("katamari audio SDL init failed: %s", SDL_GetError());
    }

    g_audio_mutex = SDL_CreateMutex();
    if (!g_audio_mutex) {
        trace("katamari audio mutex failed: %s", SDL_GetError());
    } else {
        SDL_AudioSpec desired;
        SDL_zero(desired);
        desired.freq = 44100;
        desired.format = AUDIO_S16SYS;
        desired.channels = 2;
        desired.samples = 1024;
        desired.callback = audio_callback;
        g_audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &g_audio_spec, 0);
        if (!g_audio_device) {
            trace("katamari audio output unavailable: %s", SDL_GetError());
        } else if (g_audio_spec.format != AUDIO_S16SYS ||
                   g_audio_spec.channels != 2) {
            trace("katamari audio output format unsupported: %d Hz/0x%x/%d",
                  g_audio_spec.freq, g_audio_spec.format,
                  g_audio_spec.channels);
            SDL_CloseAudioDevice(g_audio_device);
            g_audio_device = 0;
        } else {
            trace("katamari audio output: %d Hz/%d channels", g_audio_spec.freq,
                  g_audio_spec.channels);
            SDL_PauseAudioDevice(g_audio_device, 0);
        }
    }

    if (!g_mpg_initialized) {
        int error = mpg123_init();
        if (error == MPG123_OK)
            g_mpg_initialized = true;
        else
            trace("katamari mpg123 init failed: %s", mpg123_plain_strerror(error));
    }
}

static bool convert_audio(const SDL_AudioSpec &source, const Uint8 *data,
                          Uint32 length, std::vector<int16_t> *output)
{
    if (!data || !length || !source.freq || !source.channels)
        return false;

    SDL_AudioStream *stream = SDL_NewAudioStream(
        source.format, source.channels, source.freq,
        AUDIO_S16SYS, 2, 44100);
    if (!stream)
        return false;
    bool okay = SDL_AudioStreamPut(stream, data, (int)length) == 0 &&
                SDL_AudioStreamFlush(stream) == 0;
    if (okay) {
        int available = SDL_AudioStreamAvailable(stream);
        if (available <= 0 || (available & (int)(sizeof(int16_t) - 1))) {
            okay = false;
        } else {
            output->resize((size_t)available / sizeof(int16_t));
            int received = SDL_AudioStreamGet(
                stream, output->data(), available);
            if (received != available)
                okay = false;
        }
    }
    SDL_FreeAudioStream(stream);
    if (!okay)
        output->clear();
    return okay;
}

static bool load_wav(const char *path, std::vector<int16_t> *output)
{
    SDL_AudioSpec source;
    Uint8 *data = NULL;
    Uint32 length = 0;
    if (!SDL_LoadWAV(path, &source, &data, &length))
        return false;
    bool okay = convert_audio(source, data, length, output);
    SDL_FreeWAV(data);
    return okay;
}

static bool load_mp3(const char *path, std::vector<int16_t> *output)
{
    if (!g_mpg_initialized)
        return false;

    int error = MPG123_OK;
    mpg123_handle *decoder = mpg123_new(NULL, &error);
    if (!decoder)
        return false;
    bool okay = false;
    long rate = 0;
    int channels = 0;
    int encoding = 0;
    if (mpg123_open(decoder, path) == MPG123_OK &&
        mpg123_getformat(decoder, &rate, &channels, &encoding) == MPG123_OK) {
        if (encoding != MPG123_ENC_SIGNED_16) {
            mpg123_format_none(decoder);
            mpg123_format(decoder, rate, channels, MPG123_ENC_SIGNED_16);
            encoding = MPG123_ENC_SIGNED_16;
        }

        std::vector<Uint8> compressed_pcm;
        unsigned char block[16384];
        for (;;) {
            size_t received = 0;
            int result = mpg123_read(decoder, block, sizeof(block), &received);
            if (received)
                compressed_pcm.insert(compressed_pcm.end(), block,
                                       block + received);
            if (result == MPG123_OK || result == MPG123_NEW_FORMAT)
                continue;
            if (result == MPG123_DONE)
                break;
            compressed_pcm.clear();
            break;
        }

        SDL_AudioSpec source;
        SDL_zero(source);
        source.freq = (int)rate;
        source.channels = (Uint8)channels;
        source.format = AUDIO_S16LSB;
        okay = convert_audio(source, compressed_pcm.data(),
                             (Uint32)compressed_pcm.size(), output);
    }
    if (!okay)
        trace("katamari mp3 decode failed for '%s': %s", path,
              mpg123_strerror(decoder));
    mpg123_close(decoder);
    mpg123_delete(decoder);
    return okay;
}

static bool load_ogg(const char *path, std::vector<int16_t> *output)
{
    OggVorbis_File decoder;
    memset(&decoder, 0, sizeof(decoder));
    int error = ov_fopen(path, &decoder);
    if (error != 0) {
        trace("katamari ogg open failed for '%s' (vorbis %d)", path, error);
        return false;
    }

    bool okay = false;
    int decode_error = 0;
    vorbis_info *info = ov_info(&decoder, -1);
    if (info && info->rate > 0 && info->channels > 0 && info->channels <= 255) {
        std::vector<Uint8> decoded;
        char block[16384];
        int bitstream = 0;
        for (;;) {
            long received = ov_read(&decoder, block, sizeof(block), 0, 2, 1,
                                    &bitstream);
            if (received == 0)
                break;
            if (received < 0) {
                decode_error = (int)received;
                break;
            }
            decoded.insert(decoded.end(), block, block + received);
        }

        if (decode_error == 0 && !decoded.empty() &&
            decoded.size() <= (size_t)UINT32_MAX) {
            SDL_AudioSpec source;
            SDL_zero(source);
            source.freq = info->rate;
            source.channels = (Uint8)info->channels;
            source.format = AUDIO_S16LSB;
            okay = convert_audio(source, decoded.data(),
                                 (Uint32)decoded.size(), output);
        }
    }

    ov_clear(&decoder);
    if (!okay)
        trace("katamari ogg decode failed for '%s' (vorbis %d)", path,
              decode_error);
    return okay;
}

static bool load_sound(const char *name, std::vector<int16_t> *output)
{
    char path[PATH_MAX];
    if (!resolve_asset(name, path, sizeof(path))) {
        trace("katamari audio asset missing: '%s'", name ? name : "(null)");
        return false;
    }
    ensure_audio();

    const char *extension = strrchr(path, '.');
    bool okay = extension && strcasecmp(extension, ".wav") == 0
                    ? load_wav(path, output)
                    : extension && strcasecmp(extension, ".mp3") == 0
                    ? load_mp3(path, output)
                    : extension && strcasecmp(extension, ".ogg") == 0
                    ? load_ogg(path, output)
                    : false;
    if (okay) {
        trace("katamari audio loaded '%s' (%zu frames)", name,
              output->size() / 2);
    } else {
        trace("katamari audio format unsupported or unreadable: '%s'", name);
    }
    return okay;
}

}

jint Katamari::getAssetFileLength(JNIEnv *env, jclass clazz, jstring name)
{
    (void)env;
    (void)clazz;

    char path[PATH_MAX];
    off_t length = 0;
    if (!resolve_asset(string_value(name), path, sizeof(path)))
        return -1;
    if (!regular_file(path, &length) || length > INT_MAX)
        return -1;
    return (jint)length;
}

jint Katamari::loadAssetFile(JNIEnv *env, jclass clazz, jstring name,
                             jbyteArray buffer, jint length)
{
    (void)env;
    (void)clazz;

    char path[PATH_MAX];
    const char *asset = string_value(name);
    if (!resolve_asset(asset, path, sizeof(path))) {
        trace_asset("asset-miss", asset, NULL, 0);
        return 0;
    }

    jint result = read_file(path, buffer, length);
    if (result > 0)
        g_asset_reads++;
    trace_asset("asset-read", asset, path, result);
    return result;
}

jint Katamari::saveUserFile(JNIEnv *env, jclass clazz, jstring name,
                            jbyteArray buffer, jint length)
{
    (void)env;
    (void)clazz;

    char path[PATH_MAX];
    const char *file = string_value(name);
    if (!resolve_user_file(file, path, sizeof(path)))
        return 0;

    jint result = write_file(path, buffer, length);
    trace_asset("user-write", file, path, result);
    return result;
}

jint Katamari::loadUserFile(JNIEnv *env, jclass clazz, jstring name,
                            jbyteArray buffer, jint length)
{
    (void)env;
    (void)clazz;

    char path[PATH_MAX];
    const char *file = string_value(name);
    if (!resolve_user_file(file, path, sizeof(path)))
        return 0;

    jint result = read_file(path, buffer, length);
    trace_asset("user-read", file, path, result);
    return result;
}

void Katamari::openWebNamcoKT(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    trace("katamari openWebNamcoKT ignored on Linux");
}

void AudioTool::addSound(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    const char *name = raw_sound_name(id);

    audio_lock();
    AudioSlot *slot = audio_slot(id);
    if (!slot) {
        audio_unlock();
        return;
    }
    if (!name || slot->decoded) {
        slot->allocated = true;
        trace_audio("addSound", id, name);
        audio_unlock();
        return;
    }
    slot->allocated = true;
    audio_unlock();

    std::vector<int16_t> pcm;
    bool okay = load_sound(name, &pcm);

    audio_lock();
    slot = audio_slot(id);
    if (slot) {
        slot->allocated = true;
        slot->decoded = okay && !pcm.empty();
        slot->pcm = std::move(pcm);
        snprintf(slot->name, sizeof(slot->name), "%s", name);
        trace_audio("addSound", id, slot->name);
    }
    audio_unlock();
}

void AudioTool::deallocAll(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    audio_lock();
    for (AudioSlot &slot : g_audio)
        slot = AudioSlot{};
    g_next_audio_id = kDynamicAudioBase;
    audio_unlock();
}

void AudioTool::dispose(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    audio_lock();
    AudioSlot *slot = audio_slot(id);
    if (slot)
        *slot = AudioSlot{};
    audio_unlock();
}

jint AudioTool::isLoop(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    audio_lock();
    AudioSlot *slot = audio_slot(id);
    jint result = slot && slot->allocated && slot->loop ? 1 : 0;
    audio_unlock();
    return result;
}

jint AudioTool::isPlaying(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    audio_lock();
    AudioSlot *slot = audio_slot(id);
    jint result = slot && slot->playing ? 1 : 0;
    audio_unlock();
    return result;
}

jint AudioTool::newID(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    audio_lock();
    const int dynamic_count = kAudioSlotCount - kDynamicAudioBase;
    for (int i = 0; i < dynamic_count; i++) {
        int id = kDynamicAudioBase +
                 ((g_next_audio_id - kDynamicAudioBase + i) % dynamic_count);
        if (!g_audio[id].allocated) {
            g_audio[id].allocated = true;
            g_next_audio_id = id + 1 < kAudioSlotCount
                                  ? id + 1 : kDynamicAudioBase;
            trace_audio("newID", id);
            audio_unlock();
            return id;
        }
    }
    audio_unlock();
    return -1;
}

void AudioTool::pause(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    audio_lock();
    AudioSlot *slot = audio_slot(id);
    if (slot)
        slot->playing = false;
    audio_unlock();
}

void AudioTool::play(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    audio_lock();
    AudioSlot *slot = audio_slot(id);
    if (slot && slot->allocated) {
        slot->playing = true;
        if (slot->position >= slot->pcm.size())
            slot->position = 0;
        trace_audio("play", id, slot->name);
    }
    audio_unlock();
}

void AudioTool::playSound(JNIEnv *env, jclass clazz, jint id, jint loops)
{
    (void)env;
    (void)clazz;
    audio_lock();
    AudioSlot *slot = audio_slot(id);
    if (slot && slot->allocated) {
        slot->playing = true;
        slot->loop = loops != 0;
        slot->position = 0;
        trace_audio("playSound", id, slot->name);
    }
    audio_unlock();
}

void AudioTool::releaseSound(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
}

void AudioTool::rewind(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    audio_lock();
    AudioSlot *slot = audio_slot(id);
    if (slot)
        slot->position = 0;
    audio_unlock();
}

void AudioTool::setLoop(JNIEnv *env, jclass clazz, jint id, jint loop)
{
    (void)env;
    (void)clazz;
    audio_lock();
    AudioSlot *slot = audio_slot(id);
    if (slot)
        slot->loop = loop != 0;
    audio_unlock();
}

void AudioTool::setNextPlayer(JNIEnv *env, jclass clazz, jint id, jint next)
{
    (void)env;
    (void)clazz;
    audio_lock();
    AudioSlot *slot = audio_slot(id);
    if (slot)
        slot->next = next;
    audio_unlock();
}

void AudioTool::setSfxVolume(JNIEnv *env, jclass clazz, jfloat volume)
{
    (void)env;
    (void)clazz;
    audio_lock();
    g_sfx_volume = volume;
    audio_unlock();
}

void AudioTool::setVolume(JNIEnv *env, jclass clazz, jint id, jfloat volume)
{
    (void)env;
    (void)clazz;
    audio_lock();
    AudioSlot *slot = audio_slot(id);
    if (slot)
        slot->volume = volume * g_sfx_volume;
    audio_unlock();
}

void AudioTool::setup(JNIEnv *env, jclass clazz, jint id, jstring name,
                      jint loop)
{
    (void)env;
    (void)clazz;
    AudioSlot *slot = audio_slot(id);
    if (!slot)
        return;

    const char *value = string_value(name);
    std::vector<int16_t> pcm;
    load_sound(value, &pcm);

    audio_lock();
    slot = audio_slot(id);
    if (slot) {
        slot->allocated = true;
        slot->playing = false;
        slot->loop = loop != 0;
        slot->position = 0;
        slot->pcm = std::move(pcm);
        slot->decoded = !slot->pcm.empty();
        snprintf(slot->name, sizeof(slot->name), "%s", value ? value : "");
    }
    trace_audio("setup", id, slot->name);
    audio_unlock();
}

void AudioTool::stop(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    audio_lock();
    AudioSlot *slot = audio_slot(id);
    if (slot)
        slot->playing = false;
    audio_unlock();
}

void AudioTool::stopAll(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    audio_lock();
    for (AudioSlot &slot : g_audio)
        slot.playing = false;
    audio_unlock();
}

static const ManagedMethod KatamariMethods[] = {
    ManagedMethod::RegisterStatic<&Katamari::getAssetFileLength>(
        Katamari::clazz, "getAssetFileLength", "(Ljava/lang/String;)I"),
    ManagedMethod::RegisterStatic<&Katamari::loadAssetFile>(
        Katamari::clazz, "loadAssetFile", "(Ljava/lang/String;[BI)I"),
    ManagedMethod::RegisterStatic<&Katamari::loadUserFile>(
        Katamari::clazz, "loadUserFile", "(Ljava/lang/String;[BI)I"),
    ManagedMethod::RegisterStatic<&Katamari::saveUserFile>(
        Katamari::clazz, "saveUserFile", "(Ljava/lang/String;[BI)I"),
    ManagedMethod::RegisterStatic<&Katamari::openWebNamcoKT>(
        Katamari::clazz, "openWebNamcoKT", "()V"),
    {NULL},
};

Class Katamari::clazz = {
    .classpath = "com/namcobandaigames/katamari/Katamari",
    .classname = "Katamari",
    .managed_methods = KatamariMethods,
    .native_methods = {NULL},
    .fields = {NULL},
    .instance_size = 0,
};

static const ManagedMethod AudioToolMethods[] = {
    ManagedMethod::RegisterStatic<&AudioTool::addSound>(
        AudioTool::clazz, "addSound", "(I)V"),
    ManagedMethod::RegisterStatic<&AudioTool::deallocAll>(
        AudioTool::clazz, "deallocAll", "()V"),
    ManagedMethod::RegisterStatic<&AudioTool::dispose>(
        AudioTool::clazz, "dispose", "(I)V"),
    ManagedMethod::RegisterStatic<&AudioTool::isLoop>(
        AudioTool::clazz, "isLoop", "(I)I"),
    ManagedMethod::RegisterStatic<&AudioTool::isPlaying>(
        AudioTool::clazz, "isPlaying", "(I)I"),
    ManagedMethod::RegisterStatic<&AudioTool::newID>(
        AudioTool::clazz, "newID", "()I"),
    ManagedMethod::RegisterStatic<&AudioTool::pause>(
        AudioTool::clazz, "pause", "(I)V"),
    ManagedMethod::RegisterStatic<&AudioTool::play>(
        AudioTool::clazz, "play", "(I)V"),
    ManagedMethod::RegisterStatic<&AudioTool::playSound>(
        AudioTool::clazz, "playSound", "(II)V"),
    ManagedMethod::RegisterStatic<&AudioTool::releaseSound>(
        AudioTool::clazz, "releaseSound", "()V"),
    ManagedMethod::RegisterStatic<&AudioTool::rewind>(
        AudioTool::clazz, "rewind", "(I)V"),
    ManagedMethod::RegisterStatic<&AudioTool::setLoop>(
        AudioTool::clazz, "setLoop", "(II)V"),
    ManagedMethod::RegisterStatic<&AudioTool::setNextPlayer>(
        AudioTool::clazz, "setNextPlayer", "(II)V"),
    ManagedMethod::RegisterStatic<&AudioTool::setSfxVolume>(
        AudioTool::clazz, "setSfxVolume", "(F)V"),
    ManagedMethod::RegisterStatic<&AudioTool::setVolume>(
        AudioTool::clazz, "setVolume", "(IF)V"),
    ManagedMethod::RegisterStatic<&AudioTool::setup>(
        AudioTool::clazz, "setup", "(ILjava/lang/String;I)V"),
    ManagedMethod::RegisterStatic<&AudioTool::stop>(
        AudioTool::clazz, "stop", "(I)V"),
    ManagedMethod::RegisterStatic<&AudioTool::stopAll>(
        AudioTool::clazz, "stopAll", "()V"),
    {NULL},
};

Class AudioTool::clazz = {
    .classpath = "com/namcobandaigames/katamari/AudioTool",
    .classname = "AudioTool",
    .managed_methods = AudioToolMethods,
    .native_methods = {NULL},
    .fields = {NULL},
    .instance_size = 0,
};

static const int registered_katamari = ClassRegistry::register_class(Katamari::clazz);
static const int registered_audio = ClassRegistry::register_class(AudioTool::clazz);

extern "C" long katamari_asset_files_loaded(void)
{
    return g_asset_reads;
}
