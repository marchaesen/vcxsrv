/***********************************************************
Copyright (c) 1993, Oracle and/or its affiliates.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

Copyright 1987, 1988 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

/*

Copyright 1987, 1988, 1994, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

*/

/* Conversion.c - implementations of resource type conversion procs */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include        "IntrinsicI.h"
#include        "StringDefs.h"
#include        "Shell.h"
#include        <stdio.h>
#include        <X11/cursorfont.h>
#include        <X11/keysym.h>
#include        <X11/Xlocale.h>
#include        <errno.h>       /* for StringToDirectoryString */

#define IsNewline(str) ((str) == '\n')
#define IsWhitespace(str) ((str)== ' ' || (str) == '\t')

static _Xconst _XtString XtNwrongParameters = "wrongParameters";
static _Xconst _XtString XtNconversionError = "conversionError";
static _Xconst _XtString XtNmissingCharsetList = "missingCharsetList";

/* Representation types */

#define XtQAtom                 XrmPermStringToQuark(XtRAtom)
#define XtQCommandArgArray      XrmPermStringToQuark(XtRCommandArgArray)
#define XtQCursor               XrmPermStringToQuark(XtRCursor)
#define XtQDirectoryString      XrmPermStringToQuark(XtRDirectoryString)
#define XtQDisplay              XrmPermStringToQuark(XtRDisplay)
#define XtQFile                 XrmPermStringToQuark(XtRFile)
#define XtQFloat                XrmPermStringToQuark(XtRFloat)
#define XtQInitialState         XrmPermStringToQuark(XtRInitialState)
#define XtQPixmap               XrmPermStringToQuark(XtRPixmap)
#define XtQRestartStyle         XrmPermStringToQuark(XtRRestartStyle)
#define XtQShort                XrmPermStringToQuark(XtRShort)
#define XtQUnsignedChar         XrmPermStringToQuark(XtRUnsignedChar)
#define XtQVisual               XrmPermStringToQuark(XtRVisual)

static XrmQuark XtQBool;
static XrmQuark XtQBoolean;
static XrmQuark XtQColor;
static XrmQuark XtQDimension;
static XrmQuark XtQFont;
static XrmQuark XtQFontSet;
static XrmQuark XtQFontStruct;
static XrmQuark XtQGravity;
static XrmQuark XtQInt;
static XrmQuark XtQPixel;
static XrmQuark XtQPosition;
XrmQuark _XtQString;

void
_XtConvertInitialize(void)
{
    XtQBool = XrmPermStringToQuark(XtRBool);
    XtQBoolean = XrmPermStringToQuark(XtRBoolean);
    XtQColor = XrmPermStringToQuark(XtRColor);
    XtQDimension = XrmPermStringToQuark(XtRDimension);
    XtQFont = XrmPermStringToQuark(XtRFont);
    XtQFontSet = XrmPermStringToQuark(XtRFontSet);
    XtQFontStruct = XrmPermStringToQuark(XtRFontStruct);
    XtQGravity = XrmPermStringToQuark(XtRGravity);
    XtQInt = XrmPermStringToQuark(XtRInt);
    XtQPixel = XrmPermStringToQuark(XtRPixel);
    XtQPosition = XrmPermStringToQuark(XtRPosition);
    _XtQString = XrmPermStringToQuark(XtRString);
}

#define done_typed_string(type, typed_value, tstr) \
        {                                                       \
            if (toVal->addr != NULL) {                          \
                if (toVal->size < sizeof(type)) {               \
                    toVal->size = sizeof(type);                 \
                    XtDisplayStringConversionWarning(dpy,       \
                        (char*) fromVal->addr, tstr);           \
                    return False;                               \
                }                                               \
                *(type*)(toVal->addr) = typed_value;            \
            }                                                   \
            else {                                              \
                static type static_val;                         \
                static_val = typed_value;                       \
                toVal->addr = (XPointer)&static_val;            \
            }                                                   \
            toVal->size = sizeof(type);                         \
            return True;                                        \
        }

#define done_string(type, value, tstr) \
        done_typed_string(type, (type) (value), tstr)

#define done_typed(type, typed_value) \
        {                                                       \
            if (toVal->addr != NULL) {                          \
                if (toVal->size < sizeof(type)) {               \
                    toVal->size = sizeof(type);                 \
                    return False;                               \
                }                                               \
                *(type*)(toVal->addr) = typed_value;            \
            }                                                   \
            else {                                              \
                static type static_val;                         \
                static_val = typed_value;                       \
                toVal->addr = (XPointer)&static_val;            \
            }                                                   \
            toVal->size = sizeof(type);                         \
            return True;                                        \
        }

#define done(type, value) \
        done_typed(type, (type) (value))

void
XtDisplayStringConversionWarning(Display *dpy,
                                 _Xconst char *from,
                                 _Xconst char *toType)
{
#ifndef NO_MIT_HACKS
    /* Allow suppression of conversion warnings. %%%  Not specified. */

    static enum { Check, Report, Ignore } report_it = Check;
    XtAppContext app = XtDisplayToApplicationContext(dpy);

    LOCK_APP(app);
    LOCK_PROCESS;
    if (report_it == Check) {
        XrmDatabase rdb = XtDatabase(dpy);
        XrmName xrm_name[2];
        XrmClass xrm_class[2];
        XrmRepresentation rep_type;
        XrmValue value;

        xrm_name[0] = XrmPermStringToQuark("stringConversionWarnings");
        xrm_name[1] = 0;
        xrm_class[0] = XrmPermStringToQuark("StringConversionWarnings");
        xrm_class[1] = 0;
        if (XrmQGetResource(rdb, xrm_name, xrm_class, &rep_type, &value)) {
            if (rep_type == XtQBoolean)
                report_it = *(Boolean *) value.addr ? Report : Ignore;
            else if (rep_type == _XtQString) {
                XrmValue toVal;
                Boolean report = False;

                toVal.addr = (XPointer) &report;
                toVal.size = sizeof(Boolean);
                if (XtCallConverter
                    (dpy, XtCvtStringToBoolean, (XrmValuePtr) NULL,
                     (Cardinal) 0, &value, &toVal, (XtCacheRef *) NULL))
                    report_it = report ? Report : Ignore;
            }
            else
                report_it = Report;
        }
        else
            report_it = Report;
    }

    if (report_it == Report) {
#endif                          /* ifndef NO_MIT_HACKS */
        String params[2];
        Cardinal num_params = 2;

        params[0] = (String) from;
        params[1] = (String) toType;
        XtAppWarningMsg(app,
                        XtNconversionError, "string", XtCXtToolkitError,
                        "Cannot convert string \"%s\" to type %s",
                        params, &num_params);
#ifndef NO_MIT_HACKS
    }
#endif                          /* ifndef NO_MIT_HACKS */
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
}

void
XtStringConversionWarning(_Xconst char *from, _Xconst char *toType)
{
    String params[2];
    Cardinal num_params = 2;

    params[0] = (String) from;
    params[1] = (String) toType;
    XtWarningMsg(XtNconversionError, "string", XtCXtToolkitError,
                 "Cannot convert string \"%s\" to type %s",
                 params, &num_params);
}

static int CompareISOLatin1(const char *, const char *);

static Boolean
IsInteger(String string, int *value)
{
    Boolean foundDigit = False;
    Boolean isNegative = False;
    Boolean isPositive = False;
    int val = 0;
    char ch;

    /* skip leading whitespace */
    while ((ch = *string) == ' ' || ch == '\t')
        string++;
    while ((ch = *string++)) {
        if (ch >= '0' && ch <= '9') {
            val *= 10;
            val += ch - '0';
            foundDigit = True;
            continue;
        }
        if (IsWhitespace(ch)) {
            if (!foundDigit)
                return False;
            /* make sure only trailing whitespace */
            while ((ch = *string++)) {
                if (!IsWhitespace(ch))
                    return False;
            }
            break;
        }
        if (ch == '-' && !foundDigit && !isNegative && !isPositive) {
            isNegative = True;
            continue;
        }
        if (ch == '+' && !foundDigit && !isNegative && !isPositive) {
            isPositive = True;
            continue;
        }
        return False;
    }
    if (ch == '\0') {
        if (isNegative)
            *value = -val;
        else
            *value = val;
        return True;
    }
    return False;
}

Boolean
XtCvtIntToBoolean(Display *dpy,
                  XrmValuePtr args _X_UNUSED,
                  Cardinal *num_args,
                  XrmValuePtr fromVal,
                  XrmValuePtr toVal,
                  XtPointer *closure_ret _X_UNUSED)
{
    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtIntToBoolean",
                        XtCXtToolkitError,
                        "Integer to Boolean conversion needs no extra arguments",
                        NULL, NULL);
    done(Boolean, (*(int *) fromVal->addr != 0));
}

Boolean
XtCvtIntToShort(Display *dpy,
                XrmValuePtr args _X_UNUSED,
                Cardinal *num_args,
                XrmValuePtr fromVal,
                XrmValuePtr toVal,
                XtPointer *closure_ret _X_UNUSED)
{
    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtIntToShort", XtCXtToolkitError,
                        "Integer to Short conversion needs no extra arguments",
                        NULL, NULL);
    done(short, (*(int *) fromVal->addr));
}

Boolean
XtCvtStringToBoolean(Display *dpy,
                     XrmValuePtr args _X_UNUSED,
                     Cardinal *num_args,
                     XrmValuePtr fromVal,
                     XrmValuePtr toVal,
                     XtPointer *closure_ret _X_UNUSED)
{
    String str = (String) fromVal->addr;

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToBoolean",
                        XtCXtToolkitError,
                        "String to Boolean conversion needs no extra arguments",
                        NULL, NULL);

    if ((CompareISOLatin1(str, "true") == 0)
        || (CompareISOLatin1(str, "yes") == 0)
        || (CompareISOLatin1(str, "on") == 0)
        || (CompareISOLatin1(str, "1") == 0))
        done_string(Boolean, True, XtRBoolean);

    if ((CompareISOLatin1(str, "false") == 0)
        || (CompareISOLatin1(str, "no") == 0)
        || (CompareISOLatin1(str, "off") == 0)
        || (CompareISOLatin1(str, "0") == 0))
        done_string(Boolean, False, XtRBoolean);

    XtDisplayStringConversionWarning(dpy, str, XtRBoolean);
    return False;
}

Boolean
XtCvtIntToBool(Display *dpy,
               XrmValuePtr args _X_UNUSED,
               Cardinal *num_args,
               XrmValuePtr fromVal,
               XrmValuePtr toVal,
               XtPointer *closure_ret _X_UNUSED)
{
    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtIntToBool", XtCXtToolkitError,
                        "Integer to Bool conversion needs no extra arguments",
                        NULL, NULL);
    done(Bool, (*(int *) fromVal->addr != 0));
}

Boolean
XtCvtStringToBool(Display *dpy,
                  XrmValuePtr args _X_UNUSED,
                  Cardinal *num_args,
                  XrmValuePtr fromVal,
                  XrmValuePtr toVal,
                  XtPointer *closure_ret _X_UNUSED)
{
    String str = (String) fromVal->addr;

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToBool",
                        XtCXtToolkitError,
                        "String to Bool conversion needs no extra arguments",
                        NULL, NULL);

    if ((CompareISOLatin1(str, "true") == 0)
        || (CompareISOLatin1(str, "yes") == 0)
        || (CompareISOLatin1(str, "on") == 0)
        || (CompareISOLatin1(str, "1") == 0))
        done_string(Bool, True, XtRBool);

    if ((CompareISOLatin1(str, "false") == 0)
        || (CompareISOLatin1(str, "no") == 0)
        || (CompareISOLatin1(str, "off") == 0)
        || (CompareISOLatin1(str, "0") == 0))
        done_string(Bool, False, XtRBool);

    XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr, XtRBool);
    return False;
}

/* *INDENT-OFF* */
XtConvertArgRec const colorConvertArgs[] = {
    {XtWidgetBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.screen),
     sizeof(Screen *)},
    {XtWidgetBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.colormap),
     sizeof(Colormap)}
};
/* *INDENT-ON* */

Boolean
XtCvtIntToColor(Display *dpy,
                XrmValuePtr args,
                Cardinal *num_args,
                XrmValuePtr fromVal,
                XrmValuePtr toVal,
                XtPointer *closure_ret _X_UNUSED)
{
    XColor c;
    Screen *screen;
    Colormap colormap;

    if (*num_args != 2) {
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtIntOrPixelToXColor",
                        XtCXtToolkitError,
                        "Pixel to color conversion needs screen and colormap arguments",
                        NULL, NULL);
        return False;
    }
    screen = *((Screen **) args[0].addr);
    colormap = *((Colormap *) args[1].addr);
    c.pixel = (unsigned long) (*(int *) fromVal->addr);

    XQueryColor(DisplayOfScreen(screen), colormap, &c);
    done_typed(XColor, c);
}

Boolean
XtCvtStringToPixel(Display *dpy,
                   XrmValuePtr args,
                   Cardinal *num_args,
                   XrmValuePtr fromVal,
                   XrmValuePtr toVal,
                   XtPointer *closure_ret)
{
    String str = (String) fromVal->addr;
    XColor screenColor;
    XColor exactColor;
    Screen *screen;
    XtPerDisplay pd = _XtGetPerDisplay(dpy);
    Colormap colormap;
    Status status;
    Cardinal num_params = 1;

    if (*num_args != 2) {
        XtAppWarningMsg(pd->appContext, XtNwrongParameters, "cvtStringToPixel",
                        XtCXtToolkitError,
                        "String to pixel conversion needs screen and colormap arguments",
                        NULL, NULL);
        return False;
    }

    screen = *((Screen **) args[0].addr);
    colormap = *((Colormap *) args[1].addr);

    if (CompareISOLatin1(str, XtDefaultBackground) == 0) {
        *closure_ret = NULL;
        if (pd->rv) {
            done_string(Pixel, BlackPixelOfScreen(screen), XtRPixel);
        }
        else {
            done_string(Pixel, WhitePixelOfScreen(screen), XtRPixel);
        }
    }
    if (CompareISOLatin1(str, XtDefaultForeground) == 0) {
        *closure_ret = NULL;
        if (pd->rv) {
            done_string(Pixel, WhitePixelOfScreen(screen), XtRPixel);
        }
        else {
            done_string(Pixel, BlackPixelOfScreen(screen), XtRPixel);
        }
    }

    status = XAllocNamedColor(DisplayOfScreen(screen), colormap,
                              (_Xconst char *) str, &screenColor, &exactColor);
    if (status == 0) {
        _Xconst _XtString msg;
        _Xconst _XtString type;
        String params[1];

        params[0] = str;
        /* Server returns a specific error code but Xlib discards it.  Ugh */
        if (XLookupColor
            (DisplayOfScreen(screen), colormap, (_Xconst char *) str,
             &exactColor, &screenColor)) {
            type = "noColormap";
            msg = "Cannot allocate colormap entry for \"%s\"";
        }
        else {
            type = "badValue";
            msg = "Color name \"%s\" is not defined";
        }

        XtAppWarningMsg(pd->appContext, type, "cvtStringToPixel",
                        XtCXtToolkitError, msg, params, &num_params);
        *closure_ret = NULL;
        return False;
    }
    else {
        *closure_ret = (char *) True;
        done_string(Pixel, screenColor.pixel, XtRPixel);
    }
}

static void
FreePixel(XtAppContext app,
          XrmValuePtr toVal,
          XtPointer closure,
          XrmValuePtr args,
          Cardinal *num_args)
{
    Screen *screen;
    Colormap colormap;

    if (*num_args != 2) {
        XtAppWarningMsg(app, XtNwrongParameters, "freePixel", XtCXtToolkitError,
                        "Freeing a pixel requires screen and colormap arguments",
                        NULL, NULL);
        return;
    }

    screen = *((Screen **) args[0].addr);
    colormap = *((Colormap *) args[1].addr);

    if (closure) {
        XFreeColors(DisplayOfScreen(screen), colormap,
                    (unsigned long *) toVal->addr, 1, (unsigned long) 0);
    }
}

/* no longer used by Xt, but it's in the spec */
XtConvertArgRec const screenConvertArg[] = {
    {XtWidgetBaseOffset, (XtPointer) XtOffsetOf(WidgetRec, core.screen),
     sizeof(Screen *)}
};

static void
FetchDisplayArg(Widget widget, Cardinal *size _X_UNUSED, XrmValue *value)
{
    if (widget == NULL) {
        XtErrorMsg("missingWidget", "fetchDisplayArg", XtCXtToolkitError,
                   "FetchDisplayArg called without a widget to reference",
                   NULL, NULL);
        /* can't return any useful Display and caller will de-ref NULL,
           so aborting is the only useful option */
    }
    else {
        value->size = sizeof(Display *);
        value->addr = (XPointer) &DisplayOfScreen(XtScreenOfObject(widget));
    }
}

/* *INDENT-OFF* */
static XtConvertArgRec const displayConvertArg[] = {
    {XtProcedureArg, (XtPointer)FetchDisplayArg, 0},
};
/* *INDENT-ON* */

Boolean
XtCvtStringToCursor(Display *dpy,
                    XrmValuePtr args,
                    Cardinal *num_args,
                    XrmValuePtr fromVal,
                    XrmValuePtr toVal,
                    XtPointer *closure_ret _X_UNUSED)
{
    /* *INDENT-OFF* */
    static const struct _CursorName {
        const char      *name;
        unsigned int    shape;
    } cursor_names[] = {
        {"X_cursor",            XC_X_cursor},
        {"arrow",               XC_arrow},
        {"based_arrow_down",    XC_based_arrow_down},
        {"based_arrow_up",      XC_based_arrow_up},
        {"boat",                XC_boat},
        {"bogosity",            XC_bogosity},
        {"bottom_left_corner",  XC_bottom_left_corner},
        {"bottom_right_corner", XC_bottom_right_corner},
        {"bottom_side",         XC_bottom_side},
        {"bottom_tee",          XC_bottom_tee},
        {"box_spiral",          XC_box_spiral},
        {"center_ptr",          XC_center_ptr},
        {"circle",              XC_circle},
        {"clock",               XC_clock},
        {"coffee_mug",          XC_coffee_mug},
        {"cross",               XC_cross},
        {"cross_reverse",       XC_cross_reverse},
        {"crosshair",           XC_crosshair},
        {"diamond_cross",       XC_diamond_cross},
        {"dot",                 XC_dot},
        {"dotbox",              XC_dotbox},
        {"double_arrow",        XC_double_arrow},
        {"draft_large",         XC_draft_large},
        {"draft_small",         XC_draft_small},
        {"draped_box",          XC_draped_box},
        {"exchange",            XC_exchange},
        {"fleur",               XC_fleur},
        {"gobbler",             XC_gobbler},
        {"gumby",               XC_gumby},
        {"hand1",               XC_hand1},
        {"hand2",               XC_hand2},
        {"heart",               XC_heart},
        {"icon",                XC_icon},
        {"iron_cross",          XC_iron_cross},
        {"left_ptr",            XC_left_ptr},
        {"left_side",           XC_left_side},
        {"left_tee",            XC_left_tee},
        {"leftbutton",          XC_leftbutton},
        {"ll_angle",            XC_ll_angle},
        {"lr_angle",            XC_lr_angle},
        {"man",                 XC_man},
        {"middlebutton",        XC_middlebutton},
        {"mouse",               XC_mouse},
        {"pencil",              XC_pencil},
        {"pirate",              XC_pirate},
        {"plus",                XC_plus},
        {"question_arrow",      XC_question_arrow},
        {"right_ptr",           XC_right_ptr},
        {"right_side",          XC_right_side},
        {"right_tee",           XC_right_tee},
        {"rightbutton",         XC_rightbutton},
        {"rtl_logo",            XC_rtl_logo},
        {"sailboat",            XC_sailboat},
        {"sb_down_arrow",       XC_sb_down_arrow},
        {"sb_h_double_arrow",   XC_sb_h_double_arrow},
        {"sb_left_arrow",       XC_sb_left_arrow},
        {"sb_right_arrow",      XC_sb_right_arrow},
        {"sb_up_arrow",         XC_sb_up_arrow},
        {"sb_v_double_arrow",   XC_sb_v_double_arrow},
        {"shuttle",             XC_shuttle},
        {"sizing",              XC_sizing},
        {"spider",              XC_spider},
        {"spraycan",            XC_spraycan},
        {"star",                XC_star},
        {"target",              XC_target},
        {"tcross",              XC_tcross},
        {"top_left_arrow",      XC_top_left_arrow},
        {"top_left_corner",     XC_top_left_corner},
        {"top_right_corner",    XC_top_right_corner},
        {"top_side",            XC_top_side},
        {"top_tee",             XC_top_tee},
        {"trek",                XC_trek},
        {"ul_angle",            XC_ul_angle},
        {"umbrella",            XC_umbrella},
        {"ur_angle",            XC_ur_angle},
        {"watch",               XC_watch},
        {"xterm",               XC_xterm},
    };
    /* *INDENT-ON* */
    const struct _CursorName *nP;
    char *name = (char *) fromVal->addr;
    register Cardinal i;

    if (*num_args != 1) {
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToCursor",
                        XtCXtToolkitError,
                        "String to cursor conversion needs display argument",
                        NULL, NULL);
        return False;
    }

    for (i = 0, nP = cursor_names; i < XtNumber(cursor_names); i++, nP++) {
        if (strcmp(name, nP->name) == 0) {
            Display *display = *(Display **) args[0].addr;
            Cursor cursor = XCreateFontCursor(display, nP->shape);

            done_string(Cursor, cursor, XtRCursor);
        }
    }
    XtDisplayStringConversionWarning(dpy, name, XtRCursor);
    return False;
}

static void
FreeCursor(XtAppContext app,
           XrmValuePtr toVal,
           XtPointer closure _X_UNUSED,
           XrmValuePtr args,
           Cardinal *num_args)
{
    Display *display;

    if (*num_args != 1) {
        XtAppWarningMsg(app,
                        XtNwrongParameters, "freeCursor", XtCXtToolkitError,
                        "Free Cursor requires display argument", NULL, NULL);
        return;
    }

    display = *(Display **) args[0].addr;
    XFreeCursor(display, *(Cursor *) toVal->addr);
}

Boolean
XtCvtStringToDisplay(Display *dpy,
                     XrmValuePtr args _X_UNUSED,
                     Cardinal *num_args,
                     XrmValuePtr fromVal,
                     XrmValuePtr toVal,
                     XtPointer *closure_ret _X_UNUSED)
{
    Display *d;

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToDisplay",
                        XtCXtToolkitError,
                        "String to Display conversion needs no extra arguments",
                        NULL, NULL);

    d = XOpenDisplay((char *) fromVal->addr);
    if (d != NULL)
        done_string(Display *, d, XtRDisplay);

    XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr, XtRDisplay);
    return False;
}

Boolean
XtCvtStringToFile(Display *dpy,
                  XrmValuePtr args _X_UNUSED,
                  Cardinal *num_args,
                  XrmValuePtr fromVal,
                  XrmValuePtr toVal,
                  XtPointer *closure_ret _X_UNUSED)
{
    FILE *f;

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToFile",
                        XtCXtToolkitError,
                        "String to File conversion needs no extra arguments",
                        NULL, NULL);

    f = fopen((char *) fromVal->addr, "r");
    if (f != NULL)
        done_string(FILE *, f, XtRFile);

    XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr, XtRFile);
    return False;
}

static void
FreeFile(XtAppContext app,
         XrmValuePtr toVal,
         XtPointer closure _X_UNUSED,
         XrmValuePtr args _X_UNUSED,
         Cardinal *num_args)
{
    if (*num_args != 0)
        XtAppWarningMsg(app,
                        XtNwrongParameters, "freeFile", XtCXtToolkitError,
                        "Free File requires no extra arguments", NULL, NULL);

    fclose(*(FILE **) toVal->addr);
}

Boolean
XtCvtIntToFloat(Display *dpy,
                XrmValuePtr args _X_UNUSED,
                Cardinal *num_args,
                XrmValuePtr fromVal,
                XrmValuePtr toVal,
                XtPointer *closure_ret _X_UNUSED)
{
    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtIntToFloat", XtCXtToolkitError,
                        "Integer to Float conversion needs no extra arguments",
                        NULL, NULL);
    done(float, (*(int *) fromVal->addr));
}

Boolean
XtCvtStringToFloat(Display *dpy,
                   XrmValuePtr args _X_UNUSED,
                   Cardinal *num_args,
                   XrmValuePtr fromVal,
                   XrmValuePtr toVal,
                   XtPointer *closure_ret _X_UNUSED)
{
    int ret;
    float f, nan = 0.0;

    (void) sscanf("NaN", "%g",
                  toVal->addr != NULL ? (float *) toVal->addr : &nan);

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToFloat",
                        XtCXtToolkitError,
                        "String to Float conversion needs no extra arguments",
                        NULL, NULL);

    ret = sscanf(fromVal->addr, "%g", &f);
    if (ret == 0) {
        if (toVal->addr != NULL && toVal->size == sizeof nan)
            *(float *) toVal->addr = nan;
        XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr, XtRFloat);
        return False;
    }
    done_string(float, f, XtRFloat);
}

Boolean
XtCvtStringToFont(Display *dpy,
                  XrmValuePtr args,
                  Cardinal *num_args,
                  XrmValuePtr fromVal,
                  XrmValuePtr toVal,
                  XtPointer *closure_ret _X_UNUSED)
{
    Font f;
    Display *display;

    if (*num_args != 1) {
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToFont",
                        XtCXtToolkitError,
                        "String to font conversion needs display argument",
                        NULL, NULL);
        return False;
    }

    display = *(Display **) args[0].addr;

    if (CompareISOLatin1((String) fromVal->addr, XtDefaultFont) != 0) {
        f = XLoadFont(display, (char *) fromVal->addr);

        if (f != 0) {
 Done:     done_string(Font, f, XtRFont);
        }
        XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr, XtRFont);
    }
    /* try and get the default font */

    {
        XrmName xrm_name[2];
        XrmClass xrm_class[2];
        XrmRepresentation rep_type;
        XrmValue value;

        xrm_name[0] = XrmPermStringToQuark("xtDefaultFont");
        xrm_name[1] = 0;
        xrm_class[0] = XrmPermStringToQuark("XtDefaultFont");
        xrm_class[1] = 0;
        if (XrmQGetResource(XtDatabase(display), xrm_name, xrm_class,
                            &rep_type, &value)) {
            if (rep_type == _XtQString) {
                f = XLoadFont(display, (char *) value.addr);

                if (f != 0)
                    goto Done;
                else
                    XtDisplayStringConversionWarning(dpy, (char *) value.addr,
                                                     XtRFont);
            }
            else if (rep_type == XtQFont) {
                f = *(Font *) value.addr;
                goto Done;
            }
            else if (rep_type == XtQFontStruct) {
                f = ((XFontStruct *) value.addr)->fid;
                goto Done;
            }
        }
    }
    /* Should really do XListFonts, but most servers support this */
    f = XLoadFont(display, "-*-*-*-R-*-*-*-120-*-*-*-*-ISO8859-*");

    if (f != 0)
        goto Done;

    XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                    "noFont", "cvtStringToFont", XtCXtToolkitError,
                    "Unable to load any usable ISO8859 font", NULL, NULL);

    return False;
}

static void
FreeFont(XtAppContext app,
         XrmValuePtr toVal,
         XtPointer closure _X_UNUSED,
         XrmValuePtr args,
         Cardinal *num_args)
{
    Display *display;

    if (*num_args != 1) {
        XtAppWarningMsg(app,
                        XtNwrongParameters, "freeFont", XtCXtToolkitError,
                        "Free Font needs display argument", NULL, NULL);
        return;
    }

    display = *(Display **) args[0].addr;
    XUnloadFont(display, *(Font *) toVal->addr);
}

Boolean
XtCvtIntToFont(Display *dpy,
               XrmValuePtr args _X_UNUSED,
               Cardinal *num_args,
               XrmValuePtr fromVal,
               XrmValuePtr toVal,
               XtPointer *closure_ret _X_UNUSED)
{
    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtIntToFont", XtCXtToolkitError,
                        "Integer to Font conversion needs no extra arguments",
                        NULL, NULL);
    done(Font, *(int *) fromVal->addr);
}

Boolean
XtCvtStringToFontSet(Display *dpy,
                     XrmValuePtr args,
                     Cardinal *num_args,
                     XrmValuePtr fromVal,
                     XrmValuePtr toVal,
                     XtPointer *closure_ret _X_UNUSED)
{
    XFontSet f;
    Display *display;
    char **missing_charset_list;
    int missing_charset_count;
    char *def_string;

    if (*num_args != 2) {
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToFontSet",
                        XtCXtToolkitError,
                        "String to FontSet conversion needs display and locale arguments",
                        NULL, NULL);
        return False;
    }

    display = *(Display **) args[0].addr;

    if (CompareISOLatin1((String) fromVal->addr, XtDefaultFontSet) != 0) {
        f = XCreateFontSet(display, (char *) fromVal->addr,
                           &missing_charset_list, &missing_charset_count,
                           &def_string);
        /* Free any returned missing charset list */
        if (missing_charset_count) {
            XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                            XtNmissingCharsetList, "cvtStringToFontSet",
                            XtCXtToolkitError,
                            "Missing charsets in String to FontSet conversion",
                            NULL, NULL);
            XFreeStringList(missing_charset_list);
        }
        if (f != NULL) {
 Done:     done_string(XFontSet, f, XtRFontSet);
        }
        XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr,
                                         XtRFontSet);
    }
    /* try and get the default fontset */

    {
        XrmName xrm_name[2];
        XrmClass xrm_class[2];
        XrmRepresentation rep_type;
        XrmValue value;

        xrm_name[0] = XrmPermStringToQuark("xtDefaultFontSet");
        xrm_name[1] = 0;
        xrm_class[0] = XrmPermStringToQuark("XtDefaultFontSet");
        xrm_class[1] = 0;
        if (XrmQGetResource(XtDatabase(display), xrm_name, xrm_class,
                            &rep_type, &value)) {
            if (rep_type == _XtQString) {

                f = XCreateFontSet(display, (char *) value.addr,
                                   &missing_charset_list,
                                   &missing_charset_count, &def_string);
                /* Free any returned missing charset list */
                if (missing_charset_count) {
                    XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                                    XtNmissingCharsetList, "cvtStringToFontSet",
                                    XtCXtToolkitError,
                                    "Missing charsets in String to FontSet conversion",
                                    NULL, NULL);
                    XFreeStringList(missing_charset_list);
                }
                if (f != NULL)
                    goto Done;
                else
                    XtDisplayStringConversionWarning(dpy, (char *) value.addr,
                                                     XtRFontSet);
            }
            else if (rep_type == XtQFontSet) {
                f = *(XFontSet *) value.addr;
                goto Done;
            }
        }
    }

    /* Should really do XListFonts, but most servers support this */
    f = XCreateFontSet(display, "-*-*-*-R-*-*-*-120-*-*-*-*,*",
                       &missing_charset_list, &missing_charset_count,
                       &def_string);

    /* Free any returned missing charset list */
    if (missing_charset_count) {
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNmissingCharsetList, "cvtStringToFontSet",
                        XtCXtToolkitError,
                        "Missing charsets in String to FontSet conversion",
                        NULL, NULL);
        XFreeStringList(missing_charset_list);
    }
    if (f != NULL)
        goto Done;

    XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                    "noFont", "cvtStringToFontSet", XtCXtToolkitError,
                    "Unable to load any usable fontset", NULL, NULL);

    return False;
}

static void
FreeFontSet(XtAppContext app,
            XrmValuePtr toVal,
            XtPointer closure _X_UNUSED,
            XrmValuePtr args,
            Cardinal *num_args)
{
    Display *display;

    if (*num_args != 2) {
        XtAppWarningMsg(app,
                        XtNwrongParameters, "freeFontSet", XtCXtToolkitError,
                        "FreeFontSet needs display and locale arguments",
                        NULL, NULL);
        return;
    }

    display = *(Display **) args[0].addr;
    XFreeFontSet(display, *(XFontSet *) toVal->addr);
}

static void
FetchLocaleArg(Widget widget _X_UNUSED,
               Cardinal *size _X_UNUSED,
               XrmValue *value)
{
    static XrmString locale;

    locale = XrmQuarkToString(XrmStringToQuark
                              (setlocale(LC_CTYPE, (char *) NULL)));
    value->size = sizeof(XrmString);
    value->addr = (XPointer) &locale;
}

/* *INDENT-OFF* */
static XtConvertArgRec const localeDisplayConvertArgs[] = {
    {XtProcedureArg, (XtPointer)FetchDisplayArg, 0},
    {XtProcedureArg, (XtPointer)FetchLocaleArg, 0},
};
/* *INDENT-ON* */

Boolean
XtCvtStringToFontStruct(Display *dpy,
                        XrmValuePtr args,
                        Cardinal *num_args,
                        XrmValuePtr fromVal,
                        XrmValuePtr toVal,
                        XtPointer *closure_ret _X_UNUSED)
{
    XFontStruct *f;
    Display *display;

    if (*num_args != 1) {
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToFontStruct",
                        XtCXtToolkitError,
                        "String to font conversion needs display argument",
                        NULL, NULL);
        return False;
    }

    display = *(Display **) args[0].addr;

    if (CompareISOLatin1((String) fromVal->addr, XtDefaultFont) != 0) {
        f = XLoadQueryFont(display, (char *) fromVal->addr);

        if (f != NULL) {
 Done:     done_string(XFontStruct *, f, XtRFontStruct);
        }

        XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr,
                                         XtRFontStruct);
    }

    /* try and get the default font */

    {
        XrmName xrm_name[2];
        XrmClass xrm_class[2];
        XrmRepresentation rep_type;
        XrmValue value;

        xrm_name[0] = XrmPermStringToQuark("xtDefaultFont");
        xrm_name[1] = 0;
        xrm_class[0] = XrmPermStringToQuark("XtDefaultFont");
        xrm_class[1] = 0;
        if (XrmQGetResource(XtDatabase(display), xrm_name, xrm_class,
                            &rep_type, &value)) {
            if (rep_type == _XtQString) {
                f = XLoadQueryFont(display, (char *) value.addr);

                if (f != NULL)
                    goto Done;
                else
                    XtDisplayStringConversionWarning(dpy, (char *) value.addr,
                                                     XtRFontStruct);
            }
            else if (rep_type == XtQFont) {
                f = XQueryFont(display, *(Font *) value.addr);

                if (f != NULL)
                    goto Done;
            }
            else if (rep_type == XtQFontStruct) {
                f = (XFontStruct *) value.addr;
                goto Done;
            }
        }
    }
    /* Should really do XListFonts, but most servers support this */
    f = XLoadQueryFont(display, "-*-*-*-R-*-*-*-120-*-*-*-*-ISO8859-*");

    if (f != NULL)
        goto Done;

    XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                    "noFont", "cvtStringToFontStruct", XtCXtToolkitError,
                    "Unable to load any usable ISO8859 font", NULL, NULL);

    return False;
}

static void
FreeFontStruct(XtAppContext app,
               XrmValuePtr toVal,
               XtPointer closure _X_UNUSED,
               XrmValuePtr args,
               Cardinal *num_args)
{
    Display *display;

    if (*num_args != 1) {
        XtAppWarningMsg(app,
                        XtNwrongParameters, "freeFontStruct", XtCXtToolkitError,
                        "Free FontStruct requires display argument",
                        NULL, NULL);
        return;
    }

    display = *(Display **) args[0].addr;
    XFreeFont(display, *(XFontStruct **) toVal->addr);
}

Boolean
XtCvtStringToInt(Display *dpy,
                 XrmValuePtr args _X_UNUSED,
                 Cardinal *num_args,
                 XrmValuePtr fromVal,
                 XrmValuePtr toVal,
                 XtPointer *closure_ret _X_UNUSED)
{
    int i;

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToInt", XtCXtToolkitError,
                        "String to Integer conversion needs no extra arguments",
                        NULL, NULL);
    if (IsInteger((String) fromVal->addr, &i))
        done_string(int, i, XtRInt);

    XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr, XtRInt);
    return False;
}

Boolean
XtCvtStringToShort(Display *dpy,
                   XrmValuePtr args _X_UNUSED,
                   Cardinal *num_args,
                   XrmValuePtr fromVal,
                   XrmValuePtr toVal,
                   XtPointer *closure_ret _X_UNUSED)
{
    int i;

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToShort",
                        XtCXtToolkitError,
                        "String to Integer conversion needs no extra arguments",
                        NULL, NULL);
    if (IsInteger((String) fromVal->addr, &i))
        done_string(short, (short) i, XtRShort);

    XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr, XtRShort);
    return False;
}

Boolean
XtCvtStringToDimension(Display *dpy,
                       XrmValuePtr args _X_UNUSED,
                       Cardinal *num_args,
                       XrmValuePtr fromVal,
                       XrmValuePtr toVal,
                       XtPointer *closure_ret _X_UNUSED)
{
    int i;

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToDimension",
                        XtCXtToolkitError,
                        "String to Dimension conversion needs no extra arguments",
                        NULL, NULL);
    if (IsInteger((String) fromVal->addr, &i)) {
        if (i < 0)
            XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr,
                                             XtRDimension);
        done_string(Dimension, (Dimension) i, XtRDimension);
    }
    XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr, XtRDimension);
    return False;
}

Boolean
XtCvtIntToUnsignedChar(Display *dpy,
                       XrmValuePtr args _X_UNUSED,
                       Cardinal *num_args,
                       XrmValuePtr fromVal,
                       XrmValuePtr toVal,
                       XtPointer *closure_ret _X_UNUSED)
{
    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtIntToUnsignedChar",
                        XtCXtToolkitError,
                        "Integer to UnsignedChar conversion needs no extra arguments",
                        NULL, NULL);
    done(unsigned char, (*(int *) fromVal->addr));
}

Boolean
XtCvtStringToUnsignedChar(Display *dpy,
                          XrmValuePtr args _X_UNUSED,
                          Cardinal *num_args,
                          XrmValuePtr fromVal,
                          XrmValuePtr toVal,
                          XtPointer *closure_ret _X_UNUSED)
{
    int i;

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToUnsignedChar",
                        XtCXtToolkitError,
                        "String to Integer conversion needs no extra arguments",
                        NULL, NULL);
    if (IsInteger((String) fromVal->addr, &i)) {
        if (i < 0 || i > 255)
            XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr,
                                             XtRUnsignedChar);
        done_string(unsigned char, i, XtRUnsignedChar);
    }
    XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr,
                                     XtRUnsignedChar);
    return False;
}

Boolean
XtCvtColorToPixel(Display *dpy,
                  XrmValuePtr args _X_UNUSED,
                  Cardinal *num_args,
                  XrmValuePtr fromVal,
                  XrmValuePtr toVal,
                  XtPointer *closure_ret _X_UNUSED)
{
    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtXColorToPixel",
                        XtCXtToolkitError,
                        "Color to Pixel conversion needs no extra arguments",
                        NULL, NULL);
    done(Pixel, ((XColor *) fromVal->addr)->pixel);
}

Boolean
XtCvtIntToPixel(Display *dpy,
                XrmValuePtr args _X_UNUSED,
                Cardinal *num_args,
                XrmValuePtr fromVal,
                XrmValuePtr toVal,
                XtPointer *closure_ret _X_UNUSED)
{
    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtIntToPixel", XtCXtToolkitError,
                        "Integer to Pixel conversion needs no extra arguments",
                        NULL, NULL);
    done(Pixel, *(int *) fromVal->addr);
}

Boolean
XtCvtIntToPixmap(Display *dpy,
                 XrmValuePtr args _X_UNUSED,
                 Cardinal *num_args,
                 XrmValuePtr fromVal,
                 XrmValuePtr toVal,
                 XtPointer *closure_ret _X_UNUSED)
{
    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtIntToPixmap", XtCXtToolkitError,
                        "Integer to Pixmap conversion needs no extra arguments",
                        NULL, NULL);
    done(Pixmap, *(int *) fromVal->addr);
}

#ifdef MOTIFBC
void
LowerCase(register char *source, register *dest)
{
    register char ch;
    int i;

    for (i = 0; (ch = *source) != 0 && i < 999; source++, dest++, i++) {
        if ('A' <= ch && ch <= 'Z')
            *dest = ch - 'A' + 'a';
        else
            *dest = ch;
    }
    *dest = 0;
}
#endif

static int
CompareISOLatin1(const char *first, const char *second)
{
    register const unsigned char *ap, *bp;

    for (ap = (const unsigned char *) first,
         bp = (const unsigned char *) second; *ap && *bp; ap++, bp++) {
        register unsigned char a, b;

        if ((a = *ap) != (b = *bp)) {
            /* try lowercasing and try again */

            if ((a >= XK_A) && (a <= XK_Z))
                a = (unsigned char) (a + (XK_a - XK_A));
            else if ((a >= XK_Agrave) && (a <= XK_Odiaeresis))
                a = (unsigned char) (a + (XK_agrave - XK_Agrave));
            else if ((a >= XK_Ooblique) && (a <= XK_Thorn))
                a = (unsigned char) (a + (XK_oslash - XK_Ooblique));

            if ((b >= XK_A) && (b <= XK_Z))
                b = (unsigned char) (b + (XK_a - XK_A));
            else if ((b >= XK_Agrave) && (b <= XK_Odiaeresis))
                b = (unsigned char) (b + (XK_agrave - XK_Agrave));
            else if ((b >= XK_Ooblique) && (b <= XK_Thorn))
                b = (unsigned char) (b + (XK_oslash - XK_Ooblique));

            if (a != b)
                break;
        }
    }
    return (((int) *bp) - ((int) *ap));
}

static void
CopyISOLatin1Lowered(char *dst, const char *src)
{
    unsigned char *dest = (unsigned char *) dst;
    const unsigned char *source = (const unsigned char *) src;

    for (; *source; source++, dest++) {
        if (*source >= XK_A && *source <= XK_Z)
            *dest = (unsigned char) (*source + (XK_a - XK_A));
        else if (*source >= XK_Agrave && *source <= XK_Odiaeresis)
            *dest = (unsigned char) (*source + (XK_agrave - XK_Agrave));
        else if (*source >= XK_Ooblique && *source <= XK_Thorn)
            *dest = (unsigned char) (*source + (XK_oslash - XK_Ooblique));
        else
            *dest = *source;
    }
    *dest = '\0';
}

Boolean
XtCvtStringToInitialState(Display *dpy,
                          XrmValuePtr args _X_UNUSED,
                          Cardinal *num_args,
                          XrmValuePtr fromVal,
                          XrmValuePtr toVal,
                          XtPointer *closure_ret _X_UNUSED)
{
    String str = (String) fromVal->addr;

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToInitialState",
                        XtCXtToolkitError,
                        "String to InitialState conversion needs no extra arguments",
                        NULL, NULL);

    if (CompareISOLatin1(str, "NormalState") == 0)
        done_string(int, NormalState, XtRInitialState);

    if (CompareISOLatin1(str, "IconicState") == 0)
        done_string(int, IconicState, XtRInitialState);

    {
        int val;

        if (IsInteger(str, &val))
            done_string(int, val, XtRInitialState);
    }
    XtDisplayStringConversionWarning(dpy, str, XtRInitialState);
    return False;
}

/* *INDENT-OFF* */
static XtConvertArgRec const visualConvertArgs[] = {
    {XtWidgetBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.screen),
     sizeof(Screen *)},
    {XtWidgetBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.depth),
     sizeof(Cardinal)}
};
/* *INDENT-ON* */

Boolean
XtCvtStringToVisual(Display *dpy, XrmValuePtr args,     /* Screen, depth */
                    Cardinal *num_args,        /* 2 */
                    XrmValuePtr fromVal,
                    XrmValuePtr toVal,
                    XtPointer *closure_ret _X_UNUSED)
{
    String str = (String) fromVal->addr;
    int vc;
    XVisualInfo vinfo;

    if (*num_args != 2) {
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToVisual",
                        XtCXtToolkitError,
                        "String to Visual conversion needs screen and depth arguments",
                        NULL, NULL);
        return False;
    }

    if (CompareISOLatin1(str, "StaticGray") == 0)
        vc = StaticGray;
    else if (CompareISOLatin1(str, "StaticColor") == 0)
        vc = StaticColor;
    else if (CompareISOLatin1(str, "TrueColor") == 0)
        vc = TrueColor;
    else if (CompareISOLatin1(str, "GrayScale") == 0)
        vc = GrayScale;
    else if (CompareISOLatin1(str, "PseudoColor") == 0)
        vc = PseudoColor;
    else if (CompareISOLatin1(str, "DirectColor") == 0)
        vc = DirectColor;
    else if (!IsInteger(str, &vc)) {
        XtDisplayStringConversionWarning(dpy, str, "Visual class name");
        return False;
    }

    if (XMatchVisualInfo(XDisplayOfScreen((Screen *) *(Screen **) args[0].addr),
                         XScreenNumberOfScreen((Screen *) *(Screen **) args[0].
                                               addr),
                         (int) *(int *) args[1].addr, vc, &vinfo)) {
        done_string(Visual *, vinfo.visual, XtRVisual);
    }
    else {
        String params[2];
        Cardinal num_params = 2;

        params[0] = str;
        params[1] =
            DisplayString(XDisplayOfScreen
                          ((Screen *) *(Screen **) args[0].addr));
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy), XtNconversionError,
                        "stringToVisual", XtCXtToolkitError,
                        "Cannot find Visual of class %s for display %s", params,
                        &num_params);
        return False;
    }
}

Boolean
XtCvtStringToAtom(Display *dpy,
                  XrmValuePtr args,
                  Cardinal *num_args,
                  XrmValuePtr fromVal,
                  XrmValuePtr toVal,
                  XtPointer *closure_ret _X_UNUSED)
{
    Atom atom;

    if (*num_args != 1) {
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToAtom",
                        XtCXtToolkitError,
                        "String to Atom conversion needs Display argument",
                        NULL, NULL);
        return False;
    }

    atom = XInternAtom(*(Display **) args->addr, (char *) fromVal->addr, False);
    done_string(Atom, atom, XtRAtom);
}

Boolean
XtCvtStringToDirectoryString(Display *dpy,
                             XrmValuePtr args _X_UNUSED,
                             Cardinal *num_args,
                             XrmValuePtr fromVal,
                             XrmValuePtr toVal,
                             XtPointer *closure_ret _X_UNUSED)
{
    String str;
    char directory[PATH_MAX + 1];

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToDirectoryString",
                        XtCXtToolkitError,
                        "String to DirectoryString conversion needs no extra arguments",
                        NULL, NULL);

    str = (String) fromVal->addr;
    if (CompareISOLatin1(str, "XtCurrentDirectory") == 0) {
        /* uglier, but does not depend on compiler knowing return type */
#if !defined(X_NOT_POSIX) || defined(SYSV) || defined(WIN32)
        if (getcwd(directory, PATH_MAX + 1))
            str = directory;
#else
        if (getwd(directory))
            str = directory;
#endif
        if (!str) {
            if (errno == EACCES)
                errno = 0;      /* reset errno */
            XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr,
                                             XtRDirectoryString);
            return False;
        }
    }

    /* Since memory from the resource database or from static buffers of
     * system libraries may be freed or overwritten, allocate memory.
     * The memory is freed when all cache references are released.
     */
    str = XtNewString(str);
    done_string(String, str, XtRDirectoryString);
}

static void
FreeDirectoryString(XtAppContext app,
                    XrmValuePtr toVal,
                    XtPointer closure _X_UNUSED,
                    XrmValuePtr args _X_UNUSED,
                    Cardinal *num_args)
{
    if (*num_args != 0)
        XtAppWarningMsg(app,
                        XtNwrongParameters, "freeDirectoryString",
                        XtCXtToolkitError,
                        "Free Directory String requires no extra arguments",
                        NULL, NULL);

    XtFree((char *) toVal->addr);
}

Boolean
XtCvtStringToRestartStyle(Display *dpy,
                          XrmValuePtr args _X_UNUSED,
                          Cardinal *num_args,
                          XrmValuePtr fromVal,
                          XrmValuePtr toVal,
                          XtPointer *closure_ret _X_UNUSED)
{
    String str = (String) fromVal->addr;

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToRestartStyle",
                        XtCXtToolkitError,
                        "String to RestartStyle conversion needs no extra arguments",
                        NULL, NULL);

    if (CompareISOLatin1(str, "RestartIfRunning") == 0)
        done_string(unsigned char, SmRestartIfRunning, XtRRestartStyle);

    if (CompareISOLatin1(str, "RestartAnyway") == 0)
        done_string(unsigned char, SmRestartAnyway, XtRRestartStyle);

    if (CompareISOLatin1(str, "RestartImmediately") == 0)
        done_string(unsigned char, SmRestartImmediately, XtRRestartStyle);

    if (CompareISOLatin1(str, "RestartNever") == 0)
        done_string(unsigned char, SmRestartNever, XtRRestartStyle);

    XtDisplayStringConversionWarning(dpy, str, XtRRestartStyle);
    return False;
}

Boolean
XtCvtStringToCommandArgArray(Display *dpy,
                             XrmValuePtr args _X_UNUSED,
                             Cardinal *num_args,
                             XrmValuePtr fromVal,
                             XrmValuePtr toVal,
                             XtPointer *closure_ret)
{
    String *strarray, *ptr;
    char *src;
    char *dst, *dst_str;
    int tokens, len;

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        XtNwrongParameters, "cvtStringToCommandArgArray",
                        XtCXtToolkitError,
                        "String to CommandArgArray conversion needs no extra arguments",
                        NULL, NULL);

    src = fromVal->addr;
    dst = dst_str = __XtMalloc((unsigned) strlen(src) + 1);
    tokens = 0;

    while (*src != '\0') {
        char *start;

        /* skip whitespace */
        while (IsWhitespace(*src) || IsNewline(*src))
            src++;
        /* test for end of string */
        if (*src == '\0')
            break;

        /* start new token */
        tokens++;
        start = src;
        while (*src != '\0' && !IsWhitespace(*src) && !IsNewline(*src)) {
            if (*src == '\\' &&
                (IsWhitespace(*(src + 1)) || IsNewline(*(src + 1)))) {
                len = (int) (src - start);
                if (len) {
                    /* copy preceding part of token */
                    memcpy(dst, start, (size_t) len);
                    dst += len;
                }
                /* skip backslash */
                src++;
                /* next part of token starts at whitespace */
                start = src;
            }
            src++;
        }
        len = (int) (src - start);
        if (len) {
            /* copy last part of token */
            memcpy(dst, start, (size_t) len);
            dst += len;
        }
        *dst = '\0';
        if (*src != '\0')
            dst++;
    }

    ptr = strarray = XtMallocArray((Cardinal) tokens + 1,
                                   (Cardinal) sizeof(String));
    src = dst_str;
    while (--tokens >= 0) {
        *ptr = src;
        ptr++;
        if (tokens) {
            len = (int) strlen(src);
            src = src + len + 1;
        }
    }
    *ptr = NULL;

    *closure_ret = (XtPointer) strarray;
    done_typed_string(String *, strarray, XtRCommandArgArray)
}

static void
ArgArrayDestructor(XtAppContext app _X_UNUSED,
                   XrmValuePtr toVal _X_UNUSED,
                   XtPointer closure,
                   XrmValuePtr args _X_UNUSED,
                   Cardinal *num_args _X_UNUSED)
{
    if (closure) {
        _XtString *strarray = (_XtString *) closure;

        XtFree(*strarray);
        XtFree((char *) strarray);
    }
}

Boolean
XtCvtStringToGravity(Display *dpy,
                     XrmValuePtr args _X_UNUSED,
                     Cardinal *num_args,
                     XrmValuePtr fromVal,
                     XrmValuePtr toVal,
                     XtPointer *closure_ret _X_UNUSED)
{
    /* *INDENT-OFF* */
    static struct _namepair {
        XrmQuark quark;
        const char *name;
        int gravity;
    } names[] = {
        { NULLQUARK, "forget",          ForgetGravity },
        { NULLQUARK, "northwest",       NorthWestGravity },
        { NULLQUARK, "north",           NorthGravity },
        { NULLQUARK, "northeast",       NorthEastGravity },
        { NULLQUARK, "west",            WestGravity },
        { NULLQUARK, "center",          CenterGravity },
        { NULLQUARK, "east",            EastGravity },
        { NULLQUARK, "southwest",       SouthWestGravity },
        { NULLQUARK, "south",           SouthGravity },
        { NULLQUARK, "southeast",       SouthEastGravity },
        { NULLQUARK, "static",          StaticGravity },
        { NULLQUARK, "unmap",           UnmapGravity },
        { NULLQUARK, "0",               ForgetGravity },
        { NULLQUARK, "1",               NorthWestGravity },
        { NULLQUARK, "2",               NorthGravity },
        { NULLQUARK, "3",               NorthEastGravity },
        { NULLQUARK, "4",               WestGravity },
        { NULLQUARK, "5",               CenterGravity },
        { NULLQUARK, "6",               EastGravity },
        { NULLQUARK, "7",               SouthWestGravity },
        { NULLQUARK, "8",               SouthGravity },
        { NULLQUARK, "9",               SouthEastGravity },
        { NULLQUARK, "10",              StaticGravity },
        { NULLQUARK, NULL,              ForgetGravity }
    };
    /* *INDENT-ON* */
    static Boolean haveQuarks = FALSE;
    char lowerName[40];
    XrmQuark q;
    char *s;
    struct _namepair *np;

    if (*num_args != 0) {
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        "wrongParameters", "cvtStringToGravity",
                        "XtToolkitError",
                        "String to Gravity conversion needs no extra arguments",
                        NULL, NULL);
        return False;
    }
    if (!haveQuarks) {
        for (np = names; np->name; np++) {
            np->quark = XrmPermStringToQuark(np->name);
        }
        haveQuarks = TRUE;
    }
    s = (char *) fromVal->addr;
    if (strlen(s) < sizeof lowerName) {
        CopyISOLatin1Lowered(lowerName, s);
        q = XrmStringToQuark(lowerName);
        for (np = names; np->name; np++)
            if (np->quark == q)
                done_string(int, np->gravity, XtRGravity);
    }
    XtDisplayStringConversionWarning(dpy, (char *) fromVal->addr, XtRGravity);
    return False;
}

void
_XtAddDefaultConverters(ConverterTable table)
{
#define Add(from, to, proc, convert_args, num_args, cache) \
    _XtTableAddConverter(table, from, to, proc, \
            (XtConvertArgRec const*) convert_args, (Cardinal)num_args, \
            True, cache, (XtDestructor)NULL, True)

#define Add2(from, to, proc, convert_args, num_args, cache, destructor) \
    _XtTableAddConverter(table, from, to, proc, \
            (XtConvertArgRec const *) convert_args, (Cardinal)num_args, \
            True, cache, destructor, True)

    Add(XtQColor, XtQPixel, XtCvtColorToPixel, NULL, 0, XtCacheNone);

    Add(XtQInt, XtQBool, XtCvtIntToBool, NULL, 0, XtCacheNone);
    Add(XtQInt, XtQBoolean, XtCvtIntToBoolean, NULL, 0, XtCacheNone);
    Add(XtQInt, XtQColor, XtCvtIntToColor,
        colorConvertArgs, XtNumber(colorConvertArgs), XtCacheByDisplay);
    Add(XtQInt, XtQDimension, XtCvtIntToShort, NULL, 0, XtCacheNone);
    Add(XtQInt, XtQFloat, XtCvtIntToFloat, NULL, 0, XtCacheNone);
    Add(XtQInt, XtQFont, XtCvtIntToFont, NULL, 0, XtCacheNone);
    Add(XtQInt, XtQPixel, XtCvtIntToPixel, NULL, 0, XtCacheNone);
    Add(XtQInt, XtQPixmap, XtCvtIntToPixmap, NULL, 0, XtCacheNone);
    Add(XtQInt, XtQPosition, XtCvtIntToShort, NULL, 0, XtCacheNone);
    Add(XtQInt, XtQShort, XtCvtIntToShort, NULL, 0, XtCacheNone);
    Add(XtQInt, XtQUnsignedChar, XtCvtIntToUnsignedChar, NULL, 0, XtCacheNone);

    Add(XtQPixel, XtQColor, XtCvtIntToColor,
        colorConvertArgs, XtNumber(colorConvertArgs), XtCacheByDisplay);

    Add(_XtQString, XtQAtom, XtCvtStringToAtom,
        displayConvertArg, XtNumber(displayConvertArg), XtCacheNone);
    Add(_XtQString, XtQBool, XtCvtStringToBool, NULL, 0, XtCacheNone);
    Add(_XtQString, XtQBoolean, XtCvtStringToBoolean, NULL, 0, XtCacheNone);
    Add2(_XtQString, XtQCommandArgArray, XtCvtStringToCommandArgArray,
         NULL, 0, XtCacheNone | XtCacheRefCount, ArgArrayDestructor);
    Add2(_XtQString, XtQCursor, XtCvtStringToCursor,
         displayConvertArg, XtNumber(displayConvertArg),
         XtCacheByDisplay, FreeCursor);
    Add(_XtQString, XtQDimension, XtCvtStringToDimension, NULL, 0, XtCacheNone);
    Add2(_XtQString, XtQDirectoryString, XtCvtStringToDirectoryString, NULL, 0,
         XtCacheNone | XtCacheRefCount, FreeDirectoryString);
    Add(_XtQString, XtQDisplay, XtCvtStringToDisplay, NULL, 0, XtCacheAll);
    Add2(_XtQString, XtQFile, XtCvtStringToFile, NULL, 0,
         XtCacheAll | XtCacheRefCount, FreeFile);
    Add(_XtQString, XtQFloat, XtCvtStringToFloat, NULL, 0, XtCacheNone);

    Add2(_XtQString, XtQFont, XtCvtStringToFont,
         displayConvertArg, XtNumber(displayConvertArg),
         XtCacheByDisplay, FreeFont);
    Add2(_XtQString, XtQFontSet, XtCvtStringToFontSet,
         localeDisplayConvertArgs, XtNumber(localeDisplayConvertArgs),
         XtCacheByDisplay, FreeFontSet);
    Add2(_XtQString, XtQFontStruct, XtCvtStringToFontStruct,
         displayConvertArg, XtNumber(displayConvertArg),
         XtCacheByDisplay, FreeFontStruct);

    Add(_XtQString, XtQGravity, XtCvtStringToGravity, NULL, 0, XtCacheNone);
    Add(_XtQString, XtQInitialState, XtCvtStringToInitialState, NULL, 0,
        XtCacheNone);
    Add(_XtQString, XtQInt, XtCvtStringToInt, NULL, 0, XtCacheAll);
    Add2(_XtQString, XtQPixel, XtCvtStringToPixel,
         colorConvertArgs, XtNumber(colorConvertArgs),
         XtCacheByDisplay, FreePixel);
    Add(_XtQString, XtQPosition, XtCvtStringToShort, NULL, 0, XtCacheAll);
    Add(_XtQString, XtQRestartStyle, XtCvtStringToRestartStyle, NULL, 0,
        XtCacheNone);
    Add(_XtQString, XtQShort, XtCvtStringToShort, NULL, 0, XtCacheAll);
    Add(_XtQString, XtQUnsignedChar, XtCvtStringToUnsignedChar,
        NULL, 0, XtCacheAll);
    Add2(_XtQString, XtQVisual, XtCvtStringToVisual,
         visualConvertArgs, XtNumber(visualConvertArgs),
         XtCacheByDisplay, NULL);

    _XtAddTMConverters(table);
}
