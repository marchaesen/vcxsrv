
#ifndef INLINE_SW_HELPER_H
#define INLINE_SW_HELPER_H

#include "pipe/p_compiler.h"
#include "util/u_debug.h"
#include "frontend/sw_winsys.h"

#ifdef GALLIUM_SWR
#include "swr/swr_public.h"
#endif

/* Helper function to choose and instantiate one of the software rasterizers:
 * llvmpipe, softpipe.
 */

#ifdef GALLIUM_SOFTPIPE
#include "softpipe/sp_public.h"
#endif

#ifdef GALLIUM_LLVMPIPE
#include "llvmpipe/lp_public.h"
#endif

#ifdef GALLIUM_VIRGL
#include "virgl/virgl_public.h"
#include "virgl/vtest/virgl_vtest_public.h"
#endif

#ifdef GALLIUM_D3D12
#include "d3d12/d3d12_public.h"
#endif

static inline struct pipe_screen *
sw_screen_create_named(struct sw_winsys *winsys, const char *driver)
{
   struct pipe_screen *screen = NULL;

#if defined(GALLIUM_LLVMPIPE)
   if (screen == NULL && strcmp(driver, "llvmpipe") == 0)
      screen = llvmpipe_create_screen(winsys);
#endif

#if defined(GALLIUM_VIRGL)
   if (screen == NULL && strcmp(driver, "virpipe") == 0) {
      struct virgl_winsys *vws;
      vws = virgl_vtest_winsys_wrap(winsys);
      screen = virgl_create_screen(vws, NULL);
   }
#endif

#if defined(GALLIUM_SOFTPIPE)
   if (screen == NULL && strcmp(driver, "softpipe") == 0)
      screen = softpipe_create_screen(winsys);
#endif

#if defined(GALLIUM_SWR)
   if (screen == NULL && strcmp(driver, "swr") == 0)
      screen = swr_create_screen(winsys);
#endif

#if defined(GALLIUM_ZINK)
   if (screen == NULL && strcmp(driver, "zink") == 0)
      screen = zink_create_screen(winsys);
#endif

#if defined(GALLIUM_D3D12)
   if (screen == NULL && strcmp(driver, "d3d12") == 0)
      screen = d3d12_create_dxcore_screen(winsys, NULL);
#endif

   return screen;
}


static inline struct pipe_screen *
sw_screen_create(struct sw_winsys *winsys)
{
   const char *drivers[] = {
      debug_get_option("GALLIUM_DRIVER", ""),
#if defined(GALLIUM_ZINK)
      "zink",
#endif
#if defined(GALLIUM_D3D12)
      "d3d12",
#endif
#if defined(GALLIUM_LLVMPIPE)
      "llvmpipe",
#endif
#if defined(GALLIUM_SOFTPIPE)
      "softpipe",
#endif
#if defined(GALLIUM_SWR)
      "swr",
#endif
   };

   for (unsigned i = 0; i < ARRAY_SIZE(drivers); i++) {
      struct pipe_screen *screen = sw_screen_create_named(winsys, drivers[i]);
      if (screen)
         return screen;
      /* If the env var is set, don't keep trying things */
      else if (i == 0 && drivers[i][0] != '\0')
         return NULL;
   }
   return NULL;
}

#endif
