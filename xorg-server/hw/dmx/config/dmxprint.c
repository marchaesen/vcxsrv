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
 * to pretty-print DMX configurations.
 *
 * Because the DMX configuration file parsing should be capable of being
 * used in a stand-alone fashion (i.e., independent from the DMX server
 * source tree), no dependencies on other DMX routines are made. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmxconfig.h"
#include "dmxparse.h"
#include "dmxprint.h"
#include "parser.h"
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

static FILE *str = NULL;
static int indent = 0;
static int pos = 0;

/** Stack of indentation information used for pretty-printing
 * configuration information. */
static struct stack {
    int base;
    int comment;
    int step;
    struct stack *next;
} *stack, initialStack = {
0, 0, 4, NULL};

static void
dmxConfigIndent(void)
{
    int i;

    if (indent < 0)
        indent = 0;
    if (indent > 40)
        indent = 40;
    for (i = 0; i < indent; i++)
        fprintf(str, " ");
}

static void
dmxConfigNewline(void)
{
    if (pos)
        fprintf(str, "\n");
    pos = 0;
}

static void
dmxConfigPushState(int base, int comment, int step)
{
    struct stack *new = dmxConfigAlloc(sizeof(*new));

    new->base = base;
    new->comment = comment;
    new->step = step;
    new->next = stack;
    stack = new;
    indent = base;
    dmxConfigNewline();
}

static void
dmxConfigPushComment(void)
{
    if (stack)
        indent = stack->comment;
}

static void
dmxConfigPushStep(void)
{
    if (stack)
        indent = stack->step;
}

static void
dmxConfigPopState(void)
{
    struct stack *old = stack;

    if (!stack)
        return;
    indent = old->base;
    stack = old->next;
    if (!stack)
        dmxConfigLog("Stack underflow\n");
    dmxConfigFree(old);
    dmxConfigNewline();
}

static void
dmxConfigOutput(int addSpace, int doNewline, const char *comment,
                const char *format, ...)
{
    va_list args;

    if (!pos)
        dmxConfigIndent();
    else if (addSpace)
        fprintf(str, " ");

    if (format) {
        va_start(args, format);
        /* RATS: This hasn't been audited -- it
         * could probably result in a buffer
         * overflow. */
        pos += vfprintf(str, format, args);     /* assumes no newlines! */
        va_end(args);
    }

    if (comment) {
        if (pos)
            fprintf(str, " ");
        pos += fprintf(str, "#%s", comment);
        dmxConfigNewline();
        dmxConfigPushComment();
    }
    else if (doNewline)
        dmxConfigNewline();
}

static void
dmxConfigPrintComment(DMXConfigCommentPtr p)
{
    dmxConfigOutput(1, 1, p->comment, NULL);
}

static void
dmxConfigPrintTokenFlag(DMXConfigTokenPtr p, int flag)
{
    if (!p)
        return;
    switch (p->token) {
    case T_VIRTUAL:
        dmxConfigPushState(0, 4, 4);
        dmxConfigOutput(0, 0, p->comment, "virtual");
        break;
    case T_DISPLAY:
        dmxConfigPushState(4, 12, 16);
        dmxConfigOutput(0, 0, p->comment, "display");
        break;
    case T_WALL:
        dmxConfigPushState(4, 12, 16);
        dmxConfigOutput(0, 0, p->comment, "wall");
        break;
    case T_OPTION:
        dmxConfigPushState(4, 12, 16);
        dmxConfigOutput(0, 0, p->comment, "option");
        break;
    case T_PARAM:
        dmxConfigPushState(4, 8, 12);
        dmxConfigOutput(0, 0, p->comment, "param");
        break;
    case ';':
        dmxConfigOutput(0, 1, p->comment, ";");
        if (flag)
            dmxConfigPopState();
        break;
    case '{':
        dmxConfigOutput(1, 1, p->comment, "{");
        dmxConfigPushStep();
        break;
    case '}':
        if (flag)
            dmxConfigPopState();
        dmxConfigOutput(0, 1, p->comment, "}");
        break;
    case '/':
        dmxConfigOutput(1, 0, NULL, "/");
        break;
    default:
        dmxConfigLog("unknown token %d on line %d\n", p->token, p->line);
    }
}

static void
dmxConfigPrintToken(DMXConfigTokenPtr p)
{
    dmxConfigPrintTokenFlag(p, 1);
}

static void
dmxConfigPrintTokenNopop(DMXConfigTokenPtr p)
{
    dmxConfigPrintTokenFlag(p, 0);
}

static int
dmxConfigPrintQuotedString(const char *s)
{
    const char *pt;

    if (!s || !s[0])
        return 1;               /* Quote empty string */
    for (pt = s; *pt; ++pt)
        if (isspace(*pt))
            return 1;
    return 0;
}

static void
dmxConfigPrintString(DMXConfigStringPtr p, int quote)
{
    DMXConfigStringPtr pt;

    if (!p)
        return;
    for (pt = p; pt; pt = pt->next) {
        if (quote && dmxConfigPrintQuotedString(pt->string)) {
            dmxConfigOutput(1, 0, pt->comment, "\"%s\"",
                            pt->string ? pt->string : "");
        }
        else
            dmxConfigOutput(1, 0, pt->comment, "%s",
                            pt->string ? pt->string : "");
    }
}

static int
dmxConfigPrintPair(DMXConfigPairPtr p, int addSpace)
{
    const char *format = NULL;

    if (!p)
        return 0;
    switch (p->token) {
    case T_ORIGIN:
        format = "@%dx%d";
        break;
    case T_DIMENSION:
        format = "%dx%d";
        break;
    case T_OFFSET:
        format = "%c%d%c%d";
        break;
    }
    if (p->token == T_OFFSET) {
        if (!p->comment && !p->x && !p->y && p->xsign >= 0 && p->ysign >= 0)
            return 0;
        dmxConfigOutput(addSpace, 0, p->comment, format,
                        p->xsign < 0 ? '-' : '+', p->x,
                        p->ysign < 0 ? '-' : '+', p->y);
    }
    else {
        if (!p->comment && !p->x && !p->y)
            return 0;
        dmxConfigOutput(addSpace, 0, p->comment, format, p->x, p->y);
    }
    return 1;
}

static void
dmxConfigPrintDisplay(DMXConfigDisplayPtr p)
{
    DMXConfigToken dummyStart = { T_DISPLAY, 0, NULL };
    DMXConfigToken dummyEnd = { ';', 0, NULL };
    DMXConfigToken dummySep = { '/', 0, NULL };
    DMXConfigString dummyName = { T_STRING, 0, NULL, NULL, NULL };
    DMXConfigPair dummySDim = { T_DIMENSION, 0, NULL, 0, 0, 0, 0 };
    DMXConfigPair dummySOffset = { T_OFFSET, 0, NULL, 0, 0, 0, 0 };
    DMXConfigPair dummyRDim = { T_DIMENSION, 0, NULL, 0, 0, 0, 0 };
    DMXConfigPair dummyROffset = { T_OFFSET, 0, NULL, 0, 0, 0, 0 };
    DMXConfigPair dummyOrigin = { T_ORIGIN, 0, NULL, 0, 0, 0, 0 };
    int output;

    if (p->dname)
        p->dname->string = p->name;
    else
        dummyName.string = p->name;

    if (p->dim && p->dim->scrn && p->dim->scrn->dim) {
        p->dim->scrn->dim->x = p->scrnWidth;
        p->dim->scrn->dim->y = p->scrnHeight;
    }
    else {
        dummySDim.x = p->scrnWidth;
        dummySDim.y = p->scrnHeight;
    }

    if (p->dim && p->dim->scrn && p->dim->scrn->offset) {
        p->dim->scrn->offset->x = p->scrnX;
        p->dim->scrn->offset->y = p->scrnY;
    }
    else {
        dummySOffset.x = p->scrnX;
        dummySOffset.y = p->scrnY;
    }

    if (p->dim && p->dim->root && p->dim->root->dim) {
        p->dim->root->dim->x = p->rootWidth;
        p->dim->root->dim->y = p->rootHeight;
    }
    else {
        dummyRDim.x = p->rootWidth;
        dummyRDim.y = p->rootHeight;
    }

    if (p->dim && p->dim->root && p->dim->root->offset) {
        p->dim->root->offset->x = p->rootX;
        p->dim->root->offset->y = p->rootY;
    }
    else {
        dummyROffset.x = p->rootX;
        dummyROffset.y = p->rootY;
    }

    if (p->origin) {
        p->origin->x = p->rootXOrigin, p->origin->y = p->rootYOrigin;
        p->origin->xsign = p->rootXSign, p->origin->ysign = p->rootYSign;
    }
    else {
        dummyOrigin.x = p->rootXOrigin, dummyOrigin.y = p->rootYOrigin;
        dummyOrigin.xsign = p->rootXSign, dummyOrigin.ysign = p->rootYSign;
    }

    dmxConfigPrintToken(p->start ? p->start : &dummyStart);
    dmxConfigPrintString(p->dname ? p->dname : &dummyName, 1);

    if (p->dim && p->dim->scrn && p->dim->scrn->dim)
        output = dmxConfigPrintPair(p->dim->scrn->dim, 1);
    else
        output = dmxConfigPrintPair(&dummySDim, 1);
    if (p->dim && p->dim->scrn && p->dim->scrn->offset)
        dmxConfigPrintPair(p->dim->scrn->offset, !output);
    else
        dmxConfigPrintPair(&dummySOffset, !output);

    if (p->scrnWidth != p->rootWidth
        || p->scrnHeight != p->rootHeight || p->rootX || p->rootY) {
        dmxConfigPrintToken(&dummySep);
        if (p->dim && p->dim->root && p->dim->root->dim)
            output = dmxConfigPrintPair(p->dim->root->dim, 1);
        else
            output = dmxConfigPrintPair(&dummyRDim, 1);
        if (p->dim && p->dim->root && p->dim->root->offset)
            dmxConfigPrintPair(p->dim->root->offset, !output);
        else
            dmxConfigPrintPair(&dummyROffset, !output);
    }

    dmxConfigPrintPair(p->origin ? p->origin : &dummyOrigin, 1);
    dmxConfigPrintToken(p->end ? p->end : &dummyEnd);
}

static void
dmxConfigPrintWall(DMXConfigWallPtr p)
{
    dmxConfigPrintToken(p->start);
    dmxConfigPrintPair(p->wallDim, 1);
    dmxConfigPrintPair(p->displayDim, 1);
    dmxConfigPrintString(p->nameList, 1);
    dmxConfigPrintToken(p->end);
}

static void
dmxConfigPrintOption(DMXConfigOptionPtr p)
{
    DMXConfigToken dummyStart = { T_OPTION, 0, NULL };
    DMXConfigString dummyOption = { T_STRING, 0, NULL, NULL, NULL };
    DMXConfigToken dummyEnd = { ';', 0, NULL };

    dummyOption.string = p->string;

    dmxConfigPrintToken(p->start ? p->start : &dummyStart);
    dmxConfigPrintString(&dummyOption, 0);
    dmxConfigPrintToken(p->end ? p->end : &dummyEnd);
}

static void
dmxConfigPrintParam(DMXConfigParamPtr p)
{
    if (!p)
        return;
    if (p->start) {
        if (p->open && p->close) {
            dmxConfigPrintToken(p->start);
            dmxConfigPrintToken(p->open);
            dmxConfigPrintParam(p->next);
            dmxConfigPrintToken(p->close);
        }
        else if (p->end && p->param) {
            dmxConfigPrintToken(p->start);
            dmxConfigPrintString(p->param, 1);
            dmxConfigPrintToken(p->end);
        }
        else
            dmxConfigLog("dmxConfigPrintParam: cannot handle format (a)\n");
    }
    else if (p->end && p->param) {
        dmxConfigPrintString(p->param, 1);
        dmxConfigPrintTokenNopop(p->end);
        dmxConfigPrintParam(p->next);
    }
    else
        dmxConfigLog("dmxConfigPrintParam: cannot handle format (b)\n");
}

static void
dmxConfigPrintSub(DMXConfigSubPtr p)
{
    DMXConfigSubPtr pt;

    if (!p)
        return;
    for (pt = p; pt; pt = pt->next) {
        switch (pt->type) {
        case dmxConfigComment:
            dmxConfigPrintComment(pt->comment);
            break;
        case dmxConfigDisplay:
            dmxConfigPrintDisplay(pt->display);
            break;
        case dmxConfigWall:
            dmxConfigPrintWall(pt->wall);
            break;
        case dmxConfigOption:
            dmxConfigPrintOption(pt->option);
            break;
        case dmxConfigParam:
            dmxConfigPrintParam(pt->param);
            break;
        default:
            dmxConfigLog("dmxConfigPrintSub:"
                         " cannot handle type %d in subentry\n", pt->type);
        }
    }
}

static void
dmxConfigPrintVirtual(DMXConfigVirtualPtr p)
{
    DMXConfigToken dummyStart = { T_VIRTUAL, 0, NULL };
    DMXConfigToken dummyOpen = { '{', 0, NULL };
    DMXConfigToken dummyClose = { '}', 0, NULL };
    DMXConfigString dummyName = { T_STRING, 0, NULL, NULL, NULL };
    DMXConfigPair dummyDim = { T_DIMENSION, 0, NULL, 0, 0 };

    if (p->vname)
        p->vname->string = p->name;
    else
        dummyName.string = p->name;

    if (p->dim)
        p->dim->x = p->width, p->dim->y = p->height;
    else
        dummyDim.x = p->width, dummyDim.y = p->height;

    dmxConfigPrintToken(p->start ? p->start : &dummyStart);
    dmxConfigPrintString(p->vname ? p->vname : &dummyName, 1);
    dmxConfigPrintPair(p->dim ? p->dim : &dummyDim, 1);
    dmxConfigPrintToken(p->open ? p->open : &dummyOpen);
    dmxConfigPrintSub(p->subentry);
    dmxConfigPrintToken(p->close ? p->close : &dummyClose);
}

/** The configuration information in \a entry will be pretty-printed to
 * the \a stream.  If \a stream is NULL, then stdout will be used. */
void
dmxConfigPrint(FILE * stream, DMXConfigEntryPtr entry)
{
    DMXConfigEntryPtr pt;

    if (!stream)
        str = stdout;
    else
        str = stream;

    stack = &initialStack;

    for (pt = entry; pt; pt = pt->next) {
        switch (pt->type) {
        case dmxConfigComment:
            dmxConfigPrintComment(pt->comment);
            break;
        case dmxConfigVirtual:
            dmxConfigPrintVirtual(pt->virtual);
            break;
        default:
            dmxConfigLog("dmxConfigPrint: cannot handle type %d in entry\n",
                         pt->type);
        }
    }
    if (pos)
        dmxConfigNewline();
}

/** The configuration information in \a p will be pretty-printed to the
 * \a stream.  If \a stream is NULL, then stdout will be used. */
void
dmxConfigVirtualPrint(FILE * stream, DMXConfigVirtualPtr p)
{
    if (!stream)
        str = stdout;
    else
        str = stream;

    stack = &initialStack;

    dmxConfigPrintVirtual(p);
    if (pos)
        dmxConfigNewline();
}
