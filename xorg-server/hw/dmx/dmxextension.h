/*
 * Copyright 2003-2004 Red Hat Inc., Durham, North Carolina.
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
 * Author:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *   Kevin E. Martin <kem@redhat.com>
 *
 */

/** \file
 * Interface for DMX extension support.  These routines are called by
 * function in Xserver/Xext/dmx.c.  \see dmxextension.c */

#ifndef _DMXEXTENSION_H_
#define _DMXEXTENSION_H_

/** Screen attributes.  Used by #ProcDMXGetScreenAttributes and
 * \a ProcDMXChangeScreensAttributes. */
typedef struct {
    const char *displayName;
    int logicalScreen;

    unsigned int screenWindowWidth;     /* displayName's coordinate system */
    unsigned int screenWindowHeight;    /* displayName's coordinate system */
    int screenWindowXoffset;    /* displayName's coordinate system */
    int screenWindowYoffset;    /* displayName's coordinate system */

    unsigned int rootWindowWidth;       /* screenWindow's coordinate system */
    unsigned int rootWindowHeight;      /* screenWindow's coordinate system */
    int rootWindowXoffset;      /* screenWindow's coordinate system */
    int rootWindowYoffset;      /* screenWindow's coordinate system */

    int rootWindowXorigin;      /* global coordinate system */
    int rootWindowYorigin;      /* global coordinate system */
} DMXScreenAttributesRec, *DMXScreenAttributesPtr;

/** Window attributes.  Used by #ProcDMXGetWindowAttributes. */
typedef struct {
    int screen;
    Window window;
    xRectangle pos;
    xRectangle vis;
} DMXWindowAttributesRec, *DMXWindowAttributesPtr;

/** Desktop attributes.  Used by #ProcDMXGetDesktopAttributes and
 * #ProcDMXChangeDesktopAttributes. */
typedef struct {
    int width;
    int height;
    int shiftX;
    int shiftY;
} DMXDesktopAttributesRec, *DMXDesktopAttributesPtr;

/** Input attributes.  Used by #ProcDMXGetInputAttributes. */
typedef struct {
    const char *name;
    int inputType;
    int physicalScreen;
    int physicalId;
    int isCore;
    int sendsCore;
    int detached;
} DMXInputAttributesRec, *DMXInputAttributesPtr;

extern unsigned long dmxGetNumScreens(void);
extern void dmxForceWindowCreation(WindowPtr pWindow);
extern void dmxFlushPendingSyncs(void);
extern Bool dmxGetScreenAttributes(int physical, DMXScreenAttributesPtr attr);
extern Bool dmxGetWindowAttributes(WindowPtr pWindow,
                                   DMXWindowAttributesPtr attr);
extern void dmxGetDesktopAttributes(DMXDesktopAttributesPtr attr);
extern int dmxGetInputCount(void);
extern int dmxGetInputAttributes(int deviceId, DMXInputAttributesPtr attr);
extern int dmxAddInput(DMXInputAttributesPtr attr, int *deviceId);
extern int dmxRemoveInput(int deviceId);

extern int dmxConfigureScreenWindows(int nscreens,
                                     CARD32 *screens,
                                     DMXScreenAttributesPtr attribs,
                                     int *errorScreen);

extern int dmxConfigureDesktop(DMXDesktopAttributesPtr attribs);

/* dmxUpdateScreenResources exposed for dmxCreateWindow in dmxwindow.c */
extern void dmxUpdateScreenResources(ScreenPtr pScreen,
                                     int x, int y, int w, int h);

extern int dmxAttachScreen(int idx, DMXScreenAttributesPtr attr);
extern int dmxDetachScreen(int idx);
#endif
