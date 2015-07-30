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

/*
 * This file checks for invalid IR indicating a bug somewhere in the compiler.
 */

/* Since this file is just a pile of asserts, don't bother compiling it if
 * we're not building a debug build.
 */
#ifdef DEBUG

/*
 * Per-register validation state.
 */

typedef struct {
   /*
    * equivalent to the uses and defs in nir_register, but built up by the
    * validator. At the end, we verify that the sets have the same entries.
    */
   struct set *uses, *if_uses, *defs;
   nir_function_impl *where_defined; /* NULL for global registers */
} reg_validate_state;

typedef struct {
   /*
    * equivalent to the uses in nir_ssa_def, but built up by the validator.
    * At the end, we verify that the sets have the same entries.
    */
   struct set *uses, *if_uses;
   nir_function_impl *where_defined;
} ssa_def_validate_state;

typedef struct {
   /* map of register -> validation state (struct above) */
   struct hash_table *regs;

   /* the current shader being validated */
   nir_shader *shader;

   /* the current instruction being validated */
   nir_instr *instr;

   /* the current basic block being validated */
   nir_block *block;

   /* the current if statement being validated */
   nir_if *if_stmt;

   /* the parent of the current cf node being visited */
   nir_cf_node *parent_node;

   /* the current function implementation being validated */
   nir_function_impl *impl;

   /* map of SSA value -> function implementation where it is defined */
   struct hash_table *ssa_defs;

   /* bitset of ssa definitions we have found; used to check uniqueness */
   BITSET_WORD *ssa_defs_found;

   /* bitset of registers we have currently found; used to check uniqueness */
   BITSET_WORD *regs_found;

   /* map of local variable -> function implementation where it is defined */
   struct hash_table *var_defs;
} validate_state;

static void validate_src(nir_src *src, validate_state *state);

static void
validate_reg_src(nir_src *src, validate_state *state)
{
   assert(src->reg.reg != NULL);

   struct hash_entry *entry;
   entry = _mesa_hash_table_search(state->regs, src->reg.reg);
   assert(entry);

   reg_validate_state *reg_state = (reg_validate_state *) entry->data;

   if (state->instr) {
      _mesa_set_add(reg_state->uses, src);
   } else {
      assert(state->if_stmt);
      _mesa_set_add(reg_state->if_uses, src);
   }

   if (!src->reg.reg->is_global) {
      assert(reg_state->where_defined == state->impl &&
             "using a register declared in a different function");
   }

   assert((src->reg.reg->num_array_elems == 0 ||
          src->reg.base_offset < src->reg.reg->num_array_elems) &&
          "definitely out-of-bounds array access");

   if (src->reg.indirect) {
      assert(src->reg.reg->num_array_elems != 0);
      assert((src->reg.indirect->is_ssa ||
              src->reg.indirect->reg.indirect == NULL) &&
             "only one level of indirection allowed");
      validate_src(src->reg.indirect, state);
   }
}

static void
validate_ssa_src(nir_src *src, validate_state *state)
{
   assert(src->ssa != NULL);

   struct hash_entry *entry = _mesa_hash_table_search(state->ssa_defs, src->ssa);

   assert(entry);

   ssa_def_validate_state *def_state = (ssa_def_validate_state *)entry->data;

   assert(def_state->where_defined == state->impl &&
          "using an SSA value defined in a different function");

   if (state->instr) {
      _mesa_set_add(def_state->uses, src);
   } else {
      assert(state->if_stmt);
      _mesa_set_add(def_state->if_uses, src);
   }

   /* TODO validate that the use is dominated by the definition */
}

static void
validate_src(nir_src *src, validate_state *state)
{
   if (state->instr)
      assert(src->parent_instr == state->instr);
   else
      assert(src->parent_if == state->if_stmt);

   if (src->is_ssa)
      validate_ssa_src(src, state);
   else
      validate_reg_src(src, state);
}

static void
validate_alu_src(nir_alu_instr *instr, unsigned index, validate_state *state)
{
   nir_alu_src *src = &instr->src[index];

   unsigned num_components;
   if (src->src.is_ssa)
      num_components = src->src.ssa->num_components;
   else {
      if (src->src.reg.reg->is_packed)
         num_components = 4; /* can't check anything */
      else
         num_components = src->src.reg.reg->num_components;
   }
   for (unsigned i = 0; i < 4; i++) {
      assert(src->swizzle[i] < 4);

      if (nir_alu_instr_channel_used(instr, index, i))
         assert(src->swizzle[i] < num_components);
   }

   validate_src(&src->src, state);
}

static void
validate_reg_dest(nir_reg_dest *dest, validate_state *state)
{
   assert(dest->reg != NULL);

   assert(dest->parent_instr == state->instr);

   struct hash_entry *entry2;
   entry2 = _mesa_hash_table_search(state->regs, dest->reg);

   assert(entry2);

   reg_validate_state *reg_state = (reg_validate_state *) entry2->data;
   _mesa_set_add(reg_state->defs, dest);

   if (!dest->reg->is_global) {
      assert(reg_state->where_defined == state->impl &&
             "writing to a register declared in a different function");
   }

   assert((dest->reg->num_array_elems == 0 ||
          dest->base_offset < dest->reg->num_array_elems) &&
          "definitely out-of-bounds array access");

   if (dest->indirect) {
      assert(dest->reg->num_array_elems != 0);
      assert((dest->indirect->is_ssa || dest->indirect->reg.indirect == NULL) &&
             "only one level of indirection allowed");
      validate_src(dest->indirect, state);
   }
}

static void
validate_ssa_def(nir_ssa_def *def, validate_state *state)
{
   assert(def->index < state->impl->ssa_alloc);
   assert(!BITSET_TEST(state->ssa_defs_found, def->index));
   BITSET_SET(state->ssa_defs_found, def->index);

   assert(def->parent_instr == state->instr);

   assert(def->num_components <= 4);

   list_validate(&def->uses);
   list_validate(&def->if_uses);

   ssa_def_validate_state *def_state = ralloc(state->ssa_defs,
                                              ssa_def_validate_state);
   def_state->where_defined = state->impl;
   def_state->uses = _mesa_set_create(def_state, _mesa_hash_pointer,
                                      _mesa_key_pointer_equal);
   def_state->if_uses = _mesa_set_create(def_state, _mesa_hash_pointer,
                                         _mesa_key_pointer_equal);
   _mesa_hash_table_insert(state->ssa_defs, def, def_state);
}

static void
validate_dest(nir_dest *dest, validate_state *state)
{
   if (dest->is_ssa)
      validate_ssa_def(&dest->ssa, state);
   else
      validate_reg_dest(&dest->reg, state);
}

static void
validate_alu_dest(nir_alu_dest *dest, validate_state *state)
{
   unsigned dest_size =
      dest->dest.is_ssa ? dest->dest.ssa.num_components
                        : dest->dest.reg.reg->num_components;
   bool is_packed = !dest->dest.is_ssa && dest->dest.reg.reg->is_packed;
   /*
    * validate that the instruction doesn't write to components not in the
    * register/SSA value
    */
   assert(is_packed || !(dest->write_mask & ~((1 << dest_size) - 1)));

   /* validate that saturate is only ever used on instructions with
    * destinations of type float
    */
   nir_alu_instr *alu = nir_instr_as_alu(state->instr);
   assert(nir_op_infos[alu->op].output_type == nir_type_float ||
          !dest->saturate);

   validate_dest(&dest->dest, state);
}

static void
validate_alu_instr(nir_alu_instr *instr, validate_state *state)
{
   assert(instr->op < nir_num_opcodes);

   validate_alu_dest(&instr->dest, state);

   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      validate_alu_src(instr, i, state);
   }
}

static void
validate_deref_chain(nir_deref *deref, validate_state *state)
{
   assert(deref->child == NULL || ralloc_parent(deref->child) == deref);

   nir_deref *parent = NULL;
   while (deref != NULL) {
      switch (deref->deref_type) {
      case nir_deref_type_array:
         assert(deref->type == glsl_get_array_element(parent->type));
         if (nir_deref_as_array(deref)->deref_array_type ==
             nir_deref_array_type_indirect)
            validate_src(&nir_deref_as_array(deref)->indirect, state);
         break;

      case nir_deref_type_struct:
         assert(deref->type ==
                glsl_get_struct_field(parent->type,
                                      nir_deref_as_struct(deref)->index));
         break;

      case nir_deref_type_var:
         break;

      default:
         assert(!"Invalid deref type");
         break;
      }

      parent = deref;
      deref = deref->child;
   }
}

static void
validate_var_use(nir_variable *var, validate_state *state)
{
   if (var->data.mode == nir_var_local) {
      struct hash_entry *entry = _mesa_hash_table_search(state->var_defs, var);

      assert(entry);
      assert((nir_function_impl *) entry->data == state->impl);
   }
}

static void
validate_deref_var(void *parent_mem_ctx, nir_deref_var *deref, validate_state *state)
{
   assert(deref != NULL);
   assert(ralloc_parent(deref) == parent_mem_ctx);
   assert(deref->deref.type == deref->var->type);

   validate_var_use(deref->var, state);

   validate_deref_chain(&deref->deref, state);
}

static void
validate_intrinsic_instr(nir_intrinsic_instr *instr, validate_state *state)
{
   unsigned num_srcs = nir_intrinsic_infos[instr->intrinsic].num_srcs;
   for (unsigned i = 0; i < num_srcs; i++) {
      unsigned components_read =
         nir_intrinsic_infos[instr->intrinsic].src_components[i];
      if (components_read == 0)
         components_read = instr->num_components;

      assert(components_read > 0);

      if (instr->src[i].is_ssa) {
         assert(components_read <= instr->src[i].ssa->num_components);
      } else if (!instr->src[i].reg.reg->is_packed) {
         assert(components_read <= instr->src[i].reg.reg->num_components);
      }

      validate_src(&instr->src[i], state);
   }

   if (nir_intrinsic_infos[instr->intrinsic].has_dest) {
      unsigned components_written =
         nir_intrinsic_infos[instr->intrinsic].dest_components;
      if (components_written == 0)
         components_written = instr->num_components;

      assert(components_written > 0);

      if (instr->dest.is_ssa) {
         assert(components_written <= instr->dest.ssa.num_components);
      } else if (!instr->dest.reg.reg->is_packed) {
         assert(components_written <= instr->dest.reg.reg->num_components);
      }

      validate_dest(&instr->dest, state);
   }

   unsigned num_vars = nir_intrinsic_infos[instr->intrinsic].num_variables;
   for (unsigned i = 0; i < num_vars; i++) {
      validate_deref_var(instr, instr->variables[i], state);
   }

   switch (instr->intrinsic) {
   case nir_intrinsic_load_var:
      assert(instr->variables[0]->var->data.mode != nir_var_shader_out);
      break;
   case nir_intrinsic_store_var:
      assert(instr->variables[0]->var->data.mode != nir_var_shader_in &&
             instr->variables[0]->var->data.mode != nir_var_uniform &&
             instr->variables[0]->var->data.mode != nir_var_shader_storage);
      break;
   case nir_intrinsic_copy_var:
      assert(instr->variables[0]->var->data.mode != nir_var_shader_in &&
             instr->variables[0]->var->data.mode != nir_var_uniform &&
             instr->variables[0]->var->data.mode != nir_var_shader_storage);
      assert(instr->variables[1]->var->data.mode != nir_var_shader_out);
      break;
   default:
      break;
   }
}

static void
validate_tex_instr(nir_tex_instr *instr, validate_state *state)
{
   validate_dest(&instr->dest, state);

   bool src_type_seen[nir_num_tex_src_types];
   for (unsigned i = 0; i < nir_num_tex_src_types; i++)
      src_type_seen[i] = false;

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      assert(!src_type_seen[instr->src[i].src_type]);
      src_type_seen[instr->src[i].src_type] = true;
      validate_src(&instr->src[i].src, state);
   }

   if (instr->sampler != NULL)
      validate_deref_var(instr, instr->sampler, state);
}

static void
validate_call_instr(nir_call_instr *instr, validate_state *state)
{
   if (instr->return_deref == NULL)
      assert(glsl_type_is_void(instr->callee->return_type));
   else
      assert(instr->return_deref->deref.type == instr->callee->return_type);

   assert(instr->num_params == instr->callee->num_params);

   for (unsigned i = 0; i < instr->num_params; i++) {
      assert(instr->callee->params[i].type == instr->params[i]->deref.type);
      validate_deref_var(instr, instr->params[i], state);
   }

   validate_deref_var(instr, instr->return_deref, state);
}

static void
validate_load_const_instr(nir_load_const_instr *instr, validate_state *state)
{
   validate_ssa_def(&instr->def, state);
}

static void
validate_ssa_undef_instr(nir_ssa_undef_instr *instr, validate_state *state)
{
   validate_ssa_def(&instr->def, state);
}

static void
validate_phi_instr(nir_phi_instr *instr, validate_state *state)
{
   /*
    * don't validate the sources until we get to them from their predecessor
    * basic blocks, to avoid validating an SSA use before its definition.
    */

   validate_dest(&instr->dest, state);

   exec_list_validate(&instr->srcs);
   assert(exec_list_length(&instr->srcs) ==
          state->block->predecessors->entries);
}

static void
validate_instr(nir_instr *instr, validate_state *state)
{
   assert(instr->block == state->block);

   state->instr = instr;

   switch (instr->type) {
   case nir_instr_type_alu:
      validate_alu_instr(nir_instr_as_alu(instr), state);
      break;

   case nir_instr_type_call:
      validate_call_instr(nir_instr_as_call(instr), state);
      break;

   case nir_instr_type_intrinsic:
      validate_intrinsic_instr(nir_instr_as_intrinsic(instr), state);
      break;

   case nir_instr_type_tex:
      validate_tex_instr(nir_instr_as_tex(instr), state);
      break;

   case nir_instr_type_load_const:
      validate_load_const_instr(nir_instr_as_load_const(instr), state);
      break;

   case nir_instr_type_phi:
      validate_phi_instr(nir_instr_as_phi(instr), state);
      break;

   case nir_instr_type_ssa_undef:
      validate_ssa_undef_instr(nir_instr_as_ssa_undef(instr), state);
      break;

   case nir_instr_type_jump:
      break;

   default:
      assert(!"Invalid ALU instruction type");
      break;
   }

   state->instr = NULL;
}

static void
validate_phi_src(nir_phi_instr *instr, nir_block *pred, validate_state *state)
{
   state->instr = &instr->instr;

   assert(instr->dest.is_ssa);

   exec_list_validate(&instr->srcs);
   nir_foreach_phi_src(instr, src) {
      if (src->pred == pred) {
         assert(src->src.is_ssa);
         assert(src->src.ssa->num_components ==
                instr->dest.ssa.num_components);

         validate_src(&src->src, state);
         state->instr = NULL;
         return;
      }
   }

   abort();
}

static void
validate_phi_srcs(nir_block *block, nir_block *succ, validate_state *state)
{
   nir_foreach_instr(succ, instr) {
      if (instr->type != nir_instr_type_phi)
         break;

      validate_phi_src(nir_instr_as_phi(instr), block, state);
   }
}

static void validate_cf_node(nir_cf_node *node, validate_state *state);

static void
validate_block(nir_block *block, validate_state *state)
{
   assert(block->cf_node.parent == state->parent_node);

   state->block = block;

   exec_list_validate(&block->instr_list);
   nir_foreach_instr(block, instr) {
      if (instr->type == nir_instr_type_phi) {
         assert(instr == nir_block_first_instr(block) ||
                nir_instr_prev(instr)->type == nir_instr_type_phi);
      }

      if (instr->type == nir_instr_type_jump) {
         assert(instr == nir_block_last_instr(block));
      }

      validate_instr(instr, state);
   }

   assert(block->successors[0] != NULL);

   for (unsigned i = 0; i < 2; i++) {
      if (block->successors[i] != NULL) {
         struct set_entry *entry =
            _mesa_set_search(block->successors[i]->predecessors, block);
         assert(entry);

         validate_phi_srcs(block, block->successors[i], state);
      }
   }

   if (!exec_list_is_empty(&block->instr_list) &&
       nir_block_last_instr(block)->type == nir_instr_type_jump)
      assert(block->successors[1] == NULL);
}

static void
validate_if(nir_if *if_stmt, validate_state *state)
{
   state->if_stmt = if_stmt;

   assert(!exec_node_is_head_sentinel(if_stmt->cf_node.node.prev));
   nir_cf_node *prev_node = nir_cf_node_prev(&if_stmt->cf_node);
   assert(prev_node->type == nir_cf_node_block);

   nir_block *prev_block = nir_cf_node_as_block(prev_node);
   assert(&prev_block->successors[0]->cf_node ==
          nir_if_first_then_node(if_stmt));
   assert(&prev_block->successors[1]->cf_node ==
          nir_if_first_else_node(if_stmt));

   assert(!exec_node_is_tail_sentinel(if_stmt->cf_node.node.next));
   nir_cf_node *next_node = nir_cf_node_next(&if_stmt->cf_node);
   assert(next_node->type == nir_cf_node_block);

   validate_src(&if_stmt->condition, state);

   assert(!exec_list_is_empty(&if_stmt->then_list));
   assert(!exec_list_is_empty(&if_stmt->else_list));

   nir_cf_node *old_parent = state->parent_node;
   state->parent_node = &if_stmt->cf_node;

   exec_list_validate(&if_stmt->then_list);
   foreach_list_typed(nir_cf_node, cf_node, node, &if_stmt->then_list) {
      validate_cf_node(cf_node, state);
   }

   exec_list_validate(&if_stmt->else_list);
   foreach_list_typed(nir_cf_node, cf_node, node, &if_stmt->else_list) {
      validate_cf_node(cf_node, state);
   }

   state->parent_node = old_parent;
   state->if_stmt = NULL;
}

static void
validate_loop(nir_loop *loop, validate_state *state)
{
   assert(!exec_node_is_head_sentinel(loop->cf_node.node.prev));
   nir_cf_node *prev_node = nir_cf_node_prev(&loop->cf_node);
   assert(prev_node->type == nir_cf_node_block);

   nir_block *prev_block = nir_cf_node_as_block(prev_node);
   assert(&prev_block->successors[0]->cf_node == nir_loop_first_cf_node(loop));
   assert(prev_block->successors[1] == NULL);

   assert(!exec_node_is_tail_sentinel(loop->cf_node.node.next));
   nir_cf_node *next_node = nir_cf_node_next(&loop->cf_node);
   assert(next_node->type == nir_cf_node_block);

   assert(!exec_list_is_empty(&loop->body));

   nir_cf_node *old_parent = state->parent_node;
   state->parent_node = &loop->cf_node;

   exec_list_validate(&loop->body);
   foreach_list_typed(nir_cf_node, cf_node, node, &loop->body) {
      validate_cf_node(cf_node, state);
   }

   state->parent_node = old_parent;
}

static void
validate_cf_node(nir_cf_node *node, validate_state *state)
{
   assert(node->parent == state->parent_node);

   switch (node->type) {
   case nir_cf_node_block:
      validate_block(nir_cf_node_as_block(node), state);
      break;

   case nir_cf_node_if:
      validate_if(nir_cf_node_as_if(node), state);
      break;

   case nir_cf_node_loop:
      validate_loop(nir_cf_node_as_loop(node), state);
      break;

   default:
      unreachable("Invalid CF node type");
   }
}

static void
prevalidate_reg_decl(nir_register *reg, bool is_global, validate_state *state)
{
   assert(reg->is_global == is_global);

   if (is_global)
      assert(reg->index < state->shader->reg_alloc);
   else
      assert(reg->index < state->impl->reg_alloc);
   assert(!BITSET_TEST(state->regs_found, reg->index));
   BITSET_SET(state->regs_found, reg->index);

   list_validate(&reg->uses);
   list_validate(&reg->defs);
   list_validate(&reg->if_uses);

   reg_validate_state *reg_state = ralloc(state->regs, reg_validate_state);
   reg_state->uses = _mesa_set_create(reg_state, _mesa_hash_pointer,
                                      _mesa_key_pointer_equal);
   reg_state->if_uses = _mesa_set_create(reg_state, _mesa_hash_pointer,
                                         _mesa_key_pointer_equal);
   reg_state->defs = _mesa_set_create(reg_state, _mesa_hash_pointer,
                                      _mesa_key_pointer_equal);

   reg_state->where_defined = is_global ? NULL : state->impl;

   _mesa_hash_table_insert(state->regs, reg, reg_state);
}

static void
postvalidate_reg_decl(nir_register *reg, validate_state *state)
{
   struct hash_entry *entry = _mesa_hash_table_search(state->regs, reg);

   reg_validate_state *reg_state = (reg_validate_state *) entry->data;

   nir_foreach_use(reg, src) {
      struct set_entry *entry = _mesa_set_search(reg_state->uses, src);
      assert(entry);
      _mesa_set_remove(reg_state->uses, entry);
   }

   if (reg_state->uses->entries != 0) {
      printf("extra entries in register uses:\n");
      struct set_entry *entry;
      set_foreach(reg_state->uses, entry)
         printf("%p\n", entry->key);

      abort();
   }

   nir_foreach_if_use(reg, src) {
      struct set_entry *entry = _mesa_set_search(reg_state->if_uses, src);
      assert(entry);
      _mesa_set_remove(reg_state->if_uses, entry);
   }

   if (reg_state->if_uses->entries != 0) {
      printf("extra entries in register if_uses:\n");
      struct set_entry *entry;
      set_foreach(reg_state->if_uses, entry)
         printf("%p\n", entry->key);

      abort();
   }

   nir_foreach_def(reg, src) {
      struct set_entry *entry = _mesa_set_search(reg_state->defs, src);
      assert(entry);
      _mesa_set_remove(reg_state->defs, entry);
   }

   if (reg_state->defs->entries != 0) {
      printf("extra entries in register defs:\n");
      struct set_entry *entry;
      set_foreach(reg_state->defs, entry)
         printf("%p\n", entry->key);

      abort();
   }
}

static void
validate_var_decl(nir_variable *var, bool is_global, validate_state *state)
{
   assert(is_global != (var->data.mode == nir_var_local));

   /*
    * TODO validate some things ir_validate.cpp does (requires more GLSL type
    * support)
    */

   if (!is_global) {
      _mesa_hash_table_insert(state->var_defs, var, state->impl);
   }
}

static bool
postvalidate_ssa_def(nir_ssa_def *def, void *void_state)
{
   validate_state *state = void_state;

   struct hash_entry *entry = _mesa_hash_table_search(state->ssa_defs, def);
   ssa_def_validate_state *def_state = (ssa_def_validate_state *)entry->data;

   nir_foreach_use(def, src) {
      struct set_entry *entry = _mesa_set_search(def_state->uses, src);
      assert(entry);
      _mesa_set_remove(def_state->uses, entry);
   }

   if (def_state->uses->entries != 0) {
      printf("extra entries in register uses:\n");
      struct set_entry *entry;
      set_foreach(def_state->uses, entry)
         printf("%p\n", entry->key);

      abort();
   }

   nir_foreach_if_use(def, src) {
      struct set_entry *entry = _mesa_set_search(def_state->if_uses, src);
      assert(entry);
      _mesa_set_remove(def_state->if_uses, entry);
   }

   if (def_state->if_uses->entries != 0) {
      printf("extra entries in register uses:\n");
      struct set_entry *entry;
      set_foreach(def_state->if_uses, entry)
         printf("%p\n", entry->key);

      abort();
   }

   return true;
}

static bool
postvalidate_ssa_defs_block(nir_block *block, void *state)
{
   nir_foreach_instr(block, instr)
      nir_foreach_ssa_def(instr, postvalidate_ssa_def, state);

   return true;
}

static void
validate_function_impl(nir_function_impl *impl, validate_state *state)
{
   assert(impl->overload->impl == impl);
   assert(impl->cf_node.parent == NULL);

   assert(impl->num_params == impl->overload->num_params);
   for (unsigned i = 0; i < impl->num_params; i++)
      assert(impl->params[i]->type == impl->overload->params[i].type);

   if (glsl_type_is_void(impl->overload->return_type))
      assert(impl->return_var == NULL);
   else
      assert(impl->return_var->type == impl->overload->return_type);

   assert(exec_list_is_empty(&impl->end_block->instr_list));
   assert(impl->end_block->successors[0] == NULL);
   assert(impl->end_block->successors[1] == NULL);

   state->impl = impl;
   state->parent_node = &impl->cf_node;

   exec_list_validate(&impl->locals);
   foreach_list_typed(nir_variable, var, node, &impl->locals) {
      validate_var_decl(var, false, state);
   }

   state->regs_found = realloc(state->regs_found,
                               BITSET_WORDS(impl->reg_alloc) *
                               sizeof(BITSET_WORD));
   memset(state->regs_found, 0, BITSET_WORDS(impl->reg_alloc) *
                                sizeof(BITSET_WORD));
   exec_list_validate(&impl->registers);
   foreach_list_typed(nir_register, reg, node, &impl->registers) {
      prevalidate_reg_decl(reg, false, state);
   }

   state->ssa_defs_found = realloc(state->ssa_defs_found,
                                   BITSET_WORDS(impl->ssa_alloc) *
                                   sizeof(BITSET_WORD));
   memset(state->ssa_defs_found, 0, BITSET_WORDS(impl->ssa_alloc) *
                                    sizeof(BITSET_WORD));
   exec_list_validate(&impl->body);
   foreach_list_typed(nir_cf_node, node, node, &impl->body) {
      validate_cf_node(node, state);
   }

   foreach_list_typed(nir_register, reg, node, &impl->registers) {
      postvalidate_reg_decl(reg, state);
   }

   nir_foreach_block(impl, postvalidate_ssa_defs_block, state);
}

static void
validate_function_overload(nir_function_overload *overload,
                           validate_state *state)
{
   if (overload->impl != NULL)
      validate_function_impl(overload->impl, state);
}

static void
validate_function(nir_function *func, validate_state *state)
{
   exec_list_validate(&func->overload_list);
   foreach_list_typed(nir_function_overload, overload, node, &func->overload_list) {
      assert(overload->function == func);
      validate_function_overload(overload, state);
   }
}

static void
init_validate_state(validate_state *state)
{
   state->regs = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                         _mesa_key_pointer_equal);
   state->ssa_defs = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                             _mesa_key_pointer_equal);
   state->ssa_defs_found = NULL;
   state->regs_found = NULL;
   state->var_defs = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                             _mesa_key_pointer_equal);
}

static void
destroy_validate_state(validate_state *state)
{
   _mesa_hash_table_destroy(state->regs, NULL);
   _mesa_hash_table_destroy(state->ssa_defs, NULL);
   free(state->ssa_defs_found);
   free(state->regs_found);
   _mesa_hash_table_destroy(state->var_defs, NULL);
}

void
nir_validate_shader(nir_shader *shader)
{
   validate_state state;
   init_validate_state(&state);

   state.shader = shader;

   exec_list_validate(&shader->uniforms);
   foreach_list_typed(nir_variable, var, node, &shader->uniforms) {
      validate_var_decl(var, true, &state);
   }

   exec_list_validate(&shader->inputs);
   foreach_list_typed(nir_variable, var, node, &shader->inputs) {
     validate_var_decl(var, true, &state);
   }

   exec_list_validate(&shader->outputs);
   foreach_list_typed(nir_variable, var, node, &shader->outputs) {
     validate_var_decl(var, true, &state);
   }

   exec_list_validate(&shader->globals);
   foreach_list_typed(nir_variable, var, node, &shader->globals) {
     validate_var_decl(var, true, &state);
   }

   exec_list_validate(&shader->system_values);
   foreach_list_typed(nir_variable, var, node, &shader->system_values) {
     validate_var_decl(var, true, &state);
   }

   state.regs_found = realloc(state.regs_found,
                              BITSET_WORDS(shader->reg_alloc) *
                              sizeof(BITSET_WORD));
   memset(state.regs_found, 0, BITSET_WORDS(shader->reg_alloc) *
                               sizeof(BITSET_WORD));
   exec_list_validate(&shader->registers);
   foreach_list_typed(nir_register, reg, node, &shader->registers) {
      prevalidate_reg_decl(reg, true, &state);
   }

   exec_list_validate(&shader->functions);
   foreach_list_typed(nir_function, func, node, &shader->functions) {
      validate_function(func, &state);
   }

   foreach_list_typed(nir_register, reg, node, &shader->registers) {
      postvalidate_reg_decl(reg, &state);
   }

   destroy_validate_state(&state);
}

#endif /* NDEBUG */
