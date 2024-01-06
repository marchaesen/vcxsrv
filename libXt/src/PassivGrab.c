/*

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

*/
/********************************************************

Copyright 1988 by Hewlett-Packard Company
Copyright 1987, 1988, 1989,1990 by Digital Equipment Corporation, Maynard, Massachusetts

Permission to use, copy, modify, and distribute this software
and its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that copyright notice and this permission
notice appear in supporting documentation, and that the names of
Hewlett-Packard or Digital not be used in advertising or
publicity pertaining to distribution of the software without specific,
written prior permission.

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

********************************************************/

/*

Copyright 1987, 1988, 1989, 1990, 1994, 1998  The Open Group

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
#include "PassivGraI.h"

/* typedef unsigned long Mask; */
#define BITMASK(i) (((Mask)1) << ((i) & 31))
#define MASKIDX(i) ((i) >> 5)
#define MASKWORD(buf, i) buf[MASKIDX(i)]
#define BITSET(buf, i) MASKWORD(buf, i) |= BITMASK(i)
#define BITCLEAR(buf, i) MASKWORD(buf, i) &= ~BITMASK(i)
#define GETBIT(buf, i) (MASKWORD(buf, i) & BITMASK(i))
#define MasksPerDetailMask 8

#define pDisplay(grabPtr) (((grabPtr)->widget)->core.screen->display)
#define pWindow(grabPtr) (((grabPtr)->widget)->core.window)

/***************************************************************************/
/*********************** Internal Support Routines *************************/
/***************************************************************************/

/*
 * Turn off (clear) the bit in the specified detail mask which is associated
 * with the detail.
 */

static void
DeleteDetailFromMask(Mask **ppDetailMask, unsigned short detail)
{
    Mask *pDetailMask = *ppDetailMask;

    if (!pDetailMask) {
        int i;

        pDetailMask = XtMallocArray(MasksPerDetailMask, (Cardinal) sizeof(Mask));
        for (i = MasksPerDetailMask; --i >= 0;)
            pDetailMask[i] = (unsigned long) (~0);
        *ppDetailMask = pDetailMask;
    }
    BITCLEAR((pDetailMask), detail);
}

/*
 * Make an exact copy of the specified detail mask.
 */

static Mask *
CopyDetailMask(const Mask *pOriginalDetailMask)
{
    Mask *pTempMask;
    int i;

    if (!pOriginalDetailMask)
        return NULL;

    pTempMask = XtMallocArray(MasksPerDetailMask, (Cardinal) sizeof(Mask));

    for (i = 0; i < MasksPerDetailMask; i++)
        pTempMask[i] = pOriginalDetailMask[i];

    return pTempMask;
}

/*
 * Allocate a new grab entry, and fill in all of the fields using the
 * specified parameters.
 */

static XtServerGrabPtr
CreateGrab(Widget widget,
           Boolean ownerEvents,
           Modifiers modifiers,
           KeyCode keybut,
           int pointer_mode,
           int keyboard_mode,
           Mask event_mask,
           Window confine_to,
           Cursor cursor,
           Boolean need_ext)
{
    XtServerGrabPtr grab;

    if (confine_to || cursor)
        need_ext = True;
    grab = (XtServerGrabPtr) __XtMalloc(sizeof(XtServerGrabRec) +
                                        (need_ext ? sizeof(XtServerGrabExtRec)
                                         : 0));
    grab->next = NULL;
    grab->widget = widget;
    XtSetBit(grab->ownerEvents, ownerEvents);
    XtSetBit(grab->pointerMode, pointer_mode);
    XtSetBit(grab->keyboardMode, keyboard_mode);
    grab->eventMask = (unsigned short) event_mask;
    XtSetBit(grab->hasExt, need_ext);
    grab->confineToIsWidgetWin = (XtWindow(widget) == confine_to);
    grab->modifiers = (unsigned short) modifiers;
    grab->keybut = keybut;
    if (need_ext) {
        XtServerGrabExtPtr ext = GRABEXT(grab);

        ext->pModifiersMask = NULL;
        ext->pKeyButMask = NULL;
        ext->confineTo = confine_to;
        ext->cursor = cursor;
    }
    return grab;
}

/*
 * Free up the space occupied by a grab entry.
 */

static void
FreeGrab(XtServerGrabPtr pGrab)
{
    if (pGrab->hasExt) {
	XtServerGrabExtPtr ext = GRABEXT(pGrab);
	XtFree((char *)ext->pModifiersMask);
	XtFree((char *)ext->pKeyButMask);
    }
    XtFree((char *) pGrab);
}

typedef struct _DetailRec {
    unsigned short exact;
    Mask *pMask;
} DetailRec, *DetailPtr;

/*
 * If the first detail is set to 'exception' and the second detail
 * is contained in the mask of the first, then TRUE is returned.
 */

static Bool
IsInGrabMask(register DetailPtr firstDetail,
             register DetailPtr secondDetail,
             unsigned short exception)
{
    if (firstDetail->exact == exception) {
        if (!firstDetail->pMask)
            return TRUE;

        /* (at present) never called with two non-null pMasks */
        if (secondDetail->exact == exception)
            return FALSE;

        if (GETBIT(firstDetail->pMask, secondDetail->exact))
            return TRUE;
    }

    return FALSE;
}

/*
 * If neither of the details is set to 'exception', and they match
 * exactly, then TRUE is returned.
 */

static Bool
IdenticalExactDetails(unsigned short firstExact,
                      unsigned short secondExact,
                      unsigned short exception)
{
    if ((firstExact == exception) || (secondExact == exception))
        return FALSE;

    if (firstExact == secondExact)
        return TRUE;

    return FALSE;
}

/*
 * If the first detail is set to 'exception', and its mask has the bit
 * enabled which corresponds to the second detail, OR if neither of the
 * details is set to 'exception' and the details match exactly, then
 * TRUE is returned.
 */

static Bool
DetailSupersedesSecond(register DetailPtr firstDetail,
                       register DetailPtr secondDetail,
                       unsigned short exception)
{
    if (IsInGrabMask(firstDetail, secondDetail, exception))
        return TRUE;

    if (IdenticalExactDetails(firstDetail->exact, secondDetail->exact,
                              exception))
        return TRUE;

    return FALSE;
}

/*
 * If the two grab events match exactly, or if the first grab entry
 * 'encompasses' the second grab entry, then TRUE is returned.
 */

static Bool
GrabSupersedesSecond(register XtServerGrabPtr pFirstGrab,
                     register XtServerGrabPtr pSecondGrab)
{
    DetailRec first, second;

    first.exact = pFirstGrab->modifiers;
    if (pFirstGrab->hasExt)
        first.pMask = GRABEXT(pFirstGrab)->pModifiersMask;
    else
        first.pMask = NULL;
    second.exact = pSecondGrab->modifiers;
    if (pSecondGrab->hasExt)
        second.pMask = GRABEXT(pSecondGrab)->pModifiersMask;
    else
        second.pMask = NULL;
    if (!DetailSupersedesSecond(&first, &second, (unsigned short) AnyModifier))
        return FALSE;

    first.exact = pFirstGrab->keybut;
    if (pFirstGrab->hasExt)
        first.pMask = GRABEXT(pFirstGrab)->pKeyButMask;
    else
        first.pMask = NULL;
    second.exact = pSecondGrab->keybut;
    if (pSecondGrab->hasExt)
        second.pMask = GRABEXT(pSecondGrab)->pKeyButMask;
    else
        second.pMask = NULL;
    if (DetailSupersedesSecond(&first, &second, (unsigned short) AnyKey))
        return TRUE;

    return FALSE;
}

/*
 * Two grabs are considered to be matching if either of the following are true:
 *
 * 1) The two grab entries match exactly, or the first grab entry
 *    encompasses the second grab entry.
 * 2) The second grab entry encompasses the first grab entry.
 * 3) The keycodes match exactly, and one entry's modifiers encompasses
 *    the others.
 * 4) The keycode for one entry encompasses the other, and the detail
 *    for the other entry encompasses the first.
 */

static Bool
GrabMatchesSecond(register XtServerGrabPtr pFirstGrab,
                  register XtServerGrabPtr pSecondGrab)
{
    DetailRec firstD, firstM, secondD, secondM;

    if (pDisplay(pFirstGrab) != pDisplay(pSecondGrab))
        return FALSE;

    if (GrabSupersedesSecond(pFirstGrab, pSecondGrab))
        return TRUE;

    if (GrabSupersedesSecond(pSecondGrab, pFirstGrab))
        return TRUE;

    firstD.exact = pFirstGrab->keybut;
    firstM.exact = pFirstGrab->modifiers;
    if (pFirstGrab->hasExt) {
        firstD.pMask = GRABEXT(pFirstGrab)->pKeyButMask;
        firstM.pMask = GRABEXT(pFirstGrab)->pModifiersMask;
    }
    else {
        firstD.pMask = NULL;
        firstM.pMask = NULL;
    }
    secondD.exact = pSecondGrab->keybut;
    secondM.exact = pSecondGrab->modifiers;
    if (pSecondGrab->hasExt) {
        secondD.pMask = GRABEXT(pSecondGrab)->pKeyButMask;
        secondM.pMask = GRABEXT(pSecondGrab)->pModifiersMask;
    }
    else {
        secondD.pMask = NULL;
        secondM.pMask = NULL;
    }

    if (DetailSupersedesSecond(&secondD, &firstD, (unsigned short) AnyKey) &&
        DetailSupersedesSecond(&firstM, &secondM, (unsigned short) AnyModifier))
        return TRUE;

    if (DetailSupersedesSecond(&firstD, &secondD, (unsigned short) AnyKey) &&
        DetailSupersedesSecond(&secondM, &firstM, (unsigned short) AnyModifier))
        return TRUE;

    return FALSE;
}

/*
 * Delete a grab combination from the passive grab list.  Each entry will
 * be checked to see if it is affected by the grab being deleted.  This
 * may result in multiple entries being modified/deleted.
 */

static void
DeleteServerGrabFromList(XtServerGrabPtr *passiveListPtr,
                         XtServerGrabPtr pMinuendGrab)
{
    register XtServerGrabPtr *next;
    register XtServerGrabPtr grab;
    register XtServerGrabExtPtr ext;

    for (next = passiveListPtr; (grab = *next);) {
        if (GrabMatchesSecond(grab, pMinuendGrab) &&
            (pDisplay(grab) == pDisplay(pMinuendGrab))) {
            if (GrabSupersedesSecond(pMinuendGrab, grab)) {
                /*
                 * The entry being deleted encompasses the list entry,
                 * so delete the list entry.
                 */
                *next = grab->next;
                FreeGrab(grab);
                continue;
            }

            if (!grab->hasExt) {
                grab = (XtServerGrabPtr)
                    XtRealloc((char *) grab, (sizeof(XtServerGrabRec) +
                                              sizeof(XtServerGrabExtRec)));
                *next = grab;
                grab->hasExt = True;
                ext = GRABEXT(grab);
                ext->pKeyButMask = NULL;
                ext->pModifiersMask = NULL;
                ext->confineTo = None;
                ext->cursor = None;
            }
            else
                ext = GRABEXT(grab);
            if ((grab->keybut == AnyKey) && (grab->modifiers != AnyModifier)) {
                /*
                 * If the list entry has the key detail of AnyKey, and
                 * a modifier detail not set to AnyModifier, then we
                 * simply need to turn off the key detail bit in the
                 * list entry's key detail mask.
                 */
                DeleteDetailFromMask(&ext->pKeyButMask, pMinuendGrab->keybut);
            }
            else if ((grab->modifiers == AnyModifier) &&
                     (grab->keybut != AnyKey)) {
                /*
                 * The list entry has a specific key detail, but its
                 * modifier detail is set to AnyModifier; so, we only
                 * need to turn off the specified modifier combination
                 * in the list entry's modifier mask.
                 */
                DeleteDetailFromMask(&ext->pModifiersMask,
                                     pMinuendGrab->modifiers);
            }
            else if ((pMinuendGrab->keybut != AnyKey) &&
                     (pMinuendGrab->modifiers != AnyModifier)) {
                /*
                 * The list entry has a key detail of AnyKey and a
                 * modifier detail of AnyModifier; the entry being
                 * deleted has a specific key and a specific modifier
                 * combination.  Therefore, we need to mask off the
                 * keycode from the list entry, and also create a
                 * new entry for this keycode, which has a modifier
                 * mask set to AnyModifier & ~(deleted modifiers).
                 */
                XtServerGrabPtr pNewGrab;

                DeleteDetailFromMask(&ext->pKeyButMask, pMinuendGrab->keybut);
                pNewGrab = CreateGrab(grab->widget,
                                      (Boolean) grab->ownerEvents,
                                      (Modifiers) AnyModifier,
                                      pMinuendGrab->keybut,
                                      (int) grab->pointerMode,
                                      (int) grab->keyboardMode,
                                      (Mask) 0, (Window) 0, (Cursor) 0, True);
                GRABEXT(pNewGrab)->pModifiersMask =
                    CopyDetailMask(ext->pModifiersMask);

                DeleteDetailFromMask(&GRABEXT(pNewGrab)->pModifiersMask,
                                     pMinuendGrab->modifiers);

                pNewGrab->next = *passiveListPtr;
                *passiveListPtr = pNewGrab;
            }
            else if (pMinuendGrab->keybut == AnyKey) {
                /*
                 * The list entry has keycode AnyKey and modifier
                 * AnyModifier; the entry being deleted has
                 * keycode AnyKey and specific modifiers.  So we
                 * simply need to mask off the specified modifier
                 * combination.
                 */
                DeleteDetailFromMask(&ext->pModifiersMask,
                                     pMinuendGrab->modifiers);
            }
            else {
                /*
                 * The list entry has keycode AnyKey and modifier
                 * AnyModifier; the entry being deleted has a
                 * specific keycode and modifier AnyModifier.  So
                 * we simply need to mask off the specified
                 * keycode.
                 */
                DeleteDetailFromMask(&ext->pKeyButMask, pMinuendGrab->keybut);
            }
        }
        next = &(*next)->next;
    }
}

static void
DestroyPassiveList(XtServerGrabPtr *passiveListPtr)
{
    XtServerGrabPtr next, grab;

    for (next = *passiveListPtr; next;) {
        grab = next;
        next = grab->next;

        /* not necessary to explicitly ungrab key or button;
         * window is being destroyed so server will take care of it.
         */

        FreeGrab(grab);
    }
}

/*
 * This function is called at widget destroy time to clean up
 */
void
_XtDestroyServerGrabs(Widget w,
                      XtPointer closure,
                      XtPointer call_data _X_UNUSED)
{
    XtPerWidgetInput pwi = (XtPerWidgetInput) closure;
    XtPerDisplayInput pdi;

    LOCK_PROCESS;
    pdi = _XtGetPerDisplayInput(XtDisplay(w));
    _XtClearAncestorCache(w);
    UNLOCK_PROCESS;

    /* Remove the active grab, if necessary */
    if ((pdi->keyboard.grabType != XtNoServerGrab) &&
        (pdi->keyboard.grab.widget == w)) {
        pdi->keyboard.grabType = XtNoServerGrab;
        pdi->activatingKey = (KeyCode) 0;
    }
    if ((pdi->pointer.grabType != XtNoServerGrab) &&
        (pdi->pointer.grab.widget == w))
        pdi->pointer.grabType = XtNoServerGrab;

    DestroyPassiveList(&pwi->keyList);
    DestroyPassiveList(&pwi->ptrList);

    _XtFreePerWidgetInput(w, pwi);
}

/*
 * If the incoming event is on the passive grab list, then activate
 * the grab.  The grab will remain in effect until the key is released.
 */

XtServerGrabPtr
_XtCheckServerGrabsOnWidget(XEvent *event, Widget widget, _XtBoolean isKeyboard)
{
    register XtServerGrabPtr grab;
    XtServerGrabRec tempGrab;
    XtServerGrabPtr *passiveListPtr;
    XtPerWidgetInput pwi;

    LOCK_PROCESS;
    pwi = _XtGetPerWidgetInput(widget, FALSE);
    UNLOCK_PROCESS;
    if (!pwi)
        return (XtServerGrabPtr) NULL;
    if (isKeyboard)
        passiveListPtr = &pwi->keyList;
    else
        passiveListPtr = &pwi->ptrList;

    /*
     * if either there is no entry in the context manager or the entry
     * is empty, or the keyboard is grabbed, then no work to be done
     */
    if (!*passiveListPtr)
        return (XtServerGrabPtr) NULL;

    /* Take only the lower thirteen bits as modifier state.  The X Keyboard
     * Extension may be representing keyboard group state in two upper bits.
     */
    tempGrab.widget = widget;
    tempGrab.keybut = (KeyCode) event->xkey.keycode;    /* also xbutton.button */
    tempGrab.modifiers = event->xkey.state & 0x1FFF;    /*also xbutton.state */
    tempGrab.hasExt = False;

    for (grab = *passiveListPtr; grab; grab = grab->next) {
        if (GrabMatchesSecond(&tempGrab, grab))
            return (grab);
    }
    return (XtServerGrabPtr) NULL;
}

/*
 * This handler is needed to guarantee that we see releases on passive
 * button grabs for widgets that haven't selected for button release.
 */

static void
ActiveHandler(Widget widget _X_UNUSED,
              XtPointer pdi _X_UNUSED,
              XEvent *event _X_UNUSED,
              Boolean *cont _X_UNUSED)
{
    /* nothing */
}

/*
 *      MakeGrab
 */
static void
MakeGrab(XtServerGrabPtr grab,
         XtServerGrabPtr *passiveListPtr,
         Boolean isKeyboard,
         XtPerDisplayInput pdi,
         XtPerWidgetInput pwi)
{
    if (!isKeyboard && !pwi->active_handler_added) {
        XtAddEventHandler(grab->widget, ButtonReleaseMask, FALSE,
                          ActiveHandler, (XtPointer) pdi);
        pwi->active_handler_added = TRUE;
    }

    if (isKeyboard) {
        XGrabKey(pDisplay(grab),
                 grab->keybut, grab->modifiers,
                 pWindow(grab), grab->ownerEvents,
                 grab->pointerMode, grab->keyboardMode);
    }
    else {
        Window confineTo = None;
        Cursor cursor = None;

        if (grab->hasExt) {
            if (grab->confineToIsWidgetWin)
                confineTo = XtWindow(grab->widget);
            else
                confineTo = GRABEXT(grab)->confineTo;
            cursor = GRABEXT(grab)->cursor;
        }
        XGrabButton(pDisplay(grab),
                    grab->keybut, grab->modifiers,
                    pWindow(grab), grab->ownerEvents, grab->eventMask,
                    grab->pointerMode, grab->keyboardMode, confineTo, cursor);
    }

    /* Add the new grab entry to the passive key grab list */
    grab->next = *passiveListPtr;
    *passiveListPtr = grab;
}

static void
MakeGrabs(XtServerGrabPtr *passiveListPtr,
          Boolean isKeyboard,
          XtPerDisplayInput pdi)
{
    XtServerGrabPtr next = *passiveListPtr;

    /*
     * make MakeGrab build a new list that has had the merge
     * processing done on it. Start with an empty list
     * (passiveListPtr).
     */
    LOCK_PROCESS;
    *passiveListPtr = NULL;
    while (next) {
        XtServerGrabPtr grab;
        XtPerWidgetInput pwi;

        grab = next;
        next = grab->next;
        pwi = _XtGetPerWidgetInput(grab->widget, FALSE);
        MakeGrab(grab, passiveListPtr, isKeyboard, pdi, pwi);
    }
    UNLOCK_PROCESS;
}

/*
 * This function is the event handler attached to the associated widget
 * when grabs need to be added, but the widget is not yet realized.  When
 * it is first mapped, this handler will be invoked, and it will add all
 * needed grabs.
 */

static void
RealizeHandler(Widget widget,
               XtPointer closure,
               XEvent *event _X_UNUSED,
               Boolean *cont _X_UNUSED)
{
    XtPerWidgetInput pwi = (XtPerWidgetInput) closure;
    XtPerDisplayInput pdi;

    LOCK_PROCESS;
    pdi = _XtGetPerDisplayInput(XtDisplay(widget));
    UNLOCK_PROCESS;
    MakeGrabs(&pwi->keyList, KEYBOARD, pdi);
    MakeGrabs(&pwi->ptrList, POINTER, pdi);

    XtRemoveEventHandler(widget, XtAllEvents, True,
                         RealizeHandler, (XtPointer) pwi);
    pwi->realize_handler_added = FALSE;
}

/***************************************************************************/
/**************************** Global Routines ******************************/
/***************************************************************************/

/*
 * Routine used by an application to set up a passive grab for a key/modifier
 * combination.
 */

static void
GrabKeyOrButton(Widget widget,
                KeyCode keyOrButton,
                Modifiers modifiers,
                Boolean owner_events,
                int pointer_mode,
                int keyboard_mode,
                Mask event_mask,
                Window confine_to,
                Cursor cursor,
                Boolean isKeyboard)
{
    XtServerGrabPtr *passiveListPtr;
    XtServerGrabPtr newGrab;
    XtPerWidgetInput pwi;
    XtPerDisplayInput pdi;

    XtCheckSubclass(widget, coreWidgetClass, "in XtGrabKey or XtGrabButton");
    LOCK_PROCESS;
    pwi = _XtGetPerWidgetInput(widget, TRUE);
    if (isKeyboard)
        passiveListPtr = &pwi->keyList;
    else
        passiveListPtr = &pwi->ptrList;
    pdi = _XtGetPerDisplayInput(XtDisplay(widget));
    UNLOCK_PROCESS;
    newGrab = CreateGrab(widget, owner_events, modifiers,
                         keyOrButton, pointer_mode, keyboard_mode,
                         event_mask, confine_to, cursor, False);
    /*
     *  if the widget is realized then process the entry into the grab
     * list. else if the list is empty (i.e. first time) then add the
     * event handler. then add the raw entry to the list for processing
     * in the handler at realize time.
     */
    if (XtIsRealized(widget))
        MakeGrab(newGrab, passiveListPtr, isKeyboard, pdi, pwi);
    else {
        if (!pwi->realize_handler_added) {
            XtAddEventHandler(widget, StructureNotifyMask, FALSE,
                              RealizeHandler, (XtPointer) pwi);
            pwi->realize_handler_added = TRUE;
        }

        while (*passiveListPtr)
            passiveListPtr = &(*passiveListPtr)->next;
        *passiveListPtr = newGrab;
    }
}

static void
UngrabKeyOrButton(Widget widget,
                  int keyOrButton,
                  Modifiers modifiers,
                  Boolean isKeyboard)
{
    XtServerGrabRec tempGrab;
    XtPerWidgetInput pwi;

    XtCheckSubclass(widget, coreWidgetClass,
                    "in XtUngrabKey or XtUngrabButton");

    /* Build a temporary grab list entry */
    tempGrab.widget = widget;
    tempGrab.modifiers = (unsigned short) modifiers;
    tempGrab.keybut = (KeyCode) keyOrButton;
    tempGrab.hasExt = False;

    LOCK_PROCESS;
    pwi = _XtGetPerWidgetInput(widget, FALSE);
    UNLOCK_PROCESS;
    /*
     * if there is no entry in the context manager then somethings wrong
     */
    if (!pwi) {
        XtAppWarningMsg(XtWidgetToApplicationContext(widget),
                        "invalidGrab", "ungrabKeyOrButton", XtCXtToolkitError,
                        "Attempt to remove nonexistent passive grab",
                        NULL, NULL);
        return;
    }

    if (XtIsRealized(widget)) {
        if (isKeyboard)
            XUngrabKey(widget->core.screen->display,
                       keyOrButton, (unsigned int) modifiers,
                       widget->core.window);
        else
            XUngrabButton(widget->core.screen->display,
                          (unsigned) keyOrButton, (unsigned int) modifiers,
                          widget->core.window);
    }

    /* Delete all entries which are encompassed by the specified grab. */
    DeleteServerGrabFromList(isKeyboard ? &pwi->keyList : &pwi->ptrList,
                             &tempGrab);
}

void
XtGrabKey(Widget widget,
          _XtKeyCode keycode,
          Modifiers modifiers,
          _XtBoolean owner_events,
          int pointer_mode,
          int keyboard_mode)
{
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    GrabKeyOrButton(widget, (KeyCode) keycode, modifiers,
                    (Boolean) owner_events, pointer_mode, keyboard_mode,
                    (Mask) 0, (Window) None, (Cursor) None, KEYBOARD);
    UNLOCK_APP(app);
}

void
XtGrabButton(Widget widget,
             int button,
             Modifiers modifiers,
             _XtBoolean owner_events,
             unsigned int event_mask,
             int pointer_mode,
             int keyboard_mode,
             Window confine_to,
             Cursor cursor)
{
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    GrabKeyOrButton(widget, (KeyCode) button, modifiers, (Boolean) owner_events,
                    pointer_mode, keyboard_mode,
                    (Mask) event_mask, confine_to, cursor, POINTER);
    UNLOCK_APP(app);
}

/*
 * Routine used by an application to clear a passive grab for a key/modifier
 * combination.
 */

void
XtUngrabKey(Widget widget, _XtKeyCode keycode, Modifiers modifiers)
{
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    UngrabKeyOrButton(widget, (int) keycode, modifiers, KEYBOARD);
    UNLOCK_APP(app);
}

void
XtUngrabButton(Widget widget, unsigned int button, Modifiers modifiers)
{
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    UngrabKeyOrButton(widget, (KeyCode) button, modifiers, POINTER);
    UNLOCK_APP(app);
}

/*
 * Active grab of Device. clear any client side grabs so we don't lock
 */
static int
GrabDevice(Widget widget,
           Boolean owner_events,
           int pointer_mode,
           int keyboard_mode,
           Mask event_mask,
           Window confine_to,
           Cursor cursor,
           Time time,
           Boolean isKeyboard)
{
    XtPerDisplayInput pdi;
    int returnVal;

    XtCheckSubclass(widget, coreWidgetClass,
                    "in XtGrabKeyboard or XtGrabPointer");
    if (!XtIsRealized(widget))
        return GrabNotViewable;
    LOCK_PROCESS;
    pdi = _XtGetPerDisplayInput(XtDisplay(widget));
    UNLOCK_PROCESS;
    if (!isKeyboard)
        returnVal = XGrabPointer(XtDisplay(widget), XtWindow(widget),
                                 owner_events, (unsigned) event_mask,
                                 pointer_mode, keyboard_mode,
                                 confine_to, cursor, time);
    else
        returnVal = XGrabKeyboard(XtDisplay(widget), XtWindow(widget),
                                  owner_events, pointer_mode,
                                  keyboard_mode, time);

    if (returnVal == GrabSuccess) {
        XtDevice device;

        device = isKeyboard ? &pdi->keyboard : &pdi->pointer;

        /* fill in the server grab rec */
        device->grab.widget = widget;
        device->grab.modifiers = 0;
        device->grab.keybut = 0;
        XtSetBit(device->grab.ownerEvents, owner_events);
        XtSetBit(device->grab.pointerMode, pointer_mode);
        XtSetBit(device->grab.keyboardMode, keyboard_mode);
        device->grab.hasExt = False;
        device->grabType = XtActiveServerGrab;
        pdi->activatingKey = (KeyCode) 0;
    }
    return returnVal;
}

static void
UngrabDevice(Widget widget, Time time, Boolean isKeyboard)
{
    XtPerDisplayInput pdi;
    XtDevice device;

    LOCK_PROCESS;
    pdi = _XtGetPerDisplayInput(XtDisplay(widget));
    UNLOCK_PROCESS;
    device = isKeyboard ? &pdi->keyboard : &pdi->pointer;

    XtCheckSubclass(widget, coreWidgetClass,
                    "in XtUngrabKeyboard or XtUngrabPointer");

    if (device->grabType != XtNoServerGrab) {

        if (device->grabType != XtPseudoPassiveServerGrab
            && XtIsRealized(widget)) {
            if (isKeyboard)
                XUngrabKeyboard(XtDisplay(widget), time);
            else
                XUngrabPointer(XtDisplay(widget), time);
        }
        device->grabType = XtNoServerGrab;
        pdi->activatingKey = (KeyCode) 0;
    }
}

/*
 * Active grab of keyboard. clear any client side grabs so we don't lock
 */
int
XtGrabKeyboard(Widget widget,
               _XtBoolean owner_events,
               int pointer_mode,
               int keyboard_mode,
               Time time)
{
    int retval;

    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    retval = GrabDevice(widget, (Boolean) owner_events,
                        pointer_mode, keyboard_mode,
                        (Mask) 0, (Window) None, (Cursor) None, time, KEYBOARD);
    UNLOCK_APP(app);
    return retval;
}

/*
 * Ungrab the keyboard
 */

void
XtUngrabKeyboard(Widget widget, Time time)
{
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    UngrabDevice(widget, time, KEYBOARD);
    UNLOCK_APP(app);
}

/*
 * grab the pointer
 */
int
XtGrabPointer(Widget widget,
              _XtBoolean owner_events,
              unsigned int event_mask,
              int pointer_mode,
              int keyboard_mode,
              Window confine_to,
              Cursor cursor,
              Time time)
{
    int retval;

    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    retval = GrabDevice(widget, (Boolean) owner_events,
                        pointer_mode, keyboard_mode,
                        (Mask) event_mask, confine_to, cursor, time, POINTER);
    UNLOCK_APP(app);
    return retval;
}

/*
 * Ungrab the pointer
 */

void
XtUngrabPointer(Widget widget, Time time)
{
    WIDGET_TO_APPCON(widget);

    LOCK_APP(app);
    UngrabDevice(widget, time, POINTER);
    UNLOCK_APP(app);
}

void
_XtRegisterPassiveGrabs(Widget widget)
{
    XtPerWidgetInput pwi = _XtGetPerWidgetInput(widget, FALSE);

    if (pwi != NULL && !pwi->realize_handler_added) {
        XtAddEventHandler(widget, StructureNotifyMask, FALSE,
                          RealizeHandler, (XtPointer) pwi);
        pwi->realize_handler_added = TRUE;
    }
}
