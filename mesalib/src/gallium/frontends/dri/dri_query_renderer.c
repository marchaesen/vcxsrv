#include "dri_query_renderer.h"

#include "util/u_inlines.h"
#include "frontend/drm_driver.h"

#include "utils.h"
#include "dri_screen.h"
#include "dri_query_renderer.h"
#include "pipe-loader/pipe_loader.h"

static int
dri2_query_renderer_integer(__DRIscreen *_screen, int param,
                            unsigned int *value)
{
   struct dri_screen *screen = dri_screen(_screen);

   switch (param) {
   case __DRI2_RENDERER_VENDOR_ID:
      value[0] =
         (unsigned int)screen->base.screen->get_param(screen->base.screen,
                                                      PIPE_CAP_VENDOR_ID);
      return 0;
   case __DRI2_RENDERER_DEVICE_ID:
      value[0] =
         (unsigned int)screen->base.screen->get_param(screen->base.screen,
                                                      PIPE_CAP_DEVICE_ID);
      return 0;
   case __DRI2_RENDERER_ACCELERATED:
      value[0] =
         (unsigned int)screen->base.screen->get_param(screen->base.screen,
                                                      PIPE_CAP_ACCELERATED);
      return 0;

   case __DRI2_RENDERER_VIDEO_MEMORY: {
      int ov = driQueryOptioni(&screen->dev->option_cache, "override_vram_size");
      value[0] =
         (unsigned int)screen->base.screen->get_param(screen->base.screen,
                                                      PIPE_CAP_VIDEO_MEMORY);
      if (ov >= 0)
         value[0] = MIN2(ov, value[0]);
      return 0;
   }

   case __DRI2_RENDERER_UNIFIED_MEMORY_ARCHITECTURE:
      value[0] =
         (unsigned int)screen->base.screen->get_param(screen->base.screen,
                                                      PIPE_CAP_UMA);
      return 0;

   case __DRI2_RENDERER_HAS_TEXTURE_3D:
      value[0] =
         screen->base.screen->get_param(screen->base.screen,
                                        PIPE_CAP_MAX_TEXTURE_3D_LEVELS) != 0;
      return 0;

   case __DRI2_RENDERER_HAS_FRAMEBUFFER_SRGB:
      value[0] =
         screen->base.screen->is_format_supported(screen->base.screen,
                                                  PIPE_FORMAT_B8G8R8A8_SRGB,
                                                  PIPE_TEXTURE_2D, 0, 0,
                                                  PIPE_BIND_RENDER_TARGET);
      return 0;
   case __DRI2_RENDERER_HAS_CONTEXT_PRIORITY:
      value[0] =
         screen->base.screen->get_param(screen->base.screen,
                                        PIPE_CAP_CONTEXT_PRIORITY_MASK);
      if (!value[0])
         return -1;
      return 0;
   case __DRI2_RENDERER_HAS_PROTECTED_CONTENT:
      value[0] =
         screen->base.screen->get_param(screen->base.screen,
                                        PIPE_CAP_DEVICE_PROTECTED_CONTENT);
      if (!value[0])
         return -1;
      return 0;
   default:
      return driQueryRendererIntegerCommon(_screen, param, value);
   }
}

static int
dri2_query_renderer_string(__DRIscreen *_screen, int param,
                           const char **value)
{
   struct dri_screen *screen = dri_screen(_screen);

   switch (param) {
   case __DRI2_RENDERER_VENDOR_ID:
      value[0] = screen->base.screen->get_vendor(screen->base.screen);
      return 0;
   case __DRI2_RENDERER_DEVICE_ID:
      value[0] = screen->base.screen->get_name(screen->base.screen);
      return 0;
   default:
      return -1;
   }
}

const __DRI2rendererQueryExtension dri2RendererQueryExtension = {
    .base = { __DRI2_RENDERER_QUERY, 1 },

    .queryInteger         = dri2_query_renderer_integer,
    .queryString          = dri2_query_renderer_string
};
