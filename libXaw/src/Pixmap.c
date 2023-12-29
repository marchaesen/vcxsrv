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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <X11/IntrinsicP.h>
#include <X11/Xmu/CharSet.h>
#include <X11/Xfuncs.h>
#include <X11/extensions/shape.h>
#ifndef OLDXAW
#include <X11/xpm.h>
#endif
#include "Private.h"


#ifndef OLDXAW

/*
 * Types
 */
typedef struct _XawCache {
  long value;
  XtPointer *elems;
  unsigned int num_elems;
} XawCache;

typedef struct _XawPixmapLoaderInfo {
  XawPixmapLoader loader;
  String type;
  String ext;
} XawPixmapLoaderInfo;

/*
 * Private Methods
 */
static Bool BitmapLoader(XawParams*, Screen*, Colormap, int,
			    Pixmap*, Pixmap*, Dimension*, Dimension*);
static Bool GradientLoader(XawParams*, Screen*, Colormap, int,
			      Pixmap*, Pixmap*, Dimension*, Dimension*);
static Bool XPixmapLoader(XawParams*, Screen*, Colormap, int,
			  Pixmap*, Pixmap*, Dimension*, Dimension*);
static XawPixmap *_XawFindPixmap(String, Screen*, Colormap, int);
static void _XawCachePixmap(XawPixmap*, Screen*, Colormap, int);
static int _XawFindPixmapLoaderIndex(String, String);
static int qcmp_long(register _Xconst void*, register _Xconst void *);
static int bcmp_long(register _Xconst void*, register _Xconst void *);
static int qcmp_string(register _Xconst void*, register _Xconst void *);
static int bcmp_string(register _Xconst void*, register _Xconst void *);
static void GetResourcePixmapPath(Display*);

/*
 * Initialization
 */
static XawCache xaw_pixmaps;
static XawCache x_pixmaps;   /* for fast reverse search */
static XawPixmapLoaderInfo **loader_info;
static Cardinal num_loader_info;

/*
 * Implementation
 */
Bool
XawPixmapsInitialize(void)
{
  static Boolean first_time = True;

  if (!first_time)
    return (False);

  (void)XawAddPixmapLoader(NULL, NULL, BitmapLoader);
  (void)XawAddPixmapLoader("bitmap", NULL, BitmapLoader);
  (void)XawAddPixmapLoader("gradient", NULL, GradientLoader);
  (void)XawAddPixmapLoader("xpm", "xpm", XPixmapLoader);

  return (True);
}

XawParams *
XawParseParamsString(String name)
{
  XawParams *xaw_params;
  char *tok, *str, *type = NULL, *ext = NULL, *params = NULL;

  if (!name)
    return (NULL);

  xaw_params = (XawParams *)XtMalloc(sizeof(XawParams));

  str = XtNewString(name);

  /* Find type */
  tok = str;
  while (tok = strchr(tok, ':'), tok)
    {
      if (tok == str || tok[-1] != '\\')
	break;
      memmove(&tok[-1], tok, strlen(tok) + 1);
    }
  if (tok)
    {
      *tok = '\0';
      if (strchr(str, '?'))
	{
	  *tok = ':';
	}
      else
	{
	  ++tok;
	  type = XtNewString(str);
	  memmove(str, tok, strlen(tok) + 1);
	}
    }

  /* Find params */
  tok = str;
  while (tok = strchr(tok, '?'), tok)
    {
      if (tok == str || tok[-1] != '\\')
	params = tok;
      if (tok != str && tok[-1] == '\\')
	memmove(&tok[-1], tok, strlen(tok) + 1);
      else
	break;
    }
  if (params)
    {
      *params = '\0';
      ++params;
    }

  /* Find ext */
  tok = str;
  while (tok = strchr(tok, '.'), tok)
    {
      if (tok == str || tok[-1] != '\\')
	ext = tok;
      if (tok != str && tok[-1] == '\\')
	memmove(&tok[-1], tok, strlen(tok) + 1);
      else
	break;
    }
  if (ext)
    {
      ++ext;
      if (strchr(ext, '/'))
	ext = NULL;
    }

  xaw_params->name = XtNewString(str);
  xaw_params->type = type;
  xaw_params->ext = ext ? XtNewString(ext) : ext;
  xaw_params->args = NULL;
  xaw_params->num_args = 0;

  /* Parse params */
  if (params)
    {
      char *arg, *val;
      XawArgVal *xaw_arg;

      for (tok = strtok(params, "&"); tok; tok = strtok(NULL, "&"))
	{
	  val = strchr(tok, '=');
	  if (val)
	    {
	      *val = '\0';
	      ++val;
	      if (*val != '\0')
		val = XtNewString(val);
	      else
		val = NULL;
	    }
	  arg = XtNewString(tok);
	  xaw_arg = (XawArgVal *)XtMalloc(sizeof(XawArgVal));
	  xaw_arg->name = arg;
	  xaw_arg->value = val;
	  if (!xaw_params->num_args)
	    {
	      xaw_params->num_args = 1;
	      xaw_params->args = (XawArgVal **)
		XtMalloc(sizeof(XawArgVal*));
	    }
	  else
	    {
	      ++xaw_params->num_args;
	      xaw_params->args = (XawArgVal **)
		XtRealloc((char *)xaw_params->args,
			  (Cardinal)(sizeof(XawArgVal*) * (size_t)xaw_params->num_args));
	    }
	  xaw_params->args[xaw_params->num_args - 1] = xaw_arg;
	}
    }

  if (xaw_params->num_args > 1)
    qsort(xaw_params->args, xaw_params->num_args, sizeof(XtPointer),
	  qcmp_string);

  XtFree(str);

  return (xaw_params);
}

void
XawFreeParamsStruct(XawParams *params)
{
  unsigned int i;

  if (!params)
    return;

  for (i = 0; i < params->num_args; i++)
    {
      XtFree((char *)params->args[i]->name);
      if (params->args[i]->value)
	XtFree((char *)params->args[i]->value);
      XtFree((char *)params->args[i]);
    }

  if (params->args)
    XtFree((char *)params->args);
  XtFree((char *)params);
}

XawArgVal *
XawFindArgVal(XawParams *params, String name)
{
  XawArgVal **arg_val;

  if (!params->args)
    return (NULL);

  arg_val = (XawArgVal **)bsearch((void *)name, params->args,
				  params->num_args, sizeof(XtPointer*),
				  bcmp_string);
  if (!arg_val)
    return (NULL);

  return (*arg_val);
}

XawPixmap *
XawLoadPixmap(String name, Screen *screen, Colormap colormap, int depth)
{
  int idx;
  Bool success;
  XawPixmap *xaw_pixmap;
  Pixmap pixmap, mask;
  Dimension width, height;
  XawParams *xaw_params;

  if (!name)
    return (NULL);

  xaw_pixmap = _XawFindPixmap(name, screen, colormap, depth);

  if (xaw_pixmap)
    return (xaw_pixmap);

  if ((xaw_params = XawParseParamsString(name)) == NULL)
    return (NULL);

  idx = _XawFindPixmapLoaderIndex(xaw_params->type, xaw_params->ext);
  if (idx < 0)
    return (NULL);

#ifdef DIAGNOSTIC
  fprintf(stderr, "(*) Loading pixmap \"%s\": ", name);
#endif

  success = loader_info[idx]->loader(xaw_params, screen, colormap, depth,
				       &pixmap, &mask, &width, &height);
  if (success)
    {
      xaw_pixmap = (XawPixmap *)XtMalloc(sizeof(XawPixmap));
      xaw_pixmap->name = XtNewString(name);
      xaw_pixmap->pixmap = pixmap;
      xaw_pixmap->mask = mask;
      xaw_pixmap->width = width;
      xaw_pixmap->height = height;
      _XawCachePixmap(xaw_pixmap, screen, colormap, depth);
    }

  XawFreeParamsStruct(xaw_params);

#ifdef DIAGNOSTIC
  fprintf(stderr, "%s", success ? "success\n" : "failed\n");
#endif

  return (success ? xaw_pixmap : NULL);
}

Bool
XawAddPixmapLoader(String type, String ext, XawPixmapLoader loader)
{
  XawPixmapLoaderInfo *info;
  int i;

  if (!loader)
    return (False);

  i = _XawFindPixmapLoaderIndex(type, ext);

  if (i >= 0)
    {
      loader_info[i]->loader = loader;
      if (loader_info[i]->type)
	XtFree((char *)loader_info[i]->type);
      if (loader_info[i]->ext)
	XtFree((char *)loader_info[i]->ext);
      loader_info[i]->type = type ? XtNewString(type) : NULL;
      loader_info[i]->ext = ext ? XtNewString(ext) : NULL;
      return (True);
    }

  if ((info = (XawPixmapLoaderInfo *)XtMalloc(sizeof(XawPixmapLoaderInfo)))
      == NULL)
    return (False);

  info->loader = loader;
  info->type = type ? XtNewString(type) : NULL;
  info->ext = ext ? XtNewString(ext) : NULL;

  if (!loader_info)
    {
      num_loader_info = 1;
      loader_info = (XawPixmapLoaderInfo**)
	XtMalloc(sizeof(XawPixmapLoaderInfo*));
    }
  else
    {
      ++num_loader_info;
      loader_info = (XawPixmapLoaderInfo**)
	XtRealloc((char *)loader_info,
		  (Cardinal)(sizeof(XawPixmapLoaderInfo) * (size_t)num_loader_info));
    }
  loader_info[num_loader_info - 1] = info;

  return (True);
}

static int
_XawFindPixmapLoaderIndex(String type, String ext)
{
  Cardinal i;

  if (!loader_info)
    return (-1);

  for (i = 0; i < num_loader_info; i++)
    if ((type && loader_info[i]->type && strcmp(type, loader_info[i]->type) == 0)
	|| (ext && loader_info[i]->ext && strcmp(ext, loader_info[i]->ext) == 0))
      return ((int)i);

  if (!type)
    return (0);	/* try a bitmap */

  return (-1);
}

static int
qcmp_x_cache(register _Xconst void *left, register _Xconst void *right)
{
  return ((int)((*(XawPixmap **)left)->pixmap) -
	  (int)((*(XawPixmap **)right)->pixmap));
}

static int
bcmp_x_cache(register _Xconst void *pixmap, register _Xconst void *xaw)
{
  return (int)((long)pixmap - (long)((*(XawPixmap **)xaw)->pixmap));
}

static int
qcmp_long(register _Xconst void *left, register _Xconst void *right)
{
  return (int)((long)((*(XawCache **)left)->value) -
	       (long)((*(XawCache **)right)->value));
}

static int
qcmp_string(register _Xconst void *left, register _Xconst void *right)
{
  return (strcmp((String)((*(XawCache **)left)->value),
		 (String)((*(XawCache **)right)->value)));
}

static int
bcmp_long(register _Xconst void *value, register _Xconst void *cache)
{
  return (int)((long)value - (long)((*(XawCache **)cache)->value));
}

static int
bcmp_string(register _Xconst void *string,
	    register _Xconst void *cache)
{
  return (strcmp((String)string, (String)((*(XawCache **)cache)->value)));
}

#define FIND_ALL      0
#define FIND_SCREEN   1
#define FIND_COLORMAP 2
#define FIND_DEPTH    3
static XawCache *
_XawFindCache(XawCache *xaw,
	      Screen *screen, Colormap colormap, int depth, int flags)
{
  XawCache **cache;

  if (!xaw->num_elems)
    return (NULL);

  /* Screen */
  cache = (XawCache **)bsearch(screen, xaw->elems,
			       xaw->num_elems, sizeof(XtPointer),
			       bcmp_long);
  if (!cache || !(*cache)->num_elems)
    return (NULL);
  if (flags == FIND_SCREEN)
    return (*cache);

  /* Colormap */
  cache = (XawCache **)bsearch((void *)colormap, (*cache)->elems,
			       (*cache)->num_elems, sizeof(XtPointer),
			       bcmp_long);
  if (!cache || !(*cache)->num_elems)
    return (NULL);
  if (flags == FIND_COLORMAP)
    return (*cache);

  /* Depth */
  cache = (XawCache **)bsearch((void *)(long)depth, (*cache)->elems,
			       (*cache)->num_elems, sizeof(XtPointer),
			       bcmp_long);

  if (!cache || !(*cache)->num_elems)
    return (NULL);
  return (*cache);
}

static XawCache *
_XawGetCache(XawCache *xaw, Screen *screen, Colormap colormap, int depth)
{
  XawCache *s_cache, *c_cache, *d_cache, *cache, *pcache;

  cache = _XawFindCache(xaw, screen, colormap, depth, FIND_ALL);

  if (!cache)
    {
      s_cache = _XawFindCache(xaw,
			      screen, colormap, depth, FIND_SCREEN);
      if (!s_cache)
	{
	  pcache = (XawCache *)XtMalloc(sizeof(XawCache));
	  if (!xaw->num_elems)
	    {
	      xaw->num_elems = 1;
	      xaw->elems = (XtPointer*)XtMalloc(sizeof(XtPointer));
	    }
	  else
	    {
	      ++xaw->num_elems;
	      xaw->elems = (XtPointer*)
		XtRealloc((char *)xaw->elems,
			  (Cardinal)(sizeof(XtPointer) * (size_t)xaw->num_elems));
	    }
	  pcache->value = (long)screen;
	  pcache->elems = NULL;
	  pcache->num_elems = 0;
	  xaw->elems[xaw->num_elems - 1] = (XtPointer)pcache;
	  s_cache = (XawCache *)xaw->elems[xaw->num_elems - 1];
	  if (xaw->num_elems > 1)
	    qsort(xaw->elems, xaw->num_elems, sizeof(XtPointer), qcmp_long);
	}

      c_cache = _XawFindCache(xaw,
			      screen, colormap, depth, FIND_COLORMAP);
      if (!c_cache)
	{
	  pcache = (XawCache *)XtMalloc(sizeof(XawCache));
	  if (!s_cache->num_elems)
	    {
	      s_cache->num_elems = 1;
	      s_cache->elems = (XtPointer*)XtMalloc(sizeof(XtPointer));
	    }
	  else
	    {
	      ++s_cache->num_elems;
	      s_cache->elems = (XtPointer*)
		XtRealloc((char *)s_cache->elems,
			  (Cardinal)(sizeof(XtPointer) * s_cache->num_elems));
	    }
	  pcache->value = (long)colormap;
	  pcache->elems = NULL;
	  pcache->num_elems = 0;
	  s_cache->elems[s_cache->num_elems - 1] = (XtPointer)pcache;
	  c_cache = (XawCache *)s_cache->elems[s_cache->num_elems - 1];
	  if (s_cache->num_elems > 1)
	    qsort(s_cache->elems, s_cache->num_elems,
		  sizeof(XtPointer), qcmp_long);
	}

      d_cache = _XawFindCache(xaw,
			      screen, colormap, depth, FIND_DEPTH);
      if (!d_cache)
	{
	  pcache = (XawCache *)XtMalloc(sizeof(XawCache));
	  if (!c_cache->num_elems)
	    {
	      c_cache->num_elems = 1;
	      c_cache->elems = (XtPointer*)XtMalloc(sizeof(XtPointer));
	    }
	  else
	    {
	      ++c_cache->num_elems;
	      c_cache->elems = (XtPointer*)
		XtRealloc((char *)c_cache->elems,
			  (Cardinal)(sizeof(XtPointer) * c_cache->num_elems));
	    }
	  pcache->value = (long)depth;
	  pcache->elems = NULL;
	  pcache->num_elems = 0;
	  c_cache->elems[c_cache->num_elems - 1] = (XtPointer)pcache;
	  d_cache = (XawCache *)c_cache->elems[c_cache->num_elems - 1];
	  if (c_cache->num_elems > 1)
	    qsort(c_cache->elems, c_cache->num_elems,
		  sizeof(XtPointer), qcmp_long);
	}

      cache = d_cache;
    }

  return (cache);
}

static XawPixmap *
_XawFindPixmap(String name, Screen *screen, Colormap colormap, int depth)
{
  XawCache *cache;
  XawPixmap **pixmap;

  cache = _XawFindCache(&xaw_pixmaps, screen, colormap, depth, FIND_ALL);

  if (!cache)
    return (NULL);

  /* Name */
  pixmap = (XawPixmap **)bsearch((void *)name, cache->elems,
				 cache->num_elems, sizeof(XtPointer),
				 bcmp_string);
  if (!pixmap)
    return (NULL);

  return (*pixmap);
}

XawPixmap *
XawPixmapFromXPixmap(Pixmap pixmap,
		     Screen *screen, Colormap colormap, int depth)
{
  XawCache *cache;
  XawPixmap **x_pixmap;

  cache = _XawFindCache(&x_pixmaps, screen, colormap, depth, FIND_ALL);

  if (!cache)
    return (NULL);

  /* Pixmap */
  x_pixmap = (XawPixmap **)bsearch((void *)pixmap, cache->elems,
				   cache->num_elems, sizeof(XtPointer),
				   bcmp_x_cache);
  if (!x_pixmap)
    return (NULL);

  return (*x_pixmap);
}

static void
_XawCachePixmap(XawPixmap *pixmap,
		Screen *screen, Colormap colormap, int depth)
{
  XawCache *xaw_cache, *x_cache;

  xaw_cache = _XawGetCache(&xaw_pixmaps, screen, colormap, depth);
  x_cache = _XawGetCache(&x_pixmaps, screen, colormap, depth);

  if (!xaw_cache->num_elems)
    {
      xaw_cache->num_elems = 1;
      xaw_cache->elems = (XtPointer*)XtMalloc(sizeof(XtPointer));
    }
  else
    {
      ++xaw_cache->num_elems;
      xaw_cache->elems = (XtPointer*)XtRealloc((char *)xaw_cache->elems,
					       (Cardinal)(sizeof(XtPointer) *
							  xaw_cache->num_elems));
    }

  xaw_cache->elems[xaw_cache->num_elems - 1] = (XtPointer)pixmap;
  if (xaw_cache->num_elems > 1)
    qsort(xaw_cache->elems, xaw_cache->num_elems,
	  sizeof(XtPointer), qcmp_string);


  if (!x_cache->num_elems)
    {
      x_cache->num_elems = 1;
      x_cache->elems = (XtPointer*)XtMalloc(sizeof(XtPointer));
    }
  else
    {
      ++x_cache->num_elems;
      x_cache->elems = (XtPointer*)XtRealloc((char *)x_cache->elems,
					     (Cardinal)(sizeof(XtPointer) *
							x_cache->num_elems));
    }

  x_cache->elems[x_cache->num_elems - 1] = (XtPointer)pixmap;
  if (x_cache->num_elems > 1)
    qsort(x_cache->elems, x_cache->num_elems, sizeof(XtPointer), qcmp_x_cache);
}

#ifndef PROJECT_ROOT
#define PROJECT_ROOT	"/usr/X11R6"
#endif

static char *pixmap_path = NULL;

static void
GetResourcePixmapPath(Display *display)
{
    XrmName xrm_name[2];
    XrmClass xrm_class[2];
    XrmRepresentation rep_type;
    XrmValue value;
    static char *default_path =
	"%H/%T/%N:%P/include/X11/%T/%N:/usr/X11R6/include/X11/%T/%N:/usr/include/X11/%T/%N:%N";

    xrm_name[0] = XrmPermStringToQuark("pixmapFilePath");
    xrm_name[1] = NULLQUARK;
    xrm_class[0] = XrmPermStringToQuark("PixmapFilePath");
    xrm_class[1] = NULLQUARK;
    if (!XrmGetDatabase(display))
	(void) XGetDefault(display, "", "");
    if (XrmQGetResource(XrmGetDatabase(display), xrm_name, xrm_class,
			&rep_type, &value) &&
			rep_type == XrmPermStringToQuark("String")) {
	int length = 0;
	char *tok, *buffer = XtNewString(value.addr);

	for (tok = strtok(buffer, ":"); tok; tok = strtok(NULL, ":")) {
	    int toklen = (int)strlen(tok);

	    if (toklen) {
		pixmap_path = XtRealloc(pixmap_path, (Cardinal)(length + toklen + 5));
		strcpy(pixmap_path + length, tok);
		if (length)
		    pixmap_path[length++] = ':';
		sprintf(pixmap_path + length, "%s/%%N", tok);
		length = (length + (int)strlen(tok) + 3);
	    }
	}
	XtFree(buffer);
	pixmap_path = XtRealloc(pixmap_path, (Cardinal)((size_t)length + strlen(default_path) + 2));
	if (length)
	    pixmap_path[length++] = ':';
	strcpy(pixmap_path + length, default_path);
    }
    else
	pixmap_path = default_path;
}

static Bool
BitmapLoader(XawParams *params, Screen *screen, Colormap colormap, int depth,
	     Pixmap *pixmap_return, Pixmap *mask_return,
	     Dimension *width_return, Dimension *height_return)
{
  Pixel fg, bg;
  XColor color, exact;
  Pixmap pixmap;
  unsigned int width, height;
  unsigned char *data = NULL;
  int hotX, hotY;
  XawArgVal *argval;
  Bool retval = False;
  static SubstitutionRec sub[] = {
    {'H',   NULL},
    {'N',   NULL},
    {'T',   "bitmaps"},
    {'P',   PROJECT_ROOT},
  };
  char *filename;

  fg = BlackPixelOfScreen(screen);
  bg = WhitePixelOfScreen(screen);

  if ((argval = XawFindArgVal(params, "foreground")) != NULL
      && argval->value)
    {
      if (XAllocNamedColor(DisplayOfScreen(screen), colormap, argval->value,
			   &color, &exact))
	fg = color.pixel;
      else
	return (False);
    }
  if ((argval = XawFindArgVal(params, "background")) != NULL
      && argval->value)
    {
      if (XAllocNamedColor(DisplayOfScreen(screen), colormap, argval->value,
			   &color, &exact))
	bg = color.pixel;
      else
	return (False);
    }

  if (params->name[0] != '/' && params->name[0] != '.')
    {
      if (!sub[0].substitution)
	sub[0].substitution = getenv("HOME");
      sub[1].substitution = params->name;
      if (pixmap_path == NULL)
	GetResourcePixmapPath(DisplayOfScreen(screen));
      filename = XtFindFile(pixmap_path, sub, XtNumber(sub), NULL);
      if (!filename)
	return (FALSE);
    }
  else
    filename = params->name;

  if (XReadBitmapFileData(filename, &width, &height, &data,
			  &hotX, &hotY) == BitmapSuccess)
    {
      pixmap = XCreatePixmapFromBitmapData(DisplayOfScreen(screen),
					   RootWindowOfScreen(screen),
					   (char *)data,
					   width, height, fg, bg, (unsigned)depth);
      if (data)
	XFree(data);
      *pixmap_return = pixmap;
      *mask_return = None;
      *width_return = (Dimension)width;
      *height_return = (Dimension)height;

      retval = True;
    }

  if (filename != params->name)
    XtFree(filename);

  return (retval);
}

#define VERTICAL   1
#define HORIZONTAL 2
static Bool
GradientLoader(XawParams *params, Screen *screen, Colormap colormap, int depth,
	       Pixmap *pixmap_return, Pixmap *mask_return,
	       Dimension *width_return, Dimension *height_return)
{
  double ired, igreen, iblue, red, green, blue;
  XColor start, end, color;
  XGCValues values;
  GC gc;
  double i, inc, x, y, xend, yend;
  Pixmap pixmap;
  XawArgVal *argval;
  int orientation, dimension, steps;
  char *value;

  if (XmuCompareISOLatin1(params->name, "vertical") == 0)
    orientation = VERTICAL;
  else if (XmuCompareISOLatin1(params->name, "horizontal") == 0)
    orientation = HORIZONTAL;
  else
    return (False);

  if ((argval = XawFindArgVal(params, "dimension")) != NULL
      && argval->value)
    {
      dimension = atoi(argval->value);
      if (dimension <= 0)
	return (False);
    }
  else
    dimension = 50;

  if ((argval = XawFindArgVal(params, "steps")) != NULL
      && argval->value)
    {
      steps = atoi(argval->value);
      if (steps <= 0)
	return (False);
    }
  else
      steps = dimension;

  steps = XawMin(steps, dimension);

  value = NULL;
  if ((argval = XawFindArgVal(params, "start")) != NULL)
    value = argval->value;
  if (value && !XAllocNamedColor(DisplayOfScreen(screen), colormap, value,
			    &start, &color))
    return (False);
  else if (!value)
    {
      start.pixel = WhitePixelOfScreen(screen);
      XQueryColor(DisplayOfScreen(screen), colormap, &start);
    }
  value = NULL;
  if ((argval = XawFindArgVal(params, "end")) != NULL)
    value = argval->value;
  if (value && !XAllocNamedColor(DisplayOfScreen(screen), colormap, value,
			    &end, &color))
    return (False);
  else if (!value)
    {
      end.pixel = BlackPixelOfScreen(screen);
      XQueryColor(DisplayOfScreen(screen), colormap, &end);
    }

  if ((pixmap = XCreatePixmap(DisplayOfScreen(screen),
			      RootWindowOfScreen(screen),
			      (unsigned)(orientation == VERTICAL ? 1 : dimension),
			      (unsigned)(orientation == VERTICAL ? dimension : 1), (unsigned)depth))
      == 0)
    return (False);

  ired   = (double)(end.red   - start.red)   / (double)steps;
  igreen = (double)(end.green - start.green) / (double)steps;
  iblue  = (double)(end.blue  - start.blue)  / (double)steps;

  red   = color.red   = start.red;
  green = color.green = start.green;
  blue  = color.blue  = start.blue;

  inc = (double)dimension / (double)steps;

  gc = XCreateGC(DisplayOfScreen(screen), pixmap, 0, &values);

  x = y = 0.0;
  if (orientation == VERTICAL)
    {
      xend = 1;
      yend = 0;
    }
  else
    {
      xend = 0;
      yend = 1;
    }

  color.flags = DoRed | DoGreen | DoBlue;

  XSetForeground(DisplayOfScreen(screen), gc, start.pixel);
  for (i = 0.0; i < dimension; i += inc)
    {
      if ((int)color.red != (int)red || (int)color.green != (int)green
	  || (int)color.blue != (int)blue)
	{
	  XFillRectangle(DisplayOfScreen(screen), pixmap, gc, (int)x, (int)y,
			 (unsigned int)xend, (unsigned int)yend);
	  color.red   = (unsigned short)red;
	  color.green = (unsigned short)green;
	  color.blue  = (unsigned short)blue;
	  if (!XAllocColor(DisplayOfScreen(screen), colormap, &color))
	    {
	      XFreePixmap(DisplayOfScreen(screen), pixmap);
	      return (False);
	    }
	  XSetForeground(DisplayOfScreen(screen), gc, color.pixel);
	  if (orientation == VERTICAL)
	    y = yend;
	  else
	    x = xend;
	}
      red   += ired;
      green += igreen;
      blue  += iblue;
      if (orientation == VERTICAL)
	yend += inc;
      else
	xend += inc;
    }
  XFillRectangle(DisplayOfScreen(screen), pixmap, gc, (int)x, (int)y,
		 (unsigned int)xend, (unsigned int)yend);

  *pixmap_return = pixmap;
  *mask_return = None;
  *width_return = (Dimension)(orientation == VERTICAL ? 1 : dimension);
  *height_return = (Dimension)(orientation == VERTICAL ? dimension : 1);

  XFreeGC(DisplayOfScreen(screen), gc);

  return (True);
}

static Bool
XPixmapLoader(XawParams *params, Screen *screen, Colormap colormap, int depth _X_UNUSED,
	      Pixmap *pixmap_return, Pixmap *mask_return,
	      Dimension *width_return, Dimension *height_return)
{
  XpmAttributes xpm_attributes;
  XawArgVal *argval;
  unsigned int closeness = 4000;
  static SubstitutionRec sub[] = {
    {'H',   NULL},
    {'N',   NULL},
    {'T',   "pixmaps"},
    {'P',   PROJECT_ROOT},
  };
  const char *filename;

  if ((argval = XawFindArgVal(params, "closeness")) != NULL
      && argval->value)
    closeness = (unsigned)atoi(argval->value);

  if (params->name[0] != '/' && params->name[0] != '.')
    {
      if (!sub[0].substitution)
	sub[0].substitution = getenv("HOME");
      sub[1].substitution = params->name;
      if (pixmap_path == NULL)
	GetResourcePixmapPath(DisplayOfScreen(screen));
      filename = XtFindFile(pixmap_path, sub, XtNumber(sub), NULL);
      if (!filename)
	return (False);
    }
  else
    filename = params->name;

  xpm_attributes.colormap = colormap;
  xpm_attributes.closeness = closeness;
  xpm_attributes.valuemask = XpmSize | XpmColormap | XpmCloseness;
  if (XpmReadFileToPixmap(DisplayOfScreen(screen),
			  RootWindowOfScreen(screen), filename, pixmap_return,
			  mask_return, &xpm_attributes) == XpmSuccess)
    {
      *width_return = (Dimension)xpm_attributes.width;
      *height_return = (Dimension)xpm_attributes.height;

      return (True);
    }

  return (False);
}

void
XawReshapeWidget(Widget w, XawPixmap *pixmap)
{
  if (!pixmap || pixmap->mask == None)
    XShapeCombineMask(XtDisplay(w), XtWindow(w), ShapeBounding, 0, 0,
		      None, ShapeSet);
  else
    XShapeCombineMask(XtDisplay(w), XtWindow(w), ShapeBounding, 0, 0,
		      pixmap->mask, ShapeSet);
}

#endif /* OLDXAW */
