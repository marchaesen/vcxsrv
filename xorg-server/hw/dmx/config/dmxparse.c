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

/** \file
 *
 * This file provides support routines and helper functions to be used
 * by the DMX configuration file parser.
 *
 * Because the DMX configuration file parsing should be capable of being
 * used in a stand-alone fashion (i.e., independent from the DMX server
 * source tree), no dependencies on other DMX routines are made. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "dmxparse.h"

/** A general error logging routine that does not depend on the dmxLog
 * functions. */
void
dmxConfigLog(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vprintf(format, args);      /* RATS: All calls to dmxConfigLog from
                                 * dmxparse.c and dmxprint.c use a
                                 * trusted format. */
    va_end(args);
}

void *
dmxConfigAlloc(unsigned long bytes)
{
    void *area = calloc(1, bytes);

    if (!area) {
        dmxConfigLog("dmxConfigAlloc: out of memory\n");
        return NULL;
    }
    return area;
}

void *
dmxConfigRealloc(void *orig, unsigned long orig_bytes, unsigned long bytes)
{
    unsigned char *area = realloc(orig, bytes);

    if (!area) {
        dmxConfigLog("dmxConfigRealloc: out of memory\n");
        return NULL;
    }
    memset(area + orig_bytes, 0, bytes - orig_bytes);
    return area;
}

const char *
dmxConfigCopyString(const char *string, int length)
{
    char *copy;

    if (!length)
        length = strlen(string);
    copy = dmxConfigAlloc(length + 1);
    if (length)
        strncpy(copy, string, length);
    copy[length] = '\0';
    return copy;
}

void
dmxConfigFree(void *area)
{
    free(area);
}

DMXConfigTokenPtr
dmxConfigCreateToken(int token, int line, const char *comment)
{
    DMXConfigTokenPtr pToken = dmxConfigAlloc(sizeof(*pToken));

    pToken->token = token;
    pToken->line = line;
    pToken->comment = comment;
    return pToken;
}

void
dmxConfigFreeToken(DMXConfigTokenPtr p)
{
    if (!p)
        return;
    dmxConfigFree((void *) p->comment);
    dmxConfigFree(p);
}

DMXConfigStringPtr
dmxConfigCreateString(int token, int line,
                      const char *comment, const char *string)
{
    DMXConfigStringPtr pString = dmxConfigAlloc(sizeof(*pString));

    pString->token = token;
    pString->line = line;
    pString->comment = comment;
    pString->string = string;
    return pString;
}

void
dmxConfigFreeString(DMXConfigStringPtr p)
{
    DMXConfigStringPtr next;

    if (!p)
        return;
    do {
        next = p->next;
        dmxConfigFree((void *) p->comment);
        dmxConfigFree((void *) p->string);
        dmxConfigFree(p);
    } while ((p = next));
}

DMXConfigNumberPtr
dmxConfigCreateNumber(int token, int line, const char *comment, int number)
{
    DMXConfigNumberPtr pNumber = dmxConfigAlloc(sizeof(*pNumber));

    pNumber->token = token;
    pNumber->line = line;
    pNumber->comment = comment;
    pNumber->number = number;
    return pNumber;
}

void
dmxConfigFreeNumber(DMXConfigNumberPtr p)
{
    if (!p)
        return;
    dmxConfigFree((void *) p->comment);
    dmxConfigFree(p);
}

DMXConfigPairPtr
dmxConfigCreatePair(int token, int line,
                    const char *comment, int x, int y, int xsign, int ysign)
{
    DMXConfigPairPtr pPair = dmxConfigAlloc(sizeof(*pPair));

    pPair->token = token;
    pPair->line = line;
    pPair->comment = comment;
    pPair->x = x;
    pPair->y = y;
    pPair->xsign = (xsign < 0) ? -1 : 1;
    pPair->ysign = (ysign < 0) ? -1 : 1;
    return pPair;
}

void
dmxConfigFreePair(DMXConfigPairPtr p)
{
    if (!p)
        return;
    dmxConfigFree((void *) p->comment);
    dmxConfigFree(p);
}

DMXConfigCommentPtr
dmxConfigCreateComment(int token, int line, const char *comment)
{
    DMXConfigCommentPtr pComment = dmxConfigAlloc(sizeof(*pComment));

    pComment->token = token;
    pComment->line = line;
    pComment->comment = comment;
    return pComment;
}

void
dmxConfigFreeComment(DMXConfigCommentPtr p)
{
    if (!p)
        return;
    dmxConfigFree((void *) p->comment);
    dmxConfigFree(p);
}

DMXConfigPartDimPtr
dmxConfigCreatePartDim(DMXConfigPairPtr pDim, DMXConfigPairPtr pOffset)
{
    DMXConfigPartDimPtr pPart = dmxConfigAlloc(sizeof(*pPart));

    pPart->dim = pDim;
    pPart->offset = pOffset;
    return pPart;
}

void
dmxConfigFreePartDim(DMXConfigPartDimPtr p)
{
    if (!p)
        return;
    dmxConfigFreePair(p->dim);
    dmxConfigFreePair(p->offset);
    dmxConfigFree(p);
}

DMXConfigFullDimPtr
dmxConfigCreateFullDim(DMXConfigPartDimPtr pScrn, DMXConfigPartDimPtr pRoot)
{
    DMXConfigFullDimPtr pFull = dmxConfigAlloc(sizeof(*pFull));

    pFull->scrn = pScrn;
    pFull->root = pRoot;
    return pFull;
}

void
dmxConfigFreeFullDim(DMXConfigFullDimPtr p)
{
    if (!p)
        return;
    dmxConfigFreePartDim(p->scrn);
    dmxConfigFreePartDim(p->root);
    dmxConfigFree(p);
}

DMXConfigDisplayPtr
dmxConfigCreateDisplay(DMXConfigTokenPtr pStart,
                       DMXConfigStringPtr pName,
                       DMXConfigFullDimPtr pDim,
                       DMXConfigPairPtr pOrigin, DMXConfigTokenPtr pEnd)
{
    DMXConfigDisplayPtr pDisplay = dmxConfigAlloc(sizeof(*pDisplay));

    pDisplay->start = pStart;
    pDisplay->dname = pName;
    pDisplay->dim = pDim;
    pDisplay->origin = pOrigin;
    pDisplay->end = pEnd;

    pDisplay->name = pName ? pName->string : NULL;
    pDisplay->rootXOrigin = pOrigin ? pOrigin->x : 0;
    pDisplay->rootYOrigin = pOrigin ? pOrigin->y : 0;

    if (pDim && pDim->scrn && pDim->scrn->dim) {
        pDisplay->scrnWidth = pDim->scrn->dim->x;
        pDisplay->scrnHeight = pDim->scrn->dim->y;
    }
    if (pDim && pDim->scrn && pDim->scrn->offset) {
        pDisplay->scrnX = pDim->scrn->offset->x;
        pDisplay->scrnY = pDim->scrn->offset->y;
        pDisplay->scrnXSign = pDim->scrn->offset->xsign;
        pDisplay->scrnYSign = pDim->scrn->offset->ysign;
    }

    if (pDim && pDim->root) {
        if (pDim->root->dim) {
            pDisplay->rootWidth = pDim->root->dim->x;
            pDisplay->rootHeight = pDim->root->dim->y;
        }
        if (pDim->root->offset) {
            pDisplay->rootX = pDim->root->offset->x;
            pDisplay->rootY = pDim->root->offset->y;
            pDisplay->rootXSign = pDim->root->offset->xsign;
            pDisplay->rootYSign = pDim->root->offset->ysign;
        }
    }
    else {                      /* If no root specification, copy width
                                 * and height from scrn -- leave offset
                                 * as zero, since it is relative to
                                 * scrn. */
        pDisplay->rootWidth = pDisplay->scrnWidth;
        pDisplay->rootHeight = pDisplay->scrnHeight;
    }

    return pDisplay;
}

void
dmxConfigFreeDisplay(DMXConfigDisplayPtr p)
{
    if (!p)
        return;
    dmxConfigFreeToken(p->start);
    dmxConfigFreeString(p->dname);
    dmxConfigFreeFullDim(p->dim);
    dmxConfigFreeToken(p->end);
    dmxConfigFree(p);
}

DMXConfigWallPtr
dmxConfigCreateWall(DMXConfigTokenPtr pStart,
                    DMXConfigPairPtr pWallDim,
                    DMXConfigPairPtr pDisplayDim,
                    DMXConfigStringPtr pNameList, DMXConfigTokenPtr pEnd)
{
    DMXConfigWallPtr pWall = dmxConfigAlloc(sizeof(*pWall));

    pWall->start = pStart;
    pWall->wallDim = pWallDim;
    pWall->displayDim = pDisplayDim;
    pWall->nameList = pNameList;
    pWall->end = pEnd;

    pWall->width = pDisplayDim ? pDisplayDim->x : 0;
    pWall->height = pDisplayDim ? pDisplayDim->y : 0;
    pWall->xwall = pWallDim ? pWallDim->x : 0;
    pWall->ywall = pWallDim ? pWallDim->y : 0;

    return pWall;
}

void
dmxConfigFreeWall(DMXConfigWallPtr p)
{
    if (!p)
        return;
    dmxConfigFreeToken(p->start);
    dmxConfigFreePair(p->wallDim);
    dmxConfigFreePair(p->displayDim);
    dmxConfigFreeString(p->nameList);
    dmxConfigFreeToken(p->end);
    dmxConfigFree(p);
}

DMXConfigOptionPtr
dmxConfigCreateOption(DMXConfigTokenPtr pStart,
                      DMXConfigStringPtr pOption, DMXConfigTokenPtr pEnd)
{
    int length = 0;
    int offset = 0;
    DMXConfigStringPtr p;
    DMXConfigOptionPtr option = dmxConfigAlloc(sizeof(*option));

    for (p = pOption; p; p = p->next) {
        if (p->string)
            length += strlen(p->string) + 1;
    }

    option->string = dmxConfigAlloc(length + 1);

    for (p = pOption; p; p = p->next) {
        if (p->string) {
            int len = strlen(p->string);

            strncpy(option->string + offset, p->string, len);
            offset += len;
            if (p->next)
                option->string[offset++] = ' ';
        }
    }
    option->string[offset] = '\0';

    option->start = pStart;
    option->option = pOption;
    option->end = pEnd;

    return option;
}

void
dmxConfigFreeOption(DMXConfigOptionPtr p)
{
    if (!p)
        return;
    free(p->string);
    dmxConfigFreeToken(p->start);
    dmxConfigFreeString(p->option);
    dmxConfigFreeToken(p->end);
    dmxConfigFree(p);
}

const char **
dmxConfigLookupParam(DMXConfigParamPtr p, const char *key, int *argc)
{
    DMXConfigParamPtr pt;

    for (pt = p; pt; pt = pt->next) {
        if (pt->argv && !strcasecmp(pt->argv[0], key)) {
            *argc = pt->argc;
            return pt->argv;
        }
    }
    *argc = 0;
    return NULL;
}

DMXConfigParamPtr
dmxConfigCreateParam(DMXConfigTokenPtr pStart,
                     DMXConfigTokenPtr pOpen,
                     DMXConfigStringPtr pParam,
                     DMXConfigTokenPtr pClose, DMXConfigTokenPtr pEnd)
{
    DMXConfigParamPtr param = dmxConfigAlloc(sizeof(*param));
    DMXConfigStringPtr pt;

    param->argc = 0;
    param->argv = NULL;
    for (pt = pParam; pt; pt = pt->next) {
        if (pt->string) {
            param->argv = realloc(param->argv,
                                  (param->argc + 2) * sizeof(*param->argv));
            param->argv[param->argc] = pt->string;
            ++param->argc;
        }
    }
    if (param->argv)
        param->argv[param->argc] = NULL;

    param->start = pStart;
    param->open = pOpen;
    param->param = pParam;
    param->close = pClose;
    param->end = pEnd;

    return param;
}

void
dmxConfigFreeParam(DMXConfigParamPtr p)
{
    DMXConfigParamPtr next;

    if (!p)
        return;
    do {
        next = p->next;
        dmxConfigFreeToken(p->start);
        dmxConfigFreeToken(p->open);
        dmxConfigFreeString(p->param);
        dmxConfigFreeToken(p->close);
        dmxConfigFreeToken(p->end);
        dmxConfigFree(p->argv);
        dmxConfigFree(p);
    } while ((p = next));
}

DMXConfigSubPtr
dmxConfigCreateSub(DMXConfigType type,
                   DMXConfigCommentPtr comment,
                   DMXConfigDisplayPtr display,
                   DMXConfigWallPtr wall,
                   DMXConfigOptionPtr option, DMXConfigParamPtr param)
{
    DMXConfigSubPtr pSub = dmxConfigAlloc(sizeof(*pSub));

    pSub->type = type;
    switch (type) {
    case dmxConfigComment:
        pSub->comment = comment;
        break;
    case dmxConfigDisplay:
        pSub->display = display;
        break;
    case dmxConfigWall:
        pSub->wall = wall;
        break;
    case dmxConfigOption:
        pSub->option = option;
        break;
    case dmxConfigParam:
        pSub->param = param;
        break;
    default:
        dmxConfigLog("Type %d not supported in subentry\n", type);
        break;
    }
    return pSub;
}

void
dmxConfigFreeSub(DMXConfigSubPtr sub)
{
    DMXConfigSubPtr pt;

    for (pt = sub; pt; pt = pt->next) {
        switch (pt->type) {
        case dmxConfigComment:
            dmxConfigFreeComment(pt->comment);
            break;
        case dmxConfigDisplay:
            dmxConfigFreeDisplay(pt->display);
            break;
        case dmxConfigWall:
            dmxConfigFreeWall(pt->wall);
            break;
        case dmxConfigOption:
            dmxConfigFreeOption(pt->option);
            break;
        case dmxConfigParam:
            dmxConfigFreeParam(pt->param);
            break;
        default:
            dmxConfigLog("Type %d not supported in subentry\n", pt->type);
            break;
        }
    }
    dmxConfigFree(sub);
}

DMXConfigSubPtr
dmxConfigSubComment(DMXConfigCommentPtr comment)
{
    return dmxConfigCreateSub(dmxConfigComment, comment, NULL, NULL, NULL,
                              NULL);
}

DMXConfigSubPtr
dmxConfigSubDisplay(DMXConfigDisplayPtr display)
{
    return dmxConfigCreateSub(dmxConfigDisplay, NULL, display, NULL, NULL,
                              NULL);
}

DMXConfigSubPtr
dmxConfigSubWall(DMXConfigWallPtr wall)
{
    return dmxConfigCreateSub(dmxConfigWall, NULL, NULL, wall, NULL, NULL);
}

DMXConfigSubPtr
dmxConfigSubOption(DMXConfigOptionPtr option)
{
    return dmxConfigCreateSub(dmxConfigOption, NULL, NULL, NULL, option, NULL);
}

DMXConfigSubPtr
dmxConfigSubParam(DMXConfigParamPtr param)
{
    return dmxConfigCreateSub(dmxConfigParam, NULL, NULL, NULL, NULL, param);
}

extern DMXConfigSubPtr
dmxConfigAddSub(DMXConfigSubPtr head, DMXConfigSubPtr sub)
{
    DMXConfigSubPtr pt;

    if (!head)
        return sub;
    for (pt = head; pt->next; pt = pt->next);
    pt->next = sub;
    return head;
}

DMXConfigVirtualPtr
dmxConfigCreateVirtual(DMXConfigTokenPtr pStart,
                       DMXConfigStringPtr pName,
                       DMXConfigPairPtr pDim,
                       DMXConfigTokenPtr pOpen,
                       DMXConfigSubPtr pSubentry, DMXConfigTokenPtr pClose)
{
    DMXConfigVirtualPtr pVirtual = dmxConfigAlloc(sizeof(*pVirtual));

    pVirtual->start = pStart;
    pVirtual->vname = pName;
    pVirtual->dim = pDim;
    pVirtual->open = pOpen;
    pVirtual->subentry = pSubentry;
    pVirtual->close = pClose;

    pVirtual->name = pName ? pName->string : NULL;
    pVirtual->width = pDim ? pDim->x : 0;
    pVirtual->height = pDim ? pDim->y : 0;

    return pVirtual;
}

void
dmxConfigFreeVirtual(DMXConfigVirtualPtr virtual)
{
    dmxConfigFreeToken(virtual->start);
    dmxConfigFreeString(virtual->vname);
    dmxConfigFreePair(virtual->dim);
    dmxConfigFreeToken(virtual->open);
    dmxConfigFreeSub(virtual->subentry);
    dmxConfigFreeToken(virtual->close);
    dmxConfigFree(virtual);
}

DMXConfigEntryPtr
dmxConfigCreateEntry(DMXConfigType type,
                     DMXConfigCommentPtr comment, DMXConfigVirtualPtr virtual)
{
    DMXConfigEntryPtr pEntry = dmxConfigAlloc(sizeof(*pEntry));

    pEntry->type = type;
    switch (type) {
    case dmxConfigComment:
        pEntry->comment = comment;
        break;
    case dmxConfigVirtual:
        pEntry->virtual = virtual;
        break;
    default:
        dmxConfigLog("Type %d not supported in entry\n", type);
        break;
    }
    return pEntry;
}

void
dmxConfigFreeEntry(DMXConfigEntryPtr entry)
{
    DMXConfigEntryPtr pt;

    for (pt = entry; pt; pt = pt->next) {
        switch (pt->type) {
        case dmxConfigComment:
            dmxConfigFreeComment(pt->comment);
            break;
        case dmxConfigVirtual:
            dmxConfigFreeVirtual(pt->virtual);
            break;
        default:
            dmxConfigLog("Type %d not supported in entry\n", pt->type);
            break;
        }
    }
    dmxConfigFree(entry);
}

DMXConfigEntryPtr
dmxConfigAddEntry(DMXConfigEntryPtr head,
                  DMXConfigType type,
                  DMXConfigCommentPtr comment, DMXConfigVirtualPtr virtual)
{
    DMXConfigEntryPtr child = dmxConfigCreateEntry(type, comment, virtual);
    DMXConfigEntryPtr pt;

    if (!head)
        return child;

    for (pt = head; pt->next; pt = pt->next);
    pt->next = child;

    return head;
}

DMXConfigEntryPtr
dmxConfigEntryComment(DMXConfigCommentPtr comment)
{
    return dmxConfigCreateEntry(dmxConfigComment, comment, NULL);
}

DMXConfigEntryPtr
dmxConfigEntryVirtual(DMXConfigVirtualPtr virtual)
{
    return dmxConfigCreateEntry(dmxConfigVirtual, NULL, virtual);
}
