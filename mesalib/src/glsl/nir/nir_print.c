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
#include <stdio.h>
#include <stdlib.h>

static void
print_tabs(unsigned num_tabs, FILE *fp)
{
   for (unsigned i = 0; i < num_tabs; i++)
      fprintf(fp, "\t");
}

typedef struct {
   /** map from nir_variable -> printable name */
   struct hash_table *ht;

   /** set of names used so far for nir_variables */
   struct set *syms;

   /* an index used to make new non-conflicting names */
   unsigned index;
} print_var_state;

static void
print_register(nir_register *reg, FILE *fp)
{
   if (reg->name != NULL)
      fprintf(fp, "/* %s */ ", reg->name);
   if (reg->is_global)
      fprintf(fp, "gr%u", reg->index);
   else
      fprintf(fp, "r%u", reg->index);
}

static const char *sizes[] = { "error", "vec1", "vec2", "vec3", "vec4" };

static void
print_register_decl(nir_register *reg, FILE *fp)
{
   fprintf(fp, "decl_reg %s ", sizes[reg->num_components]);
   if (reg->is_packed)
      fprintf(fp, "(packed) ");
   print_register(reg, fp);
   if (reg->num_array_elems != 0)
      fprintf(fp, "[%u]", reg->num_array_elems);
   fprintf(fp, "\n");
}

static void
print_ssa_def(nir_ssa_def *def, FILE *fp)
{
   if (def->name != NULL)
      fprintf(fp, "/* %s */ ", def->name);
   fprintf(fp, "%s ssa_%u", sizes[def->num_components], def->index);
}

static void
print_ssa_use(nir_ssa_def *def, FILE *fp)
{
   if (def->name != NULL)
      fprintf(fp, "/* %s */ ", def->name);
   fprintf(fp, "ssa_%u", def->index);
}

static void print_src(nir_src *src, FILE *fp);

static void
print_reg_src(nir_reg_src *src, FILE *fp)
{
   print_register(src->reg, fp);
   if (src->reg->num_array_elems != 0) {
      fprintf(fp, "[%u", src->base_offset);
      if (src->indirect != NULL) {
         fprintf(fp, " + ");
         print_src(src->indirect, fp);
      }
      fprintf(fp, "]");
   }
}

static void
print_reg_dest(nir_reg_dest *dest, FILE *fp)
{
   print_register(dest->reg, fp);
   if (dest->reg->num_array_elems != 0) {
      fprintf(fp, "[%u", dest->base_offset);
      if (dest->indirect != NULL) {
         fprintf(fp, " + ");
         print_src(dest->indirect, fp);
      }
      fprintf(fp, "]");
   }
}

static void
print_src(nir_src *src, FILE *fp)
{
   if (src->is_ssa)
      print_ssa_use(src->ssa, fp);
   else
      print_reg_src(&src->reg, fp);
}

static void
print_dest(nir_dest *dest, FILE *fp)
{
   if (dest->is_ssa)
      print_ssa_def(&dest->ssa, fp);
   else
      print_reg_dest(&dest->reg, fp);
}

static void
print_alu_src(nir_alu_instr *instr, unsigned src, FILE *fp)
{
   if (instr->src[src].negate)
      fprintf(fp, "-");
   if (instr->src[src].abs)
      fprintf(fp, "abs(");

   print_src(&instr->src[src].src, fp);

   bool print_swizzle = false;
   for (unsigned i = 0; i < 4; i++) {
      if (!nir_alu_instr_channel_used(instr, src, i))
         continue;

      if (instr->src[src].swizzle[i] != i) {
         print_swizzle = true;
         break;
      }
   }

   if (print_swizzle) {
      fprintf(fp, ".");
      for (unsigned i = 0; i < 4; i++) {
         if (!nir_alu_instr_channel_used(instr, src, i))
            continue;

         fprintf(fp, "%c", "xyzw"[instr->src[src].swizzle[i]]);
      }
   }

   if (instr->src[src].abs)
      fprintf(fp, ")");
}

static void
print_alu_dest(nir_alu_dest *dest, FILE *fp)
{
   /* we're going to print the saturate modifier later, after the opcode */

   print_dest(&dest->dest, fp);

   if (!dest->dest.is_ssa &&
       dest->write_mask != (1 << dest->dest.reg.reg->num_components) - 1) {
      fprintf(fp, ".");
      for (unsigned i = 0; i < 4; i++)
         if ((dest->write_mask >> i) & 1)
            fprintf(fp, "%c", "xyzw"[i]);
   }
}

static void
print_alu_instr(nir_alu_instr *instr, FILE *fp)
{
   print_alu_dest(&instr->dest, fp);

   fprintf(fp, " = %s", nir_op_infos[instr->op].name);
   if (instr->dest.saturate)
      fprintf(fp, ".sat");
   fprintf(fp, " ");

   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      if (i != 0)
         fprintf(fp, ", ");

      print_alu_src(instr, i, fp);
   }
}

static void
print_var_decl(nir_variable *var, print_var_state *state, FILE *fp)
{
   fprintf(fp, "decl_var ");

   const char *const cent = (var->data.centroid) ? "centroid " : "";
   const char *const samp = (var->data.sample) ? "sample " : "";
   const char *const inv = (var->data.invariant) ? "invariant " : "";
   const char *const mode[] = { "shader_in ", "shader_out ", "", "",
                                "uniform ", "shader_storage", "system " };
   const char *const interp[] = { "", "smooth", "flat", "noperspective" };

   fprintf(fp, "%s%s%s%s%s ",
      cent, samp, inv, mode[var->data.mode], interp[var->data.interpolation]);

   glsl_print_type(var->type, fp);

   struct set_entry *entry = NULL;
   if (state)
      entry = _mesa_set_search(state->syms, var->name);

   char *name;

   if (entry != NULL) {
      /* we have a collision with another name, append an @ + a unique index */
      name = ralloc_asprintf(state->syms, "%s@%u", var->name, state->index++);
   } else {
      name = var->name;
   }

   fprintf(fp, " %s", name);

   if (var->data.mode == nir_var_shader_in ||
       var->data.mode == nir_var_shader_out ||
       var->data.mode == nir_var_uniform ||
       var->data.mode == nir_var_shader_storage) {
      fprintf(fp, " (%u, %u)", var->data.location, var->data.driver_location);
   }

   fprintf(fp, "\n");

   if (state) {
      _mesa_set_add(state->syms, name);
      _mesa_hash_table_insert(state->ht, var, name);
   }
}

static void
print_var(nir_variable *var, print_var_state *state, FILE *fp)
{
   const char *name;
   if (state) {
      struct hash_entry *entry = _mesa_hash_table_search(state->ht, var);

      assert(entry != NULL);
      name = entry->data;
   } else {
      name = var->name;
   }

   fprintf(fp, "%s", name);
}

static void
print_deref_var(nir_deref_var *deref, print_var_state *state, FILE *fp)
{
   print_var(deref->var, state, fp);
}

static void
print_deref_array(nir_deref_array *deref, print_var_state *state, FILE *fp)
{
   fprintf(fp, "[");
   switch (deref->deref_array_type) {
   case nir_deref_array_type_direct:
      fprintf(fp, "%u", deref->base_offset);
      break;
   case nir_deref_array_type_indirect:
      if (deref->base_offset != 0)
         fprintf(fp, "%u + ", deref->base_offset);
      print_src(&deref->indirect, fp);
      break;
   case nir_deref_array_type_wildcard:
      fprintf(fp, "*");
      break;
   }
   fprintf(fp, "]");
}

static void
print_deref_struct(nir_deref_struct *deref, const struct glsl_type *parent_type,
                   print_var_state *state, FILE *fp)
{
   fprintf(fp, ".%s", glsl_get_struct_elem_name(parent_type, deref->index));
}

static void
print_deref(nir_deref_var *deref, print_var_state *state, FILE *fp)
{
   nir_deref *tail = &deref->deref;
   nir_deref *pretail = NULL;
   while (tail != NULL) {
      switch (tail->deref_type) {
      case nir_deref_type_var:
         assert(pretail == NULL);
         assert(tail == &deref->deref);
         print_deref_var(deref, state, fp);
         break;

      case nir_deref_type_array:
         assert(pretail != NULL);
         print_deref_array(nir_deref_as_array(tail), state, fp);
         break;

      case nir_deref_type_struct:
         assert(pretail != NULL);
         print_deref_struct(nir_deref_as_struct(tail),
                            pretail->type, state, fp);
         break;

      default:
         unreachable("Invalid deref type");
      }

      pretail = tail;
      tail = pretail->child;
   }
}

static void
print_intrinsic_instr(nir_intrinsic_instr *instr, print_var_state *state,
                      FILE *fp)
{
   unsigned num_srcs = nir_intrinsic_infos[instr->intrinsic].num_srcs;

   if (nir_intrinsic_infos[instr->intrinsic].has_dest) {
      print_dest(&instr->dest, fp);
      fprintf(fp, " = ");
   }

   fprintf(fp, "intrinsic %s (", nir_intrinsic_infos[instr->intrinsic].name);

   for (unsigned i = 0; i < num_srcs; i++) {
      if (i != 0)
         fprintf(fp, ", ");

      print_src(&instr->src[i], fp);
   }

   fprintf(fp, ") (");

   unsigned num_vars = nir_intrinsic_infos[instr->intrinsic].num_variables;

   for (unsigned i = 0; i < num_vars; i++) {
      if (i != 0)
         fprintf(fp, ", ");

      print_deref(instr->variables[i], state, fp);
   }

   fprintf(fp, ") (");

   unsigned num_indices = nir_intrinsic_infos[instr->intrinsic].num_indices;

   for (unsigned i = 0; i < num_indices; i++) {
      if (i != 0)
         fprintf(fp, ", ");

      fprintf(fp, "%u", instr->const_index[i]);
   }

   fprintf(fp, ")");
}

static void
print_tex_instr(nir_tex_instr *instr, print_var_state *state, FILE *fp)
{
   print_dest(&instr->dest, fp);

   fprintf(fp, " = ");

   switch (instr->op) {
   case nir_texop_tex:
      fprintf(fp, "tex ");
      break;
   case nir_texop_txb:
      fprintf(fp, "txb ");
      break;
   case nir_texop_txl:
      fprintf(fp, "txl ");
      break;
   case nir_texop_txd:
      fprintf(fp, "txd ");
      break;
   case nir_texop_txf:
      fprintf(fp, "txf ");
      break;
   case nir_texop_txf_ms:
      fprintf(fp, "txf_ms ");
      break;
   case nir_texop_txs:
      fprintf(fp, "txs ");
      break;
   case nir_texop_lod:
      fprintf(fp, "lod ");
      break;
   case nir_texop_tg4:
      fprintf(fp, "tg4 ");
      break;
   case nir_texop_query_levels:
      fprintf(fp, "query_levels ");
      break;

   default:
      unreachable("Invalid texture operation");
      break;
   }

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      print_src(&instr->src[i].src, fp);

      fprintf(fp, " ");

      switch(instr->src[i].src_type) {
      case nir_tex_src_coord:
         fprintf(fp, "(coord)");
         break;
      case nir_tex_src_projector:
         fprintf(fp, "(projector)");
         break;
      case nir_tex_src_comparitor:
         fprintf(fp, "(comparitor)");
         break;
      case nir_tex_src_offset:
         fprintf(fp, "(offset)");
         break;
      case nir_tex_src_bias:
         fprintf(fp, "(bias)");
         break;
      case nir_tex_src_lod:
         fprintf(fp, "(lod)");
         break;
      case nir_tex_src_ms_index:
         fprintf(fp, "(ms_index)");
         break;
      case nir_tex_src_ddx:
         fprintf(fp, "(ddx)");
         break;
      case nir_tex_src_ddy:
         fprintf(fp, "(ddy)");
         break;
      case nir_tex_src_sampler_offset:
         fprintf(fp, "(sampler_offset)");
         break;

      default:
         unreachable("Invalid texture source type");
         break;
      }

      fprintf(fp, ", ");
   }

   bool has_nonzero_offset = false;
   for (unsigned i = 0; i < 4; i++) {
      if (instr->const_offset[i] != 0) {
         has_nonzero_offset = true;
         break;
      }
   }

   if (has_nonzero_offset) {
      fprintf(fp, "[%i %i %i %i] (offset), ",
              instr->const_offset[0], instr->const_offset[1],
              instr->const_offset[2], instr->const_offset[3]);
   }

   if (instr->op == nir_texop_tg4) {
      fprintf(fp, "%u (gather_component), ", instr->component);
   }

   if (instr->sampler) {
      print_deref(instr->sampler, state, fp);
   } else {
      fprintf(fp, "%u", instr->sampler_index);
   }

   fprintf(fp, " (sampler)");
}

static void
print_call_instr(nir_call_instr *instr, print_var_state *state, FILE *fp)
{
   fprintf(fp, "call %s ", instr->callee->function->name);

   for (unsigned i = 0; i < instr->num_params; i++) {
      if (i != 0)
         fprintf(fp, ", ");

      print_deref(instr->params[i], state, fp);
   }

   if (instr->return_deref != NULL) {
      if (instr->num_params != 0)
         fprintf(fp, ", ");
      fprintf(fp, "returning ");
      print_deref(instr->return_deref, state, fp);
   }
}

static void
print_load_const_instr(nir_load_const_instr *instr, unsigned tabs, FILE *fp)
{
   print_ssa_def(&instr->def, fp);

   fprintf(fp, " = load_const (");

   for (unsigned i = 0; i < instr->def.num_components; i++) {
      if (i != 0)
         fprintf(fp, ", ");

      /*
       * we don't really know the type of the constant (if it will be used as a
       * float or an int), so just print the raw constant in hex for fidelity
       * and then print the float in a comment for readability.
       */

      fprintf(fp, "0x%08x /* %f */", instr->value.u[i], instr->value.f[i]);
   }

   fprintf(fp, ")");
}

static void
print_jump_instr(nir_jump_instr *instr, FILE *fp)
{
   switch (instr->type) {
   case nir_jump_break:
      fprintf(fp, "break");
      break;

   case nir_jump_continue:
      fprintf(fp, "continue");
      break;

   case nir_jump_return:
      fprintf(fp, "return");
      break;
   }
}

static void
print_ssa_undef_instr(nir_ssa_undef_instr* instr, FILE *fp)
{
   print_ssa_def(&instr->def, fp);
   fprintf(fp, " = undefined");
}

static void
print_phi_instr(nir_phi_instr *instr, FILE *fp)
{
   print_dest(&instr->dest, fp);
   fprintf(fp, " = phi ");
   nir_foreach_phi_src(instr, src) {
      if (&src->node != exec_list_get_head(&instr->srcs))
         fprintf(fp, ", ");

      fprintf(fp, "block_%u: ", src->pred->index);
      print_src(&src->src, fp);
   }
}

static void
print_parallel_copy_instr(nir_parallel_copy_instr *instr, FILE *fp)
{
   nir_foreach_parallel_copy_entry(instr, entry) {
      if (&entry->node != exec_list_get_head(&instr->entries))
         fprintf(fp, "; ");

      print_dest(&entry->dest, fp);
      fprintf(fp, " = ");
      print_src(&entry->src, fp);
   }
}

static void
print_instr(const nir_instr *instr, print_var_state *state, unsigned tabs, FILE *fp)
{
   print_tabs(tabs, fp);

   switch (instr->type) {
   case nir_instr_type_alu:
      print_alu_instr(nir_instr_as_alu(instr), fp);
      break;

   case nir_instr_type_call:
      print_call_instr(nir_instr_as_call(instr), state, fp);
      break;

   case nir_instr_type_intrinsic:
      print_intrinsic_instr(nir_instr_as_intrinsic(instr), state, fp);
      break;

   case nir_instr_type_tex:
      print_tex_instr(nir_instr_as_tex(instr), state, fp);
      break;

   case nir_instr_type_load_const:
      print_load_const_instr(nir_instr_as_load_const(instr), tabs, fp);
      break;

   case nir_instr_type_jump:
      print_jump_instr(nir_instr_as_jump(instr), fp);
      break;

   case nir_instr_type_ssa_undef:
      print_ssa_undef_instr(nir_instr_as_ssa_undef(instr), fp);
      break;

   case nir_instr_type_phi:
      print_phi_instr(nir_instr_as_phi(instr), fp);
      break;

   case nir_instr_type_parallel_copy:
      print_parallel_copy_instr(nir_instr_as_parallel_copy(instr), fp);
      break;

   default:
      unreachable("Invalid instruction type");
      break;
   }
}

static int
compare_block_index(const void *p1, const void *p2)
{
   const nir_block *block1 = *((const nir_block **) p1);
   const nir_block *block2 = *((const nir_block **) p2);

   return (int) block1->index - (int) block2->index;
}

static void print_cf_node(nir_cf_node *node, print_var_state *state,
                          unsigned tabs, FILE *fp);

static void
print_block(nir_block *block, print_var_state *state, unsigned tabs, FILE *fp)
{
   print_tabs(tabs, fp);
   fprintf(fp, "block block_%u:\n", block->index);

   /* sort the predecessors by index so we consistently print the same thing */

   nir_block **preds =
      malloc(block->predecessors->entries * sizeof(nir_block *));

   struct set_entry *entry;
   unsigned i = 0;
   set_foreach(block->predecessors, entry) {
      preds[i++] = (nir_block *) entry->key;
   }

   qsort(preds, block->predecessors->entries, sizeof(nir_block *),
         compare_block_index);

   print_tabs(tabs, fp);
   fprintf(fp, "/* preds: ");
   for (unsigned i = 0; i < block->predecessors->entries; i++) {
      fprintf(fp, "block_%u ", preds[i]->index);
   }
   fprintf(fp, "*/\n");

   free(preds);

   nir_foreach_instr(block, instr) {
      print_instr(instr, state, tabs, fp);
      fprintf(fp, "\n");
   }

   print_tabs(tabs, fp);
   fprintf(fp, "/* succs: ");
   for (unsigned i = 0; i < 2; i++)
      if (block->successors[i]) {
         fprintf(fp, "block_%u ", block->successors[i]->index);
      }
   fprintf(fp, "*/\n");
}

static void
print_if(nir_if *if_stmt, print_var_state *state, unsigned tabs, FILE *fp)
{
   print_tabs(tabs, fp);
   fprintf(fp, "if ");
   print_src(&if_stmt->condition, fp);
   fprintf(fp, " {\n");
   foreach_list_typed(nir_cf_node, node, node, &if_stmt->then_list) {
      print_cf_node(node, state, tabs + 1, fp);
   }
   print_tabs(tabs, fp);
   fprintf(fp, "} else {\n");
   foreach_list_typed(nir_cf_node, node, node, &if_stmt->else_list) {
      print_cf_node(node, state, tabs + 1, fp);
   }
   print_tabs(tabs, fp);
   fprintf(fp, "}\n");
}

static void
print_loop(nir_loop *loop, print_var_state *state, unsigned tabs, FILE *fp)
{
   print_tabs(tabs, fp);
   fprintf(fp, "loop {\n");
   foreach_list_typed(nir_cf_node, node, node, &loop->body) {
      print_cf_node(node, state, tabs + 1, fp);
   }
   print_tabs(tabs, fp);
   fprintf(fp, "}\n");
}

static void
print_cf_node(nir_cf_node *node, print_var_state *state, unsigned int tabs,
              FILE *fp)
{
   switch (node->type) {
   case nir_cf_node_block:
      print_block(nir_cf_node_as_block(node), state, tabs, fp);
      break;

   case nir_cf_node_if:
      print_if(nir_cf_node_as_if(node), state, tabs, fp);
      break;

   case nir_cf_node_loop:
      print_loop(nir_cf_node_as_loop(node), state, tabs, fp);
      break;

   default:
      unreachable("Invalid CFG node type");
   }
}

static void
print_function_impl(nir_function_impl *impl, print_var_state *state, FILE *fp)
{
   fprintf(fp, "\nimpl %s ", impl->overload->function->name);

   for (unsigned i = 0; i < impl->num_params; i++) {
      if (i != 0)
         fprintf(fp, ", ");

      print_var(impl->params[i], state, fp);
   }

   if (impl->return_var != NULL) {
      if (impl->num_params != 0)
         fprintf(fp, ", ");
      fprintf(fp, "returning ");
      print_var(impl->return_var, state, fp);
   }

   fprintf(fp, "{\n");

   foreach_list_typed(nir_variable, var, node, &impl->locals) {
      fprintf(fp, "\t");
      print_var_decl(var, state, fp);
   }

   foreach_list_typed(nir_register, reg, node, &impl->registers) {
      fprintf(fp, "\t");
      print_register_decl(reg, fp);
   }

   nir_index_blocks(impl);

   foreach_list_typed(nir_cf_node, node, node, &impl->body) {
      print_cf_node(node, state, 1, fp);
   }

   fprintf(fp, "\tblock block_%u:\n}\n\n", impl->end_block->index);
}

static void
print_function_overload(nir_function_overload *overload,
                        print_var_state *state, FILE *fp)
{
   fprintf(fp, "decl_overload %s ", overload->function->name);

   for (unsigned i = 0; i < overload->num_params; i++) {
      if (i != 0)
         fprintf(fp, ", ");

      switch (overload->params[i].param_type) {
      case nir_parameter_in:
         fprintf(fp, "in ");
         break;
      case nir_parameter_out:
         fprintf(fp, "out ");
         break;
      case nir_parameter_inout:
         fprintf(fp, "inout ");
         break;
      default:
         unreachable("Invalid parameter type");
      }

      glsl_print_type(overload->params[i].type, fp);
   }

   if (overload->return_type != NULL) {
      if (overload->num_params != 0)
         fprintf(fp, ", ");
      fprintf(fp, "returning ");
      glsl_print_type(overload->return_type, fp);
   }

   fprintf(fp, "\n");

   if (overload->impl != NULL) {
      print_function_impl(overload->impl, state, fp);
      return;
   }
}

static void
print_function(nir_function *func, print_var_state *state, FILE *fp)
{
   foreach_list_typed(nir_function_overload, overload, node, &func->overload_list) {
      print_function_overload(overload, state, fp);
   }
}

static void
init_print_state(print_var_state *state)
{
   state->ht = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                       _mesa_key_pointer_equal);
   state->syms = _mesa_set_create(NULL, _mesa_key_hash_string,
                                  _mesa_key_string_equal);
   state->index = 0;
}

static void
destroy_print_state(print_var_state *state)
{
   _mesa_hash_table_destroy(state->ht, NULL);
   _mesa_set_destroy(state->syms, NULL);
}

void
nir_print_shader(nir_shader *shader, FILE *fp)
{
   print_var_state state;
   init_print_state(&state);

   foreach_list_typed(nir_variable, var, node, &shader->uniforms) {
      print_var_decl(var, &state, fp);
   }

   foreach_list_typed(nir_variable, var, node, &shader->inputs) {
      print_var_decl(var, &state, fp);
   }

   foreach_list_typed(nir_variable, var, node, &shader->outputs) {
      print_var_decl(var, &state, fp);
   }

   foreach_list_typed(nir_variable, var, node, &shader->globals) {
      print_var_decl(var, &state, fp);
   }

   foreach_list_typed(nir_variable, var, node, &shader->system_values) {
      print_var_decl(var, &state, fp);
   }

   foreach_list_typed(nir_register, reg, node, &shader->registers) {
      print_register_decl(reg, fp);
   }

   foreach_list_typed(nir_function, func, node, &shader->functions) {
      print_function(func, &state, fp);
   }

   destroy_print_state(&state);
}

void
nir_print_instr(const nir_instr *instr, FILE *fp)
{
   print_instr(instr, NULL, 0, fp);
}
