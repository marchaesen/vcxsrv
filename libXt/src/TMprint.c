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
#include <stdio.h>

typedef struct _TMStringBufRec {
    _XtString start;
    _XtString current;
    Cardinal max;
} TMStringBufRec, *TMStringBuf;

#define STR_THRESHOLD 25
#define STR_INCAMOUNT 100

#define CHECK_STR_OVERFLOW(sb) \
    if (sb->current - sb->start > (int)sb->max - STR_THRESHOLD)           \
    {                                                                     \
        _XtString old = sb->start;                                        \
        sb->start = XtRealloc(old, (Cardinal)(sb->max += STR_INCAMOUNT)); \
        sb->current = sb->current - old + sb->start;                      \
    }

#define ExpandForChars(sb, nchars )                                       \
    if ((unsigned)(sb->current - sb->start) > (sb->max - STR_THRESHOLD - (Cardinal) nchars)) { \
        _XtString old = sb->start;                                        \
        sb->start = XtRealloc(old,                                        \
            (Cardinal)(sb->max = (Cardinal)(sb->max + STR_INCAMOUNT + (Cardinal) nchars)));     \
        sb->current = sb->current - old + sb->start;                      \
    }

#define ExpandToFit(sb, more) \
{                                                               \
        size_t l = strlen(more);                                \
        ExpandForChars(sb, l);                                  \
      }

static void
PrintModifiers(TMStringBuf sb, unsigned long mask, unsigned long mod)
{
    Boolean notfirst = False;

    CHECK_STR_OVERFLOW(sb);

    if (mask == ~0UL && mod == 0) {
        *sb->current++ = '!';
        *sb->current = '\0';
        return;
    }

#define PRINTMOD(modmask,modstring)                     \
    if (mask & modmask) {                               \
        if (! (mod & modmask)) {                        \
            *sb->current++ = '~';                       \
            notfirst = True;                            \
        }                                               \
        else if (notfirst)                              \
            *sb->current++ = ' ';                       \
        else notfirst = True;                           \
        strcpy(sb->current, modstring);                 \
        sb->current += strlen(sb->current);             \
    }

    PRINTMOD(ShiftMask, "Shift");
    PRINTMOD(ControlMask, "Ctrl");      /* name is not CtrlMask... */
    PRINTMOD(LockMask, "Lock");
    PRINTMOD(Mod1Mask, "Mod1");
    CHECK_STR_OVERFLOW(sb);
    PRINTMOD(Mod2Mask, "Mod2");
    PRINTMOD(Mod3Mask, "Mod3");
    PRINTMOD(Mod4Mask, "Mod4");
    PRINTMOD(Mod5Mask, "Mod5");
    CHECK_STR_OVERFLOW(sb);
    PRINTMOD(Button1Mask, "Button1");
    PRINTMOD(Button2Mask, "Button2");
    PRINTMOD(Button3Mask, "Button3");
    CHECK_STR_OVERFLOW(sb);
    PRINTMOD(Button4Mask, "Button4");
    PRINTMOD(Button5Mask, "Button5");
    (void) notfirst;

#undef PRINTMOD
}

static void
PrintEventType(TMStringBuf sb, unsigned long event)
{
    CHECK_STR_OVERFLOW(sb);
    switch (event) {
#define PRINTEVENT(event, name) case event: (void) strcpy(sb->current, name); break;
        PRINTEVENT(KeyPress, "<KeyPress>")
            PRINTEVENT(KeyRelease, "<KeyRelease>")
            PRINTEVENT(ButtonPress, "<ButtonPress>")
            PRINTEVENT(ButtonRelease, "<ButtonRelease>")
            PRINTEVENT(MotionNotify, "<MotionNotify>")
            PRINTEVENT(EnterNotify, "<EnterNotify>")
            PRINTEVENT(LeaveNotify, "<LeaveNotify>")
            PRINTEVENT(FocusIn, "<FocusIn>")
            PRINTEVENT(FocusOut, "<FocusOut>")
            PRINTEVENT(KeymapNotify, "<KeymapNotify>")
            PRINTEVENT(Expose, "<Expose>")
            PRINTEVENT(GraphicsExpose, "<GraphicsExpose>")
            PRINTEVENT(NoExpose, "<NoExpose>")
            PRINTEVENT(VisibilityNotify, "<VisibilityNotify>")
            PRINTEVENT(CreateNotify, "<CreateNotify>")
            PRINTEVENT(DestroyNotify, "<DestroyNotify>")
            PRINTEVENT(UnmapNotify, "<UnmapNotify>")
            PRINTEVENT(MapNotify, "<MapNotify>")
            PRINTEVENT(MapRequest, "<MapRequest>")
            PRINTEVENT(ReparentNotify, "<ReparentNotify>")
            PRINTEVENT(ConfigureNotify, "<ConfigureNotify>")
            PRINTEVENT(ConfigureRequest, "<ConfigureRequest>")
            PRINTEVENT(GravityNotify, "<GravityNotify>")
            PRINTEVENT(ResizeRequest, "<ResizeRequest>")
            PRINTEVENT(CirculateNotify, "<CirculateNotify>")
            PRINTEVENT(CirculateRequest, "<CirculateRequest>")
            PRINTEVENT(PropertyNotify, "<PropertyNotify>")
            PRINTEVENT(SelectionClear, "<SelectionClear>")
            PRINTEVENT(SelectionRequest, "<SelectionRequest>")
            PRINTEVENT(SelectionNotify, "<SelectionNotify>")
            PRINTEVENT(ColormapNotify, "<ColormapNotify>")
            PRINTEVENT(ClientMessage, "<ClientMessage>")
    case _XtEventTimerEventType:
        (void) strcpy(sb->current, "<EventTimer>");
        break;
    default:
        (void) sprintf(sb->current, "<0x%x>", (int) event);
#undef PRINTEVENT
    }
    sb->current += strlen(sb->current);
}

static void
PrintCode(TMStringBuf sb, unsigned long mask, unsigned long code)
{
    CHECK_STR_OVERFLOW(sb);
    if (mask != 0) {
        if (mask != ~0UL)
            (void) sprintf(sb->current, "0x%lx:0x%lx", mask, code);
        else
            (void) sprintf(sb->current, /*"0x%lx" */ "%u", (unsigned) code);
        sb->current += strlen(sb->current);
    }
}

static void
PrintKeysym(TMStringBuf sb, KeySym keysym)
{
    String keysymName;

    if (keysym == 0)
        return;

    CHECK_STR_OVERFLOW(sb);
    keysymName = XKeysymToString(keysym);
    if (keysymName == NULL)
        PrintCode(sb, ~0UL, (unsigned long) keysym);
    else {
        ExpandToFit(sb, keysymName);
        strcpy(sb->current, keysymName);
        sb->current += strlen(sb->current);
    }
}

static void
PrintAtom(TMStringBuf sb, Display *dpy, Atom atom)
{
    _XtString atomName;

    if (atom == 0)
        return;

    atomName = (dpy ? XGetAtomName(dpy, atom) : NULL);

    if (!atomName)
        PrintCode(sb, ~0UL, (unsigned long) atom);
    else {
        ExpandToFit(sb, atomName);
        strcpy(sb->current, atomName);
        sb->current += strlen(sb->current);
        XFree(atomName);
    }
}

static void
PrintLateModifiers(TMStringBuf sb, LateBindingsPtr lateModifiers)
{
    for (; lateModifiers->keysym; lateModifiers++) {
        CHECK_STR_OVERFLOW(sb);
        if (lateModifiers->knot) {
            *sb->current++ = '~';
        }
        else {
            *sb->current++ = ' ';
        }
        strcpy(sb->current, XKeysymToString(lateModifiers->keysym));
        sb->current += strlen(sb->current);
        if (lateModifiers->pair) {
            *(sb->current -= 2) = '\0'; /* strip "_L" */
            lateModifiers++;    /* skip _R keysym */
        }
    }
}

static void
PrintEvent(TMStringBuf sb,
           register TMTypeMatch typeMatch,
           register TMModifierMatch modMatch,
           Display *dpy)
{
    if (modMatch->standard)
        *sb->current++ = ':';

    PrintModifiers(sb, modMatch->modifierMask, modMatch->modifiers);
    if (modMatch->lateModifiers != NULL)
        PrintLateModifiers(sb, modMatch->lateModifiers);
    PrintEventType(sb, typeMatch->eventType);
    switch (typeMatch->eventType) {
    case KeyPress:
    case KeyRelease:
        PrintKeysym(sb, (KeySym) typeMatch->eventCode);
        break;

    case PropertyNotify:
    case SelectionClear:
    case SelectionRequest:
    case SelectionNotify:
    case ClientMessage:
        PrintAtom(sb, dpy, (Atom) typeMatch->eventCode);
        break;

    default:
        PrintCode(sb, typeMatch->eventCodeMask, typeMatch->eventCode);
    }
}

static void
PrintParams(TMStringBuf sb, String *params, Cardinal num_params)
{
    register Cardinal i;

    for (i = 0; i < num_params; i++) {
        ExpandToFit(sb, params[i]);
        if (i != 0) {
            *sb->current++ = ',';
            *sb->current++ = ' ';
        }
        *sb->current++ = '"';
        strcpy(sb->current, params[i]);
        sb->current += strlen(sb->current);
        *sb->current++ = '"';
    }
    *sb->current = '\0';
}

static void
PrintActions(TMStringBuf sb,
             register ActionPtr actions,
             XrmQuark *quarkTbl,
             Widget accelWidget)
{
    while (actions != NULL) {
        String proc;

        *sb->current++ = ' ';

        if (accelWidget) {
            /* accelerator */
            String name = XtName(accelWidget);
            int nameLen = (int) strlen(name);

            ExpandForChars(sb, nameLen);
            XtMemmove(sb->current, name, nameLen);
            sb->current += nameLen;
            *sb->current++ = '`';
        }
        proc = XrmQuarkToString(quarkTbl[actions->idx]);
        ExpandToFit(sb, proc);
        strcpy(sb->current, proc);
        sb->current += strlen(proc);
        *sb->current++ = '(';
        PrintParams(sb, actions->params, actions->num_params);
        *sb->current++ = ')';
        actions = actions->next;
    }
    *sb->current = '\0';
}

static Boolean
LookAheadForCycleOrMulticlick(register StatePtr state,
                              StatePtr *state_return, /* state to print, usually startState */
                              int *countP,
                              StatePtr *nextLevelP)
{
    int repeatCount = 0;
    StatePtr startState = state;
    Boolean isCycle = startState->isCycleEnd;
    TMTypeMatch sTypeMatch;
    TMModifierMatch sModMatch;

    LOCK_PROCESS;
    sTypeMatch = TMGetTypeMatch(startState->typeIndex);
    sModMatch = TMGetModifierMatch(startState->modIndex);

    *state_return = startState;

    for (state = state->nextLevel; state != NULL; state = state->nextLevel) {
        TMTypeMatch typeMatch = TMGetTypeMatch(state->typeIndex);
        TMModifierMatch modMatch = TMGetModifierMatch(state->modIndex);

        /* try to pick up the correct state with actions, to be printed */
        /* This is to accommodate <ButtonUp>(2+), for example */
        if (state->isCycleStart)
            *state_return = state;

        if (state->isCycleEnd) {
            *countP = repeatCount;
            UNLOCK_PROCESS;
            return True;
        }
        if ((startState->typeIndex == state->typeIndex) &&
            (startState->modIndex == state->modIndex)) {
            repeatCount++;
            *nextLevelP = state;
        }
        else if (typeMatch->eventType == _XtEventTimerEventType)
            continue;
        else {                  /* not same event as starting event and not timer */

            unsigned int type = (unsigned) sTypeMatch->eventType;
            unsigned int t = (unsigned) typeMatch->eventType;

            if ((type == ButtonPress && t != ButtonRelease)
                || (type == ButtonRelease && t != ButtonPress)
                || (type == KeyPress && t != KeyRelease)
                || (type == KeyRelease && t != KeyPress)
                || typeMatch->eventCode != sTypeMatch->eventCode
                || modMatch->modifiers != sModMatch->modifiers
                || modMatch->modifierMask != sModMatch->modifierMask
                || modMatch->lateModifiers != sModMatch->lateModifiers
                || typeMatch->eventCodeMask != sTypeMatch->eventCodeMask
                || typeMatch->matchEvent != sTypeMatch->matchEvent
                || modMatch->standard != sModMatch->standard)
                /* not inverse of starting event, either */
                break;
        }
    }
    *countP = repeatCount;
    UNLOCK_PROCESS;
    return isCycle;
}

static void
PrintComplexState(TMStringBuf sb,
                  Boolean includeRHS,
                  StatePtr state,
                  TMStateTree stateTree,
                  Widget accelWidget,
                  Display *dpy)
{
    int clickCount = 0;
    Boolean cycle;
    StatePtr nextLevel = NULL;
    StatePtr triggerState = NULL;

    /* print the current state */
    if (!state)
        return;
    LOCK_PROCESS;
    cycle = LookAheadForCycleOrMulticlick(state, &triggerState, &clickCount,
                                          &nextLevel);

    PrintEvent(sb, TMGetTypeMatch(triggerState->typeIndex),
               TMGetModifierMatch(triggerState->modIndex), dpy);

    if (cycle || clickCount) {
        if (clickCount)
            sprintf(sb->current, "(%d%s)", clickCount + 1, cycle ? "+" : "");
        else
            (void) strncpy(sb->current, "(+)", 4);
        sb->current += strlen(sb->current);
        if (!state->actions && nextLevel)
            state = nextLevel;
        while (!state->actions && !state->isCycleEnd)
            state = state->nextLevel;   /* should be trigger state */
    }

    if (state->actions) {
        if (includeRHS) {
            CHECK_STR_OVERFLOW(sb);
            *sb->current++ = ':';
            PrintActions(sb,
                         state->actions,
                         ((TMSimpleStateTree) stateTree)->quarkTbl,
                         accelWidget);
            *sb->current++ = '\n';
        }
    }
    else {
        if (state->nextLevel && !cycle && !clickCount)
            *sb->current++ = ',';
        else {
            /* no actions are attached to this production */
            *sb->current++ = ':';
            *sb->current++ = '\n';
        }
    }
    *sb->current = '\0';

    /* print succeeding states */
    if (state->nextLevel && !cycle && !clickCount)
        PrintComplexState(sb, includeRHS, state->nextLevel,
                          stateTree, accelWidget, dpy);
    UNLOCK_PROCESS;
}

typedef struct {
    TMShortCard tIndex;
    TMShortCard bIndex;
} PrintRec, *Print;

static int
FindNextMatch(PrintRec *printData,
              TMShortCard numPrints,
              XtTranslations xlations,
              TMBranchHead branchHead,
              StatePtr nextLevel,
              TMShortCard startIndex)
{
    TMShortCard i;
    StatePtr currState, candState;
    Boolean noMatch = True;

    for (i = startIndex; noMatch && i < numPrints; i++) {
        TMBranchHead prBranchHead;
        TMComplexStateTree stateTree;

        stateTree = (TMComplexStateTree)
            xlations->stateTreeTbl[printData[i].tIndex];
        prBranchHead = &(stateTree->branchHeadTbl[printData[i].bIndex]);

        if ((prBranchHead->typeIndex == branchHead->typeIndex) &&
            (prBranchHead->modIndex == branchHead->modIndex)) {
            if (prBranchHead->isSimple) {
                if (!nextLevel)
                    return i;
            }
            else {
                currState = TMComplexBranchHead(stateTree, prBranchHead);
                currState = currState->nextLevel;
                candState = nextLevel;
                for (;
                     ((currState && !currState->isCycleEnd) &&
                      (candState && !candState->isCycleEnd));
                     currState = currState->nextLevel,
                     candState = candState->nextLevel) {
                    if ((currState->typeIndex != candState->typeIndex) ||
                        (currState->modIndex != candState->modIndex))
                        break;
                }
                if (candState == currState) {
                    return i;
                }
            }
        }
    }
    return TM_NO_MATCH;
}

static void
ProcessLaterMatches(PrintRec *printData,
                    XtTranslations xlations,
                    TMShortCard tIndex,
                    int bIndex,
                    TMShortCard *numPrintsRtn)
{
    TMComplexStateTree stateTree;
    int i, j;
    TMBranchHead branchHead, matchBranch = NULL;

    for (i = tIndex; i < (int) xlations->numStateTrees; i++) {
        stateTree = (TMComplexStateTree) xlations->stateTreeTbl[i];
        if (i == tIndex) {
            matchBranch = &stateTree->branchHeadTbl[bIndex];
            j = bIndex + 1;
        }
        else
            j = 0;
        for (branchHead = &stateTree->branchHeadTbl[j];
             j < (int) stateTree->numBranchHeads; j++, branchHead++) {
            if ((branchHead->typeIndex == matchBranch->typeIndex) &&
                (branchHead->modIndex == matchBranch->modIndex)) {
                StatePtr state;

                if (!branchHead->isSimple)
                    state = TMComplexBranchHead(stateTree, branchHead);
                else
                    state = NULL;
                if ((!branchHead->isSimple || branchHead->hasActions) &&
                    (FindNextMatch(printData,
                                   *numPrintsRtn,
                                   xlations,
                                   branchHead,
                                   (state ? state->nextLevel : NULL),
                                   0) == TM_NO_MATCH)) {
                    printData[*numPrintsRtn].tIndex = (TMShortCard) i;
                    printData[*numPrintsRtn].bIndex = (TMShortCard) j;
                    (*numPrintsRtn)++;
                }
            }
        }
    }
}

static void
ProcessStateTree(PrintRec *printData,
                 XtTranslations xlations,
                 TMShortCard tIndex,
                 TMShortCard *numPrintsRtn)
{
    TMComplexStateTree stateTree;
    int i;
    TMBranchHead branchHead;

    stateTree = (TMComplexStateTree) xlations->stateTreeTbl[tIndex];

    for (i = 0, branchHead = stateTree->branchHeadTbl;
         i < (int) stateTree->numBranchHeads; i++, branchHead++) {
        StatePtr state;

        if (!branchHead->isSimple)
            state = TMComplexBranchHead(stateTree, branchHead);
        else
            state = NULL;
        if (FindNextMatch(printData, *numPrintsRtn, xlations, branchHead,
                          (state ? state->nextLevel : NULL), 0)
            == TM_NO_MATCH) {
            if (!branchHead->isSimple || branchHead->hasActions) {
                printData[*numPrintsRtn].tIndex = tIndex;
                printData[*numPrintsRtn].bIndex = (TMShortCard) i;
                (*numPrintsRtn)++;
            }
            LOCK_PROCESS;
            if (_XtGlobalTM.newMatchSemantics == False)
                ProcessLaterMatches(printData,
                                    xlations, tIndex, i, numPrintsRtn);
            UNLOCK_PROCESS;
        }
    }
}

static void
PrintState(TMStringBuf sb,
           TMStateTree tree,
           TMBranchHead branchHead,
           Boolean includeRHS,
           Widget accelWidget,
           Display *dpy)
{
    TMComplexStateTree stateTree = (TMComplexStateTree) tree;

    LOCK_PROCESS;
    if (branchHead->isSimple) {
        PrintEvent(sb,
                   TMGetTypeMatch(branchHead->typeIndex),
                   TMGetModifierMatch(branchHead->modIndex), dpy);
        if (includeRHS) {
            ActionRec actRec;

            CHECK_STR_OVERFLOW(sb);
            *sb->current++ = ':';
            actRec.idx = TMBranchMore(branchHead);
            actRec.num_params = 0;
            actRec.params = NULL;
            actRec.next = NULL;
            PrintActions(sb, &actRec, stateTree->quarkTbl, accelWidget);
            *sb->current++ = '\n';
        }
        else
            *sb->current++ = ',';
#ifdef TRACE_TM
        if (!branchHead->hasActions)
            printf(" !! no actions !! ");
#endif
    }
    else {                      /* it's a complex branchHead */
        StatePtr state = TMComplexBranchHead(stateTree, branchHead);

        PrintComplexState(sb,
                          includeRHS,
                          state, tree, accelWidget, (Display *) NULL);
    }
    *sb->current = '\0';
    UNLOCK_PROCESS;
}

_XtString
_XtPrintXlations(Widget w,
                 XtTranslations xlations,
                 Widget accelWidget,
                 _XtBoolean includeRHS)
{
    register Cardinal i;

#define STACKPRINTSIZE 250
    PrintRec stackPrints[STACKPRINTSIZE];
    PrintRec *prints;
    TMStringBufRec sbRec, *sb = &sbRec;
    TMShortCard numPrints, maxPrints;

#ifdef TRACE_TM
    TMBindData bindData = (TMBindData) w->core.tm.proc_table;
    Boolean hasAccel = (accelWidget ? True : False);
#endif                          /* TRACE_TM */
    if (xlations == NULL)
        return NULL;

    sb->current = sb->start = __XtMalloc((Cardinal) 1000);
    sb->max = 1000;
    maxPrints = 0;
    for (i = 0; i < xlations->numStateTrees; i++)
        maxPrints = (TMShortCard) (maxPrints +
                                   ((TMSimpleStateTree)
                                    (xlations->stateTreeTbl[i]))->
                                   numBranchHeads);
    prints = (PrintRec *)
        XtStackAlloc(maxPrints * sizeof(PrintRec), stackPrints);

    numPrints = 0;
    for (i = 0; i < xlations->numStateTrees; i++)
        ProcessStateTree(prints, xlations, (TMShortCard) i, &numPrints);

    for (i = 0; i < numPrints; i++) {
        TMSimpleStateTree stateTree = (TMSimpleStateTree)
            xlations->stateTreeTbl[prints[i].tIndex];
        TMBranchHead branchHead = &stateTree->branchHeadTbl[prints[i].bIndex];

#ifdef TRACE_TM
        TMComplexBindProcs complexBindProcs;

        if (hasAccel == False) {
            accelWidget = NULL;
            if (bindData->simple.isComplex) {
                complexBindProcs = TMGetComplexBindEntry(bindData, 0);
                accelWidget = complexBindProcs[prints[i].tIndex].widget;
            }
        }
#endif                          /* TRACE_TM */
        PrintState(sb, (TMStateTree) stateTree, branchHead,
                   (Boolean) includeRHS, accelWidget, XtDisplay(w));
    }
    XtStackFree((XtPointer) prints, (XtPointer) stackPrints);
    return (sb->start);
}

#ifndef NO_MIT_HACKS
void
_XtDisplayTranslations(Widget widget,
                       XEvent *event _X_UNUSED,
                       String *params _X_UNUSED,
                       Cardinal *num_params _X_UNUSED)
{
    _XtString xString;

    xString = _XtPrintXlations(widget,
                               widget->core.tm.translations, NULL, True);
    if (xString) {
        printf("%s\n", xString);
        XtFree(xString);
    }
}

void
_XtDisplayAccelerators(Widget widget,
                       XEvent *event _X_UNUSED,
                       String *params _X_UNUSED,
                       Cardinal *num_params _X_UNUSED)
{
    _XtString xString;

    xString = _XtPrintXlations(widget, widget->core.accelerators, NULL, True);
    if (xString) {
        printf("%s\n", xString);
        XtFree(xString);
    }
}

void
_XtDisplayInstalledAccelerators(Widget widget,
                                XEvent *event,
                                String *params _X_UNUSED,
                                Cardinal *num_params _X_UNUSED)
{
    Widget eventWidget
        = XtWindowToWidget(event->xany.display, event->xany.window);
    register Cardinal i;
    TMStringBufRec sbRec, *sb = &sbRec;
    XtTranslations xlations;

#define STACKPRINTSIZE 250
    PrintRec stackPrints[STACKPRINTSIZE];
    PrintRec *prints;
    TMShortCard numPrints, maxPrints;
    TMBindData bindData;
    TMComplexBindProcs complexBindProcs;

    if ((eventWidget == NULL) || (eventWidget->core.tm.translations == NULL))
        return;

    xlations = eventWidget->core.tm.translations;
    bindData = (TMBindData) eventWidget->core.tm.proc_table;
    if (bindData->simple.isComplex == False)
        return;

    sb->current = sb->start = __XtMalloc((Cardinal) 1000);
    sb->start[0] = '\0';
    sb->max = 1000;
    maxPrints = 0;
    for (i = 0; i < xlations->numStateTrees; i++)
        maxPrints = (TMShortCard) (maxPrints +
                                   ((TMSimpleStateTree) xlations->
                                    stateTreeTbl[i])->numBranchHeads);
    prints = (PrintRec *)
        XtStackAlloc(maxPrints * sizeof(PrintRec), stackPrints);

    numPrints = 0;

    complexBindProcs = TMGetComplexBindEntry(bindData, 0);
    for (i = 0; i < xlations->numStateTrees; i++, complexBindProcs++) {
        if (complexBindProcs->widget) {
            ProcessStateTree(prints, xlations, (TMShortCard) i, &numPrints);
        }
    }
    for (i = 0; i < numPrints; i++) {
        TMSimpleStateTree stateTree = (TMSimpleStateTree)
            xlations->stateTreeTbl[prints[i].tIndex];
        TMBranchHead branchHead = &stateTree->branchHeadTbl[prints[i].bIndex];

        complexBindProcs = TMGetComplexBindEntry(bindData, 0);

        PrintState(sb, (TMStateTree) stateTree, branchHead, True,
                   complexBindProcs[prints[i].tIndex].widget,
                   XtDisplay(widget));
    }
    XtStackFree((XtPointer) prints, (XtPointer) stackPrints);
    printf("%s\n", sb->start);
    XtFree(sb->start);
}
#endif                          /*NO_MIT_HACKS */

String
_XtPrintActions(register ActionRec *actions, XrmQuark *quarkTbl)
{
    TMStringBufRec sbRec, *sb = &sbRec;

    sb->max = 1000;
    sb->current = sb->start = __XtMalloc((Cardinal) 1000);
    PrintActions(sb, actions, quarkTbl, (Widget) NULL);
    return sb->start;
}

String
_XtPrintState(TMStateTree stateTree, TMBranchHead branchHead)
{
    TMStringBufRec sbRec, *sb = &sbRec;

    sb->current = sb->start = __XtMalloc((Cardinal) 1000);
    sb->max = 1000;
    PrintState(sb, stateTree, branchHead,
               True, (Widget) NULL, (Display *) NULL);
    return sb->start;
}

String
_XtPrintEventSeq(register EventSeqPtr eventSeq, Display *dpy)
{
    TMStringBufRec sbRec, *sb = &sbRec;

#define MAXSEQS 100
    EventSeqPtr eventSeqs[MAXSEQS];
    TMShortCard i, j;
    Boolean cycle = False;

    sb->current = sb->start = __XtMalloc((Cardinal) 1000);
    sb->max = 1000;
    for (i = 0;
         i < MAXSEQS && eventSeq != NULL && !cycle;
         eventSeq = eventSeq->next, i++) {
        eventSeqs[i] = eventSeq;
        for (j = 0; j < i && !cycle; j++)
            if (eventSeqs[j] == eventSeq)
                cycle = True;
    }
    LOCK_PROCESS;
    for (j = 0; j < i; j++) {
        TMTypeMatch typeMatch;
        TMModifierMatch modMatch;

        typeMatch = TMGetTypeMatch(_XtGetTypeIndex(&eventSeqs[j]->event));
        modMatch =
            TMGetModifierMatch(_XtGetModifierIndex(&eventSeqs[j]->event));
        PrintEvent(sb, typeMatch, modMatch, dpy);
        *sb->current++ = ',';
    }
    UNLOCK_PROCESS;
    return sb->start;
}
