/*
 * Copyright © 2009-2011 Arnaud Fontaine <arnau@debian.org>
 *
 * Permission  is  hereby  granted,  free  of charge,  to  any  person
 * obtaining  a copy  of  this software  and associated  documentation
 * files   (the  "Software"),   to  deal   in  the   Software  without
 * restriction, including without limitation  the rights to use, copy,
 * modify, merge, publish,  distribute, sublicense, and/or sell copies
 * of  the Software, and  to permit  persons to  whom the  Software is
 * furnished to do so, subject to the following conditions:
 *
 * The  above copyright  notice and  this permission  notice  shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE  IS PROVIDED  "AS IS", WITHOUT  WARRANTY OF  ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT  NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY,   FITNESS    FOR   A   PARTICULAR    PURPOSE   AND
 * NONINFRINGEMENT. IN  NO EVENT SHALL  THE AUTHORS BE LIABLE  FOR ANY
 * CLAIM,  DAMAGES  OR  OTHER  LIABILITY,  WHETHER  IN  AN  ACTION  OF
 * CONTRACT, TORT OR OTHERWISE, ARISING  FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as  contained in  this notice, the  names of the  authors or
 * their institutions shall not be used in advertising or otherwise to
 * promote the  sale, use or  other dealings in this  Software without
 * prior written authorization from the authors.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xcb_ewmh.h"

#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stddef.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include <sys/types.h>

#define ssizeof(foo)            (ssize_t)sizeof(foo)
#define countof(foo)            (ssizeof(foo) / ssizeof(foo[0]))

/**
 * @brief The  structure used  on screen initialization  including the
 * atoms name and its length
 */
typedef struct {
  /** The Atom name length */
  uint8_t name_len;
  /** The Atom name string */
  const char *name;
  size_t m_offset;
} ewmh_atom_t;



/**
 * @brief List  of atoms where each  entry contains the  Atom name and
 * its length
 */
static ewmh_atom_t ewmh_atoms[] = {  
  { sizeof("_NET_SUPPORTED") - 1, "_NET_SUPPORTED", offsetof(xcb_ewmh_connection_t, _NET_SUPPORTED) },
  { sizeof("_NET_CLIENT_LIST") - 1, "_NET_CLIENT_LIST", offsetof(xcb_ewmh_connection_t, _NET_CLIENT_LIST) },
  { sizeof("_NET_CLIENT_LIST_STACKING") - 1, "_NET_CLIENT_LIST_STACKING", offsetof(xcb_ewmh_connection_t, _NET_CLIENT_LIST_STACKING) },
  { sizeof("_NET_NUMBER_OF_DESKTOPS") - 1, "_NET_NUMBER_OF_DESKTOPS", offsetof(xcb_ewmh_connection_t, _NET_NUMBER_OF_DESKTOPS) },
  { sizeof("_NET_DESKTOP_GEOMETRY") - 1, "_NET_DESKTOP_GEOMETRY", offsetof(xcb_ewmh_connection_t, _NET_DESKTOP_GEOMETRY) },
  { sizeof("_NET_DESKTOP_VIEWPORT") - 1, "_NET_DESKTOP_VIEWPORT", offsetof(xcb_ewmh_connection_t, _NET_DESKTOP_VIEWPORT) },
  { sizeof("_NET_CURRENT_DESKTOP") - 1, "_NET_CURRENT_DESKTOP", offsetof(xcb_ewmh_connection_t, _NET_CURRENT_DESKTOP) },
  { sizeof("_NET_DESKTOP_NAMES") - 1, "_NET_DESKTOP_NAMES", offsetof(xcb_ewmh_connection_t, _NET_DESKTOP_NAMES) },
  { sizeof("_NET_ACTIVE_WINDOW") - 1, "_NET_ACTIVE_WINDOW", offsetof(xcb_ewmh_connection_t, _NET_ACTIVE_WINDOW) },
  { sizeof("_NET_WORKAREA") - 1, "_NET_WORKAREA", offsetof(xcb_ewmh_connection_t, _NET_WORKAREA) },
  { sizeof("_NET_SUPPORTING_WM_CHECK") - 1, "_NET_SUPPORTING_WM_CHECK", offsetof(xcb_ewmh_connection_t, _NET_SUPPORTING_WM_CHECK) },
  { sizeof("_NET_VIRTUAL_ROOTS") - 1, "_NET_VIRTUAL_ROOTS", offsetof(xcb_ewmh_connection_t, _NET_VIRTUAL_ROOTS) },
  { sizeof("_NET_DESKTOP_LAYOUT") - 1, "_NET_DESKTOP_LAYOUT", offsetof(xcb_ewmh_connection_t, _NET_DESKTOP_LAYOUT) },
  { sizeof("_NET_SHOWING_DESKTOP") - 1, "_NET_SHOWING_DESKTOP", offsetof(xcb_ewmh_connection_t, _NET_SHOWING_DESKTOP) },
  { sizeof("_NET_CLOSE_WINDOW") - 1, "_NET_CLOSE_WINDOW", offsetof(xcb_ewmh_connection_t, _NET_CLOSE_WINDOW) },
  { sizeof("_NET_MOVERESIZE_WINDOW") - 1, "_NET_MOVERESIZE_WINDOW", offsetof(xcb_ewmh_connection_t, _NET_MOVERESIZE_WINDOW) },
  { sizeof("_NET_WM_MOVERESIZE") - 1, "_NET_WM_MOVERESIZE", offsetof(xcb_ewmh_connection_t, _NET_WM_MOVERESIZE) },
  { sizeof("_NET_RESTACK_WINDOW") - 1, "_NET_RESTACK_WINDOW", offsetof(xcb_ewmh_connection_t, _NET_RESTACK_WINDOW) },
  { sizeof("_NET_REQUEST_FRAME_EXTENTS") - 1, "_NET_REQUEST_FRAME_EXTENTS", offsetof(xcb_ewmh_connection_t, _NET_REQUEST_FRAME_EXTENTS) },
  { sizeof("_NET_WM_NAME") - 1, "_NET_WM_NAME", offsetof(xcb_ewmh_connection_t, _NET_WM_NAME) },
  { sizeof("_NET_WM_VISIBLE_NAME") - 1, "_NET_WM_VISIBLE_NAME", offsetof(xcb_ewmh_connection_t, _NET_WM_VISIBLE_NAME) },
  { sizeof("_NET_WM_ICON_NAME") - 1, "_NET_WM_ICON_NAME", offsetof(xcb_ewmh_connection_t, _NET_WM_ICON_NAME) },
  { sizeof("_NET_WM_VISIBLE_ICON_NAME") - 1, "_NET_WM_VISIBLE_ICON_NAME", offsetof(xcb_ewmh_connection_t, _NET_WM_VISIBLE_ICON_NAME) },
  { sizeof("_NET_WM_DESKTOP") - 1, "_NET_WM_DESKTOP", offsetof(xcb_ewmh_connection_t, _NET_WM_DESKTOP) },
  { sizeof("_NET_WM_WINDOW_TYPE") - 1, "_NET_WM_WINDOW_TYPE", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE) },
  { sizeof("_NET_WM_STATE") - 1, "_NET_WM_STATE", offsetof(xcb_ewmh_connection_t, _NET_WM_STATE) },
  { sizeof("_NET_WM_ALLOWED_ACTIONS") - 1, "_NET_WM_ALLOWED_ACTIONS", offsetof(xcb_ewmh_connection_t, _NET_WM_ALLOWED_ACTIONS) },
  { sizeof("_NET_WM_STRUT") - 1, "_NET_WM_STRUT", offsetof(xcb_ewmh_connection_t, _NET_WM_STRUT) },
  { sizeof("_NET_WM_STRUT_PARTIAL") - 1, "_NET_WM_STRUT_PARTIAL", offsetof(xcb_ewmh_connection_t, _NET_WM_STRUT_PARTIAL) },
  { sizeof("_NET_WM_ICON_GEOMETRY") - 1, "_NET_WM_ICON_GEOMETRY", offsetof(xcb_ewmh_connection_t, _NET_WM_ICON_GEOMETRY) },
  { sizeof("_NET_WM_ICON") - 1, "_NET_WM_ICON", offsetof(xcb_ewmh_connection_t, _NET_WM_ICON) },
  { sizeof("_NET_WM_PID") - 1, "_NET_WM_PID", offsetof(xcb_ewmh_connection_t, _NET_WM_PID) },
  { sizeof("_NET_WM_HANDLED_ICONS") - 1, "_NET_WM_HANDLED_ICONS", offsetof(xcb_ewmh_connection_t, _NET_WM_HANDLED_ICONS) },
  { sizeof("_NET_WM_USER_TIME") - 1, "_NET_WM_USER_TIME", offsetof(xcb_ewmh_connection_t, _NET_WM_USER_TIME) },
  { sizeof("_NET_WM_USER_TIME_WINDOW") - 1, "_NET_WM_USER_TIME_WINDOW", offsetof(xcb_ewmh_connection_t, _NET_WM_USER_TIME_WINDOW) },
  { sizeof("_NET_FRAME_EXTENTS") - 1, "_NET_FRAME_EXTENTS", offsetof(xcb_ewmh_connection_t, _NET_FRAME_EXTENTS) },
  { sizeof("_NET_WM_PING") - 1, "_NET_WM_PING", offsetof(xcb_ewmh_connection_t, _NET_WM_PING) },
  { sizeof("_NET_WM_SYNC_REQUEST") - 1, "_NET_WM_SYNC_REQUEST", offsetof(xcb_ewmh_connection_t, _NET_WM_SYNC_REQUEST) },
  { sizeof("_NET_WM_SYNC_REQUEST_COUNTER") - 1, "_NET_WM_SYNC_REQUEST_COUNTER", offsetof(xcb_ewmh_connection_t, _NET_WM_SYNC_REQUEST_COUNTER) },
  { sizeof("_NET_WM_FULLSCREEN_MONITORS") - 1, "_NET_WM_FULLSCREEN_MONITORS", offsetof(xcb_ewmh_connection_t, _NET_WM_FULLSCREEN_MONITORS) },
  { sizeof("_NET_WM_FULL_PLACEMENT") - 1, "_NET_WM_FULL_PLACEMENT", offsetof(xcb_ewmh_connection_t, _NET_WM_FULL_PLACEMENT) },
  { sizeof("UTF8_STRING") - 1, "UTF8_STRING", offsetof(xcb_ewmh_connection_t, UTF8_STRING) },
  { sizeof("WM_PROTOCOLS") - 1, "WM_PROTOCOLS", offsetof(xcb_ewmh_connection_t, WM_PROTOCOLS) },
  { sizeof("MANAGER") - 1, "MANAGER", offsetof(xcb_ewmh_connection_t, MANAGER) },
  { sizeof("_NET_WM_WINDOW_TYPE_DESKTOP") - 1, "_NET_WM_WINDOW_TYPE_DESKTOP", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_DESKTOP) },
  { sizeof("_NET_WM_WINDOW_TYPE_DOCK") - 1, "_NET_WM_WINDOW_TYPE_DOCK", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_DOCK) },
  { sizeof("_NET_WM_WINDOW_TYPE_TOOLBAR") - 1, "_NET_WM_WINDOW_TYPE_TOOLBAR", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_TOOLBAR) },
  { sizeof("_NET_WM_WINDOW_TYPE_MENU") - 1, "_NET_WM_WINDOW_TYPE_MENU", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_MENU) },
  { sizeof("_NET_WM_WINDOW_TYPE_UTILITY") - 1, "_NET_WM_WINDOW_TYPE_UTILITY", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_UTILITY) },
  { sizeof("_NET_WM_WINDOW_TYPE_SPLASH") - 1, "_NET_WM_WINDOW_TYPE_SPLASH", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_SPLASH) },
  { sizeof("_NET_WM_WINDOW_TYPE_DIALOG") - 1, "_NET_WM_WINDOW_TYPE_DIALOG", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_DIALOG) },
  { sizeof("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU") - 1, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_DROPDOWN_MENU) },
  { sizeof("_NET_WM_WINDOW_TYPE_POPUP_MENU") - 1, "_NET_WM_WINDOW_TYPE_POPUP_MENU", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_POPUP_MENU) },
  { sizeof("_NET_WM_WINDOW_TYPE_TOOLTIP") - 1, "_NET_WM_WINDOW_TYPE_TOOLTIP", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_TOOLTIP) },
  { sizeof("_NET_WM_WINDOW_TYPE_NOTIFICATION") - 1, "_NET_WM_WINDOW_TYPE_NOTIFICATION", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_NOTIFICATION) },
  { sizeof("_NET_WM_WINDOW_TYPE_COMBO") - 1, "_NET_WM_WINDOW_TYPE_COMBO", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_COMBO) },
  { sizeof("_NET_WM_WINDOW_TYPE_DND") - 1, "_NET_WM_WINDOW_TYPE_DND", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_DND) },
  { sizeof("_NET_WM_WINDOW_TYPE_NORMAL") - 1, "_NET_WM_WINDOW_TYPE_NORMAL", offsetof(xcb_ewmh_connection_t, _NET_WM_WINDOW_TYPE_NORMAL) },
  { sizeof("_NET_WM_STATE_MODAL") - 1, "_NET_WM_STATE_MODAL", offsetof(xcb_ewmh_connection_t, _NET_WM_STATE_MODAL) },
  { sizeof("_NET_WM_STATE_STICKY") - 1, "_NET_WM_STATE_STICKY", offsetof(xcb_ewmh_connection_t, _NET_WM_STATE_STICKY) },
  { sizeof("_NET_WM_STATE_MAXIMIZED_VERT") - 1, "_NET_WM_STATE_MAXIMIZED_VERT", offsetof(xcb_ewmh_connection_t, _NET_WM_STATE_MAXIMIZED_VERT) },
  { sizeof("_NET_WM_STATE_MAXIMIZED_HORZ") - 1, "_NET_WM_STATE_MAXIMIZED_HORZ", offsetof(xcb_ewmh_connection_t, _NET_WM_STATE_MAXIMIZED_HORZ) },
  { sizeof("_NET_WM_STATE_SHADED") - 1, "_NET_WM_STATE_SHADED", offsetof(xcb_ewmh_connection_t, _NET_WM_STATE_SHADED) },
  { sizeof("_NET_WM_STATE_SKIP_TASKBAR") - 1, "_NET_WM_STATE_SKIP_TASKBAR", offsetof(xcb_ewmh_connection_t, _NET_WM_STATE_SKIP_TASKBAR) },
  { sizeof("_NET_WM_STATE_SKIP_PAGER") - 1, "_NET_WM_STATE_SKIP_PAGER", offsetof(xcb_ewmh_connection_t, _NET_WM_STATE_SKIP_PAGER) },
  { sizeof("_NET_WM_STATE_HIDDEN") - 1, "_NET_WM_STATE_HIDDEN", offsetof(xcb_ewmh_connection_t, _NET_WM_STATE_HIDDEN) },
  { sizeof("_NET_WM_STATE_FULLSCREEN") - 1, "_NET_WM_STATE_FULLSCREEN", offsetof(xcb_ewmh_connection_t, _NET_WM_STATE_FULLSCREEN) },
  { sizeof("_NET_WM_STATE_ABOVE") - 1, "_NET_WM_STATE_ABOVE", offsetof(xcb_ewmh_connection_t, _NET_WM_STATE_ABOVE) },
  { sizeof("_NET_WM_STATE_BELOW") - 1, "_NET_WM_STATE_BELOW", offsetof(xcb_ewmh_connection_t, _NET_WM_STATE_BELOW) },
  { sizeof("_NET_WM_STATE_DEMANDS_ATTENTION") - 1, "_NET_WM_STATE_DEMANDS_ATTENTION", offsetof(xcb_ewmh_connection_t, _NET_WM_STATE_DEMANDS_ATTENTION) },
  { sizeof("_NET_WM_ACTION_MOVE") - 1, "_NET_WM_ACTION_MOVE", offsetof(xcb_ewmh_connection_t, _NET_WM_ACTION_MOVE) },
  { sizeof("_NET_WM_ACTION_RESIZE") - 1, "_NET_WM_ACTION_RESIZE", offsetof(xcb_ewmh_connection_t, _NET_WM_ACTION_RESIZE) },
  { sizeof("_NET_WM_ACTION_MINIMIZE") - 1, "_NET_WM_ACTION_MINIMIZE", offsetof(xcb_ewmh_connection_t, _NET_WM_ACTION_MINIMIZE) },
  { sizeof("_NET_WM_ACTION_SHADE") - 1, "_NET_WM_ACTION_SHADE", offsetof(xcb_ewmh_connection_t, _NET_WM_ACTION_SHADE) },
  { sizeof("_NET_WM_ACTION_STICK") - 1, "_NET_WM_ACTION_STICK", offsetof(xcb_ewmh_connection_t, _NET_WM_ACTION_STICK) },
  { sizeof("_NET_WM_ACTION_MAXIMIZE_HORZ") - 1, "_NET_WM_ACTION_MAXIMIZE_HORZ", offsetof(xcb_ewmh_connection_t, _NET_WM_ACTION_MAXIMIZE_HORZ) },
  { sizeof("_NET_WM_ACTION_MAXIMIZE_VERT") - 1, "_NET_WM_ACTION_MAXIMIZE_VERT", offsetof(xcb_ewmh_connection_t, _NET_WM_ACTION_MAXIMIZE_VERT) },
  { sizeof("_NET_WM_ACTION_FULLSCREEN") - 1, "_NET_WM_ACTION_FULLSCREEN", offsetof(xcb_ewmh_connection_t, _NET_WM_ACTION_FULLSCREEN) },
  { sizeof("_NET_WM_ACTION_CHANGE_DESKTOP") - 1, "_NET_WM_ACTION_CHANGE_DESKTOP", offsetof(xcb_ewmh_connection_t, _NET_WM_ACTION_CHANGE_DESKTOP) },
  { sizeof("_NET_WM_ACTION_CLOSE") - 1, "_NET_WM_ACTION_CLOSE", offsetof(xcb_ewmh_connection_t, _NET_WM_ACTION_CLOSE) },
  { sizeof("_NET_WM_ACTION_ABOVE") - 1, "_NET_WM_ACTION_ABOVE", offsetof(xcb_ewmh_connection_t, _NET_WM_ACTION_ABOVE) },
  { sizeof("_NET_WM_ACTION_BELOW") - 1, "_NET_WM_ACTION_BELOW", offsetof(xcb_ewmh_connection_t, _NET_WM_ACTION_BELOW) }
};

#define NB_EWMH_ATOMS countof(ewmh_atoms)

/**
 * Common functions and macro
 */

#define DO_GET_PROPERTY(fname, property, type, length)                  \
  xcb_get_property_cookie_t                                             \
  xcb_ewmh_get_##fname(xcb_ewmh_connection_t *ewmh,                     \
                       xcb_window_t window)                             \
  {                                                                     \
    return xcb_get_property(ewmh->connection, 0, window,                \
                            ewmh->property, type, 0, length);           \
  }                                                                     \
                                                                        \
  xcb_get_property_cookie_t                                             \
  xcb_ewmh_get_##fname##_unchecked(xcb_ewmh_connection_t *ewmh,         \
                                   xcb_window_t window)                 \
  {                                                                     \
    return xcb_get_property_unchecked(ewmh->connection, 0, window,      \
                                      ewmh->property, type, 0, length); \
  }

#define DO_GET_ROOT_PROPERTY(fname, property, atype, length)            \
  xcb_get_property_cookie_t                                             \
  xcb_ewmh_get_##fname(xcb_ewmh_connection_t *ewmh,                     \
                       int screen_nbr)                                  \
  {                                                                     \
    return xcb_get_property(ewmh->connection, 0,                        \
                            ewmh->screens[screen_nbr]->root,            \
                            ewmh->property, atype, 0, length);          \
  }                                                                     \
                                                                        \
  xcb_get_property_cookie_t                                             \
  xcb_ewmh_get_##fname##_unchecked(xcb_ewmh_connection_t *ewmh,         \
                                   int screen_nbr)                      \
  {                                                                     \
    return xcb_get_property_unchecked(ewmh->connection, 0,              \
                                      ewmh->screens[screen_nbr]->root,  \
                                      ewmh->property, atype, 0,         \
                                      length);                          \
  }

/**
 * Generic  function for  EWMH atoms  with  a single  value which  may
 * actually be either WINDOW or CARDINAL
 *
 * _NET_NUMBER_OF_DESKTOPS, CARDINAL/32
 * _NET_CURRENT_DESKTOP desktop, CARDINAL/32
 * _NET_ACTIVE_WINDOW, WINDOW/32
 * _NET_SUPPORTING_WM_CHECK, WINDOW/32
 * _NET_SHOWING_DESKTOP desktop, CARDINAL/32
 * _NET_WM_DESKTOP desktop, CARDINAL/32
 * _NET_WM_PID CARDINAL/32
 * _NET_WM_USER_TIME CARDINAL/32
 * _NET_WM_USER_TIME_WINDOW WINDOW/32
 */

/**
 * Macro defining  a generic function  for reply with a  single value,
 * considering that the  value is 32-bit long (actually  only used for
 * WINDOW and CARDINAL)
 */
#define DO_REPLY_SINGLE_VALUE(fname, atype, ctype)                      \
  uint8_t                                                               \
  xcb_ewmh_get_##fname##_from_reply(ctype *atom_value,                  \
                                    xcb_get_property_reply_t *r)        \
  {                                                                     \
    if(!r || r->type != atype || r->format != 32 ||                     \
       xcb_get_property_value_length(r) != sizeof(ctype))               \
      return 0;                                                         \
                                                                        \
    *atom_value = *((ctype *) xcb_get_property_value(r));               \
    return 1;                                                           \
  }                                                                     \
                                                                        \
  uint8_t                                                               \
  xcb_ewmh_get_##fname##_reply(xcb_ewmh_connection_t *ewmh,             \
                               xcb_get_property_cookie_t cookie,        \
                               ctype *atom_value,                       \
                               xcb_generic_error_t **e)                 \
  {                                                                     \
    xcb_get_property_reply_t *r =                                       \
      xcb_get_property_reply(ewmh->connection,                          \
                             cookie, e);                                \
                                                                        \
    const uint8_t ret = xcb_ewmh_get_##fname##_from_reply(atom_value, r); \
                                                                        \
    free(r);                                                            \
    return ret;                                                         \
  }

/** Define reply functions for common WINDOW Atom */
DO_REPLY_SINGLE_VALUE(window, XCB_ATOM_WINDOW, xcb_window_t)

/** Define reply functions for common CARDINAL Atom */
DO_REPLY_SINGLE_VALUE(cardinal, XCB_ATOM_CARDINAL, uint32_t)

#define DO_SINGLE_VALUE(fname, property, atype, ctype)                  \
  DO_GET_PROPERTY(fname, property, atype, 1L)                           \
                                                                        \
  xcb_void_cookie_t                                                     \
  xcb_ewmh_set_##fname##_checked(xcb_ewmh_connection_t *ewmh,           \
                                 xcb_window_t window,                   \
                                 ctype value)                           \
  {                                                                     \
    return xcb_change_property_checked(ewmh->connection,                \
                                       XCB_PROP_MODE_REPLACE,           \
                                       window, ewmh->property,          \
                                       atype, 32, 1, &value);           \
  }                                                                     \
                                                                        \
  xcb_void_cookie_t                                                     \
  xcb_ewmh_set_##fname(xcb_ewmh_connection_t *ewmh,                     \
                       xcb_window_t window,                             \
                       ctype value)                                     \
  {                                                                     \
    return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE, \
                               window, ewmh->property, atype, 32, 1,    \
                               &value);                                 \
  }

#define DO_ROOT_SINGLE_VALUE(fname, property, atype, ctype)             \
  DO_GET_ROOT_PROPERTY(fname, property, atype, 1L)                      \
                                                                        \
  xcb_void_cookie_t                                                     \
  xcb_ewmh_set_##fname##_checked(xcb_ewmh_connection_t *ewmh,           \
                                 int screen_nbr,                        \
                                 ctype value)                           \
  {                                                                     \
    return xcb_change_property_checked(ewmh->connection,                \
                                       XCB_PROP_MODE_REPLACE,           \
                                       ewmh->screens[screen_nbr]->root, \
                                       ewmh->property, atype, 32, 1,    \
                                       &value);                         \
  }                                                                     \
                                                                        \
  xcb_void_cookie_t                                                     \
  xcb_ewmh_set_##fname(xcb_ewmh_connection_t *ewmh,                     \
                       int screen_nbr,                                  \
                       ctype value)                                     \
  {                                                                     \
    return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE, \
                               ewmh->screens[screen_nbr]->root,         \
                               ewmh->property, atype,                   \
                               32, 1, &value);                          \
  }

/**
 * Generic function for EWMH atoms with  a list of values which may be
 * actually WINDOW or ATOM.
 *
 * _NET_SUPPORTED, ATOM[]/32
 * _NET_CLIENT_LIST, WINDOW[]/32
 * _NET_CLIENT_LIST_STACKING, WINDOW[]/32
 * _NET_VIRTUAL_ROOTS, WINDOW[]/32
 * _NET_WM_WINDOW_TYPE, ATOM[]/32
 * _NET_WM_ALLOWED_ACTIONS, ATOM[]
 */

/**
 * Macro defining  a generic function  for reply containing a  list of
 * values and also defines a function to wipe the reply.
 */
#define DO_REPLY_LIST_VALUES(fname, atype, ctype)                       \
  uint8_t                                                               \
  xcb_ewmh_get_##fname##_from_reply(xcb_ewmh_get_##fname##_reply_t *data, \
                                    xcb_get_property_reply_t *r)        \
  {                                                                     \
    if(!r || r->type != atype || r->format != 32)                       \
      return 0;                                                         \
                                                                        \
    data->_reply = r;                                                   \
    data->fname##_len = xcb_get_property_value_length(data->_reply) /   \
      sizeof(ctype);                                                    \
                                                                        \
    data->fname = (ctype *) xcb_get_property_value(data->_reply);       \
    return 1;                                                           \
  }                                                                     \
                                                                        \
  uint8_t                                                               \
  xcb_ewmh_get_##fname##_reply(xcb_ewmh_connection_t *ewmh,             \
                               xcb_get_property_cookie_t cookie,        \
                               xcb_ewmh_get_##fname##_reply_t *data,    \
                               xcb_generic_error_t **e)                 \
  {                                                                     \
    xcb_get_property_reply_t *r =                                       \
      xcb_get_property_reply(ewmh->connection,                          \
                             cookie, e);                                \
                                                                        \
    const uint8_t ret = xcb_ewmh_get_##fname##_from_reply(data, r);     \
                                                                        \
    /* If the  last call  was not successful  (ret equals to  0), then  \
       just free the reply as the data value is not consistent */       \
    if(!ret)                                                            \
      free(r);                                                          \
                                                                        \
    return ret;                                                         \
  }                                                                     \
                                                                        \
  void                                                                  \
  xcb_ewmh_get_##fname##_reply_wipe(xcb_ewmh_get_##fname##_reply_t *data) \
  {                                                                     \
    free(data->_reply);                                                 \
  }

#define DO_ROOT_LIST_VALUES(fname, property, atype, ctype)              \
  DO_GET_ROOT_PROPERTY(fname, property, atype, UINT_MAX)                \
                                                                        \
  xcb_void_cookie_t                                                     \
  xcb_ewmh_set_##fname##_checked(xcb_ewmh_connection_t *ewmh,           \
                                 int screen_nbr,                        \
                                 uint32_t list_len,                     \
                                 ctype *list)                           \
  {                                                                     \
    return xcb_change_property_checked(ewmh->connection,                \
                                       XCB_PROP_MODE_REPLACE,           \
                                       ewmh->screens[screen_nbr]->root, \
                                       ewmh->property, atype, 32,       \
                                       list_len * (sizeof(ctype) >> 2), \
                                       list);                           \
  }                                                                     \
                                                                        \
  xcb_void_cookie_t                                                     \
  xcb_ewmh_set_##fname(xcb_ewmh_connection_t *ewmh,                     \
                       int screen_nbr,                                  \
                       uint32_t list_len,                               \
                       ctype *list)                                     \
  {                                                                     \
    return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE, \
                               ewmh->screens[screen_nbr]->root,         \
                               ewmh->property, atype, 32,               \
                               list_len * (sizeof(ctype) >> 2),         \
                               list);                                   \
  }

#define DO_LIST_VALUES(fname, property, atype, kind)                    \
  DO_GET_PROPERTY(fname, property, atype, UINT_MAX)                     \
                                                                        \
  xcb_void_cookie_t                                                     \
  xcb_ewmh_set_##fname##_checked(xcb_ewmh_connection_t *ewmh,           \
                                 xcb_window_t window,                   \
                                 uint32_t list_len,                     \
                                 xcb_##kind##_t *list)                  \
  {                                                                     \
    return xcb_change_property_checked(ewmh->connection,                \
                                       XCB_PROP_MODE_REPLACE, window,   \
                                       ewmh->property, atype, 32,       \
                                       list_len, list);                 \
  }                                                                     \
                                                                        \
  xcb_void_cookie_t                                                     \
  xcb_ewmh_set_##fname(xcb_ewmh_connection_t *ewmh,                     \
                       xcb_window_t window,                             \
                       uint32_t list_len,                               \
                       xcb_##kind##_t *list)                            \
  {                                                                     \
    return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE, \
                               window, ewmh->property, atype, 32,       \
                               list_len, list);                         \
  }                                                                     \
                                                                        \
  uint8_t                                                               \
  xcb_ewmh_get_##fname##_from_reply(xcb_ewmh_get_##kind##s_reply_t *name, \
                                    xcb_get_property_reply_t *r)        \
  {                                                                     \
    return xcb_ewmh_get_##kind##s_from_reply(name, r);                  \
  }                                                                     \
                                                                        \
  uint8_t                                                               \
  xcb_ewmh_get_##fname##_reply(xcb_ewmh_connection_t *ewmh,             \
                               xcb_get_property_cookie_t cookie,        \
                               xcb_ewmh_get_##kind##s_reply_t *name,    \
                               xcb_generic_error_t **e)                 \
  {                                                                     \
    return xcb_ewmh_get_##kind##s_reply(ewmh, cookie, name, e);         \
  }

#define DO_REPLY_STRUCTURE(fname, ctype)                                \
  uint8_t                                                               \
  xcb_ewmh_get_##fname##_from_reply(ctype *out,                         \
                                    xcb_get_property_reply_t *r)        \
  {                                                                     \
    if(!r || r->type != XCB_ATOM_CARDINAL || r->format != 32 ||         \
       xcb_get_property_value_length(r) != sizeof(ctype))               \
      return 0;                                                         \
                                                                        \
    memcpy(out, xcb_get_property_value(r),                              \
           xcb_get_property_value_length(r));                           \
                                                                        \
    return 1;                                                           \
  }                                                                     \
                                                                        \
  uint8_t                                                               \
  xcb_ewmh_get_##fname##_reply(xcb_ewmh_connection_t *ewmh,             \
                               xcb_get_property_cookie_t cookie,        \
                               ctype *out,                              \
                               xcb_generic_error_t **e)                 \
  {                                                                     \
    xcb_get_property_reply_t *r =                                       \
      xcb_get_property_reply(ewmh->connection, cookie, e);              \
                                                                        \
    const uint8_t ret = xcb_ewmh_get_##fname##_from_reply(out, r);      \
    free(r);                                                            \
    return ret;                                                         \
  }

/**
 * UTF8_STRING handling
 */

uint8_t
xcb_ewmh_get_utf8_strings_from_reply(xcb_ewmh_connection_t *ewmh,
                                     xcb_ewmh_get_utf8_strings_reply_t *data,
                                     xcb_get_property_reply_t *r)
{
  if(!r || r->type != ewmh->UTF8_STRING || r->format != 8)
    return 0;

  data->_reply = r;
  data->strings_len = xcb_get_property_value_length(data->_reply);
  data->strings = (char *) xcb_get_property_value(data->_reply);

  return 1;
}

uint8_t
xcb_ewmh_get_utf8_strings_reply(xcb_ewmh_connection_t *ewmh,
                                xcb_get_property_cookie_t cookie,
                                xcb_ewmh_get_utf8_strings_reply_t *data,
                                xcb_generic_error_t **e)
{
  xcb_get_property_reply_t *r = xcb_get_property_reply(ewmh->connection,
                                                       cookie, e);

  const uint8_t ret = xcb_ewmh_get_utf8_strings_from_reply(ewmh, data, r);

  /* If the last call was not  successful (ret equals to 0), then just
     free the reply as the data value is not consistent */
  if(!ret)
    free(r);

  return ret;
}

void
xcb_ewmh_get_utf8_strings_reply_wipe(xcb_ewmh_get_utf8_strings_reply_t *data)
{
  free(data->_reply);
}

#define DO_ROOT_UTF8_STRING(fname, property)                            \
  DO_GET_ROOT_PROPERTY(fname, property, 0, UINT_MAX)                    \
                                                                        \
  xcb_void_cookie_t                                                     \
  xcb_ewmh_set_##fname(xcb_ewmh_connection_t *ewmh,                     \
                       int screen_nbr,                                  \
                       uint32_t strings_len,                            \
                       const char *strings)                             \
  {                                                                     \
    return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE, \
                               ewmh->screens[screen_nbr]->root,         \
                               ewmh->property, ewmh->UTF8_STRING, 8,    \
                               strings_len, strings);                   \
  }                                                                     \
                                                                        \
  xcb_void_cookie_t                                                     \
  xcb_ewmh_set_##fname##_checked(xcb_ewmh_connection_t *ewmh,           \
                                 int screen_nbr,                        \
                                 uint32_t strings_len,                  \
                                 const char *strings)                   \
  {                                                                     \
    return xcb_change_property_checked(ewmh->connection,                \
                                       XCB_PROP_MODE_REPLACE,           \
                                       ewmh->screens[screen_nbr]->root, \
                                       ewmh->property,                  \
                                       ewmh->UTF8_STRING, 8,            \
                                       strings_len, strings);           \
  }

#define DO_UTF8_STRING(fname, property)                                 \
  DO_GET_PROPERTY(fname, property, 0, UINT_MAX)                         \
                                                                        \
  xcb_void_cookie_t                                                     \
  xcb_ewmh_set_##fname(xcb_ewmh_connection_t *ewmh,                     \
                       xcb_window_t window,                             \
                       uint32_t strings_len,                            \
                       const char *strings)                             \
  {                                                                     \
    return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE, \
                               window, ewmh->property,                  \
                               ewmh->UTF8_STRING, 8, strings_len,       \
                               strings);                                \
  }                                                                     \
                                                                        \
  xcb_void_cookie_t                                                     \
  xcb_ewmh_set_##fname##_checked(xcb_ewmh_connection_t *ewmh,           \
                                 xcb_window_t window,                   \
                                 uint32_t strings_len,                  \
                                 const char *strings)                   \
  {                                                                     \
    return xcb_change_property_checked(ewmh->connection,                \
                                       XCB_PROP_MODE_REPLACE,           \
                                       window, ewmh->property,          \
                                       ewmh->UTF8_STRING, 8,            \
                                       strings_len, strings);           \
  }

/**
 * ClientMessage generic function
 */
xcb_void_cookie_t
xcb_ewmh_send_client_message(xcb_connection_t *c,
                             xcb_window_t window,
                             xcb_window_t dest,
                             xcb_atom_t atom,
                             uint32_t data_len,
                             const uint32_t *data)
{
  xcb_client_message_event_t ev;
  memset(&ev, 0, sizeof(xcb_client_message_event_t));

  ev.response_type = XCB_CLIENT_MESSAGE;
  ev.window = window;
  ev.format = 32;
  ev.type = atom;

  assert(data_len <= (5 * sizeof(uint32_t)));

  memcpy(ev.data.data32, data, data_len);

  return xcb_send_event(c, 0, dest, XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                        (char *) &ev);
}

DO_REPLY_LIST_VALUES(windows, XCB_ATOM_WINDOW, xcb_window_t)
DO_REPLY_LIST_VALUES(atoms, XCB_ATOM_ATOM, xcb_atom_t)

/**
 * Atoms initialisation
 */

xcb_intern_atom_cookie_t *
xcb_ewmh_init_atoms(xcb_connection_t *c,
                    xcb_ewmh_connection_t *ewmh)
{
  int screen_nbr, atom_nbr;

  ewmh->connection = c;

  const xcb_setup_t *setup = xcb_get_setup(c);

  ewmh->nb_screens = xcb_setup_roots_length(setup);
  if(!ewmh->nb_screens)
    return NULL;

  /* Allocate the data structures depending of the number of screens */
  ewmh->screens = malloc(sizeof(xcb_screen_t *) * ewmh->nb_screens);
  ewmh->_NET_WM_CM_Sn = malloc(sizeof(xcb_atom_t) * ewmh->nb_screens);

  xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(setup);
  for(screen_iter = xcb_setup_roots_iterator(setup), screen_nbr = 0; screen_iter.rem;
      xcb_screen_next(&screen_iter))
    ewmh->screens[screen_nbr++] = screen_iter.data;

  /* _NET_WM_CM_Sn atoms  will be treated differently,  by adding them
     at the end  of this array, than other atoms as  it depends on the
     number of screens */
  xcb_intern_atom_cookie_t *ewmh_cookies = malloc(sizeof(xcb_intern_atom_cookie_t) *
                                                  (NB_EWMH_ATOMS + ewmh->nb_screens));

  /* First, send InternAtom request for all Atoms except _NET_WM_CM_Sn */
  for(atom_nbr = 0; atom_nbr < NB_EWMH_ATOMS; atom_nbr++)
    ewmh_cookies[atom_nbr] = xcb_intern_atom(ewmh->connection, 0,
                                             ewmh_atoms[atom_nbr].name_len,
                                             ewmh_atoms[atom_nbr].name);

  /* Then,  send  InternAtom requests  for  _NET_WM_CM_Sn and  compute
     _NET_WM_CM_Sn according to the screen number 'n' */
  for(screen_nbr = 0; screen_nbr < ewmh->nb_screens; screen_nbr++)
    {
      char wm_cm_sn[32];

      const int wm_cm_sn_len = snprintf(wm_cm_sn, 32, "_NET_WM_CM_S%d",
                                        screen_nbr);

      assert(wm_cm_sn_len > 0 && wm_cm_sn_len < 32);

      ewmh_cookies[atom_nbr++] = xcb_intern_atom(ewmh->connection, 0,
                                                 wm_cm_sn_len,
                                                 wm_cm_sn);
    }

  return ewmh_cookies;
}

uint8_t
xcb_ewmh_init_atoms_replies(xcb_ewmh_connection_t *ewmh,
                            xcb_intern_atom_cookie_t *ewmh_cookies,
                            xcb_generic_error_t **e)
{
  int atom_nbr;
  int screen_nbr = 0;
  uint8_t ret = 1;
  xcb_intern_atom_reply_t *reply;

  for(atom_nbr = 0; atom_nbr < NB_EWMH_ATOMS + ewmh->nb_screens; atom_nbr++)
    if((reply = xcb_intern_atom_reply(ewmh->connection, ewmh_cookies[atom_nbr], e)))
      {
        if(ret)
          {
            if(atom_nbr < NB_EWMH_ATOMS)
              *((xcb_atom_t *) (((char *) ewmh) + ewmh_atoms[atom_nbr].m_offset)) = reply->atom;
            else
              ewmh->_NET_WM_CM_Sn[screen_nbr++] = reply->atom;
          }

        free(reply);
      }
    else
      ret = 0;

  if(!ret)
    xcb_ewmh_connection_wipe(ewmh);

  free(ewmh_cookies);
  return ret;
}

/**
 * _NET_SUPPORTED
 */

DO_ROOT_LIST_VALUES(supported, _NET_SUPPORTED, XCB_ATOM_ATOM, xcb_atom_t)

/**
 * _NET_CLIENT_LIST
 * _NET_CLIENT_LIST_STACKING
 */

DO_ROOT_LIST_VALUES(client_list, _NET_CLIENT_LIST, XCB_ATOM_WINDOW, xcb_window_t)

DO_ROOT_LIST_VALUES(client_list_stacking, _NET_CLIENT_LIST_STACKING,
                    XCB_ATOM_WINDOW, xcb_window_t)

/**
 * _NET_NUMBER_OF_DESKTOPS
 */

DO_ROOT_SINGLE_VALUE(number_of_desktops, _NET_NUMBER_OF_DESKTOPS,
                     XCB_ATOM_CARDINAL, uint32_t)

/**
 * _NET_DESKTOP_GEOMETRY
 */

DO_GET_ROOT_PROPERTY(desktop_geometry, _NET_DESKTOP_GEOMETRY,
                     XCB_ATOM_CARDINAL, 2L)

xcb_void_cookie_t
xcb_ewmh_set_desktop_geometry(xcb_ewmh_connection_t *ewmh, int screen_nbr,
                              uint32_t new_width, uint32_t new_height)
{
  const uint32_t data[] = { new_width, new_height };

  return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE,
                             ewmh->screens[screen_nbr]->root,
                             ewmh->_NET_DESKTOP_GEOMETRY, XCB_ATOM_CARDINAL,
                             32, 2, data);
}

xcb_void_cookie_t
xcb_ewmh_set_desktop_geometry_checked(xcb_ewmh_connection_t *ewmh,
                                      int screen_nbr, uint32_t new_width,
                                      uint32_t new_height)
{
  const uint32_t data[] = { new_width, new_height };

  return xcb_change_property_checked(ewmh->connection, XCB_PROP_MODE_REPLACE,
                                     ewmh->screens[screen_nbr]->root,
                                     ewmh->_NET_DESKTOP_GEOMETRY,
                                     XCB_ATOM_CARDINAL, 32, 2, data);
}

xcb_void_cookie_t
xcb_ewmh_request_change_desktop_geometry(xcb_ewmh_connection_t *ewmh,
                                         int screen_nbr, uint32_t new_width,
                                         uint32_t new_height)
{
  const uint32_t data[] = { new_width, new_height };

  return xcb_ewmh_send_client_message(ewmh->connection,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->_NET_DESKTOP_GEOMETRY,
                                      sizeof(data), data);
}

uint8_t
xcb_ewmh_get_desktop_geometry_from_reply(uint32_t *width, uint32_t *height,
                                         xcb_get_property_reply_t *r)
{
  if(!r || r->type != XCB_ATOM_CARDINAL || r->format != 32 ||
     xcb_get_property_value_length(r) != (sizeof(uint32_t) * 2))
    return 0;

  uint32_t *value = (uint32_t *) xcb_get_property_value(r);

  *width = value[0];
  *height = value[1];

  return 1;
}

uint8_t
xcb_ewmh_get_desktop_geometry_reply(xcb_ewmh_connection_t *ewmh,
                                    xcb_get_property_cookie_t cookie,
                                    uint32_t *width, uint32_t *height,
                                    xcb_generic_error_t **e)
{
  xcb_get_property_reply_t *r = xcb_get_property_reply(ewmh->connection, cookie, e);
  const uint8_t ret = xcb_ewmh_get_desktop_geometry_from_reply(width, height, r);
  free(r);
  return ret;
}

/**
 * _NET_DESKTOP_VIEWPORT
 */

DO_ROOT_LIST_VALUES(desktop_viewport, _NET_DESKTOP_VIEWPORT, XCB_ATOM_CARDINAL,
                    xcb_ewmh_coordinates_t)

DO_REPLY_LIST_VALUES(desktop_viewport, XCB_ATOM_CARDINAL,
                     xcb_ewmh_coordinates_t)

xcb_void_cookie_t
xcb_ewmh_request_change_desktop_viewport(xcb_ewmh_connection_t *ewmh,
                                         int screen_nbr, uint32_t x,
                                         uint32_t y)
{
  const uint32_t data[] = { x, y };

  return xcb_ewmh_send_client_message(ewmh->connection,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->_NET_DESKTOP_VIEWPORT,
                                      sizeof(data), data);
}

/**
 * _NET_CURRENT_DESKTOP
 */

DO_ROOT_SINGLE_VALUE(current_desktop, _NET_CURRENT_DESKTOP, XCB_ATOM_CARDINAL,
                     uint32_t)

xcb_void_cookie_t
xcb_ewmh_request_change_current_desktop(xcb_ewmh_connection_t *ewmh,
                                        int screen_nbr, uint32_t new_desktop,
                                        xcb_timestamp_t timestamp)
{
  const uint32_t data[] = { new_desktop, timestamp };

  return xcb_ewmh_send_client_message(ewmh->connection,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->_NET_CURRENT_DESKTOP,
                                      sizeof(data), data);
}

/**
 * _NET_DESKTOP_NAMES
 */
DO_ROOT_UTF8_STRING(desktop_names, _NET_DESKTOP_NAMES)

/**
 * _NET_ACTIVE_WINDOW
 */

DO_ROOT_SINGLE_VALUE(active_window, _NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW,
                     xcb_window_t)

xcb_void_cookie_t
xcb_ewmh_request_change_active_window(xcb_ewmh_connection_t *ewmh,
                                      int screen_nbr,
                                      xcb_window_t window_to_activate,
                                      xcb_ewmh_client_source_type_t source_indication,
                                      xcb_timestamp_t timestamp,
                                      xcb_window_t current_active_window)
{
  const uint32_t data[] = { source_indication, timestamp, current_active_window };

  return xcb_ewmh_send_client_message(ewmh->connection, window_to_activate,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->_NET_ACTIVE_WINDOW, sizeof(data),
                                      data);
}

/**
 * _NET_WORKAREA
 */

DO_ROOT_LIST_VALUES(workarea, _NET_WORKAREA, XCB_ATOM_CARDINAL,
                    xcb_ewmh_geometry_t)

DO_REPLY_LIST_VALUES(workarea, XCB_ATOM_CARDINAL, xcb_ewmh_geometry_t)

/**
 * _NET_SUPPORTING_WM_CHECK
 */

DO_SINGLE_VALUE(supporting_wm_check, _NET_SUPPORTING_WM_CHECK,
                XCB_ATOM_WINDOW, xcb_window_t)

/**
 * _NET_VIRTUAL_ROOTS
 */

DO_ROOT_LIST_VALUES(virtual_roots, _NET_VIRTUAL_ROOTS, XCB_ATOM_WINDOW,
                    xcb_window_t)

/**
 * _NET_DESKTOP_LAYOUT
 */

DO_GET_ROOT_PROPERTY(desktop_layout, _NET_DESKTOP_LAYOUT, XCB_ATOM_CARDINAL, 4)
DO_REPLY_STRUCTURE(desktop_layout, xcb_ewmh_get_desktop_layout_reply_t)

xcb_void_cookie_t
xcb_ewmh_set_desktop_layout(xcb_ewmh_connection_t *ewmh, int screen_nbr,
                            xcb_ewmh_desktop_layout_orientation_t orientation,
                            uint32_t columns, uint32_t rows,
                            xcb_ewmh_desktop_layout_starting_corner_t starting_corner)
{
  const uint32_t data[] = { orientation, columns, rows, starting_corner };

  return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE,
                             ewmh->screens[screen_nbr]->root,
                             ewmh->_NET_DESKTOP_LAYOUT, XCB_ATOM_CARDINAL, 32,
                             countof(data), data);
}

xcb_void_cookie_t
xcb_ewmh_set_desktop_layout_checked(xcb_ewmh_connection_t *ewmh, int screen_nbr,
                                    xcb_ewmh_desktop_layout_orientation_t orientation,
                                    uint32_t columns, uint32_t rows,
                                    xcb_ewmh_desktop_layout_starting_corner_t starting_corner)
{
  const uint32_t data[] = { orientation, columns, rows, starting_corner };

  return xcb_change_property_checked(ewmh->connection, XCB_PROP_MODE_REPLACE,
                                     ewmh->screens[screen_nbr]->root,
                                     ewmh->_NET_DESKTOP_LAYOUT,
                                     XCB_ATOM_CARDINAL, 32, countof(data),
                                     data);
}

/**
 * _NET_SHOWING_DESKTOP
 */

DO_ROOT_SINGLE_VALUE(showing_desktop, _NET_SHOWING_DESKTOP, XCB_ATOM_CARDINAL,
                     uint32_t)

/**
 * _NET_CLOSE_WINDOW
 */

xcb_void_cookie_t
xcb_ewmh_request_close_window(xcb_ewmh_connection_t *ewmh, int screen_nbr,
                              xcb_window_t window_to_close,
                              xcb_timestamp_t timestamp,
                              xcb_ewmh_client_source_type_t source_indication)
{
  const uint32_t data[] = { timestamp, source_indication };

  return xcb_ewmh_send_client_message(ewmh->connection, window_to_close,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->_NET_CLOSE_WINDOW, sizeof(data),
                                      data);
}

/**
 * _NET_MOVERESIZE_WINDOW
 */

/* x, y, width, height may be equal to -1 */
xcb_void_cookie_t
xcb_ewmh_request_moveresize_window(xcb_ewmh_connection_t *ewmh, int screen_nbr,
                                   xcb_window_t moveresize_window,
                                   xcb_gravity_t gravity,
                                   xcb_ewmh_client_source_type_t source_indication,
                                   xcb_ewmh_moveresize_window_opt_flags_t flags,
                                   uint32_t x, uint32_t y,
                                   uint32_t width, uint32_t height)
{
  const uint32_t data[] = { (gravity | flags | source_indication << 12),
                            x, y, width, height };

  return xcb_ewmh_send_client_message(ewmh->connection, moveresize_window,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->_NET_MOVERESIZE_WINDOW,
                                      sizeof(data), data);
}

/**
 * _NET_WM_MOVERESIZE
 */

xcb_void_cookie_t
xcb_ewmh_request_wm_moveresize(xcb_ewmh_connection_t *ewmh, int screen_nbr,
                               xcb_window_t moveresize_window,
                               uint32_t x_root, uint32_t y_root,
                               xcb_ewmh_moveresize_direction_t direction,
                               xcb_button_index_t button,
                               xcb_ewmh_client_source_type_t source_indication)
{
  const uint32_t data[] = { x_root, y_root, direction, button, source_indication };

  return xcb_ewmh_send_client_message(ewmh->connection, moveresize_window,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->_NET_WM_MOVERESIZE, sizeof(data),
                                      data);
}

/**
 * _NET_RESTACK_WINDOW
 */

xcb_void_cookie_t
xcb_ewmh_request_restack_window(xcb_ewmh_connection_t *ewmh, int screen_nbr,
                                xcb_window_t window_to_restack,
                                xcb_window_t sibling_window,
                                xcb_stack_mode_t detail)
{
  const uint32_t data[] = { XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER, sibling_window,
                            detail };

  return xcb_ewmh_send_client_message(ewmh->connection, window_to_restack,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->_NET_RESTACK_WINDOW, sizeof(data),
                                      data);
}

/**
 * _NET_WM_NAME
 */

DO_UTF8_STRING(wm_name, _NET_WM_NAME)

/**
 * _NET_WM_VISIBLE_NAME
 */

DO_UTF8_STRING(wm_visible_name, _NET_WM_VISIBLE_NAME)

/**
 * _NET_WM_ICON_NAME
 */

DO_UTF8_STRING(wm_icon_name, _NET_WM_ICON_NAME)

/**
 * _NET_WM_VISIBLE_ICON_NAME
 */

DO_UTF8_STRING(wm_visible_icon_name, _NET_WM_VISIBLE_ICON_NAME)

/**
 * _NET_WM_DESKTOP
 */

DO_SINGLE_VALUE(wm_desktop, _NET_WM_DESKTOP, XCB_ATOM_CARDINAL, uint32_t)

xcb_void_cookie_t
xcb_ewmh_request_change_wm_desktop(xcb_ewmh_connection_t *ewmh, int screen_nbr,
                                   xcb_window_t client_window,
                                   uint32_t new_desktop,
                                   xcb_ewmh_client_source_type_t source_indication)
{
  const uint32_t data[] = { new_desktop, source_indication };

  return xcb_ewmh_send_client_message(ewmh->connection, client_window,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->_NET_WM_DESKTOP, sizeof(data),
                                      data);
}

/**
 * _NET_WM_WINDOW_TYPE
 *
 * TODO: check possible atoms?
 */

DO_LIST_VALUES(wm_window_type, _NET_WM_WINDOW_TYPE, XCB_ATOM_ATOM, atom)

/**
 * _NET_WM_STATE
 *
 * TODO: check possible atoms?
 */

DO_LIST_VALUES(wm_state, _NET_WM_STATE, XCB_ATOM_ATOM, atom)

xcb_void_cookie_t
xcb_ewmh_request_change_wm_state(xcb_ewmh_connection_t *ewmh, int screen_nbr,
                                 xcb_window_t client_window,
                                 xcb_ewmh_wm_state_action_t action,
                                 xcb_atom_t first_property,
                                 xcb_atom_t second_property,
                                 xcb_ewmh_client_source_type_t source_indication)
{
  const uint32_t data[] = { action, first_property, second_property,
                            source_indication };

  return xcb_ewmh_send_client_message(ewmh->connection, client_window,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->_NET_WM_STATE, sizeof(data), data);
}

/**
 * _NET_WM_ALLOWED_ACTIONS
 *
 * TODO: check possible atoms?
 */

DO_LIST_VALUES(wm_allowed_actions, _NET_WM_ALLOWED_ACTIONS, XCB_ATOM_ATOM, atom)

/**
 * _NET_WM_STRUT
 */

xcb_void_cookie_t
xcb_ewmh_set_wm_strut(xcb_ewmh_connection_t *ewmh,
                      xcb_window_t window,
                      uint32_t left, uint32_t right,
                      uint32_t top, uint32_t bottom)
{
  const uint32_t data[] = { left, right, top, bottom };

  return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE, window,
                             ewmh->_NET_WM_STRUT, XCB_ATOM_CARDINAL, 32,
                             countof(data), data);
}

xcb_void_cookie_t
xcb_ewmh_set_wm_strut_checked(xcb_ewmh_connection_t *ewmh,
                              xcb_window_t window,
                              uint32_t left, uint32_t right,
                              uint32_t top, uint32_t bottom)
{
  const uint32_t data[] = { left, right, top, bottom };

  return xcb_change_property_checked(ewmh->connection, XCB_PROP_MODE_REPLACE,
                                     window, ewmh->_NET_WM_STRUT,
                                     XCB_ATOM_CARDINAL, 32, countof(data),
                                     data);
}

DO_GET_PROPERTY(wm_strut, _NET_WM_STRUT, XCB_ATOM_CARDINAL, 4)
DO_REPLY_STRUCTURE(wm_strut, xcb_ewmh_get_extents_reply_t)

/*
 * _NET_WM_STRUT_PARTIAL
 */

xcb_void_cookie_t
xcb_ewmh_set_wm_strut_partial(xcb_ewmh_connection_t *ewmh,
                              xcb_window_t window,
                              xcb_ewmh_wm_strut_partial_t wm_strut)
{
  return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE, window,
                             ewmh->_NET_WM_STRUT_PARTIAL, XCB_ATOM_CARDINAL, 32,
                             12, &wm_strut);
}

xcb_void_cookie_t
xcb_ewmh_set_wm_strut_partial_checked(xcb_ewmh_connection_t *ewmh,
                                      xcb_window_t window,
                                      xcb_ewmh_wm_strut_partial_t wm_strut)
{
  return xcb_change_property_checked(ewmh->connection, XCB_PROP_MODE_REPLACE,
                                     window, ewmh->_NET_WM_STRUT_PARTIAL,
                                     XCB_ATOM_CARDINAL, 32, 12, &wm_strut);
}

DO_GET_PROPERTY(wm_strut_partial, _NET_WM_STRUT_PARTIAL, XCB_ATOM_CARDINAL, 12)
DO_REPLY_STRUCTURE(wm_strut_partial, xcb_ewmh_wm_strut_partial_t)

/**
 * _NET_WM_ICON_GEOMETRY
 */

xcb_void_cookie_t
xcb_ewmh_set_wm_icon_geometry_checked(xcb_ewmh_connection_t *ewmh,
                                      xcb_window_t window,
                                      uint32_t left, uint32_t right,
                                      uint32_t top, uint32_t bottom)
{
  const uint32_t data[] = { left, right, top, bottom };

  return xcb_change_property_checked(ewmh->connection, XCB_PROP_MODE_REPLACE,
                                     window, ewmh->_NET_WM_ICON_GEOMETRY,
                                     XCB_ATOM_CARDINAL, 32, countof(data),
                                     data);
}

xcb_void_cookie_t
xcb_ewmh_set_wm_icon_geometry(xcb_ewmh_connection_t *ewmh,
                              xcb_window_t window,
                              uint32_t left, uint32_t right,
                              uint32_t top, uint32_t bottom)
{
  const uint32_t data[] = { left, right, top, bottom };

  return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE, window,
                             ewmh->_NET_WM_ICON_GEOMETRY, XCB_ATOM_CARDINAL, 32,
                             countof(data), data);
}

DO_GET_PROPERTY(wm_icon_geometry, _NET_WM_ICON_GEOMETRY, XCB_ATOM_CARDINAL, 4)
DO_REPLY_STRUCTURE(wm_icon_geometry, xcb_ewmh_geometry_t)

/**
 * _NET_WM_ICON
 */

static inline void
set_wm_icon_data(uint32_t data[], uint32_t width, uint32_t height,
                 uint32_t img_len, uint32_t *img)
{
  data[0] = width;
  data[1] = height;

  memcpy(data + 2, img, img_len);
}

xcb_void_cookie_t
xcb_ewmh_append_wm_icon_checked(xcb_ewmh_connection_t *ewmh,
                                xcb_window_t window,
                                uint32_t width, uint32_t height,
                                uint32_t img_len, uint32_t *img)
{
  const uint32_t data_len = img_len + 2;
  uint32_t *data=(uint32_t*)_alloca(data_len);

  set_wm_icon_data(data, width, height, img_len, img);

  return xcb_ewmh_set_wm_icon_checked(ewmh, XCB_PROP_MODE_APPEND, window,
                                      data_len, data);
}

xcb_void_cookie_t
xcb_ewmh_append_wm_icon(xcb_ewmh_connection_t *ewmh,
                        xcb_window_t window,
                        uint32_t width, uint32_t height,
                        uint32_t img_len, uint32_t *img)
{
  const uint32_t data_len = img_len + 2;
  uint32_t *data=(uint32_t*)_alloca(data_len);

  set_wm_icon_data(data, width, height, img_len, img);

  return xcb_ewmh_set_wm_icon(ewmh, XCB_PROP_MODE_APPEND, window,
                              data_len, data);
}

DO_GET_PROPERTY(wm_icon, _NET_WM_ICON, XCB_ATOM_CARDINAL, UINT_MAX)

uint8_t
xcb_ewmh_get_wm_icon_from_reply(xcb_ewmh_get_wm_icon_reply_t *wm_icon,
                                xcb_get_property_reply_t *r)
{
  if(!r || r->type != XCB_ATOM_CARDINAL || r->format != 32)
    return 0;

  uint32_t r_value_len = xcb_get_property_value_length(r);
  uint32_t *r_value = (uint32_t *) xcb_get_property_value(r);

  /* Find the number of icons in the reply. */
  wm_icon->num_icons = 0;
  while(r_value_len > (sizeof(uint32_t) * 2) && r_value && r_value[0] && r_value[1])
  {
    /* Check that the property is as long as it should be (in bytes),
       handling integer overflow. "+2" to handle the width and height
       fields. */
    const uint64_t expected_len = (r_value[0] * (uint64_t) r_value[1] + 2) * 4;
    if(expected_len > r_value_len)
      break;

    wm_icon->num_icons++;

    /* Find pointer to next icon in the reply. */
    r_value_len -= expected_len;
    r_value = (uint32_t *) (((uint8_t *) r_value) + expected_len);
  }

  if(!wm_icon->num_icons)
    return 0;

  wm_icon->_reply = r;

  return 1;
}

uint8_t
xcb_ewmh_get_wm_icon_reply(xcb_ewmh_connection_t *ewmh,
                           xcb_get_property_cookie_t cookie,
                           xcb_ewmh_get_wm_icon_reply_t *wm_icon,
                           xcb_generic_error_t **e)
{
  xcb_get_property_reply_t *r = xcb_get_property_reply(ewmh->connection, cookie, e);
  const uint8_t ret = xcb_ewmh_get_wm_icon_from_reply(wm_icon, r);
  if(!ret)
    free(r);

  return ret;
}

void
xcb_ewmh_get_wm_icon_reply_wipe(xcb_ewmh_get_wm_icon_reply_t *wm_icon)
{
  free(wm_icon->_reply);
}

xcb_ewmh_wm_icon_iterator_t
xcb_ewmh_get_wm_icon_iterator(const xcb_ewmh_get_wm_icon_reply_t *wm_icon)
{
  xcb_ewmh_wm_icon_iterator_t ret;

  ret.width = 0;
  ret.height = 0;
  ret.data = NULL;
  ret.rem = wm_icon->num_icons;
  ret.index = 0;

  if(ret.rem > 0)
  {
    uint32_t *r_value = (uint32_t *) xcb_get_property_value(wm_icon->_reply);
    ret.width = r_value[0];
    ret.height = r_value[1];
    ret.data = &r_value[2];
  }

  return ret;
}

unsigned int xcb_ewmh_get_wm_icon_length(const xcb_ewmh_get_wm_icon_reply_t *wm_icon)
{
  return wm_icon->num_icons;
}

void xcb_ewmh_get_wm_icon_next(xcb_ewmh_wm_icon_iterator_t *iterator)
{
  if(iterator->rem <= 1)
  {
    iterator->index += iterator->rem;
    iterator->rem = 0;
    iterator->width = 0;
    iterator->height = 0;
    iterator->data = NULL;
    return;
  }

  uint64_t icon_len = iterator->width * (uint64_t) iterator->height;
  uint32_t *data = iterator->data + icon_len;

  iterator->rem--;
  iterator->index++;
  iterator->width = data[0];
  iterator->height = data[1];
  iterator->data = &data[2];
}

/**
 * _NET_WM_PID
 */

DO_SINGLE_VALUE(wm_pid, _NET_WM_PID, XCB_ATOM_CARDINAL, uint32_t)

/**
 * _NET_WM_HANDLED_ICONS
 */

DO_SINGLE_VALUE(wm_handled_icons, _NET_WM_HANDLED_ICONS, XCB_ATOM_CARDINAL,
                uint32_t)

/**
 * _NET_WM_USER_TIME
 */

DO_SINGLE_VALUE(wm_user_time, _NET_WM_USER_TIME, XCB_ATOM_CARDINAL, uint32_t)

/**
 * _NET_WM_USER_TIME_WINDOW
 */

DO_SINGLE_VALUE(wm_user_time_window, _NET_WM_USER_TIME_WINDOW, XCB_ATOM_CARDINAL,
                uint32_t)

/**
 * _NET_FRAME_EXTENTS
 */

xcb_void_cookie_t
xcb_ewmh_set_frame_extents(xcb_ewmh_connection_t *ewmh,
                           xcb_window_t window,
                           uint32_t left, uint32_t right,
                           uint32_t top, uint32_t bottom)
{
  const uint32_t data[] = { left, right, top, bottom };

  return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE, window,
                             ewmh->_NET_FRAME_EXTENTS, XCB_ATOM_CARDINAL, 32,
                             countof(data), data);
}

xcb_void_cookie_t
xcb_ewmh_set_frame_extents_checked(xcb_ewmh_connection_t *ewmh,
                                   xcb_window_t window,
                                   uint32_t left, uint32_t right,
                                   uint32_t top, uint32_t bottom)
{
  const uint32_t data[] = { left, right, top, bottom };

  return xcb_change_property_checked(ewmh->connection, XCB_PROP_MODE_REPLACE,
                                     window, ewmh->_NET_FRAME_EXTENTS,
                                     XCB_ATOM_CARDINAL, 32, countof(data),
                                     data);
}

DO_GET_PROPERTY(frame_extents, _NET_FRAME_EXTENTS, XCB_ATOM_CARDINAL, 4)
DO_REPLY_STRUCTURE(frame_extents, xcb_ewmh_get_extents_reply_t)

/**
 * _NET_WM_PING
 *
 * TODO: client resend function?
 */

xcb_void_cookie_t
xcb_ewmh_send_wm_ping(xcb_ewmh_connection_t *ewmh,
                      xcb_window_t window,
                      xcb_timestamp_t timestamp)
{
  const uint32_t data[] = { ewmh->_NET_WM_PING, timestamp, window };

  return xcb_ewmh_send_client_message(ewmh->connection, window, window,
                                      ewmh->WM_PROTOCOLS, sizeof(data), data);
}

/**
 * _NET_WM_SYNC_REQUEST
 * _NET_WM_SYNC_REQUEST_COUNTER
 */

xcb_void_cookie_t
xcb_ewmh_set_wm_sync_request_counter(xcb_ewmh_connection_t *ewmh,
                                     xcb_window_t window,
                                     xcb_atom_t wm_sync_request_counter_atom,
                                     uint32_t low, uint32_t high)
{
  const uint32_t data[] = { low, high };

  return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE, window,
                             ewmh->_NET_WM_SYNC_REQUEST_COUNTER,
                             XCB_ATOM_CARDINAL, 32,
                             countof(data), data);
}

xcb_void_cookie_t
xcb_ewmh_set_wm_sync_request_counter_checked(xcb_ewmh_connection_t *ewmh,
                                             xcb_window_t window,
                                             xcb_atom_t wm_sync_request_counter_atom,
                                             uint32_t low, uint32_t high)
{
  const uint32_t data[] = { low, high };

  return xcb_change_property_checked(ewmh->connection, XCB_PROP_MODE_REPLACE,
                                     window, ewmh->_NET_WM_SYNC_REQUEST_COUNTER,
                                     XCB_ATOM_CARDINAL, 32, countof(data),
                                     data);
}

DO_GET_PROPERTY(wm_sync_request_counter, _NET_WM_SYNC_REQUEST_COUNTER,
                XCB_ATOM_CARDINAL, 2)

uint8_t
xcb_ewmh_get_wm_sync_request_counter_from_reply(uint64_t *counter,
                                                xcb_get_property_reply_t *r)
{
  /* 2 cardinals? */
  if(!r || r->type != XCB_ATOM_CARDINAL || r->format != 32 ||
     xcb_get_property_value_length(r) != sizeof(uint64_t))
    return 0;

  uint32_t *r_value = (uint32_t *) xcb_get_property_value(r);
  *counter = (r_value[0] | ((uint64_t) r_value[1]) << 32);

  return 1;
}

uint8_t
xcb_ewmh_get_wm_sync_request_counter_reply(xcb_ewmh_connection_t *ewmh,
                                           xcb_get_property_cookie_t cookie,
                                           uint64_t *counter,
                                           xcb_generic_error_t **e)
{
  xcb_get_property_reply_t *r = xcb_get_property_reply(ewmh->connection, cookie, e);
  const uint8_t ret = xcb_ewmh_get_wm_sync_request_counter_from_reply(counter, r);
  free(r);
  return ret;
}

xcb_void_cookie_t
xcb_ewmh_send_wm_sync_request(xcb_ewmh_connection_t *ewmh,
                              xcb_window_t window,
                              xcb_atom_t wm_protocols_atom,
                              xcb_atom_t wm_sync_request_atom,
                              xcb_timestamp_t timestamp,
                              uint64_t counter)
{
  const uint32_t data[] = { ewmh->_NET_WM_SYNC_REQUEST, timestamp, counter,
                            counter >> 32 };

  return xcb_ewmh_send_client_message(ewmh->connection, window, window,
                                      ewmh->WM_PROTOCOLS, sizeof(data), data);
}

/**
 * _NET_WM_FULLSCREEN_MONITORS
 */

xcb_void_cookie_t
xcb_ewmh_set_wm_fullscreen_monitors(xcb_ewmh_connection_t *ewmh,
                                    xcb_window_t window,
                                    uint32_t top, uint32_t bottom,
                                    uint32_t left, uint32_t right)
{
  const uint32_t data[] = { top, bottom, left, right };

  return xcb_change_property(ewmh->connection, XCB_PROP_MODE_REPLACE, window,
                             ewmh->_NET_WM_FULLSCREEN_MONITORS,
                             XCB_ATOM_CARDINAL, 32, countof(data), data);
}

xcb_void_cookie_t
xcb_ewmh_set_wm_fullscreen_monitors_checked(xcb_ewmh_connection_t *ewmh,
                                            xcb_window_t window,
                                            uint32_t top, uint32_t bottom,
                                            uint32_t left, uint32_t right)
{
  const uint32_t data[] = { top, bottom, left, right };

  return xcb_change_property_checked(ewmh->connection, XCB_PROP_MODE_REPLACE,
                                     window, ewmh->_NET_WM_FULLSCREEN_MONITORS,
                                     XCB_ATOM_CARDINAL, 32, countof(data),
                                     data);
}

DO_GET_PROPERTY(wm_fullscreen_monitors, _NET_WM_FULLSCREEN_MONITORS,
                XCB_ATOM_CARDINAL, 4)

DO_REPLY_STRUCTURE(wm_fullscreen_monitors,
                   xcb_ewmh_get_wm_fullscreen_monitors_reply_t)

xcb_void_cookie_t
xcb_ewmh_request_change_wm_fullscreen_monitors(xcb_ewmh_connection_t *ewmh,
                                               int screen_nbr,
                                               xcb_window_t window,
                                               uint32_t top, uint32_t bottom,
                                               uint32_t left, uint32_t right,
                                               xcb_ewmh_client_source_type_t source_indication)
{
  const uint32_t data[] = { top, bottom, left, right, source_indication };

  return xcb_ewmh_send_client_message(ewmh->connection, window,
                                      ewmh->screens[screen_nbr]->root,
                                      ewmh->_NET_WM_FULLSCREEN_MONITORS,
                                      sizeof(data), data);
}

/**
 * _NET_WM_FULL_PLACEMENT
 */

/**
 * _NET_WM_CM_Sn
 */

xcb_get_selection_owner_cookie_t
xcb_ewmh_get_wm_cm_owner(xcb_ewmh_connection_t *ewmh,
                         int screen_nbr)
{
  return xcb_get_selection_owner(ewmh->connection,
                                 ewmh->_NET_WM_CM_Sn[screen_nbr]);
}

xcb_get_selection_owner_cookie_t
xcb_ewmh_get_wm_cm_owner_unchecked(xcb_ewmh_connection_t *ewmh,
                                   int screen_nbr)
{
  return xcb_get_selection_owner_unchecked(ewmh->connection,
                                           ewmh->_NET_WM_CM_Sn[screen_nbr]);
}

uint8_t
xcb_ewmh_get_wm_cm_owner_from_reply(xcb_window_t *owner,
                                    xcb_get_selection_owner_reply_t *r)
{
  if(!r)
    return 0;

  *owner = r->owner;
  free(r);
  return 1;
}

uint8_t
xcb_ewmh_get_wm_cm_owner_reply(xcb_ewmh_connection_t *ewmh,
                               xcb_get_selection_owner_cookie_t cookie,
                               xcb_window_t *owner,
                               xcb_generic_error_t **e)
{
  xcb_get_selection_owner_reply_t *r =
    xcb_get_selection_owner_reply(ewmh->connection, cookie, e);

  return xcb_ewmh_get_wm_cm_owner_from_reply(owner, r);
}

/* TODO: section 2.1, 2.2 */
static xcb_void_cookie_t
set_wm_cm_owner_client_message(xcb_ewmh_connection_t *ewmh,
                               int screen_nbr,
                               xcb_window_t owner,
                               xcb_timestamp_t timestamp,
                               uint32_t selection_data1,
                               uint32_t selection_data2)
{
  xcb_client_message_event_t ev;
  memset(&ev, 0, sizeof(xcb_client_message_event_t));

  ev.response_type = XCB_CLIENT_MESSAGE;
  ev.format = 32;
  ev.type = ewmh->MANAGER;
  ev.data.data32[0] = timestamp;
  ev.data.data32[1] = ewmh->_NET_WM_CM_Sn[screen_nbr];
  ev.data.data32[2] = owner;
  ev.data.data32[3] = selection_data1;
  ev.data.data32[4] = selection_data2;

  return xcb_send_event(ewmh->connection, 0, ewmh->screens[screen_nbr]->root,
                        XCB_EVENT_MASK_STRUCTURE_NOTIFY,
                        (char *) &ev);
}

/* TODO: check both */
xcb_void_cookie_t
xcb_ewmh_set_wm_cm_owner(xcb_ewmh_connection_t *ewmh,
                         int screen_nbr,
                         xcb_window_t owner,
                         xcb_timestamp_t timestamp,
                         uint32_t selection_data1,
                         uint32_t selection_data2)
{
  xcb_set_selection_owner(ewmh->connection, owner,
                          ewmh->_NET_WM_CM_Sn[screen_nbr], 0);

  return set_wm_cm_owner_client_message(ewmh, screen_nbr, owner, timestamp,
                                        selection_data1, selection_data2);
}

xcb_void_cookie_t
xcb_ewmh_set_wm_cm_owner_checked(xcb_ewmh_connection_t *ewmh,
                                 int screen_nbr,
                                 xcb_window_t owner,
                                 xcb_timestamp_t timestamp,
                                 uint32_t selection_data1,
                                 uint32_t selection_data2)
{
  xcb_set_selection_owner_checked(ewmh->connection, owner,
                                  ewmh->_NET_WM_CM_Sn[screen_nbr], 0);

  return set_wm_cm_owner_client_message(ewmh, screen_nbr, owner, timestamp,
                                        selection_data1, selection_data2);
}
