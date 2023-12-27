/*
 * Copyright © 2000 Keith Packard
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

#include "xftint.h"

static XftSymbolic XftXlfdWeights[] = {
    {	"light",    FC_WEIGHT_LIGHT	},
    {	"medium",   FC_WEIGHT_MEDIUM	},
    {	"regular",  FC_WEIGHT_MEDIUM	},
    {	"demibold", FC_WEIGHT_DEMIBOLD },
    {	"bold",	    FC_WEIGHT_BOLD	},
    {	"black",    FC_WEIGHT_BLACK	},
};

#define NUM_XLFD_WEIGHTS    (sizeof XftXlfdWeights/sizeof XftXlfdWeights[0])

static XftSymbolic XftXlfdSlants[] = {
    {	"r",	    FC_SLANT_ROMAN	},
    {	"i",	    FC_SLANT_ITALIC	},
    {	"o",	    FC_SLANT_OBLIQUE	},
};

#define NUM_XLFD_SLANTS    (sizeof XftXlfdSlants/sizeof XftXlfdSlants[0])

/*
 * Cut out one XLFD field, placing it in 'save' and return
 * the start of 'save'
 */
static char *
XftSplitStr (const char *field, char *save)
{
    char    *s = save;
    char    c;

    while (*field)
    {
	if (*field == '-')
	    break;
	c = *field++;
	*save++ = c;
    }
    *save = 0;
    return s;
}

/*
 * convert one XLFD numeric field.  Return -1 if the field is '*'
 */

static const char *
XftGetInt(const char *ptr, int *val)
{
    if (*ptr == '*') {
	*val = -1;
	ptr++;
    } else
	for (*val = 0; *ptr >= '0' && *ptr <= '9';)
	    *val = *val * 10 + *ptr++ - '0';
    if (*ptr == '-')
	return ptr;
    return (char *) 0;
}

_X_EXPORT FcPattern *
XftXlfdParse (const char *xlfd_orig, FcBool ignore_scalable _X_UNUSED, FcBool complete _X_UNUSED)
{
    FcPattern	*pat;
    const char	*xlfd = xlfd_orig;
    const char	*foundry;
    const char	*family;
    const char	*weight_name;
    const char	*slant;
    char	*save;
    int		pixel;
    int		point;
    int		resx;
    int		resy;
    int		slant_value, weight_value;
    double	dpixel;

    if (*xlfd != '-')
	return NULL;
    if (!(xlfd = strchr (foundry = ++xlfd, '-'))) return NULL;
    if (!(xlfd = strchr (family = ++xlfd, '-'))) return NULL;
    if (!(xlfd = strchr (weight_name = ++xlfd, '-'))) return NULL;
    if (!(xlfd = strchr (slant = ++xlfd, '-'))) return NULL;
    if (!(xlfd = strchr (/* setwidth_name = */ ++xlfd, '-'))) return NULL;
    if (!(xlfd = strchr (/* add_style_name = */ ++xlfd, '-'))) return NULL;
    if (!(xlfd = XftGetInt (++xlfd, &pixel))) return NULL;
    if (!(xlfd = XftGetInt (++xlfd, &point))) return NULL;
    if (!(xlfd = XftGetInt (++xlfd, &resx))) return NULL;
    if (!(xlfd = XftGetInt (++xlfd, &resy))) return NULL;
    if (!(xlfd = strchr (/* spacing = */ ++xlfd, '-'))) return NULL;
    if (!(xlfd = strchr (/* average_width = */ ++xlfd, '-'))) return NULL;
    if (!(xlfd = strchr (/* registry = */ ++xlfd, '-'))) return NULL;
    /* make sure no fields follow this one */
    if ((/* xlfd = */ strchr (/* encoding = */ ++xlfd, '-'))) return NULL;

    if (!pixel)
	return NULL;

    pat = FcPatternCreate ();
    if (!pat)
	return NULL;

    save = (char *) malloc (strlen (foundry) + 1);

    if (!save) {
	FcPatternDestroy (pat);
	return NULL;
    }

    if (!FcPatternAddString (pat, XFT_XLFD, (const FcChar8 *) xlfd_orig)) goto bail;

    XftSplitStr (foundry, save);
    if (save[0] && strcmp (save, "*") != 0)
	if (!FcPatternAddString (pat, FC_FOUNDRY, (FcChar8 *) save)) goto bail;

    XftSplitStr (family, save);
    if (save[0] && strcmp (save, "*") != 0)
	if (!FcPatternAddString (pat, FC_FAMILY, (FcChar8 *) save)) goto bail;

    weight_value = _XftMatchSymbolic (XftXlfdWeights, NUM_XLFD_WEIGHTS,
				      XftSplitStr (weight_name, save),
				      FC_WEIGHT_MEDIUM);
    if (!FcPatternAddInteger (pat, FC_WEIGHT, weight_value))
	goto bail;

    slant_value = _XftMatchSymbolic (XftXlfdSlants, NUM_XLFD_SLANTS,
				     XftSplitStr (slant, save),
				     FC_SLANT_ROMAN);
    if (!FcPatternAddInteger (pat, FC_SLANT, slant_value))
	goto bail;

    dpixel = (double) pixel;

    if (point > 0)
    {
	if (!FcPatternAddDouble (pat, FC_SIZE, ((double) point) / 10.0)) goto bail;
	if (pixel <= 0 && resy > 0)
	{
	    dpixel = (double) point * (double) resy / 720.0;
	}
    }

    if (dpixel > 0)
	if (!FcPatternAddDouble (pat, FC_PIXEL_SIZE, dpixel)) goto bail;

    free (save);
    return pat;

bail:
    free (save);
    FcPatternDestroy (pat);
    return NULL;
}
