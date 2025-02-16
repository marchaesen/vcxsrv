/**************************************************************************
 *
 * Copyright 2007 VMware, Inc.
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

#ifndef PIPE_DEFINES_H
#define PIPE_DEFINES_H

/* For pipe_blend* and pipe_logicop enums */
#include "util/blend.h"

#include "util/compiler.h"

#include "compiler/shader_enums.h"
#include "util/os_time.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Gallium error codes.
 *
 * - A zero value always means success.
 * - A negative value always means failure.
 * - The meaning of a positive value is function dependent.
 */
enum pipe_error
{
   PIPE_OK = 0,
   PIPE_ERROR = -1,    /**< Generic error */
   PIPE_ERROR_BAD_INPUT = -2,
   PIPE_ERROR_OUT_OF_MEMORY = -3,
   PIPE_ERROR_RETRY = -4
   /* TODO */
};

/**
 * Inequality functions.  Used for depth test, stencil compare, alpha
 * test, shadow compare, etc.
 */
enum pipe_compare_func {
   PIPE_FUNC_NEVER,
   PIPE_FUNC_LESS,
   PIPE_FUNC_EQUAL,
   PIPE_FUNC_LEQUAL,
   PIPE_FUNC_GREATER,
   PIPE_FUNC_NOTEQUAL,
   PIPE_FUNC_GEQUAL,
   PIPE_FUNC_ALWAYS,
};

/** Polygon fill mode */
enum {
   PIPE_POLYGON_MODE_FILL,
   PIPE_POLYGON_MODE_LINE,
   PIPE_POLYGON_MODE_POINT,
   PIPE_POLYGON_MODE_FILL_RECTANGLE,
};

/** Polygon face specification, eg for culling */
#define PIPE_FACE_NONE           0
#define PIPE_FACE_FRONT          1
#define PIPE_FACE_BACK           2
#define PIPE_FACE_FRONT_AND_BACK (PIPE_FACE_FRONT | PIPE_FACE_BACK)

/** Stencil ops */
enum pipe_stencil_op {
   PIPE_STENCIL_OP_KEEP,
   PIPE_STENCIL_OP_ZERO,
   PIPE_STENCIL_OP_REPLACE,
   PIPE_STENCIL_OP_INCR,
   PIPE_STENCIL_OP_DECR,
   PIPE_STENCIL_OP_INCR_WRAP,
   PIPE_STENCIL_OP_DECR_WRAP,
   PIPE_STENCIL_OP_INVERT,
};

/** Texture types.
 * See the documentation for info on PIPE_TEXTURE_RECT vs PIPE_TEXTURE_2D
 */
enum pipe_texture_target
{
   PIPE_BUFFER,
   PIPE_TEXTURE_1D,
   PIPE_TEXTURE_2D,
   PIPE_TEXTURE_3D,
   PIPE_TEXTURE_CUBE,
   PIPE_TEXTURE_RECT,
   PIPE_TEXTURE_1D_ARRAY,
   PIPE_TEXTURE_2D_ARRAY,
   PIPE_TEXTURE_CUBE_ARRAY,
   PIPE_MAX_TEXTURE_TYPES,
};

enum pipe_tex_face {
   PIPE_TEX_FACE_POS_X,
   PIPE_TEX_FACE_NEG_X,
   PIPE_TEX_FACE_POS_Y,
   PIPE_TEX_FACE_NEG_Y,
   PIPE_TEX_FACE_POS_Z,
   PIPE_TEX_FACE_NEG_Z,
   PIPE_TEX_FACE_MAX,
};

enum pipe_tex_wrap {
   PIPE_TEX_WRAP_REPEAT,
   PIPE_TEX_WRAP_CLAMP,
   PIPE_TEX_WRAP_CLAMP_TO_EDGE,
   PIPE_TEX_WRAP_CLAMP_TO_BORDER,
   PIPE_TEX_WRAP_MIRROR_REPEAT,
   PIPE_TEX_WRAP_MIRROR_CLAMP,
   PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE,
   PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER,
};

/** Between mipmaps, ie mipfilter */
enum pipe_tex_mipfilter {
   PIPE_TEX_MIPFILTER_NEAREST,
   PIPE_TEX_MIPFILTER_LINEAR,
   PIPE_TEX_MIPFILTER_NONE,
};

/** Within a mipmap, ie min/mag filter */
enum pipe_tex_filter {
   PIPE_TEX_FILTER_NEAREST,
   PIPE_TEX_FILTER_LINEAR,
};

enum pipe_tex_compare {
   PIPE_TEX_COMPARE_NONE,
   PIPE_TEX_COMPARE_R_TO_TEXTURE,
};

enum pipe_tex_reduction_mode {
   PIPE_TEX_REDUCTION_WEIGHTED_AVERAGE,
   PIPE_TEX_REDUCTION_MIN,
   PIPE_TEX_REDUCTION_MAX,
};

/**
 * Clear buffer bits
 */
#define PIPE_CLEAR_DEPTH        (1 << 0)
#define PIPE_CLEAR_STENCIL      (1 << 1)
#define PIPE_CLEAR_COLOR0       (1 << 2)
#define PIPE_CLEAR_COLOR1       (1 << 3)
#define PIPE_CLEAR_COLOR2       (1 << 4)
#define PIPE_CLEAR_COLOR3       (1 << 5)
#define PIPE_CLEAR_COLOR4       (1 << 6)
#define PIPE_CLEAR_COLOR5       (1 << 7)
#define PIPE_CLEAR_COLOR6       (1 << 8)
#define PIPE_CLEAR_COLOR7       (1 << 9)
/** Combined flags */
/** All color buffers currently bound */
#define PIPE_CLEAR_COLOR        (PIPE_CLEAR_COLOR0 | PIPE_CLEAR_COLOR1 | \
                                 PIPE_CLEAR_COLOR2 | PIPE_CLEAR_COLOR3 | \
                                 PIPE_CLEAR_COLOR4 | PIPE_CLEAR_COLOR5 | \
                                 PIPE_CLEAR_COLOR6 | PIPE_CLEAR_COLOR7)
#define PIPE_CLEAR_DEPTHSTENCIL (PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL)

/**
 * CPU access map flags
 */
enum pipe_map_flags
{
   PIPE_MAP_NONE = 0,
   /**
    * Resource contents read back (or accessed directly) at transfer
    * create time.
    */
   PIPE_MAP_READ = 1 << 0,

   /**
    * Resource contents will be written back at buffer/texture_unmap
    * time (or modified as a result of being accessed directly).
    */
   PIPE_MAP_WRITE = 1 << 1,

   /**
    * Read/modify/write
    */
   PIPE_MAP_READ_WRITE = PIPE_MAP_READ | PIPE_MAP_WRITE,

   /**
    * The transfer should map the texture storage directly. The driver may
    * return NULL if that isn't possible, and the gallium frontend needs to cope
    * with that and use an alternative path without this flag.
    *
    * E.g. the gallium frontend could have a simpler path which maps textures and
    * does read/modify/write cycles on them directly, and a more complicated
    * path which uses minimal read and write transfers.
    *
    * This flag supresses implicit "DISCARD" for buffer_subdata.
    */
   PIPE_MAP_DIRECTLY = 1 << 2,

   /**
    * Discards the memory within the mapped region.
    *
    * It should not be used with PIPE_MAP_READ.
    *
    * See also:
    * - OpenGL's ARB_map_buffer_range extension, MAP_INVALIDATE_RANGE_BIT flag.
    */
   PIPE_MAP_DISCARD_RANGE = 1 << 3,

   /**
    * Fail if the resource cannot be mapped immediately.
    *
    * See also:
    * - Direct3D's D3DLOCK_DONOTWAIT flag.
    * - Mesa's MESA_MAP_NOWAIT_BIT flag.
    * - WDDM's D3DDDICB_LOCKFLAGS.DonotWait flag.
    */
   PIPE_MAP_DONTBLOCK = 1 << 4,

   /**
    * Do not attempt to synchronize pending operations on the resource when mapping.
    *
    * It should not be used with PIPE_MAP_READ.
    *
    * See also:
    * - OpenGL's ARB_map_buffer_range extension, MAP_UNSYNCHRONIZED_BIT flag.
    * - Direct3D's D3DLOCK_NOOVERWRITE flag.
    * - WDDM's D3DDDICB_LOCKFLAGS.IgnoreSync flag.
    */
   PIPE_MAP_UNSYNCHRONIZED = 1 << 5,

   /**
    * Written ranges will be notified later with
    * pipe_context::transfer_flush_region.
    *
    * It should not be used with PIPE_MAP_READ.
    *
    * See also:
    * - pipe_context::transfer_flush_region
    * - OpenGL's ARB_map_buffer_range extension, MAP_FLUSH_EXPLICIT_BIT flag.
    */
   PIPE_MAP_FLUSH_EXPLICIT = 1 << 6,

   /**
    * Discards all memory backing the resource.
    *
    * It should not be used with PIPE_MAP_READ.
    *
    * This is equivalent to:
    * - OpenGL's ARB_map_buffer_range extension, MAP_INVALIDATE_BUFFER_BIT
    * - BufferData(NULL) on a GL buffer
    * - Direct3D's D3DLOCK_DISCARD flag.
    * - WDDM's D3DDDICB_LOCKFLAGS.Discard flag.
    * - D3D10 DDI's D3D10_DDI_MAP_WRITE_DISCARD flag
    * - D3D10's D3D10_MAP_WRITE_DISCARD flag.
    */
   PIPE_MAP_DISCARD_WHOLE_RESOURCE = 1 << 7,

   /**
    * Allows the resource to be used for rendering while mapped.
    *
    * PIPE_RESOURCE_FLAG_MAP_PERSISTENT must be set when creating
    * the resource.
    *
    * If COHERENT is not set, memory_barrier(PIPE_BARRIER_MAPPED_BUFFER)
    * must be called to ensure the device can see what the CPU has written.
    */
   PIPE_MAP_PERSISTENT = 1 << 8,

   /**
    * If PERSISTENT is set, this ensures any writes done by the device are
    * immediately visible to the CPU and vice versa.
    *
    * PIPE_RESOURCE_FLAG_MAP_COHERENT must be set when creating
    * the resource.
    */
   PIPE_MAP_COHERENT = 1 << 9,

   /**
    * Map a resource in a thread-safe manner, because the calling thread can
    * be any thread. It can only be used if both WRITE and UNSYNCHRONIZED are
    * set.
    */
   PIPE_MAP_THREAD_SAFE = 1 << 10,

   /**
    * Map only the depth aspect of a resource
    */
   PIPE_MAP_DEPTH_ONLY = 1 << 11,

   /**
    * Map only the stencil aspect of a resource
    */
   PIPE_MAP_STENCIL_ONLY = 1 << 12,

   /**
    * Mapping will be used only once (never remapped).
    */
   PIPE_MAP_ONCE = 1 << 13,

   /**
    * This and higher bits are reserved for private use by drivers. Drivers
    * should use this as (PIPE_MAP_DRV_PRV << i).
    */
   PIPE_MAP_DRV_PRV = 1 << 14,
};

/**
 * Flags for the flush function.
 */
enum pipe_flush_flags
{
   PIPE_FLUSH_END_OF_FRAME = (1 << 0),
   PIPE_FLUSH_DEFERRED = (1 << 1),
   PIPE_FLUSH_FENCE_FD = (1 << 2),
   PIPE_FLUSH_ASYNC = (1 << 3),
   PIPE_FLUSH_HINT_FINISH = (1 << 4),
   PIPE_FLUSH_TOP_OF_PIPE = (1 << 5),
   PIPE_FLUSH_BOTTOM_OF_PIPE = (1 << 6),
};

/**
 * Flags for pipe_context::dump_debug_state.
 */
#define PIPE_DUMP_DEVICE_STATUS_REGISTERS    (1 << 0)

/**
 * Create a compute-only context. Use in pipe_screen::context_create.
 * This disables draw, blit, and clear*, render_condition, and other graphics
 * functions. Interop with other graphics contexts is still allowed.
 * This allows scheduling jobs on a compute-only hardware command queue that
 * can run in parallel with graphics without stalling it.
 */
#define PIPE_CONTEXT_COMPUTE_ONLY      (1 << 0)

/**
 * Gather debug information and expect that pipe_context::dump_debug_state
 * will be called. Use in pipe_screen::context_create.
 */
#define PIPE_CONTEXT_DEBUG             (1 << 1)

/**
 * Whether out-of-bounds shader loads must return zero and out-of-bounds
 * shader stores must be dropped.
 */
#define PIPE_CONTEXT_ROBUST_BUFFER_ACCESS (1 << 2)

/**
 * Prefer threaded pipe_context. It also implies that video codec functions
 * will not be used. (they will be either no-ops or NULL when threading is
 * enabled)
 */
#define PIPE_CONTEXT_PREFER_THREADED   (1 << 3)

/**
 * Create a high priority context.
 */
#define PIPE_CONTEXT_HIGH_PRIORITY     (1 << 4)

/**
 * Create a low priority context.
 */
#define PIPE_CONTEXT_LOW_PRIORITY      (1 << 5)

/** Stop execution if the device is reset. */
#define PIPE_CONTEXT_LOSE_CONTEXT_ON_RESET (1 << 6)

/**
 * Create a protected context to access protected content (surfaces,
 * textures, ...)
 *
 * This is required to access protected images and surfaces if
 * EGL_EXT_protected_surface is not supported.
 */
#define PIPE_CONTEXT_PROTECTED         (1 << 7)

/**
 * Create a context that does not use sampler LOD bias. If this is set, the
 * frontend MUST set pipe_sampler_state::lod_bias to 0.0f for all samplers used
 * with the context. Drivers MAY ignore lod_bias for such contexts.
 *
 * This may allow driver fast paths for GLES, which lacks sampler LOD bias.
 */
#define PIPE_CONTEXT_NO_LOD_BIAS (1 << 8)

/**
 * Create a media-only context. Use in pipe_screen::context_create.
 * This disables draw, blit, and clear*, render_condition, and other graphics.
 * This also disabled all compute related functions
 * functions. Interop with other media contexts is still allowed.
 * This allows scheduling jobs on a media-only hardware command queue that
 * can run in parallel with media without stalling it.
 */
#define PIPE_CONTEXT_MEDIA_ONLY      (1 << 9)

/**
 * Create a realtime priority context.
 *
 * The context must run at the highest possible priority and be capable of
 * preempting the current executing context when commands are flushed
 * by such a realtime context.
 */
#define PIPE_CONTEXT_REALTIME_PRIORITY (1 << 10)

/**
 * Flags for pipe_context::memory_barrier.
 */
#define PIPE_BARRIER_MAPPED_BUFFER     (1 << 0)
#define PIPE_BARRIER_SHADER_BUFFER     (1 << 1)
#define PIPE_BARRIER_QUERY_BUFFER      (1 << 2)
#define PIPE_BARRIER_VERTEX_BUFFER     (1 << 3)
#define PIPE_BARRIER_INDEX_BUFFER      (1 << 4)
#define PIPE_BARRIER_CONSTANT_BUFFER   (1 << 5)
#define PIPE_BARRIER_INDIRECT_BUFFER   (1 << 6)
#define PIPE_BARRIER_TEXTURE           (1 << 7)
#define PIPE_BARRIER_IMAGE             (1 << 8)
#define PIPE_BARRIER_FRAMEBUFFER       (1 << 9)
#define PIPE_BARRIER_STREAMOUT_BUFFER  (1 << 10)
#define PIPE_BARRIER_GLOBAL_BUFFER     (1 << 11)
#define PIPE_BARRIER_UPDATE_BUFFER     (1 << 12)
#define PIPE_BARRIER_UPDATE_TEXTURE    (1 << 13)
#define PIPE_BARRIER_ALL               ((1 << 14) - 1)

#define PIPE_BARRIER_UPDATE \
   (PIPE_BARRIER_UPDATE_BUFFER | PIPE_BARRIER_UPDATE_TEXTURE)

/**
 * Flags for pipe_context::texture_barrier.
 */
#define PIPE_TEXTURE_BARRIER_SAMPLER      (1 << 0)
#define PIPE_TEXTURE_BARRIER_FRAMEBUFFER  (1 << 1)

/**
 * Resource binding flags -- gallium frontends must specify in advance all
 * the ways a resource might be used.
 */
#define PIPE_BIND_DEPTH_STENCIL        (1 << 0) /* create_surface */
#define PIPE_BIND_RENDER_TARGET        (1 << 1) /* create_surface */
#define PIPE_BIND_BLENDABLE            (1 << 2) /* create_surface */
#define PIPE_BIND_SAMPLER_VIEW         (1 << 3) /* create_sampler_view */
#define PIPE_BIND_VERTEX_BUFFER        (1 << 4) /* set_vertex_buffers */
#define PIPE_BIND_INDEX_BUFFER         (1 << 5) /* draw_elements */
#define PIPE_BIND_CONSTANT_BUFFER      (1 << 6) /* set_constant_buffer */
#define PIPE_BIND_DISPLAY_TARGET       (1 << 7) /* flush_front_buffer */
#define PIPE_BIND_VERTEX_STATE         (1 << 8) /* create_vertex_state */
/* gap */
#define PIPE_BIND_STREAM_OUTPUT        (1 << 10) /* set_stream_output_buffers */
#define PIPE_BIND_CURSOR               (1 << 11) /* mouse cursor */
#define PIPE_BIND_CUSTOM               (1 << 12) /* gallium frontend/winsys usages */
#define PIPE_BIND_GLOBAL               (1 << 13) /* set_global_binding */
#define PIPE_BIND_SHADER_BUFFER        (1 << 14) /* set_shader_buffers */
#define PIPE_BIND_SHADER_IMAGE         (1 << 15) /* set_shader_images */
#define PIPE_BIND_COMPUTE_RESOURCE     (1 << 16) /* set_compute_resources */
#define PIPE_BIND_COMMAND_ARGS_BUFFER  (1 << 17) /* pipe_draw_info.indirect */
#define PIPE_BIND_QUERY_BUFFER         (1 << 18) /* get_query_result_resource */

/**
 * The first two flags above were previously part of the amorphous
 * TEXTURE_USAGE, most of which are now descriptions of the ways a
 * particular texture can be bound to the gallium pipeline.  The two flags
 * below do not fit within that and probably need to be migrated to some
 * other place.
 *
 * Scanout is used to ask for a texture suitable for actual scanout (hence
 * the name), which implies extra layout constraints on some hardware.
 * It may also have some special meaning regarding mouse cursor images.
 *
 * The shared flag is quite underspecified, but certainly isn't a
 * binding flag - it seems more like a message to the winsys to create
 * a shareable allocation.
 *
 * The third flag has been added to be able to force textures to be created
 * in linear mode (no tiling).
 */
#define PIPE_BIND_SCANOUT     (1 << 19) /*  */
#define PIPE_BIND_SHARED      (1 << 20) /* get_texture_handle ??? */
#define PIPE_BIND_LINEAR      (1 << 21)
#define PIPE_BIND_PROTECTED   (1 << 22) /* Resource will be protected/encrypted */
#define PIPE_BIND_SAMPLER_REDUCTION_MINMAX (1 << 23) /* pipe_caps.sampler_reduction_minmax */
/* Resource is the DRI_PRIME blit destination. Only set on on the render GPU. */
#define PIPE_BIND_PRIME_BLIT_DST (1 << 24)
#define PIPE_BIND_USE_FRONT_RENDERING (1 << 25) /* Resource may be used for frontbuffer rendering */
#define PIPE_BIND_CONST_BW    (1 << 26) /* Avoid using a data dependent layout (AFBC, UBWC, etc) */
#define PIPE_BIND_VIDEO_DECODE_DPB     (1 << 27) /* video engine DPB decode reconstructed picture */
#define PIPE_BIND_VIDEO_ENCODE_DPB     (1 << 28) /* video engine DPB encode reconstructed picture */

/**
 * Flags for the driver about resource behaviour:
 */
#define PIPE_RESOURCE_FLAG_MAP_PERSISTENT (1 << 0)
#define PIPE_RESOURCE_FLAG_MAP_COHERENT   (1 << 1)
#define PIPE_RESOURCE_FLAG_TEXTURING_MORE_LIKELY (1 << 2)
#define PIPE_RESOURCE_FLAG_SPARSE                (1 << 3)
#define PIPE_RESOURCE_FLAG_SINGLE_THREAD_USE     (1 << 4)
#define PIPE_RESOURCE_FLAG_ENCRYPTED             (1 << 5)
#define PIPE_RESOURCE_FLAG_DONT_OVER_ALLOCATE    (1 << 6)
#define PIPE_RESOURCE_FLAG_DONT_MAP_DIRECTLY     (1 << 7) /* for small visible VRAM */
#define PIPE_RESOURCE_FLAG_UNMAPPABLE            (1 << 8) /* implies staging transfers due to VK interop */
#define PIPE_RESOURCE_FLAG_DRV_PRIV              (1 << 9) /* driver/winsys private */
#define PIPE_RESOURCE_FLAG_FRONTEND_PRIV         (1 << 24) /* gallium frontend private */

/**
 * Fixed-rate compression
 */
#define PIPE_COMPRESSION_FIXED_RATE_NONE    0x0
#define PIPE_COMPRESSION_FIXED_RATE_DEFAULT 0xF

/**
 * Hint about the expected lifecycle of a resource.
 * Sorted according to GPU vs CPU access.
 */
enum pipe_resource_usage {
   PIPE_USAGE_DEFAULT,        /* fast GPU access */
   PIPE_USAGE_IMMUTABLE,      /* fast GPU access, immutable */
   PIPE_USAGE_DYNAMIC,        /* uploaded data is used multiple times */
   PIPE_USAGE_STREAM,         /* uploaded data is used once */
   PIPE_USAGE_STAGING,        /* fast CPU access */
};

/**
 * Tessellator spacing types
 */
enum pipe_tess_spacing {
   PIPE_TESS_SPACING_FRACTIONAL_ODD,
   PIPE_TESS_SPACING_FRACTIONAL_EVEN,
   PIPE_TESS_SPACING_EQUAL,
};

/**
 * Query object types
 */
enum pipe_query_type {
   PIPE_QUERY_OCCLUSION_COUNTER,
   PIPE_QUERY_OCCLUSION_PREDICATE,
   PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE,
   PIPE_QUERY_TIMESTAMP,
   PIPE_QUERY_TIMESTAMP_DISJOINT,
   PIPE_QUERY_TIME_ELAPSED,
   PIPE_QUERY_PRIMITIVES_GENERATED,
   PIPE_QUERY_PRIMITIVES_EMITTED,
   PIPE_QUERY_SO_STATISTICS,
   PIPE_QUERY_SO_OVERFLOW_PREDICATE,
   PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE,
   PIPE_QUERY_GPU_FINISHED,
   PIPE_QUERY_PIPELINE_STATISTICS,
   PIPE_QUERY_PIPELINE_STATISTICS_SINGLE,
   PIPE_QUERY_TYPES,
   /* start of driver queries, see pipe_screen::get_driver_query_info */
   PIPE_QUERY_DRIVER_SPECIFIC = 256,
};

/**
 * Index for PIPE_QUERY_PIPELINE_STATISTICS subqueries.
 */
enum pipe_statistics_query_index {
   PIPE_STAT_QUERY_IA_VERTICES,
   PIPE_STAT_QUERY_IA_PRIMITIVES,
   PIPE_STAT_QUERY_VS_INVOCATIONS,
   PIPE_STAT_QUERY_GS_INVOCATIONS,
   PIPE_STAT_QUERY_GS_PRIMITIVES,
   PIPE_STAT_QUERY_C_INVOCATIONS,
   PIPE_STAT_QUERY_C_PRIMITIVES,
   PIPE_STAT_QUERY_PS_INVOCATIONS,
   PIPE_STAT_QUERY_HS_INVOCATIONS,
   PIPE_STAT_QUERY_DS_INVOCATIONS,
   PIPE_STAT_QUERY_CS_INVOCATIONS,
   PIPE_STAT_QUERY_TS_INVOCATIONS,
   PIPE_STAT_QUERY_MS_INVOCATIONS,
};

/**
 * Conditional rendering modes
 */
enum pipe_render_cond_flag {
   PIPE_RENDER_COND_WAIT,
   PIPE_RENDER_COND_NO_WAIT,
   PIPE_RENDER_COND_BY_REGION_WAIT,
   PIPE_RENDER_COND_BY_REGION_NO_WAIT,
};

/**
 * Point sprite coord modes
 */
enum pipe_sprite_coord_mode {
   PIPE_SPRITE_COORD_UPPER_LEFT,
   PIPE_SPRITE_COORD_LOWER_LEFT,
};

/**
 * Viewport swizzles
 */
enum pipe_viewport_swizzle {
   PIPE_VIEWPORT_SWIZZLE_POSITIVE_X,
   PIPE_VIEWPORT_SWIZZLE_NEGATIVE_X,
   PIPE_VIEWPORT_SWIZZLE_POSITIVE_Y,
   PIPE_VIEWPORT_SWIZZLE_NEGATIVE_Y,
   PIPE_VIEWPORT_SWIZZLE_POSITIVE_Z,
   PIPE_VIEWPORT_SWIZZLE_NEGATIVE_Z,
   PIPE_VIEWPORT_SWIZZLE_POSITIVE_W,
   PIPE_VIEWPORT_SWIZZLE_NEGATIVE_W,
};

/**
 * Device reset status.
 */
enum pipe_reset_status
{
   PIPE_NO_RESET,
   PIPE_GUILTY_CONTEXT_RESET,
   PIPE_INNOCENT_CONTEXT_RESET,
   PIPE_UNKNOWN_CONTEXT_RESET,
};

enum pipe_vertex_input_alignment {
   PIPE_VERTEX_INPUT_ALIGNMENT_NONE,
   PIPE_VERTEX_INPUT_ALIGNMENT_4BYTE,
   PIPE_VERTEX_INPUT_ALIGNMENT_ELEMENT,
};


/**
 * Conservative rasterization modes.
 */
enum pipe_conservative_raster_mode
{
   PIPE_CONSERVATIVE_RASTER_OFF,

   /**
    * The post-snap mode means the conservative rasterization occurs after
    * the conversion from floating-point to fixed-point coordinates
    * on the subpixel grid.
    */
   PIPE_CONSERVATIVE_RASTER_POST_SNAP,

   /**
    * The pre-snap mode means the conservative rasterization occurs before
    * the conversion from floating-point to fixed-point coordinates.
    */
   PIPE_CONSERVATIVE_RASTER_PRE_SNAP,
};


/**
 * resource_get_handle flags.
 */
/* Requires pipe_context::flush_resource before external use. */
#define PIPE_HANDLE_USAGE_EXPLICIT_FLUSH     (1 << 0)
/* Expected external use of the resource: */
#define PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE  (1 << 1)
#define PIPE_HANDLE_USAGE_SHADER_WRITE       (1 << 2)

/**
 * pipe_image_view access flags.
 */
#define PIPE_IMAGE_ACCESS_READ               (1 << 0)
#define PIPE_IMAGE_ACCESS_WRITE              (1 << 1)
#define PIPE_IMAGE_ACCESS_READ_WRITE         (PIPE_IMAGE_ACCESS_READ | \
                                              PIPE_IMAGE_ACCESS_WRITE)
#define PIPE_IMAGE_ACCESS_COHERENT           (1 << 2)
#define PIPE_IMAGE_ACCESS_VOLATILE           (1 << 3)
#define PIPE_IMAGE_ACCESS_TEX2D_FROM_BUFFER  (1 << 4)
#define PIPE_IMAGE_ACCESS_DRIVER_INTERNAL    (1 << 5)

/**
 * Shader subgroup feature flags aligned with GL_KHR_shader_subgroup.
 */
#define PIPE_SHADER_SUBGROUP_FEATURE_BASIC            (1 << 0)
#define PIPE_SHADER_SUBGROUP_FEATURE_VOTE             (1 << 1)
#define PIPE_SHADER_SUBGROUP_FEATURE_ARITHMETIC       (1 << 2)
#define PIPE_SHADER_SUBGROUP_FEATURE_BALLOT           (1 << 3)
#define PIPE_SHADER_SUBGROUP_FEATURE_SHUFFLE          (1 << 4)
#define PIPE_SHADER_SUBGROUP_FEATURE_SHUFFLE_RELATIVE (1 << 5)
#define PIPE_SHADER_SUBGROUP_FEATURE_CLUSTERED        (1 << 6)
#define PIPE_SHADER_SUBGROUP_FEATURE_QUAD             (1 << 7)
#define PIPE_SHADER_SUBGROUP_NUM_FEATURES             8

enum pipe_point_size_lower_mode {
   PIPE_POINT_SIZE_LOWER_ALWAYS,
   PIPE_POINT_SIZE_LOWER_NEVER,
   PIPE_POINT_SIZE_LOWER_USER_ONLY,
};

enum pipe_texture_transfer_mode {
   PIPE_TEXTURE_TRANSFER_DEFAULT = 0,
   PIPE_TEXTURE_TRANSFER_BLIT = (1 << 0),
   PIPE_TEXTURE_TRANSFER_COMPUTE = (1 << 1),
};

/**
 * Possible bits for pipe_caps.context_priority_mask param, which should
 * return a bitmask of the supported priorities.  If the driver does not
 * support prioritized contexts, it can return 0.
 *
 * Note that these match __EGL_CONTEXT_PRIORITY_*_BIT.
 */
#define PIPE_CONTEXT_PRIORITY_LOW      (1 << 0)
#define PIPE_CONTEXT_PRIORITY_MEDIUM   (1 << 1)
#define PIPE_CONTEXT_PRIORITY_HIGH     (1 << 2)
#define PIPE_CONTEXT_PRIORITY_REALTIME (1 << 3)

enum pipe_quirk_texture_border_color_swizzle {
   PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_NV50 = (1 << 0),
   PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_R600 = (1 << 1),
   PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_FREEDRENO = (1 << 2),
   PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_ALPHA_NOT_W = (1 << 3),
};

enum pipe_endian
{
   PIPE_ENDIAN_LITTLE = 0,
   PIPE_ENDIAN_BIG = 1,
#if UTIL_ARCH_LITTLE_ENDIAN
   PIPE_ENDIAN_NATIVE = PIPE_ENDIAN_LITTLE
#elif UTIL_ARCH_BIG_ENDIAN
   PIPE_ENDIAN_NATIVE = PIPE_ENDIAN_BIG
#endif
};

/**
 * Shader intermediate representation.
 *
 * Note that if the driver requests something other than TGSI, it must
 * always be prepared to receive TGSI in addition to its preferred IR.
 * If the driver requests TGSI as its preferred IR, it will *always*
 * get TGSI.
 *
 * Note that PIPE_SHADER_IR_TGSI should be zero for backwards compat with
 * gallium frontends that only understand TGSI.
 */
enum pipe_shader_ir
{
   PIPE_SHADER_IR_TGSI = 0,
   PIPE_SHADER_IR_NATIVE,
   PIPE_SHADER_IR_NIR,
};

/** Shader caps not specific to any single stage */
struct pipe_shader_caps {
   unsigned max_instructions; /* if 0, it means the stage is unsupported */
   unsigned max_alu_instructions;
   unsigned max_tex_instructions;
   unsigned max_tex_indirections;
   unsigned max_control_flow_depth;
   unsigned max_inputs;
   unsigned max_outputs;
   unsigned max_const_buffer0_size;
   unsigned max_const_buffers;
   unsigned max_temps;
   unsigned max_texture_samplers;
   unsigned max_sampler_views;
   unsigned max_shader_buffers;
   unsigned max_shader_images;
   unsigned max_hw_atomic_counters;
   unsigned max_hw_atomic_counter_buffers;
   unsigned supported_irs;

   bool cont_supported;
   bool indirect_temp_addr;
   bool indirect_const_addr;
   bool subroutines; /* BGNSUB, ENDSUB, CAL, RET */
   bool integers;
   bool int64_atomics;
   bool fp16;
   bool fp16_derivatives;
   bool fp16_const_buffers;
   bool int16;
   bool glsl_16bit_consts;
   bool tgsi_sqrt_supported;
   bool tgsi_any_inout_decl_range;
};

/** Compute-specific implementation capability. */
struct pipe_compute_caps {
   unsigned address_bits;
   unsigned grid_dimension;
   unsigned max_grid_size[3];
   unsigned max_block_size[3];
   unsigned max_block_size_clover[3];
   unsigned max_threads_per_block;
   unsigned max_threads_per_block_clover;
   unsigned max_local_size;
   unsigned max_private_size;
   unsigned max_input_size;
   unsigned max_clock_frequency;
   unsigned max_compute_units;
   unsigned max_subgroups;
   unsigned subgroup_sizes;
   unsigned max_variable_threads_per_block;
   uint64_t max_mem_alloc_size;
   uint64_t max_global_size;
   char ir_target[32];
   bool images_supported;
};

struct pipe_caps {
   bool graphics;
   bool npot_textures;
   bool anisotropic_filter;
   bool occlusion_query;
   bool query_time_elapsed;
   bool texture_shadow_map;
   bool texture_swizzle;
   bool texture_mirror_clamp;
   bool blend_equation_separate;
   bool primitive_restart;
   bool primitive_restart_fixed_index;
   bool indep_blend_enable;
   bool indep_blend_func;
   bool fs_coord_origin_upper_left;
   bool fs_coord_origin_lower_left;
   bool fs_coord_pixel_center_half_integer;
   bool fs_coord_pixel_center_integer;
   bool depth_clip_disable;
   bool depth_clip_disable_separate;
   bool depth_clamp_enable;
   bool shader_stencil_export;
   bool vs_instanceid;
   bool vertex_element_instance_divisor;
   bool fragment_color_clamped;
   bool mixed_colorbuffer_formats;
   bool seamless_cube_map;
   bool seamless_cube_map_per_texture;
   bool conditional_render;
   bool texture_barrier;
   bool stream_output_pause_resume;
   bool tgsi_can_compact_constants;
   bool vertex_color_unclamped;
   bool vertex_color_clamped;
   bool quads_follow_provoking_vertex_convention;
   bool user_vertex_buffers;
   bool compute;
   bool start_instance;
   bool query_timestamp;
   bool texture_multisample;
   bool cube_map_array;
   bool texture_buffer_objects;
   bool buffer_sampler_view_rgba_only;
   bool tgsi_texcoord;
   bool query_pipeline_statistics;
   bool mixed_framebuffer_sizes;
   bool vs_layer_viewport;
   bool texture_gather_sm5;
   bool buffer_map_persistent_coherent;
   bool fake_sw_msaa;
   bool texture_query_lod;
   bool sample_shading;
   bool texture_gather_offsets;
   bool vs_window_space_position;
   bool draw_indirect;
   bool fs_fine_derivative;
   bool uma;
   bool conditional_render_inverted;
   bool sampler_view_target;
   bool clip_halfz;
   bool polygon_offset_clamp;
   bool multisample_z_resolve;
   bool resource_from_user_memory;
   bool resource_from_user_memory_compute_only;
   bool device_reset_status_query;
   bool texture_float_linear;
   bool texture_half_float_linear;
   bool depth_bounds_test;
   bool texture_query_samples;
   bool force_persample_interp;
   bool shareable_shaders;
   bool copy_between_compressed_and_plain_formats;
   bool clear_scissored;
   bool draw_parameters;
   bool shader_pack_half_float;
   bool multi_draw_indirect;
   bool multi_draw_indirect_params;
   bool multi_draw_indirect_partial_stride;
   bool fs_position_is_sysval;
   bool fs_point_is_sysval;
   bool fs_face_is_integer_sysval;
   bool invalidate_buffer;
   bool generate_mipmap;
   bool string_marker;
   bool surface_reinterpret_blocks;
   bool query_buffer_object;
   bool query_memory_info;
   bool framebuffer_no_attachment;
   bool robust_buffer_access_behavior;
   bool cull_distance;
   bool shader_group_vote;
   bool polygon_offset_units_unscaled;
   bool shader_array_components;
   bool stream_output_interleave_buffers;
   bool native_fence_fd;
   bool glsl_tess_levels_as_inputs;
   bool legacy_math_rules;
   bool fp16;
   bool doubles;
   bool int64;
   bool tgsi_tex_txf_lz;
   bool shader_clock;
   bool polygon_mode_fill_rectangle;
   bool shader_ballot;
   bool tes_layer_viewport;
   bool can_bind_const_buffer_as_vertex;
   bool allow_mapped_buffers_during_execution;
   bool post_depth_coverage;
   bool bindless_texture;
   bool nir_samplers_as_deref;
   bool query_so_overflow;
   bool memobj;
   bool load_constbuf;
   bool tile_raster_order;
   bool signed_vertex_buffer_offset;
   bool fence_signal;
   bool packed_uniforms;
   bool conservative_raster_post_snap_triangles;
   bool conservative_raster_post_snap_points_lines;
   bool conservative_raster_pre_snap_triangles;
   bool conservative_raster_pre_snap_points_lines;
   bool conservative_raster_post_depth_coverage;
   bool conservative_raster_inner_coverage;
   bool programmable_sample_locations;
   bool texture_mirror_clamp_to_edge;
   bool surface_sample_count;
   bool image_atomic_float_add;
   bool query_pipeline_statistics_single;
   bool dest_surface_srgb_control;
   bool compute_grid_info_last_block;
   bool compute_shader_derivatives;
   bool image_load_formatted;
   bool image_store_formatted;
   bool throttle;
   bool cl_gl_sharing;
   bool prefer_compute_for_multimedia;
   bool fragment_shader_interlock;
   bool fbfetch_coherent;
   bool atomic_float_minmax;
   bool tgsi_div;
   bool fragment_shader_texture_lod;
   bool fragment_shader_derivatives;
   bool texture_shadow_lod;
   bool shader_samples_identical;
   bool image_atomic_inc_wrap;
   bool prefer_imm_arrays_as_constbuf;
   bool gl_spirv;
   bool gl_spirv_variable_pointers;
   bool demote_to_helper_invocation;
   bool tgsi_tg4_component_in_swizzle;
   bool flatshade;
   bool alpha_test;
   bool two_sided_color;
   bool opencl_integer_functions;
   bool integer_multiply_32x16;
   bool frontend_noop;
   bool nir_images_as_deref;
   bool packed_stream_output;
   bool viewport_transform_lowered;
   bool psiz_clamped;
   bool viewport_swizzle;
   bool system_svm;
   bool viewport_mask;
   bool alpha_to_coverage_dither_control;
   bool map_unsynchronized_thread_safe;
   bool blend_equation_advanced;
   bool nir_atomics_as_deref;
   bool no_clip_on_copy_tex;
   bool shader_atomic_int64;
   bool device_protected_surface;
   bool prefer_real_buffer_in_constbuf0;
   bool gl_clamp;
   bool texrect;
   bool sampler_reduction_minmax;
   bool sampler_reduction_minmax_arb;
   bool allow_dynamic_vao_fastpath;
   bool emulate_nonfixed_primitive_restart;
   bool prefer_back_buffer_reuse;
   bool draw_vertex_state;
   bool prefer_pot_aligned_varyings;
   bool sparse_texture_full_array_cube_mipmaps;
   bool query_sparse_texture_residency;
   bool clamp_sparse_texture_lod;
   bool allow_draw_out_of_order;
   bool hardware_gl_select;
   bool dithering;
   bool fbfetch_zs;
   bool timeline_semaphore_import;
   bool device_protected_context;
   bool allow_glthread_buffer_subdata_opt;
   bool null_textures;
   bool astc_void_extents_need_denorm_flush;
   bool validate_all_dirty_states;
   bool has_const_bw;
   bool performance_monitor;
   bool texture_sampler_independent;
   bool astc_decode_mode;
   bool shader_subgroup_quad_all_stages;
   bool call_finalize_nir_in_linker;

   int accelerated;
   int min_texel_offset;
   int max_texel_offset;
   int min_texture_gather_offset;
   int max_texture_gather_offset;

   unsigned max_dual_source_render_targets;
   unsigned max_render_targets;
   unsigned max_texture_2d_size;
   unsigned max_texture_3d_levels;
   unsigned max_texture_cube_levels;
   unsigned max_stream_output_buffers;
   unsigned max_texture_array_layers;
   unsigned max_stream_output_separate_components;
   unsigned max_stream_output_interleaved_components;
   unsigned glsl_feature_level;
   unsigned glsl_feature_level_compatibility;
   unsigned essl_feature_level;
   unsigned constant_buffer_offset_alignment;
   unsigned timer_resolution;
   unsigned min_map_buffer_alignment;
   unsigned texture_buffer_offset_alignment;
   unsigned linear_image_pitch_alignment;
   unsigned linear_image_base_address_alignment;
   /* pipe_texture_transfer_mode */
   unsigned texture_transfer_modes;
   /* pipe_quirk_texture_border_color_swizzle */
   unsigned texture_border_color_quirk;
   unsigned max_texel_buffer_elements;
   unsigned max_viewports;
   unsigned max_geometry_output_vertices;
   unsigned max_geometry_total_output_components;
   unsigned max_texture_gather_components;
   unsigned max_vertex_streams;
   unsigned vendor_id;
   unsigned device_id;
   unsigned video_memory;
   unsigned max_vertex_attrib_stride;
   unsigned max_shader_patch_varyings;
   unsigned shader_buffer_offset_alignment;
   unsigned pci_group;
   unsigned pci_bus;
   unsigned pci_device;
   unsigned pci_function;
   unsigned max_window_rectangles;
   unsigned viewport_subpixel_bits;
   unsigned rasterizer_subpixel_bits;
   unsigned mixed_color_depth_bits;
   unsigned fbfetch;
   unsigned sparse_buffer_page_size;
   unsigned max_combined_shader_output_resources;
   unsigned framebuffer_msaa_constraints;
   unsigned context_priority_mask;
   unsigned constbuf0_flags;
   unsigned max_conservative_raster_subpixel_precision_bias;
   unsigned max_gs_invocations;
   unsigned max_shader_buffer_size;
   unsigned max_combined_shader_buffers;
   unsigned max_combined_hw_atomic_counters;
   unsigned max_combined_hw_atomic_counter_buffers;
   unsigned max_texture_upload_memory_budget;
   unsigned max_vertex_element_src_offset;
   unsigned max_varyings;
   unsigned dmabuf;
   unsigned clip_planes;
   unsigned max_vertex_buffers;
   unsigned gl_begin_end_buffer_size;
   unsigned glsl_zero_init;
   unsigned max_texture_mb;
   unsigned supported_prim_modes;
   unsigned supported_prim_modes_with_restart;
   unsigned max_sparse_texture_size;
   unsigned max_sparse_3d_texture_size;
   unsigned max_sparse_array_texture_layers;
   unsigned max_constant_buffer_size;
   unsigned query_timestamp_bits;
   unsigned shader_subgroup_size;
   unsigned shader_subgroup_supported_stages;
   unsigned shader_subgroup_supported_features;
   unsigned multiview;

   enum pipe_vertex_input_alignment vertex_input_alignment;
   enum pipe_endian endianness;
   enum pipe_point_size_lower_mode point_size_fixed;

   float min_line_width;
   float min_line_width_aa;
   float max_line_width;
   float max_line_width_aa;
   float line_width_granularity;
   float min_point_size;
   float min_point_size_aa;
   float max_point_size;
   float max_point_size_aa;
   float point_size_granularity;
   float max_texture_anisotropy;
   float max_texture_lod_bias;
   float min_conservative_raster_dilate;
   float max_conservative_raster_dilate;
   float conservative_raster_dilate_granularity;
};

/**
 * Resource parameters. They can be queried using
 * pipe_screen::get_resource_param.
 */
enum pipe_resource_param
{
   PIPE_RESOURCE_PARAM_NPLANES,
   PIPE_RESOURCE_PARAM_STRIDE,
   PIPE_RESOURCE_PARAM_OFFSET,
   PIPE_RESOURCE_PARAM_MODIFIER,
   PIPE_RESOURCE_PARAM_HANDLE_TYPE_SHARED,
   PIPE_RESOURCE_PARAM_HANDLE_TYPE_KMS,
   PIPE_RESOURCE_PARAM_HANDLE_TYPE_FD,
   PIPE_RESOURCE_PARAM_LAYER_STRIDE,
};

/**
 * Types of parameters for pipe_context::set_context_param.
 */
enum pipe_context_param
{
   /* Call util_thread_sched_apply_policy() for each driver thread that
    * benefits from it.
    */
   PIPE_CONTEXT_PARAM_UPDATE_THREAD_SCHEDULING,
};

/**
 * Composite query types
 */

/**
 * Query result for PIPE_QUERY_SO_STATISTICS.
 */
struct pipe_query_data_so_statistics
{
   uint64_t num_primitives_written;
   uint64_t primitives_storage_needed;
};

/**
 * Query result for PIPE_QUERY_TIMESTAMP_DISJOINT.
 */
struct pipe_query_data_timestamp_disjoint
{
   uint64_t frequency;
   bool     disjoint;
};

/**
 * Query result for PIPE_QUERY_PIPELINE_STATISTICS.
 */
struct pipe_query_data_pipeline_statistics
{
   union {
      struct {
         uint64_t ia_vertices;    /**< Num vertices read by the vertex fetcher. */
         uint64_t ia_primitives;  /**< Num primitives read by the vertex fetcher. */
         uint64_t vs_invocations; /**< Num vertex shader invocations. */
         uint64_t gs_invocations; /**< Num geometry shader invocations. */
         uint64_t gs_primitives;  /**< Num primitives output by a geometry shader. */
         uint64_t c_invocations;  /**< Num primitives sent to the rasterizer. */
         uint64_t c_primitives;   /**< Num primitives that were rendered. */
         uint64_t ps_invocations; /**< Num pixel shader invocations. */
         uint64_t hs_invocations; /**< Num hull shader invocations. */
         uint64_t ds_invocations; /**< Num domain shader invocations. */
         uint64_t cs_invocations; /**< Num compute shader invocations. */
         uint64_t ts_invocations; /**< Num task shader invocations. */
         uint64_t ms_invocations; /**< Num mesh shader invocations. */
      };
      uint64_t counters[13];
   };
};

/**
 * For batch queries.
 */
union pipe_numeric_type_union
{
   uint64_t u64;
   uint32_t u32;
   float f;
};

/**
 * Query result (returned by pipe_context::get_query_result).
 */
union pipe_query_result
{
   /* PIPE_QUERY_OCCLUSION_PREDICATE */
   /* PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE */
   /* PIPE_QUERY_SO_OVERFLOW_PREDICATE */
   /* PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE */
   /* PIPE_QUERY_GPU_FINISHED */
   bool b;

   /* PIPE_QUERY_OCCLUSION_COUNTER */
   /* PIPE_QUERY_TIMESTAMP */
   /* PIPE_QUERY_TIME_ELAPSED */
   /* PIPE_QUERY_PRIMITIVES_GENERATED */
   /* PIPE_QUERY_PRIMITIVES_EMITTED */
   /* PIPE_DRIVER_QUERY_TYPE_UINT64 */
   /* PIPE_DRIVER_QUERY_TYPE_BYTES */
   /* PIPE_DRIVER_QUERY_TYPE_MICROSECONDS */
   /* PIPE_DRIVER_QUERY_TYPE_HZ */
   uint64_t u64;

   /* PIPE_DRIVER_QUERY_TYPE_UINT */
   uint32_t u32;

   /* PIPE_DRIVER_QUERY_TYPE_FLOAT */
   /* PIPE_DRIVER_QUERY_TYPE_PERCENTAGE */
   float f;

   /* PIPE_QUERY_SO_STATISTICS */
   struct pipe_query_data_so_statistics so_statistics;

   /* PIPE_QUERY_TIMESTAMP_DISJOINT */
   struct pipe_query_data_timestamp_disjoint timestamp_disjoint;

   /* PIPE_QUERY_PIPELINE_STATISTICS */
   struct pipe_query_data_pipeline_statistics pipeline_statistics;

   /* batch queries (variable length) */
   union pipe_numeric_type_union batch[1];
};

enum pipe_query_value_type
{
   PIPE_QUERY_TYPE_I32,
   PIPE_QUERY_TYPE_U32,
   PIPE_QUERY_TYPE_I64,
   PIPE_QUERY_TYPE_U64,
};

enum pipe_query_flags
{
   PIPE_QUERY_WAIT = (1 << 0),
   PIPE_QUERY_PARTIAL = (1 << 1),
};

enum pipe_driver_query_type
{
   PIPE_DRIVER_QUERY_TYPE_UINT64,
   PIPE_DRIVER_QUERY_TYPE_UINT,
   PIPE_DRIVER_QUERY_TYPE_FLOAT,
   PIPE_DRIVER_QUERY_TYPE_PERCENTAGE,
   PIPE_DRIVER_QUERY_TYPE_BYTES,
   PIPE_DRIVER_QUERY_TYPE_MICROSECONDS,
   PIPE_DRIVER_QUERY_TYPE_HZ,
   PIPE_DRIVER_QUERY_TYPE_DBM,
   PIPE_DRIVER_QUERY_TYPE_TEMPERATURE,
   PIPE_DRIVER_QUERY_TYPE_VOLTS,
   PIPE_DRIVER_QUERY_TYPE_AMPS,
   PIPE_DRIVER_QUERY_TYPE_WATTS,
};

/* Whether an average value per frame or a cumulative value should be
 * displayed.
 */
enum pipe_driver_query_result_type
{
   PIPE_DRIVER_QUERY_RESULT_TYPE_AVERAGE,
   PIPE_DRIVER_QUERY_RESULT_TYPE_CUMULATIVE,
};

/**
 * Some hardware requires some hardware-specific queries to be submitted
 * as batched queries. The corresponding query objects are created using
 * create_batch_query, and at most one such query may be active at
 * any time.
 */
#define PIPE_DRIVER_QUERY_FLAG_BATCH     (1 << 0)

/* Do not list this query in the HUD. */
#define PIPE_DRIVER_QUERY_FLAG_DONT_LIST (1 << 1)

struct pipe_driver_query_info
{
   const char *name;
   unsigned query_type; /* PIPE_QUERY_DRIVER_SPECIFIC + i */
   union pipe_numeric_type_union max_value; /* max value that can be returned */
   enum pipe_driver_query_type type;
   enum pipe_driver_query_result_type result_type;
   unsigned group_id;
   unsigned flags;
};

struct pipe_driver_query_group_info
{
   const char *name;
   unsigned max_active_queries;
   unsigned num_queries;
};

enum pipe_fd_type
{
   PIPE_FD_TYPE_NATIVE_SYNC,
   PIPE_FD_TYPE_SYNCOBJ,
   PIPE_FD_TYPE_TIMELINE_SEMAPHORE,
};

/**
 * counter type and counter data type enums used by INTEL_performance_query
 * APIs in gallium drivers.
 */
enum pipe_perf_counter_type
{
   PIPE_PERF_COUNTER_TYPE_EVENT,
   PIPE_PERF_COUNTER_TYPE_DURATION_NORM,
   PIPE_PERF_COUNTER_TYPE_DURATION_RAW,
   PIPE_PERF_COUNTER_TYPE_THROUGHPUT,
   PIPE_PERF_COUNTER_TYPE_RAW,
   PIPE_PERF_COUNTER_TYPE_TIMESTAMP,
};

enum pipe_perf_counter_data_type
{
   PIPE_PERF_COUNTER_DATA_TYPE_BOOL32,
   PIPE_PERF_COUNTER_DATA_TYPE_UINT32,
   PIPE_PERF_COUNTER_DATA_TYPE_UINT64,
   PIPE_PERF_COUNTER_DATA_TYPE_FLOAT,
   PIPE_PERF_COUNTER_DATA_TYPE_DOUBLE,
};

#define PIPE_ASTC_DECODE_FORMAT_FLOAT16 0
#define PIPE_ASTC_DECODE_FORMAT_UNORM8  1
#define PIPE_ASTC_DECODE_FORMAT_RGB9E5  2

#define PIPE_UUID_SIZE 16
#define PIPE_LUID_SIZE 8

#if DETECT_OS_POSIX
#define PIPE_MEMORY_FD
#endif

#ifdef __cplusplus
}
#endif

#endif
