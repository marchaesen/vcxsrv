/*
 * fontconfig/src/fcname.c
 *
 * Copyright © 2000 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the author(s) not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  The authors make no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * THE AUTHOR(S) DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "fcint.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Please do not change this list, it is used to initialize the object
 * list in this order to match the FC_foo_OBJECT constants. Those
 * constants are written into cache files.
 */

static const FcObjectType _FcBaseObjectTypes[] = {
    { FC_FAMILY,	FcTypeString, },    /* 1 */
    { FC_FAMILYLANG,	FcTypeString, },
    { FC_STYLE,		FcTypeString, },
    { FC_STYLELANG,	FcTypeString, },
    { FC_FULLNAME,	FcTypeString, },
    { FC_FULLNAMELANG,	FcTypeString, },
    { FC_SLANT,		FcTypeInteger, },
    { FC_WEIGHT,	FcTypeInteger, },
    { FC_WIDTH,		FcTypeInteger, },
    { FC_SIZE,		FcTypeDouble, },
    { FC_ASPECT,	FcTypeDouble, },
    { FC_PIXEL_SIZE,	FcTypeDouble, },
    { FC_SPACING,	FcTypeInteger, },
    { FC_FOUNDRY,	FcTypeString, },
    { FC_ANTIALIAS,	FcTypeBool, },
    { FC_HINT_STYLE,    FcTypeInteger, },
    { FC_HINTING,	FcTypeBool, },
    { FC_VERTICAL_LAYOUT,   FcTypeBool, },
    { FC_AUTOHINT,	FcTypeBool, },
    { FC_GLOBAL_ADVANCE,    FcTypeBool, },	/* deprecated */
    { FC_FILE,		FcTypeString, },
    { FC_INDEX,		FcTypeInteger, },
    { FC_RASTERIZER,	FcTypeString, },
    { FC_OUTLINE,	FcTypeBool, },
    { FC_SCALABLE,	FcTypeBool, },
    { FC_DPI,		FcTypeDouble },
    { FC_RGBA,		FcTypeInteger, },
    { FC_SCALE,		FcTypeDouble, },
    { FC_MINSPACE,	FcTypeBool, },
    { FC_CHAR_WIDTH,	FcTypeInteger },
    { FC_CHAR_HEIGHT,	FcTypeInteger },
    { FC_MATRIX,	FcTypeMatrix },
    { FC_CHARSET,	FcTypeCharSet },
    { FC_LANG,		FcTypeLangSet },
    { FC_FONTVERSION,	FcTypeInteger },
    { FC_CAPABILITY,	FcTypeString },
    { FC_FONTFORMAT,	FcTypeString },
    { FC_EMBOLDEN,	FcTypeBool },
    { FC_EMBEDDED_BITMAP,   FcTypeBool },
    { FC_DECORATIVE,	FcTypeBool },
    { FC_LCD_FILTER,	FcTypeInteger }, /* 41 */
    { FC_NAMELANG,	FcTypeString }, /* 42 */
};

#define NUM_OBJECT_TYPES    (sizeof _FcBaseObjectTypes / sizeof _FcBaseObjectTypes[0])

typedef struct _FcObjectTypeList    FcObjectTypeList;

struct _FcObjectTypeList {
    const FcObjectTypeList  *next;
    const FcObjectType	    *types;
    int			    ntypes;
};

static const FcObjectTypeList _FcBaseObjectTypesList = {
    0,
    _FcBaseObjectTypes,
    NUM_OBJECT_TYPES,
};

static const FcObjectTypeList	*_FcObjectTypes = &_FcBaseObjectTypesList;

#define OBJECT_HASH_SIZE    31

typedef struct _FcObjectBucket {
    struct _FcObjectBucket  *next;
    FcChar32		    hash;
    FcObject		    id;
} FcObjectBucket;

static FcObjectBucket	*FcObjectBuckets[OBJECT_HASH_SIZE];

static FcObjectType	*FcObjects = (FcObjectType *) _FcBaseObjectTypes;
static int		FcObjectsNumber = NUM_OBJECT_TYPES;
static int		FcObjectsSize = 0;
static FcBool		FcObjectsInited;

static FcObjectType *
FcObjectInsert (const char *name, FcType type)
{
    FcObjectType    *o;
    if (FcObjectsNumber >= FcObjectsSize)
    {
	int		newsize = FcObjectsNumber * 2;
	FcObjectType	*newobjects;
	
	if (FcObjectsSize)
	    newobjects = realloc (FcObjects, newsize * sizeof (FcObjectType));
	else
	{
	    newobjects = malloc (newsize * sizeof (FcObjectType));
	    if (newobjects)
		memcpy (newobjects, FcObjects,
			FcObjectsNumber * sizeof (FcObjectType));
	}
	if (!newobjects)
	    return NULL;
	FcObjects = newobjects;
	FcObjectsSize = newsize;
    }
    o = &FcObjects[FcObjectsNumber];
    o->object = name;
    o->type = type;
    ++FcObjectsNumber;
    return o;
}

static FcObject
FcObjectId (FcObjectType *o)
{
    return o - FcObjects + 1;
}

static FcObjectType *
FcObjectFindByName (const char *object, FcBool insert)
{
    FcChar32	    hash = FcStringHash ((const FcChar8 *) object);
    FcObjectBucket  **p;
    FcObjectBucket  *b;
    FcObjectType    *o;

    if (!FcObjectsInited)
	FcObjectInit ();
    for (p = &FcObjectBuckets[hash%OBJECT_HASH_SIZE]; (b = *p); p = &(b->next))
    {
	o = FcObjects + b->id - 1;
        if (b->hash == hash && !strcmp (object, (o->object)))
            return o;
    }
    if (!insert)
	return NULL;
    /*
     * Hook it into the hash chain
     */
    b = malloc (sizeof(FcObjectBucket));
    if (!b)
	return NULL;
    object = (const char *) FcStrCopy ((FcChar8 *) object);
    if (!object) {
	free (b);
	return NULL;
    }
    o = FcObjectInsert (object, -1);
    b->next = NULL;
    b->hash = hash;
    b->id = FcObjectId (o);
    *p = b;
    return o;
}

static FcObjectType *
FcObjectFindById (FcObject object)
{
    if (1 <= object && object <= FcObjectsNumber)
	return FcObjects + object - 1;
    return NULL;
}

static FcBool
FcObjectHashInsert (const FcObjectType *object, FcBool copy)
{
    FcChar32	    hash = FcStringHash ((const FcChar8 *) object->object);
    FcObjectBucket  **p;
    FcObjectBucket  *b;
    FcObjectType    *o;

    if (!FcObjectsInited)
	FcObjectInit ();
    for (p = &FcObjectBuckets[hash%OBJECT_HASH_SIZE]; (b = *p); p = &(b->next))
    {
	o = FcObjects + b->id - 1;
        if (b->hash == hash && !strcmp (object->object, o->object))
            return FcFalse;
    }
    /*
     * Hook it into the hash chain
     */
    b = malloc (sizeof(FcObjectBucket));
    if (!b)
	return FcFalse;
    if (copy)
    {
	o = FcObjectInsert (object->object, object->type);
	if (!o)
	{
	    free (b);
	    return FcFalse;
	}
    }
    else
	o = (FcObjectType *) object;
    b->next = NULL;
    b->hash = hash;
    b->id = FcObjectId (o);
    *p = b;
    return FcTrue;
}

static void
FcObjectHashRemove (const FcObjectType *object, FcBool cleanobj)
{
    FcChar32	    hash = FcStringHash ((const FcChar8 *) object->object);
    FcObjectBucket  **p;
    FcObjectBucket  *b;
    FcObjectType    *o;

    if (!FcObjectsInited)
	FcObjectInit ();
    for (p = &FcObjectBuckets[hash%OBJECT_HASH_SIZE]; (b = *p); p = &(b->next))
    {
	o = FcObjects + b->id - 1;
        if (b->hash == hash && !strcmp (object->object, o->object))
	{
	    *p = b->next;
	    free (b);
	    if (cleanobj)
	    {
		/* Clean up object array */
		o->object = NULL;
		o->type = -1;
		while (FcObjects[FcObjectsNumber-1].object == NULL)
		    --FcObjectsNumber;
	    }
            break;
	}
    }
}

FcBool
FcNameRegisterObjectTypes (const FcObjectType *types, int ntypes)
{
    int	i;

    for (i = 0; i < ntypes; i++)
	if (!FcObjectHashInsert (&types[i], FcTrue))
	    return FcFalse;
    return FcTrue;
}

FcBool
FcNameUnregisterObjectTypes (const FcObjectType *types, int ntypes)
{
    int	i;

    for (i = 0; i < ntypes; i++)
	FcObjectHashRemove (&types[i], FcTrue);
    return FcTrue;
}

const FcObjectType *
FcNameGetObjectType (const char *object)
{
    return FcObjectFindByName (object, FcFalse);
}

FcBool
FcObjectValidType (FcObject object, FcType type)
{
    FcObjectType    *t = FcObjectFindById (object);

    if (t) {
	switch (t->type) {
	case FcTypeDouble:
	case FcTypeInteger:
	    if (type == FcTypeDouble || type == FcTypeInteger)
		return FcTrue;
	    break;
	case FcTypeLangSet:
	    if (type == FcTypeLangSet || type == FcTypeString)
		return FcTrue;
	    break;
	default:
	    if (t->type == -1 || type == t->type)
		return FcTrue;
	    break;
	}
	return FcFalse;
    }
    return FcTrue;
}

FcObject
FcObjectFromName (const char * name)
{
    FcObjectType    *o = FcObjectFindByName (name, FcTrue);

    if (o)
	return FcObjectId (o);
    return 0;
}

FcObjectSet *
FcObjectGetSet (void)
{
    int		i;
    FcObjectSet	*os = NULL;


    os = FcObjectSetCreate ();
    for (i = 0; i < FcObjectsNumber; i++)
	FcObjectSetAdd (os, FcObjects[i].object);

    return os;
}

FcBool
FcObjectInit (void)
{
    int	i;

    if (FcObjectsInited)
	return FcTrue;

    FcObjectsInited = FcTrue;
    for (i = 0; i < NUM_OBJECT_TYPES; i++)
	if (!FcObjectHashInsert (&_FcBaseObjectTypes[i], FcFalse))
	    return FcFalse;
    return FcTrue;
}

void
FcObjectFini (void)
{
    int		    i;
    FcObjectBucket  *b, *next;

    for (i = 0; i < OBJECT_HASH_SIZE; i++)
    {
	for (b = FcObjectBuckets[i]; b; b = next)
	{
	    next = b->next;
	    free (b);
	}
	FcObjectBuckets[i] = 0;
    }
    for (i = 0; i < FcObjectsNumber; i++)
	if (FcObjects[i].type == -1)
	    free ((void*) FcObjects[i].object);
    if (FcObjects != _FcBaseObjectTypes)
	free (FcObjects);
    FcObjects = (FcObjectType *) _FcBaseObjectTypes;
    FcObjectsNumber = NUM_OBJECT_TYPES;
    FcObjectsSize = 0;
    FcObjectsInited = FcFalse;
}

const char *
FcObjectName (FcObject object)
{
    FcObjectType    *o = FcObjectFindById (object);

    if (o)
	return o->object;
    return NULL;
}

static const FcConstant _FcBaseConstants[] = {
    { (FcChar8 *) "thin",	    "weight",   FC_WEIGHT_THIN, },
    { (FcChar8 *) "extralight",	    "weight",   FC_WEIGHT_EXTRALIGHT, },
    { (FcChar8 *) "ultralight",	    "weight",   FC_WEIGHT_EXTRALIGHT, },
    { (FcChar8 *) "light",	    "weight",   FC_WEIGHT_LIGHT, },
    { (FcChar8 *) "book",	    "weight",	FC_WEIGHT_BOOK, },
    { (FcChar8 *) "regular",	    "weight",   FC_WEIGHT_REGULAR, },
    { (FcChar8 *) "medium",	    "weight",   FC_WEIGHT_MEDIUM, },
    { (FcChar8 *) "demibold",	    "weight",   FC_WEIGHT_DEMIBOLD, },
    { (FcChar8 *) "semibold",	    "weight",   FC_WEIGHT_DEMIBOLD, },
    { (FcChar8 *) "bold",	    "weight",   FC_WEIGHT_BOLD, },
    { (FcChar8 *) "extrabold",	    "weight",   FC_WEIGHT_EXTRABOLD, },
    { (FcChar8 *) "ultrabold",	    "weight",   FC_WEIGHT_EXTRABOLD, },
    { (FcChar8 *) "black",	    "weight",   FC_WEIGHT_BLACK, },
    { (FcChar8 *) "heavy",	    "weight",	FC_WEIGHT_HEAVY, },

    { (FcChar8 *) "roman",	    "slant",    FC_SLANT_ROMAN, },
    { (FcChar8 *) "italic",	    "slant",    FC_SLANT_ITALIC, },
    { (FcChar8 *) "oblique",	    "slant",    FC_SLANT_OBLIQUE, },

    { (FcChar8 *) "ultracondensed", "width",	FC_WIDTH_ULTRACONDENSED },
    { (FcChar8 *) "extracondensed", "width",	FC_WIDTH_EXTRACONDENSED },
    { (FcChar8 *) "condensed",	    "width",	FC_WIDTH_CONDENSED },
    { (FcChar8 *) "semicondensed", "width",	FC_WIDTH_SEMICONDENSED },
    { (FcChar8 *) "normal",	    "width",	FC_WIDTH_NORMAL },
    { (FcChar8 *) "semiexpanded",   "width",	FC_WIDTH_SEMIEXPANDED },
    { (FcChar8 *) "expanded",	    "width",	FC_WIDTH_EXPANDED },
    { (FcChar8 *) "extraexpanded",  "width",	FC_WIDTH_EXTRAEXPANDED },
    { (FcChar8 *) "ultraexpanded",  "width",	FC_WIDTH_ULTRAEXPANDED },

    { (FcChar8 *) "proportional",   "spacing",  FC_PROPORTIONAL, },
    { (FcChar8 *) "dual",	    "spacing",  FC_DUAL, },
    { (FcChar8 *) "mono",	    "spacing",  FC_MONO, },
    { (FcChar8 *) "charcell",	    "spacing",  FC_CHARCELL, },

    { (FcChar8 *) "unknown",	    "rgba",	    FC_RGBA_UNKNOWN },
    { (FcChar8 *) "rgb",	    "rgba",	    FC_RGBA_RGB, },
    { (FcChar8 *) "bgr",	    "rgba",	    FC_RGBA_BGR, },
    { (FcChar8 *) "vrgb",	    "rgba",	    FC_RGBA_VRGB },
    { (FcChar8 *) "vbgr",	    "rgba",	    FC_RGBA_VBGR },
    { (FcChar8 *) "none",	    "rgba",	    FC_RGBA_NONE },

    { (FcChar8 *) "hintnone",	    "hintstyle",   FC_HINT_NONE },
    { (FcChar8 *) "hintslight",	    "hintstyle",   FC_HINT_SLIGHT },
    { (FcChar8 *) "hintmedium",	    "hintstyle",   FC_HINT_MEDIUM },
    { (FcChar8 *) "hintfull",	    "hintstyle",   FC_HINT_FULL },

    { (FcChar8 *) "antialias",	    "antialias",    FcTrue },
    { (FcChar8 *) "hinting",	    "hinting",	    FcTrue },
    { (FcChar8 *) "verticallayout", "verticallayout",	FcTrue },
    { (FcChar8 *) "autohint",	    "autohint",	    FcTrue },
    { (FcChar8 *) "globaladvance",  "globaladvance",	FcTrue }, /* deprecated */
    { (FcChar8 *) "outline",	    "outline",	    FcTrue },
    { (FcChar8 *) "scalable",	    "scalable",	    FcTrue },
    { (FcChar8 *) "minspace",	    "minspace",	    FcTrue },
    { (FcChar8 *) "embolden",	    "embolden",	    FcTrue },
    { (FcChar8 *) "embeddedbitmap", "embeddedbitmap",	FcTrue },
    { (FcChar8 *) "decorative",	    "decorative",   FcTrue },
    { (FcChar8 *) "lcdnone",	    "lcdfilter",    FC_LCD_NONE },
    { (FcChar8 *) "lcddefault",	    "lcdfilter",    FC_LCD_DEFAULT },
    { (FcChar8 *) "lcdlight",	    "lcdfilter",    FC_LCD_LIGHT },
    { (FcChar8 *) "lcdlegacy",	    "lcdfilter",    FC_LCD_LEGACY },
};

#define NUM_FC_CONSTANTS   (sizeof _FcBaseConstants/sizeof _FcBaseConstants[0])

typedef struct _FcConstantList FcConstantList;

struct _FcConstantList {
    const FcConstantList    *next;
    const FcConstant	    *consts;
    int			    nconsts;
};

static const FcConstantList _FcBaseConstantList = {
    0,
    _FcBaseConstants,
    NUM_FC_CONSTANTS
};

static const FcConstantList	*_FcConstants = &_FcBaseConstantList;

FcBool
FcNameRegisterConstants (const FcConstant *consts, int nconsts)
{
    FcConstantList	*l;

    l = (FcConstantList *) malloc (sizeof (FcConstantList));
    if (!l)
	return FcFalse;
    FcMemAlloc (FC_MEM_CONSTANT, sizeof (FcConstantList));
    l->consts = consts;
    l->nconsts = nconsts;
    l->next = _FcConstants;
    _FcConstants = l;
    return FcTrue;
}

FcBool
FcNameUnregisterConstants (const FcConstant *consts, int nconsts)
{
    const FcConstantList	*l, **prev;

    for (prev = &_FcConstants;
	 (l = *prev);
	 prev = (const FcConstantList **) &(l->next))
    {
	if (l->consts == consts && l->nconsts == nconsts)
	{
	    *prev = l->next;
	    FcMemFree (FC_MEM_CONSTANT, sizeof (FcConstantList));
	    free ((void *) l);
	    return FcTrue;
	}
    }
    return FcFalse;
}

const FcConstant *
FcNameGetConstant (const FcChar8 *string)
{
    const FcConstantList    *l;
    int			    i;

    for (l = _FcConstants; l; l = l->next)
    {
	for (i = 0; i < l->nconsts; i++)
	    if (!FcStrCmpIgnoreCase (string, l->consts[i].name))
		return &l->consts[i];
    }
    return 0;
}

FcBool
FcNameConstant (const FcChar8 *string, int *result)
{
    const FcConstant	*c;

    if ((c = FcNameGetConstant(string)))
    {
	*result = c->value;
	return FcTrue;
    }
    return FcFalse;
}

FcBool
FcNameBool (const FcChar8 *v, FcBool *result)
{
    char    c0, c1;

    c0 = *v;
    c0 = FcToLower (c0);
    if (c0 == 't' || c0 == 'y' || c0 == '1')
    {
	*result = FcTrue;
	return FcTrue;
    }
    if (c0 == 'f' || c0 == 'n' || c0 == '0')
    {
	*result = FcFalse;
	return FcTrue;
    }
    if (c0 == 'o')
    {
	c1 = v[1];
	c1 = FcToLower (c1);
	if (c1 == 'n')
	{
	    *result = FcTrue;
	    return FcTrue;
	}
	if (c1 == 'f')
	{
	    *result = FcFalse;
	    return FcTrue;
	}
    }
    return FcFalse;
}

static FcValue
FcNameConvert (FcType type, FcChar8 *string)
{
    FcValue	v;
    FcMatrix	m;

    v.type = type;
    switch (v.type) {
    case FcTypeInteger:
	if (!FcNameConstant (string, &v.u.i))
	    v.u.i = atoi ((char *) string);
	break;
    case FcTypeString:
	v.u.s = FcSharedStr (string);
	if (!v.u.s)
	    v.type = FcTypeVoid;
	break;
    case FcTypeBool:
	if (!FcNameBool (string, &v.u.b))
	    v.u.b = FcFalse;
	break;
    case FcTypeDouble:
	v.u.d = strtod ((char *) string, 0);
	break;
    case FcTypeMatrix:
	sscanf ((char *) string, "%lg %lg %lg %lg", &m.xx, &m.xy, &m.yx, &m.yy);
	v.u.m = FcMatrixCopy (&m);
	break;
    case FcTypeCharSet:
	v.u.c = FcNameParseCharSet (string);
	if (!v.u.c)
	    v.type = FcTypeVoid;
	break;
    case FcTypeLangSet:
	v.u.l = FcNameParseLangSet (string);
	if (!v.u.l)
	    v.type = FcTypeVoid;
	break;
    default:
	break;
    }
    return v;
}

static const FcChar8 *
FcNameFindNext (const FcChar8 *cur, const char *delim, FcChar8 *save, FcChar8 *last)
{
    FcChar8    c;

    while ((c = *cur))
    {
	if (c == '\\')
	{
	    ++cur;
	    if (!(c = *cur))
		break;
	}
	else if (strchr (delim, c))
	    break;
	++cur;
	*save++ = c;
    }
    *save = 0;
    *last = *cur;
    if (*cur)
	cur++;
    return cur;
}

FcPattern *
FcNameParse (const FcChar8 *name)
{
    FcChar8		*save;
    FcPattern		*pat;
    double		d;
    FcChar8		*e;
    FcChar8		delim;
    FcValue		v;
    const FcObjectType	*t;
    const FcConstant	*c;

    /* freed below */
    save = malloc (strlen ((char *) name) + 1);
    if (!save)
	goto bail0;
    pat = FcPatternCreate ();
    if (!pat)
	goto bail1;

    for (;;)
    {
	name = FcNameFindNext (name, "-,:", save, &delim);
	if (save[0])
	{
	    if (!FcPatternAddString (pat, FC_FAMILY, save))
		goto bail2;
	}
	if (delim != ',')
	    break;
    }
    if (delim == '-')
    {
	for (;;)
	{
	    name = FcNameFindNext (name, "-,:", save, &delim);
	    d = strtod ((char *) save, (char **) &e);
	    if (e != save)
	    {
		if (!FcPatternAddDouble (pat, FC_SIZE, d))
		    goto bail2;
	    }
	    if (delim != ',')
		break;
	}
    }
    while (delim == ':')
    {
	name = FcNameFindNext (name, "=_:", save, &delim);
	if (save[0])
	{
	    if (delim == '=' || delim == '_')
	    {
		t = FcNameGetObjectType ((char *) save);
		for (;;)
		{
		    name = FcNameFindNext (name, ":,", save, &delim);
		    if (t)
		    {
			v = FcNameConvert (t->type, save);
			if (!FcPatternAdd (pat, t->object, v, FcTrue))
			{
			    FcValueDestroy (v);
			    goto bail2;
			}
			FcValueDestroy (v);
		    }
		    if (delim != ',')
			break;
		}
	    }
	    else
	    {
		if ((c = FcNameGetConstant (save)))
		{
		    t = FcNameGetObjectType ((char *) c->object);
		    switch (t->type) {
		    case FcTypeInteger:
		    case FcTypeDouble:
			if (!FcPatternAddInteger (pat, c->object, c->value))
			    goto bail2;
			break;
		    case FcTypeBool:
			if (!FcPatternAddBool (pat, c->object, c->value))
			    goto bail2;
			break;
		    default:
			break;
		    }
		}
	    }
	}
    }

    free (save);
    return pat;

bail2:
    FcPatternDestroy (pat);
bail1:
    free (save);
bail0:
    return 0;
}
static FcBool
FcNameUnparseString (FcStrBuf	    *buf,
		     const FcChar8  *string,
		     const FcChar8  *escape)
{
    FcChar8 c;
    while ((c = *string++))
    {
	if (escape && strchr ((char *) escape, (char) c))
	{
	    if (!FcStrBufChar (buf, escape[0]))
		return FcFalse;
	}
	if (!FcStrBufChar (buf, c))
	    return FcFalse;
    }
    return FcTrue;
}

FcBool
FcNameUnparseValue (FcStrBuf	*buf,
		    FcValue	*v0,
		    FcChar8	*escape)
{
    FcChar8	temp[1024];
    FcValue v = FcValueCanonicalize(v0);

    switch (v.type) {
    case FcTypeVoid:
	return FcTrue;
    case FcTypeInteger:
	sprintf ((char *) temp, "%d", v.u.i);
	return FcNameUnparseString (buf, temp, 0);
    case FcTypeDouble:
	sprintf ((char *) temp, "%g", v.u.d);
	return FcNameUnparseString (buf, temp, 0);
    case FcTypeString:
	return FcNameUnparseString (buf, v.u.s, escape);
    case FcTypeBool:
	return FcNameUnparseString (buf, v.u.b ? (FcChar8 *) "True" : (FcChar8 *) "False", 0);
    case FcTypeMatrix:
	sprintf ((char *) temp, "%g %g %g %g",
		 v.u.m->xx, v.u.m->xy, v.u.m->yx, v.u.m->yy);
	return FcNameUnparseString (buf, temp, 0);
    case FcTypeCharSet:
	return FcNameUnparseCharSet (buf, v.u.c);
    case FcTypeLangSet:
	return FcNameUnparseLangSet (buf, v.u.l);
    case FcTypeFTFace:
	return FcTrue;
    }
    return FcFalse;
}

FcBool
FcNameUnparseValueList (FcStrBuf	*buf,
			FcValueListPtr	v,
			FcChar8		*escape)
{
    while (v)
    {
	if (!FcNameUnparseValue (buf, &v->value, escape))
	    return FcFalse;
	if ((v = FcValueListNext(v)) != NULL)
	    if (!FcNameUnparseString (buf, (FcChar8 *) ",", 0))
		return FcFalse;
    }
    return FcTrue;
}

#define FC_ESCAPE_FIXED    "\\-:,"
#define FC_ESCAPE_VARIABLE "\\=_:,"

FcChar8 *
FcNameUnparse (FcPattern *pat)
{
    return FcNameUnparseEscaped (pat, FcTrue);
}

FcChar8 *
FcNameUnparseEscaped (FcPattern *pat, FcBool escape)
{
    FcStrBuf		    buf;
    FcChar8		    buf_static[8192];
    int			    i;
    FcPatternElt	    *e;
    const FcObjectTypeList  *l;
    const FcObjectType	    *o;

    FcStrBufInit (&buf, buf_static, sizeof (buf_static));
    e = FcPatternObjectFindElt (pat, FC_FAMILY_OBJECT);
    if (e)
    {
        if (!FcNameUnparseValueList (&buf, FcPatternEltValues(e), escape ? (FcChar8 *) FC_ESCAPE_FIXED : 0))
	    goto bail0;
    }
    e = FcPatternObjectFindElt (pat, FC_SIZE_OBJECT);
    if (e)
    {
	if (!FcNameUnparseString (&buf, (FcChar8 *) "-", 0))
	    goto bail0;
	if (!FcNameUnparseValueList (&buf, FcPatternEltValues(e), escape ? (FcChar8 *) FC_ESCAPE_FIXED : 0))
	    goto bail0;
    }
    for (l = _FcObjectTypes; l; l = l->next)
    {
	for (i = 0; i < l->ntypes; i++)
	{
	    o = &l->types[i];
	    if (!strcmp (o->object, FC_FAMILY) ||
		!strcmp (o->object, FC_SIZE))
		continue;
	
	    e = FcPatternObjectFindElt (pat, FcObjectFromName (o->object));
	    if (e)
	    {
		if (!FcNameUnparseString (&buf, (FcChar8 *) ":", 0))
		    goto bail0;
		if (!FcNameUnparseString (&buf, (FcChar8 *) o->object, escape ? (FcChar8 *) FC_ESCAPE_VARIABLE : 0))
		    goto bail0;
		if (!FcNameUnparseString (&buf, (FcChar8 *) "=", 0))
		    goto bail0;
		if (!FcNameUnparseValueList (&buf, FcPatternEltValues(e), escape ?
					     (FcChar8 *) FC_ESCAPE_VARIABLE : 0))
		    goto bail0;
	    }
	}
    }
    return FcStrBufDone (&buf);
bail0:
    FcStrBufDestroy (&buf);
    return 0;
}
#define __fcname__
#include "fcaliastail.h"
#undef __fcname__
