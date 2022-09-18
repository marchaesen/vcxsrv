/*
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

#include <GL/gl.h> /* dri_interface needs GL types */
#include <GL/internal/dri_interface.h>

#include "drm-uapi/drm_fourcc.h"
#include "loader_dri_helper.h"
#include "util/driconf.h"

__DRIimage *loader_dri_create_image(__DRIscreen *screen,
                                    const __DRIimageExtension *image,
                                    uint32_t width, uint32_t height,
                                    uint32_t dri_format, uint32_t dri_usage,
                                    const uint64_t *modifiers,
                                    unsigned int modifiers_count,
                                    void *loaderPrivate)
{
   if (modifiers && modifiers_count > 0 &&
       image->base.version > 14 && image->createImageWithModifiers) {
      bool has_valid_modifier = false;
      int i;

      /* It's acceptable to create an image with INVALID modifier in the list,
       * but it cannot be on the only modifier (since it will certainly fail
       * later). While we could easily catch this after modifier creation, doing
       * the check here is a convenient debug check likely pointing at whatever
       * interface the client is using to build its modifier list.
       */
      for (i = 0; i < modifiers_count; i++) {
         if (modifiers[i] != DRM_FORMAT_MOD_INVALID) {
            has_valid_modifier = true;
            break;
         }
      }
      if (!has_valid_modifier)
         return NULL;

      if (image->base.version >= 19 && image->createImageWithModifiers2)
         return image->createImageWithModifiers2(screen, width, height,
                                                 dri_format, modifiers,
                                                 modifiers_count, dri_usage,
                                                 loaderPrivate);
      else
         return image->createImageWithModifiers(screen, width, height,
                                                dri_format, modifiers,
                                                modifiers_count, loaderPrivate);
   }

   /* No modifier given or fallback to the legacy createImage allowed */
   return image->createImage(screen, width, height, dri_format, dri_usage,
                             loaderPrivate);
}

static int dri_vblank_mode(__DRIscreen *driScreen, const __DRI2configQueryExtension *config)
{
   GLint vblank_mode = DRI_CONF_VBLANK_DEF_INTERVAL_1;

   if (config)
      config->configQueryi(driScreen, "vblank_mode", &vblank_mode);

   return vblank_mode;
}

int dri_get_initial_swap_interval(__DRIscreen *driScreen,
                                  const __DRI2configQueryExtension *config)
{
   int vblank_mode = dri_vblank_mode(driScreen, config);

   switch (vblank_mode) {
   case DRI_CONF_VBLANK_NEVER:
   case DRI_CONF_VBLANK_DEF_INTERVAL_0:
      return 0;
   case DRI_CONF_VBLANK_DEF_INTERVAL_1:
   case DRI_CONF_VBLANK_ALWAYS_SYNC:
   default:
      return 1;
   }
}

bool dri_valid_swap_interval(__DRIscreen *driScreen,
                             const __DRI2configQueryExtension *config, int interval)
{
   int vblank_mode = dri_vblank_mode(driScreen, config);

   switch (vblank_mode) {
   case DRI_CONF_VBLANK_NEVER:
      if (interval != 0)
         return false;
      break;
   case DRI_CONF_VBLANK_ALWAYS_SYNC:
      if (interval <= 0)
         return false;
      break;
   default:
      break;
   }

   return true;
}
