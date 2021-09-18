/*
 * Copyright © 2016 Broadcom
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

#include <inttypes.h>
#include "util/format/u_format.h"
#include "util/u_helpers.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/ralloc.h"
#include "util/hash_table.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "common/v3d_device_info.h"
#include "v3d_compiler.h"

/* We don't do any address packing. */
#define __gen_user_data void
#define __gen_address_type uint32_t
#define __gen_address_offset(reloc) (*reloc)
#define __gen_emit_reloc(cl, reloc)
#include "cle/v3d_packet_v41_pack.h"

#define GENERAL_TMU_LOOKUP_PER_QUAD                 (0 << 7)
#define GENERAL_TMU_LOOKUP_PER_PIXEL                (1 << 7)
#define GENERAL_TMU_LOOKUP_TYPE_8BIT_I              (0 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_16BIT_I             (1 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_VEC2                (2 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_VEC3                (3 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_VEC4                (4 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_8BIT_UI             (5 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_16BIT_UI            (6 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_32BIT_UI            (7 << 0)

#define V3D_TSY_SET_QUORUM          0
#define V3D_TSY_INC_WAITERS         1
#define V3D_TSY_DEC_WAITERS         2
#define V3D_TSY_INC_QUORUM          3
#define V3D_TSY_DEC_QUORUM          4
#define V3D_TSY_FREE_ALL            5
#define V3D_TSY_RELEASE             6
#define V3D_TSY_ACQUIRE             7
#define V3D_TSY_WAIT                8
#define V3D_TSY_WAIT_INC            9
#define V3D_TSY_WAIT_CHECK          10
#define V3D_TSY_WAIT_INC_CHECK      11
#define V3D_TSY_WAIT_CV             12
#define V3D_TSY_INC_SEMAPHORE       13
#define V3D_TSY_DEC_SEMAPHORE       14
#define V3D_TSY_SET_QUORUM_FREE_ALL 15

enum v3d_tmu_op_type
{
        V3D_TMU_OP_TYPE_REGULAR,
        V3D_TMU_OP_TYPE_ATOMIC,
        V3D_TMU_OP_TYPE_CACHE
};

static enum v3d_tmu_op_type
v3d_tmu_get_type_from_op(uint32_t tmu_op, bool is_write)
{
        switch(tmu_op) {
        case V3D_TMU_OP_WRITE_ADD_READ_PREFETCH:
        case V3D_TMU_OP_WRITE_SUB_READ_CLEAR:
        case V3D_TMU_OP_WRITE_XCHG_READ_FLUSH:
        case V3D_TMU_OP_WRITE_CMPXCHG_READ_FLUSH:
        case V3D_TMU_OP_WRITE_UMIN_FULL_L1_CLEAR:
                return is_write ? V3D_TMU_OP_TYPE_ATOMIC : V3D_TMU_OP_TYPE_CACHE;
        case V3D_TMU_OP_WRITE_UMAX:
        case V3D_TMU_OP_WRITE_SMIN:
        case V3D_TMU_OP_WRITE_SMAX:
                assert(is_write);
                FALLTHROUGH;
        case V3D_TMU_OP_WRITE_AND_READ_INC:
        case V3D_TMU_OP_WRITE_OR_READ_DEC:
        case V3D_TMU_OP_WRITE_XOR_READ_NOT:
                return V3D_TMU_OP_TYPE_ATOMIC;
        case V3D_TMU_OP_REGULAR:
                return V3D_TMU_OP_TYPE_REGULAR;

        default:
                unreachable("Unknown tmu_op\n");
        }
}
static void
ntq_emit_cf_list(struct v3d_compile *c, struct exec_list *list);

static void
resize_qreg_array(struct v3d_compile *c,
                  struct qreg **regs,
                  uint32_t *size,
                  uint32_t decl_size)
{
        if (*size >= decl_size)
                return;

        uint32_t old_size = *size;
        *size = MAX2(*size * 2, decl_size);
        *regs = reralloc(c, *regs, struct qreg, *size);
        if (!*regs) {
                fprintf(stderr, "Malloc failure\n");
                abort();
        }

        for (uint32_t i = old_size; i < *size; i++)
                (*regs)[i] = c->undef;
}

static void
resize_interp_array(struct v3d_compile *c,
                    struct v3d_interp_input **regs,
                    uint32_t *size,
                    uint32_t decl_size)
{
        if (*size >= decl_size)
                return;

        uint32_t old_size = *size;
        *size = MAX2(*size * 2, decl_size);
        *regs = reralloc(c, *regs, struct v3d_interp_input, *size);
        if (!*regs) {
                fprintf(stderr, "Malloc failure\n");
                abort();
        }

        for (uint32_t i = old_size; i < *size; i++) {
                (*regs)[i].vp = c->undef;
                (*regs)[i].C = c->undef;
        }
}

void
vir_emit_thrsw(struct v3d_compile *c)
{
        if (c->threads == 1)
                return;

        /* Always thread switch after each texture operation for now.
         *
         * We could do better by batching a bunch of texture fetches up and
         * then doing one thread switch and collecting all their results
         * afterward.
         */
        c->last_thrsw = vir_NOP(c);
        c->last_thrsw->qpu.sig.thrsw = true;
        c->last_thrsw_at_top_level = !c->in_control_flow;

        /* We need to lock the scoreboard before any tlb acess happens. If this
         * thread switch comes after we have emitted a tlb load, then it means
         * that we can't lock on the last thread switch any more.
         */
        if (c->emitted_tlb_load)
                c->lock_scoreboard_on_first_thrsw = true;
}

uint32_t
v3d_get_op_for_atomic_add(nir_intrinsic_instr *instr, unsigned src)
{
        if (nir_src_is_const(instr->src[src])) {
                int64_t add_val = nir_src_as_int(instr->src[src]);
                if (add_val == 1)
                        return V3D_TMU_OP_WRITE_AND_READ_INC;
                else if (add_val == -1)
                        return V3D_TMU_OP_WRITE_OR_READ_DEC;
        }

        return V3D_TMU_OP_WRITE_ADD_READ_PREFETCH;
}

static uint32_t
v3d_general_tmu_op(nir_intrinsic_instr *instr)
{
        switch (instr->intrinsic) {
        case nir_intrinsic_load_ssbo:
        case nir_intrinsic_load_ubo:
        case nir_intrinsic_load_uniform:
        case nir_intrinsic_load_shared:
        case nir_intrinsic_load_scratch:
        case nir_intrinsic_store_ssbo:
        case nir_intrinsic_store_shared:
        case nir_intrinsic_store_scratch:
                return V3D_TMU_OP_REGULAR;
        case nir_intrinsic_ssbo_atomic_add:
                return v3d_get_op_for_atomic_add(instr, 2);
        case nir_intrinsic_shared_atomic_add:
                return v3d_get_op_for_atomic_add(instr, 1);
        case nir_intrinsic_ssbo_atomic_imin:
        case nir_intrinsic_shared_atomic_imin:
                return V3D_TMU_OP_WRITE_SMIN;
        case nir_intrinsic_ssbo_atomic_umin:
        case nir_intrinsic_shared_atomic_umin:
                return V3D_TMU_OP_WRITE_UMIN_FULL_L1_CLEAR;
        case nir_intrinsic_ssbo_atomic_imax:
        case nir_intrinsic_shared_atomic_imax:
                return V3D_TMU_OP_WRITE_SMAX;
        case nir_intrinsic_ssbo_atomic_umax:
        case nir_intrinsic_shared_atomic_umax:
                return V3D_TMU_OP_WRITE_UMAX;
        case nir_intrinsic_ssbo_atomic_and:
        case nir_intrinsic_shared_atomic_and:
                return V3D_TMU_OP_WRITE_AND_READ_INC;
        case nir_intrinsic_ssbo_atomic_or:
        case nir_intrinsic_shared_atomic_or:
                return V3D_TMU_OP_WRITE_OR_READ_DEC;
        case nir_intrinsic_ssbo_atomic_xor:
        case nir_intrinsic_shared_atomic_xor:
                return V3D_TMU_OP_WRITE_XOR_READ_NOT;
        case nir_intrinsic_ssbo_atomic_exchange:
        case nir_intrinsic_shared_atomic_exchange:
                return V3D_TMU_OP_WRITE_XCHG_READ_FLUSH;
        case nir_intrinsic_ssbo_atomic_comp_swap:
        case nir_intrinsic_shared_atomic_comp_swap:
                return V3D_TMU_OP_WRITE_CMPXCHG_READ_FLUSH;
        default:
                unreachable("unknown intrinsic op");
        }
}

/**
 * Checks if pipelining a new TMU operation requiring 'components' LDTMUs
 * would overflow the Output TMU fifo.
 *
 * It is not allowed to overflow the Output fifo, however, we can overflow
 * Input and Config fifos. Doing that makes the shader stall, but only for as
 * long as it needs to be able to continue so it is better for pipelining to
 * let the QPU stall on these if needed than trying to emit TMU flushes in the
 * driver.
 */
bool
ntq_tmu_fifo_overflow(struct v3d_compile *c, uint32_t components)
{
        if (c->tmu.flush_count >= MAX_TMU_QUEUE_SIZE)
                return true;

        return components > 0 &&
               c->tmu.output_fifo_size + components > 16 / c->threads;
}

/**
 * Emits the thread switch and LDTMU/TMUWT for all outstanding TMU operations,
 * popping all TMU fifo entries.
 */
void
ntq_flush_tmu(struct v3d_compile *c)
{
        if (c->tmu.flush_count == 0)
                return;

        vir_emit_thrsw(c);

        bool emitted_tmuwt = false;
        for (int i = 0; i < c->tmu.flush_count; i++) {
                if (c->tmu.flush[i].component_mask > 0) {
                        nir_dest *dest = c->tmu.flush[i].dest;
                        assert(dest);

                        for (int j = 0; j < 4; j++) {
                                if (c->tmu.flush[i].component_mask & (1 << j)) {
                                        ntq_store_dest(c, dest, j,
                                                       vir_MOV(c, vir_LDTMU(c)));
                                }
                        }
                } else if (!emitted_tmuwt) {
                        vir_TMUWT(c);
                        emitted_tmuwt = true;
                }
        }

        c->tmu.output_fifo_size = 0;
        c->tmu.flush_count = 0;
        _mesa_set_clear(c->tmu.outstanding_regs, NULL);
}

/**
 * Queues a pending thread switch + LDTMU/TMUWT for a TMU operation. The caller
 * is reponsible for ensuring that doing this doesn't overflow the TMU fifos,
 * and more specifically, the output fifo, since that can't stall.
 */
void
ntq_add_pending_tmu_flush(struct v3d_compile *c,
                          nir_dest *dest,
                          uint32_t component_mask)
{
        const uint32_t num_components = util_bitcount(component_mask);
        assert(!ntq_tmu_fifo_overflow(c, num_components));

        if (num_components > 0) {
                c->tmu.output_fifo_size += num_components;
                if (!dest->is_ssa)
                        _mesa_set_add(c->tmu.outstanding_regs, dest->reg.reg);
        }

        c->tmu.flush[c->tmu.flush_count].dest = dest;
        c->tmu.flush[c->tmu.flush_count].component_mask = component_mask;
        c->tmu.flush_count++;

        if (c->disable_tmu_pipelining)
                ntq_flush_tmu(c);
        else if (c->tmu.flush_count > 1)
                c->pipelined_any_tmu = true;
}

enum emit_mode {
    MODE_COUNT = 0,
    MODE_EMIT,
    MODE_LAST,
};

/**
 * For a TMU general store instruction:
 *
 * In MODE_COUNT mode, records the number of TMU writes required and flushes
 * any outstanding TMU operations the instruction depends on, but it doesn't
 * emit any actual register writes.
 *
 * In MODE_EMIT mode, emits the data register writes required by the
 * instruction.
 */
static void
emit_tmu_general_store_writes(struct v3d_compile *c,
                              enum emit_mode mode,
                              nir_intrinsic_instr *instr,
                              uint32_t base_const_offset,
                              uint32_t *writemask,
                              uint32_t *const_offset,
                              uint32_t *tmu_writes)
{
        struct qreg tmud = vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_TMUD);

        /* Find the first set of consecutive components that
         * are enabled in the writemask and emit the TMUD
         * instructions for them.
         */
        assert(*writemask != 0);
        uint32_t first_component = ffs(*writemask) - 1;
        uint32_t last_component = first_component;
        while (*writemask & BITFIELD_BIT(last_component + 1))
                last_component++;

        assert(first_component <= last_component &&
               last_component < instr->num_components);

        for (int i = first_component; i <= last_component; i++) {
                struct qreg data = ntq_get_src(c, instr->src[0], i);
                if (mode == MODE_COUNT)
                        (*tmu_writes)++;
                else
                        vir_MOV_dest(c, tmud, data);
        }

        if (mode == MODE_EMIT) {
                /* Update the offset for the TMU write based on the
                 * the first component we are writing.
                 */
                *const_offset = base_const_offset + first_component * 4;

                /* Clear these components from the writemask */
                uint32_t written_mask =
                        BITFIELD_RANGE(first_component, *tmu_writes);
                (*writemask) &= ~written_mask;
        }
}

/**
 * For a TMU general atomic instruction:
 *
 * In MODE_COUNT mode, records the number of TMU writes required and flushes
 * any outstanding TMU operations the instruction depends on, but it doesn't
 * emit any actual register writes.
 *
 * In MODE_EMIT mode, emits the data register writes required by the
 * instruction.
 */
static void
emit_tmu_general_atomic_writes(struct v3d_compile *c,
                               enum emit_mode mode,
                               nir_intrinsic_instr *instr,
                               uint32_t tmu_op,
                               bool has_index,
                               uint32_t *tmu_writes)
{
        struct qreg tmud = vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_TMUD);

        struct qreg data = ntq_get_src(c, instr->src[1 + has_index], 0);
        if (mode == MODE_COUNT)
                (*tmu_writes)++;
        else
                vir_MOV_dest(c, tmud, data);

        if (tmu_op == V3D_TMU_OP_WRITE_CMPXCHG_READ_FLUSH) {
                data = ntq_get_src(c, instr->src[2 + has_index], 0);
                if (mode == MODE_COUNT)
                        (*tmu_writes)++;
                else
                        vir_MOV_dest(c, tmud, data);
        }
}

/**
 * For any TMU general instruction:
 *
 * In MODE_COUNT mode, records the number of TMU writes required to emit the
 * address parameter and flushes any outstanding TMU operations the instruction
 * depends on, but it doesn't emit any actual register writes.
 *
 * In MODE_EMIT mode, emits register writes required to emit the address.
 */
static void
emit_tmu_general_address_write(struct v3d_compile *c,
                               enum emit_mode mode,
                               nir_intrinsic_instr *instr,
                               uint32_t config,
                               bool dynamic_src,
                               int offset_src,
                               struct qreg base_offset,
                               uint32_t const_offset,
                               uint32_t *tmu_writes)
{
        if (mode == MODE_COUNT) {
                (*tmu_writes)++;
                if (dynamic_src)
                        ntq_get_src(c, instr->src[offset_src], 0);
                return;
        }

        if (vir_in_nonuniform_control_flow(c)) {
                vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), c->execute),
                           V3D_QPU_PF_PUSHZ);
        }

        struct qreg tmua;
        if (config == ~0)
                tmua = vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_TMUA);
        else
                tmua = vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_TMUAU);

        struct qinst *tmu;
        if (dynamic_src) {
                struct qreg offset = base_offset;
                if (const_offset != 0) {
                        offset = vir_ADD(c, offset,
                                         vir_uniform_ui(c, const_offset));
                }
                struct qreg data = ntq_get_src(c, instr->src[offset_src], 0);
                tmu = vir_ADD_dest(c, tmua, offset, data);
        } else {
                if (const_offset != 0) {
                        tmu = vir_ADD_dest(c, tmua, base_offset,
                                           vir_uniform_ui(c, const_offset));
                } else {
                        tmu = vir_MOV_dest(c, tmua, base_offset);
                }
        }

        if (config != ~0) {
                tmu->uniform =
                        vir_get_uniform_index(c, QUNIFORM_CONSTANT, config);
        }

        if (vir_in_nonuniform_control_flow(c))
                vir_set_cond(tmu, V3D_QPU_COND_IFA);
}

/**
 * Implements indirect uniform loads and SSBO accesses through the TMU general
 * memory access interface.
 */
static void
ntq_emit_tmu_general(struct v3d_compile *c, nir_intrinsic_instr *instr,
                     bool is_shared_or_scratch)
{
        uint32_t tmu_op = v3d_general_tmu_op(instr);

        /* If we were able to replace atomic_add for an inc/dec, then we
         * need/can to do things slightly different, like not loading the
         * amount to add/sub, as that is implicit.
         */
        bool atomic_add_replaced =
                ((instr->intrinsic == nir_intrinsic_ssbo_atomic_add ||
                  instr->intrinsic == nir_intrinsic_shared_atomic_add) &&
                 (tmu_op == V3D_TMU_OP_WRITE_AND_READ_INC ||
                  tmu_op == V3D_TMU_OP_WRITE_OR_READ_DEC));

        bool is_store = (instr->intrinsic == nir_intrinsic_store_ssbo ||
                         instr->intrinsic == nir_intrinsic_store_scratch ||
                         instr->intrinsic == nir_intrinsic_store_shared);

        bool is_load = (instr->intrinsic == nir_intrinsic_load_uniform ||
                        instr->intrinsic == nir_intrinsic_load_ubo ||
                        instr->intrinsic == nir_intrinsic_load_ssbo ||
                        instr->intrinsic == nir_intrinsic_load_scratch ||
                        instr->intrinsic == nir_intrinsic_load_shared);

        if (!is_load)
                c->tmu_dirty_rcl = true;

        bool has_index = !is_shared_or_scratch;

        int offset_src;
        if (instr->intrinsic == nir_intrinsic_load_uniform) {
                offset_src = 0;
        } else if (instr->intrinsic == nir_intrinsic_load_ssbo ||
                   instr->intrinsic == nir_intrinsic_load_ubo ||
                   instr->intrinsic == nir_intrinsic_load_scratch ||
                   instr->intrinsic == nir_intrinsic_load_shared ||
                   atomic_add_replaced) {
                offset_src = 0 + has_index;
        } else if (is_store) {
                offset_src = 1 + has_index;
        } else {
                offset_src = 0 + has_index;
        }

        bool dynamic_src = !nir_src_is_const(instr->src[offset_src]);
        uint32_t const_offset = 0;
        if (!dynamic_src)
                const_offset = nir_src_as_uint(instr->src[offset_src]);

        struct qreg base_offset;
        if (instr->intrinsic == nir_intrinsic_load_uniform) {
                const_offset += nir_intrinsic_base(instr);
                base_offset = vir_uniform(c, QUNIFORM_UBO_ADDR,
                                          v3d_unit_data_create(0, const_offset));
                const_offset = 0;
        } else if (instr->intrinsic == nir_intrinsic_load_ubo) {
                uint32_t index = nir_src_as_uint(instr->src[0]);
                /* On OpenGL QUNIFORM_UBO_ADDR takes a UBO index
                 * shifted up by 1 (0 is gallium's constant buffer 0).
                 */
                if (c->key->environment == V3D_ENVIRONMENT_OPENGL)
                        index++;

                base_offset =
                        vir_uniform(c, QUNIFORM_UBO_ADDR,
                                    v3d_unit_data_create(index, const_offset));
                const_offset = 0;
        } else if (is_shared_or_scratch) {
                /* Shared and scratch variables have no buffer index, and all
                 * start from a common base that we set up at the start of
                 * dispatch.
                 */
                if (instr->intrinsic == nir_intrinsic_load_scratch ||
                    instr->intrinsic == nir_intrinsic_store_scratch) {
                        base_offset = c->spill_base;
                } else {
                        base_offset = c->cs_shared_offset;
                        const_offset += nir_intrinsic_base(instr);
                }
        } else {
                base_offset = vir_uniform(c, QUNIFORM_SSBO_OFFSET,
                                          nir_src_as_uint(instr->src[is_store ?
                                                                      1 : 0]));
        }

        /* We are ready to emit TMU register writes now, but before we actually
         * emit them we need to flush outstanding TMU operations if any of our
         * writes reads from the result of an outstanding TMU operation before
         * we start the TMU sequence for this operation, since otherwise the
         * flush could happen in the middle of the TMU sequence we are about to
         * emit, which is illegal. To do this we run this logic twice, the
         * first time it will count required register writes and flush pending
         * TMU requests if necessary due to a dependency, and the second one
         * will emit the actual TMU writes.
         */
        const uint32_t dest_components = nir_intrinsic_dest_components(instr);
        uint32_t base_const_offset = const_offset;
        uint32_t writemask = is_store ? nir_intrinsic_write_mask(instr) : 0;
        uint32_t tmu_writes = 0;
        for (enum emit_mode mode = MODE_COUNT; mode != MODE_LAST; mode++) {
                assert(mode == MODE_COUNT || tmu_writes > 0);

                if (is_store) {
                        emit_tmu_general_store_writes(c, mode, instr,
                                                      base_const_offset,
                                                      &writemask,
                                                      &const_offset,
                                                      &tmu_writes);
                } else if (!is_load && !atomic_add_replaced) {
                         emit_tmu_general_atomic_writes(c, mode, instr,
                                                        tmu_op, has_index,
                                                        &tmu_writes);
                }

                /* For atomics we use 32bit except for CMPXCHG, that we need
                 * to use VEC2. For the rest of the cases we use the number of
                 * tmud writes we did to decide the type. For cache operations
                 * the type is ignored.
                 */
                uint32_t config = 0;
                if (mode == MODE_EMIT) {
                        uint32_t num_components;
                        if (is_load || atomic_add_replaced) {
                                num_components = instr->num_components;
                        } else {
                                assert(tmu_writes > 0);
                                num_components = tmu_writes - 1;
                        }
                        bool is_atomic =
                                v3d_tmu_get_type_from_op(tmu_op, !is_load) ==
                                V3D_TMU_OP_TYPE_ATOMIC;

                        uint32_t perquad =
                                is_load && !vir_in_nonuniform_control_flow(c)
                                ? GENERAL_TMU_LOOKUP_PER_QUAD
                                : GENERAL_TMU_LOOKUP_PER_PIXEL;
                        config = 0xffffff00 | tmu_op << 3 | perquad;

                        if (tmu_op == V3D_TMU_OP_WRITE_CMPXCHG_READ_FLUSH) {
                                config |= GENERAL_TMU_LOOKUP_TYPE_VEC2;
                        } else if (is_atomic || num_components == 1) {
                                config |= GENERAL_TMU_LOOKUP_TYPE_32BIT_UI;
                        } else {
                                config |= GENERAL_TMU_LOOKUP_TYPE_VEC2 +
                                          num_components - 2;
                        }
                }

                emit_tmu_general_address_write(c, mode, instr, config,
                                               dynamic_src, offset_src,
                                               base_offset, const_offset,
                                               &tmu_writes);

                assert(tmu_writes > 0);
                if (mode == MODE_COUNT) {
                        /* Make sure we won't exceed the 16-entry TMU
                         * fifo if each thread is storing at the same
                         * time.
                         */
                        while (tmu_writes > 16 / c->threads)
                                c->threads /= 2;

                        /* If pipelining this TMU operation would
                         * overflow TMU fifos, we need to flush.
                         */
                        if (ntq_tmu_fifo_overflow(c, dest_components))
                                ntq_flush_tmu(c);
                } else {
                        /* Delay emission of the thread switch and
                         * LDTMU/TMUWT until we really need to do it to
                         * improve pipelining.
                         */
                        const uint32_t component_mask =
                                (1 << dest_components) - 1;
                        ntq_add_pending_tmu_flush(c, &instr->dest,
                                                  component_mask);
                }
        }

        /* nir_lower_wrmasks should've ensured that any writemask on a store
         * operation only has consecutive bits set, in which case we should've
         * processed the full writemask above.
         */
        assert(writemask == 0);
}

static struct qreg *
ntq_init_ssa_def(struct v3d_compile *c, nir_ssa_def *def)
{
        struct qreg *qregs = ralloc_array(c->def_ht, struct qreg,
                                          def->num_components);
        _mesa_hash_table_insert(c->def_ht, def, qregs);
        return qregs;
}

static bool
is_ld_signal(const struct v3d_qpu_sig *sig)
{
        return (sig->ldunif ||
                sig->ldunifa ||
                sig->ldunifrf ||
                sig->ldunifarf ||
                sig->ldtmu ||
                sig->ldvary ||
                sig->ldvpm ||
                sig->ldtlb ||
                sig->ldtlbu);
}

static inline bool
is_ldunif_signal(const struct v3d_qpu_sig *sig)
{
        return sig->ldunif || sig->ldunifrf;
}

/**
 * This function is responsible for getting VIR results into the associated
 * storage for a NIR instruction.
 *
 * If it's a NIR SSA def, then we just set the associated hash table entry to
 * the new result.
 *
 * If it's a NIR reg, then we need to update the existing qreg assigned to the
 * NIR destination with the incoming value.  To do that without introducing
 * new MOVs, we require that the incoming qreg either be a uniform, or be
 * SSA-defined by the previous VIR instruction in the block and rewritable by
 * this function.  That lets us sneak ahead and insert the SF flag beforehand
 * (knowing that the previous instruction doesn't depend on flags) and rewrite
 * its destination to be the NIR reg's destination
 */
void
ntq_store_dest(struct v3d_compile *c, nir_dest *dest, int chan,
               struct qreg result)
{
        struct qinst *last_inst = NULL;
        if (!list_is_empty(&c->cur_block->instructions))
                last_inst = (struct qinst *)c->cur_block->instructions.prev;

        bool is_reused_uniform =
                is_ldunif_signal(&c->defs[result.index]->qpu.sig) &&
                last_inst != c->defs[result.index];

        assert(result.file == QFILE_TEMP && last_inst &&
               (last_inst == c->defs[result.index] || is_reused_uniform));

        if (dest->is_ssa) {
                assert(chan < dest->ssa.num_components);

                struct qreg *qregs;
                struct hash_entry *entry =
                        _mesa_hash_table_search(c->def_ht, &dest->ssa);

                if (entry)
                        qregs = entry->data;
                else
                        qregs = ntq_init_ssa_def(c, &dest->ssa);

                qregs[chan] = result;
        } else {
                nir_register *reg = dest->reg.reg;
                assert(dest->reg.base_offset == 0);
                assert(reg->num_array_elems == 0);
                struct hash_entry *entry =
                        _mesa_hash_table_search(c->def_ht, reg);
                struct qreg *qregs = entry->data;

                /* If the previous instruction can't be predicated for
                 * the store into the nir_register, then emit a MOV
                 * that can be.
                 */
                if (is_reused_uniform ||
                    (vir_in_nonuniform_control_flow(c) &&
                     is_ld_signal(&c->defs[last_inst->dst.index]->qpu.sig))) {
                        result = vir_MOV(c, result);
                        last_inst = c->defs[result.index];
                }

                /* We know they're both temps, so just rewrite index. */
                c->defs[last_inst->dst.index] = NULL;
                last_inst->dst.index = qregs[chan].index;

                /* If we're in control flow, then make this update of the reg
                 * conditional on the execution mask.
                 */
                if (vir_in_nonuniform_control_flow(c)) {
                        last_inst->dst.index = qregs[chan].index;

                        /* Set the flags to the current exec mask.
                         */
                        c->cursor = vir_before_inst(last_inst);
                        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), c->execute),
                                   V3D_QPU_PF_PUSHZ);
                        c->cursor = vir_after_inst(last_inst);

                        vir_set_cond(last_inst, V3D_QPU_COND_IFA);
                }
        }
}

/**
 * This looks up the qreg associated with a particular ssa/reg used as a source
 * in any instruction.
 *
 * It is expected that the definition for any NIR value read as a source has
 * been emitted by a previous instruction, however, in the case of TMU
 * operations we may have postponed emission of the thread switch and LDTMUs
 * required to read the TMU results until the results are actually used to
 * improve pipelining, which then would lead to us not finding them here
 * (for SSA defs) or finding them in the list of registers awaiting a TMU flush
 * (for registers), meaning that we need to flush outstanding TMU operations
 * to read the correct value.
 */
struct qreg
ntq_get_src(struct v3d_compile *c, nir_src src, int i)
{
        struct hash_entry *entry;
        if (src.is_ssa) {
                assert(i < src.ssa->num_components);

                entry = _mesa_hash_table_search(c->def_ht, src.ssa);
                if (!entry) {
                        ntq_flush_tmu(c);
                        entry = _mesa_hash_table_search(c->def_ht, src.ssa);
                }
        } else {
                nir_register *reg = src.reg.reg;
                assert(reg->num_array_elems == 0);
                assert(src.reg.base_offset == 0);
                assert(i < reg->num_components);

                if (_mesa_set_search(c->tmu.outstanding_regs, reg))
                        ntq_flush_tmu(c);
                entry = _mesa_hash_table_search(c->def_ht, reg);
        }
        assert(entry);

        struct qreg *qregs = entry->data;
        return qregs[i];
}

static struct qreg
ntq_get_alu_src(struct v3d_compile *c, nir_alu_instr *instr,
                unsigned src)
{
        assert(util_is_power_of_two_or_zero(instr->dest.write_mask));
        unsigned chan = ffs(instr->dest.write_mask) - 1;
        struct qreg r = ntq_get_src(c, instr->src[src].src,
                                    instr->src[src].swizzle[chan]);

        assert(!instr->src[src].abs);
        assert(!instr->src[src].negate);

        return r;
};

static struct qreg
ntq_minify(struct v3d_compile *c, struct qreg size, struct qreg level)
{
        return vir_MAX(c, vir_SHR(c, size, level), vir_uniform_ui(c, 1));
}

static void
ntq_emit_txs(struct v3d_compile *c, nir_tex_instr *instr)
{
        unsigned unit = instr->texture_index;
        int lod_index = nir_tex_instr_src_index(instr, nir_tex_src_lod);
        int dest_size = nir_tex_instr_dest_size(instr);

        struct qreg lod = c->undef;
        if (lod_index != -1)
                lod = ntq_get_src(c, instr->src[lod_index].src, 0);

        for (int i = 0; i < dest_size; i++) {
                assert(i < 3);
                enum quniform_contents contents;

                if (instr->is_array && i == dest_size - 1)
                        contents = QUNIFORM_TEXTURE_ARRAY_SIZE;
                else
                        contents = QUNIFORM_TEXTURE_WIDTH + i;

                struct qreg size = vir_uniform(c, contents, unit);

                switch (instr->sampler_dim) {
                case GLSL_SAMPLER_DIM_1D:
                case GLSL_SAMPLER_DIM_2D:
                case GLSL_SAMPLER_DIM_MS:
                case GLSL_SAMPLER_DIM_3D:
                case GLSL_SAMPLER_DIM_CUBE:
                case GLSL_SAMPLER_DIM_BUF:
                        /* Don't minify the array size. */
                        if (!(instr->is_array && i == dest_size - 1)) {
                                size = ntq_minify(c, size, lod);
                        }
                        break;

                case GLSL_SAMPLER_DIM_RECT:
                        /* There's no LOD field for rects */
                        break;

                default:
                        unreachable("Bad sampler type");
                }

                ntq_store_dest(c, &instr->dest, i, size);
        }
}

static void
ntq_emit_tex(struct v3d_compile *c, nir_tex_instr *instr)
{
        unsigned unit = instr->texture_index;

        /* Since each texture sampling op requires uploading uniforms to
         * reference the texture, there's no HW support for texture size and
         * you just upload uniforms containing the size.
         */
        switch (instr->op) {
        case nir_texop_query_levels:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_TEXTURE_LEVELS, unit));
                return;
        case nir_texop_texture_samples:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_TEXTURE_SAMPLES, unit));
                return;
        case nir_texop_txs:
                ntq_emit_txs(c, instr);
                return;
        default:
                break;
        }

        if (c->devinfo->ver >= 40)
                v3d40_vir_emit_tex(c, instr);
        else
                v3d33_vir_emit_tex(c, instr);
}

static struct qreg
ntq_fsincos(struct v3d_compile *c, struct qreg src, bool is_cos)
{
        struct qreg input = vir_FMUL(c, src, vir_uniform_f(c, 1.0f / M_PI));
        if (is_cos)
                input = vir_FADD(c, input, vir_uniform_f(c, 0.5));

        struct qreg periods = vir_FROUND(c, input);
        struct qreg sin_output = vir_SIN(c, vir_FSUB(c, input, periods));
        return vir_XOR(c, sin_output, vir_SHL(c,
                                              vir_FTOIN(c, periods),
                                              vir_uniform_ui(c, -1)));
}

static struct qreg
ntq_fsign(struct v3d_compile *c, struct qreg src)
{
        struct qreg t = vir_get_temp(c);

        vir_MOV_dest(c, t, vir_uniform_f(c, 0.0));
        vir_set_pf(c, vir_FMOV_dest(c, vir_nop_reg(), src), V3D_QPU_PF_PUSHZ);
        vir_MOV_cond(c, V3D_QPU_COND_IFNA, t, vir_uniform_f(c, 1.0));
        vir_set_pf(c, vir_FMOV_dest(c, vir_nop_reg(), src), V3D_QPU_PF_PUSHN);
        vir_MOV_cond(c, V3D_QPU_COND_IFA, t, vir_uniform_f(c, -1.0));
        return vir_MOV(c, t);
}

static void
emit_fragcoord_input(struct v3d_compile *c, int attr)
{
        c->inputs[attr * 4 + 0] = vir_FXCD(c);
        c->inputs[attr * 4 + 1] = vir_FYCD(c);
        c->inputs[attr * 4 + 2] = c->payload_z;
        c->inputs[attr * 4 + 3] = vir_RECIP(c, c->payload_w);
}

static struct qreg
emit_smooth_varying(struct v3d_compile *c,
                    struct qreg vary, struct qreg w, struct qreg r5)
{
        return vir_FADD(c, vir_FMUL(c, vary, w), r5);
}

static struct qreg
emit_noperspective_varying(struct v3d_compile *c,
                           struct qreg vary, struct qreg r5)
{
        return vir_FADD(c, vir_MOV(c, vary), r5);
}

static struct qreg
emit_flat_varying(struct v3d_compile *c,
                  struct qreg vary, struct qreg r5)
{
        vir_MOV_dest(c, c->undef, vary);
        return vir_MOV(c, r5);
}

static struct qreg
emit_fragment_varying(struct v3d_compile *c, nir_variable *var,
                      int8_t input_idx, uint8_t swizzle, int array_index)
{
        struct qreg r3 = vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_R3);
        struct qreg r5 = vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_R5);

        struct qinst *ldvary = NULL;
        struct qreg vary;
        if (c->devinfo->ver >= 41) {
                ldvary = vir_add_inst(V3D_QPU_A_NOP, c->undef,
                                      c->undef, c->undef);
                ldvary->qpu.sig.ldvary = true;
                vary = vir_emit_def(c, ldvary);
        } else {
                vir_NOP(c)->qpu.sig.ldvary = true;
                vary = r3;
        }

        /* Store the input value before interpolation so we can implement
         * GLSL's interpolateAt functions if the shader uses them.
         */
        if (input_idx >= 0) {
                assert(var);
                c->interp[input_idx].vp = vary;
                c->interp[input_idx].C = vir_MOV(c, r5);
                c->interp[input_idx].mode = var->data.interpolation;
        }

        /* For gl_PointCoord input or distance along a line, we'll be called
         * with no nir_variable, and we don't count toward VPM size so we
         * don't track an input slot.
         */
        if (!var) {
                assert(input_idx < 0);
                return emit_smooth_varying(c, vary, c->payload_w, r5);
        }

        int i = c->num_inputs++;
        c->input_slots[i] =
                v3d_slot_from_slot_and_component(var->data.location +
                                                 array_index, swizzle);

        struct qreg result;
        switch (var->data.interpolation) {
        case INTERP_MODE_NONE:
        case INTERP_MODE_SMOOTH:
                if (var->data.centroid) {
                        BITSET_SET(c->centroid_flags, i);
                        result = emit_smooth_varying(c, vary,
                                                     c->payload_w_centroid, r5);
                } else {
                        result = emit_smooth_varying(c, vary, c->payload_w, r5);
                }
                break;

        case INTERP_MODE_NOPERSPECTIVE:
                BITSET_SET(c->noperspective_flags, i);
                result = emit_noperspective_varying(c, vary, r5);
                break;

        case INTERP_MODE_FLAT:
                BITSET_SET(c->flat_shade_flags, i);
                result = emit_flat_varying(c, vary, r5);
                break;

        default:
                unreachable("Bad interp mode");
        }

        if (input_idx >= 0)
                c->inputs[input_idx] = result;
        return result;
}

static void
emit_fragment_input(struct v3d_compile *c, int base_attr, nir_variable *var,
                    int array_index, unsigned nelem)
{
        for (int i = 0; i < nelem ; i++) {
                int chan = var->data.location_frac + i;
                int input_idx = (base_attr + array_index) * 4 + chan;
                emit_fragment_varying(c, var, input_idx, chan, array_index);
        }
}

static void
emit_compact_fragment_input(struct v3d_compile *c, int attr, nir_variable *var,
                            int array_index)
{
        /* Compact variables are scalar arrays where each set of 4 elements
         * consumes a single location.
         */
        int loc_offset = array_index / 4;
        int chan = var->data.location_frac + array_index % 4;
        int input_idx = (attr + loc_offset) * 4  + chan;
        emit_fragment_varying(c, var, input_idx, chan, loc_offset);
}

static void
add_output(struct v3d_compile *c,
           uint32_t decl_offset,
           uint8_t slot,
           uint8_t swizzle)
{
        uint32_t old_array_size = c->outputs_array_size;
        resize_qreg_array(c, &c->outputs, &c->outputs_array_size,
                          decl_offset + 1);

        if (old_array_size != c->outputs_array_size) {
                c->output_slots = reralloc(c,
                                           c->output_slots,
                                           struct v3d_varying_slot,
                                           c->outputs_array_size);
        }

        c->output_slots[decl_offset] =
                v3d_slot_from_slot_and_component(slot, swizzle);
}

/**
 * If compare_instr is a valid comparison instruction, emits the
 * compare_instr's comparison and returns the sel_instr's return value based
 * on the compare_instr's result.
 */
static bool
ntq_emit_comparison(struct v3d_compile *c,
                    nir_alu_instr *compare_instr,
                    enum v3d_qpu_cond *out_cond)
{
        struct qreg src0 = ntq_get_alu_src(c, compare_instr, 0);
        struct qreg src1;
        if (nir_op_infos[compare_instr->op].num_inputs > 1)
                src1 = ntq_get_alu_src(c, compare_instr, 1);
        bool cond_invert = false;
        struct qreg nop = vir_nop_reg();

        switch (compare_instr->op) {
        case nir_op_feq32:
        case nir_op_seq:
                vir_set_pf(c, vir_FCMP_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHZ);
                break;
        case nir_op_ieq32:
                vir_set_pf(c, vir_XOR_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHZ);
                break;

        case nir_op_fneu32:
        case nir_op_sne:
                vir_set_pf(c, vir_FCMP_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHZ);
                cond_invert = true;
                break;
        case nir_op_ine32:
                vir_set_pf(c, vir_XOR_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHZ);
                cond_invert = true;
                break;

        case nir_op_fge32:
        case nir_op_sge:
                vir_set_pf(c, vir_FCMP_dest(c, nop, src1, src0), V3D_QPU_PF_PUSHC);
                break;
        case nir_op_ige32:
                vir_set_pf(c, vir_MIN_dest(c, nop, src1, src0), V3D_QPU_PF_PUSHC);
                cond_invert = true;
                break;
        case nir_op_uge32:
                vir_set_pf(c, vir_SUB_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHC);
                cond_invert = true;
                break;

        case nir_op_slt:
        case nir_op_flt32:
                vir_set_pf(c, vir_FCMP_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHN);
                break;
        case nir_op_ilt32:
                vir_set_pf(c, vir_MIN_dest(c, nop, src1, src0), V3D_QPU_PF_PUSHC);
                break;
        case nir_op_ult32:
                vir_set_pf(c, vir_SUB_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHC);
                break;

        case nir_op_i2b32:
                vir_set_pf(c, vir_MOV_dest(c, nop, src0), V3D_QPU_PF_PUSHZ);
                cond_invert = true;
                break;

        case nir_op_f2b32:
                vir_set_pf(c, vir_FMOV_dest(c, nop, src0), V3D_QPU_PF_PUSHZ);
                cond_invert = true;
                break;

        default:
                return false;
        }

        *out_cond = cond_invert ? V3D_QPU_COND_IFNA : V3D_QPU_COND_IFA;

        return true;
}

/* Finds an ALU instruction that generates our src value that could
 * (potentially) be greedily emitted in the consuming instruction.
 */
static struct nir_alu_instr *
ntq_get_alu_parent(nir_src src)
{
        if (!src.is_ssa || src.ssa->parent_instr->type != nir_instr_type_alu)
                return NULL;
        nir_alu_instr *instr = nir_instr_as_alu(src.ssa->parent_instr);
        if (!instr)
                return NULL;

        /* If the ALU instr's srcs are non-SSA, then we would have to avoid
         * moving emission of the ALU instr down past another write of the
         * src.
         */
        for (int i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
                if (!instr->src[i].src.is_ssa)
                        return NULL;
        }

        return instr;
}

/* Turns a NIR bool into a condition code to predicate on. */
static enum v3d_qpu_cond
ntq_emit_bool_to_cond(struct v3d_compile *c, nir_src src)
{
        struct qreg qsrc = ntq_get_src(c, src, 0);
        /* skip if we already have src in the flags */
        if (qsrc.file == QFILE_TEMP && c->flags_temp == qsrc.index)
                return c->flags_cond;

        nir_alu_instr *compare = ntq_get_alu_parent(src);
        if (!compare)
                goto out;

        enum v3d_qpu_cond cond;
        if (ntq_emit_comparison(c, compare, &cond))
                return cond;

out:

        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), ntq_get_src(c, src, 0)),
                   V3D_QPU_PF_PUSHZ);
        return V3D_QPU_COND_IFNA;
}

static struct qreg
ntq_emit_cond_to_bool(struct v3d_compile *c, enum v3d_qpu_cond cond)
{
        struct qreg result =
                vir_MOV(c, vir_SEL(c, cond,
                                   vir_uniform_ui(c, ~0),
                                   vir_uniform_ui(c, 0)));
        c->flags_temp = result.index;
        c->flags_cond = cond;
        return result;
}

static void
ntq_emit_alu(struct v3d_compile *c, nir_alu_instr *instr)
{
        /* This should always be lowered to ALU operations for V3D. */
        assert(!instr->dest.saturate);

        /* Vectors are special in that they have non-scalarized writemasks,
         * and just take the first swizzle channel for each argument in order
         * into each writemask channel.
         */
        if (instr->op == nir_op_vec2 ||
            instr->op == nir_op_vec3 ||
            instr->op == nir_op_vec4) {
                struct qreg srcs[4];
                for (int i = 0; i < nir_op_infos[instr->op].num_inputs; i++)
                        srcs[i] = ntq_get_src(c, instr->src[i].src,
                                              instr->src[i].swizzle[0]);
                for (int i = 0; i < nir_op_infos[instr->op].num_inputs; i++)
                        ntq_store_dest(c, &instr->dest.dest, i,
                                       vir_MOV(c, srcs[i]));
                return;
        }

        /* General case: We can just grab the one used channel per src. */
        struct qreg src[nir_op_infos[instr->op].num_inputs];
        for (int i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
                src[i] = ntq_get_alu_src(c, instr, i);
        }

        struct qreg result;

        switch (instr->op) {
        case nir_op_mov:
                result = vir_MOV(c, src[0]);
                break;

        case nir_op_fneg:
                result = vir_XOR(c, src[0], vir_uniform_ui(c, 1 << 31));
                break;
        case nir_op_ineg:
                result = vir_NEG(c, src[0]);
                break;

        case nir_op_fmul:
                result = vir_FMUL(c, src[0], src[1]);
                break;
        case nir_op_fadd:
                result = vir_FADD(c, src[0], src[1]);
                break;
        case nir_op_fsub:
                result = vir_FSUB(c, src[0], src[1]);
                break;
        case nir_op_fmin:
                result = vir_FMIN(c, src[0], src[1]);
                break;
        case nir_op_fmax:
                result = vir_FMAX(c, src[0], src[1]);
                break;

        case nir_op_f2i32: {
                nir_alu_instr *src0_alu = ntq_get_alu_parent(instr->src[0].src);
                if (src0_alu && src0_alu->op == nir_op_fround_even) {
                        result = vir_FTOIN(c, ntq_get_alu_src(c, src0_alu, 0));
                } else {
                        result = vir_FTOIZ(c, src[0]);
                }
                break;
        }

        case nir_op_f2u32:
                result = vir_FTOUZ(c, src[0]);
                break;
        case nir_op_i2f32:
                result = vir_ITOF(c, src[0]);
                break;
        case nir_op_u2f32:
                result = vir_UTOF(c, src[0]);
                break;
        case nir_op_b2f32:
                result = vir_AND(c, src[0], vir_uniform_f(c, 1.0));
                break;
        case nir_op_b2i32:
                result = vir_AND(c, src[0], vir_uniform_ui(c, 1));
                break;

        case nir_op_iadd:
                result = vir_ADD(c, src[0], src[1]);
                break;
        case nir_op_ushr:
                result = vir_SHR(c, src[0], src[1]);
                break;
        case nir_op_isub:
                result = vir_SUB(c, src[0], src[1]);
                break;
        case nir_op_ishr:
                result = vir_ASR(c, src[0], src[1]);
                break;
        case nir_op_ishl:
                result = vir_SHL(c, src[0], src[1]);
                break;
        case nir_op_imin:
                result = vir_MIN(c, src[0], src[1]);
                break;
        case nir_op_umin:
                result = vir_UMIN(c, src[0], src[1]);
                break;
        case nir_op_imax:
                result = vir_MAX(c, src[0], src[1]);
                break;
        case nir_op_umax:
                result = vir_UMAX(c, src[0], src[1]);
                break;
        case nir_op_iand:
                result = vir_AND(c, src[0], src[1]);
                break;
        case nir_op_ior:
                result = vir_OR(c, src[0], src[1]);
                break;
        case nir_op_ixor:
                result = vir_XOR(c, src[0], src[1]);
                break;
        case nir_op_inot:
                result = vir_NOT(c, src[0]);
                break;

        case nir_op_ufind_msb:
                result = vir_SUB(c, vir_uniform_ui(c, 31), vir_CLZ(c, src[0]));
                break;

        case nir_op_imul:
                result = vir_UMUL(c, src[0], src[1]);
                break;

        case nir_op_seq:
        case nir_op_sne:
        case nir_op_sge:
        case nir_op_slt: {
                enum v3d_qpu_cond cond;
                ASSERTED bool ok = ntq_emit_comparison(c, instr, &cond);
                assert(ok);
                result = vir_MOV(c, vir_SEL(c, cond,
                                            vir_uniform_f(c, 1.0),
                                            vir_uniform_f(c, 0.0)));
                c->flags_temp = result.index;
                c->flags_cond = cond;
                break;
        }

        case nir_op_i2b32:
        case nir_op_f2b32:
        case nir_op_feq32:
        case nir_op_fneu32:
        case nir_op_fge32:
        case nir_op_flt32:
        case nir_op_ieq32:
        case nir_op_ine32:
        case nir_op_ige32:
        case nir_op_uge32:
        case nir_op_ilt32:
        case nir_op_ult32: {
                enum v3d_qpu_cond cond;
                ASSERTED bool ok = ntq_emit_comparison(c, instr, &cond);
                assert(ok);
                result = ntq_emit_cond_to_bool(c, cond);
                break;
        }

        case nir_op_b32csel:
                result = vir_MOV(c,
                                 vir_SEL(c,
                                         ntq_emit_bool_to_cond(c, instr->src[0].src),
                                         src[1], src[2]));
                break;

        case nir_op_fcsel:
                vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), src[0]),
                           V3D_QPU_PF_PUSHZ);
                result = vir_MOV(c, vir_SEL(c, V3D_QPU_COND_IFNA,
                                            src[1], src[2]));
                break;

        case nir_op_frcp:
                result = vir_RECIP(c, src[0]);
                break;
        case nir_op_frsq:
                result = vir_RSQRT(c, src[0]);
                break;
        case nir_op_fexp2:
                result = vir_EXP(c, src[0]);
                break;
        case nir_op_flog2:
                result = vir_LOG(c, src[0]);
                break;

        case nir_op_fceil:
                result = vir_FCEIL(c, src[0]);
                break;
        case nir_op_ffloor:
                result = vir_FFLOOR(c, src[0]);
                break;
        case nir_op_fround_even:
                result = vir_FROUND(c, src[0]);
                break;
        case nir_op_ftrunc:
                result = vir_FTRUNC(c, src[0]);
                break;

        case nir_op_fsin:
                result = ntq_fsincos(c, src[0], false);
                break;
        case nir_op_fcos:
                result = ntq_fsincos(c, src[0], true);
                break;

        case nir_op_fsign:
                result = ntq_fsign(c, src[0]);
                break;

        case nir_op_fabs: {
                result = vir_FMOV(c, src[0]);
                vir_set_unpack(c->defs[result.index], 0, V3D_QPU_UNPACK_ABS);
                break;
        }

        case nir_op_iabs:
                result = vir_MAX(c, src[0], vir_NEG(c, src[0]));
                break;

        case nir_op_fddx:
        case nir_op_fddx_coarse:
        case nir_op_fddx_fine:
                result = vir_FDX(c, src[0]);
                break;

        case nir_op_fddy:
        case nir_op_fddy_coarse:
        case nir_op_fddy_fine:
                result = vir_FDY(c, src[0]);
                break;

        case nir_op_uadd_carry:
                vir_set_pf(c, vir_ADD_dest(c, vir_nop_reg(), src[0], src[1]),
                           V3D_QPU_PF_PUSHC);
                result = ntq_emit_cond_to_bool(c, V3D_QPU_COND_IFA);
                break;

        case nir_op_pack_half_2x16_split:
                result = vir_VFPACK(c, src[0], src[1]);
                break;

        case nir_op_unpack_half_2x16_split_x:
                result = vir_FMOV(c, src[0]);
                vir_set_unpack(c->defs[result.index], 0, V3D_QPU_UNPACK_L);
                break;

        case nir_op_unpack_half_2x16_split_y:
                result = vir_FMOV(c, src[0]);
                vir_set_unpack(c->defs[result.index], 0, V3D_QPU_UNPACK_H);
                break;

        case nir_op_fquantize2f16: {
                /* F32 -> F16 -> F32 conversion */
                struct qreg tmp = vir_FMOV(c, src[0]);
                vir_set_pack(c->defs[tmp.index], V3D_QPU_PACK_L);
                tmp = vir_FMOV(c, tmp);
                vir_set_unpack(c->defs[tmp.index], 0, V3D_QPU_UNPACK_L);

                /* Check for denorm */
                struct qreg abs_src = vir_FMOV(c, src[0]);
                vir_set_unpack(c->defs[abs_src.index], 0, V3D_QPU_UNPACK_ABS);
                struct qreg threshold = vir_uniform_f(c, ldexpf(1.0, -14));
                vir_set_pf(c, vir_FCMP_dest(c, vir_nop_reg(), abs_src, threshold),
                                         V3D_QPU_PF_PUSHC);

                /* Return +/-0 for denorms */
                struct qreg zero =
                        vir_AND(c, src[0], vir_uniform_ui(c, 0x80000000));
                result = vir_FMOV(c, vir_SEL(c, V3D_QPU_COND_IFNA, tmp, zero));
                break;
        }

        default:
                fprintf(stderr, "unknown NIR ALU inst: ");
                nir_print_instr(&instr->instr, stderr);
                fprintf(stderr, "\n");
                abort();
        }

        /* We have a scalar result, so the instruction should only have a
         * single channel written to.
         */
        assert(util_is_power_of_two_or_zero(instr->dest.write_mask));
        ntq_store_dest(c, &instr->dest.dest,
                       ffs(instr->dest.write_mask) - 1, result);
}

/* Each TLB read/write setup (a render target or depth buffer) takes an 8-bit
 * specifier.  They come from a register that's preloaded with 0xffffffff
 * (0xff gets you normal vec4 f16 RT0 writes), and when one is neaded the low
 * 8 bits are shifted off the bottom and 0xff shifted in from the top.
 */
#define TLB_TYPE_F16_COLOR         (3 << 6)
#define TLB_TYPE_I32_COLOR         (1 << 6)
#define TLB_TYPE_F32_COLOR         (0 << 6)
#define TLB_RENDER_TARGET_SHIFT    3 /* Reversed!  7 = RT 0, 0 = RT 7. */
#define TLB_SAMPLE_MODE_PER_SAMPLE (0 << 2)
#define TLB_SAMPLE_MODE_PER_PIXEL  (1 << 2)
#define TLB_F16_SWAP_HI_LO         (1 << 1)
#define TLB_VEC_SIZE_4_F16         (1 << 0)
#define TLB_VEC_SIZE_2_F16         (0 << 0)
#define TLB_VEC_SIZE_MINUS_1_SHIFT 0

/* Triggers Z/Stencil testing, used when the shader state's "FS modifies Z"
 * flag is set.
 */
#define TLB_TYPE_DEPTH             ((2 << 6) | (0 << 4))
#define TLB_DEPTH_TYPE_INVARIANT   (0 << 2) /* Unmodified sideband input used */
#define TLB_DEPTH_TYPE_PER_PIXEL   (1 << 2) /* QPU result used */
#define TLB_V42_DEPTH_TYPE_INVARIANT   (0 << 3) /* Unmodified sideband input used */
#define TLB_V42_DEPTH_TYPE_PER_PIXEL   (1 << 3) /* QPU result used */

/* Stencil is a single 32-bit write. */
#define TLB_TYPE_STENCIL_ALPHA     ((2 << 6) | (1 << 4))

static void
vir_emit_tlb_color_write(struct v3d_compile *c, unsigned rt)
{
        if (!(c->fs_key->cbufs & (1 << rt)) || !c->output_color_var[rt])
                return;

        struct qreg tlb_reg = vir_magic_reg(V3D_QPU_WADDR_TLB);
        struct qreg tlbu_reg = vir_magic_reg(V3D_QPU_WADDR_TLBU);

        nir_variable *var = c->output_color_var[rt];
        int num_components = glsl_get_vector_elements(var->type);
        uint32_t conf = 0xffffff00;
        struct qinst *inst;

        conf |= c->msaa_per_sample_output ? TLB_SAMPLE_MODE_PER_SAMPLE :
                                            TLB_SAMPLE_MODE_PER_PIXEL;
        conf |= (7 - rt) << TLB_RENDER_TARGET_SHIFT;

        if (c->fs_key->swap_color_rb & (1 << rt))
                num_components = MAX2(num_components, 3);
        assert(num_components != 0);

        enum glsl_base_type type = glsl_get_base_type(var->type);
        bool is_int_format = type == GLSL_TYPE_INT || type == GLSL_TYPE_UINT;
        bool is_32b_tlb_format = is_int_format ||
                                 (c->fs_key->f32_color_rb & (1 << rt));

        if (is_int_format) {
                /* The F32 vs I32 distinction was dropped in 4.2. */
                if (c->devinfo->ver < 42)
                        conf |= TLB_TYPE_I32_COLOR;
                else
                        conf |= TLB_TYPE_F32_COLOR;
                conf |= ((num_components - 1) << TLB_VEC_SIZE_MINUS_1_SHIFT);
        } else {
                if (c->fs_key->f32_color_rb & (1 << rt)) {
                        conf |= TLB_TYPE_F32_COLOR;
                        conf |= ((num_components - 1) <<
                                TLB_VEC_SIZE_MINUS_1_SHIFT);
                } else {
                        conf |= TLB_TYPE_F16_COLOR;
                        conf |= TLB_F16_SWAP_HI_LO;
                        if (num_components >= 3)
                                conf |= TLB_VEC_SIZE_4_F16;
                        else
                                conf |= TLB_VEC_SIZE_2_F16;
                }
        }

        int num_samples = c->msaa_per_sample_output ? V3D_MAX_SAMPLES : 1;
        for (int i = 0; i < num_samples; i++) {
                struct qreg *color = c->msaa_per_sample_output ?
                        &c->sample_colors[(rt * V3D_MAX_SAMPLES + i) * 4] :
                        &c->outputs[var->data.driver_location * 4];

                struct qreg r = color[0];
                struct qreg g = color[1];
                struct qreg b = color[2];
                struct qreg a = color[3];

                if (c->fs_key->swap_color_rb & (1 << rt))  {
                        r = color[2];
                        b = color[0];
                }

                if (c->fs_key->sample_alpha_to_one)
                        a = vir_uniform_f(c, 1.0);

                if (is_32b_tlb_format) {
                        if (i == 0) {
                                inst = vir_MOV_dest(c, tlbu_reg, r);
                                inst->uniform =
                                        vir_get_uniform_index(c,
                                                              QUNIFORM_CONSTANT,
                                                              conf);
                        } else {
                                vir_MOV_dest(c, tlb_reg, r);
                        }

                        if (num_components >= 2)
                                vir_MOV_dest(c, tlb_reg, g);
                        if (num_components >= 3)
                                vir_MOV_dest(c, tlb_reg, b);
                        if (num_components >= 4)
                                vir_MOV_dest(c, tlb_reg, a);
                } else {
                        inst = vir_VFPACK_dest(c, tlb_reg, r, g);
                        if (conf != ~0 && i == 0) {
                                inst->dst = tlbu_reg;
                                inst->uniform =
                                        vir_get_uniform_index(c,
                                                              QUNIFORM_CONSTANT,
                                                              conf);
                        }

                        if (num_components >= 3)
                                vir_VFPACK_dest(c, tlb_reg, b, a);
                }
        }
}

static void
emit_frag_end(struct v3d_compile *c)
{
        /* If the shader has no non-TLB side effects and doesn't write Z
         * we can promote it to enabling early_fragment_tests even
         * if the user didn't.
         */
        if (c->output_position_index == -1 &&
            !(c->s->info.num_images || c->s->info.num_ssbos)) {
                c->s->info.fs.early_fragment_tests = true;
        }

        if (c->output_sample_mask_index != -1) {
                vir_SETMSF_dest(c, vir_nop_reg(),
                                vir_AND(c,
                                        vir_MSF(c),
                                        c->outputs[c->output_sample_mask_index]));
        }

        bool has_any_tlb_color_write = false;
        for (int rt = 0; rt < V3D_MAX_DRAW_BUFFERS; rt++) {
                if (c->fs_key->cbufs & (1 << rt) && c->output_color_var[rt])
                        has_any_tlb_color_write = true;
        }

        if (c->fs_key->sample_alpha_to_coverage && c->output_color_var[0]) {
                struct nir_variable *var = c->output_color_var[0];
                struct qreg *color = &c->outputs[var->data.driver_location * 4];

                vir_SETMSF_dest(c, vir_nop_reg(),
                                vir_AND(c,
                                        vir_MSF(c),
                                        vir_FTOC(c, color[3])));
        }

        struct qreg tlbu_reg = vir_magic_reg(V3D_QPU_WADDR_TLBU);
        if (c->output_position_index != -1 &&
            !c->s->info.fs.early_fragment_tests) {
                struct qinst *inst = vir_MOV_dest(c, tlbu_reg,
                                                  c->outputs[c->output_position_index]);
                uint8_t tlb_specifier = TLB_TYPE_DEPTH;

                if (c->devinfo->ver >= 42) {
                        tlb_specifier |= (TLB_V42_DEPTH_TYPE_PER_PIXEL |
                                          TLB_SAMPLE_MODE_PER_PIXEL);
                } else
                        tlb_specifier |= TLB_DEPTH_TYPE_PER_PIXEL;

                inst->uniform = vir_get_uniform_index(c, QUNIFORM_CONSTANT,
                                                      tlb_specifier |
                                                      0xffffff00);
                c->writes_z = true;
        } else if (c->s->info.fs.uses_discard ||
                   !c->s->info.fs.early_fragment_tests ||
                   c->fs_key->sample_alpha_to_coverage ||
                   !has_any_tlb_color_write) {
                /* Emit passthrough Z if it needed to be delayed until shader
                 * end due to potential discards.
                 *
                 * Since (single-threaded) fragment shaders always need a TLB
                 * write, emit passthrouh Z if we didn't have any color
                 * buffers and flag us as potentially discarding, so that we
                 * can use Z as the TLB write.
                 */
                c->s->info.fs.uses_discard = true;

                struct qinst *inst = vir_MOV_dest(c, tlbu_reg,
                                                  vir_nop_reg());
                uint8_t tlb_specifier = TLB_TYPE_DEPTH;

                if (c->devinfo->ver >= 42) {
                        /* The spec says the PER_PIXEL flag is ignored for
                         * invariant writes, but the simulator demands it.
                         */
                        tlb_specifier |= (TLB_V42_DEPTH_TYPE_INVARIANT |
                                          TLB_SAMPLE_MODE_PER_PIXEL);
                } else {
                        tlb_specifier |= TLB_DEPTH_TYPE_INVARIANT;
                }

                inst->uniform = vir_get_uniform_index(c,
                                                      QUNIFORM_CONSTANT,
                                                      tlb_specifier |
                                                      0xffffff00);
                c->writes_z = true;
        }

        /* XXX: Performance improvement: Merge Z write and color writes TLB
         * uniform setup
         */
        for (int rt = 0; rt < V3D_MAX_DRAW_BUFFERS; rt++)
                vir_emit_tlb_color_write(c, rt);
}

static inline void
vir_VPM_WRITE_indirect(struct v3d_compile *c,
                       struct qreg val,
                       struct qreg vpm_index,
                       bool uniform_vpm_index)
{
        assert(c->devinfo->ver >= 40);
        if (uniform_vpm_index)
                vir_STVPMV(c, vpm_index, val);
        else
                vir_STVPMD(c, vpm_index, val);
}

static void
vir_VPM_WRITE(struct v3d_compile *c, struct qreg val, uint32_t vpm_index)
{
        if (c->devinfo->ver >= 40) {
                vir_VPM_WRITE_indirect(c, val,
                                       vir_uniform_ui(c, vpm_index), true);
        } else {
                /* XXX: v3d33_vir_vpm_write_setup(c); */
                vir_MOV_dest(c, vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_VPM), val);
        }
}

static void
emit_vert_end(struct v3d_compile *c)
{
        /* GFXH-1684: VPM writes need to be complete by the end of the shader.
         */
        if (c->devinfo->ver >= 40 && c->devinfo->ver <= 42)
                vir_VPMWT(c);
}

static void
emit_geom_end(struct v3d_compile *c)
{
        /* GFXH-1684: VPM writes need to be complete by the end of the shader.
         */
        if (c->devinfo->ver >= 40 && c->devinfo->ver <= 42)
                vir_VPMWT(c);
}

static bool
mem_vectorize_callback(unsigned align_mul, unsigned align_offset,
                       unsigned bit_size,
                       unsigned num_components,
                       nir_intrinsic_instr *low,
                       nir_intrinsic_instr *high,
                       void *data)
{
        /* Our backend is 32-bit only at present */
        if (bit_size != 32)
                return false;

        if (align_mul % 4 != 0 || align_offset % 4 != 0)
                return false;

        /* Vector accesses wrap at 16-byte boundaries so we can't vectorize
         * if the resulting vector crosses a 16-byte boundary.
         */
        assert(util_is_power_of_two_nonzero(align_mul));
        align_mul = MIN2(align_mul, 16);
        align_offset &= 0xf;
        if (16 - align_mul + align_offset + num_components * 4 > 16)
                return false;

        return true;
}

void
v3d_optimize_nir(struct v3d_compile *c, struct nir_shader *s)
{
        bool progress;
        unsigned lower_flrp =
                (s->options->lower_flrp16 ? 16 : 0) |
                (s->options->lower_flrp32 ? 32 : 0) |
                (s->options->lower_flrp64 ? 64 : 0);

        do {
                progress = false;

                NIR_PASS_V(s, nir_lower_vars_to_ssa);
                NIR_PASS(progress, s, nir_lower_alu_to_scalar, NULL, NULL);
                NIR_PASS(progress, s, nir_lower_phis_to_scalar, false);
                NIR_PASS(progress, s, nir_copy_prop);
                NIR_PASS(progress, s, nir_opt_remove_phis);
                NIR_PASS(progress, s, nir_opt_dce);
                NIR_PASS(progress, s, nir_opt_dead_cf);
                NIR_PASS(progress, s, nir_opt_cse);
                NIR_PASS(progress, s, nir_opt_peephole_select, 8, true, true);
                NIR_PASS(progress, s, nir_opt_algebraic);
                NIR_PASS(progress, s, nir_opt_constant_folding);

                nir_load_store_vectorize_options vectorize_opts = {
                        .modes = nir_var_mem_ssbo | nir_var_mem_ubo |
                                 nir_var_mem_push_const | nir_var_mem_shared |
                                 nir_var_mem_global,
                        .callback = mem_vectorize_callback,
                        .robust_modes = 0,
                };
                NIR_PASS(progress, s, nir_opt_load_store_vectorize, &vectorize_opts);

                if (lower_flrp != 0) {
                        bool lower_flrp_progress = false;

                        NIR_PASS(lower_flrp_progress, s, nir_lower_flrp,
                                 lower_flrp,
                                 false /* always_precise */);
                        if (lower_flrp_progress) {
                                NIR_PASS(progress, s, nir_opt_constant_folding);
                                progress = true;
                        }

                        /* Nothing should rematerialize any flrps, so we only
                         * need to do this lowering once.
                         */
                        lower_flrp = 0;
                }

                NIR_PASS(progress, s, nir_opt_undef);
                NIR_PASS(progress, s, nir_lower_undef_to_zero);

                if (c && !c->disable_loop_unrolling &&
                    s->options->max_unroll_iterations > 0) {
                       bool local_progress = false;
                       NIR_PASS(local_progress, s, nir_opt_loop_unroll);
                       c->unrolled_any_loops |= local_progress;
                       progress |= local_progress;
                }
        } while (progress);

        nir_move_options sink_opts =
                nir_move_const_undef | nir_move_comparisons | nir_move_copies |
                nir_move_load_ubo;
        NIR_PASS(progress, s, nir_opt_sink, sink_opts);

        NIR_PASS(progress, s, nir_opt_move, nir_move_load_ubo);
}

static int
driver_location_compare(const nir_variable *a, const nir_variable *b)
{
        return a->data.driver_location == b->data.driver_location ?
               a->data.location_frac - b->data.location_frac :
               a->data.driver_location - b->data.driver_location;
}

static struct qreg
ntq_emit_vpm_read(struct v3d_compile *c,
                  uint32_t *num_components_queued,
                  uint32_t *remaining,
                  uint32_t vpm_index)
{
        struct qreg vpm = vir_reg(QFILE_VPM, vpm_index);

        if (c->devinfo->ver >= 40 ) {
                return vir_LDVPMV_IN(c,
                                     vir_uniform_ui(c,
                                                    (*num_components_queued)++));
        }

        if (*num_components_queued != 0) {
                (*num_components_queued)--;
                return vir_MOV(c, vpm);
        }

        uint32_t num_components = MIN2(*remaining, 32);

        v3d33_vir_vpm_read_setup(c, num_components);

        *num_components_queued = num_components - 1;
        *remaining -= num_components;

        return vir_MOV(c, vpm);
}

static void
ntq_setup_vs_inputs(struct v3d_compile *c)
{
        /* Figure out how many components of each vertex attribute the shader
         * uses.  Each variable should have been split to individual
         * components and unused ones DCEed.  The vertex fetcher will load
         * from the start of the attribute to the number of components we
         * declare we need in c->vattr_sizes[].
         *
         * BGRA vertex attributes are a bit special: since we implement these
         * as RGBA swapping R/B components we always need at least 3 components
         * if component 0 is read.
         */
        nir_foreach_shader_in_variable(var, c->s) {
                /* No VS attribute array support. */
                assert(MAX2(glsl_get_length(var->type), 1) == 1);

                unsigned loc = var->data.driver_location;
                int start_component = var->data.location_frac;
                int num_components = glsl_get_components(var->type);

                c->vattr_sizes[loc] = MAX2(c->vattr_sizes[loc],
                                           start_component + num_components);

                /* Handle BGRA inputs */
                if (start_component == 0 &&
                    c->vs_key->va_swap_rb_mask & (1 << var->data.location)) {
                        c->vattr_sizes[loc] = MAX2(3, c->vattr_sizes[loc]);
                }
        }

        unsigned num_components = 0;
        uint32_t vpm_components_queued = 0;
        bool uses_iid = BITSET_TEST(c->s->info.system_values_read,
                                    SYSTEM_VALUE_INSTANCE_ID) ||
                        BITSET_TEST(c->s->info.system_values_read,
                                    SYSTEM_VALUE_INSTANCE_INDEX);
        bool uses_biid = BITSET_TEST(c->s->info.system_values_read,
                                     SYSTEM_VALUE_BASE_INSTANCE);
        bool uses_vid = BITSET_TEST(c->s->info.system_values_read,
                                    SYSTEM_VALUE_VERTEX_ID) ||
                        BITSET_TEST(c->s->info.system_values_read,
                                    SYSTEM_VALUE_VERTEX_ID_ZERO_BASE);

        num_components += uses_iid;
        num_components += uses_biid;
        num_components += uses_vid;

        for (int i = 0; i < ARRAY_SIZE(c->vattr_sizes); i++)
                num_components += c->vattr_sizes[i];

        if (uses_iid) {
                c->iid = ntq_emit_vpm_read(c, &vpm_components_queued,
                                           &num_components, ~0);
        }

        if (uses_biid) {
                c->biid = ntq_emit_vpm_read(c, &vpm_components_queued,
                                            &num_components, ~0);
        }

        if (uses_vid) {
                c->vid = ntq_emit_vpm_read(c, &vpm_components_queued,
                                           &num_components, ~0);
        }

        /* The actual loads will happen directly in nir_intrinsic_load_input
         * on newer versions.
         */
        if (c->devinfo->ver >= 40)
                return;

        for (int loc = 0; loc < ARRAY_SIZE(c->vattr_sizes); loc++) {
                resize_qreg_array(c, &c->inputs, &c->inputs_array_size,
                                  (loc + 1) * 4);

                for (int i = 0; i < c->vattr_sizes[loc]; i++) {
                        c->inputs[loc * 4 + i] =
                                ntq_emit_vpm_read(c,
                                                  &vpm_components_queued,
                                                  &num_components,
                                                  loc * 4 + i);

                }
        }

        if (c->devinfo->ver >= 40) {
                assert(vpm_components_queued == num_components);
        } else {
                assert(vpm_components_queued == 0);
                assert(num_components == 0);
        }
}

static bool
program_reads_point_coord(struct v3d_compile *c)
{
        nir_foreach_shader_in_variable(var, c->s) {
                if (util_varying_is_point_coord(var->data.location,
                                                c->fs_key->point_sprite_mask)) {
                        return true;
                }
        }

        return false;
}

static void
ntq_setup_gs_inputs(struct v3d_compile *c)
{
        nir_sort_variables_with_modes(c->s, driver_location_compare,
                                      nir_var_shader_in);

        nir_foreach_shader_in_variable(var, c->s) {
                /* All GS inputs are arrays with as many entries as vertices
                 * in the input primitive, but here we only care about the
                 * per-vertex input type.
                 */
                assert(glsl_type_is_array(var->type));
                const struct glsl_type *type = glsl_get_array_element(var->type);
                unsigned array_len = MAX2(glsl_get_length(type), 1);
                unsigned loc = var->data.driver_location;

                resize_qreg_array(c, &c->inputs, &c->inputs_array_size,
                                  (loc + array_len) * 4);

                if (var->data.compact) {
                        for (unsigned j = 0; j < array_len; j++) {
                                unsigned input_idx = c->num_inputs++;
                                unsigned loc_frac = var->data.location_frac + j;
                                unsigned loc = var->data.location + loc_frac / 4;
                                unsigned comp = loc_frac % 4;
                                c->input_slots[input_idx] =
                                        v3d_slot_from_slot_and_component(loc, comp);
                        }
                       continue;
                }

                for (unsigned j = 0; j < array_len; j++) {
                        unsigned num_elements = glsl_get_vector_elements(type);
                        for (unsigned k = 0; k < num_elements; k++) {
                                unsigned chan = var->data.location_frac + k;
                                unsigned input_idx = c->num_inputs++;
                                struct v3d_varying_slot slot =
                                        v3d_slot_from_slot_and_component(var->data.location + j, chan);
                                c->input_slots[input_idx] = slot;
                        }
                }
        }
}


static void
ntq_setup_fs_inputs(struct v3d_compile *c)
{
        nir_sort_variables_with_modes(c->s, driver_location_compare,
                                      nir_var_shader_in);

        nir_foreach_shader_in_variable(var, c->s) {
                unsigned var_len = glsl_count_vec4_slots(var->type, false, false);
                unsigned loc = var->data.driver_location;

                uint32_t inputs_array_size = c->inputs_array_size;
                uint32_t inputs_array_required_size = (loc + var_len) * 4;
                resize_qreg_array(c, &c->inputs, &c->inputs_array_size,
                                  inputs_array_required_size);
                resize_interp_array(c, &c->interp, &inputs_array_size,
                                    inputs_array_required_size);

                if (var->data.location == VARYING_SLOT_POS) {
                        emit_fragcoord_input(c, loc);
                } else if (var->data.location == VARYING_SLOT_PRIMITIVE_ID &&
                           !c->fs_key->has_gs) {
                        /* If the fragment shader reads gl_PrimitiveID and we
                         * don't have a geometry shader in the pipeline to write
                         * it then we program the hardware to inject it as
                         * an implicit varying. Take it from there.
                         */
                        c->inputs[loc * 4] = c->primitive_id;
                } else if (util_varying_is_point_coord(var->data.location,
                                                       c->fs_key->point_sprite_mask)) {
                        c->inputs[loc * 4 + 0] = c->point_x;
                        c->inputs[loc * 4 + 1] = c->point_y;
                } else if (var->data.compact) {
                        for (int j = 0; j < var_len; j++)
                                emit_compact_fragment_input(c, loc, var, j);
                } else if (glsl_type_is_struct(var->type)) {
                        for (int j = 0; j < var_len; j++) {
                           emit_fragment_input(c, loc, var, j, 4);
                        }
                } else {
                        for (int j = 0; j < var_len; j++) {
                                emit_fragment_input(c, loc, var, j, glsl_get_vector_elements(var->type));
                        }
                }
        }
}

static void
ntq_setup_outputs(struct v3d_compile *c)
{
        if (c->s->info.stage != MESA_SHADER_FRAGMENT)
                return;

        nir_foreach_shader_out_variable(var, c->s) {
                unsigned array_len = MAX2(glsl_get_length(var->type), 1);
                unsigned loc = var->data.driver_location * 4;

                assert(array_len == 1);
                (void)array_len;

                for (int i = 0; i < 4 - var->data.location_frac; i++) {
                        add_output(c, loc + var->data.location_frac + i,
                                   var->data.location,
                                   var->data.location_frac + i);
                }

                switch (var->data.location) {
                case FRAG_RESULT_COLOR:
                        c->output_color_var[0] = var;
                        c->output_color_var[1] = var;
                        c->output_color_var[2] = var;
                        c->output_color_var[3] = var;
                        break;
                case FRAG_RESULT_DATA0:
                case FRAG_RESULT_DATA1:
                case FRAG_RESULT_DATA2:
                case FRAG_RESULT_DATA3:
                        c->output_color_var[var->data.location -
                                            FRAG_RESULT_DATA0] = var;
                        break;
                case FRAG_RESULT_DEPTH:
                        c->output_position_index = loc;
                        break;
                case FRAG_RESULT_SAMPLE_MASK:
                        c->output_sample_mask_index = loc;
                        break;
                }
        }
}

/**
 * Sets up the mapping from nir_register to struct qreg *.
 *
 * Each nir_register gets a struct qreg per 32-bit component being stored.
 */
static void
ntq_setup_registers(struct v3d_compile *c, struct exec_list *list)
{
        foreach_list_typed(nir_register, nir_reg, node, list) {
                unsigned array_len = MAX2(nir_reg->num_array_elems, 1);
                struct qreg *qregs = ralloc_array(c->def_ht, struct qreg,
                                                  array_len *
                                                  nir_reg->num_components);

                _mesa_hash_table_insert(c->def_ht, nir_reg, qregs);

                for (int i = 0; i < array_len * nir_reg->num_components; i++)
                        qregs[i] = vir_get_temp(c);
        }
}

static void
ntq_emit_load_const(struct v3d_compile *c, nir_load_const_instr *instr)
{
        /* XXX perf: Experiment with using immediate loads to avoid having
         * these end up in the uniform stream.  Watch out for breaking the
         * small immediates optimization in the process!
         */
        struct qreg *qregs = ntq_init_ssa_def(c, &instr->def);
        for (int i = 0; i < instr->def.num_components; i++)
                qregs[i] = vir_uniform_ui(c, instr->value[i].u32);

        _mesa_hash_table_insert(c->def_ht, &instr->def, qregs);
}

static void
ntq_emit_image_size(struct v3d_compile *c, nir_intrinsic_instr *instr)
{
        unsigned image_index = nir_src_as_uint(instr->src[0]);
        bool is_array = nir_intrinsic_image_array(instr);

        assert(nir_src_as_uint(instr->src[1]) == 0);

        ntq_store_dest(c, &instr->dest, 0,
                       vir_uniform(c, QUNIFORM_IMAGE_WIDTH, image_index));
        if (instr->num_components > 1) {
                ntq_store_dest(c, &instr->dest, 1,
                               vir_uniform(c,
                                           instr->num_components == 2 && is_array ?
                                                   QUNIFORM_IMAGE_ARRAY_SIZE :
                                                   QUNIFORM_IMAGE_HEIGHT,
                                           image_index));
        }
        if (instr->num_components > 2) {
                ntq_store_dest(c, &instr->dest, 2,
                               vir_uniform(c,
                                           is_array ?
                                           QUNIFORM_IMAGE_ARRAY_SIZE :
                                           QUNIFORM_IMAGE_DEPTH,
                                           image_index));
        }
}

static void
vir_emit_tlb_color_read(struct v3d_compile *c, nir_intrinsic_instr *instr)
{
        assert(c->s->info.stage == MESA_SHADER_FRAGMENT);

        int rt = nir_src_as_uint(instr->src[0]);
        assert(rt < V3D_MAX_DRAW_BUFFERS);

        int sample_index = nir_intrinsic_base(instr) ;
        assert(sample_index < V3D_MAX_SAMPLES);

        int component = nir_intrinsic_component(instr);
        assert(component < 4);

        /* We need to emit our TLB reads after we have acquired the scoreboard
         * lock, or the GPU will hang. Usually, we do our scoreboard locking on
         * the last thread switch to improve parallelism, however, that is only
         * guaranteed to happen before the tlb color writes.
         *
         * To fix that, we make sure we always emit a thread switch before the
         * first tlb color read. If that happens to be the last thread switch
         * we emit, then everything is fine, but otherwsie, if any code after
         * this point needs to emit additional thread switches, then we will
         * switch the strategy to locking the scoreboard on the first thread
         * switch instead -- see vir_emit_thrsw().
         */
        if (!c->emitted_tlb_load) {
                if (!c->last_thrsw_at_top_level) {
                        assert(c->devinfo->ver >= 41);
                        vir_emit_thrsw(c);
                }

                c->emitted_tlb_load = true;
        }

        struct qreg *color_reads_for_sample =
                &c->color_reads[(rt * V3D_MAX_SAMPLES + sample_index) * 4];

        if (color_reads_for_sample[component].file == QFILE_NULL) {
                enum pipe_format rt_format = c->fs_key->color_fmt[rt].format;
                int num_components =
                        util_format_get_nr_components(rt_format);

                const bool swap_rb = c->fs_key->swap_color_rb & (1 << rt);
                if (swap_rb)
                        num_components = MAX2(num_components, 3);

                nir_variable *var = c->output_color_var[rt];
                enum glsl_base_type type = glsl_get_base_type(var->type);

                bool is_int_format = type == GLSL_TYPE_INT ||
                                     type == GLSL_TYPE_UINT;

                bool is_32b_tlb_format = is_int_format ||
                                         (c->fs_key->f32_color_rb & (1 << rt));

                int num_samples = c->fs_key->msaa ? V3D_MAX_SAMPLES : 1;

                uint32_t conf = 0xffffff00;
                conf |= c->fs_key->msaa ? TLB_SAMPLE_MODE_PER_SAMPLE :
                                          TLB_SAMPLE_MODE_PER_PIXEL;
                conf |= (7 - rt) << TLB_RENDER_TARGET_SHIFT;

                if (is_32b_tlb_format) {
                        /* The F32 vs I32 distinction was dropped in 4.2. */
                        conf |= (c->devinfo->ver < 42 && is_int_format) ?
                                TLB_TYPE_I32_COLOR : TLB_TYPE_F32_COLOR;

                        conf |= ((num_components - 1) <<
                                 TLB_VEC_SIZE_MINUS_1_SHIFT);
                } else {
                        conf |= TLB_TYPE_F16_COLOR;
                        conf |= TLB_F16_SWAP_HI_LO;

                        if (num_components >= 3)
                                conf |= TLB_VEC_SIZE_4_F16;
                        else
                                conf |= TLB_VEC_SIZE_2_F16;
                }


                for (int i = 0; i < num_samples; i++) {
                        struct qreg r, g, b, a;
                        if (is_32b_tlb_format) {
                                r = conf != 0xffffffff && i == 0?
                                        vir_TLBU_COLOR_READ(c, conf) :
                                        vir_TLB_COLOR_READ(c);
                                if (num_components >= 2)
                                        g = vir_TLB_COLOR_READ(c);
                                if (num_components >= 3)
                                        b = vir_TLB_COLOR_READ(c);
                                if (num_components >= 4)
                                        a = vir_TLB_COLOR_READ(c);
                        } else {
                                struct qreg rg = conf != 0xffffffff && i == 0 ?
                                        vir_TLBU_COLOR_READ(c, conf) :
                                        vir_TLB_COLOR_READ(c);
                                r = vir_FMOV(c, rg);
                                vir_set_unpack(c->defs[r.index], 0,
                                               V3D_QPU_UNPACK_L);
                                g = vir_FMOV(c, rg);
                                vir_set_unpack(c->defs[g.index], 0,
                                               V3D_QPU_UNPACK_H);

                                if (num_components > 2) {
                                    struct qreg ba = vir_TLB_COLOR_READ(c);
                                    b = vir_FMOV(c, ba);
                                    vir_set_unpack(c->defs[b.index], 0,
                                                   V3D_QPU_UNPACK_L);
                                    a = vir_FMOV(c, ba);
                                    vir_set_unpack(c->defs[a.index], 0,
                                                   V3D_QPU_UNPACK_H);
                                }
                        }

                        struct qreg *color_reads =
                                &c->color_reads[(rt * V3D_MAX_SAMPLES + i) * 4];

                        color_reads[0] = swap_rb ? b : r;
                        if (num_components >= 2)
                                color_reads[1] = g;
                        if (num_components >= 3)
                                color_reads[2] = swap_rb ? r : b;
                        if (num_components >= 4)
                                color_reads[3] = a;
                }
        }

        assert(color_reads_for_sample[component].file != QFILE_NULL);
        ntq_store_dest(c, &instr->dest, 0,
                       vir_MOV(c, color_reads_for_sample[component]));
}

static void
ntq_emit_load_uniform(struct v3d_compile *c, nir_intrinsic_instr *instr)
{
        if (nir_src_is_const(instr->src[0])) {
                int offset = (nir_intrinsic_base(instr) +
                             nir_src_as_uint(instr->src[0]));
                assert(offset % 4 == 0);
                /* We need dwords */
                offset = offset / 4;
                for (int i = 0; i < instr->num_components; i++) {
                        ntq_store_dest(c, &instr->dest, i,
                                       vir_uniform(c, QUNIFORM_UNIFORM,
                                                   offset + i));
                }
        } else {
               ntq_emit_tmu_general(c, instr, false);
        }
}

static void
ntq_emit_load_input(struct v3d_compile *c, nir_intrinsic_instr *instr)
{
        /* XXX: Use ldvpmv (uniform offset) or ldvpmd (non-uniform offset).
         *
         * Right now the driver sets PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR even
         * if we don't support non-uniform offsets because we also set the
         * lower_all_io_to_temps option in the NIR compiler. This ensures that
         * any indirect indexing on in/out variables is turned into indirect
         * indexing on temporary variables instead, that we handle by lowering
         * to scratch. If we implement non-uniform offset here we might be able
         * to avoid the temp and scratch lowering, which involves copying from
         * the input to the temp variable, possibly making code more optimal.
         */
        unsigned offset =
                nir_intrinsic_base(instr) + nir_src_as_uint(instr->src[0]);

        if (c->s->info.stage != MESA_SHADER_FRAGMENT && c->devinfo->ver >= 40) {
               /* Emit the LDVPM directly now, rather than at the top
                * of the shader like we did for V3D 3.x (which needs
                * vpmsetup when not just taking the next offset).
                *
                * Note that delaying like this may introduce stalls,
                * as LDVPMV takes a minimum of 1 instruction but may
                * be slower if the VPM unit is busy with another QPU.
                */
               int index = 0;
               if (BITSET_TEST(c->s->info.system_values_read,
                               SYSTEM_VALUE_INSTANCE_ID)) {
                      index++;
               }
               if (BITSET_TEST(c->s->info.system_values_read,
                               SYSTEM_VALUE_BASE_INSTANCE)) {
                      index++;
               }
               if (BITSET_TEST(c->s->info.system_values_read,
                               SYSTEM_VALUE_VERTEX_ID)) {
                      index++;
               }
               for (int i = 0; i < offset; i++)
                      index += c->vattr_sizes[i];
               index += nir_intrinsic_component(instr);
               for (int i = 0; i < instr->num_components; i++) {
                      struct qreg vpm_offset = vir_uniform_ui(c, index++);
                      ntq_store_dest(c, &instr->dest, i,
                                     vir_LDVPMV_IN(c, vpm_offset));
                }
        } else {
                for (int i = 0; i < instr->num_components; i++) {
                        int comp = nir_intrinsic_component(instr) + i;
                        ntq_store_dest(c, &instr->dest, i,
                                       vir_MOV(c, c->inputs[offset * 4 + comp]));
                }
        }
}

static void
ntq_emit_per_sample_color_write(struct v3d_compile *c,
                                nir_intrinsic_instr *instr)
{
        assert(instr->intrinsic == nir_intrinsic_store_tlb_sample_color_v3d);

        unsigned rt = nir_src_as_uint(instr->src[1]);
        assert(rt < V3D_MAX_DRAW_BUFFERS);

        unsigned sample_idx = nir_intrinsic_base(instr);
        assert(sample_idx < V3D_MAX_SAMPLES);

        unsigned offset = (rt * V3D_MAX_SAMPLES + sample_idx) * 4;
        for (int i = 0; i < instr->num_components; i++) {
                c->sample_colors[offset + i] =
                        vir_MOV(c, ntq_get_src(c, instr->src[0], i));
        }
}

static void
ntq_emit_color_write(struct v3d_compile *c,
                     nir_intrinsic_instr *instr)
{
        unsigned offset = (nir_intrinsic_base(instr) +
                           nir_src_as_uint(instr->src[1])) * 4 +
                          nir_intrinsic_component(instr);
        for (int i = 0; i < instr->num_components; i++) {
                c->outputs[offset + i] =
                        vir_MOV(c, ntq_get_src(c, instr->src[0], i));
        }
}

static void
emit_store_output_gs(struct v3d_compile *c, nir_intrinsic_instr *instr)
{
        assert(instr->num_components == 1);

        struct qreg offset = ntq_get_src(c, instr->src[1], 0);

        uint32_t base_offset = nir_intrinsic_base(instr);

        if (base_offset)
                offset = vir_ADD(c, vir_uniform_ui(c, base_offset), offset);

        /* Usually, for VS or FS, we only emit outputs once at program end so
         * our VPM writes are never in non-uniform control flow, but this
         * is not true for GS, where we are emitting multiple vertices.
         */
        if (vir_in_nonuniform_control_flow(c)) {
                vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), c->execute),
                           V3D_QPU_PF_PUSHZ);
        }

        struct qreg val = ntq_get_src(c, instr->src[0], 0);

        /* The offset isn’t necessarily dynamically uniform for a geometry
         * shader. This can happen if the shader sometimes doesn’t emit one of
         * the vertices. In that case subsequent vertices will be written to
         * different offsets in the VPM and we need to use the scatter write
         * instruction to have a different offset for each lane.
         */
         bool is_uniform_offset =
                 !vir_in_nonuniform_control_flow(c) &&
                 !nir_src_is_divergent(instr->src[1]);
         vir_VPM_WRITE_indirect(c, val, offset, is_uniform_offset);

        if (vir_in_nonuniform_control_flow(c)) {
                struct qinst *last_inst =
                        (struct qinst *)c->cur_block->instructions.prev;
                vir_set_cond(last_inst, V3D_QPU_COND_IFA);
        }
}

static void
emit_store_output_vs(struct v3d_compile *c, nir_intrinsic_instr *instr)
{
        assert(c->s->info.stage == MESA_SHADER_VERTEX);
        assert(instr->num_components == 1);

        uint32_t base = nir_intrinsic_base(instr);
        struct qreg val = ntq_get_src(c, instr->src[0], 0);

        if (nir_src_is_const(instr->src[1])) {
                vir_VPM_WRITE(c, val,
                              base + nir_src_as_uint(instr->src[1]));
        } else {
                struct qreg offset = vir_ADD(c,
                                             ntq_get_src(c, instr->src[1], 1),
                                             vir_uniform_ui(c, base));
                bool is_uniform_offset =
                        !vir_in_nonuniform_control_flow(c) &&
                        !nir_src_is_divergent(instr->src[1]);
                vir_VPM_WRITE_indirect(c, val, offset, is_uniform_offset);
        }
}

static void
ntq_emit_store_output(struct v3d_compile *c, nir_intrinsic_instr *instr)
{
        if (c->s->info.stage == MESA_SHADER_FRAGMENT)
               ntq_emit_color_write(c, instr);
        else if (c->s->info.stage == MESA_SHADER_GEOMETRY)
               emit_store_output_gs(c, instr);
        else
               emit_store_output_vs(c, instr);
}

/**
 * This implementation is based on v3d_sample_{x,y}_offset() from
 * v3d_sample_offset.h.
 */
static void
ntq_get_sample_offset(struct v3d_compile *c, struct qreg sample_idx,
                      struct qreg *sx, struct qreg *sy)
{
        sample_idx = vir_ITOF(c, sample_idx);

        struct qreg offset_x =
                vir_FADD(c, vir_uniform_f(c, -0.125f),
                            vir_FMUL(c, sample_idx,
                                        vir_uniform_f(c, 0.5f)));
        vir_set_pf(c, vir_FCMP_dest(c, vir_nop_reg(),
                                    vir_uniform_f(c, 2.0f), sample_idx),
                   V3D_QPU_PF_PUSHC);
        offset_x = vir_SEL(c, V3D_QPU_COND_IFA,
                              vir_FSUB(c, offset_x, vir_uniform_f(c, 1.25f)),
                              offset_x);

        struct qreg offset_y =
                   vir_FADD(c, vir_uniform_f(c, -0.375f),
                               vir_FMUL(c, sample_idx,
                                           vir_uniform_f(c, 0.25f)));
        *sx = offset_x;
        *sy = offset_y;
}

/**
 * This implementation is based on get_centroid_offset() from fep.c.
 */
static void
ntq_get_barycentric_centroid(struct v3d_compile *c,
                             struct qreg *out_x,
                             struct qreg *out_y)
{
        struct qreg sample_mask;
        if (c->output_sample_mask_index != -1)
                sample_mask = c->outputs[c->output_sample_mask_index];
        else
                sample_mask = vir_MSF(c);

        struct qreg i0 = vir_uniform_ui(c, 0);
        struct qreg i1 = vir_uniform_ui(c, 1);
        struct qreg i2 = vir_uniform_ui(c, 2);
        struct qreg i3 = vir_uniform_ui(c, 3);
        struct qreg i4 = vir_uniform_ui(c, 4);
        struct qreg i8 = vir_uniform_ui(c, 8);

        /* sN = TRUE if sample N enabled in sample mask, FALSE otherwise */
        struct qreg F = vir_uniform_ui(c, 0);
        struct qreg T = vir_uniform_ui(c, ~0);
        struct qreg s0 = vir_XOR(c, vir_AND(c, sample_mask, i1), i1);
        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), s0), V3D_QPU_PF_PUSHZ);
        s0 = vir_SEL(c, V3D_QPU_COND_IFA, T, F);
        struct qreg s1 = vir_XOR(c, vir_AND(c, sample_mask, i2), i2);
        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), s1), V3D_QPU_PF_PUSHZ);
        s1 = vir_SEL(c, V3D_QPU_COND_IFA, T, F);
        struct qreg s2 = vir_XOR(c, vir_AND(c, sample_mask, i4), i4);
        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), s2), V3D_QPU_PF_PUSHZ);
        s2 = vir_SEL(c, V3D_QPU_COND_IFA, T, F);
        struct qreg s3 = vir_XOR(c, vir_AND(c, sample_mask, i8), i8);
        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), s3), V3D_QPU_PF_PUSHZ);
        s3 = vir_SEL(c, V3D_QPU_COND_IFA, T, F);

        /* sample_idx = s0 ? 0 : s2 ? 2 : s1 ? 1 : 3 */
        struct qreg sample_idx = i3;
        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), s1), V3D_QPU_PF_PUSHZ);
        sample_idx = vir_SEL(c, V3D_QPU_COND_IFNA, i1, sample_idx);
        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), s2), V3D_QPU_PF_PUSHZ);
        sample_idx = vir_SEL(c, V3D_QPU_COND_IFNA, i2, sample_idx);
        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), s0), V3D_QPU_PF_PUSHZ);
        sample_idx = vir_SEL(c, V3D_QPU_COND_IFNA, i0, sample_idx);

        /* Get offset at selected sample index */
        struct qreg offset_x, offset_y;
        ntq_get_sample_offset(c, sample_idx, &offset_x, &offset_y);

        /* Select pixel center [offset=(0,0)] if two opposing samples (or none)
         * are selected.
         */
        struct qreg s0_and_s3 = vir_AND(c, s0, s3);
        struct qreg s1_and_s2 = vir_AND(c, s1, s2);

        struct qreg use_center = vir_XOR(c, sample_mask, vir_uniform_ui(c, 0));
        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), use_center), V3D_QPU_PF_PUSHZ);
        use_center = vir_SEL(c, V3D_QPU_COND_IFA, T, F);
        use_center = vir_OR(c, use_center, s0_and_s3);
        use_center = vir_OR(c, use_center, s1_and_s2);

        struct qreg zero = vir_uniform_f(c, 0.0f);
        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), use_center), V3D_QPU_PF_PUSHZ);
        offset_x = vir_SEL(c, V3D_QPU_COND_IFNA, zero, offset_x);
        offset_y = vir_SEL(c, V3D_QPU_COND_IFNA, zero, offset_y);

        *out_x = offset_x;
        *out_y = offset_y;
}

static struct qreg
ntq_emit_load_interpolated_input(struct v3d_compile *c,
                                 struct qreg p,
                                 struct qreg C,
                                 struct qreg offset_x,
                                 struct qreg offset_y,
                                 unsigned mode)
{
        if (mode == INTERP_MODE_FLAT)
                return C;

        struct qreg sample_offset_x =
                vir_FSUB(c, vir_FXCD(c), vir_ITOF(c, vir_XCD(c)));
        struct qreg sample_offset_y =
                vir_FSUB(c, vir_FYCD(c), vir_ITOF(c, vir_YCD(c)));

        struct qreg scaleX =
                vir_FADD(c, vir_FSUB(c, vir_uniform_f(c, 0.5f), sample_offset_x),
                            offset_x);
        struct qreg scaleY =
                vir_FADD(c, vir_FSUB(c, vir_uniform_f(c, 0.5f), sample_offset_y),
                            offset_y);

        struct qreg pInterp =
                vir_FADD(c, p, vir_FADD(c, vir_FMUL(c, vir_FDX(c, p), scaleX),
                                           vir_FMUL(c, vir_FDY(c, p), scaleY)));

        if (mode == INTERP_MODE_NOPERSPECTIVE)
                return vir_FADD(c, pInterp, C);

        struct qreg w = c->payload_w;
        struct qreg wInterp =
                vir_FADD(c, w, vir_FADD(c, vir_FMUL(c, vir_FDX(c, w), scaleX),
                                           vir_FMUL(c, vir_FDY(c, w), scaleY)));

        return vir_FADD(c, vir_FMUL(c, pInterp, wInterp), C);
}

static void
emit_ldunifa(struct v3d_compile *c, struct qreg *result)
{
        struct qinst *ldunifa =
                vir_add_inst(V3D_QPU_A_NOP, c->undef, c->undef, c->undef);
        ldunifa->qpu.sig.ldunifa = true;
        if (result)
                *result = vir_emit_def(c, ldunifa);
        else
                vir_emit_nondef(c, ldunifa);
        c->current_unifa_offset += 4;
}

static void
ntq_emit_load_ubo_unifa(struct v3d_compile *c, nir_intrinsic_instr *instr)
{
        /* Every ldunifa auto-increments the unifa address by 4 bytes, so our
         * current unifa offset is 4 bytes ahead of the offset of the last load.
         */
        static const int32_t max_unifa_skip_dist =
                MAX_UNIFA_SKIP_DISTANCE - 4;

        bool dynamic_src = !nir_src_is_const(instr->src[1]);
        uint32_t const_offset =
                dynamic_src ? 0 : nir_src_as_uint(instr->src[1]);

        /* On OpenGL QUNIFORM_UBO_ADDR takes a UBO index
         * shifted up by 1 (0 is gallium's constant buffer 0).
         */
        uint32_t index = nir_src_as_uint(instr->src[0]);
        if (c->key->environment == V3D_ENVIRONMENT_OPENGL)
                index++;

        /* We can only keep track of the last unifa address we used with
         * constant offset loads. If the new load targets the same UBO and
         * is close enough to the previous load, we can skip the unifa register
         * write by emitting dummy ldunifa instructions to update the unifa
         * address.
         */
        bool skip_unifa = false;
        uint32_t ldunifa_skips = 0;
        if (dynamic_src) {
                c->current_unifa_block = NULL;
        } else if (c->cur_block == c->current_unifa_block &&
                   c->current_unifa_index == index &&
                   c->current_unifa_offset <= const_offset &&
                   c->current_unifa_offset + max_unifa_skip_dist >= const_offset) {
                skip_unifa = true;
                ldunifa_skips = (const_offset - c->current_unifa_offset) / 4;
        } else {
                c->current_unifa_block = c->cur_block;
                c->current_unifa_index = index;
                c->current_unifa_offset = const_offset;
        }

        if (!skip_unifa) {
                struct qreg base_offset =
                        vir_uniform(c, QUNIFORM_UBO_ADDR,
                                    v3d_unit_data_create(index, const_offset));

                struct qreg unifa = vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_UNIFA);
                if (!dynamic_src) {
                        vir_MOV_dest(c, unifa, base_offset);
                } else {
                        vir_ADD_dest(c, unifa, base_offset,
                                     ntq_get_src(c, instr->src[1], 0));
                }
        } else {
                for (int i = 0; i < ldunifa_skips; i++)
                        emit_ldunifa(c, NULL);
        }

        for (uint32_t i = 0; i < nir_intrinsic_dest_components(instr); i++) {
                struct qreg data;
                emit_ldunifa(c, &data);
                ntq_store_dest(c, &instr->dest, i, vir_MOV(c, data));
        }
}

static inline struct qreg
emit_load_local_invocation_index(struct v3d_compile *c)
{
        return vir_SHR(c, c->cs_payload[1],
                       vir_uniform_ui(c, 32 - c->local_invocation_index_bits));
}

/* Various subgroup operations rely on the A flags, so this helper ensures that
 * A flags represents currently active lanes in the subgroup.
 */
static void
set_a_flags_for_subgroup(struct v3d_compile *c)
{
        /* MSF returns 0 for disabled lanes in compute shaders so
         * PUSHZ will set A=1 for disabled lanes. We want the inverse
         * of this but we don't have any means to negate the A flags
         * directly, but we can do it by repeating the same operation
         * with NORZ (A = ~A & ~Z).
         */
        assert(c->s->info.stage == MESA_SHADER_COMPUTE);
        vir_set_pf(c, vir_MSF_dest(c, vir_nop_reg()), V3D_QPU_PF_PUSHZ);
        vir_set_uf(c, vir_MSF_dest(c, vir_nop_reg()), V3D_QPU_UF_NORZ);

        /* If we are under non-uniform control flow we also need to
         * AND the A flags with the current execute mask.
         */
        if (vir_in_nonuniform_control_flow(c)) {
                const uint32_t bidx = c->cur_block->index;
                vir_set_uf(c, vir_XOR_dest(c, vir_nop_reg(),
                                           c->execute,
                                           vir_uniform_ui(c, bidx)),
                           V3D_QPU_UF_ANDZ);
        }
}

static void
ntq_emit_intrinsic(struct v3d_compile *c, nir_intrinsic_instr *instr)
{
        switch (instr->intrinsic) {
        case nir_intrinsic_load_uniform:
                ntq_emit_load_uniform(c, instr);
                break;

        case nir_intrinsic_load_ubo:
                if (!nir_src_is_divergent(instr->src[1]))
                        ntq_emit_load_ubo_unifa(c, instr);
                else
                        ntq_emit_tmu_general(c, instr, false);
                break;

        case nir_intrinsic_ssbo_atomic_add:
        case nir_intrinsic_ssbo_atomic_imin:
        case nir_intrinsic_ssbo_atomic_umin:
        case nir_intrinsic_ssbo_atomic_imax:
        case nir_intrinsic_ssbo_atomic_umax:
        case nir_intrinsic_ssbo_atomic_and:
        case nir_intrinsic_ssbo_atomic_or:
        case nir_intrinsic_ssbo_atomic_xor:
        case nir_intrinsic_ssbo_atomic_exchange:
        case nir_intrinsic_ssbo_atomic_comp_swap:
        case nir_intrinsic_load_ssbo:
        case nir_intrinsic_store_ssbo:
                ntq_emit_tmu_general(c, instr, false);
                break;

        case nir_intrinsic_shared_atomic_add:
        case nir_intrinsic_shared_atomic_imin:
        case nir_intrinsic_shared_atomic_umin:
        case nir_intrinsic_shared_atomic_imax:
        case nir_intrinsic_shared_atomic_umax:
        case nir_intrinsic_shared_atomic_and:
        case nir_intrinsic_shared_atomic_or:
        case nir_intrinsic_shared_atomic_xor:
        case nir_intrinsic_shared_atomic_exchange:
        case nir_intrinsic_shared_atomic_comp_swap:
        case nir_intrinsic_load_shared:
        case nir_intrinsic_store_shared:
        case nir_intrinsic_load_scratch:
        case nir_intrinsic_store_scratch:
                ntq_emit_tmu_general(c, instr, true);
                break;

        case nir_intrinsic_image_load:
        case nir_intrinsic_image_store:
        case nir_intrinsic_image_atomic_add:
        case nir_intrinsic_image_atomic_imin:
        case nir_intrinsic_image_atomic_umin:
        case nir_intrinsic_image_atomic_imax:
        case nir_intrinsic_image_atomic_umax:
        case nir_intrinsic_image_atomic_and:
        case nir_intrinsic_image_atomic_or:
        case nir_intrinsic_image_atomic_xor:
        case nir_intrinsic_image_atomic_exchange:
        case nir_intrinsic_image_atomic_comp_swap:
                v3d40_vir_emit_image_load_store(c, instr);
                break;

        case nir_intrinsic_get_ssbo_size:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_GET_SSBO_SIZE,
                                           nir_src_comp_as_uint(instr->src[0], 0)));
                break;

        case nir_intrinsic_get_ubo_size:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_GET_UBO_SIZE,
                                           nir_src_comp_as_uint(instr->src[0], 0)));
                break;

        case nir_intrinsic_load_user_clip_plane:
                for (int i = 0; i < nir_intrinsic_dest_components(instr); i++) {
                        ntq_store_dest(c, &instr->dest, i,
                                       vir_uniform(c, QUNIFORM_USER_CLIP_PLANE,
                                                   nir_intrinsic_ucp_id(instr) *
                                                   4 + i));
                }
                break;

        case nir_intrinsic_load_viewport_x_scale:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_VIEWPORT_X_SCALE, 0));
                break;

        case nir_intrinsic_load_viewport_y_scale:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_VIEWPORT_Y_SCALE, 0));
                break;

        case nir_intrinsic_load_viewport_z_scale:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_VIEWPORT_Z_SCALE, 0));
                break;

        case nir_intrinsic_load_viewport_z_offset:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_VIEWPORT_Z_OFFSET, 0));
                break;

        case nir_intrinsic_load_line_coord:
                ntq_store_dest(c, &instr->dest, 0, vir_MOV(c, c->line_x));
                break;

        case nir_intrinsic_load_line_width:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_LINE_WIDTH, 0));
                break;

        case nir_intrinsic_load_aa_line_width:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_AA_LINE_WIDTH, 0));
                break;

        case nir_intrinsic_load_sample_mask_in:
                ntq_store_dest(c, &instr->dest, 0, vir_MSF(c));
                break;

        case nir_intrinsic_load_helper_invocation:
                vir_set_pf(c, vir_MSF_dest(c, vir_nop_reg()), V3D_QPU_PF_PUSHZ);
                struct qreg qdest = ntq_emit_cond_to_bool(c, V3D_QPU_COND_IFA);
                ntq_store_dest(c, &instr->dest, 0, qdest);
                break;

        case nir_intrinsic_load_front_face:
                /* The register contains 0 (front) or 1 (back), and we need to
                 * turn it into a NIR bool where true means front.
                 */
                ntq_store_dest(c, &instr->dest, 0,
                               vir_ADD(c,
                                       vir_uniform_ui(c, -1),
                                       vir_REVF(c)));
                break;

        case nir_intrinsic_load_base_instance:
                ntq_store_dest(c, &instr->dest, 0, vir_MOV(c, c->biid));
                break;

        case nir_intrinsic_load_instance_id:
                ntq_store_dest(c, &instr->dest, 0, vir_MOV(c, c->iid));
                break;

        case nir_intrinsic_load_vertex_id:
                ntq_store_dest(c, &instr->dest, 0, vir_MOV(c, c->vid));
                break;

        case nir_intrinsic_load_tlb_color_v3d:
                vir_emit_tlb_color_read(c, instr);
                break;

        case nir_intrinsic_load_input:
                ntq_emit_load_input(c, instr);
                break;

        case nir_intrinsic_store_tlb_sample_color_v3d:
               ntq_emit_per_sample_color_write(c, instr);
               break;

       case nir_intrinsic_store_output:
                ntq_emit_store_output(c, instr);
                break;

        case nir_intrinsic_image_size:
                ntq_emit_image_size(c, instr);
                break;

        case nir_intrinsic_discard:
                ntq_flush_tmu(c);

                if (vir_in_nonuniform_control_flow(c)) {
                        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), c->execute),
                                   V3D_QPU_PF_PUSHZ);
                        vir_set_cond(vir_SETMSF_dest(c, vir_nop_reg(),
                                                     vir_uniform_ui(c, 0)),
                                V3D_QPU_COND_IFA);
                } else {
                        vir_SETMSF_dest(c, vir_nop_reg(),
                                        vir_uniform_ui(c, 0));
                }
                break;

        case nir_intrinsic_discard_if: {
                ntq_flush_tmu(c);

                enum v3d_qpu_cond cond = ntq_emit_bool_to_cond(c, instr->src[0]);

                if (vir_in_nonuniform_control_flow(c)) {
                        struct qinst *exec_flag = vir_MOV_dest(c, vir_nop_reg(),
                                                               c->execute);
                        if (cond == V3D_QPU_COND_IFA) {
                                vir_set_uf(c, exec_flag, V3D_QPU_UF_ANDZ);
                        } else {
                                vir_set_uf(c, exec_flag, V3D_QPU_UF_NORNZ);
                                cond = V3D_QPU_COND_IFA;
                        }
                }

                vir_set_cond(vir_SETMSF_dest(c, vir_nop_reg(),
                                             vir_uniform_ui(c, 0)), cond);

                break;
        }

        case nir_intrinsic_memory_barrier:
        case nir_intrinsic_memory_barrier_buffer:
        case nir_intrinsic_memory_barrier_image:
        case nir_intrinsic_memory_barrier_shared:
        case nir_intrinsic_memory_barrier_tcs_patch:
        case nir_intrinsic_group_memory_barrier:
                /* We don't do any instruction scheduling of these NIR
                 * instructions between each other, so we just need to make
                 * sure that the TMU operations before the barrier are flushed
                 * before the ones after the barrier.
                 */
                ntq_flush_tmu(c);
                break;

        case nir_intrinsic_control_barrier:
                /* Emit a TSY op to get all invocations in the workgroup
                 * (actually supergroup) to block until the last invocation
                 * reaches the TSY op.
                 */
                ntq_flush_tmu(c);

                if (c->devinfo->ver >= 42) {
                        vir_BARRIERID_dest(c, vir_reg(QFILE_MAGIC,
                                                      V3D_QPU_WADDR_SYNCB));
                } else {
                        struct qinst *sync =
                                vir_BARRIERID_dest(c,
                                                   vir_reg(QFILE_MAGIC,
                                                           V3D_QPU_WADDR_SYNCU));
                        sync->uniform =
                                vir_get_uniform_index(c, QUNIFORM_CONSTANT,
                                                      0xffffff00 |
                                                      V3D_TSY_WAIT_INC_CHECK);

                }

                /* The blocking of a TSY op only happens at the next thread
                 * switch.  No texturing may be outstanding at the time of a
                 * TSY blocking operation.
                 */
                vir_emit_thrsw(c);
                break;

        case nir_intrinsic_load_num_workgroups:
                for (int i = 0; i < 3; i++) {
                        ntq_store_dest(c, &instr->dest, i,
                                       vir_uniform(c, QUNIFORM_NUM_WORK_GROUPS,
                                                   i));
                }
                break;

        case nir_intrinsic_load_workgroup_id: {
                struct qreg x = vir_AND(c, c->cs_payload[0],
                                         vir_uniform_ui(c, 0xffff));

                struct qreg y = vir_SHR(c, c->cs_payload[0],
                                         vir_uniform_ui(c, 16));

                struct qreg z = vir_AND(c, c->cs_payload[1],
                                         vir_uniform_ui(c, 0xffff));

                /* We only support dispatch base in Vulkan */
                if (c->key->environment == V3D_ENVIRONMENT_VULKAN) {
                        x = vir_ADD(c, x,
                                    vir_uniform(c, QUNIFORM_WORK_GROUP_BASE, 0));
                        y = vir_ADD(c, y,
                                    vir_uniform(c, QUNIFORM_WORK_GROUP_BASE, 1));
                        z = vir_ADD(c, z,
                                    vir_uniform(c, QUNIFORM_WORK_GROUP_BASE, 2));
                }

                ntq_store_dest(c, &instr->dest, 0, vir_MOV(c, x));
                ntq_store_dest(c, &instr->dest, 1, vir_MOV(c, y));
                ntq_store_dest(c, &instr->dest, 2, vir_MOV(c, z));
                break;
        }

        case nir_intrinsic_load_local_invocation_index:
                ntq_store_dest(c, &instr->dest, 0,
                               emit_load_local_invocation_index(c));
                break;

        case nir_intrinsic_load_subgroup_id: {
                /* This is basically the batch index, which is the Local
                 * Invocation Index divided by the SIMD width).
                 */
                STATIC_ASSERT(util_is_power_of_two_nonzero(V3D_CHANNELS));
                const uint32_t divide_shift = ffs(V3D_CHANNELS) - 1;
                struct qreg lii = emit_load_local_invocation_index(c);
                ntq_store_dest(c, &instr->dest, 0,
                               vir_SHR(c, lii,
                                       vir_uniform_ui(c, divide_shift)));
                break;
        }

        case nir_intrinsic_load_per_vertex_input: {
                /* The vertex shader writes all its used outputs into
                 * consecutive VPM offsets, so if any output component is
                 * unused, its VPM offset is used by the next used
                 * component. This means that we can't assume that each
                 * location will use 4 consecutive scalar offsets in the VPM
                 * and we need to compute the VPM offset for each input by
                 * going through the inputs and finding the one that matches
                 * our location and component.
                 *
                 * col: vertex index, row = varying index
                 */
                assert(nir_src_is_const(instr->src[1]));
                uint32_t location =
                        nir_intrinsic_io_semantics(instr).location +
                        nir_src_as_uint(instr->src[1]);
                uint32_t component = nir_intrinsic_component(instr);

                int32_t row_idx = -1;
                for (int i = 0; i < c->num_inputs; i++) {
                        struct v3d_varying_slot slot = c->input_slots[i];
                        if (v3d_slot_get_slot(slot) == location &&
                            v3d_slot_get_component(slot) == component) {
                                row_idx = i;
                                break;
                        }
                }

                assert(row_idx != -1);

                struct qreg col = ntq_get_src(c, instr->src[0], 0);
                for (int i = 0; i < instr->num_components; i++) {
                        struct qreg row = vir_uniform_ui(c, row_idx++);
                        ntq_store_dest(c, &instr->dest, i,
                                       vir_LDVPMG_IN(c, row, col));
                }
                break;
        }

        case nir_intrinsic_emit_vertex:
        case nir_intrinsic_end_primitive:
                unreachable("Should have been lowered in v3d_nir_lower_io");
                break;

        case nir_intrinsic_load_primitive_id: {
                /* gl_PrimitiveIdIn is written by the GBG in the first word of
                 * VPM output header. According to docs, we should read this
                 * using ldvpm(v,d)_in (See Table 71).
                 */
                assert(c->s->info.stage == MESA_SHADER_GEOMETRY);
                ntq_store_dest(c, &instr->dest, 0,
                               vir_LDVPMV_IN(c, vir_uniform_ui(c, 0)));
                break;
        }

        case nir_intrinsic_load_invocation_id:
                ntq_store_dest(c, &instr->dest, 0, vir_IID(c));
                break;

        case nir_intrinsic_load_fb_layers_v3d:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_FB_LAYERS, 0));
                break;

        case nir_intrinsic_load_sample_id:
                ntq_store_dest(c, &instr->dest, 0, vir_SAMPID(c));
                break;

        case nir_intrinsic_load_sample_pos:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_FSUB(c, vir_FXCD(c), vir_ITOF(c, vir_XCD(c))));
                ntq_store_dest(c, &instr->dest, 1,
                               vir_FSUB(c, vir_FYCD(c), vir_ITOF(c, vir_YCD(c))));
                break;

        case nir_intrinsic_load_barycentric_at_offset:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_MOV(c, ntq_get_src(c, instr->src[0], 0)));
                ntq_store_dest(c, &instr->dest, 1,
                               vir_MOV(c, ntq_get_src(c, instr->src[0], 1)));
                break;

        case nir_intrinsic_load_barycentric_pixel:
                ntq_store_dest(c, &instr->dest, 0, vir_uniform_f(c, 0.0f));
                ntq_store_dest(c, &instr->dest, 1, vir_uniform_f(c, 0.0f));
                break;

        case nir_intrinsic_load_barycentric_at_sample: {
                if (!c->fs_key->msaa) {
                        ntq_store_dest(c, &instr->dest, 0, vir_uniform_f(c, 0.0f));
                        ntq_store_dest(c, &instr->dest, 1, vir_uniform_f(c, 0.0f));
                        return;
                }

                struct qreg offset_x, offset_y;
                struct qreg sample_idx = ntq_get_src(c, instr->src[0], 0);
                ntq_get_sample_offset(c, sample_idx, &offset_x, &offset_y);

                ntq_store_dest(c, &instr->dest, 0, vir_MOV(c, offset_x));
                ntq_store_dest(c, &instr->dest, 1, vir_MOV(c, offset_y));
                break;
        }

        case nir_intrinsic_load_barycentric_sample: {
                struct qreg offset_x =
                        vir_FSUB(c, vir_FXCD(c), vir_ITOF(c, vir_XCD(c)));
                struct qreg offset_y =
                        vir_FSUB(c, vir_FYCD(c), vir_ITOF(c, vir_YCD(c)));

                ntq_store_dest(c, &instr->dest, 0,
                                  vir_FSUB(c, offset_x, vir_uniform_f(c, 0.5f)));
                ntq_store_dest(c, &instr->dest, 1,
                                  vir_FSUB(c, offset_y, vir_uniform_f(c, 0.5f)));
                break;
        }

        case nir_intrinsic_load_barycentric_centroid: {
                struct qreg offset_x, offset_y;
                ntq_get_barycentric_centroid(c, &offset_x, &offset_y);
                ntq_store_dest(c, &instr->dest, 0, vir_MOV(c, offset_x));
                ntq_store_dest(c, &instr->dest, 1, vir_MOV(c, offset_y));
                break;
        }

        case nir_intrinsic_load_interpolated_input: {
                assert(nir_src_is_const(instr->src[1]));
                const uint32_t offset = nir_src_as_uint(instr->src[1]);

                for (int i = 0; i < instr->num_components; i++) {
                        const uint32_t input_idx =
                                (nir_intrinsic_base(instr) + offset) * 4 +
                                nir_intrinsic_component(instr) + i;

                        /* If we are not in MSAA or if we are not interpolating
                         * a user varying, just return the pre-computed
                         * interpolated input.
                         */
                        if (!c->fs_key->msaa ||
                            c->interp[input_idx].vp.file == QFILE_NULL) {
                                ntq_store_dest(c, &instr->dest, i,
                                               vir_MOV(c, c->inputs[input_idx]));
                                continue;
                        }

                        /* Otherwise compute interpolation at the specified
                         * offset.
                         */
                        struct qreg p = c->interp[input_idx].vp;
                        struct qreg C = c->interp[input_idx].C;
                        unsigned interp_mode =  c->interp[input_idx].mode;

                        struct qreg offset_x = ntq_get_src(c, instr->src[0], 0);
                        struct qreg offset_y = ntq_get_src(c, instr->src[0], 1);

                        struct qreg result =
                              ntq_emit_load_interpolated_input(c, p, C,
                                                               offset_x, offset_y,
                                                               interp_mode);
                        ntq_store_dest(c, &instr->dest, i, result);
                }
                break;
        }

        case nir_intrinsic_load_subgroup_size:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform_ui(c, V3D_CHANNELS));
                break;

        case nir_intrinsic_load_subgroup_invocation:
                ntq_store_dest(c, &instr->dest, 0, vir_EIDX(c));
                break;

        case nir_intrinsic_elect: {
                set_a_flags_for_subgroup(c);
                struct qreg first = vir_FLAFIRST(c);

                /* Produce a boolean result from Flafirst */
                vir_set_pf(c, vir_XOR_dest(c, vir_nop_reg(),
                                           first, vir_uniform_ui(c, 1)),
                                           V3D_QPU_PF_PUSHZ);
                struct qreg result = ntq_emit_cond_to_bool(c, V3D_QPU_COND_IFA);
                ntq_store_dest(c, &instr->dest, 0, result);
                break;
        }

        case nir_intrinsic_load_num_subgroups:
                unreachable("Should have been lowered");
                break;

        case nir_intrinsic_load_view_index:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_VIEW_INDEX, 0));
                break;

        default:
                fprintf(stderr, "Unknown intrinsic: ");
                nir_print_instr(&instr->instr, stderr);
                fprintf(stderr, "\n");
                break;
        }
}

/* Clears (activates) the execute flags for any channels whose jump target
 * matches this block.
 *
 * XXX perf: Could we be using flpush/flpop somehow for our execution channel
 * enabling?
 *
 */
static void
ntq_activate_execute_for_block(struct v3d_compile *c)
{
        vir_set_pf(c, vir_XOR_dest(c, vir_nop_reg(),
                                c->execute, vir_uniform_ui(c, c->cur_block->index)),
                   V3D_QPU_PF_PUSHZ);

        vir_MOV_cond(c, V3D_QPU_COND_IFA, c->execute, vir_uniform_ui(c, 0));
}

static void
ntq_emit_uniform_if(struct v3d_compile *c, nir_if *if_stmt)
{
        nir_block *nir_else_block = nir_if_first_else_block(if_stmt);
        bool empty_else_block =
                (nir_else_block == nir_if_last_else_block(if_stmt) &&
                 exec_list_is_empty(&nir_else_block->instr_list));

        struct qblock *then_block = vir_new_block(c);
        struct qblock *after_block = vir_new_block(c);
        struct qblock *else_block;
        if (empty_else_block)
                else_block = after_block;
        else
                else_block = vir_new_block(c);

        /* Check if this if statement is really just a conditional jump with
         * the form:
         *
         * if (cond) {
         *    break/continue;
         * } else {
         * }
         *
         * In which case we can skip the jump to ELSE we emit before the THEN
         * block and instead just emit the break/continue directly.
         */
        nir_jump_instr *conditional_jump = NULL;
        if (empty_else_block) {
                nir_block *nir_then_block = nir_if_first_then_block(if_stmt);
                struct nir_instr *inst = nir_block_first_instr(nir_then_block);
                if (inst && inst->type == nir_instr_type_jump)
                        conditional_jump = nir_instr_as_jump(inst);
        }

        /* Set up the flags for the IF condition (taking the THEN branch). */
        enum v3d_qpu_cond cond = ntq_emit_bool_to_cond(c, if_stmt->condition);

        if (!conditional_jump) {
                /* Jump to ELSE. */
                struct qinst *branch = vir_BRANCH(c, cond == V3D_QPU_COND_IFA ?
                           V3D_QPU_BRANCH_COND_ANYNA :
                           V3D_QPU_BRANCH_COND_ANYA);
                /* Pixels that were not dispatched or have been discarded
                 * should not contribute to the ANYA/ANYNA condition.
                 */
                branch->qpu.branch.msfign = V3D_QPU_MSFIGN_P;

                vir_link_blocks(c->cur_block, else_block);
                vir_link_blocks(c->cur_block, then_block);

                /* Process the THEN block. */
                vir_set_emit_block(c, then_block);
                ntq_emit_cf_list(c, &if_stmt->then_list);

                if (!empty_else_block) {
                        /* At the end of the THEN block, jump to ENDIF, unless
                         * the block ended in a break or continue.
                         */
                        if (!c->cur_block->branch_emitted) {
                                vir_BRANCH(c, V3D_QPU_BRANCH_COND_ALWAYS);
                                vir_link_blocks(c->cur_block, after_block);
                        }

                        /* Emit the else block. */
                        vir_set_emit_block(c, else_block);
                        ntq_emit_cf_list(c, &if_stmt->else_list);
                }
        } else {
                /* Emit the conditional jump directly.
                 *
                 * Use ALL with breaks and ANY with continues to ensure that
                 * we always break and never continue when all lanes have been
                 * disabled (for example because of discards) to prevent
                 * infinite loops.
                 */
                assert(conditional_jump &&
                       (conditional_jump->type == nir_jump_continue ||
                        conditional_jump->type == nir_jump_break));

                struct qinst *branch = vir_BRANCH(c, cond == V3D_QPU_COND_IFA ?
                           (conditional_jump->type == nir_jump_break ?
                            V3D_QPU_BRANCH_COND_ALLA :
                            V3D_QPU_BRANCH_COND_ANYA) :
                           (conditional_jump->type == nir_jump_break ?
                            V3D_QPU_BRANCH_COND_ALLNA :
                            V3D_QPU_BRANCH_COND_ANYNA));
                branch->qpu.branch.msfign = V3D_QPU_MSFIGN_P;

                vir_link_blocks(c->cur_block,
                                conditional_jump->type == nir_jump_break ?
                                        c->loop_break_block :
                                        c->loop_cont_block);
        }

        vir_link_blocks(c->cur_block, after_block);

        vir_set_emit_block(c, after_block);
}

static void
ntq_emit_nonuniform_if(struct v3d_compile *c, nir_if *if_stmt)
{
        nir_block *nir_else_block = nir_if_first_else_block(if_stmt);
        bool empty_else_block =
                (nir_else_block == nir_if_last_else_block(if_stmt) &&
                 exec_list_is_empty(&nir_else_block->instr_list));

        struct qblock *then_block = vir_new_block(c);
        struct qblock *after_block = vir_new_block(c);
        struct qblock *else_block;
        if (empty_else_block)
                else_block = after_block;
        else
                else_block = vir_new_block(c);

        bool was_uniform_control_flow = false;
        if (!vir_in_nonuniform_control_flow(c)) {
                c->execute = vir_MOV(c, vir_uniform_ui(c, 0));
                was_uniform_control_flow = true;
        }

        /* Set up the flags for the IF condition (taking the THEN branch). */
        enum v3d_qpu_cond cond = ntq_emit_bool_to_cond(c, if_stmt->condition);

        /* Update the flags+cond to mean "Taking the ELSE branch (!cond) and
         * was previously active (execute Z) for updating the exec flags.
         */
        if (was_uniform_control_flow) {
                cond = v3d_qpu_cond_invert(cond);
        } else {
                struct qinst *inst = vir_MOV_dest(c, vir_nop_reg(), c->execute);
                if (cond == V3D_QPU_COND_IFA) {
                        vir_set_uf(c, inst, V3D_QPU_UF_NORNZ);
                } else {
                        vir_set_uf(c, inst, V3D_QPU_UF_ANDZ);
                        cond = V3D_QPU_COND_IFA;
                }
        }

        vir_MOV_cond(c, cond,
                     c->execute,
                     vir_uniform_ui(c, else_block->index));

        /* Jump to ELSE if nothing is active for THEN, otherwise fall
         * through.
         */
        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), c->execute), V3D_QPU_PF_PUSHZ);
        vir_BRANCH(c, V3D_QPU_BRANCH_COND_ALLNA);
        vir_link_blocks(c->cur_block, else_block);
        vir_link_blocks(c->cur_block, then_block);

        /* Process the THEN block. */
        vir_set_emit_block(c, then_block);
        ntq_emit_cf_list(c, &if_stmt->then_list);

        if (!empty_else_block) {
                /* Handle the end of the THEN block.  First, all currently
                 * active channels update their execute flags to point to
                 * ENDIF
                 */
                vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), c->execute),
                           V3D_QPU_PF_PUSHZ);
                vir_MOV_cond(c, V3D_QPU_COND_IFA, c->execute,
                             vir_uniform_ui(c, after_block->index));

                /* If everything points at ENDIF, then jump there immediately. */
                vir_set_pf(c, vir_XOR_dest(c, vir_nop_reg(),
                                        c->execute,
                                        vir_uniform_ui(c, after_block->index)),
                           V3D_QPU_PF_PUSHZ);
                vir_BRANCH(c, V3D_QPU_BRANCH_COND_ALLA);
                vir_link_blocks(c->cur_block, after_block);
                vir_link_blocks(c->cur_block, else_block);

                vir_set_emit_block(c, else_block);
                ntq_activate_execute_for_block(c);
                ntq_emit_cf_list(c, &if_stmt->else_list);
        }

        vir_link_blocks(c->cur_block, after_block);

        vir_set_emit_block(c, after_block);
        if (was_uniform_control_flow)
                c->execute = c->undef;
        else
                ntq_activate_execute_for_block(c);
}

static void
ntq_emit_if(struct v3d_compile *c, nir_if *nif)
{
        bool was_in_control_flow = c->in_control_flow;
        c->in_control_flow = true;
        if (!vir_in_nonuniform_control_flow(c) &&
            !nir_src_is_divergent(nif->condition)) {
                ntq_emit_uniform_if(c, nif);
        } else {
                ntq_emit_nonuniform_if(c, nif);
        }
        c->in_control_flow = was_in_control_flow;
}

static void
ntq_emit_jump(struct v3d_compile *c, nir_jump_instr *jump)
{
        switch (jump->type) {
        case nir_jump_break:
                vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), c->execute),
                           V3D_QPU_PF_PUSHZ);
                vir_MOV_cond(c, V3D_QPU_COND_IFA, c->execute,
                             vir_uniform_ui(c, c->loop_break_block->index));
                break;

        case nir_jump_continue:
                vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), c->execute),
                           V3D_QPU_PF_PUSHZ);
                vir_MOV_cond(c, V3D_QPU_COND_IFA, c->execute,
                             vir_uniform_ui(c, c->loop_cont_block->index));
                break;

        case nir_jump_return:
                unreachable("All returns should be lowered\n");
                break;

        case nir_jump_halt:
        case nir_jump_goto:
        case nir_jump_goto_if:
                unreachable("not supported\n");
                break;
        }
}

static void
ntq_emit_uniform_jump(struct v3d_compile *c, nir_jump_instr *jump)
{
        switch (jump->type) {
        case nir_jump_break:
                vir_BRANCH(c, V3D_QPU_BRANCH_COND_ALWAYS);
                vir_link_blocks(c->cur_block, c->loop_break_block);
                c->cur_block->branch_emitted = true;
                break;
        case nir_jump_continue:
                vir_BRANCH(c, V3D_QPU_BRANCH_COND_ALWAYS);
                vir_link_blocks(c->cur_block, c->loop_cont_block);
                c->cur_block->branch_emitted = true;
                break;

        case nir_jump_return:
                unreachable("All returns should be lowered\n");
                break;

        case nir_jump_halt:
        case nir_jump_goto:
        case nir_jump_goto_if:
                unreachable("not supported\n");
                break;
        }
}

static void
ntq_emit_instr(struct v3d_compile *c, nir_instr *instr)
{
        switch (instr->type) {
        case nir_instr_type_alu:
                ntq_emit_alu(c, nir_instr_as_alu(instr));
                break;

        case nir_instr_type_intrinsic:
                ntq_emit_intrinsic(c, nir_instr_as_intrinsic(instr));
                break;

        case nir_instr_type_load_const:
                ntq_emit_load_const(c, nir_instr_as_load_const(instr));
                break;

        case nir_instr_type_ssa_undef:
                unreachable("Should've been lowered by nir_lower_undef_to_zero");
                break;

        case nir_instr_type_tex:
                ntq_emit_tex(c, nir_instr_as_tex(instr));
                break;

        case nir_instr_type_jump:
                /* Always flush TMU before jumping to another block, for the
                 * same reasons as in ntq_emit_block.
                 */
                ntq_flush_tmu(c);
                if (vir_in_nonuniform_control_flow(c))
                        ntq_emit_jump(c, nir_instr_as_jump(instr));
                else
                        ntq_emit_uniform_jump(c, nir_instr_as_jump(instr));
                break;

        default:
                fprintf(stderr, "Unknown NIR instr type: ");
                nir_print_instr(instr, stderr);
                fprintf(stderr, "\n");
                abort();
        }
}

static void
ntq_emit_block(struct v3d_compile *c, nir_block *block)
{
        nir_foreach_instr(instr, block) {
                ntq_emit_instr(c, instr);
        }

        /* Always process pending TMU operations in the same block they were
         * emitted: we can't emit TMU operations in a block and then emit a
         * thread switch and LDTMU/TMUWT for them in another block, possibly
         * under control flow.
         */
        ntq_flush_tmu(c);
}

static void ntq_emit_cf_list(struct v3d_compile *c, struct exec_list *list);

static void
ntq_emit_nonuniform_loop(struct v3d_compile *c, nir_loop *loop)
{
        bool was_uniform_control_flow = false;
        if (!vir_in_nonuniform_control_flow(c)) {
                c->execute = vir_MOV(c, vir_uniform_ui(c, 0));
                was_uniform_control_flow = true;
        }

        c->loop_cont_block = vir_new_block(c);
        c->loop_break_block = vir_new_block(c);

        vir_link_blocks(c->cur_block, c->loop_cont_block);
        vir_set_emit_block(c, c->loop_cont_block);
        ntq_activate_execute_for_block(c);

        ntq_emit_cf_list(c, &loop->body);

        /* Re-enable any previous continues now, so our ANYA check below
         * works.
         *
         * XXX: Use the .ORZ flags update, instead.
         */
        vir_set_pf(c, vir_XOR_dest(c,
                                vir_nop_reg(),
                                c->execute,
                                vir_uniform_ui(c, c->loop_cont_block->index)),
                   V3D_QPU_PF_PUSHZ);
        vir_MOV_cond(c, V3D_QPU_COND_IFA, c->execute, vir_uniform_ui(c, 0));

        vir_set_pf(c, vir_MOV_dest(c, vir_nop_reg(), c->execute), V3D_QPU_PF_PUSHZ);

        struct qinst *branch = vir_BRANCH(c, V3D_QPU_BRANCH_COND_ANYA);
        /* Pixels that were not dispatched or have been discarded should not
         * contribute to looping again.
         */
        branch->qpu.branch.msfign = V3D_QPU_MSFIGN_P;
        vir_link_blocks(c->cur_block, c->loop_cont_block);
        vir_link_blocks(c->cur_block, c->loop_break_block);

        vir_set_emit_block(c, c->loop_break_block);
        if (was_uniform_control_flow)
                c->execute = c->undef;
        else
                ntq_activate_execute_for_block(c);
}

static void
ntq_emit_uniform_loop(struct v3d_compile *c, nir_loop *loop)
{

        c->loop_cont_block = vir_new_block(c);
        c->loop_break_block = vir_new_block(c);

        vir_link_blocks(c->cur_block, c->loop_cont_block);
        vir_set_emit_block(c, c->loop_cont_block);

        ntq_emit_cf_list(c, &loop->body);

        if (!c->cur_block->branch_emitted) {
                vir_BRANCH(c, V3D_QPU_BRANCH_COND_ALWAYS);
                vir_link_blocks(c->cur_block, c->loop_cont_block);
        }

        vir_set_emit_block(c, c->loop_break_block);
}

static void
ntq_emit_loop(struct v3d_compile *c, nir_loop *loop)
{
        bool was_in_control_flow = c->in_control_flow;
        c->in_control_flow = true;

        struct qblock *save_loop_cont_block = c->loop_cont_block;
        struct qblock *save_loop_break_block = c->loop_break_block;

        if (vir_in_nonuniform_control_flow(c) || loop->divergent) {
                ntq_emit_nonuniform_loop(c, loop);
        } else {
                ntq_emit_uniform_loop(c, loop);
        }

        c->loop_break_block = save_loop_break_block;
        c->loop_cont_block = save_loop_cont_block;

        c->loops++;

        c->in_control_flow = was_in_control_flow;
}

static void
ntq_emit_function(struct v3d_compile *c, nir_function_impl *func)
{
        fprintf(stderr, "FUNCTIONS not handled.\n");
        abort();
}

static void
ntq_emit_cf_list(struct v3d_compile *c, struct exec_list *list)
{
        foreach_list_typed(nir_cf_node, node, node, list) {
                switch (node->type) {
                case nir_cf_node_block:
                        ntq_emit_block(c, nir_cf_node_as_block(node));
                        break;

                case nir_cf_node_if:
                        ntq_emit_if(c, nir_cf_node_as_if(node));
                        break;

                case nir_cf_node_loop:
                        ntq_emit_loop(c, nir_cf_node_as_loop(node));
                        break;

                case nir_cf_node_function:
                        ntq_emit_function(c, nir_cf_node_as_function(node));
                        break;

                default:
                        fprintf(stderr, "Unknown NIR node type\n");
                        abort();
                }
        }
}

static void
ntq_emit_impl(struct v3d_compile *c, nir_function_impl *impl)
{
        ntq_setup_registers(c, &impl->registers);
        ntq_emit_cf_list(c, &impl->body);
}

static void
nir_to_vir(struct v3d_compile *c)
{
        switch (c->s->info.stage) {
        case MESA_SHADER_FRAGMENT:
                c->payload_w = vir_MOV(c, vir_reg(QFILE_REG, 0));
                c->payload_w_centroid = vir_MOV(c, vir_reg(QFILE_REG, 1));
                c->payload_z = vir_MOV(c, vir_reg(QFILE_REG, 2));

                /* V3D 4.x can disable implicit varyings if they are not used */
                c->fs_uses_primitive_id =
                        nir_find_variable_with_location(c->s, nir_var_shader_in,
                                                        VARYING_SLOT_PRIMITIVE_ID);
                if (c->fs_uses_primitive_id && !c->fs_key->has_gs) {
                       c->primitive_id =
                               emit_fragment_varying(c, NULL, -1, 0, 0);
                }

                if (c->fs_key->is_points &&
                    (c->devinfo->ver < 40 || program_reads_point_coord(c))) {
                        c->point_x = emit_fragment_varying(c, NULL, -1, 0, 0);
                        c->point_y = emit_fragment_varying(c, NULL, -1, 0, 0);
                        c->uses_implicit_point_line_varyings = true;
                } else if (c->fs_key->is_lines &&
                           (c->devinfo->ver < 40 ||
                            BITSET_TEST(c->s->info.system_values_read,
                                        SYSTEM_VALUE_LINE_COORD))) {
                        c->line_x = emit_fragment_varying(c, NULL, -1, 0, 0);
                        c->uses_implicit_point_line_varyings = true;
                }

                c->force_per_sample_msaa =
                   c->s->info.fs.uses_sample_qualifier ||
                   BITSET_TEST(c->s->info.system_values_read,
                               SYSTEM_VALUE_SAMPLE_ID) ||
                   BITSET_TEST(c->s->info.system_values_read,
                               SYSTEM_VALUE_SAMPLE_POS);
                break;
        case MESA_SHADER_COMPUTE:
                /* Set up the TSO for barriers, assuming we do some. */
                if (c->devinfo->ver < 42) {
                        vir_BARRIERID_dest(c, vir_reg(QFILE_MAGIC,
                                                      V3D_QPU_WADDR_SYNC));
                }

                c->cs_payload[0] = vir_MOV(c, vir_reg(QFILE_REG, 0));
                c->cs_payload[1] = vir_MOV(c, vir_reg(QFILE_REG, 2));

                /* Set up the division between gl_LocalInvocationIndex and
                 * wg_in_mem in the payload reg.
                 */
                int wg_size = (c->s->info.workgroup_size[0] *
                               c->s->info.workgroup_size[1] *
                               c->s->info.workgroup_size[2]);
                c->local_invocation_index_bits =
                        ffs(util_next_power_of_two(MAX2(wg_size, 64))) - 1;
                assert(c->local_invocation_index_bits <= 8);

                if (c->s->info.shared_size) {
                        struct qreg wg_in_mem = vir_SHR(c, c->cs_payload[1],
                                                        vir_uniform_ui(c, 16));
                        if (c->s->info.workgroup_size[0] != 1 ||
                            c->s->info.workgroup_size[1] != 1 ||
                            c->s->info.workgroup_size[2] != 1) {
                                int wg_bits = (16 -
                                               c->local_invocation_index_bits);
                                int wg_mask = (1 << wg_bits) - 1;
                                wg_in_mem = vir_AND(c, wg_in_mem,
                                                    vir_uniform_ui(c, wg_mask));
                        }
                        struct qreg shared_per_wg =
                                vir_uniform_ui(c, c->s->info.shared_size);

                        c->cs_shared_offset =
                                vir_ADD(c,
                                        vir_uniform(c, QUNIFORM_SHARED_OFFSET,0),
                                        vir_UMUL(c, wg_in_mem, shared_per_wg));
                }
                break;
        default:
                break;
        }

        if (c->s->scratch_size) {
                v3d_setup_spill_base(c);
                c->spill_size += V3D_CHANNELS * c->s->scratch_size;
        }

        switch (c->s->info.stage) {
        case MESA_SHADER_VERTEX:
                ntq_setup_vs_inputs(c);
                break;
        case MESA_SHADER_GEOMETRY:
                ntq_setup_gs_inputs(c);
                break;
        case MESA_SHADER_FRAGMENT:
                ntq_setup_fs_inputs(c);
                break;
        case MESA_SHADER_COMPUTE:
                break;
        default:
                unreachable("unsupported shader stage");
        }

        ntq_setup_outputs(c);

        /* Find the main function and emit the body. */
        nir_foreach_function(function, c->s) {
                assert(strcmp(function->name, "main") == 0);
                assert(function->impl);
                ntq_emit_impl(c, function->impl);
        }
}

/**
 * When demoting a shader down to single-threaded, removes the THRSW
 * instructions (one will still be inserted at v3d_vir_to_qpu() for the
 * program end).
 */
static void
vir_remove_thrsw(struct v3d_compile *c)
{
        vir_for_each_block(block, c) {
                vir_for_each_inst_safe(inst, block) {
                        if (inst->qpu.sig.thrsw)
                                vir_remove_instruction(c, inst);
                }
        }

        c->last_thrsw = NULL;
}

/**
 * This makes sure we have a top-level last thread switch which signals the
 * start of the last thread section, which may include adding a new thrsw
 * instruction if needed. We don't allow spilling in the last thread section, so
 * if we need to do any spills that inject additional thread switches later on,
 * we ensure this thread switch will still be the last thread switch in the
 * program, which makes last thread switch signalling a lot easier when we have
 * spilling. If in the end we don't need to spill to compile the program and we
 * injected a new thread switch instruction here only for that, we will
 * eventually restore the previous last thread switch and remove the one we
 * added here.
 */
static void
vir_emit_last_thrsw(struct v3d_compile *c,
                    struct qinst **restore_last_thrsw,
                    bool *restore_scoreboard_lock)
{
        *restore_last_thrsw = c->last_thrsw;

        /* On V3D before 4.1, we need a TMU op to be outstanding when thread
         * switching, so disable threads if we didn't do any TMU ops (each of
         * which would have emitted a THRSW).
         */
        if (!c->last_thrsw_at_top_level && c->devinfo->ver < 41) {
                c->threads = 1;
                if (c->last_thrsw)
                        vir_remove_thrsw(c);
                *restore_last_thrsw = NULL;
        }

        /* If we're threaded and the last THRSW was in conditional code, then
         * we need to emit another one so that we can flag it as the last
         * thrsw.
         */
        if (c->last_thrsw && !c->last_thrsw_at_top_level) {
                assert(c->devinfo->ver >= 41);
                vir_emit_thrsw(c);
        }

        /* If we're threaded, then we need to mark the last THRSW instruction
         * so we can emit a pair of them at QPU emit time.
         *
         * For V3D 4.x, we can spawn the non-fragment shaders already in the
         * post-last-THRSW state, so we can skip this.
         */
        if (!c->last_thrsw && c->s->info.stage == MESA_SHADER_FRAGMENT) {
                assert(c->devinfo->ver >= 41);
                vir_emit_thrsw(c);
        }

        /* If we have not inserted a last thread switch yet, do it now to ensure
         * any potential spilling we do happens before this. If we don't spill
         * in the end, we will restore the previous one.
         */
        if (*restore_last_thrsw == c->last_thrsw) {
                if (*restore_last_thrsw)
                        (*restore_last_thrsw)->is_last_thrsw = false;
                *restore_scoreboard_lock = c->lock_scoreboard_on_first_thrsw;
                vir_emit_thrsw(c);
        } else {
                *restore_last_thrsw = c->last_thrsw;
        }

        assert(c->last_thrsw);
        c->last_thrsw->is_last_thrsw = true;
}

static void
vir_restore_last_thrsw(struct v3d_compile *c,
                       struct qinst *thrsw,
                       bool scoreboard_lock)
{
        assert(c->last_thrsw);
        vir_remove_instruction(c, c->last_thrsw);
        c->last_thrsw = thrsw;
        if (c->last_thrsw)
                c->last_thrsw->is_last_thrsw = true;
        c->lock_scoreboard_on_first_thrsw = scoreboard_lock;
}

/* There's a flag in the shader for "center W is needed for reasons other than
 * non-centroid varyings", so we just walk the program after VIR optimization
 * to see if it's used.  It should be harmless to set even if we only use
 * center W for varyings.
 */
static void
vir_check_payload_w(struct v3d_compile *c)
{
        if (c->s->info.stage != MESA_SHADER_FRAGMENT)
                return;

        vir_for_each_inst_inorder(inst, c) {
                for (int i = 0; i < vir_get_nsrc(inst); i++) {
                        if (inst->src[i].file == QFILE_REG &&
                            inst->src[i].index == 0) {
                                c->uses_center_w = true;
                                return;
                        }
                }
        }
}

void
v3d_nir_to_vir(struct v3d_compile *c)
{
        if (V3D_DEBUG & (V3D_DEBUG_NIR |
                         v3d_debug_flag_for_shader_stage(c->s->info.stage))) {
                fprintf(stderr, "%s prog %d/%d NIR:\n",
                        vir_get_stage_name(c),
                        c->program_id, c->variant_id);
                nir_print_shader(c->s, stderr);
        }

        nir_to_vir(c);

        bool restore_scoreboard_lock = false;
        struct qinst *restore_last_thrsw;

        /* Emit the last THRSW before STVPM and TLB writes. */
        vir_emit_last_thrsw(c,
                            &restore_last_thrsw,
                            &restore_scoreboard_lock);


        switch (c->s->info.stage) {
        case MESA_SHADER_FRAGMENT:
                emit_frag_end(c);
                break;
        case MESA_SHADER_GEOMETRY:
                emit_geom_end(c);
                break;
        case MESA_SHADER_VERTEX:
                emit_vert_end(c);
                break;
        case MESA_SHADER_COMPUTE:
                break;
        default:
                unreachable("bad stage");
        }

        if (V3D_DEBUG & (V3D_DEBUG_VIR |
                         v3d_debug_flag_for_shader_stage(c->s->info.stage))) {
                fprintf(stderr, "%s prog %d/%d pre-opt VIR:\n",
                        vir_get_stage_name(c),
                        c->program_id, c->variant_id);
                vir_dump(c);
                fprintf(stderr, "\n");
        }

        vir_optimize(c);

        vir_check_payload_w(c);

        /* XXX perf: On VC4, we do a VIR-level instruction scheduling here.
         * We used that on that platform to pipeline TMU writes and reduce the
         * number of thread switches, as well as try (mostly successfully) to
         * reduce maximum register pressure to allow more threads.  We should
         * do something of that sort for V3D -- either instruction scheduling
         * here, or delay the the THRSW and LDTMUs from our texture
         * instructions until the results are needed.
         */

        if (V3D_DEBUG & (V3D_DEBUG_VIR |
                         v3d_debug_flag_for_shader_stage(c->s->info.stage))) {
                fprintf(stderr, "%s prog %d/%d VIR:\n",
                        vir_get_stage_name(c),
                        c->program_id, c->variant_id);
                vir_dump(c);
                fprintf(stderr, "\n");
        }

        /* Attempt to allocate registers for the temporaries.  If we fail,
         * reduce thread count and try again.
         */
        int min_threads = (c->devinfo->ver >= 41) ? 2 : 1;
        struct qpu_reg *temp_registers;
        while (true) {
                bool spilled;
                temp_registers = v3d_register_allocate(c, &spilled);
                if (spilled)
                        continue;

                if (temp_registers)
                        break;

                if (c->threads == min_threads &&
                    (V3D_DEBUG & V3D_DEBUG_RA)) {
                        fprintf(stderr,
                                "Failed to register allocate using %s\n",
                                c->fallback_scheduler ? "the fallback scheduler:" :
                                "the normal scheduler: \n");

                        vir_dump(c);

                        char *shaderdb;
                        int ret = v3d_shaderdb_dump(c, &shaderdb);
                        if (ret > 0) {
                                fprintf(stderr, "%s\n", shaderdb);
                                free(shaderdb);
                        }
                }

                if (c->threads <= MAX2(c->min_threads_for_reg_alloc, min_threads)) {
                        if (V3D_DEBUG & V3D_DEBUG_PERF) {
                                fprintf(stderr,
                                        "Failed to register allocate %s at "
                                        "%d threads.\n", vir_get_stage_name(c),
                                        c->threads);
                        }
                        c->compilation_result =
                                V3D_COMPILATION_FAILED_REGISTER_ALLOCATION;
                        return;
                }

                c->spill_count = 0;
                c->threads /= 2;

                if (c->threads == 1)
                        vir_remove_thrsw(c);
        }

        /* If we didn't spill, then remove the last thread switch we injected
         * artificially (if any) and restore the previous one.
         */
        if (!c->spills && c->last_thrsw != restore_last_thrsw)
                vir_restore_last_thrsw(c, restore_last_thrsw, restore_scoreboard_lock);

        if (c->spills &&
            (V3D_DEBUG & (V3D_DEBUG_VIR |
                          v3d_debug_flag_for_shader_stage(c->s->info.stage)))) {
                fprintf(stderr, "%s prog %d/%d spilled VIR:\n",
                        vir_get_stage_name(c),
                        c->program_id, c->variant_id);
                vir_dump(c);
                fprintf(stderr, "\n");
        }

        v3d_vir_to_qpu(c, temp_registers);
}
