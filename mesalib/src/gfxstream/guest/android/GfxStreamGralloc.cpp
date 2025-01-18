/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "gfxstream/guest/GfxStreamGralloc.h"

#if defined(__ANDROID__)

#include <string>

#include "GrallocGoldfish.h"
#include "GrallocMinigbm.h"
#include "android-base/properties.h"

namespace gfxstream {

Gralloc* createPlatformGralloc(int32_t descriptor) {
    const std::string value = android::base::GetProperty("ro.hardware.gralloc", "");
    if (value == "minigbm") {
        auto gralloc = new MinigbmGralloc(descriptor);
        return gralloc;
    }
    return new GoldfishGralloc();
}

}  // namespace gfxstream

#endif
