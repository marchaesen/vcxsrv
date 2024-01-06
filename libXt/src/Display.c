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
#ifndef X_NO_RESOURCE_CONFIGURATION_MANAGEMENT
#include "ResConfigP.h"
#endif

#include <stdlib.h>

#ifdef XTHREADS
void (*_XtProcessLock) (void) = NULL;
void (*_XtProcessUnlock) (void) = NULL;
void (*_XtInitAppLock) (XtAppContext) = NULL;
#endif

static _Xconst _XtString XtNnoPerDisplay = "noPerDisplay";

ProcessContext
_XtGetProcessContext(void)
{
    static ProcessContextRec processContextRec = {
        (XtAppContext) NULL,
        (XtAppContext) NULL,
        (ConverterTable) NULL,
        {(XtLanguageProc) NULL, (XtPointer) NULL}
    };

    return &processContextRec;
}

XtAppContext
_XtDefaultAppContext(void)
{
    ProcessContext process = _XtGetProcessContext();
    XtAppContext app;

    LOCK_PROCESS;
    if (process->defaultAppContext == NULL) {
        process->defaultAppContext = XtCreateApplicationContext();
    }
    app = process->defaultAppContext;
    UNLOCK_PROCESS;
    return app;
}

static void
AddToAppContext(Display *d, XtAppContext app)
{
#define DISPLAYS_TO_ADD 4

    if (app->count >= app->max) {
        app->max = (short) (app->max + DISPLAYS_TO_ADD);
        app->list = XtReallocArray(app->list,
                                   (Cardinal) app->max,
                                   (Cardinal) sizeof(Display *));
    }

    app->list[app->count++] = d;
    app->rebuild_fdlist = TRUE;
#ifdef USE_POLL
    app->fds.nfds++;
#else
    if (ConnectionNumber(d) + 1 > app->fds.nfds) {
        app->fds.nfds = ConnectionNumber(d) + 1;
    }
#endif
#undef DISPLAYS_TO_ADD
}

static void
XtDeleteFromAppContext(const Display *d, register XtAppContext app)
{
    register int i;

    for (i = 0; i < app->count; i++)
        if (app->list[i] == d)
            break;

    if (i < app->count) {
        if (i <= app->last && app->last > 0)
            app->last--;
        for (i++; i < app->count; i++)
            app->list[i - 1] = app->list[i];
        app->count--;
    }
    app->rebuild_fdlist = TRUE;
#ifdef USE_POLL
    app->fds.nfds--;
#else
    if ((ConnectionNumber(d) + 1) == app->fds.nfds)
        app->fds.nfds--;
    else                        /* Unnecessary, just to be fool-proof */
        FD_CLR(ConnectionNumber(d), &app->fds.rmask);
#endif
}

static XtPerDisplay
NewPerDisplay(Display *dpy)
{
    PerDisplayTablePtr pd;

    pd = XtNew(PerDisplayTable);

    LOCK_PROCESS;
    pd->dpy = dpy;
    pd->next = _XtperDisplayList;
    _XtperDisplayList = pd;
    UNLOCK_PROCESS;
    return &(pd->perDpy);
}

static XtPerDisplay
InitPerDisplay(Display *dpy,
               XtAppContext app,
               _Xconst char *name,
               _Xconst char *classname)
{
    XtPerDisplay pd;

    AddToAppContext(dpy, app);

    pd = NewPerDisplay(dpy);
    _XtHeapInit(&pd->heap);
    pd->destroy_callbacks = NULL;
    pd->region = XCreateRegion();
    pd->case_cvt = NULL;
    pd->defaultKeycodeTranslator = XtTranslateKey;
    pd->keysyms_serial = 0;
    pd->keysyms = NULL;
    XDisplayKeycodes(dpy, &pd->min_keycode, &pd->max_keycode);
    pd->modKeysyms = NULL;
    pd->modsToKeysyms = NULL;
    pd->appContext = app;
    pd->name = XrmStringToName(name);
    pd->class = XrmStringToClass(classname);
    pd->being_destroyed = False;
    pd->GClist = NULL;
    pd->pixmap_tab = NULL;
    pd->language = NULL;
    pd->rv = False;
    pd->last_event.xany.serial = 0;
    pd->last_timestamp = 0;
    _XtAllocTMContext(pd);
    pd->mapping_callbacks = NULL;

    pd->pdi.grabList = NULL;
    pd->pdi.trace = NULL;
    pd->pdi.traceDepth = 0;
    pd->pdi.traceMax = 0;
    pd->pdi.focusWidget = NULL;
    pd->pdi.activatingKey = 0;
    pd->pdi.keyboard.grabType = XtNoServerGrab;
    pd->pdi.pointer.grabType = XtNoServerGrab;

    _XtAllocWWTable(pd);
    pd->per_screen_db = (XrmDatabase *) __XtCalloc((Cardinal) ScreenCount(dpy),
                                                   (Cardinal)
                                                   sizeof(XrmDatabase));
    pd->cmd_db = (XrmDatabase) NULL;
    pd->server_db = (XrmDatabase) NULL;
    pd->dispatcher_list = NULL;
    pd->ext_select_list = NULL;
    pd->ext_select_count = 0;
    pd->hook_object = NULL;
#if 0
    pd->hook_object = _XtCreate("hooks", "Hooks", hookObjectClass,
                                (Widget) NULL,
                                (Screen *) DefaultScreenOfDisplay(dpy),
                                (ArgList) NULL, 0, (XtTypedArgList) NULL, 0,
                                (ConstraintWidgetClass) NULL);
#endif

#ifndef X_NO_RESOURCE_CONFIGURATION_MANAGEMENT
    pd->rcm_init = XInternAtom(dpy, RCM_INIT, 0);
    pd->rcm_data = XInternAtom(dpy, RCM_DATA, 0);
#endif

    return pd;
}

#define THIS_FUNC "XtOpenDisplay"
Display *
XtOpenDisplay(XtAppContext app,
              _Xconst _XtString displayName,
              _Xconst _XtString applName,
              _Xconst _XtString className,
              XrmOptionDescRec *urlist,
              Cardinal num_urs,
              int *argc,
              _XtString *argv)
{
    Display *d;
    XrmDatabase db = NULL;
    String language = NULL;

    LOCK_APP(app);
    LOCK_PROCESS;
    /* parse the command line for name, display, and/or language */
    db = _XtPreparseCommandLine(urlist, num_urs, *argc, argv,
                                (String *) &applName,
                                (String *) (displayName ? NULL : &displayName),
                                (app->process->globalLangProcRec.proc ?
                                 &language : NULL));
    UNLOCK_PROCESS;
    d = XOpenDisplay(displayName);
    if (d != NULL) {
        if (ScreenCount(d) <= 0) {
            XtErrorMsg("nullDisplay",
                       THIS_FUNC, XtCXtToolkitError,
                       THIS_FUNC " requires a non-NULL display",
                       NULL, NULL);
        }
        if (DefaultScreen(d) < 0 || DefaultScreen(d) >= ScreenCount(d)) {
            XtWarningMsg("nullDisplay",
                         THIS_FUNC, XtCXtToolkitError,
                         THIS_FUNC " default screen is invalid (ignoring)",
                         NULL, NULL);
            DefaultScreen(d) = 0;
        }
    }

    if (!applName && !(applName = getenv("RESOURCE_NAME"))) {
        if (*argc > 0 && argv[0] && *argv[0]) {
#ifdef WIN32
            char *ptr = strrchr(argv[0], '\\');
#else
            char *ptr = strrchr(argv[0], '/');
#endif

            if (ptr)
                applName = ++ptr;
            else
                applName = argv[0];
        }
        else
            applName = "main";
    }

    if (d) {
        XtPerDisplay pd;

        pd = InitPerDisplay(d, app, applName, className);
        pd->language = language;
        _XtDisplayInitialize(d, pd, applName, urlist, num_urs, argc, argv);
    }
    else {
        int len;

        displayName = XDisplayName(displayName);
        len = (int) strlen(displayName);
        app->display_name_tried = (_XtString) __XtMalloc((Cardinal) (len + 1));
        strncpy((char *) app->display_name_tried, displayName,
                (size_t) (len + 1));
        app->display_name_tried[len] = '\0';
    }
    if (db)
        XrmDestroyDatabase(db);
    UNLOCK_APP(app);
    return d;
}

Display *
_XtAppInit(XtAppContext *app_context_return,
           String application_class,
           XrmOptionDescRec *options,
           Cardinal num_options,
           int *argc_in_out,
           _XtString **argv_in_out,
           String *fallback_resources)
{
    _XtString *saved_argv;
    int i;
    Display *dpy;

    /*
     * Save away argv and argc so we can set the properties later
     */
    saved_argv = XtMallocArray((Cardinal) *argc_in_out + 1,
                               (Cardinal) sizeof(_XtString));

    for (i = 0; i < *argc_in_out; i++)
        saved_argv[i] = (*argv_in_out)[i];
    saved_argv[i] = NULL;       /* NULL terminate that sucker. */

    *app_context_return = XtCreateApplicationContext();

    LOCK_APP((*app_context_return));
    if (fallback_resources)     /* save a procedure call */
        XtAppSetFallbackResources(*app_context_return, fallback_resources);

    dpy = XtOpenDisplay(*app_context_return, NULL, NULL,
                        application_class,
                        options, num_options, argc_in_out, *argv_in_out);

    if (!dpy) {
        String param = (*app_context_return)->display_name_tried;
        Cardinal param_count = 1;

        XtErrorMsg("invalidDisplay", "xtInitialize", XtCXtToolkitError,
                   "Can't open display: %s", &param, &param_count);
        XtFree((char *) (*app_context_return)->display_name_tried);
    }
    *argv_in_out = saved_argv;
    UNLOCK_APP((*app_context_return));
    return dpy;
}

void
XtDisplayInitialize(XtAppContext app,
                    Display *dpy,
                    _Xconst _XtString name,
                    _Xconst _XtString classname,
                    XrmOptionDescRec *urlist,
                    Cardinal num_urs,
                    int *argc,
                    _XtString *argv)
{
    XtPerDisplay pd;
    XrmDatabase db = NULL;

    LOCK_APP(app);
    pd = InitPerDisplay(dpy, app, name, classname);
    LOCK_PROCESS;
    if (app->process->globalLangProcRec.proc)
        /* pre-parse the command line for the language resource */
        db = _XtPreparseCommandLine(urlist, num_urs, *argc, argv, NULL, NULL,
                                    &pd->language);
    UNLOCK_PROCESS;
    _XtDisplayInitialize(dpy, pd, name, urlist, num_urs, argc, argv);
    if (db)
        XrmDestroyDatabase(db);
    UNLOCK_APP(app);
}

XtAppContext
XtCreateApplicationContext(void)
{
    XtAppContext app = XtNew(XtAppStruct);

#ifdef XTHREADS
    app->lock_info = NULL;
    app->lock = NULL;
    app->unlock = NULL;
    app->yield_lock = NULL;
    app->restore_lock = NULL;
    app->free_lock = NULL;
#endif
    INIT_APP_LOCK(app);
    LOCK_APP(app);
    LOCK_PROCESS;
    app->process = _XtGetProcessContext();
    app->next = app->process->appContextList;
    app->process->appContextList = app;
    app->langProcRec.proc = app->process->globalLangProcRec.proc;
    app->langProcRec.closure = app->process->globalLangProcRec.closure;
    app->destroy_callbacks = NULL;
    app->list = NULL;
    app->count = app->max = app->last = 0;
    app->timerQueue = NULL;
    app->workQueue = NULL;
    app->signalQueue = NULL;
    app->input_list = NULL;
    app->outstandingQueue = NULL;
    app->errorDB = NULL;
    _XtSetDefaultErrorHandlers(&app->errorMsgHandler,
                               &app->warningMsgHandler, &app->errorHandler,
                               &app->warningHandler);
    app->action_table = NULL;
    _XtSetDefaultSelectionTimeout(&app->selectionTimeout);
    _XtSetDefaultConverterTable(&app->converterTable);
    app->sync = app->being_destroyed = app->error_inited = FALSE;
    app->in_phase2_destroy = NULL;
#ifndef USE_POLL
    FD_ZERO(&app->fds.rmask);
    FD_ZERO(&app->fds.wmask);
    FD_ZERO(&app->fds.emask);
#endif
    app->fds.nfds = 0;
    app->input_count = app->input_max = 0;
    _XtHeapInit(&app->heap);
    app->fallback_resources = NULL;
    _XtPopupInitialize(app);
    app->action_hook_list = NULL;
    app->block_hook_list = NULL;
    app->destroy_list_size = app->destroy_count = app->dispatch_level = 0;
    app->destroy_list = NULL;
#ifndef NO_IDENTIFY_WINDOWS
    app->identify_windows = False;
#endif
    app->free_bindings = NULL;
    app->display_name_tried = NULL;
    app->dpy_destroy_count = 0;
    app->dpy_destroy_list = NULL;
    app->exit_flag = FALSE;
    app->rebuild_fdlist = TRUE;
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
    return app;
}

void
XtAppSetExitFlag(XtAppContext app)
{
    LOCK_APP(app);
    app->exit_flag = TRUE;
    UNLOCK_APP(app);
}

Boolean
XtAppGetExitFlag(XtAppContext app)
{
    Boolean retval;

    LOCK_APP(app);
    retval = app->exit_flag;
    UNLOCK_APP(app);
    return retval;
}

static void
DestroyAppContext(XtAppContext app)
{
    XtAppContext *prev_app;

    prev_app = &app->process->appContextList;
    while (app->count-- > 0)
        XtCloseDisplay(app->list[app->count]);
    if (app->list != NULL)
        XtFree((char *) app->list);
    _XtFreeConverterTable(app->converterTable);
    _XtCacheFlushTag(app, (XtPointer) &app->heap);
    _XtFreeActions(app->action_table);
    if (app->destroy_callbacks != NULL) {
        XtCallCallbackList((Widget) NULL,
                           (XtCallbackList) app->destroy_callbacks,
                           (XtPointer) app);
        _XtRemoveAllCallbacks(&app->destroy_callbacks);
    }
    while (app->timerQueue)
        XtRemoveTimeOut((XtIntervalId) app->timerQueue);
    while (app->workQueue)
        XtRemoveWorkProc((XtWorkProcId) app->workQueue);
    while (app->signalQueue)
        XtRemoveSignal((XtSignalId) app->signalQueue);
    if (app->input_list)
        _XtRemoveAllInputs(app);
    XtFree((char *) app->destroy_list);
    _XtHeapFree(&app->heap);
    while (*prev_app != app)
        prev_app = &(*prev_app)->next;
    *prev_app = app->next;
    if (app->process->defaultAppContext == app)
        app->process->defaultAppContext = NULL;
    if (app->free_bindings)
        _XtDoFreeBindings(app);
    FREE_APP_LOCK(app);
    XtFree((char *) app);
}

static XtAppContext *appDestroyList = NULL;
int _XtAppDestroyCount = 0;

void
XtDestroyApplicationContext(XtAppContext app)
{
    LOCK_APP(app);
    if (app->being_destroyed) {
        UNLOCK_APP(app);
        return;
    }

    if (_XtSafeToDestroy(app)) {
        LOCK_PROCESS;
        DestroyAppContext(app);
        UNLOCK_PROCESS;
    }
    else {
        app->being_destroyed = TRUE;
        LOCK_PROCESS;
        _XtAppDestroyCount++;
        appDestroyList = XtReallocArray(appDestroyList,
                                        (Cardinal) _XtAppDestroyCount,
                                        (Cardinal) sizeof(XtAppContext));
        appDestroyList[_XtAppDestroyCount - 1] = app;
        UNLOCK_PROCESS;
        UNLOCK_APP(app);
    }
}

void
_XtDestroyAppContexts(void)
{
    int i, ii;
    XtAppContext apps[8];
    XtAppContext *pApps;

    pApps =
        XtStackAlloc(sizeof(XtAppContext) * (size_t) _XtAppDestroyCount, apps);

    for (i = ii = 0; i < _XtAppDestroyCount; i++) {
        if (_XtSafeToDestroy(appDestroyList[i]))
            DestroyAppContext(appDestroyList[i]);
        else
            pApps[ii++] = appDestroyList[i];
    }
    _XtAppDestroyCount = ii;
    if (_XtAppDestroyCount == 0) {
        XtFree((char *) appDestroyList);
        appDestroyList = NULL;
    }
    else {
        for (i = 0; i < ii; i++)
            appDestroyList[i] = pApps[i];
    }
    XtStackFree((XtPointer) pApps, apps);
}

XrmDatabase
XtDatabase(Display *dpy)
{
    XrmDatabase retval;

    DPY_TO_APPCON(dpy);

    LOCK_APP(app);
    retval = XrmGetDatabase(dpy);
    UNLOCK_APP(app);
    return retval;
}

PerDisplayTablePtr _XtperDisplayList = NULL;

XtPerDisplay
_XtSortPerDisplayList(Display *dpy)
{
    register PerDisplayTablePtr pd, opd = NULL;
    XtPerDisplay result = NULL;

    LOCK_PROCESS;
    for (pd = _XtperDisplayList; pd != NULL && pd->dpy != dpy; pd = pd->next) {
        opd = pd;
    }

    if (pd == NULL) {
        XtErrorMsg(XtNnoPerDisplay, "getPerDisplay", XtCXtToolkitError,
                   "Couldn't find per display information", NULL, NULL);
    }
    else {
        if (pd != _XtperDisplayList) {  /* move it to the front */
            /* opd points to the previous one... */

            opd->next = pd->next;
            pd->next = _XtperDisplayList;
            _XtperDisplayList = pd;
        }
        result = &(pd->perDpy);
    }
    UNLOCK_PROCESS;
    return result;
}

XtAppContext
XtDisplayToApplicationContext(Display *dpy)
{
    XtAppContext retval;

    retval = _XtGetPerDisplay(dpy)->appContext;
    return retval;
}

static void
CloseDisplay(Display *dpy)
{
    register XtPerDisplay xtpd = NULL;
    register PerDisplayTablePtr pd, opd = NULL;
    XrmDatabase db;

    XtDestroyWidget(XtHooksOfDisplay(dpy));

    LOCK_PROCESS;
    for (pd = _XtperDisplayList; pd != NULL && pd->dpy != dpy; pd = pd->next) {
        opd = pd;
    }

    if (pd == NULL) {
        XtErrorMsg(XtNnoPerDisplay, "closeDisplay", XtCXtToolkitError,
                   "Couldn't find per display information", NULL, NULL);
    }
    else {

        if (pd == _XtperDisplayList)
            _XtperDisplayList = pd->next;
        else
            opd->next = pd->next;

        xtpd = &(pd->perDpy);
    }

    if (xtpd != NULL) {
        int i;

        if (xtpd->destroy_callbacks != NULL) {
            XtCallCallbackList((Widget) NULL,
                               (XtCallbackList) xtpd->destroy_callbacks,
                               (XtPointer) xtpd);
            _XtRemoveAllCallbacks(&xtpd->destroy_callbacks);
        }
        if (xtpd->mapping_callbacks != NULL)
            _XtRemoveAllCallbacks(&xtpd->mapping_callbacks);
        XtDeleteFromAppContext(dpy, xtpd->appContext);
        if (xtpd->keysyms)
            XFree((char *) xtpd->keysyms);
        XtFree((char *) xtpd->modKeysyms);
        XtFree((char *) xtpd->modsToKeysyms);
        xtpd->keysyms_per_keycode = 0;
        xtpd->being_destroyed = FALSE;
        xtpd->keysyms = NULL;
        xtpd->modKeysyms = NULL;
        xtpd->modsToKeysyms = NULL;
        XDestroyRegion(xtpd->region);
        _XtCacheFlushTag(xtpd->appContext, (XtPointer) &xtpd->heap);
        _XtGClistFree(dpy, xtpd);
        XtFree((char *) xtpd->pdi.trace);
        _XtHeapFree(&xtpd->heap);
        _XtFreeWWTable(xtpd);
        xtpd->per_screen_db[DefaultScreen(dpy)] = (XrmDatabase) NULL;
        for (i = ScreenCount(dpy); --i >= 0;) {
            db = xtpd->per_screen_db[i];
            if (db)
                XrmDestroyDatabase(db);
        }
        XtFree((char *) xtpd->per_screen_db);
        if ((db = XrmGetDatabase(dpy)))
            XrmDestroyDatabase(db);
        if (xtpd->cmd_db)
            XrmDestroyDatabase(xtpd->cmd_db);
        if (xtpd->server_db)
            XrmDestroyDatabase(xtpd->server_db);
        XtFree((_XtString) xtpd->language);
        if (xtpd->dispatcher_list != NULL)
            XtFree((char *) xtpd->dispatcher_list);
        if (xtpd->ext_select_list != NULL)
            XtFree((char *) xtpd->ext_select_list);
    }
    XtFree((char *) pd);
    XrmSetDatabase(dpy, (XrmDatabase) NULL);
    XCloseDisplay(dpy);
    UNLOCK_PROCESS;
}

void
XtCloseDisplay(Display *dpy)
{
    XtPerDisplay pd;
    XtAppContext app = XtDisplayToApplicationContext(dpy);

    LOCK_APP(app);
    pd = _XtGetPerDisplay(dpy);
    if (pd->being_destroyed) {
        UNLOCK_APP(app);
        return;
    }

    if (_XtSafeToDestroy(app))
        CloseDisplay(dpy);
    else {
        pd->being_destroyed = TRUE;
        app->dpy_destroy_count++;
        app->dpy_destroy_list = XtReallocArray(app->dpy_destroy_list,
                                               (Cardinal) app->dpy_destroy_count,
                                               (Cardinal) sizeof(Display *));
        app->dpy_destroy_list[app->dpy_destroy_count - 1] = dpy;
    }
    UNLOCK_APP(app);
}

void
_XtCloseDisplays(XtAppContext app)
{
    int i;

    LOCK_APP(app);
    for (i = 0; i < app->dpy_destroy_count; i++) {
        CloseDisplay(app->dpy_destroy_list[i]);
    }
    app->dpy_destroy_count = 0;
    XtFree((char *) app->dpy_destroy_list);
    app->dpy_destroy_list = NULL;
    UNLOCK_APP(app);
}

XtAppContext
XtWidgetToApplicationContext(Widget w)
{
    XtAppContext retval;

    retval = _XtGetPerDisplay(XtDisplayOfObject(w))->appContext;
    return retval;
}

void
XtGetApplicationNameAndClass(Display *dpy,
                             String *name_return,
                             String *class_return)
{
    XtPerDisplay pd;

    pd = _XtGetPerDisplay(dpy);
    *name_return = XrmQuarkToString(pd->name);
    *class_return = XrmQuarkToString(pd->class);
}

XtPerDisplay
_XtGetPerDisplay(Display *display)
{
    XtPerDisplay retval;

    LOCK_PROCESS;
    retval = ((_XtperDisplayList != NULL && _XtperDisplayList->dpy == display)
              ? &_XtperDisplayList->perDpy : _XtSortPerDisplayList(display));

    UNLOCK_PROCESS;
    return retval;
}

XtPerDisplayInputRec *
_XtGetPerDisplayInput(Display *display)
{
    XtPerDisplayInputRec *retval;

    LOCK_PROCESS;
    retval = ((_XtperDisplayList != NULL && _XtperDisplayList->dpy == display)
              ? &_XtperDisplayList->perDpy.pdi
              : &_XtSortPerDisplayList(display)->pdi);
    UNLOCK_PROCESS;
    return retval;
}

void
XtGetDisplays(XtAppContext app_context,
              Display ***dpy_return,
              Cardinal *num_dpy_return)
{
    int ii;

    LOCK_APP(app_context);
    *num_dpy_return = (Cardinal) app_context->count;
    *dpy_return = XtMallocArray((Cardinal) app_context->count,
                                (Cardinal) sizeof(Display *));
    for (ii = 0; ii < app_context->count; ii++)
        (*dpy_return)[ii] = app_context->list[ii];
    UNLOCK_APP(app_context);
}
