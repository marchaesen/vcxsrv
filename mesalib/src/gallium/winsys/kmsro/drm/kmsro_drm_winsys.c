/*
 * Copyright (C) 2016 Christian Gmeiner <christian.gmeiner@gmail.com>
 * Copyright (C) 2017 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <fcntl.h>
#include <unistd.h>

#include "kmsro_drm_public.h"
#include "v3d/drm/v3d_drm_public.h"
#include "vc4/drm/vc4_drm_public.h"
#include "etnaviv/drm/etnaviv_drm_public.h"
#include "freedreno/drm/freedreno_drm_public.h"
#include "panfrost/drm/panfrost_drm_public.h"
#include "lima/drm/lima_drm_public.h"
#include "xf86drm.h"

#include "pipe/p_screen.h"
#include "renderonly/renderonly.h"

struct pipe_screen *kmsro_drm_screen_create(int fd,
                                            const struct pipe_screen_config *config)
{
   struct pipe_screen *screen = NULL;
   struct renderonly ro = {
      .kms_fd = fd,
      .gpu_fd = -1,
   };

#if defined(GALLIUM_VC4)
   ro.gpu_fd = drmOpenWithType("vc4", NULL, DRM_NODE_RENDER);
   if (ro.gpu_fd >= 0) {
      /* Passes the vc4-allocated BO through to the KMS-only DRM device using
       * PRIME buffer sharing.  The VC4 BO must be linear, which the SCANOUT
       * flag on allocation will have ensured.
       */
      ro.create_for_resource = renderonly_create_gpu_import_for_resource,
      screen = vc4_drm_screen_create_renderonly(&ro, config);
      if (!screen)
         close(ro.gpu_fd);

      return screen;
   }
#endif

#if defined(GALLIUM_ETNAVIV)
   ro.gpu_fd = drmOpenWithType("etnaviv", NULL, DRM_NODE_RENDER);
   if (ro.gpu_fd >= 0) {
      ro.create_for_resource = renderonly_create_kms_dumb_buffer_for_resource,
      screen = etna_drm_screen_create_renderonly(&ro);
      if (!screen)
         close(ro.gpu_fd);

      return screen;
   }
#endif

#if defined(GALLIUM_FREEDRENO)
   ro.gpu_fd = drmOpenWithType("msm", NULL, DRM_NODE_RENDER);
   if (ro.gpu_fd >= 0) {
      ro.create_for_resource = renderonly_create_kms_dumb_buffer_for_resource,
      screen = fd_drm_screen_create(ro.gpu_fd, &ro);
      if (!screen)
         close(ro.gpu_fd);

      return screen;
   }
#endif

#if defined(GALLIUM_PANFROST)
   ro.gpu_fd = drmOpenWithType("panfrost", NULL, DRM_NODE_RENDER);

   if (ro.gpu_fd >= 0) {
      ro.create_for_resource = renderonly_create_kms_dumb_buffer_for_resource,
      screen = panfrost_drm_screen_create_renderonly(&ro);
      if (!screen)
         close(ro.gpu_fd);

      return screen;
   }
#endif

#if defined(GALLIUM_LIMA)
   ro.gpu_fd = drmOpenWithType("lima", NULL, DRM_NODE_RENDER);
   if (ro.gpu_fd >= 0) {
      ro.create_for_resource = renderonly_create_kms_dumb_buffer_for_resource,
      screen = lima_drm_screen_create_renderonly(&ro);
      if (!screen)
         close(ro.gpu_fd);

      return screen;
   }
#endif

#if defined(GALLIUM_V3D)
   ro.gpu_fd = drmOpenWithType("v3d", NULL, DRM_NODE_RENDER);
   if (ro.gpu_fd >= 0) {
      ro.create_for_resource = renderonly_create_kms_dumb_buffer_for_resource,
      screen = v3d_drm_screen_create_renderonly(&ro, config);
      if (!screen)
         close(ro.gpu_fd);

      return screen;
   }
#endif

   return screen;
}
