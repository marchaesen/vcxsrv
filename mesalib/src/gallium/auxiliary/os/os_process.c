/**************************************************************************
 *
 * Copyright 2013 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#include "pipe/p_config.h"
#include "os/os_process.h"
#include "util/u_memory.h"
#include "util/u_process.h"

#if defined(PIPE_OS_WINDOWS)
#  include <windows.h>
#elif defined(PIPE_OS_HAIKU)
#  include <kernel/OS.h>
#  include <kernel/image.h>
#endif

#if defined(PIPE_OS_LINUX)
#  include <fcntl.h>
#endif


/**
 * Return the name of the current process.
 * \param procname  returns the process name
 * \param size  size of the procname buffer
 * \return  TRUE or FALSE for success, failure
 */
boolean
os_get_process_name(char *procname, size_t size)
{
   const char *name;

   /* First, check if the GALLIUM_PROCESS_NAME env var is set to
    * override the normal process name query.
    */
   name = os_get_option("GALLIUM_PROCESS_NAME");

   if (!name) {
      /* do normal query */

#if defined(PIPE_OS_WINDOWS)
      char szProcessPath[MAX_PATH];
      char *lpProcessName;
      char *lpProcessExt;

      GetModuleFileNameA(NULL, szProcessPath, ARRAY_SIZE(szProcessPath));

      lpProcessName = strrchr(szProcessPath, '\\');
      lpProcessName = lpProcessName ? lpProcessName + 1 : szProcessPath;

      lpProcessExt = strrchr(lpProcessName, '.');
      if (lpProcessExt) {
         *lpProcessExt = '\0';
      }

      name = lpProcessName;

#elif defined(PIPE_OS_HAIKU)
      image_info info;
      get_image_info(B_CURRENT_TEAM, &info);
      name = info.name;
#else
      name = util_get_process_name();
#endif
   }

   assert(size > 0);
   assert(procname);

   if (name && procname && size > 0) {
      strncpy(procname, name, size);
      procname[size - 1] = '\0';
      return TRUE;
   }
   else {
      return FALSE;
   }
}


/**
 * Return the command line for the calling process.  This is basically
 * the argv[] array with the arguments separated by spaces.
 * \param cmdline  returns the command line string
 * \param size  size of the cmdline buffer
 * \return  TRUE or FALSE for success, failure
 */
boolean
os_get_command_line(char *cmdline, size_t size)
{
#if defined(PIPE_OS_WINDOWS)
   const char *args = GetCommandLineA();
   if (args) {
      strncpy(cmdline, args, size);
      // make sure we terminate the string
      cmdline[size - 1] = 0;
      return TRUE;
   }
#elif defined(PIPE_OS_LINUX)
   int f = open("/proc/self/cmdline", O_RDONLY);
   if (f != -1) {
      const int n = read(f, cmdline, size - 1);
      int i;
      assert(n < size);
      // The arguments are separated by '\0' chars.  Convert them to spaces.
      for (i = 0; i < n; i++) {
         if (cmdline[i] == 0) {
            cmdline[i] = ' ';
         }
      }
      // terminate the string
      cmdline[n] = 0;
      close(f);
      return TRUE;
   }
#endif

   /* XXX to-do: implement this function for other operating systems */

   cmdline[0] = 0;
   return FALSE;
}
