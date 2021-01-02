/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 ***************************************************************************/

#ifndef SWR_SCREEN_H
#define SWR_SCREEN_H

#include "swr_resource.h"

#include "pipe/p_screen.h"
#include "pipe/p_defines.h"
#include "util/u_dl.h"
#include "util/format/u_format.h"
#include "api.h"

#include "memory/TilingFunctions.h"
#include "memory/InitMemory.h"
#include <stdio.h>
#include <stdarg.h>

struct sw_winsys;

struct swr_screen {
   struct pipe_screen base;
   struct pipe_context *pipe;

   struct pipe_fence_handle *flush_fence;

   struct sw_winsys *winsys;

   /* Configurable environment settings */
   bool msaa_force_enable;
   uint8_t msaa_max_count;
   uint32_t client_copy_limit;

   HANDLE hJitMgr;

   /* Dynamic backend implementations */
   util_dl_library *pLibrary;
   PFNSwrGetInterface pfnSwrGetInterface;
   PFNSwrGetTileInterface pfnSwrGetTileInterface;

   /* Do we run on Xeon Phi? */
   bool is_knl;
};

static INLINE struct swr_screen *
swr_screen(struct pipe_screen *pipe)
{
   return (struct swr_screen *)pipe;
}

SWR_FORMAT
mesa_to_swr_format(enum pipe_format format);

INLINE void swr_print_info(const char *format, ...)
{
   static bool print_info = debug_get_bool_option("SWR_PRINT_INFO", false);
   if(print_info) {
      va_list args;
      va_start(args, format);
      vfprintf(stderr, format, args);
      va_end(args);
   }
}

#endif
