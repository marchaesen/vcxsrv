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
/*
 * Contains XtAppAddActionHook, XtRemoveActionHook
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "IntrinsicI.h"

static void
FreeActionHookList(Widget widget _X_UNUSED,
		   XtPointer closure,  /* ActionHook* */
                   XtPointer call_data _X_UNUSED)
{
    ActionHook list = *(ActionHook *) closure;

    while (list != NULL) {
        ActionHook next = list->next;

        XtFree((XtPointer) list);
        list = next;
    }
}

XtActionHookId
XtAppAddActionHook(XtAppContext app, XtActionHookProc proc, XtPointer closure)
{
    ActionHook hook = XtNew(ActionHookRec);

    LOCK_APP(app);
    hook->next = app->action_hook_list;
    hook->app = app;
    hook->proc = proc;
    hook->closure = closure;
    if (app->action_hook_list == NULL) {
        _XtAddCallback(&app->destroy_callbacks,
                       FreeActionHookList, (XtPointer) &app->action_hook_list);
    }
    app->action_hook_list = hook;
    UNLOCK_APP(app);
    return (XtActionHookId) hook;
}

void
XtRemoveActionHook(XtActionHookId id)
{
    ActionHook *p, hook = (ActionHook) id;
    XtAppContext app = hook->app;

    LOCK_APP(app);
    for (p = &app->action_hook_list; p != NULL && *p != hook; p = &(*p)->next);
    if (p) {
        *p = hook->next;
        XtFree((XtPointer) hook);
        if (app->action_hook_list == NULL)
            _XtRemoveCallback(&app->destroy_callbacks, FreeActionHookList,
                              (XtPointer) &app->action_hook_list);
    }
#ifdef DEBUG
    else {
        XtAppWarningMsg(app, "badId", "xtRemoveActionHook", XtCXtToolkitError,
                        "XtRemoveActionHook called with bad or old hook id",
                        NULL, NULL);
    }
#endif /*DEBUG*/
        UNLOCK_APP(app);
}
