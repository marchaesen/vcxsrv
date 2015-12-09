/*
 * Copyright 2002 Red Hat Inc., Durham, North Carolina.
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


#include <stdio.h>
#include <stdlib.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Box.h>
/* #include <X11/Xaw/Paned.h> */
#include <X11/Xaw/Command.h>
#include <X11/Xaw/SimpleMenu.h>
#include <X11/Xaw/SmeBSB.h>
#include <X11/Xaw/MenuButton.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/Dialog.h>
#include <X11/keysym.h>
#include "Canvas.h"

#include "dmxparse.h"
#include "dmxprint.h"
#include "dmxlog.h"

extern int yyparse(void);
extern int yydebug;
extern FILE *yyin;

#define DMX_INFO "xdmxconfig v0.9\nCopyright 2002 Red Hat Inc.\n"

#define DMX_MAIN_WIDTH    800
#define DMX_MAIN_HEIGHT   600
#define DMX_DATA_WIDTH    200
#define DMX_DATA_HEIGHT   200
#define DMX_CANVAS_WIDTH  400
#define DMX_CANVAS_HEIGHT 500

DMXConfigEntryPtr dmxConfigEntry;
static DMXConfigVirtualPtr dmxConfigCurrent, dmxConfigNewVirtual;
static DMXConfigDisplayPtr dmxConfigCurrentDisplay, dmxConfigNewDisplay;
static int dmxConfigGrabbed, dmxConfigGrabbedFine;
static int dmxConfigGrabbedX, dmxConfigGrabbedY;
static char *dmxConfigFilename;
static GC dmxConfigGC, dmxConfigGCRev, dmxConfigGCHL;
static int dmxConfigGCInit = 0;
static Dimension dmxConfigWidgetWidth, dmxConfigWidgetHeight;
static Dimension dmxConfigWallWidth, dmxConfigWallHeight;
static double dmxConfigScaleX, dmxConfigScaleY;
static int dmxConfigNotSaved;
static enum {
    dmxConfigStateOpen,
    dmxConfigStateSave
} dmxConfigState;

/* Global widgets */
static Widget canvas;
static Widget cnamebox, cdimbox;
static Widget openpopup, opendialog;
static Widget namebox, dimbox, rtbox, origbox;
static Widget okbutton, buttonpopup;
static Widget ecbutton, dcbutton;
static Widget ndbutton0, ndbutton1, edbutton, ddbutton;
static Widget ecpopup, ecdialog0, ecdialog1;
static Widget edpopup, eddialog0, eddialog1, eddialog2;
static Widget aboutpopup, quitpopup;

static void
dmxConfigCanvasGCs(void)
{
    Display *dpy = XtDisplay(canvas);
    Window win = XtWindow(canvas);
    XGCValues gcvals;
    unsigned long mask;
    Colormap colormap;
    XColor fg, bg, hl, tmp;

    if (dmxConfigGCInit++)
        return;

    XtVaGetValues(canvas, XtNcolormap, &colormap, NULL);
    XAllocNamedColor(XtDisplay(canvas), colormap, "black", &bg, &tmp);
    XAllocNamedColor(XtDisplay(canvas), colormap, "white", &fg, &tmp);
    XAllocNamedColor(XtDisplay(canvas), colormap, "red", &hl, &tmp);

    mask = (GCFunction | GCPlaneMask | GCClipMask | GCForeground |
            GCBackground | GCLineWidth | GCLineStyle | GCCapStyle |
            GCFillStyle);

    /* FIXME: copy this from widget */
    gcvals.function = GXcopy;
    gcvals.plane_mask = AllPlanes;
    gcvals.clip_mask = None;
    gcvals.foreground = fg.pixel;
    gcvals.background = bg.pixel;
    gcvals.line_width = 0;
    gcvals.line_style = LineSolid;
    gcvals.cap_style = CapNotLast;
    gcvals.fill_style = FillSolid;

    dmxConfigGC = XCreateGC(dpy, win, mask, &gcvals);
    gcvals.foreground = hl.pixel;
    dmxConfigGCHL = XCreateGC(dpy, win, mask, &gcvals);
    gcvals.foreground = bg.pixel;
    gcvals.background = fg.pixel;
    dmxConfigGCRev = XCreateGC(dpy, win, mask, &gcvals);
}

static void
dmxConfigGetDims(int *maxWidth, int *maxHeight)
{
    DMXConfigSubPtr pt;
    DMXConfigEntryPtr e;

    *maxWidth = dmxConfigWallWidth = 0;
    *maxHeight = dmxConfigWallHeight = 0;
    if (!dmxConfigCurrent)
        return;

    dmxConfigWallWidth = dmxConfigCurrent->width;
    dmxConfigWallHeight = dmxConfigCurrent->height;
    if (!dmxConfigWallWidth || !dmxConfigWallHeight) {
        for (pt = dmxConfigCurrent->subentry; pt; pt = pt->next) {
            if (pt->type == dmxConfigDisplay) {
                int x = pt->display->scrnWidth + pt->display->rootXOrigin;
                int y = pt->display->scrnHeight + pt->display->rootYOrigin;

                if (x > dmxConfigWallWidth)
                    dmxConfigWallWidth = x;
                if (y > dmxConfigWallHeight)
                    dmxConfigWallHeight = y;
            }
        }
    }
    /* Compute maximums */
    *maxWidth = *maxHeight = 0;
    for (e = dmxConfigEntry; e; e = e->next) {
        if (e->type != dmxConfigVirtual)
            continue;
        for (pt = e->virtual->subentry; pt; pt = pt->next) {
            if (pt->type == dmxConfigDisplay) {
                int x = pt->display->scrnWidth + pt->display->rootXOrigin;
                int y = pt->display->scrnHeight + pt->display->rootYOrigin;

                if (x > *maxWidth)
                    *maxWidth = x;
                if (y > *maxHeight)
                    *maxHeight = y;
            }
        }
    }
    if (dmxConfigWallWidth > *maxWidth)
        *maxWidth = dmxConfigWallWidth;
    if (dmxConfigWallHeight > *maxHeight)
        *maxHeight = dmxConfigWallHeight;
}

static int
scalex(int x)
{
    return (int) ((x * dmxConfigScaleX) + .5);
}

static int
scaley(int y)
{
    return (int) ((y * dmxConfigScaleY) + .5);
}

static int
unscalex(int x)
{
    return (int) ((x / dmxConfigScaleX) + .5);
}

static int
unscaley(int y)
{
    return (int) ((y / dmxConfigScaleY) + .5);
}

static void
dmxConfigDataUpdate(void)
{
    /* FIXME: could result in buffer overflows */
    char cnambuf[512];
    char cdimbuf[128];
    char nambuf[512];
    char dimbuf[128];
    char rtbuf[128];
    char offbuf[128];
    const char *name;

    if (!dmxConfigCurrent) {
        XtVaSetValues(cnamebox, XtNlabel, "", XtNsensitive, False, NULL);
        XtVaSetValues(cdimbox, XtNlabel, "", XtNsensitive, False, NULL);
        XtVaSetValues(ecbutton, XtNsensitive, False, NULL);
        XtVaSetValues(dcbutton, XtNsensitive, False, NULL);
        XtVaSetValues(ndbutton0, XtNsensitive, False, NULL);
        XtVaSetValues(ndbutton1, XtNsensitive, False, NULL);
    }
    else {
        name = dmxConfigCurrent->name;
        snprintf(cnambuf, sizeof(cnambuf), "%s", name ? name : "");
        snprintf(cdimbuf, sizeof(cdimbuf), "%dx%d",
                 dmxConfigWallWidth, dmxConfigWallHeight);
        XtVaSetValues(cnamebox, XtNlabel, cnambuf, XtNsensitive, True, NULL);
        XtVaSetValues(cdimbox, XtNlabel, cdimbuf, XtNsensitive, True, NULL);
        XtVaSetValues(ecbutton, XtNsensitive, True, NULL);
        XtVaSetValues(dcbutton, XtNsensitive, True, NULL);
        XtVaSetValues(ndbutton0, XtNsensitive, True, NULL);
        XtVaSetValues(ndbutton1, XtNsensitive, True, NULL);
    }

    if (!dmxConfigCurrentDisplay) {
        XtVaSetValues(namebox, XtNlabel, "", XtNsensitive, False, NULL);
        XtVaSetValues(dimbox, XtNlabel, "", XtNsensitive, False, NULL);
        XtVaSetValues(rtbox, XtNlabel, "", XtNsensitive, False, NULL);
        XtVaSetValues(origbox, XtNlabel, "", XtNsensitive, False, NULL);
        XtVaSetValues(edbutton, XtNsensitive, False, NULL);
        XtVaSetValues(ddbutton, XtNsensitive, False, NULL);
    }
    else {
        name = dmxConfigCurrentDisplay->name;
        snprintf(nambuf, sizeof(nambuf), "%s", name ? name : "");
        snprintf(dimbuf, sizeof(dimbuf), "%dx%d%c%d%c%d",
                 dmxConfigCurrentDisplay->scrnWidth,
                 dmxConfigCurrentDisplay->scrnHeight,
                 dmxConfigCurrentDisplay->scrnXSign < 0 ? '-' : '+',
                 dmxConfigCurrentDisplay->scrnX,
                 dmxConfigCurrentDisplay->scrnYSign < 0 ? '-' : '+',
                 dmxConfigCurrentDisplay->scrnY);
        snprintf(rtbuf, sizeof(dimbuf), "%dx%d%c%d%c%d",
                 dmxConfigCurrentDisplay->rootWidth,
                 dmxConfigCurrentDisplay->rootHeight,
                 dmxConfigCurrentDisplay->rootXSign < 0 ? '-' : '+',
                 dmxConfigCurrentDisplay->rootX,
                 dmxConfigCurrentDisplay->rootYSign < 0 ? '-' : '+',
                 dmxConfigCurrentDisplay->rootY);
        snprintf(offbuf, sizeof(offbuf), "@%dx%d",
                 dmxConfigCurrentDisplay->rootXOrigin,
                 dmxConfigCurrentDisplay->rootYOrigin);
        XtVaSetValues(namebox, XtNlabel, nambuf, XtNsensitive, True, NULL);
        XtVaSetValues(dimbox, XtNlabel, dimbuf, XtNsensitive, True, NULL);
        XtVaSetValues(rtbox, XtNlabel, rtbuf, XtNsensitive, True, NULL);
        XtVaSetValues(origbox, XtNlabel, offbuf, XtNsensitive, True, NULL);
        XtVaSetValues(edbutton, XtNsensitive, True, NULL);
        XtVaSetValues(ddbutton, XtNsensitive, True, NULL);
    }
}

static void
dmxConfigCanvasUpdate(void)
{
    DMXConfigSubPtr pt;
    Display *dpy = XtDisplay(canvas);
    Window win = XtWindow(canvas);
    GContext gcontext = XGContextFromGC(dmxConfigGC);
    XFontStruct *fs;
    int w, h;

    XFillRectangle(dpy, win, dmxConfigGCRev,
                   0, 0, dmxConfigWidgetWidth, dmxConfigWidgetHeight);
    dmxConfigDataUpdate();
    if (!dmxConfigCurrent)
        return;

    w = scalex(dmxConfigWallWidth);
    h = scaley(dmxConfigWallHeight);
    if (w > dmxConfigWidgetWidth - 1)
        w = dmxConfigWidgetWidth - 1;
    if (h > dmxConfigWidgetHeight - 1)
        h = dmxConfigWidgetHeight - 1;
    XDrawRectangle(dpy, win, dmxConfigGC, 0, 0, w, h);
    fs = XQueryFont(dpy, gcontext);
    for (pt = dmxConfigCurrent->subentry; pt; pt = pt->next) {
        int x, y, len;
        GC gc;

        if (pt->type != dmxConfigDisplay)
            continue;
        gc = (pt->display == dmxConfigCurrentDisplay
              ? dmxConfigGCHL : dmxConfigGC);
        x = scalex(pt->display->rootXOrigin);
        y = scaley(pt->display->rootYOrigin);
        w = scalex(pt->display->scrnWidth);
        h = scaley(pt->display->scrnHeight);
        len = pt->display->name ? strlen(pt->display->name) : 0;
        if (x > dmxConfigWidgetWidth - 1)
            x = dmxConfigWidgetWidth - 1;
        if (y > dmxConfigWidgetHeight - 1)
            y = dmxConfigWidgetHeight - 1;
        XDrawRectangle(dpy, win, gc, x, y, w, h);
        if (fs && len) {
            int xo = 3, yo = fs->ascent + fs->descent + 2;

            while (len && XTextWidth(fs, pt->display->name, len) >= w - 2 * xo)
                --len;
            if (len)
                XDrawString(dpy, win, gc, x + xo, y + yo, pt->display->name,
                            len);
        }
    }
    if (fs)
        XFreeFontInfo(NULL, fs, 0);
}

static void
dmxConfigCanvasDraw(Region region)
{
    Display *dpy = XtDisplay(canvas);
    int maxWidth, maxHeight;

    dmxConfigCanvasGCs();
    if (region) {
        XSetRegion(dpy, dmxConfigGC, region);
        XSetRegion(dpy, dmxConfigGCRev, region);
        XSetRegion(dpy, dmxConfigGCHL, region);
    }
    XtVaGetValues(canvas,
                  XtNwidth, &dmxConfigWidgetWidth,
                  XtNheight, &dmxConfigWidgetHeight, NULL);
    dmxConfigGetDims(&maxWidth, &maxHeight);
    dmxConfigScaleX = (double) dmxConfigWidgetWidth / maxWidth;
    dmxConfigScaleY = (double) dmxConfigWidgetHeight / maxHeight;
    if (dmxConfigScaleX > dmxConfigScaleY)
        dmxConfigScaleX = dmxConfigScaleY;
    if (dmxConfigScaleY > dmxConfigScaleX)
        dmxConfigScaleY = dmxConfigScaleX;
    dmxConfigCanvasUpdate();
    if (region) {
        XSetClipMask(dpy, dmxConfigGC, None);
        XSetClipMask(dpy, dmxConfigGCRev, None);
        XSetClipMask(dpy, dmxConfigGCHL, None);
    }
}

static void
dmxConfigSelectCallback(Widget w, XtPointer closure, XtPointer callData)
{
    dmxConfigCurrent = closure;
    dmxConfigVirtualPrint(stdout, dmxConfigCurrent);
    dmxConfigCanvasDraw(NULL);
}

static void
dmxConfigCopystrings(void)
{
    DMXConfigEntryPtr pt;
    DMXConfigSubPtr sub;

    if (!dmxConfigCurrent)
        return;

    /* FIXME: this is all a per-config file
     * memory leak */
    for (pt = dmxConfigEntry; pt; pt = pt->next) {
        if (pt->type == dmxConfigVirtual) {
            pt->virtual->name = XtNewString(pt->virtual->name
                                            ? pt->virtual->name : "");

            for (sub = pt->virtual->subentry; sub; sub = sub->next) {
                if (sub->type != dmxConfigDisplay)
                    continue;
                sub->display->name = XtNewString(sub->display->name
                                                 ? sub->display->name : "");
            }
        }
    }
}

static void
dmxConfigGetValueString(char **d, Widget w)
{
    const char *tmp = XawDialogGetValueString(w);

    if (*d)
        XtFree(*d);
    *d = XtNewString(tmp);
}

static void
dmxConfigSetupCnamemenu(void)
{
    static Widget cnamemenu = NULL;
    Widget w;
    DMXConfigEntryPtr pt;

    if (cnamemenu)
        XtDestroyWidget(cnamemenu);
    cnamemenu = NULL;

    if (!dmxConfigCurrent)
        return;
    cnamemenu = XtVaCreatePopupShell("cnamemenu", simpleMenuWidgetClass,
                                     cnamebox, NULL);

    for (pt = dmxConfigEntry; pt; pt = pt->next) {
        if (pt->type == dmxConfigVirtual) {
            w = XtVaCreateManagedWidget(pt->virtual->name
                                        ? pt->virtual->name
                                        : "",
                                        smeBSBObjectClass, cnamemenu, NULL);
            XtAddCallback(w, XtNcallback, dmxConfigSelectCallback, pt->virtual);
        }
    }
}

static void
dmxConfigReadFile(void)
{
    FILE *str;
    DMXConfigEntryPtr pt;

    if (!(str = fopen(dmxConfigFilename, "r"))) {
        dmxLog(dmxWarning, "Unable to read configuration file %s\n",
               dmxConfigFilename);
        return;
    }
    yyin = str;
    yydebug = 0;
    yyparse();
    fclose(str);
    dmxLog(dmxInfo, "Read configuration file %s\n", dmxConfigFilename);

    for (pt = dmxConfigEntry; pt; pt = pt->next) {
        if (pt->type == dmxConfigVirtual) {
            dmxConfigCurrent = pt->virtual;
            break;
        }
    }

    if (XtIsRealized(canvas)) {
        dmxConfigCopystrings();
        dmxConfigSetupCnamemenu();
        dmxConfigCanvasDraw(NULL);
    }
    dmxConfigVirtualPrint(stdout, dmxConfigCurrent);
}

static void
dmxConfigWriteFile(void)
{
    FILE *str;

    if (!(str = fopen(dmxConfigFilename, "w"))) {
        dmxLog(dmxWarning, "Unable to write configuration file %s\n",
               dmxConfigFilename);
        return;
    }
    dmxConfigPrint(str, dmxConfigEntry);
    fclose(str);
}

static DMXConfigDisplayPtr
dmxConfigFindDisplay(int x, int y)
{
    DMXConfigSubPtr pt;

    if (!dmxConfigCurrent)
        return NULL;
    for (pt = dmxConfigCurrent->subentry; pt; pt = pt->next) {
        DMXConfigDisplayPtr d = pt->display;

        if (pt->type != dmxConfigDisplay)
            continue;
        if (x >= scalex(d->rootXOrigin)
            && x <= scalex(d->rootXOrigin + d->scrnWidth)
            && y >= scaley(d->rootYOrigin)
            && y <= scaley(d->rootYOrigin + d->scrnHeight))
            return d;
    }
    return NULL;
}

static void
dmxConfigSetPopupPosition(Widget popup)
{
    Position x, y;
    Window t1, t2;
    int root_x, root_y;
    int temp_x, temp_y;
    unsigned int temp;

    XtRealizeWidget(popup);
    if (!XQueryPointer(XtDisplay(popup), XtWindow(popup), &t1, &t2,
                       &root_x, &root_y, &temp_x, &temp_y, &temp))
        root_x = root_y = 0;

    x = root_x - 5;
    y = root_y - 5;
    XtVaSetValues(popup, XtNx, x, XtNy, y, NULL);
}

static void
dmxConfigPlaceMenu(Widget w, XEvent * event,
                   String * params, Cardinal * num_params)
{
    dmxConfigSetPopupPosition(buttonpopup);
}

static void
dmxConfigMove(int deltaX, int deltaY)
{
    dmxConfigCurrentDisplay->rootXOrigin += deltaX;
    dmxConfigCurrentDisplay->rootYOrigin += deltaY;
    if (dmxConfigCurrentDisplay->rootXOrigin < 0)
        dmxConfigCurrentDisplay->rootXOrigin = 0;
    if (dmxConfigCurrentDisplay->rootYOrigin < 0)
        dmxConfigCurrentDisplay->rootYOrigin = 0;
    if (dmxConfigWallWidth && dmxConfigWallHeight) {
        if (dmxConfigCurrentDisplay->rootXOrigin >= dmxConfigWallWidth)
            dmxConfigCurrentDisplay->rootXOrigin = dmxConfigWallWidth - 1;
        if (dmxConfigCurrentDisplay->rootYOrigin >= dmxConfigWallHeight)
            dmxConfigCurrentDisplay->rootYOrigin = dmxConfigWallHeight - 1;
    }
    dmxConfigCanvasUpdate();
    dmxConfigNotSaved = 1;
}

static void
dmxConfigCanvasInput(Widget w, XtPointer closure, XtPointer callData)
{
    XEvent *e = (XEvent *) callData;
    DMXConfigDisplayPtr display = NULL;

    switch (e->type) {
    case ButtonPress:
        if (e->xbutton.button == Button1) {
            dmxConfigGrabbed = 1;
            dmxConfigGrabbedFine = 0;
            dmxConfigGrabbedX = e->xbutton.x;
            dmxConfigGrabbedY = e->xbutton.y;
        }
        if (e->xbutton.button == Button2) {
            dmxConfigGrabbed = 1;
            dmxConfigGrabbedFine = 1;
            dmxConfigGrabbedX = e->xbutton.x;
            dmxConfigGrabbedY = e->xbutton.y;
        }
        break;
    case ButtonRelease:
        if (e->xbutton.button == Button1)
            dmxConfigGrabbed = 0;
        if (e->xbutton.button == Button2)
            dmxConfigGrabbed = 0;
        break;
    case MotionNotify:
        if (dmxConfigGrabbed && dmxConfigCurrentDisplay) {
            int deltaX = e->xmotion.x - dmxConfigGrabbedX;
            int deltaY = e->xmotion.y - dmxConfigGrabbedY;

            dmxConfigMove(dmxConfigGrabbedFine ? deltaX : unscalex(deltaX),
                          dmxConfigGrabbedFine ? deltaY : unscaley(deltaY));
            dmxConfigGrabbedX = e->xmotion.x;
            dmxConfigGrabbedY = e->xmotion.y;
        }
        else {
            display = dmxConfigFindDisplay(e->xmotion.x, e->xmotion.y);
            if (display != dmxConfigCurrentDisplay) {
                dmxConfigCurrentDisplay = display;
                dmxConfigCanvasUpdate();
            }
        }
        break;
    case KeyPress:
        switch (XLookupKeysym(&e->xkey, 0)) {
        case XK_Right:
            dmxConfigMove(1, 0);
            break;
        case XK_Left:
            dmxConfigMove(-1, 0);
            break;
        case XK_Down:
            dmxConfigMove(0, 1);
            break;
        case XK_Up:
            dmxConfigMove(0, -1);
            break;
        }
        break;
    }
}

static void
dmxConfigCanvasResize(Widget w, XtPointer closure, XtPointer callData)
{
    dmxConfigCanvasDraw(NULL);
}

static void
dmxConfigCanvasExpose(Widget w, XtPointer closure, XtPointer callData)
{
    CanvasExposeDataPtr data = (CanvasExposeDataPtr) callData;

    dmxConfigCanvasDraw(data->region);
}

static void
dmxConfigOpenCallback(Widget w, XtPointer closure, XtPointer callData)
{
    dmxConfigState = dmxConfigStateOpen;
    XtVaSetValues(okbutton, XtNlabel, "Open", NULL);
    dmxConfigSetPopupPosition(openpopup);
    XtPopup(openpopup, XtGrabExclusive);
}

static void
dmxConfigSaveCallback(Widget w, XtPointer closure, XtPointer callData)
{
    dmxConfigState = dmxConfigStateSave;
    XtVaSetValues(okbutton, XtNlabel, "Save", NULL);
    dmxConfigSetPopupPosition(openpopup);
    XtPopup(openpopup, XtGrabExclusive);
}

static void
dmxConfigOkCallback(Widget w, XtPointer closure, XtPointer callData)
{
    dmxConfigGetValueString(&dmxConfigFilename, opendialog);
    XtPopdown(openpopup);
    if (dmxConfigState == dmxConfigStateOpen)
        dmxConfigReadFile();
    else
        dmxConfigWriteFile();
    dmxConfigNotSaved = 0;
}

static void
dmxConfigCanCallback(Widget w, XtPointer closure, XtPointer callData)
{
    XtPopdown(openpopup);
}

static void
dmxConfigECCallback(Widget w, XtPointer closure, XtPointer callData)
{
    char buf[256];              /* RATS: Only used in snprintf */

    if (!dmxConfigCurrent)
        return;
    dmxConfigSetPopupPosition(ecpopup);
    XtVaSetValues(ecdialog0, XtNvalue,
                  dmxConfigCurrent->name ? dmxConfigCurrent->name : "", NULL);
    snprintf(buf, sizeof(buf), "%dx%d",
             dmxConfigCurrent->width, dmxConfigCurrent->height);
    XtVaSetValues(ecdialog1, XtNvalue, buf, NULL);
    XtPopup(ecpopup, XtGrabExclusive);
}

static void
dmxConfigNCCallback(Widget w, XtPointer closure, XtPointer callData)
{
    int width = 1280 * 2, height = 1024 * 2;

    if (dmxConfigCurrent) {
        width = dmxConfigCurrent->width;
        height = dmxConfigCurrent->height;
    }

    dmxConfigCurrent = dmxConfigCreateVirtual(NULL, NULL, NULL,
                                              NULL, NULL, NULL);
    dmxConfigNewVirtual = dmxConfigCurrent;
    dmxConfigCurrent->width = width;
    dmxConfigCurrent->height = height;
    dmxConfigEntry = dmxConfigAddEntry(dmxConfigEntry, dmxConfigVirtual, NULL,
                                       dmxConfigCurrent);
    dmxConfigECCallback(w, closure, callData);
}

static void
dmxConfigDCCallback(Widget w, XtPointer closure, XtPointer callData)
{
    DMXConfigEntryPtr pt;

    if (!dmxConfigEntry)
        return;
    if (dmxConfigEntry
        && dmxConfigEntry->type == dmxConfigVirtual
        && dmxConfigEntry->virtual == dmxConfigCurrent) {
        dmxConfigEntry = dmxConfigEntry->next;
    }
    else {
        for (pt = dmxConfigEntry; pt && pt->next; pt = pt->next)
            if (pt->next->type == dmxConfigVirtual
                && pt->next->virtual == dmxConfigCurrent) {
                pt->next = pt->next->next;
                break;
            }
    }
    dmxConfigFreeVirtual(dmxConfigCurrent);
    dmxConfigCurrent = NULL;
    dmxConfigCurrentDisplay = NULL;

    /* Make the first entry current */
    for (pt = dmxConfigEntry; pt; pt = pt->next) {
        if (pt->type == dmxConfigVirtual) {
            dmxConfigCurrent = pt->virtual;
            break;
        }
    }

    dmxConfigSetupCnamemenu();
    dmxConfigCanvasDraw(NULL);
}

static void
dmxConfigECOkCallback(Widget w, XtPointer closure, XtPointer callData)
{
    const char *value;
    char *endpt;

    dmxConfigGetValueString((char **) &dmxConfigCurrent->name, ecdialog0);
    value = XawDialogGetValueString(ecdialog1);
    dmxConfigCurrent->width = strtol(value, &endpt, 10);
    dmxConfigCurrent->height = strtol(endpt + 1, NULL, 10);
    XtPopdown(ecpopup);
    dmxConfigCurrentDisplay = NULL;
    dmxConfigNewVirtual = NULL;
    dmxConfigSetupCnamemenu();
    dmxConfigCanvasDraw(NULL);
    dmxConfigNotSaved = 1;
}

static void
dmxConfigECCanCallback(Widget w, XtPointer closure, XtPointer callData)
{
    if (dmxConfigNewVirtual)
        dmxConfigDCCallback(w, closure, callData);
    dmxConfigNewVirtual = NULL;
    XtPopdown(ecpopup);
}

static void
dmxConfigEDCallback(Widget w, XtPointer closure, XtPointer callData)
{
    char buf[256];              /* RATS: Only used in snprintf */

    if (!dmxConfigCurrent || !dmxConfigCurrentDisplay)
        return;
    dmxConfigSetPopupPosition(edpopup);
    XtVaSetValues(eddialog0, XtNvalue,
                  dmxConfigCurrentDisplay->name
                  ? dmxConfigCurrentDisplay->name : "", NULL);
    snprintf(buf, sizeof(buf), "%dx%d%c%d%c%d",
             dmxConfigCurrentDisplay->scrnWidth,
             dmxConfigCurrentDisplay->scrnHeight,
             dmxConfigCurrentDisplay->scrnXSign < 0 ? '-' : '+',
             dmxConfigCurrentDisplay->scrnY,
             dmxConfigCurrentDisplay->scrnYSign < 0 ? '-' : '+',
             dmxConfigCurrentDisplay->scrnY);
    XtVaSetValues(eddialog1, XtNvalue, buf, NULL);
    snprintf(buf, sizeof(buf), "@%dx%d",
             dmxConfigCurrentDisplay->rootXOrigin,
             dmxConfigCurrentDisplay->rootYOrigin);
    XtVaSetValues(eddialog2, XtNvalue, buf, NULL);
    XtPopup(edpopup, XtGrabExclusive);
}

static void
dmxConfigNDCallback(Widget w, XtPointer closure, XtPointer callData)
{
    int width = 1280, height = 1024;

    if (!dmxConfigCurrent)
        return;
    if (dmxConfigCurrentDisplay) {
        width = dmxConfigCurrentDisplay->scrnWidth;
        height = dmxConfigCurrentDisplay->scrnHeight;
    }
    dmxConfigCurrentDisplay = dmxConfigCreateDisplay(NULL, NULL, NULL,
                                                     NULL, NULL);
    dmxConfigNewDisplay = dmxConfigCurrentDisplay;
    dmxConfigCurrentDisplay->scrnWidth = width;
    dmxConfigCurrentDisplay->scrnHeight = height;

    dmxConfigCurrent->subentry
        = dmxConfigAddSub(dmxConfigCurrent->subentry,
                          dmxConfigSubDisplay(dmxConfigCurrentDisplay));
    dmxConfigEDCallback(w, closure, callData);
}

static void
dmxConfigDDCallback(Widget w, XtPointer closure, XtPointer callData)
{
    DMXConfigSubPtr pt;

    if (!dmxConfigCurrent || !dmxConfigCurrentDisplay)
        return;
    /* First */
    if (dmxConfigCurrent->subentry
        && dmxConfigCurrent->subentry->type == dmxConfigDisplay
        && dmxConfigCurrent->subentry->display == dmxConfigCurrentDisplay) {
        dmxConfigCurrent->subentry = dmxConfigCurrent->subentry->next;
    }
    else {
        for (pt = dmxConfigCurrent->subentry; pt && pt->next; pt = pt->next)
            if (pt->next->type == dmxConfigDisplay
                && pt->next->display == dmxConfigCurrentDisplay) {
                pt->next = pt->next->next;
                break;
            }
    }
    dmxConfigFreeDisplay(dmxConfigCurrentDisplay);
    dmxConfigCurrentDisplay = NULL;
    dmxConfigSetupCnamemenu();
    dmxConfigCanvasDraw(NULL);
}

static void
dmxConfigAboutCallback(Widget w, XtPointer closure, XtPointer callData)
{
    dmxConfigSetPopupPosition(aboutpopup);
    XtPopup(aboutpopup, XtGrabExclusive);
}

static void
dmxConfigAboutOkCallback(Widget w, XtPointer closure, XtPointer CallData)
{
    XtPopdown(aboutpopup);
}

static void
dmxConfigQuitCallback(Widget w, XtPointer closure, XtPointer callData)
{
    if (dmxConfigNotSaved) {
        dmxConfigSetPopupPosition(quitpopup);
        XtPopup(quitpopup, XtGrabExclusive);
        return;
    }
    exit(0);
}

static void
dmxConfigQuitOkCallback(Widget w, XtPointer closure, XtPointer callData)
{
    XtPopdown(quitpopup);
    exit(0);
}

static void
dmxConfigQuitCanCallback(Widget w, XtPointer closure, XtPointer callData)
{
    XtPopdown(quitpopup);
}

static void
dmxConfigEDOkCallback(Widget w, XtPointer closure, XtPointer callData)
{
    char *value;
    char *endpt;

    dmxConfigNewDisplay = NULL;
    dmxConfigGetValueString((char **) &dmxConfigCurrentDisplay->name,
                            eddialog0);
    value = XawDialogGetValueString(eddialog1);
    if (*value == '-' || *value == '+') {
        dmxConfigCurrentDisplay->scrnWidth = 0;
        dmxConfigCurrentDisplay->scrnHeight = 0;
        endpt = value;
    }
    else {
        dmxConfigCurrentDisplay->scrnWidth = strtol(value, &endpt, 10);
        dmxConfigCurrentDisplay->scrnHeight = strtol(endpt + 1, &endpt, 10);
    }
    if (*endpt) {
        dmxConfigCurrentDisplay->scrnXSign = (*endpt == '-') ? -1 : 1;
        dmxConfigCurrentDisplay->scrnX = strtol(endpt + 1, &endpt, 10);
        dmxConfigCurrentDisplay->scrnYSign = (*endpt == '-') ? -1 : 1;
        dmxConfigCurrentDisplay->scrnY = strtol(endpt + 1, NULL, 10);
    }
    if (dmxConfigCurrentDisplay->scrnX < 0)
        dmxConfigCurrentDisplay->scrnX = -dmxConfigCurrentDisplay->scrnX;
    if (dmxConfigCurrentDisplay->scrnY < 0)
        dmxConfigCurrentDisplay->scrnY = -dmxConfigCurrentDisplay->scrnY;
    value = XawDialogGetValueString(eddialog2);
    dmxConfigCurrentDisplay->rootXOrigin = strtol(value + 1, &endpt, 10);
    dmxConfigCurrentDisplay->rootYOrigin = strtol(endpt + 1, NULL, 10);
    XtPopdown(edpopup);
    dmxConfigSetupCnamemenu();
    dmxConfigCanvasDraw(NULL);
    dmxConfigNotSaved = 1;
}

static void
dmxConfigEDCanCallback(Widget w, XtPointer closure, XtPointer callData)
{
    if (dmxConfigNewDisplay)
        dmxConfigDDCallback(w, closure, callData);
    dmxConfigNewDisplay = NULL;
    XtPopdown(edpopup);
}

static void
dmxConfigOkAction(Widget w, XEvent * event,
                  String * params, Cardinal * num_params)
{
    Widget p = XtParent(w);
    Widget t;

    if (p == opendialog)
        dmxConfigOkCallback(w, NULL, NULL);

    if (p == ecdialog0) {
        t = XtNameToWidget(ecdialog1, "value");
        XWarpPointer(XtDisplay(t), None, XtWindow(t), 0, 0, 0, 0, 0, 10);
    }
    if (p == ecdialog1)
        dmxConfigECOkCallback(w, NULL, NULL);

    if (p == eddialog0) {
        t = XtNameToWidget(eddialog1, "value");
        XWarpPointer(XtDisplay(t), None, XtWindow(t), 0, 0, 0, 0, 0, 10);
    }
    if (p == eddialog1) {
        t = XtNameToWidget(eddialog2, "value");
        XWarpPointer(XtDisplay(t), None, XtWindow(t), 0, 0, 0, 0, 0, 10);
    }
    if (p == eddialog2)
        dmxConfigEDOkCallback(w, NULL, NULL);
}

int
main(int argc, char **argv)
{
    XtAppContext appContext;
    Widget toplevel;
    Widget parent, menubox, bottombox, databox, canvasbox;
    Widget filebutton, helpbutton;
    Widget filemenu, openbutton, savebutton, quitbutton;
    Widget helpmenu, aboutbutton, aboutbox, aboutok;
    Widget quitbox, quitok, quitcan;
    Widget ncbutton;
    Widget canbutton;
    Widget ecbox, ecokbutton, eccanbutton;
    Widget edbox, edokbutton;
    Widget edcanbutton;

    /* FIXME: add meta-i, ctrl,meta-z,v? */
    const char *opentrans = "<Key>Return: openOk()\n\
                                 <Key>Linefeed: openOk()\n\
                                 Ctrl<Key>M: openOk()\n\
                                 Ctrl<Key>J: openOk()\n\
                                 Ctrl<Key>O: noop()\n\
                                 Ctrl<Key>N: noop()\n\
                                 Ctrl<Key>P: noop()";
    const char *canvastrans =
        "<Btn3Down>: placeMenu() XtMenuPopup(buttonpopup)";
    XtActionsRec actiontable[] = {
        {(char *) "openOk", dmxConfigOkAction},
        {(char *) "placeMenu", dmxConfigPlaceMenu},
        {(char *) "noop", NULL}
    };

    dmxConfigFilename = XtNewString((argc >= 2) ? argv[1] : "");

    toplevel = XtVaAppInitialize(&appContext, "XDmxconfig",
                                 NULL, 0, &argc, argv, NULL, NULL);

    /* Main boxes */
    parent = XtVaCreateManagedWidget("parent", formWidgetClass, toplevel,
                                     XtNorientation, XtorientVertical,
                                     XtNwidth, DMX_MAIN_WIDTH,
                                     XtNheight, DMX_MAIN_HEIGHT, NULL);
    menubox = XtVaCreateManagedWidget("menubox", boxWidgetClass, parent,
                                      XtNborderWidth, 0,
                                      XtNorientation, XtorientHorizontal,
                                      XtNtop, XtChainTop, NULL);
    bottombox = XtVaCreateManagedWidget("bottombox", formWidgetClass, parent,
                                        XtNborderWidth, 0,
                                        XtNfromVert, menubox,
                                        XtNorientation, XtorientHorizontal,
                                        NULL);
    databox = XtVaCreateManagedWidget("databox", formWidgetClass,
                                      bottombox,
                                      XtNborderWidth, 0,
                                      XtNhorizDistance, 0,
                                      XtNwidth, DMX_DATA_WIDTH,
                                      XtNheight, DMX_DATA_HEIGHT,
                                      XtNleft, XtChainLeft,
                                      XtNorientation, XtorientVertical, NULL);

    /* Data */
    cnamebox = XtVaCreateManagedWidget("cnamebox", menuButtonWidgetClass,
                                       databox,
                                       XtNtop, XtChainTop,
                                       XtNjustify, XtJustifyLeft,
                                       XtNwidth, DMX_DATA_WIDTH,
                                       XtNlabel, "",
                                       XtNmenuName, "cnamemenu", NULL);
    cdimbox = XtVaCreateManagedWidget("cdimbox", labelWidgetClass,
                                      databox,
                                      XtNfromVert, cnamebox,
                                      XtNjustify, XtJustifyLeft,
                                      XtNwidth, DMX_DATA_WIDTH,
                                      XtNlabel, "", NULL);
    namebox = XtVaCreateManagedWidget("namebox", labelWidgetClass, databox,
                                      XtNfromVert, cdimbox,
                                      XtNjustify, XtJustifyLeft,
                                      XtNwidth, DMX_DATA_WIDTH,
                                      XtNlabel, "", NULL);
    dimbox = XtVaCreateManagedWidget("dimbox", labelWidgetClass,
                                     databox,
                                     XtNfromVert, namebox,
                                     XtNjustify, XtJustifyLeft,
                                     XtNwidth, DMX_DATA_WIDTH,
                                     XtNlabel, "", NULL);
    rtbox = XtVaCreateManagedWidget("rtbox", labelWidgetClass,
                                    databox,
                                    XtNfromVert, dimbox,
                                    XtNjustify, XtJustifyLeft,
                                    XtNwidth, DMX_DATA_WIDTH,
                                    XtNlabel, "", NULL);
    origbox = XtVaCreateManagedWidget("origbox", labelWidgetClass,
                                      databox,
                                      XtNfromVert, rtbox,
                                      XtNjustify, XtJustifyLeft,
                                      XtNwidth, DMX_DATA_WIDTH,
                                      XtNlabel, "", NULL);

    /* Canvas */
    canvasbox = XtVaCreateManagedWidget("canvasbox", boxWidgetClass,
                                        bottombox,
                                        XtNborderWidth, 0,
                                        XtNwidth, DMX_CANVAS_WIDTH,
                                        XtNheight, DMX_CANVAS_HEIGHT,
                                        XtNfromHoriz, databox, NULL);

    canvas = XtVaCreateManagedWidget("canvas", canvasWidgetClass,
                                     canvasbox,
                                     XtNwidth, DMX_CANVAS_WIDTH,
                                     XtNheight, DMX_CANVAS_HEIGHT, NULL);

    /* Main menu buttons */
    filebutton = XtVaCreateManagedWidget("File", menuButtonWidgetClass,
                                         menubox,
                                         XtNmenuName, "filemenu", NULL);
    helpbutton = XtVaCreateManagedWidget("Help", menuButtonWidgetClass,
                                         menubox,
                                         XtNmenuName, "helpmenu", NULL);

    /* File submenu buttons */
    filemenu = XtVaCreatePopupShell("filemenu", simpleMenuWidgetClass,
                                    filebutton, NULL);
    openbutton = XtVaCreateManagedWidget("Open File", smeBSBObjectClass,
                                         filemenu, NULL);
    savebutton = XtVaCreateManagedWidget("Save File", smeBSBObjectClass,
                                         filemenu, NULL);
    ncbutton = XtVaCreateManagedWidget("New Global", smeBSBObjectClass,
                                       filemenu, NULL);
    ecbutton = XtVaCreateManagedWidget("Edit Global", smeBSBObjectClass,
                                       filemenu, NULL);
    dcbutton = XtVaCreateManagedWidget("Delete Global", smeBSBObjectClass,
                                       filemenu, NULL);
    ndbutton0 = XtVaCreateManagedWidget("New Display", smeBSBObjectClass,
                                        filemenu, NULL);
    quitbutton = XtVaCreateManagedWidget("Quit", smeBSBObjectClass,
                                         filemenu, NULL);

    /* Help submenu button */
    helpmenu = XtVaCreatePopupShell("helpmenu", simpleMenuWidgetClass,
                                    helpbutton, NULL);
    aboutbutton = XtVaCreateManagedWidget("About", smeBSBObjectClass,
                                          helpmenu, NULL);

    /* Open popup */
    openpopup = XtVaCreatePopupShell("openpopup", transientShellWidgetClass,
                                     toplevel, NULL);
    opendialog = XtVaCreateManagedWidget("opendialog", dialogWidgetClass,
                                         openpopup,
                                         XtNlabel, "Filename: ",
                                         XtNvalue, dmxConfigFilename, NULL);
    okbutton = XtVaCreateManagedWidget("Open", commandWidgetClass,
                                       opendialog, NULL);
    canbutton = XtVaCreateManagedWidget("Cancel", commandWidgetClass,
                                        opendialog, NULL);

    /* EC popup */
    ecpopup = XtVaCreatePopupShell("ecpopup", transientShellWidgetClass,
                                   toplevel, NULL);
    ecbox = XtVaCreateManagedWidget("ecbox", boxWidgetClass, ecpopup, NULL);
    ecdialog0 = XtVaCreateManagedWidget("ecdialog0", dialogWidgetClass,
                                        ecbox,
                                        XtNlabel, "Name:              ",
                                        XtNvalue, "", NULL);
    ecdialog1 = XtVaCreateManagedWidget("ecdialog1", dialogWidgetClass,
                                        ecbox,
                                        XtNlabel, "Dimension:         ",
                                        XtNvalue, "", NULL);
    ecokbutton = XtVaCreateManagedWidget("OK", commandWidgetClass, ecbox, NULL);
    eccanbutton = XtVaCreateManagedWidget("Cancel", commandWidgetClass,
                                          ecbox, NULL);

    /* ED popup */
    edpopup = XtVaCreatePopupShell("edpopup", transientShellWidgetClass,
                                   toplevel, NULL);
    edbox = XtVaCreateManagedWidget("edbox", boxWidgetClass, edpopup, NULL);
    eddialog0 = XtVaCreateManagedWidget("eddialog0", dialogWidgetClass,
                                        edbox,
                                        XtNlabel, "Display Name:      ",
                                        XtNvalue, "", NULL);
    eddialog1 = XtVaCreateManagedWidget("eddialog1", dialogWidgetClass,
                                        edbox,
                                        XtNlabel, "Geometry:          ",
                                        XtNvalue, "", NULL);
    eddialog2 = XtVaCreateManagedWidget("eddialog2", dialogWidgetClass,
                                        edbox,
                                        XtNlabel, "Offset:            ",
                                        XtNvalue, "", NULL);
    edokbutton = XtVaCreateManagedWidget("OK", commandWidgetClass, edbox, NULL);
    edcanbutton = XtVaCreateManagedWidget("Cancel", commandWidgetClass,
                                          edbox, NULL);

    /* About popup */
    aboutpopup = XtVaCreatePopupShell("aboutpopup", transientShellWidgetClass,
                                      toplevel, NULL);
    aboutbox = XtVaCreateManagedWidget("aboutbox", boxWidgetClass,
                                       aboutpopup, NULL);
    XtVaCreateManagedWidget("abouttext", labelWidgetClass,
                            aboutbox, XtNlabel, DMX_INFO, NULL);
    aboutok = XtVaCreateManagedWidget("OK", commandWidgetClass, aboutbox, NULL);

    /* Quit popup */
    quitpopup = XtVaCreatePopupShell("quitpopup", transientShellWidgetClass,
                                     toplevel, NULL);
    quitbox = XtVaCreateManagedWidget("quitbox", boxWidgetClass,
                                      quitpopup, NULL);
    XtVaCreateManagedWidget("quittext", labelWidgetClass,
                            quitbox,
                            XtNlabel,
                            "Changes to the configuration\n"
                            "been made that have not yet\n"
                            "been saved.  Do you want to\n"
                            "quit without saving?", NULL);
    quitok = XtVaCreateManagedWidget("Quit WITHOUT Saving",
                                     commandWidgetClass, quitbox, NULL);
    quitcan = XtVaCreateManagedWidget("Continue Editing",
                                      commandWidgetClass, quitbox, NULL);

    /* Button popup */
    buttonpopup = XtVaCreatePopupShell("buttonpopup", simpleMenuWidgetClass,
                                       toplevel, NULL);
    ndbutton1 = XtVaCreateManagedWidget("New Display", smeBSBObjectClass,
                                        buttonpopup, NULL);
    edbutton = XtVaCreateManagedWidget("Edit Display", smeBSBObjectClass,
                                       buttonpopup, NULL);
    ddbutton = XtVaCreateManagedWidget("Delete Display", smeBSBObjectClass,
                                       buttonpopup, NULL);

    /* Callbacks */
    XtAddCallback(openbutton, XtNcallback, dmxConfigOpenCallback, NULL);
    XtAddCallback(savebutton, XtNcallback, dmxConfigSaveCallback, NULL);
    XtAddCallback(okbutton, XtNcallback, dmxConfigOkCallback, NULL);
    XtAddCallback(canbutton, XtNcallback, dmxConfigCanCallback, NULL);

    XtAppAddActions(appContext, actiontable, XtNumber(actiontable));
    XtOverrideTranslations(canvas, XtParseTranslationTable(canvastrans));
    XtOverrideTranslations(XtNameToWidget(opendialog, "value"),
                           XtParseTranslationTable(opentrans));
    XtOverrideTranslations(XtNameToWidget(ecdialog0, "value"),
                           XtParseTranslationTable(opentrans));
    XtOverrideTranslations(XtNameToWidget(ecdialog1, "value"),
                           XtParseTranslationTable(opentrans));
    XtOverrideTranslations(XtNameToWidget(eddialog0, "value"),
                           XtParseTranslationTable(opentrans));
    XtOverrideTranslations(XtNameToWidget(eddialog1, "value"),
                           XtParseTranslationTable(opentrans));
    XtOverrideTranslations(XtNameToWidget(eddialog2, "value"),
                           XtParseTranslationTable(opentrans));

    XtAddCallback(ncbutton, XtNcallback, dmxConfigNCCallback, NULL);
    XtAddCallback(ecbutton, XtNcallback, dmxConfigECCallback, NULL);
    XtAddCallback(ecokbutton, XtNcallback, dmxConfigECOkCallback, NULL);
    XtAddCallback(eccanbutton, XtNcallback, dmxConfigECCanCallback, NULL);
    XtAddCallback(dcbutton, XtNcallback, dmxConfigDCCallback, NULL);

    XtAddCallback(ndbutton0, XtNcallback, dmxConfigNDCallback, NULL);
    XtAddCallback(ndbutton1, XtNcallback, dmxConfigNDCallback, NULL);
    XtAddCallback(edbutton, XtNcallback, dmxConfigEDCallback, NULL);
    XtAddCallback(ddbutton, XtNcallback, dmxConfigDDCallback, NULL);
    XtAddCallback(edokbutton, XtNcallback, dmxConfigEDOkCallback, NULL);
    XtAddCallback(edcanbutton, XtNcallback, dmxConfigEDCanCallback, NULL);

    XtAddCallback(aboutbutton, XtNcallback, dmxConfigAboutCallback, NULL);
    XtAddCallback(aboutok, XtNcallback, dmxConfigAboutOkCallback, NULL);
    XtAddCallback(quitok, XtNcallback, dmxConfigQuitOkCallback, NULL);
    XtAddCallback(quitcan, XtNcallback, dmxConfigQuitCanCallback, NULL);

    XtAddCallback(quitbutton, XtNcallback, dmxConfigQuitCallback, NULL);

    XtAddCallback(canvas, XtNcallback, dmxConfigCanvasInput, NULL);
    XtAddCallback(canvas, XtNcanvasExposeCallback, dmxConfigCanvasExpose, NULL);
    XtAddCallback(canvas, XtNcanvasResizeCallback, dmxConfigCanvasResize, NULL);

    if (dmxConfigFilename)
        dmxConfigReadFile();

    XtRealizeWidget(toplevel);
    dmxConfigCopystrings();
    dmxConfigSetupCnamemenu();
    XtAppMainLoop(appContext);
    return 0;
}
