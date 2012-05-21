/*
 * fontconfig/src/fcinit.c
 *
 * Copyright © 2001 Keith Packard
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
#include <stdlib.h>

static FcConfig *
FcInitFallbackConfig (void)
{
    FcConfig	*config;

    config = FcConfigCreate ();
    if (!config)
	goto bail0;
    if (!FcConfigAddDir (config, (FcChar8 *) FC_DEFAULT_FONTS))
	goto bail1;
    if (!FcConfigAddCacheDir (config, (FcChar8 *) FC_CACHEDIR))
	goto bail1;
    return config;

bail1:
    FcConfigDestroy (config);
bail0:
    return 0;
}

int
FcGetVersion (void)
{
    return FC_VERSION;
}

/*
 * Load the configuration files
 */
FcConfig *
FcInitLoadConfig (void)
{
    FcConfig	*config;

    FcInitDebug ();
    config = FcConfigCreate ();
    if (!config)
	return NULL;

    if (!FcConfigParseAndLoad (config, 0, FcTrue))
    {
	FcConfigDestroy (config);
	return FcInitFallbackConfig ();
    }

    if (config->cacheDirs && config->cacheDirs->num == 0)
    {
	FcChar8 *prefix;
	size_t plen;

	fprintf (stderr,
		 "Fontconfig warning: no <cachedir> elements found. Check configuration.\n");
	fprintf (stderr,
		 "Fontconfig warning: adding <cachedir>%s</cachedir>\n",
		 FC_CACHEDIR);
	prefix = FcConfigXdgCacheHome ();
	plen = prefix ? strlen ((const char *)prefix) : 0;
	if (!prefix)
	    goto bail;
	prefix = realloc (prefix, plen + 12);
	if (!prefix)
	    goto bail;
	memcpy (&prefix[plen], FC_DIR_SEPARATOR_S "fontconfig", 11);
	prefix[plen + 11] = 0;
	fprintf (stderr,
		 "Fontconfig warning: adding <cachedir prefix=\"xdg\">fontconfig</cachedir>\n");

	if (!FcConfigAddCacheDir (config, (FcChar8 *) FC_CACHEDIR) ||
	    !FcConfigAddCacheDir (config, (FcChar8 *) prefix))
	{
	  bail:
	    fprintf (stderr,
		     "Fontconfig error: out of memory");
	    free (prefix);
	    FcConfigDestroy (config);
	    return FcInitFallbackConfig ();
	}
	free (prefix);
    }

    return config;
}

/*
 * Load the configuration files and scan for available fonts
 */
FcConfig *
FcInitLoadConfigAndFonts (void)
{
    FcConfig	*config = FcInitLoadConfig ();

    FcInitDebug ();
    if (!config)
	return 0;
    if (!FcConfigBuildFonts (config))
    {
	FcConfigDestroy (config);
	return 0;
    }
    return config;
}

/*
 * Initialize the default library configuration
 */
FcBool
FcInit (void)
{
    FcConfig	*config;

    if (_fcConfig)
	return FcTrue;
    config = FcInitLoadConfigAndFonts ();
    if (!config)
	return FcFalse;
    FcConfigSetCurrent (config);
    if (FcDebug() & FC_DBG_MEMORY)
	FcMemReport ();
    return FcTrue;
}

/*
 * Free all library-allocated data structures.
 */
void
FcFini (void)
{
    if (_fcConfig)
	FcConfigDestroy (_fcConfig);

    FcObjectFini ();
    FcCacheFini ();
    if (FcDebug() & FC_DBG_MEMORY)
	FcMemReport ();
}

/*
 * Reread the configuration and available font lists
 */
FcBool
FcInitReinitialize (void)
{
    FcConfig	*config;

    config = FcInitLoadConfigAndFonts ();
    if (!config)
	return FcFalse;
    FcConfigSetCurrent (config);
    return FcTrue;
}

FcBool
FcInitBringUptoDate (void)
{
    FcConfig	*config = FcConfigGetCurrent ();
    time_t	now;

    /*
     * rescanInterval == 0 disables automatic up to date
     */
    if (config->rescanInterval == 0)
	return FcTrue;
    /*
     * Check no more often than rescanInterval seconds
     */
    now = time (0);
    if (config->rescanTime + config->rescanInterval - now > 0)
	return FcTrue;
    /*
     * If up to date, don't reload configuration
     */
    if (FcConfigUptoDate (0))
	return FcTrue;
    return FcInitReinitialize ();
}

static struct {
    char    name[16];
    int	    alloc_count;
    int	    alloc_mem;
    int	    free_count;
    int	    free_mem;
} FcInUse[FC_MEM_NUM] = {
    { "charset" },
    { "charleaf" },
    { "fontset" },
    { "fontptr" },
    { "objectset" },
    { "objectptr" },
    { "matrix" },
    { "pattern" },
    { "patelt" },
    { "vallist" },
    { "substate" },
    { "string" },
    { "listbuck" },
    { "strset" },
    { "strlist" },
    { "config" },
    { "langset" },
    { "atomic" },
    { "blanks" },
    { "cache" },
    { "strbuf" },
    { "subst" },
    { "objecttype" },
    { "constant" },
    { "test" },
    { "expr" },
    { "vstack" },
    { "attr" },
    { "pstack" },
    { "sharedstr" },
};

static int  FcAllocCount, FcAllocMem;
static int  FcFreeCount, FcFreeMem;

static int  FcMemNotice = 1*1024*1024;

static int  FcAllocNotify, FcFreeNotify;

void
FcMemReport (void)
{
    int	i;
    printf ("Fc Memory Usage:\n");
    printf ("\t   Which       Alloc           Free           Active\n");
    printf ("\t           count   bytes   count   bytes   count   bytes\n");
    for (i = 0; i < FC_MEM_NUM; i++)
	printf ("%16.16s%8d%8d%8d%8d%8d%8d\n",
		FcInUse[i].name,
		FcInUse[i].alloc_count, FcInUse[i].alloc_mem,
		FcInUse[i].free_count, FcInUse[i].free_mem,
		FcInUse[i].alloc_count - FcInUse[i].free_count,
		FcInUse[i].alloc_mem - FcInUse[i].free_mem);
    printf ("%16.16s%8d%8d%8d%8d%8d%8d\n",
	    "Total",
	    FcAllocCount, FcAllocMem,
	    FcFreeCount, FcFreeMem,
	    FcAllocCount - FcFreeCount,
	    FcAllocMem - FcFreeMem);
    FcAllocNotify = 0;
    FcFreeNotify = 0;
}

void
FcMemAlloc (int kind, int size)
{
    if (FcDebug() & FC_DBG_MEMORY)
    {
	FcInUse[kind].alloc_count++;
	FcInUse[kind].alloc_mem += size;
	FcAllocCount++;
	FcAllocMem += size;
	FcAllocNotify += size;
	if (FcAllocNotify > FcMemNotice)
	    FcMemReport ();
    }
}

void
FcMemFree (int kind, int size)
{
    if (FcDebug() & FC_DBG_MEMORY)
    {
	FcInUse[kind].free_count++;
	FcInUse[kind].free_mem += size;
	FcFreeCount++;
	FcFreeMem += size;
	FcFreeNotify += size;
	if (FcFreeNotify > FcMemNotice)
	    FcMemReport ();
    }
}
#define __fcinit__
#include "fcaliastail.h"
#undef __fcinit__
