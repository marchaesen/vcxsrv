
#ifndef SW_HELPER_H
#define SW_HELPER_H

#include "pipe/p_compiler.h"
#include "util/u_debug.h"
#include "util/debug.h"
#include "target-helpers/sw_helper_public.h"
#include "frontend/sw_winsys.h"


/* Helper function to choose and instantiate one of the software rasterizers:
 * llvmpipe, softpipe, swr.
 */

#ifdef GALLIUM_ZINK
#include "zink/zink_public.h"
#endif

#ifdef GALLIUM_D3D12
#include "d3d12/d3d12_public.h"
#endif

#ifdef GALLIUM_ASAHI
#include "asahi/agx_public.h"
#endif

#ifdef GALLIUM_SOFTPIPE
#include "softpipe/sp_public.h"
#endif

#ifdef GALLIUM_LLVMPIPE
#include "llvmpipe/lp_public.h"
#endif

#ifdef GALLIUM_SWR
#include "swr/swr_public.h"
#endif

#ifdef GALLIUM_VIRGL
#include "virgl/virgl_public.h"
#include "virgl/vtest/virgl_vtest_public.h"
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

#if defined(GALLIUM_ASAHI)
   if (screen == NULL && strcmp(driver, "asahi") == 0)
      screen = agx_screen_create(winsys);
#endif

   return screen;
}

struct pipe_screen *
sw_screen_create_vk(struct sw_winsys *winsys, bool sw_vk)
{
   UNUSED bool only_sw = env_var_as_boolean("LIBGL_ALWAYS_SOFTWARE", false);
   const char *drivers[] = {
      (sw_vk ? "" : debug_get_option("GALLIUM_DRIVER", "")),
#if defined(GALLIUM_D3D12)
      (sw_vk || only_sw) ? "" : "d3d12",
#endif
#if defined(GALLIUM_ASAHI)
      (sw_vk || only_sw) ? "" : "asahi",
#endif
#if defined(GALLIUM_LLVMPIPE)
      "llvmpipe",
#endif
#if defined(GALLIUM_SOFTPIPE)
      sw_vk ? "" : "softpipe",
#endif
#if defined(GALLIUM_SWR)
      sw_vk ? "" : "swr",
#endif
#if defined(GALLIUM_ZINK)
      (sw_vk || only_sw) ? "" : "zink",
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

struct pipe_screen *
sw_screen_create(struct sw_winsys *winsys)
{
   return sw_screen_create_vk(winsys, false);
}
#endif
