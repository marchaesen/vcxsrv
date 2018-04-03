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
#ifndef NDEBUG

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

   /* the current variable being validated */
   nir_variable *var;

   /* the current basic block being validated */
   nir_block *block;

   /* the current if statement being validated */
   nir_if *if_stmt;

   /* the current loop being visited */
   nir_loop *loop;

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

   /* map of variable -> function implementation where it is defined or NULL
    * if it is a global variable
    */
   struct hash_table *var_defs;

   /* map of instruction/var/etc to failed assert string */
   struct hash_table *errors;
} validate_state;

static void
log_error(validate_state *state, const char *cond, const char *file, int line)
{
   const void *obj;

   if (state->instr)
      obj = state->instr;
   else if (state->var)
      obj = state->var;
   else
      obj = cond;

   char *msg = ralloc_asprintf(state->errors, "error: %s (%s:%d)",
                               cond, file, line);

   _mesa_hash_table_insert(state->errors, obj, msg);
}

#define validate_assert(state, cond) do {             \
      if (!(cond))                                    \
         log_error(state, #cond, __FILE__, __LINE__); \
   } while (0)

static void validate_src(nir_src *src, validate_state *state,
                         unsigned bit_size, unsigned num_components);

static void
validate_reg_src(nir_src *src, validate_state *state,
                 unsigned bit_size, unsigned num_components)
{
   validate_assert(state, src->reg.reg != NULL);

   struct hash_entry *entry;
   entry = _mesa_hash_table_search(state->regs, src->reg.reg);
   validate_assert(state, entry);

   reg_validate_state *reg_state = (reg_validate_state *) entry->data;

   if (state->instr) {
      _mesa_set_add(reg_state->uses, src);
   } else {
      validate_assert(state, state->if_stmt);
      _mesa_set_add(reg_state->if_uses, src);
   }

   if (!src->reg.reg->is_global) {
      validate_assert(state, reg_state->where_defined == state->impl &&
             "using a register declared in a different function");
   }

   if (!src->reg.reg->is_packed) {
      if (bit_size)
         validate_assert(state, src->reg.reg->bit_size == bit_size);
      if (num_components)
         validate_assert(state, src->reg.reg->num_components == num_components);
   }

   validate_assert(state, (src->reg.reg->num_array_elems == 0 ||
          src->reg.base_offset < src->reg.reg->num_array_elems) &&
          "definitely out-of-bounds array access");

   if (src->reg.indirect) {
      validate_assert(state, src->reg.reg->num_array_elems != 0);
      validate_assert(state, (src->reg.indirect->is_ssa ||
              src->reg.indirect->reg.indirect == NULL) &&
             "only one level of indirection allowed");
      validate_src(src->reg.indirect, state, 32, 1);
   }
}

static void
validate_ssa_src(nir_src *src, validate_state *state,
                 unsigned bit_size, unsigned num_components)
{
   validate_assert(state, src->ssa != NULL);

   struct hash_entry *entry = _mesa_hash_table_search(state->ssa_defs, src->ssa);

   validate_assert(state, entry);

   if (!entry)
      return;

   ssa_def_validate_state *def_state = (ssa_def_validate_state *)entry->data;

   validate_assert(state, def_state->where_defined == state->impl &&
          "using an SSA value defined in a different function");

   if (state->instr) {
      _mesa_set_add(def_state->uses, src);
   } else {
      validate_assert(state, state->if_stmt);
      _mesa_set_add(def_state->if_uses, src);
   }

   if (bit_size)
      validate_assert(state, src->ssa->bit_size == bit_size);
   if (num_components)
      validate_assert(state, src->ssa->num_components == num_components);

   /* TODO validate that the use is dominated by the definition */
}

static void
validate_src(nir_src *src, validate_state *state,
             unsigned bit_size, unsigned num_components)
{
   if (state->instr)
      validate_assert(state, src->parent_instr == state->instr);
   else
      validate_assert(state, src->parent_if == state->if_stmt);

   if (src->is_ssa)
      validate_ssa_src(src, state, bit_size, num_components);
   else
      validate_reg_src(src, state, bit_size, num_components);
}

static void
validate_alu_src(nir_alu_instr *instr, unsigned index, validate_state *state)
{
   nir_alu_src *src = &instr->src[index];

   unsigned num_components;
   if (src->src.is_ssa) {
      num_components = src->src.ssa->num_components;
   } else {
      if (src->src.reg.reg->is_packed)
         num_components = 4; /* can't check anything */
      else
         num_components = src->src.reg.reg->num_components;
   }
   for (unsigned i = 0; i < 4; i++) {
      validate_assert(state, src->swizzle[i] < 4);

      if (nir_alu_instr_channel_used(instr, index, i))
         validate_assert(state, src->swizzle[i] < num_components);
   }

   validate_src(&src->src, state, 0, 0);
}

static void
validate_reg_dest(nir_reg_dest *dest, validate_state *state,
                  unsigned bit_size, unsigned num_components)
{
   validate_assert(state, dest->reg != NULL);

   validate_assert(state, dest->parent_instr == state->instr);

   struct hash_entry *entry2;
   entry2 = _mesa_hash_table_search(state->regs, dest->reg);

   validate_assert(state, entry2);

   reg_validate_state *reg_state = (reg_validate_state *) entry2->data;
   _mesa_set_add(reg_state->defs, dest);

   if (!dest->reg->is_global) {
      validate_assert(state, reg_state->where_defined == state->impl &&
             "writing to a register declared in a different function");
   }

   if (!dest->reg->is_packed) {
      if (bit_size)
         validate_assert(state, dest->reg->bit_size == bit_size);
      if (num_components)
         validate_assert(state, dest->reg->num_components == num_components);
   }

   validate_assert(state, (dest->reg->num_array_elems == 0 ||
          dest->base_offset < dest->reg->num_array_elems) &&
          "definitely out-of-bounds array access");

   if (dest->indirect) {
      validate_assert(state, dest->reg->num_array_elems != 0);
      validate_assert(state, (dest->indirect->is_ssa || dest->indirect->reg.indirect == NULL) &&
             "only one level of indirection allowed");
      validate_src(dest->indirect, state, 32, 1);
   }
}

static void
validate_ssa_def(nir_ssa_def *def, validate_state *state)
{
   validate_assert(state, def->index < state->impl->ssa_alloc);
   validate_assert(state, !BITSET_TEST(state->ssa_defs_found, def->index));
   BITSET_SET(state->ssa_defs_found, def->index);

   validate_assert(state, def->parent_instr == state->instr);

   validate_assert(state, (def->num_components <= 4) ||
                          (def->num_components == 8) ||
                          (def->num_components == 16));

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
validate_dest(nir_dest *dest, validate_state *state,
              unsigned bit_size, unsigned num_components)
{
   if (dest->is_ssa) {
      if (bit_size)
         validate_assert(state, dest->ssa.bit_size == bit_size);
      if (num_components)
         validate_assert(state, dest->ssa.num_components == num_components);
      validate_ssa_def(&dest->ssa, state);
   } else {
      validate_reg_dest(&dest->reg, state, bit_size, num_components);
   }
}

static void
validate_alu_dest(nir_alu_instr *instr, validate_state *state)
{
   nir_alu_dest *dest = &instr->dest;

   unsigned dest_size =
      dest->dest.is_ssa ? dest->dest.ssa.num_components
                        : dest->dest.reg.reg->num_components;
   bool is_packed = !dest->dest.is_ssa && dest->dest.reg.reg->is_packed;
   /*
    * validate that the instruction doesn't write to components not in the
    * register/SSA value
    */
   validate_assert(state, is_packed || !(dest->write_mask & ~((1 << dest_size) - 1)));

   /* validate that saturate is only ever used on instructions with
    * destinations of type float
    */
   nir_alu_instr *alu = nir_instr_as_alu(state->instr);
   validate_assert(state,
          (nir_alu_type_get_base_type(nir_op_infos[alu->op].output_type) ==
           nir_type_float) ||
          !dest->saturate);

   validate_dest(&dest->dest, state, 0, 0);
}

static void
validate_alu_instr(nir_alu_instr *instr, validate_state *state)
{
   validate_assert(state, instr->op < nir_num_opcodes);

   unsigned instr_bit_size = 0;
   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      nir_alu_type src_type = nir_op_infos[instr->op].input_types[i];
      unsigned src_bit_size = nir_src_bit_size(instr->src[i].src);
      if (nir_alu_type_get_type_size(src_type)) {
         validate_assert(state, src_bit_size == nir_alu_type_get_type_size(src_type));
      } else if (instr_bit_size) {
         validate_assert(state, src_bit_size == instr_bit_size);
      } else {
         instr_bit_size = src_bit_size;
      }

      if (nir_alu_type_get_base_type(src_type) == nir_type_float) {
         /* 8-bit float isn't a thing */
         validate_assert(state, src_bit_size == 16 || src_bit_size == 32 ||
                                src_bit_size == 64);
      }

      validate_alu_src(instr, i, state);
   }

   nir_alu_type dest_type = nir_op_infos[instr->op].output_type;
   unsigned dest_bit_size = nir_dest_bit_size(instr->dest.dest);
   if (nir_alu_type_get_type_size(dest_type)) {
      validate_assert(state, dest_bit_size == nir_alu_type_get_type_size(dest_type));
   } else if (instr_bit_size) {
      validate_assert(state, dest_bit_size == instr_bit_size);
   } else {
      /* The only unsized thing is the destination so it's vacuously valid */
   }

   if (nir_alu_type_get_base_type(dest_type) == nir_type_float) {
      /* 8-bit float isn't a thing */
      validate_assert(state, dest_bit_size == 16 || dest_bit_size == 32 ||
                             dest_bit_size == 64);
   }

   validate_alu_dest(instr, state);
}

static void
validate_deref_chain(nir_deref *deref, nir_variable_mode mode,
                     validate_state *state)
{
   validate_assert(state, deref->child == NULL || ralloc_parent(deref->child) == deref);

   nir_deref *parent = NULL;
   while (deref != NULL) {
      switch (deref->deref_type) {
      case nir_deref_type_array:
         if (mode == nir_var_shared) {
            /* Shared variables have a bit more relaxed rules because we need
             * to be able to handle array derefs on vectors.  Fortunately,
             * nir_lower_io handles these just fine.
             */
            validate_assert(state, glsl_type_is_array(parent->type) ||
                                   glsl_type_is_matrix(parent->type) ||
                                   glsl_type_is_vector(parent->type));
         } else {
            /* Most of NIR cannot handle array derefs on vectors */
            validate_assert(state, glsl_type_is_array(parent->type) ||
                                   glsl_type_is_matrix(parent->type));
         }
         validate_assert(state, deref->type == glsl_get_array_element(parent->type));
         if (nir_deref_as_array(deref)->deref_array_type ==
             nir_deref_array_type_indirect)
            validate_src(&nir_deref_as_array(deref)->indirect, state, 32, 1);
         break;

      case nir_deref_type_struct:
         assume(parent); /* cannot happen: deref change starts w/ nir_deref_var */
         validate_assert(state, deref->type ==
                glsl_get_struct_field(parent->type,
                                      nir_deref_as_struct(deref)->index));
         break;

      case nir_deref_type_var:
         break;

      default:
         validate_assert(state, !"Invalid deref type");
         break;
      }

      parent = deref;
      deref = deref->child;
   }
}

static void
validate_var_use(nir_variable *var, validate_state *state)
{
   struct hash_entry *entry = _mesa_hash_table_search(state->var_defs, var);
   validate_assert(state, entry);
   if (var->data.mode == nir_var_local)
      validate_assert(state, (nir_function_impl *) entry->data == state->impl);
}

static void
validate_deref_var(void *parent_mem_ctx, nir_deref_var *deref, validate_state *state)
{
   validate_assert(state, deref != NULL);
   validate_assert(state, ralloc_parent(deref) == parent_mem_ctx);
   validate_assert(state, deref->deref.type == deref->var->type);

   validate_var_use(deref->var, state);

   validate_deref_chain(&deref->deref, deref->var->data.mode, state);
}

static void
validate_intrinsic_instr(nir_intrinsic_instr *instr, validate_state *state)
{
   unsigned bit_size = 0;
   if (instr->intrinsic == nir_intrinsic_load_var ||
       instr->intrinsic == nir_intrinsic_store_var) {
      const struct glsl_type *type =
         nir_deref_tail(&instr->variables[0]->deref)->type;
      bit_size = glsl_get_bit_size(type);
   }

   unsigned num_srcs = nir_intrinsic_infos[instr->intrinsic].num_srcs;
   for (unsigned i = 0; i < num_srcs; i++) {
      unsigned components_read = nir_intrinsic_src_components(instr, i);

      validate_assert(state, components_read > 0);

      validate_src(&instr->src[i], state, bit_size, components_read);
   }

   unsigned num_vars = nir_intrinsic_infos[instr->intrinsic].num_variables;
   for (unsigned i = 0; i < num_vars; i++) {
      validate_deref_var(instr, instr->variables[i], state);
   }

   if (nir_intrinsic_infos[instr->intrinsic].has_dest) {
      unsigned components_written = nir_intrinsic_dest_components(instr);

      validate_assert(state, components_written > 0);

      validate_dest(&instr->dest, state, bit_size, components_written);
   }

   switch (instr->intrinsic) {
   case nir_intrinsic_load_var: {
      const struct glsl_type *type =
         nir_deref_tail(&instr->variables[0]->deref)->type;
      validate_assert(state, glsl_type_is_vector_or_scalar(type) ||
             (instr->variables[0]->var->data.mode == nir_var_uniform &&
              glsl_get_base_type(type) == GLSL_TYPE_SUBROUTINE));
      validate_assert(state, instr->num_components == glsl_get_vector_elements(type));
      break;
   }
   case nir_intrinsic_store_var: {
      const struct glsl_type *type =
         nir_deref_tail(&instr->variables[0]->deref)->type;
      validate_assert(state, glsl_type_is_vector_or_scalar(type) ||
             (instr->variables[0]->var->data.mode == nir_var_uniform &&
              glsl_get_base_type(type) == GLSL_TYPE_SUBROUTINE));
      validate_assert(state, instr->num_components == glsl_get_vector_elements(type));
      validate_assert(state, instr->variables[0]->var->data.mode != nir_var_shader_in &&
             instr->variables[0]->var->data.mode != nir_var_uniform &&
             instr->variables[0]->var->data.mode != nir_var_shader_storage);
      validate_assert(state, (nir_intrinsic_write_mask(instr) & ~((1 << instr->num_components) - 1)) == 0);
      break;
   }
   case nir_intrinsic_copy_var:
      validate_assert(state, nir_deref_tail(&instr->variables[0]->deref)->type ==
             nir_deref_tail(&instr->variables[1]->deref)->type);
      validate_assert(state, instr->variables[0]->var->data.mode != nir_var_shader_in &&
             instr->variables[0]->var->data.mode != nir_var_uniform &&
             instr->variables[0]->var->data.mode != nir_var_shader_storage);
      break;
   default:
      break;
   }
}

static void
validate_tex_instr(nir_tex_instr *instr, validate_state *state)
{
   bool src_type_seen[nir_num_tex_src_types];
   for (unsigned i = 0; i < nir_num_tex_src_types; i++)
      src_type_seen[i] = false;

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      validate_assert(state, !src_type_seen[instr->src[i].src_type]);
      src_type_seen[instr->src[i].src_type] = true;
      validate_src(&instr->src[i].src, state,
                   0, nir_tex_instr_src_size(instr, i));
   }

   if (instr->texture != NULL)
      validate_deref_var(instr, instr->texture, state);

   if (instr->sampler != NULL)
      validate_deref_var(instr, instr->sampler, state);

   validate_dest(&instr->dest, state, 0, nir_tex_instr_dest_size(instr));
}

static void
validate_call_instr(nir_call_instr *instr, validate_state *state)
{
   if (instr->return_deref == NULL) {
      validate_assert(state, glsl_type_is_void(instr->callee->return_type));
   } else {
      validate_assert(state, instr->return_deref->deref.type == instr->callee->return_type);
      validate_deref_var(instr, instr->return_deref, state);
   }

   validate_assert(state, instr->num_params == instr->callee->num_params);

   for (unsigned i = 0; i < instr->num_params; i++) {
      validate_assert(state, instr->callee->params[i].type == instr->params[i]->deref.type);
      validate_deref_var(instr, instr->params[i], state);
   }
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

   validate_dest(&instr->dest, state, 0, 0);

   exec_list_validate(&instr->srcs);
   validate_assert(state, exec_list_length(&instr->srcs) ==
          state->block->predecessors->entries);
}

static void
validate_instr(nir_instr *instr, validate_state *state)
{
   validate_assert(state, instr->block == state->block);

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
      validate_assert(state, !"Invalid ALU instruction type");
      break;
   }

   state->instr = NULL;
}

static void
validate_phi_src(nir_phi_instr *instr, nir_block *pred, validate_state *state)
{
   state->instr = &instr->instr;

   validate_assert(state, instr->dest.is_ssa);

   exec_list_validate(&instr->srcs);
   nir_foreach_phi_src(src, instr) {
      if (src->pred == pred) {
         validate_assert(state, src->src.is_ssa);
         validate_src(&src->src, state, instr->dest.ssa.bit_size,
                      instr->dest.ssa.num_components);
         state->instr = NULL;
         return;
      }
   }

   abort();
}

static void
validate_phi_srcs(nir_block *block, nir_block *succ, validate_state *state)
{
   nir_foreach_instr(instr, succ) {
      if (instr->type != nir_instr_type_phi)
         break;

      validate_phi_src(nir_instr_as_phi(instr), block, state);
   }
}

static void validate_cf_node(nir_cf_node *node, validate_state *state);

static void
validate_block(nir_block *block, validate_state *state)
{
   validate_assert(state, block->cf_node.parent == state->parent_node);

   state->block = block;

   exec_list_validate(&block->instr_list);
   nir_foreach_instr(instr, block) {
      if (instr->type == nir_instr_type_phi) {
         validate_assert(state, instr == nir_block_first_instr(block) ||
                nir_instr_prev(instr)->type == nir_instr_type_phi);
      }

      if (instr->type == nir_instr_type_jump) {
         validate_assert(state, instr == nir_block_last_instr(block));
      }

      validate_instr(instr, state);
   }

   validate_assert(state, block->successors[0] != NULL);
   validate_assert(state, block->successors[0] != block->successors[1]);

   for (unsigned i = 0; i < 2; i++) {
      if (block->successors[i] != NULL) {
         struct set_entry *entry =
            _mesa_set_search(block->successors[i]->predecessors, block);
         validate_assert(state, entry);

         validate_phi_srcs(block, block->successors[i], state);
      }
   }

   struct set_entry *entry;
   set_foreach(block->predecessors, entry) {
      const nir_block *pred = entry->key;
      validate_assert(state, pred->successors[0] == block ||
             pred->successors[1] == block);
   }

   if (!exec_list_is_empty(&block->instr_list) &&
       nir_block_last_instr(block)->type == nir_instr_type_jump) {
      validate_assert(state, block->successors[1] == NULL);
      nir_jump_instr *jump = nir_instr_as_jump(nir_block_last_instr(block));
      switch (jump->type) {
      case nir_jump_break: {
         nir_block *after =
            nir_cf_node_as_block(nir_cf_node_next(&state->loop->cf_node));
         validate_assert(state, block->successors[0] == after);
         break;
      }

      case nir_jump_continue: {
         nir_block *first = nir_loop_first_block(state->loop);
         validate_assert(state, block->successors[0] == first);
         break;
      }

      case nir_jump_return:
         validate_assert(state, block->successors[0] == state->impl->end_block);
         break;

      default:
         unreachable("bad jump type");
      }
   } else {
      nir_cf_node *next = nir_cf_node_next(&block->cf_node);
      if (next == NULL) {
         switch (state->parent_node->type) {
         case nir_cf_node_loop: {
            nir_block *first = nir_loop_first_block(state->loop);
            validate_assert(state, block->successors[0] == first);
            /* due to the hack for infinite loops, block->successors[1] may
             * point to the block after the loop.
             */
            break;
         }

         case nir_cf_node_if: {
            nir_block *after =
               nir_cf_node_as_block(nir_cf_node_next(state->parent_node));
            validate_assert(state, block->successors[0] == after);
            validate_assert(state, block->successors[1] == NULL);
            break;
         }

         case nir_cf_node_function:
            validate_assert(state, block->successors[0] == state->impl->end_block);
            validate_assert(state, block->successors[1] == NULL);
            break;

         default:
            unreachable("unknown control flow node type");
         }
      } else {
         if (next->type == nir_cf_node_if) {
            nir_if *if_stmt = nir_cf_node_as_if(next);
            validate_assert(state, block->successors[0] ==
                   nir_if_first_then_block(if_stmt));
            validate_assert(state, block->successors[1] ==
                   nir_if_first_else_block(if_stmt));
         } else {
            validate_assert(state, next->type == nir_cf_node_loop);
            nir_loop *loop = nir_cf_node_as_loop(next);
            validate_assert(state, block->successors[0] ==
                   nir_loop_first_block(loop));
            validate_assert(state, block->successors[1] == NULL);
         }
      }
   }
}

static void
validate_if(nir_if *if_stmt, validate_state *state)
{
   state->if_stmt = if_stmt;

   validate_assert(state, !exec_node_is_head_sentinel(if_stmt->cf_node.node.prev));
   nir_cf_node *prev_node = nir_cf_node_prev(&if_stmt->cf_node);
   validate_assert(state, prev_node->type == nir_cf_node_block);

   validate_assert(state, !exec_node_is_tail_sentinel(if_stmt->cf_node.node.next));
   nir_cf_node *next_node = nir_cf_node_next(&if_stmt->cf_node);
   validate_assert(state, next_node->type == nir_cf_node_block);

   validate_src(&if_stmt->condition, state, 32, 1);

   validate_assert(state, !exec_list_is_empty(&if_stmt->then_list));
   validate_assert(state, !exec_list_is_empty(&if_stmt->else_list));

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
   validate_assert(state, !exec_node_is_head_sentinel(loop->cf_node.node.prev));
   nir_cf_node *prev_node = nir_cf_node_prev(&loop->cf_node);
   validate_assert(state, prev_node->type == nir_cf_node_block);

   validate_assert(state, !exec_node_is_tail_sentinel(loop->cf_node.node.next));
   nir_cf_node *next_node = nir_cf_node_next(&loop->cf_node);
   validate_assert(state, next_node->type == nir_cf_node_block);

   validate_assert(state, !exec_list_is_empty(&loop->body));

   nir_cf_node *old_parent = state->parent_node;
   state->parent_node = &loop->cf_node;
   nir_loop *old_loop = state->loop;
   state->loop = loop;

   exec_list_validate(&loop->body);
   foreach_list_typed(nir_cf_node, cf_node, node, &loop->body) {
      validate_cf_node(cf_node, state);
   }

   state->parent_node = old_parent;
   state->loop = old_loop;
}

static void
validate_cf_node(nir_cf_node *node, validate_state *state)
{
   validate_assert(state, node->parent == state->parent_node);

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
   validate_assert(state, reg->is_global == is_global);

   if (is_global)
      validate_assert(state, reg->index < state->shader->reg_alloc);
   else
      validate_assert(state, reg->index < state->impl->reg_alloc);
   validate_assert(state, !BITSET_TEST(state->regs_found, reg->index));
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

   assume(entry);
   reg_validate_state *reg_state = (reg_validate_state *) entry->data;

   nir_foreach_use(src, reg) {
      struct set_entry *entry = _mesa_set_search(reg_state->uses, src);
      validate_assert(state, entry);
      _mesa_set_remove(reg_state->uses, entry);
   }

   if (reg_state->uses->entries != 0) {
      printf("extra entries in register uses:\n");
      struct set_entry *entry;
      set_foreach(reg_state->uses, entry)
         printf("%p\n", entry->key);

      abort();
   }

   nir_foreach_if_use(src, reg) {
      struct set_entry *entry = _mesa_set_search(reg_state->if_uses, src);
      validate_assert(state, entry);
      _mesa_set_remove(reg_state->if_uses, entry);
   }

   if (reg_state->if_uses->entries != 0) {
      printf("extra entries in register if_uses:\n");
      struct set_entry *entry;
      set_foreach(reg_state->if_uses, entry)
         printf("%p\n", entry->key);

      abort();
   }

   nir_foreach_def(src, reg) {
      struct set_entry *entry = _mesa_set_search(reg_state->defs, src);
      validate_assert(state, entry);
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
   state->var = var;

   validate_assert(state, is_global == nir_variable_is_global(var));

   /* Must have exactly one mode set */
   validate_assert(state, util_is_power_of_two_nonzero(var->data.mode));

   if (var->data.compact) {
      /* The "compact" flag is only valid on arrays of scalars. */
      assert(glsl_type_is_array(var->type));

      const struct glsl_type *type = glsl_get_array_element(var->type);
      if (nir_is_per_vertex_io(var, state->shader->info.stage)) {
         assert(glsl_type_is_array(type));
         assert(glsl_type_is_scalar(glsl_get_array_element(type)));
      } else {
         assert(glsl_type_is_scalar(type));
      }
   }

   /*
    * TODO validate some things ir_validate.cpp does (requires more GLSL type
    * support)
    */

   _mesa_hash_table_insert(state->var_defs, var,
                           is_global ? NULL : state->impl);

   state->var = NULL;
}

static bool
postvalidate_ssa_def(nir_ssa_def *def, void *void_state)
{
   validate_state *state = void_state;

   struct hash_entry *entry = _mesa_hash_table_search(state->ssa_defs, def);

   assume(entry);
   ssa_def_validate_state *def_state = (ssa_def_validate_state *)entry->data;

   nir_foreach_use(src, def) {
      struct set_entry *entry = _mesa_set_search(def_state->uses, src);
      validate_assert(state, entry);
      _mesa_set_remove(def_state->uses, entry);
   }

   if (def_state->uses->entries != 0) {
      printf("extra entries in SSA def uses:\n");
      struct set_entry *entry;
      set_foreach(def_state->uses, entry)
         printf("%p\n", entry->key);

      abort();
   }

   nir_foreach_if_use(src, def) {
      struct set_entry *entry = _mesa_set_search(def_state->if_uses, src);
      validate_assert(state, entry);
      _mesa_set_remove(def_state->if_uses, entry);
   }

   if (def_state->if_uses->entries != 0) {
      printf("extra entries in SSA def uses:\n");
      struct set_entry *entry;
      set_foreach(def_state->if_uses, entry)
         printf("%p\n", entry->key);

      abort();
   }

   return true;
}

static void
validate_function_impl(nir_function_impl *impl, validate_state *state)
{
   validate_assert(state, impl->function->impl == impl);
   validate_assert(state, impl->cf_node.parent == NULL);

   validate_assert(state, impl->num_params == impl->function->num_params);
   for (unsigned i = 0; i < impl->num_params; i++) {
      validate_assert(state, impl->params[i]->type == impl->function->params[i].type);
      validate_assert(state, impl->params[i]->data.mode == nir_var_param);
      validate_assert(state, impl->params[i]->data.location == i);
      validate_var_decl(impl->params[i], false, state);
   }

   if (glsl_type_is_void(impl->function->return_type)) {
      validate_assert(state, impl->return_var == NULL);
   } else {
      validate_assert(state, impl->return_var->type == impl->function->return_type);
      validate_assert(state, impl->return_var->data.mode == nir_var_param);
      validate_assert(state, impl->return_var->data.location == -1);
      validate_var_decl(impl->return_var, false, state);
   }

   validate_assert(state, exec_list_is_empty(&impl->end_block->instr_list));
   validate_assert(state, impl->end_block->successors[0] == NULL);
   validate_assert(state, impl->end_block->successors[1] == NULL);

   state->impl = impl;
   state->parent_node = &impl->cf_node;

   exec_list_validate(&impl->locals);
   nir_foreach_variable(var, &impl->locals) {
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

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block)
         nir_foreach_ssa_def(instr, postvalidate_ssa_def, state);
   }
}

static void
validate_function(nir_function *func, validate_state *state)
{
   if (func->impl != NULL) {
      validate_assert(state, func->impl->function == func);
      validate_function_impl(func->impl, state);
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
   state->errors = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                           _mesa_key_pointer_equal);

   state->loop = NULL;
   state->instr = NULL;
   state->var = NULL;
}

static void
destroy_validate_state(validate_state *state)
{
   _mesa_hash_table_destroy(state->regs, NULL);
   _mesa_hash_table_destroy(state->ssa_defs, NULL);
   free(state->ssa_defs_found);
   free(state->regs_found);
   _mesa_hash_table_destroy(state->var_defs, NULL);
   _mesa_hash_table_destroy(state->errors, NULL);
}

static void
dump_errors(validate_state *state)
{
   struct hash_table *errors = state->errors;

   fprintf(stderr, "%d errors:\n", _mesa_hash_table_num_entries(errors));

   nir_print_shader_annotated(state->shader, stderr, errors);

   if (_mesa_hash_table_num_entries(errors) > 0) {
      fprintf(stderr, "%d additional errors:\n",
              _mesa_hash_table_num_entries(errors));
      struct hash_entry *entry;
      hash_table_foreach(errors, entry) {
         fprintf(stderr, "%s\n", (char *)entry->data);
      }
   }

   abort();
}

void
nir_validate_shader(nir_shader *shader)
{
   static int should_validate = -1;
   if (should_validate < 0)
      should_validate = env_var_as_boolean("NIR_VALIDATE", true);
   if (!should_validate)
      return;

   validate_state state;
   init_validate_state(&state);

   state.shader = shader;

   exec_list_validate(&shader->uniforms);
   nir_foreach_variable(var, &shader->uniforms) {
      validate_var_decl(var, true, &state);
   }

   exec_list_validate(&shader->inputs);
   nir_foreach_variable(var, &shader->inputs) {
     validate_var_decl(var, true, &state);
   }

   exec_list_validate(&shader->outputs);
   nir_foreach_variable(var, &shader->outputs) {
     validate_var_decl(var, true, &state);
   }

   exec_list_validate(&shader->shared);
   nir_foreach_variable(var, &shader->shared) {
      validate_var_decl(var, true, &state);
   }

   exec_list_validate(&shader->globals);
   nir_foreach_variable(var, &shader->globals) {
     validate_var_decl(var, true, &state);
   }

   exec_list_validate(&shader->system_values);
   nir_foreach_variable(var, &shader->system_values) {
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

   if (_mesa_hash_table_num_entries(state.errors) > 0)
      dump_errors(&state);

   destroy_validate_state(&state);
}

#endif /* NDEBUG */
