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
 */

/** \file
 * This file provides some compatibility support for reading VDL files
 * that are used by xmovie
 * (http://www.llnl.gov/icc/sdd/img/xmovie/xmovie.shtml).
 *
 * This file is not used by the DMX server.
 */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "os.h"
#include "dmxconfig.h"
#include "dmxparse.h"
#include "dmxcompat.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int
dmxVDLReadLine(FILE * str, char *buf, int len)
{
    if (fgets(buf, len, str))
        return strlen(buf);
    return 0;
}

static int
dmxVDLCount(const char *buf)
{
    return strtol(buf, NULL, 10);
}

static void
dmxVDLVirtualEntry(const char *buf, char *name, int *len, int *x, int *y)
{
    char *end;
    const char *s;
    char *d;
    int start;

    *x = strtol(buf, &end, 10);
    *y = strtol(end, &end, 10);

    for (s = end, d = name, start = 1; *s && *s != '['; ++s) {
        if (start && isspace(*s))
            continue;
        *d++ = *s;
        start = 0;
    }
    *d = '\0';
    while (d > name && isspace(d[-1]))
        *--d = '\0';            /* remove trailing space */
    *len = strlen(name);
}

static void
dmxVDLDisplayEntry(const char *buf,
                   char *name, int *len,
                   int *x, int *y, int *xoff, int *yoff, int *xorig, int *yorig)
{
    const char *pt;
    char *end;

    pt = strchr(buf, ' ');
    strlcpy(name, buf, 1 + pt - buf);
    *len = strlen(name);

    *x = strtol(pt, &end, 10);
    *y = strtol(end, &end, 10);
    *xorig = strtol(end, &end, 10);
    *yorig = strtol(end, &end, 10);
    *xoff = strtol(end, &end, 10);
    *yoff = strtol(end, NULL, 10);
}

/** Read from the VDL format \a filename and return a newly allocated \a
 * DMXConfigEntryPtr */
DMXConfigEntryPtr
dmxVDLRead(const char *filename)
{
    FILE *str;
    char buf[2048];             /* RATS: Use ok */
    char *pt;
    int lineno = 0;
    DMXConfigEntryPtr entry = NULL;
    DMXConfigVirtualPtr virtual = NULL;
    DMXConfigSubPtr sub = NULL;
    DMXConfigDisplayPtr display = NULL;
    DMXConfigFullDimPtr fdim = NULL;
    int dcount = 0;
    int icount = 0;
    int x, y, xoff, yoff, xorig, yorig;
    char name[2048];            /* RATS: Use ok */
    const char *tmp;
    int len;
    enum {
        simulateFlag,
        virtualCount,
        virtualEntry,
        displayCount,
        displayEntry,
        ignoreCount,
        ignoreEntry
    } state = simulateFlag;

    if (!filename)
        str = stdin;
    else
        str = fopen(filename, "r");
    if (!str)
        return NULL;

    while (dmxVDLReadLine(str, buf, sizeof(buf))) {
        DMXConfigCommentPtr comment = NULL;

        ++lineno;
        for (pt = buf; *pt; pt++)
            if (*pt == '\r' || *pt == '\n') {
                *pt = '\0';
                break;
            }
        if (buf[0] == '#') {
            tmp = dmxConfigCopyString(buf + 1, strlen(buf + 1));
            comment = dmxConfigCreateComment(T_COMMENT, lineno, tmp);
            entry = dmxConfigAddEntry(entry, dmxConfigComment, comment, NULL);
            continue;
        }
        switch (state) {
        case simulateFlag:
            state = virtualCount;
            break;
        case virtualCount:
            state = virtualEntry;
            break;
        case virtualEntry:
            len = sizeof(name);
            dmxVDLVirtualEntry(buf, name, &len, &x, &y);
            tmp = dmxConfigCopyString(name, len);
            virtual = dmxConfigCreateVirtual(NULL,
                                             dmxConfigCreateString(T_STRING,
                                                                   lineno,
                                                                   NULL,
                                                                   tmp),
                                             dmxConfigCreatePair(T_DIMENSION,
                                                                 lineno,
                                                                 NULL,
                                                                 x, y, 0, 0),
                                             NULL, NULL, NULL);
            state = displayCount;
            break;
        case displayCount:
            dcount = dmxVDLCount(buf);
            state = displayEntry;
            break;
        case displayEntry:
            dmxVDLDisplayEntry(buf, name, &len, &x, &y, &xoff, &yoff,
                               &xorig, &yorig);
            tmp = dmxConfigCopyString(name, len);
            fdim =
                dmxConfigCreateFullDim(dmxConfigCreatePartDim
                                       (dmxConfigCreatePair
                                        (T_DIMENSION, lineno, NULL, x, y, 0, 0),
                                        dmxConfigCreatePair(T_OFFSET, lineno,
                                                            NULL, xoff, yoff,
                                                            xoff, yoff)), NULL);
            display =
                dmxConfigCreateDisplay(NULL,
                                       dmxConfigCreateString(T_STRING, lineno,
                                                             NULL, tmp), fdim,
                                       dmxConfigCreatePair(T_ORIGIN, lineno,
                                                           NULL, xorig, yorig,
                                                           0, 0), NULL);
            sub = dmxConfigAddSub(sub, dmxConfigSubDisplay(display));
            if (!--dcount) {
                state = ignoreCount;
                virtual->subentry = sub;
                entry = dmxConfigAddEntry(entry,
                                          dmxConfigVirtual, NULL, virtual);
                virtual = NULL;
                sub = NULL;
            }
            break;
        case ignoreCount:
            icount = dmxVDLCount(buf);
            state = ignoreEntry;
            break;
        case ignoreEntry:
            if (!--icount)
                state = virtualEntry;
            break;
        }
    }

    if (str != stdin)
        fclose(str);

    return entry;
}
