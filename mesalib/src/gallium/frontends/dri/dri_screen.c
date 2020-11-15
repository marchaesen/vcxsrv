/**************************************************************************
 *
 * Copyright 2009, VMware, Inc.
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
/*
 * Author: Keith Whitwell <keithw@vmware.com>
 * Author: Jakob Bornecrantz <wallbraker@gmail.com>
 */

#include "utils.h"

#include "dri_screen.h"
#include "dri_context.h"
#include "dri_helpers.h"

#include "util/u_inlines.h"
#include "pipe/p_screen.h"
#include "pipe/p_format.h"
#include "pipe-loader/pipe_loader.h"
#include "state_tracker/st_gl_api.h" /* for st_gl_api_create */
#include "frontend/drm_driver.h"

#include "util/u_debug.h"
#include "util/format/u_format_s3tc.h"

#define MSAA_VISUAL_MAX_SAMPLES 32

#undef false

const __DRIconfigOptionsExtension gallium_config_options = {
   .base = { __DRI_CONFIG_OPTIONS, 2 },
   .getXml = pipe_loader_get_driinfo_xml
};

#define false 0

static void
dri_fill_st_options(struct dri_screen *screen)
{
   struct st_config_options *options = &screen->options;
   const struct driOptionCache *optionCache = &screen->dev->option_cache;

   options->disable_blend_func_extended =
      driQueryOptionb(optionCache, "disable_blend_func_extended");
   options->disable_arb_gpu_shader5 =
      driQueryOptionb(optionCache, "disable_arb_gpu_shader5");
   options->disable_glsl_line_continuations =
      driQueryOptionb(optionCache, "disable_glsl_line_continuations");
   options->force_glsl_extensions_warn =
      driQueryOptionb(optionCache, "force_glsl_extensions_warn");
   options->force_glsl_version =
      driQueryOptioni(optionCache, "force_glsl_version");
   options->allow_extra_pp_tokens =
      driQueryOptionb(optionCache, "allow_extra_pp_tokens");
   options->allow_glsl_extension_directive_midshader =
      driQueryOptionb(optionCache, "allow_glsl_extension_directive_midshader");
   options->allow_glsl_120_subset_in_110 =
      driQueryOptionb(optionCache, "allow_glsl_120_subset_in_110");
   options->allow_glsl_builtin_const_expression =
      driQueryOptionb(optionCache, "allow_glsl_builtin_const_expression");
   options->allow_glsl_relaxed_es =
      driQueryOptionb(optionCache, "allow_glsl_relaxed_es");
   options->allow_glsl_builtin_variable_redeclaration =
      driQueryOptionb(optionCache, "allow_glsl_builtin_variable_redeclaration");
   options->allow_higher_compat_version =
      driQueryOptionb(optionCache, "allow_higher_compat_version");
   options->glsl_zero_init = driQueryOptionb(optionCache, "glsl_zero_init");
   options->force_integer_tex_nearest =
      driQueryOptionb(optionCache, "force_integer_tex_nearest");
   options->vs_position_always_invariant =
      driQueryOptionb(optionCache, "vs_position_always_invariant");
   options->force_glsl_abs_sqrt =
      driQueryOptionb(optionCache, "force_glsl_abs_sqrt");
   options->allow_glsl_cross_stage_interpolation_mismatch =
      driQueryOptionb(optionCache, "allow_glsl_cross_stage_interpolation_mismatch");
   options->allow_draw_out_of_order =
      driQueryOptionb(optionCache, "allow_draw_out_of_order");
   options->force_gl_names_reuse =
      driQueryOptionb(optionCache, "force_gl_names_reuse");

   char *vendor_str = driQueryOptionstr(optionCache, "force_gl_vendor");
   /* not an empty string */
   if (*vendor_str)
      options->force_gl_vendor = strdup(vendor_str);

   driComputeOptionsSha1(optionCache, options->config_options_sha1);
}

static unsigned
dri_loader_get_cap(struct dri_screen *screen, enum dri_loader_cap cap)
{
   const __DRIdri2LoaderExtension *dri2_loader = screen->sPriv->dri2.loader;
   const __DRIimageLoaderExtension *image_loader = screen->sPriv->image.loader;

   if (dri2_loader && dri2_loader->base.version >= 4 &&
       dri2_loader->getCapability)
      return dri2_loader->getCapability(screen->sPriv->loaderPrivate, cap);

   if (image_loader && image_loader->base.version >= 2 &&
       image_loader->getCapability)
      return image_loader->getCapability(screen->sPriv->loaderPrivate, cap);

   return 0;
}

static const __DRIconfig **
dri_fill_in_modes(struct dri_screen *screen)
{
   static const mesa_format mesa_formats[] = {
      MESA_FORMAT_B10G10R10A2_UNORM,
      MESA_FORMAT_B10G10R10X2_UNORM,
      MESA_FORMAT_R10G10B10A2_UNORM,
      MESA_FORMAT_R10G10B10X2_UNORM,
      MESA_FORMAT_B8G8R8A8_UNORM,
      MESA_FORMAT_B8G8R8X8_UNORM,
      MESA_FORMAT_B8G8R8A8_SRGB,
      MESA_FORMAT_B8G8R8X8_SRGB,
      MESA_FORMAT_B5G6R5_UNORM,
      MESA_FORMAT_RGBA_FLOAT16,
      MESA_FORMAT_RGBX_FLOAT16,

      /* The 32-bit RGBA format must not precede the 32-bit BGRA format.
       * Likewise for RGBX and BGRX.  Otherwise, the GLX client and the GLX
       * server may disagree on which format the GLXFBConfig represents,
       * resulting in swapped color channels.
       *
       * The problem, as of 2017-05-30:
       * When matching a GLXFBConfig to a __DRIconfig, GLX ignores the channel
       * order and chooses the first __DRIconfig with the expected channel
       * sizes. Specifically, GLX compares the GLXFBConfig's and __DRIconfig's
       * __DRI_ATTRIB_{CHANNEL}_SIZE but ignores __DRI_ATTRIB_{CHANNEL}_MASK.
       *
       * EGL does not suffer from this problem. It correctly compares the
       * channel masks when matching EGLConfig to __DRIconfig.
       */

      /* Required by Android, for HAL_PIXEL_FORMAT_RGBA_8888. */
      MESA_FORMAT_R8G8B8A8_UNORM,

      /* Required by Android, for HAL_PIXEL_FORMAT_RGBX_8888. */
      MESA_FORMAT_R8G8B8X8_UNORM,
   };
   static const enum pipe_format pipe_formats[] = {
      PIPE_FORMAT_B10G10R10A2_UNORM,
      PIPE_FORMAT_B10G10R10X2_UNORM,
      PIPE_FORMAT_R10G10B10A2_UNORM,
      PIPE_FORMAT_R10G10B10X2_UNORM,
      PIPE_FORMAT_BGRA8888_UNORM,
      PIPE_FORMAT_BGRX8888_UNORM,
      PIPE_FORMAT_BGRA8888_SRGB,
      PIPE_FORMAT_BGRX8888_SRGB,
      PIPE_FORMAT_B5G6R5_UNORM,
      PIPE_FORMAT_R16G16B16A16_FLOAT,
      PIPE_FORMAT_R16G16B16X16_FLOAT,
      PIPE_FORMAT_RGBA8888_UNORM,
      PIPE_FORMAT_RGBX8888_UNORM,
   };
   mesa_format format;
   __DRIconfig **configs = NULL;
   uint8_t depth_bits_array[5];
   uint8_t stencil_bits_array[5];
   unsigned depth_buffer_factor;
   unsigned msaa_samples_max;
   unsigned i;
   struct pipe_screen *p_screen = screen->base.screen;
   bool pf_z16, pf_x8z24, pf_z24x8, pf_s8z24, pf_z24s8, pf_z32;
   bool mixed_color_depth;
   bool allow_rgba_ordering;
   bool allow_rgb10;
   bool allow_fp16;

   static const GLenum back_buffer_modes[] = {
      __DRI_ATTRIB_SWAP_NONE, __DRI_ATTRIB_SWAP_UNDEFINED,
      __DRI_ATTRIB_SWAP_COPY
   };

   if (driQueryOptionb(&screen->dev->option_cache, "always_have_depth_buffer")) {
      /* all visuals will have a depth buffer */
      depth_buffer_factor = 0;
   }
   else {
      depth_bits_array[0] = 0;
      stencil_bits_array[0] = 0;
      depth_buffer_factor = 1;
   }

   allow_rgba_ordering = dri_loader_get_cap(screen, DRI_LOADER_CAP_RGBA_ORDERING);
   allow_rgb10 = driQueryOptionb(&screen->dev->option_cache, "allow_rgb10_configs");
   allow_fp16 = driQueryOptionb(&screen->dev->option_cache, "allow_fp16_configs");
   allow_fp16 &= dri_loader_get_cap(screen, DRI_LOADER_CAP_FP16);

   msaa_samples_max = (screen->st_api->feature_mask & ST_API_FEATURE_MS_VISUALS_MASK)
      ? MSAA_VISUAL_MAX_SAMPLES : 1;

   pf_x8z24 = p_screen->is_format_supported(p_screen, PIPE_FORMAT_Z24X8_UNORM,
					    PIPE_TEXTURE_2D, 0, 0,
                                            PIPE_BIND_DEPTH_STENCIL);
   pf_z24x8 = p_screen->is_format_supported(p_screen, PIPE_FORMAT_X8Z24_UNORM,
					    PIPE_TEXTURE_2D, 0, 0,
                                            PIPE_BIND_DEPTH_STENCIL);
   pf_s8z24 = p_screen->is_format_supported(p_screen, PIPE_FORMAT_Z24_UNORM_S8_UINT,
					    PIPE_TEXTURE_2D, 0, 0,
                                            PIPE_BIND_DEPTH_STENCIL);
   pf_z24s8 = p_screen->is_format_supported(p_screen, PIPE_FORMAT_S8_UINT_Z24_UNORM,
					    PIPE_TEXTURE_2D, 0, 0,
                                            PIPE_BIND_DEPTH_STENCIL);
   pf_z16 = p_screen->is_format_supported(p_screen, PIPE_FORMAT_Z16_UNORM,
                                          PIPE_TEXTURE_2D, 0, 0,
                                          PIPE_BIND_DEPTH_STENCIL);
   pf_z32 = p_screen->is_format_supported(p_screen, PIPE_FORMAT_Z32_UNORM,
                                          PIPE_TEXTURE_2D, 0, 0,
                                          PIPE_BIND_DEPTH_STENCIL);

   if (pf_z16) {
      depth_bits_array[depth_buffer_factor] = 16;
      stencil_bits_array[depth_buffer_factor++] = 0;
   }
   if (pf_x8z24 || pf_z24x8) {
      depth_bits_array[depth_buffer_factor] = 24;
      stencil_bits_array[depth_buffer_factor++] = 0;
      screen->d_depth_bits_last = pf_x8z24;
   }
   if (pf_s8z24 || pf_z24s8) {
      depth_bits_array[depth_buffer_factor] = 24;
      stencil_bits_array[depth_buffer_factor++] = 8;
      screen->sd_depth_bits_last = pf_s8z24;
   }
   if (pf_z32) {
      depth_bits_array[depth_buffer_factor] = 32;
      stencil_bits_array[depth_buffer_factor++] = 0;
   }

   mixed_color_depth =
      p_screen->get_param(p_screen, PIPE_CAP_MIXED_COLOR_DEPTH_BITS);

   assert(ARRAY_SIZE(mesa_formats) == ARRAY_SIZE(pipe_formats));

   /* Add configs. */
   for (format = 0; format < ARRAY_SIZE(mesa_formats); format++) {
      __DRIconfig **new_configs = NULL;
      unsigned num_msaa_modes = 0; /* includes a single-sample mode */
      uint8_t msaa_modes[MSAA_VISUAL_MAX_SAMPLES];

      /* Expose only BGRA ordering if the loader doesn't support RGBA ordering. */
      if (!allow_rgba_ordering &&
          (mesa_formats[format] == MESA_FORMAT_R8G8B8A8_UNORM ||
           mesa_formats[format] == MESA_FORMAT_R8G8B8X8_UNORM))
         continue;

      if (!allow_rgb10 &&
          (mesa_formats[format] == MESA_FORMAT_B10G10R10A2_UNORM ||
           mesa_formats[format] == MESA_FORMAT_B10G10R10X2_UNORM ||
           mesa_formats[format] == MESA_FORMAT_R10G10B10A2_UNORM ||
           mesa_formats[format] == MESA_FORMAT_R10G10B10X2_UNORM))
         continue;

      if (!allow_fp16 &&
          (mesa_formats[format] == MESA_FORMAT_RGBA_FLOAT16 ||
           mesa_formats[format] == MESA_FORMAT_RGBX_FLOAT16))
         continue;

      if (!p_screen->is_format_supported(p_screen, pipe_formats[format],
                                         PIPE_TEXTURE_2D, 0, 0,
                                         PIPE_BIND_RENDER_TARGET |
                                         PIPE_BIND_DISPLAY_TARGET))
         continue;

      for (i = 1; i <= msaa_samples_max; i++) {
         int samples = i > 1 ? i : 0;

         if (p_screen->is_format_supported(p_screen, pipe_formats[format],
                                           PIPE_TEXTURE_2D, samples, samples,
                                           PIPE_BIND_RENDER_TARGET)) {
            msaa_modes[num_msaa_modes++] = samples;
         }
      }

      if (num_msaa_modes) {
         /* Single-sample configs with an accumulation buffer. */
         new_configs = driCreateConfigs(mesa_formats[format],
                                        depth_bits_array, stencil_bits_array,
                                        depth_buffer_factor, back_buffer_modes,
                                        ARRAY_SIZE(back_buffer_modes),
                                        msaa_modes, 1,
                                        GL_TRUE, !mixed_color_depth, GL_FALSE);
         configs = driConcatConfigs(configs, new_configs);

         /* Multi-sample configs without an accumulation buffer. */
         if (num_msaa_modes > 1) {
            new_configs = driCreateConfigs(mesa_formats[format],
                                           depth_bits_array, stencil_bits_array,
                                           depth_buffer_factor, back_buffer_modes,
                                           ARRAY_SIZE(back_buffer_modes),
                                           msaa_modes+1, num_msaa_modes-1,
                                           GL_FALSE, !mixed_color_depth, GL_FALSE);
            configs = driConcatConfigs(configs, new_configs);
         }
      }
   }

   if (configs == NULL) {
      debug_printf("%s: driCreateConfigs failed\n", __FUNCTION__);
      return NULL;
   }

   return (const __DRIconfig **)configs;
}

/**
 * Roughly the converse of dri_fill_in_modes.
 */
void
dri_fill_st_visual(struct st_visual *stvis,
                   const struct dri_screen *screen,
                   const struct gl_config *mode)
{
   memset(stvis, 0, sizeof(*stvis));

   if (!mode) {
      stvis->no_config = true;
      return;
   }

   /* Deduce the color format. */
   switch (mode->redMask) {
   case 0:
      /* Formats > 32 bpp */
      assert(mode->floatMode);
      if (mode->alphaShift > -1) {
         assert(mode->alphaShift == 48);
         stvis->color_format = PIPE_FORMAT_R16G16B16A16_FLOAT;
      } else {
         stvis->color_format = PIPE_FORMAT_R16G16B16X16_FLOAT;
      }
      break;

   case 0x3FF00000:
      if (mode->alphaMask) {
         assert(mode->alphaMask == 0xC0000000);
         stvis->color_format = PIPE_FORMAT_B10G10R10A2_UNORM;
      } else {
         stvis->color_format = PIPE_FORMAT_B10G10R10X2_UNORM;
      }
      break;

   case 0x000003FF:
      if (mode->alphaMask) {
         assert(mode->alphaMask == 0xC0000000);
         stvis->color_format = PIPE_FORMAT_R10G10B10A2_UNORM;
      } else {
         stvis->color_format = PIPE_FORMAT_R10G10B10X2_UNORM;
      }
      break;

   case 0x00FF0000:
      if (mode->alphaMask) {
         assert(mode->alphaMask == 0xFF000000);
         stvis->color_format = mode->sRGBCapable ?
                                  PIPE_FORMAT_BGRA8888_SRGB :
                                  PIPE_FORMAT_BGRA8888_UNORM;
      } else {
         stvis->color_format = mode->sRGBCapable ?
                                  PIPE_FORMAT_BGRX8888_SRGB :
                                  PIPE_FORMAT_BGRX8888_UNORM;
      }
      break;

   case 0x000000FF:
      if (mode->alphaMask) {
         assert(mode->alphaMask == 0xFF000000);
         stvis->color_format = mode->sRGBCapable ?
                                  PIPE_FORMAT_RGBA8888_SRGB :
                                  PIPE_FORMAT_RGBA8888_UNORM;
      } else {
         stvis->color_format = mode->sRGBCapable ?
                                  PIPE_FORMAT_RGBX8888_SRGB :
                                  PIPE_FORMAT_RGBX8888_UNORM;
      }
      break;

   case 0x0000F800:
      stvis->color_format = PIPE_FORMAT_B5G6R5_UNORM;
      break;

   default:
      assert(!"unsupported visual: invalid red mask");
      return;
   }

   if (mode->sampleBuffers) {
      stvis->samples = mode->samples;
   }

   switch (mode->depthBits) {
   default:
   case 0:
      stvis->depth_stencil_format = PIPE_FORMAT_NONE;
      break;
   case 16:
      stvis->depth_stencil_format = PIPE_FORMAT_Z16_UNORM;
      break;
   case 24:
      if (mode->stencilBits == 0) {
	 stvis->depth_stencil_format = (screen->d_depth_bits_last) ?
                                          PIPE_FORMAT_Z24X8_UNORM:
                                          PIPE_FORMAT_X8Z24_UNORM;
      } else {
	 stvis->depth_stencil_format = (screen->sd_depth_bits_last) ?
                                          PIPE_FORMAT_Z24_UNORM_S8_UINT:
                                          PIPE_FORMAT_S8_UINT_Z24_UNORM;
      }
      break;
   case 32:
      stvis->depth_stencil_format = PIPE_FORMAT_Z32_UNORM;
      break;
   }

   stvis->accum_format = (mode->accumRedBits > 0) ?
      PIPE_FORMAT_R16G16B16A16_SNORM : PIPE_FORMAT_NONE;

   stvis->buffer_mask |= ST_ATTACHMENT_FRONT_LEFT_MASK;
   stvis->render_buffer = ST_ATTACHMENT_FRONT_LEFT;
   if (mode->doubleBufferMode) {
      stvis->buffer_mask |= ST_ATTACHMENT_BACK_LEFT_MASK;
      stvis->render_buffer = ST_ATTACHMENT_BACK_LEFT;
   }
   if (mode->stereoMode) {
      stvis->buffer_mask |= ST_ATTACHMENT_FRONT_RIGHT_MASK;
      if (mode->doubleBufferMode)
         stvis->buffer_mask |= ST_ATTACHMENT_BACK_RIGHT_MASK;
   }

   if (mode->depthBits > 0 || mode->stencilBits > 0)
      stvis->buffer_mask |= ST_ATTACHMENT_DEPTH_STENCIL_MASK;
   /* let the gallium frontend allocate the accum buffer */
}

static bool
dri_get_egl_image(struct st_manager *smapi,
                  void *egl_image,
                  struct st_egl_image *stimg)
{
   struct dri_screen *screen = (struct dri_screen *)smapi;
   __DRIimage *img = NULL;
   const struct dri2_format_mapping *map;

   if (screen->lookup_egl_image) {
      img = screen->lookup_egl_image(screen, egl_image);
   }

   if (!img)
      return FALSE;

   stimg->texture = NULL;
   pipe_resource_reference(&stimg->texture, img->texture);
   map = dri2_get_mapping_by_fourcc(img->dri_fourcc);
   stimg->format = map ? map->pipe_format : img->texture->format;
   stimg->level = img->level;
   stimg->layer = img->layer;

   if (img->imported_dmabuf && map) {
      /* Guess sized internal format for dma-bufs. Could be used
       * by EXT_EGL_image_storage.
       */
      mesa_format mesa_format = driImageFormatToGLFormat(map->dri_format);
      stimg->internalformat = driGLFormatToSizedInternalGLFormat(mesa_format);
   }

   return TRUE;
}

static int
dri_get_param(struct st_manager *smapi,
              enum st_manager_param param)
{
   struct dri_screen *screen = (struct dri_screen *)smapi;

   switch(param) {
   case ST_MANAGER_BROKEN_INVALIDATE:
      return screen->broken_invalidate;
   default:
      return 0;
   }
}

void
dri_destroy_screen_helper(struct dri_screen * screen)
{
   if (screen->base.destroy)
      screen->base.destroy(&screen->base);

   if (screen->st_api && screen->st_api->destroy)
      screen->st_api->destroy(screen->st_api);

   if (screen->base.screen)
      screen->base.screen->destroy(screen->base.screen);

   mtx_destroy(&screen->opencl_func_mutex);
}

void
dri_destroy_screen(__DRIscreen * sPriv)
{
   struct dri_screen *screen = dri_screen(sPriv);

   dri_destroy_screen_helper(screen);

   pipe_loader_release(&screen->dev, 1);

   free(screen->options.force_gl_vendor);

   /* The caller in dri_util preserves the fd ownership */
   free(screen);
   sPriv->driverPrivate = NULL;
   sPriv->extensions = NULL;
}

static void
dri_postprocessing_init(struct dri_screen *screen)
{
   unsigned i;

   for (i = 0; i < PP_FILTERS; i++) {
      screen->pp_enabled[i] = driQueryOptioni(&screen->dev->option_cache,
                                              pp_filters[i].name);
   }
}

static void
dri_set_background_context(struct st_context_iface *st,
                           struct util_queue_monitoring *queue_info)
{
   struct dri_context *ctx = (struct dri_context *)st->st_manager_private;
   const __DRIbackgroundCallableExtension *backgroundCallable =
      ctx->sPriv->dri2.backgroundCallable;

   /* Note: Mesa will only call this function if GL multithreading is enabled
    * We only do that if the loader exposed the __DRI_BACKGROUND_CALLABLE
    * extension. So we know that backgroundCallable is not NULL.
    */
   assert(backgroundCallable);
   backgroundCallable->setBackgroundContext(ctx->cPriv->loaderPrivate);

   if (ctx->hud)
      hud_add_queue_for_monitoring(ctx->hud, queue_info);
}

void
dri_init_options(struct dri_screen *screen)
{
   pipe_loader_load_options(screen->dev);

   dri_fill_st_options(screen);
}

const __DRIconfig **
dri_init_screen_helper(struct dri_screen *screen,
                       struct pipe_screen *pscreen)
{
   screen->base.screen = pscreen;
   screen->base.get_egl_image = dri_get_egl_image;
   screen->base.get_param = dri_get_param;
   screen->base.set_background_context = dri_set_background_context;

   screen->st_api = st_gl_api_create();
   if (!screen->st_api)
      return NULL;

   if(pscreen->get_param(pscreen, PIPE_CAP_NPOT_TEXTURES))
      screen->target = PIPE_TEXTURE_2D;
   else
      screen->target = PIPE_TEXTURE_RECT;

   dri_postprocessing_init(screen);

   screen->st_api->query_versions(screen->st_api, &screen->base,
                                  &screen->options,
                                  &screen->sPriv->max_gl_core_version,
                                  &screen->sPriv->max_gl_compat_version,
                                  &screen->sPriv->max_gl_es1_version,
                                  &screen->sPriv->max_gl_es2_version);

   return dri_fill_in_modes(screen);
}

/* vim: set sw=3 ts=8 sts=3 expandtab: */
