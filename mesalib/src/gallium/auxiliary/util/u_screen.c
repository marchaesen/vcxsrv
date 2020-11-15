/*
 * Copyright © 2018 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "pipe/p_screen.h"
#include "util/u_screen.h"

/**
 * Helper to use from a pipe_screen->get_param() implementation to return
 * default values for unsupported PIPE_CAPs.
 *
 * Call this function from your pipe_screen->get_param() implementation's
 * default case, so that implementors of new pipe caps don't need to
 */
int
u_pipe_screen_get_param_defaults(struct pipe_screen *pscreen,
                                 enum pipe_cap param)
{
   /* Let's keep these sorted by position in p_defines.h. */
   switch (param) {
   case PIPE_CAP_NPOT_TEXTURES:
   case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
   case PIPE_CAP_ANISOTROPIC_FILTER:
   case PIPE_CAP_POINT_SPRITE:
      return 0;

   case PIPE_CAP_GRAPHICS:
   case PIPE_CAP_MAX_RENDER_TARGETS:
      return 1;

   case PIPE_CAP_OCCLUSION_QUERY:
   case PIPE_CAP_QUERY_TIME_ELAPSED:
   case PIPE_CAP_TEXTURE_SWIZZLE:
      return 0;

   case PIPE_CAP_MAX_TEXTURE_2D_SIZE:
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      unreachable("driver must implement these.");

   case PIPE_CAP_TEXTURE_MIRROR_CLAMP:
   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
   case PIPE_CAP_FRAGMENT_SHADER_TEXTURE_LOD:
   case PIPE_CAP_FRAGMENT_SHADER_DERIVATIVES:
   case PIPE_CAP_VERTEX_SHADER_SATURATE:
   case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS: /* enables EXT_transform_feedback */
   case PIPE_CAP_PRIMITIVE_RESTART:
   case PIPE_CAP_PRIMITIVE_RESTART_FIXED_INDEX:
   case PIPE_CAP_INDEP_BLEND_ENABLE:
   case PIPE_CAP_INDEP_BLEND_FUNC:
   case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS: /* Enables GL_EXT_texture_array */
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
   case PIPE_CAP_DEPTH_CLIP_DISABLE:
   case PIPE_CAP_DEPTH_CLIP_DISABLE_SEPARATE:
   case PIPE_CAP_SHADER_STENCIL_EXPORT:
   case PIPE_CAP_TGSI_INSTANCEID:
   case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
   case PIPE_CAP_FRAGMENT_COLOR_CLAMPED:
   case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
   case PIPE_CAP_SEAMLESS_CUBE_MAP:
   case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
   case PIPE_CAP_RGB_OVERRIDE_DST_ALPHA_BLEND:
      return 0;

   case PIPE_CAP_MIN_TEXEL_OFFSET:
      /* GL 3.x minimum value. */
      return -8;
   case PIPE_CAP_MAX_TEXEL_OFFSET:
      return 7;

   case PIPE_CAP_CONDITIONAL_RENDER:
   case PIPE_CAP_TEXTURE_BARRIER:
      return 0;

   case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
      /* GL_EXT_transform_feedback minimum value. */
      return 4;
   case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
      return 64;

   case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
   case PIPE_CAP_TGSI_CAN_COMPACT_CONSTANTS:
   case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
   case PIPE_CAP_VERTEX_COLOR_CLAMPED:
      return 0;

   case PIPE_CAP_GLSL_FEATURE_LEVEL:
   case PIPE_CAP_GLSL_FEATURE_LEVEL_COMPATIBILITY:
      /* Minimum GLSL level implemented by gallium drivers. */
      return 120;

   case PIPE_CAP_ESSL_FEATURE_LEVEL:
      /* Tell gallium frontend to fallback to PIPE_CAP_GLSL_FEATURE_LEVEL */
      return 0;

   case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
   case PIPE_CAP_USER_VERTEX_BUFFERS:
   case PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_COMPUTE:
      return 0;

   case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
      /* GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT default value. */
      return 1;

   case PIPE_CAP_START_INSTANCE:
   case PIPE_CAP_QUERY_TIMESTAMP:
   case PIPE_CAP_TEXTURE_MULTISAMPLE:
      return 0;

   case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
      /* GL_ARB_map_buffer_alignment minimum value. All drivers expose the
       * extension.
       */
      return 64;

   case PIPE_CAP_CUBE_MAP_ARRAY:
   case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
      return 0;

   case PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT:
      /* GL_EXT_texture_buffer minimum value. */
      return 256;

   case PIPE_CAP_BUFFER_SAMPLER_VIEW_RGBA_ONLY:
   case PIPE_CAP_TGSI_TEXCOORD:
      return 0;

   case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
      return 1;

   case PIPE_CAP_QUERY_PIPELINE_STATISTICS:
   case PIPE_CAP_QUERY_PIPELINE_STATISTICS_SINGLE:
   case PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK:
      return 0;

   case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
      /* GL_EXT_texture_buffer minimum value. */
      return 65536;

   case PIPE_CAP_MAX_VIEWPORTS:
      return 1;

   case PIPE_CAP_ENDIANNESS:
      return PIPE_ENDIAN_LITTLE;

   case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
   case PIPE_CAP_TGSI_VS_LAYER_VIEWPORT:
   case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
   case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
   case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS: /* Enables ARB_texture_gather */
   case PIPE_CAP_TEXTURE_GATHER_SM5:
   case PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT:
   case PIPE_CAP_FAKE_SW_MSAA:
   case PIPE_CAP_TEXTURE_QUERY_LOD:
      return 0;

   case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
      return -8;
   case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
      return 7;

   case PIPE_CAP_SAMPLE_SHADING:
   case PIPE_CAP_TEXTURE_GATHER_OFFSETS:
   case PIPE_CAP_TGSI_VS_WINDOW_SPACE_POSITION:
   case PIPE_CAP_MAX_VERTEX_STREAMS:
   case PIPE_CAP_DRAW_INDIRECT:
   case PIPE_CAP_TGSI_FS_FINE_DERIVATIVE:
      return 0;

   case PIPE_CAP_VENDOR_ID:
   case PIPE_CAP_DEVICE_ID:
      return 0xffffffff;

   case PIPE_CAP_ACCELERATED:
   case PIPE_CAP_VIDEO_MEMORY:
   case PIPE_CAP_UMA:
      unreachable("driver must implement these.");

   case PIPE_CAP_CONDITIONAL_RENDER_INVERTED:
      return 0;

   case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
      /* GL minimum value */
      return 2048;

   case PIPE_CAP_SAMPLER_VIEW_TARGET:
   case PIPE_CAP_CLIP_HALFZ:
   case PIPE_CAP_VERTEXID_NOBASE:
   case PIPE_CAP_POLYGON_OFFSET_CLAMP:
   case PIPE_CAP_MULTISAMPLE_Z_RESOLVE:
   case PIPE_CAP_RESOURCE_FROM_USER_MEMORY:
   case PIPE_CAP_RESOURCE_FROM_USER_MEMORY_COMPUTE_ONLY:
   case PIPE_CAP_DEVICE_RESET_STATUS_QUERY:
   case PIPE_CAP_DEVICE_PROTECTED_CONTENT:
   case PIPE_CAP_MAX_SHADER_PATCH_VARYINGS:
   case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
   case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
   case PIPE_CAP_DEPTH_BOUNDS_TEST:
   case PIPE_CAP_TGSI_TXQS:
   case PIPE_CAP_FORCE_PERSAMPLE_INTERP:
   case PIPE_CAP_SHAREABLE_SHADERS:
   case PIPE_CAP_COPY_BETWEEN_COMPRESSED_AND_PLAIN_FORMATS:
   case PIPE_CAP_CLEAR_TEXTURE:
   case PIPE_CAP_CLEAR_SCISSORED:
   case PIPE_CAP_DRAW_PARAMETERS:
   case PIPE_CAP_TGSI_PACK_HALF_FLOAT:
   case PIPE_CAP_MULTI_DRAW_INDIRECT:
   case PIPE_CAP_MULTI_DRAW_INDIRECT_PARAMS:
   case PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL:
   case PIPE_CAP_TGSI_FS_POINT_IS_SYSVAL:
   case PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL:
      return 0;

   case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
      /* Enables GL_ARB_shader_storage_buffer_object */
      return 0;

   case PIPE_CAP_INVALIDATE_BUFFER:
   case PIPE_CAP_GENERATE_MIPMAP:
   case PIPE_CAP_STRING_MARKER:
   case PIPE_CAP_SURFACE_REINTERPRET_BLOCKS:
   case PIPE_CAP_QUERY_BUFFER_OBJECT:
   case PIPE_CAP_QUERY_MEMORY_INFO: /* Enables GL_ATI_meminfo */
      return 0;

   case PIPE_CAP_PCI_GROUP:
   case PIPE_CAP_PCI_BUS:
   case PIPE_CAP_PCI_DEVICE:
   case PIPE_CAP_PCI_FUNCTION:
      unreachable("driver must implement these.");

   case PIPE_CAP_FRAMEBUFFER_NO_ATTACHMENT:
   case PIPE_CAP_ROBUST_BUFFER_ACCESS_BEHAVIOR:
   case PIPE_CAP_CULL_DISTANCE:
   case PIPE_CAP_PRIMITIVE_RESTART_FOR_PATCHES:
   case PIPE_CAP_TGSI_VOTE:
   case PIPE_CAP_MAX_WINDOW_RECTANGLES: /* Enables EXT_window_rectangles */
   case PIPE_CAP_POLYGON_OFFSET_UNITS_UNSCALED:
   case PIPE_CAP_VIEWPORT_SUBPIXEL_BITS:
   case PIPE_CAP_VIEWPORT_SWIZZLE:
   case PIPE_CAP_VIEWPORT_MASK:
   case PIPE_CAP_MIXED_COLOR_DEPTH_BITS:
   case PIPE_CAP_TGSI_ARRAY_COMPONENTS:
   case PIPE_CAP_STREAM_OUTPUT_INTERLEAVE_BUFFERS:
   case PIPE_CAP_TGSI_CAN_READ_OUTPUTS:
   case PIPE_CAP_NATIVE_FENCE_FD:
      return 0;

   case PIPE_CAP_RASTERIZER_SUBPIXEL_BITS:
      return 4; /* GLES 2.0 minimum value */

   case PIPE_CAP_GLSL_OPTIMIZE_CONSERVATIVELY:
      return 1;

   case PIPE_CAP_GLSL_TESS_LEVELS_AS_INPUTS:
      return 0;

   case PIPE_CAP_FBFETCH:
   case PIPE_CAP_FBFETCH_COHERENT:
   case PIPE_CAP_BLEND_EQUATION_ADVANCED:
   case PIPE_CAP_TGSI_MUL_ZERO_WINS:
   case PIPE_CAP_DOUBLES:
   case PIPE_CAP_INT64:
   case PIPE_CAP_INT64_DIVMOD:
   case PIPE_CAP_TGSI_TEX_TXF_LZ:
   case PIPE_CAP_TGSI_CLOCK:
   case PIPE_CAP_POLYGON_MODE_FILL_RECTANGLE:
   case PIPE_CAP_SPARSE_BUFFER_PAGE_SIZE:
   case PIPE_CAP_TGSI_BALLOT:
   case PIPE_CAP_TGSI_TES_LAYER_VIEWPORT:
   case PIPE_CAP_CAN_BIND_CONST_BUFFER_AS_VERTEX:
   case PIPE_CAP_TGSI_DIV:
   case PIPE_CAP_NIR_ATOMICS_AS_DEREF:
      return 0;

   case PIPE_CAP_ALLOW_MAPPED_BUFFERS_DURING_EXECUTION:
      /* Drivers generally support this, and it reduces GL overhead just to
       * throw an error when buffers are mapped.
       */
      return 1;

   case PIPE_CAP_PREFER_IMM_ARRAYS_AS_CONSTBUF:
      /* Don't unset this unless your driver can do better */
      return 1;

   case PIPE_CAP_POST_DEPTH_COVERAGE:
   case PIPE_CAP_BINDLESS_TEXTURE:
   case PIPE_CAP_NIR_SAMPLERS_AS_DEREF:
   case PIPE_CAP_NIR_COMPACT_ARRAYS:
   case PIPE_CAP_QUERY_SO_OVERFLOW:
   case PIPE_CAP_MEMOBJ:
   case PIPE_CAP_LOAD_CONSTBUF:
   case PIPE_CAP_TGSI_ANY_REG_AS_ADDRESS:
   case PIPE_CAP_TILE_RASTER_ORDER:
      return 0;

   case PIPE_CAP_MAX_COMBINED_SHADER_OUTPUT_RESOURCES:
      /* nonzero overrides defaults */
      return 0;

   case PIPE_CAP_FRAMEBUFFER_MSAA_CONSTRAINTS:
   case PIPE_CAP_SIGNED_VERTEX_BUFFER_OFFSET:
   case PIPE_CAP_CONTEXT_PRIORITY_MASK:
   case PIPE_CAP_FENCE_SIGNAL:
   case PIPE_CAP_CONSTBUF0_FLAGS:
   case PIPE_CAP_PACKED_UNIFORMS:
   case PIPE_CAP_CONSERVATIVE_RASTER_POST_SNAP_TRIANGLES:
   case PIPE_CAP_CONSERVATIVE_RASTER_POST_SNAP_POINTS_LINES:
   case PIPE_CAP_CONSERVATIVE_RASTER_PRE_SNAP_TRIANGLES:
   case PIPE_CAP_CONSERVATIVE_RASTER_PRE_SNAP_POINTS_LINES:
   case PIPE_CAP_MAX_CONSERVATIVE_RASTER_SUBPIXEL_PRECISION_BIAS:
   case PIPE_CAP_CONSERVATIVE_RASTER_POST_DEPTH_COVERAGE:
   case PIPE_CAP_CONSERVATIVE_RASTER_INNER_COVERAGE:
   case PIPE_CAP_PROGRAMMABLE_SAMPLE_LOCATIONS:
   case PIPE_CAP_MAX_COMBINED_SHADER_BUFFERS:
   case PIPE_CAP_MAX_COMBINED_HW_ATOMIC_COUNTERS:
   case PIPE_CAP_MAX_COMBINED_HW_ATOMIC_COUNTER_BUFFERS:
   case PIPE_CAP_TGSI_ATOMFADD:
   case PIPE_CAP_TGSI_SKIP_SHRINK_IO_ARRAYS:
   case PIPE_CAP_IMAGE_LOAD_FORMATTED:
   case PIPE_CAP_PREFER_COMPUTE_FOR_MULTIMEDIA:
   case PIPE_CAP_FRAGMENT_SHADER_INTERLOCK:
   case PIPE_CAP_CS_DERIVED_SYSTEM_VALUES_SUPPORTED:
   case PIPE_CAP_ATOMIC_FLOAT_MINMAX:
   case PIPE_CAP_SHADER_SAMPLES_IDENTICAL:
   case PIPE_CAP_TGSI_ATOMINC_WRAP:
   case PIPE_CAP_TGSI_TG4_COMPONENT_IN_SWIZZLE:
   case PIPE_CAP_GLSL_ZERO_INIT:
      return 0;

   case PIPE_CAP_MAX_GS_INVOCATIONS:
      return 32;

   case PIPE_CAP_MAX_SHADER_BUFFER_SIZE:
      return 1 << 27;

   case PIPE_CAP_TEXTURE_MIRROR_CLAMP_TO_EDGE:
   case PIPE_CAP_MAX_TEXTURE_UPLOAD_MEMORY_BUDGET:
      return 0;

   case PIPE_CAP_MAX_VERTEX_ELEMENT_SRC_OFFSET:
      return 2047;

   case PIPE_CAP_SURFACE_SAMPLE_COUNT:
      return 0;
   case PIPE_CAP_DEST_SURFACE_SRGB_CONTROL:
      return 1;

   case PIPE_CAP_MAX_VARYINGS:
      return 8;

   case PIPE_CAP_COMPUTE_GRID_INFO_LAST_BLOCK:
      return 0;

   case PIPE_CAP_COMPUTE_SHADER_DERIVATIVES:
      return 0;

   case PIPE_CAP_THROTTLE:
      return 1;

   case PIPE_CAP_TEXTURE_SHADOW_LOD:
      return 0;

   case PIPE_CAP_GL_SPIRV:
   case PIPE_CAP_GL_SPIRV_VARIABLE_POINTERS:
      return 0;

   case PIPE_CAP_DEMOTE_TO_HELPER_INVOCATION:
      return 0;

   case PIPE_CAP_DMABUF:
#if defined(PIPE_OS_LINUX) || defined(PIPE_OS_BSD)
      return 1;
#else
      return 0;
#endif

   case PIPE_CAP_TEXTURE_SHADOW_MAP: /* Enables ARB_shadow */
      return 1;

   case PIPE_CAP_FLATSHADE:
   case PIPE_CAP_ALPHA_TEST:
   case PIPE_CAP_POINT_SIZE_FIXED:
   case PIPE_CAP_TWO_SIDED_COLOR:
   case PIPE_CAP_CLIP_PLANES:
      return 1;

   case PIPE_CAP_MAX_VERTEX_BUFFERS:
      return 16;

   case PIPE_CAP_OPENCL_INTEGER_FUNCTIONS:
   case PIPE_CAP_INTEGER_MULTIPLY_32X16:
   case PIPE_CAP_DRAW_INFO_START_WITH_USER_INDICES:
      return 0;
   case PIPE_CAP_NIR_IMAGES_AS_DEREF:
      return 1;

   case PIPE_CAP_FRONTEND_NOOP:
      /* Enables INTEL_blackhole_render */
      return 0;

   case PIPE_CAP_PACKED_STREAM_OUTPUT:
      return 1;

   case PIPE_CAP_VIEWPORT_TRANSFORM_LOWERED:
   case PIPE_CAP_PSIZ_CLAMPED:
   case PIPE_CAP_MAP_UNSYNCHRONIZED_THREAD_SAFE:
      return 0;

   case PIPE_CAP_GL_BEGIN_END_BUFFER_SIZE:
      return 512 * 1024;

   case PIPE_CAP_SYSTEM_SVM:
   case PIPE_CAP_ALPHA_TO_COVERAGE_DITHER_CONTROL:
   case PIPE_CAP_NO_CLIP_ON_COPY_TEX:
   case PIPE_CAP_MAX_TEXTURE_MB:
      return 0;

   case PIPE_CAP_SHADER_ATOMIC_INT64:
      return 0;

   default:
      unreachable("bad PIPE_CAP_*");
   }
}
