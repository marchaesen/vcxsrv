#ifndef UTILS_H
#define	UTILS_H 1

  /*\
   *
   *                          COPYRIGHT 1990
   *                    DIGITAL EQUIPMENT CORPORATION
   *                       MAYNARD, MASSACHUSETTS
   *                        ALL RIGHTS RESERVED.
   *
   * THE INFORMATION IN THIS SOFTWARE IS SUBJECT TO CHANGE WITHOUT NOTICE AND
   * SHOULD NOT BE CONSTRUED AS A COMMITMENT BY DIGITAL EQUIPMENT CORPORATION.
   * DIGITAL MAKES NO REPRESENTATIONS ABOUT THE SUITABILITY OF THIS SOFTWARE 
   * FOR ANY PURPOSE.  IT IS SUPPLIED "AS IS" WITHOUT EXPRESS OR IMPLIED 
   * WARRANTY.
   *
   * IF THE SOFTWARE IS MODIFIED IN A MANNER CREATING DERIVATIVE COPYRIGHT
   * RIGHTS, APPROPRIATE LEGENDS MAY BE PLACED ON THE DERIVATIVE WORK IN
   * ADDITION TO THAT SET FORTH ABOVE.
   *
   * Permission to use, copy, modify, and distribute this software and its
   * documentation for any purpose and without fee is hereby granted, provided
   * that the above copyright notice appear in all copies and that both that
   * copyright notice and this permission notice appear in supporting
   * documentation, and that the name of Digital Equipment Corporation not be
   * used in advertising or publicity pertaining to distribution of the 
   * software without specific, written prior permission.
   \*/

/***====================================================================***/

#include 	<stdio.h>
#include	<X11/Xos.h>
#include	<X11/Xfuncproto.h>
#include	<X11/Xfuncs.h>

#include <stddef.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef NUL
#define	NUL	'\0'
#endif

/***====================================================================***/

#ifndef OPAQUE_DEFINED
typedef void *Opaque;
#endif
#ifndef NullOpaque
#define	NullOpaque	((Opaque)NULL)
#endif

#ifndef BOOLEAN_DEFINED
typedef char Boolean;
#endif

#ifndef True
#define	True	((Boolean)1)
#define	False	((Boolean)0)
#endif /* ndef True */
#define	booleanText(b)	((b)?"True":"False")

#ifndef COMPARISON_DEFINED
typedef int Comparison;

#define	Greater		((Comparison)1)
#define	Equal		((Comparison)0)
#define	Less		((Comparison)-1)
#define	CannotCompare	((Comparison)-37)
#define	comparisonText(c)	((c)?((c)<0?"Less":"Greater"):"Equal")
#endif

/***====================================================================***/

extern Opaque uAlloc(unsigned   /* size */
    );
extern Opaque uCalloc(unsigned /* n */ ,
                      unsigned  /* size */
    );
extern Opaque uRealloc(Opaque /* old */ ,
                       unsigned /* newSize */
    );
extern Opaque uRecalloc(Opaque /* old */ ,
                        unsigned /* nOld */ ,
                        unsigned /* nNew */ ,
                        unsigned        /* newSize */
    );
extern void uFree(Opaque        /* ptr */
    );

#define	uTypedAlloc(t)		((t *)uAlloc((unsigned)sizeof(t)))
#define	uTypedCalloc(n,t)	((t *)uCalloc((unsigned)n,(unsigned)sizeof(t)))
#define	uTypedRealloc(pO,n,t)	((t *)uRealloc((Opaque)pO,((unsigned)n)*sizeof(t)))
#define	uTypedRecalloc(pO,o,n,t) ((t *)uRecalloc((Opaque)pO,((unsigned)o),((unsigned)n),sizeof(t)))
#if (defined mdHasAlloca) && (mdHasAlloca)
#define	uTmpAlloc(n)	((Opaque)alloca((unsigned)n))
#define	uTmpFree(p)
#else
#define	uTmpAlloc(n)	uAlloc(n)
#define	uTmpFree(p)	uFree(p)
#endif

/***====================================================================***/

extern Boolean uSetErrorFile(char *     /* name */
    );

#define INFO6 			uInformation
#define INFO5 			uInformation
#define INFO4 			uInformation
#define INFO3 			uInformation
#define INFO2 			uInformation
#define INFO1 			uInformation
#define INFO 			uInformation

extern void
uInformation(const char * /* s */ , ...
    ) _X_ATTRIBUTE_PRINTF(1, 2);

#define ACTION6			uAction
#define ACTION5			uAction
#define ACTION4			uAction
#define ACTION3			uAction
#define ACTION2			uAction
#define ACTION1			uAction
#define ACTION			uAction

     extern void uAction(const char * /* s  */ , ...
    ) _X_ATTRIBUTE_PRINTF(1, 2);

#define WARN6			uWarning
#define WARN5			uWarning
#define WARN4			uWarning
#define WARN3			uWarning
#define WARN2			uWarning
#define WARN1			uWarning
#define WARN			uWarning

     extern void uWarning(const char * /* s  */ , ...
    ) _X_ATTRIBUTE_PRINTF(1, 2);

#define ERROR6			uError
#define ERROR5			uError
#define ERROR4			uError
#define ERROR3			uError
#define ERROR2			uError
#define ERROR1			uError
#define ERROR			uError

     extern void uError(const char * /* s  */ , ...
    ) _X_ATTRIBUTE_PRINTF(1, 2);

#define FATAL6			uFatalError
#define FATAL5			uFatalError
#define FATAL4			uFatalError
#define FATAL3			uFatalError
#define FATAL2			uFatalError
#define FATAL1			uFatalError
#define FATAL			uFatalError

     extern void uFatalError(const char * /* s  */ , ...
    ) _X_ATTRIBUTE_PRINTF(1, 2) _X_NORETURN;

/* WSGO stands for "Weird Stuff Going On" */
#define WSGO6			uInternalError
#define WSGO5			uInternalError
#define WSGO4			uInternalError
#define WSGO3			uInternalError
#define WSGO2			uInternalError
#define WSGO1			uInternalError
#define WSGO			uInternalError

     extern void uInternalError(const char * /* s  */ , ...
    ) _X_ATTRIBUTE_PRINTF(1, 2);

     extern void uSetPreErrorMessage(char *     /* msg */
    );

     extern void uSetPostErrorMessage(char *    /* msg */
    );

     extern void uSetErrorPrefix(char * /* void */
    );

     extern void uFinishUp(void);


/***====================================================================***/

#define	NullString	((char *)NULL)

#define	uStringText(s)		((s)==NullString?"<NullString>":(s))
#define	uStringEqual(s1,s2)	(uStringCompare(s1,s2)==Equal)
#define	uStringPrefix(p,s)	(strncmp(p,s,strlen(p))==0)
#define	uStringCompare(s1,s2)	(((s1)==NullString||(s2)==NullString)?\
                                 (s1)!=(s2):strcmp(s1,s2))
#define	uStrCaseEqual(s1,s2)	(uStrCaseCmp(s1,s2)==0)
#ifdef HAVE_STRCASECMP
#include <strings.h>
#define	uStrCaseCmp(s1,s2)	(strcasecmp(s1,s2))
#define	uStrCasePrefix(p,s)	(strncasecmp(p,s,strlen(p))==0)
#else
     extern int uStrCaseCmp(const char * /* s1 */ ,
                            const char *        /* s2 */
    );
     extern int uStrCasePrefix(const char * /* p */ ,
                               char *   /* str */
    );
#endif
#ifdef HAVE_STRDUP
#include <string.h>
#define	uStringDup(s1)		((s1) ? strdup(s1) : NULL)
#else
     extern char *uStringDup(const char *       /* s1 */
    );
#endif

/***====================================================================***/

#ifndef DEBUG_VAR
#define	DEBUG_VAR	debugFlags
#endif

extern
     unsigned int DEBUG_VAR;

     extern void uDebug(char * /* s  */ , ...
    ) _X_ATTRIBUTE_PRINTF(1, 2);

     extern void uDebugNOI(     /* no indent */
                              char * /* s  */ , ...
    ) _X_ATTRIBUTE_PRINTF(1, 2);

     extern Boolean uSetDebugFile(char *name);

     extern FILE *uDebugFile;
     extern int uDebugIndentLevel;
     extern int uDebugIndentSize;
#define	uDebugIndent(l)		(uDebugIndentLevel+=(l))
#define	uDebugOutdent(l)	(uDebugIndentLevel-=(l))
#ifdef DEBUG
#define	uDEBUG(f,s)		{ if (DEBUG_VAR&(f)) uDebug(s);}
#define	uDEBUG1(f,s,a)		{ if (DEBUG_VAR&(f)) uDebug(s,a);}
#define	uDEBUG2(f,s,a,b)	{ if (DEBUG_VAR&(f)) uDebug(s,a,b);}
#define	uDEBUG3(f,s,a,b,c)	{ if (DEBUG_VAR&(f)) uDebug(s,a,b,c);}
#define	uDEBUG4(f,s,a,b,c,d)	{ if (DEBUG_VAR&(f)) uDebug(s,a,b,c,d);}
#define	uDEBUG5(f,s,a,b,c,d,e)	{ if (DEBUG_VAR&(f)) uDebug(s,a,b,c,d,e);}
#define	uDEBUG_NOI(f,s)		{ if (DEBUG_VAR&(f)) uDebug(s);}
#define	uDEBUG_NOI1(f,s,a)	{ if (DEBUG_VAR&(f)) uDebugNOI(s,a);}
#define	uDEBUG_NOI2(f,s,a,b)	{ if (DEBUG_VAR&(f)) uDebugNOI(s,a,b);}
#define	uDEBUG_NOI3(f,s,a,b,c)	{ if (DEBUG_VAR&(f)) uDebugNOI(s,a,b,c);}
#define	uDEBUG_NOI4(f,s,a,b,c,d) { if (DEBUG_VAR&(f)) uDebugNOI(s,a,b,c,d);}
#define	uDEBUG_NOI5(f,s,a,b,c,d,e) { if (DEBUG_VAR&(f)) uDebugNOI(s,a,b,c,d,e);}
#else
#define	uDEBUG(f,s)
#define	uDEBUG1(f,s,a)
#define	uDEBUG2(f,s,a,b)
#define	uDEBUG3(f,s,a,b,c)
#define	uDEBUG4(f,s,a,b,c,d)
#define	uDEBUG5(f,s,a,b,c,d,e)
#define	uDEBUG_NOI(f,s)
#define	uDEBUG_NOI1(f,s,a)
#define	uDEBUG_NOI2(f,s,a,b)
#define	uDEBUG_NOI3(f,s,a,b,c)
#define	uDEBUG_NOI4(f,s,a,b,c,d)
#define	uDEBUG_NOI5(f,s,a,b,c,d,e)
#endif

#endif /* UTILS_H */
