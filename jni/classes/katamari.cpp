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
    bool playing = false;
    bool loop = false;
    int next = -1;
    float volume = 1.0f;
    char name[128] = {};
};

static AudioSlot g_audio[128];
static int g_next_audio_id = 0;
static float g_sfx_volume = 1.0f;

static AudioSlot *audio_slot(jint id)
{
    if (id < 0 || id >= (jint)(sizeof(g_audio) / sizeof(g_audio[0])))
        return NULL;
    return &g_audio[id];
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
    AudioSlot *slot = audio_slot(id);
    if (slot)
        slot->allocated = true;
}

void AudioTool::deallocAll(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    memset(g_audio, 0, sizeof(g_audio));
    g_next_audio_id = 0;
}

void AudioTool::dispose(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    AudioSlot *slot = audio_slot(id);
    if (slot)
        *slot = AudioSlot{};
}

jint AudioTool::isLoop(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    AudioSlot *slot = audio_slot(id);
    return slot && slot->allocated && slot->loop ? 1 : 0;
}

jint AudioTool::isPlaying(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    AudioSlot *slot = audio_slot(id);
    return slot && slot->playing ? 1 : 0;
}

jint AudioTool::newID(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    for (size_t i = 0; i < sizeof(g_audio) / sizeof(g_audio[0]); i++) {
        int id = (g_next_audio_id + (int)i) % (int)(sizeof(g_audio) / sizeof(g_audio[0]));
        if (!g_audio[id].allocated) {
            g_audio[id].allocated = true;
            g_next_audio_id = (id + 1) % (int)(sizeof(g_audio) / sizeof(g_audio[0]));
            return id;
        }
    }
    return -1;
}

void AudioTool::pause(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    AudioSlot *slot = audio_slot(id);
    if (slot)
        slot->playing = false;
}

void AudioTool::play(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    AudioSlot *slot = audio_slot(id);
    if (slot && slot->allocated)
        slot->playing = true;
}

void AudioTool::playSound(JNIEnv *env, jclass clazz, jint id, jint loops)
{
    (void)env;
    (void)clazz;
    AudioSlot *slot = audio_slot(id);
    if (slot && slot->allocated) {
        slot->playing = true;
        slot->loop = loops != 0;
    }
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
    (void)id;
}

void AudioTool::setLoop(JNIEnv *env, jclass clazz, jint id, jint loop)
{
    (void)env;
    (void)clazz;
    AudioSlot *slot = audio_slot(id);
    if (slot)
        slot->loop = loop != 0;
}

void AudioTool::setNextPlayer(JNIEnv *env, jclass clazz, jint id, jint next)
{
    (void)env;
    (void)clazz;
    AudioSlot *slot = audio_slot(id);
    if (slot)
        slot->next = next;
}

void AudioTool::setSfxVolume(JNIEnv *env, jclass clazz, jfloat volume)
{
    (void)env;
    (void)clazz;
    g_sfx_volume = volume;
}

void AudioTool::setVolume(JNIEnv *env, jclass clazz, jint id, jfloat volume)
{
    (void)env;
    (void)clazz;
    AudioSlot *slot = audio_slot(id);
    if (slot)
        slot->volume = volume * g_sfx_volume;
}

void AudioTool::setup(JNIEnv *env, jclass clazz, jint id, jstring name,
                      jint loop)
{
    (void)env;
    (void)clazz;
    AudioSlot *slot = audio_slot(id);
    if (!slot)
        return;
    slot->allocated = true;
    slot->loop = loop != 0;
    snprintf(slot->name, sizeof(slot->name), "%s",
             string_value(name) ? string_value(name) : "");
}

void AudioTool::stop(JNIEnv *env, jclass clazz, jint id)
{
    (void)env;
    (void)clazz;
    AudioSlot *slot = audio_slot(id);
    if (slot)
        slot->playing = false;
}

void AudioTool::stopAll(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    for (AudioSlot &slot : g_audio)
        slot.playing = false;
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

