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
 * Routines that are common between \a usb-keyboard.c, \a usb-mouse.c, and
 * \a usb-other.c */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "usb-private.h"

#define USB_COMMON_DEBUG 1

/*****************************************************************************/
/* Define some macros to make it easier to move this file to another
 * part of the Xserver tree.  All calls to the dmx* layer are #defined
 * here for the .c file.  The .h file will also have to be edited. */
#include "usb-mouse.h"

#define GETPRIV       myPrivate *priv                                         \
                      = ((DMXLocalInputInfoPtr)(pDev->devicePrivate))->private

#define GETNAME       ((DMXLocalInputInfoPtr)(pDevice->public.devicePrivate)) \
                      ->name

#define LOG0(f)       dmxLog(dmxDebug,f)
#define LOG1(f,a)     dmxLog(dmxDebug,f,a)
#define LOG2(f,a,b)   dmxLog(dmxDebug,f,a,b)
#define LOG3(f,a,b,c) dmxLog(dmxDebug,f,a,b,c)
#define LOG1INPUT(p,f,a)         dmxLogInput(p->dmxInput,f,a)
#define LOG3INPUT(p,f,a,b,c)     dmxLogInput(p->dmxInput,f,a,b,c)
#define LOG5INPUT(p,f,a,b,c,d,e) dmxLogInput(p->dmxInput,f,a,b,c,d,e)
#define FATAL0(f)     dmxLog(dmxFatal,f)
#define FATAL1(f,a)   dmxLog(dmxFatal,f,a)
#define FATAL2(f,a,b) dmxLog(dmxFatal,f,a,b)
#define MOTIONPROC    dmxMotionProcPtr
#define ENQUEUEPROC   dmxEnqueueProcPtr
#define CHECKPROC     dmxCheckSpecialProcPtr
#define BLOCK         DMXBlockType

/* End of interface definitions. */
/*****************************************************************************/

/** Read an event from the \a pDev device.  If the event is a motion
 * event, enqueue it with the \a motion function.  Otherwise, enqueue
 * the event with the \a enqueue function.  The \a block type is passed
 * to the functions so that they may block SIGIO handling as appropriate
 * to the caller of this function.
 *
 * Since USB devices return EV_KEY events for buttons and keys, \a
 * minButton is used to decide if a Button or Key event should be
 * queued.*/
void
usbRead(DevicePtr pDev,
        MOTIONPROC motion, ENQUEUEPROC enqueue, int minButton, BLOCK block)
{
    GETPRIV;
    struct input_event raw;
    int v[DMX_MAX_AXES];
    int axis;

#define PRESS(b)                                         \
    do {                                                 \
        enqueue(pDev, ButtonPress, 0, 0, NULL, block);   \
    } while (0)

#define RELEASE(b)                                       \
    do {                                                 \
        enqueue(pDev, ButtonRelease, 0, 0, NULL, block); \
    } while (0)

    while (read(priv->fd, &raw, sizeof(raw)) > 0) {
#if USB_COMMON_DEBUG > 1
        LOG3("USB: type = %d, code = 0x%02x, value = %d\n",
             raw.type, raw.code, raw.value);
#endif
        switch (raw.type) {
        case EV_KEY:
            /* raw.value = 1 for first, 2 for repeat */
            if (raw.code > minButton) {
                if (raw.value)
                    PRESS((raw.code & 0x0f) + 1);
                else
                    RELEASE((raw.code & 0x0f) + 1);
            }
            else {
                enqueue(pDev, raw.value ? KeyPress : KeyRelease,
                        0, 0, NULL, block);
            }
            break;
        case EV_REL:
            switch (raw.code) {
            case REL_X:
                v[0] = -raw.value;
                v[1] = 0;
                motion(pDev, v, 0, 2, DMX_RELATIVE, block);
                break;
            case REL_Y:
                v[0] = 0;
                v[1] = -raw.value;
                motion(pDev, v, 0, 2, DMX_RELATIVE, block);
                break;
            case REL_WHEEL:
                if ((int) raw.value > 0) {
                    PRESS(4);
                    RELEASE(4);
                }
                else if ((int) raw.value < 0) {
                    PRESS(5);
                    RELEASE(5);
                }
                break;
            default:
                memset(v, 0, sizeof(v));
                axis = priv->relmap[raw.code];
                v[axis] = raw.value;
                motion(pDev, v, axis, 1, DMX_RELATIVE, block);
            }
            break;
        case EV_ABS:
            memset(v, 0, sizeof(v));
            axis = priv->absmap[raw.code];
            v[axis] = raw.value;
            motion(pDev, v, axis, 1, DMX_ABSOLUTE, block);
            break;
        }
    }
}

#define test_bit(bit)  (priv->mask[(bit)/8] & (1 << ((bit)%8)))
#define test_bits(bit) (bits[(bit)/8] & (1 << ((bit)%8)))

static void
usbPrint(myPrivate * priv, const char *filename, const char *devname, int fd)
{
    int j, k;
    DeviceIntPtr pDevice = priv->pDevice;
    unsigned char bits[KEY_MAX / 8 + 1];        /* RATS: Use ok assuming that
                                                 * KEY_MAX is greater than
                                                 * REL_MAX, ABS_MAX, SND_MAX, and
                                                 * LED_MAX. */

    LOG3INPUT(priv, "%s (%s) using %s\n", pDevice->name, GETNAME, filename);
    LOG1INPUT(priv, "    %s\n", devname);
    for (j = 0; j < EV_MAX; j++) {
        if (test_bit(j)) {
            const char *type = "unknown";
            char extra[256];    /* FIXME: may cause buffer overflow */

            extra[0] = '\0';
            switch (j) {
            case EV_KEY:
                type = "keys/buttons";
                break;
            case EV_REL:
                type = "relative";
                memset(bits, 0, sizeof(bits));
                ioctl(priv->fd, EVIOCGBIT(EV_REL, sizeof(bits)), bits);
                for (k = 0; k < REL_MAX; k++) {
                    if (test_bits(k))
                        switch (k) {
                        case REL_X:
                            strcat(extra, " X");
                            break;
                        case REL_Y:
                            strcat(extra, " Y");
                            break;
                        case REL_Z:
                            strcat(extra, " Z");
                            break;
                        case REL_HWHEEL:
                            strcat(extra, " HWheel");
                            break;
                        case REL_DIAL:
                            strcat(extra, " Dial");
                            break;
                        case REL_WHEEL:
                            strcat(extra, " Wheel");
                            break;
                        case REL_MISC:
                            strcat(extra, " Misc");
                            break;
                        }
                }
                break;
            case EV_ABS:
                type = "absolute";
                memset(bits, 0, sizeof(bits));
                ioctl(priv->fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits);
                for (k = 0; k < ABS_MAX; k++) {
                    if (test_bits(k))
                        switch (k) {
                        case ABS_X:
                            strcat(extra, " X");
                            break;
                        case ABS_Y:
                            strcat(extra, " Y");
                            break;
                        case ABS_Z:
                            strcat(extra, " Z");
                            break;
                        case ABS_RX:
                            strcat(extra, " RX");
                            break;
                        case ABS_RY:
                            strcat(extra, " RY");
                            break;
                        case ABS_RZ:
                            strcat(extra, " RZ");
                            break;
                        case ABS_THROTTLE:
                            strcat(extra, " Throttle");
                            break;
                        case ABS_RUDDER:
                            strcat(extra, " Rudder");
                            break;
                        case ABS_WHEEL:
                            strcat(extra, " Wheel");
                            break;
                        case ABS_GAS:
                            strcat(extra, " Gas");
                            break;
                        case ABS_BRAKE:
                            strcat(extra, " Break");
                            break;
                        case ABS_HAT0X:
                            strcat(extra, " Hat0X");
                            break;
                        case ABS_HAT0Y:
                            strcat(extra, " Hat0Y");
                            break;
                        case ABS_HAT1X:
                            strcat(extra, " Hat1X");
                            break;
                        case ABS_HAT1Y:
                            strcat(extra, " Hat1Y");
                            break;
                        case ABS_HAT2X:
                            strcat(extra, " Hat2X");
                            break;
                        case ABS_HAT2Y:
                            strcat(extra, " Hat2Y");
                            break;
                        case ABS_HAT3X:
                            strcat(extra, " Hat3X");
                            break;
                        case ABS_HAT3Y:
                            strcat(extra, " Hat3Y");
                            break;
                        case ABS_PRESSURE:
                            strcat(extra, " Pressure");
                            break;
                        case ABS_DISTANCE:
                            strcat(extra, " Distance");
                            break;
                        case ABS_TILT_X:
                            strcat(extra, " TiltX");
                            break;
                        case ABS_TILT_Y:
                            strcat(extra, " TiltY");
                            break;
                        case ABS_MISC:
                            strcat(extra, " Misc");
                            break;
                        }
                }
                break;
            case EV_MSC:
                type = "reserved";
                break;
            case EV_LED:
                type = "leds";
                memset(bits, 0, sizeof(bits));
                ioctl(priv->fd, EVIOCGBIT(EV_LED, sizeof(bits)), bits);
                for (k = 0; k < LED_MAX; k++) {
                    if (test_bits(k))
                        switch (k) {
                        case LED_NUML:
                            strcat(extra, " NumLock");
                            break;
                        case LED_CAPSL:
                            strcat(extra, " CapsLock");
                            break;
                        case LED_SCROLLL:
                            strcat(extra, " ScrlLock");
                            break;
                        case LED_COMPOSE:
                            strcat(extra, " Compose");
                            break;
                        case LED_KANA:
                            strcat(extra, " Kana");
                            break;
                        case LED_SLEEP:
                            strcat(extra, " Sleep");
                            break;
                        case LED_SUSPEND:
                            strcat(extra, " Suspend");
                            break;
                        case LED_MUTE:
                            strcat(extra, " Mute");
                            break;
                        case LED_MISC:
                            strcat(extra, " Misc");
                            break;
                        }
                }
                break;
            case EV_SND:
                type = "sound";
                memset(bits, 0, sizeof(bits));
                ioctl(priv->fd, EVIOCGBIT(EV_SND, sizeof(bits)), bits);
                for (k = 0; k < SND_MAX; k++) {
                    if (test_bits(k))
                        switch (k) {
                        case SND_CLICK:
                            strcat(extra, " Click");
                            break;
                        case SND_BELL:
                            strcat(extra, " Bell");
                            break;
                        }
                }
                break;
            case EV_REP:
                type = "repeat";
                break;
            case EV_FF:
                type = "feedback";
                break;
            }
            LOG5INPUT(priv, "    Feature 0x%02x = %s%s%s%s\n", j, type,
                      extra[0] ? " [" : "",
                      extra[0] ? extra + 1 : "", extra[0] ? "]" : "");
        }
    }
}

/** Initialized \a pDev as a \a usbMouse, \a usbKeyboard, or \a usbOther
device. */
void
usbInit(DevicePtr pDev, usbType type)
{
    GETPRIV;
    char name[64];              /* RATS: Only used in snprintf */
    int i, j, k;
    char buf[256] = { 0, };     /* RATS: Use ok */
    int version;
    unsigned char bits[KEY_MAX / 8 + 1];        /* RATS: Use ok assuming that
                                                 * KEY_MAX is greater than
                                                 * REL_MAX, ABS_MAX, SND_MAX, and
                                                 * LED_MAX. */

    if (priv->fd >= 0)
        return;

    for (i = 0; i < 32; i++) {
        snprintf(name, sizeof(name), "/dev/input/event%d", i);
        if ((priv->fd = open(name, O_RDWR | O_NONBLOCK, 0)) >= 0) {
            ioctl(priv->fd, EVIOCGVERSION, &version);
            ioctl(priv->fd, EVIOCGNAME(sizeof(buf)), buf);
            memset(priv->mask, 0, sizeof(priv->mask));
            ioctl(priv->fd, EVIOCGBIT(0, sizeof(priv->mask)), priv->mask);

            for (j = 0; j < EV_MAX; j++) {
                if (test_bit(j)) {
                    switch (j) {
                    case EV_REL:
                        memset(bits, 0, sizeof(bits));
                        ioctl(priv->fd, EVIOCGBIT(EV_REL, sizeof(bits)), bits);
                        for (k = 0; k < REL_MAX; k++) {
                            if (test_bits(k)) {
                                if (k == REL_X)
                                    priv->relmap[k] = 0;
                                else if (k == REL_Y)
                                    priv->relmap[k] = 1;
                                else
                                    priv->relmap[k] = 2 + priv->numAbs;
                                ++priv->numRel;
                            }
                        }
                        break;
                    case EV_ABS:
                        memset(bits, 0, sizeof(bits));
                        ioctl(priv->fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits);
                        for (k = 0; k < ABS_MAX; k++) {
                            if (test_bits(k)) {
                                priv->absmap[k] = priv->numAbs;
                                ++priv->numAbs;
                            }
                        }
                        break;
                    case EV_LED:
                        memset(bits, 0, sizeof(bits));
                        ioctl(priv->fd, EVIOCGBIT(EV_LED, sizeof(bits)), bits);
                        for (k = 0; k < LED_MAX; k++) {
                            if (test_bits(k))
                                ++priv->numLeds;
                        }
                        break;
                    }
                }
            }
            switch (type) {
            case usbMouse:
                if (test_bit(EV_REL) && test_bit(EV_KEY))
                    goto found;
                break;
            case usbKeyboard:
                if (test_bit(EV_KEY) && test_bit(EV_LED) && !test_bit(EV_ABS))
                    goto found;
                break;
            case usbOther:
                if (!(test_bit(EV_REL) && test_bit(EV_KEY))
                    && !(test_bit(EV_KEY) && test_bit(EV_LED)
                         && !test_bit(EV_ABS)))
                    goto found;
                break;
            }
            close(priv->fd);
            priv->fd = -1;
        }
    }
    if (priv->fd < 0)
        FATAL1("usbInit: Cannot open /dev/input/event* port (%s)\n"
               "         If you have not done so, you may need to:\n"
               "           rmmod mousedev; rmmod keybdev\n"
               "           modprobe evdev\n", strerror(errno));
 found:
    usbPrint(priv, name, buf, priv->fd);
}

/** Turn \a pDev off (i.e., stop taking input from \a pDev). */
void
usbOff(DevicePtr pDev)
{
    GETPRIV;

    if (priv->fd >= 0)
        close(priv->fd);
    priv->fd = -1;
}

/** Create a private structure for use within this file. */
void *
usbCreatePrivate(DeviceIntPtr pDevice)
{
    myPrivate *priv = calloc(1, sizeof(*priv));

    priv->fd = -1;
    priv->pDevice = pDevice;
    return priv;
}

/** Destroy a private structure. */
void
usbDestroyPrivate(void *priv)
{
    free(priv);
}
