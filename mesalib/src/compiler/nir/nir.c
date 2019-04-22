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
#include "nir_control_flow_private.h"
#include "util/half_float.h"
#include <limits.h>
#include <assert.h>
#include <math.h>
#include "util/u_math.h"

#include "main/menums.h" /* BITFIELD64_MASK */

nir_shader *
nir_shader_create(void *mem_ctx,
                  gl_shader_stage stage,
                  const nir_shader_compiler_options *options,
                  shader_info *si)
{
   nir_shader *shader = rzalloc(mem_ctx, nir_shader);

   exec_list_make_empty(&shader->uniforms);
   exec_list_make_empty(&shader->inputs);
   exec_list_make_empty(&shader->outputs);
   exec_list_make_empty(&shader->shared);

   shader->options = options;

   if (si) {
      assert(si->stage == stage);
      shader->info = *si;
   } else {
      shader->info.stage = stage;
   }

   exec_list_make_empty(&shader->functions);
   exec_list_make_empty(&shader->globals);
   exec_list_make_empty(&shader->system_values);

   shader->num_inputs = 0;
   shader->num_outputs = 0;
   shader->num_uniforms = 0;
   shader->num_shared = 0;

   return shader;
}

static nir_register *
reg_create(void *mem_ctx, struct exec_list *list)
{
   nir_register *reg = ralloc(mem_ctx, nir_register);

   list_inithead(&reg->uses);
   list_inithead(&reg->defs);
   list_inithead(&reg->if_uses);

   reg->num_components = 0;
   reg->bit_size = 32;
   reg->num_array_elems = 0;
   reg->name = NULL;

   exec_list_push_tail(list, &reg->node);

   return reg;
}

nir_register *
nir_local_reg_create(nir_function_impl *impl)
{
   nir_register *reg = reg_create(ralloc_parent(impl), &impl->registers);
   reg->index = impl->reg_alloc++;

   return reg;
}

void
nir_reg_remove(nir_register *reg)
{
   exec_node_remove(&reg->node);
}

void
nir_shader_add_variable(nir_shader *shader, nir_variable *var)
{
   switch (var->data.mode) {
   case nir_var_all:
      assert(!"invalid mode");
      break;

   case nir_var_function_temp:
      assert(!"nir_shader_add_variable cannot be used for local variables");
      break;

   case nir_var_shader_temp:
      exec_list_push_tail(&shader->globals, &var->node);
      break;

   case nir_var_shader_in:
      exec_list_push_tail(&shader->inputs, &var->node);
      break;

   case nir_var_shader_out:
      exec_list_push_tail(&shader->outputs, &var->node);
      break;

   case nir_var_uniform:
   case nir_var_mem_ubo:
   case nir_var_mem_ssbo:
      exec_list_push_tail(&shader->uniforms, &var->node);
      break;

   case nir_var_mem_shared:
      assert(gl_shader_stage_is_compute(shader->info.stage));
      exec_list_push_tail(&shader->shared, &var->node);
      break;

   case nir_var_mem_global:
      assert(!"nir_shader_add_variable cannot be used for global memory");
      break;

   case nir_var_system_value:
      exec_list_push_tail(&shader->system_values, &var->node);
      break;
   }
}

nir_variable *
nir_variable_create(nir_shader *shader, nir_variable_mode mode,
                    const struct glsl_type *type, const char *name)
{
   nir_variable *var = rzalloc(shader, nir_variable);
   var->name = ralloc_strdup(var, name);
   var->type = type;
   var->data.mode = mode;
   var->data.how_declared = nir_var_declared_normally;

   if ((mode == nir_var_shader_in &&
        shader->info.stage != MESA_SHADER_VERTEX) ||
       (mode == nir_var_shader_out &&
        shader->info.stage != MESA_SHADER_FRAGMENT))
      var->data.interpolation = INTERP_MODE_SMOOTH;

   if (mode == nir_var_shader_in || mode == nir_var_uniform)
      var->data.read_only = true;

   nir_shader_add_variable(shader, var);

   return var;
}

nir_variable *
nir_local_variable_create(nir_function_impl *impl,
                          const struct glsl_type *type, const char *name)
{
   nir_variable *var = rzalloc(impl->function->shader, nir_variable);
   var->name = ralloc_strdup(var, name);
   var->type = type;
   var->data.mode = nir_var_function_temp;

   nir_function_impl_add_variable(impl, var);

   return var;
}

nir_function *
nir_function_create(nir_shader *shader, const char *name)
{
   nir_function *func = ralloc(shader, nir_function);

   exec_list_push_tail(&shader->functions, &func->node);

   func->name = ralloc_strdup(func, name);
   func->shader = shader;
   func->num_params = 0;
   func->params = NULL;
   func->impl = NULL;
   func->is_entrypoint = false;

   return func;
}

/* NOTE: if the instruction you are copying a src to is already added
 * to the IR, use nir_instr_rewrite_src() instead.
 */
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

void nir_dest_copy(nir_dest *dest, const nir_dest *src, nir_instr *instr)
{
   /* Copying an SSA definition makes no sense whatsoever. */
   assert(!src->is_ssa);

   dest->is_ssa = false;

   dest->reg.base_offset = src->reg.base_offset;
   dest->reg.reg = src->reg.reg;
   if (src->reg.indirect) {
      dest->reg.indirect = ralloc(instr, nir_src);
      nir_src_copy(dest->reg.indirect, src->reg.indirect, instr);
   } else {
      dest->reg.indirect = NULL;
   }
}

void
nir_alu_src_copy(nir_alu_src *dest, const nir_alu_src *src,
                 nir_alu_instr *instr)
{
   nir_src_copy(&dest->src, &src->src, &instr->instr);
   dest->abs = src->abs;
   dest->negate = src->negate;
   for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; i++)
      dest->swizzle[i] = src->swizzle[i];
}

void
nir_alu_dest_copy(nir_alu_dest *dest, const nir_alu_dest *src,
                  nir_alu_instr *instr)
{
   nir_dest_copy(&dest->dest, &src->dest, &instr->instr);
   dest->write_mask = src->write_mask;
   dest->saturate = src->saturate;
}


static void
cf_init(nir_cf_node *node, nir_cf_node_type type)
{
   exec_node_init(&node->node);
   node->parent = NULL;
   node->type = type;
}

nir_function_impl *
nir_function_impl_create_bare(nir_shader *shader)
{
   nir_function_impl *impl = ralloc(shader, nir_function_impl);

   impl->function = NULL;

   cf_init(&impl->cf_node, nir_cf_node_function);

   exec_list_make_empty(&impl->body);
   exec_list_make_empty(&impl->registers);
   exec_list_make_empty(&impl->locals);
   impl->reg_alloc = 0;
   impl->ssa_alloc = 0;
   impl->valid_metadata = nir_metadata_none;

   /* create start & end blocks */
   nir_block *start_block = nir_block_create(shader);
   nir_block *end_block = nir_block_create(shader);
   start_block->cf_node.parent = &impl->cf_node;
   end_block->cf_node.parent = &impl->cf_node;
   impl->end_block = end_block;

   exec_list_push_tail(&impl->body, &start_block->cf_node.node);

   start_block->successors[0] = end_block;
   _mesa_set_add(end_block->predecessors, start_block);
   return impl;
}

nir_function_impl *
nir_function_impl_create(nir_function *function)
{
   assert(function->impl == NULL);

   nir_function_impl *impl = nir_function_impl_create_bare(function->shader);

   function->impl = impl;
   impl->function = function;

   return impl;
}

nir_block *
nir_block_create(nir_shader *shader)
{
   nir_block *block = rzalloc(shader, nir_block);

   cf_init(&block->cf_node, nir_cf_node_block);

   block->successors[0] = block->successors[1] = NULL;
   block->predecessors = _mesa_pointer_set_create(block);
   block->imm_dom = NULL;
   /* XXX maybe it would be worth it to defer allocation?  This
    * way it doesn't get allocated for shader refs that never run
    * nir_calc_dominance?  For example, state-tracker creates an
    * initial IR, clones that, runs appropriate lowering pass, passes
    * to driver which does common lowering/opt, and then stores ref
    * which is later used to do state specific lowering and futher
    * opt.  Do any of the references not need dominance metadata?
    */
   block->dom_frontier = _mesa_pointer_set_create(block);

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
nir_if_create(nir_shader *shader)
{
   nir_if *if_stmt = ralloc(shader, nir_if);

   if_stmt->control = nir_selection_control_none;

   cf_init(&if_stmt->cf_node, nir_cf_node_if);
   src_init(&if_stmt->condition);

   nir_block *then = nir_block_create(shader);
   exec_list_make_empty(&if_stmt->then_list);
   exec_list_push_tail(&if_stmt->then_list, &then->cf_node.node);
   then->cf_node.parent = &if_stmt->cf_node;

   nir_block *else_stmt = nir_block_create(shader);
   exec_list_make_empty(&if_stmt->else_list);
   exec_list_push_tail(&if_stmt->else_list, &else_stmt->cf_node.node);
   else_stmt->cf_node.parent = &if_stmt->cf_node;

   return if_stmt;
}

nir_loop *
nir_loop_create(nir_shader *shader)
{
   nir_loop *loop = rzalloc(shader, nir_loop);

   cf_init(&loop->cf_node, nir_cf_node_loop);

   nir_block *body = nir_block_create(shader);
   exec_list_make_empty(&loop->body);
   exec_list_push_tail(&loop->body, &body->cf_node.node);
   body->cf_node.parent = &loop->cf_node;

   body->successors[0] = body;
   _mesa_set_add(body->predecessors, body);

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
   for (int i = 0; i < NIR_MAX_VEC_COMPONENTS; ++i)
      src->swizzle[i] = i;
}

nir_alu_instr *
nir_alu_instr_create(nir_shader *shader, nir_op op)
{
   unsigned num_srcs = nir_op_infos[op].num_inputs;
   /* TODO: don't use rzalloc */
   nir_alu_instr *instr =
      rzalloc_size(shader,
                   sizeof(nir_alu_instr) + num_srcs * sizeof(nir_alu_src));

   instr_init(&instr->instr, nir_instr_type_alu);
   instr->op = op;
   alu_dest_init(&instr->dest);
   for (unsigned i = 0; i < num_srcs; i++)
      alu_src_init(&instr->src[i]);

   return instr;
}

nir_deref_instr *
nir_deref_instr_create(nir_shader *shader, nir_deref_type deref_type)
{
   nir_deref_instr *instr =
      rzalloc_size(shader, sizeof(nir_deref_instr));

   instr_init(&instr->instr, nir_instr_type_deref);

   instr->deref_type = deref_type;
   if (deref_type != nir_deref_type_var)
      src_init(&instr->parent);

   if (deref_type == nir_deref_type_array ||
       deref_type == nir_deref_type_ptr_as_array)
      src_init(&instr->arr.index);

   dest_init(&instr->dest);

   return instr;
}

nir_jump_instr *
nir_jump_instr_create(nir_shader *shader, nir_jump_type type)
{
   nir_jump_instr *instr = ralloc(shader, nir_jump_instr);
   instr_init(&instr->instr, nir_instr_type_jump);
   instr->type = type;
   return instr;
}

nir_load_const_instr *
nir_load_const_instr_create(nir_shader *shader, unsigned num_components,
                            unsigned bit_size)
{
   nir_load_const_instr *instr =
      rzalloc_size(shader, sizeof(*instr) + num_components * sizeof(*instr->value));
   instr_init(&instr->instr, nir_instr_type_load_const);

   nir_ssa_def_init(&instr->instr, &instr->def, num_components, bit_size, NULL);

   return instr;
}

nir_intrinsic_instr *
nir_intrinsic_instr_create(nir_shader *shader, nir_intrinsic_op op)
{
   unsigned num_srcs = nir_intrinsic_infos[op].num_srcs;
   /* TODO: don't use rzalloc */
   nir_intrinsic_instr *instr =
      rzalloc_size(shader,
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
nir_call_instr_create(nir_shader *shader, nir_function *callee)
{
   const unsigned num_params = callee->num_params;
   nir_call_instr *instr =
      rzalloc_size(shader, sizeof(*instr) +
                   num_params * sizeof(instr->params[0]));

   instr_init(&instr->instr, nir_instr_type_call);
   instr->callee = callee;
   instr->num_params = num_params;
   for (unsigned i = 0; i < num_params; i++)
      src_init(&instr->params[i]);

   return instr;
}

static int8_t default_tg4_offsets[4][2] =
{
   { 0, 1 },
   { 1, 1 },
   { 1, 0 },
   { 0, 0 },
};

nir_tex_instr *
nir_tex_instr_create(nir_shader *shader, unsigned num_srcs)
{
   nir_tex_instr *instr = rzalloc(shader, nir_tex_instr);
   instr_init(&instr->instr, nir_instr_type_tex);

   dest_init(&instr->dest);

   instr->num_srcs = num_srcs;
   instr->src = ralloc_array(instr, nir_tex_src, num_srcs);
   for (unsigned i = 0; i < num_srcs; i++)
      src_init(&instr->src[i].src);

   instr->texture_index = 0;
   instr->texture_array_size = 0;
   instr->sampler_index = 0;
   memcpy(instr->tg4_offsets, default_tg4_offsets, sizeof(instr->tg4_offsets));

   return instr;
}

void
nir_tex_instr_add_src(nir_tex_instr *tex,
                      nir_tex_src_type src_type,
                      nir_src src)
{
   nir_tex_src *new_srcs = rzalloc_array(tex, nir_tex_src,
                                         tex->num_srcs + 1);

   for (unsigned i = 0; i < tex->num_srcs; i++) {
      new_srcs[i].src_type = tex->src[i].src_type;
      nir_instr_move_src(&tex->instr, &new_srcs[i].src,
                         &tex->src[i].src);
   }

   ralloc_free(tex->src);
   tex->src = new_srcs;

   tex->src[tex->num_srcs].src_type = src_type;
   nir_instr_rewrite_src(&tex->instr, &tex->src[tex->num_srcs].src, src);
   tex->num_srcs++;
}

void
nir_tex_instr_remove_src(nir_tex_instr *tex, unsigned src_idx)
{
   assert(src_idx < tex->num_srcs);

   /* First rewrite the source to NIR_SRC_INIT */
   nir_instr_rewrite_src(&tex->instr, &tex->src[src_idx].src, NIR_SRC_INIT);

   /* Now, move all of the other sources down */
   for (unsigned i = src_idx + 1; i < tex->num_srcs; i++) {
      tex->src[i-1].src_type = tex->src[i].src_type;
      nir_instr_move_src(&tex->instr, &tex->src[i-1].src, &tex->src[i].src);
   }
   tex->num_srcs--;
}

bool
nir_tex_instr_has_explicit_tg4_offsets(nir_tex_instr *tex)
{
   if (tex->op != nir_texop_tg4)
      return false;
   return memcmp(tex->tg4_offsets, default_tg4_offsets,
                 sizeof(tex->tg4_offsets)) != 0;
}

nir_phi_instr *
nir_phi_instr_create(nir_shader *shader)
{
   nir_phi_instr *instr = ralloc(shader, nir_phi_instr);
   instr_init(&instr->instr, nir_instr_type_phi);

   dest_init(&instr->dest);
   exec_list_make_empty(&instr->srcs);
   return instr;
}

nir_parallel_copy_instr *
nir_parallel_copy_instr_create(nir_shader *shader)
{
   nir_parallel_copy_instr *instr = ralloc(shader, nir_parallel_copy_instr);
   instr_init(&instr->instr, nir_instr_type_parallel_copy);

   exec_list_make_empty(&instr->entries);

   return instr;
}

nir_ssa_undef_instr *
nir_ssa_undef_instr_create(nir_shader *shader,
                           unsigned num_components,
                           unsigned bit_size)
{
   nir_ssa_undef_instr *instr = ralloc(shader, nir_ssa_undef_instr);
   instr_init(&instr->instr, nir_instr_type_ssa_undef);

   nir_ssa_def_init(&instr->instr, &instr->def, num_components, bit_size, NULL);

   return instr;
}

static nir_const_value
const_value_float(double d, unsigned bit_size)
{
   nir_const_value v;
   memset(&v, 0, sizeof(v));
   switch (bit_size) {
   case 16: v.u16 = _mesa_float_to_half(d);  break;
   case 32: v.f32 = d;                       break;
   case 64: v.f64 = d;                       break;
   default:
      unreachable("Invalid bit size");
   }
   return v;
}

static nir_const_value
const_value_int(int64_t i, unsigned bit_size)
{
   nir_const_value v;
   memset(&v, 0, sizeof(v));
   switch (bit_size) {
   case 1:  v.b   = i & 1;  break;
   case 8:  v.i8  = i;  break;
   case 16: v.i16 = i;  break;
   case 32: v.i32 = i;  break;
   case 64: v.i64 = i;  break;
   default:
      unreachable("Invalid bit size");
   }
   return v;
}

nir_const_value
nir_alu_binop_identity(nir_op binop, unsigned bit_size)
{
   const int64_t max_int = (1ull << (bit_size - 1)) - 1;
   const int64_t min_int = -max_int - 1;
   switch (binop) {
   case nir_op_iadd:
      return const_value_int(0, bit_size);
   case nir_op_fadd:
      return const_value_float(0, bit_size);
   case nir_op_imul:
      return const_value_int(1, bit_size);
   case nir_op_fmul:
      return const_value_float(1, bit_size);
   case nir_op_imin:
      return const_value_int(max_int, bit_size);
   case nir_op_umin:
      return const_value_int(~0ull, bit_size);
   case nir_op_fmin:
      return const_value_float(INFINITY, bit_size);
   case nir_op_imax:
      return const_value_int(min_int, bit_size);
   case nir_op_umax:
      return const_value_int(0, bit_size);
   case nir_op_fmax:
      return const_value_float(-INFINITY, bit_size);
   case nir_op_iand:
      return const_value_int(~0ull, bit_size);
   case nir_op_ior:
      return const_value_int(0, bit_size);
   case nir_op_ixor:
      return const_value_int(0, bit_size);
   default:
      unreachable("Invalid reduction operation");
   }
}

nir_function_impl *
nir_cf_node_get_function(nir_cf_node *node)
{
   while (node->type != nir_cf_node_function) {
      node = node->parent;
   }

   return nir_cf_node_as_function(node);
}

/* Reduces a cursor by trying to convert everything to after and trying to
 * go up to block granularity when possible.
 */
static nir_cursor
reduce_cursor(nir_cursor cursor)
{
   switch (cursor.option) {
   case nir_cursor_before_block:
      assert(nir_cf_node_prev(&cursor.block->cf_node) == NULL ||
             nir_cf_node_prev(&cursor.block->cf_node)->type != nir_cf_node_block);
      if (exec_list_is_empty(&cursor.block->instr_list)) {
         /* Empty block.  After is as good as before. */
         cursor.option = nir_cursor_after_block;
      }
      return cursor;

   case nir_cursor_after_block:
      return cursor;

   case nir_cursor_before_instr: {
      nir_instr *prev_instr = nir_instr_prev(cursor.instr);
      if (prev_instr) {
         /* Before this instruction is after the previous */
         cursor.instr = prev_instr;
         cursor.option = nir_cursor_after_instr;
      } else {
         /* No previous instruction.  Switch to before block */
         cursor.block = cursor.instr->block;
         cursor.option = nir_cursor_before_block;
      }
      return reduce_cursor(cursor);
   }

   case nir_cursor_after_instr:
      if (nir_instr_next(cursor.instr) == NULL) {
         /* This is the last instruction, switch to after block */
         cursor.option = nir_cursor_after_block;
         cursor.block = cursor.instr->block;
      }
      return cursor;

   default:
      unreachable("Inavlid cursor option");
   }
}

bool
nir_cursors_equal(nir_cursor a, nir_cursor b)
{
   /* Reduced cursors should be unique */
   a = reduce_cursor(a);
   b = reduce_cursor(b);

   return a.block == b.block && a.option == b.option;
}

static bool
add_use_cb(nir_src *src, void *state)
{
   nir_instr *instr = state;

   src->parent_instr = instr;
   list_addtail(&src->use_link,
                src->is_ssa ? &src->ssa->uses : &src->reg.reg->uses);

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

   if (!dest->is_ssa) {
      dest->reg.parent_instr = instr;
      list_addtail(&dest->reg.def_link, &dest->reg.reg->defs);
   }

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
nir_instr_insert(nir_cursor cursor, nir_instr *instr)
{
   switch (cursor.option) {
   case nir_cursor_before_block:
      /* Only allow inserting jumps into empty blocks. */
      if (instr->type == nir_instr_type_jump)
         assert(exec_list_is_empty(&cursor.block->instr_list));

      instr->block = cursor.block;
      add_defs_uses(instr);
      exec_list_push_head(&cursor.block->instr_list, &instr->node);
      break;
   case nir_cursor_after_block: {
      /* Inserting instructions after a jump is illegal. */
      nir_instr *last = nir_block_last_instr(cursor.block);
      assert(last == NULL || last->type != nir_instr_type_jump);
      (void) last;

      instr->block = cursor.block;
      add_defs_uses(instr);
      exec_list_push_tail(&cursor.block->instr_list, &instr->node);
      break;
   }
   case nir_cursor_before_instr:
      assert(instr->type != nir_instr_type_jump);
      instr->block = cursor.instr->block;
      add_defs_uses(instr);
      exec_node_insert_node_before(&cursor.instr->node, &instr->node);
      break;
   case nir_cursor_after_instr:
      /* Inserting instructions after a jump is illegal. */
      assert(cursor.instr->type != nir_instr_type_jump);

      /* Only allow inserting jumps at the end of the block. */
      if (instr->type == nir_instr_type_jump)
         assert(cursor.instr == nir_block_last_instr(cursor.instr->block));

      instr->block = cursor.instr->block;
      add_defs_uses(instr);
      exec_node_insert_after(&cursor.instr->node, &instr->node);
      break;
   }

   if (instr->type == nir_instr_type_jump)
      nir_handle_add_jump(instr->block);
}

static bool
src_is_valid(const nir_src *src)
{
   return src->is_ssa ? (src->ssa != NULL) : (src->reg.reg != NULL);
}

static bool
remove_use_cb(nir_src *src, void *state)
{
   (void) state;

   if (src_is_valid(src))
      list_del(&src->use_link);

   return true;
}

static bool
remove_def_cb(nir_dest *dest, void *state)
{
   (void) state;

   if (!dest->is_ssa)
      list_del(&dest->reg.def_link);

   return true;
}

static void
remove_defs_uses(nir_instr *instr)
{
   nir_foreach_dest(instr, remove_def_cb, instr);
   nir_foreach_src(instr, remove_use_cb, instr);
}

void nir_instr_remove_v(nir_instr *instr)
{
   remove_defs_uses(instr);
   exec_node_remove(&instr->node);

   if (instr->type == nir_instr_type_jump) {
      nir_jump_instr *jump_instr = nir_instr_as_jump(instr);
      nir_handle_remove_jump(instr->block, jump_instr->type);
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

static bool
visit_alu_dest(nir_alu_instr *instr, nir_foreach_dest_cb cb, void *state)
{
   return cb(&instr->dest.dest, state);
}

static bool
visit_deref_dest(nir_deref_instr *instr, nir_foreach_dest_cb cb, void *state)
{
   return cb(&instr->dest, state);
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
   nir_foreach_parallel_copy_entry(entry, instr) {
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
   case nir_instr_type_deref:
      return visit_deref_dest(nir_instr_as_deref(instr), cb, state);
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
   case nir_instr_type_deref:
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
visit_alu_src(nir_alu_instr *instr, nir_foreach_src_cb cb, void *state)
{
   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++)
      if (!visit_src(&instr->src[i].src, cb, state))
         return false;

   return true;
}

static bool
visit_deref_instr_src(nir_deref_instr *instr,
                      nir_foreach_src_cb cb, void *state)
{
   if (instr->deref_type != nir_deref_type_var) {
      if (!visit_src(&instr->parent, cb, state))
         return false;
   }

   if (instr->deref_type == nir_deref_type_array ||
       instr->deref_type == nir_deref_type_ptr_as_array) {
      if (!visit_src(&instr->arr.index, cb, state))
         return false;
   }

   return true;
}

static bool
visit_tex_src(nir_tex_instr *instr, nir_foreach_src_cb cb, void *state)
{
   for (unsigned i = 0; i < instr->num_srcs; i++) {
      if (!visit_src(&instr->src[i].src, cb, state))
         return false;
   }

   return true;
}

static bool
visit_intrinsic_src(nir_intrinsic_instr *instr, nir_foreach_src_cb cb,
                    void *state)
{
   unsigned num_srcs = nir_intrinsic_infos[instr->intrinsic].num_srcs;
   for (unsigned i = 0; i < num_srcs; i++) {
      if (!visit_src(&instr->src[i], cb, state))
         return false;
   }

   return true;
}

static bool
visit_call_src(nir_call_instr *instr, nir_foreach_src_cb cb, void *state)
{
   for (unsigned i = 0; i < instr->num_params; i++) {
      if (!visit_src(&instr->params[i], cb, state))
         return false;
   }

   return true;
}

static bool
visit_phi_src(nir_phi_instr *instr, nir_foreach_src_cb cb, void *state)
{
   nir_foreach_phi_src(src, instr) {
      if (!visit_src(&src->src, cb, state))
         return false;
   }

   return true;
}

static bool
visit_parallel_copy_src(nir_parallel_copy_instr *instr,
                        nir_foreach_src_cb cb, void *state)
{
   nir_foreach_parallel_copy_entry(entry, instr) {
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
   case nir_instr_type_deref:
      if (!visit_deref_instr_src(nir_instr_as_deref(instr), cb, state))
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
      /* Constant load instructions have no regular sources */
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

int64_t
nir_src_comp_as_int(nir_src src, unsigned comp)
{
   assert(nir_src_is_const(src));
   nir_load_const_instr *load = nir_instr_as_load_const(src.ssa->parent_instr);

   assert(comp < load->def.num_components);
   switch (load->def.bit_size) {
   /* int1_t uses 0/-1 convention */
   case 1:  return -(int)load->value[comp].b;
   case 8:  return load->value[comp].i8;
   case 16: return load->value[comp].i16;
   case 32: return load->value[comp].i32;
   case 64: return load->value[comp].i64;
   default:
      unreachable("Invalid bit size");
   }
}

uint64_t
nir_src_comp_as_uint(nir_src src, unsigned comp)
{
   assert(nir_src_is_const(src));
   nir_load_const_instr *load = nir_instr_as_load_const(src.ssa->parent_instr);

   assert(comp < load->def.num_components);
   switch (load->def.bit_size) {
   case 1:  return load->value[comp].b;
   case 8:  return load->value[comp].u8;
   case 16: return load->value[comp].u16;
   case 32: return load->value[comp].u32;
   case 64: return load->value[comp].u64;
   default:
      unreachable("Invalid bit size");
   }
}

bool
nir_src_comp_as_bool(nir_src src, unsigned comp)
{
   int64_t i = nir_src_comp_as_int(src, comp);

   /* Booleans of any size use 0/-1 convention */
   assert(i == 0 || i == -1);

   return i;
}

double
nir_src_comp_as_float(nir_src src, unsigned comp)
{
   assert(nir_src_is_const(src));
   nir_load_const_instr *load = nir_instr_as_load_const(src.ssa->parent_instr);

   assert(comp < load->def.num_components);
   switch (load->def.bit_size) {
   case 16: return _mesa_half_to_float(load->value[comp].u16);
   case 32: return load->value[comp].f32;
   case 64: return load->value[comp].f64;
   default:
      unreachable("Invalid bit size");
   }
}

int64_t
nir_src_as_int(nir_src src)
{
   assert(nir_src_num_components(src) == 1);
   return nir_src_comp_as_int(src, 0);
}

uint64_t
nir_src_as_uint(nir_src src)
{
   assert(nir_src_num_components(src) == 1);
   return nir_src_comp_as_uint(src, 0);
}

bool
nir_src_as_bool(nir_src src)
{
   assert(nir_src_num_components(src) == 1);
   return nir_src_comp_as_bool(src, 0);
}

double
nir_src_as_float(nir_src src)
{
   assert(nir_src_num_components(src) == 1);
   return nir_src_comp_as_float(src, 0);
}

nir_const_value *
nir_src_as_const_value(nir_src src)
{
   if (!src.is_ssa)
      return NULL;

   if (src.ssa->parent_instr->type != nir_instr_type_load_const)
      return NULL;

   nir_load_const_instr *load = nir_instr_as_load_const(src.ssa->parent_instr);

   return load->value;
}

/**
 * Returns true if the source is known to be dynamically uniform. Otherwise it
 * returns false which means it may or may not be dynamically uniform but it
 * can't be determined.
 */
bool
nir_src_is_dynamically_uniform(nir_src src)
{
   if (!src.is_ssa)
      return false;

   /* Constants are trivially dynamically uniform */
   if (src.ssa->parent_instr->type == nir_instr_type_load_const)
      return true;

   /* As are uniform variables */
   if (src.ssa->parent_instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(src.ssa->parent_instr);

      if (intr->intrinsic == nir_intrinsic_load_uniform)
         return true;
   }

   /* XXX: this could have many more tests, such as when a sampler function is
    * called with dynamically uniform arguments.
    */
   return false;
}

static void
src_remove_all_uses(nir_src *src)
{
   for (; src; src = src->is_ssa ? NULL : src->reg.indirect) {
      if (!src_is_valid(src))
         continue;

      list_del(&src->use_link);
   }
}

static void
src_add_all_uses(nir_src *src, nir_instr *parent_instr, nir_if *parent_if)
{
   for (; src; src = src->is_ssa ? NULL : src->reg.indirect) {
      if (!src_is_valid(src))
         continue;

      if (parent_instr) {
         src->parent_instr = parent_instr;
         if (src->is_ssa)
            list_addtail(&src->use_link, &src->ssa->uses);
         else
            list_addtail(&src->use_link, &src->reg.reg->uses);
      } else {
         assert(parent_if);
         src->parent_if = parent_if;
         if (src->is_ssa)
            list_addtail(&src->use_link, &src->ssa->if_uses);
         else
            list_addtail(&src->use_link, &src->reg.reg->if_uses);
      }
   }
}

void
nir_instr_rewrite_src(nir_instr *instr, nir_src *src, nir_src new_src)
{
   assert(!src_is_valid(src) || src->parent_instr == instr);

   src_remove_all_uses(src);
   *src = new_src;
   src_add_all_uses(src, instr, NULL);
}

void
nir_instr_move_src(nir_instr *dest_instr, nir_src *dest, nir_src *src)
{
   assert(!src_is_valid(dest) || dest->parent_instr == dest_instr);

   src_remove_all_uses(dest);
   src_remove_all_uses(src);
   *dest = *src;
   *src = NIR_SRC_INIT;
   src_add_all_uses(dest, dest_instr, NULL);
}

void
nir_if_rewrite_condition(nir_if *if_stmt, nir_src new_src)
{
   nir_src *src = &if_stmt->condition;
   assert(!src_is_valid(src) || src->parent_if == if_stmt);

   src_remove_all_uses(src);
   *src = new_src;
   src_add_all_uses(src, NULL, if_stmt);
}

void
nir_instr_rewrite_dest(nir_instr *instr, nir_dest *dest, nir_dest new_dest)
{
   if (dest->is_ssa) {
      /* We can only overwrite an SSA destination if it has no uses. */
      assert(list_empty(&dest->ssa.uses) && list_empty(&dest->ssa.if_uses));
   } else {
      list_del(&dest->reg.def_link);
      if (dest->reg.indirect)
         src_remove_all_uses(dest->reg.indirect);
   }

   /* We can't re-write with an SSA def */
   assert(!new_dest.is_ssa);

   nir_dest_copy(dest, &new_dest, instr);

   dest->reg.parent_instr = instr;
   list_addtail(&dest->reg.def_link, &new_dest.reg.reg->defs);

   if (dest->reg.indirect)
      src_add_all_uses(dest->reg.indirect, instr, NULL);
}

/* note: does *not* take ownership of 'name' */
void
nir_ssa_def_init(nir_instr *instr, nir_ssa_def *def,
                 unsigned num_components,
                 unsigned bit_size, const char *name)
{
   def->name = ralloc_strdup(instr, name);
   def->parent_instr = instr;
   list_inithead(&def->uses);
   list_inithead(&def->if_uses);
   def->num_components = num_components;
   def->bit_size = bit_size;

   if (instr->block) {
      nir_function_impl *impl =
         nir_cf_node_get_function(&instr->block->cf_node);

      def->index = impl->ssa_alloc++;
   } else {
      def->index = UINT_MAX;
   }
}

/* note: does *not* take ownership of 'name' */
void
nir_ssa_dest_init(nir_instr *instr, nir_dest *dest,
                 unsigned num_components, unsigned bit_size,
                 const char *name)
{
   dest->is_ssa = true;
   nir_ssa_def_init(instr, &dest->ssa, num_components, bit_size, name);
}

void
nir_ssa_def_rewrite_uses(nir_ssa_def *def, nir_src new_src)
{
   assert(!new_src.is_ssa || def != new_src.ssa);

   nir_foreach_use_safe(use_src, def)
      nir_instr_rewrite_src(use_src->parent_instr, use_src, new_src);

   nir_foreach_if_use_safe(use_src, def)
      nir_if_rewrite_condition(use_src->parent_if, new_src);
}

static bool
is_instr_between(nir_instr *start, nir_instr *end, nir_instr *between)
{
   assert(start->block == end->block);

   if (between->block != start->block)
      return false;

   /* Search backwards looking for "between" */
   while (start != end) {
      if (between == end)
         return true;

      end = nir_instr_prev(end);
      assert(end);
   }

   return false;
}

/* Replaces all uses of the given SSA def with the given source but only if
 * the use comes after the after_me instruction.  This can be useful if you
 * are emitting code to fix up the result of some instruction: you can freely
 * use the result in that code and then call rewrite_uses_after and pass the
 * last fixup instruction as after_me and it will replace all of the uses you
 * want without touching the fixup code.
 *
 * This function assumes that after_me is in the same block as
 * def->parent_instr and that after_me comes after def->parent_instr.
 */
void
nir_ssa_def_rewrite_uses_after(nir_ssa_def *def, nir_src new_src,
                               nir_instr *after_me)
{
   if (new_src.is_ssa && def == new_src.ssa)
      return;

   nir_foreach_use_safe(use_src, def) {
      assert(use_src->parent_instr != def->parent_instr);
      /* Since def already dominates all of its uses, the only way a use can
       * not be dominated by after_me is if it is between def and after_me in
       * the instruction list.
       */
      if (!is_instr_between(def->parent_instr, after_me, use_src->parent_instr))
         nir_instr_rewrite_src(use_src->parent_instr, use_src, new_src);
   }

   nir_foreach_if_use_safe(use_src, def)
      nir_if_rewrite_condition(use_src->parent_if, new_src);
}

nir_component_mask_t
nir_ssa_def_components_read(const nir_ssa_def *def)
{
   nir_component_mask_t read_mask = 0;
   nir_foreach_use(use, def) {
      if (use->parent_instr->type == nir_instr_type_alu) {
         nir_alu_instr *alu = nir_instr_as_alu(use->parent_instr);
         nir_alu_src *alu_src = exec_node_data(nir_alu_src, use, src);
         int src_idx = alu_src - &alu->src[0];
         assert(src_idx >= 0 && src_idx < nir_op_infos[alu->op].num_inputs);
         read_mask |= nir_alu_instr_src_read_mask(alu, src_idx);
      } else {
         return (1 << def->num_components) - 1;
      }
   }

   if (!list_empty(&def->if_uses))
      read_mask |= 1;

   return read_mask;
}

nir_block *
nir_block_cf_tree_next(nir_block *block)
{
   if (block == NULL) {
      /* nir_foreach_block_safe() will call this function on a NULL block
       * after the last iteration, but it won't use the result so just return
       * NULL here.
       */
      return NULL;
   }

   nir_cf_node *cf_next = nir_cf_node_next(&block->cf_node);
   if (cf_next)
      return nir_cf_node_cf_tree_first(cf_next);

   nir_cf_node *parent = block->cf_node.parent;

   switch (parent->type) {
   case nir_cf_node_if: {
      /* Are we at the end of the if? Go to the beginning of the else */
      nir_if *if_stmt = nir_cf_node_as_if(parent);
      if (block == nir_if_last_then_block(if_stmt))
         return nir_if_first_else_block(if_stmt);

      assert(block == nir_if_last_else_block(if_stmt));
      /* fall through */
   }

   case nir_cf_node_loop:
      return nir_cf_node_as_block(nir_cf_node_next(parent));

   case nir_cf_node_function:
      return NULL;

   default:
      unreachable("unknown cf node type");
   }
}

nir_block *
nir_block_cf_tree_prev(nir_block *block)
{
   if (block == NULL) {
      /* do this for consistency with nir_block_cf_tree_next() */
      return NULL;
   }

   nir_cf_node *cf_prev = nir_cf_node_prev(&block->cf_node);
   if (cf_prev)
      return nir_cf_node_cf_tree_last(cf_prev);

   nir_cf_node *parent = block->cf_node.parent;

   switch (parent->type) {
   case nir_cf_node_if: {
      /* Are we at the beginning of the else? Go to the end of the if */
      nir_if *if_stmt = nir_cf_node_as_if(parent);
      if (block == nir_if_first_else_block(if_stmt))
         return nir_if_last_then_block(if_stmt);

      assert(block == nir_if_first_then_block(if_stmt));
      /* fall through */
   }

   case nir_cf_node_loop:
      return nir_cf_node_as_block(nir_cf_node_prev(parent));

   case nir_cf_node_function:
      return NULL;

   default:
      unreachable("unknown cf node type");
   }
}

nir_block *nir_cf_node_cf_tree_first(nir_cf_node *node)
{
   switch (node->type) {
   case nir_cf_node_function: {
      nir_function_impl *impl = nir_cf_node_as_function(node);
      return nir_start_block(impl);
   }

   case nir_cf_node_if: {
      nir_if *if_stmt = nir_cf_node_as_if(node);
      return nir_if_first_then_block(if_stmt);
   }

   case nir_cf_node_loop: {
      nir_loop *loop = nir_cf_node_as_loop(node);
      return nir_loop_first_block(loop);
   }

   case nir_cf_node_block: {
      return nir_cf_node_as_block(node);
   }

   default:
      unreachable("unknown node type");
   }
}

nir_block *nir_cf_node_cf_tree_last(nir_cf_node *node)
{
   switch (node->type) {
   case nir_cf_node_function: {
      nir_function_impl *impl = nir_cf_node_as_function(node);
      return nir_impl_last_block(impl);
   }

   case nir_cf_node_if: {
      nir_if *if_stmt = nir_cf_node_as_if(node);
      return nir_if_last_else_block(if_stmt);
   }

   case nir_cf_node_loop: {
      nir_loop *loop = nir_cf_node_as_loop(node);
      return nir_loop_last_block(loop);
   }

   case nir_cf_node_block: {
      return nir_cf_node_as_block(node);
   }

   default:
      unreachable("unknown node type");
   }
}

nir_block *nir_cf_node_cf_tree_next(nir_cf_node *node)
{
   if (node->type == nir_cf_node_block)
      return nir_block_cf_tree_next(nir_cf_node_as_block(node));
   else if (node->type == nir_cf_node_function)
      return NULL;
   else
      return nir_cf_node_as_block(nir_cf_node_next(node));
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

nir_loop *
nir_block_get_following_loop(nir_block *block)
{
   if (exec_node_is_tail_sentinel(&block->cf_node.node))
      return NULL;

   if (nir_cf_node_is_last(&block->cf_node))
      return NULL;

   nir_cf_node *next_node = nir_cf_node_next(&block->cf_node);

   if (next_node->type != nir_cf_node_loop)
      return NULL;

   return nir_cf_node_as_loop(next_node);
}

void
nir_index_blocks(nir_function_impl *impl)
{
   unsigned index = 0;

   if (impl->valid_metadata & nir_metadata_block_index)
      return;

   nir_foreach_block(block, impl) {
      block->index = index++;
   }

   /* The end_block isn't really part of the program, which is why its index
    * is >= num_blocks.
    */
   impl->num_blocks = impl->end_block->index = index;
}

static bool
index_ssa_def_cb(nir_ssa_def *def, void *state)
{
   unsigned *index = (unsigned *) state;
   def->index = (*index)++;

   return true;
}

/**
 * The indices are applied top-to-bottom which has the very nice property
 * that, if A dominates B, then A->index <= B->index.
 */
void
nir_index_ssa_defs(nir_function_impl *impl)
{
   unsigned index = 0;

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block)
         nir_foreach_ssa_def(instr, index_ssa_def_cb, &index);
   }

   impl->ssa_alloc = index;
}

/**
 * The indices are applied top-to-bottom which has the very nice property
 * that, if A dominates B, then A->index <= B->index.
 */
unsigned
nir_index_instrs(nir_function_impl *impl)
{
   unsigned index = 0;

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block)
         instr->index = index++;
   }

   return index;
}

nir_intrinsic_op
nir_intrinsic_from_system_value(gl_system_value val)
{
   switch (val) {
   case SYSTEM_VALUE_VERTEX_ID:
      return nir_intrinsic_load_vertex_id;
   case SYSTEM_VALUE_INSTANCE_ID:
      return nir_intrinsic_load_instance_id;
   case SYSTEM_VALUE_DRAW_ID:
      return nir_intrinsic_load_draw_id;
   case SYSTEM_VALUE_BASE_INSTANCE:
      return nir_intrinsic_load_base_instance;
   case SYSTEM_VALUE_VERTEX_ID_ZERO_BASE:
      return nir_intrinsic_load_vertex_id_zero_base;
   case SYSTEM_VALUE_IS_INDEXED_DRAW:
      return nir_intrinsic_load_is_indexed_draw;
   case SYSTEM_VALUE_FIRST_VERTEX:
      return nir_intrinsic_load_first_vertex;
   case SYSTEM_VALUE_BASE_VERTEX:
      return nir_intrinsic_load_base_vertex;
   case SYSTEM_VALUE_INVOCATION_ID:
      return nir_intrinsic_load_invocation_id;
   case SYSTEM_VALUE_FRAG_COORD:
      return nir_intrinsic_load_frag_coord;
   case SYSTEM_VALUE_FRONT_FACE:
      return nir_intrinsic_load_front_face;
   case SYSTEM_VALUE_SAMPLE_ID:
      return nir_intrinsic_load_sample_id;
   case SYSTEM_VALUE_SAMPLE_POS:
      return nir_intrinsic_load_sample_pos;
   case SYSTEM_VALUE_SAMPLE_MASK_IN:
      return nir_intrinsic_load_sample_mask_in;
   case SYSTEM_VALUE_LOCAL_INVOCATION_ID:
      return nir_intrinsic_load_local_invocation_id;
   case SYSTEM_VALUE_LOCAL_INVOCATION_INDEX:
      return nir_intrinsic_load_local_invocation_index;
   case SYSTEM_VALUE_WORK_GROUP_ID:
      return nir_intrinsic_load_work_group_id;
   case SYSTEM_VALUE_NUM_WORK_GROUPS:
      return nir_intrinsic_load_num_work_groups;
   case SYSTEM_VALUE_PRIMITIVE_ID:
      return nir_intrinsic_load_primitive_id;
   case SYSTEM_VALUE_TESS_COORD:
      return nir_intrinsic_load_tess_coord;
   case SYSTEM_VALUE_TESS_LEVEL_OUTER:
      return nir_intrinsic_load_tess_level_outer;
   case SYSTEM_VALUE_TESS_LEVEL_INNER:
      return nir_intrinsic_load_tess_level_inner;
   case SYSTEM_VALUE_VERTICES_IN:
      return nir_intrinsic_load_patch_vertices_in;
   case SYSTEM_VALUE_HELPER_INVOCATION:
      return nir_intrinsic_load_helper_invocation;
   case SYSTEM_VALUE_VIEW_INDEX:
      return nir_intrinsic_load_view_index;
   case SYSTEM_VALUE_SUBGROUP_SIZE:
      return nir_intrinsic_load_subgroup_size;
   case SYSTEM_VALUE_SUBGROUP_INVOCATION:
      return nir_intrinsic_load_subgroup_invocation;
   case SYSTEM_VALUE_SUBGROUP_EQ_MASK:
      return nir_intrinsic_load_subgroup_eq_mask;
   case SYSTEM_VALUE_SUBGROUP_GE_MASK:
      return nir_intrinsic_load_subgroup_ge_mask;
   case SYSTEM_VALUE_SUBGROUP_GT_MASK:
      return nir_intrinsic_load_subgroup_gt_mask;
   case SYSTEM_VALUE_SUBGROUP_LE_MASK:
      return nir_intrinsic_load_subgroup_le_mask;
   case SYSTEM_VALUE_SUBGROUP_LT_MASK:
      return nir_intrinsic_load_subgroup_lt_mask;
   case SYSTEM_VALUE_NUM_SUBGROUPS:
      return nir_intrinsic_load_num_subgroups;
   case SYSTEM_VALUE_SUBGROUP_ID:
      return nir_intrinsic_load_subgroup_id;
   case SYSTEM_VALUE_LOCAL_GROUP_SIZE:
      return nir_intrinsic_load_local_group_size;
   case SYSTEM_VALUE_GLOBAL_INVOCATION_ID:
      return nir_intrinsic_load_global_invocation_id;
   case SYSTEM_VALUE_GLOBAL_INVOCATION_INDEX:
      return nir_intrinsic_load_global_invocation_index;
   case SYSTEM_VALUE_WORK_DIM:
      return nir_intrinsic_load_work_dim;
   default:
      unreachable("system value does not directly correspond to intrinsic");
   }
}

gl_system_value
nir_system_value_from_intrinsic(nir_intrinsic_op intrin)
{
   switch (intrin) {
   case nir_intrinsic_load_vertex_id:
      return SYSTEM_VALUE_VERTEX_ID;
   case nir_intrinsic_load_instance_id:
      return SYSTEM_VALUE_INSTANCE_ID;
   case nir_intrinsic_load_draw_id:
      return SYSTEM_VALUE_DRAW_ID;
   case nir_intrinsic_load_base_instance:
      return SYSTEM_VALUE_BASE_INSTANCE;
   case nir_intrinsic_load_vertex_id_zero_base:
      return SYSTEM_VALUE_VERTEX_ID_ZERO_BASE;
   case nir_intrinsic_load_first_vertex:
      return SYSTEM_VALUE_FIRST_VERTEX;
   case nir_intrinsic_load_is_indexed_draw:
      return SYSTEM_VALUE_IS_INDEXED_DRAW;
   case nir_intrinsic_load_base_vertex:
      return SYSTEM_VALUE_BASE_VERTEX;
   case nir_intrinsic_load_invocation_id:
      return SYSTEM_VALUE_INVOCATION_ID;
   case nir_intrinsic_load_frag_coord:
      return SYSTEM_VALUE_FRAG_COORD;
   case nir_intrinsic_load_front_face:
      return SYSTEM_VALUE_FRONT_FACE;
   case nir_intrinsic_load_sample_id:
      return SYSTEM_VALUE_SAMPLE_ID;
   case nir_intrinsic_load_sample_pos:
      return SYSTEM_VALUE_SAMPLE_POS;
   case nir_intrinsic_load_sample_mask_in:
      return SYSTEM_VALUE_SAMPLE_MASK_IN;
   case nir_intrinsic_load_local_invocation_id:
      return SYSTEM_VALUE_LOCAL_INVOCATION_ID;
   case nir_intrinsic_load_local_invocation_index:
      return SYSTEM_VALUE_LOCAL_INVOCATION_INDEX;
   case nir_intrinsic_load_num_work_groups:
      return SYSTEM_VALUE_NUM_WORK_GROUPS;
   case nir_intrinsic_load_work_group_id:
      return SYSTEM_VALUE_WORK_GROUP_ID;
   case nir_intrinsic_load_primitive_id:
      return SYSTEM_VALUE_PRIMITIVE_ID;
   case nir_intrinsic_load_tess_coord:
      return SYSTEM_VALUE_TESS_COORD;
   case nir_intrinsic_load_tess_level_outer:
      return SYSTEM_VALUE_TESS_LEVEL_OUTER;
   case nir_intrinsic_load_tess_level_inner:
      return SYSTEM_VALUE_TESS_LEVEL_INNER;
   case nir_intrinsic_load_patch_vertices_in:
      return SYSTEM_VALUE_VERTICES_IN;
   case nir_intrinsic_load_helper_invocation:
      return SYSTEM_VALUE_HELPER_INVOCATION;
   case nir_intrinsic_load_view_index:
      return SYSTEM_VALUE_VIEW_INDEX;
   case nir_intrinsic_load_subgroup_size:
      return SYSTEM_VALUE_SUBGROUP_SIZE;
   case nir_intrinsic_load_subgroup_invocation:
      return SYSTEM_VALUE_SUBGROUP_INVOCATION;
   case nir_intrinsic_load_subgroup_eq_mask:
      return SYSTEM_VALUE_SUBGROUP_EQ_MASK;
   case nir_intrinsic_load_subgroup_ge_mask:
      return SYSTEM_VALUE_SUBGROUP_GE_MASK;
   case nir_intrinsic_load_subgroup_gt_mask:
      return SYSTEM_VALUE_SUBGROUP_GT_MASK;
   case nir_intrinsic_load_subgroup_le_mask:
      return SYSTEM_VALUE_SUBGROUP_LE_MASK;
   case nir_intrinsic_load_subgroup_lt_mask:
      return SYSTEM_VALUE_SUBGROUP_LT_MASK;
   case nir_intrinsic_load_num_subgroups:
      return SYSTEM_VALUE_NUM_SUBGROUPS;
   case nir_intrinsic_load_subgroup_id:
      return SYSTEM_VALUE_SUBGROUP_ID;
   case nir_intrinsic_load_local_group_size:
      return SYSTEM_VALUE_LOCAL_GROUP_SIZE;
   case nir_intrinsic_load_global_invocation_id:
      return SYSTEM_VALUE_GLOBAL_INVOCATION_ID;
   default:
      unreachable("intrinsic doesn't produce a system value");
   }
}

/* OpenGL utility method that remaps the location attributes if they are
 * doubles. Not needed for vulkan due the differences on the input location
 * count for doubles on vulkan vs OpenGL
 *
 * The bitfield returned in dual_slot is one bit for each double input slot in
 * the original OpenGL single-slot input numbering.  The mapping from old
 * locations to new locations is as follows:
 *
 *    new_loc = loc + util_bitcount(dual_slot & BITFIELD64_MASK(loc))
 */
void
nir_remap_dual_slot_attributes(nir_shader *shader, uint64_t *dual_slot)
{
   assert(shader->info.stage == MESA_SHADER_VERTEX);

   *dual_slot = 0;
   nir_foreach_variable(var, &shader->inputs) {
      if (glsl_type_is_dual_slot(glsl_without_array(var->type))) {
         unsigned slots = glsl_count_attribute_slots(var->type, true);
         *dual_slot |= BITFIELD64_MASK(slots) << var->data.location;
      }
   }

   nir_foreach_variable(var, &shader->inputs) {
      var->data.location +=
         util_bitcount64(*dual_slot & BITFIELD64_MASK(var->data.location));
   }
}

/* Returns an attribute mask that has been re-compacted using the given
 * dual_slot mask.
 */
uint64_t
nir_get_single_slot_attribs_mask(uint64_t attribs, uint64_t dual_slot)
{
   while (dual_slot) {
      unsigned loc = u_bit_scan64(&dual_slot);
      /* mask of all bits up to and including loc */
      uint64_t mask = BITFIELD64_MASK(loc + 1);
      attribs = (attribs & mask) | ((attribs & ~mask) >> 1);
   }
   return attribs;
}

void
nir_rewrite_image_intrinsic(nir_intrinsic_instr *intrin, nir_ssa_def *src,
                            bool bindless)
{
   switch (intrin->intrinsic) {
#define CASE(op) \
   case nir_intrinsic_image_deref_##op: \
      intrin->intrinsic = bindless ? nir_intrinsic_bindless_image_##op \
                                   : nir_intrinsic_image_##op; \
      break;
   CASE(load)
   CASE(store)
   CASE(atomic_add)
   CASE(atomic_min)
   CASE(atomic_max)
   CASE(atomic_and)
   CASE(atomic_or)
   CASE(atomic_xor)
   CASE(atomic_exchange)
   CASE(atomic_comp_swap)
   CASE(atomic_fadd)
   CASE(size)
   CASE(samples)
   CASE(load_raw_intel)
   CASE(store_raw_intel)
#undef CASE
   default:
      unreachable("Unhanded image intrinsic");
   }

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   nir_variable *var = nir_deref_instr_get_variable(deref);

   nir_intrinsic_set_image_dim(intrin, glsl_get_sampler_dim(deref->type));
   nir_intrinsic_set_image_array(intrin, glsl_sampler_type_is_array(deref->type));
   nir_intrinsic_set_access(intrin, var->data.image.access);
   nir_intrinsic_set_format(intrin, var->data.image.format);

   nir_instr_rewrite_src(&intrin->instr, &intrin->src[0],
                         nir_src_for_ssa(src));
}
