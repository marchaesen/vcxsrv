/*
 * Copyright 1991 by OMRON Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of OMRON not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  OMRON makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * OMRON DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
 * ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
 * OMRON BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
 * ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTUOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 *
 *	Author:	Seiji Kuwari	OMRON Corporation
 *				kuwa@omron.co.jp
 *				kuwa%omron.co.jp@uunet.uu.net
 */


/*

Copyright 1994, 1998  The Open Group

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
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Xos.h>
#include <X11/Xfuncs.h>
#include <X11/ShellP.h>
#include <X11/Xaw/TextP.h>
#include <X11/Xaw/MultiSrc.h>
#include <X11/Xaw/MultiSinkP.h>
#include <X11/Xaw/XawImP.h>
#include <X11/Xaw/VendorEP.h>
#include "XawI18n.h"
#include <ctype.h>

#include <stdarg.h>

#define maxAscentOfFontSet(fontset)     \
        ( - (XExtentsOfFontSet((fontset)))->max_logical_extent.y)

#define maxHeightOfFontSet(fontset) \
        ((XExtentsOfFontSet((fontset)))->max_logical_extent.height)

#define maxDescentOfFontSet(fontset) \
        (maxHeightOfFontSet(fontset) - maxAscentOfFontSet(fontset))

#define Offset(field) (XtOffsetOf(XawIcTablePart, field))

/*****************************************************
 *
 * Forward reference prototypes
 *
 *****************************************************/

/*
 * Prototypes
 */
static void AllCreateIC(XawVendorShellExtPart*);
static void CloseIM(XawVendorShellExtPart*);
static void CompileResourceList(XtResourceList, unsigned int);
static void ConfigureCB(Widget, XtPointer, XEvent*, Boolean*);
static void CreateIC(Widget, XawVendorShellExtPart*);
static XawIcTableList CreateIcTable(Widget, XawVendorShellExtPart*);
static XawIcTableList CurrentSharedIcTable(XawVendorShellExtPart*);
static void Destroy(Widget, XawVendorShellExtPart*);
static void DestroyAllIM(XawVendorShellExtPart*);
static void DestroyIC(Widget, XawVendorShellExtPart*);
static void FreeAllDataOfVendorShell(XawVendorShellExtPart*,
				     VendorShellWidget);
static XawVendorShellExtPart *GetExtPart(VendorShellWidget);
static XawIcTableList GetIcTable(Widget, XawVendorShellExtPart*);
static XawIcTableList GetIcTableShared(Widget, XawVendorShellExtPart*);
static XIMStyle GetInputStyleOfIC(XawVendorShellExtPart*);
static Bool Initialize(VendorShellWidget, XawVendorShellExtPart*);
static Bool IsCreatedIC(Widget, XawVendorShellExtPart*);
static Bool IsRegistered(Widget, XawVendorShellExtPart*);
static Bool IsSharedIC(XawVendorShellExtPart*);
static Bool NoRegistered(XawVendorShellExtPart*);
static void OpenIM(XawVendorShellExtPart*);
static void Reconnect(XawVendorShellExtPart*);
static void Register(Widget, XawVendorShellExtPart*);
static Bool RegisterToVendorShell(Widget, XawVendorShellExtPart*);
static void ResizeVendorShell(VendorShellWidget, XawVendorShellExtPart*);
static Bool ResizeVendorShell_Core(VendorShellWidget, XawVendorShellExtPart*,
				   XawIcTableList);
static VendorShellWidget SearchVendorShell(Widget);
static Widget SetErrCnxt(Widget, XIM);
static XawVendorShellExtPart *SetExtPart(VendorShellWidget,
					 XawVendorShellExtWidget);
static void SetFocus(Widget, XawVendorShellExtPart*);
static void SetFocusValues(Widget, ArgList, Cardinal, Bool);
static void SetICFocus(Widget, XawVendorShellExtPart*);
static void SetICValues(Widget, XawVendorShellExtPart*, Bool);
static void SetICValuesShared(Widget, XawVendorShellExtPart*, XawIcTableList,
			      Bool);
static void SetValues(Widget, XawVendorShellExtPart*, ArgList, Cardinal);
static unsigned int SetVendorShellHeight(XawVendorShellExtPart*,
					 unsigned int);
static void SharedICChangeFocusWindow(Widget, XawVendorShellExtPart*,
				      XawIcTableList);
static void SizeNegotiation(XawIcTableList, unsigned int, unsigned int);
static void Unregister(Widget, XawVendorShellExtPart*);
static void UnregisterFromVendorShell(Widget, XawVendorShellExtPart*);
static void UnsetFocus(Widget);
static void UnsetICFocus(Widget, XawVendorShellExtPart*);
static void VendorShellDestroyed(Widget, XtPointer, XtPointer);

/*
 * From Vendor.c
 */
void XawVendorShellExtResize(Widget);
void XawVendorStructureNotifyHandler(Widget, XtPointer, XEvent*, Boolean*);


/*
 * From Xt/Resources.c
 */
void _XtCopyFromArg(XtArgVal src, char*, unsigned int);

static XtResource resources[] =
{
    {
	XtNfontSet, XtCFontSet, XtRFontSet, sizeof(XFontSet),
	Offset (font_set), XtRString, (XtPointer)XtDefaultFontSet
    },
    {
	XtNforeground, XtCForeground, XtRPixel, sizeof(Pixel),
	Offset (foreground), XtRString, (XtPointer)"XtDefaultForeground"
    },
    {
	XtNbackground, XtCBackground, XtRPixel, sizeof(Pixel),
	Offset (background), XtRString, (XtPointer)"XtDefaultBackground"
    },
    {
	XtNbackgroundPixmap, XtCPixmap, XtRPixmap, sizeof(Pixmap),
	Offset (bg_pixmap), XtRImmediate, (XtPointer) XtUnspecifiedPixmap
    },
    {
	XtNinsertPosition, XtCTextPosition, XtRInt, sizeof (XawTextPosition),
	Offset (cursor_position), XtRImmediate, (XtPointer) 0
    }
};
#undef Offset


static VendorShellWidget SearchVendorShell(Widget w)
{
    while(w && !XtIsShell(w)) w = XtParent(w);
    if (w && XtIsVendorShell(w)) return((VendorShellWidget)w);
    return(NULL);
}

static XContext extContext = (XContext)0;

static XawVendorShellExtPart *
SetExtPart(VendorShellWidget w, XawVendorShellExtWidget vew)
{
    contextDataRec *contextData;

    if (extContext == (XContext)0) extContext = XUniqueContext();

    contextData = XtNew(contextDataRec);
    contextData->parent = (Widget)w;
    contextData->ve = (Widget)vew;
    if (XSaveContext(XtDisplay(w), (Window)w, extContext, (char *)contextData)) {
	return(NULL);
    }
    return(&(vew->vendor_ext));
}

static XawVendorShellExtPart *
GetExtPart(VendorShellWidget w)
{
    contextDataRec *contextData;
    XawVendorShellExtWidget vew;

    if (XFindContext(XtDisplay(w), (Window)w, extContext,
		      (XPointer*)&contextData)) {
	return(NULL);
    }
    vew = (XawVendorShellExtWidget)contextData->ve;
    return(&(vew->vendor_ext));
}

static Bool
IsSharedIC(XawVendorShellExtPart * ve)
{
    return( ve->ic.shared_ic );
}

static XawIcTableList
GetIcTableShared(Widget w, XawVendorShellExtPart *ve)
{
    XawIcTableList	p;

    for (p = ve->ic.ic_table; p; p = p->next) {
	if (p->widget == w) {
	    if (IsSharedIC(ve)) {
		return(ve->ic.shared_ic_table);
	    } else {
		return(p);
	    }
	}
    }
    return(NULL);
}

static XawIcTableList
GetIcTable(Widget w, XawVendorShellExtPart *ve)
{
    XawIcTableList	p;

    for (p = ve->ic.ic_table; p; p = p->next) {
	if (p->widget == w) {
	    return(p);
	}
    }
    return(NULL);
}

static XIMStyle
GetInputStyleOfIC(XawVendorShellExtPart *ve)
{

    if (!ve) return((XIMStyle)0);
    return(ve->ic.input_style);
}

/*ARGSUSED*/
static void
ConfigureCB(Widget w, XtPointer closure _X_UNUSED, XEvent *event, Boolean *unused _X_UNUSED)
{
    XawIcTableList		p;
    XawVendorShellExtPart	*ve;
    VendorShellWidget		vw;
    XVaNestedList		pe_attr;
    XRectangle			pe_area;
    XawTextMargin		*margin;

    if (event->type != ConfigureNotify) return;

    if ((vw = SearchVendorShell(w)) == NULL) return;

    if ((ve = GetExtPart(vw)) != NULL) {
        if (IsSharedIC(ve)) return;
	if ((ve->im.xim == NULL) ||
	    ((p = GetIcTableShared(w, ve)) == NULL) ||
	    (p->xic == NULL) || !(p->input_style & XIMPreeditPosition)) return;
	pe_area.x = 0;
        pe_area.y = 0;
        pe_area.width = w->core.width;
        pe_area.height = w->core.height;
	margin = &(((TextWidget)w)->text.margin);
	pe_area.x = (short)(pe_area.x + margin->left);
	pe_area.y = (short)(pe_area.y + margin->top);
	pe_area.width = (unsigned short)(pe_area.width - (margin->left + margin->right - 1));
	pe_area.height = (unsigned short)(pe_area.height - (margin->top + margin->bottom - 1));

	pe_attr = XVaCreateNestedList(0, XNArea, &pe_area, NULL);
	XSetICValues(p->xic, XNPreeditAttributes, pe_attr, NULL);
	XtFree(pe_attr);
    }
}

static XContext errContext = (XContext)0;

static Widget SetErrCnxt(Widget w, XIM xim)
{
    contextErrDataRec *contextErrData;

    if (errContext == (XContext)0) errContext = XUniqueContext();

    contextErrData = XtNew(contextErrDataRec);
    contextErrData->widget = w;
    contextErrData->xim = xim;
    if (XSaveContext(XtDisplay(w), (Window)xim, errContext,
	(char *)contextErrData)) {
	return(NULL);
    }
    return(contextErrData->widget);
}

#if 0
static Widget
GetErrCnxt(XIM error_im)
{
    contextErrDataRec *contextErrData;

    if (XFindContext(XDisplayOfIM(error_im), (Window)error_im, errContext,
		      (XPointer*)&contextErrData)) {
	return(NULL);
    }
    return(contextErrData->widget);
}
#endif

static void
CloseIM(XawVendorShellExtPart *ve)
{
    if (ve->im.xim)
	XCloseIM(ve->im.xim);
}

static unsigned int
SetVendorShellHeight(XawVendorShellExtPart* ve, unsigned int height)
{
    Arg			args[2];
    Cardinal		i = 0;

   if (ve->im.area_height < height || height == 0) {
       XtSetArg(args[i], XtNheight,
		(ve->parent->core.height + height - ve->im.area_height));
       ve->im.area_height = (Dimension)height;
       XtSetValues(ve->parent, args, 1);
   }
   return(ve->im.area_height);
}

static void
DestroyAllIM(XawVendorShellExtPart *ve)
{
    XawIcTableList	p;
    contextErrDataRec *contextErrData;

    /*
     * Destroy all ICs
     */
    if (IsSharedIC(ve)) {
        if ((p = ve->ic.shared_ic_table) && p->xic) {
            DestroyIC(p->widget, ve);
            p->xic = NULL;
            p->ic_focused = FALSE;
        }
    } else {
	for (p = ve->ic.ic_table; p; p = p->next) {
	    if (p->xic == NULL) continue;
	    DestroyIC(p->widget, ve);
	    p->xic = NULL;
	    p->ic_focused = FALSE;
	}
    }
    if (!ve->im.xim) return;
    /*
     * Close Input Method
     */
    if (!XFindContext(XDisplayOfIM(ve->im.xim), (Window)ve->im.xim, errContext,
		      (XPointer*)&contextErrData)) {
	if (contextErrData) XtFree((char *)contextErrData);
    }
    XDeleteContext(XDisplayOfIM(ve->im.xim), (Window)ve->im.xim, errContext);
    CloseIM(ve);
    ve->im.xim = NULL;

    /*
     * resize vendor shell to core size
     */
    (void) SetVendorShellHeight(ve, 0);
    /*
    XawVendorShellExtResize(vw);
    */
    return;
}

static void
FreeAllDataOfVendorShell(XawVendorShellExtPart *ve, VendorShellWidget vw)
{
    XawIcTableList       p, next;
    contextErrDataRec *contextErrData;

    if (!XFindContext(XtDisplay(vw), (Window)vw, extContext,
		      (XPointer*)&contextErrData)) {
	if (contextErrData) XtFree((char *)contextErrData);
    }
    XDeleteContext(XtDisplay(vw), (Window)vw, extContext);
    if (ve->ic.shared_ic_table)
        XtFree((char *)ve->ic.shared_ic_table);
    if (ve->im.resources) XtFree((char *)ve->im.resources);
    for (p = ve->ic.ic_table; p; p = next) {
        next = p->next;
        XtFree((char *)p);
    }
}

static void
VendorShellDestroyed(Widget w, XtPointer cl_data _X_UNUSED, XtPointer ca_data _X_UNUSED)
{
    XawVendorShellExtPart	*ve;

    if ( ( ve = GetExtPart( (VendorShellWidget) w ) ) == NULL ) return;
    DestroyAllIM( ve );
    FreeAllDataOfVendorShell( ve, (VendorShellWidget) w );
    return;
}

#if 0
static int
IOErrorHandler(XIM error_im)
{
    VendorShellWidget vw;
    XawVendorShellExtPart * ve;

    if ((vw = (VendorShellWidget)GetErrCnxt(error_im)) == NULL
	|| (ve = GetExtPart(vw)) == NULL) return(0);

    DestroyAllIM(ve);
    return(0);
}
#endif

/*
 * Attempt to open an input method
 */

static void
OpenIM(XawVendorShellExtPart *ve)
{
    int		i;
    _Xconst char *s, *ns, *end;
    char	*p, *pbuf, buf[32];
    XIM		xim = NULL;
    XIMStyles	*xim_styles;
    XIMStyle	input_style = 0;
    Boolean	found;

    if (ve->im.open_im == False) return;
    ve->im.xim = NULL;
    if (ve->im.input_method == NULL) {
	if ((p = XSetLocaleModifiers("@im=none")) != NULL && *p)
	    xim = XOpenIM(XtDisplay(ve->parent), NULL, NULL, NULL);
    } else {
	/* no fragment can be longer than the whole string */
	Cardinal len = (Cardinal)strlen (ve->im.input_method) + 5;

	if (len < sizeof buf) pbuf = buf;
	else pbuf = XtMalloc (len);

	if (pbuf == NULL) return;

	for(ns=s=ve->im.input_method; ns && *s;) {
	    /* skip any leading blanks */
	    while (*s && isspace((unsigned char)*s)) s++;
	    if (!*s) break;
	    if ((ns = end = strchr(s, ',')) == NULL)
		end = s + strlen(s);
	    /* If there is a spurious comma end can be the same as s */
	    if (end > s) {
		/* strip any trailing blanks */
		while (isspace((unsigned char)*(end - 1))) end--;

		strcpy (pbuf, "@im=");
		strncat (pbuf, s, (size_t)(end - s));
		pbuf[end - s + 4] = '\0';
	    }

	    if ((p = XSetLocaleModifiers(pbuf)) != NULL && *p
		&& (xim = XOpenIM(XtDisplay(ve->parent), NULL, NULL, NULL)) != NULL)
		break;

	    s = ns + 1;
	}

	if (pbuf != buf) XtFree (pbuf);
    }
    if (xim == NULL) {
	if ((p = XSetLocaleModifiers("")) != NULL) {
	    xim = XOpenIM(XtDisplay(ve->parent), NULL, NULL, NULL);
	}
    }
    if (xim == NULL) {
	XtAppWarning(XtWidgetToApplicationContext(ve->parent),
	    "Input Method Open Failed");
	return;
    }
    if (XGetIMValues(xim, XNQueryInputStyle, &xim_styles, NULL)
	|| !xim_styles) {
	XtAppWarning(XtWidgetToApplicationContext(ve->parent),
	    "input method doesn't support any style");
	XCloseIM(xim);
	return;
    }
    found = False;
    for(ns = s = ve->im.preedit_type; s && !found;) {
	while (*s && isspace((unsigned char)*s)) s++;
	if (!*s) break;
	if ((ns = end = strchr(s, ',')) == NULL)
	    end = s + strlen(s);
	else
	    ns++;
	if (end > s)
	    while (isspace((unsigned char)*(end - 1))) end--;

	if (!strncmp(s, "OverTheSpot", (size_t)(end - s))) {
	    input_style = (XIMPreeditPosition | XIMStatusArea);
	} else if (!strncmp(s, "OffTheSpot", (size_t)(end - s))) {
	    input_style = (XIMPreeditArea | XIMStatusArea);
	} else if (!strncmp(s, "Root", (size_t)(end - s))) {
	    input_style = (XIMPreeditNothing | XIMStatusNothing);
	}
	for (i = 0; (unsigned short)i < xim_styles->count_styles; i++)
	    if (input_style == xim_styles->supported_styles[i]) {
		ve->ic.input_style = input_style;
		SetErrCnxt(ve->parent, xim);
		ve->im.xim = xim;
		found = True;
		break;
	    }

	s = ns;
    }
    XFree(xim_styles);

    if (!found) {
	XCloseIM(xim);
	XtAppWarning(XtWidgetToApplicationContext(ve->parent),
		     "input method doesn't support my input style");
    }
}

static Bool
ResizeVendorShell_Core(VendorShellWidget vw, XawVendorShellExtPart *ve,
		       XawIcTableList p)
{
    XVaNestedList		pe_attr, st_attr;
    XRectangle			pe_area, st_area;
    XRectangle			*get_pe_area = NULL, *get_st_area = NULL;

    st_area.width = 0;
    if (p->input_style & XIMStatusArea) {
	st_attr = XVaCreateNestedList(0, XNArea, &get_st_area, NULL);
	XGetICValues(p->xic, XNStatusAttributes, st_attr, NULL);
	XFree(st_attr);
	if (p->xic == NULL) {
	    return(FALSE);
	}
	st_area.x = 0;
	st_area.y = (short)(vw->core.height - ve->im.area_height);
	st_area.width = get_st_area->width;
	st_area.height = get_st_area->height;
	XFree(get_st_area);
	st_attr = XVaCreateNestedList(0, XNArea, &st_area, NULL);
	XSetICValues(p->xic, XNStatusAttributes, st_attr, NULL);
	XFree(st_attr);
	if (p->xic == NULL) {
	    return(FALSE);
	}
    }
    if (p->input_style & XIMPreeditArea) {
	pe_attr = XVaCreateNestedList(0, XNArea, &get_pe_area, NULL);
	XGetICValues(p->xic, XNPreeditAttributes, pe_attr, NULL);
	XFree(pe_attr);
	if (p->xic == NULL) {
	    return(FALSE);
	}
	pe_area.x = (short)st_area.width;
	pe_area.y = (short)(vw->core.height - ve->im.area_height);
	pe_area.width = vw->core.width;
	pe_area.height = get_pe_area->height;
	if (p->input_style & XIMStatusArea) {
	    pe_area.width = (unsigned short)(pe_area.width - st_area.width);
	}
	XFree(get_pe_area);
	pe_attr = XVaCreateNestedList(0, XNArea, &pe_area, NULL);
	XSetICValues(p->xic, XNPreeditAttributes, pe_attr, NULL);
	XFree(pe_attr);
    }
    return(TRUE);
}

static void
ResizeVendorShell(VendorShellWidget vw, XawVendorShellExtPart *ve)
{
    XawIcTableList               p;

    if (IsSharedIC(ve)) {
	p = ve->ic.shared_ic_table;
	if (p->xic == NULL) return;
	ResizeVendorShell_Core(vw, ve, p);
	return;
    }
    for (p = ve->ic.ic_table; p; p = p->next) {
	if (p->xic == NULL) continue;
	if (ResizeVendorShell_Core(vw, ve, p) == FALSE) return;
    }
}

static XawIcTableList
CreateIcTable(Widget w, XawVendorShellExtPart *ve _X_UNUSED)
{
    XawIcTableList	table;

    table = (XawIcTableList) XtMalloc(sizeof(XawIcTablePart));
    if (table == NULL) return(NULL);
    table->widget = w;
    table->xic = NULL;
    table->flg = table->prev_flg = 0;
    table->font_set = NULL;
    table->foreground = table->background = 0xffffffff;
    table->bg_pixmap = 0;
    table->cursor_position = 0xffff;
    table->line_spacing = 0;
    table->ic_focused = FALSE;
    table->openic_error = FALSE;
    return(table);
}

static Bool
RegisterToVendorShell(Widget w, XawVendorShellExtPart *ve)
{
    XawIcTableList	table;

    if ((table = CreateIcTable(w, ve)) == NULL) return(FALSE);
    table->next = ve->ic.ic_table;
    ve->ic.ic_table = table;
    return(TRUE);
}

static void
UnregisterFromVendorShell(Widget w, XawVendorShellExtPart *ve)
{
    XawIcTableList	*prev, p;

    for (prev = &ve->ic.ic_table; (p = *prev) != NULL; prev = &p->next) {
	if (p->widget == w) {
	    *prev = p->next;
	    XtFree((char *)p);
	    break;
	}
    }
    return;
}

static void
SetICValuesShared(Widget w, XawVendorShellExtPart *ve,
		  XawIcTableList p, Bool check)
{
    XawIcTableList	pp;

    if ((pp = GetIcTable(w, ve)) == NULL) return;
    if (check == TRUE && CurrentSharedIcTable(ve) != pp) return;

    if (pp->prev_flg & CICursorP && p->cursor_position != pp->cursor_position) {
	p->cursor_position = pp->cursor_position;
	p->flg |= CICursorP;
    }
    if (pp->prev_flg & CIFontSet && p->font_set != pp->font_set) {
	p->font_set = pp->font_set;
	p->flg |= (CIFontSet|CICursorP);
    }
    if (pp->prev_flg & CIFg && p->foreground != pp->foreground) {
	p->foreground = pp->foreground;
	p->flg |= CIFg;
    }
    if (pp->prev_flg & CIBg && p->background != pp->background) {
	p->background = pp->background;
	p->flg |= CIBg;
    }
    if (pp->prev_flg & CIBgPixmap && p->bg_pixmap != pp->bg_pixmap) {
	p->bg_pixmap = pp->bg_pixmap;
	p->flg |= CIBgPixmap;
    }
    if (pp->prev_flg & CILineS && p->line_spacing != pp->line_spacing) {
	p->line_spacing = pp->line_spacing;
	p->flg |= CILineS;
    }
}

static Bool
IsCreatedIC(Widget w, XawVendorShellExtPart *ve)
{
    XawIcTableList	p;

    if (ve->im.xim == NULL) return(FALSE);
    if ((p = GetIcTableShared(w, ve)) == NULL) return(FALSE);
    if (p->xic == NULL) return(FALSE);
    return(TRUE);
}

static void
SizeNegotiation(XawIcTableList p, unsigned int width, unsigned int height)
{
    XRectangle		pe_area, st_area;
    XVaNestedList	pe_attr = NULL, st_attr = NULL;
    int			ic_cnt = 0;
    XRectangle		*pe_area_needed = NULL, *st_area_needed = NULL;
    XPointer		ic_a[5];

    if (p->input_style & XIMPreeditArea) {
	pe_attr = XVaCreateNestedList(0, XNAreaNeeded, &pe_area_needed, NULL);
	ic_a[ic_cnt] = (XPointer) XNPreeditAttributes; ic_cnt++;
	ic_a[ic_cnt] = (XPointer) pe_attr; ic_cnt++;
    }
    if (p->input_style & XIMStatusArea) {
	st_attr = XVaCreateNestedList(0, XNAreaNeeded, &st_area_needed, NULL);
	ic_a[ic_cnt] = (XPointer) XNStatusAttributes; ic_cnt++;
	ic_a[ic_cnt] = (XPointer) st_attr; ic_cnt++;
    }
    ic_a[ic_cnt] = (XPointer) NULL;

    if (ic_cnt > 0) {
	XGetICValues(p->xic, ic_a[0], ic_a[1], ic_a[2], ic_a[3], NULL);
	if (pe_attr) XFree(pe_attr);
	if (st_attr) XFree(st_attr);
	if (p->xic == NULL) {
	    p->openic_error = True;
	    return;
	}
	pe_attr = st_attr = NULL;
	ic_cnt = 0;
	if (p->input_style & XIMStatusArea) {
	    st_area.height = st_area_needed->height;
	    st_area.x = 0;
	    st_area.y = (short)(height - st_area.height);
	    if (p->input_style & XIMPreeditArea) {
		st_area.width = st_area_needed->width;
	    } else {
		st_area.width = (unsigned short)width;
	    }

	    XFree(st_area_needed);
	    st_attr = XVaCreateNestedList(0, XNArea, &st_area, NULL);
	    ic_a[ic_cnt] = (XPointer) XNStatusAttributes; ic_cnt++;
	    ic_a[ic_cnt] = (XPointer) st_attr; ic_cnt++;
	}
	if (p->input_style & XIMPreeditArea) {
	    if (p->input_style & XIMStatusArea) {
		pe_area.x = (short)st_area.width;
		pe_area.width = (unsigned short)(width - st_area.width);
	    } else {
		pe_area.x = 0;
		pe_area.width = (unsigned short)width;
	    }
	    pe_area.height = pe_area_needed->height;
	    XFree(pe_area_needed);
	    pe_area.y = (short)(height - pe_area.height);
	    pe_attr = XVaCreateNestedList(0, XNArea, &pe_area, NULL);
	    ic_a[ic_cnt] = (XPointer) XNPreeditAttributes; ic_cnt++;
	    ic_a[ic_cnt] = (XPointer) pe_attr; ic_cnt++;
	}
	ic_a[ic_cnt] = (XPointer) NULL;
	XSetICValues(p->xic, ic_a[0], ic_a[1], ic_a[2], ic_a[3], NULL);
	if (pe_attr) XFree(pe_attr);
	if (st_attr) XFree(st_attr);
	if (p->xic == NULL) {
	    p->openic_error = True;
	    return;
	}
    }
}

static void
CreateIC(Widget w, XawVendorShellExtPart *ve)
{
    XawIcTableList	p;
    XPoint		position;
    XRectangle		pe_area, st_area;
    XVaNestedList	pe_attr = NULL, st_attr = NULL;
    XPointer		ic_a[20], pe_a[20], st_a[20];
    Dimension		height = 0;
    int			ic_cnt = 0, pe_cnt = 0, st_cnt = 0;
    XawTextMargin	*margin;

    if (!XtIsRealized(w)) return;
    if (((ve->im.xim == NULL) || (p = GetIcTableShared(w, ve)) == NULL) ||
	p->xic || (p->openic_error != FALSE)) return;

    p->input_style = GetInputStyleOfIC(ve);

    if (IsSharedIC(ve)) SetICValuesShared(w, ve, p, FALSE);
    XFlush(XtDisplay(w));

    if (p->input_style & (XIMPreeditArea|XIMPreeditPosition|XIMStatusArea)) {
	if (p->flg & CIFontSet) {
	    pe_a[pe_cnt] = (XPointer) XNFontSet; pe_cnt++;
	    pe_a[pe_cnt] = (XPointer) p->font_set; pe_cnt++;
	    st_a[st_cnt] = (XPointer) XNFontSet; st_cnt++;
	    st_a[st_cnt] = (XPointer) p->font_set; st_cnt++;
	    if (p->font_set) {
		height = (Dimension)(maxAscentOfFontSet(p->font_set)
				     + maxDescentOfFontSet(p->font_set));
	    }
	    height = (Dimension)SetVendorShellHeight(ve, height);
	}
	if (p->flg & CIFg) {
	    pe_a[pe_cnt] = (XPointer) XNForeground; pe_cnt++;
	    pe_a[pe_cnt] = (XPointer) p->foreground; pe_cnt++;
	    st_a[st_cnt] = (XPointer) XNForeground; st_cnt++;
	    st_a[st_cnt] = (XPointer) p->foreground; st_cnt++;
	}
	if (p->flg & CIBg) {
	    pe_a[pe_cnt] = (XPointer) XNBackground; pe_cnt++;
	    pe_a[pe_cnt] = (XPointer) p->background; pe_cnt++;
	    st_a[st_cnt] = (XPointer) XNBackground; st_cnt++;
	    st_a[st_cnt] = (XPointer) p->background; st_cnt++;
	}
	if (p->flg & CIBgPixmap) {
	    pe_a[pe_cnt] = (XPointer) XNBackgroundPixmap; pe_cnt++;
	    pe_a[pe_cnt] = (XPointer) p->bg_pixmap; pe_cnt++;
	    st_a[st_cnt] = (XPointer) XNBackgroundPixmap; st_cnt++;
	    st_a[st_cnt] = (XPointer) p->bg_pixmap; st_cnt++;
	}
	if (p->flg & CILineS) {
	    pe_a[pe_cnt] = (XPointer) XNLineSpace; pe_cnt++;
	    pe_a[pe_cnt] = (XPointer) p->line_spacing; pe_cnt++;
	    st_a[st_cnt] = (XPointer) XNLineSpace; st_cnt++;
	    st_a[st_cnt] = (XPointer) p->line_spacing; st_cnt++;
	}
    }
    if (p->input_style & XIMPreeditArea) {
	pe_area.x = 0;
	pe_area.y = (short)(ve->parent->core.height - height);
	pe_area.width = ve->parent->core.width;
	pe_area.height = height;
	pe_a[pe_cnt] = (XPointer) XNArea; pe_cnt++;
	pe_a[pe_cnt] = (XPointer) &pe_area; pe_cnt++;
    }
    if (p->input_style & XIMPreeditPosition) {
	pe_area.x = 0;
	pe_area.y = 0;
	pe_area.width = w->core.width;
	pe_area.height = w->core.height;
	margin = &(((TextWidget)w)->text.margin);
	pe_area.x = (short)(pe_area.x + margin->left);
	pe_area.y = (short)(pe_area.y + margin->top);
	pe_area.width = (unsigned short)(pe_area.width - (margin->left + margin->right - 1));
	pe_area.height = (unsigned short)(pe_area.height - (margin->top + margin->bottom - 1));
	pe_a[pe_cnt] = (XPointer) XNArea; pe_cnt++;
	pe_a[pe_cnt] = (XPointer) &pe_area; pe_cnt++;
	if (p->flg & CICursorP) {
	    _XawMultiSinkPosToXY(w, p->cursor_position, &position.x, &position.y);
	} else {
	    position.x = position.y = 0;
	}
	pe_a[pe_cnt] = (XPointer) XNSpotLocation; pe_cnt++;
	pe_a[pe_cnt] = (XPointer) &position; pe_cnt++;
    }
    if (p->input_style & XIMStatusArea) {
	st_area.x = 0;
	st_area.y = (short)(ve->parent->core.height - height);
	st_area.width = ve->parent->core.width;
	st_area.height = height;
	st_a[st_cnt] = (XPointer) XNArea; st_cnt++;
	st_a[st_cnt] = (XPointer) &st_area; st_cnt++;
    }

    ic_a[ic_cnt] = (XPointer) XNInputStyle; ic_cnt++;
    ic_a[ic_cnt] = (XPointer) p->input_style; ic_cnt++;
    ic_a[ic_cnt] = (XPointer) XNClientWindow; ic_cnt++;
    ic_a[ic_cnt] = (XPointer) XtWindow(ve->parent); ic_cnt++;
    ic_a[ic_cnt] = (XPointer) XNFocusWindow; ic_cnt++;
    ic_a[ic_cnt] = (XPointer) XtWindow(w); ic_cnt++;

    if (pe_cnt > 0) {
	pe_a[pe_cnt] = (XPointer) NULL;
	pe_attr = XVaCreateNestedList(0, pe_a[0], pe_a[1], pe_a[2], pe_a[3],
				   pe_a[4], pe_a[5], pe_a[6], pe_a[7], pe_a[8],
				   pe_a[9], pe_a[10], pe_a[11], pe_a[12],
				   pe_a[13], pe_a[14], pe_a[15], pe_a[16],
				   pe_a[17], pe_a[18],  NULL);
	ic_a[ic_cnt] = (XPointer) XNPreeditAttributes; ic_cnt++;
	ic_a[ic_cnt] = (XPointer) pe_attr; ic_cnt++;
    }

    if (st_cnt > 0) {
	st_a[st_cnt] = (XPointer) NULL;
	st_attr = XVaCreateNestedList(0, st_a[0], st_a[1], st_a[2], st_a[3],
				   st_a[4], st_a[5], st_a[6], st_a[7], st_a[8],
				   st_a[9], st_a[10], st_a[11], st_a[12],
				   st_a[13], st_a[14], st_a[15], st_a[16],
				   st_a[17], st_a[18],  NULL);
	ic_a[ic_cnt] = (XPointer) XNStatusAttributes; ic_cnt++;
	ic_a[ic_cnt] = (XPointer) st_attr; ic_cnt++;
    }
    ic_a[ic_cnt] = (XPointer) NULL;

    p->xic = XCreateIC(ve->im.xim, ic_a[0], ic_a[1], ic_a[2], ic_a[3],
		       ic_a[4], ic_a[5], ic_a[6], ic_a[7], ic_a[8], ic_a[9],
		       ic_a[10], ic_a[11], ic_a[12], ic_a[13], ic_a[14],
		       ic_a[15], ic_a[16], ic_a[17], ic_a[18], NULL);
    if (pe_attr) XtFree(pe_attr);
    if (st_attr) XtFree(st_attr);

    if (p->xic == NULL) {
	p->openic_error = True;
	return;
    }

    SizeNegotiation(p, ve->parent->core.width, ve->parent->core.height);

    p->flg &= (unsigned long)(~(CIFontSet | CIFg | CIBg | CIBgPixmap | CICursorP | CILineS));

    if (!IsSharedIC(ve)) {
	if (p->input_style & XIMPreeditPosition) {
	    XtAddEventHandler(w, (EventMask)StructureNotifyMask, FALSE,
			      (XtEventHandler)ConfigureCB, (Opaque)NULL);
	}
    }
}

static void
SetICValues(Widget w, XawVendorShellExtPart *ve, Bool focus)
{
    XawIcTableList	p;
    XPoint		position;
    XRectangle		pe_area;
    XVaNestedList	pe_attr = NULL, st_attr = NULL;
    XPointer		ic_a[20], pe_a[20], st_a[20];
    int			ic_cnt = 0, pe_cnt = 0, st_cnt = 0;
    XawTextMargin	*margin;
    int			height = 0;

    if ((ve->im.xim == NULL) || ((p = GetIcTableShared(w, ve)) == NULL) ||
	(p->xic == NULL)) return;

    if (IsSharedIC(ve)) SetICValuesShared(w, ve, p, TRUE);
    XFlush(XtDisplay(w));
    if (focus == FALSE &&
	!(p->flg & (CIFontSet | CIFg | CIBg |
		    CIBgPixmap | CICursorP | CILineS))) return;
#ifdef SPOT
    if ((p->input_style & XIMPreeditPosition)
	&& ((!IsSharedIC(ve) && ((p->flg & ~CIICFocus) == CICursorP))
	    || (IsSharedIC(ve) && p->flg == CICursorP))) {
	_XawMultiSinkPosToXY(w, p->cursor_position, &position.x, &position.y);
	_XipChangeSpot(p->xic, position.x, position.y);
	p->flg &= ~CICursorP;
	return;
    }
#endif

    if (p->input_style & (XIMPreeditArea|XIMPreeditPosition|XIMStatusArea)) {
	if (p->flg & CIFontSet) {
	    pe_a[pe_cnt] = (XPointer) XNFontSet; pe_cnt++;
	    pe_a[pe_cnt] = (XPointer) p->font_set; pe_cnt++;
	    st_a[st_cnt] = (XPointer) XNFontSet; st_cnt++;
	    st_a[st_cnt] = (XPointer) p->font_set; st_cnt++;
	    if (p->font_set) {
		height = maxAscentOfFontSet(p->font_set)
		    + maxDescentOfFontSet(p->font_set);
	    }
	    height = (int)SetVendorShellHeight(ve, (unsigned)height);
	}
	if (p->flg & CIFg) {
	    pe_a[pe_cnt] = (XPointer) XNForeground; pe_cnt++;
	    pe_a[pe_cnt] = (XPointer) p->foreground; pe_cnt++;
	    st_a[st_cnt] = (XPointer) XNForeground; st_cnt++;
	    st_a[st_cnt] = (XPointer) p->foreground; st_cnt++;
	}
	if (p->flg & CIBg) {
	    pe_a[pe_cnt] = (XPointer) XNBackground; pe_cnt++;
	    pe_a[pe_cnt] = (XPointer) p->background; pe_cnt++;
	    st_a[st_cnt] = (XPointer) XNBackground; st_cnt++;
	    st_a[st_cnt] = (XPointer) p->background; st_cnt++;
	}
	if (p->flg & CIBgPixmap) {
	    pe_a[pe_cnt] = (XPointer) XNBackgroundPixmap; pe_cnt++;
	    pe_a[pe_cnt] = (XPointer) p->bg_pixmap; pe_cnt++;
	    st_a[st_cnt] = (XPointer) XNBackgroundPixmap; st_cnt++;
	    st_a[st_cnt] = (XPointer) p->bg_pixmap; st_cnt++;
	}
	if (p->flg & CILineS) {
	    pe_a[pe_cnt] = (XPointer) XNLineSpace; pe_cnt++;
	    pe_a[pe_cnt] = (XPointer) p->line_spacing; pe_cnt++;
	    st_a[st_cnt] = (XPointer) XNLineSpace; st_cnt++;
	    st_a[st_cnt] = (XPointer) p->line_spacing; st_cnt++;
	}
    }
    if (p->input_style & XIMPreeditPosition) {
	if (p->flg & CICursorP) {
	    _XawMultiSinkPosToXY(w, p->cursor_position, &position.x, &position.y);
	    pe_a[pe_cnt] = (XPointer) XNSpotLocation; pe_cnt++;
	    pe_a[pe_cnt] = (XPointer) &position; pe_cnt++;
	}
    }
    if (IsSharedIC(ve)) {
	if (p->input_style & XIMPreeditPosition) {
	    pe_area.x = 0;
	    pe_area.y = 0;
	    pe_area.width = w->core.width;
	    pe_area.height = w->core.height;
	    margin = &(((TextWidget)w)->text.margin);
	    pe_area.x = (short)(pe_area.x + margin->left);
	    pe_area.y = (short)(pe_area.y + margin->top);
	    pe_area.width = (unsigned short)(pe_area.width - (margin->left + margin->right - 1));
	    pe_area.height = (unsigned short)(pe_area.height - (margin->top + margin->bottom - 1));
	    pe_a[pe_cnt] = (XPointer) XNArea; pe_cnt++;
	    pe_a[pe_cnt] = (XPointer) &pe_area; pe_cnt++;
	}
    }

    if (pe_cnt > 0) {
	pe_a[pe_cnt] = (XPointer) NULL;
	pe_attr = XVaCreateNestedList(0, pe_a[0], pe_a[1], pe_a[2], pe_a[3],
				      pe_a[4], pe_a[5], pe_a[6], pe_a[7],
				      pe_a[8], pe_a[9], pe_a[10], pe_a[11],
				      pe_a[12], pe_a[13], pe_a[14], pe_a[15],
				      pe_a[16], pe_a[17], pe_a[18],  NULL);
	ic_a[ic_cnt] = (XPointer) XNPreeditAttributes; ic_cnt++;
	ic_a[ic_cnt] = (XPointer) pe_attr; ic_cnt++;
    }
    if (st_cnt > 0) {
	st_a[st_cnt] = (XPointer) NULL;
	st_attr = XVaCreateNestedList(0, st_a[0], st_a[1], st_a[2], st_a[3],
				      st_a[4], st_a[5], st_a[6], st_a[7],
				      st_a[8], st_a[9], st_a[10], st_a[11],
				      st_a[12], st_a[13], st_a[14], st_a[15],
				      st_a[16], st_a[17], st_a[18],  NULL);
	ic_a[ic_cnt] = (XPointer) XNStatusAttributes; ic_cnt++;
	ic_a[ic_cnt] = (XPointer) st_attr; ic_cnt++;
    }
    if (focus == TRUE) {
	ic_a[ic_cnt] = (XPointer) XNFocusWindow; ic_cnt++;
	ic_a[ic_cnt] = (XPointer) XtWindow(w); ic_cnt++;
    }
    if (ic_cnt > 0) {
	ic_a[ic_cnt] = (XPointer) NULL;
	XSetICValues(p->xic, ic_a[0], ic_a[1], ic_a[2], ic_a[3], ic_a[4],
		     ic_a[5], ic_a[6], ic_a[7], ic_a[8], ic_a[9], ic_a[10],
		     ic_a[11], ic_a[12], ic_a[13], ic_a[14], ic_a[15],
		     ic_a[16], ic_a[17], ic_a[18], NULL);
	if (pe_attr) XtFree(pe_attr);
	if (st_attr) XtFree(st_attr);
    }

    if (IsSharedIC(ve) && p->flg & CIFontSet)
	SizeNegotiation(p, ve->parent->core.width, ve->parent->core.height);

    p->flg &= (unsigned long)(~(CIFontSet | CIFg | CIBg | CIBgPixmap | CICursorP | CILineS));
}

static void
SharedICChangeFocusWindow(Widget w, XawVendorShellExtPart *ve,
			  XawIcTableList p _X_UNUSED)
{
    XawIcTableList	pp;

    if (w == NULL) {
	ve->ic.current_ic_table = NULL;
	return;
    }
    if ((pp = GetIcTable(w, ve)) == NULL) return;
    ve->ic.current_ic_table = pp;
    SetICValues(w, ve, TRUE);
}

static XawIcTableList
CurrentSharedIcTable(XawVendorShellExtPart *ve)
{
    return(ve->ic.current_ic_table);
}

static void
SetICFocus(Widget w, XawVendorShellExtPart *ve)
{
    XawIcTableList	p, pp;

    if ((ve->im.xim == NULL) || ((p = GetIcTableShared(w, ve)) == NULL) ||
	(p->xic == NULL)) return;

    if (IsSharedIC(ve)) {
	pp = CurrentSharedIcTable(ve);
	if (pp == NULL || pp->widget != w) {
	    SharedICChangeFocusWindow(w, ve, p);
	}
    }
    if (p->flg & CIICFocus && p->ic_focused == FALSE) {
	p->ic_focused = TRUE;
	XSetICFocus(p->xic);
    }
    p->flg &= (unsigned long)(~CIICFocus);
}

static void
UnsetICFocus(Widget w, XawVendorShellExtPart *ve)
{
    XawIcTableList	p, pp;

    if ((ve->im.xim == NULL) || ((p = GetIcTableShared(w, ve)) == NULL) ||
	(p->xic == NULL)) return;

    if (IsSharedIC(ve) && (pp = CurrentSharedIcTable(ve))) {
	if (pp->widget != w) {
	    return;
	}
	SharedICChangeFocusWindow(NULL, ve, p);
    }
    if (p->ic_focused == TRUE) {
	XUnsetICFocus(p->xic);
	p->ic_focused = FALSE;
    }
}

static void
SetValues(Widget w, XawVendorShellExtPart *ve,
	  ArgList args, Cardinal num_args)
{
    ArgList	arg;

    XrmName	argName;
    XrmResourceList	xrmres;
    Cardinal	i;
    XawIcTablePart	*p, save_tbl;

    if ((p = GetIcTable(w, ve)) == NULL) return;

    memcpy(&save_tbl, p, sizeof(XawIcTablePart));

    for (arg = args ; num_args != 0; num_args--, arg++) {
	argName = XrmStringToName(arg->name);
	for (xrmres = (XrmResourceList)ve->im.resources, i = 0;
	     i < ve->im.num_resources; i++, xrmres++) {
            if (argName == xrmres->xrm_name) {
                _XtCopyFromArg(arg->value,
			       (char *)p - xrmres->xrm_offset - 1,
			       xrmres->xrm_size);
                break;
            }
        }
    }
    if (p->font_set != save_tbl.font_set) {
	p->flg |= CIFontSet;
    }
    if (p->foreground != save_tbl.foreground) {
	p->flg |= CIFg;
    }
    if (p->background !=save_tbl.background) {
	p->flg |= CIBg;
    }
    if (p->bg_pixmap != save_tbl.bg_pixmap) {
	p->flg |= CIBgPixmap;
    }
    if (p->cursor_position != save_tbl.cursor_position) {
	p->flg |= CICursorP;
    }
    if (p->line_spacing != save_tbl.line_spacing) {
	p->flg |= CILineS;
    }
    p->prev_flg |= p->flg;
}

static void
SetFocus(Widget w, XawVendorShellExtPart *ve)
{
    XawIcTableList	p;
    if ((p = GetIcTableShared(w, ve)) == NULL) return;

    if ( p->ic_focused == FALSE || IsSharedIC(ve)) {
	p->flg |= CIICFocus;
    }
    p->prev_flg |= p->flg;
}

static void
DestroyIC(Widget w, XawVendorShellExtPart *ve)
{
    XawIcTableList	p;

    if ((ve->im.xim == NULL) || ((p = GetIcTableShared(w, ve)) == NULL) ||
	(p->xic == NULL)) return;
    if (IsSharedIC(ve)) {
	if (GetIcTable(w, ve) == ve->ic.current_ic_table) {
	    UnsetICFocus(w, ve);
	}
        return;
    }
    XDestroyIC(p->xic);
    if (!IsSharedIC(ve)) {
	if (p->input_style & XIMPreeditPosition) {
	    XtRemoveEventHandler(w, (EventMask)StructureNotifyMask, FALSE,
				 (XtEventHandler)ConfigureCB, (Opaque)NULL);
	}
    }
}

static void
SetFocusValues(Widget inwidg, ArgList args, Cardinal num_args, Bool focus)
{
    XawVendorShellExtPart	*ve;
    VendorShellWidget		vw;

    if ((vw = SearchVendorShell(inwidg)) == NULL) return;
    if ((ve = GetExtPart(vw)) != NULL) {
	if (num_args > 0) SetValues(inwidg, ve, args, num_args);
	if (focus) SetFocus(inwidg, ve);
	if (XtIsRealized((Widget)vw) && ve->im.xim) {
	    if (IsCreatedIC(inwidg, ve)) {
		SetICValues(inwidg, ve, FALSE);
		if (focus) SetICFocus(inwidg, ve);
	    } else {
		CreateIC(inwidg, ve);
		SetICFocus(inwidg, ve);
	    }
	}
    }
}

static void
UnsetFocus(Widget inwidg)
{
    XawVendorShellExtPart	*ve;
    VendorShellWidget		vw;
    XawIcTableList		p;

    if ((vw = SearchVendorShell(inwidg)) == NULL) return;
    if ((ve = GetExtPart(vw)) != NULL) {
	if ((p = GetIcTableShared(inwidg, ve)) == NULL) return;
	if (p->flg & CIICFocus) {
	    p->flg &= (unsigned long)(~CIICFocus);
	}
	p->prev_flg &= (unsigned long)(~CIICFocus);
	if (ve->im.xim && XtIsRealized((Widget)vw) && p->xic) {
	    UnsetICFocus(inwidg, ve);
	}
    }
}

static Bool
IsRegistered(Widget w, XawVendorShellExtPart* ve)
{
    XawIcTableList	p;

    for (p = ve->ic.ic_table; p; p = p->next)
	{
	    if (p->widget == w) return(TRUE);
	}
    return(FALSE);
}

static void
Register(Widget inwidg, XawVendorShellExtPart* ve)
{
    if (ve->im.xim == NULL)
	{
	    OpenIM(ve);
	}

    if (IsRegistered(inwidg, ve)) return;

    if (RegisterToVendorShell(inwidg, ve) == FALSE) return;

    if (ve->im.xim == NULL) return;

    if (XtIsRealized(ve->parent))
	{
	    CreateIC(inwidg, ve);
	    SetICFocus(inwidg, ve);
	}
}

static Bool
NoRegistered(XawVendorShellExtPart* ve)
{
    if (ve->ic.ic_table == NULL) return(TRUE);
    return(FALSE);
}

static void
Unregister(Widget inwidg, XawVendorShellExtPart *ve)
{
    if (!IsRegistered(inwidg, ve)) return;

    DestroyIC(inwidg, ve);

    UnregisterFromVendorShell(inwidg, ve);

    if (NoRegistered(ve))
	{
	    CloseIM(ve);
	    ve->im.xim = NULL;
	    /*
	     * resize vendor shell to core size
	    */
	    (void) SetVendorShellHeight(ve, 0);
	}
}

static void
AllCreateIC(XawVendorShellExtPart *ve)
{
    XawIcTableList p;

    if (ve->im.xim == NULL) return;
    if (IsSharedIC(ve) && ve->ic.ic_table[0].widget) {
	p = ve->ic.shared_ic_table;
	if (p->xic == NULL)
	    CreateIC(ve->ic.ic_table[0].widget, ve);
	SetICFocus(ve->ic.ic_table[0].widget, ve);
	return;
    }
    for (p = ve->ic.ic_table; p; p = p->next) {
	if (p->xic == NULL)
	    CreateIC(p->widget, ve);
    }
    for (p = ve->ic.ic_table; p; p = p->next) {
	SetICFocus(p->widget, ve);
    }
}


static void
Reconnect(XawVendorShellExtPart *ve)
{
    XawIcTableList	p;

    ve->im.open_im = True;
    if (ve->im.xim == NULL) {
	OpenIM(ve);
    }
    if (ve->im.xim == NULL) return;

    if (IsSharedIC(ve)) {
	p = ve->ic.shared_ic_table;
	p->flg = p->prev_flg;
	p->openic_error = FALSE;
    } else {
	for (p = ve->ic.ic_table; p; p = p->next) {
	    p->flg = p->prev_flg;
	    p->openic_error = FALSE;
	}
    }
    AllCreateIC(ve);
}


static void
CompileResourceList(XtResourceList res, unsigned int num_res)
{
    unsigned int count;

#define xrmres	((XrmResourceList) res)
    for (count = 0; count < num_res; res++, count++) {
	xrmres->xrm_name         = XrmPermStringToQuark(res->resource_name);
	xrmres->xrm_class        = XrmPermStringToQuark(res->resource_class);
	xrmres->xrm_type         = XrmPermStringToQuark(res->resource_type);
	xrmres->xrm_offset	 = (int)(-res->resource_offset - 1);
	xrmres->xrm_default_type = XrmPermStringToQuark(res->default_type);
    }
#undef xrmres
}

static Bool
Initialize(VendorShellWidget vw, XawVendorShellExtPart *ve)
{
    if (!XtIsVendorShell((Widget)vw)) return(FALSE);
    ve->parent = (Widget)vw;
    ve->im.xim = NULL;
    ve->im.area_height = 0;
    ve->im.resources = (XrmResourceList)XtMalloc(sizeof(resources));
    if (ve->im.resources == NULL) return(FALSE);
    memcpy((char *)ve->im.resources, (char *)resources, sizeof(resources));
    ve->im.num_resources = XtNumber(resources);
    CompileResourceList( (XtResourceList) ve->im.resources,
			   ve->im.num_resources );
    if ((ve->ic.shared_ic_table = CreateIcTable( (Widget)vw, ve)) == NULL)
	return(FALSE);
    ve->ic.current_ic_table = NULL;
    ve->ic.ic_table = NULL;
    return(TRUE);
}


/* Destroy()
 *
 * This frees all (most?) of the resources malloced by XawIm.
 * It is called by _XawImDestroy, which is called by Vendor.c's
 * VendorExt's Destroy method.           Sheeran, Omron KK, 93/08/05 */

static void
Destroy(Widget w, XawVendorShellExtPart *ve)
{
    contextDataRec *contextData;
    contextErrDataRec *contextErrData;

    if (!XtIsVendorShell( w ) )
	return;
    XtFree( (char*) ve->im.resources );

    if (extContext != (XContext)0 &&
	!XFindContext (XtDisplay (w), (Window)w,
		       extContext, (XPointer*)&contextData))
        XtFree( (char*) contextData );

    if (errContext != (XContext)0 &&
	!XFindContext (XDisplayOfIM( ve->im.xim ), (Window) ve->im.xim,
		       errContext, (XPointer*) &contextErrData))
        XtFree( (char*) contextErrData );
}

/*********************************************
 *
 * SEMI-PRIVATE FUNCTIONS
 * For use by other Xaw modules
 *
 ********************************************/

void
_XawImResizeVendorShell(Widget w)
{
    XawVendorShellExtPart *ve;

    if ( ( ve = GetExtPart( (VendorShellWidget) w ) ) && ve->im.xim ) {
	ResizeVendorShell( (VendorShellWidget) w, ve );
    }
}


Dimension
_XawImGetShellHeight(Widget w)
{
    XawVendorShellExtPart *ve;

    if (!XtIsVendorShell( w ) ) return( w->core.height );
    if ( (ve = GetExtPart( (VendorShellWidget) w )) != NULL ) {
	return (Dimension)( w->core.height - ve->im.area_height );
    }
    return( w->core.height );
}

void
_XawImRealize(Widget w)
{
    XawVendorShellExtPart	*ve;

    if ( !XtIsRealized( w ) || !XtIsVendorShell( w ) ) return;
    if ( (ve = GetExtPart( (VendorShellWidget) w )) != NULL ) {
	XtAddEventHandler( w, (EventMask)StructureNotifyMask, FALSE,
			  XawVendorStructureNotifyHandler, (XtPointer)NULL );
	AllCreateIC(ve);
    }
}

void
_XawImInitialize(Widget w, Widget ext)
{
    XawVendorShellExtPart	*ve;

    if ( !XtIsVendorShell( w ) ) return;
    if ( (ve = SetExtPart( (VendorShellWidget) w, (XawVendorShellExtWidget)ext )) != NULL ) {
	if ( Initialize( (VendorShellWidget) w, ve ) == FALSE ) return;
	XtAddCallback( w, XtNdestroyCallback, VendorShellDestroyed,
		      (XtPointer) NULL );
    }
}

void
_XawImReconnect(Widget inwidg)
{
    XawVendorShellExtPart	*ve;
    VendorShellWidget		vw;

    if ((vw = SearchVendorShell(inwidg)) == NULL) return;
    if ((ve = GetExtPart(vw)) != NULL) {
	Reconnect(ve);
    }
}

void
_XawImRegister(Widget inwidg)
{
    XawVendorShellExtPart	*ve;
    VendorShellWidget		vw;

    if ((vw = SearchVendorShell(inwidg)) == NULL) return;
    if ((ve = GetExtPart(vw)) != NULL) {
	Register(inwidg, ve);
    }
}

void
_XawImUnregister(Widget inwidg)
{
    XawVendorShellExtPart	*ve;
    VendorShellWidget		vw;

    if ((vw = SearchVendorShell(inwidg)) == NULL) return;
    if ((ve = GetExtPart(vw)) != NULL) {
	Unregister(inwidg, ve);
    }
}

void
_XawImSetValues(Widget inwidg, ArgList args, Cardinal num_args)
{
    SetFocusValues( inwidg, args, num_args, FALSE );
}

void
_XawImSetFocusValues(Widget inwidg, ArgList args, Cardinal num_args)
{
    SetFocusValues(inwidg, args, num_args, TRUE);
}

void
_XawImUnsetFocus(Widget inwidg)
{
    UnsetFocus(inwidg);
}

int
_XawImWcLookupString(Widget inwidg, XKeyPressedEvent *event,
		     wchar_t* buffer_return, int bytes_buffer,
		     KeySym *keysym_return)
{
    XawVendorShellExtPart*	ve;
    VendorShellWidget		vw;
    XawIcTableList		p;
    int				i, ret;
    char			tmp_buf[64], *tmp_p;
    wchar_t*			buf_p;

    if ((vw = SearchVendorShell(inwidg)) && (ve = GetExtPart(vw)) &&
	ve->im.xim && (p = GetIcTableShared(inwidg, ve)) && p->xic) {
	  return(XwcLookupString(p->xic, event, buffer_return,
				 (int)((size_t)bytes_buffer/sizeof(wchar_t)),
				 keysym_return, NULL));
    }
    ret = XLookupString( event, tmp_buf, sizeof(tmp_buf), keysym_return,
		         NULL );
    for ( i = 0, tmp_p = tmp_buf, buf_p = buffer_return; i < ret; i++ ) {
	*buf_p++ = _Xaw_atowc((unsigned char)*tmp_p++);
    }
    return( ret );
}

int
_XawLookupString(Widget w, XKeyEvent *event, char *buffer_return, int buffer_size,
		 KeySym *keysym_return)
{
    XawVendorShellExtPart *ve;
    VendorShellWidget vw;
    XawIcTableList p;

    if ((vw = SearchVendorShell(w)) && (ve = GetExtPart(vw))
	&& ve->im.xim && (p = GetIcTableShared(w, ve)) && p->xic)
	return (XmbLookupString(p->xic, event, buffer_return, buffer_size,
				keysym_return, NULL));

    return (XLookupString(event, buffer_return, buffer_size,
			  keysym_return, NULL));
}

int
_XawImGetImAreaHeight(Widget w)
{
    XawVendorShellExtPart	*ve;
    VendorShellWidget		vw;

    if ((vw = SearchVendorShell(w)) && (ve = GetExtPart(vw))) {
	return(ve->im.area_height);
    }
    return(0);
}

void
_XawImCallVendorShellExtResize(Widget w)
{
    VendorShellWidget		vw;

    if ((vw = SearchVendorShell(w)) && GetExtPart(vw)) {
	XawVendorShellExtResize((Widget)vw);
    }
}


/* _XawImDestroy()
 *
 * This should be called by the VendorExt from its
 * core Destroy method.  Sheeran, Omron KK 93/08/05 */

void
_XawImDestroy(Widget w, Widget ext _X_UNUSED)
{
    XawVendorShellExtPart        *ve;

    if ( !XtIsVendorShell( w ) ) return;
    if ( (ve = GetExtPart( (VendorShellWidget) w )) != NULL )
        Destroy( w, ve );
}
