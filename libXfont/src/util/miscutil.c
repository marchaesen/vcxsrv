/*

Copyright 1991, 1994, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from The Open Group.

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <X11/Xosdefs.h>
#include <stdlib.h>
#include <X11/fonts/fontmisc.h>
#include "stubs.h"

#define XK_LATIN1
#include    <X11/keysymdef.h>


#ifdef __SUNPRO_C
#pragma weak serverGeneration
#pragma weak register_fpe_functions
#endif

extern void BuiltinRegisterFpeFunctions(void);

/* make sure everything initializes themselves at least once */
weak unsigned long serverGeneration = 1;

unsigned long __GetServerGeneration (void);

unsigned long
__GetServerGeneration (void)
{
  OVERRIDE_DATA(serverGeneration);
  return serverGeneration;
}

weak void
register_fpe_functions (void)
{
    OVERRIDE_SYMBOL(register_fpe_functions);
    BuiltinRegisterFpeFunctions();
    FontFileRegisterFpeFunctions();
#ifdef XFONT_FC
    fs_register_fpe_functions();
#endif
}
