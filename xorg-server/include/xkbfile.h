/************************************************************
 Copyright (c) 1994 by Silicon Graphics Computer Systems, Inc.

 Permission to use, copy, modify, and distribute this
 software and its documentation for any purpose and without
 fee is hereby granted, provided that the above copyright
 notice appear in all copies and that both that copyright
 notice and this permission notice appear in supporting
 documentation, and that the name of Silicon Graphics not be
 used in advertising or publicity pertaining to distribution
 of the software without specific prior written permission.
 Silicon Graphics makes no representation about the suitability
 of this software for any purpose. It is provided "as is"
 without any express or implied warranty.

 SILICON GRAPHICS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
 SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL SILICON
 GRAPHICS BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL
 DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION  WITH
 THE USE OR PERFORMANCE OF THIS SOFTWARE.

 ********************************************************/

#ifndef _XKBFILE_H_
#define	_XKBFILE_H_ 1

#include "xkbstr.h"

typedef void (*XkbFileAddOnFunc) (FILE * /* file */ ,
                                  XkbDescPtr /* result */ ,
                                  Bool /* topLevel */ ,
                                  Bool /* showImplicit */ ,
                                  int /* fileSection */ ,
                                  void *        /* priv */
    );

/***====================================================================***/

#define	_XkbSuccess			0	/* unused */
#define	_XkbErrMissingNames		1
#define	_XkbErrMissingTypes		2
#define	_XkbErrMissingReqTypes		3
#define	_XkbErrMissingSymbols		4
#define	_XkbErrMissingVMods		5	/* unused */
#define	_XkbErrMissingIndicators	6	/* unused */
#define	_XkbErrMissingCompatMap		7
#define	_XkbErrMissingSymInterps	8	/* unused */
#define	_XkbErrMissingGeometry		9
#define	_XkbErrIllegalDoodad		10	/* unused */
#define	_XkbErrIllegalTOCType		11	/* unused */
#define	_XkbErrIllegalContents		12
#define	_XkbErrEmptyFile		13	/* unused */
#define	_XkbErrFileNotFound		14	/* unused */
#define	_XkbErrFileCannotOpen		15	/* unused */
#define	_XkbErrBadValue			16
#define	_XkbErrBadMatch			17
#define	_XkbErrBadTypeName		18
#define	_XkbErrBadTypeWidth		19
#define	_XkbErrBadFileType		20
#define	_XkbErrBadFileVersion		21
#define	_XkbErrBadFileFormat		22	/* unused */
#define	_XkbErrBadAlloc			23
#define	_XkbErrBadLength		24
#define	_XkbErrXReqFailure		25	/* unused */
#define	_XkbErrBadImplementation	26

/***====================================================================***/

_XFUNCPROTOBEGIN

#define	_XkbKSLower	(1<<0)
#define	_XkbKSUpper	(1<<1)

#define	XkbKSIsLower(k)		(_XkbKSCheckCase(k)&_XkbKSLower)
#define	XkbKSIsUpper(k)		(_XkbKSCheckCase(k)&_XkbKSUpper)
#define XkbKSIsKeypad(k)	(((k)>=XK_KP_Space)&&((k)<=XK_KP_Equal))

extern _X_EXPORT unsigned _XkbKSCheckCase(KeySym        /* sym */
    );

extern _X_EXPORT int XkbFindKeycodeByName(XkbDescPtr /* xkb */ ,
                                          char * /* name */ ,
                                          Bool  /* use_aliases */
    );

/***====================================================================***/

extern _X_EXPORT unsigned XkbConvertGetByNameComponents(Bool /* toXkm */ ,
                                                        unsigned        /* orig */
    );

/***====================================================================***/

extern _X_EXPORT Bool XkbWriteXKBKeycodes(FILE * /* file */ ,
                                          XkbDescPtr /* result */ ,
                                          Bool /* topLevel */ ,
                                          Bool /* showImplicit */ ,
                                          XkbFileAddOnFunc /* addOn */ ,
                                          void *        /* priv */
    );

extern _X_EXPORT Bool XkbWriteXKBKeyTypes(FILE * /* file */ ,
                                          XkbDescPtr /* result */ ,
                                          Bool /* topLevel */ ,
                                          Bool /* showImplicit */ ,
                                          XkbFileAddOnFunc /* addOn */ ,
                                          void *        /* priv */
    );

extern _X_EXPORT Bool XkbWriteXKBCompatMap(FILE * /* file */ ,
                                           XkbDescPtr /* result */ ,
                                           Bool /* topLevel */ ,
                                           Bool /* showImplicit */ ,
                                           XkbFileAddOnFunc /* addOn */ ,
                                           void *       /* priv */
    );

extern _X_EXPORT Bool XkbWriteXKBSymbols(FILE * /* file */ ,
                                         XkbDescPtr /* result */ ,
                                         Bool /* topLevel */ ,
                                         Bool /* showImplicit */ ,
                                         XkbFileAddOnFunc /* addOn */ ,
                                         void * /* priv */
    );

extern _X_EXPORT Bool XkbWriteXKBGeometry(FILE * /* file */ ,
                                          XkbDescPtr /* result */ ,
                                          Bool /* topLevel */ ,
                                          Bool /* showImplicit */ ,
                                          XkbFileAddOnFunc /* addOn */ ,
                                          void *        /* priv */
    );

extern _X_EXPORT Bool XkbWriteXKBKeymapForNames(FILE * /* file */ ,
                                                XkbComponentNamesPtr /* names */
                                                ,
                                                XkbDescPtr /* xkb */ ,
                                                unsigned /* want */ ,
                                                unsigned        /* need */
    );

/***====================================================================***/

extern _X_EXPORT unsigned XkmReadFile(FILE * /* file */ ,
                                      unsigned /* need */ ,
                                      unsigned /* want */ ,
                                      XkbDescPtr *      /* result */
    );

_XFUNCPROTOEND
#endif                          /* _XKBFILE_H_ */
