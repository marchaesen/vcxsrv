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

#include "d3d12_bufmgr.h"
#include "d3d12_compiler.h"
#include "d3d12_context.h"
#include "d3d12_debug.h"
#include "d3d12_fence.h"
#include "d3d12_format.h"
#include "d3d12_resource.h"
#include "d3d12_nir_passes.h"

#include "pipebuffer/pb_bufmgr.h"
#include "util/debug.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "util/u_dl.h"

#include "nir.h"
#include "frontend/sw_winsys.h"

#include <directx/d3d12sdklayers.h>

#include <dxguids/dxguids.h>
static GUID OpenGLOn12CreatorID = { 0x6bb3cd34, 0x0d19, 0x45ab, 0x97, 0xed, 0xd7, 0x20, 0xba, 0x3d, 0xfc, 0x80 };

static const struct debug_named_value
d3d12_debug_options[] = {
   { "verbose",      D3D12_DEBUG_VERBOSE,       NULL },
   { "blit",         D3D12_DEBUG_BLIT,          "Trace blit and copy resource calls" },
   { "experimental", D3D12_DEBUG_EXPERIMENTAL,  "Enable experimental shader models feature" },
   { "dxil",         D3D12_DEBUG_DXIL,          "Dump DXIL during program compile" },
   { "disass",       D3D12_DEBUG_DISASS,        "Dump disassambly of created DXIL shader" },
   { "res",          D3D12_DEBUG_RESOURCE,      "Debug resources" },
   { "debuglayer",   D3D12_DEBUG_DEBUG_LAYER,   "Enable debug layer" },
   { "gpuvalidator", D3D12_DEBUG_GPU_VALIDATOR, "Enable GPU validator" },
   DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(d3d12_debug, "D3D12_DEBUG", d3d12_debug_options, 0)

uint32_t
d3d12_debug;

enum {
    HW_VENDOR_AMD                   = 0x1002,
    HW_VENDOR_INTEL                 = 0x8086,
    HW_VENDOR_MICROSOFT             = 0x1414,
    HW_VENDOR_NVIDIA                = 0x10de,
};

static const char *
d3d12_get_vendor(struct pipe_screen *pscreen)
{
   return "Microsoft Corporation";
}

static const char *
d3d12_get_device_vendor(struct pipe_screen *pscreen)
{
   struct d3d12_screen* screen = d3d12_screen(pscreen);

   switch (screen->vendor_id) {
   case HW_VENDOR_MICROSOFT:
      return "Microsoft";
   case HW_VENDOR_AMD:
      return "AMD";
   case HW_VENDOR_NVIDIA:
      return "NVIDIA";
   case HW_VENDOR_INTEL:
      return "Intel";
   default:
      return "Unknown";
   }
}

static int
d3d12_get_video_mem(struct pipe_screen *pscreen)
{
   struct d3d12_screen* screen = d3d12_screen(pscreen);

   return screen->memory_size_megabytes;
}

static int
d3d12_get_param(struct pipe_screen *pscreen, enum pipe_cap param)
{
   struct d3d12_screen *screen = d3d12_screen(pscreen);

   switch (param) {
   case PIPE_CAP_NPOT_TEXTURES:
      return 1;

   case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
      /* D3D12 only supports dual-source blending for a single
       * render-target. From the D3D11 functional spec (which also defines
       * this for D3D12):
       *
       * "When Dual Source Color Blending is enabled, the Pixel Shader must
       *  have only a single RenderTarget bound, at slot 0, and must output
       *  both o0 and o1. Writing to other outputs (o2, o3 etc.) produces
       *  undefined results for the corresponding RenderTargets, if bound
       *  illegally."
       *
       * Source: https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#17.6%20Dual%20Source%20Color%20Blending
       */
      return 1;

   case PIPE_CAP_ANISOTROPIC_FILTER:
      return 1;

   case PIPE_CAP_MAX_RENDER_TARGETS:
      if (screen->max_feature_level >= D3D_FEATURE_LEVEL_10_0)
         return 8;
      else if (screen->max_feature_level == D3D_FEATURE_LEVEL_9_3)
         return 4;
      return 1;

   case PIPE_CAP_TEXTURE_SWIZZLE:
      return 1;

   case PIPE_CAP_MAX_TEXTURE_2D_SIZE:
      if (screen->max_feature_level >= D3D_FEATURE_LEVEL_11_0)
         return 16384;
      else if (screen->max_feature_level >= D3D_FEATURE_LEVEL_10_0)
         return 8192;
      else if (screen->max_feature_level >= D3D_FEATURE_LEVEL_9_3)
         return 4096;
      return 2048;

   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
      if (screen->max_feature_level >= D3D_FEATURE_LEVEL_10_0)
         return 11;
      return 9;

   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      if (screen->max_feature_level >= D3D_FEATURE_LEVEL_11_0)
         return 14;
      else if (screen->max_feature_level >= D3D_FEATURE_LEVEL_10_0)
         return 13;
      else if (screen->max_feature_level == D3D_FEATURE_LEVEL_9_3)
         return 12;
      return 9;

   case PIPE_CAP_PRIMITIVE_RESTART:
   case PIPE_CAP_INDEP_BLEND_ENABLE:
   case PIPE_CAP_INDEP_BLEND_FUNC:
   case PIPE_CAP_FRAGMENT_SHADER_TEXTURE_LOD:
   case PIPE_CAP_FRAGMENT_SHADER_DERIVATIVES:
   case PIPE_CAP_VERTEX_SHADER_SATURATE:
   case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
   case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_RGB_OVERRIDE_DST_ALPHA_BLEND:
      return 1;

   /* We need to do some lowering that requires a link to the sampler */
   case PIPE_CAP_NIR_SAMPLERS_AS_DEREF:
      return 1;

   case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
      if (screen->max_feature_level >= D3D_FEATURE_LEVEL_11_0)
         return 1 << 14;
      else if (screen->max_feature_level >= D3D_FEATURE_LEVEL_10_0)
         return 1 << 13;
      return 0;

   case PIPE_CAP_DEPTH_CLIP_DISABLE:
      return 1;

   case PIPE_CAP_TGSI_TEXCOORD:
      return 0;

   case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
      return 1;

   case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
      return 1;

   case PIPE_CAP_GLSL_FEATURE_LEVEL:
      return 330;
   case PIPE_CAP_GLSL_FEATURE_LEVEL_COMPATIBILITY:
      return 140;

#if 0 /* TODO: Enable me */
   case PIPE_CAP_COMPUTE:
      return 0;
#endif

   case PIPE_CAP_TEXTURE_MULTISAMPLE:
      return 1;

#if 0 /* TODO: Enable me */
   case PIPE_CAP_CUBE_MAP_ARRAY:
      return screen->max_feature_level >= D3D_FEATURE_LEVEL_10_1;
#endif

   case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
      return 1;

   case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
      return 0; /* unsure */

   case PIPE_CAP_ENDIANNESS:
      return PIPE_ENDIAN_NATIVE; /* unsure */

   case PIPE_CAP_MAX_VIEWPORTS:
      return 1; /* probably wrong */

   case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
      return 1;

#if 0 /* TODO: Enable me. Enables ARB_texture_gather */
   case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS:
      return 4;
#endif

   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
      return 1;

   case PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL:
      return 1;

   case PIPE_CAP_ACCELERATED:
      return 1;

   case PIPE_CAP_VIDEO_MEMORY:
      return d3d12_get_video_mem(pscreen);

   case PIPE_CAP_UMA:
      return screen->architecture.UMA;

   case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
      return 2048; /* FIXME: no clue how to query this */

   case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
   case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
      return 1;

#if 0 /* TODO: Enable me. Enables GL_ARB_shader_storage_buffer_object */
   case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
      return screen->max_feature_level >= D3D_FEATURE_LEVEL_10_0;
#endif

   case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
      return 256;

   case PIPE_CAP_PCI_GROUP:
   case PIPE_CAP_PCI_BUS:
   case PIPE_CAP_PCI_DEVICE:
   case PIPE_CAP_PCI_FUNCTION:
      return 0; /* TODO: figure these out */

   case PIPE_CAP_GLSL_OPTIMIZE_CONSERVATIVELY:
      return 0; /* not sure */

   case PIPE_CAP_FLATSHADE:
   case PIPE_CAP_ALPHA_TEST:
   case PIPE_CAP_TWO_SIDED_COLOR:
   case PIPE_CAP_CLIP_PLANES:
      return 0;

   case PIPE_CAP_SHADER_STENCIL_EXPORT:
      return screen->opts.PSSpecifiedStencilRefSupported;

   case PIPE_CAP_SEAMLESS_CUBE_MAP:
   case PIPE_CAP_TEXTURE_QUERY_LOD:
   case PIPE_CAP_TGSI_INSTANCEID:
   case PIPE_CAP_TGSI_TEX_TXF_LZ:
   case PIPE_CAP_OCCLUSION_QUERY:
   case PIPE_CAP_POINT_SPRITE:
   case PIPE_CAP_VIEWPORT_TRANSFORM_LOWERED:
   case PIPE_CAP_PSIZ_CLAMPED:
   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
   case PIPE_CAP_CONDITIONAL_RENDER:
   case PIPE_CAP_QUERY_TIMESTAMP:
   case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
   case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
      return 1;

   case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
      return 4;

   case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
   case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
      return 16 * 4;

   /* Geometry shader output. */
   case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
      return 256;
   case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
      return 256 * 4;

   case PIPE_CAP_MAX_VARYINGS:
      return 32;

   case PIPE_CAP_NIR_COMPACT_ARRAYS:
      return 1;

   default:
      return u_pipe_screen_get_param_defaults(pscreen, param);
   }
}

static float
d3d12_get_paramf(struct pipe_screen *pscreen, enum pipe_capf param)
{
   struct d3d12_screen *screen = d3d12_screen(pscreen);

   switch (param) {
   case PIPE_CAPF_MAX_LINE_WIDTH:
   case PIPE_CAPF_MAX_LINE_WIDTH_AA:
      return 1.0f; /* no clue */

   case PIPE_CAPF_MAX_POINT_WIDTH:
   case PIPE_CAPF_MAX_POINT_WIDTH_AA:
      return D3D12_MAX_POINT_SIZE;

   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      return screen->max_feature_level >= D3D_FEATURE_LEVEL_10_0 ? 16.0f : 2.0f;

   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return 15.99f;

   case PIPE_CAPF_MIN_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_MAX_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_CONSERVATIVE_RASTER_DILATE_GRANULARITY:
      return 0.0f; /* not implemented */

   default:
      unreachable("unknown pipe_capf");
   }

   return 0.0;
}

static int
d3d12_get_shader_param(struct pipe_screen *pscreen,
                       enum pipe_shader_type shader,
                       enum pipe_shader_cap param)
{
   struct d3d12_screen *screen = d3d12_screen(pscreen);

   switch (param) {
   case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
   case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
      if (shader == PIPE_SHADER_VERTEX ||
          shader == PIPE_SHADER_FRAGMENT ||
          shader == PIPE_SHADER_GEOMETRY)
         return INT_MAX;
      return 0;

   case PIPE_SHADER_CAP_MAX_INPUTS:
      return screen->max_feature_level >= D3D_FEATURE_LEVEL_10_1 ? 32 : 16;

   case PIPE_SHADER_CAP_MAX_OUTPUTS:
      if (shader == PIPE_SHADER_FRAGMENT) {
         /* same as max MRTs (not sure if this is correct) */
         if (screen->max_feature_level >= D3D_FEATURE_LEVEL_10_0)
            return 8;
         else if (screen->max_feature_level == D3D_FEATURE_LEVEL_9_3)
            return 4;
         return 1;
      }
      return screen->max_feature_level >= D3D_FEATURE_LEVEL_10_1 ? 32 : 16;

   case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
      if (screen->opts.ResourceBindingTier == D3D12_RESOURCE_BINDING_TIER_1)
         return 16;
      return PIPE_MAX_SAMPLERS;

   case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
      return 65536;

   case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
      return 13; /* 15 - 2 for lowered uniforms and state vars*/

   case PIPE_SHADER_CAP_MAX_TEMPS:
      return INT_MAX;

   case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
   case PIPE_SHADER_CAP_SUBROUTINES:
      return 0; /* not implemented */

   case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
   case PIPE_SHADER_CAP_INTEGERS:
      return 1;

   case PIPE_SHADER_CAP_INT64_ATOMICS:
   case PIPE_SHADER_CAP_FP16:
      return 0; /* not implemented */

   case PIPE_SHADER_CAP_PREFERRED_IR:
      return PIPE_SHADER_IR_NIR;

   case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
      return 0; /* not implemented */

   case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
      if (screen->opts.ResourceBindingTier == D3D12_RESOURCE_BINDING_TIER_1)
         return 128;
      return PIPE_MAX_SHADER_SAMPLER_VIEWS;

   case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
      return 0; /* not implemented */

   case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
      return 0; /* no idea */

   case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
      return 32; /* arbitrary */

#if 0
   case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
      return 8; /* no clue */
#endif

   case PIPE_SHADER_CAP_SUPPORTED_IRS:
      return 1 << PIPE_SHADER_IR_NIR;

   case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
      return 0; /* TODO: enable me */

   case PIPE_SHADER_CAP_LOWER_IF_THRESHOLD:
   case PIPE_SHADER_CAP_TGSI_SKIP_MERGE_REGISTERS:
      return 0; /* unsure */

   case PIPE_SHADER_CAP_TGSI_LDEXP_SUPPORTED:
   case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTERS:
   case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTER_BUFFERS:
   case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
      return 0; /* not implemented */

   /* should only get here on unhandled cases */
   default: return 0;
   }
}

static bool
d3d12_is_format_supported(struct pipe_screen *pscreen,
                          enum pipe_format format,
                          enum pipe_texture_target target,
                          unsigned sample_count,
                          unsigned storage_sample_count,
                          unsigned bind)
{
   struct d3d12_screen *screen = d3d12_screen(pscreen);

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   if (target == PIPE_BUFFER) {
      /* Replace emulated vertex element formats for the tests */
      format = d3d12_emulated_vtx_format(format);
   } else {
      /* Allow 3-comp 32 bit formats only for BOs (needed for ARB_tbo_rgb32) */
      if ((format == PIPE_FORMAT_R32G32B32_FLOAT ||
           format == PIPE_FORMAT_R32G32B32_SINT ||
           format == PIPE_FORMAT_R32G32B32_UINT))
         return false;
   }

   /* Don't advertise alpha/luminance_alpha formats because they can't be used
    * for render targets (except A8_UNORM) and can't be emulated by R/RG formats.
    * Let the state tracker choose an RGBA format instead. */
   if (format != PIPE_FORMAT_A8_UNORM &&
       (util_format_is_alpha(format) ||
        util_format_is_luminance_alpha(format)))
      return false;

   DXGI_FORMAT dxgi_format = d3d12_get_format(format);
   if (dxgi_format == DXGI_FORMAT_UNKNOWN)
      return false;

   enum D3D12_FORMAT_SUPPORT1 dim_support = D3D12_FORMAT_SUPPORT1_NONE;
   switch (target) {
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_1D_ARRAY:
      dim_support = D3D12_FORMAT_SUPPORT1_TEXTURE1D;
      break;
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D_ARRAY:
      dim_support = D3D12_FORMAT_SUPPORT1_TEXTURE2D;
      break;
   case PIPE_TEXTURE_3D:
      dim_support = D3D12_FORMAT_SUPPORT1_TEXTURE3D;
      break;
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
      dim_support = D3D12_FORMAT_SUPPORT1_TEXTURECUBE;
      break;
   case PIPE_BUFFER:
      dim_support = D3D12_FORMAT_SUPPORT1_BUFFER;
      break;
   default:
      unreachable("Unknown target");
   }

   D3D12_FEATURE_DATA_FORMAT_SUPPORT fmt_info;
   fmt_info.Format = d3d12_get_resource_rt_format(format);
   if (FAILED(screen->dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                               &fmt_info, sizeof(fmt_info))))
      return false;

   if (!(fmt_info.Support1 & dim_support))
      return false;

   if (target == PIPE_BUFFER) {
      if (bind & PIPE_BIND_VERTEX_BUFFER &&
          !(fmt_info.Support1 & D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER))
         return false;

      if (bind & PIPE_BIND_INDEX_BUFFER) {
         if (format != PIPE_FORMAT_R8_UINT &&
             format != PIPE_FORMAT_R16_UINT &&
             format != PIPE_FORMAT_R32_UINT)
            return false;
      }

      if (sample_count > 0)
         return false;
   } else {
      /* all other targets are texture-targets */
      if (bind & PIPE_BIND_RENDER_TARGET &&
          !(fmt_info.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
         return false;

      if (bind & PIPE_BIND_BLENDABLE &&
         !(fmt_info.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE))
         return false;

      D3D12_FEATURE_DATA_FORMAT_SUPPORT fmt_info_sv;
      if (util_format_is_depth_or_stencil(format)) {
         fmt_info_sv.Format = d3d12_get_resource_srv_format(format, target);
         if (FAILED(screen->dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                                     &fmt_info_sv, sizeof(fmt_info_sv))))
            return false;
      } else
         fmt_info_sv = fmt_info;

      if (bind & PIPE_BIND_DISPLAY_TARGET &&
         (!(fmt_info.Support1 & D3D12_FORMAT_SUPPORT1_DISPLAY) ||
            // Disable formats that don't support flip model
            dxgi_format == DXGI_FORMAT_B8G8R8X8_UNORM ||
            dxgi_format == DXGI_FORMAT_B5G5R5A1_UNORM ||
            dxgi_format == DXGI_FORMAT_B5G6R5_UNORM ||
            dxgi_format == DXGI_FORMAT_B4G4R4A4_UNORM))
         return false;

      if (bind & PIPE_BIND_DEPTH_STENCIL &&
          !(fmt_info.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL))
            return false;

      if (sample_count > 0) {
         if (!(fmt_info_sv.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD))
            return false;

         if (!util_is_power_of_two_nonzero(sample_count))
            return false;

         D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms_info = {};
         ms_info.Format = dxgi_format;
         ms_info.SampleCount = sample_count;
         if (FAILED(screen->dev->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                                                     &ms_info,
                                                     sizeof(ms_info))) ||
             !ms_info.NumQualityLevels)
            return false;
      }
   }
   return true;
}

static void
d3d12_destroy_screen(struct pipe_screen *pscreen)
{
   struct d3d12_screen *screen = d3d12_screen(pscreen);
   slab_destroy_parent(&screen->transfer_pool);
   d3d12_descriptor_pool_free(screen->rtv_pool);
   d3d12_descriptor_pool_free(screen->dsv_pool);
   d3d12_descriptor_pool_free(screen->view_pool);
   screen->readback_slab_bufmgr->destroy(screen->readback_slab_bufmgr);
   screen->slab_bufmgr->destroy(screen->slab_bufmgr);
   screen->cache_bufmgr->destroy(screen->cache_bufmgr);
   screen->bufmgr->destroy(screen->bufmgr);
   mtx_destroy(&screen->descriptor_pool_mutex);
   FREE(screen);
}

static void
d3d12_flush_frontbuffer(struct pipe_screen * pscreen,
                        struct pipe_context *pctx,
                        struct pipe_resource *pres,
                        unsigned level, unsigned layer,
                        void *winsys_drawable_handle,
                        struct pipe_box *sub_box)
{
   struct d3d12_screen *screen = d3d12_screen(pscreen);
   struct sw_winsys *winsys = screen->winsys;
   struct d3d12_resource *res = d3d12_resource(pres);

   if (!winsys || !pctx)
     return;

   assert(res->dt);
   void *map = winsys->displaytarget_map(winsys, res->dt, 0);

   if (map) {
      pipe_transfer *transfer = nullptr;
      void *res_map = pipe_texture_map(pctx, pres, level, layer, PIPE_MAP_READ, 0, 0,
                                        u_minify(pres->width0, level),
                                        u_minify(pres->height0, level),
                                        &transfer);
      if (res_map) {
         util_copy_rect((ubyte*)map, pres->format, res->dt_stride, 0, 0,
                        transfer->box.width, transfer->box.height,
                        (const ubyte*)res_map, transfer->stride, 0, 0);
         pipe_texture_unmap(pctx, transfer);
      }
      winsys->displaytarget_unmap(winsys, res->dt);
   }

#ifdef _WIN32
   // WindowFromDC is Windows-only, and this method requires an HWND, so only use it on Windows
   ID3D12SharingContract *sharing_contract;
   if (SUCCEEDED(screen->cmdqueue->QueryInterface(IID_PPV_ARGS(&sharing_contract)))) {
      ID3D12Resource *d3d12_res = d3d12_resource_resource(res);
      sharing_contract->Present(d3d12_res, 0, WindowFromDC((HDC)winsys_drawable_handle));
   }
#endif

   winsys->displaytarget_display(winsys, res->dt, winsys_drawable_handle, sub_box);
}

static ID3D12Debug *
get_debug_interface()
{
   typedef HRESULT(WINAPI *PFN_D3D12_GET_DEBUG_INTERFACE)(REFIID riid, void **ppFactory);
   PFN_D3D12_GET_DEBUG_INTERFACE D3D12GetDebugInterface;

   util_dl_library *d3d12_mod = util_dl_open(UTIL_DL_PREFIX "d3d12" UTIL_DL_EXT);
   if (!d3d12_mod) {
      debug_printf("D3D12: failed to load D3D12.DLL\n");
      return NULL;
   }

   D3D12GetDebugInterface = (PFN_D3D12_GET_DEBUG_INTERFACE)util_dl_get_proc_address(d3d12_mod, "D3D12GetDebugInterface");
   if (!D3D12GetDebugInterface) {
      debug_printf("D3D12: failed to load D3D12GetDebugInterface from D3D12.DLL\n");
      return NULL;
   }

   ID3D12Debug *debug;
   if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
      debug_printf("D3D12: D3D12GetDebugInterface failed\n");
      return NULL;
   }

   return debug;
}

static void
enable_d3d12_debug_layer()
{
   ID3D12Debug *debug = get_debug_interface();
   if (debug)
      debug->EnableDebugLayer();
}

static void
enable_gpu_validation()
{
   ID3D12Debug *debug = get_debug_interface();
   ID3D12Debug3 *debug3;
   if (debug &&
       SUCCEEDED(debug->QueryInterface(IID_PPV_ARGS(&debug3))))
      debug3->SetEnableGPUBasedValidation(true);
}

static ID3D12Device *
create_device(IUnknown *adapter)
{
   typedef HRESULT(WINAPI *PFN_D3D12CREATEDEVICE)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
   typedef HRESULT(WINAPI *PFN_D3D12ENABLEEXPERIMENTALFEATURES)(UINT, const IID*, void*, UINT*);
   PFN_D3D12CREATEDEVICE D3D12CreateDevice;
   PFN_D3D12ENABLEEXPERIMENTALFEATURES D3D12EnableExperimentalFeatures;

   util_dl_library *d3d12_mod = util_dl_open(UTIL_DL_PREFIX "d3d12" UTIL_DL_EXT);
   if (!d3d12_mod) {
      debug_printf("D3D12: failed to load D3D12.DLL\n");
      return NULL;
   }

#ifdef _WIN32
   if (!(d3d12_debug & D3D12_DEBUG_EXPERIMENTAL)) {
      struct d3d12_validation_tools *validation_tools = d3d12_validator_create();
      if (!validation_tools) {
         debug_printf("D3D12: failed to initialize validator with experimental shader models disabled\n");
         return nullptr;
      }
      d3d12_validator_destroy(validation_tools);
   } else
#endif
   {
      D3D12EnableExperimentalFeatures = (PFN_D3D12ENABLEEXPERIMENTALFEATURES)util_dl_get_proc_address(d3d12_mod, "D3D12EnableExperimentalFeatures");
      if (FAILED(D3D12EnableExperimentalFeatures(1, &D3D12ExperimentalShaderModels, NULL, NULL))) {
         debug_printf("D3D12: failed to enable experimental shader models\n");
         return nullptr;
      }
   }

   D3D12CreateDevice = (PFN_D3D12CREATEDEVICE)util_dl_get_proc_address(d3d12_mod, "D3D12CreateDevice");
   if (!D3D12CreateDevice) {
      debug_printf("D3D12: failed to load D3D12CreateDevice from D3D12.DLL\n");
      return NULL;
   }

   ID3D12Device *dev;
   if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                 IID_PPV_ARGS(&dev))))
      return dev;

   debug_printf("D3D12: D3D12CreateDevice failed\n");
   return NULL;
}

static bool
can_attribute_at_vertex(struct d3d12_screen *screen)
{
   switch (screen->vendor_id)  {
   case HW_VENDOR_MICROSOFT:
      return true;
   default:
      return screen->opts3.BarycentricsSupported;
   }
}

static void
d3d12_init_null_srvs(struct d3d12_screen *screen)
{
   for (unsigned i = 0; i < RESOURCE_DIMENSION_COUNT; ++i) {
      D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};

      srv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
      srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      switch (i) {
      case RESOURCE_DIMENSION_BUFFER:
      case RESOURCE_DIMENSION_UNKNOWN:
         srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
         srv.Buffer.FirstElement = 0;
         srv.Buffer.NumElements = 0;
         srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
         srv.Buffer.StructureByteStride = 0;
         break;
      case RESOURCE_DIMENSION_TEXTURE1D:
         srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
         srv.Texture1D.MipLevels = 1;
         srv.Texture1D.MostDetailedMip = 0;
         srv.Texture1D.ResourceMinLODClamp = 0.0f;
         break;
      case RESOURCE_DIMENSION_TEXTURE1DARRAY:
         srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
         srv.Texture1DArray.MipLevels = 1;
         srv.Texture1DArray.ArraySize = 1;
         srv.Texture1DArray.MostDetailedMip = 0;
         srv.Texture1DArray.FirstArraySlice = 0;
         srv.Texture1DArray.ResourceMinLODClamp = 0.0f;
         break;
      case RESOURCE_DIMENSION_TEXTURE2D:
         srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
         srv.Texture2D.MipLevels = 1;
         srv.Texture2D.MostDetailedMip = 0;
         srv.Texture2D.PlaneSlice = 0;
         srv.Texture2D.ResourceMinLODClamp = 0.0f;
         break;
      case RESOURCE_DIMENSION_TEXTURE2DARRAY:
         srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
         srv.Texture2DArray.MipLevels = 1;
         srv.Texture2DArray.ArraySize = 1;
         srv.Texture2DArray.MostDetailedMip = 0;
         srv.Texture2DArray.FirstArraySlice = 0;
         srv.Texture2DArray.PlaneSlice = 0;
         srv.Texture2DArray.ResourceMinLODClamp = 0.0f;
         break;
      case RESOURCE_DIMENSION_TEXTURE2DMS:
         srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
         break;
      case RESOURCE_DIMENSION_TEXTURE2DMSARRAY:
         srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
         srv.Texture2DMSArray.ArraySize = 1;
         srv.Texture2DMSArray.FirstArraySlice = 0;
         break;
      case RESOURCE_DIMENSION_TEXTURE3D:
         srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
         srv.Texture3D.MipLevels = 1;
         srv.Texture3D.MostDetailedMip = 0;
         srv.Texture3D.ResourceMinLODClamp = 0.0f;
         break;
      case RESOURCE_DIMENSION_TEXTURECUBE:
         srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
         srv.TextureCube.MipLevels = 1;
         srv.TextureCube.MostDetailedMip = 0;
         srv.TextureCube.ResourceMinLODClamp = 0.0f;
         break;
      case RESOURCE_DIMENSION_TEXTURECUBEARRAY:
         srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
         srv.TextureCubeArray.MipLevels = 1;
         srv.TextureCubeArray.NumCubes = 1;
         srv.TextureCubeArray.MostDetailedMip = 0;
         srv.TextureCubeArray.First2DArrayFace = 0;
         srv.TextureCubeArray.ResourceMinLODClamp = 0.0f;
         break;
      }

      if (srv.ViewDimension != D3D12_SRV_DIMENSION_UNKNOWN)
      {
         d3d12_descriptor_pool_alloc_handle(screen->view_pool, &screen->null_srvs[i]);
         screen->dev->CreateShaderResourceView(NULL, &srv, screen->null_srvs[i].cpu_handle);
      }
   }
}

static void
d3d12_init_null_rtv(struct d3d12_screen *screen)
{
   D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
   rtv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
   rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
   rtv.Texture2D.MipSlice = 0;
   rtv.Texture2D.PlaneSlice = 0;
   d3d12_descriptor_pool_alloc_handle(screen->rtv_pool, &screen->null_rtv);
   screen->dev->CreateRenderTargetView(NULL, &rtv, screen->null_rtv.cpu_handle);
}

bool
d3d12_init_screen(struct d3d12_screen *screen, struct sw_winsys *winsys, IUnknown *adapter)
{
   d3d12_debug = debug_get_option_d3d12_debug();

   screen->winsys = winsys;
   mtx_init(&screen->descriptor_pool_mutex, mtx_plain);

   screen->base.get_vendor = d3d12_get_vendor;
   screen->base.get_device_vendor = d3d12_get_device_vendor;
   screen->base.get_param = d3d12_get_param;
   screen->base.get_paramf = d3d12_get_paramf;
   screen->base.get_shader_param = d3d12_get_shader_param;
   screen->base.is_format_supported = d3d12_is_format_supported;
   screen->base.get_compiler_options = d3d12_get_compiler_options;
   screen->base.context_create = d3d12_context_create;
   screen->base.flush_frontbuffer = d3d12_flush_frontbuffer;
   screen->base.destroy = d3d12_destroy_screen;

#ifndef DEBUG
   if (d3d12_debug & D3D12_DEBUG_DEBUG_LAYER)
#endif
      enable_d3d12_debug_layer();

   if (d3d12_debug & D3D12_DEBUG_GPU_VALIDATOR)
      enable_gpu_validation();

   screen->dev = create_device(adapter);

   if (!screen->dev) {
      debug_printf("D3D12: failed to create device\n");
      goto failed;
   }

   ID3D12InfoQueue *info_queue;
   if (SUCCEEDED(screen->dev->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
      D3D12_MESSAGE_SEVERITY severities[] = {
         D3D12_MESSAGE_SEVERITY_INFO,
         D3D12_MESSAGE_SEVERITY_WARNING,
      };

      D3D12_MESSAGE_ID msg_ids[] = {
         D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
      };

      D3D12_INFO_QUEUE_FILTER NewFilter = {};
      NewFilter.DenyList.NumSeverities = ARRAY_SIZE(severities);
      NewFilter.DenyList.pSeverityList = severities;
      NewFilter.DenyList.NumIDs = ARRAY_SIZE(msg_ids);
      NewFilter.DenyList.pIDList = msg_ids;

      info_queue->PushStorageFilter(&NewFilter);
   }

   if (FAILED(screen->dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,
                                               &screen->opts,
                                               sizeof(screen->opts)))) {
      debug_printf("D3D12: failed to get device options\n");
      goto failed;
   }
   if (FAILED(screen->dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2,
                                               &screen->opts2,
                                               sizeof(screen->opts2)))) {
      debug_printf("D3D12: failed to get device options\n");
      goto failed;
   }
   if (FAILED(screen->dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3,
                                               &screen->opts3,
                                               sizeof(screen->opts3)))) {
      debug_printf("D3D12: failed to get device options\n");
      goto failed;
   }
   if (FAILED(screen->dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4,
                                               &screen->opts4,
                                               sizeof(screen->opts4)))) {
      debug_printf("D3D12: failed to get device options\n");
      goto failed;
   }

   screen->architecture.NodeIndex = 0;
   if (FAILED(screen->dev->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE,
                                               &screen->architecture,
                                               sizeof(screen->architecture)))) {
      debug_printf("D3D12: failed to get device architecture\n");
      goto failed;
   }

   D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels;
   static const D3D_FEATURE_LEVEL levels[] = {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_12_1,
   };
   feature_levels.NumFeatureLevels = ARRAY_SIZE(levels);
   feature_levels.pFeatureLevelsRequested = levels;
   if (FAILED(screen->dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,
                                               &feature_levels,
                                               sizeof(feature_levels)))) {
      debug_printf("D3D12: failed to get device feature levels\n");
      goto failed;
   }
   screen->max_feature_level = feature_levels.MaxSupportedFeatureLevel;

   D3D12_COMMAND_QUEUE_DESC queue_desc;
   queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
   queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
   queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
   queue_desc.NodeMask = 0;

   ID3D12Device9 *device9;
   if (SUCCEEDED(screen->dev->QueryInterface(&device9))) {
      if (FAILED(device9->CreateCommandQueue1(&queue_desc, OpenGLOn12CreatorID,
                                              IID_PPV_ARGS(&screen->cmdqueue))))
         goto failed;
      device9->Release();
   } else {
      if (FAILED(screen->dev->CreateCommandQueue(&queue_desc,
                                                 IID_PPV_ARGS(&screen->cmdqueue))))
         goto failed;
   }

   UINT64 timestamp_freq;
   if (FAILED(screen->cmdqueue->GetTimestampFrequency(&timestamp_freq)))
       timestamp_freq = 10000000;
   screen->timestamp_multiplier = 1000000000.0 / timestamp_freq;

   d3d12_screen_fence_init(&screen->base);
   d3d12_screen_resource_init(&screen->base);
   slab_create_parent(&screen->transfer_pool, sizeof(struct d3d12_transfer), 16);

   struct pb_desc desc;
   desc.alignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
   desc.usage = (pb_usage_flags)(PB_USAGE_CPU_WRITE | PB_USAGE_GPU_READ);

   screen->bufmgr = d3d12_bufmgr_create(screen);
   screen->cache_bufmgr = pb_cache_manager_create(screen->bufmgr, 0xfffff, 2, 0, 64 * 1024 * 1024);
   screen->slab_bufmgr = pb_slab_range_manager_create(screen->cache_bufmgr, 16, 512,
                                                      D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                                                      &desc);
   desc.usage = (pb_usage_flags)(PB_USAGE_CPU_READ_WRITE | PB_USAGE_GPU_WRITE);
   screen->readback_slab_bufmgr = pb_slab_range_manager_create(screen->cache_bufmgr, 16, 512,
                                                               D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                                                               &desc);

   screen->rtv_pool = d3d12_descriptor_pool_new(screen,
                                                D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                                64);
   screen->dsv_pool = d3d12_descriptor_pool_new(screen,
                                                D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                                                64);
   screen->view_pool = d3d12_descriptor_pool_new(screen,
                                                 D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                 1024);

   d3d12_init_null_srvs(screen);
   d3d12_init_null_rtv(screen);

   screen->have_load_at_vertex = can_attribute_at_vertex(screen);
   return true;

failed:
   return false;
}
