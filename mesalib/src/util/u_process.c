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
#include "detect_os.h"
#include "macros.h"
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#undef GET_PROGRAM_NAME

#if DETECT_OS_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

#if DETECT_OS_APPLE
#include <mach-o/dyld.h>
#endif

#if defined(__linux__) && defined(HAVE_PROGRAM_INVOCATION_NAME)

static char *path = NULL;

static void __freeProgramPath()
{
   free(path);
   path = NULL;
}

static const char *
__getProgramName()
{
   char * arg = strrchr(program_invocation_name, '/');
   if (arg) {
      /* If the / character was found this is likely a linux path or
       * an invocation path for a 64-bit wine program.
       *
       * However, some programs pass command line arguments into argv[0].
       * Strip these arguments out by using the realpath only if it was
       * a prefix of the invocation name.
       */
      if (!path) {
         path = realpath("/proc/self/exe", NULL);
         atexit(__freeProgramPath);
      }

      if (path && strncmp(path, program_invocation_name, strlen(path)) == 0) {
         /* This shouldn't be null because path is a a prefix,
          * but check it anyway since path is static. */
         char * name = strrchr(path, '/');
         if (name)
            return name + 1;
      }

      return arg+1;
   }

   /* If there was no '/' at all we likely have a windows like path from
    * a wine application.
    */
   arg = strrchr(program_invocation_name, '\\');
   if (arg)
      return arg+1;

   return program_invocation_name;
}
#    define GET_PROGRAM_NAME() __getProgramName()
#elif defined(HAVE_PROGRAM_INVOCATION_NAME)
#    define GET_PROGRAM_NAME() program_invocation_short_name
#elif defined(__FreeBSD__) && (__FreeBSD__ >= 2)
#    include <osreldate.h>
#    if (__FreeBSD_version >= 440000)
#        define GET_PROGRAM_NAME() getprogname()
#    endif
#elif defined(__NetBSD__)
#    include <sys/param.h>
#    if defined(__NetBSD_Version__) && (__NetBSD_Version__ >= 106000100)
#        define GET_PROGRAM_NAME() getprogname()
#    endif
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
#    if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__UCLIBC__) || defined(ANDROID)
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

size_t
util_get_process_exec_path(char* process_path, size_t len)
{
#if DETECT_OS_WINDOWS
   return GetModuleFileNameA(NULL, process_path, len);
#elif DETECT_OS_APPLE
   uint32_t bufSize = len;
   int result = _NSGetExecutablePath(process_path, &bufSize);

   return (result == 0) ? strlen(process_path) : 0;
#elif DETECT_OS_UNIX
   ssize_t r;

   if ((r = readlink("/proc/self/exe", process_path, len)) > 0)
      goto success;
   if ((r = readlink("/proc/curproc/exe", process_path, len)) > 0)
      goto success;
   if ((r = readlink("/proc/curproc/file", process_path, len)) > 0)
      goto success;

    return 0;
success:
   if (r == len)
      return 0;

    process_path[r] = '\0';
    return r;

#endif
   return 0;
}
