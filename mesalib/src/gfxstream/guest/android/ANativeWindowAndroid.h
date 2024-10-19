/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "gfxstream/guest/ANativeWindow.h"

namespace gfxstream {

class ANativeWindowHelperAndroid : public ANativeWindowHelper {
   public:
    ANativeWindowHelperAndroid() = default;

    bool isValid(EGLNativeWindowType window);
    bool isValid(EGLClientBuffer buffer);

    void acquire(EGLNativeWindowType window);
    void release(EGLNativeWindowType window);

    void acquire(EGLClientBuffer buffer);
    void release(EGLClientBuffer buffer);

    int getConsumerUsage(EGLNativeWindowType window, int* usage);
    void setUsage(EGLNativeWindowType window, int usage);

    int getWidth(EGLNativeWindowType window);
    int getHeight(EGLNativeWindowType window);

    int getWidth(EGLClientBuffer buffer);
    int getHeight(EGLClientBuffer buffer);

    int getFormat(EGLClientBuffer buffer, Gralloc* helper);

    void setSwapInterval(EGLNativeWindowType window, int interval);

    int queueBuffer(EGLNativeWindowType window, EGLClientBuffer buffer, int fence);
    int dequeueBuffer(EGLNativeWindowType window, EGLClientBuffer* buffer, int* fence);
    int cancelBuffer(EGLNativeWindowType window, EGLClientBuffer buffer);

    int getHostHandle(EGLClientBuffer buffer, Gralloc* helper);
};

}  // namespace gfxstream
