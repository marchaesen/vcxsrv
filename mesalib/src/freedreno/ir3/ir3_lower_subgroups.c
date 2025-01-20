/*
 * Copyright © 2021 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "ir3.h"
#include "ir3_nir.h"
#include "util/ralloc.h"

/* Lower several macro-instructions needed for shader subgroup support that
 * must be turned into if statements. We do this after RA and post-RA
 * scheduling to give the scheduler a chance to rearrange them, because RA
 * may need to insert OPC_META_READ_FIRST to handle splitting live ranges, and
 * also because some (e.g. BALLOT and READ_FIRST) must produce a shared
 * register that cannot be spilled to a normal register until after the if,
 * which makes implementing spilling more complicated if they are already
 * lowered.
 */

static void
replace_pred(struct ir3_block *block, struct ir3_block *old_pred,
             struct ir3_block *new_pred)
{
   for (unsigned i = 0; i < block->predecessors_count; i++) {
      if (block->predecessors[i] == old_pred) {
         block->predecessors[i] = new_pred;
         return;
      }
   }
}

static void
replace_physical_pred(struct ir3_block *block, struct ir3_block *old_pred,
                      struct ir3_block *new_pred)
{
   for (unsigned i = 0; i < block->physical_predecessors_count; i++) {
      if (block->physical_predecessors[i] == old_pred) {
         block->physical_predecessors[i] = new_pred;
         return;
      }
   }
}

static void
mov_immed(struct ir3_register *dst, struct ir3_block *block, unsigned immed)
{
   struct ir3_instruction *mov =
      ir3_instr_create_at(ir3_before_terminator(block), OPC_MOV, 1, 1);
   struct ir3_register *mov_dst = ir3_dst_create(mov, dst->num, dst->flags);
   mov_dst->wrmask = dst->wrmask;
   struct ir3_register *src = ir3_src_create(
      mov, INVALID_REG, (dst->flags & IR3_REG_HALF) | IR3_REG_IMMED);
   src->uim_val = immed;
   mov->cat1.dst_type = (dst->flags & IR3_REG_HALF) ? TYPE_U16 : TYPE_U32;
   mov->cat1.src_type = mov->cat1.dst_type;
   mov->repeat = util_last_bit(mov_dst->wrmask) - 1;
}

static void
mov_reg(struct ir3_block *block, struct ir3_register *dst,
        struct ir3_register *src)
{
   struct ir3_instruction *mov =
      ir3_instr_create_at(ir3_before_terminator(block), OPC_MOV, 1, 1);

   struct ir3_register *mov_dst =
      ir3_dst_create(mov, dst->num, dst->flags & (IR3_REG_HALF | IR3_REG_SHARED));
   struct ir3_register *mov_src =
      ir3_src_create(mov, src->num, src->flags & (IR3_REG_HALF | IR3_REG_SHARED));
   mov_dst->wrmask = dst->wrmask;
   mov_src->wrmask = src->wrmask;
   mov->repeat = util_last_bit(mov_dst->wrmask) - 1;

   mov->cat1.dst_type = (dst->flags & IR3_REG_HALF) ? TYPE_U16 : TYPE_U32;
   mov->cat1.src_type = (src->flags & IR3_REG_HALF) ? TYPE_U16 : TYPE_U32;
}

static void
binop(struct ir3_block *block, opc_t opc, struct ir3_register *dst,
      struct ir3_register *src0, struct ir3_register *src1)
{
   struct ir3_instruction *instr =
      ir3_instr_create_at(ir3_before_terminator(block), opc, 1, 2);

   unsigned flags = dst->flags & IR3_REG_HALF;
   struct ir3_register *instr_dst = ir3_dst_create(instr, dst->num, flags);
   struct ir3_register *instr_src0 = ir3_src_create(instr, src0->num, flags);
   struct ir3_register *instr_src1 = ir3_src_create(instr, src1->num, flags);

   instr_dst->wrmask = dst->wrmask;
   instr_src0->wrmask = src0->wrmask;
   instr_src1->wrmask = src1->wrmask;
   instr->repeat = util_last_bit(instr_dst->wrmask) - 1;
}

static void
triop(struct ir3_block *block, opc_t opc, struct ir3_register *dst,
      struct ir3_register *src0, struct ir3_register *src1,
      struct ir3_register *src2)
{
   struct ir3_instruction *instr =
      ir3_instr_create_at(ir3_before_terminator(block), opc, 1, 3);

   unsigned flags = dst->flags & IR3_REG_HALF;
   struct ir3_register *instr_dst = ir3_dst_create(instr, dst->num, flags);
   struct ir3_register *instr_src0 = ir3_src_create(instr, src0->num, flags);
   struct ir3_register *instr_src1 = ir3_src_create(instr, src1->num, flags);
   struct ir3_register *instr_src2 = ir3_src_create(instr, src2->num, flags);

   instr_dst->wrmask = dst->wrmask;
   instr_src0->wrmask = src0->wrmask;
   instr_src1->wrmask = src1->wrmask;
   instr_src2->wrmask = src2->wrmask;
   instr->repeat = util_last_bit(instr_dst->wrmask) - 1;
}

static void
do_reduce(struct ir3_block *block, reduce_op_t opc,
          struct ir3_register *dst, struct ir3_register *src0,
          struct ir3_register *src1)
{
   switch (opc) {
#define CASE(name)                                                             \
   case REDUCE_OP_##name:                                                      \
      binop(block, OPC_##name, dst, src0, src1);                               \
      break;

   CASE(ADD_U)
   CASE(ADD_F)
   CASE(MUL_F)
   CASE(MIN_U)
   CASE(MIN_S)
   CASE(MIN_F)
   CASE(MAX_U)
   CASE(MAX_S)
   CASE(MAX_F)
   CASE(AND_B)
   CASE(OR_B)
   CASE(XOR_B)

#undef CASE

   case REDUCE_OP_MUL_U:
      if (dst->flags & IR3_REG_HALF) {
         binop(block, OPC_MUL_S24, dst, src0, src1);
      } else {
         /* 32-bit multiplication macro - see ir3_nir_imul */
         binop(block, OPC_MULL_U, dst, src0, src1);
         triop(block, OPC_MADSH_M16, dst, src0, src1, dst);
         triop(block, OPC_MADSH_M16, dst, src1, src0, dst);
      }
      break;
   }
}

static struct ir3_block *
split_block(struct ir3 *ir, struct ir3_block *before_block,
            struct ir3_instruction *instr)
{
   struct ir3_block *after_block = ir3_block_create(ir);
   list_add(&after_block->node, &before_block->node);

   for (unsigned i = 0; i < ARRAY_SIZE(before_block->successors); i++) {
      after_block->successors[i] = before_block->successors[i];
      if (after_block->successors[i])
         replace_pred(after_block->successors[i], before_block, after_block);
   }

   for (unsigned i = 0; i < before_block->physical_successors_count; i++) {
      replace_physical_pred(before_block->physical_successors[i],
                            before_block, after_block);
   }

   ralloc_steal(after_block, before_block->physical_successors);
   after_block->physical_successors = before_block->physical_successors;
   after_block->physical_successors_sz = before_block->physical_successors_sz;
   after_block->physical_successors_count =
      before_block->physical_successors_count;

   before_block->successors[0] = before_block->successors[1] = NULL;
   before_block->physical_successors = NULL;
   before_block->physical_successors_count = 0;
   before_block->physical_successors_sz = 0;

   foreach_instr_from_safe (rem_instr, &instr->node,
                            &before_block->instr_list) {
      list_del(&rem_instr->node);
      list_addtail(&rem_instr->node, &after_block->instr_list);
      rem_instr->block = after_block;
   }

   after_block->divergent_condition = before_block->divergent_condition;
   before_block->divergent_condition = false;
   return after_block;
}

static void
link_blocks(struct ir3_block *pred, struct ir3_block *succ, unsigned index)
{
   pred->successors[index] = succ;
   ir3_block_add_predecessor(succ, pred);
   ir3_block_link_physical(pred, succ);
}

static void
link_blocks_jump(struct ir3_block *pred, struct ir3_block *succ)
{
   struct ir3_builder build = ir3_builder_at(ir3_after_block(pred));
   ir3_JUMP(&build);
   link_blocks(pred, succ, 0);
}

static void
link_blocks_branch(struct ir3_block *pred, struct ir3_block *target,
                   struct ir3_block *fallthrough, unsigned opc, unsigned flags,
                   struct ir3_instruction *condition)
{
   unsigned nsrc = condition ? 1 : 0;
   struct ir3_instruction *branch =
      ir3_instr_create_at(ir3_after_block(pred), opc, 0, nsrc);
   branch->flags |= flags;

   if (condition) {
      struct ir3_register *cond_dst = condition->dsts[0];
      struct ir3_register *src =
         ir3_src_create(branch, cond_dst->num, cond_dst->flags);
      src->def = cond_dst;
   }

   link_blocks(pred, target, 0);
   link_blocks(pred, fallthrough, 1);

   if (opc != OPC_BALL && opc != OPC_BANY) {
      pred->divergent_condition = true;
   }
}

static struct ir3_block *
create_if(struct ir3 *ir, struct ir3_block *before_block,
          struct ir3_block *after_block, unsigned opc, unsigned flags,
          struct ir3_instruction *condition)
{
   struct ir3_block *then_block = ir3_block_create(ir);
   list_add(&then_block->node, &before_block->node);

   link_blocks_branch(before_block, then_block, after_block, opc, flags,
                      condition);
   link_blocks_jump(then_block, after_block);

   return then_block;
}

static bool
lower_instr(struct ir3 *ir, struct ir3_block **block, struct ir3_instruction *instr)
{
   switch (instr->opc) {
   case OPC_BALLOT_MACRO:
   case OPC_ANY_MACRO:
   case OPC_ALL_MACRO:
   case OPC_ELECT_MACRO:
   case OPC_READ_COND_MACRO:
   case OPC_READ_GETLAST_MACRO:
   case OPC_SCAN_MACRO:
   case OPC_SCAN_CLUSTERS_MACRO:
      break;
   case OPC_READ_FIRST_MACRO:
      /* Moves to shared registers read the first active fiber, so we can just
       * turn read_first.macro into a move. However we must still use the macro
       * and lower it late because in ir3_cp we need to distinguish between
       * moves where all source fibers contain the same value, which can be copy
       * propagated, and moves generated from API-level ReadFirstInvocation
       * which cannot.
       */
      assert(instr->dsts[0]->flags & IR3_REG_SHARED);
      instr->opc = OPC_MOV;
      instr->cat1.dst_type = TYPE_U32;
      instr->cat1.src_type =
         (instr->srcs[0]->flags & IR3_REG_HALF) ? TYPE_U16 : TYPE_U32;
      return false;
   default:
      return false;
   }

   struct ir3_block *before_block = *block;
   struct ir3_block *after_block = split_block(ir, before_block, instr);

   if (instr->opc == OPC_SCAN_MACRO) {
      /* The pseudo-code for the scan macro is:
       *
       * while (true) {
       *    header:
       *    if (elect()) {
       *       exit:
       *       exclusive = reduce;
       *       inclusive = src OP exclusive;
       *       reduce = inclusive;
       *       break;
       *    }
       *    footer:
       * }
       *
       * This is based on the blob's sequence, and carefully crafted to avoid
       * using the shared register "reduce" except in move instructions, since
       * using it in the actual OP isn't possible for half-registers.
       */
      struct ir3_block *header = ir3_block_create(ir);
      list_add(&header->node, &before_block->node);

      struct ir3_block *exit = ir3_block_create(ir);
      list_add(&exit->node, &header->node);

      struct ir3_block *footer = ir3_block_create(ir);
      list_add(&footer->node, &exit->node);
      footer->reconvergence_point = true;

      after_block->reconvergence_point = true;

      link_blocks_jump(before_block, header);

      link_blocks_branch(header, exit, footer, OPC_GETONE,
                         IR3_INSTR_NEEDS_HELPERS, NULL);

      link_blocks_jump(exit, after_block);
      ir3_block_link_physical(exit, footer);

      link_blocks_jump(footer, header);

      struct ir3_register *exclusive = instr->dsts[0];
      struct ir3_register *inclusive = instr->dsts[1];
      struct ir3_register *reduce = instr->dsts[2];
      struct ir3_register *src = instr->srcs[0];

      mov_reg(exit, exclusive, reduce);
      do_reduce(exit, instr->cat1.reduce_op, inclusive, src, exclusive);
      mov_reg(exit, reduce, inclusive);
   } else if (instr->opc == OPC_SCAN_CLUSTERS_MACRO) {
      /* The pseudo-code for the scan macro is:
       *
       * while (true) {
       *    body:
       *    scratch = reduce;
       *
       *    inclusive = inclusive_src OP scratch;
       *
       *    static if (is exclusive scan)
       *       exclusive = exclusive_src OP scratch
       *
       *    if (getlast()) {
       *       store:
       *       reduce = inclusive;
       *       if (elect())
       *           break;
       *    } else {
       *       break;
       *    }
       * }
       * after_block:
       */
      struct ir3_block *body = ir3_block_create(ir);
      list_add(&body->node, &before_block->node);

      struct ir3_block *store = ir3_block_create(ir);
      list_add(&store->node, &body->node);

      after_block->reconvergence_point = true;

      link_blocks_jump(before_block, body);

      link_blocks_branch(body, store, after_block, OPC_GETLAST, 0, NULL);

      link_blocks_branch(store, after_block, body, OPC_GETONE,
                         IR3_INSTR_NEEDS_HELPERS, NULL);

      struct ir3_register *reduce = instr->dsts[0];
      struct ir3_register *inclusive = instr->dsts[1];
      struct ir3_register *inclusive_src = instr->srcs[1];

      /* We need to perform the following operations:
       *  - inclusive = inclusive_src OP reduce
       *  - exclusive = exclusive_src OP reduce (iff exclusive scan)
       * Since reduce is initially in a shared register, we need to copy it to a
       * scratch register before performing the operations.
       *
       * The scratch register used is:
       *  - an explicitly allocated one if op is 32b mul_u.
       *    - necessary because we cannot do 'foo = foo mul_u bar' since mul_u
       *      clobbers its destination.
       *  - exclusive if this is an exclusive scan (and not 32b mul_u).
       *    - since we calculate inclusive first.
       *  - inclusive otherwise.
       *
       * In all cases, this is the last destination.
       */
      struct ir3_register *scratch = instr->dsts[instr->dsts_count - 1];

      mov_reg(body, scratch, reduce);
      do_reduce(body, instr->cat1.reduce_op, inclusive, inclusive_src, scratch);

      /* exclusive scan */
      if (instr->srcs_count == 3) {
         struct ir3_register *exclusive_src = instr->srcs[2];
         struct ir3_register *exclusive = instr->dsts[2];
         do_reduce(body, instr->cat1.reduce_op, exclusive, exclusive_src,
                   scratch);
      }

      mov_reg(store, reduce, inclusive);
   } else {
      /* For ballot, the destination must be initialized to 0 before we do
       * the movmsk because the condition may be 0 and then the movmsk will
       * be skipped.
       */
      if (instr->opc == OPC_BALLOT_MACRO) {
         mov_immed(instr->dsts[0], before_block, 0);
      }

      struct ir3_instruction *condition = NULL;
      unsigned branch_opc = 0;
      unsigned branch_flags = 0;

      switch (instr->opc) {
      case OPC_BALLOT_MACRO:
      case OPC_READ_COND_MACRO:
      case OPC_ANY_MACRO:
      case OPC_ALL_MACRO:
         condition = instr->srcs[0]->def->instr;
         break;
      default:
         break;
      }

      switch (instr->opc) {
      case OPC_BALLOT_MACRO:
      case OPC_READ_COND_MACRO:
         after_block->reconvergence_point = true;
         branch_opc = OPC_BR;
         break;
      case OPC_ANY_MACRO:
         branch_opc = OPC_BANY;
         break;
      case OPC_ALL_MACRO:
         branch_opc = OPC_BALL;
         break;
      case OPC_ELECT_MACRO:
         after_block->reconvergence_point = true;
         branch_opc = OPC_GETONE;
         branch_flags = instr->flags & IR3_INSTR_NEEDS_HELPERS;
         break;
      case OPC_READ_GETLAST_MACRO:
         after_block->reconvergence_point = true;
         branch_opc = OPC_GETLAST;
         branch_flags = instr->flags & IR3_INSTR_NEEDS_HELPERS;
         break;
      default:
         unreachable("bad opcode");
      }

      struct ir3_block *then_block =
         create_if(ir, before_block, after_block, branch_opc, branch_flags,
                   condition);

      switch (instr->opc) {
      case OPC_ALL_MACRO:
      case OPC_ANY_MACRO:
      case OPC_ELECT_MACRO:
         mov_immed(instr->dsts[0], then_block, 1);
         mov_immed(instr->dsts[0], before_block, 0);
         break;

      case OPC_BALLOT_MACRO: {
         unsigned wrmask = instr->dsts[0]->wrmask;
         unsigned comp_count = util_last_bit(wrmask);
         struct ir3_instruction *movmsk = ir3_instr_create_at(
            ir3_before_terminator(then_block), OPC_MOVMSK, 1, 0);
         struct ir3_register *dst =
            ir3_dst_create(movmsk, instr->dsts[0]->num, instr->dsts[0]->flags);
         dst->wrmask = wrmask;
         movmsk->repeat = comp_count - 1;
         break;
      }

      case OPC_READ_GETLAST_MACRO:
      case OPC_READ_COND_MACRO: {
         struct ir3_instruction *mov = ir3_instr_create_at(
            ir3_before_terminator(then_block), OPC_MOV, 1, 1);
         ir3_dst_create(mov, instr->dsts[0]->num, instr->dsts[0]->flags);
         struct ir3_register *new_src = ir3_src_create(mov, 0, 0);
         unsigned idx = instr->opc == OPC_READ_COND_MACRO ? 1 : 0;
         *new_src = *instr->srcs[idx];
         mov->cat1.dst_type = TYPE_U32;
         mov->cat1.src_type =
            (new_src->flags & IR3_REG_HALF) ? TYPE_U16 : TYPE_U32;
         mov->flags |= IR3_INSTR_NEEDS_HELPERS;
         break;
      }

      default:
         unreachable("bad opcode");
      }
   }

   *block = after_block;
   list_delinit(&instr->node);
   return true;
}

static bool
lower_block(struct ir3 *ir, struct ir3_block **block)
{
   bool progress = true;

   bool inner_progress;
   do {
      inner_progress = false;
      foreach_instr (instr, &(*block)->instr_list) {
         if (lower_instr(ir, block, instr)) {
            /* restart the loop with the new block we created because the
             * iterator has been invalidated.
             */
            progress = inner_progress = true;
            break;
         }
      }
   } while (inner_progress);

   return progress;
}

bool
ir3_lower_subgroups(struct ir3 *ir)
{
   bool progress = false;

   foreach_block (block, &ir->block_list)
      progress |= lower_block(ir, &block);

   return progress;
}

static const struct glsl_type *
glsl_type_for_def(nir_def *def)
{
   assert(def->num_components == 1);
   return def->bit_size == 1 ? glsl_bool_type()
                             : glsl_uintN_t_type(def->bit_size);
}

static bool
filter_scan_reduce(const nir_instr *instr, const void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_reduce:
   case nir_intrinsic_inclusive_scan:
   case nir_intrinsic_exclusive_scan:
      return true;
   default:
      return false;
   }
}

typedef nir_def *(*reduce_cluster)(nir_builder *, nir_op, nir_def *);

/* Execute `reduce` for each cluster in the subgroup with only the invocations
 * in the current cluster active.
 */
static nir_def *
foreach_cluster(nir_builder *b, nir_op op, nir_def *inclusive,
                unsigned cluster_size, reduce_cluster reduce)
{
   nir_def *id = nir_load_subgroup_invocation(b);
   nir_def *cluster_size_imm = nir_imm_int(b, cluster_size);

   /* cur_cluster_end = cluster_size;
    * while (true) {
    *    if (gl_SubgroupInvocationID < cur_cluster_end) {
    *       cluster_val = reduce(inclusive);
    *       break;
    *    }
    *
    *    cur_cluster_end += cluster_size;
    * }
    */
   nir_variable *cur_cluster_end_var =
      nir_local_variable_create(b->impl, glsl_uint_type(), "cur_cluster_end");
   nir_store_var(b, cur_cluster_end_var, cluster_size_imm, 1);
   nir_variable *cluster_val_var = nir_local_variable_create(
      b->impl, glsl_type_for_def(inclusive), "cluster_val");

   nir_loop *loop = nir_push_loop(b);
   {
      nir_def *cur_cluster_end = nir_load_var(b, cur_cluster_end_var);
      nir_def *in_cur_cluster = nir_ult(b, id, cur_cluster_end);

      nir_if *nif = nir_push_if(b, in_cur_cluster);
      {
         nir_def *reduced = reduce(b, op, inclusive);
         nir_store_var(b, cluster_val_var, reduced, 1);
         nir_jump(b, nir_jump_break);
      }
      nir_pop_if(b, nif);

      nir_def *next_cluster_end =
         nir_iadd(b, cur_cluster_end, cluster_size_imm);
      nir_store_var(b, cur_cluster_end_var, next_cluster_end, 1);
   }
   nir_pop_loop(b, loop);

   return nir_load_var(b, cluster_val_var);
}

static nir_def *
read_last(nir_builder *b, nir_op op, nir_def *val)
{
   return nir_read_getlast_ir3(b, val);
}

static nir_def *
reduce_clusters(nir_builder *b, nir_op op, nir_def *val)
{
   return nir_reduce_clusters_ir3(b, val, .reduction_op = op);
}

static nir_def *
lower_scan_reduce(struct nir_builder *b, nir_instr *instr, void *data)
{
   struct ir3_shader_variant *v = data;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   unsigned bit_size = intrin->def.bit_size;
   assert(bit_size < 64);

   nir_op op = nir_intrinsic_reduction_op(intrin);
   nir_const_value ident_val = nir_alu_binop_identity(op, bit_size);
   nir_def *ident = nir_build_imm(b, 1, bit_size, &ident_val);
   nir_def *inclusive = intrin->src[0].ssa;
   nir_def *exclusive = ident;
   unsigned cluster_size = nir_intrinsic_has_cluster_size(intrin)
                              ? nir_intrinsic_cluster_size(intrin)
                              : 0;
   bool clustered = cluster_size != 0;
   unsigned subgroup_size, max_subgroup_size;
   ir3_shader_get_subgroup_size(v->compiler, &v->shader_options, v->type,
                                &subgroup_size, &max_subgroup_size);

   if (subgroup_size == 0) {
      subgroup_size = max_subgroup_size;
   }

   /* Should have been lowered by nir_lower_subgroups. */
   assert(cluster_size != 1);

   /* Only clustered reduce operations are supported. */
   assert(intrin->intrinsic == nir_intrinsic_reduce || !clustered);

   unsigned max_brcst_cluster_size = clustered ? MIN2(cluster_size, 8) : 8;

   for (unsigned brcst_cluster_size = 2;
        brcst_cluster_size <= max_brcst_cluster_size; brcst_cluster_size *= 2) {
      nir_def *brcst = nir_brcst_active_ir3(b, ident, inclusive,
                                            .cluster_size = brcst_cluster_size);
      inclusive = nir_build_alu2(b, op, inclusive, brcst);

      if (intrin->intrinsic == nir_intrinsic_exclusive_scan)
         exclusive = nir_build_alu2(b, op, exclusive, brcst);
   }

   switch (intrin->intrinsic) {
   case nir_intrinsic_reduce:
      if (!clustered || cluster_size >= subgroup_size) {
         /* The normal (non-clustered) path does a full reduction of all brcst
          * clusters.
          */
         return nir_reduce_clusters_ir3(b, inclusive, .reduction_op = op);
      } else if (cluster_size <= 8) {
         /* After the brcsts have been executed, each brcst cluster has its
          * reduction in its last fiber. So if the cluster size is at most the
          * maximum brcst cluster size (8) we can simply iterate the clusters
          * and read the value from their last fibers.
          */
         return foreach_cluster(b, op, inclusive, cluster_size, read_last);
      } else {
         /* For larger clusters, we do a normal reduction for every cluster.
          */
         return foreach_cluster(b, op, inclusive, cluster_size,
                                reduce_clusters);
      }
   case nir_intrinsic_inclusive_scan:
      return nir_inclusive_scan_clusters_ir3(b, inclusive, .reduction_op = op);
   case nir_intrinsic_exclusive_scan:
      return nir_exclusive_scan_clusters_ir3(b, inclusive, exclusive,
                                             .reduction_op = op);
   default:
      unreachable("filtered intrinsic");
   }
}

bool
ir3_nir_opt_subgroups(nir_shader *nir, struct ir3_shader_variant *v)
{
   if (!v->compiler->has_getfiberid)
      return false;

   return nir_shader_lower_instructions(nir, filter_scan_reduce,
                                        lower_scan_reduce, v);
}

bool
ir3_nir_lower_subgroups_filter(const nir_instr *instr, const void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   const struct ir3_compiler *compiler = data;

   switch (intrin->intrinsic) {
   case nir_intrinsic_reduce:
      if (nir_intrinsic_cluster_size(intrin) == 1) {
         return true;
      }
      if (nir_intrinsic_cluster_size(intrin) > 0 && !compiler->has_getfiberid) {
         return true;
      }
      FALLTHROUGH;
   case nir_intrinsic_inclusive_scan:
   case nir_intrinsic_exclusive_scan:
      switch (nir_intrinsic_reduction_op(intrin)) {
      case nir_op_imul:
      case nir_op_imin:
      case nir_op_imax:
      case nir_op_umin:
      case nir_op_umax:
         if (intrin->def.bit_size == 64) {
            return true;
         }
         FALLTHROUGH;
      default:
         return intrin->def.num_components > 1;
      }
   default:
      return true;
   }
}

static bool
filter_shuffle(const nir_instr *instr, const void *data)
{
   if (instr->type != nir_instr_type_intrinsic) {
      return false;
   }

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_shuffle:
   case nir_intrinsic_shuffle_up:
   case nir_intrinsic_shuffle_down:
   case nir_intrinsic_shuffle_xor:
      return true;
   default:
      return false;
   }
}

static nir_def *
shuffle_to_uniform(nir_builder *b, nir_intrinsic_op op, struct nir_def *val,
                   struct nir_def *id)
{
   switch (op) {
   case nir_intrinsic_shuffle:
      return nir_rotate(b, val, id);
   case nir_intrinsic_shuffle_up:
      return nir_shuffle_up_uniform_ir3(b, val, id);
   case nir_intrinsic_shuffle_down:
      return nir_shuffle_down_uniform_ir3(b, val, id);
   case nir_intrinsic_shuffle_xor:
      return nir_shuffle_xor_uniform_ir3(b, val, id);
   default:
      unreachable("filtered intrinsic");
   }
}

/* Transforms a shuffle operation into a loop that only uses shuffles with
 * (dynamically) uniform indices. This is based on the blob's sequence and
 * carefully makes sure that the least amount of iterations are performed (i.e.,
 * one iteration per distinct index) while keeping all invocations active during
 * each shfl operation. This is necessary since shfl does not update its dst
 * when its src is inactive.
 *
 * done = false;
 * while (true) {
 *    next_index = read_invocation_cond_ir3(index, !done);
 *    shuffled = op_uniform(val, next_index);
 *
 *    if (index == next_index) {
 *       result = shuffled;
 *       done = true;
 *    }
 *
 *    if (subgroupAll(done)) {
 *       break;
 *    }
 * }
 */
static nir_def *
make_shuffle_uniform(nir_builder *b, nir_def *val, nir_def *index,
                     nir_intrinsic_op op)
{
   nir_variable *done =
      nir_local_variable_create(b->impl, glsl_bool_type(), "done");
   nir_store_var(b, done, nir_imm_false(b), 1);
   nir_variable *result =
      nir_local_variable_create(b->impl, glsl_type_for_def(val), "result");

   nir_loop *loop = nir_push_loop(b);
   {
      nir_def *next_index = nir_read_invocation_cond_ir3(
         b, index->bit_size, index, nir_inot(b, nir_load_var(b, done)));
      next_index->divergent = false;
      nir_def *shuffled = shuffle_to_uniform(b, op, val, next_index);

      nir_if *nif = nir_push_if(b, nir_ieq(b, index, next_index));
      {
         nir_store_var(b, result, shuffled, 1);
         nir_store_var(b, done, nir_imm_true(b), 1);
      }
      nir_pop_if(b, nif);

      nir_break_if(b, nir_vote_all(b, 1, nir_load_var(b, done)));
   }
   nir_pop_loop(b, loop);

   return nir_load_var(b, result);
}

static nir_def *
lower_shuffle(nir_builder *b, nir_instr *instr, void *data)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   nir_def *val = intrin->src[0].ssa;
   nir_def *index = intrin->src[1].ssa;

   if (intrin->intrinsic == nir_intrinsic_shuffle) {
      /* The hw only does relative shuffles/rotates so transform shuffle(val, x)
       * into rotate(val, x - gl_SubgroupInvocationID) which is valid since we
       * make sure to only use it with uniform indices.
       */
      index = nir_isub(b, index, nir_load_subgroup_invocation(b));
   }

   if (!index->divergent) {
      return shuffle_to_uniform(b, intrin->intrinsic, val, index);
   }

   return make_shuffle_uniform(b, val, index, intrin->intrinsic);
}

/* Lower (relative) shuffles to be able to use the shfl instruction. One quirk
 * of shfl is that its index has to be dynamically uniform, so we transform the
 * standard NIR intrinsics into ir3-specific ones which require their index to
 * be uniform.
 */
bool
ir3_nir_lower_shuffle(nir_shader *nir, struct ir3_shader *shader)
{
   if (!shader->compiler->has_shfl) {
      return false;
   }

   nir_divergence_analysis(nir);
   return nir_shader_lower_instructions(nir, filter_shuffle, lower_shuffle,
                                        NULL);
}
