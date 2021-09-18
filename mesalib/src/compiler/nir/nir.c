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
#include "nir_builder.h"
#include "nir_control_flow_private.h"
#include "nir_worklist.h"
#include "util/half_float.h"
#include <limits.h>
#include <assert.h>
#include <math.h>
#include "util/u_math.h"
#include "util/u_qsort.h"

#include "main/menums.h" /* BITFIELD64_MASK */


/** Return true if the component mask "mask" with bit size "old_bit_size" can
 * be re-interpreted to be used with "new_bit_size".
 */
bool
nir_component_mask_can_reinterpret(nir_component_mask_t mask,
                                   unsigned old_bit_size,
                                   unsigned new_bit_size)
{
   assert(util_is_power_of_two_nonzero(old_bit_size));
   assert(util_is_power_of_two_nonzero(new_bit_size));

   if (old_bit_size == new_bit_size)
      return true;

   if (old_bit_size == 1 || new_bit_size == 1)
      return false;

   if (old_bit_size > new_bit_size) {
      unsigned ratio = old_bit_size / new_bit_size;
      return util_last_bit(mask) * ratio <= NIR_MAX_VEC_COMPONENTS;
   }

   unsigned iter = mask;
   while (iter) {
      int start, count;
      u_bit_scan_consecutive_range(&iter, &start, &count);
      start *= old_bit_size;
      count *= old_bit_size;
      if (start % new_bit_size != 0)
         return false;
      if (count % new_bit_size != 0)
         return false;
   }
   return true;
}

/** Re-interprets a component mask "mask" with bit size "old_bit_size" so that
 * it can be used can be used with "new_bit_size".
 */
nir_component_mask_t
nir_component_mask_reinterpret(nir_component_mask_t mask,
                               unsigned old_bit_size,
                               unsigned new_bit_size)
{
   assert(nir_component_mask_can_reinterpret(mask, old_bit_size, new_bit_size));

   if (old_bit_size == new_bit_size)
      return mask;

   nir_component_mask_t new_mask = 0;
   unsigned iter = mask;
   while (iter) {
      int start, count;
      u_bit_scan_consecutive_range(&iter, &start, &count);
      start = start * old_bit_size / new_bit_size;
      count = count * old_bit_size / new_bit_size;
      new_mask |= BITFIELD_RANGE(start, count);
   }
   return new_mask;
}

static void
nir_shader_destructor(void *ptr)
{
   nir_shader *shader = ptr;

   /* Free all instrs from the shader, since they're not ralloced. */
   list_for_each_entry_safe(nir_instr, instr, &shader->gc_list, gc_node) {
      nir_instr_free(instr);
   }
}

nir_shader *
nir_shader_create(void *mem_ctx,
                  gl_shader_stage stage,
                  const nir_shader_compiler_options *options,
                  shader_info *si)
{
   nir_shader *shader = rzalloc(mem_ctx, nir_shader);
   ralloc_set_destructor(shader, nir_shader_destructor);

   exec_list_make_empty(&shader->variables);

   shader->options = options;

   if (si) {
      assert(si->stage == stage);
      shader->info = *si;
   } else {
      shader->info.stage = stage;
   }

   exec_list_make_empty(&shader->functions);

   list_inithead(&shader->gc_list);

   shader->num_inputs = 0;
   shader->num_outputs = 0;
   shader->num_uniforms = 0;

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
   case nir_var_function_temp:
      assert(!"nir_shader_add_variable cannot be used for local variables");
      return;

   case nir_var_shader_temp:
   case nir_var_shader_in:
   case nir_var_shader_out:
   case nir_var_uniform:
   case nir_var_mem_ubo:
   case nir_var_mem_ssbo:
   case nir_var_mem_shared:
   case nir_var_system_value:
   case nir_var_mem_push_const:
   case nir_var_mem_constant:
   case nir_var_shader_call_data:
   case nir_var_ray_hit_attrib:
      break;

   case nir_var_mem_global:
      assert(!"nir_shader_add_variable cannot be used for global memory");
      return;

   default:
      assert(!"invalid mode");
      return;
   }

   exec_list_push_tail(&shader->variables, &var->node);
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
        shader->info.stage != MESA_SHADER_VERTEX &&
        shader->info.stage != MESA_SHADER_KERNEL) ||
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

nir_variable *
nir_find_variable_with_location(nir_shader *shader,
                                nir_variable_mode mode,
                                unsigned location)
{
   assert(util_bitcount(mode) == 1 && mode != nir_var_function_temp);
   nir_foreach_variable_with_modes(var, shader, mode) {
      if (var->data.location == location)
         return var;
   }
   return NULL;
}

nir_variable *
nir_find_variable_with_driver_location(nir_shader *shader,
                                       nir_variable_mode mode,
                                       unsigned location)
{
   assert(util_bitcount(mode) == 1 && mode != nir_var_function_temp);
   nir_foreach_variable_with_modes(var, shader, mode) {
      if (var->data.driver_location == location)
         return var;
   }
   return NULL;
}

/* Annoyingly, qsort_r is not in the C standard library and, in particular, we
 * can't count on it on MSV and Android.  So we stuff the CMP function into
 * each array element.  It's a bit messy and burns more memory but the list of
 * variables should hever be all that long.
 */
struct var_cmp {
   nir_variable *var;
   int (*cmp)(const nir_variable *, const nir_variable *);
};

static int
var_sort_cmp(const void *_a, const void *_b, void *_cmp)
{
   const struct var_cmp *a = _a;
   const struct var_cmp *b = _b;
   assert(a->cmp == b->cmp);
   return a->cmp(a->var, b->var);
}

void
nir_sort_variables_with_modes(nir_shader *shader,
                              int (*cmp)(const nir_variable *,
                                         const nir_variable *),
                              nir_variable_mode modes)
{
   unsigned num_vars = 0;
   nir_foreach_variable_with_modes(var, shader, modes) {
      ++num_vars;
   }
   struct var_cmp *vars = ralloc_array(shader, struct var_cmp, num_vars);
   unsigned i = 0;
   nir_foreach_variable_with_modes_safe(var, shader, modes) {
      exec_node_remove(&var->node);
      vars[i++] = (struct var_cmp){
         .var = var,
         .cmp = cmp,
      };
   }
   assert(i == num_vars);

   util_qsort_r(vars, num_vars, sizeof(*vars), var_sort_cmp, cmp);

   for (i = 0; i < num_vars; i++)
      exec_list_push_tail(&shader->variables, &vars[i].var->node);

   ralloc_free(vars);
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

static bool src_has_indirect(nir_src *src)
{
   return !src->is_ssa && src->reg.indirect;
}

static void src_free_indirects(nir_src *src)
{
   if (src_has_indirect(src)) {
      assert(src->reg.indirect->is_ssa || !src->reg.indirect->reg.indirect);
      free(src->reg.indirect);
      src->reg.indirect = NULL;
   }
}

static void dest_free_indirects(nir_dest *dest)
{
   if (!dest->is_ssa && dest->reg.indirect) {
      assert(dest->reg.indirect->is_ssa || !dest->reg.indirect->reg.indirect);
      free(dest->reg.indirect);
      dest->reg.indirect = NULL;
   }
}

/* NOTE: if the instruction you are copying a src to is already added
 * to the IR, use nir_instr_rewrite_src() instead.
 */
void nir_src_copy(nir_src *dest, const nir_src *src)
{
   src_free_indirects(dest);

   dest->is_ssa = src->is_ssa;
   if (src->is_ssa) {
      dest->ssa = src->ssa;
   } else {
      dest->reg.base_offset = src->reg.base_offset;
      dest->reg.reg = src->reg.reg;
      if (src->reg.indirect) {
         dest->reg.indirect = malloc(sizeof(nir_src));
         nir_src_copy(dest->reg.indirect, src->reg.indirect);
      } else {
         dest->reg.indirect = NULL;
      }
   }
}

void nir_dest_copy(nir_dest *dest, const nir_dest *src)
{
   /* Copying an SSA definition makes no sense whatsoever. */
   assert(!src->is_ssa);

   dest_free_indirects(dest);

   dest->is_ssa = false;

   dest->reg.base_offset = src->reg.base_offset;
   dest->reg.reg = src->reg.reg;
   if (src->reg.indirect) {
      dest->reg.indirect = malloc(sizeof(nir_src));
      nir_src_copy(dest->reg.indirect, src->reg.indirect);
   } else {
      dest->reg.indirect = NULL;
   }
}

void
nir_alu_src_copy(nir_alu_src *dest, const nir_alu_src *src)
{
   nir_src_copy(&dest->src, &src->src);
   dest->abs = src->abs;
   dest->negate = src->negate;
   for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; i++)
      dest->swizzle[i] = src->swizzle[i];
}

void
nir_alu_dest_copy(nir_alu_dest *dest, const nir_alu_dest *src)
{
   nir_dest_copy(&dest->dest, &src->dest);
   dest->write_mask = src->write_mask;
   dest->saturate = src->saturate;
}

bool
nir_alu_src_is_trivial_ssa(const nir_alu_instr *alu, unsigned srcn)
{
   static uint8_t trivial_swizzle[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
   STATIC_ASSERT(ARRAY_SIZE(trivial_swizzle) == NIR_MAX_VEC_COMPONENTS);

   const nir_alu_src *src = &alu->src[srcn];
   unsigned num_components = nir_ssa_alu_instr_src_components(alu, srcn);

   return src->src.is_ssa && (src->src.ssa->num_components == num_components) &&
          !src->abs && !src->negate &&
          (memcmp(src->swizzle, trivial_swizzle, num_components) == 0);
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
   impl->num_blocks = 0;
   impl->valid_metadata = nir_metadata_none;
   impl->structured = true;

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
   /* Assume that loops are divergent until proven otherwise */
   loop->divergent = true;

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
   /* TODO: don't use calloc */
   nir_alu_instr *instr = calloc(1, sizeof(nir_alu_instr) + num_srcs * sizeof(nir_alu_src));

   instr_init(&instr->instr, nir_instr_type_alu);
   instr->op = op;
   alu_dest_init(&instr->dest);
   for (unsigned i = 0; i < num_srcs; i++)
      alu_src_init(&instr->src[i]);

   list_add(&instr->instr.gc_node, &shader->gc_list);

   return instr;
}

nir_deref_instr *
nir_deref_instr_create(nir_shader *shader, nir_deref_type deref_type)
{
   nir_deref_instr *instr = calloc(1, sizeof(*instr));

   instr_init(&instr->instr, nir_instr_type_deref);

   instr->deref_type = deref_type;
   if (deref_type != nir_deref_type_var)
      src_init(&instr->parent);

   if (deref_type == nir_deref_type_array ||
       deref_type == nir_deref_type_ptr_as_array)
      src_init(&instr->arr.index);

   dest_init(&instr->dest);

   list_add(&instr->instr.gc_node, &shader->gc_list);

   return instr;
}

nir_jump_instr *
nir_jump_instr_create(nir_shader *shader, nir_jump_type type)
{
   nir_jump_instr *instr = malloc(sizeof(*instr));
   instr_init(&instr->instr, nir_instr_type_jump);
   src_init(&instr->condition);
   instr->type = type;
   instr->target = NULL;
   instr->else_target = NULL;

   list_add(&instr->instr.gc_node, &shader->gc_list);

   return instr;
}

nir_load_const_instr *
nir_load_const_instr_create(nir_shader *shader, unsigned num_components,
                            unsigned bit_size)
{
   nir_load_const_instr *instr =
      calloc(1, sizeof(*instr) + num_components * sizeof(*instr->value));
   instr_init(&instr->instr, nir_instr_type_load_const);

   nir_ssa_def_init(&instr->instr, &instr->def, num_components, bit_size);

   list_add(&instr->instr.gc_node, &shader->gc_list);

   return instr;
}

nir_intrinsic_instr *
nir_intrinsic_instr_create(nir_shader *shader, nir_intrinsic_op op)
{
   unsigned num_srcs = nir_intrinsic_infos[op].num_srcs;
   /* TODO: don't use calloc */
   nir_intrinsic_instr *instr =
      calloc(1, sizeof(nir_intrinsic_instr) + num_srcs * sizeof(nir_src));

   instr_init(&instr->instr, nir_instr_type_intrinsic);
   instr->intrinsic = op;

   if (nir_intrinsic_infos[op].has_dest)
      dest_init(&instr->dest);

   for (unsigned i = 0; i < num_srcs; i++)
      src_init(&instr->src[i]);

   list_add(&instr->instr.gc_node, &shader->gc_list);

   return instr;
}

nir_call_instr *
nir_call_instr_create(nir_shader *shader, nir_function *callee)
{
   const unsigned num_params = callee->num_params;
   nir_call_instr *instr =
      calloc(1, sizeof(*instr) + num_params * sizeof(instr->params[0]));

   instr_init(&instr->instr, nir_instr_type_call);
   instr->callee = callee;
   instr->num_params = num_params;
   for (unsigned i = 0; i < num_params; i++)
      src_init(&instr->params[i]);

   list_add(&instr->instr.gc_node, &shader->gc_list);

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
   nir_tex_instr *instr = calloc(1, sizeof(*instr));
   instr_init(&instr->instr, nir_instr_type_tex);

   dest_init(&instr->dest);

   instr->num_srcs = num_srcs;
   instr->src = malloc(sizeof(nir_tex_src) * num_srcs);
   for (unsigned i = 0; i < num_srcs; i++)
      src_init(&instr->src[i].src);

   instr->texture_index = 0;
   instr->sampler_index = 0;
   memcpy(instr->tg4_offsets, default_tg4_offsets, sizeof(instr->tg4_offsets));

   list_add(&instr->instr.gc_node, &shader->gc_list);

   return instr;
}

void
nir_tex_instr_add_src(nir_tex_instr *tex,
                      nir_tex_src_type src_type,
                      nir_src src)
{
   nir_tex_src *new_srcs = calloc(sizeof(*new_srcs),
                                         tex->num_srcs + 1);

   for (unsigned i = 0; i < tex->num_srcs; i++) {
      new_srcs[i].src_type = tex->src[i].src_type;
      nir_instr_move_src(&tex->instr, &new_srcs[i].src,
                         &tex->src[i].src);
   }

   free(tex->src);
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
   nir_phi_instr *instr = malloc(sizeof(*instr));
   instr_init(&instr->instr, nir_instr_type_phi);

   dest_init(&instr->dest);
   exec_list_make_empty(&instr->srcs);

   list_add(&instr->instr.gc_node, &shader->gc_list);

   return instr;
}

/**
 * Adds a new source to a NIR instruction.
 *
 * Note that this does not update the def/use relationship for src, assuming
 * that the instr is not in the shader.  If it is, you have to do:
 *
 * list_addtail(&phi_src->src.use_link, &src.ssa->uses);
 */
nir_phi_src *
nir_phi_instr_add_src(nir_phi_instr *instr, nir_block *pred, nir_src src)
{
   nir_phi_src *phi_src;

   phi_src = calloc(1, sizeof(nir_phi_src));
   phi_src->pred = pred;
   phi_src->src = src;
   phi_src->src.parent_instr = &instr->instr;
   exec_list_push_tail(&instr->srcs, &phi_src->node);

   return phi_src;
}

nir_parallel_copy_instr *
nir_parallel_copy_instr_create(nir_shader *shader)
{
   nir_parallel_copy_instr *instr = malloc(sizeof(*instr));
   instr_init(&instr->instr, nir_instr_type_parallel_copy);

   exec_list_make_empty(&instr->entries);

   list_add(&instr->instr.gc_node, &shader->gc_list);

   return instr;
}

nir_ssa_undef_instr *
nir_ssa_undef_instr_create(nir_shader *shader,
                           unsigned num_components,
                           unsigned bit_size)
{
   nir_ssa_undef_instr *instr = malloc(sizeof(*instr));
   instr_init(&instr->instr, nir_instr_type_ssa_undef);

   nir_ssa_def_init(&instr->instr, &instr->def, num_components, bit_size);

   list_add(&instr->instr.gc_node, &shader->gc_list);

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

      impl->valid_metadata &= ~nir_metadata_live_ssa_defs;
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

   nir_function_impl *impl = nir_cf_node_get_function(&instr->block->cf_node);
   impl->valid_metadata &= ~nir_metadata_instr_index;
}

bool
nir_instr_move(nir_cursor cursor, nir_instr *instr)
{
   /* If the cursor happens to refer to this instruction (either before or
    * after), don't do anything.
    */
   if ((cursor.option == nir_cursor_before_instr ||
        cursor.option == nir_cursor_after_instr) &&
       cursor.instr == instr)
      return false;

   nir_instr_remove(instr);
   nir_instr_insert(cursor, instr);
   return true;
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

static bool free_src_indirects_cb(nir_src *src, void *state)
{
   src_free_indirects(src);
   return true;
}

static bool free_dest_indirects_cb(nir_dest *dest, void *state)
{
   dest_free_indirects(dest);
   return true;
}

void nir_instr_free(nir_instr *instr)
{
   nir_foreach_src(instr, free_src_indirects_cb, NULL);
   nir_foreach_dest(instr, free_dest_indirects_cb, NULL);

   switch (instr->type) {
   case nir_instr_type_tex:
      free(nir_instr_as_tex(instr)->src);
      break;

   case nir_instr_type_phi: {
      nir_phi_instr *phi = nir_instr_as_phi(instr);
      nir_foreach_phi_src_safe(phi_src, phi) {
         free(phi_src);
      }
      break;
   }

   default:
      break;
   }

   list_del(&instr->gc_node);
   free(instr);
}

void
nir_instr_free_list(struct exec_list *list)
{
   struct exec_node *node;
   while ((node = exec_list_pop_head(list))) {
      nir_instr *removed_instr = exec_node_data(nir_instr, node, node);
      nir_instr_free(removed_instr);
   }
}

static bool nir_instr_free_and_dce_live_cb(nir_ssa_def *def, void *state)
{
   bool *live = state;

   if (!nir_ssa_def_is_unused(def)) {
      *live = true;
      return false;
   } else {
      return true;
   }
}

static bool nir_instr_free_and_dce_is_live(nir_instr *instr)
{
   /* Note: don't have to worry about jumps because they don't have dests to
    * become unused.
    */
   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      const nir_intrinsic_info *info = &nir_intrinsic_infos[intr->intrinsic];
      if (!(info->flags & NIR_INTRINSIC_CAN_ELIMINATE))
         return true;
   }

   bool live = false;
   nir_foreach_ssa_def(instr, nir_instr_free_and_dce_live_cb, &live);
   return live;
}

static bool
nir_instr_dce_add_dead_srcs_cb(nir_src *src, void *state)
{
   nir_instr_worklist *wl = state;

   if (src->is_ssa) {
      list_del(&src->use_link);
      if (!nir_instr_free_and_dce_is_live(src->ssa->parent_instr))
         nir_instr_worklist_push_tail(wl, src->ssa->parent_instr);

      /* Stop nir_instr_remove from trying to delete the link again. */
      src->ssa = NULL;
   }

   return true;
}

static void
nir_instr_dce_add_dead_ssa_srcs(nir_instr_worklist *wl, nir_instr *instr)
{
   nir_foreach_src(instr, nir_instr_dce_add_dead_srcs_cb, wl);
}

/**
 * Frees an instruction and any SSA defs that it used that are now dead,
 * returning a nir_cursor where the instruction previously was.
 */
nir_cursor
nir_instr_free_and_dce(nir_instr *instr)
{
   nir_instr_worklist *worklist = nir_instr_worklist_create();

   nir_instr_dce_add_dead_ssa_srcs(worklist, instr);
   nir_cursor c = nir_instr_remove(instr);

   struct exec_list to_free;
   exec_list_make_empty(&to_free);

   nir_instr *dce_instr;
   while ((dce_instr = nir_instr_worklist_pop_head(worklist))) {
      nir_instr_dce_add_dead_ssa_srcs(worklist, dce_instr);

      /* If we're removing the instr where our cursor is, then we have to
       * point the cursor elsewhere.
       */
      if ((c.option == nir_cursor_before_instr ||
           c.option == nir_cursor_after_instr) &&
          c.instr == dce_instr)
         c = nir_instr_remove(dce_instr);
      else
         nir_instr_remove(dce_instr);
      exec_list_push_tail(&to_free, &dce_instr->node);
   }

   nir_instr_free_list(&to_free);

   nir_instr_worklist_destroy(worklist);

   return c;
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

nir_ssa_def *
nir_instr_ssa_def(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_alu:
      assert(nir_instr_as_alu(instr)->dest.dest.is_ssa);
      return &nir_instr_as_alu(instr)->dest.dest.ssa;

   case nir_instr_type_deref:
      assert(nir_instr_as_deref(instr)->dest.is_ssa);
      return &nir_instr_as_deref(instr)->dest.ssa;

   case nir_instr_type_tex:
      assert(nir_instr_as_tex(instr)->dest.is_ssa);
      return &nir_instr_as_tex(instr)->dest.ssa;

   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      if (nir_intrinsic_infos[intrin->intrinsic].has_dest) {
         assert(intrin->dest.is_ssa);
         return &intrin->dest.ssa;
      } else {
         return NULL;
      }
   }

   case nir_instr_type_phi:
      assert(nir_instr_as_phi(instr)->dest.is_ssa);
      return &nir_instr_as_phi(instr)->dest.ssa;

   case nir_instr_type_parallel_copy:
      unreachable("Parallel copies are unsupported by this function");

   case nir_instr_type_load_const:
      return &nir_instr_as_load_const(instr)->def;

   case nir_instr_type_ssa_undef:
      return &nir_instr_as_ssa_undef(instr)->def;

   case nir_instr_type_call:
   case nir_instr_type_jump:
      return NULL;
   }

   unreachable("Invalid instruction type");
}

bool
nir_foreach_phi_src_leaving_block(nir_block *block,
                                  nir_foreach_src_cb cb,
                                  void *state)
{
   for (unsigned i = 0; i < ARRAY_SIZE(block->successors); i++) {
      if (block->successors[i] == NULL)
         continue;

      nir_foreach_instr(instr, block->successors[i]) {
         if (instr->type != nir_instr_type_phi)
            break;

         nir_phi_instr *phi = nir_instr_as_phi(instr);
         nir_foreach_phi_src(phi_src, phi) {
            if (phi_src->pred == block) {
               if (!cb(&phi_src->src, state))
                  return false;
            }
         }
      }
   }

   return true;
}

nir_const_value
nir_const_value_for_float(double f, unsigned bit_size)
{
   nir_const_value v;
   memset(&v, 0, sizeof(v));

   switch (bit_size) {
   case 16:
      v.u16 = _mesa_float_to_half(f);
      break;
   case 32:
      v.f32 = f;
      break;
   case 64:
      v.f64 = f;
      break;
   default:
      unreachable("Invalid bit size");
   }

   return v;
}

double
nir_const_value_as_float(nir_const_value value, unsigned bit_size)
{
   switch (bit_size) {
   case 16: return _mesa_half_to_float(value.u16);
   case 32: return value.f32;
   case 64: return value.f64;
   default:
      unreachable("Invalid bit size");
   }
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

   if (src.ssa->parent_instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(src.ssa->parent_instr);
      /* As are uniform variables */
      if (intr->intrinsic == nir_intrinsic_load_uniform &&
          nir_src_is_dynamically_uniform(intr->src[0]))
         return true;
      /* Push constant loads always use uniform offsets. */
      if (intr->intrinsic == nir_intrinsic_load_push_constant)
         return true;
      if (intr->intrinsic == nir_intrinsic_load_deref &&
          nir_deref_mode_is(nir_src_as_deref(intr->src[0]), nir_var_mem_push_const))
         return true;
   }

   /* Operating together dynamically uniform expressions produces a
    * dynamically uniform result
    */
   if (src.ssa->parent_instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(src.ssa->parent_instr);
      for (int i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
         if (!nir_src_is_dynamically_uniform(alu->src[i].src))
            return false;
      }

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
   nir_src_copy(src, &new_src);
   src_add_all_uses(src, instr, NULL);
}

void
nir_instr_move_src(nir_instr *dest_instr, nir_src *dest, nir_src *src)
{
   assert(!src_is_valid(dest) || dest->parent_instr == dest_instr);

   src_remove_all_uses(dest);
   src_free_indirects(dest);
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
   nir_src_copy(src, &new_src);
   src_add_all_uses(src, NULL, if_stmt);
}

void
nir_instr_rewrite_dest(nir_instr *instr, nir_dest *dest, nir_dest new_dest)
{
   if (dest->is_ssa) {
      /* We can only overwrite an SSA destination if it has no uses. */
      assert(nir_ssa_def_is_unused(&dest->ssa));
   } else {
      list_del(&dest->reg.def_link);
      if (dest->reg.indirect)
         src_remove_all_uses(dest->reg.indirect);
   }

   /* We can't re-write with an SSA def */
   assert(!new_dest.is_ssa);

   nir_dest_copy(dest, &new_dest);

   dest->reg.parent_instr = instr;
   list_addtail(&dest->reg.def_link, &new_dest.reg.reg->defs);

   if (dest->reg.indirect)
      src_add_all_uses(dest->reg.indirect, instr, NULL);
}

/* note: does *not* take ownership of 'name' */
void
nir_ssa_def_init(nir_instr *instr, nir_ssa_def *def,
                 unsigned num_components,
                 unsigned bit_size)
{
   def->parent_instr = instr;
   list_inithead(&def->uses);
   list_inithead(&def->if_uses);
   def->num_components = num_components;
   def->bit_size = bit_size;
   def->divergent = true; /* This is the safer default */

   if (instr->block) {
      nir_function_impl *impl =
         nir_cf_node_get_function(&instr->block->cf_node);

      def->index = impl->ssa_alloc++;

      impl->valid_metadata &= ~nir_metadata_live_ssa_defs;
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
   nir_ssa_def_init(instr, &dest->ssa, num_components, bit_size);
}

void
nir_ssa_def_rewrite_uses(nir_ssa_def *def, nir_ssa_def *new_ssa)
{
   assert(def != new_ssa);
   nir_foreach_use_safe(use_src, def)
      nir_instr_rewrite_src_ssa(use_src->parent_instr, use_src, new_ssa);

   nir_foreach_if_use_safe(use_src, def)
      nir_if_rewrite_condition_ssa(use_src->parent_if, use_src, new_ssa);
}

void
nir_ssa_def_rewrite_uses_src(nir_ssa_def *def, nir_src new_src)
{
   if (new_src.is_ssa) {
      nir_ssa_def_rewrite_uses(def, new_src.ssa);
   } else {
      nir_foreach_use_safe(use_src, def)
         nir_instr_rewrite_src(use_src->parent_instr, use_src, new_src);

      nir_foreach_if_use_safe(use_src, def)
         nir_if_rewrite_condition(use_src->parent_if, new_src);
   }
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
nir_ssa_def_rewrite_uses_after(nir_ssa_def *def, nir_ssa_def *new_ssa,
                               nir_instr *after_me)
{
   if (def == new_ssa)
      return;

   nir_foreach_use_safe(use_src, def) {
      assert(use_src->parent_instr != def->parent_instr);
      /* Since def already dominates all of its uses, the only way a use can
       * not be dominated by after_me is if it is between def and after_me in
       * the instruction list.
       */
      if (!is_instr_between(def->parent_instr, after_me, use_src->parent_instr))
         nir_instr_rewrite_src_ssa(use_src->parent_instr, use_src, new_ssa);
   }

   nir_foreach_if_use_safe(use_src, def) {
      nir_if_rewrite_condition_ssa(use_src->parent_if,
                                   &use_src->parent_if->condition,
                                   new_ssa);
   }
}

static nir_ssa_def *
get_store_value(nir_intrinsic_instr *intrin)
{
   assert(nir_intrinsic_has_write_mask(intrin));
   /* deref stores have the deref in src[0] and the store value in src[1] */
   if (intrin->intrinsic == nir_intrinsic_store_deref ||
       intrin->intrinsic == nir_intrinsic_store_deref_block_intel)
      return intrin->src[1].ssa;

   /* all other stores have the store value in src[0] */
   return intrin->src[0].ssa;
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
      } else if (use->parent_instr->type == nir_instr_type_intrinsic) {
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(use->parent_instr);
         if (nir_intrinsic_has_write_mask(intrin) && use->ssa == get_store_value(intrin)) {
            read_mask |= nir_intrinsic_write_mask(intrin);
         } else {
            return (1 << def->num_components) - 1;
         }
      } else {
         return (1 << def->num_components) - 1;
      }
   }

   if (!list_is_empty(&def->if_uses))
      read_mask |= 1;

   return read_mask;
}

nir_block *
nir_block_unstructured_next(nir_block *block)
{
   if (block == NULL) {
      /* nir_foreach_block_unstructured_safe() will call this function on a
       * NULL block after the last iteration, but it won't use the result so
       * just return NULL here.
       */
      return NULL;
   }

   nir_cf_node *cf_next = nir_cf_node_next(&block->cf_node);
   if (cf_next == NULL && block->cf_node.parent->type == nir_cf_node_function)
      return NULL;

   if (cf_next && cf_next->type == nir_cf_node_block)
      return nir_cf_node_as_block(cf_next);

   return nir_block_cf_tree_next(block);
}

nir_block *
nir_unstructured_start_block(nir_function_impl *impl)
{
   return nir_start_block(impl);
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

   assert(nir_cf_node_get_function(&block->cf_node)->structured);

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
   }
   FALLTHROUGH;

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

   assert(nir_cf_node_get_function(&block->cf_node)->structured);

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
   }
   FALLTHROUGH;

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

static int
compare_block_index(const void *p1, const void *p2)
{
   const nir_block *block1 = *((const nir_block **) p1);
   const nir_block *block2 = *((const nir_block **) p2);

   return (int) block1->index - (int) block2->index;
}

nir_block **
nir_block_get_predecessors_sorted(const nir_block *block, void *mem_ctx)
{
   nir_block **preds =
      ralloc_array(mem_ctx, nir_block *, block->predecessors->entries);

   unsigned i = 0;
   set_foreach(block->predecessors, entry)
      preds[i++] = (nir_block *) entry->key;
   assert(i == block->predecessors->entries);

   qsort(preds, block->predecessors->entries, sizeof(nir_block *),
         compare_block_index);

   return preds;
}

void
nir_index_blocks(nir_function_impl *impl)
{
   unsigned index = 0;

   if (impl->valid_metadata & nir_metadata_block_index)
      return;

   nir_foreach_block_unstructured(block, impl) {
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

   impl->valid_metadata &= ~nir_metadata_live_ssa_defs;

   nir_foreach_block_unstructured(block, impl) {
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
      block->start_ip = index++;

      nir_foreach_instr(instr, block)
         instr->index = index++;

      block->end_ip = index++;
   }

   return index;
}

unsigned
nir_shader_index_vars(nir_shader *shader, nir_variable_mode modes)
{
   unsigned count = 0;
   nir_foreach_variable_with_modes(var, shader, modes)
      var->index = count++;
   return count;
}

unsigned
nir_function_impl_index_vars(nir_function_impl *impl)
{
   unsigned count = 0;
   nir_foreach_function_temp_variable(var, impl)
      var->index = count++;
   return count;
}

static nir_instr *
cursor_next_instr(nir_cursor cursor)
{
   switch (cursor.option) {
   case nir_cursor_before_block:
      for (nir_block *block = cursor.block; block;
           block = nir_block_cf_tree_next(block)) {
         nir_instr *instr = nir_block_first_instr(block);
         if (instr)
            return instr;
      }
      return NULL;

   case nir_cursor_after_block:
      cursor.block = nir_block_cf_tree_next(cursor.block);
      if (cursor.block == NULL)
         return NULL;

      cursor.option = nir_cursor_before_block;
      return cursor_next_instr(cursor);

   case nir_cursor_before_instr:
      return cursor.instr;

   case nir_cursor_after_instr:
      if (nir_instr_next(cursor.instr))
         return nir_instr_next(cursor.instr);

      cursor.option = nir_cursor_after_block;
      cursor.block = cursor.instr->block;
      return cursor_next_instr(cursor);
   }

   unreachable("Inavlid cursor option");
}

ASSERTED static bool
dest_is_ssa(nir_dest *dest, void *_state)
{
   (void) _state;
   return dest->is_ssa;
}

bool
nir_function_impl_lower_instructions(nir_function_impl *impl,
                                     nir_instr_filter_cb filter,
                                     nir_lower_instr_cb lower,
                                     void *cb_data)
{
   nir_builder b;
   nir_builder_init(&b, impl);

   nir_metadata preserved = nir_metadata_block_index |
                            nir_metadata_dominance;

   bool progress = false;
   nir_cursor iter = nir_before_cf_list(&impl->body);
   nir_instr *instr;
   while ((instr = cursor_next_instr(iter)) != NULL) {
      if (filter && !filter(instr, cb_data)) {
         iter = nir_after_instr(instr);
         continue;
      }

      assert(nir_foreach_dest(instr, dest_is_ssa, NULL));
      nir_ssa_def *old_def = nir_instr_ssa_def(instr);
      struct list_head old_uses, old_if_uses;
      if (old_def != NULL) {
         /* We're about to ask the callback to generate a replacement for instr.
          * Save off the uses from instr's SSA def so we know what uses to
          * rewrite later.  If we use nir_ssa_def_rewrite_uses, it fails in the
          * case where the generated replacement code uses the result of instr
          * itself.  If we use nir_ssa_def_rewrite_uses_after (which is the
          * normal solution to this problem), it doesn't work well if control-
          * flow is inserted as part of the replacement, doesn't handle cases
          * where the replacement is something consumed by instr, and suffers
          * from performance issues.  This is the only way to 100% guarantee
          * that we rewrite the correct set efficiently.
          */

         list_replace(&old_def->uses, &old_uses);
         list_inithead(&old_def->uses);
         list_replace(&old_def->if_uses, &old_if_uses);
         list_inithead(&old_def->if_uses);
      }

      b.cursor = nir_after_instr(instr);
      nir_ssa_def *new_def = lower(&b, instr, cb_data);
      if (new_def && new_def != NIR_LOWER_INSTR_PROGRESS &&
          new_def != NIR_LOWER_INSTR_PROGRESS_REPLACE) {
         assert(old_def != NULL);
         if (new_def->parent_instr->block != instr->block)
            preserved = nir_metadata_none;

         nir_src new_src = nir_src_for_ssa(new_def);
         list_for_each_entry_safe(nir_src, use_src, &old_uses, use_link)
            nir_instr_rewrite_src(use_src->parent_instr, use_src, new_src);

         list_for_each_entry_safe(nir_src, use_src, &old_if_uses, use_link)
            nir_if_rewrite_condition(use_src->parent_if, new_src);

         if (nir_ssa_def_is_unused(old_def)) {
            iter = nir_instr_free_and_dce(instr);
         } else {
            iter = nir_after_instr(instr);
         }
         progress = true;
      } else {
         /* We didn't end up lowering after all.  Put the uses back */
         if (old_def) {
            list_replace(&old_uses, &old_def->uses);
            list_replace(&old_if_uses, &old_def->if_uses);
         }
         if (new_def == NIR_LOWER_INSTR_PROGRESS_REPLACE) {
            /* Only instructions without a return value can be removed like this */
            assert(!old_def);
            iter = nir_instr_free_and_dce(instr);
            progress = true;
         } else
            iter = nir_after_instr(instr);

         if (new_def == NIR_LOWER_INSTR_PROGRESS)
            progress = true;
      }
   }

   if (progress) {
      nir_metadata_preserve(impl, preserved);
   } else {
      nir_metadata_preserve(impl, nir_metadata_all);
   }

   return progress;
}

bool
nir_shader_lower_instructions(nir_shader *shader,
                              nir_instr_filter_cb filter,
                              nir_lower_instr_cb lower,
                              void *cb_data)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl &&
          nir_function_impl_lower_instructions(function->impl,
                                               filter, lower, cb_data))
         progress = true;
   }

   return progress;
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
   case SYSTEM_VALUE_POINT_COORD:
      return nir_intrinsic_load_point_coord;
   case SYSTEM_VALUE_LINE_COORD:
      return nir_intrinsic_load_line_coord;
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
   case SYSTEM_VALUE_WORKGROUP_ID:
      return nir_intrinsic_load_workgroup_id;
   case SYSTEM_VALUE_NUM_WORKGROUPS:
      return nir_intrinsic_load_num_workgroups;
   case SYSTEM_VALUE_PRIMITIVE_ID:
      return nir_intrinsic_load_primitive_id;
   case SYSTEM_VALUE_TESS_COORD:
      return nir_intrinsic_load_tess_coord;
   case SYSTEM_VALUE_TESS_LEVEL_OUTER:
      return nir_intrinsic_load_tess_level_outer;
   case SYSTEM_VALUE_TESS_LEVEL_INNER:
      return nir_intrinsic_load_tess_level_inner;
   case SYSTEM_VALUE_TESS_LEVEL_OUTER_DEFAULT:
      return nir_intrinsic_load_tess_level_outer_default;
   case SYSTEM_VALUE_TESS_LEVEL_INNER_DEFAULT:
      return nir_intrinsic_load_tess_level_inner_default;
   case SYSTEM_VALUE_VERTICES_IN:
      return nir_intrinsic_load_patch_vertices_in;
   case SYSTEM_VALUE_HELPER_INVOCATION:
      return nir_intrinsic_load_helper_invocation;
   case SYSTEM_VALUE_COLOR0:
      return nir_intrinsic_load_color0;
   case SYSTEM_VALUE_COLOR1:
      return nir_intrinsic_load_color1;
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
   case SYSTEM_VALUE_WORKGROUP_SIZE:
      return nir_intrinsic_load_workgroup_size;
   case SYSTEM_VALUE_GLOBAL_INVOCATION_ID:
      return nir_intrinsic_load_global_invocation_id;
   case SYSTEM_VALUE_BASE_GLOBAL_INVOCATION_ID:
      return nir_intrinsic_load_base_global_invocation_id;
   case SYSTEM_VALUE_GLOBAL_INVOCATION_INDEX:
      return nir_intrinsic_load_global_invocation_index;
   case SYSTEM_VALUE_WORK_DIM:
      return nir_intrinsic_load_work_dim;
   case SYSTEM_VALUE_USER_DATA_AMD:
      return nir_intrinsic_load_user_data_amd;
   case SYSTEM_VALUE_RAY_LAUNCH_ID:
      return nir_intrinsic_load_ray_launch_id;
   case SYSTEM_VALUE_RAY_LAUNCH_SIZE:
      return nir_intrinsic_load_ray_launch_size;
   case SYSTEM_VALUE_RAY_WORLD_ORIGIN:
      return nir_intrinsic_load_ray_world_origin;
   case SYSTEM_VALUE_RAY_WORLD_DIRECTION:
      return nir_intrinsic_load_ray_world_direction;
   case SYSTEM_VALUE_RAY_OBJECT_ORIGIN:
      return nir_intrinsic_load_ray_object_origin;
   case SYSTEM_VALUE_RAY_OBJECT_DIRECTION:
      return nir_intrinsic_load_ray_object_direction;
   case SYSTEM_VALUE_RAY_T_MIN:
      return nir_intrinsic_load_ray_t_min;
   case SYSTEM_VALUE_RAY_T_MAX:
      return nir_intrinsic_load_ray_t_max;
   case SYSTEM_VALUE_RAY_OBJECT_TO_WORLD:
      return nir_intrinsic_load_ray_object_to_world;
   case SYSTEM_VALUE_RAY_WORLD_TO_OBJECT:
      return nir_intrinsic_load_ray_world_to_object;
   case SYSTEM_VALUE_RAY_HIT_KIND:
      return nir_intrinsic_load_ray_hit_kind;
   case SYSTEM_VALUE_RAY_FLAGS:
      return nir_intrinsic_load_ray_flags;
   case SYSTEM_VALUE_RAY_GEOMETRY_INDEX:
      return nir_intrinsic_load_ray_geometry_index;
   case SYSTEM_VALUE_RAY_INSTANCE_CUSTOM_INDEX:
      return nir_intrinsic_load_ray_instance_custom_index;
   case SYSTEM_VALUE_FRAG_SHADING_RATE:
      return nir_intrinsic_load_frag_shading_rate;
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
   case nir_intrinsic_load_point_coord:
      return SYSTEM_VALUE_POINT_COORD;
   case nir_intrinsic_load_line_coord:
      return SYSTEM_VALUE_LINE_COORD;
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
   case nir_intrinsic_load_num_workgroups:
      return SYSTEM_VALUE_NUM_WORKGROUPS;
   case nir_intrinsic_load_workgroup_id:
      return SYSTEM_VALUE_WORKGROUP_ID;
   case nir_intrinsic_load_primitive_id:
      return SYSTEM_VALUE_PRIMITIVE_ID;
   case nir_intrinsic_load_tess_coord:
      return SYSTEM_VALUE_TESS_COORD;
   case nir_intrinsic_load_tess_level_outer:
      return SYSTEM_VALUE_TESS_LEVEL_OUTER;
   case nir_intrinsic_load_tess_level_inner:
      return SYSTEM_VALUE_TESS_LEVEL_INNER;
   case nir_intrinsic_load_tess_level_outer_default:
      return SYSTEM_VALUE_TESS_LEVEL_OUTER_DEFAULT;
   case nir_intrinsic_load_tess_level_inner_default:
      return SYSTEM_VALUE_TESS_LEVEL_INNER_DEFAULT;
   case nir_intrinsic_load_patch_vertices_in:
      return SYSTEM_VALUE_VERTICES_IN;
   case nir_intrinsic_load_helper_invocation:
      return SYSTEM_VALUE_HELPER_INVOCATION;
   case nir_intrinsic_load_color0:
      return SYSTEM_VALUE_COLOR0;
   case nir_intrinsic_load_color1:
      return SYSTEM_VALUE_COLOR1;
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
   case nir_intrinsic_load_workgroup_size:
      return SYSTEM_VALUE_WORKGROUP_SIZE;
   case nir_intrinsic_load_global_invocation_id:
      return SYSTEM_VALUE_GLOBAL_INVOCATION_ID;
   case nir_intrinsic_load_base_global_invocation_id:
      return SYSTEM_VALUE_BASE_GLOBAL_INVOCATION_ID;
   case nir_intrinsic_load_global_invocation_index:
      return SYSTEM_VALUE_GLOBAL_INVOCATION_INDEX;
   case nir_intrinsic_load_work_dim:
      return SYSTEM_VALUE_WORK_DIM;
   case nir_intrinsic_load_user_data_amd:
      return SYSTEM_VALUE_USER_DATA_AMD;
   case nir_intrinsic_load_barycentric_model:
      return SYSTEM_VALUE_BARYCENTRIC_PULL_MODEL;
   case nir_intrinsic_load_gs_header_ir3:
      return SYSTEM_VALUE_GS_HEADER_IR3;
   case nir_intrinsic_load_tcs_header_ir3:
      return SYSTEM_VALUE_TCS_HEADER_IR3;
   case nir_intrinsic_load_ray_launch_id:
      return SYSTEM_VALUE_RAY_LAUNCH_ID;
   case nir_intrinsic_load_ray_launch_size:
      return SYSTEM_VALUE_RAY_LAUNCH_SIZE;
   case nir_intrinsic_load_ray_world_origin:
      return SYSTEM_VALUE_RAY_WORLD_ORIGIN;
   case nir_intrinsic_load_ray_world_direction:
      return SYSTEM_VALUE_RAY_WORLD_DIRECTION;
   case nir_intrinsic_load_ray_object_origin:
      return SYSTEM_VALUE_RAY_OBJECT_ORIGIN;
   case nir_intrinsic_load_ray_object_direction:
      return SYSTEM_VALUE_RAY_OBJECT_DIRECTION;
   case nir_intrinsic_load_ray_t_min:
      return SYSTEM_VALUE_RAY_T_MIN;
   case nir_intrinsic_load_ray_t_max:
      return SYSTEM_VALUE_RAY_T_MAX;
   case nir_intrinsic_load_ray_object_to_world:
      return SYSTEM_VALUE_RAY_OBJECT_TO_WORLD;
   case nir_intrinsic_load_ray_world_to_object:
      return SYSTEM_VALUE_RAY_WORLD_TO_OBJECT;
   case nir_intrinsic_load_ray_hit_kind:
      return SYSTEM_VALUE_RAY_HIT_KIND;
   case nir_intrinsic_load_ray_flags:
      return SYSTEM_VALUE_RAY_FLAGS;
   case nir_intrinsic_load_ray_geometry_index:
      return SYSTEM_VALUE_RAY_GEOMETRY_INDEX;
   case nir_intrinsic_load_ray_instance_custom_index:
      return SYSTEM_VALUE_RAY_INSTANCE_CUSTOM_INDEX;
   case nir_intrinsic_load_frag_shading_rate:
      return SYSTEM_VALUE_FRAG_SHADING_RATE;
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
   nir_foreach_shader_in_variable(var, shader) {
      if (glsl_type_is_dual_slot(glsl_without_array(var->type))) {
         unsigned slots = glsl_count_attribute_slots(var->type, true);
         *dual_slot |= BITFIELD64_MASK(slots) << var->data.location;
      }
   }

   nir_foreach_shader_in_variable(var, shader) {
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
   enum gl_access_qualifier access = nir_intrinsic_access(intrin);

   /* Image intrinsics only have one of these */
   assert(!nir_intrinsic_has_src_type(intrin) ||
          !nir_intrinsic_has_dest_type(intrin));

   nir_alu_type data_type = nir_type_invalid;
   if (nir_intrinsic_has_src_type(intrin))
      data_type = nir_intrinsic_src_type(intrin);
   if (nir_intrinsic_has_dest_type(intrin))
      data_type = nir_intrinsic_dest_type(intrin);

   switch (intrin->intrinsic) {
#define CASE(op) \
   case nir_intrinsic_image_deref_##op: \
      intrin->intrinsic = bindless ? nir_intrinsic_bindless_image_##op \
                                   : nir_intrinsic_image_##op; \
      break;
   CASE(load)
   CASE(sparse_load)
   CASE(store)
   CASE(atomic_add)
   CASE(atomic_imin)
   CASE(atomic_umin)
   CASE(atomic_imax)
   CASE(atomic_umax)
   CASE(atomic_and)
   CASE(atomic_or)
   CASE(atomic_xor)
   CASE(atomic_exchange)
   CASE(atomic_comp_swap)
   CASE(atomic_fadd)
   CASE(atomic_fmin)
   CASE(atomic_fmax)
   CASE(atomic_inc_wrap)
   CASE(atomic_dec_wrap)
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

   /* Only update the format if the intrinsic doesn't have one set */
   if (nir_intrinsic_format(intrin) == PIPE_FORMAT_NONE)
      nir_intrinsic_set_format(intrin, var->data.image.format);

   nir_intrinsic_set_access(intrin, access | var->data.access);
   if (nir_intrinsic_has_src_type(intrin))
      nir_intrinsic_set_src_type(intrin, data_type);
   if (nir_intrinsic_has_dest_type(intrin))
      nir_intrinsic_set_dest_type(intrin, data_type);

   nir_instr_rewrite_src(&intrin->instr, &intrin->src[0],
                         nir_src_for_ssa(src));
}

unsigned
nir_image_intrinsic_coord_components(const nir_intrinsic_instr *instr)
{
   enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
   int coords = glsl_get_sampler_dim_coordinate_components(dim);
   if (dim == GLSL_SAMPLER_DIM_CUBE)
      return coords;
   else
      return coords + nir_intrinsic_image_array(instr);
}

nir_src *
nir_get_shader_call_payload_src(nir_intrinsic_instr *call)
{
   switch (call->intrinsic) {
   case nir_intrinsic_trace_ray:
   case nir_intrinsic_rt_trace_ray:
      return &call->src[10];
   case nir_intrinsic_execute_callable:
   case nir_intrinsic_rt_execute_callable:
      return &call->src[1];
   default:
      unreachable("Not a call intrinsic");
      return NULL;
   }
}

nir_binding nir_chase_binding(nir_src rsrc)
{
   nir_binding res = {0};
   if (rsrc.ssa->parent_instr->type == nir_instr_type_deref) {
      const struct glsl_type *type = glsl_without_array(nir_src_as_deref(rsrc)->type);
      bool is_image = glsl_type_is_image(type) || glsl_type_is_sampler(type);
      while (rsrc.ssa->parent_instr->type == nir_instr_type_deref) {
         nir_deref_instr *deref = nir_src_as_deref(rsrc);

         if (deref->deref_type == nir_deref_type_var) {
            res.success = true;
            res.var = deref->var;
            res.desc_set = deref->var->data.descriptor_set;
            res.binding = deref->var->data.binding;
            return res;
         } else if (deref->deref_type == nir_deref_type_array && is_image) {
            if (res.num_indices == ARRAY_SIZE(res.indices))
               return (nir_binding){0};
            res.indices[res.num_indices++] = deref->arr.index;
         }

         rsrc = deref->parent;
      }
   }

   /* Skip copies and trimming. Trimming can appear as nir_op_mov instructions
    * when removing the offset from addresses. We also consider nir_op_is_vec()
    * instructions to skip trimming of vec2_index_32bit_offset addresses after
    * lowering ALU to scalar.
    */
   while (true) {
      nir_alu_instr *alu = nir_src_as_alu_instr(rsrc);
      nir_intrinsic_instr *intrin = nir_src_as_intrinsic(rsrc);
      if (alu && alu->op == nir_op_mov) {
         for (unsigned i = 0; i < alu->dest.dest.ssa.num_components; i++) {
            if (alu->src[0].swizzle[i] != i)
               return (nir_binding){0};
         }
         rsrc = alu->src[0].src;
      } else if (alu && nir_op_is_vec(alu->op)) {
         for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
            if (alu->src[i].swizzle[0] != i || alu->src[i].src.ssa != alu->src[0].src.ssa)
               return (nir_binding){0};
         }
         rsrc = alu->src[0].src;
      } else if (intrin && intrin->intrinsic == nir_intrinsic_read_first_invocation) {
         /* The caller might want to be aware if only the first invocation of
          * the indices are used.
          */
         res.read_first_invocation = true;
         rsrc = intrin->src[0];
      } else {
         break;
      }
   }

   if (nir_src_is_const(rsrc)) {
      /* GL binding model after deref lowering */
      res.success = true;
      res.binding = nir_src_as_uint(rsrc);
      return res;
   }

   /* otherwise, must be Vulkan binding model after deref lowering or GL bindless */

   nir_intrinsic_instr *intrin = nir_src_as_intrinsic(rsrc);
   if (!intrin)
      return (nir_binding){0};

   /* skip load_vulkan_descriptor */
   if (intrin->intrinsic == nir_intrinsic_load_vulkan_descriptor) {
      intrin = nir_src_as_intrinsic(intrin->src[0]);
      if (!intrin)
         return (nir_binding){0};
   }

   if (intrin->intrinsic != nir_intrinsic_vulkan_resource_index)
      return (nir_binding){0};

   assert(res.num_indices == 0);
   res.success = true;
   res.desc_set = nir_intrinsic_desc_set(intrin);
   res.binding = nir_intrinsic_binding(intrin);
   res.num_indices = 1;
   res.indices[0] = intrin->src[0];
   return res;
}

nir_variable *nir_get_binding_variable(nir_shader *shader, nir_binding binding)
{
   nir_variable *binding_var = NULL;
   unsigned count = 0;

   if (!binding.success)
      return NULL;

   if (binding.var)
      return binding.var;

   nir_foreach_variable_with_modes(var, shader, nir_var_mem_ubo | nir_var_mem_ssbo) {
      if (var->data.descriptor_set == binding.desc_set && var->data.binding == binding.binding) {
         binding_var = var;
         count++;
      }
   }

   /* Be conservative if another variable is using the same binding/desc_set
    * because the access mask might be different and we can't get it reliably.
    */
   if (count > 1)
      return NULL;

   return binding_var;
}

bool
nir_alu_instr_is_copy(nir_alu_instr *instr)
{
   assert(instr->src[0].src.is_ssa);

   if (instr->op == nir_op_mov) {
      return !instr->dest.saturate &&
             !instr->src[0].abs &&
             !instr->src[0].negate;
   } else if (nir_op_is_vec(instr->op)) {
      for (unsigned i = 0; i < instr->dest.dest.ssa.num_components; i++) {
         if (instr->src[i].abs || instr->src[i].negate)
            return false;
      }
      return !instr->dest.saturate;
   } else {
      return false;
   }
}

nir_ssa_scalar
nir_ssa_scalar_chase_movs(nir_ssa_scalar s)
{
   while (nir_ssa_scalar_is_alu(s)) {
      nir_alu_instr *alu = nir_instr_as_alu(s.def->parent_instr);
      if (!nir_alu_instr_is_copy(alu))
         break;

      if (alu->op == nir_op_mov) {
         s.def = alu->src[0].src.ssa;
         s.comp = alu->src[0].swizzle[s.comp];
      } else {
         assert(nir_op_is_vec(alu->op));
         s.def = alu->src[s.comp].src.ssa;
         s.comp = alu->src[s.comp].swizzle[0];
      }
   }

   return s;
}
