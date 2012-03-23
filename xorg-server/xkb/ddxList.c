/************************************************************
Copyright (c) 1995 by Silicon Graphics Computer Systems, Inc.

Permission to use, copy, modify, and distribute this
software and its documentation for any purpose and without
fee is hereby granted, provided that the above copyright
notice appear in all copies and that both that copyright
notice and this permission notice appear in supporting
documentation, and that the name of Silicon Graphics not be 
used in advertising or publicity pertaining to distribution 
of the software without specific prior written permission.
Silicon Graphics makes no representation about the suitability 
of this software for any purpose. It is provided "as is"
without any express or implied warranty.

SILICON GRAPHICS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS 
SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY 
AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL SILICON
GRAPHICS BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL 
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, 
DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE 
OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION  WITH
THE USE OR PERFORMANCE OF THIS SOFTWARE.

********************************************************/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdio.h>
#include <ctype.h>
#include <X11/X.h>
#include <X11/Xos.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>
#include <X11/extensions/XKM.h>
#include "inputstr.h"
#include "scrnintstr.h"
#include "windowstr.h"
#define	XKBSRV_NEED_FILE_FUNCS
#include <xkbsrv.h>
#include <X11/extensions/XI.h>

#ifdef WIN32
/* from ddxLoad.c */
extern const char *Win32TempDir(void);
extern int Win32System(const char *cmdline);

#undef System
#define System Win32System

#define W32_tmparg " '%s'"
#define W32_tmpfile ,tmpname
#define W32_tmplen strlen(tmpname)+3
#else
#define W32_tmparg
#define W32_tmpfile
#define W32_tmplen 0
#endif

/***====================================================================***/

static const char *componentDirs[_XkbListNumComponents] = {
    "keycodes", "types", "compat", "symbols", "geometry"
};

/***====================================================================***/

static Status
_AddListComponent(XkbSrvListInfoPtr list,
                  int what, unsigned flags, char *str, ClientPtr client)
{
    int slen, wlen;
    unsigned char *wire8;
    unsigned short *wire16;
    char *tmp;

    if (list->nTotal >= list->maxRtrn) {
        list->nTotal++;
        return Success;
    }
    tmp = strchr(str, ')');
    if ((tmp == NULL) && ((tmp = strchr(str, '(')) == NULL)) {
        slen = strlen(str);
        while ((slen > 0) && isspace(str[slen - 1])) {
            slen--;
        }
    }
    else {
        slen = (tmp - str + 1);
    }
    wlen = (((slen + 1) / 2) * 2) + 4;  /* four bytes for flags and length, pad to */
    /* 2-byte boundary */
    if ((list->szPool - list->nPool) < wlen) {
        if (wlen > 1024)
            list->szPool += XkbPaddedSize(wlen * 2);
        else
            list->szPool += 1024;
        list->pool = realloc(list->pool, list->szPool * sizeof(char));
        if (!list->pool)
            return BadAlloc;
    }
    wire16 = (unsigned short *) &list->pool[list->nPool];
    wire8 = (unsigned char *) &wire16[2];
    wire16[0] = flags;
    wire16[1] = slen;
    memcpy(wire8, str, slen);
    if (client->swapped) {
        swaps(&wire16[0]);
        swaps(&wire16[1]);
    }
    list->nPool += wlen;
    list->nFound[what]++;
    list->nTotal++;
    return Success;
}

/***====================================================================***/
static Status
XkbDDXListComponent(DeviceIntPtr dev,
                    int what, XkbSrvListInfoPtr list, ClientPtr client)
{
    char *file, *map, *tmp, *buf = NULL;
    FILE *in;
    Status status;
    Bool haveDir;

#ifdef WIN32
    char tmpname[PATH_MAX];
#else
    int rval;
#endif

    if ((list->pattern[what] == NULL) || (list->pattern[what][0] == '\0'))
        return Success;
    file = list->pattern[what];
    map = strrchr(file, '(');
    if (map != NULL) {
        char *tmp;

        map++;
        tmp = strrchr(map, ')');
        if ((tmp == NULL) || (tmp[1] != '\0')) {
            /* illegal pattern.  No error, but no match */
            return Success;
        }
    }

    in = NULL;
    haveDir = TRUE;
#ifdef WIN32
    strcpy(tmpname, Win32TempDir());
    strcat(tmpname, "\\xkb_XXXXXX");
    (void) mktemp(tmpname);
#endif
    if (XkbBaseDirectory != NULL) {
        if ((list->pattern[what][0] == '*') && (list->pattern[what][1] == '\0')) {
            if (asprintf(&buf, "%s/%s.dir", XkbBaseDirectory,
                         componentDirs[what]) == -1)
                buf = NULL;
            else
                in = fopen(buf, "r");
        }
        if (!in) {
            haveDir = FALSE;
            free(buf);
            if (asprintf
                (&buf,
                 "'%s/xkbcomp' '-R%s/%s' -w %ld -l -vlfhpR '%s'" W32_tmparg,
                 XkbBinDirectory, XkbBaseDirectory, componentDirs[what],
                 (long) ((xkbDebugFlags < 2) ? 1 :
                         ((xkbDebugFlags > 10) ? 10 : xkbDebugFlags)),
                 file W32_tmpfile) == -1)
                buf = NULL;
        }
    }
    else {
        if ((list->pattern[what][0] == '*') && (list->pattern[what][1] == '\0')) {
            if (asprintf(&buf, "%s.dir", componentDirs[what]) == -1)
                buf = NULL;
            else
                in = fopen(buf, "r");
        }
        if (!in) {
            haveDir = FALSE;
            free(buf);
            if (asprintf
                (&buf,
                 "xkbcomp -R%s -w %ld -l -vlfhpR '%s'" W32_tmparg,
                 componentDirs[what],
                 (long) ((xkbDebugFlags < 2) ? 1 :
                         ((xkbDebugFlags > 10) ? 10 : xkbDebugFlags)),
                 file W32_tmpfile) == -1)
                buf = NULL;
        }
    }
    status = Success;
    if (!haveDir) {
#ifndef WIN32
        in = Popen(buf, "r");
#else
        if (xkbDebugFlags)
            DebugF("[xkb] xkbList executes: %s\n", buf);
        if (System(buf) < 0)
            ErrorF("[xkb] Could not invoke keymap compiler\n");
        else
            in = fopen(tmpname, "r");
#endif
    }
    if (!in) {
        free(buf);
#ifdef WIN32
        unlink(tmpname);
#endif
        return BadImplementation;
    }
    list->nFound[what] = 0;
    free(buf);
    buf = malloc(PATH_MAX * sizeof(char));
    if (!buf) {
        fclose(in);
#ifdef WIN32
        unlink(tmpname);
#endif
        return BadAlloc;
    }
    while ((status == Success) && ((tmp = fgets(buf, PATH_MAX, in)) != NULL)) {
        unsigned flags;
        register unsigned int i;

        if (*tmp == '#')        /* comment, skip it */
            continue;
        if (!strncmp(tmp, "Warning:", 8) || !strncmp(tmp, "        ", 8))
            /* skip warnings too */
            continue;
        flags = 0;
        /* each line in the listing is supposed to start with two */
        /* groups of eight characters, which specify the general  */
        /* flags and the flags that are specific to the component */
        /* if they're missing, fail with BadImplementation        */
        for (i = 0; (i < 8) && (status == Success); i++) {      /* read the general flags */
            if (isalpha(*tmp))
                flags |= (1L << i);
            else if (*tmp != '-')
                status = BadImplementation;
            tmp++;
        }
        if (status != Success)
            break;
        if (!isspace(*tmp)) {
            status = BadImplementation;
            break;
        }
        else
            tmp++;
        for (i = 0; (i < 8) && (status == Success); i++) {      /* read the component flags */
            if (isalpha(*tmp))
                flags |= (1L << (i + 8));
            else if (*tmp != '-')
                status = BadImplementation;
            tmp++;
        }
        if (status != Success)
            break;
        if (isspace(*tmp)) {
            while (isspace(*tmp)) {
                tmp++;
            }
        }
        else {
            status = BadImplementation;
            break;
        }
        status = _AddListComponent(list, what, flags, tmp, client);
    }
#ifndef WIN32
    if (haveDir)
        fclose(in);
    else if ((rval = Pclose(in)) != 0) {
        if (xkbDebugFlags)
            ErrorF("[xkb] xkbcomp returned exit code %d\n", rval);
    }
#else
    fclose(in);
    unlink(tmpname);
#endif
    free(buf);
    return status;
}

/***====================================================================***/

/* ARGSUSED */
Status
XkbDDXList(DeviceIntPtr dev, XkbSrvListInfoPtr list, ClientPtr client)
{
    Status status;

    status = XkbDDXListComponent(dev, _XkbListKeycodes, list, client);
    if (status == Success)
        status = XkbDDXListComponent(dev, _XkbListTypes, list, client);
    if (status == Success)
        status = XkbDDXListComponent(dev, _XkbListCompat, list, client);
    if (status == Success)
        status = XkbDDXListComponent(dev, _XkbListSymbols, list, client);
    if (status == Success)
        status = XkbDDXListComponent(dev, _XkbListGeometry, list, client);
    return status;
}
