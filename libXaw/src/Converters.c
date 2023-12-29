/*
 * Copyright (c) 1998 by The XFree86 Project, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of the XFree86 Project shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from the
 * XFree86 Project.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xmu/CharSet.h>
#include <X11/Xaw/Simple.h>
#include <X11/Xaw/XawInit.h>
#include "Private.h"

#ifndef OLDXAW

/*
 * Definitions
 */
#define done(type, value)			\
{						\
  if (toVal->addr != NULL)			\
    {						\
      if (toVal->size < sizeof(type))		\
	{					\
	  toVal->size = sizeof(type);		\
	  return (False);			\
	}					\
      *(type *)(toVal->addr) = (value);		\
    }						\
  else						\
    {						\
      static type static_val;			\
						\
      static_val = (value);			\
      toVal->addr = (XPointer)&static_val;	\
    }						\
  toVal->size = sizeof(type);			\
  return (True);				\
}

#define string_done(value)			\
{						\
  if (toVal->addr != NULL)			\
    {						\
      if (toVal->size < size)			\
	{					\
	  toVal->size = size;			\
	  return (False);			\
	}					\
      strcpy((char *)toVal->addr, (value));	\
    }						\
  else						\
    toVal->addr = (XPointer)(value);		\
  toVal->size = size;				\
  return (True);				\
}

/*
 * Prototypes
 */
static Boolean _XawCvtAtomToString(Display*, XrmValue*, Cardinal*,
				   XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtBooleanToString(Display*, XrmValue*, Cardinal*,
				      XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtBoolToString(Display*, XrmValue*, Cardinal*,
				   XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtCARD32ToString(Display*, XrmValue*, Cardinal*,
				     XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtCardinalToString(Display*, XrmValue*, Cardinal*,
				       XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtDimensionToString(Display*, XrmValue*, Cardinal*,
					XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtDisplayListToString(Display*, XrmValue*, Cardinal*,
				    XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtFontStructToString(Display*, XrmValue*, Cardinal*,
					 XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtIntToString(Display*, XrmValue*, Cardinal*,
					  XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtPixelToString(Display*, XrmValue*, Cardinal*,
				    XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtPixmapToString(Display*, XrmValue*, Cardinal*,
				     XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtShortToString(Display*, XrmValue*, Cardinal*,
				    XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtPositionToString(Display*, XrmValue*, Cardinal*,
				       XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtStringToDisplayList(Display*, XrmValue*, Cardinal*,
					  XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtStringToPixmap(Display*, XrmValue*, Cardinal*,
				     XrmValue*, XrmValue*, XtPointer*);
static Boolean _XawCvtUnsignedCharToString(Display*, XrmValue*, Cardinal*,
				     XrmValue*, XrmValue*, XtPointer*);
static void TypeToStringNoArgsWarning(Display*, String);

/*
 * Initialization
 */
static XtConvertArgRec PixelArgs[] = {
  {XtWidgetBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.colormap),
   sizeof(Colormap)},
};

static XtConvertArgRec DLArgs[] = {
  {XtWidgetBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.screen),
   sizeof(Screen *)},
  {XtWidgetBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.colormap),
   sizeof(Colormap)},
  {XtWidgetBaseOffset, (XtPointer)XtOffsetOf(WidgetRec, core.depth),
   sizeof(int)},
};
#endif /* OLDXAW */

static String XtCToolkitError = "ToolkitError";
static String XtNconversionError = "conversionError";

#ifndef OLDXAW
static String XtNwrongParameters = "wrongParameters";

/*
 * Implementation
 */
void
XawInitializeDefaultConverters(void)
{
  static Boolean first_time = True;

  if (first_time == False)
    return;

  first_time = False;

  /* Replace with more appropriate converters */
  XtSetTypeConverter(XtRCallback, XtRString, _XawCvtCARD32ToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRColormap, XtRString, _XawCvtCARD32ToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRFunction, XtRString, _XawCvtCARD32ToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRPointer, XtRString, _XawCvtCARD32ToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRScreen, XtRString, _XawCvtCARD32ToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRStringArray, XtRString, _XawCvtCARD32ToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRVisual, XtRString, _XawCvtCARD32ToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRWidget, XtRString, _XawCvtCARD32ToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRWidgetList, XtRString, _XawCvtCARD32ToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRWindow, XtRString, _XawCvtCARD32ToString,
		     NULL, 0, XtCacheNone, NULL);

  XtSetTypeConverter(XtRAtom, XtRString, _XawCvtAtomToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRBool, XtRString,  _XawCvtBoolToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRBoolean, XtRString,  _XawCvtBooleanToString,
		     NULL, 0,  XtCacheNone, NULL);
  XtSetTypeConverter(XtRCardinal, XtRString, _XawCvtCardinalToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRDimension, XtRString, _XawCvtDimensionToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XawRDisplayList, XtRString, _XawCvtDisplayListToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRFontStruct, XtRString, _XawCvtFontStructToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRInt, XtRString, _XawCvtIntToString,
		     NULL, 0, XtCacheNone, NULL);
  XtSetTypeConverter(XtRPixel, XtRString, _XawCvtPixelToString,
		     &PixelArgs[0], XtNumber(PixelArgs), XtCacheNone, NULL);
  XtSetTypeConverter(XtRPixmap, XtRString, _XawCvtPixmapToString,
		     &DLArgs[0], XtNumber(DLArgs), XtCacheNone, NULL);
  XtSetTypeConverter(XtRPosition, XtRString, _XawCvtPositionToString,
		     NULL, 0, XtCacheNone,  NULL);
  XtSetTypeConverter(XtRShort, XtRString, _XawCvtShortToString,
		     NULL, 0, XtCacheNone,  NULL);
  XtSetTypeConverter(XtRString, XawRDisplayList, _XawCvtStringToDisplayList,
		     &DLArgs[0], XtNumber(DLArgs), XtCacheAll, NULL);
  XtSetTypeConverter(XtRString, XtRPixmap, _XawCvtStringToPixmap,
		     &DLArgs[0], XtNumber(DLArgs), XtCacheAll, NULL);
  XtSetTypeConverter(XtRUnsignedChar, XtRString, _XawCvtUnsignedCharToString,
		     NULL, 0, XtCacheNone, NULL);
}
#endif /* OLDXAW */

void
XawTypeToStringWarning(Display *dpy, String type)
{
  char fname[64];
  String params[1];
  Cardinal num_params;

  snprintf(fname, sizeof(fname), "cvt%sToString", type);

  params[0] = type;
  num_params = 1;
  XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
		  XtNconversionError, fname, XtCToolkitError,
		  "Cannot convert %s to String",
		  params, &num_params);
}

#ifndef OLDXAW
static void
TypeToStringNoArgsWarning(Display *dpy, String type)
{
  char fname[64];
  String params[1];
  Cardinal num_params;

  snprintf(fname, sizeof(fname), "cvt%sToString", type);

  params[0] = type;
  num_params = 1;
  XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
		  XtNconversionError, fname,
		  XtCToolkitError,
		  "%s to String conversion needs no extra arguments",
		  params, &num_params);
}

/*ARGSUSED*/
static Boolean
_XawCvtBooleanToString(Display *dpy, XrmValue *args _X_UNUSED, Cardinal *num_args,
		       XrmValue *fromVal, XrmValue *toVal,
		       XtPointer *converter_data _X_UNUSED)
{
  static char buffer[6];
  Cardinal size;

  if (*num_args != 0)
    TypeToStringNoArgsWarning(dpy, XtRBoolean);

  snprintf(buffer, sizeof(buffer), "%s",
	   *(Boolean *)fromVal->addr ? XtEtrue : XtEfalse);
  size = (Cardinal)(strlen(buffer) + 1);

  string_done(buffer);
}

/*ARGSUSED*/
static Boolean
_XawCvtBoolToString(Display *dpy, XrmValue *args _X_UNUSED, Cardinal *num_args,
		    XrmValue *fromVal, XrmValue *toVal,
		    XtPointer *converter_data _X_UNUSED)
{
  static char buffer[6];
  Cardinal size;

  if (*num_args != 0)
    TypeToStringNoArgsWarning(dpy, XtRBool);

  snprintf(buffer, sizeof(buffer), "%s",
	   *(Bool *)fromVal->addr ? XtEtrue : XtEfalse);
  size = (Cardinal)(strlen(buffer) + 1);

  string_done(buffer);
}

/*ARGSUSED*/
static Boolean
_XawCvtPositionToString(Display *dpy, XrmValue *args _X_UNUSED, Cardinal *num_args,
			XrmValue *fromVal, XrmValue *toVal,
			XtPointer *converter_data _X_UNUSED)
{
  static char buffer[7];
  Cardinal size;

  if (*num_args != 0)
    TypeToStringNoArgsWarning(dpy, XtRPosition);

  snprintf(buffer, sizeof(buffer), "%d", *(Position *)fromVal->addr);
  size = (Cardinal)(strlen(buffer) + 1);

  string_done(buffer);
}

/*ARGSUSED*/
static Boolean
_XawCvtShortToString(Display *dpy, XrmValue *args _X_UNUSED, Cardinal *num_args,
		     XrmValue *fromVal, XrmValue *toVal,
		     XtPointer *converter_data _X_UNUSED)
{
  static char buffer[7];
  Cardinal size;

  if (*num_args != 0)
    TypeToStringNoArgsWarning(dpy, XtRShort);

  snprintf(buffer, sizeof(buffer), "%d", *(short *)fromVal->addr);
  size = (Cardinal)(strlen(buffer) + 1);

  string_done(buffer);
}

/*ARGSUSED*/
static Boolean
_XawCvtDimensionToString(Display *dpy, XrmValue *args _X_UNUSED, Cardinal *num_args,
			 XrmValue *fromVal, XrmValue *toVal,
			 XtPointer *converter_data _X_UNUSED)
{
  static char buffer[6];
  Cardinal size;

  if (*num_args != 0)
    TypeToStringNoArgsWarning(dpy, XtRDimension);

  snprintf(buffer, sizeof(buffer), "%u", *(Dimension *)fromVal->addr);
  size = (Cardinal)(strlen(buffer) + 1);

  string_done(buffer);
}

/*ARGSUSED*/
static Boolean
_XawCvtCARD32ToString(Display *dpy, XrmValue *args _X_UNUSED, Cardinal *num_args,
		      XrmValue *fromVal, XrmValue *toVal,
		      XtPointer *converter_data _X_UNUSED)
{
  static char buffer[11];
  Cardinal size;

  if (*num_args != 0)
    TypeToStringNoArgsWarning(dpy, "CARD32");

  snprintf(buffer, sizeof(buffer), "0x%08hx", *(int *)fromVal->addr);
  size = (Cardinal)(strlen(buffer) + 1);

  string_done(buffer);
}

/*ARGSUSED*/
static Boolean
_XawCvtIntToString(Display *dpy, XrmValue *args _X_UNUSED, Cardinal *num_args,
		   XrmValue *fromVal, XrmValue *toVal,
		   XtPointer *converter_data _X_UNUSED)
{
  static char buffer[12];
  Cardinal size;

  if (*num_args != 0)
    TypeToStringNoArgsWarning(dpy, XtRInt);

  snprintf(buffer, sizeof(buffer), "%d", *(int *)fromVal->addr);
  size = (Cardinal)(strlen(buffer) + 1);

  string_done(buffer);
}

/*ARGSUSED*/
static Boolean
_XawCvtCardinalToString(Display *dpy, XrmValue *args _X_UNUSED, Cardinal *num_args,
			XrmValue *fromVal, XrmValue *toVal,
			XtPointer *converter_data _X_UNUSED)
{
  static char buffer[11];
  Cardinal size;

  if (*num_args != 0)
    TypeToStringNoArgsWarning(dpy, XtRCardinal);

  snprintf(buffer, sizeof(buffer), "%u", *(Cardinal *)fromVal->addr);
  size = (Cardinal)(strlen(buffer) + 1);

  string_done(buffer);
}

/*ARGSUSED*/
static Boolean
_XawCvtAtomToString(Display *dpy, XrmValue *args _X_UNUSED, Cardinal *num_args,
		    XrmValue *fromVal, XrmValue *toVal,
		    XtPointer *converter_data _X_UNUSED)
{
  static char *buffer = NULL;
  static char *nullatom = (char *)"NULL";
  Cardinal size;
  Atom atom;

  if (*num_args != 0)
    TypeToStringNoArgsWarning(dpy, XtRAtom);

  if (buffer && buffer != nullatom)
    XFree(buffer);

  atom = *(Atom *)fromVal[0].addr;
  if (atom == 0)
    buffer = nullatom;
  else if ((buffer = XGetAtomName(dpy, *(Atom *)fromVal[0].addr)) == NULL)
    {
      XawTypeToStringWarning(dpy, XtRAtom);
      toVal->addr = NULL;
      toVal->size = sizeof(String);
      return (False);
    }

  size = (Cardinal)(strlen(buffer) + 1);

  string_done(buffer);
}

/*ARGSUSED*/
static Boolean
_XawCvtPixelToString(Display *dpy, XrmValue *args, Cardinal *num_args,
		     XrmValue *fromVal, XrmValue *toVal,
		     XtPointer *converter_data _X_UNUSED)
{
  static char buffer[19];
  Cardinal size;
  Colormap colormap;
  XColor color;

  if (*num_args != 1)
    {
      XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
		      XtNwrongParameters, "cvtPixelToString",
		      XtCToolkitError,
		      "Pixel to String conversion needs colormap argument",
		      NULL, NULL);
      return (False);
    }

  colormap = *(Colormap *)args[0].addr;
  color.pixel = *(Pixel *)fromVal->addr;

  /* Note:
   * If we know the visual type, we can calculate the xcolor
   * without asking Xlib.
   */
  XQueryColor(dpy, colormap, &color);
  snprintf(buffer, sizeof(buffer), "rgb:%04hx/%04hx/%04hx",
	   color.red, color.green, color.blue);
  size = (Cardinal)(strlen(buffer) + 1);

  string_done(buffer);
}

/*ARGSUSED*/
static Boolean
_XawCvtFontStructToString(Display *dpy, XrmValue *args _X_UNUSED, Cardinal *num_args,
			  XrmValue *fromVal, XrmValue *toVal,
			  XtPointer *converter_data _X_UNUSED)
{
  static char buffer[128];
  Cardinal size;
  Atom atom;
  unsigned long value;

  if (*num_args != 0)
    TypeToStringNoArgsWarning(dpy, XtRFontStruct);

  if ((atom = XInternAtom(dpy, "FONT", True)) == None)
    return (False);

  size = 0;

  if (XGetFontProperty(*(XFontStruct **)fromVal->addr, atom, &value))
    {
      char *tmp = XGetAtomName(dpy, value);

      if (tmp)
	{
	  snprintf(buffer, sizeof(buffer), "%s", tmp);
	  size = (Cardinal)strlen(tmp);
	  XFree(tmp);
	}
    }

  if (size)
    {
      ++size;
    string_done(buffer);
    }

  XawTypeToStringWarning(dpy, XtRFontStruct);

  return (False);
}

/*ARGSUSED*/
static Boolean
_XawCvtUnsignedCharToString(Display *dpy, XrmValue *args _X_UNUSED, Cardinal *num_args,
			    XrmValue *fromVal, XrmValue *toVal,
			    XtPointer *converter_data _X_UNUSED)
{
  static char buffer[4];
  Cardinal size;

  if (*num_args != 0)
    TypeToStringNoArgsWarning(dpy, XtRUnsignedChar);

  snprintf(buffer, sizeof(buffer), "%u",
	   *(unsigned char *)fromVal->addr);
  size = (Cardinal)(strlen(buffer) + 1);

  string_done(buffer);
}

/*ARGSUSED*/
static Boolean
_XawCvtStringToDisplayList(Display *dpy, XrmValue *args, Cardinal *num_args,
			   XrmValue *fromVal, XrmValue *toVal,
			   XtPointer *converter_data _X_UNUSED)
{
  XawDisplayList *dlist;
  Screen *screen;
  Colormap colormap;
  int depth;
  String commands;

  if (*num_args != 3)
    {
      XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
		      XtNwrongParameters, "cvtStringToDisplayList",
		      XtCToolkitError,
		      "String to DisplayList conversion needs screen, "
		      "colormap, and depth arguments",
		      NULL, NULL);
      return (False);
    }

  screen     = *(Screen **)args[0].addr;
  colormap   = *(Colormap *)args[1].addr;
  depth      = *(int *)args[2].addr;

  commands = (String)(fromVal[0].addr);

  dlist = XawCreateDisplayList(commands, screen, colormap, depth);

  if (!dlist)
    {
      XtDisplayStringConversionWarning(dpy, (String)fromVal->addr,
				       XawRDisplayList);
      toVal->addr = NULL;
      toVal->size = sizeof(XawDisplayList*);
      return (False);
    }

  done(XawDisplayList*, dlist);
}

/*ARGSUSED*/
static Boolean
_XawCvtDisplayListToString(Display *dpy, XrmValue *args _X_UNUSED, Cardinal *num_args,
			   XrmValue *fromVal, XrmValue *toVal,
			   XtPointer *converter_data _X_UNUSED)
{
  String buffer;
  Cardinal size;

  if (*num_args != 0)
    TypeToStringNoArgsWarning(dpy, XawRDisplayList);

  buffer = XawDisplayListString(*(XawDisplayList **)(fromVal[0].addr));
  size = (Cardinal)(strlen(buffer) + 1);

  string_done(buffer);
}

/*ARGSUSED*/
static Boolean
_XawCvtStringToPixmap(Display *dpy, XrmValue *args, Cardinal *num_args,
		      XrmValue *fromVal, XrmValue *toVal,
		      XtPointer *converter_data _X_UNUSED)
{
  XawPixmap *xaw_pixmap;
  Pixmap pixmap;
  Screen *screen;
  Colormap colormap;
  int depth;
  String name;

  if (*num_args != 3)
    {
      XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
		      XtNwrongParameters, "cvtStringToPixmap",
		      XtCToolkitError,
		      "String to Pixmap conversion needs screen, "
		      "colormap, and depth arguments",
		      NULL, NULL);
      return (False);
    }

  screen     = *(Screen **)args[0].addr;
  colormap   = *(Colormap *)args[1].addr;
  depth      = *(int *)args[2].addr;

  name = (String)(fromVal[0].addr);

  if (XmuCompareISOLatin1(name, "None") == 0)
    pixmap = None;
  else if (XmuCompareISOLatin1(name, "ParentRelative") == 0)
    pixmap = ParentRelative;
  else if (XmuCompareISOLatin1(name, "XtUnspecifiedPixmap") == 0)
    pixmap = XtUnspecifiedPixmap;
  else
    {
      xaw_pixmap = XawLoadPixmap(name, screen, colormap, depth);
      if (!xaw_pixmap)
	{
	  XtDisplayStringConversionWarning(dpy, (String)fromVal->addr,
					   XtRPixmap);
	  toVal->addr = (XtPointer)XtUnspecifiedPixmap;
	  toVal->size = sizeof(Pixmap);
	  return (False);
	}
      else
	pixmap = xaw_pixmap->pixmap;
    }

  done(Pixmap, pixmap);
}

/*ARGSUSED*/
static Boolean
_XawCvtPixmapToString(Display *dpy, XrmValue *args, Cardinal *num_args,
		      XrmValue *fromVal, XrmValue *toVal,
		      XtPointer *converter_data)
{
  XawPixmap *xaw_pixmap;
  Pixmap pixmap;
  Screen *screen;
  Colormap colormap;
  int depth;
  String buffer = NULL;
  Cardinal size;

  if (*num_args != 3)
    {
      XtAppWarningMsg(XtDisplayToApplicationContext(dpy),
		      XtNwrongParameters, "cvtPixmapToString",
		      XtCToolkitError,
		      "Pixmap to String conversion needs screen, "
		      "colormap, and depth arguments",
		      NULL, NULL);
      return (False);
    }

  screen     = *(Screen **)args[0].addr;
  colormap   = *(Colormap *)args[1].addr;
  depth      = *(int *)args[2].addr;

  pixmap = *(Pixmap *)(fromVal[0].addr);

  switch (pixmap)
    {
    case None:
      buffer = "None";
      break;
    case ParentRelative:
      buffer = "ParentRelative";
      break;
    case XtUnspecifiedPixmap:
      buffer = "XtUnspecifiedPixmap";
      break;
    default:
      xaw_pixmap = XawPixmapFromXPixmap(pixmap, screen, colormap, depth);
      if (xaw_pixmap)
	buffer = xaw_pixmap->name;
      break;
    }

  if (!buffer)
    /* Bad Pixmap or Pixmap was not loaded by XawLoadPixmap() */
    return (_XawCvtCARD32ToString(dpy, args, num_args, fromVal, toVal,
				  converter_data));

  size = (Cardinal)(strlen(buffer) + 1);

  string_done(buffer);
}

#endif /* OLDXAW */
