/*
 * Copyright © 2003 Felix Kuehling
 * Copyright © 2018 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

#include "u_process.h"
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#undef GET_PROGRAM_NAME

#if (defined(__GNU_LIBRARY__) || defined(__GLIBC__)) && !defined(__UCLIBC__)
#    if !defined(__GLIBC__) || (__GLIBC__ < 2)
/* These aren't declared in any libc5 header */
extern char *program_invocation_name, *program_invocation_short_name;
#    endif
static const char *
__getProgramName()
{
   char * arg = strrchr(program_invocation_name, '/');
   if (arg)
      return arg+1;

   /* If there was no '/' at all we likely have a windows like path from
    * a wine application.
    */
   arg = strrchr(program_invocation_name, '\\');
   if (arg)
      return arg+1;

   return program_invocation_name;
}
#    define GET_PROGRAM_NAME() __getProgramName()
#elif defined(__CYGWIN__)
#    define GET_PROGRAM_NAME() program_invocation_short_name
#elif defined(__FreeBSD__) && (__FreeBSD__ >= 2)
#    include <osreldate.h>
#    if (__FreeBSD_version >= 440000)
#        define GET_PROGRAM_NAME() getprogname()
#    endif
#elif defined(__NetBSD__) && defined(__NetBSD_Version__) && (__NetBSD_Version__ >= 106000100)
#    define GET_PROGRAM_NAME() getprogname()
#elif defined(__DragonFly__)
#    define GET_PROGRAM_NAME() getprogname()
#elif defined(__APPLE__)
#    define GET_PROGRAM_NAME() getprogname()
#elif defined(ANDROID)
#    define GET_PROGRAM_NAME() getprogname()
#elif defined(__sun)
/* Solaris has getexecname() which returns the full path - return just
   the basename to match BSD getprogname() */
#    include <libgen.h>

static const char *
__getProgramName()
{
    static const char *progname;

    if (progname == NULL) {
        const char *e = getexecname();
        if (e != NULL) {
            /* Have to make a copy since getexecname can return a readonly
               string, but basename expects to be able to modify its arg. */
            char *n = strdup(e);
            if (n != NULL) {
                progname = basename(n);
            }
        }
    }
    return progname;
}

#    define GET_PROGRAM_NAME() __getProgramName()
#endif

#if !defined(GET_PROGRAM_NAME)
#    if defined(__OpenBSD__) || defined(NetBSD) || defined(__UCLIBC__) || defined(ANDROID)
/* This is a hack. It's said to work on OpenBSD, NetBSD and GNU.
 * Rogelio M.Serrano Jr. reported it's also working with UCLIBC. It's
 * used as a last resort, if there is no documented facility available. */
static const char *
__getProgramName()
{
    extern const char *__progname;
    char * arg = strrchr(__progname, '/');
    if (arg)
        return arg+1;
    else
        return __progname;
}
#        define GET_PROGRAM_NAME() __getProgramName()
#    else
#        define GET_PROGRAM_NAME() ""
#        pragma message ( "Warning: Per application configuration won't work with your OS version." )
#    endif
#endif

const char *
util_get_process_name(void)
{
   return GET_PROGRAM_NAME();
}
