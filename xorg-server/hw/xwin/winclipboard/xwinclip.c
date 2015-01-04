/*
 *Copyright (C) 1994-2000 The XFree86 Project, Inc. All Rights Reserved.
 *Copyright (C) 2003-2004 Harold L Hunt II All Rights Reserved.
 *Copyright (C) Colin Harrison 2005-2008
 *
 *Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 *"Software"), to deal in the Software without restriction, including
 *without limitation the rights to use, copy, modify, merge, publish,
 *distribute, sublicense, and/or sell copies of the Software, and to
 *permit persons to whom the Software is furnished to do so, subject to
 *the following conditions:
 *
 *The above copyright notice and this permission notice shall be
 *included in all copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 *ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *Except as contained in this notice, the name of the copyright holder(s)
 *and author(s) shall not be used in advertising or otherwise to promote
 *the sale, use or other dealings in this Software without prior written
 *authorization from the copyright holder(s) and author(s).
 *
 * Authors:	Harold L Hunt II
 *              Colin Harrison
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif

/*
 * Including any server header might define the macro _XSERVER64 on 64 bit machines.
 * That macro must _NOT_ be defined for Xlib client code, otherwise bad things happen.
 * So let's undef that macro if necessary.
 */
#ifdef _XSERVER64
#undef _XSERVER64
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* X headers */
#include <X11/Xlib.h>
#ifdef X_LOCALE
#include <X11/Xlocale.h>
#else /* X_LOCALE */
#include <locale.h>
#endif /* X_LOCALE */

#include "winclipboard.h"

/*
 * Main function
 */

int
main (int argc, char *argv[])
{
  int			i;
  char			*pszDisplay = NULL;
  int			fUnicodeClipboard = 1;

  /* Parse command-line parameters */
  for (i = 1; i < argc; ++i)
    {
      /* Look for -display "display_name" or --display "display_name" */
      if (i < argc - 1
	  && (!strcmp (argv[i], "-display")
	      || !strcmp (argv[i], "--display")))
	{
	  /* Grab a pointer to the display parameter */
	  pszDisplay = argv[i + 1];

	  /* Skip the display argument */
	  i++;
	  continue;
	}

      /* Look for -nounicodeclipboard */
      if (!strcmp (argv[i], "-nounicodeclipboard"))
	{
	  fUnicodeClipboard = 0;
	  continue;
	}

      /* Look for -noprimary */
      if (!strcmp (argv[i], "-noprimary"))
	{
	  fPrimarySelection = False;
	  continue;
	}

      /* Yack when we find a parameter that we don't know about */
      printf ("Unknown parameter: %s\nExiting.\n", argv[i]);
      exit (1);
    }

  /* Do we have Unicode support? */
  if (fUnicodeClipboard)
    {
      printf ("Unicode clipboard I/O\n");
    }
  else
    {
      printf ("Non Unicode clipboard I/O\n");
    }

  /* Apply locale specified in the LANG environment variable */
  if (!setlocale (LC_ALL, ""))
    {
      printf ("setlocale() error\n");
      exit (1);
    }

  /* See if X supports the current locale */
  if (XSupportsLocale () == False)
    {
      printf ("Locale not supported by X, falling back to 'C' locale.\n");
      setlocale(LC_ALL, "C");
    }

  winClipboardProc(fUnicodeClipboard, pszDisplay);

  return 0;
}
