/*
 * © Copyright 2017-2018 Alyssa Rosenzweig
 * © Copyright 2017-2018 Connor Abbott
 * © Copyright 2017-2018 Lyude Paul
 * © Copyright2019 Collabora, Ltd.
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

#ifndef __PANFROST_JOB_H__
#define __PANFROST_JOB_H__

#include <stdint.h>
#include <panfrost-misc.h>

enum mali_job_type {
        JOB_NOT_STARTED	= 0,
        JOB_TYPE_NULL = 1,
        JOB_TYPE_SET_VALUE = 2,
        JOB_TYPE_CACHE_FLUSH = 3,
        JOB_TYPE_COMPUTE = 4,
        JOB_TYPE_VERTEX = 5,
        JOB_TYPE_GEOMETRY = 6,
        JOB_TYPE_TILER = 7,
        JOB_TYPE_FUSED = 8,
        JOB_TYPE_FRAGMENT = 9,
};

enum mali_draw_mode {
        MALI_DRAW_NONE      = 0x0,
        MALI_POINTS         = 0x1,
        MALI_LINES          = 0x2,
        MALI_LINE_STRIP     = 0x4,
        MALI_LINE_LOOP      = 0x6,
        MALI_TRIANGLES      = 0x8,
        MALI_TRIANGLE_STRIP = 0xA,
        MALI_TRIANGLE_FAN   = 0xC,
        MALI_POLYGON        = 0xD,
        MALI_QUADS          = 0xE,
        MALI_QUAD_STRIP     = 0xF,

        /* All other modes invalid */
};

/* Applies to tiler_gl_enables */

#define MALI_OCCLUSION_QUERY    (1 << 3)
#define MALI_OCCLUSION_PRECISE  (1 << 4)

/* Set for a glFrontFace(GL_CCW) in a Y=0=TOP coordinate system (like Gallium).
 * In OpenGL, this would corresponds to glFrontFace(GL_CW). Mesa and the blob
 * disagree about how to do viewport flipping, so the blob actually sets this
 * for GL_CW but then has a negative viewport stride */

#define MALI_FRONT_CCW_TOP      (1 << 5)

#define MALI_CULL_FACE_FRONT    (1 << 6)
#define MALI_CULL_FACE_BACK     (1 << 7)

/* Used in stencil and depth tests */

enum mali_func {
        MALI_FUNC_NEVER    = 0,
        MALI_FUNC_LESS     = 1,
        MALI_FUNC_EQUAL    = 2,
        MALI_FUNC_LEQUAL   = 3,
        MALI_FUNC_GREATER  = 4,
        MALI_FUNC_NOTEQUAL = 5,
        MALI_FUNC_GEQUAL   = 6,
        MALI_FUNC_ALWAYS   = 7
};

/* Same OpenGL, but mixed up. Why? Because forget me, that's why! */

enum mali_alt_func {
        MALI_ALT_FUNC_NEVER    = 0,
        MALI_ALT_FUNC_GREATER  = 1,
        MALI_ALT_FUNC_EQUAL    = 2,
        MALI_ALT_FUNC_GEQUAL   = 3,
        MALI_ALT_FUNC_LESS     = 4,
        MALI_ALT_FUNC_NOTEQUAL = 5,
        MALI_ALT_FUNC_LEQUAL   = 6,
        MALI_ALT_FUNC_ALWAYS   = 7
};

/* Flags apply to unknown2_3? */

#define MALI_HAS_MSAA		(1 << 0)
#define MALI_CAN_DISCARD 	(1 << 5)

/* Applies on SFBD systems, specifying that programmable blending is in use */
#define MALI_HAS_BLEND_SHADER 	(1 << 6)

/* func is mali_func */
#define MALI_DEPTH_FUNC(func)	   (func << 8)
#define MALI_GET_DEPTH_FUNC(flags) ((flags >> 8) & 0x7)
#define MALI_DEPTH_FUNC_MASK	   MALI_DEPTH_FUNC(0x7)

#define MALI_DEPTH_WRITEMASK    (1 << 11)

/* Next flags to unknown2_4 */
#define MALI_STENCIL_TEST      	(1 << 0)

/* What?! */
#define MALI_SAMPLE_ALPHA_TO_COVERAGE_NO_BLEND_SHADER (1 << 1)

#define MALI_NO_DITHER		(1 << 9)
#define MALI_DEPTH_RANGE_A	(1 << 12)
#define MALI_DEPTH_RANGE_B	(1 << 13)
#define MALI_NO_MSAA		(1 << 14)

/* Stencil test state is all encoded in a single u32, just with a lot of
 * enums... */

enum mali_stencil_op {
        MALI_STENCIL_KEEP 	= 0,
        MALI_STENCIL_REPLACE 	= 1,
        MALI_STENCIL_ZERO 	= 2,
        MALI_STENCIL_INVERT 	= 3,
        MALI_STENCIL_INCR_WRAP 	= 4,
        MALI_STENCIL_DECR_WRAP 	= 5,
        MALI_STENCIL_INCR 	= 6,
        MALI_STENCIL_DECR 	= 7
};

struct mali_stencil_test {
        unsigned ref  			: 8;
        unsigned mask 			: 8;
        enum mali_func func 		: 3;
        enum mali_stencil_op sfail 	: 3;
        enum mali_stencil_op dpfail 	: 3;
        enum mali_stencil_op dppass 	: 3;
        unsigned zero			: 4;
} __attribute__((packed));

#define MALI_MASK_R (1 << 0)
#define MALI_MASK_G (1 << 1)
#define MALI_MASK_B (1 << 2)
#define MALI_MASK_A (1 << 3)

enum mali_nondominant_mode {
        MALI_BLEND_NON_MIRROR = 0,
        MALI_BLEND_NON_ZERO = 1
};

enum mali_dominant_blend {
        MALI_BLEND_DOM_SOURCE = 0,
        MALI_BLEND_DOM_DESTINATION  = 1
};

enum mali_dominant_factor {
        MALI_DOMINANT_UNK0 = 0,
        MALI_DOMINANT_ZERO = 1,
        MALI_DOMINANT_SRC_COLOR = 2,
        MALI_DOMINANT_DST_COLOR = 3,
        MALI_DOMINANT_UNK4 = 4,
        MALI_DOMINANT_SRC_ALPHA = 5,
        MALI_DOMINANT_DST_ALPHA = 6,
        MALI_DOMINANT_CONSTANT = 7,
};

enum mali_blend_modifier {
        MALI_BLEND_MOD_UNK0 = 0,
        MALI_BLEND_MOD_NORMAL = 1,
        MALI_BLEND_MOD_SOURCE_ONE = 2,
        MALI_BLEND_MOD_DEST_ONE = 3,
};

struct mali_blend_mode {
        enum mali_blend_modifier clip_modifier : 2;
        unsigned unused_0 : 1;
        unsigned negate_source : 1;

        enum mali_dominant_blend dominant : 1;

        enum mali_nondominant_mode nondominant_mode : 1;

        unsigned unused_1 : 1;

        unsigned negate_dest : 1;

        enum mali_dominant_factor dominant_factor : 3;
        unsigned complement_dominant : 1;
} __attribute__((packed));

struct mali_blend_equation {
        /* Of type mali_blend_mode */
        unsigned rgb_mode : 12;
        unsigned alpha_mode : 12;

        unsigned zero1 : 4;

        /* Corresponds to MALI_MASK_* above and glColorMask arguments */

        unsigned color_mask : 4;
} __attribute__((packed));

/* Used with channel swizzling */
enum mali_channel {
	MALI_CHANNEL_RED = 0,
	MALI_CHANNEL_GREEN = 1,
	MALI_CHANNEL_BLUE = 2,
	MALI_CHANNEL_ALPHA = 3,
	MALI_CHANNEL_ZERO = 4,
	MALI_CHANNEL_ONE = 5,
	MALI_CHANNEL_RESERVED_0 = 6,
	MALI_CHANNEL_RESERVED_1 = 7,
};

struct mali_channel_swizzle {
	enum mali_channel r : 3;
	enum mali_channel g : 3;
	enum mali_channel b : 3;
	enum mali_channel a : 3;
} __attribute__((packed));

/* Compressed per-pixel formats. Each of these formats expands to one to four
 * floating-point or integer numbers, as defined by the OpenGL specification.
 * There are various places in OpenGL where the user can specify a compressed
 * format in memory, which all use the same 8-bit enum in the various
 * descriptors, although different hardware units support different formats.
 */

/* The top 3 bits specify how the bits of each component are interpreted. */

/* e.g. R11F_G11F_B10F */
#define MALI_FORMAT_SPECIAL (2 << 5)

/* signed normalized, e.g. RGBA8_SNORM */
#define MALI_FORMAT_SNORM (3 << 5)

/* e.g. RGBA8UI */
#define MALI_FORMAT_UINT (4 << 5)

/* e.g. RGBA8 and RGBA32F */
#define MALI_FORMAT_UNORM (5 << 5)

/* e.g. RGBA8I and RGBA16F */
#define MALI_FORMAT_SINT (6 << 5)

/* These formats seem to largely duplicate the others. They're used at least
 * for Bifrost framebuffer output.
 */
#define MALI_FORMAT_SPECIAL2 (7 << 5)

/* If the high 3 bits are 3 to 6 these two bits say how many components
 * there are.
 */
#define MALI_NR_CHANNELS(n) ((n - 1) << 3)

/* If the high 3 bits are 3 to 6, then the low 3 bits say how big each
 * component is, except the special MALI_CHANNEL_FLOAT which overrides what the
 * bits mean.
 */

#define MALI_CHANNEL_4 2

#define MALI_CHANNEL_8 3

#define MALI_CHANNEL_16 4

#define MALI_CHANNEL_32 5

/* For MALI_FORMAT_SINT it means a half-float (e.g. RG16F). For
 * MALI_FORMAT_UNORM, it means a 32-bit float.
 */
#define MALI_CHANNEL_FLOAT 7

enum mali_format {
	MALI_RGB565         = MALI_FORMAT_SPECIAL | 0x0,
	MALI_RGB5_A1_UNORM  = MALI_FORMAT_SPECIAL | 0x2,
	MALI_RGB10_A2_UNORM = MALI_FORMAT_SPECIAL | 0x3,
	MALI_RGB10_A2_SNORM = MALI_FORMAT_SPECIAL | 0x5,
	MALI_RGB10_A2UI     = MALI_FORMAT_SPECIAL | 0x7,
	MALI_RGB10_A2I      = MALI_FORMAT_SPECIAL | 0x9,

	/* YUV formats */
	MALI_NV12           = MALI_FORMAT_SPECIAL | 0xc,

	MALI_Z32_UNORM      = MALI_FORMAT_SPECIAL | 0xD,
	MALI_R32_FIXED      = MALI_FORMAT_SPECIAL | 0x11,
	MALI_RG32_FIXED     = MALI_FORMAT_SPECIAL | 0x12,
	MALI_RGB32_FIXED    = MALI_FORMAT_SPECIAL | 0x13,
	MALI_RGBA32_FIXED   = MALI_FORMAT_SPECIAL | 0x14,
	MALI_R11F_G11F_B10F = MALI_FORMAT_SPECIAL | 0x19,
        MALI_R9F_G9F_B9F_E5F = MALI_FORMAT_SPECIAL | 0x1b,
	/* Only used for varyings, to indicate the transformed gl_Position */
	MALI_VARYING_POS    = MALI_FORMAT_SPECIAL | 0x1e,
	/* Only used for varyings, to indicate that the write should be
	 * discarded.
	 */
	MALI_VARYING_DISCARD = MALI_FORMAT_SPECIAL | 0x1f,

	MALI_R8_SNORM     = MALI_FORMAT_SNORM | MALI_NR_CHANNELS(1) | MALI_CHANNEL_8,
	MALI_R16_SNORM    = MALI_FORMAT_SNORM | MALI_NR_CHANNELS(1) | MALI_CHANNEL_16,
	MALI_R32_SNORM    = MALI_FORMAT_SNORM | MALI_NR_CHANNELS(1) | MALI_CHANNEL_32,
	MALI_RG8_SNORM    = MALI_FORMAT_SNORM | MALI_NR_CHANNELS(2) | MALI_CHANNEL_8,
	MALI_RG16_SNORM   = MALI_FORMAT_SNORM | MALI_NR_CHANNELS(2) | MALI_CHANNEL_16,
	MALI_RG32_SNORM   = MALI_FORMAT_SNORM | MALI_NR_CHANNELS(2) | MALI_CHANNEL_32,
	MALI_RGB8_SNORM   = MALI_FORMAT_SNORM | MALI_NR_CHANNELS(3) | MALI_CHANNEL_8,
	MALI_RGB16_SNORM  = MALI_FORMAT_SNORM | MALI_NR_CHANNELS(3) | MALI_CHANNEL_16,
	MALI_RGB32_SNORM  = MALI_FORMAT_SNORM | MALI_NR_CHANNELS(3) | MALI_CHANNEL_32,
	MALI_RGBA8_SNORM  = MALI_FORMAT_SNORM | MALI_NR_CHANNELS(4) | MALI_CHANNEL_8,
	MALI_RGBA16_SNORM = MALI_FORMAT_SNORM | MALI_NR_CHANNELS(4) | MALI_CHANNEL_16,
	MALI_RGBA32_SNORM = MALI_FORMAT_SNORM | MALI_NR_CHANNELS(4) | MALI_CHANNEL_32,

	MALI_R8UI     = MALI_FORMAT_UINT | MALI_NR_CHANNELS(1) | MALI_CHANNEL_8,
	MALI_R16UI    = MALI_FORMAT_UINT | MALI_NR_CHANNELS(1) | MALI_CHANNEL_16,
	MALI_R32UI    = MALI_FORMAT_UINT | MALI_NR_CHANNELS(1) | MALI_CHANNEL_32,
	MALI_RG8UI    = MALI_FORMAT_UINT | MALI_NR_CHANNELS(2) | MALI_CHANNEL_8,
	MALI_RG16UI   = MALI_FORMAT_UINT | MALI_NR_CHANNELS(2) | MALI_CHANNEL_16,
	MALI_RG32UI   = MALI_FORMAT_UINT | MALI_NR_CHANNELS(2) | MALI_CHANNEL_32,
	MALI_RGB8UI   = MALI_FORMAT_UINT | MALI_NR_CHANNELS(3) | MALI_CHANNEL_8,
	MALI_RGB16UI  = MALI_FORMAT_UINT | MALI_NR_CHANNELS(3) | MALI_CHANNEL_16,
	MALI_RGB32UI  = MALI_FORMAT_UINT | MALI_NR_CHANNELS(3) | MALI_CHANNEL_32,
	MALI_RGBA8UI  = MALI_FORMAT_UINT | MALI_NR_CHANNELS(4) | MALI_CHANNEL_8,
	MALI_RGBA16UI = MALI_FORMAT_UINT | MALI_NR_CHANNELS(4) | MALI_CHANNEL_16,
	MALI_RGBA32UI = MALI_FORMAT_UINT | MALI_NR_CHANNELS(4) | MALI_CHANNEL_32,

	MALI_R8_UNORM = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(1) | MALI_CHANNEL_8,
	MALI_R16_UNORM = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(1) | MALI_CHANNEL_16,
	MALI_R32_UNORM = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(1) | MALI_CHANNEL_32,
	MALI_R32F = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(1) | MALI_CHANNEL_FLOAT,
	MALI_RG8_UNORM    = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(2) | MALI_CHANNEL_8,
	MALI_RG16_UNORM   = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(2) | MALI_CHANNEL_16,
	MALI_RG32_UNORM   = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(2) | MALI_CHANNEL_32,
	MALI_RG32F = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(2) | MALI_CHANNEL_FLOAT,
	MALI_RGB8_UNORM   = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(3) | MALI_CHANNEL_8,
	MALI_RGB16_UNORM  = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(3) | MALI_CHANNEL_16,
	MALI_RGB32_UNORM  = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(3) | MALI_CHANNEL_32,
	MALI_RGB32F = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(3) | MALI_CHANNEL_FLOAT,
	MALI_RGBA4_UNORM  = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(4) | MALI_CHANNEL_4,
	MALI_RGBA8_UNORM  = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(4) | MALI_CHANNEL_8,
	MALI_RGBA16_UNORM = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(4) | MALI_CHANNEL_16,
	MALI_RGBA32_UNORM = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(4) | MALI_CHANNEL_32,
	MALI_RGBA32F = MALI_FORMAT_UNORM | MALI_NR_CHANNELS(4) | MALI_CHANNEL_FLOAT,

	MALI_R8I     = MALI_FORMAT_SINT | MALI_NR_CHANNELS(1) | MALI_CHANNEL_8,
	MALI_R16I    = MALI_FORMAT_SINT | MALI_NR_CHANNELS(1) | MALI_CHANNEL_16,
	MALI_R32I    = MALI_FORMAT_SINT | MALI_NR_CHANNELS(1) | MALI_CHANNEL_32,
	MALI_R16F    = MALI_FORMAT_SINT | MALI_NR_CHANNELS(1) | MALI_CHANNEL_FLOAT,
	MALI_RG8I    = MALI_FORMAT_SINT | MALI_NR_CHANNELS(2) | MALI_CHANNEL_8,
	MALI_RG16I   = MALI_FORMAT_SINT | MALI_NR_CHANNELS(2) | MALI_CHANNEL_16,
	MALI_RG32I   = MALI_FORMAT_SINT | MALI_NR_CHANNELS(2) | MALI_CHANNEL_32,
	MALI_RG16F   = MALI_FORMAT_SINT | MALI_NR_CHANNELS(2) | MALI_CHANNEL_FLOAT,
	MALI_RGB8I   = MALI_FORMAT_SINT | MALI_NR_CHANNELS(3) | MALI_CHANNEL_8,
	MALI_RGB16I  = MALI_FORMAT_SINT | MALI_NR_CHANNELS(3) | MALI_CHANNEL_16,
	MALI_RGB32I  = MALI_FORMAT_SINT | MALI_NR_CHANNELS(3) | MALI_CHANNEL_32,
	MALI_RGB16F  = MALI_FORMAT_SINT | MALI_NR_CHANNELS(3) | MALI_CHANNEL_FLOAT,
	MALI_RGBA8I  = MALI_FORMAT_SINT | MALI_NR_CHANNELS(4) | MALI_CHANNEL_8,
	MALI_RGBA16I = MALI_FORMAT_SINT | MALI_NR_CHANNELS(4) | MALI_CHANNEL_16,
	MALI_RGBA32I = MALI_FORMAT_SINT | MALI_NR_CHANNELS(4) | MALI_CHANNEL_32,
	MALI_RGBA16F = MALI_FORMAT_SINT | MALI_NR_CHANNELS(4) | MALI_CHANNEL_FLOAT,

	MALI_RGBA4      = MALI_FORMAT_SPECIAL2 | 0x8,
	MALI_RGBA8_2    = MALI_FORMAT_SPECIAL2 | 0xd,
	MALI_RGB10_A2_2 = MALI_FORMAT_SPECIAL2 | 0xe,
};


/* Alpha coverage is encoded as 4-bits (from a clampf), with inversion
 * literally performing a bitwise invert. This function produces slightly wrong
 * results and I'm not sure why; some rounding issue I suppose... */

#define MALI_ALPHA_COVERAGE(clampf) ((uint16_t) (int) (clampf * 15.0f))
#define MALI_GET_ALPHA_COVERAGE(nibble) ((float) nibble / 15.0f)

/* Applies to midgard1.flags */

/* Should the hardware perform early-Z testing? Normally should be set
 * for performance reasons. Clear if you use: discard,
 * alpha-to-coverage... * It's also possible this disables
 * forward-pixel kill; we're not quite sure which bit is which yet.
 * TODO: How does this interact with blending?*/

#define MALI_EARLY_Z (1 << 6)

/* Should the hardware calculate derivatives (via helper invocations)? Set in a
 * fragment shader that uses texturing or derivative functions */

#define MALI_HELPER_INVOCATIONS (1 << 7)

/* Flags denoting the fragment shader's use of tilebuffer readback. If the
 * shader might read any part of the tilebuffer, set MALI_READS_TILEBUFFER. If
 * it might read depth/stencil in particular, also set MALI_READS_ZS */

#define MALI_READS_ZS (1 << 8)
#define MALI_READS_TILEBUFFER (1 << 12)

/* The raw Midgard blend payload can either be an equation or a shader
 * address, depending on the context */

union midgard_blend {
        mali_ptr shader;

        struct {
                struct mali_blend_equation equation;
                float constant;
        };
};

/* On MRT Midgard systems (using an MFBD), each render target gets its own
 * blend descriptor */

#define MALI_BLEND_SRGB (0x400)

/* Dithering is specified here for MFBD, otherwise NO_DITHER for SFBD */
#define MALI_BLEND_NO_DITHER (0x800)

struct midgard_blend_rt {
        /* Flags base value of 0x200 to enable the render target.
         * OR with 0x1 for blending (anything other than REPLACE).
         * OR with 0x2 for programmable blending with 0-2 registers
         * OR with 0x3 for programmable blending with 2+ registers
         * OR with MALI_BLEND_SRGB for implicit sRGB
         */

        u64 flags;
        union midgard_blend blend;
} __attribute__((packed));

/* On Bifrost systems (all MRT), each render target gets one of these
 * descriptors */

struct bifrost_blend_rt {
        /* This is likely an analogue of the flags on
         * midgard_blend_rt */

        u16 flags; // = 0x200

        /* Single-channel blend constants are encoded in a sort of
         * fixed-point. Basically, the float is mapped to a byte, becoming
         * a high byte, and then the lower-byte is added for precision.
         * For the original float f:
         *
         * f = (constant_hi / 255) + (constant_lo / 65535)
         *
         * constant_hi = int(f / 255)
         * constant_lo = 65535*f - (65535/255) * constant_hi
         */

        u16 constant;

        struct mali_blend_equation equation;
        /*
         * - 0x19 normally
         * - 0x3 when this slot is unused (everything else is 0 except the index)
         * - 0x11 when this is the fourth slot (and it's used)
+	 * - 0 when there is a blend shader
         */
        u16 unk2;
        /* increments from 0 to 3 */
        u16 index;

	union {
		struct {
			/* So far, I've only seen:
			 * - R001 for 1-component formats
			 * - RG01 for 2-component formats
			 * - RGB1 for 3-component formats
			 * - RGBA for 4-component formats
			 */
			u32 swizzle : 12;
			enum mali_format format : 8;

			/* Type of the shader output variable. Note, this can
			 * be different from the format.
			 *
			 * 0: f16 (mediump float)
			 * 1: f32 (highp float)
			 * 2: i32 (highp int)
			 * 3: u32 (highp uint)
			 * 4: i16 (mediump int)
			 * 5: u16 (mediump uint)
			 */
			u32 shader_type : 3;
			u32 zero : 9;
		};

		/* Only the low 32 bits of the blend shader are stored, the
		 * high 32 bits are implicitly the same as the original shader.
		 * According to the kernel driver, the program counter for
		 * shaders is actually only 24 bits, so shaders cannot cross
		 * the 2^24-byte boundary, and neither can the blend shader.
		 * The blob handles this by allocating a 2^24 byte pool for
		 * shaders, and making sure that any blend shaders are stored
		 * in the same pool as the original shader. The kernel will
		 * make sure this allocation is aligned to 2^24 bytes.
		 */
		u32 shader;
	};
} __attribute__((packed));

/* Descriptor for the shader. Following this is at least one, up to four blend
 * descriptors for each active render target */

struct mali_shader_meta {
        mali_ptr shader;
        u16 sampler_count;
        u16 texture_count;
        u16 attribute_count;
        u16 varying_count;

        union {
                struct {
                        u32 uniform_buffer_count : 4;
                        u32 unk1 : 28; // = 0x800000 for vertex, 0x958020 for tiler
                } bifrost1;
                struct {
                        unsigned uniform_buffer_count : 4;
                        unsigned flags : 12;

                        /* Whole number of uniform registers used, times two;
                         * whole number of work registers used (no scale).
                         */
                        unsigned work_count : 5;
                        unsigned uniform_count : 5;
                        unsigned unknown2 : 6;
                } midgard1;
        };

        /* Same as glPolygoOffset() arguments */
        float depth_units;
        float depth_factor;

        u32 unknown2_2;

        u16 alpha_coverage;
        u16 unknown2_3;

        u8 stencil_mask_front;
        u8 stencil_mask_back;
        u16 unknown2_4;

        struct mali_stencil_test stencil_front;
        struct mali_stencil_test stencil_back;

        union {
                struct {
                        u32 unk3 : 7;
                        /* On Bifrost, some system values are preloaded in
                         * registers R55-R62 by the thread dispatcher prior to
                         * the start of shader execution. This is a bitfield
                         * with one entry for each register saying which
                         * registers need to be preloaded. Right now, the known
                         * values are:
                         *
                         * Vertex/compute:
                         * - R55 : gl_LocalInvocationID.xy
                         * - R56 : gl_LocalInvocationID.z + unknown in high 16 bits
                         * - R57 : gl_WorkGroupID.x
                         * - R58 : gl_WorkGroupID.y
                         * - R59 : gl_WorkGroupID.z
                         * - R60 : gl_GlobalInvocationID.x
                         * - R61 : gl_GlobalInvocationID.y/gl_VertexID (without base)
                         * - R62 : gl_GlobalInvocationID.z/gl_InstanceID (without base)
                         *
                         * Fragment:
                         * - R55 : unknown, never seen (but the bit for this is
                         *   always set?)
                         * - R56 : unknown (bit always unset)
                         * - R57 : gl_PrimitiveID
                         * - R58 : gl_FrontFacing in low bit, potentially other stuff
                         * - R59 : u16 fragment coordinates (used to compute
                         *   gl_FragCoord.xy, together with sample positions)
                         * - R60 : gl_SampleMask (used in epilog, so pretty
                         *   much always used, but the bit is always 0 -- is
                         *   this just always pushed?)
                         * - R61 : gl_SampleMaskIn and gl_SampleID, used by
                         *   varying interpolation.
                         * - R62 : unknown (bit always unset).
                         */
                        u32 preload_regs : 8;
                        /* In units of 8 bytes or 64 bits, since the
                         * uniform/const port loads 64 bits at a time.
                         */
                        u32 uniform_count : 7;
                        u32 unk4 : 10; // = 2
                } bifrost2;
                struct {
                        u32 unknown2_7;
                } midgard2;
        };

        /* zero on bifrost */
        u32 unknown2_8;

        /* Blending information for the older non-MRT Midgard HW. Check for
         * MALI_HAS_BLEND_SHADER to decide how to interpret.
         */

        union midgard_blend blend;
} __attribute__((packed));

/* This only concerns hardware jobs */

/* Possible values for job_descriptor_size */

#define MALI_JOB_32 0
#define MALI_JOB_64 1

struct mali_job_descriptor_header {
        u32 exception_status;
        u32 first_incomplete_task;
        u64 fault_pointer;
        u8 job_descriptor_size : 1;
        enum mali_job_type job_type : 7;
        u8 job_barrier : 1;
        u8 unknown_flags : 7;
        u16 job_index;
        u16 job_dependency_index_1;
        u16 job_dependency_index_2;

        union {
                u64 next_job_64;
                u32 next_job_32;
        };
} __attribute__((packed));

/* These concern exception_status */

/* Access type causing a fault, paralleling AS_FAULTSTATUS_* entries in the
 * kernel */

enum mali_exception_access {
        /* Atomic in the kernel for MMU, but that doesn't make sense for a job
         * fault so it's just unused */
        MALI_EXCEPTION_ACCESS_NONE    = 0,

        MALI_EXCEPTION_ACCESS_EXECUTE = 1,
        MALI_EXCEPTION_ACCESS_READ    = 2,
        MALI_EXCEPTION_ACCESS_WRITE   = 3
};

struct mali_payload_set_value {
        u64 out;
        u64 unknown;
} __attribute__((packed));

/* Special attributes have a fixed index */
#define MALI_SPECIAL_ATTRIBUTE_BASE 16
#define MALI_VERTEX_ID   (MALI_SPECIAL_ATTRIBUTE_BASE + 0)
#define MALI_INSTANCE_ID (MALI_SPECIAL_ATTRIBUTE_BASE + 1)

/*
 * Mali Attributes
 *
 * This structure lets the attribute unit compute the address of an attribute
 * given the vertex and instance ID. Unfortunately, the way this works is
 * rather complicated when instancing is enabled.
 *
 * To explain this, first we need to explain how compute and vertex threads are
 * dispatched. This is a guess (although a pretty firm guess!) since the
 * details are mostly hidden from the driver, except for attribute instancing.
 * When a quad is dispatched, it receives a single, linear index. However, we
 * need to translate that index into a (vertex id, instance id) pair, or a
 * (local id x, local id y, local id z) triple for compute shaders (although
 * vertex shaders and compute shaders are handled almost identically).
 * Focusing on vertex shaders, one option would be to do:
 *
 * vertex_id = linear_id % num_vertices
 * instance_id = linear_id / num_vertices
 *
 * but this involves a costly division and modulus by an arbitrary number.
 * Instead, we could pad num_vertices. We dispatch padded_num_vertices *
 * num_instances threads instead of num_vertices * num_instances, which results
 * in some "extra" threads with vertex_id >= num_vertices, which we have to
 * discard.  The more we pad num_vertices, the more "wasted" threads we
 * dispatch, but the division is potentially easier.
 *
 * One straightforward choice is to pad num_vertices to the next power of two,
 * which means that the division and modulus are just simple bit shifts and
 * masking. But the actual algorithm is a bit more complicated. The thread
 * dispatcher has special support for dividing by 3, 5, 7, and 9, in addition
 * to dividing by a power of two. This is possibly using the technique
 * described in patent US20170010862A1. As a result, padded_num_vertices can be
 * 1, 3, 5, 7, or 9 times a power of two. This results in less wasted threads,
 * since we need less padding.
 *
 * padded_num_vertices is picked by the hardware. The driver just specifies the
 * actual number of vertices. At least for Mali G71, the first few cases are
 * given by:
 *
 * num_vertices	| padded_num_vertices
 * 3		| 4
 * 4-7		| 8
 * 8-11		| 12 (3 * 4)
 * 12-15	| 16
 * 16-19	| 20 (5 * 4)
 *
 * Note that padded_num_vertices is a multiple of four (presumably because
 * threads are dispatched in groups of 4). Also, padded_num_vertices is always
 * at least one more than num_vertices, which seems like a quirk of the
 * hardware. For larger num_vertices, the hardware uses the following
 * algorithm: using the binary representation of num_vertices, we look at the
 * most significant set bit as well as the following 3 bits. Let n be the
 * number of bits after those 4 bits. Then we set padded_num_vertices according
 * to the following table:
 *
 * high bits	| padded_num_vertices
 * 1000		| 9 * 2^n
 * 1001		| 5 * 2^(n+1)
 * 101x		| 3 * 2^(n+2)
 * 110x		| 7 * 2^(n+1)
 * 111x		| 2^(n+4)
 *
 * For example, if num_vertices = 70 is passed to glDraw(), its binary
 * representation is 1000110, so n = 3 and the high bits are 1000, and
 * therefore padded_num_vertices = 9 * 2^3 = 72.
 *
 * The attribute unit works in terms of the original linear_id. if
 * num_instances = 1, then they are the same, and everything is simple.
 * However, with instancing things get more complicated. There are four
 * possible modes, two of them we can group together:
 *
 * 1. Use the linear_id directly. Only used when there is no instancing.
 *
 * 2. Use the linear_id modulo a constant. This is used for per-vertex
 * attributes with instancing enabled by making the constant equal
 * padded_num_vertices. Because the modulus is always padded_num_vertices, this
 * mode only supports a modulus that is a power of 2 times 1, 3, 5, 7, or 9.
 * The shift field specifies the power of two, while the extra_flags field
 * specifies the odd number. If shift = n and extra_flags = m, then the modulus
 * is (2m + 1) * 2^n. As an example, if num_vertices = 70, then as computed
 * above, padded_num_vertices = 9 * 2^3, so we should set extra_flags = 4 and
 * shift = 3. Note that we must exactly follow the hardware algorithm used to
 * get padded_num_vertices in order to correctly implement per-vertex
 * attributes.
 *
 * 3. Divide the linear_id by a constant. In order to correctly implement
 * instance divisors, we have to divide linear_id by padded_num_vertices times
 * to user-specified divisor. So first we compute padded_num_vertices, again
 * following the exact same algorithm that the hardware uses, then multiply it
 * by the GL-level divisor to get the hardware-level divisor. This case is
 * further divided into two more cases. If the hardware-level divisor is a
 * power of two, then we just need to shift. The shift amount is specified by
 * the shift field, so that the hardware-level divisor is just 2^shift.
 *
 * If it isn't a power of two, then we have to divide by an arbitrary integer.
 * For that, we use the well-known technique of multiplying by an approximation
 * of the inverse. The driver must compute the magic multiplier and shift
 * amount, and then the hardware does the multiplication and shift. The
 * hardware and driver also use the "round-down" optimization as described in
 * http://ridiculousfish.com/files/faster_unsigned_division_by_constants.pdf.
 * The hardware further assumes the multiplier is between 2^31 and 2^32, so the
 * high bit is implicitly set to 1 even though it is set to 0 by the driver --
 * presumably this simplifies the hardware multiplier a little. The hardware
 * first multiplies linear_id by the multiplier and takes the high 32 bits,
 * then applies the round-down correction if extra_flags = 1, then finally
 * shifts right by the shift field.
 *
 * There are some differences between ridiculousfish's algorithm and the Mali
 * hardware algorithm, which means that the reference code from ridiculousfish
 * doesn't always produce the right constants. Mali does not use the pre-shift
 * optimization, since that would make a hardware implementation slower (it
 * would have to always do the pre-shift, multiply, and post-shift operations).
 * It also forces the multplier to be at least 2^31, which means that the
 * exponent is entirely fixed, so there is no trial-and-error. Altogether,
 * given the divisor d, the algorithm the driver must follow is:
 *
 * 1. Set shift = floor(log2(d)).
 * 2. Compute m = ceil(2^(shift + 32) / d) and e = 2^(shift + 32) % d.
 * 3. If e <= 2^shift, then we need to use the round-down algorithm. Set
 * magic_divisor = m - 1 and extra_flags = 1.
 * 4. Otherwise, set magic_divisor = m and extra_flags = 0.
 *
 * Unrelated to instancing/actual attributes, images (the OpenCL kind) are
 * implemented as special attributes, denoted by MALI_ATTR_IMAGE. For images,
 * let shift=extra_flags=0. Stride is set to the image format's bytes-per-pixel
 * (*NOT the row stride*). Size is set to the size of the image itself.
 *
 * Special internal varyings (including gl_FrontFacing) could be seen as
 * IMAGE/INTERNAL as well as LINEAR, setting all fields set to zero and using a
 * special elements pseudo-pointer.
 */

enum mali_attr_mode {
	MALI_ATTR_UNUSED = 0,
	MALI_ATTR_LINEAR = 1,
	MALI_ATTR_POT_DIVIDE = 2,
	MALI_ATTR_MODULO = 3,
	MALI_ATTR_NPOT_DIVIDE = 4,
        MALI_ATTR_IMAGE = 5,
        MALI_ATTR_INTERNAL = 6
};

/* Pseudo-address for gl_FrontFacing, used with INTERNAL. Same addres is used
 * for gl_FragCoord with IMAGE, needing a coordinate flip. Who knows. */

#define MALI_VARYING_FRAG_COORD (0x25)
#define MALI_VARYING_FRONT_FACING (0x26)

/* This magic "pseudo-address" is used as `elements` to implement
 * gl_PointCoord. When read from a fragment shader, it generates a point
 * coordinate per the OpenGL ES 2.0 specification. Flipped coordinate spaces
 * require an affine transformation in the shader. */

#define MALI_VARYING_POINT_COORD (0x61)

/* Used for comparison to check if an address is special. Mostly a guess, but
 * it doesn't really matter. */

#define MALI_VARYING_SPECIAL (0x100)

union mali_attr {
	/* This is used for actual attributes. */
	struct {
		/* The bottom 3 bits are the mode */
		mali_ptr elements : 64 - 8;
		u32 shift : 5;
		u32 extra_flags : 3;
		u32 stride;
		u32 size;
	};
	/* The entry after an NPOT_DIVIDE entry has this format. It stores
	 * extra information that wouldn't fit in a normal entry.
	 */
	struct {
		u32 unk; /* = 0x20 */
		u32 magic_divisor;
		u32 zero;
		/* This is the original, GL-level divisor. */
		u32 divisor;
	};
} __attribute__((packed));

struct mali_attr_meta {
        /* Vertex buffer index */
        u8 index;

        unsigned unknown1 : 2;
        unsigned swizzle : 12;
        enum mali_format format : 8;

        /* Always observed to be zero at the moment */
        unsigned unknown3 : 2;

        /* When packing multiple attributes in a buffer, offset addresses by
         * this value. Obscurely, this is signed. */
        int32_t src_offset;
} __attribute__((packed));

enum mali_fbd_type {
        MALI_SFBD = 0,
        MALI_MFBD = 1,
};

#define FBD_TYPE (1)
#define FBD_MASK (~0x3f)

/* ORed into an MFBD address to specify the fbx section is included */
#define MALI_MFBD_TAG_EXTRA (0x2)

struct mali_uniform_buffer_meta {
        /* This is actually the size minus 1 (MALI_POSITIVE), in units of 16
         * bytes. This gives a maximum of 2^14 bytes, which just so happens to
         * be the GL minimum-maximum for GL_MAX_UNIFORM_BLOCK_SIZE.
         */
        u64 size : 10;

        /* This is missing the bottom 2 bits and top 8 bits. The top 8 bits
         * should be 0 for userspace pointers, according to
         * https://lwn.net/Articles/718895/. By reusing these bits, we can make
         * each entry in the table only 64 bits.
         */
        mali_ptr ptr : 64 - 10;
};

/* On Bifrost, these fields are the same between the vertex and tiler payloads.
 * They also seem to be the same between Bifrost and Midgard. They're shared in
 * fused payloads.
 */

/* Applies to unknown_draw */

#define MALI_DRAW_INDEXED_UINT8  (0x10)
#define MALI_DRAW_INDEXED_UINT16 (0x20)
#define MALI_DRAW_INDEXED_UINT32 (0x30)
#define MALI_DRAW_INDEXED_SIZE   (0x30)
#define MALI_DRAW_INDEXED_SHIFT  (4)

#define MALI_DRAW_VARYING_SIZE   (0x100)
#define MALI_DRAW_PRIMITIVE_RESTART_FIXED_INDEX (0x10000)

struct mali_vertex_tiler_prefix {
        /* This is a dynamic bitfield containing the following things in this order:
         *
         * - gl_WorkGroupSize.x
         * - gl_WorkGroupSize.y
         * - gl_WorkGroupSize.z
         * - gl_NumWorkGroups.x
         * - gl_NumWorkGroups.y
         * - gl_NumWorkGroups.z
         *
         * The number of bits allocated for each number is based on the *_shift
         * fields below. For example, workgroups_y_shift gives the bit that
         * gl_NumWorkGroups.y starts at, and workgroups_z_shift gives the bit
         * that gl_NumWorkGroups.z starts at (and therefore one after the bit
         * that gl_NumWorkGroups.y ends at). The actual value for each gl_*
         * value is one more than the stored value, since if any of the values
         * are zero, then there would be no invocations (and hence no job). If
         * there were 0 bits allocated to a given field, then it must be zero,
         * and hence the real value is one.
         *
         * Vertex jobs reuse the same job dispatch mechanism as compute jobs,
         * effectively doing glDispatchCompute(1, vertex_count, instance_count)
         * where vertex count is the number of vertices.
         */
        u32 invocation_count;

        u32 size_y_shift : 5;
        u32 size_z_shift : 5;
        u32 workgroups_x_shift : 6;
        u32 workgroups_y_shift : 6;
        u32 workgroups_z_shift : 6;
        /* This is max(workgroups_x_shift, 2) in all the cases I've seen. */
        u32 workgroups_x_shift_2 : 4;

        u32 draw_mode : 4;
        u32 unknown_draw : 22;

        /* This is the the same as workgroups_x_shift_2 in compute shaders, but
         * always 5 for vertex jobs and 6 for tiler jobs. I suspect this has
         * something to do with how many quads get put in the same execution
         * engine, which is a balance (you don't want to starve the engine, but
         * you also want to distribute work evenly).
         */
        u32 workgroups_x_shift_3 : 6;


        /* Negative of min_index. This is used to compute
         * the unbiased index in tiler/fragment shader runs.
         * 
         * The hardware adds offset_bias_correction in each run,
         * so that absent an index bias, the first vertex processed is
         * genuinely the first vertex (0). But with an index bias,
         * the first vertex process is numbered the same as the bias.
         *
         * To represent this more conviniently:
         * unbiased_index = lower_bound_index +
         *                  index_bias +
         *                  offset_bias_correction
         *
         * This is done since the hardware doesn't accept a index_bias
         * and this allows it to recover the unbiased index.
         */
        int32_t offset_bias_correction;
        u32 zero1;

        /* Like many other strictly nonzero quantities, index_count is
         * subtracted by one. For an indexed cube, this is equal to 35 = 6
         * faces * 2 triangles/per face * 3 vertices/per triangle - 1. That is,
         * for an indexed draw, index_count is the number of actual vertices
         * rendered whereas invocation_count is the number of unique vertices
         * rendered (the number of times the vertex shader must be invoked).
         * For non-indexed draws, this is just equal to invocation_count. */

        u32 index_count;

        /* No hidden structure; literally just a pointer to an array of uint
         * indices (width depends on flags). Thanks, guys, for not making my
         * life insane for once! NULL for non-indexed draws. */

        u64 indices;
} __attribute__((packed));

/* Point size / line width can either be specified as a 32-bit float (for
 * constant size) or as a [machine word size]-bit GPU pointer (for varying size). If a pointer
 * is selected, by setting the appropriate MALI_DRAW_VARYING_SIZE bit in the tiler
 * payload, the contents of varying_pointer will be intepreted as an array of
 * fp16 sizes, one for each vertex. gl_PointSize is therefore implemented by
 * creating a special MALI_R16F varying writing to varying_pointer. */

union midgard_primitive_size {
        float constant;
        u64 pointer;
};

struct bifrost_vertex_only {
        u32 unk2; /* =0x2 */

        u32 zero0;

        u64 zero1;
} __attribute__((packed));

struct bifrost_tiler_heap_meta {
        u32 zero;
        u32 heap_size;
        /* note: these are just guesses! */
        mali_ptr tiler_heap_start;
        mali_ptr tiler_heap_free;
        mali_ptr tiler_heap_end;

        /* hierarchy weights? but they're still 0 after the job has run... */
        u32 zeros[12];
} __attribute__((packed));

struct bifrost_tiler_meta {
        u64 zero0;
        u16 hierarchy_mask;
        u16 flags;
        u16 width;
        u16 height;
        u64 zero1;
        mali_ptr tiler_heap_meta;
        /* TODO what is this used for? */
        u64 zeros[20];
} __attribute__((packed));

struct bifrost_tiler_only {
        /* 0x20 */
        union midgard_primitive_size primitive_size;

        mali_ptr tiler_meta;

        u64 zero1, zero2, zero3, zero4, zero5, zero6;

        u32 gl_enables;
        u32 zero7;
        u64 zero8;
} __attribute__((packed));

struct bifrost_scratchpad {
        u32 zero;
        u32 flags; // = 0x1f
        /* This is a pointer to a CPU-inaccessible buffer, 16 pages, allocated
         * during startup. It seems to serve the same purpose as the
         * gpu_scratchpad in the SFBD for Midgard, although it's slightly
         * larger.
         */
        mali_ptr gpu_scratchpad;
} __attribute__((packed));

struct mali_vertex_tiler_postfix {
        /* Zero for vertex jobs. Pointer to the position (gl_Position) varying
         * output from the vertex shader for tiler jobs.
         */

        u64 position_varying;

        /* An array of mali_uniform_buffer_meta's. The size is given by the
         * shader_meta.
         */
        u64 uniform_buffers;

        /* This is a pointer to an array of pointers to the texture
         * descriptors, number of pointers bounded by number of textures. The
         * indirection is needed to accomodate varying numbers and sizes of
         * texture descriptors */
        u64 texture_trampoline;

        /* For OpenGL, from what I've seen, this is intimately connected to
         * texture_meta. cwabbott says this is not the case under Vulkan, hence
         * why this field is seperate (Midgard is Vulkan capable). Pointer to
         * array of sampler descriptors (which are uniform in size) */
        u64 sampler_descriptor;

        u64 uniforms;
        u64 shader;
        u64 attributes; /* struct attribute_buffer[] */
        u64 attribute_meta; /* attribute_meta[] */
        u64 varyings; /* struct attr */
        u64 varying_meta; /* pointer */
        u64 viewport;
        u64 occlusion_counter; /* A single bit as far as I can tell */

        /* Note: on Bifrost, this isn't actually the FBD. It points to
         * bifrost_scratchpad instead. However, it does point to the same thing
         * in vertex and tiler jobs.
         */
        mali_ptr framebuffer;
} __attribute__((packed));

struct midgard_payload_vertex_tiler {
        struct mali_vertex_tiler_prefix prefix;

        u16 gl_enables; // 0x5

        /* Both zero for non-instanced draws. For instanced draws, a
         * decomposition of padded_num_vertices. See the comments about the
         * corresponding fields in mali_attr for context. */

        unsigned instance_shift : 5;
        unsigned instance_odd : 3;

        u8 zero4;

        /* Offset for first vertex in buffer */
        u32 offset_start;

	u64 zero5;

        struct mali_vertex_tiler_postfix postfix;

        union midgard_primitive_size primitive_size;
} __attribute__((packed));

struct bifrost_payload_vertex {
        struct mali_vertex_tiler_prefix prefix;
        struct bifrost_vertex_only vertex;
        struct mali_vertex_tiler_postfix postfix;
} __attribute__((packed));

struct bifrost_payload_tiler {
        struct mali_vertex_tiler_prefix prefix;
        struct bifrost_tiler_only tiler;
        struct mali_vertex_tiler_postfix postfix;
} __attribute__((packed));

struct bifrost_payload_fused {
        struct mali_vertex_tiler_prefix prefix;
        struct bifrost_tiler_only tiler;
        struct mali_vertex_tiler_postfix tiler_postfix;
        u64 padding; /* zero */
        struct bifrost_vertex_only vertex;
        struct mali_vertex_tiler_postfix vertex_postfix;
} __attribute__((packed));

/* Purposeful off-by-one in width, height fields. For example, a (64, 64)
 * texture is stored as (63, 63) in these fields. This adjusts for that.
 * There's an identical pattern in the framebuffer descriptor. Even vertex
 * count fields work this way, hence the generic name -- integral fields that
 * are strictly positive generally need this adjustment. */

#define MALI_POSITIVE(dim) (dim - 1)

/* Opposite of MALI_POSITIVE, found in the depth_units field */

#define MALI_NEGATIVE(dim) (dim + 1)

/* Used with wrapping. Incomplete (this is a 4-bit field...) */

enum mali_wrap_mode {
        MALI_WRAP_REPEAT = 0x8,
        MALI_WRAP_CLAMP_TO_EDGE = 0x9,
        MALI_WRAP_CLAMP_TO_BORDER = 0xB,
        MALI_WRAP_MIRRORED_REPEAT = 0xC
};

/* Shared across both command stream and Midgard, and even with Bifrost */

enum mali_texture_type {
        MALI_TEX_CUBE = 0x0,
        MALI_TEX_1D = 0x1,
        MALI_TEX_2D = 0x2,
        MALI_TEX_3D = 0x3
};

/* 8192x8192 */
#define MAX_MIP_LEVELS (13)

/* Cubemap bloats everything up */
#define MAX_CUBE_FACES (6)

/* For each pointer, there is an address and optionally also a stride */
#define MAX_ELEMENTS (2)

/* It's not known why there are 4-bits allocated -- this enum is almost
 * certainly incomplete */

enum mali_texture_layout {
        /* For a Z/S texture, this is linear */
        MALI_TEXTURE_TILED = 0x1,

        /* Z/S textures cannot be tiled */
        MALI_TEXTURE_LINEAR = 0x2,

        /* 16x16 sparse */
        MALI_TEXTURE_AFBC = 0xC
};

/* Corresponds to the type passed to glTexImage2D and so forth */

struct mali_texture_format {
        unsigned swizzle : 12;
        enum mali_format format : 8;

        unsigned srgb : 1;
        unsigned unknown1 : 1;

        enum mali_texture_type type : 2;
        enum mali_texture_layout layout : 4;

        /* Always set */
        unsigned unknown2 : 1;

        /* Set to allow packing an explicit stride */
        unsigned manual_stride : 1;

        unsigned zero : 2;
} __attribute__((packed));

struct mali_texture_descriptor {
        uint16_t width;
        uint16_t height;
        uint16_t depth;
        uint16_t array_size;

        struct mali_texture_format format;

        uint16_t unknown3;

        /* One for non-mipmapped, zero for mipmapped */
        uint8_t unknown3A;

        /* Zero for non-mipmapped, (number of levels - 1) for mipmapped */
        uint8_t levels;

        /* Swizzling is a single 32-bit word, broken up here for convenience.
         * Here, swizzling refers to the ES 3.0 texture parameters for channel
         * level swizzling, not the internal pixel-level swizzling which is
         * below OpenGL's reach */

        unsigned swizzle : 12;
        unsigned swizzle_zero       : 20;

        uint32_t unknown5;
        uint32_t unknown6;
        uint32_t unknown7;

        mali_ptr payload[MAX_MIP_LEVELS * MAX_CUBE_FACES * MAX_ELEMENTS];
} __attribute__((packed));

/* filter_mode */

#define MALI_SAMP_MAG_NEAREST (1 << 0)
#define MALI_SAMP_MIN_NEAREST (1 << 1)

/* TODO: What do these bits mean individually? Only seen set together */

#define MALI_SAMP_MIP_LINEAR_1 (1 << 3)
#define MALI_SAMP_MIP_LINEAR_2 (1 << 4)

/* Flag in filter_mode, corresponding to OpenCL's NORMALIZED_COORDS_TRUE
 * sampler_t flag. For typical OpenGL textures, this is always set. */

#define MALI_SAMP_NORM_COORDS (1 << 5)

/* Used for lod encoding. Thanks @urjaman for pointing out these routines can
 * be cleaned up a lot. */

#define DECODE_FIXED_16(x) ((float) (x / 256.0))

static inline uint16_t
FIXED_16(float x)
{
        /* Clamp inputs, accounting for float error */
        float max_lod = (32.0 - (1.0 / 512.0));

        x = ((x > max_lod) ? max_lod : ((x < 0.0) ? 0.0 : x));

        return (int) (x * 256.0);
}

struct mali_sampler_descriptor {
        uint32_t filter_mode;

        /* Fixed point. Upper 8-bits is before the decimal point, although it
         * caps [0-31]. Lower 8-bits is after the decimal point: int(round(x *
         * 256)) */

        uint16_t min_lod;
        uint16_t max_lod;

        /* All one word in reality, but packed a bit */

        enum mali_wrap_mode wrap_s : 4;
        enum mali_wrap_mode wrap_t : 4;
        enum mali_wrap_mode wrap_r : 4;
        enum mali_alt_func compare_func : 3;

        /* No effect on 2D textures. For cubemaps, set for ES3 and clear for
         * ES2, controlling seamless cubemapping */
        unsigned seamless_cube_map : 1;

        unsigned zero : 16;

        uint32_t zero2;
        float border_color[4];
} __attribute__((packed));

/* viewport0/viewport1 form the arguments to glViewport. viewport1 is
 * modified by MALI_POSITIVE; viewport0 is as-is.
 */

struct mali_viewport {
        /* XY clipping planes */
        float clip_minx;
        float clip_miny;
        float clip_maxx;
        float clip_maxy;

        /* Depth clipping planes */
        float clip_minz;
        float clip_maxz;

        u16 viewport0[2];
        u16 viewport1[2];
} __attribute__((packed));

/* From presentations, 16x16 tiles externally. Use shift for fast computation
 * of tile numbers. */

#define MALI_TILE_SHIFT 4
#define MALI_TILE_LENGTH (1 << MALI_TILE_SHIFT)

/* Tile coordinates are stored as a compact u32, as only 12 bits are needed to
 * each component. Notice that this provides a theoretical upper bound of (1 <<
 * 12) = 4096 tiles in each direction, addressing a maximum framebuffer of size
 * 65536x65536. Multiplying that together, times another four given that Mali
 * framebuffers are 32-bit ARGB8888, means that this upper bound would take 16
 * gigabytes of RAM just to store the uncompressed framebuffer itself, let
 * alone rendering in real-time to such a buffer.
 *
 * Nice job, guys.*/

/* From mali_kbase_10969_workaround.c */
#define MALI_X_COORD_MASK 0x00000FFF
#define MALI_Y_COORD_MASK 0x0FFF0000

/* Extract parts of a tile coordinate */

#define MALI_TILE_COORD_X(coord) ((coord) & MALI_X_COORD_MASK)
#define MALI_TILE_COORD_Y(coord) (((coord) & MALI_Y_COORD_MASK) >> 16)

/* Helpers to generate tile coordinates based on the boundary coordinates in
 * screen space. So, with the bounds (0, 0) to (128, 128) for the screen, these
 * functions would convert it to the bounding tiles (0, 0) to (7, 7).
 * Intentional "off-by-one"; finding the tile number is a form of fencepost
 * problem. */

#define MALI_MAKE_TILE_COORDS(X, Y) ((X) | ((Y) << 16))
#define MALI_BOUND_TO_TILE(B, bias) ((B - bias) >> MALI_TILE_SHIFT)
#define MALI_COORDINATE_TO_TILE(W, H, bias) MALI_MAKE_TILE_COORDS(MALI_BOUND_TO_TILE(W, bias), MALI_BOUND_TO_TILE(H, bias))
#define MALI_COORDINATE_TO_TILE_MIN(W, H) MALI_COORDINATE_TO_TILE(W, H, 0)
#define MALI_COORDINATE_TO_TILE_MAX(W, H) MALI_COORDINATE_TO_TILE(W, H, 1)

struct mali_payload_fragment {
        u32 min_tile_coord;
        u32 max_tile_coord;
        mali_ptr framebuffer;
} __attribute__((packed));

/* Single Framebuffer Descriptor */

/* Flags apply to format. With just MSAA_A and MSAA_B, the framebuffer is
 * configured for 4x. With MSAA_8, it is configured for 8x. */

#define MALI_SFBD_FORMAT_MSAA_8 (1 << 3)
#define MALI_SFBD_FORMAT_MSAA_A (1 << 4)
#define MALI_SFBD_FORMAT_MSAA_B (1 << 4)
#define MALI_SFBD_FORMAT_SRGB 	(1 << 5)

/* Fast/slow based on whether all three buffers are cleared at once */

#define MALI_CLEAR_FAST         (1 << 18)
#define MALI_CLEAR_SLOW         (1 << 28)
#define MALI_CLEAR_SLOW_STENCIL (1 << 31)

/* Configures hierarchical tiling on Midgard for both SFBD/MFBD (embedded
 * within the larget framebuffer descriptor). Analogous to
 * bifrost_tiler_heap_meta and bifrost_tiler_meta*/

/* See pan_tiler.c for derivation */
#define MALI_HIERARCHY_MASK ((1 << 9) - 1)

/* Flag disabling the tiler for clear-only jobs */
#define MALI_TILER_DISABLED (1 << 12)

struct midgard_tiler_descriptor {
        /* Size of the entire polygon list; see pan_tiler.c for the
         * computation. It's based on hierarchical tiling */

        u32 polygon_list_size;

        /* Name known from the replay workaround in the kernel. What exactly is
         * flagged here is less known. We do that (tiler_hierarchy_mask & 0x1ff)
         * specifies a mask of hierarchy weights, which explains some of the
         * performance mysteries around setting it. We also see the bottom bit
         * of tiler_flags set in the kernel, but no comment why.
         *
         * hierarchy_mask can have the TILER_DISABLED flag */

        u16 hierarchy_mask;
        u16 flags;

        /* See mali_tiler.c for an explanation */
        mali_ptr polygon_list;
        mali_ptr polygon_list_body;

        /* Names based on we see symmetry with replay jobs which name these
         * explicitly */

        mali_ptr heap_start; /* tiler heap_free_address */
        mali_ptr heap_end;

        /* Hierarchy weights. We know these are weights based on the kernel,
         * but I've never seen them be anything other than zero */
        u32 weights[8];
};

enum mali_block_format {
        MALI_BLOCK_TILED   = 0x0,
        MALI_BLOCK_UNKNOWN = 0x1,
        MALI_BLOCK_LINEAR  = 0x2,
        MALI_BLOCK_AFBC    = 0x3,
};

struct mali_sfbd_format {
        /* 0x1 */
        unsigned unk1 : 6;

        /* mali_channel_swizzle */
        unsigned swizzle : 12;

        /* MALI_POSITIVE */
        unsigned nr_channels : 2;

        /* 0x4 */
        unsigned unk2 : 6;

        enum mali_block_format block : 2;

        /* 0xb */
        unsigned unk3 : 4;
};

struct mali_single_framebuffer {
        u32 unknown1;
        u32 unknown2;
        u64 unknown_address_0;
        u64 zero1;
        u64 zero0;

        struct mali_sfbd_format format;

        u32 clear_flags;
        u32 zero2;

        /* Purposeful off-by-one in these fields should be accounted for by the
         * MALI_DIMENSION macro */

        u16 width;
        u16 height;

        u32 zero3[4];
        mali_ptr checksum;
        u32 checksum_stride;
        u32 zero5;

        /* By default, the framebuffer is upside down from OpenGL's
         * perspective. Set framebuffer to the end and negate the stride to
         * flip in the Y direction */

        mali_ptr framebuffer;
        int32_t stride;

        u32 zero4;

        /* Depth and stencil buffers are interleaved, it appears, as they are
         * set to the same address in captures. Both fields set to zero if the
         * buffer is not being cleared. Depending on GL_ENABLE magic, you might
         * get a zero enable despite the buffer being present; that still is
         * disabled. */

        mali_ptr depth_buffer; // not SAME_VA
        u32 depth_stride_zero : 4;
        u32 depth_stride : 28;
        u32 zero7;

        mali_ptr stencil_buffer; // not SAME_VA
        u32 stencil_stride_zero : 4;
        u32 stencil_stride : 28;
        u32 zero8;

        u32 clear_color_1; // RGBA8888 from glClear, actually used by hardware
        u32 clear_color_2; // always equal, but unclear function?
        u32 clear_color_3; // always equal, but unclear function?
        u32 clear_color_4; // always equal, but unclear function?

        /* Set to zero if not cleared */

        float clear_depth_1; // float32, ditto
        float clear_depth_2; // float32, ditto
        float clear_depth_3; // float32, ditto
        float clear_depth_4; // float32, ditto

        u32 clear_stencil; // Exactly as it appears in OpenGL

        u32 zero6[7];

        struct midgard_tiler_descriptor tiler;

        /* More below this, maybe */
} __attribute__((packed));

/* On Midgard, this "framebuffer descriptor" is used for the framebuffer field
 * of compute jobs. Superficially resembles a single framebuffer descriptor */

struct mali_compute_fbd {
        u32 unknown1[8];
} __attribute__((packed));

/* Format bits for the render target flags */

#define MALI_MFBD_FORMAT_MSAA 	  (1 << 1)
#define MALI_MFBD_FORMAT_SRGB 	  (1 << 2)

struct mali_rt_format {
        unsigned unk1 : 32;
        unsigned unk2 : 3;

        unsigned nr_channels : 2; /* MALI_POSITIVE */

        unsigned unk3 : 5;
        enum mali_block_format block : 2;
        unsigned flags : 4;

        unsigned swizzle : 12;

        unsigned zero : 3;

        /* Disables MFBD preload. When this bit is set, the render target will
         * be cleared every frame. When this bit is clear, the hardware will
         * automatically wallpaper the render target back from main memory.
         * Unfortunately, MFBD preload is very broken on Midgard, so in
         * practice, this is a chicken bit that should always be set.
         * Discovered by accident, as all good chicken bits are. */

        unsigned no_preload : 1;
} __attribute__((packed));

struct bifrost_render_target {
        struct mali_rt_format format;

        u64 zero1;

        struct {
                /* Stuff related to ARM Framebuffer Compression. When AFBC is enabled,
                 * there is an extra metadata buffer that contains 16 bytes per tile.
                 * The framebuffer needs to be the same size as before, since we don't
                 * know ahead of time how much space it will take up. The
                 * framebuffer_stride is set to 0, since the data isn't stored linearly
                 * anymore.
                 *
                 * When AFBC is disabled, these fields are zero.
                 */

                mali_ptr metadata;
                u32 stride; // stride in units of tiles
                u32 unk; // = 0x20000
        } afbc;

        mali_ptr framebuffer;

        u32 zero2 : 4;
        u32 framebuffer_stride : 28; // in units of bytes
        u32 zero3;

        u32 clear_color_1; // RGBA8888 from glClear, actually used by hardware
        u32 clear_color_2; // always equal, but unclear function?
        u32 clear_color_3; // always equal, but unclear function?
        u32 clear_color_4; // always equal, but unclear function?
} __attribute__((packed));

/* An optional part of bifrost_framebuffer. It comes between the main structure
 * and the array of render targets. It must be included if any of these are
 * enabled:
 *
 * - Transaction Elimination
 * - Depth/stencil
 * - TODO: Anything else?
 */

/* Flags field: note, these are guesses */

#define MALI_EXTRA_PRESENT      (0x400)
#define MALI_EXTRA_AFBC         (0x20)
#define MALI_EXTRA_AFBC_ZS      (0x10)
#define MALI_EXTRA_ZS           (0x4)

struct bifrost_fb_extra {
        mali_ptr checksum;
        /* Each tile has an 8 byte checksum, so the stride is "width in tiles * 8" */
        u32 checksum_stride;

        u32 flags;

        union {
                /* Note: AFBC is only allowed for 24/8 combined depth/stencil. */
                struct {
                        mali_ptr depth_stencil_afbc_metadata;
                        u32 depth_stencil_afbc_stride; // in units of tiles
                        u32 zero1;

                        mali_ptr depth_stencil;

                        u64 padding;
                } ds_afbc;

                struct {
                        /* Depth becomes depth/stencil in case of combined D/S */
                        mali_ptr depth;
                        u32 depth_stride_zero : 4;
                        u32 depth_stride : 28;
                        u32 zero1;

                        mali_ptr stencil;
                        u32 stencil_stride_zero : 4;
                        u32 stencil_stride : 28;
                        u32 zero2;
                } ds_linear;
        };


        u64 zero3, zero4;
} __attribute__((packed));

/* Flags for mfbd_flags */

/* Enables writing depth results back to main memory (rather than keeping them
 * on-chip in the tile buffer and then discarding) */

#define MALI_MFBD_DEPTH_WRITE (1 << 10)

/* The MFBD contains the extra bifrost_fb_extra section */

#define MALI_MFBD_EXTRA (1 << 13)

struct bifrost_framebuffer {
        u32 unk0; // = 0x10

        u32 unknown2; // = 0x1f, same as SFBD
        mali_ptr scratchpad;

        /* 0x10 */
        mali_ptr sample_locations;
        mali_ptr unknown1;
        /* 0x20 */
        u16 width1, height1;
        u32 zero3;
        u16 width2, height2;
        u32 unk1 : 19; // = 0x01000
        u32 rt_count_1 : 2; // off-by-one (use MALI_POSITIVE)
        u32 unk2 : 3; // = 0
        u32 rt_count_2 : 3; // no off-by-one
        u32 zero4 : 5;
        /* 0x30 */
        u32 clear_stencil : 8;
        u32 mfbd_flags : 24; // = 0x100
        float clear_depth;

        struct midgard_tiler_descriptor tiler;

        /* optional: struct bifrost_fb_extra extra */
        /* struct bifrost_render_target rts[] */
} __attribute__((packed));

#endif /* __PANFROST_JOB_H__ */
