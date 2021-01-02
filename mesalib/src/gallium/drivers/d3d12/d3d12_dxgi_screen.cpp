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

#include "d3d12_screen.h"
#include "d3d12_public.h"

#include "util/debug.h"
#include "util/u_memory.h"
#include "util/u_dl.h"

#include <dxgi1_4.h>

static IDXGIFactory4 *
get_dxgi_factory()
{
   static const GUID IID_IDXGIFactory4 = {
      0x1bc6ea02, 0xef36, 0x464f,
      { 0xbf, 0x0c, 0x21, 0xca, 0x39, 0xe5, 0x16, 0x8a }
   };

   typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY)(REFIID riid, void **ppFactory);
   PFN_CREATE_DXGI_FACTORY CreateDXGIFactory;

   util_dl_library *dxgi_mod = util_dl_open(UTIL_DL_PREFIX "dxgi" UTIL_DL_EXT);
   if (!dxgi_mod) {
      debug_printf("D3D12: failed to load DXGI.DLL\n");
      return NULL;
   }

   CreateDXGIFactory = (PFN_CREATE_DXGI_FACTORY)util_dl_get_proc_address(dxgi_mod, "CreateDXGIFactory");
   if (!CreateDXGIFactory) {
      debug_printf("D3D12: failed to load CreateDXGIFactory from DXGI.DLL\n");
      return NULL;
   }

   IDXGIFactory4 *factory = NULL;
   HRESULT hr = CreateDXGIFactory(IID_IDXGIFactory4, (void **)&factory);
   if (FAILED(hr)) {
      debug_printf("D3D12: CreateDXGIFactory failed: %08x\n", hr);
      return NULL;
   }

   return factory;
}

static IDXGIAdapter1 *
choose_dxgi_adapter(IDXGIFactory4 *factory, LUID *adapter)
{
   IDXGIAdapter1 *ret;
   if (adapter) {
      if (SUCCEEDED(factory->EnumAdapterByLuid(*adapter,
                                               IID_PPV_ARGS(&ret))))
         return ret;
      debug_printf("D3D12: requested adapter missing, falling back to auto-detection...\n");
   }

   bool want_warp = env_var_as_boolean("LIBGL_ALWAYS_SOFTWARE", false);
   if (want_warp) {
      if (SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&ret))))
         return ret;
      debug_printf("D3D12: failed to enum warp adapter\n");
      return NULL;
   }

   // The first adapter is the default
   if (SUCCEEDED(factory->EnumAdapters1(0, &ret)))
      return ret;

   return NULL;
}

static const char *
dxgi_get_name(struct pipe_screen *screen)
{
   struct d3d12_dxgi_screen *dxgi_screen = d3d12_dxgi_screen(d3d12_screen(screen));
   static char buf[1000];
   if (dxgi_screen->description[0] == L'\0')
      return "D3D12 (Unknown)";

   snprintf(buf, sizeof(buf), "D3D12 (%S)", dxgi_screen->description);
   return buf;
}

struct pipe_screen *
d3d12_create_dxgi_screen(struct sw_winsys *winsys, LUID *adapter_luid)
{
   struct d3d12_dxgi_screen *screen = CALLOC_STRUCT(d3d12_dxgi_screen);
   if (!screen)
      return nullptr;

   screen->factory = get_dxgi_factory();
   if (!screen->factory) {
      FREE(screen);
      return nullptr;
   }

   screen->adapter = choose_dxgi_adapter(screen->factory, adapter_luid);
   if (!screen->adapter) {
      debug_printf("D3D12: no suitable adapter\n");
      FREE(screen);
      return nullptr;
   }

   DXGI_ADAPTER_DESC1 adapter_desc = {};
   if (FAILED(screen->adapter->GetDesc1(&adapter_desc))) {
      debug_printf("D3D12: failed to retrieve adapter description\n");
      FREE(screen);
      return nullptr;
   }

   screen->base.vendor_id = adapter_desc.VendorId;
   // Note: memory sizes in bytes, but stored in size_t, so may be capped at 4GB.
   // In that case, adding before conversion to MB can easily overflow.
   screen->base.memory_size_megabytes = (adapter_desc.DedicatedVideoMemory >> 20) +
                                        (adapter_desc.DedicatedSystemMemory >> 20) +
                                        (adapter_desc.SharedSystemMemory >> 20);
   wcsncpy(screen->description, adapter_desc.Description, ARRAY_SIZE(screen->description));
   screen->base.base.get_name = dxgi_get_name;

   if (!d3d12_init_screen(&screen->base, winsys, screen->adapter)) {
      debug_printf("D3D12: failed to initialize DXGI screen\n");
      FREE(screen);
      return nullptr;
   }

   return &screen->base.base;
}
