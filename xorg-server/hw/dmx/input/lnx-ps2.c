/* Portions of this file were derived from the following files:
 *
 **********************************************************************
 *
 * Xserver/hw/kdrive/linux/ps2.c
 *
 * Copyright (c) 1999 by Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */

/*
 * Copyright 2001,2003 Red Hat Inc., Durham, North Carolina.
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
 * This code implements a low-level device driver for a serial MS mouse.
 * The code is derived from code by Keith Packard (see the source code
 * for complete references). */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "inputstr.h"
#include <X11/Xos.h>
#include <errno.h>
#include <termios.h>

/*****************************************************************************/
/* Define some macros to make it easier to move this file to another
 * part of the Xserver tree.  All calls to the dmx* layer are #defined
 * here for the .c file.  The .h file will also have to be edited. */
#include "dmxinputinit.h"
#include "lnx-ps2.h"

#define GETPRIV       myPrivate *priv                            \
                      = ((DMXLocalInputInfoPtr)(pDev->devicePrivate))->private

#define LOG0(f)       dmxLog(dmxDebug,f)
#define LOG1(f,a)     dmxLog(dmxDebug,f,a)
#define LOG2(f,a,b)   dmxLog(dmxDebug,f,a,b)
#define LOG3(f,a,b,c) dmxLog(dmxDebug,f,a,b,c)
#define FATAL0(f)     dmxLog(dmxFatal,f)
#define FATAL1(f,a)   dmxLog(dmxFatal,f,a)
#define FATAL2(f,a,b) dmxLog(dmxFatal,f,a,b)
#define MOTIONPROC    dmxMotionProcPtr
#define ENQUEUEPROC   dmxEnqueueProcPtr
#define CHECKPROC     dmxCheckSpecialProcPtr
#define BLOCK         DMXBlockType

/* End of interface definitions. */
/*****************************************************************************/

/* Private area for PS/2 devices. */
typedef struct _myPrivate {
    DeviceIntPtr pMouse;
    int fd;
    enum {
        button1 = 0x0001,
        button2 = 0x0002,
        button3 = 0x0004,
        button4 = 0x0008,
        button5 = 0x0010
    } buttons;
} myPrivate;

static int
ps2LinuxReadBytes(int fd, unsigned char *buf, int len, int min)
{
    int n, tot;
    fd_set set;
    struct timeval tv;

    tot = 0;
    while (len) {
        n = read(fd, buf, len);
        if (n > 0) {
            tot += n;
            buf += n;
            len -= n;
        }
        if (tot % min == 0)
            break;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        n = select(fd + 1, &set, 0, 0, &tv);
        if (n <= 0)
            break;
    }
    return tot;
}

static void
ps2LinuxButton(DevicePtr pDev, ENQUEUEPROC enqueue, int buttons, BLOCK block)
{
    GETPRIV;

#define PRESS(b)                                         \
    do {                                                 \
        enqueue(pDev, ButtonPress, 0, 0, NULL, block);   \
    } while (0)

#define RELEASE(b)                                       \
    do {                                                 \
        enqueue(pDev, ButtonRelease, 0, 0, NULL, block); \
    } while (0)

    if ((buttons & button1) && !(priv->buttons & button1))
        PRESS(1);
    if (!(buttons & button1) && (priv->buttons & button1))
        RELEASE(1);

    if ((buttons & button2) && !(priv->buttons & button2))
        PRESS(2);
    if (!(buttons & button2) && (priv->buttons & button2))
        RELEASE(2);

    if ((buttons & button3) && !(priv->buttons & button3))
        PRESS(3);
    if (!(buttons & button3) && (priv->buttons & button3))
        RELEASE(3);

    if ((buttons & button4) && !(priv->buttons & button4))
        PRESS(4);
    if (!(buttons & button4) && (priv->buttons & button4))
        RELEASE(4);

    if ((buttons & button5) && !(priv->buttons & button5))
        PRESS(5);
    if (!(buttons & button5) && (priv->buttons & button5))
        RELEASE(5);

    priv->buttons = buttons;
}

/** Read an event from the \a pDev device.  If the event is a motion
 * event, enqueue it with the \a motion function.  Otherwise, check for
 * special keys with the \a checkspecial function and enqueue the event
 * with the \a enqueue function.  The \a block type is passed to the
 * functions so that they may block SIGIO handling as appropriate to the
 * caller of this function. */
void
ps2LinuxRead(DevicePtr pDev, MOTIONPROC motion,
             ENQUEUEPROC enqueue, CHECKPROC checkspecial, BLOCK block)
{
    GETPRIV;
    unsigned char buf[3 * 200]; /* RATS: Use ok */
    unsigned char *b;
    int n;
    int dx, dy, v[2];

    while ((n = ps2LinuxReadBytes(priv->fd, buf, sizeof(buf), 3)) > 0) {
        b = buf;
        while (n >= 3) {
            dx = b[1] - ((b[0] & 0x10) ? 256 : 0);
            dy = -b[2] + ((b[0] & 0x20) ? 256 : 0);
            v[0] = -dx;
            v[1] = -dy;

            motion(pDev, v, 0, 2, 1, block);
            ps2LinuxButton(pDev, enqueue, (((b[0] & 4) ? button2 : 0)
                                           | ((b[0] & 2) ? button3 : 0)
                                           | ((b[0] & 1) ? button1 : 0)),
                           block);
            n -= 3;
            b += 3;
        }
    }
}

/** Initialize \a pDev. */
void
ps2LinuxInit(DevicePtr pDev)
{
    GETPRIV;
    const char *names[] = { "/dev/mouse", "/dev/psaux", NULL };
    int i;

    if (priv->fd >= 0)
        return;

    for (i = 0; names[i]; i++) {
        if ((priv->fd = open(names[i], O_RDWR | O_NONBLOCK, 0)) >= 0)
            break;
    }
    if (priv->fd < 0)
        FATAL1("ps2LinuxInit: Cannot open mouse port (%s)\n", strerror(errno));
}

/** Turn \a pDev on (i.e., take input from \a pDev). */
int
ps2LinuxOn(DevicePtr pDev)
{
    GETPRIV;

    if (priv->fd < 0)
        ps2LinuxInit(pDev);
    return priv->fd;
}

/** Turn \a pDev off (i.e., stop taking input from \a pDev). */
void
ps2LinuxOff(DevicePtr pDev)
{
    GETPRIV;

    close(priv->fd);
    priv->fd = -1;
}

static void
ps2LinuxGetMap(DevicePtr pDev, unsigned char *map, int *nButtons)
{
    int i;

    if (nButtons)
        *nButtons = 3;
    if (map)
        for (i = 0; i <= *nButtons; i++)
            map[i] = i;
}

/** Currently unused hook called prior to an VT switch. */
void
ps2LinuxVTPreSwitch(void *p)
{
}

/** Currently unused hook called after returning from a VT switch. */
void
ps2LinuxVTPostSwitch(void *p)
{
}

/** Create a private structure for use within this file. */
void *
ps2LinuxCreatePrivate(DeviceIntPtr pMouse)
{
    myPrivate *priv = calloc(1, sizeof(*priv));

    priv->fd = -1;
    priv->pMouse = pMouse;
    return priv;
}

/** Destroy a private structure. */
void
ps2LinuxDestroyPrivate(void *priv)
{
    free(priv);
}

/** Fill the \a info structure with information needed to initialize \a
 * pDev. */
void
ps2LinuxGetInfo(DevicePtr pDev, DMXLocalInitInfoPtr info)
{
    info->buttonClass = 1;
    ps2LinuxGetMap(pDev, info->map, &info->numButtons);
    info->valuatorClass = 1;
    info->numRelAxes = 2;
    info->minval[0] = 0;
    info->maxval[0] = 0;
    info->res[0] = 1;
    info->minres[0] = 0;
    info->maxres[0] = 1;
    info->ptrFeedbackClass = 1;
}
