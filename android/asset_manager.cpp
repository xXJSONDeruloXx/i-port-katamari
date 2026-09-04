#include "asset_manager.h"

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <string>

#include "jni_internals.h"
#include "so_util.h"
#include "thunk_gen.h"
#include "trace.h"

namespace fs = std::filesystem;

extern "C" {

struct AAssetManager {
    int marker;
};

struct AAsset {
    int fd;
    off_t length;
};

}

namespace {

class AndroidAssetManagerObject : public Object {
public:
    static Class clazz;
    Class *_getClass() override { return &clazz; }
};

Class AndroidAssetManagerObject::clazz = {
    .classpath = "android/content/res/AssetManager",
    .classname = "AssetManager",
    .managed_methods = nullptr,
    .native_methods = nullptr,
    .fields = nullptr,
    .instance_size = sizeof(AndroidAssetManagerObject),
};

static const int registered_asset_manager =
    ClassRegistry::register_class(AndroidAssetManagerObject::clazz);
static AndroidAssetManagerObject g_java_asset_manager;
static AAssetManager g_native_asset_manager{0x41535354};
static fs::path g_assets_dir;

static bool safe_asset_name(const char *name, fs::path *relative)
{
    if (!name || !*name)
        return false;
    fs::path p(name);
    if (p.is_absolute())
        return false;

    fs::path clean;
    for (const auto &part : p) {
        const std::string s = part.string();
        if (s.empty() || s == "." || s == "..")
            return false;
        clean /= part;
    }
    *relative = clean;
    return true;
}

} // namespace

extern "C" void open_citadel_asset_manager_configure(const char *game_dir)
{
    g_assets_dir = fs::path(game_dir ? game_dir : "") / "assets";
}

extern "C" jobject open_citadel_asset_manager_java_object(void)
{
    return reinterpret_cast<jobject>(&g_java_asset_manager);
}

extern "C" AAssetManager *AAssetManager_fromJava(JNIEnv *, jobject manager)
{
    if (!manager)
        return nullptr;
    return &g_native_asset_manager;
}

extern "C" AAsset *AAssetManager_open(AAssetManager *manager, const char *name,
                                      int mode)
{
    (void)mode;
    if (manager != &g_native_asset_manager)
        return nullptr;

    fs::path relative;
    if (!safe_asset_name(name, &relative))
        return nullptr;

    fs::path path = g_assets_dir / relative;
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        trace("AAssetManager_open miss: %s", path.c_str());
        return nullptr;
    }

    struct stat st {};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return nullptr;
    }

    AAsset *asset = new AAsset{fd, st.st_size};
    trace("AAssetManager_open: %s (%lld bytes)", path.c_str(),
          (long long)asset->length);
    return asset;
}

extern "C" int AAsset_openFileDescriptor(AAsset *asset, off_t *out_start,
                                         off_t *out_length)
{
    if (!asset || asset->fd < 0)
        return -1;
    int fd = dup(asset->fd);
    if (fd < 0)
        return -1;
    if (out_start)
        *out_start = 0;
    if (out_length)
        *out_length = asset->length;
    return fd;
}

extern "C" void AAsset_close(AAsset *asset)
{
    if (!asset)
        return;
    if (asset->fd >= 0)
        close(asset->fd);
    delete asset;
}

DynLibFunction symtable_android[] = {
    THUNK_DIRECT(AAssetManager_fromJava),
    THUNK_DIRECT(AAssetManager_open),
    THUNK_DIRECT(AAsset_openFileDescriptor),
    THUNK_DIRECT(AAsset_close),
    {nullptr, 0},
};
