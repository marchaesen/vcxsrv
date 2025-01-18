/**************************************************************************
 *
 * Copyright 2011 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include "drm-uapi/drm_fourcc.h"
#include "radeon_uvd.h"
#include "radeon_uvd_enc.h"
#include "radeon_vce.h"
#include "radeon_vcn_dec.h"
#include "radeon_vcn_enc.h"
#include "radeon_video.h"
#include "si_pipe.h"
#include "si_vpe.h"
#include "util/u_video.h"

/**
 * creates an video buffer with an UVD compatible memory layout
 */
struct pipe_video_buffer *si_video_buffer_create(struct pipe_context *pipe,
                                                 const struct pipe_video_buffer *tmpl)
{
   struct pipe_video_buffer vidbuf = *tmpl;
   uint64_t *modifiers = NULL;
   int modifiers_count = 0;
   uint64_t mod = DRM_FORMAT_MOD_LINEAR;

   /* To get tiled buffers, users need to explicitly provide a list of
    * modifiers. */
   vidbuf.bind |= PIPE_BIND_LINEAR;

   if (pipe->screen->resource_create_with_modifiers) {
      modifiers = &mod;
      modifiers_count = 1;
   }

   return vl_video_buffer_create_as_resource(pipe, &vidbuf, modifiers,
                                             modifiers_count);
}

struct pipe_video_buffer *si_video_buffer_create_with_modifiers(struct pipe_context *pipe,
                                                                const struct pipe_video_buffer *tmpl,
                                                                const uint64_t *modifiers,
                                                                unsigned int modifiers_count)
{
   uint64_t *allowed_modifiers;
   unsigned int allowed_modifiers_count, i;

   /* Filter out DCC modifiers, because we don't support them for video
    * for now. */
   allowed_modifiers = calloc(modifiers_count, sizeof(uint64_t));
   if (!allowed_modifiers)
      return NULL;

   allowed_modifiers_count = 0;
   for (i = 0; i < modifiers_count; i++) {
      if (ac_modifier_has_dcc(modifiers[i]))
         continue;
      allowed_modifiers[allowed_modifiers_count++] = modifiers[i];
   }

   struct pipe_video_buffer *buf =
      vl_video_buffer_create_as_resource(pipe, tmpl, allowed_modifiers, allowed_modifiers_count);
   free(allowed_modifiers);
   return buf;
}

/* set the decoding target buffer offsets */
static struct pb_buffer_lean *si_uvd_set_dtb(struct ruvd_msg *msg, struct vl_video_buffer *buf)
{
   struct si_screen *sscreen = (struct si_screen *)buf->base.context->screen;
   struct si_texture *luma = (struct si_texture *)buf->resources[0];
   struct si_texture *chroma = (struct si_texture *)buf->resources[1];
   enum ruvd_surface_type type =
      (sscreen->info.gfx_level >= GFX9) ? RUVD_SURFACE_TYPE_GFX9 : RUVD_SURFACE_TYPE_LEGACY;

   msg->body.decode.dt_field_mode = buf->base.interlaced;

   si_uvd_set_dt_surfaces(msg, &luma->surface, (chroma) ? &chroma->surface : NULL, type);

   return luma->buffer.buf;
}

/* get the radeon resources for VCE */
static void si_vce_get_buffer(struct pipe_resource *resource, struct pb_buffer_lean **handle,
                              struct radeon_surf **surface)
{
   struct si_texture *res = (struct si_texture *)resource;

   if (handle)
      *handle = res->buffer.buf;

   if (surface)
      *surface = &res->surface;
}

static bool si_vcn_need_context(struct si_context *ctx)
{
   /* Kernel does VCN instance scheduling per context, so when we have
    * multiple instances we should use new context to be able to utilize
    * all of them.
    * Another issue is with AV1, VCN 3 and VCN 4 only support AV1 on
    * first instance. Kernel parses IBs and switches to first instance when
    * it detects AV1, but this only works for first submitted IB in context.
    * The CS would be rejected if we first decode/encode other codecs, kernel
    * schedules on second instance (default) and then we try to decode/encode AV1.
    */
   return ctx->screen->info.ip[AMD_IP_VCN_ENC].num_instances > 1;
}

/**
 * creates an UVD compatible decoder
 */
struct pipe_video_codec *si_uvd_create_decoder(struct pipe_context *context,
                                               const struct pipe_video_codec *templ)
{
   struct si_context *ctx = (struct si_context *)context;
   bool vcn = ctx->vcn_ip_ver >= VCN_1_0_0;
   struct pipe_video_codec *codec = NULL;

   if (templ->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      if (vcn) {
         codec = radeon_create_encoder(context, templ, ctx->ws, si_vce_get_buffer);
         ctx->vcn_has_ctx = si_vcn_need_context(ctx);
         return codec;
      } else {
         if (u_reduce_video_profile(templ->profile) == PIPE_VIDEO_FORMAT_HEVC)
            return radeon_uvd_create_encoder(context, templ, ctx->ws, si_vce_get_buffer);
         else
            return si_vce_create_encoder(context, templ, ctx->ws, si_vce_get_buffer);
      }
   } else if (((struct si_screen *)(context->screen))->info.ip[AMD_IP_VPE].num_queues &&
              templ->entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING)
      return si_vpe_create_processor(context, templ);

   if (vcn) {
      codec = radeon_create_decoder(context, templ);
      ctx->vcn_has_ctx = si_vcn_need_context(ctx);
      return codec;
   }
   return si_common_uvd_create_decoder(context, templ, si_uvd_set_dtb);
}
