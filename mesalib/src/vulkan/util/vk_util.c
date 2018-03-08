/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include "vk_util.h"

uint32_t vk_get_driver_version(void)
{
   const char *minor_string = strchr(VERSION, '.');
   const char *patch_string = minor_string ? strchr(minor_string + 1, '.') : NULL;
   int major = atoi(VERSION);
   int minor = minor_string ? atoi(minor_string + 1) : 0;
   int patch = patch_string ? atoi(patch_string + 1) : 0;
   if (strstr(VERSION, "devel")) {
      if (patch == 0) {
         patch = 99;
         if (minor == 0) {
            minor = 99;
            --major;
         } else
            --minor;
      } else
         --patch;
   }
   return VK_MAKE_VERSION(major, minor, patch);
}

uint32_t vk_get_version_override(void)
{
   const char *str = getenv("MESA_VK_VERSION_OVERRIDE");
   if (str == NULL)
      return 0;

   const char *minor_str = strchr(str, '.');
   const char *patch_str = minor_str ? strchr(minor_str + 1, '.') : NULL;

   int major = atoi(str);
   int minor = minor_str ? atoi(minor_str + 1) : 0;
   int patch = patch_str ? atoi(patch_str + 1) : 0;

   /* Do some basic version sanity checking */
   if (major < 1 || minor < 0 || patch < 0 || minor > 1023 || patch > 4095)
      return 0;

   return VK_MAKE_VERSION(major, minor, patch);
}
