/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2019 Collabora, Ltd.
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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
 *
 */

#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/format/u_format.h"
#include "util/format/u_format_s3tc.h"
#include "util/u_video.h"
#include "util/u_screen.h"
#include "util/os_time.h"
#include "util/u_process.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "draw/draw_context.h"

#include <fcntl.h>

#include "drm-uapi/drm_fourcc.h"
#include "drm-uapi/panfrost_drm.h"

#include "pan_bo.h"
#include "pan_screen.h"
#include "pan_resource.h"
#include "pan_public.h"
#include "pan_util.h"
#include "decode.h"

#include "pan_context.h"
#include "midgard/midgard_compile.h"
#include "bifrost/bifrost_compile.h"
#include "panfrost-quirks.h"

static const struct debug_named_value debug_options[] = {
        {"msgs",      PAN_DBG_MSGS,	"Print debug messages"},
        {"trace",     PAN_DBG_TRACE,    "Trace the command stream"},
        {"deqp",      PAN_DBG_DEQP,     "Hacks for dEQP"},
        {"afbc",      PAN_DBG_AFBC,     "Enable AFBC buffer sharing"},
        {"sync",      PAN_DBG_SYNC,     "Wait for each job's completion and check for any GPU fault"},
        {"precompile", PAN_DBG_PRECOMPILE, "Precompile shaders for shader-db"},
        {"fp16",     PAN_DBG_FP16,     "Enable 16-bit support"},
        {"nofp16",     PAN_DBG_NOFP16,     "Disable 16-bit support"},
        {"gl3",       PAN_DBG_GL3,      "Enable experimental GL 3.x implementation, up to 3.3"},
        {"noafbc",    PAN_DBG_NO_AFBC,  "Disable AFBC support"},
        DEBUG_NAMED_VALUE_END
};

static const char *
panfrost_get_name(struct pipe_screen *screen)
{
        return panfrost_model_name(pan_device(screen)->gpu_id);
}

static const char *
panfrost_get_vendor(struct pipe_screen *screen)
{
        return "Panfrost";
}

static const char *
panfrost_get_device_vendor(struct pipe_screen *screen)
{
        return "Arm";
}

static int
panfrost_get_param(struct pipe_screen *screen, enum pipe_cap param)
{
        /* We expose in-dev stuff for dEQP that we don't want apps to use yet */
        struct panfrost_device *dev = pan_device(screen);
        bool is_deqp = dev->debug & PAN_DBG_DEQP;

        /* Our GL 3.x implementation is WIP */
        bool is_gl3 = dev->debug & PAN_DBG_GL3;
        is_gl3 |= is_deqp;

        /* Don't expose MRT related CAPs on GPUs that don't implement them */
        bool has_mrt = !(dev->quirks & MIDGARD_SFBD);

        /* Bifrost is WIP. No MRT support yet. */
        bool is_bifrost = (dev->quirks & IS_BIFROST);
        has_mrt &= !is_bifrost || is_deqp;

        switch (param) {
        case PIPE_CAP_NPOT_TEXTURES:
        case PIPE_CAP_MIXED_COLOR_DEPTH_BITS:
        case PIPE_CAP_FRAGMENT_SHADER_TEXTURE_LOD:
        case PIPE_CAP_VERTEX_SHADER_SATURATE:
        case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
        case PIPE_CAP_POINT_SPRITE:
        case PIPE_CAP_DEPTH_CLIP_DISABLE:
        case PIPE_CAP_DEPTH_CLIP_DISABLE_SEPARATE:
        case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
        case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
                return 1;

        case PIPE_CAP_MAX_RENDER_TARGETS:
        case PIPE_CAP_FBFETCH:
        case PIPE_CAP_FBFETCH_COHERENT:
                return has_mrt ? 8 : 1;

        case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
                return 1;

        case PIPE_CAP_SAMPLE_SHADING:
                /* WIP */
                return is_gl3 ? 1 : 0;

        case PIPE_CAP_OCCLUSION_QUERY:
        case PIPE_CAP_PRIMITIVE_RESTART:
        case PIPE_CAP_PRIMITIVE_RESTART_FIXED_INDEX:
                return true;

        case PIPE_CAP_ANISOTROPIC_FILTER:
                return !!(dev->quirks & HAS_ANISOTROPIC);

        /* ES3 features unsupported on Bifrost */
        case PIPE_CAP_TGSI_INSTANCEID:
        case PIPE_CAP_TEXTURE_MULTISAMPLE:
        case PIPE_CAP_SURFACE_SAMPLE_COUNT:
                return !is_bifrost || is_deqp;

        case PIPE_CAP_SAMPLER_VIEW_TARGET:
        case PIPE_CAP_TEXTURE_SWIZZLE:
        case PIPE_CAP_TEXTURE_MIRROR_CLAMP:
        case PIPE_CAP_TEXTURE_MIRROR_CLAMP_TO_EDGE:
        case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
        case PIPE_CAP_BLEND_EQUATION_SEPARATE:
        case PIPE_CAP_INDEP_BLEND_ENABLE:
        case PIPE_CAP_INDEP_BLEND_FUNC:
        case PIPE_CAP_GENERATE_MIPMAP:
        case PIPE_CAP_ACCELERATED:
        case PIPE_CAP_UMA:
        case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
        case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
        case PIPE_CAP_COPY_BETWEEN_COMPRESSED_AND_PLAIN_FORMATS:
        case PIPE_CAP_TGSI_ARRAY_COMPONENTS:
        case PIPE_CAP_CS_DERIVED_SYSTEM_VALUES_SUPPORTED:
                return 1;

        case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
                return (is_bifrost && !is_deqp) ? 0 : 4;
        case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
        case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
                return (is_bifrost && !is_deqp) ? 0 : 64;
        case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
        case PIPE_CAP_STREAM_OUTPUT_INTERLEAVE_BUFFERS:
                return (is_bifrost && !is_deqp) ? 0 : 1;

        case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
                return (is_bifrost && !is_deqp) ? 0 : 256;

        case PIPE_CAP_GLSL_FEATURE_LEVEL:
        case PIPE_CAP_GLSL_FEATURE_LEVEL_COMPATIBILITY:
                return is_gl3 ? 330 : (is_bifrost && !is_deqp) ? 120 : 140;
        case PIPE_CAP_ESSL_FEATURE_LEVEL:
                return (is_bifrost && !is_deqp) ? 120 : 300;

        case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
                return 16;

        /* For faking GLES 3.1 for dEQP-GLES31 */
        case PIPE_CAP_IMAGE_LOAD_FORMATTED:
        case PIPE_CAP_CUBE_MAP_ARRAY:
        case PIPE_CAP_COMPUTE:
                return is_deqp;
        case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
                return is_deqp ? 65536 : 0;

        case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
        case PIPE_CAP_QUERY_TIMESTAMP:
                return is_gl3;

        /* TODO: Where does this req come from in practice? */
        case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
                return 1;

        case PIPE_CAP_MAX_TEXTURE_2D_SIZE:
                return 4096;
        case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
                return (is_bifrost && !is_deqp) ? 0 : 13;
        case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
                return 13;

        case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
                /* Hardware is natively upper left */
                return 0;

        case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
        case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
        case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
        case PIPE_CAP_TGSI_TEXCOORD:
                return 1;

        /* We would prefer varyings on Midgard, but proper sysvals on Bifrost */
        case PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL:
        case PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL:
        case PIPE_CAP_TGSI_FS_POINT_IS_SYSVAL:
                return is_bifrost;

        case PIPE_CAP_SEAMLESS_CUBE_MAP:
        case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
                return !is_bifrost || is_deqp;

        case PIPE_CAP_MAX_VERTEX_ELEMENT_SRC_OFFSET:
                return 0xffff;

        case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
                return 0;

        case PIPE_CAP_ENDIANNESS:
                return PIPE_ENDIAN_NATIVE;

        case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS:
                return is_deqp ? 4 : 0;

        case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
                return -8;

        case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
                return 7;

        case PIPE_CAP_VIDEO_MEMORY: {
                uint64_t system_memory;

                if (!os_get_total_physical_memory(&system_memory))
                        return 0;

                return (int)(system_memory >> 20);
        }

        case PIPE_CAP_SHADER_STENCIL_EXPORT:
                return !is_bifrost || is_deqp;

        case PIPE_CAP_CONDITIONAL_RENDER:
        case PIPE_CAP_CONDITIONAL_RENDER_INVERTED:
                return true;

        case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
                return 4;

        case PIPE_CAP_MAX_VARYINGS:
                return 16;

        case PIPE_CAP_ALPHA_TEST:
        case PIPE_CAP_FLATSHADE:
        case PIPE_CAP_TWO_SIDED_COLOR:
        case PIPE_CAP_CLIP_PLANES:
                return 0;

        case PIPE_CAP_PACKED_STREAM_OUTPUT:
                return 0;

        case PIPE_CAP_VIEWPORT_TRANSFORM_LOWERED:
        case PIPE_CAP_PSIZ_CLAMPED:
                return 1;

        default:
                return u_pipe_screen_get_param_defaults(screen, param);
        }
}

static int
panfrost_get_shader_param(struct pipe_screen *screen,
                          enum pipe_shader_type shader,
                          enum pipe_shader_cap param)
{
        struct panfrost_device *dev = pan_device(screen);
        bool is_deqp = dev->debug & PAN_DBG_DEQP;
        bool is_fp16 = dev->debug & PAN_DBG_FP16;
        bool is_nofp16 = dev->debug & PAN_DBG_NOFP16;
        bool is_bifrost = dev->quirks & IS_BIFROST;

        if (shader != PIPE_SHADER_VERTEX &&
            shader != PIPE_SHADER_FRAGMENT &&
            !(shader == PIPE_SHADER_COMPUTE && is_deqp))
                return 0;

        /* this is probably not totally correct.. but it's a start: */
        switch (param) {
        case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
        case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
        case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
        case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
                return 16384;

        case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
                return 1024;

        case PIPE_SHADER_CAP_MAX_INPUTS:
                return 16;

        case PIPE_SHADER_CAP_MAX_OUTPUTS:
                return shader == PIPE_SHADER_FRAGMENT ? 8 : 16;

        case PIPE_SHADER_CAP_MAX_TEMPS:
                return 256; /* GL_MAX_PROGRAM_TEMPORARIES_ARB */

        case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
                return 16 * 1024 * sizeof(float);

        case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
                return PAN_MAX_CONST_BUFFERS;

        case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
                return 0;

        case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
                return (is_bifrost && !is_deqp) ? 0 : 1;
        case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
                return 0;

        case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
                return 0;

        case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
                return 1;

        case PIPE_SHADER_CAP_SUBROUTINES:
                return 0;

        case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
                return 0;

        case PIPE_SHADER_CAP_INTEGERS:
                return 1;

        case PIPE_SHADER_CAP_FP16:
        case PIPE_SHADER_CAP_GLSL_16BIT_CONSTS:
                return (!is_nofp16 && !is_bifrost) || is_fp16;

        case PIPE_SHADER_CAP_FP16_DERIVATIVES:
        case PIPE_SHADER_CAP_INT16:
        case PIPE_SHADER_CAP_INT64_ATOMICS:
        case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
        case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
        case PIPE_SHADER_CAP_TGSI_LDEXP_SUPPORTED:
        case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
        case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
                return 0;

        case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
        case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
                return 16; /* XXX: How many? */

        case PIPE_SHADER_CAP_PREFERRED_IR:
                return PIPE_SHADER_IR_NIR;

        case PIPE_SHADER_CAP_SUPPORTED_IRS:
                return (1 << PIPE_SHADER_IR_NIR) | (1 << PIPE_SHADER_IR_NIR_SERIALIZED);

        case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
                return 32;

        case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
        case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
                return is_deqp ? 8 : 0;
        case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTERS:
        case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTER_BUFFERS:
                return 0;

        case PIPE_SHADER_CAP_TGSI_SKIP_MERGE_REGISTERS:
        case PIPE_SHADER_CAP_LOWER_IF_THRESHOLD:
                return 0;

        default:
                /* Other params are unknown */
                return 0;
        }

        return 0;
}

static float
panfrost_get_paramf(struct pipe_screen *screen, enum pipe_capf param)
{
        switch (param) {
        case PIPE_CAPF_MAX_LINE_WIDTH:

        /* fall-through */
        case PIPE_CAPF_MAX_LINE_WIDTH_AA:
                return 255.0; /* arbitrary */

        case PIPE_CAPF_MAX_POINT_WIDTH:

        /* fall-through */
        case PIPE_CAPF_MAX_POINT_WIDTH_AA:
                return 1024.0;

        case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
                return 16.0;

        case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
                return 16.0; /* arbitrary */

        case PIPE_CAPF_MIN_CONSERVATIVE_RASTER_DILATE:
        case PIPE_CAPF_MAX_CONSERVATIVE_RASTER_DILATE:
        case PIPE_CAPF_CONSERVATIVE_RASTER_DILATE_GRANULARITY:
                return 0.0f;

        default:
                debug_printf("Unexpected PIPE_CAPF %d query\n", param);
                return 0.0;
        }
}

/**
 * Query format support for creating a texture, drawing surface, etc.
 * \param format  the format to test
 * \param type  one of PIPE_TEXTURE, PIPE_SURFACE
 */
static bool
panfrost_is_format_supported( struct pipe_screen *screen,
                              enum pipe_format format,
                              enum pipe_texture_target target,
                              unsigned sample_count,
                              unsigned storage_sample_count,
                              unsigned bind)
{
        struct panfrost_device *dev = pan_device(screen);
        bool is_bifrost = (dev->quirks & IS_BIFROST);
        bool is_deqp = dev->debug & PAN_DBG_DEQP;
        const struct util_format_description *format_desc;

        assert(target == PIPE_BUFFER ||
               target == PIPE_TEXTURE_1D ||
               target == PIPE_TEXTURE_1D_ARRAY ||
               target == PIPE_TEXTURE_2D ||
               target == PIPE_TEXTURE_2D_ARRAY ||
               target == PIPE_TEXTURE_RECT ||
               target == PIPE_TEXTURE_3D ||
               target == PIPE_TEXTURE_CUBE ||
               target == PIPE_TEXTURE_CUBE_ARRAY);

        format_desc = util_format_description(format);

        if (!format_desc)
                return false;

        /* MSAA 4x supported, but no more. Technically some revisions of the
         * hardware can go up to 16x but we don't support higher modes yet.
         * MSAA 2x is notably not supported and gets rounded up to MSAA 4x. */

        if (!(sample_count == 0 || sample_count == 1 || sample_count == 4))
                return false;

        if (MAX2(sample_count, 1) != MAX2(storage_sample_count, 1))
                return false;

        /* Don't advertise multisampling on Bifrost yet */
        if ((is_bifrost && !is_deqp) && sample_count > 1)
                return false;

        /* Z16 causes dEQP failures on t720 */
        if (format == PIPE_FORMAT_Z16_UNORM && dev->quirks & MIDGARD_SFBD)
                return false;

        /* Don't confuse poorly written apps (workaround dEQP bug) that expect
         * more alpha than they ask for */

        bool scanout = bind & (PIPE_BIND_SCANOUT | PIPE_BIND_SHARED | PIPE_BIND_DISPLAY_TARGET);
        bool renderable = bind & PIPE_BIND_RENDER_TARGET;

        if (scanout && renderable && !util_format_is_rgba8_variant(format_desc))
                return false;

        /* Check we support the format with the given bind */

        unsigned relevant_bind = bind &
                ( PIPE_BIND_DEPTH_STENCIL | PIPE_BIND_RENDER_TARGET
                | PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_SAMPLER_VIEW);

        struct panfrost_format fmt = dev->formats[format];
        enum mali_format indexed = MALI_EXTRACT_INDEX(fmt.hw);

        /* Also check that compressed texture formats are supported on this
         * particular chip. They may not be depending on system integration
         * differences. RGTC can be emulated so is always supported. */

        bool is_rgtc = format_desc->layout == UTIL_FORMAT_LAYOUT_RGTC;
        bool supported = panfrost_supports_compressed_format(dev, indexed);

        if (!is_rgtc && !supported)
                return false;

        return indexed && ((relevant_bind & ~fmt.bind) == 0);
}

/* We always support linear and tiled operations, both external and internal.
 * We support AFBC for a subset of formats, and colourspace transform for a
 * subset of those. */

static void
panfrost_walk_dmabuf_modifiers(struct pipe_screen *screen,
                enum pipe_format format, int max, uint64_t *modifiers, unsigned
                int *external_only, int *out_count, uint64_t test_modifier)
{
        /* Query AFBC status */
        bool afbc = panfrost_format_supports_afbc(format);
        bool ytr = panfrost_afbc_can_ytr(format);

        /* Don't advertise AFBC before T760 */
        struct panfrost_device *dev = pan_device(screen);
        afbc &= !(dev->quirks & MIDGARD_NO_AFBC);

        /* XXX: AFBC scanout is broken on mainline RK3399 with older kernels */
        afbc &= (dev->debug & PAN_DBG_AFBC);

        unsigned count = 0;

        for (unsigned i = 0; i < PAN_MODIFIER_COUNT; ++i) {
                if (drm_is_afbc(pan_best_modifiers[i]) && !afbc)
                        continue;

                if ((pan_best_modifiers[i] & AFBC_FORMAT_MOD_YTR) && !ytr)
                        continue;

                if (test_modifier != DRM_FORMAT_MOD_INVALID &&
                    test_modifier != pan_best_modifiers[i])
                        continue;

                count++;

                if (max > (int) count) {
                        modifiers[count] = pan_best_modifiers[i];

                        if (external_only)
                                external_only[count] = false;
                }
        }

        *out_count = count;
}

static void
panfrost_query_dmabuf_modifiers(struct pipe_screen *screen,
                enum pipe_format format, int max, uint64_t *modifiers, unsigned
                int *external_only, int *out_count)
{
        panfrost_walk_dmabuf_modifiers(screen, format, max, modifiers,
                external_only, out_count, DRM_FORMAT_MOD_INVALID);
}

static bool
panfrost_is_dmabuf_modifier_supported(struct pipe_screen *screen,
                uint64_t modifier, enum pipe_format format,
                bool *external_only)
{
        uint64_t unused;
        unsigned int uint_extern_only = 0;
        int count;

        panfrost_walk_dmabuf_modifiers(screen, format, 1, &unused,
                &uint_extern_only, &count, modifier);

        if (external_only)
           *external_only = uint_extern_only ? true : false;

        return count > 0;
}

static int
panfrost_get_compute_param(struct pipe_screen *pscreen, enum pipe_shader_ir ir_type,
                enum pipe_compute_cap param, void *ret)
{
        struct panfrost_device *dev = pan_device(pscreen);
	const char * const ir = "panfrost";

	if (!(dev->debug & PAN_DBG_DEQP))
		return 0;

#define RET(x) do {                  \
   if (ret)                          \
      memcpy(ret, x, sizeof(x));     \
   return sizeof(x);                 \
} while (0)

	switch (param) {
	case PIPE_COMPUTE_CAP_ADDRESS_BITS:
		RET((uint32_t []){ 64 });

	case PIPE_COMPUTE_CAP_IR_TARGET:
		if (ret)
			sprintf(ret, "%s", ir);
		return strlen(ir) * sizeof(char);

	case PIPE_COMPUTE_CAP_GRID_DIMENSION:
		RET((uint64_t []) { 3 });

	case PIPE_COMPUTE_CAP_MAX_GRID_SIZE:
		RET(((uint64_t []) { 65535, 65535, 65535 }));

	case PIPE_COMPUTE_CAP_MAX_BLOCK_SIZE:
		RET(((uint64_t []) { 1024, 1024, 64 }));

	case PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK:
		RET((uint64_t []) { 1024 });

	case PIPE_COMPUTE_CAP_MAX_GLOBAL_SIZE:
		RET((uint64_t []) { 1024*1024*512 /* Maybe get memory */ });

	case PIPE_COMPUTE_CAP_MAX_LOCAL_SIZE:
		RET((uint64_t []) { 32768 });

	case PIPE_COMPUTE_CAP_MAX_PRIVATE_SIZE:
	case PIPE_COMPUTE_CAP_MAX_INPUT_SIZE:
		RET((uint64_t []) { 4096 });

	case PIPE_COMPUTE_CAP_MAX_MEM_ALLOC_SIZE:
		RET((uint64_t []) { 1024*1024*512 /* Maybe get memory */ });

	case PIPE_COMPUTE_CAP_MAX_CLOCK_FREQUENCY:
		RET((uint32_t []) { 800 /* MHz -- TODO */ });

	case PIPE_COMPUTE_CAP_MAX_COMPUTE_UNITS:
		RET((uint32_t []) { 9999 });  // TODO

	case PIPE_COMPUTE_CAP_IMAGES_SUPPORTED:
		RET((uint32_t []) { 1 }); // TODO

	case PIPE_COMPUTE_CAP_SUBGROUP_SIZE:
		RET((uint32_t []) { 32 });  // TODO

	case PIPE_COMPUTE_CAP_MAX_VARIABLE_THREADS_PER_BLOCK:
		RET((uint64_t []) { 1024 }); // TODO
	}

	return 0;
}

static void
panfrost_destroy_screen(struct pipe_screen *pscreen)
{
        panfrost_close_device(pan_device(pscreen));
        ralloc_free(pscreen);
}

static uint64_t
panfrost_get_timestamp(struct pipe_screen *_screen)
{
        return os_time_get_nano();
}

static void
panfrost_fence_reference(struct pipe_screen *pscreen,
                         struct pipe_fence_handle **ptr,
                         struct pipe_fence_handle *fence)
{
        struct panfrost_device *dev = pan_device(pscreen);
        struct panfrost_fence **p = (struct panfrost_fence **)ptr;
        struct panfrost_fence *f = (struct panfrost_fence *)fence;
        struct panfrost_fence *old = *p;

        if (pipe_reference(&(*p)->reference, &f->reference)) {
                drmSyncobjDestroy(dev->fd, old->syncobj);
                free(old);
        }
        *p = f;
}

static bool
panfrost_fence_finish(struct pipe_screen *pscreen,
                      struct pipe_context *ctx,
                      struct pipe_fence_handle *fence,
                      uint64_t timeout)
{
        struct panfrost_device *dev = pan_device(pscreen);
        struct panfrost_fence *f = (struct panfrost_fence *)fence;
        int ret;

        if (f->signaled)
                return true;

        uint64_t abs_timeout = os_time_get_absolute_timeout(timeout);
        if (abs_timeout == OS_TIMEOUT_INFINITE)
                abs_timeout = INT64_MAX;

        ret = drmSyncobjWait(dev->fd, &f->syncobj,
                             1,
                             abs_timeout, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL,
                             NULL);

        f->signaled = (ret >= 0);
        return f->signaled;
}

struct panfrost_fence *
panfrost_fence_create(struct panfrost_context *ctx)
{
        struct panfrost_fence *f = calloc(1, sizeof(*f));
        if (!f)
                return NULL;

        struct panfrost_device *dev = pan_device(ctx->base.screen);
        int fd = -1, ret;

        /* Snapshot the last rendering out fence. We'd rather have another
         * syncobj instead of a sync file, but this is all we get.
         * (HandleToFD/FDToHandle just gives you another syncobj ID for the
         * same syncobj).
         */
        ret = drmSyncobjExportSyncFile(dev->fd, ctx->syncobj, &fd);
        if (ret || fd == -1) {
                fprintf(stderr, "export failed\n");
                goto err_free_fence;
        }

        ret = drmSyncobjCreate(dev->fd, 0, &f->syncobj);
        if (ret) {
                fprintf(stderr, "create syncobj failed\n");
                goto err_close_fd;
        }

        ret = drmSyncobjImportSyncFile(dev->fd, f->syncobj, fd);
        if (ret) {
                fprintf(stderr, "create syncobj failed\n");
                goto err_destroy_syncobj;
        }

        assert(f->syncobj != ctx->syncobj);
        close(fd);
        pipe_reference_init(&f->reference, 1);

        return f;

err_destroy_syncobj:
        drmSyncobjDestroy(dev->fd, f->syncobj);
err_close_fd:
        close(fd);
err_free_fence:
        free(f);
        return NULL;
}

static const void *
panfrost_screen_get_compiler_options(struct pipe_screen *pscreen,
                                     enum pipe_shader_ir ir,
                                     enum pipe_shader_type shader)
{
        if (pan_device(pscreen)->quirks & IS_BIFROST)
                return &bifrost_nir_options;
        else
                return &midgard_nir_options;
}

struct pipe_screen *
panfrost_create_screen(int fd, struct renderonly *ro)
{
        /* Create the screen */
        struct panfrost_screen *screen = rzalloc(NULL, struct panfrost_screen);

        if (!screen)
                return NULL;

        struct panfrost_device *dev = pan_device(&screen->base);
        panfrost_open_device(screen, fd, dev);

        dev->debug = debug_get_flags_option("PAN_MESA_DEBUG", debug_options, 0);

        if (dev->debug & PAN_DBG_NO_AFBC)
                dev->quirks |= MIDGARD_NO_AFBC;

        if (ro) {
                dev->ro = renderonly_dup(ro);
                if (!dev->ro) {
                        if (dev->debug & PAN_DBG_MSGS)
                                fprintf(stderr, "Failed to dup renderonly object\n");

                        free(screen);
                        return NULL;
                }
        }

        /* Check if we're loading against a supported GPU model. */

        switch (dev->gpu_id) {
        case 0x720: /* T720 */
        case 0x750: /* T760 */
        case 0x820: /* T820 */
        case 0x860: /* T860 */
        case 0x6221: /* G72 */
        case 0x7093: /* G31 */
        case 0x7212: /* G52 */
                break;
        default:
                /* Fail to load against untested models */
                debug_printf("panfrost: Unsupported model %X", dev->gpu_id);
                panfrost_destroy_screen(&(screen->base));
                return NULL;
        }

        if (dev->debug & (PAN_DBG_TRACE | PAN_DBG_SYNC))
                pandecode_initialize(!(dev->debug & PAN_DBG_TRACE));

        screen->base.destroy = panfrost_destroy_screen;

        screen->base.get_name = panfrost_get_name;
        screen->base.get_vendor = panfrost_get_vendor;
        screen->base.get_device_vendor = panfrost_get_device_vendor;
        screen->base.get_param = panfrost_get_param;
        screen->base.get_shader_param = panfrost_get_shader_param;
        screen->base.get_compute_param = panfrost_get_compute_param;
        screen->base.get_paramf = panfrost_get_paramf;
        screen->base.get_timestamp = panfrost_get_timestamp;
        screen->base.is_format_supported = panfrost_is_format_supported;
        screen->base.query_dmabuf_modifiers = panfrost_query_dmabuf_modifiers;
        screen->base.is_dmabuf_modifier_supported =
               panfrost_is_dmabuf_modifier_supported;
        screen->base.context_create = panfrost_create_context;
        screen->base.get_compiler_options = panfrost_screen_get_compiler_options;
        screen->base.fence_reference = panfrost_fence_reference;
        screen->base.fence_finish = panfrost_fence_finish;
        screen->base.set_damage_region = panfrost_resource_set_damage_region;

        panfrost_resource_screen_init(&screen->base);
        panfrost_init_blit_shaders(dev);

        return &screen->base;
}
