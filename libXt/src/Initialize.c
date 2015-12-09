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

/* Make sure all wm properties can make it out of the resource manager */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "IntrinsicI.h"
#include "StringDefs.h"
#include "CoreP.h"
#include "ShellP.h"
#include <stdio.h>
#include <X11/Xlocale.h>
#ifdef XTHREADS
#include <X11/Xthreads.h>
#endif
#ifndef WIN32
#define X_INCLUDE_PWD_H
#define XOS_USE_XT_LOCKING
#include <X11/Xos_r.h>
#endif

#include <stdlib.h>

#if (defined(SUNSHLIB) || defined(AIXSHLIB)) && defined(SHAREDCODE)
/*
 * If used as a shared library, generate code under a different name so that
 * the stub routines in sharedlib.c get loaded into the application binary.
 */
#define XtToolkitInitialize _XtToolkitInitialize
#define XtOpenApplication _XtOpenApplication
#define XtAppInitialize _XtAppInitialize
#define XtInitialize _XtInitialize
#endif /* (SUNSHLIB || AIXSHLIB) && SHAREDCODE */

/*
 * hpux
 * Hand-patched versions of HP-UX prior to version 7.0 can usefully add
 * -DUSE_UNAME in the appropriate config file to get long hostnames.
 */

#ifdef USG
#define USE_UNAME
#endif

#ifdef USE_UNAME
#include <sys/utsname.h>
#endif

/* some unspecified magic number of expected search levels for Xrm */
#define SEARCH_LIST_SIZE 1000

/*
 This is a set of default records describing the command line arguments that
 Xlib will parse and set into the resource data base.

 This list is applied before the users list to enforce these defaults.  This is
 policy, which the toolkit avoids but I hate differing programs at this level.
*/

static XrmOptionDescRec const opTable[] = {
{"+rv",		"*reverseVideo", XrmoptionNoArg,	(XtPointer) "off"},
{"+synchronous","*synchronous",	XrmoptionNoArg,		(XtPointer) "off"},
{"-background",	"*background",	XrmoptionSepArg,	(XtPointer) NULL},
{"-bd",		"*borderColor",	XrmoptionSepArg,	(XtPointer) NULL},
{"-bg",		"*background",	XrmoptionSepArg,	(XtPointer) NULL},
{"-bordercolor","*borderColor",	XrmoptionSepArg,	(XtPointer) NULL},
{"-borderwidth",".borderWidth",	XrmoptionSepArg,	(XtPointer) NULL},
{"-bw",		".borderWidth",	XrmoptionSepArg,	(XtPointer) NULL},
{"-display",	".display",     XrmoptionSepArg,	(XtPointer) NULL},
{"-fg",		"*foreground",	XrmoptionSepArg,	(XtPointer) NULL},
{"-fn",		"*font",	XrmoptionSepArg,	(XtPointer) NULL},
{"-font",	"*font",	XrmoptionSepArg,	(XtPointer) NULL},
{"-foreground",	"*foreground",	XrmoptionSepArg,	(XtPointer) NULL},
{"-geometry",	".geometry",	XrmoptionSepArg,	(XtPointer) NULL},
{"-iconic",	".iconic",	XrmoptionNoArg,		(XtPointer) "on"},
{"-name",	".name",	XrmoptionSepArg,	(XtPointer) NULL},
{"-reverse",	"*reverseVideo", XrmoptionNoArg,	(XtPointer) "on"},
{"-rv",		"*reverseVideo", XrmoptionNoArg,	(XtPointer) "on"},
{"-selectionTimeout",
		".selectionTimeout", XrmoptionSepArg,	(XtPointer) NULL},
{"-synchronous","*synchronous",	XrmoptionNoArg,		(XtPointer) "on"},
{"-title",	".title",	XrmoptionSepArg,	(XtPointer) NULL},
{"-xnllanguage",".xnlLanguage",	XrmoptionSepArg,	(XtPointer) NULL},
{"-xrm",	NULL,		XrmoptionResArg,	(XtPointer) NULL},
{"-xtsessionID",".sessionID",	XrmoptionSepArg,	(XtPointer) NULL},
};


/*
 * GetHostname - emulates gethostname() on non-bsd systems.
 */

static void GetHostname (
    char *buf,
    int maxlen)
{
#ifdef USE_UNAME
    int len;
    struct utsname name;

    if (maxlen <= 0 || buf == NULL)
	return;

    uname (&name);
    len = strlen (name.nodename);
    if (len >= maxlen) len = maxlen;
    (void) strncpy (buf, name.nodename, len-1);
    buf[len-1] = '\0';
#else
    if (maxlen <= 0 || buf == NULL)
	return;

    buf[0] = '\0';
    (void) gethostname (buf, maxlen);
    buf [maxlen - 1] = '\0';
#endif
}


#ifdef SUNSHLIB
void _XtInherit(void)
{
    extern void __XtInherit();
    __XtInherit();
}
#define _XtInherit __XtInherit
#endif


#if defined (WIN32) || defined(__CYGWIN__)
/*
 * The Symbol _XtInherit is used in two different manners.
 * First it could be used as a generic function and second
 * as an absolute address reference, which will be used to
 * check the initialisation process of several other libraries.
 * Because of this the symbol must be accessable by all
 * client dll's and applications.  In unix environments
 * this is no problem, because the used shared libraries
 * format (elf) supports this immediatly.  Under Windows
 * this isn't true, because a functions address in a dll
 * is different from the same function in another dll or
 * applications, because the used Portable Executable
 * File adds a code stub to each client to provide the
 * exported symbol name.  This stub uses an indirect
 * pointer to get the original symbol address, which is
 * then jumped to, like in this example:
 *
 * --- client ---                                     --- dll ----
 *  ...
 *  call foo
 *
 * foo: jmp (*_imp_foo)               ---->           foo: ....
 *      nop
 *      nop
 *
 * _imp_foo: .long <index of foo in dll export table, is
 *		    set to the real address by the runtime linker>
 *
 * Now it is clear why the clients symbol foo isn't the same
 * as in the dll and we can think about how to deal which
 * this two above mentioned requirements, to export this
 * symbol to all clients and to allow calling this symbol
 * as a function.  The solution I've used exports the
 * symbol _XtInherit as data symbol, because global data
 * symbols are exported to all clients.  But how to deal
 * with the second requirement, that this symbol should
 * be used as function.  The Trick is to build a little
 * code stub in the data section in the exact manner as
 * above explained.  This is done with the assembler code
 * below.
 *
 * Ralf Habacker
 *
 * References:
 * msdn          http://msdn.microsoft.com/msdnmag/issues/02/02/PE/PE.asp
 * cygwin-xfree: http://www.cygwin.com/ml/cygwin-xfree/2003-10/msg00000.html
 */

#ifdef __x86_64__
asm (".section .trampoline, \"dwx\" \n\
 .globl _XtInherit        \n\
 _XtInherit:              \n\
    jmp *_y(%rip)         \n\
_y: .quad __XtInherit     \n\
    .text                 \n");
#else
asm (".data\n\
 .globl __XtInherit        \n\
 __XtInherit:      jmp *_y \n\
  _y: .long ___XtInherit   \n\
    .text                 \n");
#endif

#define _XtInherit __XtInherit
#endif


void _XtInherit(void)
{
    XtErrorMsg("invalidProcedure","inheritanceProc",XtCXtToolkitError,
            "Unresolved inheritance operation",
              (String *)NULL, (Cardinal *)NULL);
}


void XtToolkitInitialize(void)
{
    static Boolean initialized = False;

    LOCK_PROCESS;
    if (initialized) {
	UNLOCK_PROCESS;
	return;
    }
    initialized = True;
    UNLOCK_PROCESS;
    /* Resource management initialization */
    XrmInitialize();
    _XtResourceListInitialize();

    /* Other intrinsic intialization */
    _XtConvertInitialize();
    _XtEventInitialize();
    _XtTranslateInitialize();

    /* Some apps rely on old (broken) XtAppPeekEvent behavior */
    if(getenv("XTAPPPEEKEVENT_SKIPTIMER"))
	XtAppPeekEvent_SkipTimer = True;
    else
	XtAppPeekEvent_SkipTimer = False;
}


String _XtGetUserName(
    String dest,
    int len)
{
#ifdef WIN32
    String ptr = NULL;

    if ((ptr = getenv("USERNAME"))) {
	(void) strncpy (dest, ptr, len-1);
	dest[len-1] = '\0';
    } else
	*dest = '\0';
#else
#ifdef X_NEEDS_PWPARAMS
    _Xgetpwparams pwparams;
#endif
    struct passwd *pw;
    char* ptr;

    if ((ptr = getenv("USER"))) {
	(void) strncpy (dest, ptr, len-1);
	dest[len-1] = '\0';
    } else {
	if ((pw = _XGetpwuid(getuid(),pwparams)) != NULL) {
	    (void) strncpy (dest, pw->pw_name, len-1);
	    dest[len-1] = '\0';
	} else
	    *dest = '\0';
    }
#endif
    return dest;
}


static String GetRootDirName(
    String dest,
    int len)
{
#ifdef WIN32
    register char *ptr1;
    register char *ptr2 = NULL;
    int len1 = 0, len2 = 0;

    if (ptr1 = getenv("HOME")) {	/* old, deprecated */
	len1 = strlen (ptr1);
    } else if ((ptr1 = getenv("HOMEDRIVE")) && (ptr2 = getenv("HOMEDIR"))) {
	len1 = strlen (ptr1);
	len2 = strlen (ptr2);
    } else if (ptr2 = getenv("USERNAME")) {
	len1 = strlen (ptr1 = "/users/");
	len2 = strlen (ptr2);
    }
    if ((len1 + len2 + 1) < len)
	sprintf (dest, "%s%s", ptr1, (ptr2) ? ptr2 : "");
    else
	*dest = '\0';
#else
#ifdef X_NEEDS_PWPARAMS
    _Xgetpwparams pwparams;
#endif
    struct passwd *pw;
    static char *ptr;

    if (len <= 0 || dest == NULL)
	return NULL;

    if ((ptr = getenv("HOME"))) {
	(void) strncpy (dest, ptr, len-1);
	dest[len-1] = '\0';
    } else {
	if ((ptr = getenv("USER")))
	    pw = _XGetpwnam(ptr,pwparams);
	else
 	    pw = _XGetpwuid(getuid(),pwparams);
	if (pw != NULL) {
	    (void) strncpy (dest, pw->pw_dir, len-1);
	    dest[len-1] = '\0';
	} else
	    *dest = '\0';
    }
#endif
    return dest;
}

static void CombineAppUserDefaults(
    Display *dpy,
    XrmDatabase *pdb)
{
    char* filename;
    char* path = NULL;
    Boolean deallocate = False;

    if (!(path = getenv("XUSERFILESEARCHPATH"))) {
#if !defined(WIN32) || !defined(__MINGW32__)
	char *old_path;
	char homedir[PATH_MAX];
	GetRootDirName(homedir, PATH_MAX);
	if (!(old_path = getenv("XAPPLRESDIR"))) {
	    XtAsprintf(&path,
		       "%s/%%L/%%N%%C:%s/%%l/%%N%%C:%s/%%N%%C:%s/%%L/%%N:%s/%%l/%%N:%s/%%N",
		       homedir, homedir, homedir, homedir, homedir, homedir);
	} else {
	    XtAsprintf(&path,
		       "%s/%%L/%%N%%C:%s/%%l/%%N%%C:%s/%%N%%C:%s/%%N%%C:%s/%%L/%%N:%s/%%l/%%N:%s/%%N:%s/%%N",
		       old_path, old_path, old_path, homedir,
		       old_path, old_path, old_path, homedir);
	}
	deallocate = True;
#endif
    }

    filename = XtResolvePathname(dpy, NULL, NULL, NULL, path, NULL, 0, NULL);
    if (filename) {
	(void)XrmCombineFileDatabase(filename, pdb, False);
	XtFree(filename);
    }

    if (deallocate)
	XtFree(path);
}

static void CombineUserDefaults(
    Display *dpy,
    XrmDatabase *pdb)
{
#ifdef __MINGW32__
    const char *slashDotXdefaults = "/Xdefaults";
#else
    const char *slashDotXdefaults = "/.Xdefaults";
#endif
    char *dpy_defaults = XResourceManagerString(dpy);

    if (dpy_defaults) {
	XrmCombineDatabase(XrmGetStringDatabase(dpy_defaults), pdb, False);
    } else {
	char filename[PATH_MAX];
	(void) GetRootDirName(filename,
			PATH_MAX - strlen (slashDotXdefaults) - 1);
	(void) strcat(filename, slashDotXdefaults);
	(void)XrmCombineFileDatabase(filename, pdb, False);
    }
}

/*ARGSUSED*/
static Bool StoreDBEntry(
    XrmDatabase		*db,
    XrmBindingList      bindings,
    XrmQuarkList	quarks,
    XrmRepresentation   *type,
    XrmValuePtr		value,
    XPointer		data)
{
    XrmQPutResource((XrmDatabase *)data, bindings, quarks, *type, value);
    return False;
}

static XrmDatabase CopyDB(XrmDatabase db)
{
    XrmDatabase copy = NULL;
    XrmQuark empty = NULLQUARK;

    XrmEnumerateDatabase(db, &empty, &empty, XrmEnumAllLevels,
			 StoreDBEntry, (XPointer)&copy);
    return copy;
}

/*ARGSUSED*/
static String _XtDefaultLanguageProc(
    Display   *dpy,	/* unused */
    String     xnl,
    XtPointer  closure)	/* unused */
{
    if (! setlocale(LC_ALL, xnl))
	XtWarning("locale not supported by C library, locale unchanged");

    if (! XSupportsLocale()) {
	XtWarning("locale not supported by Xlib, locale set to C");
	setlocale(LC_ALL, "C");
    }
    if (! XSetLocaleModifiers(""))
	XtWarning("X locale modifiers not supported, using default");

    return setlocale(LC_ALL, NULL); /* re-query in case overwritten */
}

XtLanguageProc XtSetLanguageProc(
    XtAppContext      app,
    XtLanguageProc    proc,
    XtPointer         closure)
{
    XtLanguageProc    old;

    if (!proc) {
	proc = _XtDefaultLanguageProc;
	closure = NULL;
    }

    if (app) {
	LOCK_APP(app);
	LOCK_PROCESS;
	/* set langProcRec only for this application context */
        old = app->langProcRec.proc;
        app->langProcRec.proc = proc;
        app->langProcRec.closure = closure;
	UNLOCK_PROCESS;
	UNLOCK_APP(app);
    } else {
	/* set langProcRec for all application contexts */
        ProcessContext process;

	LOCK_PROCESS;
        process = _XtGetProcessContext();
        old = process->globalLangProcRec.proc;
	process->globalLangProcRec.proc = proc;
	process->globalLangProcRec.closure = closure;
        app = process->appContextList;
        while (app) {
            app->langProcRec.proc = proc;
            app->langProcRec.closure = closure;
	    app = app->next;
        }
	UNLOCK_PROCESS;
    }
    return (old ? old : _XtDefaultLanguageProc);
}

XrmDatabase XtScreenDatabase(
    Screen *screen)
{
    int scrno;
    Bool doing_def;
    XrmDatabase db, olddb;
    XtPerDisplay pd;
    Status do_fallback;
    char *scr_resources;
    Display *dpy = DisplayOfScreen(screen);
    DPY_TO_APPCON(dpy);

    LOCK_APP(app);
    LOCK_PROCESS;
    if (screen == DefaultScreenOfDisplay(dpy)) {
	scrno = DefaultScreen(dpy);
	doing_def = True;
    } else {
	scrno = XScreenNumberOfScreen(screen);
	doing_def = False;
    }
    pd = _XtGetPerDisplay(dpy);
    if ((db = pd->per_screen_db[scrno])) {
	UNLOCK_PROCESS;
	UNLOCK_APP(app);
	return (doing_def ? XrmGetDatabase(dpy) : db);
    }
    scr_resources = XScreenResourceString(screen);

    if (ScreenCount(dpy) == 1) {
	db = pd->cmd_db;
	pd->cmd_db = NULL;
    } else {
	db = CopyDB(pd->cmd_db);
    }
    {   /* Environment defaults */
	char	filenamebuf[PATH_MAX];
	char	*filename;

	if (!(filename = getenv("XENVIRONMENT"))) {
	    int len;
#ifdef __MINGW32__
	    const char *slashDotXdefaultsDash = "/Xdefaults-";
#else
	    const char *slashDotXdefaultsDash = "/.Xdefaults-";
#endif

	    (void) GetRootDirName(filename = filenamebuf,
			PATH_MAX - strlen (slashDotXdefaultsDash) - 1);
	    (void) strcat(filename, slashDotXdefaultsDash);
	    len = strlen(filename);
	    GetHostname (filename+len, PATH_MAX-len);
	}
	(void)XrmCombineFileDatabase(filename, &db, False);
    }
    if (scr_resources)
    {   /* Screen defaults */
	XrmCombineDatabase(XrmGetStringDatabase(scr_resources), &db, False);
	XFree(scr_resources);
    }
    /* Server or host defaults */
    if (!pd->server_db)
	CombineUserDefaults(dpy, &db);
    else {
	(void) XrmCombineDatabase(pd->server_db, &db, False);
	pd->server_db = NULL;
    }

    if (!db)
	db = XrmGetStringDatabase("");
    pd->per_screen_db[scrno] = db;
    olddb = XrmGetDatabase(dpy);
    /* set database now, for XtResolvePathname to use */
    XrmSetDatabase(dpy, db);
    CombineAppUserDefaults(dpy, &db);
    do_fallback = 1;
    {   /* System app-defaults */
	char	*filename;

	if ((filename = XtResolvePathname(dpy, "app-defaults",
					 NULL, NULL, NULL, NULL, 0, NULL))) {
	    do_fallback = !XrmCombineFileDatabase(filename, &db, False);
	    XtFree(filename);
	}
    }
    /* now restore old database, if need be */
    if (!doing_def)
	XrmSetDatabase(dpy, olddb);
    if (do_fallback && pd->appContext->fallback_resources)
    {   /* Fallback defaults */
        XrmDatabase fdb = NULL;
	String *res;

	for (res = pd->appContext->fallback_resources; *res; res++)
	    XrmPutLineResource(&fdb, *res);
	(void)XrmCombineDatabase(fdb, &db, False);
    }
    UNLOCK_PROCESS;
    UNLOCK_APP(app);
    return db;
}

/*
 * Merge two option tables, allowing the second to over-ride the first,
 * so that ambiguous abbreviations can be noticed.  The merge attempts
 * to make the resulting table lexicographically sorted, but succeeds
 * only if the first source table is sorted.  Though it _is_ recommended
 * (for optimizations later in XrmParseCommand), it is not required
 * that either source table be sorted.
 *
 * Caller is responsible for freeing the returned option table.
 */

static void _MergeOptionTables(
    const XrmOptionDescRec *src1,
    Cardinal num_src1,
    const XrmOptionDescRec *src2,
    Cardinal num_src2,
    XrmOptionDescRec **dst,
    Cardinal *num_dst)
{
    XrmOptionDescRec *table, *endP;
    register XrmOptionDescRec *opt1, *whereP, *dstP;
    register const XrmOptionDescRec *opt2;
    int i1;
    Cardinal i2;
    int dst_len, order;
    Boolean found;
    enum {Check, NotSorted, IsSorted} sort_order = Check;

    *dst = table = (XrmOptionDescRec*)
	__XtMalloc( sizeof(XrmOptionDescRec) * (num_src1 + num_src2) );

    (void) memmove(table, src1, sizeof(XrmOptionDescRec) * num_src1 );
    if (num_src2 == 0) {
	*num_dst = num_src1;
	return;
    }
    endP = &table[dst_len = num_src1];
    for (opt2 = src2, i2= 0; i2 < num_src2; opt2++, i2++) {
	found = False;
	whereP = endP-1;	/* assume new option goes at the end */
	for (opt1 = table, i1 = 0; i1 < dst_len; opt1++, i1++) {
	    /* have to walk the entire new table so new list is ordered
	       (if src1 was ordered) */
	    if (sort_order == Check && i1 > 0
		&& strcmp(opt1->option, (opt1-1)->option) < 0)
		sort_order = NotSorted;
	    if ((order = strcmp(opt1->option, opt2->option)) == 0) {
		/* same option names; just overwrite opt1 with opt2 */
		*opt1 = *opt2;
		found = True;
		break;
		}
	    /* else */
	    if (sort_order == IsSorted && order > 0) {
		/* insert before opt1 to preserve order */
		/* shift rest of table forward to make room for new entry */
		for (dstP = endP++; dstP > opt1; dstP--)
		    *dstP = *(dstP-1);
		*opt1 = *opt2;
		dst_len++;
		found = True;
		break;
	    }
	    /* else */
	    if (order < 0)
		/* opt2 sorts after opt1, so remember this position */
		whereP = opt1;
	}
	if (sort_order == Check && i1 == dst_len)
	    sort_order = IsSorted;
	if (!found) {
	   /* when we get here, whereP points to the last entry in the
	      destination that sorts before "opt2".  Shift rest of table
	      forward and insert "opt2" after whereP. */
	    whereP++;
	    for (dstP = endP++; dstP > whereP; dstP--)
		*dstP = *(dstP-1);
	    *whereP = *opt2;
	    dst_len++;
	}
    }
    *num_dst = dst_len;
}


/* NOTE: name, class, and type must be permanent strings */
static Boolean _GetResource(
    Display *dpy,
    XrmSearchList list,
    String name,
    String class,
    String type,
    XrmValue* value)
{
    XrmRepresentation db_type;
    XrmValue db_value;
    XrmName Qname = XrmPermStringToQuark(name);
    XrmClass Qclass = XrmPermStringToQuark(class);
    XrmRepresentation Qtype = XrmPermStringToQuark(type);

    if (XrmQGetSearchResource(list, Qname, Qclass, &db_type, &db_value)) {
	if (db_type == Qtype) {
	    if (Qtype == _XtQString)
		*(String*)value->addr = db_value.addr;
	    else
		(void) memmove(value->addr, db_value.addr, value->size );
	    return True;
	} else {
	    WidgetRec widget; /* hack, hack */
	    bzero( &widget, sizeof(widget) );
	    widget.core.self = &widget;
	    widget.core.widget_class = coreWidgetClass;
	    widget.core.screen = (Screen*)DefaultScreenOfDisplay(dpy);
	    XtInitializeWidgetClass(coreWidgetClass);
	    if (_XtConvert(&widget,db_type,&db_value,Qtype,value,NULL)) {
		return True;
	    }
	}
    }
    return False;
}

XrmDatabase _XtPreparseCommandLine(
    XrmOptionDescRec *urlist,
    Cardinal num_urs,
    int argc,
    String *argv,
    /* return */
    String *applName,
    String *displayName,
    String *language)
{
    XrmDatabase db = NULL;
    XrmOptionDescRec *options;
    Cardinal num_options;
    XrmName name_list[3];
    XrmName class_list[3];
    XrmRepresentation type;
    XrmValue val;
    String *targv;
    int targc = argc;

    targv = (String *) __XtMalloc(sizeof(char *) * argc);
    (void) memmove(targv, argv, sizeof(char *) * argc);
    _MergeOptionTables(opTable, XtNumber(opTable), urlist, num_urs,
		       &options, &num_options);
    name_list[0] = class_list[0] = XrmPermStringToQuark(".");
    name_list[2] = class_list[2] = NULLQUARK;
    XrmParseCommand(&db, options, num_options, ".", &targc, targv);
    if (applName) {
	name_list[1] = XrmPermStringToQuark("name");
	if (XrmQGetResource(db, name_list, name_list, &type, &val) &&
	    type == _XtQString)
	    *applName = val.addr;
    }
    if (displayName) {
	name_list[1] = XrmPermStringToQuark("display");
	if (XrmQGetResource(db, name_list, name_list, &type, &val) &&
	    type == _XtQString)
	    *displayName = val.addr;
    }
    if (language) {
	name_list[1] = XrmPermStringToQuark("xnlLanguage");
	class_list[1] = XrmPermStringToQuark("XnlLanguage");
	if (XrmQGetResource(db, name_list, class_list, &type, &val) &&
	    type == _XtQString)
	    *language = val.addr;
    }

    XtFree((char *)targv);
    XtFree((char *)options);
    return db;
}


static void GetLanguage(
    Display *dpy,
    XtPerDisplay pd)
{
    XrmRepresentation type;
    XrmValue value;
    XrmName name_list[3];
    XrmName class_list[3];

    LOCK_PROCESS;
    if (! pd->language) {
	name_list[0] = pd->name;
	name_list[1] = XrmPermStringToQuark("xnlLanguage");
	class_list[0] = pd->class;
	class_list[1] = XrmPermStringToQuark("XnlLanguage");
	name_list[2] = class_list[2] = NULLQUARK;
	if (!pd->server_db)
	    CombineUserDefaults(dpy, &pd->server_db);
	if (pd->server_db &&
	    XrmQGetResource(pd->server_db,name_list,class_list, &type, &value)
	    && type == _XtQString)
	    pd->language = (char *) value.addr;
    }

    if (pd->appContext->langProcRec.proc) {
	if (! pd->language) pd->language = "";
	pd->language = (*pd->appContext->langProcRec.proc)
	    (dpy, pd->language, pd->appContext->langProcRec.closure);
    }
    else if (! pd->language || pd->language[0] == '\0') /* R4 compatibility */
	pd->language = getenv("LANG");

    if (pd->language) pd->language = XtNewString(pd->language);
    UNLOCK_PROCESS;
}

static void ProcessInternalConnection (
    XtPointer client_data,
    int* fd,
    XtInputId* id)
{
    XProcessInternalConnection ((Display *) client_data, *fd);
}

static void ConnectionWatch (
    Display* dpy,
    XPointer client_data,
    int fd,
    Bool opening,
    XPointer* watch_data)
{
    XtInputId* iptr;
    XtAppContext app = XtDisplayToApplicationContext(dpy);

    if (opening) {
	iptr = (XtInputId *) __XtMalloc(sizeof(XtInputId));
	*iptr = XtAppAddInput(app, fd, (XtPointer) XtInputReadMask,
			      ProcessInternalConnection, client_data);
	*watch_data = (XPointer) iptr;
    } else {
	iptr = (XtInputId *) *watch_data;
	XtRemoveInput(*iptr);
        (void) XtFree(*watch_data);
    }
}

void _XtDisplayInitialize(
	Display *dpy,
        XtPerDisplay pd,
	_Xconst char* name,
	XrmOptionDescRec *urlist,
	Cardinal num_urs,
	int *argc,
	char **argv)
{
	Boolean tmp_bool;
	XrmValue value;
	XrmOptionDescRec *options;
	Cardinal num_options;
	XrmDatabase db;
	XrmName name_list[2];
	XrmClass class_list[2];
	XrmHashTable* search_list;
	int search_list_size = SEARCH_LIST_SIZE;

	GetLanguage(dpy, pd);

	/* Parse the command line and remove Xt arguments from argv */
	_MergeOptionTables( opTable, XtNumber(opTable), urlist, num_urs,
			    &options, &num_options );
	XrmParseCommand(&pd->cmd_db, options, num_options, name, argc, argv);

	db = XtScreenDatabase(DefaultScreenOfDisplay(dpy));

	if (!(search_list = (XrmHashTable*)
		       ALLOCATE_LOCAL( SEARCH_LIST_SIZE*sizeof(XrmHashTable))))
	    _XtAllocError(NULL);
	name_list[0] = pd->name;
	class_list[0] = pd->class;
	name_list[1] = NULLQUARK;
	class_list[1] = NULLQUARK;

	while (!XrmQGetSearchList(db, name_list, class_list,
				  search_list, search_list_size)) {
	    XrmHashTable* old = search_list;
	    Cardinal size = (search_list_size*=2)*sizeof(XrmHashTable);
	    if (!(search_list = (XrmHashTable*)ALLOCATE_LOCAL(size)))
		_XtAllocError(NULL);
	    (void) memmove((char*)search_list, (char*)old, (size>>1) );
	    DEALLOCATE_LOCAL(old);
	}

	value.size = sizeof(tmp_bool);
	value.addr = (XtPointer)&tmp_bool;
	if (_GetResource(dpy, search_list, "synchronous", "Synchronous",
			 XtRBoolean, &value)) {
	    int i;
	    Display **dpyP = pd->appContext->list;
	    pd->appContext->sync = tmp_bool;
	    for (i = pd->appContext->count; i; dpyP++, i--) {
		(void) XSynchronize(*dpyP, (Bool)tmp_bool);
	    }
	} else {
	    (void) XSynchronize(dpy, (Bool)pd->appContext->sync);
	}

	if (_GetResource(dpy, search_list, "reverseVideo", "ReverseVideo",
			 XtRBoolean, &value)
	        && tmp_bool) {
	    pd->rv = True;
	}

	value.size = sizeof(pd->multi_click_time);
	value.addr = (XtPointer)&pd->multi_click_time;
	if (!_GetResource(dpy, search_list,
			  "multiClickTime", "MultiClickTime",
			  XtRInt, &value)) {
	    pd->multi_click_time = 200;
	}

	value.size = sizeof(pd->appContext->selectionTimeout);
	value.addr = (XtPointer)&pd->appContext->selectionTimeout;
	(void)_GetResource(dpy, search_list,
			   "selectionTimeout", "SelectionTimeout",
			   XtRInt, &value);

#ifndef NO_IDENTIFY_WINDOWS
	value.size = sizeof(pd->appContext->identify_windows);
	value.addr = (XtPointer)&pd->appContext->identify_windows;
	(void)_GetResource(dpy, search_list,
			   "xtIdentifyWindows", "XtDebug",
			   XtRBoolean, &value);
#endif

	XAddConnectionWatch(dpy, ConnectionWatch, (XPointer) dpy);

	XtFree( (XtPointer)options );
	DEALLOCATE_LOCAL( search_list );
}

/*	Function Name: XtAppSetFallbackResources
 *	Description: Sets the fallback resource list that will be loaded
 *                   at display initialization time.
 *	Arguments: app_context - the app context.
 *                 specification_list - the resource specification list.
 *	Returns: none.
 */

void
XtAppSetFallbackResources(
    XtAppContext app_context,
    String *specification_list)
{
    LOCK_APP(app_context);
    app_context->fallback_resources = specification_list;
    UNLOCK_APP(app_context);
}


Widget XtOpenApplication(XtAppContext *app_context_return,
			 _Xconst char *application_class,
			 XrmOptionDescRec *options, Cardinal num_options,
			 int *argc_in_out, String *argv_in_out,
			 String *fallback_resources, WidgetClass widget_class,
			 ArgList args_in, Cardinal num_args_in)
{
    XtAppContext app_con;
    Display * dpy;
    register int saved_argc = *argc_in_out;
    Widget root;
    Arg args[3], *merged_args;
    Cardinal num = 0;

    XtToolkitInitialize(); /* cannot be moved into _XtAppInit */

    dpy = _XtAppInit(&app_con, (String)application_class, options, num_options,
		     argc_in_out, &argv_in_out, fallback_resources);

    LOCK_APP(app_con);
    XtSetArg(args[num], XtNscreen, DefaultScreenOfDisplay(dpy)); num++;
    XtSetArg(args[num], XtNargc, saved_argc);	                 num++;
    XtSetArg(args[num], XtNargv, argv_in_out);	                 num++;

    merged_args = XtMergeArgLists(args_in, num_args_in, args, num);
    num += num_args_in;

    root = XtAppCreateShell(NULL, application_class, widget_class, dpy,
			    merged_args, num);

    if (app_context_return)
	*app_context_return = app_con;

    XtFree((XtPointer)merged_args);
    XtFree((XtPointer)argv_in_out);
    UNLOCK_APP(app_con);
    return root;
}


Widget
XtAppInitialize(
    XtAppContext * app_context_return,
    _Xconst char* application_class,
    XrmOptionDescRec *options,
    Cardinal num_options,
    int *argc_in_out,
    String *argv_in_out,
    String *fallback_resources,
    ArgList args_in,
    Cardinal num_args_in)
{
    return XtOpenApplication(app_context_return, application_class,
			     options, num_options,
			     argc_in_out, argv_in_out, fallback_resources,
			     applicationShellWidgetClass,
			     args_in, num_args_in);
}


/*ARGSUSED*/
Widget
XtInitialize(
    _Xconst char* name,
    _Xconst char* classname,
    XrmOptionDescRec *options,
    Cardinal num_options,
    int *argc,
    String *argv)
{
    Widget root;
    XtAppContext app_con;
    register ProcessContext process = _XtGetProcessContext();

    root = XtAppInitialize(&app_con, classname, options, num_options,
			   argc, argv, NULL, NULL, (Cardinal) 0);

    LOCK_PROCESS;
    process->defaultAppContext = app_con;
    UNLOCK_PROCESS;
    return root;
}
