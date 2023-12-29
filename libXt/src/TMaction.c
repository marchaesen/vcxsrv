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

/* TMaction.c -- maintains the state table of actions for the translation
 *              manager.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "IntrinsicI.h"
#include "StringDefs.h"

#if defined(__STDC__) && !defined(NORCONST)
#define RConst const
#else
#define RConst /**/
#endif
static _Xconst _XtString XtNtranslationError = "translationError";

typedef struct _CompiledAction {
    XrmQuark signature;
    XtActionProc proc;
} CompiledAction, *CompiledActionTable;

#define GetClassActions(wc) \
      ((wc->core_class.actions) \
       ? (((TMClassCache)wc->core_class.actions)->actions) \
       : NULL)

static CompiledActionTable
CompileActionTable(register RConst struct _XtActionsRec *actions, register Cardinal count,      /* may be 0 */
                   Boolean stat,        /* if False, copy before compiling in place */
                   Boolean perm)        /* if False, use XrmStringToQuark */
{
    register CompiledActionTable cActions;
    register int i;
    CompiledActionTable cTableHold;
    XrmQuark (*func) (_Xconst char *);

    if (!count)
        return (CompiledActionTable) NULL;
    func = (perm ? XrmPermStringToQuark : XrmStringToQuark);

    if (!stat) {
        cTableHold = cActions = XtMallocArray(count,
                                              (Cardinal) sizeof(CompiledAction));

        for (i = (int) count; --i >= 0; cActions++, actions++) {
            cActions->proc = actions->proc;
            cActions->signature = (*func) (actions->string);
        }
    }
    else {
        cTableHold = (CompiledActionTable) actions;

        for (i = (int) count; --i >= 0; actions++)
            ((CompiledActionTable) actions)->signature =
                (*func) (actions->string);
    }
    cActions = cTableHold;

    /* Insertion sort.  Whatever sort is used, it must be stable. */
    for (i = 1; (Cardinal) i <= count - 1; i++) {
        CompiledAction hold;
        register Cardinal j;

        hold = cActions[i];
        j = (Cardinal) i;
        while (j && cActions[j - 1].signature > hold.signature) {
            cActions[j] = cActions[j - 1];
            j--;
        }
        cActions[j] = hold;
    }

    return cActions;
}

typedef struct _ActionListRec *ActionList;
typedef struct _ActionListRec {
    ActionList next;
    CompiledActionTable table;
    TMShortCard count;
} ActionListRec;

static void
ReportUnboundActions(XtTranslations xlations, TMBindData bindData)
{
    TMSimpleStateTree stateTree;
    Cardinal num_unbound = 0;
    Cardinal num_params = 1;
    char *message;
    char messagebuf[1000];
    register Cardinal num_chars = 0;
    register Cardinal i, j;
    XtActionProc *procs;

    for (i = 0; i < xlations->numStateTrees; i++) {
        if (bindData->simple.isComplex)
            procs = TMGetComplexBindEntry(bindData, i)->procs;
        else
            procs = TMGetSimpleBindEntry(bindData, i)->procs;

        stateTree = (TMSimpleStateTree) xlations->stateTreeTbl[i];
        for (j = 0; j < stateTree->numQuarks; j++) {
            if (procs[j] == NULL) {
                String s = XrmQuarkToString(stateTree->quarkTbl[j]);

                if (num_unbound != 0)
                    num_chars += 2;
                num_chars += (Cardinal) strlen(s);
                num_unbound++;
            }
        }
    }
    if (num_unbound == 0)
        return;
    message = XtStackAlloc(num_chars + 1, messagebuf);
    if (message != NULL) {
        String params[1];

        *message = '\0';
        num_unbound = 0;
        for (i = 0; i < xlations->numStateTrees; i++) {
            if (bindData->simple.isComplex)
                procs = TMGetComplexBindEntry(bindData, i)->procs;
            else
                procs = TMGetSimpleBindEntry(bindData, i)->procs;

            stateTree = (TMSimpleStateTree) xlations->stateTreeTbl[i];
            for (j = 0; j < stateTree->numQuarks; j++) {
                if (procs[j] == NULL) {
                    String s = XrmQuarkToString(stateTree->quarkTbl[j]);

                    if (num_unbound != 0)
                        (void) strcat(message, ", ");
                    (void) strcat(message, s);
                    num_unbound++;
                }
            }
        }
        message[num_chars] = '\0';
        params[0] = message;
        XtWarningMsg(XtNtranslationError, "unboundActions", XtCXtToolkitError,
                     "Actions not found: %s", params, &num_params);
        XtStackFree(message, messagebuf);
    }
}

static CompiledAction *
SearchActionTable(XrmQuark signature,
                  CompiledActionTable actionTable,
                  Cardinal numActions)
{
    int left, right;

    left = 0;
    right = (int) numActions - 1;
    while (left <= right) {
        int i = (left + right) >> 1;

        if (signature < actionTable[i].signature)
            right = i - 1;
        else if (signature > actionTable[i].signature)
            left = i + 1;
        else {
            while (i && actionTable[i - 1].signature == signature)
                i--;
            return &actionTable[i];
        }
    }
    return (CompiledAction *) NULL;
}

static int
BindActions(TMSimpleStateTree stateTree,
            XtActionProc *procs,
            CompiledActionTable compiledActionTable,
            TMShortCard numActions,
            Cardinal *ndxP)
{
    register int unbound = (int) (stateTree->numQuarks - *ndxP);
    CompiledAction *action;
    register Cardinal ndx;
    register Boolean savedNdx = False;

    for (ndx = *ndxP; ndx < stateTree->numQuarks; ndx++) {
        if (procs[ndx] == NULL) {
            /* attempt to bind it */
            XrmQuark q = stateTree->quarkTbl[ndx];

            action = SearchActionTable(q, compiledActionTable, numActions);
            if (action) {
                procs[ndx] = action->proc;
                unbound--;
            }
            else if (!savedNdx) {
                *ndxP = ndx;
                savedNdx = True;
            }
        }
        else {
            /* already bound, leave it alone */
            unbound--;
        }
    }
    return unbound;
}

typedef struct _TMBindCacheStatusRec {
    unsigned int boundInClass:1;
    unsigned int boundInHierarchy:1;
    unsigned int boundInContext:1;
    unsigned int notFullyBound:1;
    unsigned int refCount:28;
} TMBindCacheStatusRec, *TMBindCacheStatus;

typedef struct _TMBindCacheRec {
    struct _TMBindCacheRec *next;
    TMBindCacheStatusRec status;
    TMStateTree stateTree;
#ifdef TRACE_TM
    WidgetClass widgetClass;
#endif                          /* TRACE_TM */
    XtActionProc procs[1];      /* variable length */
} TMBindCacheRec, *TMBindCache;

typedef struct _TMClassCacheRec {
    CompiledActionTable actions;
    TMBindCacheRec *bindCache;
} TMClassCacheRec, *TMClassCache;

#define IsPureClassBind(bc) \
  (bc->status.boundInClass && \
   !(bc->status.boundInHierarchy || \
     bc->status.boundInContext || \
     bc->status.notFullyBound))

#define GetClassCache(w) \
  ((TMClassCache)w->core.widget_class->core_class.actions)

static int
BindProcs(Widget widget,
          TMSimpleStateTree stateTree,
          XtActionProc *procs,
          TMBindCacheStatus bindStatus)
{
    register WidgetClass class;
    register ActionList actionList;
    int unbound = -1, newUnbound = -1;
    Cardinal ndx = 0;
    Widget w = widget;

    LOCK_PROCESS;
    do {
        class = w->core.widget_class;
        do {
            if (class->core_class.actions != NULL)
                unbound =
                    BindActions(stateTree,
                                procs,
                                GetClassActions(class),
                                (TMShortCard) class->core_class.num_actions,
                                &ndx);
            class = class->core_class.superclass;
        } while (unbound != 0 && class != NULL);
        if (unbound < (int) stateTree->numQuarks)
            bindStatus->boundInClass = True;
        else
            bindStatus->boundInClass = False;
        if (newUnbound == -1)
            newUnbound = unbound;
        w = XtParent(w);
    } while (unbound != 0 && w != NULL);

    if (newUnbound > unbound)
        bindStatus->boundInHierarchy = True;
    else
        bindStatus->boundInHierarchy = False;

    if (unbound) {
        XtAppContext app = XtWidgetToApplicationContext(widget);

        newUnbound = unbound;
        for (actionList = app->action_table;
             unbound != 0 && actionList != NULL;
             actionList = actionList->next) {
            unbound = BindActions(stateTree,
                                  procs,
                                  actionList->table, actionList->count, &ndx);
        }
        if (newUnbound > unbound)
            bindStatus->boundInContext = True;
        else
            bindStatus->boundInContext = False;

    }
    else {
        bindStatus->boundInContext = False;
    }
    UNLOCK_PROCESS;
    return unbound;
}

static XtActionProc *
TryBindCache(Widget widget, TMStateTree stateTree)
{
    TMClassCache classCache;

    LOCK_PROCESS;
    classCache = GetClassCache(widget);

    if (classCache == NULL) {
        WidgetClass wc = XtClass(widget);

        wc->core_class.actions = (XtActionList)
            _XtInitializeActionData(NULL, 0, True);
    }
    else {
        TMBindCache bindCache = (TMBindCache) (classCache->bindCache);

        for (; bindCache; bindCache = bindCache->next) {
            if (IsPureClassBind(bindCache) &&
                (stateTree == bindCache->stateTree)) {
                bindCache->status.refCount++;
                UNLOCK_PROCESS;
                return &bindCache->procs[0];
            }
        }
    }
    UNLOCK_PROCESS;
    return NULL;
}

/*
 * The class record actions field will point to the bind cache header
 * after this call is made out of coreClassPartInit.
 */
XtPointer
_XtInitializeActionData(register struct _XtActionsRec *actions,
                        register Cardinal count,
                        _XtBoolean inPlace)
{
    TMClassCache classCache;

    classCache = XtNew(TMClassCacheRec);
    classCache->actions =
        CompileActionTable(actions, count, (Boolean) inPlace, True);
    classCache->bindCache = NULL;
    return (XtPointer) classCache;
}

#define TM_BIND_CACHE_REALLOC   2

static XtActionProc *
EnterBindCache(Widget w,
               TMSimpleStateTree stateTree,
               XtActionProc *procs,
               TMBindCacheStatus bindStatus)
{
    TMClassCache classCache;
    TMBindCache *bindCachePtr;
    TMShortCard procsSize;
    TMBindCache bindCache;

    LOCK_PROCESS;
    classCache = GetClassCache(w);
    bindCachePtr = &classCache->bindCache;
    procsSize = (TMShortCard) (stateTree->numQuarks * sizeof(XtActionProc));

    for (bindCache = *bindCachePtr;
         (*bindCachePtr);
         bindCachePtr = &(*bindCachePtr)->next, bindCache = *bindCachePtr) {
        TMBindCacheStatus cacheStatus = &bindCache->status;

        if ((bindStatus->boundInClass == cacheStatus->boundInClass) &&
            (bindStatus->boundInHierarchy == cacheStatus->boundInHierarchy) &&
            (bindStatus->boundInContext == cacheStatus->boundInContext) &&
            (bindCache->stateTree == (TMStateTree) stateTree) &&
            !XtMemcmp(&bindCache->procs[0], procs, procsSize)) {
            bindCache->status.refCount++;
            break;
        }
    }
    if (*bindCachePtr == NULL) {
        *bindCachePtr = bindCache = (TMBindCache)
            __XtMalloc((Cardinal) (sizeof(TMBindCacheRec) +
                                   (size_t) (procsSize -
                                             sizeof(XtActionProc))));
        bindCache->next = NULL;
        bindCache->status = *bindStatus;
        bindCache->status.refCount = 1;
        bindCache->stateTree = (TMStateTree) stateTree;
#ifdef TRACE_TM
        bindCache->widgetClass = XtClass(w);
        if (_XtGlobalTM.numBindCache == _XtGlobalTM.bindCacheTblSize) {
            _XtGlobalTM.bindCacheTblSize =
                (TMShortCard) (_XtGlobalTM.bindCacheTblSize + 16);
            _XtGlobalTM.bindCacheTbl =
                XtReallocArray(_XtGlobalTM.bindCacheTbl,
                               (Cardinal) _XtGlobalTM.bindCacheTblSize,
                               (Cardinal) sizeof(TMBindCache));
        }
        _XtGlobalTM.bindCacheTbl[_XtGlobalTM.numBindCache++] = bindCache;
#endif                          /* TRACE_TM */
        memcpy(&bindCache->procs[0], procs, procsSize);
    }
    UNLOCK_PROCESS;
    return &bindCache->procs[0];
}

static void
RemoveFromBindCache(Widget w, XtActionProc *procs)
{
    TMClassCache classCache;
    TMBindCache *bindCachePtr;
    TMBindCache bindCache;
    XtAppContext app = XtWidgetToApplicationContext(w);

    LOCK_PROCESS;
    classCache = GetClassCache(w);
    bindCachePtr = (TMBindCache *) &classCache->bindCache;

    for (bindCache = *bindCachePtr;
         *bindCachePtr;
         bindCachePtr = &(*bindCachePtr)->next, bindCache = *bindCachePtr) {
        if (&bindCache->procs[0] == procs) {
            if (--bindCache->status.refCount == 0) {
#ifdef TRACE_TM
                TMShortCard j;
                Boolean found = False;
                TMBindCache *tbl = _XtGlobalTM.bindCacheTbl;

                for (j = 0; j < _XtGlobalTM.numBindCache; j++) {
                    if (found)
                        tbl[j - 1] = tbl[j];
                    if (tbl[j] == bindCache)
                        found = True;
                }
                if (!found)
                    XtWarning("where's the action ??? ");
                else
                    _XtGlobalTM.numBindCache--;
#endif                          /* TRACE_TM */
                *bindCachePtr = bindCache->next;
                bindCache->next = app->free_bindings;
                app->free_bindings = bindCache;
            }
            break;
        }
    }
    UNLOCK_PROCESS;
}

static void
RemoveAccelerators(Widget widget, XtPointer closure, XtPointer data _X_UNUSED)
{
    Widget destination = (Widget) closure;
    TMComplexBindProcs bindProcs;
    XtTranslations stackXlations[16];
    XtTranslations *xlationsList, destXlations;
    TMShortCard i, numXlations = 0;

    if ((destXlations = destination->core.tm.translations) == NULL) {
        XtAppWarningMsg(XtWidgetToApplicationContext(widget),
                        XtNtranslationError, "nullTable", XtCXtToolkitError,
                        "Can't remove accelerators from NULL table",
                        NULL, NULL);
        return;
    }

    xlationsList = (XtTranslations *)
        XtStackAlloc((destXlations->numStateTrees * sizeof(XtTranslations)),
                     stackXlations);

    for (i = 0, bindProcs =
         TMGetComplexBindEntry(destination->core.tm.proc_table, i);
         i < destXlations->numStateTrees; i++, bindProcs++) {
        if (bindProcs->widget == widget) {
            /*
             * if it's being destroyed don't do all the work
             */
            if (destination->core.being_destroyed) {
                bindProcs->procs = NULL;
            }
            else
                xlationsList[numXlations] = bindProcs->aXlations;
            numXlations++;
        }
    }

    if (numXlations == 0)
        XtAppWarningMsg(XtWidgetToApplicationContext(widget),
                        XtNtranslationError, "nullTable", XtCXtToolkitError,
                        "Tried to remove nonexistent accelerators", NULL, NULL);
    else {
        if (!destination->core.being_destroyed)
            for (i = 0; i < numXlations; i++)
                _XtUnmergeTranslations(destination, xlationsList[i]);
    }
    XtStackFree((char *) xlationsList, stackXlations);
}

void
_XtBindActions(Widget widget, XtTM tm)
{
    XtTranslations xlations = tm->translations;
    int globalUnbound = 0;
    Cardinal i;
    TMBindData bindData = (TMBindData) tm->proc_table;
    TMSimpleBindProcs simpleBindProcs = NULL;
    TMComplexBindProcs complexBindProcs = NULL;
    XtActionProc *newProcs;
    Widget bindWidget;

    if ((xlations == NULL) || widget->core.being_destroyed)
        return;

    for (i = 0; i < xlations->numStateTrees; i++) {
        TMSimpleStateTree stateTree;

        stateTree = (TMSimpleStateTree) xlations->stateTreeTbl[i];
        if (bindData->simple.isComplex) {
            complexBindProcs = TMGetComplexBindEntry(bindData, i);
            if (complexBindProcs->widget) {
                bindWidget = complexBindProcs->widget;

                if (bindWidget->core.destroy_callbacks != NULL)
                    _XtAddCallbackOnce((InternalCallbackList *)
                                       &bindWidget->core.destroy_callbacks,
                                       RemoveAccelerators, (XtPointer) widget);
                else
                    _XtAddCallback((InternalCallbackList *)
                                   &bindWidget->core.destroy_callbacks,
                                   RemoveAccelerators, (XtPointer) widget);
            }
            else
                bindWidget = widget;
        }
        else {
            simpleBindProcs = TMGetSimpleBindEntry(bindData, i);
            bindWidget = widget;
        }
        if ((newProcs =
             TryBindCache(bindWidget, (TMStateTree) stateTree)) == NULL) {
            XtActionProc *procs, stackProcs[256];
            int localUnbound;
            TMBindCacheStatusRec bcStatusRec;

            procs = (XtActionProc *)
                XtStackAlloc(stateTree->numQuarks * sizeof(XtActionProc),
                             stackProcs);
            XtBZero((XtPointer) procs,
                    stateTree->numQuarks * sizeof(XtActionProc));

            localUnbound = BindProcs(bindWidget,
                                     stateTree, procs, &bcStatusRec);

            if (localUnbound)
                bcStatusRec.notFullyBound = True;
            else
                bcStatusRec.notFullyBound = False;

            newProcs =
                EnterBindCache(bindWidget, stateTree, procs, &bcStatusRec);
            XtStackFree((XtPointer) procs, (XtPointer) stackProcs);
            globalUnbound += localUnbound;
        }
        if (bindData->simple.isComplex)
            complexBindProcs->procs = newProcs;
        else
            simpleBindProcs->procs = newProcs;
    }
    if (globalUnbound)
        ReportUnboundActions(xlations, (TMBindData) tm->proc_table);
}

void
_XtUnbindActions(Widget widget, XtTranslations xlations, TMBindData bindData)
{
    Cardinal i;
    Widget bindWidget;
    XtActionProc *procs;

    if ((xlations == NULL) || !XtIsRealized(widget))
        return;

    for (i = 0; i < xlations->numStateTrees; i++) {
        if (bindData->simple.isComplex) {
            TMComplexBindProcs complexBindProcs;

            complexBindProcs = TMGetComplexBindEntry(bindData, i);

            if (complexBindProcs->widget) {
                /*
                 * check for this being an accelerator binding whose
                 * source is gone ( set by RemoveAccelerators)
                 */
                if (complexBindProcs->procs == NULL)
                    continue;

                XtRemoveCallback(complexBindProcs->widget,
                                 XtNdestroyCallback,
                                 RemoveAccelerators, (XtPointer) widget);
                bindWidget = complexBindProcs->widget;
            }
            else
                bindWidget = widget;
            procs = complexBindProcs->procs;
            complexBindProcs->procs = NULL;
        }
        else {
            TMSimpleBindProcs simpleBindProcs;

            simpleBindProcs = TMGetSimpleBindEntry(bindData, i);
            procs = simpleBindProcs->procs;
            simpleBindProcs->procs = NULL;
            bindWidget = widget;
        }
        RemoveFromBindCache(bindWidget, procs);
    }
}

#ifdef notdef
void
_XtRemoveBindProcsByIndex(Widget w, TMBindData bindData, TMShortCard ndx)
{
    TMShortCard i = ndx;
    TMBindProcs bindProcs = (TMBindProcs) &bindData->bindTbl[0];

    RemoveFromBindCache(bindProcs->widget ? bindProcs->widget : w,
                        bindProcs[i].procs);

    for (; i < bindData->bindTblSize; i++)
        bindProcs[i] = bindProcs[i + 1];
}
#endif                          /* notdef */

/*
 * used to free all copied action tables, called from DestroyAppContext
 */
void
_XtFreeActions(ActionList actions)
{
    ActionList curr, next;

    for (curr = actions; curr;) {
        next = curr->next;
        XtFree((char *) curr->table);
        XtFree((char *) curr);
        curr = next;
    }
}

void
XtAddActions(XtActionList actions, Cardinal num_actions)
{
    XtAppAddActions(_XtDefaultAppContext(), actions, num_actions);
}

void
XtAppAddActions(XtAppContext app, XtActionList actions, Cardinal num_actions)
{
    register ActionList rec;

    LOCK_APP(app);
    rec = XtNew(ActionListRec);
    rec->next = app->action_table;
    app->action_table = rec;
    rec->table = CompileActionTable(actions, num_actions, False, False);
    rec->count = (TMShortCard) num_actions;
    UNLOCK_APP(app);
}

void
XtGetActionList(WidgetClass widget_class,
                XtActionList *actions_return,
                Cardinal *num_actions_return)
{
    CompiledActionTable table;

    *actions_return = NULL;
    *num_actions_return = 0;

    LOCK_PROCESS;
    if (!widget_class->core_class.class_inited) {
        UNLOCK_PROCESS;
        return;
    }
    if (!(widget_class->core_class.class_inited & WidgetClassFlag)) {
        UNLOCK_PROCESS;
        return;
    }
    *num_actions_return = widget_class->core_class.num_actions;
    if (*num_actions_return) {
        XtActionList list = *actions_return =
            XtMallocArray(*num_actions_return, (Cardinal) sizeof(XtActionsRec));

        table = GetClassActions(widget_class);

        if (table != NULL) {
            int i;

            for (i = (int) (*num_actions_return); --i >= 0; list++, table++) {
                list->string = XrmQuarkToString(table->signature);
                list->proc = table->proc;
            }
        }
    }
    UNLOCK_PROCESS;
}

/***********************************************************************
 *
 * Pop-up and Grab stuff
 *
 ***********************************************************************/

static Widget
_XtFindPopup(Widget widget, String name)
{
    register Cardinal i;
    register XrmQuark q;
    register Widget w;

    q = XrmStringToQuark(name);

    for (w = widget; w != NULL; w = w->core.parent)
        for (i = 0; i < w->core.num_popups; i++)
            if (w->core.popup_list[i]->core.xrm_name == q)
                return w->core.popup_list[i];

    return NULL;
}

void
XtMenuPopupAction(Widget widget,
                  XEvent *event,
                  String *params,
                  Cardinal *num_params)
{
    Boolean spring_loaded;
    register Widget popup_shell;
    XtAppContext app = XtWidgetToApplicationContext(widget);

    LOCK_APP(app);
    if (*num_params != 1) {
        XtAppWarningMsg(app,
                        "invalidParameters", "xtMenuPopupAction",
                        XtCXtToolkitError,
                        "MenuPopup wants exactly one argument", NULL, NULL);
        UNLOCK_APP(app);
        return;
    }

    if (event->type == ButtonPress)
        spring_loaded = True;
    else if (event->type == KeyPress || event->type == EnterNotify)
        spring_loaded = False;
    else {
        XtAppWarningMsg(app,
                        "invalidPopup", "unsupportedOperation",
                        XtCXtToolkitError,
                        "Pop-up menu creation is only supported on ButtonPress, KeyPress or EnterNotify events.",
                        NULL, NULL);
        UNLOCK_APP(app);
        return;
    }

    popup_shell = _XtFindPopup(widget, params[0]);
    if (popup_shell == NULL) {
        XtAppWarningMsg(app,
                        "invalidPopup", "xtMenuPopup", XtCXtToolkitError,
                        "Can't find popup widget \"%s\" in XtMenuPopup",
                        params, num_params);
        UNLOCK_APP(app);
        return;
    }

    if (spring_loaded)
        _XtPopup(popup_shell, XtGrabExclusive, TRUE);
    else
        _XtPopup(popup_shell, XtGrabNonexclusive, FALSE);
    UNLOCK_APP(app);
}

static void
_XtMenuPopdownAction(Widget widget,
                     XEvent *event _X_UNUSED,
                     String *params,
                     Cardinal *num_params)
{
    Widget popup_shell;

    if (*num_params == 0) {
        XtPopdown(widget);
    }
    else if (*num_params == 1) {
        popup_shell = _XtFindPopup(widget, params[0]);
        if (popup_shell == NULL) {
            XtAppWarningMsg(XtWidgetToApplicationContext(widget),
                            "invalidPopup", "xtMenuPopdown", XtCXtToolkitError,
                            "Can't find popup widget \"%s\" in XtMenuPopdown",
                            params, num_params);
            return;
        }
        XtPopdown(popup_shell);
    }
    else {
        XtAppWarningMsg(XtWidgetToApplicationContext(widget),
                        "invalidParameters", "xtMenuPopdown", XtCXtToolkitError,
                        "XtMenuPopdown called with num_params != 0 or 1",
                        NULL, NULL);
    }
}

/* *INDENT-OFF* */
static XtActionsRec RConst tmActions[] = {
    {"XtMenuPopup",                    XtMenuPopupAction},
    {"XtMenuPopdown",                  _XtMenuPopdownAction},
    {"MenuPopup",                      XtMenuPopupAction},      /* old & obsolete */
    {"MenuPopdown",                    _XtMenuPopdownAction},   /* ditto */
#ifndef NO_MIT_HACKS
    {"XtDisplayTranslations",          _XtDisplayTranslations},
    {"XtDisplayAccelerators",          _XtDisplayAccelerators},
    {"XtDisplayInstalledAccelerators", _XtDisplayInstalledAccelerators},
#endif
};
/* *INDENT-ON* */

void
_XtPopupInitialize(XtAppContext app)
{
    register ActionList rec;

    /*
     * The _XtGlobalTM.newMatchSemantics flag determines whether
     * we support old or new matching
     * behavior. This is mainly an issue of whether subsequent lhs will
     * get pushed up in the match table if a lhs containing this initial
     * sequence has already been encountered. Currently inited to False;
     */
#ifdef NEW_TM
    _XtGlobalTM.newMatchSemantics = True;
#else
    _XtGlobalTM.newMatchSemantics = False;
#endif

    rec = XtNew(ActionListRec);
    rec->next = app->action_table;
    app->action_table = rec;
    LOCK_PROCESS;
    rec->table = CompileActionTable(tmActions, XtNumber(tmActions), False,
                                    True);
    rec->count = XtNumber(tmActions);
    UNLOCK_PROCESS;
    _XtGrabInitialize(app);
}

void
XtCallActionProc(Widget widget,
                 _Xconst char *action,
                 XEvent *event,
                 String *params,
                 Cardinal num_params)
{
    CompiledAction *actionP;
    XrmQuark q = XrmStringToQuark(action);
    Widget w = widget;
    XtAppContext app = XtWidgetToApplicationContext(widget);
    ActionList actionList;
    Cardinal i;

    LOCK_APP(app);
    XtCheckSubclass(widget, coreWidgetClass,
                    "XtCallActionProc first argument is not a subclass of Core");
    LOCK_PROCESS;
    do {
        WidgetClass class = XtClass(w);

        do {
            if ((actionP = GetClassActions(class)) != NULL)
                for (i = 0; i < class->core_class.num_actions; i++, actionP++) {

                    if (actionP->signature == q) {
                        ActionHook hook = app->action_hook_list;

                        while (hook != NULL) {
                            (*hook->proc) (widget,
                                           hook->closure,
                                           (String) action,
                                           event,
                                           params,
                                           &num_params);
                            hook = hook->next;
                        }
                        (*(actionP->proc))
                            (widget, event, params, &num_params);
                        UNLOCK_PROCESS;
                        UNLOCK_APP(app);
                        return;
                    }
                }
            class = class->core_class.superclass;
        } while (class != NULL);
        w = XtParent(w);
    } while (w != NULL);
    UNLOCK_PROCESS;

    for (actionList = app->action_table;
         actionList != NULL; actionList = actionList->next) {

        for (i = 0, actionP = actionList->table;
             i < actionList->count; i++, actionP++) {
            if (actionP->signature == q) {
                ActionHook hook = app->action_hook_list;

                while (hook != NULL) {
                    (*hook->proc) (widget,
                                   hook->closure,
                                   (String) action,
                                   event,
                                   params,
                                   &num_params);
                    hook = hook->next;
                }
                (*(actionP->proc))
                    (widget, event, params, &num_params);
                UNLOCK_APP(app);
                return;
            }
        }

    }

    {
        String par[2];
        Cardinal num_par = 2;

        par[0] = (String) action;
        par[1] = XtName(widget);
        XtAppWarningMsg(app,
                        "noActionProc", "xtCallActionProc", XtCXtToolkitError,
                        "No action proc named \"%s\" is registered for widget \"%s\"",
                        par, &num_par);
    }
    UNLOCK_APP(app);
}

void
_XtDoFreeBindings(XtAppContext app)
{
    TMBindCache bcp;

    while (app->free_bindings) {
        bcp = app->free_bindings->next;
        XtFree((char *) app->free_bindings);
        app->free_bindings = bcp;
    }
}
