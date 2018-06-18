/*
 * Copyright 2007 Kim woelders
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */
#include "config.h"

#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include <stdlib.h>
#include <string.h>

#include "clientwin.h"
#include "dsimple.h"

static xcb_atom_t atom_wm_state = XCB_ATOM_NONE;

/*
 * Check if window has given property
 */
static Bool
Window_Has_Property(xcb_connection_t * dpy, xcb_window_t win, xcb_atom_t atom)
{
    xcb_get_property_cookie_t prop_cookie;
    xcb_get_property_reply_t *prop_reply;

    prop_cookie = xcb_get_property (dpy, False, win, atom,
                                    XCB_GET_PROPERTY_TYPE_ANY, 0, 0);

    prop_reply = xcb_get_property_reply (dpy, prop_cookie, NULL);

    if (prop_reply) {
        xcb_atom_t reply_type = prop_reply->type;
        free (prop_reply);
        if (reply_type != XCB_NONE)
            return True;
    }

    return False;
}

/*
 * Check if window is viewable
 */
static Bool
Window_Is_Viewable(xcb_connection_t * dpy, xcb_window_t win)
{
    Bool ok = False;
    xcb_get_window_attributes_cookie_t attr_cookie;
    xcb_get_window_attributes_reply_t *xwa;

    attr_cookie = xcb_get_window_attributes (dpy, win);
    xwa = xcb_get_window_attributes_reply (dpy, attr_cookie, NULL);

    if (xwa) {
        ok = (xwa->_class == XCB_WINDOW_CLASS_INPUT_OUTPUT) &&
            (xwa->map_state == XCB_MAP_STATE_VIEWABLE);
        free (xwa);
    }

    return ok;
}

/*
 * Find a window that has WM_STATE set in the window tree below win.
 * Unmapped/unviewable windows are not considered valid matches.
 * Children are searched in top-down stacking order.
 * The first matching window is returned, None if no match is found.
 */
static xcb_window_t
Find_Client_In_Children(xcb_connection_t * dpy, xcb_window_t win)
{
    xcb_query_tree_cookie_t qt_cookie;
    xcb_query_tree_reply_t *tree;
    xcb_window_t *children;
    unsigned int n_children;
    int i;

    qt_cookie = xcb_query_tree (dpy, win);
    tree = xcb_query_tree_reply (dpy, qt_cookie, NULL);
    if (!tree)
        return XCB_WINDOW_NONE;
    n_children = xcb_query_tree_children_length (tree);
    if (!n_children) {
        free (tree);
        return XCB_WINDOW_NONE;
    }
    children = xcb_query_tree_children (tree);

    /* Check each child for WM_STATE and other validity */
    win = XCB_WINDOW_NONE;
    for (i = (int) n_children - 1; i >= 0; i--) {
        if (!Window_Is_Viewable(dpy, children[i])) {
            /* Don't bother descending into this one */
            children[i] = XCB_WINDOW_NONE;
            continue;
        }
        if (!Window_Has_Property(dpy, children[i], atom_wm_state))
            continue;

        /* Got one */
        win = children[i];
        goto done;
    }

    /* No children matched, now descend into each child */
    for (i = (int) n_children - 1; i >= 0; i--) {
        if (children[i] == XCB_WINDOW_NONE)
            continue;
        win = Find_Client_In_Children(dpy, children[i]);
        if (win != XCB_WINDOW_NONE)
            break;
    }

  done:
    free (tree); /* includes children */

    return win;
}

/*
 * Find virtual roots (_NET_VIRTUAL_ROOTS)
 */
static xcb_window_t *
Find_Roots(xcb_connection_t * dpy, xcb_window_t root, unsigned int *num)
{
    xcb_atom_t atom_virtual_root;

    xcb_get_property_cookie_t prop_cookie;
    xcb_get_property_reply_t *prop_reply;

    xcb_window_t *prop_ret = NULL;

    *num = 0;

    atom_virtual_root = Get_Atom (dpy, "_NET_VIRTUAL_ROOTS");
    if (atom_virtual_root == XCB_ATOM_NONE)
        return NULL;

    prop_cookie = xcb_get_property (dpy, False, root, atom_virtual_root,
                                    XCB_ATOM_WINDOW, 0, 0x7fffffff);
    prop_reply = xcb_get_property_reply (dpy, prop_cookie, NULL);
    if (!prop_reply)
        return NULL;

    if ((prop_reply->value_len > 0) && (prop_reply->type == XCB_ATOM_WINDOW)
        && (prop_reply->format == 32)) {
        int length = xcb_get_property_value_length (prop_reply);
        prop_ret = malloc(length);
        if (prop_ret) {
            memcpy (prop_ret, xcb_get_property_value(prop_reply), length);
            *num = prop_reply->value_len;
        }
    }
    free (prop_reply);

    return prop_ret;
}

/*
 * Find child window at pointer location
 */
static xcb_window_t
Find_Child_At_Pointer(xcb_connection_t * dpy, xcb_window_t win)
{
    xcb_window_t child_return = XCB_WINDOW_NONE;

    xcb_query_pointer_cookie_t qp_cookie;
    xcb_query_pointer_reply_t *qp_reply;

    qp_cookie = xcb_query_pointer (dpy, win);
    qp_reply = xcb_query_pointer_reply (dpy, qp_cookie, NULL);

    if (qp_reply) {
        child_return = qp_reply->child;
        free (qp_reply);
    }

    return child_return;
}

/*
 * Find client window at pointer location
 *
 * root   is the root window.
 * subwin is the subwindow reported by a ButtonPress event on root.
 *
 * If the WM uses virtual roots subwin may be a virtual root.
 * If so, we descend the window stack at the pointer location and assume the
 * child is the client or one of its WM frame windows.
 * This will of course work only if the virtual roots are children of the real
 * root.
 */
xcb_window_t
Find_Client(xcb_connection_t * dpy, xcb_window_t root, xcb_window_t subwin)
{
    xcb_window_t *roots;
    unsigned int i, n_roots;
    xcb_window_t win;

    /* Check if subwin is a virtual root */
    roots = Find_Roots(dpy, root, &n_roots);
    for (i = 0; i < n_roots; i++) {
        if (subwin != roots[i])
            continue;
        win = Find_Child_At_Pointer(dpy, subwin);
        if (win == XCB_WINDOW_NONE) {
            free (roots);
            return subwin;      /* No child - Return virtual root. */
        }
        subwin = win;
        break;
    }
    free (roots);

    if (atom_wm_state == XCB_ATOM_NONE) {
        atom_wm_state = Get_Atom(dpy, "WM_STATE");
        if (atom_wm_state == XCB_ATOM_NONE)
            return subwin;
    }

    /* Check if subwin has WM_STATE */
    if (Window_Has_Property(dpy, subwin, atom_wm_state))
        return subwin;

    /* Attempt to find a client window in subwin's children */
    win = Find_Client_In_Children(dpy, subwin);
    if (win != XCB_WINDOW_NONE)
        return win;             /* Found a client */

    /* Did not find a client */
    return subwin;
}
