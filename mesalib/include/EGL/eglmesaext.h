/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef __eglmesaext_h_
#define __eglmesaext_h_

#ifdef __cplusplus
extern "C" {
#endif

#include <EGL/eglplatform.h>

#ifdef EGL_MESA_drm_image
/* Mesa's extension to EGL_MESA_drm_image... */
#ifndef EGL_DRM_BUFFER_USE_CURSOR_MESA
#define EGL_DRM_BUFFER_USE_CURSOR_MESA		0x0004
#endif
#endif

#ifndef EGL_WL_bind_wayland_display
#define EGL_WL_bind_wayland_display 1

#define EGL_WAYLAND_BUFFER_WL		0x31D5 /* eglCreateImageKHR target */
#define EGL_WAYLAND_PLANE_WL		0x31D6 /* eglCreateImageKHR target */

#define EGL_WAYLAND_Y_INVERTED_WL	0x31DB /* eglQueryWaylandBufferWL attribute */

#define EGL_TEXTURE_Y_U_V_WL            0x31D7
#define EGL_TEXTURE_Y_UV_WL             0x31D8
#define EGL_TEXTURE_Y_XUXV_WL           0x31D9
#define EGL_TEXTURE_EXTERNAL_WL         0x31DA

struct wl_display;
struct wl_resource;
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLBoolean EGLAPIENTRY eglBindWaylandDisplayWL(EGLDisplay dpy, struct wl_display *display);
EGLAPI EGLBoolean EGLAPIENTRY eglUnbindWaylandDisplayWL(EGLDisplay dpy, struct wl_display *display);
EGLAPI EGLBoolean EGLAPIENTRY eglQueryWaylandBufferWL(EGLDisplay dpy, struct wl_resource *buffer, EGLint attribute, EGLint *value);
#endif
typedef EGLBoolean (EGLAPIENTRYP PFNEGLBINDWAYLANDDISPLAYWL) (EGLDisplay dpy, struct wl_display *display);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLUNBINDWAYLANDDISPLAYWL) (EGLDisplay dpy, struct wl_display *display);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYWAYLANDBUFFERWL) (EGLDisplay dpy, struct wl_resource *buffer, EGLint attribute, EGLint *value);

#endif

#ifndef EGL_WL_create_wayland_buffer_from_image
#define EGL_WL_create_wayland_buffer_from_image 1

#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI struct wl_buffer * EGLAPIENTRY eglCreateWaylandBufferFromImageWL(EGLDisplay dpy, EGLImageKHR image);
#endif
typedef struct wl_buffer * (EGLAPIENTRYP PFNEGLCREATEWAYLANDBUFFERFROMIMAGEWL) (EGLDisplay dpy, EGLImageKHR image);

#endif

/* remnant of EGL_NOK_swap_region kept for compatibility because of a non-standard type name */
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSWAPBUFFERSREGIONNOK) (EGLDisplay dpy, EGLSurface surface, EGLint numRects, const EGLint* rects);

#ifndef EGL_MESA_configless_context
#define EGL_MESA_configless_context 1
#define EGL_NO_CONFIG_MESA			((EGLConfig)0)
#endif

#ifndef EGL_MESA_platform_surfaceless
#define EGL_MESA_platform_surfaceless 1
#define EGL_PLATFORM_SURFACELESS_MESA           0x31DD
#endif /* EGL_MESA_platform_surfaceless */

#ifdef __cplusplus
}
#endif

#endif
