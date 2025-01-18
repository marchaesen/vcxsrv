/**************************************************************************
 *
 * Copyright 2022 Advanced Micro Devices, Inc.
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
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <pipe/p_state.h>
#include <si_pipe.h>
#include "si_vpe.h"

#define SI_VPE_LOG_LEVEL_DEFAULT     0
#define SI_VPE_LOG_LEVEL_INFO        1
#define SI_VPE_LOG_LEVEL_WARNING     2
#define SI_VPE_LOG_LEVEL_DEBUG       3

#define SIVPE_INFO(dblv, fmt, args...)                                                              \
   if ((dblv) >= SI_VPE_LOG_LEVEL_INFO) {                                                           \
      printf("SIVPE INFO: %s: " fmt, __func__, ##args);                                             \
   }

#define SIVPE_WARN(dblv, fmt, args...)                                                              \
   if ((dblv) >= SI_VPE_LOG_LEVEL_WARNING) {                                                        \
      printf("SIVPE WARNING: %s: " fmt, __func__, ##args);                                          \
   }

#define SIVPE_DBG(dblv, fmt, args...)                                                               \
   if ((dblv) >= SI_VPE_LOG_LEVEL_DEBUG) {                                                          \
      printf("SIVPE DBG: %s: " fmt, __func__, ##args);                                              \
   }

#define SIVPE_ERR(fmt, args...)                                                                     \
   fprintf(stderr, "SIVPE ERROR %s:%d %s " fmt, __FILE__, __LINE__, __func__, ##args)

#define SIVPE_PRINT(fmt, args...)                                                                   \
   printf("SIVPE %s: " fmt, __func__, ##args);

/* Pre-defined color primaries of BT601, BT709, BT2020 */
static struct vpe_hdr_metadata Color_Parimaries[] = {
                           /* RedX   RedY  GreenX GreenY BlueX  BlueY  WhiteX WhiteY minlum maxlum maxlig avglig*/
   [VPE_PRIMARIES_BT601]  = {31500, 17000, 15500, 29750,  7750,  3500, 15635, 16450,    10,   270,   1,    1},
   [VPE_PRIMARIES_BT709]  = {32000, 16500, 15000, 30000,  7500,  3000, 15635, 16450,    10,   270,   1,    1},
   [VPE_PRIMARIES_BT2020] = {34000, 16000, 13249, 34500,  7500,  3000, 15635, 16450,    10,  1100,   1,    1},
};

/* Use this enum to help us for accessing the anonymous struct src, dst
 * in blit_info.
 */
enum {
   USE_SRC_SURFACE,
   USE_DST_SURFACE
};

static void *
si_vpe_zalloc(void* mem_ctx, size_t size)
{
   /* mem_ctx is optional for now */
   return CALLOC(1, size);
}

static void
si_vpe_free(void* mem_ctx, void *ptr)
{
   /* mem_ctx is optional for now */
   if (ptr != NULL) {
      FREE(ptr);
      ptr = NULL;
   }
}

static void
si_vpe_log(void* log_ctx, const char* fmt, ...)
{
   /* log_ctx is optional for now */
   va_list args;

   va_start(args, fmt);
   vfprintf(stderr, fmt, args);
   va_end(args);
}

static void
si_vpe_log_silent(void* log_ctx, const char* fmt, ...)
{
}

static void
si_vpe_populate_debug_options(struct vpe_debug_options* debug)
{
   /* Enable debug options here if needed */
}

static void
si_vpe_populate_callback_modules(struct vpe_callback_funcs* funcs, uint8_t log_level)
{
   funcs->log     = log_level ? si_vpe_log : si_vpe_log_silent;
   funcs->zalloc  = si_vpe_zalloc;
   funcs->free    = si_vpe_free;
}

static char*
si_vpe_get_cositing_str(enum vpe_chroma_cositing cositing)
{
   switch (cositing) {
   case VPE_CHROMA_COSITING_NONE:
      return "NONE";
   case VPE_CHROMA_COSITING_LEFT:
      return "LEFT";
   case VPE_CHROMA_COSITING_TOPLEFT:
      return "TOPLEFT";
   case VPE_CHROMA_COSITING_COUNT:
   default:
      return "ERROR";
   }
}

static char*
si_vpe_get_primarie_str(enum vpe_color_primaries primarie)
{
   switch (primarie) {
   case VPE_PRIMARIES_BT601:
      return "BT601";
   case VPE_PRIMARIES_BT709:
      return "BT709";
   case VPE_PRIMARIES_BT2020:
      return "BT2020";
   case VPE_PRIMARIES_JFIF:
      return "JFIF";
   case VPE_PRIMARIES_COUNT:
   default:
      return "ERROR";
   }
}

static char*
si_vpe_get_tf_str(enum vpe_transfer_function tf)
{
   switch (tf) {
   case VPE_TF_G22:
      return "G22";
   case VPE_TF_G24:
      return "G24";
   case VPE_TF_G10:
      return "G10";
   case VPE_TF_PQ:
      return "PQ";
   case VPE_TF_PQ_NORMALIZED:
      return "PQ_NORMALIZED";
   case VPE_TF_HLG:
      return "HLG";
   case VPE_TF_SRGB:
      return "SRGB";
   case VPE_TF_BT709:
      return "BT709";
   case VPE_TF_COUNT:
   default:
      return "ERROR";
   }
}

/* cycle to the next set of buffers */
static void
next_buffer(struct vpe_video_processor *vpeproc)
{
   ++vpeproc->cur_buf;
   vpeproc->cur_buf %= vpeproc->bufs_num;
}

static enum vpe_status
si_vpe_populate_init_data(struct si_context *sctx, struct vpe_init_data* params, uint8_t log_level)
{
   if (!sctx || !params)
      return VPE_STATUS_ERROR;

   params->ver_major = sctx->screen->info.ip[AMD_IP_VPE].ver_major;
   params->ver_minor = sctx->screen->info.ip[AMD_IP_VPE].ver_minor;
   params->ver_rev = sctx->screen->info.ip[AMD_IP_VPE].ver_rev;

   memset(&params->debug, 0, sizeof(struct vpe_debug_options));
   si_vpe_populate_debug_options(&params->debug);
   si_vpe_populate_callback_modules(&params->funcs, log_level);

   SIVPE_DBG(log_level, "Get family: %d\n", sctx->family);
   SIVPE_DBG(log_level, "Get gfx_level: %d\n", sctx->gfx_level);
   SIVPE_DBG(log_level, "Set ver_major: %d\n", params->ver_major);
   SIVPE_DBG(log_level, "Set ver_minor: %d\n", params->ver_minor);
   SIVPE_DBG(log_level, "Set ver_rev: %d\n", params->ver_rev);

   return VPE_STATUS_OK;
}


static enum vpe_status
si_vpe_allocate_buffer(struct vpe_build_bufs **bufs)
{
   if (!bufs)
      return VPE_STATUS_ERROR;

   *bufs = (struct vpe_build_bufs *)MALLOC(sizeof(struct vpe_build_bufs));
   if (!*bufs)
      return VPE_STATUS_NO_MEMORY;

   (*bufs)->cmd_buf.cpu_va = 0;
   (*bufs)->emb_buf.cpu_va = 0;
   (*bufs)->cmd_buf.size = 0;
   (*bufs)->emb_buf.size = 0;

   return VPE_STATUS_OK;
}

static void
si_vpe_free_buffer(struct vpe_build_bufs *bufs)
{
   if (!bufs)
      return;

   free(bufs);
}

static enum vpe_surface_pixel_format
si_vpe_pipe_map_to_vpe_format(enum pipe_format format)
{
   enum vpe_surface_pixel_format ret;

   switch (format) {
   /* YUV format: */
   case PIPE_FORMAT_NV12:
      ret = VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb;
      break;
   case PIPE_FORMAT_NV21:
      ret = VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr;
      break;
   case PIPE_FORMAT_P010:
      ret = VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb;
      break;
   /* RGB format: */
   case PIPE_FORMAT_A8R8G8B8_UNORM:
      ret = VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA8888;
      break;
   case PIPE_FORMAT_A8B8G8R8_UNORM:
      ret = VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA8888;
      break;
   case PIPE_FORMAT_R8G8B8A8_UNORM:
      ret = VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR8888;
      break;
   case PIPE_FORMAT_B8G8R8A8_UNORM:
      ret = VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB8888;
      break;
   case PIPE_FORMAT_X8R8G8B8_UNORM:
      ret = VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRX8888;
      break;
   case PIPE_FORMAT_X8B8G8R8_UNORM:
      ret = VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBX8888;
      break;
   case PIPE_FORMAT_R8G8B8X8_UNORM:
      ret = VPE_SURFACE_PIXEL_FORMAT_GRPH_XBGR8888;
      break;
   case PIPE_FORMAT_B8G8R8X8_UNORM:
      ret = VPE_SURFACE_PIXEL_FORMAT_GRPH_XRGB8888;
      break;
   /* ARGB 2-10-10-10 formats are not supported in Mesa VA-frontend
    * but they are defined in Mesa already.
    */
   case PIPE_FORMAT_A2R10G10B10_UNORM:
      ret = VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA1010102;
      break;
   case PIPE_FORMAT_A2B10G10R10_UNORM:
      ret = VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA1010102;
      break;
   case PIPE_FORMAT_B10G10R10A2_UNORM:
      ret = VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010;
      break;
   case PIPE_FORMAT_R10G10B10A2_UNORM:
      ret = VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010;
      break;
   default:
      ret = VPE_SURFACE_PIXEL_FORMAT_INVALID;
      break;
   }
   return ret;
}

static enum vpe_color_primaries
si_vpe_maps_vpp_to_vpe_primaries(enum pipe_video_vpp_color_primaries colour_primaries)
{
   if (colour_primaries == PIPE_VIDEO_VPP_PRI_BT470BG || colour_primaries == PIPE_VIDEO_VPP_PRI_SMPTE170M)
      return VPE_PRIMARIES_BT601;

   else if (colour_primaries == PIPE_VIDEO_VPP_PRI_BT709)
      return VPE_PRIMARIES_BT709;

   else if (colour_primaries == PIPE_VIDEO_VPP_PRI_BT2020)
      return VPE_PRIMARIES_BT2020;

   SIVPE_PRINT("WARNING: map VA-API primaries(%d) to BT709\n", colour_primaries);
   return VPE_PRIMARIES_BT709;
}

static enum vpe_transfer_function
si_vpe_maps_vpp_to_vpe_transfer_function(
                        enum pipe_video_vpp_transfer_characteristic transfer_characteristics,
                        enum pipe_video_vpp_matrix_coefficients matrix_coefficients)
{
   if (transfer_characteristics == PIPE_VIDEO_VPP_TRC_BT709)
      return (matrix_coefficients == PIPE_VIDEO_VPP_MCF_RGB)? VPE_TF_SRGB : VPE_TF_BT709;

   else if (transfer_characteristics == PIPE_VIDEO_VPP_TRC_GAMMA22)
      return VPE_TF_G22;

   else if (transfer_characteristics == PIPE_VIDEO_VPP_TRC_SMPTEST2084)
      return VPE_TF_PQ;

   else if (transfer_characteristics == PIPE_VIDEO_VPP_TRC_LINEAR)
      return VPE_TF_G10;

   else if (transfer_characteristics == PIPE_VIDEO_VPP_TRC_ARIB_STD_B67)
      return VPE_TF_HLG;

   else if (transfer_characteristics == PIPE_VIDEO_VPP_TRC_BT2020_10)
      return VPE_TF_G10;

   else if (transfer_characteristics == PIPE_VIDEO_VPP_TRC_SMPTEST428_1)
      return VPE_TF_G24;

   else if (transfer_characteristics == PIPE_VIDEO_VPP_TRC_BT2020_12)
      return (matrix_coefficients == PIPE_VIDEO_VPP_MCF_RGB)? VPE_TF_SRGB : VPE_TF_BT709;

   SIVPE_PRINT("WARNING: map VA-API transfer_characteristics(%d) to BT709/SRGB\n", transfer_characteristics);
   return (matrix_coefficients == PIPE_VIDEO_VPP_MCF_RGB)? VPE_TF_SRGB : VPE_TF_BT709;
}

static void
si_vpe_load_default_primaries(struct vpe_hdr_metadata* vpe_hdr, enum vpe_color_primaries primaries)
{
   enum vpe_color_primaries pri_idx = (primaries < VPE_PRIMARIES_COUNT)?
                                      primaries : VPE_PRIMARIES_BT709;

   vpe_hdr->redX          = Color_Parimaries[pri_idx].redX;
   vpe_hdr->redY          = Color_Parimaries[pri_idx].redY;
   vpe_hdr->greenX        = Color_Parimaries[pri_idx].greenX;
   vpe_hdr->greenY        = Color_Parimaries[pri_idx].greenY;
   vpe_hdr->blueX         = Color_Parimaries[pri_idx].blueX;
   vpe_hdr->blueY         = Color_Parimaries[pri_idx].blueY;
   vpe_hdr->whiteX        = Color_Parimaries[pri_idx].whiteX;
   vpe_hdr->whiteY        = Color_Parimaries[pri_idx].whiteY;
   vpe_hdr->min_mastering = Color_Parimaries[pri_idx].min_mastering;
   vpe_hdr->max_mastering = Color_Parimaries[pri_idx].max_mastering;
   vpe_hdr->max_content   = Color_Parimaries[pri_idx].max_content;
   vpe_hdr->avg_content   = Color_Parimaries[pri_idx].avg_content;
}

static void
si_vpe_set_color_space(const struct pipe_vpp_desc *process_properties,
                       struct vpe_color_space *color_space,
                       enum pipe_format format,
                       int which_surface)
{
   enum pipe_video_vpp_color_standard_type colors_standard;
   enum pipe_video_vpp_color_range color_range;
   enum pipe_video_vpp_chroma_siting chroma_siting;
   enum pipe_video_vpp_color_primaries colour_primaries;
   enum pipe_video_vpp_transfer_characteristic transfer_characteristics;
   enum pipe_video_vpp_matrix_coefficients matrix_coefficients;

   if (which_surface == USE_SRC_SURFACE) {
      colors_standard          = process_properties->in_colors_standard;
      color_range              = process_properties->in_color_range;
      chroma_siting            = process_properties->in_chroma_siting;
      colour_primaries         = process_properties->in_color_primaries;
      transfer_characteristics = process_properties->in_transfer_characteristics;
      matrix_coefficients      = process_properties->in_matrix_coefficients;
   } else {
      colors_standard          = process_properties->out_colors_standard;
      color_range              = process_properties->out_color_range;
      chroma_siting            = process_properties->out_chroma_siting;
      colour_primaries         = process_properties->out_color_primaries;
      transfer_characteristics = process_properties->out_transfer_characteristics;
      matrix_coefficients      = process_properties->out_matrix_coefficients;
   }

   switch (colors_standard) {
   case PIPE_VIDEO_VPP_COLOR_STANDARD_TYPE_EXPLICIT:
      /* use original settings from user application */
      break;

   case PIPE_VIDEO_VPP_COLOR_STANDARD_TYPE_BT601:
      colour_primaries         = PIPE_VIDEO_VPP_PRI_SMPTE170M;
      transfer_characteristics = PIPE_VIDEO_VPP_TRC_SMPTE170M;
      matrix_coefficients      = PIPE_VIDEO_VPP_MCF_SMPTE170M;
      break;

   case PIPE_VIDEO_VPP_COLOR_STANDARD_TYPE_BT2020:
      colour_primaries         = PIPE_VIDEO_VPP_PRI_BT2020;
      transfer_characteristics = PIPE_VIDEO_VPP_TRC_BT2020_10;
      matrix_coefficients      = PIPE_VIDEO_VPP_MCF_BT2020_NCL;
      break;

   default:
   case PIPE_VIDEO_VPP_COLOR_STANDARD_TYPE_BT709:
      colour_primaries         = PIPE_VIDEO_VPP_PRI_BT709;
      transfer_characteristics = PIPE_VIDEO_VPP_TRC_BT709;
      matrix_coefficients      = PIPE_VIDEO_VPP_MCF_BT709;
      break;
   }

   switch (format) {
   case PIPE_FORMAT_NV12:
   case PIPE_FORMAT_NV21:
   case PIPE_FORMAT_P010:
      color_space->encoding = VPE_PIXEL_ENCODING_YCbCr;
      break;
   case PIPE_FORMAT_A8R8G8B8_UNORM:
   case PIPE_FORMAT_A8B8G8R8_UNORM:
   case PIPE_FORMAT_R8G8B8A8_UNORM:
   case PIPE_FORMAT_B8G8R8A8_UNORM:
   case PIPE_FORMAT_X8R8G8B8_UNORM:
   case PIPE_FORMAT_X8B8G8R8_UNORM:
   case PIPE_FORMAT_R8G8B8X8_UNORM:
   case PIPE_FORMAT_B8G8R8X8_UNORM:
   case PIPE_FORMAT_A2R10G10B10_UNORM:
   case PIPE_FORMAT_R10G10B10A2_UNORM:
   case PIPE_FORMAT_A2B10G10R10_UNORM:
   case PIPE_FORMAT_B10G10R10A2_UNORM:
   default:
      matrix_coefficients = PIPE_VIDEO_VPP_MCF_RGB;
      color_space->encoding = VPE_PIXEL_ENCODING_RGB;
      break;
   }

   switch (color_range) {
   case PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_REDUCED:
      color_space->range = VPE_COLOR_RANGE_STUDIO;
      break;
   default:
   case PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL:
      color_space->range = VPE_COLOR_RANGE_FULL;
      break;
   }

   /* Force RGB output range is Full to have better color performance */
   /* TO-DO: Should Mesa have to know the display console is TV or PC Monitor? */
   if (!util_format_is_yuv(format) && (which_surface == USE_DST_SURFACE))
         color_space->range = VPE_COLOR_RANGE_FULL;

   /* Default use VPE_CHROMA_COSITING_NONE (CENTER | CENTER) */
   color_space->cositing = VPE_CHROMA_COSITING_NONE;
   if (chroma_siting & PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_CENTER){
      if (chroma_siting & PIPE_VIDEO_VPP_CHROMA_SITING_HORIZONTAL_LEFT)
         color_space->cositing = VPE_CHROMA_COSITING_LEFT;
   } else if (chroma_siting & PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_TOP) {
      if (chroma_siting & PIPE_VIDEO_VPP_CHROMA_SITING_HORIZONTAL_LEFT)
         color_space->cositing = VPE_CHROMA_COSITING_TOPLEFT;
   } else if (chroma_siting & PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_BOTTOM) {
      if (chroma_siting & PIPE_VIDEO_VPP_CHROMA_SITING_HORIZONTAL_LEFT)
         color_space->cositing = VPE_CHROMA_COSITING_LEFT;
   }

   color_space->primaries = si_vpe_maps_vpp_to_vpe_primaries(colour_primaries);
   color_space->tf = si_vpe_maps_vpp_to_vpe_transfer_function(transfer_characteristics, matrix_coefficients);
}

static enum vpe_status
si_vpe_set_plane_info(struct vpe_video_processor *vpeproc,
                      const struct pipe_vpp_desc *process_properties,
                      struct pipe_surface **surfaces,
                      int which_surface,
                      struct vpe_surface_info *surface_info)
{
   struct vpe_plane_address *plane_address = &surface_info->address;
   struct vpe_plane_size *plane_size = &surface_info->plane_size;
   struct si_texture *si_tex_0;
   struct si_texture *si_tex_1;
   enum pipe_format format;

   if (which_surface == USE_SRC_SURFACE)
      format = process_properties->base.input_format;
   else
      format = process_properties->base.output_format;

   /* Trusted memory not supported now */
   plane_address->tmz_surface = false;

   /* Only support 1 plane for RGB formats, and 2 plane format for YUV formats */
   if (util_format_is_yuv(format) && util_format_get_num_planes(format) == 2) {
      si_tex_0 = (struct si_texture *)surfaces[0]->texture;
      si_tex_1 = (struct si_texture *)surfaces[1]->texture;
      plane_address->type = VPE_PLN_ADDR_TYPE_VIDEO_PROGRESSIVE;
      plane_address->video_progressive.luma_addr.quad_part = si_tex_0->buffer.gpu_address + si_tex_0->surface.u.gfx9.surf_offset;
      plane_address->video_progressive.chroma_addr.quad_part = si_tex_1->buffer.gpu_address + si_tex_1->surface.u.gfx9.surf_offset;
   } else if (!util_format_is_yuv(format) && util_format_get_num_planes(format) == 1){
      si_tex_0 = (struct si_texture *)surfaces[0]->texture;
      si_tex_1 = NULL;
      plane_address->type = VPE_PLN_ADDR_TYPE_GRAPHICS;
      plane_address->grph.addr.quad_part = si_tex_0->buffer.gpu_address + si_tex_0->surface.u.gfx9.surf_offset;
   } else
      return VPE_STATUS_NOT_SUPPORTED;

   /* 1st plane ret setting */
   plane_size->surface_size.x         = 0;
   plane_size->surface_size.y         = 0;
   plane_size->surface_size.width     = surfaces[0]->width;
   plane_size->surface_size.height    = surfaces[0]->height;
   plane_size->surface_pitch          = si_tex_0->surface.u.gfx9.surf_pitch;
   plane_size->surface_aligned_height = surfaces[0]->height;

   /* YUV 2nd plane ret setting */
   if (util_format_get_num_planes(format) == 2) {
      plane_size->chroma_size.x         = 0;
      plane_size->chroma_size.y         = 0;
      plane_size->chroma_size.width     = surfaces[1]->width;
      plane_size->chroma_size.height    = surfaces[1]->height;
      plane_size->chroma_pitch          = si_tex_1->surface.u.gfx9.surf_pitch;
      plane_size->chrome_aligned_height = surfaces[1]->height;
   }

   /* Color space setting */
   surface_info->format = si_vpe_pipe_map_to_vpe_format(format);
   si_vpe_set_color_space(process_properties, &surface_info->cs, format, which_surface);
   return VPE_STATUS_OK;
}

static enum vpe_status
si_vpe_set_surface_info(struct vpe_video_processor *vpeproc,
                        const struct pipe_vpp_desc *process_properties,
                        struct pipe_surface **surfaces,
                        int which_surface,
                        struct vpe_surface_info *surface_info)
{
   assert(surface_info);

   /* Set up surface pitch, plane address, color space */
   if (VPE_STATUS_OK != si_vpe_set_plane_info(vpeproc, process_properties, surfaces, which_surface, surface_info))
      return VPE_STATUS_NOT_SUPPORTED;

   struct si_texture *tex = (struct si_texture *)surfaces[0]->texture;
   surface_info->swizzle               = tex->surface.u.gfx9.swizzle_mode;

   /* DCC not supported */
   if (tex->surface.meta_offset)
      return VPE_STATUS_NOT_SUPPORTED;

   struct vpe_plane_dcc_param *dcc_param = &surface_info->dcc;
   dcc_param->enable                   = false;
   dcc_param->meta_pitch               = 0;
   dcc_param->independent_64b_blks     = false;
   dcc_param->dcc_ind_blk              = 0;
   dcc_param->meta_pitch_c             = 0;
   dcc_param->independent_64b_blks_c   = false;
   dcc_param->dcc_ind_blk_c            = 0;

   return VPE_STATUS_OK;
}

static void
si_vpe_set_stream_in_param(struct vpe_video_processor *vpeproc,
                           const struct pipe_vpp_desc *process_properties,
                           struct vpe_stream *stream)
{
   struct vpe *vpe_handle = vpeproc->vpe_handle;
   struct vpe_scaling_info *scaling_info = &stream->scaling_info;
   struct vpe_blend_info *blend_info     = &stream->blend_info;
   struct vpe_color_adjust *color_adj    = &stream->color_adj;

   /* Init: scaling_info */
   scaling_info->src_rect.x            = process_properties->src_region.x0;
   scaling_info->src_rect.y            = process_properties->src_region.y0;
   scaling_info->src_rect.width        = process_properties->src_region.x1 - process_properties->src_region.x0;
   scaling_info->src_rect.height       = process_properties->src_region.y1 - process_properties->src_region.y0;
   scaling_info->dst_rect.x            = process_properties->dst_region.x0;
   scaling_info->dst_rect.y            = process_properties->dst_region.y0;
   scaling_info->dst_rect.width        = process_properties->dst_region.x1 - process_properties->dst_region.x0;
   scaling_info->dst_rect.height       = process_properties->dst_region.y1 - process_properties->dst_region.y0;
   scaling_info->taps.v_taps           = 0;
   scaling_info->taps.h_taps           = 0;
   scaling_info->taps.v_taps_c         = 2;
   scaling_info->taps.h_taps_c         = 2;

   vpe_get_optimal_num_of_taps(vpe_handle, scaling_info);

   blend_info->blending                = false;
   blend_info->pre_multiplied_alpha    = false;
   blend_info->global_alpha            = blend_info->blending;
   blend_info->global_alpha_value      = 1.0;

   /* Global Alpha for Background ? */
   if (process_properties->blend.mode == PIPE_VIDEO_VPP_BLEND_MODE_GLOBAL_ALPHA) {
      //blend_info->global_alpha = true;
      blend_info->global_alpha_value = process_properties->blend.global_alpha;
   }

   /* TO-DO: do ProcAmp in next stage */
   color_adj->brightness               = 0.0;
   color_adj->contrast                 = 1.0;
   color_adj->hue                      = 0.0;
   color_adj->saturation               = 1.0;

   switch (process_properties->orientation & 0xF) {
   case PIPE_VIDEO_VPP_ROTATION_90:
      stream->rotation = VPE_ROTATION_ANGLE_90;
      break;
   case PIPE_VIDEO_VPP_ROTATION_180:
      stream->rotation = VPE_ROTATION_ANGLE_180;
      break;
   case PIPE_VIDEO_VPP_ROTATION_270:
      stream->rotation = VPE_ROTATION_ANGLE_270;
      break;
   default:
      stream->rotation = VPE_ROTATION_ANGLE_0;
      break;
   }

   stream->horizontal_mirror  = (process_properties->orientation & PIPE_VIDEO_VPP_FLIP_HORIZONTAL) ? true : false;
   stream->vertical_mirror    = (process_properties->orientation & PIPE_VIDEO_VPP_FLIP_VERTICAL)   ? true : false;

   stream->enable_luma_key         = false;
   stream->lower_luma_bound        = 0.5;
   stream->upper_luma_bound        = 0.5;

   stream->flags.reserved          = 0;
   stream->flags.geometric_scaling = 0;
   stream->flags.hdr_metadata      = 0;

   /* TO-DO: support HDR10 Metadata */
   si_vpe_load_default_primaries(&stream->hdr_metadata, stream->surface_info.cs.primaries);
}

static void
si_vpe_set_stream_out_param(struct vpe_video_processor *vpeproc,
                            const struct pipe_vpp_desc *process_properties,
                            struct vpe_build_param *build_param)
{
   uint32_t background_color = process_properties->background_color;

   build_param->target_rect.x      = process_properties->dst_region.x0;
   build_param->target_rect.y      = process_properties->dst_region.y0;
   build_param->target_rect.width  = process_properties->dst_region.x1 - process_properties->dst_region.x0;
   build_param->target_rect.height = process_properties->dst_region.y1 - process_properties->dst_region.y0;

   build_param->bg_color.is_ycbcr  = false;
   build_param->bg_color.rgba.r    = 0;
   build_param->bg_color.rgba.g    = 0;
   build_param->bg_color.rgba.b    = 0;
   build_param->bg_color.rgba.a    = 0;

   /* Studio color range not start from 0 */
   if (!(background_color & 0xFFFFFF) && (build_param->dst_surface.cs.range == VPE_COLOR_RANGE_STUDIO)) {
      build_param->bg_color.rgba.a = (float)((background_color & 0xFF000000) >> 24) / 255.0;
      build_param->bg_color.rgba.r = 0.0628;
      build_param->bg_color.rgba.g = 0.0628;
      build_param->bg_color.rgba.b = 0.0628;
   } else if (process_properties->background_color) {
      build_param->bg_color.rgba.a = (float)((background_color & 0xFF000000) >> 24) / 255.0;
      build_param->bg_color.rgba.r = (float)((background_color & 0x00FF0000) >> 16) / 255.0;
      build_param->bg_color.rgba.g = (float)((background_color & 0x0000FF00) >>  8) / 255.0;
      build_param->bg_color.rgba.b = (float)(background_color & 0x000000FF) / 255.0;
   }

   build_param->alpha_mode         = VPE_ALPHA_OPAQUE;
   build_param->flags.hdr_metadata = 1;

   /* TODO: Should support HDR10 Metadata */
   si_vpe_load_default_primaries(&build_param->hdr_metadata, build_param->dst_surface.cs.primaries);
}

static void
si_vpe_processor_destroy(struct pipe_video_codec *codec)
{
   struct vpe_video_processor *vpeproc = (struct vpe_video_processor *)codec;
   unsigned int i;
   assert(codec);

   if (vpeproc->vpe_build_bufs)
      si_vpe_free_buffer(vpeproc->vpe_build_bufs);

   if (vpeproc->vpe_handle)
      vpe_destroy(&vpeproc->vpe_handle);

   if (vpeproc->vpe_build_param) {
      if (vpeproc->vpe_build_param->streams)
         FREE(vpeproc->vpe_build_param->streams);
      FREE(vpeproc->vpe_build_param);
   }

   if (vpeproc->emb_buffers) {
      for (i = 0; i < vpeproc->bufs_num; i++)
         if (vpeproc->emb_buffers[i].res)
            si_vid_destroy_buffer(&vpeproc->emb_buffers[i]);
      FREE(vpeproc->emb_buffers);
   }

   vpeproc->bufs_num = 0;
   vpeproc->ws->cs_destroy(&vpeproc->cs);
   SIVPE_DBG(vpeproc->log_level, "Success\n");
   FREE(vpeproc);
}

static void
si_vpe_processor_begin_frame(struct pipe_video_codec *codec,
                             struct pipe_video_buffer *target,
                             struct pipe_picture_desc *picture)
{
   struct vpe_video_processor *vpeproc = (struct vpe_video_processor *)codec;
   struct pipe_surface **dst_surfaces;
   assert(codec);

   dst_surfaces = target->get_surfaces(target);
   if (!dst_surfaces || !dst_surfaces[0]) {
      SIVPE_ERR("Get target surface failed\n");
      return;
   }
   vpeproc->dst_surfaces = dst_surfaces;
}

static void
si_vpe_cs_add_surface_buffer(struct vpe_video_processor *vpeproc,
                             struct pipe_surface **surfaces,
                             unsigned usage)
{
   struct si_resource *si_res;
   int i;

   for (i = 0; i < VL_MAX_SURFACES; ++i) {
      if (!surfaces[i])
         continue;

      si_res = si_resource(surfaces[i]->texture);
      vpeproc->ws->cs_add_buffer(&vpeproc->cs, si_res->buf, usage | RADEON_USAGE_SYNCHRONIZED, 0);
   }
}

static void
si_vpe_show_process_settings(struct vpe_video_processor *vpeproc,
                             struct vpe_build_param *build_param)
{
   if (vpeproc->log_level >= SI_VPE_LOG_LEVEL_DEBUG) {
      SIVPE_PRINT("src surface format(%d) rect (%d, %d, %d, %d)\n",
               build_param->streams[0].surface_info.format,
               build_param->streams[0].surface_info.plane_size.surface_size.x,
               build_param->streams[0].surface_info.plane_size.surface_size.y,
               build_param->streams[0].surface_info.plane_size.surface_size.width,
               build_param->streams[0].surface_info.plane_size.surface_size.height);

      SIVPE_PRINT("src surface Cositing(%s), primaries(%s), tf(%s), range(%s)\n",
               si_vpe_get_cositing_str(build_param->streams[0].surface_info.cs.cositing),
               si_vpe_get_primarie_str(build_param->streams[0].surface_info.cs.primaries),
               si_vpe_get_tf_str(build_param->streams[0].surface_info.cs.tf),
               (build_param->streams[0].surface_info.cs.range == VPE_COLOR_RANGE_FULL)?"FULL":"STUDIO");

      SIVPE_PRINT("dst surface format(%d) rect (%d, %d, %d, %d)\n",
               build_param->dst_surface.format,
               build_param->dst_surface.plane_size.surface_size.x,
               build_param->dst_surface.plane_size.surface_size.y,
               build_param->dst_surface.plane_size.surface_size.width,
               build_param->dst_surface.plane_size.surface_size.height);

      SIVPE_PRINT("dst surface Cositing(%s), primaries(%s), tf(%s), range(%s)\n",
               si_vpe_get_cositing_str(build_param->dst_surface.cs.cositing),
               si_vpe_get_primarie_str(build_param->dst_surface.cs.primaries),
               si_vpe_get_tf_str(build_param->dst_surface.cs.tf),
               (build_param->dst_surface.cs.range == VPE_COLOR_RANGE_FULL)?"FULL":"STUDIO");

      SIVPE_PRINT("Source surface pitch(%d), chroma pitch(%d), dst-surface pitch(%d), chroma pitch(%d)\n",
               build_param->streams[0].surface_info.plane_size.surface_pitch,
               build_param->streams[0].surface_info.plane_size.chroma_pitch,
               build_param->dst_surface.plane_size.surface_pitch,
               build_param->dst_surface.plane_size.chroma_pitch);

      SIVPE_PRINT("background color RGBA(%0.3f, %0.3f, %0.3f, %0.3f)\n",
               build_param->bg_color.rgba.r,
               build_param->bg_color.rgba.g,
               build_param->bg_color.rgba.b,
               build_param->bg_color.rgba.a);

      SIVPE_PRINT("target_rect(%d, %d, %d, %d)\n",
               build_param->target_rect.x,
               build_param->target_rect.y,
               build_param->target_rect.width,
               build_param->target_rect.height);

      SIVPE_PRINT("rotation(%d) horizontal_mirror(%d) vertical_mirror(%d)\n",
               build_param->streams[0].rotation,
               build_param->streams[0].horizontal_mirror,
               build_param->streams[0].vertical_mirror);

      SIVPE_PRINT("scaling_src_rect(%d, %d, %d, %d)\n",
               build_param->streams[0].scaling_info.src_rect.x,
               build_param->streams[0].scaling_info.src_rect.y,
               build_param->streams[0].scaling_info.src_rect.width,
               build_param->streams[0].scaling_info.src_rect.height);

      SIVPE_PRINT("scaling_dst_rect(%d, %d, %d, %d)\n",
               build_param->streams[0].scaling_info.dst_rect.x,
               build_param->streams[0].scaling_info.dst_rect.y,
               build_param->streams[0].scaling_info.dst_rect.width,
               build_param->streams[0].scaling_info.dst_rect.height);

      SIVPE_PRINT("scaling_taps h_taps(%d) v_taps(%d) h_taps_c(%d) v_taps_c(%d)\n",
               build_param->streams[0].scaling_info.taps.h_taps,
               build_param->streams[0].scaling_info.taps.v_taps,
               build_param->streams[0].scaling_info.taps.h_taps_c,
               build_param->streams[0].scaling_info.taps.v_taps_c);

      SIVPE_PRINT("blend global_alpha(%d): %0.3f\n",
               build_param->streams[0].blend_info.global_alpha,
               build_param->streams[0].blend_info.global_alpha_value);

      SIVPE_PRINT("ToneMapping shaper_tf(%d) lut_out_tf(%d) lut_in_gamut(%d) lut_out_gamut(%d)\n",
               build_param->streams[0].tm_params.shaper_tf,
               build_param->streams[0].tm_params.lut_out_tf,
               build_param->streams[0].tm_params.lut_in_gamut,
               build_param->streams[0].tm_params.lut_out_gamut);
   }
}

static int
si_vpe_processor_is_process_supported(struct pipe_video_codec *codec,
                                      struct pipe_video_buffer *input_texture,
                                      const struct pipe_vpp_desc *process_properties)
{
   enum vpe_status result = VPE_STATUS_OK;
   struct vpe_video_processor *vpeproc = (struct vpe_video_processor *)codec;
   struct vpe *vpe_handle = vpeproc->vpe_handle;
   struct vpe_build_param *build_param = vpeproc->vpe_build_param;
   struct pipe_surface **src_surfaces;
   struct vpe_bufs_req bufs_required;

   assert(codec);
   assert(process_properties);
   assert(vpeproc->dst_surfaces);

   /* Get input surface */
   src_surfaces = input_texture->get_surfaces(input_texture);
   if (!src_surfaces || !src_surfaces[0]) {
      SIVPE_ERR("Get source surface failed\n");
      return 1;
   }
   vpeproc->src_surfaces = src_surfaces;

   /* Mesa only sends one input frame at one time (one stream pipe).
    * If there is more than one pipe need to be handled, it have to re-locate memory.
    * But now we only focuse on handling one stream pipe.
    */
   build_param->num_streams = 1;
   memset(build_param->streams, 0, sizeof(struct vpe_stream) * build_param->num_streams);

   /* Init input surface setting */
   result = si_vpe_set_surface_info(vpeproc,
                                    process_properties,
                                    vpeproc->src_surfaces,
                                    USE_SRC_SURFACE,
                                    &build_param->streams[0].surface_info);
   if (VPE_STATUS_OK != result) {
      SIVPE_ERR("Set Src surface failed with result: %d\n", result);
      return 1;
   }

   /* Init input stream setting */
   si_vpe_set_stream_in_param(
               vpeproc,
               process_properties,
               &build_param->streams[0]);

   /* Init output surface setting */
   result = si_vpe_set_surface_info(vpeproc,
                                    process_properties,
                                    vpeproc->dst_surfaces,
                                    USE_DST_SURFACE,
                                    &build_param->dst_surface);
   if (VPE_STATUS_OK != result) {
      SIVPE_ERR("Set Dst surface failed with result: %d\n", result);
      return 1;
   }

   /* Init output stream setting */
   si_vpe_set_stream_out_param(
               vpeproc,
               process_properties,
               build_param);

   /* Shows details of current processing. */
   si_vpe_show_process_settings(vpeproc, build_param);

   if(vpe_handle->level == VPE_IP_LEVEL_1_1) {
      build_param->num_instances = 2;
      build_param->collaboration_mode = true;
   } else {
      build_param->num_instances = 1;
      build_param->collaboration_mode = false;
   }

   result = vpe_check_support(vpe_handle, build_param, &bufs_required);
   if (VPE_STATUS_OK != result) {
      SIVPE_WARN(vpeproc->log_level, "Check support failed with result: %d\n", result);
      return 1;
   }

   if (VPE_EMBBUF_SIZE < bufs_required.emb_buf_size) {
      SIVPE_ERR("Required Buffer size is out of allocated: %" PRIu64 "\n", bufs_required.emb_buf_size);
      return 1;
   }

   return 0;
}

static int
si_vpe_processor_process_frame(struct pipe_video_codec *codec,
                               struct pipe_video_buffer *input_texture,
                               const struct pipe_vpp_desc *process_properties)
{
   enum vpe_status result = VPE_STATUS_OK;
   struct vpe_video_processor *vpeproc = (struct vpe_video_processor *)codec;
   struct vpe *vpe_handle = vpeproc->vpe_handle;
   struct vpe_build_param *build_param = vpeproc->vpe_build_param;
   struct rvid_buffer *emb_buf;
   uint64_t *vpe_ptr;

   /* Check if the required options are supported */
   if(si_vpe_processor_is_process_supported(codec, input_texture, process_properties))
      return 1;

   /* Init CmdBuf address and size information */
   vpe_ptr = (uint64_t *)vpeproc->cs.current.buf;
   vpeproc->vpe_build_bufs->cmd_buf.cpu_va = (uintptr_t)vpe_ptr;
   vpeproc->vpe_build_bufs->cmd_buf.gpu_va = 0;
   vpeproc->vpe_build_bufs->cmd_buf.size = vpeproc->cs.current.max_dw;
   vpeproc->vpe_build_bufs->cmd_buf.tmz = false;

   /* Init EmbBuf address and size information */
   emb_buf = &vpeproc->emb_buffers[vpeproc->cur_buf];
   /* Map EmbBuf for CPU access */
   vpe_ptr = (uint64_t *)vpeproc->ws->buffer_map(vpeproc->ws,
                                                 emb_buf->res->buf,
                                                 &vpeproc->cs,
                                                 PIPE_MAP_WRITE | RADEON_MAP_TEMPORARY);
   if (!vpe_ptr) {
      SIVPE_ERR("Mapping Embbuf failed\n");
      return 1;
   }
   vpeproc->vpe_build_bufs->emb_buf.cpu_va = (uintptr_t)vpe_ptr;
   vpeproc->vpe_build_bufs->emb_buf.gpu_va = vpeproc->ws->buffer_get_virtual_address(emb_buf->res->buf);
   vpeproc->vpe_build_bufs->emb_buf.size = VPE_EMBBUF_SIZE;
   vpeproc->vpe_build_bufs->emb_buf.tmz = false;

   result = vpe_build_commands(vpe_handle, build_param, vpeproc->vpe_build_bufs);
   if (VPE_STATUS_OK != result) {
      SIVPE_ERR("Build commands failed with result: %d\n", result);
      goto fail;
   }

   /* Un-map Emb_buf */
   vpeproc->ws->buffer_unmap(vpeproc->ws, emb_buf->res->buf);

   /* Check buffer size */
   if (vpeproc->vpe_build_bufs->cmd_buf.size == 0 || vpeproc->vpe_build_bufs->cmd_buf.size == vpeproc->cs.current.max_dw) {
      SIVPE_ERR("Cmdbuf size wrong\n");
      goto fail;
   }
   if (vpeproc->vpe_build_bufs->emb_buf.size == 0 || vpeproc->vpe_build_bufs->emb_buf.size == VPE_EMBBUF_SIZE) {
      SIVPE_ERR("Embbuf size wrong\n");
      goto fail;
   }
   SIVPE_DBG(vpeproc->log_level, "Used buf size: %" PRIu64 ", %" PRIu64 "\n",
              vpeproc->vpe_build_bufs->cmd_buf.size, vpeproc->vpe_build_bufs->emb_buf.size);

   /* Have to tell Command Submission context the command length */
   vpeproc->cs.current.cdw += (vpeproc->vpe_build_bufs->cmd_buf.size / 4);

   /* Add embbuf into bo_handle list */
   vpeproc->ws->cs_add_buffer(&vpeproc->cs, emb_buf->res->buf, RADEON_USAGE_READ | RADEON_USAGE_SYNCHRONIZED, RADEON_DOMAIN_GTT);

   /* Add surface buffers into bo_handle list */
   si_vpe_cs_add_surface_buffer(vpeproc, vpeproc->src_surfaces, RADEON_USAGE_READ);
   si_vpe_cs_add_surface_buffer(vpeproc, vpeproc->dst_surfaces, RADEON_USAGE_WRITE);

   return 0;

fail:
   vpeproc->ws->buffer_unmap(vpeproc->ws, emb_buf->res->buf);
   SIVPE_ERR("Process frame failed\n");
   return 1;
}

static int
si_vpe_processor_end_frame(struct pipe_video_codec *codec,
                           struct pipe_video_buffer *target,
                           struct pipe_picture_desc *picture)
{
   struct vpe_video_processor *vpeproc = (struct vpe_video_processor *)codec;
   assert(codec);

   vpeproc->ws->cs_flush(&vpeproc->cs, picture->flush_flags, picture->fence);
   next_buffer(vpeproc);

   return 0;
}

static void
si_vpe_processor_flush(struct pipe_video_codec *codec)
{
   /* Commands will be flushed when end_frame() is called */
   return;
}

static int si_vpe_processor_fence_wait(struct pipe_video_codec *codec,
                                       struct pipe_fence_handle *fence,
                                       uint64_t timeout)
{
   struct vpe_video_processor *vpeproc = (struct vpe_video_processor *)codec;
   assert(codec);

   if (!vpeproc->ws->fence_wait(vpeproc->ws, fence, timeout)) {
      SIVPE_DBG(vpeproc->log_level, "Wait processor fence fail\n");
      return 0;
   }
   return 1;
}

static void si_vpe_processor_destroy_fence(struct pipe_video_codec *codec,
                                           struct pipe_fence_handle *fence)
{
   struct vpe_video_processor *vpeproc = (struct vpe_video_processor *)codec;
   assert(codec);

   vpeproc->ws->fence_reference(vpeproc->ws, &fence, NULL);
}

struct pipe_video_codec*
si_vpe_create_processor(struct pipe_context *context, const struct pipe_video_codec *templ)
{
   struct si_context *sctx = (struct si_context *)context;
   struct radeon_winsys *ws = sctx->ws;
   struct vpe_video_processor *vpeproc;
   struct vpe_init_data *init_data;
   unsigned int i;

   vpeproc = CALLOC_STRUCT(vpe_video_processor);
   if (!vpeproc) {
      SIVPE_ERR("Allocate struct failed\n");
      return NULL;
   }

   /* SI_VPE debug log level
    * Default level(0) only shows error messages
    */
   vpeproc->log_level = (uint8_t)debug_get_num_option("AMDGPU_SIVPE_LOG_LEVEL", SI_VPE_LOG_LEVEL_DEFAULT);

   vpeproc->base = *templ;
   vpeproc->base.context = context;
   vpeproc->base.width = templ->width;
   vpeproc->base.height = templ->height;

   vpeproc->base.destroy = si_vpe_processor_destroy;
   vpeproc->base.begin_frame = si_vpe_processor_begin_frame;
   vpeproc->base.process_frame = si_vpe_processor_process_frame;
   vpeproc->base.end_frame = si_vpe_processor_end_frame;
   vpeproc->base.flush = si_vpe_processor_flush;
   vpeproc->base.fence_wait = si_vpe_processor_fence_wait;
   vpeproc->base.destroy_fence = si_vpe_processor_destroy_fence;

   vpeproc->ver_major = sctx->screen->info.ip[AMD_IP_VPE].ver_major;
   vpeproc->ver_minor = sctx->screen->info.ip[AMD_IP_VPE].ver_minor;

   vpeproc->screen = context->screen;
   vpeproc->ws = ws;

   init_data = &vpeproc->vpe_data;
   if (VPE_STATUS_OK != si_vpe_populate_init_data(sctx, init_data, vpeproc->log_level)){
      SIVPE_ERR("Init VPE populate data failed\n");
      goto fail;
   }

   vpeproc->vpe_handle = vpe_create(init_data);
   if (!vpeproc->vpe_handle) {
      SIVPE_ERR("Create VPE handle failed\n");
      goto fail;
   }

   if (VPE_STATUS_OK != si_vpe_allocate_buffer(&vpeproc->vpe_build_bufs)) {
      SIVPE_ERR("Allocate VPE buffers failed\n");
      goto fail;
   }

   /* Create Command Submission context.
    * The cmdbuf (Vpe Descriptor) will be stored in cs.current.buf
    * there is no needs to allocate another buffer handle for cmdbuf.
    */
   if (!ws->cs_create(&vpeproc->cs, sctx->ctx, AMD_IP_VPE, NULL, NULL)) {
      SIVPE_ERR("Get command submission context failed.\n");
      goto fail;
   }

   /* Allocate Vpblit Descriptor buffers
    * Descriptor buffer is used to store plane config and VPEP commands
    */
   vpeproc->bufs_num = (uint8_t)debug_get_num_option("AMDGPU_SIVPE_BUF_NUM", VPE_BUFFERS_NUM);
   vpeproc->cur_buf = 0;
   vpeproc->emb_buffers = (struct rvid_buffer *)CALLOC(vpeproc->bufs_num, sizeof(struct rvid_buffer));
   if (!vpeproc->emb_buffers) {
      SIVPE_ERR("Allocate command buffer list failed\n");
      goto fail;
   } else
      SIVPE_INFO(vpeproc->log_level, "Number of emb_buf is %d\n", vpeproc->bufs_num);

   for (i = 0; i < vpeproc->bufs_num; i++) {
      if (!si_vid_create_buffer(vpeproc->screen, &vpeproc->emb_buffers[i], VPE_EMBBUF_SIZE, PIPE_USAGE_DEFAULT)) {
          SIVPE_ERR("Can't allocated emb_buf buffers.\n");
          goto fail;
      }
      si_vid_clear_buffer(context, &vpeproc->emb_buffers[i]);
   }

   /* Create VPE parameters structure */
   vpeproc->vpe_build_param = CALLOC_STRUCT(vpe_build_param);
   if (!vpeproc->vpe_build_param) {
      SIVPE_ERR("Allocate build-paramaters sturcture failed\n");
      goto fail;
   }

   /* Only one input frame is passed in for processing at a time (one stream pipe).
    * Only needs to handle one stream processing.
    */
   vpeproc->vpe_build_param->streams = (struct vpe_stream *)CALLOC(VPE_STREAM_MAX_NUM, sizeof(struct vpe_stream));
   if (!vpeproc->vpe_build_param->streams) {
      SIVPE_ERR("Allocate streams sturcture failed\n");
      goto fail;
   }

   return &vpeproc->base;

fail:
   SIVPE_ERR("Failed\n");
   if (vpeproc) {
      si_vpe_processor_destroy(&vpeproc->base);
   }
   return NULL;
}
