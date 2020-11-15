/**************************************************************************
 *
 * Copyright 2008-2009 VMware, Inc.
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

#include "pipe/p_screen.h"
#include "util/u_memory.h"
#include "hud/hud_context.h"
#include "util/os_time.h"
#include "frontend/api.h"

#include <GL/gl.h>
#include "gldrv.h"
#include "stw_framebuffer.h"
#include "stw_device.h"
#include "stw_winsys.h"
#include "stw_tls.h"
#include "stw_context.h"
#include "stw_st.h"


/**
 * Search the framebuffer with the matching HWND while holding the
 * stw_dev::fb_mutex global lock.
 * If a stw_framebuffer is found, lock it and return the pointer.
 * Else, return NULL.
 */
static struct stw_framebuffer *
stw_framebuffer_from_hwnd_locked(HWND hwnd)
{
   struct stw_framebuffer *fb;

   for (fb = stw_dev->fb_head; fb != NULL; fb = fb->next)
      if (fb->hWnd == hwnd) {
         stw_framebuffer_lock(fb);
         assert(fb->mutex.RecursionCount == 1);
         return fb;
      }

   return NULL;
}


/**
 * Decrement the reference count on the given stw_framebuffer object.
 * If the reference count hits zero, destroy the object.
 *
 * Note: Both stw_dev::fb_mutex and stw_framebuffer::mutex must already be
 * locked.  After this function completes, the fb's mutex will be unlocked.
 */
void
stw_framebuffer_release_locked(struct stw_framebuffer *fb)
{
   struct stw_framebuffer **link;

   assert(fb);
   assert(stw_own_mutex(&fb->mutex));
   assert(stw_own_mutex(&stw_dev->fb_mutex));

   /* check the reference count */
   fb->refcnt--;
   if (fb->refcnt) {
      stw_framebuffer_unlock(fb);
      return;
   }

   /* remove this stw_framebuffer from the device's linked list */
   link = &stw_dev->fb_head;
   while (*link != fb)
      link = &(*link)->next;
   assert(*link);
   *link = fb->next;
   fb->next = NULL;

   if (fb->shared_surface)
      stw_dev->stw_winsys->shared_surface_close(stw_dev->screen,
                                                fb->shared_surface);

   stw_st_destroy_framebuffer_locked(fb->stfb);

   stw_framebuffer_unlock(fb);

   DeleteCriticalSection(&fb->mutex);

   FREE( fb );
}


/**
 * Query the size of the given framebuffer's on-screen window and update
 * the stw_framebuffer's width/height.
 */
static void
stw_framebuffer_get_size(struct stw_framebuffer *fb)
{
   LONG width, height;
   RECT client_rect;
   RECT window_rect;
   POINT client_pos;

   /*
    * Sanity checking.
    */
   assert(fb->hWnd);
   assert(fb->width && fb->height);
   assert(fb->client_rect.right  == fb->client_rect.left + fb->width);
   assert(fb->client_rect.bottom == fb->client_rect.top  + fb->height);

   /*
    * Get the client area size.
    */
   if (!GetClientRect(fb->hWnd, &client_rect)) {
      return;
   }

   assert(client_rect.left == 0);
   assert(client_rect.top == 0);
   width  = client_rect.right  - client_rect.left;
   height = client_rect.bottom - client_rect.top;

   fb->minimized = width == 0 || height == 0;

   if (width <= 0 || height <= 0) {
      /*
       * When the window is minimized GetClientRect will return zeros.  Simply
       * preserve the current window size, until the window is restored or
       * maximized again.
       */
      return;
   }

   if (width != fb->width || height != fb->height) {
      fb->must_resize = TRUE;
      fb->width = width;
      fb->height = height;
   }

   client_pos.x = 0;
   client_pos.y = 0;
   if (ClientToScreen(fb->hWnd, &client_pos) &&
       GetWindowRect(fb->hWnd, &window_rect)) {
      fb->client_rect.left = client_pos.x - window_rect.left;
      fb->client_rect.top  = client_pos.y - window_rect.top;
   }

   fb->client_rect.right  = fb->client_rect.left + fb->width;
   fb->client_rect.bottom = fb->client_rect.top  + fb->height;

#if 0
   debug_printf("\n");
   debug_printf("%s: hwnd = %p\n", __FUNCTION__, fb->hWnd);
   debug_printf("%s: client_position = (%li, %li)\n",
                __FUNCTION__, client_pos.x, client_pos.y);
   debug_printf("%s: window_rect = (%li, %li) - (%li, %li)\n",
                __FUNCTION__,
                window_rect.left, window_rect.top,
                window_rect.right, window_rect.bottom);
   debug_printf("%s: client_rect = (%li, %li) - (%li, %li)\n",
                __FUNCTION__,
                fb->client_rect.left, fb->client_rect.top,
                fb->client_rect.right, fb->client_rect.bottom);
#endif
}


/**
 * @sa http://msdn.microsoft.com/en-us/library/ms644975(VS.85).aspx
 * @sa http://msdn.microsoft.com/en-us/library/ms644960(VS.85).aspx
 */
LRESULT CALLBACK
stw_call_window_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
   struct stw_tls_data *tls_data;
   PCWPSTRUCT pParams = (PCWPSTRUCT)lParam;
   struct stw_framebuffer *fb;

   tls_data = stw_tls_get_data();
   if (!tls_data)
      return 0;

   if (nCode < 0 || !stw_dev)
       return CallNextHookEx(tls_data->hCallWndProcHook, nCode, wParam, lParam);

   /* We check that the stw_dev object is initialized before we try to do
    * anything with it.  Otherwise, in multi-threaded programs there's a
    * chance of executing this code before the stw_dev object is fully
    * initialized.
    */
   if (stw_dev && stw_dev->initialized) {
      if (pParams->message == WM_WINDOWPOSCHANGED) {
         /* We handle WM_WINDOWPOSCHANGED instead of WM_SIZE because according
          * to http://blogs.msdn.com/oldnewthing/archive/2008/01/15/7113860.aspx
          * WM_SIZE is generated from WM_WINDOWPOSCHANGED by DefWindowProc so it
          * can be masked out by the application.
          */
         LPWINDOWPOS lpWindowPos = (LPWINDOWPOS)pParams->lParam;
         if ((lpWindowPos->flags & SWP_SHOWWINDOW) ||
             !(lpWindowPos->flags & SWP_NOMOVE) ||
             !(lpWindowPos->flags & SWP_NOSIZE)) {
            fb = stw_framebuffer_from_hwnd( pParams->hwnd );
            if (fb) {
               /* Size in WINDOWPOS includes the window frame, so get the size
                * of the client area via GetClientRect.
                */
               stw_framebuffer_get_size(fb);
               stw_framebuffer_unlock(fb);
            }
         }
      }
      else if (pParams->message == WM_DESTROY) {
         stw_lock_framebuffers(stw_dev);
         fb = stw_framebuffer_from_hwnd_locked( pParams->hwnd );
         if (fb)
            stw_framebuffer_release_locked(fb);
         stw_unlock_framebuffers(stw_dev);
      }
   }

   return CallNextHookEx(tls_data->hCallWndProcHook, nCode, wParam, lParam);
}


/**
 * Create a new stw_framebuffer object which corresponds to the given
 * HDC/window.  If successful, we return the new stw_framebuffer object
 * with its mutex locked.
 */
struct stw_framebuffer *
stw_framebuffer_create(HDC hdc, int iPixelFormat)
{
   HWND hWnd;
   struct stw_framebuffer *fb;
   const struct stw_pixelformat_info *pfi;

   /* We only support drawing to a window. */
   hWnd = WindowFromDC( hdc );
   if (!hWnd)
      return NULL;

   fb = CALLOC_STRUCT( stw_framebuffer );
   if (fb == NULL)
      return NULL;

   fb->hWnd = hWnd;
   fb->iPixelFormat = iPixelFormat;

   /*
    * We often need a displayable pixel format to make GDI happy. Set it
    * here (always 1, i.e., out first pixel format) where appropriate.
    */
   fb->iDisplayablePixelFormat = iPixelFormat <= stw_dev->pixelformat_count
      ? iPixelFormat : 1;

   fb->pfi = pfi = stw_pixelformat_get_info( iPixelFormat );
   fb->stfb = stw_st_create_framebuffer( fb );
   if (!fb->stfb) {
      FREE( fb );
      return NULL;
   }

   fb->refcnt = 1;

   /*
    * Windows can be sometimes have zero width and or height, but we ensure
    * a non-zero framebuffer size at all times.
    */

   fb->must_resize = TRUE;
   fb->width  = 1;
   fb->height = 1;
   fb->client_rect.left   = 0;
   fb->client_rect.top    = 0;
   fb->client_rect.right  = fb->client_rect.left + fb->width;
   fb->client_rect.bottom = fb->client_rect.top  + fb->height;

   stw_framebuffer_get_size(fb);

   InitializeCriticalSection(&fb->mutex);

   /* This is the only case where we lock the stw_framebuffer::mutex before
    * stw_dev::fb_mutex, since no other thread can know about this framebuffer
    * and we must prevent any other thread from destroying it before we return.
    */
   stw_framebuffer_lock(fb);

   stw_lock_framebuffers(stw_dev);
   fb->next = stw_dev->fb_head;
   stw_dev->fb_head = fb;
   stw_unlock_framebuffers(stw_dev);

   return fb;
}


/**
 * Update the framebuffer's size if necessary.
 */
void
stw_framebuffer_update(struct stw_framebuffer *fb)
{
   assert(fb->stfb);
   assert(fb->height);
   assert(fb->width);

   /* XXX: It would be nice to avoid checking the size again -- in theory
    * stw_call_window_proc would have cought the resize and stored the right
    * size already, but unfortunately threads created before the DllMain is
    * called don't get a DLL_THREAD_ATTACH notification, and there is no way
    * to know of their existing without using the not very portable PSAPI.
    */
   stw_framebuffer_get_size(fb);
}


/**
 * Try to free all stw_framebuffer objects associated with the device.
 */
void
stw_framebuffer_cleanup(void)
{
   struct stw_framebuffer *fb;
   struct stw_framebuffer *next;

   if (!stw_dev)
      return;

   stw_lock_framebuffers(stw_dev);

   fb = stw_dev->fb_head;
   while (fb) {
      next = fb->next;

      stw_framebuffer_lock(fb);
      stw_framebuffer_release_locked(fb);

      fb = next;
   }
   stw_dev->fb_head = NULL;

   stw_unlock_framebuffers(stw_dev);
}


/**
 * Given an hdc, return the corresponding stw_framebuffer.
 * The returned stw_framebuffer will have its mutex locked.
 */
static struct stw_framebuffer *
stw_framebuffer_from_hdc_locked(HDC hdc)
{
   HWND hwnd;

   hwnd = WindowFromDC(hdc);
   if (!hwnd) {
      return NULL;
   }

   return stw_framebuffer_from_hwnd_locked(hwnd);
}


/**
 * Given an HDC, return the corresponding stw_framebuffer.
 * The returned stw_framebuffer will have its mutex locked.
 */
struct stw_framebuffer *
stw_framebuffer_from_hdc(HDC hdc)
{
   struct stw_framebuffer *fb;

   if (!stw_dev)
      return NULL;

   stw_lock_framebuffers(stw_dev);
   fb = stw_framebuffer_from_hdc_locked(hdc);
   stw_unlock_framebuffers(stw_dev);

   return fb;
}


/**
 * Given an HWND, return the corresponding stw_framebuffer.
 * The returned stw_framebuffer will have its mutex locked.
 */
struct stw_framebuffer *
stw_framebuffer_from_hwnd(HWND hwnd)
{
   struct stw_framebuffer *fb;

   stw_lock_framebuffers(stw_dev);
   fb = stw_framebuffer_from_hwnd_locked(hwnd);
   stw_unlock_framebuffers(stw_dev);

   return fb;
}


BOOL APIENTRY
DrvSetPixelFormat(HDC hdc, LONG iPixelFormat)
{
   uint count;
   uint index;
   struct stw_framebuffer *fb;

   if (!stw_dev)
      return FALSE;

   index = (uint) iPixelFormat - 1;
   count = stw_pixelformat_get_count();
   if (index >= count)
      return FALSE;

   fb = stw_framebuffer_from_hdc_locked(hdc);
   if (fb) {
      /*
       * SetPixelFormat must be called only once.  However ignore
       * pbuffers, for which the framebuffer object is created first.
       */
      boolean bPbuffer = fb->bPbuffer;

      stw_framebuffer_unlock( fb );

      return bPbuffer;
   }

   fb = stw_framebuffer_create(hdc, iPixelFormat);
   if (!fb) {
      return FALSE;
   }

   stw_framebuffer_unlock( fb );

   /* Some applications mistakenly use the undocumented wglSetPixelFormat
    * function instead of SetPixelFormat, so we call SetPixelFormat here to
    * avoid opengl32.dll's wglCreateContext to fail */
   if (GetPixelFormat(hdc) == 0) {
      BOOL bRet = SetPixelFormat(hdc, iPixelFormat, NULL);
      if (!bRet) {
	  debug_printf("SetPixelFormat failed\n");
      }
   }

   return TRUE;
}


int
stw_pixelformat_get(HDC hdc)
{
   int iPixelFormat = 0;
   struct stw_framebuffer *fb;

   fb = stw_framebuffer_from_hdc(hdc);
   if (fb) {
      iPixelFormat = fb->iPixelFormat;
      stw_framebuffer_unlock(fb);
   }

   return iPixelFormat;
}


BOOL APIENTRY
DrvPresentBuffers(HDC hdc, LPPRESENTBUFFERS data)
{
   struct stw_framebuffer *fb;
   struct pipe_screen *screen;
   struct pipe_resource *res;

   if (!stw_dev)
      return FALSE;

   fb = stw_framebuffer_from_hdc( hdc );
   if (fb == NULL)
      return FALSE;

   screen = stw_dev->screen;

   res = (struct pipe_resource *)data->pPrivData;

   if (data->hSurface != fb->hSharedSurface) {
      if (fb->shared_surface) {
         stw_dev->stw_winsys->shared_surface_close(screen, fb->shared_surface);
         fb->shared_surface = NULL;
      }

      fb->hSharedSurface = data->hSurface;

      if (data->hSurface &&
         stw_dev->stw_winsys->shared_surface_open) {
         fb->shared_surface =
            stw_dev->stw_winsys->shared_surface_open(screen,
                                                     fb->hSharedSurface);
      }
   }

   if (!fb->minimized) {
      if (fb->shared_surface) {
         stw_dev->stw_winsys->compose(screen,
                                      res,
                                      fb->shared_surface,
                                      &fb->client_rect,
                                      data->ullPresentToken);
      }
      else {
         stw_dev->stw_winsys->present( screen, res, hdc );
      }
   }

   stw_framebuffer_update(fb);
   stw_notify_current_locked(fb);

   stw_framebuffer_unlock(fb);

   return TRUE;
}


/**
 * Queue a composition.
 *
 * The stw_framebuffer object must have its mutex locked.  The mutex will
 * be unlocked here before returning.
 */
BOOL
stw_framebuffer_present_locked(HDC hdc,
                               struct stw_framebuffer *fb,
                               struct pipe_resource *res)
{
   if (stw_dev->callbacks.pfnPresentBuffers &&
      stw_dev->stw_winsys->compose) {
      PRESENTBUFFERSCB data;

      memset(&data, 0, sizeof data);
      data.nVersion = 2;
      data.syncType = PRESCB_SYNCTYPE_NONE;
      data.luidAdapter = stw_dev->AdapterLuid;
      data.updateRect = fb->client_rect;
      data.pPrivData = (void *)res;

      stw_notify_current_locked(fb);
      stw_framebuffer_unlock(fb);

      return stw_dev->callbacks.pfnPresentBuffers(hdc, &data);
   }
   else {
      struct pipe_screen *screen = stw_dev->screen;

      stw_dev->stw_winsys->present( screen, res, hdc );

      stw_framebuffer_update(fb);
      stw_notify_current_locked(fb);
      stw_framebuffer_unlock(fb);

      return TRUE;
   }
}


/**
 * This is called just before issuing the buffer swap/present.
 * We query the current time and determine if we should sleep before
 * issuing the swap/present.
 * This is a bit of a hack and is certainly not very accurate but it
 * basically works.
 * This is for the WGL_ARB_swap_interval extension.
 */
static void
wait_swap_interval(struct stw_framebuffer *fb)
{
   /* Note: all time variables here are in units of microseconds */
   int64_t cur_time = os_time_get_nano() / 1000;

   if (fb->prev_swap_time != 0) {
      /* Compute time since previous swap */
      int64_t delta = cur_time - fb->prev_swap_time;
      int64_t min_swap_period =
         1.0e6 / stw_dev->refresh_rate * stw_dev->swap_interval;

      /* If time since last swap is less than wait period, wait.
       * Note that it's possible for the delta to be negative because of
       * rollover.  See https://bugs.freedesktop.org/show_bug.cgi?id=102241
       */
      if ((delta >= 0) && (delta < min_swap_period)) {
         float fudge = 1.75f;  /* emperical fudge factor */
         int64_t wait = (min_swap_period - delta) * fudge;
         os_time_sleep(wait);
      }
   }

   fb->prev_swap_time = cur_time;
}


BOOL APIENTRY
DrvSwapBuffers(HDC hdc)
{
   struct stw_context *ctx;
   struct stw_framebuffer *fb;

   if (!stw_dev)
      return FALSE;

   fb = stw_framebuffer_from_hdc( hdc );
   if (fb == NULL)
      return FALSE;

   if (!(fb->pfi->pfd.dwFlags & PFD_DOUBLEBUFFER)) {
      stw_framebuffer_unlock(fb);
      return TRUE;
   }

   ctx = stw_current_context();
   if (ctx) {
      if (ctx->hud) {
         /* Display the HUD */
         struct pipe_resource *back =
            stw_get_framebuffer_resource(fb->stfb, ST_ATTACHMENT_BACK_LEFT);
         if (back) {
            hud_run(ctx->hud, NULL, back);
         }
      }

      if (ctx->current_framebuffer == fb) {
         /* flush current context */
         ctx->st->flush(ctx->st, ST_FLUSH_END_OF_FRAME, NULL, NULL, NULL);
      }
   }

   if (stw_dev->swap_interval != 0) {
      wait_swap_interval(fb);
   }

   return stw_st_swap_framebuffer_locked(hdc, ctx->st, fb->stfb);
}


BOOL APIENTRY
DrvSwapLayerBuffers(HDC hdc, UINT fuPlanes)
{
   if (fuPlanes & WGL_SWAP_MAIN_PLANE)
      return DrvSwapBuffers(hdc);

   return FALSE;
}
