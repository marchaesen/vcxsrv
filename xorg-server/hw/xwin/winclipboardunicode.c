/*
 *Copyright (C) 2003-2004 Harold L Hunt II All Rights Reserved.
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
 *NONINFRINGEMENT. IN NO EVENT SHALL HAROLD L HUNT II BE LIABLE FOR
 *ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *Except as contained in this notice, the name of Harold L Hunt II
 *shall not be used in advertising or otherwise to promote the sale, use
 *or other dealings in this Software without prior written authorization
 *from Harold L Hunt II.
 *
 * Authors:	Harold L Hunt II
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "winclipboard.h"

/*
 * Determine whether we suport Unicode or not.
 * NOTE: Currently, just check if we are on an NT-based platform or not.
 */

Bool
winClipboardDetectUnicodeSupport(void)
{
    Bool fReturn = FALSE;
    OSVERSIONINFOEX osvi = { 0 };

    /* Get operating system version information */
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    GetVersionEx ((LPOSVERSIONINFO)&osvi);

    /* Branch on platform ID */
    switch (osvi.dwPlatformId) {
    case VER_PLATFORM_WIN32_NT:
      if (osvi.dwMajorVersion >= 6)
      {
	if (osvi.wProductType == VER_NT_WORKSTATION)
	   winDebug ("OS: Windows Vista\n");
	else
	   winDebug ("OS: Windows Server 2008\n");
	fReturn = TRUE;
      }
      else if (osvi.dwMajorVersion == 5)
      {
	if (osvi.dwMinorVersion == 2)
	{
	  winDebug ("OS: Windows 2003\n");
	  fReturn = TRUE;
	}
	else if (osvi.dwMinorVersion == 1)
	{
	  winDebug ("OS: Windows XP\n");
        fReturn = TRUE;
	}
	else if (osvi.dwMinorVersion == 0) winDebug ("OS: Windows 2000\n");
      }
      else if (osvi.dwMajorVersion <= 4) winDebug ("OS: Windows NT\n");
        break;

    case VER_PLATFORM_WIN32_WINDOWS:
      winDebug ("OS: Windows 95/98/Me\n");
        break;
    }

    return fReturn;
}
