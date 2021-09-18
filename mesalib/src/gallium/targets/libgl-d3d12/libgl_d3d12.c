/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */


#include <windows.h>

#include "util/u_debug.h"
#include "stw_winsys.h"
#include "stw_device.h"
#include "gdi/gdi_sw_winsys.h"

#include "softpipe/sp_texture.h"
#include "softpipe/sp_screen.h"
#include "softpipe/sp_public.h"

#ifndef GALLIUM_D3D12
#error "This file must be compiled only with the D3D12 driver"
#endif
#include "d3d12/wgl/d3d12_wgl_public.h"

static struct pipe_screen *
gdi_screen_create(HDC hDC)
{
   struct pipe_screen *screen = NULL;
   struct sw_winsys *winsys;

   winsys = gdi_create_sw_winsys();
   if(!winsys)
      goto no_winsys;

   screen = d3d12_wgl_create_screen( winsys, hDC );

   if(!screen)
      goto no_screen;

   return screen;

no_screen:
   winsys->destroy(winsys);
no_winsys:
   return NULL;
}


static void
gdi_present(struct pipe_screen *screen,
            struct pipe_context *context,
            struct pipe_resource *res,
            HDC hDC)
{
   d3d12_wgl_present(screen, context, res, hDC);
}


static boolean
gdi_get_adapter_luid(struct pipe_screen *screen,
                     HDC hDC,
                     LUID *adapter_luid)
{
   if (!stw_dev || !stw_dev->callbacks.pfnGetAdapterLuid)
      return false;

   stw_dev->callbacks.pfnGetAdapterLuid(hDC, adapter_luid);
   return true;
}


static unsigned
gdi_get_pfd_flags(struct pipe_screen *screen)
{
   return d3d12_wgl_get_pfd_flags(screen);
}


static struct stw_winsys_framebuffer *
gdi_create_framebuffer(struct pipe_screen *screen,
                       HWND hWnd,
                       int iPixelFormat)
{
   return d3d12_wgl_create_framebuffer(screen, hWnd, iPixelFormat);
}

static const char *
get_name(void)
{
   return "d3d12";
}

static const struct stw_winsys stw_winsys = {
   &gdi_screen_create,
   &gdi_present,
   &gdi_get_adapter_luid,
   NULL, /* shared_surface_open */
   NULL, /* shared_surface_close */
   NULL, /* compose */
   &gdi_get_pfd_flags,
   &gdi_create_framebuffer,
   &get_name,
};


EXTERN_C BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved);


BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
   switch (fdwReason) {
   case DLL_PROCESS_ATTACH:
      stw_init(&stw_winsys);
      stw_init_thread();
      break;

   case DLL_THREAD_ATTACH:
      stw_init_thread();
      break;

   case DLL_THREAD_DETACH:
      stw_cleanup_thread();
      break;

   case DLL_PROCESS_DETACH:
      if (lpvReserved == NULL) {
         // We're being unloaded from the process.
         stw_cleanup_thread();
         stw_cleanup();
      } else {
         // Process itself is terminating, and all threads and modules are
         // being detached.
         //
         // The order threads (including llvmpipe rasterizer threads) are
         // destroyed can not be relied up, so it's not safe to cleanup.
         //
         // However global destructors (e.g., LLVM's) will still be called, and
         // if Microsoft OPENGL32.DLL's DllMain is called after us, it will
         // still try to invoke DrvDeleteContext to destroys all outstanding,
         // so set stw_dev to NULL to return immediately if that happens.
         stw_dev = NULL;
      }
      break;
   }
   return TRUE;
}
