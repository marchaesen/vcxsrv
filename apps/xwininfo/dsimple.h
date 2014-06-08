/*

Copyright 1993, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from The Open Group.

*/

/*
 * dsimple.h: This file contains the definitions needed to use the
 *            functions in dsimple.c.  It also declares the global
 *            variable program_name which is needed to use dsimple.c.
 *
 * Written by Mark Lillibridge for Xlib.   Last updated 7/1/87
 * Ported to XCB over two decades later.
 */

#include <X11/Xfuncproto.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

typedef int Bool;
#define False 0
#define True 1
    /* Global variables used by routines in dsimple.c */

extern const char *program_name;             /* Name of this program */

    /* Declarations for functions in dsimple.c */

const char *Get_Display_Name (const char *displayname);
void Setup_Display_And_Screen (const char *displayname,
			       xcb_connection_t **dpy, xcb_screen_t **screen);

xcb_window_t Select_Window (xcb_connection_t *, const xcb_screen_t *, int);
xcb_window_t Window_With_Name (xcb_connection_t *, xcb_window_t, const char *);

void Fatal_Error (const char *, ...) _X_NORETURN _X_ATTRIBUTE_PRINTF(1, 2) _X_COLD;

void Print_X_Error (xcb_connection_t *, xcb_generic_error_t *);

struct atom_cache_entry *Intern_Atom (xcb_connection_t *, const char *);
xcb_atom_t Get_Atom (xcb_connection_t *, const char *);
const char *Get_Atom_Name (xcb_connection_t *, xcb_atom_t);
