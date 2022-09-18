/*
 * Copyright 2020 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * In principle this could all go in dri_interface.h, but:
 * - I want type safety in here, but I don't want to require vulkan.h from
 *   dri_interface.h
 * - I don't especially want this to be an interface outside of Mesa itself
 * - Ideally dri_interface.h wouldn't even be a thing anymore
 *
 * So instead let's just keep this as a Mesa-internal detail.
 */

#ifndef KOPPER_INTERFACE_H
#define KOPPER_INTERFACE_H

#include <GL/internal/dri_interface.h>
#include <vulkan/vulkan.h>
#ifdef VK_USE_PLATFORM_XCB_KHR
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan_wayland.h>
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_win32.h>
#endif

typedef struct __DRIkopperExtensionRec          __DRIkopperExtension;
typedef struct __DRIkopperLoaderExtensionRec    __DRIkopperLoaderExtension;

/**
 * This extension defines the core GL-atop-VK functionality. This is used by the
 * zink driver to implement GL (or other APIs) natively atop Vulkan, without
 * relying on a particular window system or DRI protocol.
 */
#define __DRI_KOPPER "DRI_Kopper"
#define __DRI_KOPPER_VERSION 1

struct kopper_surface;

struct __DRIkopperExtensionRec {
    __DRIextension base;

    /* This is called by a kopper-aware loader in preference to the one
     * in __DRI_DRISW. The additional fourth argument sets whether the winsys
     * drawable is a pixmap. This matters because swapchains correspond to
     * on-screen surfaces (eg X11 window) and trying to create a swapchain for
     * a pixmap is undefined.
     */
    __DRIdrawable *(*createNewDrawable)(__DRIscreen *screen,
                                        const __DRIconfig *config,
                                        void *loaderPrivate,
                                        int pixmap);
    int64_t (*swapBuffers)(__DRIdrawable *draw);
    void (*setSwapInterval)(__DRIdrawable *drawable, int interval);
    int (*queryBufferAge)(__DRIdrawable *drawable);
};

/**
 * Kopper loader extension.
 */

struct kopper_loader_info {
   union {
      VkBaseOutStructure bos;
#ifdef VK_USE_PLATFORM_XCB_KHR
      VkXcbSurfaceCreateInfoKHR xcb;
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
      VkWaylandSurfaceCreateInfoKHR wl;
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
      VkWin32SurfaceCreateInfoKHR win32;
#endif
   };
   int has_alpha;
   int initial_swap_interval;
};

#define __DRI_KOPPER_LOADER "DRI_KopperLoader"
#define __DRI_KOPPER_LOADER_VERSION 0
struct __DRIkopperLoaderExtensionRec {
    __DRIextension base;

    /* Asks the loader to fill in VkWhateverSurfaceCreateInfo etc. */
    void (*SetSurfaceCreateInfo)(void *draw, struct kopper_loader_info *out);
    /* Asks the loader to fill in the drawable's width and height */
    void (*GetDrawableInfo)(__DRIdrawable *draw, int *w, int *h,
                            void *closure);
};
#endif /* KOPPER_INTERFACE_H */
