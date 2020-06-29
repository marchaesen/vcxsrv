/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
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
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include <windows.h>

#include "glapi/glapi.h"
#include "util/u_debug.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "pipe/p_screen.h"

#include "stw_device.h"
#include "stw_winsys.h"
#include "stw_pixelformat.h"
#include "gldrv.h"
#include "stw_tls.h"
#include "stw_framebuffer.h"
#include "stw_st.h"


struct stw_device *stw_dev = NULL;

static int
stw_get_param(struct st_manager *smapi,
              enum st_manager_param param)
{
   switch (param) {
   case ST_MANAGER_BROKEN_INVALIDATE:
      /*
       * Force framebuffer validation on glViewport.
       *
       * Certain applications, like Rhinoceros 4, uses glReadPixels
       * exclusively (never uses SwapBuffers), so framebuffers never get
       * resized unless we check on glViewport.
       */
      return 1;
   default:
      return 0;
   }
}


/** Get the refresh rate for the monitor, in Hz */
static int
get_refresh_rate(void)
{
   DEVMODE devModes;

   if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &devModes)) {
      /* clamp the value, just in case we get garbage */
      return CLAMP(devModes.dmDisplayFrequency, 30, 120);
   }
   else {
      /* reasonable default */
      return 60;
   }
}

static bool
init_screen(const struct stw_winsys *stw_winsys)
{
   struct pipe_screen *screen = stw_winsys->create_screen();
   if (!screen)
      return false;

   if (stw_winsys->get_adapter_luid)
      stw_winsys->get_adapter_luid(screen, &stw_dev->AdapterLuid);

   stw_dev->smapi->screen = screen;
   stw_dev->screen = screen;

   stw_dev->max_2d_length = screen->get_param(screen,
                                              PIPE_CAP_MAX_TEXTURE_2D_SIZE);
   return true;
}

boolean
stw_init(const struct stw_winsys *stw_winsys)
{
   static struct stw_device stw_dev_storage;

   debug_disable_error_message_boxes();

   assert(!stw_dev);

   stw_tls_init();

   stw_dev = &stw_dev_storage;
   memset(stw_dev, 0, sizeof(*stw_dev));

   stw_dev->stw_winsys = stw_winsys;

   stw_dev->stapi = stw_st_create_api();
   stw_dev->smapi = CALLOC_STRUCT(st_manager);
   if (!stw_dev->stapi || !stw_dev->smapi)
      goto error1;

   stw_dev->smapi->get_param = stw_get_param;

   InitializeCriticalSection(&stw_dev->screen_mutex);
   InitializeCriticalSection(&stw_dev->ctx_mutex);
   InitializeCriticalSection(&stw_dev->fb_mutex);

   stw_dev->ctx_table = handle_table_create();
   if (!stw_dev->ctx_table) {
      goto error1;
   }

   /* env var override for WGL_EXT_swap_control, useful for testing/debugging */
   const char *s = os_get_option("WGL_SWAP_INTERVAL");
   if (s) {
      stw_dev->swap_interval = atoi(s);
   }
   stw_dev->refresh_rate = get_refresh_rate();

   stw_dev->initialized = true;

   return TRUE;

error1:
   FREE(stw_dev->smapi);
   if (stw_dev->stapi)
      stw_dev->stapi->destroy(stw_dev->stapi);

   stw_dev = NULL;
   return FALSE;
}

boolean
stw_init_screen()
{
   EnterCriticalSection(&stw_dev->screen_mutex);

   if (!stw_dev->screen_initialized) {
      stw_dev->screen_initialized = true;
      if (!init_screen(stw_dev->stw_winsys)) {
         LeaveCriticalSection(&stw_dev->screen_mutex);
         return false;
      }
      stw_pixelformat_init();
   }

   LeaveCriticalSection(&stw_dev->screen_mutex);
   return stw_dev->screen != NULL;
}

boolean
stw_init_thread(void)
{
   return stw_tls_init_thread();
}


void
stw_cleanup_thread(void)
{
   stw_tls_cleanup_thread();
}


void
stw_cleanup(void)
{
   DHGLRC dhglrc;

   debug_printf("%s\n", __FUNCTION__);

   if (!stw_dev)
      return;

   /*
    * Abort cleanup if there are still active contexts. In some situations
    * this DLL may be unloaded before the DLL that is using GL contexts is.
    */
   stw_lock_contexts(stw_dev);
   dhglrc = handle_table_get_first_handle(stw_dev->ctx_table);
   stw_unlock_contexts(stw_dev);
   if (dhglrc) {
      debug_printf("%s: contexts still active -- cleanup aborted\n", __FUNCTION__);
      stw_dev = NULL;
      return;
   }

   handle_table_destroy(stw_dev->ctx_table);

   stw_framebuffer_cleanup();

   DeleteCriticalSection(&stw_dev->fb_mutex);
   DeleteCriticalSection(&stw_dev->ctx_mutex);
   DeleteCriticalSection(&stw_dev->screen_mutex);

   if (stw_dev->smapi->destroy)
      stw_dev->smapi->destroy(stw_dev->smapi);

   FREE(stw_dev->smapi);
   stw_dev->stapi->destroy(stw_dev->stapi);

   stw_dev->screen->destroy(stw_dev->screen);

   /* glapi is statically linked: we can call the local destroy function. */
#ifdef _GLAPI_NO_EXPORTS
   _glapi_destroy_multithread();
#endif

   stw_tls_cleanup();

   stw_dev = NULL;
}


void APIENTRY
DrvSetCallbackProcs(INT nProcs, PROC *pProcs)
{
   size_t size;

   if (stw_dev == NULL)
      return;

   size = MIN2(nProcs * sizeof *pProcs, sizeof stw_dev->callbacks);
   memcpy(&stw_dev->callbacks, pProcs, size);

   return;
}


BOOL APIENTRY
DrvValidateVersion(ULONG ulVersion)
{
   /* ulVersion is the version reported by the KMD:
    * - via D3DKMTQueryAdapterInfo(KMTQAITYPE_UMOPENGLINFO) on WDDM,
    * - or ExtEscape on XPDM and can be used to ensure the KMD and OpenGL ICD
    *   versions match.
    *
    * We should get the expected version number from the winsys, but for now
    * ignore it.
    */
   (void)ulVersion;
   return TRUE;
}
