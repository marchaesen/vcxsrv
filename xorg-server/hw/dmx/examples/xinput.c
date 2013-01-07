/*
 * Copyright 2001,2002 Red Hat Inc., Durham, North Carolina.
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
#include <string.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XKB.h>
#include <X11/extensions/XKBstr.h>
#include <X11/extensions/dmxext.h>
#include <sys/time.h>

static const char *
core(DMXInputAttributes * iinf)
{
    if (iinf->isCore)
        return "core";
    else if (iinf->sendsCore)
        return "extension (sends core events)";
    else
        return "extension";
}

static void
printdmxinfo(Display * display, int id)
{
    int event_base;
    int error_base;
    int major_version, minor_version, patch_version;
    DMXInputAttributes iinf;
    Display *backend;
    char *backendname = NULL;

    if (!DMXQueryExtension(display, &event_base, &error_base))
        return;
    if (!DMXQueryVersion(display, &major_version, &minor_version,
                         &patch_version))
        return;
    if (major_version == 1 && minor_version == 0)
        return;                 /* too old */
    if (!DMXGetInputAttributes(display, id, &iinf))
        return;

    printf("   DMX Information: ");
    if (iinf.detached)
        printf("detached ");
    else
        printf("active   ");
    switch (iinf.inputType) {
    case DMXLocalInputType:
        printf("local, %s", core(&iinf));
        break;
    case DMXConsoleInputType:
        printf("console %s, %s", iinf.name, core(&iinf));
        break;
    case DMXBackendInputType:
        if (iinf.physicalId >= 0) {
            if ((backend = XOpenDisplay(iinf.name))) {
                XExtensionVersion *ext = XGetExtensionVersion(backend, INAME);

                if (ext && ext != (XExtensionVersion *) NoSuchExtension) {
                    int count, i;
                    XDeviceInfo *devInfo = XListInputDevices(backend, &count);

                    if (devInfo) {
                        for (i = 0; i < count; i++) {
                            if ((unsigned) iinf.physicalId == devInfo[i].id
                                && devInfo[i].name) {
                                backendname = strdup(devInfo[i].name);
                                break;
                            }
                        }
                        XFreeDeviceList(devInfo);
                    }
                }
                XCloseDisplay(backend);
            }
        }
        printf("backend o%d/%s", iinf.physicalScreen, iinf.name);
        if (iinf.physicalId >= 0)
            printf("/id%d", iinf.physicalId);
        if (backendname) {
            printf("=%s", backendname);
            free(backendname);
        }
        printf(" %s", core(&iinf));
        break;
    }
    printf("\n");
}

int
main(int argc, char **argv)
{
    Display *display = NULL;
    int device = -1;
    int newmouse = -1;
    int newkbd = -1;
    int count;
    int i, j;
    XDeviceInfo *devInfo;
    XExtensionVersion *ext;

    if (argc == 2 || argc == 3 || argc == 4 || argc == 5) {
        if (!(display = XOpenDisplay(argv[1]))) {
            printf("Cannot open display %s\n", argv[1]);
            return -1;
        }
        if (argc >= 3)
            device = strtol(argv[2], NULL, 0);
        if (argc >= 4)
            newmouse = strtol(argv[3], NULL, 0);
        if (argc >= 5)
            newkbd = strtol(argv[4], NULL, 0);
    }
    else {
        printf("Usage: %s display [device] [newmouse] [newkbd]\n", argv[0]);
        return -1;
    }

    if (!display && !(display = XOpenDisplay(NULL))) {
        printf("Cannot open default display\n");
        return -1;
    }

    ext = XGetExtensionVersion(display, INAME);
    if (!ext || ext == (XExtensionVersion *) NoSuchExtension) {
        printf("No XInputExtension\n");
        return -1;
    }
    printf("%s version %d.%d\n", INAME, ext->major_version, ext->minor_version);

    if (!(devInfo = XListInputDevices(display, &count)) || !count) {
        printf("Cannot list devices\n");
        return -1;
    }

    for (i = 0; i < count; i++) {
        XAnyClassPtr any;
        const char *kind = "Unknown";
        int has_key = 0;

        switch (devInfo[i].use) {
        case IsXPointer:
            kind = "XPointer";
            break;
        case IsXKeyboard:
            kind = "XKeyboard";
            break;
        case IsXExtensionDevice:
            kind = "XExtensionDevice";
            break;
        }
        printf("%2lu %-20.20s %-16.16s",
               (long unsigned) devInfo[i].id,
               devInfo[i].name ? devInfo[i].name : "", kind);

        for (j = 0, any = devInfo[i].inputclassinfo;
             j < devInfo[i].num_classes;
             any = (XAnyClassPtr) ((char *) any + any->length), j++) {
            const char *class = "unk";

            switch (any->class) {
            case KeyClass:
                class = "key";
                ++has_key;
                break;
            case ButtonClass:
                class = "btn";
                break;
            case ValuatorClass:
                class = "val";
                break;
            case FeedbackClass:
                class = "fdb";
                break;
            case ProximityClass:
                class = "prx";
                break;
            case FocusClass:
                class = "foc";
                break;
            case OtherClass:
                class = "oth";
                break;
            }
            printf(" %s", class);
        }
        printf("\n");
        printdmxinfo(display, i);

        if (has_key) {
            XkbDescPtr xkb;

            if ((xkb = XkbGetKeyboard(display,
                                      XkbAllComponentsMask, devInfo[i].id))) {
                printf("   Xkb Information:\n");
                printf("      Device id = %d\n", xkb->device_spec);
                printf("      Min keycode = 0x%02x\n", xkb->min_key_code);
                printf("      Max keycode = 0x%02x\n", xkb->max_key_code);
#define PRINTNAME(x)                                                     \
    printf("      %s = %s\n",                                            \
           #x, xkb->names->x ? XGetAtomName(display, xkb->names->x) : "")
                PRINTNAME(keycodes);
                PRINTNAME(geometry);
                PRINTNAME(symbols);
                PRINTNAME(types);
                PRINTNAME(compat);
            }
        }
    }

    if (newmouse >= 0) {
        XDevice *dev;

        printf("Trying to make device %d core mouse\n", newmouse);
        dev = XOpenDevice(display, devInfo[newmouse].id);
        printf("Status = %d\n", XChangePointerDevice(display, dev, 0, 1));
        return 0;
    }

    if (newkbd >= 0) {
        XDevice *dev;

        printf("Trying to make device %d core keyboard\n", newkbd);
        dev = XOpenDevice(display, devInfo[newkbd].id);
        printf("Status = %d\n", XChangeKeyboardDevice(display, dev));
        return 0;
    }

    if (device >= 0) {
#define MAX_EVENTS 100
        int cnt = 0;
        XDevice *dev;
        XEventClass event_list[MAX_EVENTS];
        int event_type[MAX_EVENTS];
        const char *names[MAX_EVENTS];
        int total = 0;

#define ADD(type)                                     \
        if (cnt >= MAX_EVENTS) abort();             \
        names[cnt] = #type;                           \
        type(dev, event_type[cnt], event_list[cnt]);  \
        if (event_type[cnt]) ++cnt

        dev = XOpenDevice(display, devInfo[device].id);
        ADD(DeviceKeyPress);
        ADD(DeviceKeyRelease);
        ADD(DeviceButtonPress);
        ADD(DeviceButtonRelease);
        ADD(DeviceMotionNotify);
        ADD(DeviceFocusIn);
        ADD(DeviceFocusOut);
        ADD(ProximityIn);
        ADD(ProximityOut);
        ADD(DeviceStateNotify);
        ADD(DeviceMappingNotify);
        ADD(ChangeDeviceNotify);

        for (i = 0; i < cnt; i++) {
            printf("Waiting for %s events of type %d (%lu) on 0x%08lx\n",
                   names[i],
                   event_type[i], (unsigned long) event_list[i],
                   (long unsigned) DefaultRootWindow(display));
        }
        XSelectExtensionEvent(display, DefaultRootWindow(display),
                              event_list, cnt);

        for (;;) {
            XEvent event;

            XNextEvent(display, &event);
            for (i = 0; i < cnt; i++) {
                XDeviceMotionEvent *e = (XDeviceMotionEvent *) &event;
                XDeviceButtonEvent *b = (XDeviceButtonEvent *) &event;

                if (event.type == event_type[i]) {
                    printf("%s id=%lu (%d @ %d,%d; s=0x%04x, d=%d, t=%lu)"
                           " axes_count=%d first=%d %d %d %d %d %d %d\n",
                           names[i],
                           (long unsigned) e->deviceid,
                           e->type,
                           e->x, e->y,
                           e->device_state,
                           b->button,
                           (long unsigned) b->time,
                           e->axes_count,
                           e->first_axis,
                           e->axis_data[0],
                           e->axis_data[1],
                           e->axis_data[2],
                           e->axis_data[3], e->axis_data[4], e->axis_data[5]);
                }
            }
            ++total;
#if 0
            /* Used to check motion history for
             * extension devices. */
            if (!(total % 10)) {
                XDeviceTimeCoord *tc;
                int n, m, a;
                struct timeval tv;
                unsigned long ms;

                gettimeofday(&tv, NULL);
                ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
                tc = XGetDeviceMotionEvents(display, dev, ms - 1000, ms,
                                            &n, &m, &a);
                printf("Got %d events of mode %s with %d axes\n",
                       n, m == Absolute ? "Absolute" : "Relative", a);
                for (i = 0; i < n && i < 10; i++) {
                    printf("  %d: %lu %d %d\n",
                           i, tc[i].time, tc[i].data[0], tc[i].data[1]);
                }
                XFreeDeviceMotionEvents(tc);
            }
#endif
        }
    }

    XCloseDisplay(display);
    return 0;
}
