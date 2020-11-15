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
#include <stdbool.h>
#include <inttypes.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t mali_ptr;

/* Compressed per-pixel formats. Each of these formats expands to one to four
 * floating-point or integer numbers, as defined by the OpenGL specification.
 * There are various places in OpenGL where the user can specify a compressed
 * format in memory, which all use the same 8-bit enum in the various
 * descriptors, although different hardware units support different formats.
 */

/* The top 3 bits specify how the bits of each component are interpreted. */

/* e.g. ETC2_RGB8 */
#define MALI_FORMAT_COMPRESSED (0 << 5)

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
#define MALI_EXTRACT_TYPE(fmt) ((fmt) & 0xe0)

/* If the high 3 bits are 3 to 6 these two bits say how many components
 * there are.
 */
#define MALI_NR_CHANNELS(n) ((n - 1) << 3)
#define MALI_EXTRACT_CHANNELS(fmt) ((((fmt) >> 3) & 3) + 1)

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
#define MALI_EXTRACT_BITS(fmt) (fmt & 0x7)

#define MALI_EXTRACT_INDEX(pixfmt) (((pixfmt) >> 12) & 0xFF)

/* The raw Midgard blend payload can either be an equation or a shader
 * address, depending on the context */

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
 */

/* Purposeful off-by-one in width, height fields. For example, a (64, 64)
 * texture is stored as (63, 63) in these fields. This adjusts for that.
 * There's an identical pattern in the framebuffer descriptor. Even vertex
 * count fields work this way, hence the generic name -- integral fields that
 * are strictly positive generally need this adjustment. */

#define MALI_POSITIVE(dim) (dim - 1)

/* 8192x8192 */
#define MAX_MIP_LEVELS (13)

/* Cubemap bloats everything up */
#define MAX_CUBE_FACES (6)

/* For each pointer, there is an address and optionally also a stride */
#define MAX_ELEMENTS (2)

/* Used for lod encoding. Thanks @urjaman for pointing out these routines can
 * be cleaned up a lot. */

#define DECODE_FIXED_16(x) ((float) (x / 256.0))

static inline int16_t
FIXED_16(float x, bool allow_negative)
{
        /* Clamp inputs, accounting for float error */
        float max_lod = (32.0 - (1.0 / 512.0));
        float min_lod = allow_negative ? -max_lod : 0.0;

        x = ((x > max_lod) ? max_lod : ((x < min_lod) ? min_lod : x));

        return (int) (x * 256.0);
}

#endif /* __PANFROST_JOB_H__ */
