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

/*LINTLIBRARY*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "IntrinsicI.h"
#include "VarargsI.h"
#include "Shell.h"
#include "ShellP.h"
#include "StringDefs.h"
#include <stdio.h>

static XrmClass	QBoolean, QString, QCallProc, QImmediate;
static XrmName QinitialResourcesPersistent, QInitialResourcesPersistent;
static XrmClass QTranslations, QTranslationTable;
static XrmName Qtranslations, QbaseTranslations;
static XrmName Qscreen;
static XrmClass QScreen;


void _XtCopyFromParent(
    Widget      widget,
    int		offset,
    XrmValue    *value)
{
    if (widget->core.parent == NULL) {
	XtAppWarningMsg(XtWidgetToApplicationContext(widget),
		"invalidParent","xtCopyFromParent",XtCXtToolkitError,
                  "CopyFromParent must have non-NULL parent",
		  (String *)NULL, (Cardinal *)NULL);
        value->addr = NULL;
        return;
    }
    value->addr = (XPointer)(((char *)widget->core.parent) + offset);
} /* _XtCopyFromParent */


void _XtCopyFromArg(
    XtArgVal src,
    char* dst,
    register unsigned int size)
{
    if (size > sizeof(XtArgVal))
	(void) memmove((char *) dst, (char *)  src, (int) size);
    else {
	union {
	    long	longval;
#ifdef LONG64
	    int		intval;
#endif
	    short	shortval;
	    char	charval;
	    char*	charptr;
	    XtPointer	ptr;
	} u;
	char *p = (char*)&u;
	if	(size == sizeof(long))	    u.longval = (long)src;
#ifdef LONG64
	else if (size == sizeof(int))	    u.intval = (int)src;
#endif
	else if (size == sizeof(short))	    u.shortval = (short)src;
	else if (size == sizeof(char))	    u.charval = (char)src;
	else if (size == sizeof(XtPointer)) u.ptr = (XtPointer)src;
	else if (size == sizeof(char*))	    u.charptr = (char*)src;
	else				    p = (char*)&src;

	(void) memmove(dst, p, (int) size);
    }
} /* _XtCopyFromArg */

void _XtCopyToArg(
    char* src,
    XtArgVal *dst,
    register unsigned int size)
{
    if (!*dst) {
#ifdef GETVALUES_BUG
	/* old GetValues semantics (storing directly into arglists) are bad,
	 * but preserve for compatibility as long as arglist contains NULL.
	 */
	union {
	    long	longval;
#ifdef LONG64
	    int		intval;
#endif
	    short	shortval;
	    char	charval;
	    char*	charptr;
	    XtPointer	ptr;
	} u;
	if (size <= sizeof(XtArgVal)) {
	    (void) memmove((char*)&u, (char*)src, (int)size );
	    if	    (size == sizeof(long)) 	*dst = (XtArgVal)u.longval;
#ifdef LONG64
	    else if (size == sizeof(int))	*dst = (XtArgVal)u.intval;
#endif
	    else if (size == sizeof(short))	*dst = (XtArgVal)u.shortval;
	    else if (size == sizeof(char))	*dst = (XtArgVal)u.charval;
	    else if (size == sizeof(char*))	*dst = (XtArgVal)u.charptr;
	    else if (size == sizeof(XtPointer))	*dst = (XtArgVal)u.ptr;
	    else (void) memmove((char*)dst, (char*)src, (int)size );
	}
	else
	    (void) memmove((char*)dst, (char*)src, (int)size );
#else
	XtErrorMsg("invalidGetValues", "xtGetValues", XtCXtToolkitError,
	    "NULL ArgVal in XtGetValues", (String*) NULL, (Cardinal*) NULL);
#endif
    }
    else {
	/* proper GetValues semantics: argval is pointer to destination */
	(void) memmove((char*)*dst, (char*)src, (int)size );
    }
} /* _XtCopyToArg */

static void CopyToArg(
    char* src,
    XtArgVal *dst,
    register unsigned int size)
{
    if (!*dst) {
	/* old GetValues semantics (storing directly into arglists) are bad,
	 * but preserve for compatibility as long as arglist contains NULL.
	 */
	union {
	    long	longval;
#ifdef LONG64
	    int		intval;
#endif
	    short	shortval;
	    char	charval;
	    char*	charptr;
	    XtPointer	ptr;
	} u;
	if (size <= sizeof(XtArgVal)) {
	    (void) memmove((char*)&u, (char*)src, (int)size );
	    if	    (size == sizeof(long)) 	*dst = (XtArgVal)u.longval;
#ifdef LONG64
	    else if (size == sizeof(int))	*dst = (XtArgVal)u.intval;
#endif
	    else if (size == sizeof(short))	*dst = (XtArgVal)u.shortval;
	    else if (size == sizeof(char))	*dst = (XtArgVal)u.charval;
	    else if (size == sizeof(char*))	*dst = (XtArgVal)u.charptr;
	    else if (size == sizeof(XtPointer))	*dst = (XtArgVal)u.ptr;
	    else (void) memmove((char*)dst, (char*)src, (int)size );
	}
	else
	    (void) memmove((char*)dst, (char*)src, (int)size );
    }
    else {
	/* proper GetValues semantics: argval is pointer to destination */
	(void) memmove((char*)*dst, (char*)src, (int)size );
    }
} /* CopyToArg */


static Cardinal CountTreeDepth(
    Widget w)
{
    Cardinal count;

    for (count = 1; w != NULL; w = (Widget) w->core.parent)
	count++;

    return count;
}

static void GetNamesAndClasses(
    register Widget	  w,
    register XrmNameList  names,
    register XrmClassList classes)
{
    register Cardinal length, j;
    register XrmQuark t;
    WidgetClass class;

    /* Return null-terminated quark arrays, with length the number of
       quarks (not including NULL) */

    LOCK_PROCESS;
    for (length = 0; w != NULL; w = (Widget) w->core.parent) {
	names[length] = w->core.xrm_name;
	class = XtClass(w);
	/* KLUDGE KLUDGE KLUDGE KLUDGE */
	if (w->core.parent == NULL && XtIsApplicationShell(w)) {
	    classes[length] =
		((ApplicationShellWidget) w)->application.xrm_class;
	} else classes[length] = class->core_class.xrm_class;
	length++;
     }
    UNLOCK_PROCESS;
    /* They're in backwards order, flop them around */
    for (j = 0; j < length/2; j++) {
	t = names[j];
	names[j] = names[length-j-1];
	names[length-j-1] = t;
        t = classes[j];
	classes[j] = classes[length-j-1];
	classes[length-j-1] = t;
    }
    names[length] = NULLQUARK;
    classes[length] = NULLQUARK;
} /* GetNamesAndClasses */


/* Spiffy fast compiled form of resource list.				*/
/* XtResourceLists are compiled in-place into XrmResourceLists		*/
/* All atoms are replaced by quarks, and offsets are -offset-1 to	*/
/* indicate that this list has been compiled already			*/

void _XtCompileResourceList(
    register XtResourceList resources,
    	     Cardinal       num_resources)
{
    register Cardinal count;

#define xrmres  ((XrmResourceList) resources)
#define PSToQ   XrmPermStringToQuark

    for (count = 0; count < num_resources; resources++, count++) {
    	xrmres->xrm_name	 = PSToQ(resources->resource_name);
    	xrmres->xrm_class	 = PSToQ(resources->resource_class);
    	xrmres->xrm_type	 = PSToQ(resources->resource_type);
        xrmres->xrm_offset	 = (Cardinal)
		(-(int)resources->resource_offset - 1);
    	xrmres->xrm_default_type = PSToQ(resources->default_type);
    }
#undef PSToQ
#undef xrmres
} /* _XtCompileResourceList */

/* Like _XtCompileResourceList, but strings are not permanent */
static void  XrmCompileResourceListEphem(
    register XtResourceList resources,
    	     Cardinal       num_resources)
{
    register Cardinal count;

#define xrmres  ((XrmResourceList) resources)

    for (count = 0; count < num_resources; resources++, count++) {
    	xrmres->xrm_name	 = StringToName(resources->resource_name);
    	xrmres->xrm_class	 = StringToClass(resources->resource_class);
    	xrmres->xrm_type	 = StringToQuark(resources->resource_type);
        xrmres->xrm_offset	 = (Cardinal)
		(-(int)resources->resource_offset - 1);
    	xrmres->xrm_default_type = StringToQuark(resources->default_type);
    }
#undef xrmres
} /* XrmCompileResourceListEphem */

static void BadSize(
    Cardinal size,
    XrmQuark name)
{
    String params[2];
    Cardinal num_params = 2;

    params[0] = (String)(long) size;
    params[1] = XrmQuarkToString(name);
    XtWarningMsg("invalidSizeOverride", "xtDependencies", XtCXtToolkitError,
	"Representation size %d must match superclass's to override %s",
	params, &num_params);
} /* BadType */

/*
 * Create a new resource list, with the class resources following the
 * superclass's resources.  If a resource in the class list overrides
 * a superclass resource, then just replace the superclass entry in place.
 *
 * At the same time, add a level of indirection to the XtResourceList to
 * create and XrmResourceList.
 */
void _XtDependencies(
    XtResourceList  *class_resp,	/* VAR */
    Cardinal	    *class_num_resp,    /* VAR */
    XrmResourceList *super_res,
    Cardinal	    super_num_res,
    Cardinal	    super_widget_size)
{
    register XrmResourceList *new_res;
	     Cardinal	     new_num_res;
	     XrmResourceList class_res = (XrmResourceList) *class_resp;
	     Cardinal        class_num_res = *class_num_resp;
    register Cardinal	     i, j;
	     Cardinal        new_next;

    if (class_num_res == 0) {
	/* Just point to superclass resource list */
	*class_resp = (XtResourceList) super_res;
	*class_num_resp = super_num_res;
	return;
    }

    /* Allocate and initialize new_res with superclass resource pointers */
    new_num_res = super_num_res + class_num_res;
    new_res = (XrmResourceList *) __XtMalloc(new_num_res*sizeof(XrmResourceList));
    if (super_num_res > 0)
	XtMemmove(new_res, super_res, super_num_res * sizeof(XrmResourceList));

    /* Put pointers to class resource entries into new_res */
    new_next = super_num_res;
    for (i = 0; i < class_num_res; i++) {
	if ((Cardinal)(-class_res[i].xrm_offset-1) < super_widget_size) {
	    /* Probably an override of superclass resources--look for overlap */
	    for (j = 0; j < super_num_res; j++) {
		if (class_res[i].xrm_offset == new_res[j]->xrm_offset) {
		    /* Spec is silent on what fields subclass can override.
		     * The only two of real concern are type & size.
		     * Although allowing type to be over-ridden introduces
		     * the possibility of errors, it's at present the only
		     * reasonable way to allow a subclass to force a private
		     * converter to be invoked for a subset of fields.
		     */
		    /* We do insist that size be identical to superclass */
		    if (class_res[i].xrm_size != new_res[j]->xrm_size) {
			BadSize(class_res[i].xrm_size,
				(XrmQuark) class_res[i].xrm_name);
			class_res[i].xrm_size = new_res[j]->xrm_size;
		    }
		    new_res[j] = &(class_res[i]);
		    new_num_res--;
		    goto NextResource;
		}
	    } /* for j */
	}
	/* Not an overlap, add an entry to new_res */
	new_res[new_next++] = &(class_res[i]);
NextResource:;
    } /* for i */

    /* Okay, stuff new resources back into class record */
    *class_resp = (XtResourceList) new_res;
    *class_num_resp = new_num_res;
} /* _XtDependencies */


void _XtResourceDependencies(
    WidgetClass wc)
{
    WidgetClass sc;

    sc = wc->core_class.superclass;
    if (sc == NULL) {
	_XtDependencies(&(wc->core_class.resources),
			&(wc->core_class.num_resources),
			(XrmResourceList *) NULL, (unsigned)0, (unsigned)0);
    } else {
	_XtDependencies(&(wc->core_class.resources),
			&(wc->core_class.num_resources),
			(XrmResourceList *) sc->core_class.resources,
			sc->core_class.num_resources,
			sc->core_class.widget_size);
    }
} /* _XtResourceDependencies */

void _XtConstraintResDependencies(
    ConstraintWidgetClass wc)
{
    ConstraintWidgetClass sc;

    if (wc == (ConstraintWidgetClass) constraintWidgetClass) {
	_XtDependencies(&(wc->constraint_class.resources),
			&(wc->constraint_class.num_resources),
			(XrmResourceList *)NULL, (unsigned)0, (unsigned)0);
    } else {
	sc = (ConstraintWidgetClass) wc->core_class.superclass;
	_XtDependencies(&(wc->constraint_class.resources),
			&(wc->constraint_class.num_resources),
			(XrmResourceList *) sc->constraint_class.resources,
			sc->constraint_class.num_resources,
			sc->constraint_class.constraint_size);
    }
} /* _XtConstraintResDependencies */




XrmResourceList* _XtCreateIndirectionTable (
    XtResourceList  resources,
    Cardinal	    num_resources)
{
    register Cardinal idx;
    XrmResourceList* table;

    table = (XrmResourceList*)__XtMalloc(num_resources * sizeof(XrmResourceList));
    for (idx = 0; idx < num_resources; idx++)
        table[idx] = (XrmResourceList)(&(resources[idx]));
    return table;
}

static XtCacheRef *GetResources(
    Widget	    widget,	    /* Widget resources are associated with */
    char*	    base,	    /* Base address of memory to write to   */
    XrmNameList     names,	    /* Full inheritance name of widget      */
    XrmClassList    classes,	    /* Full inheritance class of widget     */
    XrmResourceList*  table,	    /* The list of resources required.      */
    unsigned	    num_resources,  /* number of items in resource list     */
    XrmQuarkList    quark_args,     /* Arg names quarkified		    */
    ArgList	    args,	    /* ArgList to override resources	    */
    unsigned	    num_args,       /* number of items in arg list	    */
    XtTypedArgList  typed_args,	    /* Typed arg list to override resources */
    Cardinal*	    pNumTypedArgs,  /* number of items in typed arg list    */
    Boolean	    tm_hack)	    /* do baseTranslations		    */
{
/*
 * assert: *pNumTypedArgs == 0 if num_args > 0
 * assert: num_args == 0 if *pNumTypedArgs > 0
 */
#define SEARCHLISTLEN 100
#define MAXRESOURCES  400

    XrmValue	    value;
    XrmQuark	    rawType;
    XrmValue	    convValue;
    XrmHashTable    stackSearchList[SEARCHLISTLEN];
    XrmHashTable    *searchList = stackSearchList;
    unsigned int    searchListSize = SEARCHLISTLEN;
    Boolean	    found[MAXRESOURCES];
    int		    typed[MAXRESOURCES];
    XtCacheRef	    cache_ref[MAXRESOURCES];
    XtCacheRef      *cache_ptr, *cache_base;
    Boolean	    persistent_resources = True;
    Boolean	    found_persistence = False;
    int		    num_typed_args = *pNumTypedArgs;
    XrmDatabase     db;
    Boolean	    do_tm_hack = False;

    if ((args == NULL) && (num_args != 0)) {
    	XtAppWarningMsg(XtWidgetToApplicationContext(widget),
		"invalidArgCount","getResources",XtCXtToolkitError,
                 "argument count > 0 on NULL argument list",
                   (String *)NULL, (Cardinal *)NULL);
	num_args = 0;
    }
    if (num_resources == 0) {
	return NULL;
    } else if (num_resources >= MAXRESOURCES) {
    	XtAppWarningMsg(XtWidgetToApplicationContext(widget),
		"invalidResourceCount","getResources",XtCXtToolkitError,
              "too many resources",
	      (String *)NULL, (Cardinal *)NULL);
	return NULL;
    } else if (table == NULL) {
    	XtAppWarningMsg(XtWidgetToApplicationContext(widget),
		"invalidResourceCount","getResources",XtCXtToolkitError,
              "resource count > 0 on NULL resource list",
	      (String *)NULL, (Cardinal *)NULL);
	return NULL;
    }

    /* Mark each resource as not found on arg list */
    bzero((char *) found, (int) (num_resources * sizeof(Boolean)));
    bzero((char *) typed, (int) (num_resources * sizeof(int)));

    /* Copy the args into the resources, mark each as found */
    {
	register ArgList	    arg;
	register XtTypedArgList	    typed_arg;
	register XrmName	    argName;
	register Cardinal	    j;
	register int	    i;
	register XrmResourceList rx;
	register XrmResourceList *res;
	for (arg = args, i = 0; (Cardinal)i < num_args; i++, arg++) {
	    argName = quark_args[i];
	    if (argName == QinitialResourcesPersistent) {
		persistent_resources = (Boolean)arg->value;
		found_persistence = True;
		continue;
	    }
	    for (j = 0, res = table; j < num_resources; j++, res++) {
		rx = *res;
		if (argName == rx->xrm_name) {
		    _XtCopyFromArg(
			arg->value,
			base - rx->xrm_offset - 1,
			rx->xrm_size);
		    found[j] = TRUE;
		    break;
		}
	    }
	}
	for (typed_arg = typed_args, i = 0; i < num_typed_args;
	     i++, typed_arg++) {
	    register XrmRepresentation argType;
	    argName = quark_args[i];
	    argType = (typed_arg->type == NULL) ? NULLQUARK
		: XrmStringToRepresentation(typed_arg->type);
	    if (argName == QinitialResourcesPersistent) {
		persistent_resources = (Boolean)typed_arg->value;
		found_persistence = True;
		break;
	    }
	    for (j = 0, res = table; j < num_resources; j++, res++) {
		rx = *res;
		if (argName == rx->xrm_name) {
		    if (argType != NULLQUARK && argType != rx->xrm_type) {
			typed[j] = i + 1;
		    } else {
			_XtCopyFromArg(
				       typed_arg->value,
				       base - rx->xrm_offset - 1,
				       rx->xrm_size);
		    }
		    found[j] = TRUE;
		    break;
		}
	    }
	}
    }

    /* Ask resource manager for a list of database levels that we can
       do a single-level search on each resource */

    db = XtScreenDatabase(XtScreenOfObject(widget));
    while (!XrmQGetSearchList(db, names, classes,
			      searchList, searchListSize)) {
	if (searchList == stackSearchList)
	    searchList = NULL;
	searchList = (XrmHashTable*)XtRealloc((char*)searchList,
					      sizeof(XrmHashTable) *
					      (searchListSize *= 2));
    }

    if (persistent_resources)
	cache_base = NULL;
    else
	cache_base = cache_ref;
    /* geez, this is an ugly mess */
    if (XtIsShell(widget)) {
	register XrmResourceList  *res;
	register Cardinal	  j;
	Screen *oldscreen = widget->core.screen;

	/* look up screen resource first, since real rdb depends on it */
	for (res = table, j = 0; j < num_resources; j++, res++) {
	    if ((*res)->xrm_name != Qscreen)
		continue;
	    if (typed[j]) {
		register XtTypedArg* arg = typed_args + typed[j] - 1;
		XrmQuark from_type;
		XrmValue from_val, to_val;

		from_type = StringToQuark(arg->type);
		from_val.size = arg->size;
		if ((from_type == QString) || ((unsigned) arg->size > sizeof(XtArgVal)))
		    from_val.addr = (XPointer)arg->value;
		else
		    from_val.addr = (XPointer)&arg->value;
		to_val.size = sizeof(Screen*);
		to_val.addr = (XPointer)&widget->core.screen;
		found[j] = _XtConvert(widget, from_type, &from_val,
				      QScreen, &to_val, cache_base);
		if (cache_base && *cache_base)
		    cache_base++;
	    }
	    if (!found[j]) {
		if (XrmQGetSearchResource(searchList, Qscreen, QScreen,
				      &rawType, &value)) {
		    if (rawType != QScreen) {
			convValue.size = sizeof(Screen*);
			convValue.addr = (XPointer)&widget->core.screen;
			(void)_XtConvert(widget, rawType, &value,
					 QScreen, &convValue, cache_base);
			if (cache_base && *cache_base)
			    cache_base++;
		    } else {
			widget->core.screen = *((Screen **)value.addr);
		    }
		}
	    }
	    break;
	}
	/* now get the database to use for the rest of the resources */
	if (widget->core.screen != oldscreen) {
	    db = XtScreenDatabase(widget->core.screen);
	    while (!XrmQGetSearchList(db, names, classes,
				      searchList, searchListSize)) {
		if (searchList == stackSearchList)
		    searchList = NULL;
		searchList = (XrmHashTable*)XtRealloc((char*)searchList,
						      sizeof(XrmHashTable) *
						      (searchListSize *= 2));
	    }
	}
    }

    /* go to the resource manager for those resources not found yet */
    /* if it's not in the resource database use the default value   */

    {
	register XrmResourceList  rx;
	register XrmResourceList  *res;
	register Cardinal	  j;
	register XrmRepresentation xrm_type;
	register XrmRepresentation xrm_default_type;
	char	char_val;
	short	short_val;
	int	int_val;
	long	long_val;
	char*	char_ptr;

	if (!found_persistence) {
	    if (XrmQGetSearchResource(searchList, QinitialResourcesPersistent,
			QInitialResourcesPersistent, &rawType, &value)) {
		if (rawType != QBoolean) {
		    convValue.size = sizeof(Boolean);
		    convValue.addr = (XPointer)&persistent_resources;
		    (void)_XtConvert(widget, rawType, &value, QBoolean,
				     &convValue, NULL);
		}
		else
		    persistent_resources = *(Boolean*)value.addr;
	    }
	}
	if (persistent_resources)
	    cache_ptr = NULL;
	else if (cache_base)
	    cache_ptr = cache_base;
	else
	    cache_ptr = cache_ref;

	for (res = table, j = 0; j < num_resources; j++, res++) {
	    rx = *res;
	    xrm_type = rx->xrm_type;
	    if (typed[j]) {
		register XtTypedArg* arg = typed_args + typed[j] - 1;

		/*
                 * This resource value has been specified as a typed arg and
		 * has to be converted. Typed arg conversions are done here
		 * to correctly interpose them with normal resource conversions.
                 */
		XrmQuark	    from_type;
		XrmValue            from_val, to_val;
		Boolean		    converted;

		from_type = StringToQuark(arg->type);
    		from_val.size = arg->size;
		if ((from_type == QString) || ((unsigned) arg->size > sizeof(XtArgVal)))
        	    from_val.addr = (XPointer)arg->value;
	        else
            	    from_val.addr = (XPointer)&arg->value;
		to_val.size = rx->xrm_size;
		to_val.addr = base - rx->xrm_offset - 1;
		converted = _XtConvert(widget, from_type, &from_val,
				       xrm_type, &to_val, cache_ptr);
		if (converted) {

		    /* Copy the converted value back into the typed argument.
		     * normally the data should be <= sizeof(XtArgVal) and
		     * is stored directly into the 'value' field .... BUT
		     * if the resource size is greater than sizeof(XtArgVal)
		     * then we dynamically alloc a block of store to hold the
		     * data and zap a copy in there !!! .... freeing it later
		     * the size field in the typed arg is negated to indicate
		     * that the store pointed to by the value field is
		     * dynamic .......
		     * "freeing" happens in the case of _XtCreate after the
		     * CallInitialize ..... other clients of GetResources
		     * using typed args should be aware of the need to free
		     * this store .....
		     */

		    if(rx->xrm_size > sizeof(XtArgVal)) {
			arg->value = (XtArgVal) __XtMalloc(rx->xrm_size);
			arg->size = -(arg->size);
		    } else { /* will fit - copy directly into value field */
			arg->value = (XtArgVal) NULL;
		    }
		    CopyToArg((char *)(base - rx->xrm_offset - 1),
				 &arg->value, rx->xrm_size);

		} else {
		   /* Conversion failed. Get default value. */
		   found[j] = False;
		}

		if (cache_ptr && *cache_ptr)
		    cache_ptr++;
	    }

	    if (!found[j]) {
		Boolean	already_copied = False;
		Boolean have_value = False;

		if (XrmQGetSearchResource(searchList,
			rx->xrm_name, rx->xrm_class, &rawType, &value)) {
		    if (rawType != xrm_type) {
			convValue.size = rx->xrm_size;
			convValue.addr = (XPointer)(base - rx->xrm_offset - 1);
			already_copied = have_value =
			    _XtConvert(widget, rawType, &value,
				       xrm_type, &convValue, cache_ptr);
			if (cache_ptr && *cache_ptr)
			    cache_ptr++;
		    } else have_value = True;
		    if (have_value && rx->xrm_name == Qtranslations)
			do_tm_hack = True;
		}
		LOCK_PROCESS;
		if (!have_value
		    && ((rx->xrm_default_type == QImmediate)
			|| (rx->xrm_default_type == xrm_type)
			|| (rx->xrm_default_addr != NULL))) {
		    /* Convert default value to proper type */
		    xrm_default_type = rx->xrm_default_type;
		    if (xrm_default_type == QCallProc) {
			(*(XtResourceDefaultProc)(rx->xrm_default_addr))(
			      widget,-(rx->xrm_offset+1), &value);

		    } else if (xrm_default_type == QImmediate) {
			/* XtRImmediate == XtRString for type XtRString */
			if (xrm_type == QString) {
			    value.addr = rx->xrm_default_addr;
			} else if (rx->xrm_size == sizeof(int)) {
			    int_val = (int)(long)rx->xrm_default_addr;
			    value.addr = (XPointer) &int_val;
			} else if (rx->xrm_size == sizeof(short)) {
			    short_val = (short)(long)rx->xrm_default_addr;
			    value.addr = (XPointer) &short_val;
			} else if (rx->xrm_size == sizeof(char)) {
			    char_val = (char)(long)rx->xrm_default_addr;
			    value.addr = (XPointer) &char_val;
			} else if (rx->xrm_size == sizeof(long)) {
			    long_val = (long)rx->xrm_default_addr;
			    value.addr = (XPointer) &long_val;
			} else if (rx->xrm_size == sizeof(char*)) {
			    char_ptr = (char*)rx->xrm_default_addr;
			    value.addr = (XPointer) &char_ptr;
			} else {
			    value.addr = (XPointer) &(rx->xrm_default_addr);
			}
		    } else if (xrm_default_type == xrm_type) {
			value.addr = rx->xrm_default_addr;
		    } else {
			value.addr = rx->xrm_default_addr;
			if (xrm_default_type == QString) {
			    value.size = strlen((char *)value.addr) + 1;
			} else {
			    value.size = sizeof(XtPointer);
			}
			convValue.size = rx->xrm_size;
			convValue.addr = (XPointer)(base - rx->xrm_offset - 1);
			already_copied =
			    _XtConvert(widget, xrm_default_type, &value,
				       xrm_type, &convValue, cache_ptr);
			if (!already_copied)
			    value.addr = NULL;
			if (cache_ptr && *cache_ptr)
			    cache_ptr++;
		    }
		}
		if (!already_copied) {
		    if (xrm_type == QString) {
			*((String*)(base - rx->xrm_offset - 1)) = value.addr;
		    } else {
			if (value.addr != NULL) {
			    XtMemmove(base - rx->xrm_offset - 1,
				      value.addr, rx->xrm_size);
			} else {
			    /* didn't get value, initialize to NULL... */
			    XtBZero(base - rx->xrm_offset - 1, rx->xrm_size);
			}
		    }
		}
		UNLOCK_PROCESS;
	    } 
	}
	for (res = table, j = 0; j < num_resources; j++, res++) {
	    if (!found[j] && typed[j]) {
		/*
		 * This resource value was specified as a typed arg.
		 * However, the default value is being used here since
		 * type type conversion failed, so we compress the list.
		 */
		register XtTypedArg* arg = typed_args + typed[j] - 1;
		register int i;

		for (i = num_typed_args - typed[j]; i > 0; i--, arg++) {
		    *arg = *(arg+1);
		}
		num_typed_args--;
	    }
	}
	if (tm_hack)
	    widget->core.tm.current_state = NULL;
	if (tm_hack &&
	    (!widget->core.tm.translations ||
	     (do_tm_hack &&
	      widget->core.tm.translations->operation != XtTableReplace)) &&
	    XrmQGetSearchResource(searchList, QbaseTranslations,
				  QTranslations, &rawType, &value)) {
	    if (rawType != QTranslationTable) {
		convValue.size = sizeof(XtTranslations);
		convValue.addr = (XPointer)&widget->core.tm.current_state;
		(void)_XtConvert(widget, rawType, &value,
				 QTranslationTable, &convValue, cache_ptr);
		if (cache_ptr && *cache_ptr)
		    cache_ptr++;
	    } else {
		/* value.addr can be NULL see: !already_copied */
		if (value.addr)
		    *((XtTranslations *)&widget->core.tm.current_state) =
			*((XtTranslations *)value.addr);
	    }
	}
    }
    if ((Cardinal)num_typed_args != *pNumTypedArgs) *pNumTypedArgs = num_typed_args;
    if (searchList != stackSearchList) XtFree((char*)searchList);
    if (!cache_ptr)
	cache_ptr = cache_base;
    if (cache_ptr && cache_ptr != cache_ref) {
	int cache_ref_size = cache_ptr - cache_ref;
	XtCacheRef *refs = (XtCacheRef*)
	    __XtMalloc((unsigned)sizeof(XtCacheRef)*(cache_ref_size + 1));
	(void) memmove(refs, cache_ref, sizeof(XtCacheRef)*cache_ref_size );
	refs[cache_ref_size] = NULL;
	return refs;
    }
    return (XtCacheRef*)NULL;
}



static void CacheArgs(
    ArgList	    args,
    Cardinal	    num_args,
    XtTypedArgList  typed_args,
    Cardinal	    num_typed_args,
    XrmQuarkList    quark_cache,
    Cardinal	    num_quarks,
    XrmQuarkList    *pQuarks)       /* RETURN */
{
    register XrmQuarkList   quarks;
    register Cardinal       i;
    register Cardinal       count;

    count = (args != NULL) ? num_args : num_typed_args;

    if (num_quarks < count) {
	quarks = (XrmQuarkList) __XtMalloc(count * sizeof(XrmQuark));
    } else {
	quarks = quark_cache;
    }
    *pQuarks = quarks;

    if (args != NULL) {
	for (i = count; i; i--)
	    *quarks++ = StringToQuark((args++)->name);
    }
    else {
	for (i = count; i; i--)
	    *quarks++ = StringToQuark((typed_args++)->name);
    }
}

#define FreeCache(cache, pointer) \
	  if (cache != pointer) XtFree((char *)pointer)


XtCacheRef *_XtGetResources(
    register 	Widget	  	w,
    		ArgList	  	args,
    		Cardinal  	num_args,
		XtTypedArgList	typed_args,
		Cardinal*	num_typed_args)
{
    XrmName	    *names, names_s[50];
    XrmClass	    *classes, classes_s[50];
    XrmQuark	    quark_cache[100];
    XrmQuarkList    quark_args;
    WidgetClass     wc;
    ConstraintWidgetClass   cwc;
    XtCacheRef	    *cache_refs, *cache_refs_core;
    Cardinal	    count;

    wc = XtClass(w);

    count = CountTreeDepth(w);
    names = (XrmName*) XtStackAlloc (count * sizeof(XrmName), names_s);
    classes = (XrmClass*) XtStackAlloc (count * sizeof(XrmClass), classes_s);
    if (names == NULL || classes == NULL) _XtAllocError(NULL);

    /* Get names, classes for widget and ancestors */
    GetNamesAndClasses(w, names, classes);

    /* Compile arg list into quarks */
    CacheArgs(args, num_args, typed_args, *num_typed_args, quark_cache,
	      XtNumber(quark_cache), &quark_args);

    /* Get normal resources */
    LOCK_PROCESS;
    cache_refs = GetResources(w, (char*)w, names, classes,
	(XrmResourceList *) wc->core_class.resources,
	wc->core_class.num_resources, quark_args, args, num_args,
	typed_args, num_typed_args, XtIsWidget(w));

    if (w->core.constraints != NULL) {
	cwc = (ConstraintWidgetClass) XtClass(w->core.parent);
	cache_refs_core =
	    GetResources(w, (char*)w->core.constraints, names, classes,
	    (XrmResourceList *) cwc->constraint_class.resources,
	    cwc->constraint_class.num_resources,
	    quark_args, args, num_args, typed_args, num_typed_args, False);
	if (cache_refs_core) {
	    XtFree((char *)cache_refs_core);
	}
    }
    FreeCache(quark_cache, quark_args);
    UNLOCK_PROCESS;
    XtStackFree((XtPointer)names, names_s);
    XtStackFree((XtPointer)classes, classes_s);
    return cache_refs;
} /* _XtGetResources */


void _XtGetSubresources (
    Widget	  w,			/* Widget "parent" of subobject   */
    XtPointer	  base,			/* Base address to write to       */
    const char*   name,			/* name of subobject		    */
    const char*   class,		/* class of subobject		    */
    XtResourceList resources,		/* resource list for subobject    */
    Cardinal	  num_resources,
    ArgList	  args,			/* arg list to override resources */
    Cardinal	  num_args,
    XtTypedArgList typed_args,
    Cardinal      num_typed_args)
{
    XrmName	  *names, names_s[50];
    XrmClass	  *classes, classes_s[50];
    XrmQuark	  quark_cache[100];
    XrmQuarkList  quark_args;
    XrmResourceList* table;
    Cardinal	  count, ntyped_args = num_typed_args;
    XtCacheRef    *Resrc = NULL;
    WIDGET_TO_APPCON(w);

    if (num_resources == 0) return;

    LOCK_APP(app);
    count = CountTreeDepth(w);
    count++;	/* make sure there's enough room for name and class */
    names = (XrmName*) XtStackAlloc(count * sizeof(XrmName), names_s);
    classes = (XrmClass*) XtStackAlloc(count * sizeof(XrmClass), classes_s);
    if (names == NULL || classes == NULL) _XtAllocError(NULL);

    /* Get full name, class of subobject */
    GetNamesAndClasses(w, names, classes);
    count -= 2;
    names[count] = StringToName(name);
    classes[count] = StringToClass(class);
    count++;
    names[count] = NULLQUARK;
    classes[count] = NULLQUARK;

    /* Compile arg list into quarks */
    CacheArgs(args, num_args, typed_args, num_typed_args,
	      quark_cache, XtNumber(quark_cache), &quark_args);

    /* Compile resource list if needed */
    if (((int) resources->resource_offset) >= 0) {
	XrmCompileResourceListEphem(resources, num_resources);
    }
    table = _XtCreateIndirectionTable(resources, num_resources);
    Resrc = GetResources(w, (char*)base, names, classes, table, num_resources,
			quark_args, args, num_args,
			typed_args, &ntyped_args, False);
    FreeCache(quark_cache, quark_args);
    XtFree((char *)table);
    XtFree((char *)Resrc);
    XtStackFree((XtPointer)names, names_s);
    XtStackFree((XtPointer)classes, classes_s);
    UNLOCK_APP(app);
}

void XtGetSubresources (
    Widget	  w,			/* Widget "parent" of subobject   */
    XtPointer	  base,			/* Base address to write to       */
    _Xconst char* name,			/* name of subobject		    */
    _Xconst char* class,		/* class of subobject		    */
    XtResourceList resources,		/* resource list for subobject    */
    Cardinal	  num_resources,
    ArgList	  args,			/* arg list to override resources */
    Cardinal	  num_args)
{
    _XtGetSubresources (w, base, name, class, resources, num_resources, args, num_args, NULL, 0);
}


void _XtGetApplicationResources (
    Widget	    w,		  /* Application shell widget       */
    XtPointer	    base,	  /* Base address to write to       */
    XtResourceList  resources,	  /* resource list for subobject    */
    Cardinal	    num_resources,
    ArgList	    args,	  /* arg list to override resources */
    Cardinal	    num_args,
    XtTypedArgList  typed_args,
    Cardinal	    num_typed_args)
{
    XrmName	    *names, names_s[50];
    XrmClass	    *classes, classes_s[50];
    XrmQuark	    quark_cache[100];
    XrmQuarkList    quark_args;
    XrmResourceList* table;
    Cardinal        count, ntyped_args = num_typed_args;
#ifdef XTHREADS
    XtAppContext    app;
#endif
    XtCacheRef	    *Resrc = NULL;

    if (num_resources == 0) return;

#ifdef XTHREADS
    if (w == NULL) app = _XtDefaultAppContext();
    else app = XtWidgetToApplicationContext(w);
#endif

    LOCK_APP(app);
    /* Get full name, class of application */
    if (w == NULL) {
	/* hack for R2 compatibility */
	XtPerDisplay pd = _XtGetPerDisplay(_XtDefaultAppContext()->list[0]);
	names = (XrmName*) XtStackAlloc (2 * sizeof(XrmName), names_s);
	classes = (XrmClass*) XtStackAlloc (2 * sizeof(XrmClass), classes_s);
	names[0] = pd->name;
	names[1] = NULLQUARK;
	classes[0] = pd->class;
	classes[1] = NULLQUARK;
    }
    else {
	count = CountTreeDepth(w);
        names = (XrmName*) XtStackAlloc(count * sizeof(XrmName), names_s);
        classes = (XrmClass*) XtStackAlloc(count * sizeof(XrmClass), classes_s);
	if (names == NULL || classes == NULL) _XtAllocError(NULL);
	GetNamesAndClasses(w, names, classes);
    }

    /* Compile arg list into quarks */
    CacheArgs(args, num_args, typed_args, num_typed_args,  quark_cache,
	XtNumber(quark_cache), &quark_args);
    /* Compile resource list if needed */
    if (((int) resources->resource_offset) >= 0) {
	XrmCompileResourceListEphem(resources, num_resources);
    }
    table = _XtCreateIndirectionTable(resources,num_resources);

    Resrc = GetResources(w, (char*)base, names, classes, table, num_resources,
			quark_args, args, num_args,
			typed_args, &ntyped_args, False);
    FreeCache(quark_cache, quark_args);
    XtFree((char *)table);
    XtFree((char *)Resrc);
    if (w != NULL) {
	XtStackFree((XtPointer)names, names_s);
	XtStackFree((XtPointer)classes, classes_s);
    }
    UNLOCK_APP(app);
}

void XtGetApplicationResources (
    Widget	    w,		  /* Application shell widget       */
    XtPointer	    base,	  /* Base address to write to       */
    XtResourceList  resources,	  /* resource list for subobject    */
    Cardinal	    num_resources,
    ArgList	    args,	  /* arg list to override resources */
    Cardinal	    num_args)
{
    _XtGetApplicationResources(w, base, resources, num_resources, args, num_args, NULL, 0);
}

static Boolean initialized = FALSE;

void _XtResourceListInitialize(void)
{
    LOCK_PROCESS;
    if (initialized) {
	XtWarningMsg("initializationError","xtInitialize",XtCXtToolkitError,
                  "Initializing Resource Lists twice",
		  (String *)NULL, (Cardinal *)NULL);
	UNLOCK_PROCESS;
    	return;
    }
    initialized = TRUE;
    UNLOCK_PROCESS;

    QBoolean = XrmPermStringToQuark(XtCBoolean);
    QString = XrmPermStringToQuark(XtCString);
    QCallProc = XrmPermStringToQuark(XtRCallProc);
    QImmediate = XrmPermStringToQuark(XtRImmediate);
    QinitialResourcesPersistent = XrmPermStringToQuark(XtNinitialResourcesPersistent);
    QInitialResourcesPersistent = XrmPermStringToQuark(XtCInitialResourcesPersistent);
    Qtranslations = XrmPermStringToQuark(XtNtranslations);
    QbaseTranslations = XrmPermStringToQuark("baseTranslations");
    QTranslations = XrmPermStringToQuark(XtCTranslations);
    QTranslationTable = XrmPermStringToQuark(XtRTranslationTable);
    Qscreen = XrmPermStringToQuark(XtNscreen);
    QScreen = XrmPermStringToQuark(XtCScreen);
}
