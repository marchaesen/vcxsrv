/*
 * Copyright (C) 2021 Collabora, Ltd.
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

#include <stdio.h>
#include "pan_bo.h"
#include "pan_shader.h"
#include "pan_scoreboard.h"
#include "pan_encoder.h"
#include "pan_indirect_draw.h"
#include "pan_pool.h"
#include "pan_util.h"
#include "compiler/nir/nir_builder.h"
#include "util/u_memory.h"
#include "util/macros.h"

#define WORD(x) ((x) * 4)

#define LOOP \
        for (nir_loop *l = nir_push_loop(b); l != NULL; \
             nir_pop_loop(b, l), l = NULL)
#define BREAK nir_jump(b, nir_jump_break)
#define CONTINUE nir_jump(b, nir_jump_continue)

#define IF(cond) nir_push_if(b, cond);
#define ELSE nir_push_else(b, NULL);
#define ENDIF nir_pop_if(b, NULL);

#define MIN_MAX_JOBS 128

struct draw_data {
        nir_ssa_def *draw_buf;
        nir_ssa_def *draw_buf_stride;
        nir_ssa_def *index_buf;
        nir_ssa_def *restart_index;
        nir_ssa_def *vertex_count;
        nir_ssa_def *start_instance;
        nir_ssa_def *instance_count;
        nir_ssa_def *vertex_start;
        nir_ssa_def *index_bias;
        nir_ssa_def *draw_ctx;
        nir_ssa_def *min_max_ctx;
};

struct instance_size {
        nir_ssa_def *raw;
        nir_ssa_def *padded;
        nir_ssa_def *packed;
};

struct jobs_data {
        nir_ssa_def *vertex_job;
        nir_ssa_def *tiler_job;
        nir_ssa_def *base_vertex_offset;
        nir_ssa_def *first_vertex_sysval;
        nir_ssa_def *base_vertex_sysval;
        nir_ssa_def *base_instance_sysval;
        nir_ssa_def *offset_start;
        nir_ssa_def *invocation;
};

struct varyings_data {
        nir_ssa_def *varying_bufs;
        nir_ssa_def *pos_ptr;
        nir_ssa_def *psiz_ptr;
        nir_variable *mem_ptr;
};

struct attribs_data {
        nir_ssa_def *attrib_count;
        nir_ssa_def *attrib_bufs;
        nir_ssa_def *attribs;
};

struct indirect_draw_shader_builder {
        nir_builder b;
        const struct panfrost_device *dev;
        unsigned flags;
        bool index_min_max_search;
        unsigned index_size;
        struct draw_data draw;
        struct instance_size instance_size;
        struct jobs_data jobs;
        struct varyings_data varyings;
        struct attribs_data attribs;
};

/* Describes an indirect draw (see glDrawArraysIndirect()) */

struct indirect_draw_info {
        uint32_t count;
        uint32_t instance_count;
        uint32_t start;
        uint32_t start_instance;
};

struct indirect_indexed_draw_info {
        uint32_t count;
        uint32_t instance_count;
        uint32_t start;
        int32_t index_bias;
        uint32_t start_instance;
};

/* Store the min/max index in a separate context. This is not supported yet, but
 * the DDK seems to put all min/max search jobs at the beginning of the job chain
 * when multiple indirect draws are issued to avoid the serialization caused by
 * the draw patching jobs which have the suppress_prefetch flag set. Merging the
 * min/max and draw contexts would prevent such optimizations (draw contexts are
 * shared by all indirect draw in a batch).
 */

struct min_max_context {
        uint32_t min;
        uint32_t max;
};

/* Per-batch context shared by all indirect draws queued to a given batch. */

struct indirect_draw_context {
        /* Pointer to the top of the varying heap. */
        mali_ptr varying_mem;
};

/* Indirect draw shader inputs. Those are stored in a UBO. */

struct indirect_draw_inputs {
        /* indirect_draw_context pointer */
        mali_ptr draw_ctx;

        /* min_max_context pointer */
        mali_ptr min_max_ctx;

        /* Pointer to an array of indirect_draw_info objects */
        mali_ptr draw_buf;

        /* Pointer to an uint32_t containing the number of draws to issue */
        mali_ptr draw_count_ptr;

        /* index buffer */
        mali_ptr index_buf;

        /* {base,first}_{vertex,instance} sysvals */
        mali_ptr first_vertex_sysval;
        mali_ptr base_vertex_sysval;
        mali_ptr base_instance_sysval;

        /* Pointers to various cmdstream structs that need to be patched */
        mali_ptr vertex_job;
        mali_ptr tiler_job;
        mali_ptr attrib_bufs;
        mali_ptr attribs;
        mali_ptr varying_bufs;
        uint32_t draw_count;
        uint32_t draw_buf_stride;
        uint32_t restart_index;
        uint32_t attrib_count;
};

static nir_ssa_def *
get_input_data(nir_builder *b, unsigned offset, unsigned size)
{
        assert(!(offset & 0x3));
        assert(size && !(size & 0x3));

        return nir_load_ubo(b, 1, size,
                            nir_imm_int(b, 0),
                            nir_imm_int(b, offset),
                            .align_mul = 4,
                            .align_offset = 0,
                            .range_base = 0,
                            .range = ~0);
}

#define get_input_field(b, name) \
        get_input_data(b, offsetof(struct indirect_draw_inputs, name), \
                       sizeof(((struct indirect_draw_inputs *)0)->name) * 8)

static nir_ssa_def *
get_address(nir_builder *b, nir_ssa_def *base, nir_ssa_def *offset)
{
        return nir_iadd(b, base, nir_u2u64(b, offset));
}

static nir_ssa_def *
get_address_imm(nir_builder *b, nir_ssa_def *base, unsigned offset)
{
        return get_address(b, base, nir_imm_int(b, offset));
}

static nir_ssa_def *
load_global(nir_builder *b, nir_ssa_def *addr, unsigned ncomps, unsigned bit_size)
{
        return nir_load_global(b, addr, 4, ncomps, bit_size);
}

static void
store_global(nir_builder *b, nir_ssa_def *addr,
             nir_ssa_def *value, unsigned ncomps)
{
        nir_store_global(b, addr, 4, value, (1 << ncomps) - 1);
}

static nir_ssa_def *
get_draw_ctx_data(struct indirect_draw_shader_builder *builder,
                  unsigned offset, unsigned size)
{
        nir_builder *b = &builder->b;
        return load_global(b,
                           get_address_imm(b, builder->draw.draw_ctx, offset),
                           1, size);
}

static void
set_draw_ctx_data(struct indirect_draw_shader_builder *builder,
                  unsigned offset, nir_ssa_def *value, unsigned size)
{
        nir_builder *b = &builder->b;
        store_global(b,
                     get_address_imm(b, builder->draw.draw_ctx, offset),
                     value, 1);
}

#define get_draw_ctx_field(builder, name) \
        get_draw_ctx_data(builder, \
                          offsetof(struct indirect_draw_context, name), \
                          sizeof(((struct indirect_draw_context *)0)->name) * 8)

#define set_draw_ctx_field(builder, name, val) \
        set_draw_ctx_data(builder, \
                          offsetof(struct indirect_draw_context, name), \
                          val, \
                          sizeof(((struct indirect_draw_context *)0)->name) * 8)

static nir_ssa_def *
get_min_max_ctx_data(struct indirect_draw_shader_builder *builder,
                     unsigned offset, unsigned size)
{
        nir_builder *b = &builder->b;
        return load_global(b,
                           get_address_imm(b, builder->draw.min_max_ctx, offset),
                           1, size);
}

#define get_min_max_ctx_field(builder, name) \
        get_min_max_ctx_data(builder, \
                             offsetof(struct min_max_context, name), \
                             sizeof(((struct min_max_context *)0)->name) * 8)

static void
update_min(struct indirect_draw_shader_builder *builder, nir_ssa_def *val)
{
        nir_builder *b = &builder->b;
        nir_ssa_def *addr =
                get_address_imm(b,
                                builder->draw.min_max_ctx,
                                offsetof(struct min_max_context, min));
        nir_global_atomic_umin(b, 32, addr, val);
}

static void
update_max(struct indirect_draw_shader_builder *builder, nir_ssa_def *val)
{
        nir_builder *b = &builder->b;
        nir_ssa_def *addr =
                get_address_imm(b,
                                builder->draw.min_max_ctx,
                                offsetof(struct min_max_context, max));
        nir_global_atomic_umax(b, 32, addr, val);
}

#define get_draw_field(b, draw_ptr, field) \
        load_global(b, \
                    get_address_imm(b, draw_ptr, \
                                    offsetof(struct indirect_draw_info, field)), \
                    1, sizeof(((struct indirect_draw_info *)0)->field) * 8)

#define get_indexed_draw_field(b, draw_ptr, field) \
        load_global(b, \
                    get_address_imm(b, draw_ptr, \
                                    offsetof(struct indirect_indexed_draw_info, field)), \
                    1, sizeof(((struct indirect_indexed_draw_info *)0)->field) * 8)

static void
extract_inputs(struct indirect_draw_shader_builder *builder)
{
        nir_builder *b = &builder->b;

        builder->draw.draw_ctx = get_input_field(b, draw_ctx);
        builder->draw.draw_buf = get_input_field(b, draw_buf);
        builder->draw.draw_buf_stride = get_input_field(b, draw_buf_stride);

        if (builder->index_size) {
                builder->draw.index_buf = get_input_field(b, index_buf);
                builder->draw.min_max_ctx = get_input_field(b, min_max_ctx);
                if (builder->flags & PAN_INDIRECT_DRAW_PRIMITIVE_RESTART) {
                        builder->draw.restart_index =
                                get_input_field(b, restart_index);
                }
        }

        if (builder->index_min_max_search)
                return;

        builder->jobs.first_vertex_sysval = get_input_field(b, first_vertex_sysval);
        builder->jobs.base_vertex_sysval = get_input_field(b, base_vertex_sysval);
        builder->jobs.base_instance_sysval = get_input_field(b, base_instance_sysval);
        builder->jobs.vertex_job = get_input_field(b, vertex_job);
        builder->jobs.tiler_job = get_input_field(b, tiler_job);
        builder->attribs.attrib_bufs = get_input_field(b, attrib_bufs);
        builder->attribs.attribs = get_input_field(b, attribs);
        builder->attribs.attrib_count = get_input_field(b, attrib_count);
        builder->varyings.varying_bufs = get_input_field(b, varying_bufs);
        builder->varyings.mem_ptr =
                nir_local_variable_create(b->impl,
                                          glsl_uint64_t_type(),
                                          "var_mem_ptr");
        nir_store_var(b, builder->varyings.mem_ptr,
                      get_draw_ctx_field(builder, varying_mem), 3);
}

static void
init_shader_builder(struct indirect_draw_shader_builder *builder,
                    const struct panfrost_device *dev,
                    unsigned flags, unsigned index_size,
                    bool index_min_max_search)
{
        memset(builder, 0, sizeof(*builder));
        builder->dev = dev;
        builder->flags = flags;
        builder->index_size = index_size;

        builder->index_min_max_search = index_min_max_search;

        if (index_min_max_search) {
                builder->b =
                        nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
                                                       GENX(pan_shader_get_compiler_options)(),
                                                       "indirect_draw_min_max_index(index_size=%d)",
                                                       builder->index_size);
        } else {
                builder->b =
                        nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
                                                       GENX(pan_shader_get_compiler_options)(),
                                                       "indirect_draw(index_size=%d%s%s%s%s)",
                                                       builder->index_size,
                                                       flags & PAN_INDIRECT_DRAW_HAS_PSIZ ?
                                                       ",psiz" : "",
                                                       flags & PAN_INDIRECT_DRAW_PRIMITIVE_RESTART ?
                                                       ",primitive_restart" : "",
                                                       flags & PAN_INDIRECT_DRAW_UPDATE_PRIM_SIZE ?
                                                       ",update_primitive_size" : "",
                                                       flags & PAN_INDIRECT_DRAW_IDVS ?
                                                       ",idvs" : "");
        }

        nir_builder *b = &builder->b;
        b->shader->info.internal = true;
        nir_variable_create(b->shader, nir_var_mem_ubo,
                            glsl_uint_type(), "inputs");
        b->shader->info.num_ubos++;

        extract_inputs(builder);
}

static void
update_dcd(struct indirect_draw_shader_builder *builder,
           nir_ssa_def *job_ptr,
           unsigned draw_offset)
{
        nir_builder *b = &builder->b;
        nir_ssa_def *draw_w01 =
                load_global(b, get_address_imm(b, job_ptr, draw_offset + WORD(0)), 2, 32);
        nir_ssa_def *draw_w0 = nir_channel(b, draw_w01, 0);

        /* Update DRAW.{instance_size,offset_start} */
        nir_ssa_def *instance_size =
                nir_bcsel(b,
                          nir_ult(b, builder->draw.instance_count, nir_imm_int(b, 2)),
                          nir_imm_int(b, 0), builder->instance_size.packed);
        draw_w01 = nir_vec2(b,
                            nir_ior(b, nir_iand_imm(b, draw_w0, 0xffff),
                                    nir_ishl(b, instance_size, nir_imm_int(b, 16))),
                            builder->jobs.offset_start);
        store_global(b, get_address_imm(b, job_ptr, draw_offset + WORD(0)),
                     draw_w01, 2);
}

static void
update_job(struct indirect_draw_shader_builder *builder, enum mali_job_type type)
{
        nir_builder *b = &builder->b;
        nir_ssa_def *job_ptr =
                type == MALI_JOB_TYPE_VERTEX ?
                builder->jobs.vertex_job : builder->jobs.tiler_job;

        /* Update the invocation words. */
        store_global(b, get_address_imm(b, job_ptr, WORD(8)),
                     builder->jobs.invocation, 2);

        unsigned draw_offset =
                type == MALI_JOB_TYPE_VERTEX ?
                pan_section_offset(COMPUTE_JOB, DRAW) :
                pan_section_offset(TILER_JOB, DRAW);
        unsigned prim_offset = pan_section_offset(TILER_JOB, PRIMITIVE);
        unsigned psiz_offset = pan_section_offset(TILER_JOB, PRIMITIVE_SIZE);
        unsigned index_size = builder->index_size;

        if (type == MALI_JOB_TYPE_TILER) {
                /* Update PRIMITIVE.{base_vertex_offset,count} */
                store_global(b,
                             get_address_imm(b, job_ptr, prim_offset + WORD(1)),
                             builder->jobs.base_vertex_offset, 1);
                store_global(b,
                             get_address_imm(b, job_ptr, prim_offset + WORD(3)),
                             nir_iadd_imm(b, builder->draw.vertex_count, -1), 1);

                if (index_size) {
                        nir_ssa_def *addr =
                                get_address_imm(b, job_ptr, prim_offset + WORD(4));
                        nir_ssa_def *indices = load_global(b, addr, 1, 64);
                        nir_ssa_def *offset =
                                nir_imul_imm(b, builder->draw.vertex_start, index_size);

                        indices = get_address(b, indices, offset);
                        store_global(b, addr, indices, 2);
                }

                /* Update PRIMITIVE_SIZE.size_array */
                if ((builder->flags & PAN_INDIRECT_DRAW_HAS_PSIZ) &&
                    (builder->flags & PAN_INDIRECT_DRAW_UPDATE_PRIM_SIZE)) {
                        store_global(b,
                                     get_address_imm(b, job_ptr, psiz_offset + WORD(0)),
                                     builder->varyings.psiz_ptr, 2);
                }

                /* Update DRAW.position */
                store_global(b, get_address_imm(b, job_ptr, draw_offset + WORD(4)),
                             builder->varyings.pos_ptr, 2);
        }

        update_dcd(builder, job_ptr, draw_offset);

        if (builder->flags & PAN_INDIRECT_DRAW_IDVS) {
                assert(type == MALI_JOB_TYPE_TILER);

                update_dcd(builder, job_ptr,
                           pan_section_offset(INDEXED_VERTEX_JOB, VERTEX_DRAW));
        }
}

static void
split_div(nir_builder *b, nir_ssa_def *div, nir_ssa_def **r_e, nir_ssa_def **d)
{
        /* TODO: Lower this 64bit div to something GPU-friendly */
        nir_ssa_def *r = nir_imax(b, nir_ufind_msb(b, div), nir_imm_int(b, 0));
        nir_ssa_def *div64 = nir_u2u64(b, div);
        nir_ssa_def *half_div64 = nir_u2u64(b, nir_ushr_imm(b, div, 1));
        nir_ssa_def *f0 = nir_iadd(b,
                                   nir_ishl(b, nir_imm_int64(b, 1),
                                            nir_iadd_imm(b, r, 32)),
                                   half_div64);
        nir_ssa_def *fi = nir_idiv(b, f0, div64);
        nir_ssa_def *ff = nir_isub(b, f0, nir_imul(b, fi, div64));
        nir_ssa_def *e = nir_bcsel(b, nir_ult(b, half_div64, ff),
                                   nir_imm_int(b, 1 << 5), nir_imm_int(b, 0));
        *d = nir_iand_imm(b, nir_u2u32(b, fi), ~(1 << 31));
        *r_e = nir_ior(b, r, e);
}

static void
update_vertex_attrib_buf(struct indirect_draw_shader_builder *builder,
                         nir_ssa_def *attrib_buf_ptr,
                         enum mali_attribute_type type,
                         nir_ssa_def *div1,
                         nir_ssa_def *div2)
{
        nir_builder *b = &builder->b;
        unsigned type_mask = BITFIELD_MASK(6);
        nir_ssa_def *w01 = load_global(b, attrib_buf_ptr, 2, 32);
        nir_ssa_def *w0 = nir_channel(b, w01, 0);
        nir_ssa_def *w1 = nir_channel(b, w01, 1);

        /* Word 0 and 1 of the attribute descriptor contain the type,
         * pointer and the the divisor exponent.
         */
        w0 = nir_iand_imm(b, nir_channel(b, w01, 0), ~type_mask);
        w0 = nir_ior(b, w0, nir_imm_int(b, type));
        w1 = nir_ior(b, w1, nir_ishl(b, div1, nir_imm_int(b, 24)));

        store_global(b, attrib_buf_ptr, nir_vec2(b, w0, w1), 2);

        if (type == MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR) {
                /* If the divisor is not a power of two, the divisor numerator
                 * is passed in word 1 of the continuation attribute (word 5
                 * if we consider the attribute and its continuation as a
                 * single attribute).
                 */
                assert(div2);
                store_global(b, get_address_imm(b, attrib_buf_ptr, WORD(5)),
                             div2, 1);
        }
}

static void
zero_attrib_buf_stride(struct indirect_draw_shader_builder *builder,
                       nir_ssa_def *attrib_buf_ptr)
{
        /* Stride is an unadorned 32-bit uint at word 2 */
        nir_builder *b = &builder->b;
        store_global(b, get_address_imm(b, attrib_buf_ptr, WORD(2)),
                        nir_imm_int(b, 0), 1);
}

static void
adjust_attrib_offset(struct indirect_draw_shader_builder *builder,
                     nir_ssa_def *attrib_ptr, nir_ssa_def *attrib_buf_ptr,
                     nir_ssa_def *instance_div)
{
        nir_builder *b = &builder->b;
        nir_ssa_def *zero = nir_imm_int(b, 0);
        nir_ssa_def *two = nir_imm_int(b, 2);
        nir_ssa_def *sub_cur_offset =
                nir_iand(b, nir_ine(b, builder->jobs.offset_start, zero),
                         nir_uge(b, builder->draw.instance_count, two));

        nir_ssa_def *add_base_inst_offset =
                nir_iand(b, nir_ine(b, builder->draw.start_instance, zero),
                         nir_ine(b, instance_div, zero));

        IF (nir_ior(b, sub_cur_offset, add_base_inst_offset)) {
                nir_ssa_def *offset =
                        load_global(b, get_address_imm(b, attrib_ptr, WORD(1)), 1, 32);
                nir_ssa_def *stride =
                        load_global(b, get_address_imm(b, attrib_buf_ptr, WORD(2)), 1, 32);

                /* Per-instance data needs to be offset in response to a
                 * delayed start in an indexed draw.
                 */

                IF (add_base_inst_offset) {
                        offset = nir_iadd(b, offset,
                                          nir_idiv(b,
                                                   nir_imul(b, stride,
                                                            builder->draw.start_instance),
                                                   instance_div));
                } ENDIF

                IF (sub_cur_offset) {
                        offset = nir_isub(b, offset,
                                          nir_imul(b, stride,
                                                   builder->jobs.offset_start));
                } ENDIF

                store_global(b, get_address_imm(b, attrib_ptr, WORD(1)),
                             offset, 1);
        } ENDIF
}

/* x is power of two or zero <===> x has 0 (zero) or 1 (POT) bits set */

static nir_ssa_def *
nir_is_power_of_two_or_zero(nir_builder *b, nir_ssa_def *x)
{
        return nir_ult(b, nir_bit_count(b, x), nir_imm_int(b, 2));
}

/* Based on panfrost_emit_vertex_data() */

static void
update_vertex_attribs(struct indirect_draw_shader_builder *builder)
{
        nir_builder *b = &builder->b;
        nir_variable *attrib_idx_var =
                nir_local_variable_create(b->impl, glsl_uint_type(),
                                          "attrib_idx");
        nir_store_var(b, attrib_idx_var, nir_imm_int(b, 0), 1);

#if PAN_ARCH <= 5
        nir_ssa_def *single_instance =
                nir_ult(b, builder->draw.instance_count, nir_imm_int(b, 2));
#endif

        LOOP {
                nir_ssa_def *attrib_idx = nir_load_var(b, attrib_idx_var);
                IF (nir_uge(b, attrib_idx, builder->attribs.attrib_count))
                        BREAK;
                ENDIF

                nir_ssa_def *attrib_buf_ptr =
                         get_address(b, builder->attribs.attrib_bufs,
                                     nir_imul_imm(b, attrib_idx,
                                                  2 * pan_size(ATTRIBUTE_BUFFER)));
                nir_ssa_def *attrib_ptr =
                         get_address(b, builder->attribs.attribs,
                                     nir_imul_imm(b, attrib_idx,
                                                  pan_size(ATTRIBUTE)));

                nir_ssa_def *r_e, *d;

#if PAN_ARCH <= 5
                IF (nir_ieq_imm(b, attrib_idx, PAN_VERTEX_ID)) {
                        nir_ssa_def *r_p =
                                nir_bcsel(b, single_instance,
                                          nir_imm_int(b, 0x9f),
                                          builder->instance_size.packed);

                        store_global(b,
                                     get_address_imm(b, attrib_buf_ptr, WORD(4)),
                                     nir_ishl(b, r_p, nir_imm_int(b, 24)), 1);

                        nir_store_var(b, attrib_idx_var,
                                      nir_iadd_imm(b, attrib_idx, 1), 1);
                        CONTINUE;
                } ENDIF

                IF (nir_ieq_imm(b, attrib_idx, PAN_INSTANCE_ID)) {
                        split_div(b, builder->instance_size.padded,
                                  &r_e, &d);
                        nir_ssa_def *default_div =
                                nir_ior(b, single_instance,
                                        nir_ult(b,
                                                builder->instance_size.padded,
                                                nir_imm_int(b, 2)));
                        r_e = nir_bcsel(b, default_div,
                                        nir_imm_int(b, 0x3f), r_e);
                        d = nir_bcsel(b, default_div,
                                      nir_imm_int(b, (1u << 31) - 1), d);
                        store_global(b,
                                     get_address_imm(b, attrib_buf_ptr, WORD(1)),
                                     nir_vec2(b, nir_ishl(b, r_e, nir_imm_int(b, 24)), d),
                                     2);
                        nir_store_var(b, attrib_idx_var,
                                      nir_iadd_imm(b, attrib_idx, 1), 1);
                        CONTINUE;
                } ENDIF
#endif

                nir_ssa_def *instance_div =
                        load_global(b, get_address_imm(b, attrib_buf_ptr, WORD(7)), 1, 32);

                nir_ssa_def *div = nir_imul(b, instance_div, builder->instance_size.padded);

                nir_ssa_def *multi_instance =
                        nir_uge(b, builder->draw.instance_count, nir_imm_int(b, 2));

                IF (nir_ine(b, div, nir_imm_int(b, 0))) {
                        IF (multi_instance) {
                                IF (nir_is_power_of_two_or_zero(b, div)) {
                                        nir_ssa_def *exp =
                                                nir_imax(b, nir_ufind_msb(b, div),
                                                         nir_imm_int(b, 0));
                                        update_vertex_attrib_buf(builder, attrib_buf_ptr,
                                                                 MALI_ATTRIBUTE_TYPE_1D_POT_DIVISOR,
                                                                 exp, NULL);
                                } ELSE {
                                        split_div(b, div, &r_e, &d);
                                        update_vertex_attrib_buf(builder, attrib_buf_ptr,
                                                                 MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR,
                                                                 r_e, d);
                                } ENDIF
                        } ELSE {
                                /* Single instance with a non-0 divisor: all
                                 * accesses should point to attribute 0 */
                                zero_attrib_buf_stride(builder, attrib_buf_ptr);
                        } ENDIF

                        adjust_attrib_offset(builder, attrib_ptr, attrib_buf_ptr, instance_div);
                } ELSE IF (multi_instance) {
                        update_vertex_attrib_buf(builder, attrib_buf_ptr,
                                        MALI_ATTRIBUTE_TYPE_1D_MODULUS,
                                        builder->instance_size.packed, NULL);
                } ENDIF ENDIF

                nir_store_var(b, attrib_idx_var, nir_iadd_imm(b, attrib_idx, 1), 1);
        }
}

static nir_ssa_def *
update_varying_buf(struct indirect_draw_shader_builder *builder,
                   nir_ssa_def *varying_buf_ptr,
                   nir_ssa_def *vertex_count)
{
        nir_builder *b = &builder->b;

        nir_ssa_def *stride =
                load_global(b, get_address_imm(b, varying_buf_ptr, WORD(2)), 1, 32);
        nir_ssa_def *size = nir_imul(b, stride, vertex_count);
        nir_ssa_def *aligned_size =
                nir_iand_imm(b, nir_iadd_imm(b, size, 63), ~63);
        nir_ssa_def *var_mem_ptr =
                nir_load_var(b, builder->varyings.mem_ptr);
        nir_ssa_def *w0 =
                nir_ior(b, nir_unpack_64_2x32_split_x(b, var_mem_ptr),
                        nir_imm_int(b, MALI_ATTRIBUTE_TYPE_1D));
        nir_ssa_def *w1 = nir_unpack_64_2x32_split_y(b, var_mem_ptr);
        store_global(b, get_address_imm(b, varying_buf_ptr, WORD(0)),
                     nir_vec4(b, w0, w1, stride, size), 4);

        nir_store_var(b, builder->varyings.mem_ptr,
                      get_address(b, var_mem_ptr, aligned_size), 3);

        return var_mem_ptr;
}

/* Based on panfrost_emit_varying_descriptor() */

static void
update_varyings(struct indirect_draw_shader_builder *builder)
{
        nir_builder *b = &builder->b;
        nir_ssa_def *vertex_count =
                nir_imul(b, builder->instance_size.padded,
                         builder->draw.instance_count);
        nir_ssa_def *buf_ptr =
                get_address_imm(b, builder->varyings.varying_bufs,
                                PAN_VARY_GENERAL *
                                pan_size(ATTRIBUTE_BUFFER));
        update_varying_buf(builder, buf_ptr, vertex_count);

        buf_ptr = get_address_imm(b, builder->varyings.varying_bufs,
                                  PAN_VARY_POSITION *
                                  pan_size(ATTRIBUTE_BUFFER));
        builder->varyings.pos_ptr =
                update_varying_buf(builder, buf_ptr, vertex_count);

        if (builder->flags & PAN_INDIRECT_DRAW_HAS_PSIZ) {
                buf_ptr = get_address_imm(b, builder->varyings.varying_bufs,
                                          PAN_VARY_PSIZ *
                                          pan_size(ATTRIBUTE_BUFFER));
                builder->varyings.psiz_ptr =
                        update_varying_buf(builder, buf_ptr, vertex_count);
        }

        set_draw_ctx_field(builder, varying_mem,
                           nir_load_var(b, builder->varyings.mem_ptr));
}

/* Based on panfrost_pack_work_groups_compute() */

static void
get_invocation(struct indirect_draw_shader_builder *builder)
{
        nir_builder *b = &builder->b;
        nir_ssa_def *one = nir_imm_int(b, 1);
        nir_ssa_def *max_vertex =
                nir_usub_sat(b, builder->instance_size.raw, one);
        nir_ssa_def *max_instance =
                nir_usub_sat(b, builder->draw.instance_count, one);
        nir_ssa_def *split =
                nir_bcsel(b, nir_ieq_imm(b, max_instance, 0),
                          nir_imm_int(b, 32),
                          nir_iadd_imm(b, nir_ufind_msb(b, max_vertex), 1));

        builder->jobs.invocation =
                nir_vec2(b,
                         nir_ior(b, max_vertex,
                                 nir_ishl(b, max_instance, split)),
                         nir_ior(b, nir_ishl(b, split, nir_imm_int(b, 22)),
                                 nir_imm_int(b, 2 << 28)));
}

static nir_ssa_def *
nir_align_pot(nir_builder *b, nir_ssa_def *val, unsigned pot)
{
        assert(pot != 0 && util_is_power_of_two_or_zero(pot));

        return nir_iand_imm(b, nir_iadd_imm(b, val, pot - 1), ~(pot - 1));
}

/* Based on panfrost_padded_vertex_count() */

static nir_ssa_def *
get_padded_count(nir_builder *b, nir_ssa_def *val, nir_ssa_def **packed)
{
        nir_ssa_def *one = nir_imm_int(b, 1);
        nir_ssa_def *zero = nir_imm_int(b, 0);
        nir_ssa_def *eleven = nir_imm_int(b, 11);
        nir_ssa_def *four = nir_imm_int(b, 4);

        nir_ssa_def *exp =
                nir_usub_sat(b, nir_imax(b, nir_ufind_msb(b, val), zero), four);
        nir_ssa_def *base = nir_ushr(b, val, exp);

        base = nir_iadd(b, base,
                        nir_bcsel(b, nir_ine(b, val, nir_ishl(b, base, exp)), one, zero));

        nir_ssa_def *rshift = nir_imax(b, nir_find_lsb(b, base), zero);
        exp = nir_iadd(b, exp, rshift);
        base = nir_ushr(b, base, rshift);
        base = nir_iadd(b, base, nir_bcsel(b, nir_uge(b, base, eleven), one, zero));
        rshift = nir_imax(b, nir_find_lsb(b, base), zero);
        exp = nir_iadd(b, exp, rshift);
        base = nir_ushr(b, base, rshift);

        *packed = nir_ior(b, exp,
                          nir_ishl(b, nir_ushr_imm(b, base, 1), nir_imm_int(b, 5)));
        return nir_ishl(b, base, exp);
}

static void
update_jobs(struct indirect_draw_shader_builder *builder)
{
        get_invocation(builder);

        if (!(builder->flags & PAN_INDIRECT_DRAW_IDVS))
                update_job(builder, MALI_JOB_TYPE_VERTEX);

        update_job(builder, MALI_JOB_TYPE_TILER);
}


static void
set_null_job(struct indirect_draw_shader_builder *builder,
             nir_ssa_def *job_ptr)
{
        nir_builder *b = &builder->b;
        nir_ssa_def *w4 = get_address_imm(b, job_ptr, WORD(4));
        nir_ssa_def *val = load_global(b, w4, 1, 32);

        /* Set job type to NULL (AKA NOOP) */
        val = nir_ior(b, nir_iand_imm(b, val, 0xffffff01),
                      nir_imm_int(b, MALI_JOB_TYPE_NULL << 1));
        store_global(b, w4, val, 1);
}

static void
get_instance_size(struct indirect_draw_shader_builder *builder)
{
        nir_builder *b = &builder->b;

        if (!builder->index_size) {
                builder->jobs.base_vertex_offset = nir_imm_int(b, 0);
                builder->jobs.offset_start = builder->draw.vertex_start;
                builder->instance_size.raw = builder->draw.vertex_count;
                return;
        }

        unsigned index_size = builder->index_size;
        nir_ssa_def *min = get_min_max_ctx_field(builder, min);
        nir_ssa_def *max = get_min_max_ctx_field(builder, max);

        /* We handle unaligned indices here to avoid the extra complexity in
         * the min/max search job.
         */
        if (builder->index_size < 4) {
                nir_variable *min_var =
                        nir_local_variable_create(b->impl, glsl_uint_type(), "min");
                nir_store_var(b, min_var, min, 1);
                nir_variable *max_var =
                        nir_local_variable_create(b->impl, glsl_uint_type(), "max");
                nir_store_var(b, max_var, max, 1);

                nir_ssa_def *base =
                        get_address(b, builder->draw.index_buf,
                                    nir_imul_imm(b, builder->draw.vertex_start, index_size));
                nir_ssa_def *offset = nir_iand_imm(b, nir_unpack_64_2x32_split_x(b, base), 3);
                nir_ssa_def *end =
                        nir_iadd(b, offset,
                                 nir_imul_imm(b, builder->draw.vertex_count, index_size));
                nir_ssa_def *aligned_end = nir_iand_imm(b, end, ~3);
                unsigned shift = index_size * 8;
                unsigned mask = (1 << shift) - 1;

                base = nir_iand(b, base, nir_imm_int64(b, ~3ULL));

                /* Unaligned start offset, we need to ignore any data that's
                 * outside the requested range. We also handle ranges that are
                 * covering less than 2 words here.
                 */
                IF (nir_ior(b, nir_ine(b, offset, nir_imm_int(b, 0)), nir_ieq(b, aligned_end, nir_imm_int(b, 0)))) {
                        min = nir_load_var(b, min_var);
                        max = nir_load_var(b, max_var);

                        nir_ssa_def *val = load_global(b, base, 1, 32);
                        for (unsigned i = 0; i < sizeof(uint32_t); i += index_size) {
                                nir_ssa_def *oob =
                                        nir_ior(b,
                                                nir_ult(b, nir_imm_int(b, i), offset),
                                                nir_uge(b, nir_imm_int(b, i), end));
                                nir_ssa_def *data = nir_iand_imm(b, val, mask);

                                min = nir_umin(b, min,
                                               nir_bcsel(b, oob, nir_imm_int(b, UINT32_MAX), data));
                                max = nir_umax(b, max,
                                               nir_bcsel(b, oob, nir_imm_int(b, 0), data));
                                val = nir_ushr_imm(b, val, shift);
                        }

                        nir_store_var(b, min_var, min, 1);
                        nir_store_var(b, max_var, max, 1);
                } ENDIF

                nir_ssa_def *remaining = nir_isub(b, end, aligned_end);

                /* The last word contains less than 4bytes of data, we need to
                 * discard anything falling outside the requested range.
                 */
                IF (nir_iand(b, nir_ine(b, end, aligned_end), nir_ine(b, aligned_end, nir_imm_int(b, 0)))) {
                        min = nir_load_var(b, min_var);
                        max = nir_load_var(b, max_var);

                        nir_ssa_def *val = load_global(b, get_address(b, base, aligned_end), 1, 32);
                        for (unsigned i = 0; i < sizeof(uint32_t); i += index_size) {
                                nir_ssa_def *oob = nir_uge(b, nir_imm_int(b, i), remaining);
                                nir_ssa_def *data = nir_iand_imm(b, val, mask);

                                min = nir_umin(b, min,
                                               nir_bcsel(b, oob, nir_imm_int(b, UINT32_MAX), data));
                                max = nir_umax(b, max,
                                               nir_bcsel(b, oob, nir_imm_int(b, 0), data));
                                val = nir_ushr_imm(b, val, shift);
                        }

                        nir_store_var(b, min_var, min, 1);
                        nir_store_var(b, max_var, max, 1);
                } ENDIF

                min = nir_load_var(b, min_var);
                max = nir_load_var(b, max_var);
        }

        builder->jobs.base_vertex_offset = nir_ineg(b, min);
        builder->jobs.offset_start = nir_iadd(b, min, builder->draw.index_bias);
        builder->instance_size.raw = nir_iadd_imm(b, nir_usub_sat(b, max, min), 1);
}

/* Patch a draw sequence */

static void
patch(struct indirect_draw_shader_builder *builder)
{
        unsigned index_size = builder->index_size;
        nir_builder *b = &builder->b;

        nir_ssa_def *draw_ptr = builder->draw.draw_buf;

        if (index_size) {
                builder->draw.vertex_count = get_indexed_draw_field(b, draw_ptr, count);
                builder->draw.start_instance = get_indexed_draw_field(b, draw_ptr, start_instance);
                builder->draw.instance_count =
                        get_indexed_draw_field(b, draw_ptr, instance_count);
                builder->draw.vertex_start = get_indexed_draw_field(b, draw_ptr, start);
                builder->draw.index_bias = get_indexed_draw_field(b, draw_ptr, index_bias);
        } else {
                builder->draw.vertex_count = get_draw_field(b, draw_ptr, count);
                builder->draw.start_instance = get_draw_field(b, draw_ptr, start_instance);
                builder->draw.instance_count = get_draw_field(b, draw_ptr, instance_count);
                builder->draw.vertex_start = get_draw_field(b, draw_ptr, start);
        }

        assert(builder->draw.vertex_count->num_components);

        nir_ssa_def *num_vertices =
                nir_imul(b, builder->draw.vertex_count, builder->draw.instance_count);

        IF (nir_ieq(b, num_vertices, nir_imm_int(b, 0))) {
                /* If there's nothing to draw, turn the vertex/tiler jobs into
                 * null jobs.
                 */
                if (!(builder->flags & PAN_INDIRECT_DRAW_IDVS))
                        set_null_job(builder, builder->jobs.vertex_job);

                set_null_job(builder, builder->jobs.tiler_job);
        } ELSE {
                get_instance_size(builder);

                nir_ssa_def *count = builder->instance_size.raw;

                /* IDVS requires padding to a multiple of 4 */
                if (builder->flags & PAN_INDIRECT_DRAW_IDVS)
                        count = nir_align_pot(b, count, 4);

                builder->instance_size.padded =
                        get_padded_count(b, count,
                                         &builder->instance_size.packed);

                update_varyings(builder);
                update_jobs(builder);
                update_vertex_attribs(builder);

                IF (nir_ine(b, builder->jobs.first_vertex_sysval, nir_imm_int64(b, 0))) {
                        store_global(b, builder->jobs.first_vertex_sysval,
                                     builder->jobs.offset_start, 1);
                } ENDIF

                IF (nir_ine(b, builder->jobs.base_vertex_sysval, nir_imm_int64(b, 0))) {
                        store_global(b, builder->jobs.base_vertex_sysval,
                                     index_size ?
                                     builder->draw.index_bias :
                                     nir_imm_int(b, 0),
                                     1);
                } ENDIF

                IF (nir_ine(b, builder->jobs.base_instance_sysval, nir_imm_int64(b, 0))) {
                        store_global(b, builder->jobs.base_instance_sysval,
                                     builder->draw.start_instance, 1);
                } ENDIF
        } ENDIF
}

/* Search the min/max index in the range covered by the indirect draw call */

static void
get_index_min_max(struct indirect_draw_shader_builder *builder)
{
        nir_ssa_def *restart_index = builder->draw.restart_index;
        unsigned index_size = builder->index_size;
        nir_builder *b = &builder->b;

        nir_ssa_def *draw_ptr = builder->draw.draw_buf;

        builder->draw.vertex_count = get_draw_field(b, draw_ptr, count);
        builder->draw.vertex_start = get_draw_field(b, draw_ptr, start);

        nir_ssa_def *thread_id = nir_channel(b, nir_load_global_invocation_id(b, 32), 0);
        nir_variable *min_var =
                nir_local_variable_create(b->impl, glsl_uint_type(), "min");
        nir_store_var(b, min_var, nir_imm_int(b, UINT32_MAX), 1);
        nir_variable *max_var =
                nir_local_variable_create(b->impl, glsl_uint_type(), "max");
        nir_store_var(b, max_var, nir_imm_int(b, 0), 1);

        nir_ssa_def *base =
                get_address(b, builder->draw.index_buf,
                            nir_imul_imm(b, builder->draw.vertex_start, index_size));


        nir_ssa_def *start = nir_iand_imm(b, nir_unpack_64_2x32_split_x(b, base), 3);
        nir_ssa_def *end =
                nir_iadd(b, start, nir_imul_imm(b, builder->draw.vertex_count, index_size));

        base = nir_iand(b, base, nir_imm_int64(b, ~3ULL));

        /* Align on 4 bytes, non-aligned indices are handled in the indirect draw job. */
        start = nir_iand_imm(b, nir_iadd_imm(b, start, 3), ~3);
        end = nir_iand_imm(b, end, ~3);

        /* Add the job offset. */
        start = nir_iadd(b, start, nir_imul_imm(b, thread_id, sizeof(uint32_t)));

        nir_variable *offset_var =
                nir_local_variable_create(b->impl, glsl_uint_type(), "offset");
        nir_store_var(b, offset_var, start, 1);

        LOOP {
                nir_ssa_def *offset = nir_load_var(b, offset_var);
                IF (nir_uge(b, offset, end))
                        BREAK;
                ENDIF

                nir_ssa_def *val = load_global(b, get_address(b, base, offset), 1, 32);
                nir_ssa_def *old_min = nir_load_var(b, min_var);
                nir_ssa_def *old_max = nir_load_var(b, max_var);
                nir_ssa_def *new_min;
                nir_ssa_def *new_max;

                /* TODO: use 8/16 bit arithmetic when index_size < 4. */
                for (unsigned i = 0; i < 4; i += index_size) {
                        nir_ssa_def *data = nir_ushr_imm(b, val, i * 8);
                        data = nir_iand_imm(b, data, (1ULL << (index_size * 8)) - 1);
                        new_min = nir_umin(b, old_min, data);
                        new_max = nir_umax(b, old_max, data);
                        if (restart_index) {
                                new_min = nir_bcsel(b, nir_ine(b, restart_index, data), new_min, old_min);
                                new_max = nir_bcsel(b, nir_ine(b, restart_index, data), new_max, old_max);
                        }
                        old_min = new_min;
                        old_max = new_max;
                }

                nir_store_var(b, min_var, new_min, 1);
                nir_store_var(b, max_var, new_max, 1);
                nir_store_var(b, offset_var,
                              nir_iadd_imm(b, offset, MIN_MAX_JOBS * sizeof(uint32_t)), 1);
        }

        IF (nir_ult(b, start, end))
                update_min(builder, nir_load_var(b, min_var));
                update_max(builder, nir_load_var(b, max_var));
        ENDIF
}

static unsigned
get_shader_id(unsigned flags, unsigned index_size, bool index_min_max_search)
{
        if (!index_min_max_search) {
                flags &= PAN_INDIRECT_DRAW_FLAGS_MASK;
                flags &= ~PAN_INDIRECT_DRAW_INDEX_SIZE_MASK;
                if (index_size)
                        flags |= (util_logbase2(index_size) + 1);
                return flags;
        }

        return ((flags & PAN_INDIRECT_DRAW_PRIMITIVE_RESTART) ?
                PAN_INDIRECT_DRAW_MIN_MAX_SEARCH_1B_INDEX_PRIM_RESTART :
                PAN_INDIRECT_DRAW_MIN_MAX_SEARCH_1B_INDEX) +
               util_logbase2(index_size);
}

static void
create_indirect_draw_shader(struct panfrost_device *dev,
                            unsigned flags, unsigned index_size,
                            bool index_min_max_search)
{
        assert(flags < PAN_INDIRECT_DRAW_NUM_SHADERS);
        struct indirect_draw_shader_builder builder;
        init_shader_builder(&builder, dev, flags, index_size, index_min_max_search);

        nir_builder *b = &builder.b;

        if (index_min_max_search)
                get_index_min_max(&builder);
        else
                patch(&builder);

        struct panfrost_compile_inputs inputs = { .gpu_id = dev->gpu_id };
        struct pan_shader_info shader_info;
        struct util_dynarray binary;

        util_dynarray_init(&binary, NULL);
        GENX(pan_shader_compile)(b->shader, &inputs, &binary, &shader_info);

        assert(!shader_info.tls_size);
        assert(!shader_info.wls_size);
        assert(!shader_info.sysvals.sysval_count);

        unsigned shader_id = get_shader_id(flags, index_size, index_min_max_search);
        struct pan_indirect_draw_shader *draw_shader =
                &dev->indirect_draw_shaders.shaders[shader_id];
        void *state = dev->indirect_draw_shaders.states->ptr.cpu +
                      (shader_id * pan_size(RENDERER_STATE));

        pthread_mutex_lock(&dev->indirect_draw_shaders.lock);
        if (!draw_shader->rsd) {
                mali_ptr address =
                        pan_pool_upload_aligned(dev->indirect_draw_shaders.bin_pool,
                                                binary.data, binary.size,
                                                PAN_ARCH >= 6 ? 128 : 64);

#if PAN_ARCH <= 5
                address |= shader_info.midgard.first_tag;
#endif

                util_dynarray_fini(&binary);

                pan_pack(state, RENDERER_STATE, cfg) {
                        pan_shader_prepare_rsd(&shader_info, address, &cfg);
                }

                draw_shader->push = shader_info.push;
                draw_shader->rsd = dev->indirect_draw_shaders.states->ptr.gpu +
                                   (shader_id * pan_size(RENDERER_STATE));
        }
        pthread_mutex_unlock(&dev->indirect_draw_shaders.lock);

        ralloc_free(b->shader);
}

static mali_ptr
get_renderer_state(struct panfrost_device *dev, unsigned flags,
                   unsigned index_size, bool index_min_max_search)
{
        unsigned shader_id = get_shader_id(flags, index_size, index_min_max_search);
        struct pan_indirect_draw_shader *info =
                &dev->indirect_draw_shaders.shaders[shader_id];

        if (!info->rsd) {
                create_indirect_draw_shader(dev, flags, index_size,
                                            index_min_max_search);
                assert(info->rsd);
        }

        return info->rsd;
}

static mali_ptr
get_tls(const struct panfrost_device *dev)
{
        return dev->indirect_draw_shaders.states->ptr.gpu +
               (PAN_INDIRECT_DRAW_NUM_SHADERS * pan_size(RENDERER_STATE));
}

static mali_ptr
get_ubos(struct pan_pool *pool,
         const struct indirect_draw_inputs *inputs)
{
        struct panfrost_ptr inputs_buf =
                pan_pool_alloc_aligned(pool, sizeof(*inputs), 16);

        memcpy(inputs_buf.cpu, inputs, sizeof(*inputs));

        struct panfrost_ptr ubos_buf =
                pan_pool_alloc_desc(pool, UNIFORM_BUFFER);

        pan_pack(ubos_buf.cpu, UNIFORM_BUFFER, cfg) {
                cfg.entries = DIV_ROUND_UP(sizeof(*inputs), 16);
                cfg.pointer = inputs_buf.gpu;
        }

        return ubos_buf.gpu;
}

static mali_ptr
get_push_uniforms(struct pan_pool *pool,
                  const struct pan_indirect_draw_shader *shader,
                  const struct indirect_draw_inputs *inputs)
{
        if (!shader->push.count)
                return 0;

        struct panfrost_ptr push_consts_buf =
                pan_pool_alloc_aligned(pool, shader->push.count * 4, 16);
        uint32_t *out = push_consts_buf.cpu;
        uint8_t *in = (uint8_t *)inputs;

        for (unsigned i = 0; i < shader->push.count; ++i)
                memcpy(out + i, in + shader->push.words[i].offset, 4);

        return push_consts_buf.gpu;
}

static void
panfrost_indirect_draw_alloc_deps(struct panfrost_device *dev)
{
        pthread_mutex_lock(&dev->indirect_draw_shaders.lock);
        if (dev->indirect_draw_shaders.states)
                goto out;

        unsigned state_bo_size = (PAN_INDIRECT_DRAW_NUM_SHADERS *
                                  pan_size(RENDERER_STATE)) +
                                 pan_size(LOCAL_STORAGE);

        dev->indirect_draw_shaders.states =
                panfrost_bo_create(dev, state_bo_size, 0, "Indirect draw states");

        /* Prepare the thread storage descriptor now since it's invariant. */
        void *tsd = dev->indirect_draw_shaders.states->ptr.cpu +
                    (PAN_INDIRECT_DRAW_NUM_SHADERS * pan_size(RENDERER_STATE));
        pan_pack(tsd, LOCAL_STORAGE, ls) {
                ls.wls_instances = MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM;
        };

        /* FIXME: Currently allocating 512M of growable memory, meaning that we
         * only allocate what we really use, the problem is:
         * - allocation happens 2M at a time, which might be more than we
         *   actually need
         * - the memory is attached to the device to speed up subsequent
         *   indirect draws, but that also means it's never shrinked
         */
        dev->indirect_draw_shaders.varying_heap =
                panfrost_bo_create(dev, 512 * 1024 * 1024,
                                   PAN_BO_INVISIBLE | PAN_BO_GROWABLE,
                                   "Indirect draw varying heap");

out:
        pthread_mutex_unlock(&dev->indirect_draw_shaders.lock);
}

static unsigned
panfrost_emit_index_min_max_search(struct pan_pool *pool,
                                   struct pan_scoreboard *scoreboard,
                                   const struct pan_indirect_draw_info *draw_info,
                                   const struct indirect_draw_inputs *inputs,
                                   struct indirect_draw_context *draw_ctx,
                                   mali_ptr ubos)
{
        struct panfrost_device *dev = pool->dev;
        unsigned index_size = draw_info->index_size;

        if (!index_size)
                return 0;

        mali_ptr rsd =
                get_renderer_state(dev, draw_info->flags,
                                   draw_info->index_size, true);
        unsigned shader_id =
                get_shader_id(draw_info->flags, draw_info->index_size, true);
        const struct pan_indirect_draw_shader *shader =
                &dev->indirect_draw_shaders.shaders[shader_id];
        struct panfrost_ptr job =
                pan_pool_alloc_desc(pool, COMPUTE_JOB);
        void *invocation =
                pan_section_ptr(job.cpu, COMPUTE_JOB, INVOCATION);
        panfrost_pack_work_groups_compute(invocation,
                                          1, 1, 1, MIN_MAX_JOBS, 1, 1,
                                          false, false);

        pan_section_pack(job.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
                cfg.job_task_split = 7;
        }

        pan_section_pack(job.cpu, COMPUTE_JOB, DRAW, cfg) {
                cfg.draw_descriptor_is_64b = true;
                cfg.state = rsd;
                cfg.thread_storage = get_tls(pool->dev);
                cfg.uniform_buffers = ubos;
                cfg.push_uniforms = get_push_uniforms(pool, shader, inputs);
        }

        return panfrost_add_job(pool, scoreboard, MALI_JOB_TYPE_COMPUTE,
                                false, false, 0, 0, &job, false);
}

unsigned
GENX(panfrost_emit_indirect_draw)(struct pan_pool *pool,
                                  struct pan_scoreboard *scoreboard,
                                  const struct pan_indirect_draw_info *draw_info,
                                  struct panfrost_ptr *ctx)
{
        struct panfrost_device *dev = pool->dev;

        /* Currently only tested on Bifrost, but the logic should be the same
         * on Midgard.
         */
        assert(pan_is_bifrost(dev));

        panfrost_indirect_draw_alloc_deps(dev);

        struct panfrost_ptr job =
                pan_pool_alloc_desc(pool, COMPUTE_JOB);
        mali_ptr rsd =
                get_renderer_state(dev, draw_info->flags,
                                   draw_info->index_size, false);

        struct indirect_draw_context draw_ctx = {
                .varying_mem = dev->indirect_draw_shaders.varying_heap->ptr.gpu,
        };

        struct panfrost_ptr draw_ctx_ptr = *ctx;
        if (!draw_ctx_ptr.cpu) {
                draw_ctx_ptr = pan_pool_alloc_aligned(pool,
                                                      sizeof(draw_ctx),
                                                      sizeof(mali_ptr));
        }

        struct indirect_draw_inputs inputs = {
                .draw_ctx = draw_ctx_ptr.gpu,
                .draw_buf = draw_info->draw_buf,
                .index_buf = draw_info->index_buf,
                .first_vertex_sysval = draw_info->first_vertex_sysval,
                .base_vertex_sysval = draw_info->base_vertex_sysval,
                .base_instance_sysval = draw_info->base_instance_sysval,
                .vertex_job = draw_info->vertex_job,
                .tiler_job = draw_info->tiler_job,
                .attrib_bufs = draw_info->attrib_bufs,
                .attribs = draw_info->attribs,
                .varying_bufs = draw_info->varying_bufs,
                .attrib_count = draw_info->attrib_count,
        };

        if (draw_info->index_size) {
                inputs.restart_index = draw_info->restart_index;

                struct panfrost_ptr min_max_ctx_ptr =
                        pan_pool_alloc_aligned(pool,
                                               sizeof(struct min_max_context),
                                               4);
                struct min_max_context *ctx = min_max_ctx_ptr.cpu;

                ctx->min = UINT32_MAX;
                ctx->max = 0;
                inputs.min_max_ctx = min_max_ctx_ptr.gpu;
        }

        unsigned shader_id =
                get_shader_id(draw_info->flags, draw_info->index_size, false);
        const struct pan_indirect_draw_shader *shader =
                &dev->indirect_draw_shaders.shaders[shader_id];
        mali_ptr ubos = get_ubos(pool, &inputs);

        void *invocation =
                pan_section_ptr(job.cpu, COMPUTE_JOB, INVOCATION);
        panfrost_pack_work_groups_compute(invocation,
                                          1, 1, 1, 1, 1, 1,
                                          false, false);

        pan_section_pack(job.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
                cfg.job_task_split = 2;
        }

        pan_section_pack(job.cpu, COMPUTE_JOB, DRAW, cfg) {
                cfg.draw_descriptor_is_64b = true;
                cfg.state = rsd;
                cfg.thread_storage = get_tls(pool->dev);
                cfg.uniform_buffers = ubos;
                cfg.push_uniforms = get_push_uniforms(pool, shader, &inputs);
        }

        unsigned global_dep = draw_info->last_indirect_draw;
        unsigned local_dep =
                panfrost_emit_index_min_max_search(pool, scoreboard, draw_info,
                                                   &inputs, &draw_ctx, ubos);

        if (!ctx->cpu) {
                *ctx = draw_ctx_ptr;
                memcpy(ctx->cpu, &draw_ctx, sizeof(draw_ctx));
        }

        return panfrost_add_job(pool, scoreboard, MALI_JOB_TYPE_COMPUTE,
                                false, true, local_dep, global_dep,
                                &job, false);
}

void
GENX(panfrost_init_indirect_draw_shaders)(struct panfrost_device *dev,
                                          struct pan_pool *bin_pool)
{
        /* We allocate the states and varying_heap BO lazily to avoid
         * reserving memory when indirect draws are not used.
         */
        pthread_mutex_init(&dev->indirect_draw_shaders.lock, NULL);
        dev->indirect_draw_shaders.bin_pool = bin_pool;
}

void
GENX(panfrost_cleanup_indirect_draw_shaders)(struct panfrost_device *dev)
{
        panfrost_bo_unreference(dev->indirect_draw_shaders.states);
        panfrost_bo_unreference(dev->indirect_draw_shaders.varying_heap);
        pthread_mutex_destroy(&dev->indirect_draw_shaders.lock);
}
