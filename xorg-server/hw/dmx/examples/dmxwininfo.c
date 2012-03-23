/*
 * Copyright (c) 2003 by the XFree86 Project, Inc.
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
 * Create a window and use the DMX extension to query the window's
 * back-end properties.  Display the info inside the window itself.
 *
 * Brian Paul
 * 23 Jan 2003
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/dmxext.h>

static const char *FontName = "fixed";

static void
EventLoop(Display * dpy, Window win, GC gc)
{
    XEvent ev;

    while (1) {
        XNextEvent(dpy, &ev);
        switch (ev.type) {
        case ReparentNotify:
            break;
        case MapNotify:
            break;
        case ConfigureNotify:
        case Expose:
        {
            int numScreens, count, i;
            DMXWindowAttributes *winInfo;
            int x, y;
            const char *msg = "DMX window info:";

            DMXGetScreenCount(dpy, &numScreens);
            winInfo = (DMXWindowAttributes *)
                malloc(numScreens * sizeof(DMXWindowAttributes));
            assert(winInfo);
            if (!DMXGetWindowAttributes(dpy, win, &count, numScreens, winInfo)) {
                printf("Could not get window information for 0x%08lx\n",
                       (long unsigned) win);
            }
            x = y = 50;
            XClearWindow(dpy, win);
            XDrawString(dpy, win, gc, x, y, msg, strlen(msg));
            y += 20;
            for (i = 0; i < count; i++) {
                char str[500];

                snprintf(str, sizeof(str),
                         "screen %d:  pos: %dx%d+%d+%d  visible: %dx%d+%d+%d",
                         winInfo[i].screen,
                         winInfo[i].pos.width, winInfo[i].pos.height,
                         winInfo[i].pos.x, winInfo[i].pos.y,
                         winInfo[i].vis.width, winInfo[i].vis.height,
                         winInfo[i].vis.x, winInfo[i].vis.y);
                XDrawString(dpy, win, gc, x, y, str, strlen(str));
                y += 20;
            }
            free(winInfo);
        }
            break;
        default:
            printf("Event type 0x%x\n", ev.type);
        }
    }
}

int
main(int argc, char *argv[])
{
    const char *displayName = NULL;
    Display *dpy;
    int event_base, error_base;
    int scr, n;
    long vinfoMask, attrMask;
    XVisualInfo vinfoTemp, *visInfo;
    Visual *vis;
    Window win, root;
    XSetWindowAttributes attr;
    XFontStruct *fontInfo;
    GC gc;

    if (argc > 1) {
        displayName = argv[1];
    }

    dpy = XOpenDisplay(displayName);
    if (!dpy) {
        fprintf(stderr, "Unable to open display %s\n", displayName);
        return -1;
    }

    if (!DMXQueryExtension(dpy, &event_base, &error_base)) {
        fprintf(stderr, "DMX extension not available on this display.\n");
        return -1;
    }

    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    vis = DefaultVisual(dpy, scr);

    vinfoMask = VisualIDMask;
    vinfoTemp.visualid = vis->visualid;
    visInfo = XGetVisualInfo(dpy, vinfoMask, &vinfoTemp, &n);
    if (!visInfo || n != 1) {
        fprintf(stderr, "Unable to get visual!\n");
        XCloseDisplay(dpy);
        return -1;
    }

    attr.background_pixel = 0;
    attr.border_pixel = 0;
    attr.colormap = XCreateColormap(dpy, root, visInfo->visual, AllocNone);
    attr.event_mask = StructureNotifyMask | ExposureMask;
    attrMask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

    win = XCreateWindow(dpy, root, 500, 500, 600, 400,  /* x, y, w, h */
                        0,      /* border_width */
                        visInfo->depth, InputOutput,
                        visInfo->visual, attrMask, &attr);

    if (!win) {
        fprintf(stderr, "Unable to create window!\n");
        XCloseDisplay(dpy);
        return -1;
    }

    fontInfo = XLoadQueryFont(dpy, FontName);
    if (!fontInfo) {
        fprintf(stderr, "Error: font %s not found\n", FontName);
        exit(0);
    }

    gc = XCreateGC(dpy, win, 0, NULL);
    XSetBackground(dpy, gc, BlackPixel(dpy, scr));
    XSetForeground(dpy, gc, WhitePixel(dpy, scr));
    XSetFont(dpy, gc, fontInfo->fid);

    XMapWindow(dpy, win);

    EventLoop(dpy, win, gc);

    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}

#if 00

static void
make_window(char *title, int color_flag)
{
    int x = 10, y = 10, width = 400, height = 300;
    Display *dpy;
    int scr;
    Window root, win;
    Colormap cmap;
    XColor xcolor;
    int attr_flags;
    XVisualInfo *visinfo;
    XSetWindowAttributes attr;
    XTextProperty tp;
    XSizeHints sh;
    XEvent e;
    XMesaContext context;
    XMesaVisual visual;
    XMesaBuffer buffer;

    /*
     * Do the usual X things to make a window.
     */

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        printf("Couldn't open default display!\n");
        exit(1);
    }

    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);

    /* alloc visinfo struct */
    visinfo = (XVisualInfo *) malloc(sizeof(XVisualInfo));

    /* Get a visual and colormap */
    if (color_flag) {
        /* Open TrueColor window */

/*
      if (!XMatchVisualInfo( dpy, scr, 24, TrueColor, visinfo )) {
	 printf("Couldn't get 24-bit TrueColor visual!\n");
	 exit(1);
      }
*/
        if (!XMatchVisualInfo(dpy, scr, 8, PseudoColor, visinfo)) {
            printf("Couldn't get 8-bit PseudoColor visual!\n");
            exit(1);
        }

        cmap = XCreateColormap(dpy, root, visinfo->visual, AllocNone);
        Black = Red = Green = Blue = 0;
    }
    else {
        /* Open color index window */

        if (!XMatchVisualInfo(dpy, scr, 8, PseudoColor, visinfo)) {
            printf("Couldn't get 8-bit PseudoColor visual\n");
            exit(1);
        }

        cmap = XCreateColormap(dpy, root, visinfo->visual, AllocNone);

        /* Allocate colors */
        xcolor.red = 0x0;
        xcolor.green = 0x0;
        xcolor.blue = 0x0;
        xcolor.flags = DoRed | DoGreen | DoBlue;
        if (!XAllocColor(dpy, cmap, &xcolor)) {
            printf("Couldn't allocate black!\n");
            exit(1);
        }
        Black = xcolor.pixel;

        xcolor.red = 0xffff;
        xcolor.green = 0x0;
        xcolor.blue = 0x0;
        xcolor.flags = DoRed | DoGreen | DoBlue;
        if (!XAllocColor(dpy, cmap, &xcolor)) {
            printf("Couldn't allocate red!\n");
            exit(1);
        }
        Red = xcolor.pixel;

        xcolor.red = 0x0;
        xcolor.green = 0xffff;
        xcolor.blue = 0x0;
        xcolor.flags = DoRed | DoGreen | DoBlue;
        if (!XAllocColor(dpy, cmap, &xcolor)) {
            printf("Couldn't allocate green!\n");
            exit(1);
        }
        Green = xcolor.pixel;

        xcolor.red = 0x0;
        xcolor.green = 0x0;
        xcolor.blue = 0xffff;
        xcolor.flags = DoRed | DoGreen | DoBlue;
        if (!XAllocColor(dpy, cmap, &xcolor)) {
            printf("Couldn't allocate blue!\n");
            exit(1);
        }
        Blue = xcolor.pixel;
    }

    /* set window attributes */
    attr.colormap = cmap;
    attr.event_mask = ExposureMask | StructureNotifyMask;
    attr.border_pixel = BlackPixel(dpy, scr);
    attr.background_pixel = BlackPixel(dpy, scr);
    attr_flags = CWColormap | CWEventMask | CWBorderPixel | CWBackPixel;

    /* Create the window */
    win = XCreateWindow(dpy, root, x, y, width, height, 0,
                        visinfo->depth, InputOutput,
                        visinfo->visual, attr_flags, &attr);
    if (!win) {
        printf("Couldn't open window!\n");
        exit(1);
    }

    XStringListToTextProperty(&title, 1, &tp);
    sh.flags = USPosition | USSize;
    XSetWMProperties(dpy, win, &tp, &tp, 0, 0, &sh, 0, 0);
    XMapWindow(dpy, win);
    while (1) {
        XNextEvent(dpy, &e);
        if (e.type == MapNotify && e.xmap.window == win) {
            break;
        }
    }

    /*
     * Now do the special Mesa/Xlib stuff!
     */

    visual = XMesaCreateVisual(dpy, visinfo, (GLboolean) color_flag, GL_FALSE,  /* alpha_flag */
                               GL_FALSE,        /* db_flag */
                               GL_FALSE,        /* stereo flag */
                               GL_FALSE,        /* ximage_flag */
                               0,       /* depth size */
                               0,       /* stencil size */
                               0, 0, 0, 0,      /* accum_size */
                               0,       /* num samples */
                               0,       /* level */
                               0        /* caveat */
        );
    if (!visual) {
        printf("Couldn't create Mesa/X visual!\n");
        exit(1);
    }

    /* Create a Mesa rendering context */
    context = XMesaCreateContext(visual, NULL   /* share_list */
        );
    if (!context) {
        printf("Couldn't create Mesa/X context!\n");
        exit(1);
    }

    buffer = XMesaCreateWindowBuffer(visual, win);
    if (!buffer) {
        printf("Couldn't create Mesa/X buffer!\n");
        exit(1);
    }

    XMesaMakeCurrent(context, buffer);

    /* Ready to render! */
}

static void
draw_cube(void)
{
    /* X faces */
    glIndexi(Red);
    glColor3f(1.0, 0.0, 0.0);
    glBegin(GL_POLYGON);
    glVertex3f(1.0, 1.0, 1.0);
    glVertex3f(1.0, -1.0, 1.0);
    glVertex3f(1.0, -1.0, -1.0);
    glVertex3f(1.0, 1.0, -1.0);
    glEnd();

    glBegin(GL_POLYGON);
    glVertex3f(-1.0, 1.0, 1.0);
    glVertex3f(-1.0, 1.0, -1.0);
    glVertex3f(-1.0, -1.0, -1.0);
    glVertex3f(-1.0, -1.0, 1.0);
    glEnd();

    /* Y faces */
    glIndexi(Green);
    glColor3f(0.0, 1.0, 0.0);
    glBegin(GL_POLYGON);
    glVertex3f(1.0, 1.0, 1.0);
    glVertex3f(1.0, 1.0, -1.0);
    glVertex3f(-1.0, 1.0, -1.0);
    glVertex3f(-1.0, 1.0, 1.0);
    glEnd();

    glBegin(GL_POLYGON);
    glVertex3f(1.0, -1.0, 1.0);
    glVertex3f(-1.0, -1.0, 1.0);
    glVertex3f(-1.0, -1.0, -1.0);
    glVertex3f(1.0, -1.0, -1.0);
    glEnd();

    /* Z faces */
    glIndexi(Blue);
    glColor3f(0.0, 0.0, 1.0);
    glBegin(GL_POLYGON);
    glVertex3f(1.0, 1.0, 1.0);
    glVertex3f(-1.0, 1.0, 1.0);
    glVertex3f(-1.0, -1.0, 1.0);
    glVertex3f(1.0, -1.0, 1.0);
    glEnd();

    glBegin(GL_POLYGON);
    glVertex3f(1.0, 1.0, -1.0);
    glVertex3f(1.0, -1.0, -1.0);
    glVertex3f(-1.0, -1.0, -1.0);
    glVertex3f(-1.0, 1.0, -1.0);
    glEnd();
}

static void
display_loop(void)
{
    GLfloat xrot, yrot, zrot;

    xrot = yrot = zrot = 0.0;

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClearIndex(Black);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -1.0, 1.0, 1.0, 10.0);
    glTranslatef(0.0, 0.0, -5.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);

    glShadeModel(GL_FLAT);

    while (1) {
        glClear(GL_COLOR_BUFFER_BIT);
        glPushMatrix();
        glRotatef(xrot, 1.0, 0.0, 0.0);
        glRotatef(yrot, 0.0, 1.0, 0.0);
        glRotatef(zrot, 0.0, 0.0, 1.0);

        draw_cube();

        glPopMatrix();
        glFinish();

        xrot += 10.0;
        yrot += 7.0;
        zrot -= 3.0;
    }

}

int
main(int argc, char *argv[])
{
    int mode = 0;

    if (argc >= 2) {
        if (strcmp(argv[1], "-ci") == 0)
            mode = 0;
        else if (strcmp(argv[1], "-rgb") == 0)
            mode = 1;
        else {
            printf("Bad flag: %s\n", argv[1]);
            printf("Specify -ci for 8-bit color index or -rgb for RGB mode\n");
            exit(1);
        }
    }
    else {
        printf("Specify -ci for 8-bit color index or -rgb for RGB mode\n");
        printf("Defaulting to  8-bit color index\n");
    }

    make_window(argv[0], mode);

    display_loop();
    return 0;
}

#endif
