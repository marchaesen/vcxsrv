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
#ifndef __EPHYRHOSTGLX_H__
#define __EPHYRHOSTGLX_H__

enum EphyrHostGLXGetStringOps {
    EPHYR_HOST_GLX_UNDEF,
    EPHYR_HOST_GLX_QueryServerString,
    EPHYR_HOST_GLX_GetString,
};

Bool ephyrHostGLXQueryVersion(int *a_maj, int *a_min);
Bool ephyrHostGLXGetStringFromServer(int a_screen_number,
                                     int a_string_name,
                                     enum EphyrHostGLXGetStringOps a_op,
                                     char **a_string);
Bool ephyrHostGLXGetVisualConfigs(int a_screen,
                                  int32_t * a_num_visuals,
                                  int32_t * a_num_props,
                                  int32_t * a_props_buf_size,
                                  int32_t ** a_props_buf);
Bool

ephyrHostGLXVendorPrivGetFBConfigsSGIX(int a_screen,
                                       int32_t * a_num_visuals,
                                       int32_t * a_num_props,
                                       int32_t * a_props_buf_size,
                                       int32_t ** a_props_buf);
Bool ephyrHostGLXGetMajorOpcode(int32_t * a_opcode);
Bool ephyrHostGLXSendClientInfo(int32_t a_major, int32_t a_minor,
                                const char *a_extension_list);
Bool ephyrHostGLXCreateContext(int a_screen,
                               int a_generic_id,
                               int a_context_id,
                               int a_share_list_ctxt_id,
                               int a_render_type,
                               Bool a_direct,
                               int code);

Bool ephyrHostDestroyContext(int a_ctxt_id);

Bool ephyrHostGLXMakeCurrent(int a_drawable, int a_readable, int a_glx_ctxt_id,
                             int a_olg_ctxt_tag, int *a_ctxt_tag);

Bool ephyrHostGetIntegerValue(int a_current_context_tag, int a_int, int *a_val);

Bool ephyrHostIsContextDirect(int a_ctxt_id, int *a_is_direct);

#endif /*__EPHYRHOSTGLX_H__*/
