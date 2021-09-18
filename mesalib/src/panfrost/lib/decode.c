/*
 * Copyright (C) 2017-2019 Alyssa Rosenzweig
 * Copyright (C) 2017-2019 Connor Abbott
 * Copyright (C) 2019 Collabora, Ltd.
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
 */

#include <gen_macros.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include "decode.h"

#include "midgard/disassemble.h"
#include "bifrost/disassemble.h"

#define DUMP_UNPACKED(T, var, ...) { \
        pandecode_log(__VA_ARGS__); \
        pan_print(pandecode_dump_stream, T, var, (pandecode_indent + 1) * 2); \
}

#define DUMP_CL(T, cl, ...) {\
        pan_unpack(cl, T, temp); \
        DUMP_UNPACKED(T, temp, __VA_ARGS__); \
}

#define DUMP_SECTION(A, S, cl, ...) { \
        pan_section_unpack(cl, A, S, temp); \
        pandecode_log(__VA_ARGS__); \
        pan_section_print(pandecode_dump_stream, A, S, temp, (pandecode_indent + 1) * 2); \
}

#define MAP_ADDR(T, addr, cl) \
        const uint8_t *cl = 0; \
        { \
                struct pandecode_mapped_memory *mapped_mem = pandecode_find_mapped_gpu_mem_containing(addr); \
                cl = pandecode_fetch_gpu_mem(mapped_mem, addr, pan_size(T)); \
        }

#define DUMP_ADDR(T, addr, ...) {\
        MAP_ADDR(T, addr, cl) \
        DUMP_CL(T, cl, __VA_ARGS__); \
}

FILE *pandecode_dump_stream;

/* Semantic logging type.
 *
 * Raw: for raw messages to be printed as is.
 * Message: for helpful information to be commented out in replays.
 *
 * Use one of pandecode_log or pandecode_msg as syntax sugar.
 */

enum pandecode_log_type {
        PANDECODE_RAW,
        PANDECODE_MESSAGE,
};

#define pandecode_log(...)  pandecode_log_typed(PANDECODE_RAW,      __VA_ARGS__)
#define pandecode_msg(...)  pandecode_log_typed(PANDECODE_MESSAGE,  __VA_ARGS__)

unsigned pandecode_indent = 0;

static void
pandecode_make_indent(void)
{
        for (unsigned i = 0; i < pandecode_indent; ++i)
                fprintf(pandecode_dump_stream, "  ");
}

static void PRINTFLIKE(2, 3)
pandecode_log_typed(enum pandecode_log_type type, const char *format, ...)
{
        va_list ap;

        pandecode_make_indent();

        if (type == PANDECODE_MESSAGE)
                fprintf(pandecode_dump_stream, "// ");

        va_start(ap, format);
        vfprintf(pandecode_dump_stream, format, ap);
        va_end(ap);
}

static void
pandecode_log_cont(const char *format, ...)
{
        va_list ap;

        va_start(ap, format);
        vfprintf(pandecode_dump_stream, format, ap);
        va_end(ap);
}

/* To check for memory safety issues, validates that the given pointer in GPU
 * memory is valid, containing at least sz bytes. The goal is to eliminate
 * GPU-side memory bugs (NULL pointer dereferences, buffer overflows, or buffer
 * overruns) by statically validating pointers.
 */

static void
pandecode_validate_buffer(mali_ptr addr, size_t sz)
{
        if (!addr) {
                pandecode_msg("XXX: null pointer deref");
                return;
        }

        /* Find a BO */

        struct pandecode_mapped_memory *bo =
                pandecode_find_mapped_gpu_mem_containing(addr);

        if (!bo) {
                pandecode_msg("XXX: invalid memory dereference\n");
                return;
        }

        /* Bounds check */

        unsigned offset = addr - bo->gpu_va;
        unsigned total = offset + sz;

        if (total > bo->length) {
                pandecode_msg("XXX: buffer overrun. "
                                "Chunk of size %zu at offset %d in buffer of size %zu. "
                                "Overrun by %zu bytes. \n",
                                sz, offset, bo->length, total - bo->length);
                return;
        }
}

/* Midgard's tiler descriptor is embedded within the
 * larger FBD */

static void
pandecode_midgard_tiler_descriptor(
                const struct mali_midgard_tiler_packed *tp,
                const struct mali_midgard_tiler_weights_packed *wp)
{
        pan_unpack(tp, MIDGARD_TILER, t);
        DUMP_UNPACKED(MIDGARD_TILER, t, "Tiler:\n");

        /* We've never seen weights used in practice, but they exist */
        pan_unpack(wp, MIDGARD_TILER_WEIGHTS, w);
        bool nonzero_weights = false;

        nonzero_weights |= w.weight0 != 0x0;
        nonzero_weights |= w.weight1 != 0x0;
        nonzero_weights |= w.weight2 != 0x0;
        nonzero_weights |= w.weight3 != 0x0;
        nonzero_weights |= w.weight4 != 0x0;
        nonzero_weights |= w.weight5 != 0x0;
        nonzero_weights |= w.weight6 != 0x0;
        nonzero_weights |= w.weight7 != 0x0;

        if (nonzero_weights)
                DUMP_UNPACKED(MIDGARD_TILER_WEIGHTS, w, "Tiler Weights:\n");
}

/* Information about the framebuffer passed back for
 * additional analysis */

struct pandecode_fbd {
        unsigned width;
        unsigned height;
        unsigned rt_count;
        bool has_extra;
};

static struct pandecode_fbd
pandecode_sfbd(uint64_t gpu_va, int job_no, bool is_fragment, unsigned gpu_id)
{
        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing(gpu_va);
        const void *PANDECODE_PTR_VAR(s, mem, (mali_ptr) gpu_va);

        struct pandecode_fbd info = {
                .has_extra = false,
                .rt_count = 1
        };

        pandecode_log("Single-Target Framebuffer:\n");
        pandecode_indent++;

        DUMP_SECTION(SINGLE_TARGET_FRAMEBUFFER, LOCAL_STORAGE, s, "Local Storage:\n");
        pan_section_unpack(s, SINGLE_TARGET_FRAMEBUFFER, PARAMETERS, p);
        DUMP_UNPACKED(SINGLE_TARGET_FRAMEBUFFER_PARAMETERS, p, "Parameters:\n");

        const void *t = pan_section_ptr(s, SINGLE_TARGET_FRAMEBUFFER, TILER);
        const void *w = pan_section_ptr(s, SINGLE_TARGET_FRAMEBUFFER, TILER_WEIGHTS);

        pandecode_midgard_tiler_descriptor(t, w);

        pandecode_indent--;

        /* Dummy unpack of the padding section to make sure all words are 0.
         * No need to call print here since the section is supposed to be empty.
         */
        pan_section_unpack(s, SINGLE_TARGET_FRAMEBUFFER, PADDING_1, padding1);
        pan_section_unpack(s, SINGLE_TARGET_FRAMEBUFFER, PADDING_2, padding2);
        pandecode_log("\n");

        return info;
}

static void
pandecode_local_storage(uint64_t gpu_va, int job_no)
{
        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing(gpu_va);
        const struct mali_local_storage_packed *PANDECODE_PTR_VAR(s, mem, (mali_ptr) gpu_va);
        DUMP_CL(LOCAL_STORAGE, s, "Local Storage:\n");
}

static void
pandecode_render_target(uint64_t gpu_va, unsigned job_no, bool is_bifrost, unsigned gpu_id,
                        const struct MALI_MULTI_TARGET_FRAMEBUFFER_PARAMETERS *fb)
{
        pandecode_log("Color Render Targets:\n");
        pandecode_indent++;

        for (int i = 0; i < (fb->render_target_count); i++) {
                mali_ptr rt_va = gpu_va + i * pan_size(RENDER_TARGET);
                struct pandecode_mapped_memory *mem =
                        pandecode_find_mapped_gpu_mem_containing(rt_va);
                const struct mali_render_target_packed *PANDECODE_PTR_VAR(rtp, mem, (mali_ptr) rt_va);
                DUMP_CL(RENDER_TARGET, rtp, "Color Render Target %d:\n", i);
        }

        pandecode_indent--;
        pandecode_log("\n");
}

static void
pandecode_sample_locations(const void *fb, int job_no)
{
        pan_section_unpack(fb, MULTI_TARGET_FRAMEBUFFER, BIFROST_PARAMETERS, params);

        struct pandecode_mapped_memory *smem =
                pandecode_find_mapped_gpu_mem_containing(params.sample_locations);

        const u16 *PANDECODE_PTR_VAR(samples, smem, params.sample_locations);

        pandecode_log("Sample locations:\n");
        for (int i = 0; i < 33; i++) {
                pandecode_log("  (%d, %d),\n",
                                samples[2 * i] - 128,
                                samples[2 * i + 1] - 128);
        }
}

static void
pandecode_dcd(const struct MALI_DRAW *p,
              int job_no, enum mali_job_type job_type,
              char *suffix, bool is_bifrost, unsigned gpu_id);

static struct pandecode_fbd
pandecode_mfbd_bfr(uint64_t gpu_va, int job_no, bool is_fragment, bool is_bifrost, unsigned gpu_id)
{
        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing(gpu_va);
        const void *PANDECODE_PTR_VAR(fb, mem, (mali_ptr) gpu_va);
        pan_section_unpack(fb, MULTI_TARGET_FRAMEBUFFER, PARAMETERS, params);

        struct pandecode_fbd info;

        if (is_bifrost) {
                pandecode_sample_locations(fb, job_no);

                pan_section_unpack(fb, MULTI_TARGET_FRAMEBUFFER, BIFROST_PARAMETERS, bparams);
                unsigned dcd_size = pan_size(DRAW) + pan_size(DRAW_PADDING);
                struct pandecode_mapped_memory *dcdmem =
                        pandecode_find_mapped_gpu_mem_containing(bparams.frame_shader_dcds);

                if (bparams.pre_frame_0 != MALI_PRE_POST_FRAME_SHADER_MODE_NEVER) {
                        const void *PANDECODE_PTR_VAR(dcd, dcdmem, bparams.frame_shader_dcds + (0 * dcd_size));
                        pan_unpack(dcd, DRAW, draw);
                        pandecode_log("Pre frame 0:\n");
                        pandecode_dcd(&draw, job_no, MALI_JOB_TYPE_FRAGMENT, "", true, gpu_id);
                }

                if (bparams.pre_frame_1 != MALI_PRE_POST_FRAME_SHADER_MODE_NEVER) {
                        const void *PANDECODE_PTR_VAR(dcd, dcdmem, bparams.frame_shader_dcds + (1 * dcd_size));
                        pan_unpack(dcd, DRAW, draw);
                        pandecode_log("Pre frame 1:\n");
                        pandecode_dcd(&draw, job_no, MALI_JOB_TYPE_FRAGMENT, "", true, gpu_id);
                }

                if (bparams.post_frame != MALI_PRE_POST_FRAME_SHADER_MODE_NEVER) {
                        const void *PANDECODE_PTR_VAR(dcd, dcdmem, bparams.frame_shader_dcds + (2 * dcd_size));
                        pan_unpack(dcd, DRAW, draw);
                        pandecode_log("Post frame:\n");
                        pandecode_dcd(&draw, job_no, MALI_JOB_TYPE_FRAGMENT, "", true, gpu_id);
                }
        }
 
        pandecode_log("Multi-Target Framebuffer:\n");
        pandecode_indent++;

        if (is_bifrost) {
                DUMP_SECTION(MULTI_TARGET_FRAMEBUFFER, BIFROST_PARAMETERS, fb, "Bifrost Params:\n");
        } else {
                DUMP_SECTION(MULTI_TARGET_FRAMEBUFFER, LOCAL_STORAGE, fb, "Local Storage:\n");
        }

        info.width = params.width;
        info.height = params.height;
        info.rt_count = params.render_target_count;
        DUMP_UNPACKED(MULTI_TARGET_FRAMEBUFFER_PARAMETERS, params, "Parameters:\n");

        if (is_bifrost) {
                DUMP_SECTION(MULTI_TARGET_FRAMEBUFFER, BIFROST_TILER_POINTER, fb, "Tiler Pointer");
                pan_section_unpack(fb, MULTI_TARGET_FRAMEBUFFER, BIFROST_PADDING, padding);
        } else {
                const void *t = pan_section_ptr(fb, MULTI_TARGET_FRAMEBUFFER, TILER);
                const void *w = pan_section_ptr(fb, MULTI_TARGET_FRAMEBUFFER, TILER_WEIGHTS);
                pandecode_midgard_tiler_descriptor(t, w);
        }

        pandecode_indent--;
        pandecode_log("\n");

        gpu_va += pan_size(MULTI_TARGET_FRAMEBUFFER);

        info.has_extra = params.has_zs_crc_extension;

        if (info.has_extra) {
                struct pandecode_mapped_memory *mem =
                        pandecode_find_mapped_gpu_mem_containing(gpu_va);
                const struct mali_zs_crc_extension_packed *PANDECODE_PTR_VAR(zs_crc, mem, (mali_ptr)gpu_va);
                DUMP_CL(ZS_CRC_EXTENSION, zs_crc, "ZS CRC Extension:\n");
                pandecode_log("\n");

                gpu_va += pan_size(ZS_CRC_EXTENSION);
        }

        if (is_fragment)
                pandecode_render_target(gpu_va, job_no, is_bifrost, gpu_id, &params);

        return info;
}

static void
pandecode_attributes(const struct pandecode_mapped_memory *mem,
                            mali_ptr addr, int job_no, char *suffix,
                            int count, bool varying, enum mali_job_type job_type)
{
        char *prefix = varying ? "Varying" : "Attribute";
        assert(addr);

        if (!count) {
                pandecode_msg("warn: No %s records\n", prefix);
                return;
        }

        MAP_ADDR(ATTRIBUTE_BUFFER, addr, cl);

        for (int i = 0; i < count; ++i) {
                pan_unpack(cl + i * pan_size(ATTRIBUTE_BUFFER), ATTRIBUTE_BUFFER, temp);
                DUMP_UNPACKED(ATTRIBUTE_BUFFER, temp, "%s:\n", prefix);

                switch (temp.type) {
                case MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR_WRITE_REDUCTION:
                case MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR: {
                        pan_unpack(cl + (i + 1) * pan_size(ATTRIBUTE_BUFFER),
                                   ATTRIBUTE_BUFFER_CONTINUATION_NPOT, temp2);
                        pan_print(pandecode_dump_stream, ATTRIBUTE_BUFFER_CONTINUATION_NPOT,
                                  temp2, (pandecode_indent + 1) * 2);
                        i++;
                        break;
                }
                case MALI_ATTRIBUTE_TYPE_3D_LINEAR:
                case MALI_ATTRIBUTE_TYPE_3D_INTERLEAVED: {
                        pan_unpack(cl + (i + 1) * pan_size(ATTRIBUTE_BUFFER_CONTINUATION_3D),
                                   ATTRIBUTE_BUFFER_CONTINUATION_3D, temp2);
                        pan_print(pandecode_dump_stream, ATTRIBUTE_BUFFER_CONTINUATION_3D,
                                  temp2, (pandecode_indent + 1) * 2);
                        i++;
                        break;
                }
                default:
                        break;
                }
        }
        pandecode_log("\n");
}

/* Decodes a Bifrost blend constant. See the notes in bifrost_blend_rt */

static mali_ptr
pandecode_bifrost_blend(void *descs, int job_no, int rt_no, mali_ptr frag_shader)
{
        pan_unpack(descs + (rt_no * pan_size(BLEND)), BLEND, b);
        DUMP_UNPACKED(BLEND, b, "Blend RT %d:\n", rt_no);
        if (b.bifrost.internal.mode != MALI_BIFROST_BLEND_MODE_SHADER)
                return 0;

        return (frag_shader & 0xFFFFFFFF00000000ULL) | b.bifrost.internal.shader.pc;
}

static mali_ptr
pandecode_midgard_blend_mrt(void *descs, int job_no, int rt_no)
{
        pan_unpack(descs + (rt_no * pan_size(BLEND)), BLEND, b);
        DUMP_UNPACKED(BLEND, b, "Blend RT %d:\n", rt_no);
        return b.midgard.blend_shader ? (b.midgard.shader_pc & ~0xf) : 0;
}

static unsigned
pandecode_attribute_meta(int count, mali_ptr attribute, bool varying)
{
        unsigned max = 0;

        for (int i = 0; i < count; ++i, attribute += pan_size(ATTRIBUTE)) {
                MAP_ADDR(ATTRIBUTE, attribute, cl);
                pan_unpack(cl, ATTRIBUTE, a);
                DUMP_UNPACKED(ATTRIBUTE, a, "%s:\n", varying ? "Varying" : "Attribute");
                max = MAX2(max, a.buffer_index);
        }

        pandecode_log("\n");
        return MIN2(max + 1, 256);
}

/* return bits [lo, hi) of word */
static u32
bits(u32 word, u32 lo, u32 hi)
{
        if (hi - lo >= 32)
                return word; // avoid undefined behavior with the shift

        if (lo >= 32)
                return 0;

        return (word >> lo) & ((1 << (hi - lo)) - 1);
}

static void
pandecode_invocation(const void *i)
{
        /* Decode invocation_count. See the comment before the definition of
         * invocation_count for an explanation.
         */
        pan_unpack(i, INVOCATION, invocation);

        unsigned size_x = bits(invocation.invocations, 0, invocation.size_y_shift) + 1;
        unsigned size_y = bits(invocation.invocations, invocation.size_y_shift, invocation.size_z_shift) + 1;
        unsigned size_z = bits(invocation.invocations, invocation.size_z_shift, invocation.workgroups_x_shift) + 1;

        unsigned groups_x = bits(invocation.invocations, invocation.workgroups_x_shift, invocation.workgroups_y_shift) + 1;
        unsigned groups_y = bits(invocation.invocations, invocation.workgroups_y_shift, invocation.workgroups_z_shift) + 1;
        unsigned groups_z = bits(invocation.invocations, invocation.workgroups_z_shift, 32) + 1;

        pandecode_log("Invocation (%d, %d, %d) x (%d, %d, %d)\n",
                      size_x, size_y, size_z,
                      groups_x, groups_y, groups_z);

        DUMP_UNPACKED(INVOCATION, invocation, "Invocation:\n")
}

static void
pandecode_primitive(const void *p)
{
        pan_unpack(p, PRIMITIVE, primitive);
        DUMP_UNPACKED(PRIMITIVE, primitive, "Primitive:\n");

        /* Validate an index buffer is present if we need one. TODO: verify
         * relationship between invocation_count and index_count */

        if (primitive.indices) {
                /* Grab the size */
                unsigned size = (primitive.index_type == MALI_INDEX_TYPE_UINT32) ?
                        sizeof(uint32_t) : primitive.index_type;

                /* Ensure we got a size, and if so, validate the index buffer
                 * is large enough to hold a full set of indices of the given
                 * size */

                if (!size)
                        pandecode_msg("XXX: index size missing\n");
                else
                        pandecode_validate_buffer(primitive.indices, primitive.index_count * size);
        } else if (primitive.index_type)
                pandecode_msg("XXX: unexpected index size\n");
}

static void
pandecode_uniform_buffers(mali_ptr pubufs, int ubufs_count, int job_no)
{
        struct pandecode_mapped_memory *umem = pandecode_find_mapped_gpu_mem_containing(pubufs);
        uint64_t *PANDECODE_PTR_VAR(ubufs, umem, pubufs);

        for (int i = 0; i < ubufs_count; i++) {
                mali_ptr addr = (ubufs[i] >> 10) << 2;
                unsigned size = addr ? (((ubufs[i] & ((1 << 10) - 1)) + 1) * 16) : 0;

                pandecode_validate_buffer(addr, size);

                char *ptr = pointer_as_memory_reference(addr);
                pandecode_log("ubuf_%d[%u] = %s;\n", i, size, ptr);
                free(ptr);
        }

        pandecode_log("\n");
}

static void
pandecode_uniforms(mali_ptr uniforms, unsigned uniform_count)
{
        pandecode_validate_buffer(uniforms, uniform_count * 16);

        char *ptr = pointer_as_memory_reference(uniforms);
        pandecode_log("vec4 uniforms[%u] = %s;\n", uniform_count, ptr);
        free(ptr);
        pandecode_log("\n");
}

static const char *
shader_type_for_job(unsigned type)
{
        switch (type) {
        case MALI_JOB_TYPE_VERTEX:  return "VERTEX";
        case MALI_JOB_TYPE_TILER:   return "FRAGMENT";
        case MALI_JOB_TYPE_FRAGMENT: return "FRAGMENT";
        case MALI_JOB_TYPE_COMPUTE: return "COMPUTE";
        default: return "UNKNOWN";
        }
}

static unsigned shader_id = 0;

static struct midgard_disasm_stats
pandecode_shader_disassemble(mali_ptr shader_ptr, int shader_no, int type,
                             bool is_bifrost, unsigned gpu_id)
{
        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing(shader_ptr);
        uint8_t *PANDECODE_PTR_VAR(code, mem, shader_ptr);

        /* Compute maximum possible size */
        size_t sz = mem->length - (shader_ptr - mem->gpu_va);

        /* Print some boilerplate to clearly denote the assembly (which doesn't
         * obey indentation rules), and actually do the disassembly! */

        pandecode_log_cont("\n\n");

        struct midgard_disasm_stats stats;

        if (is_bifrost) {
                disassemble_bifrost(pandecode_dump_stream, code, sz, true);

                /* TODO: Extend stats to Bifrost */
                stats.texture_count = -128;
                stats.sampler_count = -128;
                stats.attribute_count = -128;
                stats.varying_count = -128;
                stats.uniform_count = -128;
                stats.uniform_buffer_count = -128;
                stats.work_count = -128;

                stats.instruction_count = 0;
                stats.bundle_count = 0;
                stats.quadword_count = 0;
                stats.helper_invocations = false;
        } else {
                stats = disassemble_midgard(pandecode_dump_stream,
                                code, sz, gpu_id, true);
        }

        unsigned nr_threads =
                (stats.work_count <= 4) ? 4 :
                (stats.work_count <= 8) ? 2 :
                1;

        pandecode_log_cont("shader%d - MESA_SHADER_%s shader: "
                "%u inst, %u bundles, %u quadwords, "
                "%u registers, %u threads, 0 loops, 0:0 spills:fills\n\n\n",
                shader_id++,
                shader_type_for_job(type),
                stats.instruction_count, stats.bundle_count, stats.quadword_count,
                stats.work_count, nr_threads);

        return stats;
}

static void
pandecode_texture_payload(mali_ptr payload,
                          enum mali_texture_dimension dim,
                          enum mali_texture_layout layout,
                          bool manual_stride,
                          uint8_t levels,
                          uint16_t nr_samples,
                          uint16_t array_size,
                          struct pandecode_mapped_memory *tmem)
{
        pandecode_log(".payload = {\n");
        pandecode_indent++;

        /* A bunch of bitmap pointers follow.
         * We work out the correct number,
         * based on the mipmap/cubemap
         * properties, but dump extra
         * possibilities to futureproof */

        int bitmap_count = levels;

        /* Miptree for each face */
        if (dim == MALI_TEXTURE_DIMENSION_CUBE)
                bitmap_count *= 6;

        /* Array of layers */
        bitmap_count *= nr_samples;

        /* Array of textures */
        bitmap_count *= array_size;

        /* Stride for each element */
        if (manual_stride)
                bitmap_count *= 2;

        mali_ptr *pointers_and_strides = pandecode_fetch_gpu_mem(tmem,
                payload, sizeof(mali_ptr) * bitmap_count);
        for (int i = 0; i < bitmap_count; ++i) {
                /* How we dump depends if this is a stride or a pointer */

                if (manual_stride && (i & 1)) {
                        /* signed 32-bit snuck in as a 64-bit pointer */
                        uint64_t stride_set = pointers_and_strides[i];
                        int32_t line_stride = stride_set;
                        int32_t surface_stride = stride_set >> 32;
                        pandecode_log("(mali_ptr) %d /* surface stride */ %d /* line stride */, \n",
                                      surface_stride, line_stride);
                } else {
                        char *a = pointer_as_memory_reference(pointers_and_strides[i]);
                        pandecode_log("%s, \n", a);
                        free(a);
                }
        }

        pandecode_indent--;
        pandecode_log("},\n");
}

static void
pandecode_texture(mali_ptr u,
                struct pandecode_mapped_memory *tmem,
                unsigned job_no, unsigned tex)
{
        struct pandecode_mapped_memory *mapped_mem = pandecode_find_mapped_gpu_mem_containing(u);
        const uint8_t *cl = pandecode_fetch_gpu_mem(mapped_mem, u, pan_size(MIDGARD_TEXTURE));

        pan_unpack(cl, MIDGARD_TEXTURE, temp);
        DUMP_UNPACKED(MIDGARD_TEXTURE, temp, "Texture:\n")

        pandecode_indent++;
        unsigned nr_samples = temp.dimension == MALI_TEXTURE_DIMENSION_3D ?
                              1 : temp.sample_count;
        pandecode_texture_payload(u + pan_size(MIDGARD_TEXTURE),
                        temp.dimension, temp.texel_ordering, temp.manual_stride,
                        temp.levels, nr_samples, temp.array_size, mapped_mem);
        pandecode_indent--;
}

static void
pandecode_bifrost_texture(
                const void *cl,
                unsigned job_no,
                unsigned tex)
{
        pan_unpack(cl, BIFROST_TEXTURE, temp);
        DUMP_UNPACKED(BIFROST_TEXTURE, temp, "Texture:\n")

        struct pandecode_mapped_memory *tmem = pandecode_find_mapped_gpu_mem_containing(temp.surfaces);
        unsigned nr_samples = temp.dimension == MALI_TEXTURE_DIMENSION_3D ?
                              1 : temp.sample_count;
        pandecode_indent++;
        pandecode_texture_payload(temp.surfaces, temp.dimension, temp.texel_ordering,
                                  true, temp.levels, nr_samples, temp.array_size, tmem);
        pandecode_indent--;
}

static void
pandecode_blend_shader_disassemble(mali_ptr shader, int job_no, int job_type,
                                   bool is_bifrost, unsigned gpu_id)
{
        struct midgard_disasm_stats stats =
                pandecode_shader_disassemble(shader, job_no, job_type, is_bifrost, gpu_id);

        bool has_texture = (stats.texture_count > 0);
        bool has_sampler = (stats.sampler_count > 0);
        bool has_attribute = (stats.attribute_count > 0);
        bool has_varying = (stats.varying_count > 0);
        bool has_uniform = (stats.uniform_count > 0);
        bool has_ubo = (stats.uniform_buffer_count > 0);

        if (has_texture || has_sampler)
                pandecode_msg("XXX: blend shader accessing textures\n");

        if (has_attribute || has_varying)
                pandecode_msg("XXX: blend shader accessing interstage\n");

        if (has_uniform || has_ubo)
                pandecode_msg("XXX: blend shader accessing uniforms\n");
}

static void
pandecode_textures(mali_ptr textures, unsigned texture_count, int job_no, bool is_bifrost)
{
        struct pandecode_mapped_memory *mmem = pandecode_find_mapped_gpu_mem_containing(textures);

        if (!mmem)
                return;

        pandecode_log("Textures %"PRIx64"_%d:\n", textures, job_no);
        pandecode_indent++;

        if (is_bifrost) {
                const void *cl = pandecode_fetch_gpu_mem(mmem,
                                textures, pan_size(BIFROST_TEXTURE) *
                                texture_count);

                for (unsigned tex = 0; tex < texture_count; ++tex) {
                        pandecode_bifrost_texture(cl +
                                        pan_size(BIFROST_TEXTURE) * tex,
                                        job_no, tex);
                }
        } else {
                mali_ptr *PANDECODE_PTR_VAR(u, mmem, textures);

                for (int tex = 0; tex < texture_count; ++tex) {
                        mali_ptr *PANDECODE_PTR_VAR(u, mmem, textures + tex * sizeof(mali_ptr));
                        char *a = pointer_as_memory_reference(*u);
                        pandecode_log("%s,\n", a);
                        free(a);
                }

                /* Now, finally, descend down into the texture descriptor */
                for (unsigned tex = 0; tex < texture_count; ++tex) {
                        mali_ptr *PANDECODE_PTR_VAR(u, mmem, textures + tex * sizeof(mali_ptr));
                        struct pandecode_mapped_memory *tmem = pandecode_find_mapped_gpu_mem_containing(*u);
                        if (tmem)
                                pandecode_texture(*u, tmem, job_no, tex);
                }
        }
        pandecode_indent--;
        pandecode_log("\n");
}

static void
pandecode_samplers(mali_ptr samplers, unsigned sampler_count, int job_no, bool is_bifrost)
{
        pandecode_log("Samplers %"PRIx64"_%d:\n", samplers, job_no);
        pandecode_indent++;

        for (int i = 0; i < sampler_count; ++i) {
                if (is_bifrost) {
                        DUMP_ADDR(BIFROST_SAMPLER, samplers + (pan_size(BIFROST_SAMPLER) * i), "Sampler %d:\n", i);
                } else {
                        DUMP_ADDR(MIDGARD_SAMPLER, samplers + (pan_size(MIDGARD_SAMPLER) * i), "Sampler %d:\n", i);
                }
        }

        pandecode_indent--;
        pandecode_log("\n");
}

static void
pandecode_dcd(const struct MALI_DRAW *p,
              int job_no, enum mali_job_type job_type,
              char *suffix, bool is_bifrost, unsigned gpu_id)
{
        struct pandecode_mapped_memory *attr_mem;

        bool idvs = (job_type == MALI_JOB_TYPE_INDEXED_VERTEX);

        struct pandecode_fbd fbd_info = {
                /* Default for Bifrost */
                .rt_count = 1
        };

        if ((job_type != MALI_JOB_TYPE_TILER) || is_bifrost)
                pandecode_local_storage(p->fbd & ~1, job_no);
        else if (p->fbd & MALI_FBD_TAG_IS_MFBD)
                fbd_info = pandecode_mfbd_bfr((u64) ((uintptr_t) p->fbd) & ~MALI_FBD_TAG_MASK,
                                              job_no, false, false, gpu_id);
        else
                fbd_info = pandecode_sfbd((u64) (uintptr_t) p->fbd, job_no, false, gpu_id);

        int varying_count = 0, attribute_count = 0, uniform_count = 0, uniform_buffer_count = 0;
        int texture_count = 0, sampler_count = 0;

        if (p->state) {
                struct pandecode_mapped_memory *smem = pandecode_find_mapped_gpu_mem_containing(p->state);
                uint32_t *cl = pandecode_fetch_gpu_mem(smem, p->state, pan_size(RENDERER_STATE));

                pan_unpack(cl, RENDERER_STATE, state);

                if (state.shader.shader & ~0xF)
                        pandecode_shader_disassemble(state.shader.shader & ~0xF, job_no, job_type, is_bifrost, gpu_id);

                if (idvs && state.secondary_shader)
                        pandecode_shader_disassemble(state.secondary_shader, job_no, job_type, is_bifrost, gpu_id);
                DUMP_UNPACKED(RENDERER_STATE, state, "State:\n");
                pandecode_indent++;

                /* Save for dumps */
                attribute_count = state.shader.attribute_count;
                varying_count = state.shader.varying_count;
                texture_count = state.shader.texture_count;
                sampler_count = state.shader.sampler_count;
                uniform_buffer_count = state.properties.uniform_buffer_count;

                if (is_bifrost)
                        uniform_count = state.preload.uniform_count;
                else
                        uniform_count = state.properties.midgard.uniform_count;

                if (is_bifrost)
                        DUMP_UNPACKED(PRELOAD, state.preload, "Preload:\n");

                if (!is_bifrost) {
                        mali_ptr shader = state.sfbd_blend_shader & ~0xF;
                        if (state.multisample_misc.sfbd_blend_shader && shader)
                                pandecode_blend_shader_disassemble(shader, job_no, job_type, false, gpu_id);
                }
                pandecode_indent--;
                pandecode_log("\n");

                /* MRT blend fields are used whenever MFBD is used, with
                 * per-RT descriptors */

                if ((job_type == MALI_JOB_TYPE_TILER || job_type == MALI_JOB_TYPE_FRAGMENT) &&
                    (is_bifrost || p->fbd & MALI_FBD_TAG_IS_MFBD)) {
                        void* blend_base = ((void *) cl) + pan_size(RENDERER_STATE);

                        for (unsigned i = 0; i < fbd_info.rt_count; i++) {
                                mali_ptr shader = 0;

                                if (is_bifrost)
                                        shader = pandecode_bifrost_blend(blend_base, job_no, i,
                                                                         state.shader.shader);
				else
                                        shader = pandecode_midgard_blend_mrt(blend_base, job_no, i);

                                if (shader & ~0xF)
                                        pandecode_blend_shader_disassemble(shader, job_no, job_type,
                                                                           is_bifrost, gpu_id);
                        }
                }
        } else
                pandecode_msg("XXX: missing shader descriptor\n");

        if (p->viewport) {
                DUMP_ADDR(VIEWPORT, p->viewport, "Viewport:\n");
                pandecode_log("\n");
        }

        unsigned max_attr_index = 0;

        if (p->attributes)
                max_attr_index = pandecode_attribute_meta(attribute_count, p->attributes, false);

        if (p->attribute_buffers) {
                attr_mem = pandecode_find_mapped_gpu_mem_containing(p->attribute_buffers);
                pandecode_attributes(attr_mem, p->attribute_buffers, job_no, suffix, max_attr_index, false, job_type);
        }

        if (p->varyings) {
                varying_count = pandecode_attribute_meta(varying_count, p->varyings, true);
        }

        if (p->varying_buffers) {
                attr_mem = pandecode_find_mapped_gpu_mem_containing(p->varying_buffers);
                pandecode_attributes(attr_mem, p->varying_buffers, job_no, suffix, varying_count, true, job_type);
        }

        if (p->uniform_buffers) {
                if (uniform_buffer_count)
                        pandecode_uniform_buffers(p->uniform_buffers, uniform_buffer_count, job_no);
                else
                        pandecode_msg("warn: UBOs specified but not referenced\n");
        } else if (uniform_buffer_count)
                pandecode_msg("XXX: UBOs referenced but not specified\n");

        /* We don't want to actually dump uniforms, but we do need to validate
         * that the counts we were given are sane */

        if (p->push_uniforms) {
                if (uniform_count)
                        pandecode_uniforms(p->push_uniforms, uniform_count);
                else
                        pandecode_msg("warn: Uniforms specified but not referenced\n");
        } else if (uniform_count)
                pandecode_msg("XXX: Uniforms referenced but not specified\n");

        if (p->textures)
                pandecode_textures(p->textures, texture_count, job_no, is_bifrost);

        if (p->samplers)
                pandecode_samplers(p->samplers, sampler_count, job_no, is_bifrost);
}

static void
pandecode_bifrost_tiler_heap(mali_ptr gpu_va, int job_no)
{
        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing(gpu_va);
        pan_unpack(PANDECODE_PTR(mem, gpu_va, void), BIFROST_TILER_HEAP, h);
        DUMP_UNPACKED(BIFROST_TILER_HEAP, h, "Bifrost Tiler Heap:\n");
}

static void
pandecode_bifrost_tiler(mali_ptr gpu_va, int job_no)
{
        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing(gpu_va);
        pan_unpack(PANDECODE_PTR(mem, gpu_va, void), BIFROST_TILER, t);

        pandecode_bifrost_tiler_heap(t.heap, job_no);

        DUMP_UNPACKED(BIFROST_TILER, t, "Bifrost Tiler:\n");
        pandecode_indent++;
        if (t.hierarchy_mask != 0xa &&
            t.hierarchy_mask != 0x14 &&
            t.hierarchy_mask != 0x28 &&
            t.hierarchy_mask != 0x50 &&
            t.hierarchy_mask != 0xa0)
                pandecode_msg("XXX: Unexpected hierarchy_mask (not 0xa, 0x14, 0x28, 0x50 or 0xa0)!");

        pandecode_indent--;
}

static void
pandecode_primitive_size(const void *s, bool constant)
{
        pan_unpack(s, PRIMITIVE_SIZE, ps);
        if (ps.size_array == 0x0)
                return;

        DUMP_UNPACKED(PRIMITIVE_SIZE, ps, "Primitive Size:\n")
}

static void
pandecode_vertex_compute_geometry_job(const struct MALI_JOB_HEADER *h,
                                      const struct pandecode_mapped_memory *mem,
                                      mali_ptr job, int job_no, bool is_bifrost,
                                      unsigned gpu_id)
{
        struct mali_compute_job_packed *PANDECODE_PTR_VAR(p, mem, job);
        pan_section_unpack(p, COMPUTE_JOB, DRAW, draw);
        pandecode_dcd(&draw, job_no, h->type, "", is_bifrost, gpu_id);

        pandecode_log("Vertex Job Payload:\n");
        pandecode_indent++;
        pandecode_invocation(pan_section_ptr(p, COMPUTE_JOB, INVOCATION));
        DUMP_SECTION(COMPUTE_JOB, PARAMETERS, p, "Vertex Job Parameters:\n");
        DUMP_UNPACKED(DRAW, draw, "Draw:\n");
        pandecode_indent--;
        pandecode_log("\n");
}

static void
pandecode_indexed_vertex_job(const struct MALI_JOB_HEADER *h,
                             const struct pandecode_mapped_memory *mem,
                             mali_ptr job, int job_no, bool is_bifrost,
                             unsigned gpu_id)
{
        struct mali_bifrost_indexed_vertex_job_packed *PANDECODE_PTR_VAR(p, mem, job);

        pandecode_log("Vertex:\n");
        pan_section_unpack(p, BIFROST_INDEXED_VERTEX_JOB, VERTEX_DRAW, vert_draw);
        pandecode_dcd(&vert_draw, job_no, h->type, "", is_bifrost, gpu_id);
        DUMP_UNPACKED(DRAW, vert_draw, "Vertex Draw:\n");

        pandecode_log("Fragment:\n");
        pan_section_unpack(p, BIFROST_INDEXED_VERTEX_JOB, FRAGMENT_DRAW, frag_draw);
        pandecode_dcd(&frag_draw, job_no, MALI_JOB_TYPE_FRAGMENT, "", is_bifrost, gpu_id);
        DUMP_UNPACKED(DRAW, frag_draw, "Fragment Draw:\n");

        pan_section_unpack(p, BIFROST_INDEXED_VERTEX_JOB, TILER, tiler_ptr);
        pandecode_log("Tiler Job Payload:\n");
        pandecode_indent++;
        pandecode_bifrost_tiler(tiler_ptr.address, job_no);
        pandecode_indent--;

        pandecode_invocation(pan_section_ptr(p, BIFROST_INDEXED_VERTEX_JOB, INVOCATION));
        pandecode_primitive(pan_section_ptr(p, BIFROST_INDEXED_VERTEX_JOB, PRIMITIVE));

        /* TODO: gl_PointSize on Bifrost */
        pandecode_primitive_size(pan_section_ptr(p, BIFROST_INDEXED_VERTEX_JOB, PRIMITIVE_SIZE), true);

        pan_section_unpack(p, BIFROST_INDEXED_VERTEX_JOB, PADDING, padding);
        pan_section_unpack(p, BIFROST_INDEXED_VERTEX_JOB, FRAGMENT_DRAW_PADDING, f_padding);
        pan_section_unpack(p, BIFROST_INDEXED_VERTEX_JOB, VERTEX_DRAW_PADDING, v_padding);
}

static void
pandecode_tiler_job_bfr(const struct MALI_JOB_HEADER *h,
                        const struct pandecode_mapped_memory *mem,
                        mali_ptr job, int job_no, unsigned gpu_id)
{
        struct mali_bifrost_tiler_job_packed *PANDECODE_PTR_VAR(p, mem, job);
        pan_section_unpack(p, BIFROST_TILER_JOB, DRAW, draw);
        pan_section_unpack(p, BIFROST_TILER_JOB, TILER, tiler_ptr);
        pandecode_dcd(&draw, job_no, h->type, "", true, gpu_id);

        pandecode_log("Tiler Job Payload:\n");
        pandecode_indent++;
        pandecode_bifrost_tiler(tiler_ptr.address, job_no);

        pandecode_invocation(pan_section_ptr(p, BIFROST_TILER_JOB, INVOCATION));
        pandecode_primitive(pan_section_ptr(p, BIFROST_TILER_JOB, PRIMITIVE));

        /* TODO: gl_PointSize on Bifrost */
        pandecode_primitive_size(pan_section_ptr(p, BIFROST_TILER_JOB, PRIMITIVE_SIZE), true);
        pan_section_unpack(p, BIFROST_TILER_JOB, PADDING, padding);
        DUMP_UNPACKED(DRAW, draw, "Draw:\n");
        pandecode_indent--;
        pandecode_log("\n");
}

static void
pandecode_tiler_job_mdg(const struct MALI_JOB_HEADER *h,
                        const struct pandecode_mapped_memory *mem,
                        mali_ptr job, int job_no, unsigned gpu_id)
{
        struct mali_midgard_tiler_job_packed *PANDECODE_PTR_VAR(p, mem, job);
        pan_section_unpack(p, MIDGARD_TILER_JOB, DRAW, draw);
        pandecode_dcd(&draw, job_no, h->type, "", false, gpu_id);

        pandecode_log("Tiler Job Payload:\n");
        pandecode_indent++;
        pandecode_invocation(pan_section_ptr(p, MIDGARD_TILER_JOB, INVOCATION));
        pandecode_primitive(pan_section_ptr(p, MIDGARD_TILER_JOB, PRIMITIVE));
        DUMP_UNPACKED(DRAW, draw, "Draw:\n");

        pan_section_unpack(p, MIDGARD_TILER_JOB, PRIMITIVE, primitive);
        pandecode_primitive_size(pan_section_ptr(p, MIDGARD_TILER_JOB, PRIMITIVE_SIZE),
                                 primitive.point_size_array_format == MALI_POINT_SIZE_ARRAY_FORMAT_NONE);
        pandecode_indent--;
        pandecode_log("\n");
}

static void
pandecode_fragment_job(const struct pandecode_mapped_memory *mem,
                       mali_ptr job, int job_no,
                       bool is_bifrost, unsigned gpu_id)
{
        struct mali_fragment_job_packed *PANDECODE_PTR_VAR(p, mem, job);
        pan_section_unpack(p, FRAGMENT_JOB, PAYLOAD, s);

        bool is_mfbd = s.framebuffer & MALI_FBD_TAG_IS_MFBD;

        if (!is_mfbd && is_bifrost)
                pandecode_msg("XXX: Bifrost fragment must use MFBD\n");

        struct pandecode_fbd info;

        if (is_mfbd)
                info = pandecode_mfbd_bfr(s.framebuffer & ~MALI_FBD_TAG_MASK, job_no,
                                          true, is_bifrost, gpu_id);
        else
                info = pandecode_sfbd(s.framebuffer & ~MALI_FBD_TAG_MASK, job_no,
                                      true, gpu_id);

        /* Compute the tag for the tagged pointer. This contains the type of
         * FBD (MFBD/SFBD), and in the case of an MFBD, information about which
         * additional structures follow the MFBD header (an extra payload or
         * not, as well as a count of render targets) */

        unsigned expected_tag = is_mfbd ? MALI_FBD_TAG_IS_MFBD : 0;

        if (is_mfbd) {
                if (info.has_extra)
                        expected_tag |= MALI_FBD_TAG_HAS_ZS_RT;

                expected_tag |= (MALI_POSITIVE(info.rt_count) << 2);
        }

        DUMP_UNPACKED(FRAGMENT_JOB_PAYLOAD, s, "Fragment Job Payload:\n");

        /* The FBD is a tagged pointer */

        unsigned tag = (s.framebuffer & MALI_FBD_TAG_MASK);

        if (tag != expected_tag)
                pandecode_msg("XXX: expected FBD tag %X but got %X\n", expected_tag, tag);

        pandecode_log("\n");
}

static void
pandecode_write_value_job(const struct pandecode_mapped_memory *mem,
                          mali_ptr job, int job_no)
{
        struct mali_write_value_job_packed *PANDECODE_PTR_VAR(p, mem, job);
        pan_section_unpack(p, WRITE_VALUE_JOB, PAYLOAD, u);
        DUMP_SECTION(WRITE_VALUE_JOB, PAYLOAD, p, "Write Value Payload:\n");
        pandecode_log("\n");
}

static void
pandecode_cache_flush_job(const struct pandecode_mapped_memory *mem,
                          mali_ptr job, int job_no)
{
        struct mali_cache_flush_job_packed *PANDECODE_PTR_VAR(p, mem, job);
        pan_section_unpack(p, CACHE_FLUSH_JOB, PAYLOAD, u);
        DUMP_SECTION(CACHE_FLUSH_JOB, PAYLOAD, p, "Cache Flush Payload:\n");
        pandecode_log("\n");
}

/* Entrypoint to start tracing. jc_gpu_va is the GPU address for the first job
 * in the chain; later jobs are found by walking the chain. Bifrost is, well,
 * if it's bifrost or not. GPU ID is the more finegrained ID (at some point, we
 * might wish to combine this with the bifrost parameter) because some details
 * are model-specific even within a particular architecture. */

void
pandecode_jc(mali_ptr jc_gpu_va, bool bifrost, unsigned gpu_id)
{
        pandecode_dump_file_open();

        unsigned job_descriptor_number = 0;
        mali_ptr next_job = 0;

        do {
                struct pandecode_mapped_memory *mem =
                        pandecode_find_mapped_gpu_mem_containing(jc_gpu_va);

                pan_unpack(PANDECODE_PTR(mem, jc_gpu_va, struct mali_job_header_packed),
                           JOB_HEADER, h);
                next_job = h.next;

                int job_no = job_descriptor_number++;

                DUMP_UNPACKED(JOB_HEADER, h, "Job Header:\n");
                pandecode_log("\n");

                switch (h.type) {
                case MALI_JOB_TYPE_WRITE_VALUE:
                        pandecode_write_value_job(mem, jc_gpu_va, job_no);
                        break;

                case MALI_JOB_TYPE_CACHE_FLUSH:
                        pandecode_cache_flush_job(mem, jc_gpu_va, job_no);
                        break;

                case MALI_JOB_TYPE_TILER:
                        if (bifrost)
                                pandecode_tiler_job_bfr(&h, mem, jc_gpu_va, job_no, gpu_id);
                        else
                                pandecode_tiler_job_mdg(&h, mem, jc_gpu_va, job_no, gpu_id);
                        break;

                case MALI_JOB_TYPE_VERTEX:
                case MALI_JOB_TYPE_COMPUTE:
                        pandecode_vertex_compute_geometry_job(&h, mem, jc_gpu_va, job_no,
                                                              bifrost, gpu_id);
                        break;

                case MALI_JOB_TYPE_INDEXED_VERTEX:
                        pandecode_indexed_vertex_job(&h, mem, jc_gpu_va, job_no,
                                                              bifrost, gpu_id);
                        break;

                case MALI_JOB_TYPE_FRAGMENT:
                        pandecode_fragment_job(mem, jc_gpu_va, job_no, bifrost, gpu_id);
                        break;

                default:
                        break;
                }
        } while ((jc_gpu_va = next_job));

        fflush(pandecode_dump_stream);
        pandecode_map_read_write();
}

void
pandecode_abort_on_fault(mali_ptr jc_gpu_va)
{
        mali_ptr next_job = 0;

        do {
                struct pandecode_mapped_memory *mem =
                        pandecode_find_mapped_gpu_mem_containing(jc_gpu_va);

                pan_unpack(PANDECODE_PTR(mem, jc_gpu_va, struct mali_job_header_packed),
                           JOB_HEADER, h);
                next_job = h.next;

                /* Ensure the job is marked COMPLETE */
                if (h.exception_status != 0x1) {
                        fprintf(stderr, "Incomplete job or timeout");
                        abort();
                }
        } while ((jc_gpu_va = next_job));

        pandecode_map_read_write();
}
