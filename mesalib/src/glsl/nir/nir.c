/*
 * Copyright © 2014 Intel Corporation
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
 *
 * Authors:
 *    Connor Abbott (cwabbott0@gmail.com)
 *
 */

#include "nir.h"
#include <assert.h>

nir_shader *
nir_shader_create(void *mem_ctx, const nir_shader_compiler_options *options)
{
   nir_shader *shader = ralloc(mem_ctx, nir_shader);

   exec_list_make_empty(&shader->uniforms);
   exec_list_make_empty(&shader->inputs);
   exec_list_make_empty(&shader->outputs);

   shader->options = options;

   exec_list_make_empty(&shader->functions);
   exec_list_make_empty(&shader->registers);
   exec_list_make_empty(&shader->globals);
   exec_list_make_empty(&shader->system_values);
   shader->reg_alloc = 0;

   shader->num_inputs = 0;
   shader->num_outputs = 0;
   shader->num_uniforms = 0;

   return shader;
}

static nir_register *
reg_create(void *mem_ctx, struct exec_list *list)
{
   nir_register *reg = ralloc(mem_ctx, nir_register);

   reg->parent_instr = NULL;
   reg->uses = _mesa_set_create(mem_ctx, _mesa_hash_pointer,
                                _mesa_key_pointer_equal);
   reg->defs = _mesa_set_create(mem_ctx, _mesa_hash_pointer,
                                _mesa_key_pointer_equal);
   reg->if_uses = _mesa_set_create(mem_ctx, _mesa_hash_pointer,
                                   _mesa_key_pointer_equal);

   reg->num_components = 0;
   reg->num_array_elems = 0;
   reg->is_packed = false;
   reg->name = NULL;

   exec_list_push_tail(list, &reg->node);

   return reg;
}

nir_register *
nir_global_reg_create(nir_shader *shader)
{
   nir_register *reg = reg_create(shader, &shader->registers);
   reg->index = shader->reg_alloc++;
   reg->is_global = true;

   return reg;
}

nir_register *
nir_local_reg_create(nir_function_impl *impl)
{
   nir_register *reg = reg_create(ralloc_parent(impl), &impl->registers);
   reg->index = impl->reg_alloc++;
   reg->is_global = false;

   return reg;
}

void
nir_reg_remove(nir_register *reg)
{
   exec_node_remove(&reg->node);
}

nir_function *
nir_function_create(nir_shader *shader, const char *name)
{
   nir_function *func = ralloc(shader, nir_function);

   exec_list_push_tail(&shader->functions, &func->node);
   exec_list_make_empty(&func->overload_list);
   func->name = name;
   func->shader = shader;

   return func;
}

nir_function_overload *
nir_function_overload_create(nir_function *func)
{
   void *mem_ctx = ralloc_parent(func);

   nir_function_overload *overload = ralloc(mem_ctx, nir_function_overload);

   overload->num_params = 0;
   overload->params = NULL;
   overload->return_type = glsl_void_type();
   overload->impl = NULL;

   exec_list_push_tail(&func->overload_list, &overload->node);
   overload->function = func;

   return overload;
}

void nir_src_copy(nir_src *dest, const nir_src *src, void *mem_ctx)
{
   dest->is_ssa = src->is_ssa;
   if (src->is_ssa) {
      dest->ssa = src->ssa;
   } else {
      dest->reg.base_offset = src->reg.base_offset;
      dest->reg.reg = src->reg.reg;
      if (src->reg.indirect) {
         dest->reg.indirect = ralloc(mem_ctx, nir_src);
         nir_src_copy(dest->reg.indirect, src->reg.indirect, mem_ctx);
      } else {
         dest->reg.indirect = NULL;
      }
   }
}

void nir_dest_copy(nir_dest *dest, const nir_dest *src, void *mem_ctx)
{
   dest->is_ssa = src->is_ssa;
   if (src->is_ssa) {
      dest->ssa = src->ssa;
   } else {
      dest->reg.base_offset = src->reg.base_offset;
      dest->reg.reg = src->reg.reg;
      if (src->reg.indirect) {
         dest->reg.indirect = ralloc(mem_ctx, nir_src);
         nir_src_copy(dest->reg.indirect, src->reg.indirect, mem_ctx);
      } else {
         dest->reg.indirect = NULL;
      }
   }
}

void
nir_alu_src_copy(nir_alu_src *dest, const nir_alu_src *src, void *mem_ctx)
{
   nir_src_copy(&dest->src, &src->src, mem_ctx);
   dest->abs = src->abs;
   dest->negate = src->negate;
   for (unsigned i = 0; i < 4; i++)
      dest->swizzle[i] = src->swizzle[i];
}

void
nir_alu_dest_copy(nir_alu_dest *dest, const nir_alu_dest *src, void *mem_ctx)
{
   nir_dest_copy(&dest->dest, &src->dest, mem_ctx);
   dest->write_mask = src->write_mask;
   dest->saturate = src->saturate;
}

static inline void
block_add_pred(nir_block *block, nir_block *pred)
{
   _mesa_set_add(block->predecessors, pred);
}

static void
cf_init(nir_cf_node *node, nir_cf_node_type type)
{
   exec_node_init(&node->node);
   node->parent = NULL;
   node->type = type;
}

static void
link_blocks(nir_block *pred, nir_block *succ1, nir_block *succ2)
{
   pred->successors[0] = succ1;
   block_add_pred(succ1, pred);

   pred->successors[1] = succ2;
   if (succ2 != NULL)
      block_add_pred(succ2, pred);
}

static void
unlink_blocks(nir_block *pred, nir_block *succ)
{
   if (pred->successors[0] == succ) {
      pred->successors[0] = pred->successors[1];
      pred->successors[1] = NULL;
   } else {
      assert(pred->successors[1] == succ);
      pred->successors[1] = NULL;
   }

   struct set_entry *entry = _mesa_set_search(succ->predecessors, pred);

   assert(entry);

   _mesa_set_remove(succ->predecessors, entry);
}

static void
unlink_block_successors(nir_block *block)
{
   if (block->successors[0] != NULL)
      unlink_blocks(block, block->successors[0]);
   if (block->successors[1] != NULL)
      unlink_blocks(block, block->successors[1]);
}


nir_function_impl *
nir_function_impl_create(nir_function_overload *overload)
{
   assert(overload->impl == NULL);

   void *mem_ctx = ralloc_parent(overload);

   nir_function_impl *impl = ralloc(mem_ctx, nir_function_impl);

   overload->impl = impl;
   impl->overload = overload;

   cf_init(&impl->cf_node, nir_cf_node_function);

   exec_list_make_empty(&impl->body);
   exec_list_make_empty(&impl->registers);
   exec_list_make_empty(&impl->locals);
   impl->num_params = 0;
   impl->params = NULL;
   impl->return_var = NULL;
   impl->reg_alloc = 0;
   impl->ssa_alloc = 0;
   impl->valid_metadata = nir_metadata_none;

   /* create start & end blocks */
   nir_block *start_block = nir_block_create(mem_ctx);
   nir_block *end_block = nir_block_create(mem_ctx);
   start_block->cf_node.parent = &impl->cf_node;
   end_block->cf_node.parent = &impl->cf_node;
   impl->start_block = start_block;
   impl->end_block = end_block;

   exec_list_push_tail(&impl->body, &start_block->cf_node.node);

   start_block->successors[0] = end_block;
   block_add_pred(end_block, start_block);

   return impl;
}

nir_block *
nir_block_create(void *mem_ctx)
{
   nir_block *block = ralloc(mem_ctx, nir_block);

   cf_init(&block->cf_node, nir_cf_node_block);

   block->successors[0] = block->successors[1] = NULL;
   block->predecessors = _mesa_set_create(mem_ctx, _mesa_hash_pointer,
                                          _mesa_key_pointer_equal);
   block->imm_dom = NULL;
   block->dom_frontier = _mesa_set_create(mem_ctx, _mesa_hash_pointer,
                                          _mesa_key_pointer_equal);

   exec_list_make_empty(&block->instr_list);

   return block;
}

static inline void
src_init(nir_src *src)
{
   src->is_ssa = false;
   src->reg.reg = NULL;
   src->reg.indirect = NULL;
   src->reg.base_offset = 0;
}

nir_if *
nir_if_create(void *mem_ctx)
{
   nir_if *if_stmt = ralloc(mem_ctx, nir_if);

   cf_init(&if_stmt->cf_node, nir_cf_node_if);
   src_init(&if_stmt->condition);

   nir_block *then = nir_block_create(mem_ctx);
   exec_list_make_empty(&if_stmt->then_list);
   exec_list_push_tail(&if_stmt->then_list, &then->cf_node.node);
   then->cf_node.parent = &if_stmt->cf_node;

   nir_block *else_stmt = nir_block_create(mem_ctx);
   exec_list_make_empty(&if_stmt->else_list);
   exec_list_push_tail(&if_stmt->else_list, &else_stmt->cf_node.node);
   else_stmt->cf_node.parent = &if_stmt->cf_node;

   return if_stmt;
}

nir_loop *
nir_loop_create(void *mem_ctx)
{
   nir_loop *loop = ralloc(mem_ctx, nir_loop);

   cf_init(&loop->cf_node, nir_cf_node_loop);

   nir_block *body = nir_block_create(mem_ctx);
   exec_list_make_empty(&loop->body);
   exec_list_push_tail(&loop->body, &body->cf_node.node);
   body->cf_node.parent = &loop->cf_node;

   body->successors[0] = body;
   block_add_pred(body, body);

   return loop;
}

static void
instr_init(nir_instr *instr, nir_instr_type type)
{
   instr->type = type;
   instr->block = NULL;
   exec_node_init(&instr->node);
}

static void
dest_init(nir_dest *dest)
{
   dest->is_ssa = false;
   dest->reg.reg = NULL;
   dest->reg.indirect = NULL;
   dest->reg.base_offset = 0;
}

static void
alu_dest_init(nir_alu_dest *dest)
{
   dest_init(&dest->dest);
   dest->saturate = false;
   dest->write_mask = 0xf;
}

static void
alu_src_init(nir_alu_src *src)
{
   src_init(&src->src);
   src->abs = src->negate = false;
   src->swizzle[0] = 0;
   src->swizzle[1] = 1;
   src->swizzle[2] = 2;
   src->swizzle[3] = 3;
}

nir_alu_instr *
nir_alu_instr_create(void *mem_ctx, nir_op op)
{
   unsigned num_srcs = nir_op_infos[op].num_inputs;
   nir_alu_instr *instr =
      ralloc_size(mem_ctx,
                  sizeof(nir_alu_instr) + num_srcs * sizeof(nir_alu_src));

   instr_init(&instr->instr, nir_instr_type_alu);
   instr->op = op;
   alu_dest_init(&instr->dest);
   for (unsigned i = 0; i < num_srcs; i++)
      alu_src_init(&instr->src[i]);

   return instr;
}

nir_jump_instr *
nir_jump_instr_create(void *mem_ctx, nir_jump_type type)
{
   nir_jump_instr *instr = ralloc(mem_ctx, nir_jump_instr);
   instr_init(&instr->instr, nir_instr_type_jump);
   instr->type = type;
   return instr;
}

nir_load_const_instr *
nir_load_const_instr_create(void *mem_ctx, unsigned num_components)
{
   nir_load_const_instr *instr = ralloc(mem_ctx, nir_load_const_instr);
   instr_init(&instr->instr, nir_instr_type_load_const);

   nir_ssa_def_init(&instr->instr, &instr->def, num_components, NULL);

   return instr;
}

nir_intrinsic_instr *
nir_intrinsic_instr_create(void *mem_ctx, nir_intrinsic_op op)
{
   unsigned num_srcs = nir_intrinsic_infos[op].num_srcs;
   nir_intrinsic_instr *instr =
      ralloc_size(mem_ctx,
                  sizeof(nir_intrinsic_instr) + num_srcs * sizeof(nir_src));

   instr_init(&instr->instr, nir_instr_type_intrinsic);
   instr->intrinsic = op;

   if (nir_intrinsic_infos[op].has_dest)
      dest_init(&instr->dest);

   for (unsigned i = 0; i < num_srcs; i++)
      src_init(&instr->src[i]);

   return instr;
}

nir_call_instr *
nir_call_instr_create(void *mem_ctx, nir_function_overload *callee)
{
   nir_call_instr *instr = ralloc(mem_ctx, nir_call_instr);
   instr_init(&instr->instr, nir_instr_type_call);

   instr->callee = callee;
   instr->num_params = callee->num_params;
   instr->params = ralloc_array(mem_ctx, nir_deref_var *, instr->num_params);
   instr->return_deref = NULL;

   return instr;
}

nir_tex_instr *
nir_tex_instr_create(void *mem_ctx, unsigned num_srcs)
{
   nir_tex_instr *instr = ralloc(mem_ctx, nir_tex_instr);
   instr_init(&instr->instr, nir_instr_type_tex);

   dest_init(&instr->dest);

   instr->num_srcs = num_srcs;
   instr->src = ralloc_array(mem_ctx, nir_tex_src, num_srcs);
   for (unsigned i = 0; i < num_srcs; i++)
      src_init(&instr->src[i].src);

   instr->sampler_index = 0;
   instr->sampler_array_size = 0;
   instr->sampler = NULL;

   return instr;
}

nir_phi_instr *
nir_phi_instr_create(void *mem_ctx)
{
   nir_phi_instr *instr = ralloc(mem_ctx, nir_phi_instr);
   instr_init(&instr->instr, nir_instr_type_phi);

   dest_init(&instr->dest);
   exec_list_make_empty(&instr->srcs);
   return instr;
}

nir_parallel_copy_instr *
nir_parallel_copy_instr_create(void *mem_ctx)
{
   nir_parallel_copy_instr *instr = ralloc(mem_ctx, nir_parallel_copy_instr);
   instr_init(&instr->instr, nir_instr_type_parallel_copy);

   exec_list_make_empty(&instr->entries);

   return instr;
}

nir_ssa_undef_instr *
nir_ssa_undef_instr_create(void *mem_ctx, unsigned num_components)
{
   nir_ssa_undef_instr *instr = ralloc(mem_ctx, nir_ssa_undef_instr);
   instr_init(&instr->instr, nir_instr_type_ssa_undef);

   nir_ssa_def_init(&instr->instr, &instr->def, num_components, NULL);

   return instr;
}

nir_deref_var *
nir_deref_var_create(void *mem_ctx, nir_variable *var)
{
   nir_deref_var *deref = ralloc(mem_ctx, nir_deref_var);
   deref->deref.deref_type = nir_deref_type_var;
   deref->deref.child = NULL;
   deref->deref.type = var->type;
   deref->var = var;
   return deref;
}

nir_deref_array *
nir_deref_array_create(void *mem_ctx)
{
   nir_deref_array *deref = ralloc(mem_ctx, nir_deref_array);
   deref->deref.deref_type = nir_deref_type_array;
   deref->deref.child = NULL;
   deref->deref_array_type = nir_deref_array_type_direct;
   src_init(&deref->indirect);
   deref->base_offset = 0;
   return deref;
}

nir_deref_struct *
nir_deref_struct_create(void *mem_ctx, unsigned field_index)
{
   nir_deref_struct *deref = ralloc(mem_ctx, nir_deref_struct);
   deref->deref.deref_type = nir_deref_type_struct;
   deref->deref.child = NULL;
   deref->index = field_index;
   return deref;
}

static nir_deref_var *
copy_deref_var(void *mem_ctx, nir_deref_var *deref)
{
   nir_deref_var *ret = nir_deref_var_create(mem_ctx, deref->var);
   ret->deref.type = deref->deref.type;
   if (deref->deref.child)
      ret->deref.child = nir_copy_deref(mem_ctx, deref->deref.child);
   return ret;
}

static nir_deref_array *
copy_deref_array(void *mem_ctx, nir_deref_array *deref)
{
   nir_deref_array *ret = nir_deref_array_create(mem_ctx);
   ret->base_offset = deref->base_offset;
   ret->deref_array_type = deref->deref_array_type;
   if (deref->deref_array_type == nir_deref_array_type_indirect) {
      nir_src_copy(&ret->indirect, &deref->indirect, mem_ctx);
   }
   ret->deref.type = deref->deref.type;
   if (deref->deref.child)
      ret->deref.child = nir_copy_deref(mem_ctx, deref->deref.child);
   return ret;
}

static nir_deref_struct *
copy_deref_struct(void *mem_ctx, nir_deref_struct *deref)
{
   nir_deref_struct *ret = nir_deref_struct_create(mem_ctx, deref->index);
   ret->deref.type = deref->deref.type;
   if (deref->deref.child)
      ret->deref.child = nir_copy_deref(mem_ctx, deref->deref.child);
   return ret;
}

nir_deref *
nir_copy_deref(void *mem_ctx, nir_deref *deref)
{
   switch (deref->deref_type) {
   case nir_deref_type_var:
      return &copy_deref_var(mem_ctx, nir_deref_as_var(deref))->deref;
   case nir_deref_type_array:
      return &copy_deref_array(mem_ctx, nir_deref_as_array(deref))->deref;
   case nir_deref_type_struct:
      return &copy_deref_struct(mem_ctx, nir_deref_as_struct(deref))->deref;
   default:
      unreachable("Invalid dereference type");
   }

   return NULL;
}


/**
 * \name Control flow modification
 *
 * These functions modify the control flow tree while keeping the control flow
 * graph up-to-date. The invariants respected are:
 * 1. Each then statement, else statement, or loop body must have at least one
 *    control flow node.
 * 2. Each if-statement and loop must have one basic block before it and one
 *    after.
 * 3. Two basic blocks cannot be directly next to each other.
 * 4. If a basic block has a jump instruction, there must be only one and it
 *    must be at the end of the block.
 * 5. The CFG must always be connected - this means that we must insert a fake
 *    CFG edge for loops with no break statement.
 *
 * The purpose of the second one is so that we have places to insert code during
 * GCM, as well as eliminating the possibility of critical edges.
 */
/*@{*/

static void
link_non_block_to_block(nir_cf_node *node, nir_block *block)
{
   if (node->type == nir_cf_node_if) {
      /*
       * We're trying to link an if to a block after it; this just means linking
       * the last block of the then and else branches.
       */

      nir_if *if_stmt = nir_cf_node_as_if(node);

      nir_cf_node *last_then = nir_if_last_then_node(if_stmt);
      assert(last_then->type == nir_cf_node_block);
      nir_block *last_then_block = nir_cf_node_as_block(last_then);

      nir_cf_node *last_else = nir_if_last_else_node(if_stmt);
      assert(last_else->type == nir_cf_node_block);
      nir_block *last_else_block = nir_cf_node_as_block(last_else);

      if (exec_list_is_empty(&last_then_block->instr_list) ||
          nir_block_last_instr(last_then_block)->type != nir_instr_type_jump) {
         unlink_block_successors(last_then_block);
         link_blocks(last_then_block, block, NULL);
      }

      if (exec_list_is_empty(&last_else_block->instr_list) ||
          nir_block_last_instr(last_else_block)->type != nir_instr_type_jump) {
         unlink_block_successors(last_else_block);
         link_blocks(last_else_block, block, NULL);
      }
   } else {
      assert(node->type == nir_cf_node_loop);

      /*
       * We can only get to this codepath if we're inserting a new loop, or
       * at least a loop with no break statements; we can't insert break
       * statements into a loop when we haven't inserted it into the CFG
       * because we wouldn't know which block comes after the loop
       * and therefore, which block should be the successor of the block with
       * the break). Therefore, we need to insert a fake edge (see invariant
       * #5).
       */

      nir_loop *loop = nir_cf_node_as_loop(node);

      nir_cf_node *last = nir_loop_last_cf_node(loop);
      assert(last->type == nir_cf_node_block);
      nir_block *last_block =  nir_cf_node_as_block(last);

      last_block->successors[1] = block;
      block_add_pred(block, last_block);
   }
}

static void
link_block_to_non_block(nir_block *block, nir_cf_node *node)
{
   if (node->type == nir_cf_node_if) {
      /*
       * We're trying to link a block to an if after it; this just means linking
       * the block to the first block of the then and else branches.
       */

      nir_if *if_stmt = nir_cf_node_as_if(node);

      nir_cf_node *first_then = nir_if_first_then_node(if_stmt);
      assert(first_then->type == nir_cf_node_block);
      nir_block *first_then_block = nir_cf_node_as_block(first_then);

      nir_cf_node *first_else = nir_if_first_else_node(if_stmt);
      assert(first_else->type == nir_cf_node_block);
      nir_block *first_else_block = nir_cf_node_as_block(first_else);

      unlink_block_successors(block);
      link_blocks(block, first_then_block, first_else_block);
   } else {
      /*
       * For similar reasons as the corresponding case in
       * link_non_block_to_block(), don't worry about if the loop header has
       * any predecessors that need to be unlinked.
       */

      assert(node->type == nir_cf_node_loop);

      nir_loop *loop = nir_cf_node_as_loop(node);

      nir_cf_node *loop_header = nir_loop_first_cf_node(loop);
      assert(loop_header->type == nir_cf_node_block);
      nir_block *loop_header_block = nir_cf_node_as_block(loop_header);

      unlink_block_successors(block);
      link_blocks(block, loop_header_block, NULL);
   }

}

/**
 * Takes a basic block and inserts a new empty basic block before it, making its
 * predecessors point to the new block. This essentially splits the block into
 * an empty header and a body so that another non-block CF node can be inserted
 * between the two. Note that this does *not* link the two basic blocks, so
 * some kind of cleanup *must* be performed after this call.
 */

static nir_block *
split_block_beginning(nir_block *block)
{
   nir_block *new_block = nir_block_create(ralloc_parent(block));
   new_block->cf_node.parent = block->cf_node.parent;
   exec_node_insert_node_before(&block->cf_node.node, &new_block->cf_node.node);

   struct set_entry *entry;
   set_foreach(block->predecessors, entry) {
      nir_block *pred = (nir_block *) entry->key;

      unlink_blocks(pred, block);
      link_blocks(pred, new_block, NULL);
   }

   return new_block;
}

static void
rewrite_phi_preds(nir_block *block, nir_block *old_pred, nir_block *new_pred)
{
   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_phi)
         break;

      nir_phi_instr *phi = nir_instr_as_phi(instr);
      nir_foreach_phi_src(phi, src) {
         if (src->pred == old_pred) {
            src->pred = new_pred;
            break;
         }
      }
   }
}

/**
 * Moves the successors of source to the successors of dest, leaving both
 * successors of source NULL.
 */

static void
move_successors(nir_block *source, nir_block *dest)
{
   nir_block *succ1 = source->successors[0];
   nir_block *succ2 = source->successors[1];

   if (succ1) {
      unlink_blocks(source, succ1);
      rewrite_phi_preds(succ1, source, dest);
   }

   if (succ2) {
      unlink_blocks(source, succ2);
      rewrite_phi_preds(succ2, source, dest);
   }

   unlink_block_successors(dest);
   link_blocks(dest, succ1, succ2);
}

static nir_block *
split_block_end(nir_block *block)
{
   nir_block *new_block = nir_block_create(ralloc_parent(block));
   new_block->cf_node.parent = block->cf_node.parent;
   exec_node_insert_after(&block->cf_node.node, &new_block->cf_node.node);

   move_successors(block, new_block);

   return new_block;
}

/**
 * Inserts a non-basic block between two basic blocks and links them together.
 */

static void
insert_non_block(nir_block *before, nir_cf_node *node, nir_block *after)
{
   node->parent = before->cf_node.parent;
   exec_node_insert_after(&before->cf_node.node, &node->node);
   link_block_to_non_block(before, node);
   link_non_block_to_block(node, after);
}

/**
 * Inserts a non-basic block before a basic block.
 */

static void
insert_non_block_before_block(nir_cf_node *node, nir_block *block)
{
   /* split off the beginning of block into new_block */
   nir_block *new_block = split_block_beginning(block);

   /* insert our node in between new_block and block */
   insert_non_block(new_block, node, block);
}

static void
insert_non_block_after_block(nir_block *block, nir_cf_node *node)
{
   /* split off the end of block into new_block */
   nir_block *new_block = split_block_end(block);

   /* insert our node in between block and new_block */
   insert_non_block(block, node, new_block);
}

/* walk up the control flow tree to find the innermost enclosed loop */
static nir_loop *
nearest_loop(nir_cf_node *node)
{
   while (node->type != nir_cf_node_loop) {
      node = node->parent;
   }

   return nir_cf_node_as_loop(node);
}

nir_function_impl *
nir_cf_node_get_function(nir_cf_node *node)
{
   while (node->type != nir_cf_node_function) {
      node = node->parent;
   }

   return nir_cf_node_as_function(node);
}

/*
 * update the CFG after a jump instruction has been added to the end of a block
 */

static void
handle_jump(nir_block *block)
{
   nir_instr *instr = nir_block_last_instr(block);
   nir_jump_instr *jump_instr = nir_instr_as_jump(instr);

   unlink_block_successors(block);

   nir_function_impl *impl = nir_cf_node_get_function(&block->cf_node);
   nir_metadata_preserve(impl, nir_metadata_none);

   if (jump_instr->type == nir_jump_break ||
       jump_instr->type == nir_jump_continue) {
      nir_loop *loop = nearest_loop(&block->cf_node);

      if (jump_instr->type == nir_jump_continue) {
         nir_cf_node *first_node = nir_loop_first_cf_node(loop);
         assert(first_node->type == nir_cf_node_block);
         nir_block *first_block = nir_cf_node_as_block(first_node);
         link_blocks(block, first_block, NULL);
      } else {
         nir_cf_node *after = nir_cf_node_next(&loop->cf_node);
         assert(after->type == nir_cf_node_block);
         nir_block *after_block = nir_cf_node_as_block(after);
         link_blocks(block, after_block, NULL);

         /* If we inserted a fake link, remove it */
         nir_cf_node *last = nir_loop_last_cf_node(loop);
         assert(last->type == nir_cf_node_block);
         nir_block *last_block =  nir_cf_node_as_block(last);
         if (last_block->successors[1] != NULL)
            unlink_blocks(last_block, after_block);
      }
   } else {
      assert(jump_instr->type == nir_jump_return);
      link_blocks(block, impl->end_block, NULL);
   }
}

static void
handle_remove_jump(nir_block *block, nir_jump_type type)
{
   unlink_block_successors(block);

   if (exec_node_is_tail_sentinel(block->cf_node.node.next)) {
      nir_cf_node *parent = block->cf_node.parent;
      if (parent->type == nir_cf_node_if) {
         nir_cf_node *next = nir_cf_node_next(parent);
         assert(next->type == nir_cf_node_block);
         nir_block *next_block = nir_cf_node_as_block(next);

         link_blocks(block, next_block, NULL);
      } else {
         assert(parent->type == nir_cf_node_loop);
         nir_loop *loop = nir_cf_node_as_loop(parent);

         nir_cf_node *head = nir_loop_first_cf_node(loop);
         assert(head->type == nir_cf_node_block);
         nir_block *head_block = nir_cf_node_as_block(head);

         link_blocks(block, head_block, NULL);
      }
   } else {
      nir_cf_node *next = nir_cf_node_next(&block->cf_node);
      if (next->type == nir_cf_node_if) {
         nir_if *next_if = nir_cf_node_as_if(next);

         nir_cf_node *first_then = nir_if_first_then_node(next_if);
         assert(first_then->type == nir_cf_node_block);
         nir_block *first_then_block = nir_cf_node_as_block(first_then);

         nir_cf_node *first_else = nir_if_first_else_node(next_if);
         assert(first_else->type == nir_cf_node_block);
         nir_block *first_else_block = nir_cf_node_as_block(first_else);

         link_blocks(block, first_then_block, first_else_block);
      } else {
         assert(next->type == nir_cf_node_loop);
         nir_loop *next_loop = nir_cf_node_as_loop(next);

         nir_cf_node *first = nir_loop_first_cf_node(next_loop);
         assert(first->type == nir_cf_node_block);
         nir_block *first_block = nir_cf_node_as_block(first);

         link_blocks(block, first_block, NULL);
      }
   }

   if (type == nir_jump_break) {
      nir_loop *loop = nearest_loop(&block->cf_node);

      nir_cf_node *next = nir_cf_node_next(&loop->cf_node);
      assert(next->type == nir_cf_node_block);
      nir_block *next_block = nir_cf_node_as_block(next);

      if (next_block->predecessors->entries == 0) {
         /* insert fake link */
         nir_cf_node *last = nir_loop_last_cf_node(loop);
         assert(last->type == nir_cf_node_block);
         nir_block *last_block = nir_cf_node_as_block(last);

         last_block->successors[1] = next_block;
         block_add_pred(next_block, last_block);
      }
   }

   nir_function_impl *impl = nir_cf_node_get_function(&block->cf_node);
   nir_metadata_preserve(impl, nir_metadata_none);
}

/**
 * Inserts a basic block before another by merging the instructions.
 *
 * @param block the target of the insertion
 * @param before the block to be inserted - must not have been inserted before
 * @param has_jump whether \before has a jump instruction at the end
 */

static void
insert_block_before_block(nir_block *block, nir_block *before, bool has_jump)
{
   assert(!has_jump || exec_list_is_empty(&block->instr_list));

   foreach_list_typed(nir_instr, instr, node, &before->instr_list) {
      instr->block = block;
   }

   exec_list_prepend(&block->instr_list, &before->instr_list);

   if (has_jump)
      handle_jump(block);
}

/**
 * Inserts a basic block after another by merging the instructions.
 *
 * @param block the target of the insertion
 * @param after the block to be inserted - must not have been inserted before
 * @param has_jump whether \after has a jump instruction at the end
 */

static void
insert_block_after_block(nir_block *block, nir_block *after, bool has_jump)
{
   foreach_list_typed(nir_instr, instr, node, &after->instr_list) {
      instr->block = block;
   }

   exec_list_append(&block->instr_list, &after->instr_list);

   if (has_jump)
      handle_jump(block);
}

static void
update_if_uses(nir_cf_node *node)
{
   if (node->type != nir_cf_node_if)
      return;

   nir_if *if_stmt = nir_cf_node_as_if(node);

   struct set *if_uses_set = if_stmt->condition.is_ssa ?
                             if_stmt->condition.ssa->if_uses :
                             if_stmt->condition.reg.reg->uses;

   _mesa_set_add(if_uses_set, if_stmt);
}

void
nir_cf_node_insert_after(nir_cf_node *node, nir_cf_node *after)
{
   update_if_uses(after);

   if (after->type == nir_cf_node_block) {
      /*
       * either node or the one after it must be a basic block, by invariant #2;
       * in either case, just merge the blocks together.
       */
      nir_block *after_block = nir_cf_node_as_block(after);

      bool has_jump = !exec_list_is_empty(&after_block->instr_list) &&
         nir_block_last_instr(after_block)->type == nir_instr_type_jump;

      if (node->type == nir_cf_node_block) {
         insert_block_after_block(nir_cf_node_as_block(node), after_block,
                                  has_jump);
      } else {
         nir_cf_node *next = nir_cf_node_next(node);
         assert(next->type == nir_cf_node_block);
         nir_block *next_block = nir_cf_node_as_block(next);

         insert_block_before_block(next_block, after_block, has_jump);
      }
   } else {
      if (node->type == nir_cf_node_block) {
         insert_non_block_after_block(nir_cf_node_as_block(node), after);
      } else {
         /*
          * We have to insert a non-basic block after a non-basic block. Since
          * every non-basic block has a basic block after it, this is equivalent
          * to inserting a non-basic block before a basic block.
          */

         nir_cf_node *next = nir_cf_node_next(node);
         assert(next->type == nir_cf_node_block);
         nir_block *next_block = nir_cf_node_as_block(next);

         insert_non_block_before_block(after, next_block);
      }
   }

   nir_function_impl *impl = nir_cf_node_get_function(node);
   nir_metadata_preserve(impl, nir_metadata_none);
}

void
nir_cf_node_insert_before(nir_cf_node *node, nir_cf_node *before)
{
   update_if_uses(before);

   if (before->type == nir_cf_node_block) {
      nir_block *before_block = nir_cf_node_as_block(before);

      bool has_jump = !exec_list_is_empty(&before_block->instr_list) &&
         nir_block_last_instr(before_block)->type == nir_instr_type_jump;

      if (node->type == nir_cf_node_block) {
         insert_block_before_block(nir_cf_node_as_block(node), before_block,
                                   has_jump);
      } else {
         nir_cf_node *prev = nir_cf_node_prev(node);
         assert(prev->type == nir_cf_node_block);
         nir_block *prev_block = nir_cf_node_as_block(prev);

         insert_block_after_block(prev_block, before_block, has_jump);
      }
   } else {
      if (node->type == nir_cf_node_block) {
         insert_non_block_before_block(before, nir_cf_node_as_block(node));
      } else {
         /*
          * We have to insert a non-basic block before a non-basic block. This
          * is equivalent to inserting a non-basic block after a basic block.
          */

         nir_cf_node *prev_node = nir_cf_node_prev(node);
         assert(prev_node->type == nir_cf_node_block);
         nir_block *prev_block = nir_cf_node_as_block(prev_node);

         insert_non_block_after_block(prev_block, before);
      }
   }

   nir_function_impl *impl = nir_cf_node_get_function(node);
   nir_metadata_preserve(impl, nir_metadata_none);
}

void
nir_cf_node_insert_begin(struct exec_list *list, nir_cf_node *node)
{
   nir_cf_node *begin = exec_node_data(nir_cf_node, list->head, node);
   nir_cf_node_insert_before(begin, node);
}

void
nir_cf_node_insert_end(struct exec_list *list, nir_cf_node *node)
{
   nir_cf_node *end = exec_node_data(nir_cf_node, list->tail_pred, node);
   nir_cf_node_insert_after(end, node);
}

/**
 * Stitch two basic blocks together into one. The aggregate must have the same
 * predecessors as the first and the same successors as the second.
 */

static void
stitch_blocks(nir_block *before, nir_block *after)
{
   /*
    * We move after into before, so we have to deal with up to 2 successors vs.
    * possibly a large number of predecessors.
    *
    * TODO: special case when before is empty and after isn't?
    */

   move_successors(after, before);

   foreach_list_typed(nir_instr, instr, node, &after->instr_list) {
      instr->block = before;
   }

   exec_list_append(&before->instr_list, &after->instr_list);
   exec_node_remove(&after->cf_node.node);
}

static void
remove_defs_uses(nir_instr *instr);

static void
cleanup_cf_node(nir_cf_node *node)
{
   switch (node->type) {
   case nir_cf_node_block: {
      nir_block *block = nir_cf_node_as_block(node);
      /* We need to walk the instructions and clean up defs/uses */
      nir_foreach_instr(block, instr)
         remove_defs_uses(instr);
      break;
   }

   case nir_cf_node_if: {
      nir_if *if_stmt = nir_cf_node_as_if(node);
      foreach_list_typed(nir_cf_node, child, node, &if_stmt->then_list)
         cleanup_cf_node(child);
      foreach_list_typed(nir_cf_node, child, node, &if_stmt->else_list)
         cleanup_cf_node(child);

      struct set *if_uses;
      if (if_stmt->condition.is_ssa) {
         if_uses = if_stmt->condition.ssa->if_uses;
      } else {
         if_uses = if_stmt->condition.reg.reg->if_uses;
      }

      struct set_entry *entry = _mesa_set_search(if_uses, if_stmt);
      assert(entry);
      _mesa_set_remove(if_uses, entry);
      break;
   }

   case nir_cf_node_loop: {
      nir_loop *loop = nir_cf_node_as_loop(node);
      foreach_list_typed(nir_cf_node, child, node, &loop->body)
         cleanup_cf_node(child);
      break;
   }
   case nir_cf_node_function: {
      nir_function_impl *impl = nir_cf_node_as_function(node);
      foreach_list_typed(nir_cf_node, child, node, &impl->body)
         cleanup_cf_node(child);
      break;
   }
   default:
      unreachable("Invalid CF node type");
   }
}

void
nir_cf_node_remove(nir_cf_node *node)
{
   nir_function_impl *impl = nir_cf_node_get_function(node);
   nir_metadata_preserve(impl, nir_metadata_none);

   if (node->type == nir_cf_node_block) {
      /*
       * Basic blocks can't really be removed by themselves, since they act as
       * padding between the non-basic blocks. So all we do here is empty the
       * block of instructions.
       *
       * TODO: could we assert here?
       */
      exec_list_make_empty(&nir_cf_node_as_block(node)->instr_list);
   } else {
      nir_cf_node *before = nir_cf_node_prev(node);
      assert(before->type == nir_cf_node_block);
      nir_block *before_block = nir_cf_node_as_block(before);

      nir_cf_node *after = nir_cf_node_next(node);
      assert(after->type == nir_cf_node_block);
      nir_block *after_block = nir_cf_node_as_block(after);

      exec_node_remove(&node->node);
      stitch_blocks(before_block, after_block);
   }

   cleanup_cf_node(node);
}

static bool
add_use_cb(nir_src *src, void *state)
{
   nir_instr *instr = state;

   struct set *uses_set = src->is_ssa ? src->ssa->uses : src->reg.reg->uses;

   _mesa_set_add(uses_set, instr);

   return true;
}

static bool
add_ssa_def_cb(nir_ssa_def *def, void *state)
{
   nir_instr *instr = state;

   if (instr->block && def->index == UINT_MAX) {
      nir_function_impl *impl =
         nir_cf_node_get_function(&instr->block->cf_node);

      def->index = impl->ssa_alloc++;
   }

   return true;
}

static bool
add_reg_def_cb(nir_dest *dest, void *state)
{
   nir_instr *instr = state;

   if (!dest->is_ssa)
      _mesa_set_add(dest->reg.reg->defs, instr);

   return true;
}

static void
add_defs_uses(nir_instr *instr)
{
   nir_foreach_src(instr, add_use_cb, instr);
   nir_foreach_dest(instr, add_reg_def_cb, instr);
   nir_foreach_ssa_def(instr, add_ssa_def_cb, instr);
}

void
nir_instr_insert_before(nir_instr *instr, nir_instr *before)
{
   assert(before->type != nir_instr_type_jump);
   before->block = instr->block;
   add_defs_uses(before);
   exec_node_insert_node_before(&instr->node, &before->node);
}

void
nir_instr_insert_after(nir_instr *instr, nir_instr *after)
{
   if (after->type == nir_instr_type_jump) {
      assert(instr == nir_block_last_instr(instr->block));
      assert(instr->type != nir_instr_type_jump);
   }

   after->block = instr->block;
   add_defs_uses(after);
   exec_node_insert_after(&instr->node, &after->node);

   if (after->type == nir_instr_type_jump)
      handle_jump(after->block);
}

void
nir_instr_insert_before_block(nir_block *block, nir_instr *before)
{
   if (before->type == nir_instr_type_jump)
      assert(exec_list_is_empty(&block->instr_list));

   before->block = block;
   add_defs_uses(before);
   exec_list_push_head(&block->instr_list, &before->node);

   if (before->type == nir_instr_type_jump)
      handle_jump(block);
}

void
nir_instr_insert_after_block(nir_block *block, nir_instr *after)
{
   if (after->type == nir_instr_type_jump) {
      assert(exec_list_is_empty(&block->instr_list) ||
             nir_block_last_instr(block)->type != nir_instr_type_jump);
   }

   after->block = block;
   add_defs_uses(after);
   exec_list_push_tail(&block->instr_list, &after->node);

   if (after->type == nir_instr_type_jump)
      handle_jump(block);
}

void
nir_instr_insert_before_cf(nir_cf_node *node, nir_instr *before)
{
   if (node->type == nir_cf_node_block) {
      nir_instr_insert_before_block(nir_cf_node_as_block(node), before);
   } else {
      nir_cf_node *prev = nir_cf_node_prev(node);
      assert(prev->type == nir_cf_node_block);
      nir_block *prev_block = nir_cf_node_as_block(prev);

      nir_instr_insert_before_block(prev_block, before);
   }
}

void
nir_instr_insert_after_cf(nir_cf_node *node, nir_instr *after)
{
   if (node->type == nir_cf_node_block) {
      nir_instr_insert_after_block(nir_cf_node_as_block(node), after);
   } else {
      nir_cf_node *next = nir_cf_node_next(node);
      assert(next->type == nir_cf_node_block);
      nir_block *next_block = nir_cf_node_as_block(next);

      nir_instr_insert_before_block(next_block, after);
   }
}

void
nir_instr_insert_before_cf_list(struct exec_list *list, nir_instr *before)
{
   nir_cf_node *first_node = exec_node_data(nir_cf_node,
                                            exec_list_get_head(list), node);
   nir_instr_insert_before_cf(first_node, before);
}

void
nir_instr_insert_after_cf_list(struct exec_list *list, nir_instr *after)
{
   nir_cf_node *last_node = exec_node_data(nir_cf_node,
                                           exec_list_get_tail(list), node);
   nir_instr_insert_after_cf(last_node, after);
}

static bool
remove_use_cb(nir_src *src, void *state)
{
   nir_instr *instr = state;

   struct set *uses_set = src->is_ssa ? src->ssa->uses : src->reg.reg->uses;

   struct set_entry *entry = _mesa_set_search(uses_set, instr);
   if (entry)
      _mesa_set_remove(uses_set, entry);

   return true;
}

static bool
remove_def_cb(nir_dest *dest, void *state)
{
   nir_instr *instr = state;

   if (dest->is_ssa)
      return true;

   nir_register *reg = dest->reg.reg;

   struct set_entry *entry = _mesa_set_search(reg->defs, instr);
   if (entry)
      _mesa_set_remove(reg->defs, entry);

   return true;
}

static void
remove_defs_uses(nir_instr *instr)
{
   nir_foreach_dest(instr, remove_def_cb, instr);
   nir_foreach_src(instr, remove_use_cb, instr);
}

void nir_instr_remove(nir_instr *instr)
{
   remove_defs_uses(instr);
   exec_node_remove(&instr->node);

   if (instr->type == nir_instr_type_jump) {
      nir_jump_instr *jump_instr = nir_instr_as_jump(instr);
      handle_remove_jump(instr->block, jump_instr->type);
   }
}

/*@}*/

void
nir_index_local_regs(nir_function_impl *impl)
{
   unsigned index = 0;
   foreach_list_typed(nir_register, reg, node, &impl->registers) {
      reg->index = index++;
   }
   impl->reg_alloc = index;
}

void
nir_index_global_regs(nir_shader *shader)
{
   unsigned index = 0;
   foreach_list_typed(nir_register, reg, node, &shader->registers) {
      reg->index = index++;
   }
   shader->reg_alloc = index;
}

static bool
visit_alu_dest(nir_alu_instr *instr, nir_foreach_dest_cb cb, void *state)
{
   return cb(&instr->dest.dest, state);
}

static bool
visit_intrinsic_dest(nir_intrinsic_instr *instr, nir_foreach_dest_cb cb,
                     void *state)
{
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      return cb(&instr->dest, state);

   return true;
}

static bool
visit_texture_dest(nir_tex_instr *instr, nir_foreach_dest_cb cb,
                   void *state)
{
   return cb(&instr->dest, state);
}

static bool
visit_phi_dest(nir_phi_instr *instr, nir_foreach_dest_cb cb, void *state)
{
   return cb(&instr->dest, state);
}

static bool
visit_parallel_copy_dest(nir_parallel_copy_instr *instr,
                         nir_foreach_dest_cb cb, void *state)
{
   nir_foreach_parallel_copy_entry(instr, entry) {
      if (!cb(&entry->dest, state))
         return false;
   }

   return true;
}

bool
nir_foreach_dest(nir_instr *instr, nir_foreach_dest_cb cb, void *state)
{
   switch (instr->type) {
   case nir_instr_type_alu:
      return visit_alu_dest(nir_instr_as_alu(instr), cb, state);
   case nir_instr_type_intrinsic:
      return visit_intrinsic_dest(nir_instr_as_intrinsic(instr), cb, state);
   case nir_instr_type_tex:
      return visit_texture_dest(nir_instr_as_tex(instr), cb, state);
   case nir_instr_type_phi:
      return visit_phi_dest(nir_instr_as_phi(instr), cb, state);
   case nir_instr_type_parallel_copy:
      return visit_parallel_copy_dest(nir_instr_as_parallel_copy(instr),
                                      cb, state);

   case nir_instr_type_load_const:
   case nir_instr_type_ssa_undef:
   case nir_instr_type_call:
   case nir_instr_type_jump:
      break;

   default:
      unreachable("Invalid instruction type");
      break;
   }

   return true;
}

struct foreach_ssa_def_state {
   nir_foreach_ssa_def_cb cb;
   void *client_state;
};

static inline bool
nir_ssa_def_visitor(nir_dest *dest, void *void_state)
{
   struct foreach_ssa_def_state *state = void_state;

   if (dest->is_ssa)
      return state->cb(&dest->ssa, state->client_state);
   else
      return true;
}

bool
nir_foreach_ssa_def(nir_instr *instr, nir_foreach_ssa_def_cb cb, void *state)
{
   switch (instr->type) {
   case nir_instr_type_alu:
   case nir_instr_type_tex:
   case nir_instr_type_intrinsic:
   case nir_instr_type_phi:
   case nir_instr_type_parallel_copy: {
      struct foreach_ssa_def_state foreach_state = {cb, state};
      return nir_foreach_dest(instr, nir_ssa_def_visitor, &foreach_state);
   }

   case nir_instr_type_load_const:
      return cb(&nir_instr_as_load_const(instr)->def, state);
   case nir_instr_type_ssa_undef:
      return cb(&nir_instr_as_ssa_undef(instr)->def, state);
   case nir_instr_type_call:
   case nir_instr_type_jump:
      return true;
   default:
      unreachable("Invalid instruction type");
   }
}

static bool
visit_src(nir_src *src, nir_foreach_src_cb cb, void *state)
{
   if (!cb(src, state))
      return false;
   if (!src->is_ssa && src->reg.indirect)
      return cb(src->reg.indirect, state);
   return true;
}

static bool
visit_deref_array_src(nir_deref_array *deref, nir_foreach_src_cb cb,
                      void *state)
{
   if (deref->deref_array_type == nir_deref_array_type_indirect)
      return visit_src(&deref->indirect, cb, state);
   return true;
}

static bool
visit_deref_src(nir_deref_var *deref, nir_foreach_src_cb cb, void *state)
{
   nir_deref *cur = &deref->deref;
   while (cur != NULL) {
      if (cur->deref_type == nir_deref_type_array)
         if (!visit_deref_array_src(nir_deref_as_array(cur), cb, state))
            return false;

      cur = cur->child;
   }

   return true;
}

static bool
visit_alu_src(nir_alu_instr *instr, nir_foreach_src_cb cb, void *state)
{
   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++)
      if (!visit_src(&instr->src[i].src, cb, state))
         return false;

   return true;
}

static bool
visit_tex_src(nir_tex_instr *instr, nir_foreach_src_cb cb, void *state)
{
   for (unsigned i = 0; i < instr->num_srcs; i++)
      if (!visit_src(&instr->src[i].src, cb, state))
         return false;

   if (instr->sampler != NULL)
      if (!visit_deref_src(instr->sampler, cb, state))
         return false;

   return true;
}

static bool
visit_intrinsic_src(nir_intrinsic_instr *instr, nir_foreach_src_cb cb,
                    void *state)
{
   unsigned num_srcs = nir_intrinsic_infos[instr->intrinsic].num_srcs;
   for (unsigned i = 0; i < num_srcs; i++)
      if (!visit_src(&instr->src[i], cb, state))
         return false;

   unsigned num_vars =
      nir_intrinsic_infos[instr->intrinsic].num_variables;
   for (unsigned i = 0; i < num_vars; i++)
      if (!visit_deref_src(instr->variables[i], cb, state))
         return false;

   return true;
}

static bool
visit_call_src(nir_call_instr *instr, nir_foreach_src_cb cb, void *state)
{
   return true;
}

static bool
visit_load_const_src(nir_load_const_instr *instr, nir_foreach_src_cb cb,
                     void *state)
{
   return true;
}

static bool
visit_phi_src(nir_phi_instr *instr, nir_foreach_src_cb cb, void *state)
{
   nir_foreach_phi_src(instr, src) {
      if (!visit_src(&src->src, cb, state))
         return false;
   }

   return true;
}

static bool
visit_parallel_copy_src(nir_parallel_copy_instr *instr,
                        nir_foreach_src_cb cb, void *state)
{
   nir_foreach_parallel_copy_entry(instr, entry) {
      if (!visit_src(&entry->src, cb, state))
         return false;
   }

   return true;
}

typedef struct {
   void *state;
   nir_foreach_src_cb cb;
} visit_dest_indirect_state;

static bool
visit_dest_indirect(nir_dest *dest, void *_state)
{
   visit_dest_indirect_state *state = (visit_dest_indirect_state *) _state;

   if (!dest->is_ssa && dest->reg.indirect)
      return state->cb(dest->reg.indirect, state->state);

   return true;
}

bool
nir_foreach_src(nir_instr *instr, nir_foreach_src_cb cb, void *state)
{
   switch (instr->type) {
   case nir_instr_type_alu:
      if (!visit_alu_src(nir_instr_as_alu(instr), cb, state))
         return false;
      break;
   case nir_instr_type_intrinsic:
      if (!visit_intrinsic_src(nir_instr_as_intrinsic(instr), cb, state))
         return false;
      break;
   case nir_instr_type_tex:
      if (!visit_tex_src(nir_instr_as_tex(instr), cb, state))
         return false;
      break;
   case nir_instr_type_call:
      if (!visit_call_src(nir_instr_as_call(instr), cb, state))
         return false;
      break;
   case nir_instr_type_load_const:
      if (!visit_load_const_src(nir_instr_as_load_const(instr), cb, state))
         return false;
      break;
   case nir_instr_type_phi:
      if (!visit_phi_src(nir_instr_as_phi(instr), cb, state))
         return false;
      break;
   case nir_instr_type_parallel_copy:
      if (!visit_parallel_copy_src(nir_instr_as_parallel_copy(instr),
                                   cb, state))
         return false;
      break;
   case nir_instr_type_jump:
   case nir_instr_type_ssa_undef:
      return true;

   default:
      unreachable("Invalid instruction type");
      break;
   }

   visit_dest_indirect_state dest_state;
   dest_state.state = state;
   dest_state.cb = cb;
   return nir_foreach_dest(instr, visit_dest_indirect, &dest_state);
}

nir_const_value *
nir_src_as_const_value(nir_src src)
{
   if (!src.is_ssa)
      return NULL;

   if (src.ssa->parent_instr->type != nir_instr_type_load_const)
      return NULL;

   nir_load_const_instr *load = nir_instr_as_load_const(src.ssa->parent_instr);

   return &load->value;
}

bool
nir_srcs_equal(nir_src src1, nir_src src2)
{
   if (src1.is_ssa) {
      if (src2.is_ssa) {
         return src1.ssa == src2.ssa;
      } else {
         return false;
      }
   } else {
      if (src2.is_ssa) {
         return false;
      } else {
         if ((src1.reg.indirect == NULL) != (src2.reg.indirect == NULL))
            return false;

         if (src1.reg.indirect) {
            if (!nir_srcs_equal(*src1.reg.indirect, *src2.reg.indirect))
               return false;
         }

         return src1.reg.reg == src2.reg.reg &&
                src1.reg.base_offset == src2.reg.base_offset;
      }
   }
}

static bool
src_does_not_use_def(nir_src *src, void *void_def)
{
   nir_ssa_def *def = void_def;

   if (src->is_ssa) {
      return src->ssa != def;
   } else {
      return true;
   }
}

static bool
src_does_not_use_reg(nir_src *src, void *void_reg)
{
   nir_register *reg = void_reg;

   if (src->is_ssa) {
      return true;
   } else {
      return src->reg.reg != reg;
   }
}

void
nir_instr_rewrite_src(nir_instr *instr, nir_src *src, nir_src new_src)
{
   if (src->is_ssa) {
      nir_ssa_def *old_ssa = src->ssa;
      *src = new_src;
      if (old_ssa && nir_foreach_src(instr, src_does_not_use_def, old_ssa)) {
         struct set_entry *entry = _mesa_set_search(old_ssa->uses, instr);
         assert(entry);
         _mesa_set_remove(old_ssa->uses, entry);
      }
   } else {
      if (src->reg.indirect)
         nir_instr_rewrite_src(instr, src->reg.indirect, new_src);

      nir_register *old_reg = src->reg.reg;
      *src = new_src;
      if (old_reg && nir_foreach_src(instr, src_does_not_use_reg, old_reg)) {
         struct set_entry *entry = _mesa_set_search(old_reg->uses, instr);
         assert(entry);
         _mesa_set_remove(old_reg->uses, entry);
      }
   }

   if (new_src.is_ssa) {
      if (new_src.ssa)
         _mesa_set_add(new_src.ssa->uses, instr);
   } else {
      if (new_src.reg.reg)
         _mesa_set_add(new_src.reg.reg->uses, instr);
   }
}

void
nir_ssa_def_init(nir_instr *instr, nir_ssa_def *def,
                 unsigned num_components, const char *name)
{
   void *mem_ctx = ralloc_parent(instr);

   def->name = name;
   def->parent_instr = instr;
   def->uses = _mesa_set_create(mem_ctx, _mesa_hash_pointer,
                                _mesa_key_pointer_equal);
   def->if_uses = _mesa_set_create(mem_ctx, _mesa_hash_pointer,
                                   _mesa_key_pointer_equal);
   def->num_components = num_components;

   if (instr->block) {
      nir_function_impl *impl =
         nir_cf_node_get_function(&instr->block->cf_node);

      def->index = impl->ssa_alloc++;
   } else {
      def->index = UINT_MAX;
   }
}

void
nir_ssa_dest_init(nir_instr *instr, nir_dest *dest,
                 unsigned num_components, const char *name)
{
   dest->is_ssa = true;
   nir_ssa_def_init(instr, &dest->ssa, num_components, name);
}

struct ssa_def_rewrite_state {
   void *mem_ctx;
   nir_ssa_def *old;
   nir_src new_src;
};

static bool
ssa_def_rewrite_uses_src(nir_src *src, void *void_state)
{
   struct ssa_def_rewrite_state *state = void_state;

   if (src->is_ssa && src->ssa == state->old)
      nir_src_copy(src, &state->new_src, state->mem_ctx);

   return true;
}

void
nir_ssa_def_rewrite_uses(nir_ssa_def *def, nir_src new_src, void *mem_ctx)
{
   struct ssa_def_rewrite_state state;
   state.mem_ctx = mem_ctx;
   state.old = def;
   state.new_src = new_src;

   assert(!new_src.is_ssa || def != new_src.ssa);

   struct set *new_uses, *new_if_uses;
   if (new_src.is_ssa) {
      new_uses = new_src.ssa->uses;
      new_if_uses = new_src.ssa->if_uses;
   } else {
      new_uses = new_src.reg.reg->uses;
      new_if_uses = new_src.reg.reg->if_uses;
   }

   struct set_entry *entry;
   set_foreach(def->uses, entry) {
      nir_instr *instr = (nir_instr *)entry->key;

      _mesa_set_remove(def->uses, entry);
      nir_foreach_src(instr, ssa_def_rewrite_uses_src, &state);
      _mesa_set_add(new_uses, instr);
   }

   set_foreach(def->if_uses, entry) {
      nir_if *if_use = (nir_if *)entry->key;

      _mesa_set_remove(def->if_uses, entry);
      nir_src_copy(&if_use->condition, &new_src, mem_ctx);
      _mesa_set_add(new_if_uses, if_use);
   }
}


static bool foreach_cf_node(nir_cf_node *node, nir_foreach_block_cb cb,
                            bool reverse, void *state);

static inline bool
foreach_if(nir_if *if_stmt, nir_foreach_block_cb cb, bool reverse, void *state)
{
   if (reverse) {
      foreach_list_typed_safe_reverse(nir_cf_node, node, node,
                                      &if_stmt->else_list) {
         if (!foreach_cf_node(node, cb, reverse, state))
            return false;
      }

      foreach_list_typed_safe_reverse(nir_cf_node, node, node,
                                      &if_stmt->then_list) {
         if (!foreach_cf_node(node, cb, reverse, state))
            return false;
      }
   } else {
      foreach_list_typed_safe(nir_cf_node, node, node, &if_stmt->then_list) {
         if (!foreach_cf_node(node, cb, reverse, state))
            return false;
      }

      foreach_list_typed_safe(nir_cf_node, node, node, &if_stmt->else_list) {
         if (!foreach_cf_node(node, cb, reverse, state))
            return false;
      }
   }

   return true;
}

static inline bool
foreach_loop(nir_loop *loop, nir_foreach_block_cb cb, bool reverse, void *state)
{
   if (reverse) {
      foreach_list_typed_safe_reverse(nir_cf_node, node, node, &loop->body) {
         if (!foreach_cf_node(node, cb, reverse, state))
            return false;
      }
   } else {
      foreach_list_typed_safe(nir_cf_node, node, node, &loop->body) {
         if (!foreach_cf_node(node, cb, reverse, state))
            return false;
      }
   }

   return true;
}

static bool
foreach_cf_node(nir_cf_node *node, nir_foreach_block_cb cb,
                bool reverse, void *state)
{
   switch (node->type) {
   case nir_cf_node_block:
      return cb(nir_cf_node_as_block(node), state);
   case nir_cf_node_if:
      return foreach_if(nir_cf_node_as_if(node), cb, reverse, state);
   case nir_cf_node_loop:
      return foreach_loop(nir_cf_node_as_loop(node), cb, reverse, state);
      break;

   default:
      unreachable("Invalid CFG node type");
      break;
   }

   return false;
}

bool
nir_foreach_block(nir_function_impl *impl, nir_foreach_block_cb cb, void *state)
{
   foreach_list_typed_safe(nir_cf_node, node, node, &impl->body) {
      if (!foreach_cf_node(node, cb, false, state))
         return false;
   }

   return cb(impl->end_block, state);
}

bool
nir_foreach_block_reverse(nir_function_impl *impl, nir_foreach_block_cb cb,
                          void *state)
{
   if (!cb(impl->end_block, state))
      return false;

   foreach_list_typed_safe_reverse(nir_cf_node, node, node, &impl->body) {
      if (!foreach_cf_node(node, cb, true, state))
         return false;
   }

   return true;
}

nir_if *
nir_block_get_following_if(nir_block *block)
{
   if (exec_node_is_tail_sentinel(&block->cf_node.node))
      return NULL;

   if (nir_cf_node_is_last(&block->cf_node))
      return NULL;

   nir_cf_node *next_node = nir_cf_node_next(&block->cf_node);

   if (next_node->type != nir_cf_node_if)
      return NULL;

   return nir_cf_node_as_if(next_node);
}

static bool
index_block(nir_block *block, void *state)
{
   unsigned *index = state;
   block->index = (*index)++;
   return true;
}

void
nir_index_blocks(nir_function_impl *impl)
{
   unsigned index = 0;

   if (impl->valid_metadata & nir_metadata_block_index)
      return;

   nir_foreach_block(impl, index_block, &index);

   impl->num_blocks = index;
}

static bool
index_ssa_def_cb(nir_ssa_def *def, void *state)
{
   unsigned *index = (unsigned *) state;
   def->index = (*index)++;

   return true;
}

static bool
index_ssa_block(nir_block *block, void *state)
{
   nir_foreach_instr(block, instr)
      nir_foreach_ssa_def(instr, index_ssa_def_cb, state);

   return true;
}

void
nir_index_ssa_defs(nir_function_impl *impl)
{
   unsigned index = 0;
   nir_foreach_block(impl, index_ssa_block, &index);
   impl->ssa_alloc = index;
}
