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

#define XK_MISCELLANY
#define XK_LATIN1
#define XK_LATIN2
#define XK_LATIN3
#define XK_LATIN4

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "IntrinsicI.h"
#include <X11/keysymdef.h>
#ifdef XKB
#include <X11/XKBlib.h>
#endif

#define FLUSHKEYCACHE(ctx) \
        memset((void *)&ctx->keycache, 0, sizeof(TMKeyCache))

/*
 * The following array reorders the modifier bits so that the most common ones
 * (used by a translator) are in the top-most bits with respect to the size of
 * the keycache.  The array currently just reverses the bits as a good guess.
 * This might be more trouble than it is worth, but it seems to help.
 */

#define FM(i) i >> (8 - TMKEYCACHELOG2)

/* *INDENT-OFF* */
static const unsigned char modmix[256] = {
FM(0x0f), FM(0x8f), FM(0x4f), FM(0xcf), FM(0x2f), FM(0xaf), FM(0x6f), FM(0xef),
FM(0x1f), FM(0x9f), FM(0x5f), FM(0xdf), FM(0x3f), FM(0xbf), FM(0x7f), FM(0xff),
FM(0x07), FM(0x87), FM(0x47), FM(0xc7), FM(0x27), FM(0xa7), FM(0x67), FM(0xe7),
FM(0x17), FM(0x97), FM(0x57), FM(0xd7), FM(0x37), FM(0xb7), FM(0x77), FM(0xf7),
FM(0x0b), FM(0x8b), FM(0x4b), FM(0xcb), FM(0x2b), FM(0xab), FM(0x6b), FM(0xeb),
FM(0x1b), FM(0x9b), FM(0x5b), FM(0xdb), FM(0x3b), FM(0xbb), FM(0x7b), FM(0xfb),
FM(0x03), FM(0x83), FM(0x43), FM(0xc3), FM(0x23), FM(0xa3), FM(0x63), FM(0xe3),
FM(0x13), FM(0x93), FM(0x53), FM(0xd3), FM(0x33), FM(0xb3), FM(0x73), FM(0xf3),
FM(0x0d), FM(0x8d), FM(0x4d), FM(0xcd), FM(0x2d), FM(0xad), FM(0x6d), FM(0xed),
FM(0x1d), FM(0x9d), FM(0x5d), FM(0xdd), FM(0x3d), FM(0xbd), FM(0x7d), FM(0xfd),
FM(0x05), FM(0x85), FM(0x45), FM(0xc5), FM(0x25), FM(0xa5), FM(0x65), FM(0xe5),
FM(0x15), FM(0x95), FM(0x55), FM(0xd5), FM(0x35), FM(0xb5), FM(0x75), FM(0xf5),
FM(0x09), FM(0x89), FM(0x49), FM(0xc9), FM(0x29), FM(0xa9), FM(0x69), FM(0xe9),
FM(0x19), FM(0x99), FM(0x59), FM(0xd9), FM(0x39), FM(0xb9), FM(0x79), FM(0xf9),
FM(0x01), FM(0x81), FM(0x41), FM(0xc1), FM(0x21), FM(0xa1), FM(0x61), FM(0xe1),
FM(0x11), FM(0x91), FM(0x51), FM(0xd1), FM(0x31), FM(0xb1), FM(0x71), FM(0xf1),
FM(0x00), FM(0x8e), FM(0x4e), FM(0xce), FM(0x2e), FM(0xae), FM(0x6e), FM(0xee),
FM(0x1e), FM(0x9e), FM(0x5e), FM(0xde), FM(0x3e), FM(0xbe), FM(0x7e), FM(0xfe),
FM(0x08), FM(0x88), FM(0x48), FM(0xc8), FM(0x28), FM(0xa8), FM(0x68), FM(0xe8),
FM(0x18), FM(0x98), FM(0x58), FM(0xd8), FM(0x38), FM(0xb8), FM(0x78), FM(0xf8),
FM(0x04), FM(0x84), FM(0x44), FM(0xc4), FM(0x24), FM(0xa4), FM(0x64), FM(0xe4),
FM(0x14), FM(0x94), FM(0x54), FM(0xd4), FM(0x34), FM(0xb4), FM(0x74), FM(0xf4),
FM(0x0c), FM(0x8c), FM(0x4c), FM(0xcc), FM(0x2c), FM(0xac), FM(0x6c), FM(0xec),
FM(0x1c), FM(0x9c), FM(0x5c), FM(0xdc), FM(0x3c), FM(0xbc), FM(0x7c), FM(0xfc),
FM(0x02), FM(0x82), FM(0x42), FM(0xc2), FM(0x22), FM(0xa2), FM(0x62), FM(0xe2),
FM(0x12), FM(0x92), FM(0x52), FM(0xd2), FM(0x32), FM(0xb2), FM(0x72), FM(0xf2),
FM(0x0a), FM(0x8a), FM(0x4a), FM(0xca), FM(0x2a), FM(0xaa), FM(0x6a), FM(0xea),
FM(0x1a), FM(0x9a), FM(0x5a), FM(0xda), FM(0x3a), FM(0xba), FM(0x7a), FM(0xfa),
FM(0x06), FM(0x86), FM(0x46), FM(0xc6), FM(0x26), FM(0xa6), FM(0x66), FM(0xe6),
FM(0x16), FM(0x96), FM(0x56), FM(0xd6), FM(0x36), FM(0xb6), FM(0x76), FM(0xf6),
FM(0x0e), FM(0x8e), FM(0x4e), FM(0xce), FM(0x2e), FM(0xae), FM(0x6e), FM(0xee),
FM(0x1e), FM(0x9e), FM(0x5e), FM(0xde), FM(0x3e), FM(0xbe), FM(0x7e), FM(0xfe)
};
/* *INDENT-ON* */
#undef FM

#define MOD_RETURN(ctx, key) (ctx)->keycache.modifiers_return[key]

#define TRANSLATE(ctx,pd,dpy,key,mod,mod_ret,sym_ret) \
{ \
    int _i_ = (((key) - (TMLongCard) (pd)->min_keycode + modmix[(mod) & 0xff]) & \
               (TMKEYCACHESIZE-1)); \
    if ((key) == 0) { /* Xlib XIM composed input */ \
        mod_ret = 0; \
        sym_ret = 0; \
    } else if (   /* not Xlib XIM composed input */ \
        (ctx)->keycache.keycode[_i_] == (key) && \
        (ctx)->keycache.modifiers[_i_] == (mod)) { \
        mod_ret = MOD_RETURN(ctx, key); \
        sym_ret = (ctx)->keycache.keysym[_i_]; \
    } else { \
        XtTranslateKeycode(dpy, (KeyCode) key, mod, &mod_ret, &sym_ret); \
        (ctx)->keycache.keycode[_i_] = (KeyCode) (key); \
        (ctx)->keycache.modifiers[_i_] = (unsigned char)(mod); \
        (ctx)->keycache.keysym[_i_] = sym_ret; \
        MOD_RETURN(ctx, key) = (unsigned char)mod_ret; \
    } \
}

#define UPDATE_CACHE(ctx, pd, key, mod, mod_ret, sym_ret) \
{ \
    int _i_ = (((key) - (TMLongCard) (pd)->min_keycode + modmix[(mod) & 0xff]) & \
               (TMKEYCACHESIZE-1)); \
    (ctx)->keycache.keycode[_i_] = (KeyCode) (key); \
    (ctx)->keycache.modifiers[_i_] = (unsigned char)(mod); \
    (ctx)->keycache.keysym[_i_] = sym_ret; \
    MOD_RETURN(ctx, key) = (unsigned char)(mod_ret); \
}

/* usual number of expected keycodes in XtKeysymToKeycodeList */
#define KEYCODE_ARRAY_SIZE 10

Boolean
_XtComputeLateBindings(Display *dpy,
                       LateBindingsPtr lateModifiers,
                       Modifiers *computed,
                       Modifiers *computedMask)
{
    int i, j, ref;
    ModToKeysymTable *temp;
    XtPerDisplay perDisplay;
    KeySym tempKeysym = NoSymbol;

    perDisplay = _XtGetPerDisplay(dpy);
    if (perDisplay == NULL) {
        XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
                        "displayError", "invalidDisplay", XtCXtToolkitError,
                        "Can't find display structure", NULL, NULL);
        return FALSE;
    }
    _InitializeKeysymTables(dpy, perDisplay);
    for (ref = 0; lateModifiers[ref].keysym; ref++) {
        Boolean found = FALSE;

        for (i = 0; i < 8; i++) {
            temp = &(perDisplay->modsToKeysyms[i]);
            for (j = 0; j < temp->count; j++) {
                if (perDisplay->modKeysyms[temp->idx + j] ==
                    lateModifiers[ref].keysym) {
                    *computedMask = *computedMask | temp->mask;
                    if (!lateModifiers[ref].knot)
                        *computed |= temp->mask;
                    tempKeysym = lateModifiers[ref].keysym;
                    found = TRUE;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found && !lateModifiers[ref].knot)
            if (!lateModifiers[ref].pair && (tempKeysym == NoSymbol))
                return FALSE;
        /* if you didn't find the modifier and the modifier must be
           asserted then return FALSE. If you didn't find the modifier
           and the modifier must be off, then it is OK . Don't
           return FALSE if this is the first member of a pair or if
           it is the second member of a pair when the first member
           was bound to a modifier */
        if (!lateModifiers[ref].pair)
            tempKeysym = NoSymbol;
    }
    return TRUE;
}

void
_XtAllocTMContext(XtPerDisplay pd)
{
    TMKeyContext ctx;

    ctx = (TMKeyContext) _XtHeapAlloc(&pd->heap, sizeof(TMKeyContextRec));
    ctx->event = NULL;
    ctx->serial = 0;
    ctx->keysym = NoSymbol;
    ctx->modifiers = 0;
    FLUSHKEYCACHE(ctx);
    pd->tm_context = ctx;
}

static unsigned int
num_bits(unsigned long mask)
{
    register unsigned long y;

    y = (mask >> 1) & 033333333333;
    y = mask - y - ((y >> 1) & 033333333333);
    return ((unsigned int) (((y + (y >> 3)) & 030707070707) % 077));
}

Boolean
_XtMatchUsingDontCareMods(TMTypeMatch typeMatch,
                          TMModifierMatch modMatch,
                          TMEventPtr eventSeq)
{
    Modifiers modifiers_return;
    KeySym keysym_return;
    Modifiers useful_mods;
    Modifiers computed = 0;
    Modifiers computedMask = 0;
    Boolean resolved = TRUE;
    Display *dpy = eventSeq->xev->xany.display;
    XtPerDisplay pd;

    if (modMatch->lateModifiers != NULL)
        resolved = _XtComputeLateBindings(dpy, modMatch->lateModifiers,
                                          &computed, &computedMask);
    if (!resolved)
        return FALSE;
    computed = (Modifiers) (computed | modMatch->modifiers);
    computedMask = (Modifiers) (computedMask | modMatch->modifierMask); /* gives do-care mask */

    if ((computed & computedMask) == (eventSeq->event.modifiers & computedMask)) {
        TMKeyContext tm_context;
        int num_modbits;
        int i;

        pd = _XtGetPerDisplay(dpy);
        tm_context = pd->tm_context;
        TRANSLATE(tm_context, pd, dpy, (KeyCode) eventSeq->event.eventCode,
                  (unsigned) 0, modifiers_return, keysym_return);

        if ((keysym_return & typeMatch->eventCodeMask) == typeMatch->eventCode) {
            tm_context->event = eventSeq->xev;
            tm_context->serial = eventSeq->xev->xany.serial;
            tm_context->keysym = keysym_return;
            tm_context->modifiers = (Modifiers) 0;
            return TRUE;
        }
        useful_mods = ~computedMask & modifiers_return;
        if (useful_mods == 0)
            return FALSE;

        switch (num_modbits = (int) num_bits(useful_mods)) {
        case 1:
        case 8:
            /*
             * one modbit should never happen, in fact the implementation
             * of XtTranslateKey and XmTranslateKey guarantee that it
             * won't, so don't care if the loop is set up for the case
             * when one modbit is set.
             * The performance implications of all eight modbits being
             * set is horrendous. This isn't a problem with Xt/Xaw based
             * applications. We can only hope that Motif's virtual
             * modifiers won't result in all eight modbits being set.
             */
            for (i = (int) useful_mods; i > 0; i--) {
                TRANSLATE(tm_context, pd, dpy, eventSeq->event.eventCode,
                          (Modifiers) i, modifiers_return, keysym_return);
                if (keysym_return ==
                    (typeMatch->eventCode & typeMatch->eventCodeMask)) {
                    tm_context->event = eventSeq->xev;
                    tm_context->serial = eventSeq->xev->xany.serial;
                    tm_context->keysym = keysym_return;
                    tm_context->modifiers = (Modifiers) i;
                    return TRUE;
                }
            }
            break;
        default:               /* (2..7) */
        {
            /*
             * Only translate using combinations of the useful modifiers.
             * to minimize the chance of invalidating the cache.
             */
            static char pows[] = { 0, 1, 3, 7, 15, 31, 63, 127 };
            Modifiers tmod, mod_masks[8];
            int j;

            for (tmod = 1, i = 0; tmod <= (Mod5Mask << 1); tmod <<= 1)
                if (tmod & useful_mods)
                    mod_masks[i++] = tmod;
            for (j = (int) pows[num_modbits]; j > 0; j--) {
                tmod = 0;
                for (i = 0; i < num_modbits; i++)
                    if (j & (1 << i))
                        tmod |= mod_masks[i];
                TRANSLATE(tm_context, pd, dpy, eventSeq->event.eventCode,
                          tmod, modifiers_return, keysym_return);
                if (keysym_return ==
                    (typeMatch->eventCode & typeMatch->eventCodeMask)) {
                    tm_context->event = eventSeq->xev;
                    tm_context->serial = eventSeq->xev->xany.serial;
                    tm_context->keysym = keysym_return;
                    tm_context->modifiers = (Modifiers) i;
                    return TRUE;
                }
            }
        }
            break;
        }                       /* switch (num_modbits) */
    }
    return FALSE;
}

void
XtConvertCase(Display *dpy,
              KeySym keysym,
              KeySym *lower_return,
              KeySym *upper_return)
{
    XtPerDisplay pd;
    CaseConverterPtr ptr;

    DPY_TO_APPCON(dpy);

    LOCK_APP(app);
    pd = _XtGetPerDisplay(dpy);

    *lower_return = *upper_return = keysym;
    for (ptr = pd->case_cvt; ptr; ptr = ptr->next)
        if (ptr->start <= keysym && keysym <= ptr->stop) {
            (*ptr->proc) (dpy, keysym, lower_return, upper_return);
            return;
        }
    XConvertCase(keysym, lower_return, upper_return);
    UNLOCK_APP(app);
}

Boolean
_XtMatchUsingStandardMods(TMTypeMatch typeMatch,
                          TMModifierMatch modMatch,
                          TMEventPtr eventSeq)
{
    Modifiers modifiers_return;
    KeySym keysym_return;
    Modifiers computed = 0;
    Modifiers computedMask = 0;
    Display *dpy = eventSeq->xev->xany.display;
    XtPerDisplay pd = _XtGetPerDisplay(dpy);
    TMKeyContext tm_context = pd->tm_context;
    Modifiers translateModifiers;

    /* To maximize cache utilization, we mask off nonstandard modifiers
       before cache lookup.  For a given key translator, standard modifiers
       are constant per KeyCode.  If a key translator uses no standard
       modifiers this implementation will never reference the cache.
     */

    modifiers_return = MOD_RETURN(tm_context, eventSeq->event.eventCode);
    if (!modifiers_return) {
        XtTranslateKeycode(dpy, (KeyCode) eventSeq->event.eventCode,
                           (Modifiers) eventSeq->event.modifiers,
                           &modifiers_return, &keysym_return);
        translateModifiers =
            (Modifiers) (eventSeq->event.modifiers & modifiers_return);
        UPDATE_CACHE(tm_context, pd, eventSeq->event.eventCode,
                     translateModifiers, modifiers_return, keysym_return);
    }
    else {
        translateModifiers =
            (Modifiers) (eventSeq->event.modifiers & modifiers_return);
        TRANSLATE(tm_context, pd, dpy, (KeyCode) eventSeq->event.eventCode,
                  translateModifiers, modifiers_return, keysym_return);
    }

    if ((typeMatch->eventCode & typeMatch->eventCodeMask) ==
        (keysym_return & typeMatch->eventCodeMask)) {
        Boolean resolved = TRUE;

        if (modMatch->lateModifiers != NULL)
            resolved = _XtComputeLateBindings(dpy, modMatch->lateModifiers,
                                              &computed, &computedMask);
        if (!resolved)
            return FALSE;
        computed = (Modifiers) (computed | modMatch->modifiers);
        computedMask = (Modifiers) (computedMask | modMatch->modifierMask);

        if ((computed & computedMask) ==
            (eventSeq->event.modifiers & ~modifiers_return & computedMask)) {
            tm_context->event = eventSeq->xev;
            tm_context->serial = eventSeq->xev->xany.serial;
            tm_context->keysym = keysym_return;
            tm_context->modifiers = translateModifiers;
            return TRUE;
        }
    }
    return FALSE;
}

void
_XtBuildKeysymTables(Display *dpy, register XtPerDisplay pd)
{
    ModToKeysymTable *table;
    int maxCount, i, j, k, tempCount, idx;
    KeySym keysym, tempKeysym;
    XModifierKeymap *modKeymap;
    KeyCode keycode;

#define KeysymTableSize 16

    FLUSHKEYCACHE(pd->tm_context);

    XFree((char *) pd->keysyms);
    pd->keysyms_serial = NextRequest(dpy);
    pd->keysyms = XGetKeyboardMapping(dpy, (KeyCode) pd->min_keycode,
                                      pd->max_keycode - pd->min_keycode + 1,
                                      &pd->keysyms_per_keycode);
    XtFree((char *) pd->modKeysyms);

    pd->modKeysyms = XtMallocArray(KeysymTableSize, (Cardinal) sizeof(KeySym));
    maxCount = KeysymTableSize;
    tempCount = 0;

    XtFree((char *) pd->modsToKeysyms);
    table = XtMallocArray(8, (Cardinal) sizeof(ModToKeysymTable));
    pd->modsToKeysyms = table;

    table[0].mask = ShiftMask;
    table[1].mask = LockMask;
    table[2].mask = ControlMask;
    table[3].mask = Mod1Mask;
    table[4].mask = Mod2Mask;
    table[5].mask = Mod3Mask;
    table[6].mask = Mod4Mask;
    table[7].mask = Mod5Mask;
    tempKeysym = 0;

    modKeymap = XGetModifierMapping(dpy);
    for (i = 0; i < 32; i++)
        pd->isModifier[i] = 0;
    pd->mode_switch = 0;
    pd->num_lock = 0;
    for (i = 0; i < 8; i++) {
        table[i].idx = tempCount;
        table[i].count = 0;
        for (j = 0; j < modKeymap->max_keypermod; j++) {
            keycode = modKeymap->modifiermap[i * modKeymap->max_keypermod + j];
            if (keycode != 0) {
                pd->isModifier[keycode >> 3] |=
                    (unsigned char) (1 << (keycode & 7));
                for (k = 0; k < pd->keysyms_per_keycode; k++) {
                    idx = ((keycode - pd->min_keycode) *
                           pd->keysyms_per_keycode) + k;
                    keysym = pd->keysyms[idx];
                    if ((keysym == XK_Mode_switch) && (i > 2))
                        pd->mode_switch =
                            (pd->mode_switch | (Modifiers) (1 << i));
                    if ((keysym == XK_Num_Lock) && (i > 2))
                        pd->num_lock = (pd->num_lock | (Modifiers) (1 << i));
                    if (keysym != 0 && keysym != tempKeysym) {
                        if (tempCount == maxCount) {
                            maxCount += KeysymTableSize;
                            pd->modKeysyms =
                                XtReallocArray(pd->modKeysyms,
                                               (Cardinal) maxCount,
                                               (Cardinal) sizeof(KeySym));
                        }
                        pd->modKeysyms[tempCount++] = keysym;
                        table[i].count++;
                        tempKeysym = keysym;
                    }
                }
            }
        }
    }
    pd->lock_meaning = NoSymbol;
    for (i = 0; i < table[1].count; i++) {
        keysym = pd->modKeysyms[table[1].idx + i];
        if (keysym == XK_Caps_Lock) {
            pd->lock_meaning = XK_Caps_Lock;
            break;
        }
        else if (keysym == XK_Shift_Lock) {
            pd->lock_meaning = XK_Shift_Lock;
        }
    }
    XFreeModifiermap(modKeymap);
}

void
XtTranslateKeycode(Display *dpy,
                   _XtKeyCode keycode,
                   Modifiers modifiers,
                   Modifiers *modifiers_return,
                   KeySym *keysym_return)
{
    XtPerDisplay pd;

    DPY_TO_APPCON(dpy);

    LOCK_APP(app);
    pd = _XtGetPerDisplay(dpy);
    _InitializeKeysymTables(dpy, pd);
    (*pd->defaultKeycodeTranslator) (dpy, keycode, modifiers, modifiers_return,
                                     keysym_return);
    UNLOCK_APP(app);
}

/* This code should match XTranslateKey (internal, sigh) in Xlib */
void
XtTranslateKey(register Display *dpy,
               _XtKeyCode keycode,
               Modifiers modifiers,
               Modifiers *modifiers_return,
               KeySym *keysym_return)
{
#ifndef XKB
    XtPerDisplay pd;
    int per;
    register KeySym *syms;
    KeySym sym, lsym, usym;

    DPY_TO_APPCON(dpy);

    LOCK_APP(app);
    pd = _XtGetPerDisplay(dpy);
    *modifiers_return = (ShiftMask | LockMask) | pd->mode_switch | pd->num_lock;
    if (((int) keycode < pd->min_keycode) || ((int) keycode > pd->max_keycode)) {
        *keysym_return = NoSymbol;
        UNLOCK_APP(app);
        return;
    }
    per = pd->keysyms_per_keycode;
    syms = &pd->keysyms[(keycode - pd->min_keycode) * per];
    while ((per > 2) && (syms[per - 1] == NoSymbol))
        per--;
    if ((per > 2) && (modifiers & pd->mode_switch)) {
        syms += 2;
        per -= 2;
    }
    if ((modifiers & pd->num_lock) &&
        (per > 1 && (IsKeypadKey(syms[1]) || IsPrivateKeypadKey(syms[1])))) {
        if ((modifiers & ShiftMask) ||
            ((modifiers & LockMask) && (pd->lock_meaning == XK_Shift_Lock)))
            *keysym_return = syms[0];
        else
            *keysym_return = syms[1];
    }
    else if (!(modifiers & ShiftMask) &&
             (!(modifiers & LockMask) || (pd->lock_meaning == NoSymbol))) {
        if ((per == 1) || (syms[1] == NoSymbol))
            XtConvertCase(dpy, syms[0], keysym_return, &usym);
        else
            *keysym_return = syms[0];
    }
    else if (!(modifiers & LockMask) || (pd->lock_meaning != XK_Caps_Lock)) {
        if ((per == 1) || ((usym = syms[1]) == NoSymbol))
            XtConvertCase(dpy, syms[0], &lsym, &usym);
        *keysym_return = usym;
    }
    else {
        if ((per == 1) || ((sym = syms[1]) == NoSymbol))
            sym = syms[0];
        XtConvertCase(dpy, sym, &lsym, &usym);
        if (!(modifiers & ShiftMask) && (sym != syms[0]) &&
            ((sym != usym) || (lsym == usym)))
            XtConvertCase(dpy, syms[0], &lsym, &usym);
        *keysym_return = usym;
    }

    if (*keysym_return == XK_VoidSymbol)
        *keysym_return = NoSymbol;
    UNLOCK_APP(app);
#else
    XkbLookupKeySym(dpy, (KeyCode) keycode, modifiers, modifiers_return,
                    keysym_return);
#endif
}

void
XtSetKeyTranslator(Display *dpy, XtKeyProc translator)
{
    XtPerDisplay pd;

    DPY_TO_APPCON(dpy);

    LOCK_APP(app);
    pd = _XtGetPerDisplay(dpy);

    pd->defaultKeycodeTranslator = translator;
    FLUSHKEYCACHE(pd->tm_context);
    /* XXX should now redo grabs */
    UNLOCK_APP(app);
}

void
XtRegisterCaseConverter(Display *dpy,
                        XtCaseProc proc,
                        KeySym start,
                        KeySym stop)
{
    XtPerDisplay pd;
    CaseConverterPtr ptr, prev;

    DPY_TO_APPCON(dpy);

    LOCK_APP(app);
    pd = _XtGetPerDisplay(dpy);

    ptr = (CaseConverterPtr) __XtMalloc(sizeof(CaseConverterRec));
    ptr->start = start;
    ptr->stop = stop;
    ptr->proc = proc;
    ptr->next = pd->case_cvt;
    pd->case_cvt = ptr;

    /* Remove obsolete case converters from the list */
    prev = ptr;
    for (ptr = ptr->next; ptr; ptr = prev->next) {
        if (start <= ptr->start && stop >= ptr->stop) {
            prev->next = ptr->next;
            XtFree((char *) ptr);
        }
        else
            prev = ptr;
    }
    FLUSHKEYCACHE(pd->tm_context);
    /* XXX should now redo grabs */
    UNLOCK_APP(app);
}

KeySym *
XtGetKeysymTable(Display *dpy,
                 KeyCode *min_keycode_return,
                 int *keysyms_per_keycode_return)
{
    XtPerDisplay pd;
    KeySym *retval;

    DPY_TO_APPCON(dpy);

    LOCK_APP(app);
    pd = _XtGetPerDisplay(dpy);
    _InitializeKeysymTables(dpy, pd);
    *min_keycode_return = (KeyCode) pd->min_keycode;    /* %%% */
    *keysyms_per_keycode_return = pd->keysyms_per_keycode;
    retval = pd->keysyms;
    UNLOCK_APP(app);
    return retval;
}

void
XtKeysymToKeycodeList(Display *dpy,
                      KeySym keysym,
                      KeyCode **keycodes_return,
                      Cardinal *keycount_return)
{
    XtPerDisplay pd;
    unsigned keycode;
    int per;
    register KeySym *syms;
    register int i, j;
    KeySym lsym, usym;
    unsigned maxcodes = 0;
    unsigned ncodes = 0;
    KeyCode *keycodes, *codeP = NULL;

    DPY_TO_APPCON(dpy);

    LOCK_APP(app);
    pd = _XtGetPerDisplay(dpy);
    _InitializeKeysymTables(dpy, pd);
    keycodes = NULL;
    per = pd->keysyms_per_keycode;
    for (syms = pd->keysyms, keycode = (unsigned) pd->min_keycode;
         (int) keycode <= pd->max_keycode; syms += per, keycode++) {
        int match = 0;

        for (j = 0; j < per; j++) {
            if (syms[j] == keysym) {
                match = 1;
                break;
            }
        }
        if (!match)
            for (i = 1; i < 5; i += 2) {
                if ((per == i) || ((per > i) && (syms[i] == NoSymbol))) {
                    XtConvertCase(dpy, syms[i - 1], &lsym, &usym);
                    if ((lsym == keysym) || (usym == keysym)) {
                        match = 1;
                        break;
                    }
                }
            }
        if (match) {
            if (ncodes == maxcodes) {
                KeyCode *old = keycodes;

                maxcodes += KEYCODE_ARRAY_SIZE;
                keycodes = XtMallocArray(maxcodes, (Cardinal) sizeof(KeyCode));
                if (ncodes) {
                    (void) memcpy(keycodes, old, ncodes * sizeof(KeyCode));
                    XtFree((char *) old);
                }
                codeP = &keycodes[ncodes];
            }
            *codeP++ = (KeyCode) keycode;
            ncodes++;
        }
    }
    *keycodes_return = keycodes;
    *keycount_return = ncodes;
    UNLOCK_APP(app);
}
