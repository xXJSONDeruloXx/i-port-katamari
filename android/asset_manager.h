#pragma once

#include "jni.h"

#ifdef __cplusplus
extern "C" {
#endif

void open_citadel_asset_manager_configure(const char *game_dir);
jobject open_citadel_asset_manager_java_object(void);

#ifdef __cplusplus
}
#endif
