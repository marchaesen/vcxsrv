/*
 * Copyright © 2018 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#define FD_BO_NO_HARDPIN 1

#include "drm-uapi/drm_fourcc.h"

#include "a6xx/fd6_blitter.h"
#include "fd6_resource.h"
#include "fdl/fd6_format_table.h"
#include "common/freedreno_lrz.h"
#include "common/freedreno_ubwc.h"

#include "a6xx.xml.h"

/* A subset of the valid tiled formats can be compressed.  We do
 * already require tiled in order to be compressed, but just because
 * it can be tiled doesn't mean it can be compressed.
 */
static bool
ok_ubwc_format(struct pipe_screen *pscreen, enum pipe_format pfmt, unsigned nr_samples)
{
   const struct fd_dev_info *info = fd_screen(pscreen)->info;

   switch (pfmt) {
   case PIPE_FORMAT_Z24X8_UNORM:
      /* MSAA+UBWC does not work without FMT6_Z24_UINT_S8_UINT: */
      return info->a6xx.has_z24uint_s8uint || (nr_samples <= 1);

   case PIPE_FORMAT_X24S8_UINT:
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      /* We can't sample stencil with UBWC on a630, and we may need to be able
       * to sample stencil at some point.  We can't just use
       * fd_resource_uncompress() at the point of stencil sampling because
       * that itself uses stencil sampling in the fd_blitter_blit path.
       */
      return info->a6xx.has_z24uint_s8uint;

   case PIPE_FORMAT_R8_G8B8_420_UNORM:
      /* The difference between NV12 and R8_G8B8_420_UNORM is only where the
       * conversion to RGB happens, with the latter it happens _after_ the
       * texture samp instruction.  But dri2_get_mapping_by_fourcc() doesn't
       * know this, so it asks for NV12 when it really meant to ask for
       * R8_G8B8_420_UNORM.  Just treat them the same here to work around it:
       */
   case PIPE_FORMAT_NV12:
      return true;

   default:
      break;
   }

   /* In copy_format, we treat snorm as unorm to avoid clamping.  But snorm
    * and unorm are UBWC incompatible for special values such as all 0's or
    * all 1's prior to a740.  Disable UBWC for snorm.
    */
   if (util_format_is_snorm(pfmt) &&
       !info->a7xx.ubwc_unorm_snorm_int_compatible)
      return false;

   /* A690 seem to have broken UBWC for depth/stencil, it requires
    * depth flushing where we cannot realistically place it, like between
    * ordinary draw calls writing read/depth. WSL blob seem to use ubwc
    * sometimes for depth/stencil.
    */
   if (info->a6xx.broken_ds_ubwc_quirk &&
       util_format_is_depth_or_stencil(pfmt))
      return false;

   switch (fd6_color_format(pfmt, TILE6_LINEAR)) {
   case FMT6_10_10_10_2_UINT:
   case FMT6_10_10_10_2_UNORM_DEST:
   case FMT6_11_11_10_FLOAT:
   case FMT6_16_FLOAT:
   case FMT6_16_16_16_16_FLOAT:
   case FMT6_16_16_16_16_SINT:
   case FMT6_16_16_16_16_UINT:
   case FMT6_16_16_FLOAT:
   case FMT6_16_16_SINT:
   case FMT6_16_16_UINT:
   case FMT6_16_SINT:
   case FMT6_16_UINT:
   case FMT6_32_32_32_32_SINT:
   case FMT6_32_32_32_32_UINT:
   case FMT6_32_32_SINT:
   case FMT6_32_32_UINT:
   case FMT6_5_6_5_UNORM:
   case FMT6_5_5_5_1_UNORM:
   case FMT6_8_8_8_8_SINT:
   case FMT6_8_8_8_8_UINT:
   case FMT6_8_8_8_8_UNORM:
   case FMT6_8_8_8_X8_UNORM:
   case FMT6_8_8_SINT:
   case FMT6_8_8_UINT:
   case FMT6_8_8_UNORM:
   case FMT6_Z24_UNORM_S8_UINT:
   case FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8:
      return true;
   case FMT6_8_UNORM:
      return info->a6xx.has_8bpp_ubwc;
   default:
      return false;
   }
}

static bool
can_do_ubwc(struct pipe_resource *prsc)
{
   /* limit things to simple single level 2d for now: */
   if ((prsc->depth0 != 1) || (prsc->array_size != 1) ||
       (prsc->last_level != 0))
      return false;
   if (prsc->target != PIPE_TEXTURE_2D)
      return false;
   if (!ok_ubwc_format(prsc->screen, prsc->format, prsc->nr_samples))
      return false;
   return true;
}

static bool
is_z24s8(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
   case PIPE_FORMAT_Z24X8_UNORM:
   case PIPE_FORMAT_X24S8_UINT:
   case PIPE_FORMAT_Z24_UNORM_S8_UINT_AS_R8G8B8A8:
      return true;
   default:
      return false;
   }
}

static bool
valid_ubwc_format_cast(struct fd_resource *rsc, enum pipe_format format)
{
   const struct fd_dev_info *info = fd_screen(rsc->b.b.screen)->info;
   enum pipe_format orig_format = rsc->b.b.format;

   assert(rsc->layout.ubwc);

   /* Special case "casting" format in hw: */
   if (format == PIPE_FORMAT_Z24_UNORM_S8_UINT_AS_R8G8B8A8)
      return true;

   /* If we support z24s8 ubwc then allow casts between the various
    * permutations of z24s8:
    */
   if (info->a6xx.has_z24uint_s8uint && is_z24s8(format) && is_z24s8(orig_format))
      return true;

   enum fd6_ubwc_compat_type type = fd6_ubwc_compat_mode(info, orig_format);
   if (type == FD6_UBWC_UNKNOWN_COMPAT)
      return false;

   return fd6_ubwc_compat_mode(info, format) == type;
}

/**
 * R8G8 have a different block width/height and height alignment from other
 * formats that would normally be compatible (like R16), and so if we are
 * trying to, for example, sample R16 as R8G8 we need to demote to linear.
 */
static bool
is_r8g8(enum pipe_format format)
{
   return (util_format_get_blocksize(format) == 2) &&
         (util_format_get_nr_components(format) == 2);
}

/**
 * Can a rsc as it is currently laid out be accessed as the specified format.
 * Returns whether the access is ok or whether the rsc needs to be demoted
 * to uncompressed tiled or linear.
 */
enum fd6_format_status
fd6_check_valid_format(struct fd_resource *rsc, enum pipe_format format)
{
   enum pipe_format orig_format = rsc->b.b.format;

   if (orig_format == format)
      return FORMAT_OK;

   if (rsc->layout.tile_mode && (is_r8g8(orig_format) != is_r8g8(format)))
      return DEMOTE_TO_LINEAR;

   if (!rsc->layout.ubwc)
      return FORMAT_OK;

   if (ok_ubwc_format(rsc->b.b.screen, format, rsc->b.b.nr_samples) &&
       valid_ubwc_format_cast(rsc, format))
      return FORMAT_OK;

   return DEMOTE_TO_TILED;
}

/**
 * Ensure the rsc is in an ok state to be used with the specified format.
 * This handles the case of UBWC buffers used with non-UBWC compatible
 * formats, by triggering an uncompress.
 */
void
fd6_validate_format(struct fd_context *ctx, struct fd_resource *rsc,
                    enum pipe_format format)
{
   tc_assert_driver_thread(ctx->tc);

   switch (fd6_check_valid_format(rsc, format)) {
   case FORMAT_OK:
      return;
   case DEMOTE_TO_LINEAR:
      perf_debug_ctx(ctx,
                     "%" PRSC_FMT ": demoted to linear+uncompressed due to use as %s",
                     PRSC_ARGS(&rsc->b.b), util_format_short_name(format));

      fd_resource_uncompress(ctx, rsc, true);
      return;
   case DEMOTE_TO_TILED:
      perf_debug_ctx(ctx,
                     "%" PRSC_FMT ": demoted to uncompressed due to use as %s",
                     PRSC_ARGS(&rsc->b.b), util_format_short_name(format));

      fd_resource_uncompress(ctx, rsc, false);
      return;
   }
}

template <chip CHIP>
static void
setup_lrz(struct fd_resource *rsc)
{
   struct fd_screen *screen = fd_screen(rsc->b.b.screen);
   uint32_t nr_layers = 1;
   fdl6_lrz_layout_init<CHIP>(&rsc->lrz_layout, &rsc->layout, screen->info, 0,
                              nr_layers);

   rsc->lrz = fd_bo_new(screen->dev, rsc->lrz_layout.lrz_total_size,
                        FD_BO_NOMAP, "lrz");
}

template <chip CHIP>
static uint32_t
fd6_setup_slices(struct fd_resource *rsc)
{
   struct pipe_resource *prsc = &rsc->b.b;
   struct fd_screen *screen = fd_screen(prsc->screen);

   if (rsc->layout.ubwc && !ok_ubwc_format(prsc->screen, prsc->format, prsc->nr_samples))
      rsc->layout.ubwc = false;

   fdl6_layout(&rsc->layout, screen->info, prsc->format, fd_resource_nr_samples(prsc),
               prsc->width0, prsc->height0, prsc->depth0, prsc->last_level + 1,
               prsc->array_size, prsc->target == PIPE_TEXTURE_3D, false, NULL);

   if (!FD_DBG(NOLRZ) && has_depth(prsc->format) && !is_z32(prsc->format))
      setup_lrz<CHIP>(rsc);

   return rsc->layout.size;
}

static int
fill_ubwc_buffer_sizes(struct fd_resource *rsc)
{
   struct pipe_resource *prsc = &rsc->b.b;
   struct fd_screen *screen = fd_screen(prsc->screen);
   struct fdl_explicit_layout l = {
      .offset = rsc->layout.slices[0].offset,
      .pitch = rsc->layout.pitch0,
   };

   if (!can_do_ubwc(prsc))
      return -1;

   rsc->layout.ubwc = true;
   rsc->layout.tile_mode = TILE6_3;

   if (!fdl6_layout(&rsc->layout, screen->info, prsc->format, fd_resource_nr_samples(prsc),
                    prsc->width0, prsc->height0, prsc->depth0,
                    prsc->last_level + 1, prsc->array_size, false, false, &l))
      return -1;

   if (rsc->layout.size > fd_bo_size(rsc->bo))
      return -1;

   return 0;
}

static int
fd6_layout_resource_for_modifier(struct fd_resource *rsc, uint64_t modifier)
{
   switch (modifier) {
   case DRM_FORMAT_MOD_QCOM_COMPRESSED:
      return fill_ubwc_buffer_sizes(rsc);
   case DRM_FORMAT_MOD_LINEAR:
      if (can_do_ubwc(&rsc->b.b)) {
         perf_debug("%" PRSC_FMT
                    ": not UBWC: imported with DRM_FORMAT_MOD_LINEAR!",
                    PRSC_ARGS(&rsc->b.b));
      }
      return 0;
   case DRM_FORMAT_MOD_QCOM_TILED3:
      rsc->layout.tile_mode = fd6_tile_mode(&rsc->b.b);
      FALLTHROUGH;
   case DRM_FORMAT_MOD_INVALID:
      /* For now, without buffer metadata, we must assume that buffers
       * imported with INVALID modifier are linear
       */
      if (can_do_ubwc(&rsc->b.b)) {
         perf_debug("%" PRSC_FMT
                    ": not UBWC: imported with DRM_FORMAT_MOD_INVALID!",
                    PRSC_ARGS(&rsc->b.b));
      }
      return 0;
   default:
      return -1;
   }
}

static bool
fd6_is_format_supported(struct pipe_screen *pscreen,
                        enum pipe_format fmt,
                        uint64_t modifier)
{
   switch (modifier) {
   case DRM_FORMAT_MOD_LINEAR:
      return true;
   case DRM_FORMAT_MOD_QCOM_COMPRESSED:
      /* screen->is_format_supported() is used only for dma-buf modifier queries,
       * so no super-sampled images:
       */
      return ok_ubwc_format(pscreen, fmt, 0);
   case DRM_FORMAT_MOD_QCOM_TILED3:
      return fd6_tile_mode_for_format(fmt) == TILE6_3;
   default:
      return false;
   }
}

template <chip CHIP>
void
fd6_resource_screen_init(struct pipe_screen *pscreen)
{
   struct fd_screen *screen = fd_screen(pscreen);

   screen->setup_slices = fd6_setup_slices<CHIP>;
   screen->layout_resource_for_modifier = fd6_layout_resource_for_modifier;
   screen->is_format_supported = fd6_is_format_supported;
}
FD_GENX(fd6_resource_screen_init);
