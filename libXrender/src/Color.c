/*
 *
 * Copyright © 2002 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "Xrenderint.h"

Status
XRenderParseColor(Display *dpy, char *spec, XRenderColor *def)
{

    if (!strncmp (spec, "rgba:", 5))
    {
	unsigned short	elements[4];
	unsigned short	*pShort;
	int		i;

	spec += 5;
	/*
	 * Attempt to parse the value portion.
	 */
	pShort = elements;
	for (i = 0; i < 4; i++, pShort++, spec++) {
	    int n = 0;

	    *pShort = 0;
	    while (*spec != '/' && *spec != '\0') {
		char	c;

		if (++n > 4) {
		    return 0;
		}
		c = *spec++;
		*pShort = (unsigned short) (*pShort << 4);
		if (c >= '0' && c <= '9')
		    *pShort = (unsigned short) (*pShort | (c - '0'));
		/* assume string in lowercase
		else if (c >= 'A' && c <= 'F')
		    *pShort |= c - ('A' - 10);
		*/
		else if (c >= 'a' && c <= 'f')
		    *pShort = (unsigned short) (*pShort | (c - ('a' - 10)));
		else return 0;
	    }
	    if (n == 0)
		return 0;
	    if (n < 4) {
		*pShort = (unsigned short) (((unsigned long)*pShort * 0xFFFF) / (unsigned long) ((1 << n*4) - 1));
	    }
	}
	def->red = elements[0];
	def->green = elements[1];
	def->blue = elements[2];
	def->alpha = elements[3];
    }
    else
    {
	XColor	    coreColor;
	Colormap    colormap;

	colormap = DefaultColormap (dpy, DefaultScreen (dpy));
	if (!XParseColor (dpy, colormap, spec, &coreColor))
	    return 0;
	def->red = coreColor.red;
	def->green = coreColor.green;
	def->blue = coreColor.blue;
	def->alpha = 0xffff;
    }
    def->red   = (unsigned short) ((unsigned) (def->red   * def->alpha) / 0xffffU);
    def->green = (unsigned short) ((unsigned) (def->green * def->alpha) / 0xffffU);
    def->blue  = (unsigned short) ((unsigned) (def->blue  * def->alpha) / 0xffffU);
    return 1;
}
