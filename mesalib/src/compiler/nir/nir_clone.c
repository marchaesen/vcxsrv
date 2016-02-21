/*
 * Copyright © 2015 Red Hat
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

#include "nir.h"
#include "nir_control_flow_private.h"

/* Secret Decoder Ring:
 *   clone_foo():
 *        Allocate and clone a foo.
 *   __clone_foo():
 *        Clone body of foo (ie. parent class, embedded struct, etc)
 */

typedef struct {
   /* maps orig ptr -> cloned ptr: */
   struct hash_table *ptr_table;

   /* List of phi sources. */
   struct list_head phi_srcs;

   /* new shader object, used as memctx for just about everything else: */
   nir_shader *ns;
} clone_state;

static void
init_clone_state(clone_state *state)
{
   state->ptr_table = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                              _mesa_key_pointer_equal);
   list_inithead(&state->phi_srcs);
}

static void
free_clone_state(clone_state *state)
{
   _mesa_hash_table_destroy(state->ptr_table, NULL);
}

static void *
lookup_ptr(clone_state *state, const void *ptr)
{
   struct hash_entry *entry;

   if (!ptr)
      return NULL;

   entry = _mesa_hash_table_search(state->ptr_table, ptr);
   assert(entry && "Failed to find pointer!");
   if (!entry)
      return NULL;

   return entry->data;
}

static void
store_ptr(clone_state *state, void *nptr, const void *ptr)
{
   _mesa_hash_table_insert(state->ptr_table, ptr, nptr);
}

static nir_constant *
clone_constant(clone_state *state, const nir_constant *c, nir_variable *nvar)
{
   nir_constant *nc = ralloc(nvar, nir_constant);

   nc->value = c->value;
   nc->num_elements = c->num_elements;
   nc->elements = ralloc_array(nvar, nir_constant *, c->num_elements);
   for (unsigned i = 0; i < c->num_elements; i++) {
      nc->elements[i] = clone_constant(state, c->elements[i], nvar);
   }

   return nc;
}

/* NOTE: for cloning nir_variable's, bypass nir_variable_create to avoid
 * having to deal with locals and globals separately:
 */
static nir_variable *
clone_variable(clone_state *state, const nir_variable *var)
{
   nir_variable *nvar = rzalloc(state->ns, nir_variable);
   store_ptr(state, nvar, var);

   nvar->type = var->type;
   nvar->name = ralloc_strdup(nvar, var->name);
   nvar->data = var->data;
   nvar->num_state_slots = var->num_state_slots;
   nvar->state_slots = ralloc_array(nvar, nir_state_slot, var->num_state_slots);
   memcpy(nvar->state_slots, var->state_slots,
          var->num_state_slots * sizeof(nir_state_slot));
   if (var->constant_initializer) {
      nvar->constant_initializer =
         clone_constant(state, var->constant_initializer, nvar);
   }
   nvar->interface_type = var->interface_type;

   return nvar;
}

/* clone list of nir_variable: */
static void
clone_var_list(clone_state *state, struct exec_list *dst,
               const struct exec_list *list)
{
   exec_list_make_empty(dst);
   foreach_list_typed(nir_variable, var, node, list) {
      nir_variable *nvar = clone_variable(state, var);
      exec_list_push_tail(dst, &nvar->node);
   }
}

/* NOTE: for cloning nir_register's, bypass nir_global/local_reg_create()
 * to avoid having to deal with locals and globals separately:
 */
static nir_register *
clone_register(clone_state *state, const nir_register *reg)
{
   nir_register *nreg = rzalloc(state->ns, nir_register);
   store_ptr(state, nreg, reg);

   nreg->num_components = reg->num_components;
   nreg->num_array_elems = reg->num_array_elems;
   nreg->index = reg->index;
   nreg->name = ralloc_strdup(nreg, reg->name);
   nreg->is_global = reg->is_global;
   nreg->is_packed = reg->is_packed;

   /* reconstructing uses/defs/if_uses handled by nir_instr_insert() */
   list_inithead(&nreg->uses);
   list_inithead(&nreg->defs);
   list_inithead(&nreg->if_uses);

   return nreg;
}

/* clone list of nir_register: */
static void
clone_reg_list(clone_state *state, struct exec_list *dst,
               const struct exec_list *list)
{
   exec_list_make_empty(dst);
   foreach_list_typed(nir_register, reg, node, list) {
      nir_register *nreg = clone_register(state, reg);
      exec_list_push_tail(dst, &nreg->node);
   }
}

static void
__clone_src(clone_state *state, void *ninstr_or_if,
            nir_src *nsrc, const nir_src *src)
{
   nsrc->is_ssa = src->is_ssa;
   if (src->is_ssa) {
      nsrc->ssa = lookup_ptr(state, src->ssa);
   } else {
      nsrc->reg.reg = lookup_ptr(state, src->reg.reg);
      if (src->reg.indirect) {
         nsrc->reg.indirect = ralloc(ninstr_or_if, nir_src);
         __clone_src(state, ninstr_or_if, nsrc->reg.indirect, src->reg.indirect);
      }
      nsrc->reg.base_offset = src->reg.base_offset;
   }
}

static void
__clone_dst(clone_state *state, nir_instr *ninstr,
            nir_dest *ndst, const nir_dest *dst)
{
   ndst->is_ssa = dst->is_ssa;
   if (dst->is_ssa) {
      nir_ssa_dest_init(ninstr, ndst, dst->ssa.num_components, dst->ssa.name);
      store_ptr(state, &ndst->ssa, &dst->ssa);
   } else {
      ndst->reg.reg = lookup_ptr(state, dst->reg.reg);
      if (dst->reg.indirect) {
         ndst->reg.indirect = ralloc(ninstr, nir_src);
         __clone_src(state, ninstr, ndst->reg.indirect, dst->reg.indirect);
      }
      ndst->reg.base_offset = dst->reg.base_offset;
   }
}

static nir_deref *clone_deref(clone_state *state, const nir_deref *deref,
                              nir_instr *ninstr, nir_deref *parent);

static nir_deref_var *
clone_deref_var(clone_state *state, const nir_deref_var *dvar,
                nir_instr *ninstr)
{
   nir_variable *nvar = lookup_ptr(state, dvar->var);
   nir_deref_var *ndvar = nir_deref_var_create(ninstr, nvar);

   if (dvar->deref.child)
      ndvar->deref.child = clone_deref(state, dvar->deref.child,
                                       ninstr, &ndvar->deref);

   return ndvar;
}

static nir_deref_array *
clone_deref_array(clone_state *state, const nir_deref_array *darr,
                  nir_instr *ninstr, nir_deref *parent)
{
   nir_deref_array *ndarr = nir_deref_array_create(parent);

   ndarr->deref.type = darr->deref.type;
   if (darr->deref.child)
      ndarr->deref.child = clone_deref(state, darr->deref.child,
                                       ninstr, &ndarr->deref);

   ndarr->deref_array_type = darr->deref_array_type;
   ndarr->base_offset = darr->base_offset;
   if (ndarr->deref_array_type == nir_deref_array_type_indirect)
      __clone_src(state, ninstr, &ndarr->indirect, &darr->indirect);

   return ndarr;
}

static nir_deref_struct *
clone_deref_struct(clone_state *state, const nir_deref_struct *dstr,
                   nir_instr *ninstr, nir_deref *parent)
{
   nir_deref_struct *ndstr = nir_deref_struct_create(parent, dstr->index);

   ndstr->deref.type = dstr->deref.type;
   if (dstr->deref.child)
      ndstr->deref.child = clone_deref(state, dstr->deref.child,
                                       ninstr, &ndstr->deref);

   return ndstr;
}

static nir_deref *
clone_deref(clone_state *state, const nir_deref *dref,
            nir_instr *ninstr, nir_deref *parent)
{
   switch (dref->deref_type) {
   case nir_deref_type_array:
      return &clone_deref_array(state, nir_deref_as_array(dref),
                                ninstr, parent)->deref;
   case nir_deref_type_struct:
      return &clone_deref_struct(state, nir_deref_as_struct(dref),
                                 ninstr, parent)->deref;
   default:
      unreachable("bad deref type");
      return NULL;
   }
}

static nir_alu_instr *
clone_alu(clone_state *state, const nir_alu_instr *alu)
{
   nir_alu_instr *nalu = nir_alu_instr_create(state->ns, alu->op);

   __clone_dst(state, &nalu->instr, &nalu->dest.dest, &alu->dest.dest);
   nalu->dest.saturate = alu->dest.saturate;
   nalu->dest.write_mask = alu->dest.write_mask;

   for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
      __clone_src(state, &nalu->instr, &nalu->src[i].src, &alu->src[i].src);
      nalu->src[i].negate = alu->src[i].negate;
      nalu->src[i].abs = alu->src[i].abs;
      memcpy(nalu->src[i].swizzle, alu->src[i].swizzle,
             sizeof(nalu->src[i].swizzle));
   }

   return nalu;
}

static nir_intrinsic_instr *
clone_intrinsic(clone_state *state, const nir_intrinsic_instr *itr)
{
   nir_intrinsic_instr *nitr =
      nir_intrinsic_instr_create(state->ns, itr->intrinsic);

   unsigned num_variables = nir_intrinsic_infos[itr->intrinsic].num_variables;
   unsigned num_srcs = nir_intrinsic_infos[itr->intrinsic].num_srcs;

   if (nir_intrinsic_infos[itr->intrinsic].has_dest)
      __clone_dst(state, &nitr->instr, &nitr->dest, &itr->dest);

   nitr->num_components = itr->num_components;
   memcpy(nitr->const_index, itr->const_index, sizeof(nitr->const_index));

   for (unsigned i = 0; i < num_variables; i++) {
      nitr->variables[i] = clone_deref_var(state, itr->variables[i],
                                           &nitr->instr);
   }

   for (unsigned i = 0; i < num_srcs; i++)
      __clone_src(state, &nitr->instr, &nitr->src[i], &itr->src[i]);

   return nitr;
}

static nir_load_const_instr *
clone_load_const(clone_state *state, const nir_load_const_instr *lc)
{
   nir_load_const_instr *nlc =
      nir_load_const_instr_create(state->ns, lc->def.num_components);

   memcpy(&nlc->value, &lc->value, sizeof(nlc->value));

   store_ptr(state, &nlc->def, &lc->def);

   return nlc;
}

static nir_ssa_undef_instr *
clone_ssa_undef(clone_state *state, const nir_ssa_undef_instr *sa)
{
   nir_ssa_undef_instr *nsa =
      nir_ssa_undef_instr_create(state->ns, sa->def.num_components);

   store_ptr(state, &nsa->def, &sa->def);

   return nsa;
}

static nir_tex_instr *
clone_tex(clone_state *state, const nir_tex_instr *tex)
{
   nir_tex_instr *ntex = nir_tex_instr_create(state->ns, tex->num_srcs);

   ntex->sampler_dim = tex->sampler_dim;
   ntex->dest_type = tex->dest_type;
   ntex->op = tex->op;
   __clone_dst(state, &ntex->instr, &ntex->dest, &tex->dest);
   for (unsigned i = 0; i < ntex->num_srcs; i++) {
      ntex->src[i].src_type = tex->src[i].src_type;
      __clone_src(state, &ntex->instr, &ntex->src[i].src, &tex->src[i].src);
   }
   ntex->coord_components = tex->coord_components;
   ntex->is_array = tex->is_array;
   ntex->is_shadow = tex->is_shadow;
   ntex->is_new_style_shadow = tex->is_new_style_shadow;
   ntex->component = tex->component;

   ntex->texture_index = tex->texture_index;
   if (tex->texture)
      ntex->texture = clone_deref_var(state, tex->texture, &ntex->instr);
   ntex->texture_array_size = tex->texture_array_size;

   ntex->sampler_index = tex->sampler_index;
   if (tex->sampler)
      ntex->sampler = clone_deref_var(state, tex->sampler, &ntex->instr);

   return ntex;
}

static nir_phi_instr *
clone_phi(clone_state *state, const nir_phi_instr *phi, nir_block *nblk)
{
   nir_phi_instr *nphi = nir_phi_instr_create(state->ns);

   __clone_dst(state, &nphi->instr, &nphi->dest, &phi->dest);

   /* Cloning a phi node is a bit different from other instructions.  The
    * sources of phi instructions are the only time where we can use an SSA
    * def before it is defined.  In order to handle this, we just copy over
    * the sources from the old phi instruction directly and then fix them up
    * in a second pass once all the instrutions in the function have been
    * properly cloned.
    *
    * In order to ensure that the copied sources (which are the same as the
    * old phi instruction's sources for now) don't get inserted into the old
    * shader's use-def lists, we have to add the phi instruction *before* we
    * set up its sources.
    */
   nir_instr_insert_after_block(nblk, &nphi->instr);

   foreach_list_typed(nir_phi_src, src, node, &phi->srcs) {
      nir_phi_src *nsrc = ralloc(nphi, nir_phi_src);

      /* Just copy the old source for now. */
      memcpy(nsrc, src, sizeof(*src));

      /* Since we're not letting nir_insert_instr handle use/def stuff for us,
       * we have to set the parent_instr manually.  It doesn't really matter
       * when we do it, so we might as well do it here.
       */
      nsrc->src.parent_instr = &nphi->instr;

      /* Stash it in the list of phi sources.  We'll walk this list and fix up
       * sources at the very end of clone_function_impl.
       */
      list_add(&nsrc->src.use_link, &state->phi_srcs);

      exec_list_push_tail(&nphi->srcs, &nsrc->node);
   }

   return nphi;
}

static nir_jump_instr *
clone_jump(clone_state *state, const nir_jump_instr *jmp)
{
   nir_jump_instr *njmp = nir_jump_instr_create(state->ns, jmp->type);

   return njmp;
}

static nir_call_instr *
clone_call(clone_state *state, const nir_call_instr *call)
{
   nir_function *ncallee = lookup_ptr(state, call->callee);
   nir_call_instr *ncall = nir_call_instr_create(state->ns, ncallee);

   for (unsigned i = 0; i < ncall->num_params; i++)
      ncall->params[i] = clone_deref_var(state, call->params[i], &ncall->instr);

   ncall->return_deref = clone_deref_var(state, call->return_deref,
                                         &ncall->instr);

   return ncall;
}

static nir_instr *
clone_instr(clone_state *state, const nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_alu:
      return &clone_alu(state, nir_instr_as_alu(instr))->instr;
   case nir_instr_type_intrinsic:
      return &clone_intrinsic(state, nir_instr_as_intrinsic(instr))->instr;
   case nir_instr_type_load_const:
      return &clone_load_const(state, nir_instr_as_load_const(instr))->instr;
   case nir_instr_type_ssa_undef:
      return &clone_ssa_undef(state, nir_instr_as_ssa_undef(instr))->instr;
   case nir_instr_type_tex:
      return &clone_tex(state, nir_instr_as_tex(instr))->instr;
   case nir_instr_type_phi:
      unreachable("Cannot clone phis with clone_instr");
   case nir_instr_type_jump:
      return &clone_jump(state, nir_instr_as_jump(instr))->instr;
   case nir_instr_type_call:
      return &clone_call(state, nir_instr_as_call(instr))->instr;
   case nir_instr_type_parallel_copy:
      unreachable("Cannot clone parallel copies");
   default:
      unreachable("bad instr type");
      return NULL;
   }
}

static nir_block *
clone_block(clone_state *state, struct exec_list *cf_list, const nir_block *blk)
{
   /* Don't actually create a new block.  Just use the one from the tail of
    * the list.  NIR guarantees that the tail of the list is a block and that
    * no two blocks are side-by-side in the IR;  It should be empty.
    */
   nir_block *nblk =
      exec_node_data(nir_block, exec_list_get_tail(cf_list), cf_node.node);
   assert(nblk->cf_node.type == nir_cf_node_block);
   assert(exec_list_is_empty(&nblk->instr_list));

   /* We need this for phi sources */
   store_ptr(state, nblk, blk);

   nir_foreach_instr(blk, instr) {
      if (instr->type == nir_instr_type_phi) {
         /* Phi instructions are a bit of a special case when cloning because
          * we don't want inserting the instruction to automatically handle
          * use/defs for us.  Instead, we need to wait until all the
          * blocks/instructions are in so that we can set their sources up.
          */
         clone_phi(state, nir_instr_as_phi(instr), nblk);
      } else {
         nir_instr *ninstr = clone_instr(state, instr);
         nir_instr_insert_after_block(nblk, ninstr);
      }
   }

   return nblk;
}

static void
clone_cf_list(clone_state *state, struct exec_list *dst,
              const struct exec_list *list);

static nir_if *
clone_if(clone_state *state, struct exec_list *cf_list, const nir_if *i)
{
   nir_if *ni = nir_if_create(state->ns);

   __clone_src(state, ni, &ni->condition, &i->condition);

   nir_cf_node_insert_end(cf_list, &ni->cf_node);

   clone_cf_list(state, &ni->then_list, &i->then_list);
   clone_cf_list(state, &ni->else_list, &i->else_list);

   return ni;
}

static nir_loop *
clone_loop(clone_state *state, struct exec_list *cf_list, const nir_loop *loop)
{
   nir_loop *nloop = nir_loop_create(state->ns);

   nir_cf_node_insert_end(cf_list, &nloop->cf_node);

   clone_cf_list(state, &nloop->body, &loop->body);

   return nloop;
}

/* clone list of nir_cf_node: */
static void
clone_cf_list(clone_state *state, struct exec_list *dst,
              const struct exec_list *list)
{
   foreach_list_typed(nir_cf_node, cf, node, list) {
      switch (cf->type) {
      case nir_cf_node_block:
         clone_block(state, dst, nir_cf_node_as_block(cf));
         break;
      case nir_cf_node_if:
         clone_if(state, dst, nir_cf_node_as_if(cf));
         break;
      case nir_cf_node_loop:
         clone_loop(state, dst, nir_cf_node_as_loop(cf));
         break;
      default:
         unreachable("bad cf type");
      }
   }
}

static nir_function_impl *
clone_function_impl(clone_state *state, const nir_function_impl *fi,
                    nir_function *nfxn)
{
   nir_function_impl *nfi = nir_function_impl_create(nfxn);

   clone_var_list(state, &nfi->locals, &fi->locals);
   clone_reg_list(state, &nfi->registers, &fi->registers);
   nfi->reg_alloc = fi->reg_alloc;

   nfi->num_params = fi->num_params;
   nfi->params = ralloc_array(state->ns, nir_variable *, fi->num_params);
   for (unsigned i = 0; i < fi->num_params; i++) {
      nfi->params[i] = lookup_ptr(state, fi->params[i]);
   }
   nfi->return_var = lookup_ptr(state, fi->return_var);

   assert(list_empty(&state->phi_srcs));

   clone_cf_list(state, &nfi->body, &fi->body);

   /* After we've cloned almost everything, we have to walk the list of phi
    * sources and fix them up.  Thanks to loops, the block and SSA value for a
    * phi source may not be defined when we first encounter it.  Instead, we
    * add it to the phi_srcs list and we fix it up here.
    */
   list_for_each_entry_safe(nir_phi_src, src, &state->phi_srcs, src.use_link) {
      src->pred = lookup_ptr(state, src->pred);
      assert(src->src.is_ssa);
      src->src.ssa = lookup_ptr(state, src->src.ssa);

      /* Remove from this list and place in the uses of the SSA def */
      list_del(&src->src.use_link);
      list_addtail(&src->src.use_link, &src->src.ssa->uses);
   }
   assert(list_empty(&state->phi_srcs));

   /* All metadata is invalidated in the cloning process */
   nfi->valid_metadata = 0;

   return nfi;
}

static nir_function *
clone_function(clone_state *state, const nir_function *fxn, nir_shader *ns)
{
   assert(ns == state->ns);
   nir_function *nfxn = nir_function_create(ns, fxn->name);

   /* Needed for call instructions */
   store_ptr(state, nfxn, fxn);

   nfxn->num_params = fxn->num_params;
   nfxn->params = ralloc_array(state->ns, nir_parameter, fxn->num_params);
   memcpy(nfxn->params, fxn->params, sizeof(nir_parameter) * fxn->num_params);

   nfxn->return_type = fxn->return_type;

   /* At first glance, it looks like we should clone the function_impl here.
    * However, call instructions need to be able to reference at least the
    * function and those will get processed as we clone the function_impl's.
    * We stop here and do function_impls as a second pass.
    */

   return nfxn;
}

nir_shader *
nir_shader_clone(void *mem_ctx, const nir_shader *s)
{
   clone_state state;
   init_clone_state(&state);

   nir_shader *ns = nir_shader_create(mem_ctx, s->stage, s->options);
   state.ns = ns;

   clone_var_list(&state, &ns->uniforms, &s->uniforms);
   clone_var_list(&state, &ns->inputs,   &s->inputs);
   clone_var_list(&state, &ns->outputs,  &s->outputs);
   clone_var_list(&state, &ns->globals,  &s->globals);
   clone_var_list(&state, &ns->system_values, &s->system_values);

   /* Go through and clone functions */
   foreach_list_typed(nir_function, fxn, node, &s->functions)
      clone_function(&state, fxn, ns);

   /* Only after all functions are cloned can we clone the actual function
    * implementations.  This is because nir_call_instr's need to reference the
    * functions of other functions and we don't know what order the functions
    * will have in the list.
    */
   nir_foreach_function(s, fxn) {
      nir_function *nfxn = lookup_ptr(&state, fxn);
      clone_function_impl(&state, fxn->impl, nfxn);
   }

   clone_reg_list(&state, &ns->registers, &s->registers);
   ns->reg_alloc = s->reg_alloc;

   ns->info = s->info;
   ns->info.name = ralloc_strdup(ns, ns->info.name);
   if (ns->info.label)
      ns->info.label = ralloc_strdup(ns, ns->info.label);

   ns->num_inputs = s->num_inputs;
   ns->num_uniforms = s->num_uniforms;
   ns->num_outputs = s->num_outputs;

   free_clone_state(&state);

   return ns;
}
