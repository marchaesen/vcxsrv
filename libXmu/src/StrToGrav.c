/*

Copyright 1989, 1998  The Open Group

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
#include <X11/Intrinsic.h>
#include <X11/Xmu/Converters.h>
#include <X11/Xmu/CharSet.h>

#define done(address, type) \
{ (*toVal).size = sizeof(type); (*toVal).addr = (XPointer) address; }

/*
 * Initialization
 */
static struct _namepair {
    XrmQuark quark;
    _Xconst char *name;
    XtGravity gravity;
} names[] = {
    { NULLQUARK, XtEForget, ForgetGravity },
    { NULLQUARK, XtENorthWest, NorthWestGravity },
    { NULLQUARK, XtENorth, NorthGravity },
    { NULLQUARK, XtENorthEast, NorthEastGravity },
    { NULLQUARK, XtEWest, WestGravity },
    { NULLQUARK, XtECenter, CenterGravity },
    { NULLQUARK, XtEEast, EastGravity },
    { NULLQUARK, XtESouthWest, SouthWestGravity },
    { NULLQUARK, XtESouth, SouthGravity },
    { NULLQUARK, XtESouthEast, SouthEastGravity },
    { NULLQUARK, XtEStatic, StaticGravity },
    { NULLQUARK, XtEUnmap, UnmapGravity },
    { NULLQUARK, XtEleft, WestGravity },
    { NULLQUARK, XtEtop, NorthGravity },
    { NULLQUARK, XtEright, EastGravity },
    { NULLQUARK, XtEbottom, SouthGravity },
    { NULLQUARK, NULL, ForgetGravity }
};

/*
 * This function is deprecated as of the addition of
 * XtCvtStringToGravity in R6
 */
void
XmuCvtStringToGravity(XrmValuePtr args, Cardinal *num_args,
		      XrmValuePtr fromVal, XrmValuePtr toVal)
{
  static Boolean haveQuarks = False;
  char name[10];
    XrmQuark q;
    struct _namepair *np;

    if (*num_args != 0)
        XtWarningMsg("wrongParameters","cvtStringToGravity","XtToolkitError",
                  "String to Gravity conversion needs no extra arguments",
		 (String *)NULL, (Cardinal *)NULL);

  if (!haveQuarks)
    {
      for (np = names; np->name; np++)
	np->quark = XrmPermStringToQuark(np->name);
      haveQuarks = True;
    }

  XmuNCopyISOLatin1Lowered(name, (char *)fromVal->addr, sizeof(name));
  q = XrmStringToQuark(name);

  for (np = names; np->name; np++)
    {
      if (np->quark == q)
	{
	  done(&np->gravity, XtGravity);
		return;
	    }
	}

  XtStringConversionWarning((char *)fromVal->addr, XtRGravity);
}

/*ARGSUSED*/
Boolean
XmuCvtGravityToString(Display *dpy, XrmValue *args, Cardinal *num_args,
		      XrmValue *fromVal, XrmValue *toVal, XtPointer *data)
{
  static const char *buffer;
  Cardinal size;
  struct _namepair *np;
  XtGravity gravity;

  gravity = *(XtGravity *)fromVal->addr;
  buffer = NULL;
  for (np = names; np->name; np++)
    if (np->gravity == gravity)
      {
	buffer = np->name;
	break;
      }

  if (!buffer)
    {
      XtAppWarning(XtDisplayToApplicationContext(dpy),
		   "Cannot convert Gravity to String");
      toVal->addr = NULL;
      toVal->size = 0;

      return (False);
    }

  size = strlen(buffer) + 1;
  if (toVal->addr != NULL)
    {
      if (toVal->size <= size)
	{
	  toVal->size = size;
	  return (False);
	}
      strcpy((char *)toVal->addr, buffer);
    }
  else
    toVal->addr = (XPointer)buffer;
  toVal->size = size;

  return (True);
}
