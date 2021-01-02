/****************************************************************************
 * Copyright (C) 2016 Intel Corporation.   All Rights Reserved.
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

#include "memory/InitMemory.h"
#include "util/u_cpu_detect.h"
#include "util/u_dl.h"
#include "swr_public.h"
#include "swr_screen.h"

#include <stdio.h>

// Helper function to resolve the backend filename based on architecture
static bool
swr_initialize_screen_interface(struct swr_screen *screen, const char arch[])
{
#ifdef HAVE_SWR_BUILTIN
   screen->pLibrary = NULL;
   screen->pfnSwrGetInterface = SwrGetInterface;
   screen->pfnSwrGetTileInterface = SwrGetTileIterface;
   InitTilesTable();
   swr_print_info("(using: builtin).\n");
#else
   char filename[256] = { 0 };
   sprintf(filename, "%sswr%s%s", UTIL_DL_PREFIX, arch, UTIL_DL_EXT);

   screen->pLibrary = util_dl_open(filename);
   if (!screen->pLibrary) {
      fprintf(stderr, "(skipping: %s).\n", util_dl_error());
      return false;
   }

   util_dl_proc pApiProc = util_dl_get_proc_address(screen->pLibrary,
      "SwrGetInterface");
   util_dl_proc pTileApiProc = util_dl_get_proc_address(screen->pLibrary,
      "SwrGetTileIterface");
   util_dl_proc pInitFunc = util_dl_get_proc_address(screen->pLibrary,
      "InitTilesTable");
   if (!pApiProc || !pInitFunc || !pTileApiProc) {
      fprintf(stderr, "(skipping: %s).\n", util_dl_error());
      util_dl_close(screen->pLibrary);
      screen->pLibrary = NULL;
      return false;
   }

   screen->pfnSwrGetInterface = (PFNSwrGetInterface)pApiProc;
   screen->pfnSwrGetTileInterface = (PFNSwrGetTileInterface)pTileApiProc;

   SWR_ASSERT(screen->pfnSwrGetInterface != nullptr);
   SWR_ASSERT(screen->pfnSwrGetTileInterface != nullptr);
   SWR_ASSERT(pInitFunc != nullptr);

   pInitFunc();

   swr_print_info("(using: %s).\n", filename);
#endif

   return true;
}


struct pipe_screen *
swr_create_screen(struct sw_winsys *winsys)
{
   struct pipe_screen *p_screen = swr_create_screen_internal(winsys);
   if (!p_screen) {
      return NULL;
   }

   struct swr_screen *screen = swr_screen(p_screen);
   screen->is_knl = false;

   util_cpu_detect();

   if (util_cpu_caps.has_avx512f && util_cpu_caps.has_avx512er) {
      swr_print_info("SWR detected KNL instruction support ");
#ifndef HAVE_SWR_KNL
      swr_print_info("(skipping: not built).\n");
#else
      if (swr_initialize_screen_interface(screen, "KNL")) {
         screen->is_knl = true;
         return p_screen;
      }
#endif
   }

   if (util_cpu_caps.has_avx512f && util_cpu_caps.has_avx512bw) {
      swr_print_info("SWR detected SKX instruction support ");
#ifndef HAVE_SWR_SKX
      swr_print_info("(skipping not built).\n");
#else
      if (swr_initialize_screen_interface(screen, "SKX"))
         return p_screen;
#endif
   }

   if (util_cpu_caps.has_avx2) {
      swr_print_info("SWR detected AVX2 instruction support ");
#ifndef HAVE_SWR_AVX2
      swr_print_info("(skipping not built).\n");
#else
      if (swr_initialize_screen_interface(screen, "AVX2"))
         return p_screen;
#endif
   }

   if (util_cpu_caps.has_avx) {
      swr_print_info("SWR detected AVX instruction support ");
#ifndef HAVE_SWR_AVX
      swr_print_info("(skipping not built).\n");
#else
      if (swr_initialize_screen_interface(screen, "AVX"))
         return p_screen;
#endif
   }

   fprintf(stderr, "SWR could not initialize a supported CPU architecture.\n");
   swr_destroy_screen_internal(&screen);

   return NULL;
}


#ifdef _WIN32
// swap function called from libl_gdi.c

void
swr_gdi_swap(struct pipe_screen *screen,
             struct pipe_context *ctx,
             struct pipe_resource *res,
             void *hDC)
{
   screen->flush_frontbuffer(screen,
                             ctx,
                             res,
                             0, 0,
                             hDC,
                             NULL);
}

#endif /* _WIN32 */
