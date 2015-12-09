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
#include <stdio.h>
#include <stdlib.h>

/* The error handlers in the application context aren't used since we can't
   come up with a uniform way of using them.  If you can, define
   GLOBALERRORS to be FALSE (or 0). */

#ifndef GLOBALERRORS
#define GLOBALERRORS 1
#endif

static void InitErrorHandling(XrmDatabase *);
#if GLOBALERRORS
static XrmDatabase errorDB = NULL;
static Boolean error_inited = FALSE;
void _XtDefaultErrorMsg(String, String, String, String, String*, Cardinal*);
void _XtDefaultWarningMsg(String, String, String, String, String*, Cardinal*);
void _XtDefaultError(String) _X_NORETURN;
void _XtDefaultWarning(String);
static XtErrorMsgHandler errorMsgHandler = _XtDefaultErrorMsg;
static XtErrorMsgHandler warningMsgHandler = _XtDefaultWarningMsg;
static XtErrorHandler errorHandler = _XtDefaultError;
static XtErrorHandler warningHandler = _XtDefaultWarning;
#endif /* GLOBALERRORS */

XrmDatabase *XtGetErrorDatabase(void)
{
    XrmDatabase* retval;
#if GLOBALERRORS
    LOCK_PROCESS;
    retval = &errorDB;
    UNLOCK_PROCESS;
#else
    retval = XtAppGetErrorDatabase(_XtDefaultAppContext());
#endif /* GLOBALERRORS */
    return retval;
}

XrmDatabase *XtAppGetErrorDatabase(
	XtAppContext app)
{
    XrmDatabase* retval;
#if GLOBALERRORS
    LOCK_PROCESS;
    retval = &errorDB;
    UNLOCK_PROCESS;
#else
    LOCK_APP(app);
    retval= &app->errorDB;
    UNLOCK_APP(app);
#endif /* GLOBALERRORS */
    return retval;
}

void XtGetErrorDatabaseText(
    register _Xconst char* name,
    register _Xconst char* type,
    register _Xconst char* class,
    _Xconst char* defaultp,
    String buffer,
    int nbytes)
{
#if GLOBALERRORS
    XtAppGetErrorDatabaseText(NULL,
	    name,type,class,defaultp, buffer, nbytes, NULL);
#else
    XtAppGetErrorDatabaseText(_XtDefaultAppContext(),
	    name,type,class,defaultp, buffer, nbytes, NULL);
#endif /* GLOBALERRORS */
}

void XtAppGetErrorDatabaseText(
    XtAppContext app,
    register _Xconst char* name,
    register _Xconst char* type,
    register _Xconst char* class,
    _Xconst char* defaultp,
    String buffer,
    int nbytes,
    XrmDatabase db)
{
    String str_class;
    String type_str;
    XrmValue result;
    char *str_name = NULL;
    char *temp = NULL;

#if GLOBALERRORS
    LOCK_PROCESS;
    if (error_inited == FALSE) {
        InitErrorHandling (&errorDB);
        error_inited = TRUE;
    }
#else
    LOCK_APP(app);
    if (app->error_inited == FALSE) {
        InitErrorHandling (&app->errorDB);
        app->error_inited = TRUE;
    }
#endif /* GLOBALERRORS */
    if (!(str_name = ALLOCATE_LOCAL(strlen(name) + strlen(type) + 2)))
	_XtAllocError(NULL);
    (void) sprintf(str_name, "%s.%s", name, type);
    /* XrmGetResource requires the name and class to be fully qualified
     * and to have the same number of components. */
    str_class = (char *)class;
    if (! strchr(class, '.')) {
	if (!(temp = ALLOCATE_LOCAL(2 * strlen(class) + 2)))
	    _XtAllocError(NULL);
	(void) sprintf(temp, "%s.%s", class, class);
	str_class = temp;
    }
    if (db == NULL) {
#if GLOBALERRORS
	(void) XrmGetResource(errorDB, str_name, str_class, &type_str,
			      &result);
#else
	(void) XrmGetResource(app->errorDB, str_name, str_class, &type_str,
			      &result);
#endif /* GLOBALERRORS */
    } else (void) XrmGetResource(db, str_name, str_class, &type_str, &result);
    if (result.addr) {
        (void) strncpy (buffer, result.addr, nbytes);
        if (result.size > (unsigned) nbytes) buffer[nbytes-1] = 0;
    } else {
	int len = strlen(defaultp);
	if (len >= nbytes) len = nbytes-1;
	(void) memmove(buffer, defaultp, len);
	buffer[len] = '\0';
    }
    if (str_name)
	DEALLOCATE_LOCAL(str_name);
    if (temp)
	DEALLOCATE_LOCAL(temp);
#if GLOBALERRORS
    UNLOCK_PROCESS;
#else
    UNLOCK_APP(app);
#endif
}

static void InitErrorHandling (
    XrmDatabase *db)
{
    XrmDatabase errordb;

    errordb = XrmGetFileDatabase(ERRORDB);
    XrmMergeDatabases(errordb, db);
}

static void DefaultMsg (
    String name,
    String type,
    String class,
    String defaultp,
    String* params,
    Cardinal* num_params,
    Bool error,
    void (*fn)(_Xconst _XtString))
{
#define BIGBUF 1024
    char buffer[BIGBUF];
    char* message;
    XtGetErrorDatabaseText(name,type,class,defaultp, buffer, BIGBUF);
/*need better solution here, perhaps use lower level printf primitives? */
    if (params == NULL || num_params == NULL || *num_params == 0)
	(*fn)(buffer);
#ifndef WIN32 /* and OS/2 */
    else if ((getuid () != geteuid ()) || getuid() == 0) {
	if ((error && errorHandler == _XtDefaultError) ||
	    (!error && warningHandler == _XtDefaultWarning)) {
	    /*
	     * if it's just going to go to stderr anyway, then we'll
	     * fprintf to stderr ourselves and skip the insecure sprintf.
	     */
	    Cardinal i = *num_params;
	    String par[10];
	    if (i > 10) i = 10;
	    (void) memmove((char*)par, (char*)params, i * sizeof(String) );
	    bzero( &par[i], (10-i) * sizeof(String) );
	    (void) fprintf (stderr, "%s%s",
			    error ? XTERROR_PREFIX : XTWARNING_PREFIX,
			    error ? "Error: " : "Warning: ");
	    (void) fprintf (stderr, buffer,
			    par[0], par[1], par[2], par[3], par[4],
			    par[5], par[6], par[7], par[8], par[9]);
	    (void) fprintf (stderr, "%c", '\n');
	    if (i != *num_params)
		(*fn) ( "Some arguments in previous message were lost" );
	    else if (error) exit (1);
	} else {
	    /*
	     * can't tell what it might do, so we'll play it safe
	     */
	    XtWarning ("\
This program is an suid-root program or is being run by the root user.\n\
The full text of the error or warning message cannot be safely formatted\n\
in this environment. You may get a more descriptive message by running the\n\
program as a non-root user or by removing the suid bit on the executable.");
	    (*fn)(buffer); /* if *fn is an ErrorHandler it should exit */
	}
    }
#endif
    else {
	/*
	 * If you have snprintf the worst thing that could happen is you'd
	 * lose some information. Without snprintf you're probably going to
	 * scramble your heap and perhaps SEGV -- sooner or later.
	 * If it hurts when you go like this then don't go like this! :-)
	 */
	Cardinal i = *num_params;
	String par[10];
	if (i > 10) i = 10;
	(void) memmove((char*)par, (char*)params, i * sizeof(String) );
	bzero( &par[i], (10-i) * sizeof(String) );
	if (i != *num_params)
	    XtWarning( "Some arguments in following message were lost" );
	/*
	 * resist any temptation you might have to make `message' a
	 * local buffer on the stack. Doing so is a security hole
	 * in programs executing as root. Error and Warning
	 * messages shouldn't be called frequently enough for this
	 * to be a performance issue.
	 */
	if ((message = __XtMalloc (BIGBUF))) {
	    (void) snprintf (message, BIGBUF, buffer,
			     par[0], par[1], par[2], par[3], par[4],
			     par[5], par[6], par[7], par[8], par[9]);
	    (*fn)(message);
	    XtFree(message);
	} else {
	    XtWarning ("Memory allocation failed, arguments in the following message were lost");
	    (*fn)(buffer);
	}
    }
}

void _XtDefaultErrorMsg (
    String name,
    String type,
    String class,
    String defaultp,
    String* params,
    Cardinal* num_params)
{
    DefaultMsg (name,type,class,defaultp,params,num_params,True,XtError);
}

void _XtDefaultWarningMsg (
    String name,
    String type,
    String class,
    String defaultp,
    String* params,
    Cardinal* num_params)
{
    DefaultMsg (name,type,class,defaultp,params,num_params,False,XtWarning);
}

void XtErrorMsg(
    _Xconst char* name,
    _Xconst char* type,
    _Xconst char* class,
    _Xconst char* defaultp,
    String* params,
    Cardinal* num_params)
{
#if GLOBALERRORS
    LOCK_PROCESS;
    (*errorMsgHandler)((String)name,(String)type,(String)class,
		       (String)defaultp,params,num_params);
    UNLOCK_PROCESS;
#else
    XtAppErrorMsg(_XtDefaultAppContext(),name,type,class,
	    defaultp,params,num_params);
#endif /* GLOBALERRORS */
}

void XtAppErrorMsg(
    XtAppContext app,
    _Xconst char* name,
    _Xconst char* type,
    _Xconst char* class,
    _Xconst char* defaultp,
    String* params,
    Cardinal* num_params)
{
#if GLOBALERRORS
    LOCK_PROCESS;
    (*errorMsgHandler)((String)name,(String)type,(String)class,
		       (String)defaultp,params,num_params);
    UNLOCK_PROCESS;
#else
    LOCK_APP(app);
    (*app->errorMsgHandler)(name,type,class,defaultp,params,num_params);
    UNLOCK_APP(app);
#endif /* GLOBALERRORS */
}

void XtWarningMsg(
    _Xconst char* name,
    _Xconst char* type,
    _Xconst char* class,
    _Xconst char* defaultp,
    String* params,
    Cardinal* num_params)
{
#if GLOBALERRORS
    LOCK_PROCESS;
    (*warningMsgHandler)((String)name,(String)type,(String)class,
			 (String)defaultp,params,num_params);
    UNLOCK_PROCESS;
#else
    XtAppWarningMsg(_XtDefaultAppContext(),name,type,class,
	    defaultp,params,num_params);
#endif /* GLOBALERRORS */
}

void XtAppWarningMsg(
    XtAppContext app,
    _Xconst char* name,
    _Xconst char* type,
    _Xconst char* class,
    _Xconst char* defaultp,
    String* params,
    Cardinal* num_params)
{
#if GLOBALERRORS
    LOCK_PROCESS;
    (*warningMsgHandler)((String)name,(String)type,(String)class,
			 (String)defaultp,params,num_params);
    UNLOCK_PROCESS;
#else
    LOCK_APP(app);
    (*app->warningMsgHandler)(name,type,class,defaultp,params,num_params);
    UNLOCK_APP(app);
#endif /* GLOBALERRORS */
}

void XtSetErrorMsgHandler(
    XtErrorMsgHandler handler)
{
#if GLOBALERRORS
    LOCK_PROCESS;
    if (handler != NULL) errorMsgHandler = handler;
    else errorMsgHandler  = _XtDefaultErrorMsg;
    UNLOCK_PROCESS;
#else
    XtAppSetErrorMsgHandler(_XtDefaultAppContext(), handler);
#endif /* GLOBALERRORS */
}

XtErrorMsgHandler XtAppSetErrorMsgHandler(
    XtAppContext app,
    XtErrorMsgHandler handler)
{
    XtErrorMsgHandler old;
#if GLOBALERRORS
    LOCK_PROCESS;
    old = errorMsgHandler;
    if (handler != NULL) errorMsgHandler = handler;
    else errorMsgHandler  = _XtDefaultErrorMsg;
    UNLOCK_PROCESS;
#else
    LOCK_APP(app);
    old = app->errorMsgHandler;
    if (handler != NULL) app->errorMsgHandler = handler;
    else app->errorMsgHandler  = _XtDefaultErrorMsg;
    UNLOCK_APP(app);
#endif /* GLOBALERRORS */
    return old;
}

void XtSetWarningMsgHandler(
    XtErrorMsgHandler handler)
{
#if GLOBALERRORS
    LOCK_PROCESS;
    if (handler != NULL) warningMsgHandler  = handler;
    else warningMsgHandler = _XtDefaultWarningMsg;
    UNLOCK_PROCESS;
#else
    XtAppSetWarningMsgHandler(_XtDefaultAppContext(),handler);
#endif /* GLOBALERRORS */
}

XtErrorMsgHandler XtAppSetWarningMsgHandler(
    XtAppContext app,
    XtErrorMsgHandler handler)
{
    XtErrorMsgHandler old;
#if GLOBALERRORS
    LOCK_PROCESS;
    old = warningMsgHandler;
    if (handler != NULL) warningMsgHandler  = handler;
    else warningMsgHandler = _XtDefaultWarningMsg;
    UNLOCK_PROCESS;
#else
    LOCK_APP(app);
    old = app->warningMsgHandler;
    if (handler != NULL) app->warningMsgHandler  = handler;
    else app->warningMsgHandler = _XtDefaultWarningMsg;
    UNLOCK_APP(app);
#endif /* GLOBALERRORS */
    return old;
}

void _XtDefaultError(String message)
{
    if (message && *message)
	(void)fprintf(stderr, "%sError: %s\n", XTERROR_PREFIX, message);
    exit(1);
}

void _XtDefaultWarning(String message)
{
    if (message && *message)
       (void)fprintf(stderr, "%sWarning: %s\n", XTWARNING_PREFIX, message);
    return;
}

void XtError(
    _Xconst char* message)
{
#if GLOBALERRORS
    LOCK_PROCESS;
    (*errorHandler)((String)message);
    UNLOCK_PROCESS;
#else
    XtAppError(_XtDefaultAppContext(),message);
#endif /* GLOBALERRORS */
}

void XtAppError(
    XtAppContext app,
    _Xconst char* message)
{
#if GLOBALERRORS
    LOCK_PROCESS;
    (*errorHandler)((String)message);
    UNLOCK_PROCESS;
#else
    LOCK_APP(app);
    (*app->errorHandler)(message);
    UNLOCK_APP(app);
#endif /* GLOBALERRORS */
}

void XtWarning(
    _Xconst char* message)
{
#if GLOBALERRORS
    LOCK_PROCESS;
    (*warningHandler)((String)message);
    UNLOCK_PROCESS;
#else
    XtAppWarning(_XtDefaultAppContext(),message);
#endif /* GLOBALERRORS */
}

void XtAppWarning(
    XtAppContext app,
    _Xconst char* message)
{
#if GLOBALERRORS
    LOCK_PROCESS;
    (*warningHandler)((String)message);
    UNLOCK_PROCESS;
#else
    LOCK_APP(app);
    (*app->warningHandler)(message);
    UNLOCK_APP(app);
#endif /* GLOBALERRORS */
}

void XtSetErrorHandler(XtErrorHandler handler)
{
#if GLOBALERRORS
    LOCK_PROCESS;
    if (handler != NULL) errorHandler = handler;
    else errorHandler  = _XtDefaultError;
    UNLOCK_PROCESS;
#else
    XtAppSetErrorHandler(_XtDefaultAppContext(),handler);
#endif /* GLOBALERRORS */
}

XtErrorHandler XtAppSetErrorHandler(
    XtAppContext app,
    XtErrorHandler handler)
{
    XtErrorHandler old;
#if GLOBALERRORS
    LOCK_PROCESS;
    old = errorHandler;
    if (handler != NULL) errorHandler = handler;
    else errorHandler  = _XtDefaultError;
    UNLOCK_PROCESS;
#else
    LOCK_APP(app);
    old = app->errorHandler;
    if (handler != NULL) app->errorHandler = handler;
    else app->errorHandler  = _XtDefaultError;
    UNLOCK_APP(app);
#endif /* GLOBALERRORS */
    return old;
}

void XtSetWarningHandler(XtErrorHandler handler)
{
#if GLOBALERRORS
    LOCK_PROCESS;
    if (handler != NULL) warningHandler = handler;
    else warningHandler = _XtDefaultWarning;
    UNLOCK_PROCESS;
#else
    XtAppSetWarningHandler(_XtDefaultAppContext(),handler);
#endif /* GLOBALERRORS */
}

XtErrorHandler XtAppSetWarningHandler(
    XtAppContext app,
    XtErrorHandler handler)
{
    XtErrorHandler old;
#if GLOBALERRORS
    LOCK_PROCESS;
    old = warningHandler;
    if (handler != NULL) warningHandler  = handler;
    else warningHandler = _XtDefaultWarning;
    UNLOCK_PROCESS;
#else
    LOCK_APP(app);
    old = app->warningHandler;
    if (handler != NULL) app->warningHandler  = handler;
    else app->warningHandler = _XtDefaultWarning;
    UNLOCK_APP(app);
#endif /* GLOBALERRORS */
    return old;
}

void _XtSetDefaultErrorHandlers(
    XtErrorMsgHandler *errMsg,
    XtErrorMsgHandler *warnMsg,
    XtErrorHandler *err,
    XtErrorHandler *warn)
{
#ifndef GLOBALERRORS
    LOCK_PROCESS;
    *errMsg = _XtDefaultErrorMsg;
    *warnMsg = _XtDefaultWarningMsg;
    *err = _XtDefaultError;
    *warn = _XtDefaultWarning;
    UNLOCK_PROCESS;
#endif /* GLOBALERRORS */
}
