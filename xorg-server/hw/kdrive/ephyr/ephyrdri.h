/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 *
 * Copyright © 2007 OpenedHand Ltd
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of OpenedHand Ltd not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. OpenedHand Ltd makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * OpenedHand Ltd DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OpenedHand Ltd BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *    Dodji Seketeli <dodji@openedhand.com>
 */

#ifndef __EPHYRDRI_H__
#define __EPHYRDRI_H__

#include <xf86drm.h>

Bool ephyrDRIQueryDirectRenderingCapable(int a_screen, Bool *a_is_capable);
Bool ephyrDRIOpenConnection(int screen, drm_handle_t * a_sarea,
                            char **a_bus_id_string);
Bool ephyrDRIAuthConnection(int a_screen, drm_magic_t a_magic);
Bool ephyrDRICloseConnection(int a_screen);
Bool ephyrDRIGetClientDriverName(int a_screen,
                                 int *a_ddx_driver_major_version,
                                 int *a_ddx_driver_minor_version,
                                 int *a_ddx_driver_patch_version,
                                 char **a_client_driver_name);
Bool ephyrDRICreateContext(int a_screen,
                           int a_visual_id,
                           CARD32 ctx_id, drm_context_t * a_hw_ctx);
Bool ephyrDRIDestroyContext(int a_screen, int a_context_id);
Bool ephyrDRICreateDrawable(int a_screen,
                            int a_drawable, drm_drawable_t * a_hw_drawable);
Bool ephyrDRIDestroyDrawable(int a_screen, int a_drawable);
Bool ephyrDRIGetDrawableInfo(int a_screen, int /*Drawable */ a_drawable,
                             unsigned int *a_index,
                             unsigned int *a_stamp,
                             int *a_x,
                             int *a_y,
                             int *a_w,
                             int *a_h,
                             int *a_num_clip_rects,
                             drm_clip_rect_t ** a_clip_rects,
                             int *a_back_x,
                             int *a_back_y,
                             int *num_back_clip_rects,
                             drm_clip_rect_t ** a_back_clip_rects);
Bool ephyrDRIGetDeviceInfo(int a_screen,
                           drm_handle_t * a_frame_buffer,
                           int *a_fb_origin,
                           int *a_fb_size,
                           int *a_fb_stride,
                           int *a_dev_private_size, void **a_dev_private);
#endif /*__EPHYRDRI_H__*/
