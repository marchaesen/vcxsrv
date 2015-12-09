/***********************************************************
Copyright (c) 1993, Oracle and/or its affiliates. All rights reserved.

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

Copyright 1987, 1988, 1998  The Open Group

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "IntrinsicI.h"
#include "StringDefs.h"
#include <ctype.h>
#include <stdlib.h>
#ifndef NOTASCII
#define XK_LATIN1
#endif
#define XK_MISCELLANY
#include <X11/keysymdef.h>

#ifdef CACHE_TRANSLATIONS
# ifdef REFCNT_TRANSLATIONS
#  define CACHED XtCacheAll | XtCacheRefCount
# else
#  define CACHED XtCacheAll
# endif
#else
# define CACHED XtCacheNone
#endif

#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

static String XtNtranslationParseError = "translationParseError";

typedef int		EventType;

typedef String (*ParseProc)(
    String /* str; */,
    Opaque /* closure; */,
    EventPtr /* event; */,
    Boolean* /* error */);

typedef TMShortCard	Value;
typedef void (*ModifierProc)(Value, LateBindingsPtr*, Boolean, Value*);

typedef struct _ModifierRec {
    const char *name;
    XrmQuark   signature;
    ModifierProc modifierParseProc;
    Value      value;
} ModifierRec, *ModifierKeys;

typedef struct _EventKey {
    const char  *event;
    XrmQuark	signature;
    EventType	eventType;
    ParseProc	parseDetail;
    Opaque	closure;
}EventKey, *EventKeys;

typedef struct {
    const char	*name;
    XrmQuark	signature;
    Value	value;
} NameValueRec, *NameValueTable;

static void ParseModImmed(Value, LateBindingsPtr*, Boolean, Value*);
static void ParseModSym(Value, LateBindingsPtr*, Boolean, Value*);
static String PanicModeRecovery(String);
static String CheckForPoundSign(String, _XtTranslateOp, _XtTranslateOp *);
static KeySym StringToKeySym(String, Boolean *);
static ModifierRec modifiers[] = {
    {"Shift",	0,	ParseModImmed,ShiftMask},
    {"Lock",	0,	ParseModImmed,LockMask},
    {"Ctrl",	0,	ParseModImmed,ControlMask},
    {"Mod1",	0,	ParseModImmed,Mod1Mask},
    {"Mod2",	0,	ParseModImmed,Mod2Mask},
    {"Mod3",	0,	ParseModImmed,Mod3Mask},
    {"Mod4",	0,	ParseModImmed,Mod4Mask},
    {"Mod5",	0,	ParseModImmed,Mod5Mask},
    {"Meta",	0,	ParseModSym,  XK_Meta_L},
    {"m",       0,      ParseModSym,  XK_Meta_L},
    {"h",       0,      ParseModSym,  XK_Hyper_L},
    {"su",      0,      ParseModSym,  XK_Super_L},
    {"a",       0,      ParseModSym,  XK_Alt_L},
    {"Hyper",   0,      ParseModSym,  XK_Hyper_L},
    {"Super",   0,      ParseModSym,  XK_Super_L},
    {"Alt",     0,      ParseModSym,  XK_Alt_L},
    {"Button1",	0,	ParseModImmed,Button1Mask},
    {"Button2",	0,	ParseModImmed,Button2Mask},
    {"Button3",	0,	ParseModImmed,Button3Mask},
    {"Button4",	0,	ParseModImmed,Button4Mask},
    {"Button5",	0,	ParseModImmed,Button5Mask},
    {"c",	0,	ParseModImmed,ControlMask},
    {"s",	0,	ParseModImmed,ShiftMask},
    {"l",	0,	ParseModImmed,LockMask},
};

static NameValueRec buttonNames[] = {
    {"Button1",	0,	Button1},
    {"Button2", 0,	Button2},
    {"Button3", 0,	Button3},
    {"Button4", 0,	Button4},
    {"Button5", 0,	Button5},
    {NULL, NULLQUARK, 0},
};

static NameValueRec motionDetails[] = {
    {"Normal",		0,	NotifyNormal},
    {"Hint",		0,	NotifyHint},
    {NULL, NULLQUARK, 0},
};

static NameValueRec notifyModes[] = {
    {"Normal",		0,	NotifyNormal},
    {"Grab",		0,	NotifyGrab},
    {"Ungrab",		0,	NotifyUngrab},
    {"WhileGrabbed",    0,	NotifyWhileGrabbed},
    {NULL, NULLQUARK, 0},
};

#if 0
static NameValueRec notifyDetail[] = {
    {"Ancestor",	    0,	NotifyAncestor},
    {"Virtual",		    0,	NotifyVirtual},
    {"Inferior",	    0,	NotifyInferior},
    {"Nonlinear",	    0,	NotifyNonlinear},
    {"NonlinearVirtual",    0,	NotifyNonlinearVirtual},
    {"Pointer",		    0,	NotifyPointer},
    {"PointerRoot",	    0,	NotifyPointerRoot},
    {"DetailNone",	    0,	NotifyDetailNone},
    {NULL, NULLQUARK, 0},
};

static NameValueRec visibilityNotify[] = {
    {"Unobscured",	    0,	VisibilityUnobscured},
    {"PartiallyObscured",   0,	VisibilityPartiallyObscured},
    {"FullyObscured",       0,	VisibilityFullyObscured},
    {NULL, NULLQUARK, 0},
};

static NameValueRec circulation[] = {
    {"OnTop",       0,	PlaceOnTop},
    {"OnBottom",    0,	PlaceOnBottom},
    {NULL, NULLQUARK, 0},
};

static NameValueRec propertyChanged[] = {
    {"NewValue",    0,	PropertyNewValue},
    {"Delete",      0,	PropertyDelete},
    {NULL, NULLQUARK, 0},
};
#endif /*0*/

static NameValueRec mappingNotify[] = {
    {"Modifier",	0,	MappingModifier},
    {"Keyboard",	0,	MappingKeyboard},
    {"Pointer",	0,	MappingPointer},
    {NULL, NULLQUARK, 0},
};

static String ParseKeySym(String, Opaque, EventPtr, Boolean*);
static String ParseKeyAndModifiers(String, Opaque, EventPtr, Boolean*);
static String ParseTable(String, Opaque, EventPtr, Boolean*);
static String ParseImmed(String, Opaque, EventPtr, Boolean*);
static String ParseAddModifier(String, Opaque, EventPtr, Boolean*);
static String ParseNone(String, Opaque, EventPtr, Boolean*);
static String ParseAtom(String, Opaque, EventPtr, Boolean*);

static EventKey events[] = {

/* Event Name,	  Quark, Event Type,	Detail Parser, Closure */

{"KeyPress",	    NULLQUARK, KeyPress,	ParseKeySym,	NULL},
{"Key", 	    NULLQUARK, KeyPress,	ParseKeySym,	NULL},
{"KeyDown",	    NULLQUARK, KeyPress,	ParseKeySym,	NULL},
{"Ctrl",            NULLQUARK, KeyPress, ParseKeyAndModifiers,(Opaque)ControlMask},
{"Shift",           NULLQUARK, KeyPress, ParseKeyAndModifiers,(Opaque)ShiftMask},
{"Meta",            NULLQUARK, KeyPress,   ParseKeyAndModifiers,(Opaque)NULL},
{"KeyUp",	    NULLQUARK, KeyRelease,	ParseKeySym,	NULL},
{"KeyRelease",	    NULLQUARK, KeyRelease,	ParseKeySym,	NULL},

{"ButtonPress",     NULLQUARK, ButtonPress,  ParseTable,(Opaque)buttonNames},
{"BtnDown",	    NULLQUARK, ButtonPress,  ParseTable,(Opaque)buttonNames},
{"Btn1Down",	    NULLQUARK, ButtonPress,	ParseImmed,(Opaque)Button1},
{"Btn2Down", 	    NULLQUARK, ButtonPress,	ParseImmed,(Opaque)Button2},
{"Btn3Down", 	    NULLQUARK, ButtonPress,	ParseImmed,(Opaque)Button3},
{"Btn4Down", 	    NULLQUARK, ButtonPress,	ParseImmed,(Opaque)Button4},
{"Btn5Down", 	    NULLQUARK, ButtonPress,	ParseImmed,(Opaque)Button5},

/* Event Name,	  Quark, Event Type,	Detail Parser, Closure */

{"ButtonRelease",   NULLQUARK, ButtonRelease,  ParseTable,(Opaque)buttonNames},
{"BtnUp", 	    NULLQUARK, ButtonRelease,  ParseTable,(Opaque)buttonNames},
{"Btn1Up", 	    NULLQUARK, ButtonRelease,    ParseImmed,(Opaque)Button1},
{"Btn2Up", 	    NULLQUARK, ButtonRelease,    ParseImmed,(Opaque)Button2},
{"Btn3Up", 	    NULLQUARK, ButtonRelease,    ParseImmed,(Opaque)Button3},
{"Btn4Up", 	    NULLQUARK, ButtonRelease,    ParseImmed,(Opaque)Button4},
{"Btn5Up", 	    NULLQUARK, ButtonRelease,    ParseImmed,(Opaque)Button5},

{"MotionNotify",    NULLQUARK, MotionNotify, ParseTable, (Opaque)motionDetails},
{"PtrMoved", 	    NULLQUARK, MotionNotify, ParseTable, (Opaque)motionDetails},
{"Motion", 	    NULLQUARK, MotionNotify, ParseTable, (Opaque)motionDetails},
{"MouseMoved", 	    NULLQUARK, MotionNotify, ParseTable, (Opaque)motionDetails},
{"BtnMotion",  NULLQUARK, MotionNotify, ParseAddModifier, (Opaque)AnyButtonMask},
{"Btn1Motion", NULLQUARK, MotionNotify, ParseAddModifier, (Opaque)Button1Mask},
{"Btn2Motion", NULLQUARK, MotionNotify, ParseAddModifier, (Opaque)Button2Mask},
{"Btn3Motion", NULLQUARK, MotionNotify, ParseAddModifier, (Opaque)Button3Mask},
{"Btn4Motion", NULLQUARK, MotionNotify, ParseAddModifier, (Opaque)Button4Mask},
{"Btn5Motion", NULLQUARK, MotionNotify, ParseAddModifier, (Opaque)Button5Mask},

{"EnterNotify",     NULLQUARK, EnterNotify,    ParseTable,(Opaque)notifyModes},
{"Enter",	    NULLQUARK, EnterNotify,    ParseTable,(Opaque)notifyModes},
{"EnterWindow",     NULLQUARK, EnterNotify,    ParseTable,(Opaque)notifyModes},

{"LeaveNotify",     NULLQUARK, LeaveNotify,    ParseTable,(Opaque)notifyModes},
{"LeaveWindow",     NULLQUARK, LeaveNotify,    ParseTable,(Opaque)notifyModes},
{"Leave",	    NULLQUARK, LeaveNotify,    ParseTable,(Opaque)notifyModes},

/* Event Name,	  Quark, Event Type,	Detail Parser, Closure */

{"FocusIn",	    NULLQUARK, FocusIn,	  ParseTable,(Opaque)notifyModes},

{"FocusOut",	    NULLQUARK, FocusOut,       ParseTable,(Opaque)notifyModes},

{"KeymapNotify",    NULLQUARK, KeymapNotify,	ParseNone,	NULL},
{"Keymap",	    NULLQUARK, KeymapNotify,	ParseNone,	NULL},

{"Expose", 	    NULLQUARK, Expose,		ParseNone,	NULL},

{"GraphicsExpose",  NULLQUARK, GraphicsExpose,	ParseNone,	NULL},
{"GrExp",	    NULLQUARK, GraphicsExpose,	ParseNone,	NULL},

{"NoExpose",	    NULLQUARK, NoExpose,	ParseNone,	NULL},
{"NoExp",	    NULLQUARK, NoExpose,	ParseNone,	NULL},

{"VisibilityNotify",NULLQUARK, VisibilityNotify,ParseNone,	NULL},
{"Visible",	    NULLQUARK, VisibilityNotify,ParseNone,	NULL},

{"CreateNotify",    NULLQUARK, CreateNotify,	ParseNone,	NULL},
{"Create",	    NULLQUARK, CreateNotify,	ParseNone,	NULL},

/* Event Name,	  Quark, Event Type,	Detail Parser, Closure */

{"DestroyNotify",   NULLQUARK, DestroyNotify,	ParseNone,	NULL},
{"Destroy",	    NULLQUARK, DestroyNotify,	ParseNone,	NULL},

{"UnmapNotify",     NULLQUARK, UnmapNotify,	ParseNone,	NULL},
{"Unmap",	    NULLQUARK, UnmapNotify,	ParseNone,	NULL},

{"MapNotify",	    NULLQUARK, MapNotify,	ParseNone,	NULL},
{"Map",		    NULLQUARK, MapNotify,	ParseNone,	NULL},

{"MapRequest",	    NULLQUARK, MapRequest,	ParseNone,	NULL},
{"MapReq",	    NULLQUARK, MapRequest,	ParseNone,	NULL},

{"ReparentNotify",  NULLQUARK, ReparentNotify,	ParseNone,	NULL},
{"Reparent",	    NULLQUARK, ReparentNotify,	ParseNone,	NULL},

{"ConfigureNotify", NULLQUARK, ConfigureNotify,	ParseNone,	NULL},
{"Configure",	    NULLQUARK, ConfigureNotify,	ParseNone,	NULL},

{"ConfigureRequest",NULLQUARK, ConfigureRequest,ParseNone,	NULL},
{"ConfigureReq",    NULLQUARK, ConfigureRequest,ParseNone,	NULL},

/* Event Name,	  Quark, Event Type,	Detail Parser, Closure */

{"GravityNotify",   NULLQUARK, GravityNotify,	ParseNone,	NULL},
{"Grav",	    NULLQUARK, GravityNotify,	ParseNone,	NULL},

{"ResizeRequest",   NULLQUARK, ResizeRequest,	ParseNone,	NULL},
{"ResReq",	    NULLQUARK, ResizeRequest,	ParseNone,	NULL},

{"CirculateNotify", NULLQUARK, CirculateNotify,	ParseNone,	NULL},
{"Circ",	    NULLQUARK, CirculateNotify,	ParseNone,	NULL},

{"CirculateRequest",NULLQUARK, CirculateRequest,ParseNone,	NULL},
{"CircReq",	    NULLQUARK, CirculateRequest,ParseNone,	NULL},

{"PropertyNotify",  NULLQUARK, PropertyNotify,	ParseAtom,	NULL},
{"Prop",	    NULLQUARK, PropertyNotify,	ParseAtom,	NULL},

{"SelectionClear",  NULLQUARK, SelectionClear,	ParseAtom,	NULL},
{"SelClr",	    NULLQUARK, SelectionClear,	ParseAtom,	NULL},

{"SelectionRequest",NULLQUARK, SelectionRequest,ParseAtom,	NULL},
{"SelReq",	    NULLQUARK, SelectionRequest,ParseAtom,	NULL},

/* Event Name,	  Quark, Event Type,	Detail Parser, Closure */

{"SelectionNotify", NULLQUARK, SelectionNotify,	ParseAtom,	NULL},
{"Select",	    NULLQUARK, SelectionNotify,	ParseAtom,	NULL},

{"ColormapNotify",  NULLQUARK, ColormapNotify,	ParseNone,	NULL},
{"Clrmap",	    NULLQUARK, ColormapNotify,	ParseNone,	NULL},

{"ClientMessage",   NULLQUARK, ClientMessage,	ParseAtom,	NULL},
{"Message",	    NULLQUARK, ClientMessage,	ParseAtom,	NULL},

{"MappingNotify",   NULLQUARK, MappingNotify, ParseTable, (Opaque)mappingNotify},
{"Mapping",	    NULLQUARK, MappingNotify, ParseTable, (Opaque)mappingNotify},

#ifdef DEBUG
# ifdef notdef
{"Timer",	    NULLQUARK, _XtTimerEventType,ParseNone,	NULL},
{"EventTimer",	    NULLQUARK, _XtEventTimerEventType,ParseNone,NULL},
# endif /* notdef */
#endif /* DEBUG */

/* Event Name,	  Quark, Event Type,	Detail Parser, Closure */

};

#define IsNewline(str) ((str) == '\n')

#define ScanFor(str, ch) \
    while ((*(str) != (ch)) && (*(str) != '\0') && !IsNewline(*(str))) (str)++

#define ScanNumeric(str)  while ('0' <= *(str) && *(str) <= '9') (str)++

#define ScanAlphanumeric(str) \
    while (('A' <= *(str) && *(str) <= 'Z') || \
           ('a' <= *(str) && *(str) <= 'z') || \
           ('0' <= *(str) && *(str) <= '9')) (str)++

#define ScanWhitespace(str) \
    while (*(str) == ' ' || *(str) == '\t') (str)++

static Boolean initialized = FALSE;
static XrmQuark QMeta;
static XrmQuark QCtrl;
static XrmQuark QNone;
static XrmQuark QAny;

static void FreeEventSeq(
    EventSeqPtr eventSeq)
{
    register EventSeqPtr evs = eventSeq;

    while (evs != NULL) {
	evs->state = (StatePtr) evs;
	if (evs->next != NULL
	    && evs->next->state == (StatePtr) evs->next)
	    evs->next = NULL;
	evs = evs->next;
    }

    evs = eventSeq;
    while (evs != NULL) {
	register EventPtr event = evs;
	evs = evs->next;
	if (evs == event) evs = NULL;
	XtFree((char *)event);
    }
}

static void CompileNameValueTable(
    NameValueTable table)
{
    register int i;

    for (i=0; table[i].name; i++)
        table[i].signature = XrmPermStringToQuark(table[i].name);
}

static int OrderEvents(_Xconst void *a, _Xconst void *b)
{
    return ((((_Xconst EventKey *)a)->signature <
	     ((_Xconst EventKey *)b)->signature) ? -1 : 1);
}

static void Compile_XtEventTable(
    EventKeys	table,
    Cardinal	count)
{
    register int i;
    register EventKeys entry = table;

    for (i=count; --i >= 0; entry++)
	entry->signature = XrmPermStringToQuark(entry->event);
    qsort(table, count, sizeof(EventKey), OrderEvents);
}

static int OrderModifiers(_Xconst void *a, _Xconst void *b)
{
    return ((((_Xconst ModifierRec *)a)->signature <
	     ((_Xconst ModifierRec *)b)->signature) ? -1 : 1);
}

static void Compile_XtModifierTable(
    ModifierKeys table,
    Cardinal count)
{
    register int i;
    register ModifierKeys entry = table;

    for (i=count; --i >= 0; entry++)
	entry->signature = XrmPermStringToQuark(entry->name);
    qsort(table, count, sizeof(ModifierRec), OrderModifiers);
}

static String PanicModeRecovery(
    String str)
{
     ScanFor(str,'\n');
     if (*str == '\n') str++;
     return str;

}


static void Syntax(
    String str0,String str1)
{
    Cardinal num_params = 2;
    String params[2];

    params[0] = str0;
    params[1] = str1;
    XtWarningMsg(XtNtranslationParseError,"parseError",XtCXtToolkitError,
		 "translation table syntax error: %s %s",params,&num_params);
}



static Cardinal LookupTMEventType(
  String eventStr,
  Boolean *error)
{
    register int   i = 0, left, right;
    register XrmQuark	signature;
    static int 	previous = 0;

    LOCK_PROCESS;
    if ((signature = StringToQuark(eventStr)) == events[previous].signature) {
	UNLOCK_PROCESS;
	return (Cardinal) previous;
    }

    left = 0;
    right = XtNumber(events) - 1;
    while (left <= right) {
	i = (left + right) >> 1;
	if (signature < events[i].signature)
	    right = i - 1;
	else if (signature > events[i].signature)
	    left = i + 1;
	else {
	    previous = i;
	    UNLOCK_PROCESS;
	    return (Cardinal) i;
	}
    }

    Syntax("Unknown event type :  ",eventStr);
    *error = TRUE;
    UNLOCK_PROCESS;
    return (Cardinal) i;
}

static void StoreLateBindings(
    KeySym  keysymL,
    Boolean notL,
    KeySym keysymR,
    Boolean notR,
    LateBindingsPtr* lateBindings)
{
    LateBindingsPtr temp;
    Boolean pair = FALSE;
    unsigned long count,number;
    if (lateBindings != NULL){
        temp = *lateBindings;
        if (temp != NULL) {
            for (count = 0; temp[count].keysym; count++){/*EMPTY*/}
        }
        else count = 0;
        if (! keysymR){
             number = 1;pair = FALSE;
        } else{
             number = 2;pair = TRUE;
        }

        temp = (LateBindingsPtr)XtRealloc((char *)temp,
            (unsigned)((count+number+1) * sizeof(LateBindings)) );
        *lateBindings = temp;
        temp[count].knot = notL;
        temp[count].pair = pair;
	if (count == 0)
	    temp[count].ref_count = 1;
        temp[count++].keysym = keysymL;
        if (keysymR){
            temp[count].knot = notR;
            temp[count].pair = FALSE;
	    temp[count].ref_count = 0;
            temp[count++].keysym = keysymR;
        }
        temp[count].knot = temp[count].pair = FALSE;
        temp[count].ref_count = 0;
        temp[count].keysym = 0;
    }
}

static void _XtParseKeysymMod(
    String name,
    LateBindingsPtr* lateBindings,
    Boolean notFlag,
    Value *valueP,
    Boolean *error)
{
    KeySym keySym;
    keySym = StringToKeySym(name, error);
    *valueP = 0;
    if (keySym != NoSymbol) {
        StoreLateBindings(keySym,notFlag,(KeySym) NULL,FALSE,lateBindings);
    }
}

static Boolean _XtLookupModifier(
    XrmQuark signature,
    LateBindingsPtr* lateBindings,
    Boolean notFlag,
    Value *valueP,
    Bool constMask)
{
    register int i, left, right;
    static int previous = 0;

    LOCK_PROCESS;
    if (signature == modifiers[previous].signature) {
	if (constMask)  *valueP = modifiers[previous].value;
	else /* if (modifiers[previous].modifierParseProc) always true */
	   (*modifiers[previous].modifierParseProc)
	      (modifiers[previous].value, lateBindings, notFlag, valueP);
	UNLOCK_PROCESS;
	return TRUE;
    }

    left = 0;
    right = XtNumber(modifiers) - 1;
    while (left <= right) {
	i = (left + right) >> 1;
	if (signature < modifiers[i].signature)
	    right = i - 1;
	else if (signature > modifiers[i].signature)
	    left = i + 1;
	else {
	    previous = i;
	    if (constMask)  *valueP = modifiers[i].value;
	    else /* if (modifiers[i].modifierParseProc) always true */
		(*modifiers[i].modifierParseProc)
		    (modifiers[i].value, lateBindings, notFlag, valueP);
	    UNLOCK_PROCESS;
	    return TRUE;
	}
    }
    UNLOCK_PROCESS;
    return FALSE;
}


static String ScanIdent(
    register String str)
{
    ScanAlphanumeric(str);
    while (
	   ('A' <= *str && *str <= 'Z')
	|| ('a' <= *str && *str <= 'z')
	|| ('0' <= *str && *str <= '9')
	|| (*str == '-')
	|| (*str == '_')
	|| (*str == '$')
	) str++;
    return str;
}

static String FetchModifierToken(
    String str,
    XrmQuark *token_return)
{
    String start = str;

    if (*str == '$') {
        *token_return = QMeta;
        str++;
        return str;
    }
    if (*str == '^') {
        *token_return = QCtrl;
        str++;
        return str;
    }
    str = ScanIdent(str);
    if (start != str) {
	char modStrbuf[100];
	char* modStr;

	modStr = XtStackAlloc ((size_t)(str - start + 1), modStrbuf);
	if (modStr == NULL) _XtAllocError (NULL);
	(void) memmove(modStr, start, str-start);
	modStr[str-start] = '\0';
	*token_return = XrmStringToQuark(modStr);
	XtStackFree (modStr, modStrbuf);
	return str;
    }
    return str;
}

static String ParseModifiers(
    register String str,
    EventPtr event,
    Boolean* error)
{
    register String start;
    Boolean notFlag, exclusive, keysymAsMod;
    Value maskBit;
    XrmQuark Qmod;

    ScanWhitespace(str);
    start = str;
    str = FetchModifierToken(str, &Qmod);
    exclusive = FALSE;
    if (start != str) {
	if (Qmod == QNone) {
	    event->event.modifierMask = ~0;
	    event->event.modifiers = 0;
	    ScanWhitespace(str);
	    return str;
	} else if (Qmod == QAny) { /*backward compatability*/
	    event->event.modifierMask = 0;
	    event->event.modifiers = AnyModifier;
	    ScanWhitespace(str);
	    return str;
	}
	str = start; /*if plain modifier, reset to beginning */
    }
    else while (*str == '!' || *str == ':') {
        if (*str == '!') {
             exclusive = TRUE;
             str++;
             ScanWhitespace(str);
        }
        if (*str == ':') {
             event->event.standard = TRUE;
             str++;
             ScanWhitespace(str);
        }
    }

    while (*str != '<') {
        if (*str == '~') {
             notFlag = TRUE;
             str++;
          } else
              notFlag = FALSE;
        if (*str == '@') {
            keysymAsMod = TRUE;
            str++;
        }
        else keysymAsMod = FALSE;
	start = str;
        str = FetchModifierToken(str, &Qmod);
        if (start == str) {
            Syntax("Modifier or '<' expected","");
            *error = TRUE;
            return PanicModeRecovery(str);
        }
         if (keysymAsMod) {
             _XtParseKeysymMod(XrmQuarkToString(Qmod),
			       &event->event.lateModifiers,
			       notFlag,&maskBit, error);
	     if (*error)
                 return PanicModeRecovery(str);

         } else
  	     if (!_XtLookupModifier(Qmod, &event->event.lateModifiers,
				    notFlag, &maskBit, FALSE)) {
	         Syntax("Unknown modifier name:  ", XrmQuarkToString(Qmod));
                 *error = TRUE;
                 return PanicModeRecovery(str);
             }
        event->event.modifierMask |= maskBit;
	if (notFlag) event->event.modifiers &= ~maskBit;
	else event->event.modifiers |= maskBit;
        ScanWhitespace(str);
    }
    if (exclusive) event->event.modifierMask = ~0;
    return str;
}

static String ParseXtEventType(
    register String str,
    EventPtr event,
    Cardinal *tmEventP,
    Boolean* error)
{
    String start = str;
    char eventTypeStrbuf[100];
    char* eventTypeStr;

    ScanAlphanumeric(str);
    eventTypeStr = XtStackAlloc ((size_t)(str - start + 1), eventTypeStrbuf);
    if (eventTypeStr == NULL) _XtAllocError (NULL);
    (void) memmove(eventTypeStr, start, str-start);
    eventTypeStr[str-start] = '\0';
    *tmEventP = LookupTMEventType(eventTypeStr,error);
    XtStackFree (eventTypeStr, eventTypeStrbuf);
    if (*error)
        return PanicModeRecovery(str);
    event->event.eventType = events[*tmEventP].eventType;
    return str;
}

static unsigned long StrToHex(
    String str)
{
    register char   c;
    register unsigned long    val = 0;

    while ((c = *str)) {
	if ('0' <= c && c <= '9') val = val*16+c-'0';
	else if ('a' <= c && c <= 'z') val = val*16+c-'a'+10;
	else if ('A' <= c && c <= 'Z') val = val*16+c-'A'+10;
	else return 0;
	str++;
    }

    return val;
}

static unsigned long StrToOct(
    String str)
{
    register char c;
    register unsigned long  val = 0;

    while ((c = *str)) {
        if ('0' <= c && c <= '7') val = val*8+c-'0'; else return 0;
	str++;
    }

    return val;
}

static unsigned long StrToNum(
    String str)
{
    register char c;
    register unsigned long val = 0;

    if (*str == '0') {
	str++;
	if (*str == 'x' || *str == 'X') return StrToHex(++str);
	else return StrToOct(str);
    }

    while ((c = *str)) {
	if ('0' <= c && c <= '9') val = val*10+c-'0';
	else return 0;
	str++;
    }

    return val;
}

static KeySym StringToKeySym(
    String str,
    Boolean *error)
{
    KeySym k;

    if (str == NULL || *str == '\0') return (KeySym) 0;

#ifndef NOTASCII
    /* special case single character ASCII, for speed */
    if (*(str+1) == '\0') {
	if (' ' <= *str && *str <= '~') return XK_space + (*str - ' ');
    }
#endif

    if ('0' <= *str && *str <= '9') return (KeySym) StrToNum(str);
    k = XStringToKeysym(str);
    if (k != NoSymbol) return k;

#ifdef NOTASCII
    /* fall-back case to preserve backwards compatibility; no-one
     * should be relying upon this!
     */
    if (*(str+1) == '\0') return (KeySym) *str;
#endif

    Syntax("Unknown keysym name: ", str);
    *error = TRUE;
    return NoSymbol;
}
/* ARGSUSED */
static void ParseModImmed(
    Value value,
    LateBindingsPtr* lateBindings,
    Boolean notFlag,
    Value* valueP)
{
    *valueP = value;
}

 /* is only valid with keysyms that have an _L and _R in their name;
  * and ignores keysym lookup errors (i.e. assumes only valid keysyms)
  */
static void ParseModSym(
    Value value,
    LateBindingsPtr* lateBindings,
    Boolean notFlag,
    Value* valueP)
{
    register KeySym keysymL = (KeySym)value;
    register KeySym keysymR = keysymL + 1; /* valid for supported keysyms */
    StoreLateBindings(keysymL,notFlag,keysymR,notFlag,lateBindings);
    *valueP = 0;
}

#ifdef sparc
/*
 * The stupid optimizer in SunOS 4.0.3 and below generates bogus code that
 * causes the value of the most recently used variable to be returned instead
 * of the value passed in.
 */
static String stupid_optimizer_kludge;
#define BROKEN_OPTIMIZER_HACK(val) stupid_optimizer_kludge = (val)
#else
#define BROKEN_OPTIMIZER_HACK(val) val
#endif

/* ARGSUSED */
static String ParseImmed(
    register String str,
    register Opaque closure,
    register EventPtr event,
    Boolean* error)
{
    event->event.eventCode = (unsigned long)closure;
    event->event.eventCodeMask = ~0UL;

    return BROKEN_OPTIMIZER_HACK(str);
}

/* ARGSUSED */
static String ParseAddModifier(
    register String str,
    register Opaque closure,
    register EventPtr event,
    Boolean* error)
{
    register unsigned long modval = (unsigned long)closure;
    event->event.modifiers |= modval;
    if (modval != AnyButtonMask) /* AnyButtonMask is don't-care mask */
	event->event.modifierMask |= modval;

    return BROKEN_OPTIMIZER_HACK(str);
}


static String ParseKeyAndModifiers(
    String str,
    Opaque closure,
    EventPtr event,
    Boolean* error)
{
    str = ParseKeySym(str, closure, event,error);
    if ((unsigned long) closure == 0) {
	Value metaMask; /* unused */
	(void) _XtLookupModifier(QMeta, &event->event.lateModifiers, FALSE,
				 &metaMask, FALSE);
    } else {
	event->event.modifiers |= (unsigned long) closure;
	event->event.modifierMask |= (unsigned long) closure;
    }
    return str;
}

/*ARGSUSED*/
static String ParseKeySym(
    register String str,
    Opaque closure,
    EventPtr event,
    Boolean* error)
{
    char *start;
    char keySymNamebuf[100];
    char* keySymName;

    ScanWhitespace(str);

    if (*str == '\\') {
	keySymName = keySymNamebuf;
	str++;
	keySymName[0] = *str;
	if (*str != '\0' && !IsNewline(*str)) str++;
	keySymName[1] = '\0';
	event->event.eventCode = StringToKeySym(keySymName, error);
	event->event.eventCodeMask = ~0L;
    } else if (*str == ',' || *str == ':' ||
             /* allow leftparen to be single char symbol,
              * for backwards compatibility
              */
             (*str == '(' && *(str+1) >= '0' && *(str+1) <= '9')) {
	keySymName = keySymNamebuf; /* just so we can stackfree it later */
	/* no detail */
	event->event.eventCode = 0L;
        event->event.eventCodeMask = 0L;
    } else {
	start = str;
	while (
		*str != ','
		&& *str != ':'
		&& *str != ' '
		&& *str != '\t'
                && !IsNewline(*str)
                && (*str != '(' || *(str+1) <= '0' || *(str+1) >= '9')
		&& *str != '\0') str++;
	keySymName = XtStackAlloc ((size_t)(str - start + 1), keySymNamebuf);
	(void) memmove(keySymName, start, str-start);
	keySymName[str-start] = '\0';
	event->event.eventCode = StringToKeySym(keySymName, error);
	event->event.eventCodeMask = ~0L;
    }
    if (*error) {
	/* We never get here when keySymName hasn't been allocated */
	if (keySymName[0] == '<') {
	    /* special case for common error */
	    XtWarningMsg(XtNtranslationParseError, "missingComma",
			 XtCXtToolkitError,
		     "... possibly due to missing ',' in event sequence.",
		     (String*)NULL, (Cardinal*)NULL);
	}
	XtStackFree (keySymName, keySymNamebuf);
	return PanicModeRecovery(str);
    }
    if (event->event.standard)
	event->event.matchEvent = _XtMatchUsingStandardMods;
    else
	event->event.matchEvent = _XtMatchUsingDontCareMods;

    XtStackFree (keySymName, keySymNamebuf);

    return str;
}

static String ParseTable(
    register String str,
    Opaque closure,
    EventPtr event,
    Boolean* error)
{
    register String start = str;
    register XrmQuark signature;
    NameValueTable table = (NameValueTable) closure;
    char tableSymName[100];

    event->event.eventCode = 0L;
    ScanAlphanumeric(str);
    if (str == start) {event->event.eventCodeMask = 0L; return str; }
    if (str-start >= 99) {
	Syntax("Invalid Detail Type (string is too long).", "");
	*error = TRUE;
	return str;
    }
    (void) memmove(tableSymName, start, str-start);
    tableSymName[str-start] = '\0';
    signature = StringToQuark(tableSymName);
    for (; table->signature != NULLQUARK; table++)
	if (table->signature == signature) {
	    event->event.eventCode = table->value;
	    event->event.eventCodeMask = ~0L;
	    return str;
	}

    Syntax("Unknown Detail Type:  ", tableSymName);
    *error = TRUE;
    return PanicModeRecovery(str);
}

/*ARGSUSED*/
static String ParseNone(
    String str,
    Opaque closure,
    EventPtr event,
    Boolean* error)
{
    event->event.eventCode = 0;
    event->event.eventCodeMask = 0;

    return BROKEN_OPTIMIZER_HACK(str);
}

/*ARGSUSED*/
static String ParseAtom(
    String str,
    Opaque closure,
    EventPtr event,
    Boolean* error)
{
    ScanWhitespace(str);

    if (*str == ',' || *str == ':') {
	/* no detail */
	event->event.eventCode = 0L;
        event->event.eventCodeMask = 0L;
    } else {
	char *start, atomName[1000];
	start = str;
	while (
		*str != ','
		&& *str != ':'
		&& *str != ' '
		&& *str != '\t'
                && !IsNewline(*str)
		&& *str != '\0') str++;
	if (str-start >= 999) {
	    Syntax( "Atom name must be less than 1000 characters long.", "" );
	    *error = TRUE;
	    return str;
	}
	(void) memmove(atomName, start, str-start);
	atomName[str-start] = '\0';
	event->event.eventCode = XrmStringToQuark(atomName);
	event->event.matchEvent = _XtMatchAtom;
    }
    return str;
}

static ModifierMask buttonModifierMasks[] = {
    0, Button1Mask, Button2Mask, Button3Mask, Button4Mask, Button5Mask
};
static String ParseRepeat(String, int *, Boolean *, Boolean *);

static String ParseEvent(
    register String str,
    EventPtr	event,
    int*	reps,
    Boolean*	plus,
    Boolean* error)
{
    Cardinal	tmEvent;

    str = ParseModifiers(str, event,error);
    if (*error) return str;
    if (*str != '<') {
         Syntax("Missing '<' while parsing event type.","");
         *error = TRUE;
         return PanicModeRecovery(str);
    }
    else str++;
    str = ParseXtEventType(str, event, &tmEvent,error);
    if (*error) return str;
    if (*str != '>'){
         Syntax("Missing '>' while parsing event type","");
         *error = TRUE;
         return PanicModeRecovery(str);
    }
    else str++;
    if (*str == '(') {
	str = ParseRepeat(str, reps, plus, error);
	if (*error) return str;
    }
    str = (*(events[tmEvent].parseDetail))(
        str, events[tmEvent].closure, event,error);
    if (*error) return str;

/* gross hack! ||| this kludge is related to the X11 protocol deficiency w.r.t.
 * modifiers in grabs.
 */
    if ((event->event.eventType == ButtonRelease)
	&& (event->event.modifiers | event->event.modifierMask) /* any */
        && (event->event.modifiers != AnyModifier))
    {
	event->event.modifiers
	    |= buttonModifierMasks[event->event.eventCode];
	/* the button that is going up will always be in the modifiers... */
    }

    return str;
}

static String ParseQuotedStringEvent(
    register String str,
    register EventPtr event,
    Boolean *error)
{
    Value metaMask;
    char	s[2];

    if (*str=='^') {
	str++;
	event->event.modifiers = ControlMask;
    } else if (*str == '$') {
	str++;
	(void) _XtLookupModifier(QMeta, &event->event.lateModifiers, FALSE,
				 &metaMask, FALSE);
    }
    if (*str == '\\')
	str++;
    s[0] = *str;
    s[1] = '\0';
    if (*str != '\0' && !IsNewline(*str)) str++;
    event->event.eventType = KeyPress;
    event->event.eventCode = StringToKeySym(s, error);
    if (*error) return PanicModeRecovery(str);
    event->event.eventCodeMask = ~0L;
    event->event.matchEvent = _XtMatchUsingStandardMods;
    event->event.standard = TRUE;

    return str;
}


static EventSeqRec timerEventRec = {
    {0, 0, NULL, _XtEventTimerEventType, 0L, 0L, NULL, False},
    /* (StatePtr) -1 */ NULL,
    NULL,
    NULL
};

static void RepeatDown(
    EventPtr *eventP,
    int reps,
    ActionPtr **actionsP)
{
    EventRec upEventRec;
    register EventPtr event, downEvent;
    EventPtr upEvent = &upEventRec;
    register int i;

    downEvent = event = *eventP;
    *upEvent = *downEvent;
    upEvent->event.eventType = ((event->event.eventType == ButtonPress) ?
	ButtonRelease : KeyRelease);
    if ((upEvent->event.eventType == ButtonRelease)
	&& (upEvent->event.modifiers != AnyModifier)
        && (upEvent->event.modifiers | upEvent->event.modifierMask))
	upEvent->event.modifiers
	    |= buttonModifierMasks[event->event.eventCode];

    if (event->event.lateModifiers)
	event->event.lateModifiers->ref_count += (reps - 1) * 2;

    for (i=1; i<reps; i++) {

	/* up */
	event->next = XtNew(EventSeqRec);
	event = event->next;
	*event = *upEvent;

	/* timer */
	event->next = XtNew(EventSeqRec);
	event = event->next;
	*event = timerEventRec;

	/* down */
	event->next = XtNew(EventSeqRec);
	event = event->next;
	*event = *downEvent;

    }

    event->next = NULL;
    *eventP = event;
    *actionsP = &event->actions;
}

static void RepeatDownPlus(
    EventPtr *eventP,
    int reps,
    ActionPtr **actionsP)
{
    EventRec upEventRec;
    register EventPtr event, downEvent, lastDownEvent = NULL;
    EventPtr upEvent = &upEventRec;
    register int i;

    downEvent = event = *eventP;
    *upEvent = *downEvent;
    upEvent->event.eventType = ((event->event.eventType == ButtonPress) ?
	ButtonRelease : KeyRelease);
    if ((upEvent->event.eventType == ButtonRelease)
	&& (upEvent->event.modifiers != AnyModifier)
        && (upEvent->event.modifiers | upEvent->event.modifierMask))
	upEvent->event.modifiers
	    |= buttonModifierMasks[event->event.eventCode];

    if (event->event.lateModifiers)
	event->event.lateModifiers->ref_count += reps * 2 - 1;

    for (i=0; i<reps; i++) {

	if (i > 0) {
	/* down */
	event->next = XtNew(EventSeqRec);
	event = event->next;
	*event = *downEvent;
	}
	lastDownEvent = event;

	/* up */
	event->next = XtNew(EventSeqRec);
	event = event->next;
	*event = *upEvent;

	/* timer */
	event->next = XtNew(EventSeqRec);
	event = event->next;
	*event = timerEventRec;

    }

    event->next = lastDownEvent;
    *eventP = event;
    *actionsP = &lastDownEvent->actions;
}

static void RepeatUp(
    EventPtr *eventP,
    int reps,
    ActionPtr **actionsP)
{
    EventRec upEventRec;
    register EventPtr event, downEvent;
    EventPtr upEvent = &upEventRec;
    register int i;

    /* the event currently sitting in *eventP is an "up" event */
    /* we want to make it a "down" event followed by an "up" event, */
    /* so that sequence matching on the "state" side works correctly. */

    downEvent = event = *eventP;
    *upEvent = *downEvent;
    downEvent->event.eventType = ((event->event.eventType == ButtonRelease) ?
	ButtonPress : KeyPress);
    if ((downEvent->event.eventType == ButtonPress)
	&& (downEvent->event.modifiers != AnyModifier)
        && (downEvent->event.modifiers | downEvent->event.modifierMask))
	downEvent->event.modifiers
	    &= ~buttonModifierMasks[event->event.eventCode];

    if (event->event.lateModifiers)
	event->event.lateModifiers->ref_count += reps * 2 - 1;

    /* up */
    event->next = XtNew(EventSeqRec);
    event = event->next;
    *event = *upEvent;

    for (i=1; i<reps; i++) {

	/* timer */
	event->next = XtNew(EventSeqRec);
	event = event->next;
	*event = timerEventRec;

	/* down */
	event->next = XtNew(EventSeqRec);
	event = event->next;
	*event = *downEvent;

	/* up */
	event->next = XtNew(EventSeqRec);
	event = event->next;
	*event = *upEvent;

	}

    event->next = NULL;
    *eventP = event;
    *actionsP = &event->actions;
}

static void RepeatUpPlus(
    EventPtr *eventP,
    int reps,
    ActionPtr **actionsP)
{
    EventRec upEventRec;
    register EventPtr event, downEvent, lastUpEvent = NULL;
    EventPtr upEvent = &upEventRec;
    register int i;

    /* the event currently sitting in *eventP is an "up" event */
    /* we want to make it a "down" event followed by an "up" event, */
    /* so that sequence matching on the "state" side works correctly. */

    downEvent = event = *eventP;
    *upEvent = *downEvent;
    downEvent->event.eventType = ((event->event.eventType == ButtonRelease) ?
	ButtonPress : KeyPress);
    if ((downEvent->event.eventType == ButtonPress)
	&& (downEvent->event.modifiers != AnyModifier)
        && (downEvent->event.modifiers | downEvent->event.modifierMask))
	downEvent->event.modifiers
	    &= ~buttonModifierMasks[event->event.eventCode];

    if (event->event.lateModifiers)
	event->event.lateModifiers->ref_count += reps * 2;

    for (i=0; i<reps; i++) {

	/* up */
	event->next = XtNew(EventSeqRec);
	lastUpEvent = event = event->next;
	*event = *upEvent;

	/* timer */
	event->next = XtNew(EventSeqRec);
	event = event->next;
	*event = timerEventRec;

	/* down */
	event->next = XtNew(EventSeqRec);
        event = event->next;
	*event = *downEvent;

	}

    event->next = lastUpEvent;
    *eventP = event;
    *actionsP = &lastUpEvent->actions;
}

static void RepeatOther(
    EventPtr *eventP,
    int reps,
    ActionPtr **actionsP)
{
    register EventPtr event, tempEvent;
    register int i;

    tempEvent = event = *eventP;

    if (event->event.lateModifiers)
	event->event.lateModifiers->ref_count += reps - 1;

    for (i=1; i<reps; i++) {
	event->next = XtNew(EventSeqRec);
	event = event->next;
	*event = *tempEvent;
    }

    *eventP = event;
    *actionsP = &event->actions;
}

static void RepeatOtherPlus(
    EventPtr *eventP,
    int reps,
    ActionPtr **actionsP)
{
    register EventPtr event, tempEvent;
    register int i;

    tempEvent = event = *eventP;

    if (event->event.lateModifiers)
	event->event.lateModifiers->ref_count += reps - 1;

    for (i=1; i<reps; i++) {
	event->next = XtNew(EventSeqRec);
	event = event->next;
	*event = *tempEvent;
    }

    event->next = event;
    *eventP = event;
    *actionsP = &event->actions;
}

static void RepeatEvent(
    EventPtr *eventP,
    int reps,
    Boolean plus,
    ActionPtr **actionsP)
{
    switch ((*eventP)->event.eventType) {

	case ButtonPress:
	case KeyPress:
	    if (plus) RepeatDownPlus(eventP, reps, actionsP);
	    else RepeatDown(eventP, reps, actionsP);
	    break;

	case ButtonRelease:
	case KeyRelease:
	    if (plus) RepeatUpPlus(eventP, reps, actionsP);
	    else RepeatUp(eventP, reps, actionsP);
	    break;

	default:
	    if (plus) RepeatOtherPlus(eventP, reps, actionsP);
	    else RepeatOther(eventP, reps, actionsP);
    }
}

static String ParseRepeat(
    register String str,
    int	*reps,
    Boolean *plus, Boolean *error)
{

    /*** Parse the repetitions, for double click etc... ***/
    if (*str != '(' || !(isdigit((unsigned char)str[1]) || str[1] == '+' || str[1] == ')'))
	return str;
    str++;
    if (isdigit((unsigned char)*str)) {
	String start = str;
	char repStr[7];
	size_t len;

	ScanNumeric(str);
	len = (str - start);
	if (len < sizeof repStr) {
	    (void) memmove(repStr, start, len);
	    repStr[len] = '\0';
	    *reps = StrToNum(repStr);
	} else {
	    Syntax("Repeat count too large.", "");
	    *error = TRUE;
	    return str;
	}
    }
    if (*reps == 0) {
	Syntax("Missing repeat count.","");
	*error = True;
	return str;
    }

    if (*str == '+') {
	*plus = TRUE;
	str++;
    }
    if (*str == ')')
	str++;
    else {
	Syntax("Missing ')'.","");
	*error = TRUE;
    }

    return str;
}

/***********************************************************************
 * ParseEventSeq
 * Parses the left hand side of a translation table production
 * up to, and consuming the ":".
 * Takes a pointer to a char* (where to start parsing) and returns an
 * event seq (in a passed in variable), having updated the String
 **********************************************************************/

static String ParseEventSeq(
    register String str,
    EventSeqPtr *eventSeqP,
    ActionPtr	**actionsP,
    Boolean *error)
{
    EventSeqPtr *nextEvent = eventSeqP;

    *eventSeqP = NULL;

    while ( *str != '\0' && !IsNewline(*str)) {
	static Event	nullEvent =
             {0, 0,NULL, 0, 0L, 0L,_XtRegularMatch,FALSE};
	EventPtr	event;

	ScanWhitespace(str);

	if (*str == '"') {
	    str++;
	    while (*str != '"' && *str != '\0' && !IsNewline(*str)) {
                event = XtNew(EventRec);
                event->event = nullEvent;
                event->state = /* (StatePtr) -1 */ NULL;
                event->next = NULL;
                event->actions = NULL;
		str = ParseQuotedStringEvent(str, event,error);
		if (*error) {
		    XtWarningMsg(XtNtranslationParseError, "nonLatin1",
			XtCXtToolkitError,
			"... probably due to non-Latin1 character in quoted string",
			(String*)NULL, (Cardinal*)NULL);
		    return PanicModeRecovery(str);
		}
		*nextEvent = event;
		*actionsP = &event->actions;
		nextEvent = &event->next;
	    }
	    if (*str != '"') {
                Syntax("Missing '\"'.","");
                *error = TRUE;
                return PanicModeRecovery(str);
             }
             else str++;
	} else {
	    int reps = 0;
	    Boolean plus = FALSE;

            event = XtNew(EventRec);
            event->event = nullEvent;
            event->state = /* (StatePtr) -1 */ NULL;
            event->next = NULL;
            event->actions = NULL;

	    str = ParseEvent(str, event, &reps, &plus, error);
            if (*error) return str;
	    *nextEvent = event;
	    *actionsP = &event->actions;
	    if (reps > 1 || plus)
		RepeatEvent(&event, reps, plus, actionsP);
	    nextEvent = &event->next;
	}
	ScanWhitespace(str);
        if (*str == ':') break;
        else {
            if (*str != ',') {
                Syntax("',' or ':' expected while parsing event sequence.","");
                *error = TRUE;
                return PanicModeRecovery(str);
	    } else str++;
        }
    }

    if (*str != ':') {
        Syntax("Missing ':'after event sequence.","");
        *error = TRUE;
        return PanicModeRecovery(str);
    } else str++;

    return str;
}


static String ParseActionProc(
    register String str,
    XrmQuark *actionProcNameP,
    Boolean *error)
{
    register String start = str;
    char procName[200];

    str = ScanIdent(str);
    if (str-start >= 199) {
	Syntax("Action procedure name is longer than 199 chars","");
	*error = TRUE;
	return str;
    }
    (void) memmove(procName, start, str-start);
    procName[str-start] = '\0';
    *actionProcNameP = XrmStringToQuark( procName );
    return str;
}


static String ParseString(
    register String str,
    String *strP)
{
    register String start;

    if (*str == '"') {
	register unsigned prev_len, len;
	str++;
	start = str;
	*strP = NULL;
	prev_len = 0;

	while (*str != '"' && *str != '\0') {
	    /* \"  produces double quote embedded in a quoted parameter
	     * \\" produces backslash as last character of a quoted parameter
	     */
	    if (*str == '\\' &&
		(*(str+1) == '"' || (*(str+1) == '\\' && *(str+2) == '"'))) {
		len = prev_len + (str-start+2);
		*strP = XtRealloc(*strP, len);
		(void) memmove(*strP + prev_len, start, str-start);
		prev_len = len-1;
		str++;
		(*strP)[prev_len-1] = *str;
		(*strP)[prev_len] = '\0';
		start = str+1;
	    }
	    str++;
	}
	len = prev_len + (str-start+1);
	*strP = XtRealloc(*strP, len);
	(void) memmove( *strP + prev_len, start, str-start);
	(*strP)[len-1] = '\0';
	if (*str == '"') str++; else
            XtWarningMsg(XtNtranslationParseError,"parseString",
                      XtCXtToolkitError,"Missing '\"'.",
		      (String *)NULL, (Cardinal *)NULL);
    } else {
	/* scan non-quoted string, stop on whitespace, ',' or ')' */
	start = str;
	while (*str != ' '
		&& *str != '\t'
		&& *str != ','
		&& *str != ')'
                && !IsNewline(*str)
		&& *str != '\0') str++;
	*strP = __XtMalloc((unsigned)(str-start+1));
	(void) memmove(*strP, start, str-start);
	(*strP)[str-start] = '\0';
    }
    return str;
}


static String ParseParamSeq(
    register String str,
    String **paramSeqP,
    Cardinal *paramNumP)
{
    typedef struct _ParamRec *ParamPtr;
    typedef struct _ParamRec {
	ParamPtr next;
	String	param;
    } ParamRec;

    ParamPtr params = NULL;
    register Cardinal num_params = 0;
    register Cardinal i;

    ScanWhitespace(str);
    while (*str != ')' && *str != '\0' && !IsNewline(*str)) {
	String newStr;
	str = ParseString(str, &newStr);
	if (newStr != NULL) {
	    ParamPtr temp = (ParamRec*)
		ALLOCATE_LOCAL( (unsigned)sizeof(ParamRec) );
	    if (temp == NULL) _XtAllocError (NULL);

	    num_params++;
	    temp->next = params;
	    params = temp;
	    temp->param = newStr;
	    ScanWhitespace(str);
	    if (*str == ',') {
		str++;
		ScanWhitespace(str);
	    }
	}
    }

    if (num_params != 0) {
	String *paramP = (String *)
		__XtMalloc( (unsigned)(num_params+1) * sizeof(String) );
	*paramSeqP = paramP;
	*paramNumP = num_params;
	paramP += num_params; /* list is LIFO right now */
	*paramP-- = NULL;
	for (i=0; i < num_params; i++) {
	    ParamPtr next = params->next;
	    *paramP-- = params->param;
	    DEALLOCATE_LOCAL( (char *)params );
	    params = next;
	}
    } else {
	*paramSeqP = NULL;
	*paramNumP = 0;
    }

    return str;
}

static String ParseAction(
    String str,
    ActionPtr actionP,
    XrmQuark* quarkP,
    Boolean* error)
{
    str = ParseActionProc(str, quarkP, error);
    if (*error) return str;

    if (*str == '(') {
	str++;
	str = ParseParamSeq(str, &actionP->params, &actionP->num_params);
    } else {
        Syntax("Missing '(' while parsing action sequence","");
        *error = TRUE;
        return str;
    }
    if (*str == ')') str++;
    else{
        Syntax("Missing ')' while parsing action sequence","");
        *error = TRUE;
        return str;
    }
    return str;
}


static String ParseActionSeq(
    TMParseStateTree   	parseTree,
    String 		str,
    ActionPtr 		*actionsP,
    Boolean		*error)
{
    ActionPtr *nextActionP = actionsP;

    *actionsP = NULL;
    while (*str != '\0' && !IsNewline(*str)) {
	register ActionPtr	action;
	XrmQuark quark;

	action = XtNew(ActionRec);
        action->params = NULL;
        action->num_params = 0;
        action->next = NULL;

	str = ParseAction(str, action, &quark, error);
	if (*error) return PanicModeRecovery(str);

	action->idx = _XtGetQuarkIndex(parseTree, quark);
	ScanWhitespace(str);
	*nextActionP = action;
	nextActionP = &action->next;
    }
    if (IsNewline(*str)) str++;
    ScanWhitespace(str);
    return str;
}


static void ShowProduction(
  String currentProduction)
{
    Cardinal num_params = 1;
    String params[1];
    size_t len;
    char *eol, *production, productionbuf[500];

    eol = strchr(currentProduction, '\n');
    if (eol) len = eol - currentProduction;
    else len = strlen (currentProduction);
    production = XtStackAlloc (len + 1, productionbuf);
    if (production == NULL) _XtAllocError (NULL);
    (void) memmove(production, currentProduction, len);
    production[len] = '\0';

    params[0] = production;
    XtWarningMsg(XtNtranslationParseError, "showLine", XtCXtToolkitError,
		 "... found while parsing '%s'", params, &num_params);

    XtStackFree (production, productionbuf);
}

/***********************************************************************
 * ParseTranslationTableProduction
 * Parses one line of event bindings.
 ***********************************************************************/

static String ParseTranslationTableProduction(
    TMParseStateTree	 parseTree,
    register String str,
    Boolean* error)
{
    EventSeqPtr	eventSeq = NULL;
    ActionPtr	*actionsP;
    String	production = str;

    actionsP = NULL;
    str = ParseEventSeq(str, &eventSeq, &actionsP,error);
    if (*error == TRUE) {
	ShowProduction(production);
        FreeEventSeq(eventSeq);
        return (str);
    }
    ScanWhitespace(str);
    str = ParseActionSeq(parseTree, str, actionsP, error);
    if (*error == TRUE) {
	ShowProduction(production);
        FreeEventSeq(eventSeq);
        return (str);
    }

    _XtAddEventSeqToStateTree(eventSeq, parseTree);
    FreeEventSeq(eventSeq);
    return (str);
}

static String CheckForPoundSign(
    String str,
    _XtTranslateOp defaultOp,
    _XtTranslateOp *actualOpRtn)
{
    String start;
    char operation[20];
    _XtTranslateOp opType;

    opType = defaultOp;
    ScanWhitespace(str);
    if (*str == '#') {
	int len;
	str++;
	start = str;
	str = ScanIdent(str);
	len = MIN(19, str-start);
	(void) memmove(operation, start, len);
	operation[len] = '\0';
	if (!strcmp(operation,"replace"))
	  opType = XtTableReplace;
	else if (!strcmp(operation,"augment"))
	  opType = XtTableAugment;
	else if (!strcmp(operation,"override"))
	  opType = XtTableOverride;
	ScanWhitespace(str);
	if (IsNewline(*str)) {
	    str++;
	    ScanWhitespace(str);
	}
    }
    *actualOpRtn = opType;
    return str;
}

static XtTranslations ParseTranslationTable(
    String 	source,
    Boolean	isAccelerator,
    _XtTranslateOp defaultOp,
    Boolean*	error)
{
    XtTranslations		xlations;
    TMStateTree			stateTrees[8];
    TMParseStateTreeRec		parseTreeRec, *parseTree = &parseTreeRec;
    XrmQuark			stackQuarks[200];
    TMBranchHeadRec		stackBranchHeads[200];
    StatePtr			stackComplexBranchHeads[200];
    _XtTranslateOp		actualOp;

    if (source == NULL)
      return (XtTranslations)NULL;

    source = CheckForPoundSign(source, defaultOp, &actualOp);
    if (isAccelerator && actualOp == XtTableReplace)
	actualOp = defaultOp;

    parseTree->isSimple = TRUE;
    parseTree->mappingNotifyInterest = FALSE;
    parseTree->isAccelerator = isAccelerator;
    parseTree->isStackBranchHeads =
      parseTree->isStackQuarks =
	parseTree->isStackComplexBranchHeads = TRUE;

    parseTree->numQuarks =
      parseTree->numBranchHeads =
	parseTree->numComplexBranchHeads = 0;

    parseTree->quarkTblSize =
      parseTree->branchHeadTblSize =
	parseTree->complexBranchHeadTblSize = 200;

    parseTree->quarkTbl = stackQuarks;
    parseTree->branchHeadTbl = stackBranchHeads;
    parseTree->complexBranchHeadTbl = stackComplexBranchHeads;

    while (source != NULL && *source != '\0') {
	source =  ParseTranslationTableProduction(parseTree, source, error);
	if (*error == TRUE) break;
    }
    stateTrees[0] = _XtParseTreeToStateTree(parseTree);

    if (!parseTree->isStackQuarks)
      XtFree((char *)parseTree->quarkTbl);
    if (!parseTree->isStackBranchHeads)
      XtFree((char *)parseTree->branchHeadTbl);
    if (!parseTree->isStackComplexBranchHeads)
      XtFree((char *)parseTree->complexBranchHeadTbl);

    xlations = _XtCreateXlations(stateTrees, 1, NULL, NULL);
    xlations->operation = actualOp;

#ifdef notdef
    XtFree(stateTrees);
#endif /* notdef */
    return xlations;
}

/*** public procedures ***/

/*ARGSUSED*/
Boolean XtCvtStringToAcceleratorTable(
    Display*	dpy,
    XrmValuePtr args,
    Cardinal    *num_args,
    XrmValuePtr from,
    XrmValuePtr to,
    XtPointer	*closure)
{
    String str;
    Boolean error = FALSE;

    if (*num_args != 0)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
	  "wrongParameters","cvtStringToAcceleratorTable",XtCXtToolkitError,
          "String to AcceleratorTable conversion needs no extra arguments",
	  (String *)NULL, (Cardinal *)NULL);
    str = (String)(from->addr);
    if (str == NULL) {
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
	  "badParameters","cvtStringToAcceleratorTable",XtCXtToolkitError,
          "String to AcceleratorTable conversion needs string",
	  (String *)NULL, (Cardinal *)NULL);
	return FALSE;
    }
    if (to->addr != NULL) {
	if (to->size < sizeof(XtAccelerators)) {
	    to->size = sizeof(XtAccelerators);
	    return FALSE;
	}
	*(XtAccelerators*)to->addr =
	    (XtAccelerators) ParseTranslationTable(str, TRUE, XtTableAugment, &error);
    }
    else {
	static XtAccelerators staticStateTable;
	staticStateTable =
	    (XtAccelerators) ParseTranslationTable(str, TRUE, XtTableAugment, &error);
	to->addr = (XPointer) &staticStateTable;
	to->size = sizeof(XtAccelerators);
    }
    if (error == TRUE)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
	  "parseError","cvtStringToAcceleratorTable",XtCXtToolkitError,
          "String to AcceleratorTable conversion encountered errors",
	  (String *)NULL, (Cardinal *)NULL);
    return (error != TRUE);
}


/*ARGSUSED*/
Boolean
XtCvtStringToTranslationTable(
    Display	*dpy,
    XrmValuePtr args,
    Cardinal    *num_args,
    XrmValuePtr from,
    XrmValuePtr to,
    XtPointer	*closure_ret)
{
    String str;
    Boolean error = FALSE;

    if (*num_args != 0)
      XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
	    "wrongParameters","cvtStringToTranslationTable",XtCXtToolkitError,
	    "String to TranslationTable conversion needs no extra arguments",
	    (String *)NULL, (Cardinal *)NULL);
    str = (String)(from->addr);
    if (str == NULL) {
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
	  "badParameters","cvtStringToTranslation",XtCXtToolkitError,
          "String to TranslationTable conversion needs string",
	  (String *)NULL, (Cardinal *)NULL);
	return FALSE;
    }
    if (to->addr != NULL) {
	if (to->size < sizeof(XtTranslations)) {
	    to->size = sizeof(XtTranslations);
	    return FALSE;
	}
	*(XtTranslations*)to->addr =
	    ParseTranslationTable(str, FALSE, XtTableReplace, &error);
    }
    else {
	static XtTranslations staticStateTable;
	staticStateTable =
	    ParseTranslationTable(str, FALSE, XtTableReplace, &error);
	to->addr = (XPointer) &staticStateTable;
	to->size = sizeof(XtTranslations);
    }
    if (error == TRUE)
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
	  "parseError","cvtStringToTranslationTable",XtCXtToolkitError,
          "String to TranslationTable conversion encountered errors",
	  (String *)NULL, (Cardinal *)NULL);
    return (error != TRUE);
}


/*
 * Parses a user's or applications translation table
 */
XtAccelerators XtParseAcceleratorTable(
    _Xconst char* source)
{
    Boolean error = FALSE;
    XtAccelerators ret =
	(XtAccelerators) ParseTranslationTable ((char *)source, TRUE, XtTableAugment, &error);
    if (error == TRUE)
        XtWarningMsg ("parseError", "cvtStringToAcceleratorTable",
	  XtCXtToolkitError,
          "String to AcceleratorTable conversion encountered errors",
	  (String *)NULL, (Cardinal *)NULL);
    return ret;
}

XtTranslations XtParseTranslationTable(
    _Xconst char* source)
{
    Boolean error = FALSE;
    XtTranslations ret = ParseTranslationTable((char *)source, FALSE, XtTableReplace, &error);
    if (error == TRUE)
        XtWarningMsg ("parseError",
	  "cvtStringToTranslationTable", XtCXtToolkitError,
          "String to TranslationTable conversion encountered errors",
	  (String *)NULL, (Cardinal *)NULL);
    return ret;
}

void _XtTranslateInitialize(void)
{
    LOCK_PROCESS;
    if (initialized) {
	XtWarningMsg("translationError","xtTranslateInitialize",
                  XtCXtToolkitError,"Initializing Translation manager twice.",
                    (String *)NULL, (Cardinal *)NULL);
	UNLOCK_PROCESS;
	return;
    }

    initialized = TRUE;
    UNLOCK_PROCESS;
    QMeta = XrmPermStringToQuark("Meta");
    QCtrl = XrmPermStringToQuark("Ctrl");
    QNone = XrmPermStringToQuark("None");
    QAny  = XrmPermStringToQuark("Any");

    Compile_XtEventTable( events, XtNumber(events) );
    Compile_XtModifierTable( modifiers, XtNumber(modifiers) );
    CompileNameValueTable( buttonNames );
    CompileNameValueTable( notifyModes );
    CompileNameValueTable( motionDetails );
#if 0
    CompileNameValueTable( notifyDetail );
    CompileNameValueTable( visibilityNotify );
    CompileNameValueTable( circulation );
    CompileNameValueTable( propertyChanged );
#endif
    CompileNameValueTable( mappingNotify );
}

void _XtAddTMConverters(
    ConverterTable table)
{
     _XtTableAddConverter(table,
	     _XtQString,
	     XrmPermStringToQuark(XtRTranslationTable),
 	     XtCvtStringToTranslationTable, (XtConvertArgList) NULL,
	     (Cardinal)0, True, CACHED, _XtFreeTranslations, True);
     _XtTableAddConverter(table, _XtQString,
	     XrmPermStringToQuark(XtRAcceleratorTable),
 	     XtCvtStringToAcceleratorTable, (XtConvertArgList) NULL,
	     (Cardinal)0, True, CACHED, _XtFreeTranslations, True);
     _XtTableAddConverter(table,
	     XrmPermStringToQuark( _XtRStateTablePair ),
	     XrmPermStringToQuark(XtRTranslationTable),
 	     _XtCvtMergeTranslations, (XtConvertArgList) NULL,
	     (Cardinal)0, True, CACHED, _XtFreeTranslations, True);
}
