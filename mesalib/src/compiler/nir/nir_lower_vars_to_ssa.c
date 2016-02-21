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
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_vla.h"


struct deref_node {
   struct deref_node *parent;
   const struct glsl_type *type;

   bool lower_to_ssa;

   /* Only valid for things that end up in the direct list.
    * Note that multiple nir_deref_vars may correspond to this node, but they
    * will all be equivalent, so any is as good as the other.
    */
   nir_deref_var *deref;
   struct exec_node direct_derefs_link;

   struct set *loads;
   struct set *stores;
   struct set *copies;

   nir_ssa_def **def_stack;
   nir_ssa_def **def_stack_tail;

   struct deref_node *wildcard;
   struct deref_node *indirect;
   struct deref_node *children[0];
};

struct lower_variables_state {
   nir_shader *shader;
   void *dead_ctx;
   nir_function_impl *impl;

   /* A hash table mapping variables to deref_node data */
   struct hash_table *deref_var_nodes;

   /* A hash table mapping fully-qualified direct dereferences, i.e.
    * dereferences with no indirect or wildcard array dereferences, to
    * deref_node data.
    *
    * At the moment, we only lower loads, stores, and copies that can be
    * trivially lowered to loads and stores, i.e. copies with no indirects
    * and no wildcards.  If a part of a variable that is being loaded from
    * and/or stored into is also involved in a copy operation with
    * wildcards, then we lower that copy operation to loads and stores, but
    * otherwise we leave copies with wildcards alone. Since the only derefs
    * used in these loads, stores, and trivial copies are ones with no
    * wildcards and no indirects, these are precisely the derefs that we
    * can actually consider lowering.
    */
   struct exec_list direct_deref_nodes;

   /* Controls whether get_deref_node will add variables to the
    * direct_deref_nodes table.  This is turned on when we are initially
    * scanning for load/store instructions.  It is then turned off so we
    * don't accidentally change the direct_deref_nodes table while we're
    * iterating throug it.
    */
   bool add_to_direct_deref_nodes;

   /* A hash table mapping phi nodes to deref_state data */
   struct hash_table *phi_table;
};

static struct deref_node *
deref_node_create(struct deref_node *parent,
                  const struct glsl_type *type, nir_shader *shader)
{
   size_t size = sizeof(struct deref_node) +
                 glsl_get_length(type) * sizeof(struct deref_node *);

   struct deref_node *node = rzalloc_size(shader, size);
   node->type = type;
   node->parent = parent;
   node->deref = NULL;
   exec_node_init(&node->direct_derefs_link);

   return node;
}

/* Returns the deref node associated with the given variable.  This will be
 * the root of the tree representing all of the derefs of the given variable.
 */
static struct deref_node *
get_deref_node_for_var(nir_variable *var, struct lower_variables_state *state)
{
   struct deref_node *node;

   struct hash_entry *var_entry =
      _mesa_hash_table_search(state->deref_var_nodes, var);

   if (var_entry) {
      return var_entry->data;
   } else {
      node = deref_node_create(NULL, var->type, state->dead_ctx);
      _mesa_hash_table_insert(state->deref_var_nodes, var, node);
      return node;
   }
}

/* Gets the deref_node for the given deref chain and creates it if it
 * doesn't yet exist.  If the deref is fully-qualified and direct and
 * state->add_to_direct_deref_nodes is true, it will be added to the hash
 * table of of fully-qualified direct derefs.
 */
static struct deref_node *
get_deref_node(nir_deref_var *deref, struct lower_variables_state *state)
{
   bool is_direct = true;

   /* Start at the base of the chain. */
   struct deref_node *node = get_deref_node_for_var(deref->var, state);
   assert(deref->deref.type == node->type);

   for (nir_deref *tail = deref->deref.child; tail; tail = tail->child) {
      switch (tail->deref_type) {
      case nir_deref_type_struct: {
         nir_deref_struct *deref_struct = nir_deref_as_struct(tail);

         assert(deref_struct->index < glsl_get_length(node->type));

         if (node->children[deref_struct->index] == NULL)
            node->children[deref_struct->index] =
               deref_node_create(node, tail->type, state->dead_ctx);

         node = node->children[deref_struct->index];
         break;
      }

      case nir_deref_type_array: {
         nir_deref_array *arr = nir_deref_as_array(tail);

         switch (arr->deref_array_type) {
         case nir_deref_array_type_direct:
            /* This is possible if a loop unrolls and generates an
             * out-of-bounds offset.  We need to handle this at least
             * somewhat gracefully.
             */
            if (arr->base_offset >= glsl_get_length(node->type))
               return NULL;

            if (node->children[arr->base_offset] == NULL)
               node->children[arr->base_offset] =
                  deref_node_create(node, tail->type, state->dead_ctx);

            node = node->children[arr->base_offset];
            break;

         case nir_deref_array_type_indirect:
            if (node->indirect == NULL)
               node->indirect = deref_node_create(node, tail->type,
                                                  state->dead_ctx);

            node = node->indirect;
            is_direct = false;
            break;

         case nir_deref_array_type_wildcard:
            if (node->wildcard == NULL)
               node->wildcard = deref_node_create(node, tail->type,
                                                  state->dead_ctx);

            node = node->wildcard;
            is_direct = false;
            break;

         default:
            unreachable("Invalid array deref type");
         }
         break;
      }
      default:
         unreachable("Invalid deref type");
      }
   }

   assert(node);

   /* Only insert if it isn't already in the list. */
   if (is_direct && state->add_to_direct_deref_nodes &&
       node->direct_derefs_link.next == NULL) {
      node->deref = deref;
      assert(deref->var != NULL);
      exec_list_push_tail(&state->direct_deref_nodes,
                          &node->direct_derefs_link);
   }

   return node;
}

/* \sa foreach_deref_node_match */
static bool
foreach_deref_node_worker(struct deref_node *node, nir_deref *deref,
                          bool (* cb)(struct deref_node *node,
                                      struct lower_variables_state *state),
                          struct lower_variables_state *state)
{
   if (deref->child == NULL) {
      return cb(node, state);
   } else {
      switch (deref->child->deref_type) {
      case nir_deref_type_array: {
         nir_deref_array *arr = nir_deref_as_array(deref->child);
         assert(arr->deref_array_type == nir_deref_array_type_direct);
         if (node->children[arr->base_offset] &&
             !foreach_deref_node_worker(node->children[arr->base_offset],
                                        deref->child, cb, state))
            return false;

         if (node->wildcard &&
             !foreach_deref_node_worker(node->wildcard,
                                        deref->child, cb, state))
            return false;

         return true;
      }

      case nir_deref_type_struct: {
         nir_deref_struct *str = nir_deref_as_struct(deref->child);
         return foreach_deref_node_worker(node->children[str->index],
                                          deref->child, cb, state);
      }

      default:
         unreachable("Invalid deref child type");
      }
   }
}

/* Walks over every "matching" deref_node and calls the callback.  A node
 * is considered to "match" if either refers to that deref or matches up t
 * a wildcard.  In other words, the following would match a[6].foo[3].bar:
 *
 * a[6].foo[3].bar
 * a[*].foo[3].bar
 * a[6].foo[*].bar
 * a[*].foo[*].bar
 *
 * The given deref must be a full-length and fully qualified (no wildcards
 * or indirects) deref chain.
 */
static bool
foreach_deref_node_match(nir_deref_var *deref,
                         bool (* cb)(struct deref_node *node,
                                     struct lower_variables_state *state),
                         struct lower_variables_state *state)
{
   nir_deref_var var_deref = *deref;
   var_deref.deref.child = NULL;
   struct deref_node *node = get_deref_node(&var_deref, state);

   if (node == NULL)
      return false;

   return foreach_deref_node_worker(node, &deref->deref, cb, state);
}

/* \sa deref_may_be_aliased */
static bool
deref_may_be_aliased_node(struct deref_node *node, nir_deref *deref,
                          struct lower_variables_state *state)
{
   if (deref->child == NULL) {
      return false;
   } else {
      switch (deref->child->deref_type) {
      case nir_deref_type_array: {
         nir_deref_array *arr = nir_deref_as_array(deref->child);
         if (arr->deref_array_type == nir_deref_array_type_indirect)
            return true;

         /* If there is an indirect at this level, we're aliased. */
         if (node->indirect)
            return true;

         assert(arr->deref_array_type == nir_deref_array_type_direct);

         if (node->children[arr->base_offset] &&
             deref_may_be_aliased_node(node->children[arr->base_offset],
                                       deref->child, state))
            return true;

         if (node->wildcard &&
             deref_may_be_aliased_node(node->wildcard, deref->child, state))
            return true;

         return false;
      }

      case nir_deref_type_struct: {
         nir_deref_struct *str = nir_deref_as_struct(deref->child);
         if (node->children[str->index]) {
             return deref_may_be_aliased_node(node->children[str->index],
                                              deref->child, state);
         } else {
            return false;
         }
      }

      default:
         unreachable("Invalid nir_deref child type");
      }
   }
}

/* Returns true if there are no indirects that can ever touch this deref.
 *
 * For example, if the given deref is a[6].foo, then any uses of a[i].foo
 * would cause this to return false, but a[i].bar would not affect it
 * because it's a different structure member.  A var_copy involving of
 * a[*].bar also doesn't affect it because that can be lowered to entirely
 * direct load/stores.
 *
 * We only support asking this question about fully-qualified derefs.
 * Obviously, it's pointless to ask this about indirects, but we also
 * rule-out wildcards.  Handling Wildcard dereferences would involve
 * checking each array index to make sure that there aren't any indirect
 * references.
 */
static bool
deref_may_be_aliased(nir_deref_var *deref,
                     struct lower_variables_state *state)
{
   return deref_may_be_aliased_node(get_deref_node_for_var(deref->var, state),
                                    &deref->deref, state);
}

static void
register_load_instr(nir_intrinsic_instr *load_instr,
                    struct lower_variables_state *state)
{
   struct deref_node *node = get_deref_node(load_instr->variables[0], state);
   if (node == NULL)
      return;

   if (node->loads == NULL)
      node->loads = _mesa_set_create(state->dead_ctx, _mesa_hash_pointer,
                                     _mesa_key_pointer_equal);

   _mesa_set_add(node->loads, load_instr);
}

static void
register_store_instr(nir_intrinsic_instr *store_instr,
                     struct lower_variables_state *state)
{
   struct deref_node *node = get_deref_node(store_instr->variables[0], state);
   if (node == NULL)
      return;

   if (node->stores == NULL)
      node->stores = _mesa_set_create(state->dead_ctx, _mesa_hash_pointer,
                                     _mesa_key_pointer_equal);

   _mesa_set_add(node->stores, store_instr);
}

static void
register_copy_instr(nir_intrinsic_instr *copy_instr,
                    struct lower_variables_state *state)
{
   for (unsigned idx = 0; idx < 2; idx++) {
      struct deref_node *node =
         get_deref_node(copy_instr->variables[idx], state);

      if (node == NULL)
         continue;

      if (node->copies == NULL)
         node->copies = _mesa_set_create(state->dead_ctx, _mesa_hash_pointer,
                                         _mesa_key_pointer_equal);

      _mesa_set_add(node->copies, copy_instr);
   }
}

/* Registers all variable uses in the given block. */
static bool
register_variable_uses_block(nir_block *block, void *void_state)
{
   struct lower_variables_state *state = void_state;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_var:
         register_load_instr(intrin, state);
         break;

      case nir_intrinsic_store_var:
         register_store_instr(intrin, state);
         break;

      case nir_intrinsic_copy_var:
         register_copy_instr(intrin, state);
         break;

      default:
         continue;
      }
   }

   return true;
}

/* Walks over all of the copy instructions to or from the given deref_node
 * and lowers them to load/store intrinsics.
 */
static bool
lower_copies_to_load_store(struct deref_node *node,
                           struct lower_variables_state *state)
{
   if (!node->copies)
      return true;

   struct set_entry *copy_entry;
   set_foreach(node->copies, copy_entry) {
      nir_intrinsic_instr *copy = (void *)copy_entry->key;

      nir_lower_var_copy_instr(copy, state->shader);

      for (unsigned i = 0; i < 2; ++i) {
         struct deref_node *arg_node =
            get_deref_node(copy->variables[i], state);

         /* Only bother removing copy entries for other nodes */
         if (arg_node == NULL || arg_node == node)
            continue;

         struct set_entry *arg_entry = _mesa_set_search(arg_node->copies, copy);
         assert(arg_entry);
         _mesa_set_remove(node->copies, arg_entry);
      }

      nir_instr_remove(&copy->instr);
   }

   node->copies = NULL;

   return true;
}

/** Pushes an SSA def onto the def stack for the given node
 *
 * Each node is potentially associated with a stack of SSA definitions.
 * This stack is used for determining what SSA definition reaches a given
 * point in the program for variable renaming.  The stack is always kept in
 * dominance-order with at most one SSA def per block.  If the SSA
 * definition on the top of the stack is in the same block as the one being
 * pushed, the top element is replaced.
 */
static void
def_stack_push(struct deref_node *node, nir_ssa_def *def,
               struct lower_variables_state *state)
{
   if (node->def_stack == NULL) {
      node->def_stack = ralloc_array(state->dead_ctx, nir_ssa_def *,
                                     state->impl->num_blocks);
      node->def_stack_tail = node->def_stack - 1;
   }

   if (node->def_stack_tail >= node->def_stack) {
      nir_ssa_def *top_def = *node->def_stack_tail;

      if (def->parent_instr->block == top_def->parent_instr->block) {
         /* They're in the same block, just replace the top */
         *node->def_stack_tail = def;
         return;
      }
   }

   *(++node->def_stack_tail) = def;
}

/* Pop the top of the def stack if it's in the given block */
static void
def_stack_pop_if_in_block(struct deref_node *node, nir_block *block)
{
   /* If we're popping, then we have presumably pushed at some time in the
    * past so this should exist.
    */
   assert(node->def_stack != NULL);

   /* The stack is already empty.  Do nothing. */
   if (node->def_stack_tail < node->def_stack)
      return;

   nir_ssa_def *def = *node->def_stack_tail;
   if (def->parent_instr->block == block)
      node->def_stack_tail--;
}

/** Retrieves the SSA definition on the top of the stack for the given
 * node, if one exists.  If the stack is empty, then we return the constant
 * initializer (if it exists) or an SSA undef.
 */
static nir_ssa_def *
get_ssa_def_for_block(struct deref_node *node, nir_block *block,
                      struct lower_variables_state *state)
{
   /* If we have something on the stack, go ahead and return it.  We're
    * assuming that the top of the stack dominates the given block.
    */
   if (node->def_stack && node->def_stack_tail >= node->def_stack)
      return *node->def_stack_tail;

   /* If we got here then we don't have a definition that dominates the
    * given block.  This means that we need to add an undef and use that.
    */
   nir_ssa_undef_instr *undef =
      nir_ssa_undef_instr_create(state->shader,
                                 glsl_get_vector_elements(node->type));
   nir_instr_insert_before_cf_list(&state->impl->body, &undef->instr);
   def_stack_push(node, &undef->def, state);
   return &undef->def;
}

/* Given a block and one of its predecessors, this function fills in the
 * souces of the phi nodes to take SSA defs from the given predecessor.
 * This function must be called exactly once per block/predecessor pair.
 */
static void
add_phi_sources(nir_block *block, nir_block *pred,
                struct lower_variables_state *state)
{
   nir_foreach_instr(block, instr) {
      if (instr->type != nir_instr_type_phi)
         break;

      nir_phi_instr *phi = nir_instr_as_phi(instr);

      struct hash_entry *entry =
            _mesa_hash_table_search(state->phi_table, phi);
      if (!entry)
         continue;

      struct deref_node *node = entry->data;

      nir_phi_src *src = ralloc(phi, nir_phi_src);
      src->pred = pred;
      src->src.parent_instr = &phi->instr;
      src->src.is_ssa = true;
      src->src.ssa = get_ssa_def_for_block(node, pred, state);

      list_addtail(&src->src.use_link, &src->src.ssa->uses);

      exec_list_push_tail(&phi->srcs, &src->node);
   }
}

/* Performs variable renaming by doing a DFS of the dominance tree
 *
 * This algorithm is very similar to the one outlined in "Efficiently
 * Computing Static Single Assignment Form and the Control Dependence
 * Graph" by Cytron et. al.  The primary difference is that we only put one
 * SSA def on the stack per block.
 */
static bool
rename_variables_block(nir_block *block, struct lower_variables_state *state)
{
   nir_builder b;
   nir_builder_init(&b, state->impl);

   nir_foreach_instr_safe(block, instr) {
      if (instr->type == nir_instr_type_phi) {
         nir_phi_instr *phi = nir_instr_as_phi(instr);

         struct hash_entry *entry =
            _mesa_hash_table_search(state->phi_table, phi);

         /* This can happen if we already have phi nodes in the program
          * that were not created in this pass.
          */
         if (!entry)
            continue;

         struct deref_node *node = entry->data;

         def_stack_push(node, &phi->dest.ssa, state);
      } else if (instr->type == nir_instr_type_intrinsic) {
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         switch (intrin->intrinsic) {
         case nir_intrinsic_load_var: {
            struct deref_node *node =
               get_deref_node(intrin->variables[0], state);

            if (node == NULL) {
               /* If we hit this path then we are referencing an invalid
                * value.  Most likely, we unrolled something and are
                * reading past the end of some array.  In any case, this
                * should result in an undefined value.
                */
               nir_ssa_undef_instr *undef =
                  nir_ssa_undef_instr_create(state->shader,
                                             intrin->num_components);

               nir_instr_insert_before(&intrin->instr, &undef->instr);
               nir_instr_remove(&intrin->instr);

               nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                                        nir_src_for_ssa(&undef->def));
               continue;
            }

            if (!node->lower_to_ssa)
               continue;

            nir_alu_instr *mov = nir_alu_instr_create(state->shader,
                                                      nir_op_imov);
            mov->src[0].src.is_ssa = true;
            mov->src[0].src.ssa = get_ssa_def_for_block(node, block, state);
            for (unsigned i = intrin->num_components; i < 4; i++)
               mov->src[0].swizzle[i] = 0;

            assert(intrin->dest.is_ssa);

            mov->dest.write_mask = (1 << intrin->num_components) - 1;
            nir_ssa_dest_init(&mov->instr, &mov->dest.dest,
                              intrin->num_components, NULL);

            nir_instr_insert_before(&intrin->instr, &mov->instr);
            nir_instr_remove(&intrin->instr);

            nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                                     nir_src_for_ssa(&mov->dest.dest.ssa));
            break;
         }

         case nir_intrinsic_store_var: {
            struct deref_node *node =
               get_deref_node(intrin->variables[0], state);

            if (node == NULL) {
               /* Probably an out-of-bounds array store.  That should be a
                * no-op. */
               nir_instr_remove(&intrin->instr);
               continue;
            }

            if (!node->lower_to_ssa)
               continue;

            assert(intrin->num_components ==
                   glsl_get_vector_elements(node->type));

            assert(intrin->src[0].is_ssa);

            nir_ssa_def *new_def;
            b.cursor = nir_before_instr(&intrin->instr);

            unsigned wrmask = nir_intrinsic_write_mask(intrin);
            if (wrmask == (1 << intrin->num_components) - 1) {
               /* Whole variable store - just copy the source.  Note that
                * intrin->num_components and intrin->src[0].ssa->num_components
                * may differ.
                */
               unsigned swiz[4];
               for (unsigned i = 0; i < 4; i++)
                  swiz[i] = i < intrin->num_components ? i : 0;

               new_def = nir_swizzle(&b, intrin->src[0].ssa, swiz,
                                     intrin->num_components, false);
            } else {
               nir_ssa_def *old_def = get_ssa_def_for_block(node, block, state);
               /* For writemasked store_var intrinsics, we combine the newly
                * written values with the existing contents of unwritten
                * channels, creating a new SSA value for the whole vector.
                */
               nir_ssa_def *srcs[4];
               for (unsigned i = 0; i < intrin->num_components; i++) {
                  if (wrmask & (1 << i)) {
                     srcs[i] = nir_channel(&b, intrin->src[0].ssa, i);
                  } else {
                     srcs[i] = nir_channel(&b, old_def, i);
                  }
               }
               new_def = nir_vec(&b, srcs, intrin->num_components);
            }

            assert(new_def->num_components == intrin->num_components);

            def_stack_push(node, new_def, state);

            /* We'll wait to remove the instruction until the next pass
             * where we pop the node we just pushed back off the stack.
             */
            break;
         }

         default:
            break;
         }
      }
   }

   if (block->successors[0])
      add_phi_sources(block->successors[0], block, state);
   if (block->successors[1])
      add_phi_sources(block->successors[1], block, state);

   for (unsigned i = 0; i < block->num_dom_children; ++i)
      rename_variables_block(block->dom_children[i], state);

   /* Now we iterate over the instructions and pop off any SSA defs that we
    * pushed in the first loop.
    */
   nir_foreach_instr_safe(block, instr) {
      if (instr->type == nir_instr_type_phi) {
         nir_phi_instr *phi = nir_instr_as_phi(instr);

         struct hash_entry *entry =
            _mesa_hash_table_search(state->phi_table, phi);

         /* This can happen if we already have phi nodes in the program
          * that were not created in this pass.
          */
         if (!entry)
            continue;

         struct deref_node *node = entry->data;

         def_stack_pop_if_in_block(node, block);
      } else if (instr->type == nir_instr_type_intrinsic) {
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         if (intrin->intrinsic != nir_intrinsic_store_var)
            continue;

         struct deref_node *node = get_deref_node(intrin->variables[0], state);
         if (!node)
            continue;

         if (!node->lower_to_ssa)
            continue;

         def_stack_pop_if_in_block(node, block);
         nir_instr_remove(&intrin->instr);
      }
   }

   return true;
}

/* Inserts phi nodes for all variables marked lower_to_ssa
 *
 * This is the same algorithm as presented in "Efficiently Computing Static
 * Single Assignment Form and the Control Dependence Graph" by Cytron et.
 * al.
 */
static void
insert_phi_nodes(struct lower_variables_state *state)
{
   NIR_VLA_ZERO(unsigned, work, state->impl->num_blocks);
   NIR_VLA_ZERO(unsigned, has_already, state->impl->num_blocks);

   /*
    * Since the work flags already prevent us from inserting a node that has
    * ever been inserted into W, we don't need to use a set to represent W.
    * Also, since no block can ever be inserted into W more than once, we know
    * that the maximum size of W is the number of basic blocks in the
    * function. So all we need to handle W is an array and a pointer to the
    * next element to be inserted and the next element to be removed.
    */
   NIR_VLA(nir_block *, W, state->impl->num_blocks);

   unsigned w_start, w_end;
   unsigned iter_count = 0;

   foreach_list_typed(struct deref_node, node, direct_derefs_link,
                      &state->direct_deref_nodes) {
      if (node->stores == NULL)
         continue;

      if (!node->lower_to_ssa)
         continue;

      w_start = w_end = 0;
      iter_count++;

      struct set_entry *store_entry;
      set_foreach(node->stores, store_entry) {
         nir_intrinsic_instr *store = (nir_intrinsic_instr *)store_entry->key;
         if (work[store->instr.block->index] < iter_count)
            W[w_end++] = store->instr.block;
         work[store->instr.block->index] = iter_count;
      }

      while (w_start != w_end) {
         nir_block *cur = W[w_start++];
         struct set_entry *dom_entry;
         set_foreach(cur->dom_frontier, dom_entry) {
            nir_block *next = (nir_block *) dom_entry->key;

            /*
             * If there's more than one return statement, then the end block
             * can be a join point for some definitions. However, there are
             * no instructions in the end block, so nothing would use those
             * phi nodes. Of course, we couldn't place those phi nodes
             * anyways due to the restriction of having no instructions in the
             * end block...
             */
            if (next == state->impl->end_block)
               continue;

            if (has_already[next->index] < iter_count) {
               nir_phi_instr *phi = nir_phi_instr_create(state->shader);
               nir_ssa_dest_init(&phi->instr, &phi->dest,
                                 glsl_get_vector_elements(node->type), NULL);
               nir_instr_insert_before_block(next, &phi->instr);

               _mesa_hash_table_insert(state->phi_table, phi, node);

               has_already[next->index] = iter_count;
               if (work[next->index] < iter_count) {
                  work[next->index] = iter_count;
                  W[w_end++] = next;
               }
            }
         }
      }
   }
}


/** Implements a pass to lower variable uses to SSA values
 *
 * This path walks the list of instructions and tries to lower as many
 * local variable load/store operations to SSA defs and uses as it can.
 * The process involves four passes:
 *
 *  1) Iterate over all of the instructions and mark where each local
 *     variable deref is used in a load, store, or copy.  While we're at
 *     it, we keep track of all of the fully-qualified (no wildcards) and
 *     fully-direct references we see and store them in the
 *     direct_deref_nodes hash table.
 *
 *  2) Walk over the the list of fully-qualified direct derefs generated in
 *     the previous pass.  For each deref, we determine if it can ever be
 *     aliased, i.e. if there is an indirect reference anywhere that may
 *     refer to it.  If it cannot be aliased, we mark it for lowering to an
 *     SSA value.  At this point, we lower any var_copy instructions that
 *     use the given deref to load/store operations and, if the deref has a
 *     constant initializer, we go ahead and add a load_const value at the
 *     beginning of the function with the initialized value.
 *
 *  3) Walk over the list of derefs we plan to lower to SSA values and
 *     insert phi nodes as needed.
 *
 *  4) Perform "variable renaming" by replacing the load/store instructions
 *     with SSA definitions and SSA uses.
 */
static bool
nir_lower_vars_to_ssa_impl(nir_function_impl *impl)
{
   struct lower_variables_state state;

   state.shader = impl->function->shader;
   state.dead_ctx = ralloc_context(state.shader);
   state.impl = impl;

   state.deref_var_nodes = _mesa_hash_table_create(state.dead_ctx,
                                                   _mesa_hash_pointer,
                                                   _mesa_key_pointer_equal);
   exec_list_make_empty(&state.direct_deref_nodes);
   state.phi_table = _mesa_hash_table_create(state.dead_ctx,
                                             _mesa_hash_pointer,
                                             _mesa_key_pointer_equal);

   /* Build the initial deref structures and direct_deref_nodes table */
   state.add_to_direct_deref_nodes = true;
   nir_foreach_block(impl, register_variable_uses_block, &state);

   bool progress = false;

   nir_metadata_require(impl, nir_metadata_block_index);

   /* We're about to iterate through direct_deref_nodes.  Don't modify it. */
   state.add_to_direct_deref_nodes = false;

   foreach_list_typed_safe(struct deref_node, node, direct_derefs_link,
                           &state.direct_deref_nodes) {
      nir_deref_var *deref = node->deref;

      if (deref->var->data.mode != nir_var_local) {
         exec_node_remove(&node->direct_derefs_link);
         continue;
      }

      if (deref_may_be_aliased(deref, &state)) {
         exec_node_remove(&node->direct_derefs_link);
         continue;
      }

      node->lower_to_ssa = true;
      progress = true;

      if (deref->var->constant_initializer) {
         nir_load_const_instr *load =
            nir_deref_get_const_initializer_load(state.shader, deref);
         nir_ssa_def_init(&load->instr, &load->def,
                          glsl_get_vector_elements(node->type), NULL);
         nir_instr_insert_before_cf_list(&impl->body, &load->instr);
         def_stack_push(node, &load->def, &state);
      }

      foreach_deref_node_match(deref, lower_copies_to_load_store, &state);
   }

   if (!progress)
      return false;

   nir_metadata_require(impl, nir_metadata_dominance);

   /* We may have lowered some copy instructions to load/store
    * instructions.  The uses from the copy instructions hav already been
    * removed but we need to rescan to ensure that the uses from the newly
    * added load/store instructions are registered.  We need this
    * information for phi node insertion below.
    */
   nir_foreach_block(impl, register_variable_uses_block, &state);

   insert_phi_nodes(&state);
   rename_variables_block(nir_start_block(impl), &state);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);

   ralloc_free(state.dead_ctx);

   return progress;
}

void
nir_lower_vars_to_ssa(nir_shader *shader)
{
   nir_foreach_function(shader, function) {
      if (function->impl)
         nir_lower_vars_to_ssa_impl(function->impl);
   }
}
