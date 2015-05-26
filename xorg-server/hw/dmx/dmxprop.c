/*
 * Copyright 2002-2003 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 *
 * It is possible for one of the DMX "backend displays" to actually be
 * smaller than the dimensions of the backend X server.  Therefore, it
 * is possible for more than one of the DMX "backend displays" to be
 * physically located on the same backend X server.  This situation must
 * be detected so that cursor motion can be handled in an expected
 * fashion.
 *
 * We could analyze the names used for the DMX "backend displays" (e.g.,
 * the names passed to the -display command-line parameter), but there
 * are many possible names for a single X display, and failing to detect
 * sameness leads to very unexpected results.  Therefore, whenever the
 * DMX server opens a window on a backend X server, a property value is
 * queried and set on that backend to detect when another window is
 * already open on that server.
 *
 * Further, it is possible that two different DMX server instantiations
 * both have windows on the same physical backend X server.  This case
 * is also detected so that pointer input is not taken from that
 * particular backend X server.
 *
 * The routines in this file handle the property management. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxprop.h"
#include "dmxlog.h"
#include <X11/Xmu/SysUtil.h>    /* For XmuGetHostname */

/** Holds the window id of all DMX windows on the backend X server. */
#define DMX_ATOMNAME "DMX_NAME"

/** The identification string of this DMX server */
#define DMX_IDENT    "Xdmx"

extern char *display;

static int
dmxPropertyErrorHandler(Display * dpy, XErrorEvent * ev)
{
    return 0;
}

static const unsigned char *
dmxPropertyIdentifier(void)
{
    /* RATS: These buffers are only used in
     * length-limited calls. */
    char hostname[256];
    static char buf[128];
    static int initialized = 0;

    if (initialized++)
        return (unsigned char *) buf;

    XmuGetHostname(hostname, sizeof(hostname));
    snprintf(buf, sizeof(buf), "%s:%s:%s", DMX_IDENT, hostname, display);
    return (unsigned char *) buf;
}

/** Starting with the \a start screen, iterate over all of the screens
 * on the same physical X server as \a start, calling \a f with the
 * screen and the \a closure.  (The common case is that \a start is the
 * only DMX window on the backend X server.) */
void *
dmxPropertyIterate(DMXScreenInfo * start,
                   void *(*f) (DMXScreenInfo * dmxScreen, void *),
                   void *closure)
{
    DMXScreenInfo *pt;

    if (!start->next) {
        if (!start->beDisplay)
            return NULL;
        return f(start, closure);
    }

    for (pt = start->next; /* condition at end of loop */ ; pt = pt->next) {
        void *retval;

        /* beDisplay ban be NULL if a screen was detached */
        dmxLog(dmxDebug, "pt = %p\n", pt);
        dmxLog(dmxDebug, "pt->beDisplay = %p\n", pt->beDisplay);
        if (pt->beDisplay && (retval = f(pt, closure)))
            return retval;
        if (pt == start)
            break;
    }
    return NULL;
}

/** Returns 0 if this is the only Xdmx session on the display; 1
 * otherwise. */
static int
dmxPropertyCheckOtherServers(DMXScreenInfo * dmxScreen, Atom atom)
{
    Display *dpy = dmxScreen->beDisplay;
    XTextProperty tp;
    XTextProperty tproot;
    const char *pt;
    int retcode = 0;
    char **list = NULL;
    int count = 0;
    int i;
    int (*dmxOldHandler) (Display *, XErrorEvent *);

    if (!dpy)
        return 0;

    if (!XGetTextProperty(dpy, RootWindow(dpy, 0), &tproot, atom)
        || !tproot.nitems)
        return 0;

    /* Ignore BadWindow errors for this
     * routine because the window id stored
     * in the property might be old */
    dmxOldHandler = XSetErrorHandler(dmxPropertyErrorHandler);
    for (pt = (const char *) tproot.value; pt && *pt; pt = pt ? pt + 1 : NULL) {
        if ((pt = strchr(pt, ','))) {
            Window win = strtol(pt + 1, NULL, 10);

            if (XGetTextProperty(dpy, win, &tp, atom) && tp.nitems) {
                if (!strncmp((char *) tp.value, DMX_IDENT, strlen(DMX_IDENT))) {
                    int flag = 0;

                    for (i = 0; i < count; i++)
                        if (!strcmp(list[i], (char *) tp.value)) {
                            ++flag;
                            break;
                        }
                    if (flag)
                        continue;
                    ++retcode;
                    dmxLogOutputWarning(dmxScreen,
                                        "%s also running on %s\n",
                                        tp.value, dmxScreen->name);
                    list = reallocarray(list, ++count, sizeof(*list));
                    list[count - 1] = malloc(tp.nitems + 2);
                    strncpy(list[count - 1], (char *) tp.value, tp.nitems + 1);
                }
                XFree(tp.value);
            }
        }
    }
    XSetErrorHandler(dmxOldHandler);

    for (i = 0; i < count; i++)
        free(list[i]);
    free(list);
    XFree(tproot.value);
    if (!retcode)
        dmxLogOutput(dmxScreen, "No Xdmx server running on backend\n");
    return retcode;
}

/** Returns NULL if this is the only Xdmx window on the display.
 * Otherwise, returns a pointer to the dmxScreen of the other windows on
 * the display. */
static DMXScreenInfo *
dmxPropertyCheckOtherWindows(DMXScreenInfo * dmxScreen, Atom atom)
{
    Display *dpy = dmxScreen->beDisplay;
    const unsigned char *id = dmxPropertyIdentifier();
    XTextProperty tproot;
    XTextProperty tp;
    const char *pt;
    int (*dmxOldHandler) (Display *, XErrorEvent *);

    if (!dpy)
        return NULL;

    if (!XGetTextProperty(dpy, RootWindow(dpy, 0), &tproot, atom)
        || !tproot.nitems)
        return 0;

    /* Ignore BadWindow errors for this
     * routine because the window id stored
     * in the property might be old */
    dmxOldHandler = XSetErrorHandler(dmxPropertyErrorHandler);
    for (pt = (const char *) tproot.value; pt && *pt; pt = pt ? pt + 1 : NULL) {
        if ((pt = strchr(pt, ','))) {
            Window win = strtol(pt + 1, NULL, 10);

            if (XGetTextProperty(dpy, win, &tp, atom) && tp.nitems) {
                dmxLog(dmxDebug, "On %s/%lu: %s\n",
                       dmxScreen->name, (unsigned long) win, tp.value);
                if (!strncmp((char *) tp.value, (char *) id,
                             strlen((char *) id))) {
                    int idx;

                    if (!(pt = strchr((char *) tp.value, ',')))
                        continue;
                    idx = strtol(pt + 1, NULL, 10);
                    if (idx < 0 || idx >= dmxNumScreens)
                        continue;
                    if (dmxScreens[idx].scrnWin != win)
                        continue;
                    XSetErrorHandler(dmxOldHandler);
                    return &dmxScreens[idx];
                }
                XFree(tp.value);
            }
        }
    }
    XSetErrorHandler(dmxOldHandler);
    XFree(tproot.value);
    return 0;
}

/** Returns 0 if this is the only Xdmx session on the display; 1
 * otherwise. */
int
dmxPropertyDisplay(DMXScreenInfo * dmxScreen)
{
    Atom atom;
    const unsigned char *id = dmxPropertyIdentifier();
    Display *dpy = dmxScreen->beDisplay;

    if (!dpy)
        return 0;

    atom = XInternAtom(dpy, DMX_ATOMNAME, False);
    if (dmxPropertyCheckOtherServers(dmxScreen, atom)) {
        dmxScreen->shared = 1;
        return 1;
    }
    XChangeProperty(dpy, RootWindow(dpy, 0), atom, XA_STRING, 8,
                    PropModeReplace, id, strlen((char *) id));
    return 0;
}

/** Returns 1 if the dmxScreen and the display in \a name are on the
 * same display, or 0 otherwise.  We can't just compare the display
 * names because there can be multiple synonyms for the same display,
 * some of which cannot be determined without accessing the display
 * itself (e.g., domain aliases or machines with multiple NICs). */
int
dmxPropertySameDisplay(DMXScreenInfo * dmxScreen, const char *name)
{
    Display *dpy0 = dmxScreen->beDisplay;
    Atom atom0;
    XTextProperty tp0;
    Display *dpy1 = NULL;
    Atom atom1;
    XTextProperty tp1;
    int retval = 0;

    if (!dpy0)
        return 0;

    tp0.nitems = 0;
    tp1.nitems = 0;

    if ((atom0 = XInternAtom(dpy0, DMX_ATOMNAME, True)) == None) {
        dmxLog(dmxWarning, "No atom on %s\n", dmxScreen->name);
        return 0;
    }
    if (!XGetTextProperty(dpy0, RootWindow(dpy0, 0), &tp0, atom0)
        || !tp0.nitems) {
        dmxLog(dmxWarning, "No text property on %s\n", dmxScreen->name);
        return 0;
    }

    if (!(dpy1 = XOpenDisplay(name))) {
        dmxLog(dmxWarning, "Cannot open %s\n", name);
        goto cleanup;
    }
    atom1 = XInternAtom(dpy1, DMX_ATOMNAME, True);
    if (atom1 == None) {
        dmxLog(dmxDebug, "No atom on %s\n", name);
        goto cleanup;
    }
    if (!XGetTextProperty(dpy1, RootWindow(dpy1, 0), &tp1, atom1)
        || !tp1.nitems) {
        dmxLog(dmxDebug, "No text property on %s\n", name);
        goto cleanup;
    }
    if (!strcmp((char *) tp0.value, (char *) tp1.value))
        retval = 1;

 cleanup:
    if (tp0.nitems)
        XFree(tp0.value);
    if (tp1.nitems)
        XFree(tp1.value);
    if (dpy1)
        XCloseDisplay(dpy1);
    return retval;
}

/** Prints a log message if \a dmxScreen is on the same backend X server
 * as some other DMX backend (output) screen.  Modifies the property
 * (#DMX_ATOMNAME) on the backend X server to reflect the creation of \a
 * dmxScreen.
 *
 * The root window of the backend X server holds a list of window ids
 * for all DMX windows (on this DMX server or some other DMX server).
 *
 * This list can then be iterated, and the property for each window can
 * be examined.  This property contains the following tuple (no quotes):
 *
 * "#DMX_IDENT:<hostname running DMX>:<display name of DMX>,<screen number>"
 */
void
dmxPropertyWindow(DMXScreenInfo * dmxScreen)
{
    Atom atom;
    const unsigned char *id = dmxPropertyIdentifier();
    Display *dpy = dmxScreen->beDisplay;
    Window win = dmxScreen->scrnWin;
    DMXScreenInfo *other;
    char buf[128];              /* RATS: only used with snprintf */

    if (!dpy)
        return;                 /* FIXME: What should be done here if Xdmx is started
                                 * with this screen initially detached?
                                 */

    atom = XInternAtom(dpy, DMX_ATOMNAME, False);
    if ((other = dmxPropertyCheckOtherWindows(dmxScreen, atom))) {
        DMXScreenInfo *tmp = dmxScreen->next;

        dmxScreen->next = (other->next ? other->next : other);
        other->next = (tmp ? tmp : dmxScreen);
        dmxLog(dmxDebug, "%d/%s/%lu and %d/%s/%lu are on the same backend\n",
               dmxScreen->index, dmxScreen->name, (unsigned long) dmxScreen->scrnWin,
               other->index, other->name, (unsigned long) other->scrnWin);
    }

    snprintf(buf, sizeof(buf), ".%d,%lu", dmxScreen->index,
             (long unsigned) win);
    XChangeProperty(dpy, RootWindow(dpy, 0), atom, XA_STRING, 8,
                    PropModeAppend, (unsigned char *) buf, strlen(buf));

    snprintf(buf, sizeof(buf), "%s,%d", id, dmxScreen->index);
    XChangeProperty(dpy, win, atom, XA_STRING, 8,
                    PropModeAppend, (unsigned char *) buf, strlen(buf));
}
