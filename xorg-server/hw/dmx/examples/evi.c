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
#include <X11/extensions/XEVI.h>

int
main(int argc, char **argv)
{
    Display *display = NULL;
    int major_version, minor_version;
    ExtendedVisualInfo *evi;
    int count;
    int i;

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

    if (!XeviQueryVersion(display, &major_version, &minor_version)) {
        printf("EVI extension not present\n");
        return -1;
    }
    printf("EVI Extension version: %d.%d\n", major_version, minor_version);

    XeviGetVisualInfo(display, NULL, 0, &evi, &count);

    for (i = 0; i < count; i++) {
        printf("%02d vid=0x%02lx screen=%d level=%d type=%u value=%u"
               " min=%u max=%u conflicts=%u\n",
               i,
               (long unsigned) evi[i].core_visual_id,
               evi[i].screen,
               evi[i].level,
               evi[i].transparency_type,
               evi[i].transparency_value,
               evi[i].min_hw_colormaps,
               evi[i].max_hw_colormaps, evi[i].num_colormap_conflicts);
    }

    XCloseDisplay(display);
    return 0;
}
