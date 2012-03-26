/*
 * Copyright 2003 Red Hat Inc., Durham, North Carolina.
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
#include <X11/Xlib.h>
#include <X11/extensions/XRes.h>

int
main(int argc, char **argv)
{
    Display *display = NULL;
    int major_version, minor_version;
    int event, error;
    int count;
    int i;
    XResClient *clients;

    if (argc == 2) {
        if (!(display = XOpenDisplay(argv[1]))) {
            printf("Cannot open display %s\n", argv[1]);
            return -1;
        }
    }
    else {
        printf("Usage: %s display\n", argv[0]);
        return -1;
    }

    if (!display && !(display = XOpenDisplay(NULL))) {
        printf("Cannot open default display\n");
        return -1;
    }

    if (!XResQueryExtension(display, &event, &error)) {
        printf("X-Resource extension not present\n");
        return -1;
    }
    printf("X-Resource extension present: event=%d error=%d\n", event, error);

    if (!XResQueryVersion(display, &major_version, &minor_version)) {
        printf("XResQueryVersion call failed\n");
        return -1;
    }
    printf("X-Resource extension version: %d.%d\n",
           major_version, minor_version);

    XResQueryClients(display, &count, &clients);

    printf("%d clients:\n", count);
    for (i = 0; i < count; i++) {
        int c, j;
        XResType *types;

        XResQueryClientResources(display, clients[i].resource_base, &c, &types);
        printf(" %3d: base = 0x%lx, mask = 0x%lx, %d resource types:\n",
               i, (long unsigned) clients[i].resource_base,
               (long unsigned) clients[i].resource_mask, c);
        for (j = 0; j < c; j++) {
            char *name = XGetAtomName(display, types[j].resource_type);

            printf("      %2d: %s %d\n", j, name, types[j].count);
            XFree(name);
        }
        XFree(types);
    }

    XFree(clients);
    XCloseDisplay(display);
    return 0;
}
