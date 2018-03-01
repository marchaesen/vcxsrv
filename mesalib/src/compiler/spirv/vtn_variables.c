/*
 * Copyright © 2015 Intel Corporation
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

#include "vtn_private.h"
#include "spirv_info.h"

static struct vtn_access_chain *
vtn_access_chain_create(struct vtn_builder *b, unsigned length)
{
   struct vtn_access_chain *chain;

   /* Subtract 1 from the length since there's already one built in */
   size_t size = sizeof(*chain) +
                 (MAX2(length, 1) - 1) * sizeof(chain->link[0]);
   chain = rzalloc_size(b, size);
   chain->length = length;

   return chain;
}

static struct vtn_access_chain *
vtn_access_chain_extend(struct vtn_builder *b, struct vtn_access_chain *old,
                        unsigned new_ids)
{
   struct vtn_access_chain *chain;

   unsigned old_len = old ? old->length : 0;
   chain = vtn_access_chain_create(b, old_len + new_ids);

   for (unsigned i = 0; i < old_len; i++)
      chain->link[i] = old->link[i];

   return chain;
}

static bool
vtn_pointer_uses_ssa_offset(struct vtn_builder *b,
                            struct vtn_pointer *ptr)
{
   return ptr->mode == vtn_variable_mode_ubo ||
          ptr->mode == vtn_variable_mode_ssbo ||
          (ptr->mode == vtn_variable_mode_workgroup &&
           b->options->lower_workgroup_access_to_offsets);
}

static bool
vtn_pointer_is_external_block(struct vtn_builder *b,
                              struct vtn_pointer *ptr)
{
   return ptr->mode == vtn_variable_mode_ssbo ||
          ptr->mode == vtn_variable_mode_ubo ||
          ptr->mode == vtn_variable_mode_push_constant ||
          (ptr->mode == vtn_variable_mode_workgroup &&
           b->options->lower_workgroup_access_to_offsets);
}

/* Dereference the given base pointer by the access chain */
static struct vtn_pointer *
vtn_access_chain_pointer_dereference(struct vtn_builder *b,
                                     struct vtn_pointer *base,
                                     struct vtn_access_chain *deref_chain)
{
   struct vtn_access_chain *chain =
      vtn_access_chain_extend(b, base->chain, deref_chain->length);
   struct vtn_type *type = base->type;

   /* OpPtrAccessChain is only allowed on things which support variable
    * pointers.  For everything else, the client is expected to just pass us
    * the right access chain.
    */
   vtn_assert(!deref_chain->ptr_as_array);

   unsigned start = base->chain ? base->chain->length : 0;
   for (unsigned i = 0; i < deref_chain->length; i++) {
      chain->link[start + i] = deref_chain->link[i];

      if (glsl_type_is_struct(type->type)) {
         vtn_assert(deref_chain->link[i].mode == vtn_access_mode_literal);
         type = type->members[deref_chain->link[i].id];
      } else {
         type = type->array_element;
      }
   }

   struct vtn_pointer *ptr = rzalloc(b, struct vtn_pointer);
   ptr->mode = base->mode;
   ptr->type = type;
   ptr->var = base->var;
   ptr->chain = chain;

   return ptr;
}

static nir_ssa_def *
vtn_access_link_as_ssa(struct vtn_builder *b, struct vtn_access_link link,
                       unsigned stride)
{
   vtn_assert(stride > 0);
   if (link.mode == vtn_access_mode_literal) {
      return nir_imm_int(&b->nb, link.id * stride);
   } else if (stride == 1) {
       nir_ssa_def *ssa = vtn_ssa_value(b, link.id)->def;
       if (ssa->bit_size != 32)
          ssa = nir_u2u32(&b->nb, ssa);
      return ssa;
   } else {
      nir_ssa_def *src0 = vtn_ssa_value(b, link.id)->def;
      if (src0->bit_size != 32)
         src0 = nir_u2u32(&b->nb, src0);
      return nir_imul(&b->nb, src0, nir_imm_int(&b->nb, stride));
   }
}

static nir_ssa_def *
vtn_variable_resource_index(struct vtn_builder *b, struct vtn_variable *var,
                            nir_ssa_def *desc_array_index)
{
   if (!desc_array_index) {
      vtn_assert(glsl_type_is_struct(var->type->type));
      desc_array_index = nir_imm_int(&b->nb, 0);
   }

   nir_intrinsic_instr *instr =
      nir_intrinsic_instr_create(b->nb.shader,
                                 nir_intrinsic_vulkan_resource_index);
   instr->src[0] = nir_src_for_ssa(desc_array_index);
   nir_intrinsic_set_desc_set(instr, var->descriptor_set);
   nir_intrinsic_set_binding(instr, var->binding);

   nir_ssa_dest_init(&instr->instr, &instr->dest, 1, 32, NULL);
   nir_builder_instr_insert(&b->nb, &instr->instr);

   return &instr->dest.ssa;
}

static nir_ssa_def *
vtn_resource_reindex(struct vtn_builder *b, nir_ssa_def *base_index,
                     nir_ssa_def *offset_index)
{
   nir_intrinsic_instr *instr =
      nir_intrinsic_instr_create(b->nb.shader,
                                 nir_intrinsic_vulkan_resource_reindex);
   instr->src[0] = nir_src_for_ssa(base_index);
   instr->src[1] = nir_src_for_ssa(offset_index);

   nir_ssa_dest_init(&instr->instr, &instr->dest, 1, 32, NULL);
   nir_builder_instr_insert(&b->nb, &instr->instr);

   return &instr->dest.ssa;
}

static struct vtn_pointer *
vtn_ssa_offset_pointer_dereference(struct vtn_builder *b,
                                   struct vtn_pointer *base,
                                   struct vtn_access_chain *deref_chain)
{
   nir_ssa_def *block_index = base->block_index;
   nir_ssa_def *offset = base->offset;
   struct vtn_type *type = base->type;

   unsigned idx = 0;
   if (base->mode == vtn_variable_mode_ubo ||
       base->mode == vtn_variable_mode_ssbo) {
      if (!block_index) {
         vtn_assert(base->var && base->type);
         nir_ssa_def *desc_arr_idx;
         if (glsl_type_is_array(type->type)) {
            if (deref_chain->length >= 1) {
               desc_arr_idx =
                  vtn_access_link_as_ssa(b, deref_chain->link[0], 1);
               idx++;
               /* This consumes a level of type */
               type = type->array_element;
            } else {
               /* This is annoying.  We've been asked for a pointer to the
                * array of UBOs/SSBOs and not a specifc buffer.  Return a
                * pointer with a descriptor index of 0 and we'll have to do
                * a reindex later to adjust it to the right thing.
                */
               desc_arr_idx = nir_imm_int(&b->nb, 0);
            }
         } else if (deref_chain->ptr_as_array) {
            /* You can't have a zero-length OpPtrAccessChain */
            vtn_assert(deref_chain->length >= 1);
            desc_arr_idx = vtn_access_link_as_ssa(b, deref_chain->link[0], 1);
         } else {
            /* We have a regular non-array SSBO. */
            desc_arr_idx = NULL;
         }
         block_index = vtn_variable_resource_index(b, base->var, desc_arr_idx);
      } else if (deref_chain->ptr_as_array &&
                 type->base_type == vtn_base_type_struct && type->block) {
         /* We are doing an OpPtrAccessChain on a pointer to a struct that is
          * decorated block.  This is an interesting corner in the SPIR-V
          * spec.  One interpretation would be that they client is clearly
          * trying to treat that block as if it's an implicit array of blocks
          * repeated in the buffer.  However, the SPIR-V spec for the
          * OpPtrAccessChain says:
          *
          *    "Base is treated as the address of the first element of an
          *    array, and the Element element’s address is computed to be the
          *    base for the Indexes, as per OpAccessChain."
          *
          * Taken literally, that would mean that your struct type is supposed
          * to be treated as an array of such a struct and, since it's
          * decorated block, that means an array of blocks which corresponds
          * to an array descriptor.  Therefore, we need to do a reindex
          * operation to add the index from the first link in the access chain
          * to the index we recieved.
          *
          * The downside to this interpretation (there always is one) is that
          * this might be somewhat surprising behavior to apps if they expect
          * the implicit array behavior described above.
          */
         vtn_assert(deref_chain->length >= 1);
         nir_ssa_def *offset_index =
            vtn_access_link_as_ssa(b, deref_chain->link[0], 1);
         idx++;

         block_index = vtn_resource_reindex(b, block_index, offset_index);
      }
   }

   if (!offset) {
      if (base->mode == vtn_variable_mode_workgroup) {
         /* SLM doesn't need nor have a block index */
         vtn_assert(!block_index);

         /* We need the variable for the base offset */
         vtn_assert(base->var);

         /* We need ptr_type for size and alignment */
         vtn_assert(base->ptr_type);

         /* Assign location on first use so that we don't end up bloating SLM
          * address space for variables which are never statically used.
          */
         if (base->var->shared_location < 0) {
            vtn_assert(base->ptr_type->length > 0 && base->ptr_type->align > 0);
            b->shader->num_shared = vtn_align_u32(b->shader->num_shared,
                                                  base->ptr_type->align);
            base->var->shared_location = b->shader->num_shared;
            b->shader->num_shared += base->ptr_type->length;
         }

         offset = nir_imm_int(&b->nb, base->var->shared_location);
      } else {
         /* The code above should have ensured a block_index when needed. */
         vtn_assert(block_index);

         /* Start off with at the start of the buffer. */
         offset = nir_imm_int(&b->nb, 0);
      }
   }

   if (deref_chain->ptr_as_array && idx == 0) {
      /* We need ptr_type for the stride */
      vtn_assert(base->ptr_type);

      /* We need at least one element in the chain */
      vtn_assert(deref_chain->length >= 1);

      nir_ssa_def *elem_offset =
         vtn_access_link_as_ssa(b, deref_chain->link[idx],
                                base->ptr_type->stride);
      offset = nir_iadd(&b->nb, offset, elem_offset);
      idx++;
   }

   for (; idx < deref_chain->length; idx++) {
      switch (glsl_get_base_type(type->type)) {
      case GLSL_TYPE_UINT:
      case GLSL_TYPE_INT:
      case GLSL_TYPE_UINT16:
      case GLSL_TYPE_INT16:
      case GLSL_TYPE_UINT64:
      case GLSL_TYPE_INT64:
      case GLSL_TYPE_FLOAT:
      case GLSL_TYPE_FLOAT16:
      case GLSL_TYPE_DOUBLE:
      case GLSL_TYPE_BOOL:
      case GLSL_TYPE_ARRAY: {
         nir_ssa_def *elem_offset =
            vtn_access_link_as_ssa(b, deref_chain->link[idx], type->stride);
         offset = nir_iadd(&b->nb, offset, elem_offset);
         type = type->array_element;
         break;
      }

      case GLSL_TYPE_STRUCT: {
         vtn_assert(deref_chain->link[idx].mode == vtn_access_mode_literal);
         unsigned member = deref_chain->link[idx].id;
         nir_ssa_def *mem_offset = nir_imm_int(&b->nb, type->offsets[member]);
         offset = nir_iadd(&b->nb, offset, mem_offset);
         type = type->members[member];
         break;
      }

      default:
         vtn_fail("Invalid type for deref");
      }
   }

   struct vtn_pointer *ptr = rzalloc(b, struct vtn_pointer);
   ptr->mode = base->mode;
   ptr->type = type;
   ptr->block_index = block_index;
   ptr->offset = offset;

   return ptr;
}

/* Dereference the given base pointer by the access chain */
static struct vtn_pointer *
vtn_pointer_dereference(struct vtn_builder *b,
                        struct vtn_pointer *base,
                        struct vtn_access_chain *deref_chain)
{
   if (vtn_pointer_uses_ssa_offset(b, base)) {
      return vtn_ssa_offset_pointer_dereference(b, base, deref_chain);
   } else {
      return vtn_access_chain_pointer_dereference(b, base, deref_chain);
   }
}

/* Crawls a chain of array derefs and rewrites the types so that the
 * lengths stay the same but the terminal type is the one given by
 * tail_type.  This is useful for split structures.
 */
static void
rewrite_deref_types(struct vtn_builder *b, nir_deref *deref,
                    const struct glsl_type *type)
{
   deref->type = type;
   if (deref->child) {
      vtn_assert(deref->child->deref_type == nir_deref_type_array);
      vtn_assert(glsl_type_is_array(deref->type));
      rewrite_deref_types(b, deref->child, glsl_get_array_element(type));
   }
}

struct vtn_pointer *
vtn_pointer_for_variable(struct vtn_builder *b,
                         struct vtn_variable *var, struct vtn_type *ptr_type)
{
   struct vtn_pointer *pointer = rzalloc(b, struct vtn_pointer);

   pointer->mode = var->mode;
   pointer->type = var->type;
   vtn_assert(ptr_type->base_type == vtn_base_type_pointer);
   vtn_assert(ptr_type->deref->type == var->type->type);
   pointer->ptr_type = ptr_type;
   pointer->var = var;

   return pointer;
}

nir_deref_var *
vtn_pointer_to_deref(struct vtn_builder *b, struct vtn_pointer *ptr)
{
   /* Do on-the-fly copy propagation for samplers. */
   if (ptr->var->copy_prop_sampler)
      return vtn_pointer_to_deref(b, ptr->var->copy_prop_sampler);

   nir_deref_var *deref_var;
   if (ptr->var->var) {
      deref_var = nir_deref_var_create(b, ptr->var->var);
      /* Raw variable access */
      if (!ptr->chain)
         return deref_var;
   } else {
      vtn_assert(ptr->var->members);
      /* Create the deref_var manually.  It will get filled out later. */
      deref_var = rzalloc(b, nir_deref_var);
      deref_var->deref.deref_type = nir_deref_type_var;
   }

   struct vtn_access_chain *chain = ptr->chain;
   vtn_assert(chain);

   struct vtn_type *deref_type = ptr->var->type;
   nir_deref *tail = &deref_var->deref;
   nir_variable **members = ptr->var->members;

   for (unsigned i = 0; i < chain->length; i++) {
      enum glsl_base_type base_type = glsl_get_base_type(deref_type->type);
      switch (base_type) {
      case GLSL_TYPE_UINT:
      case GLSL_TYPE_INT:
      case GLSL_TYPE_UINT16:
      case GLSL_TYPE_INT16:
      case GLSL_TYPE_UINT64:
      case GLSL_TYPE_INT64:
      case GLSL_TYPE_FLOAT:
      case GLSL_TYPE_FLOAT16:
      case GLSL_TYPE_DOUBLE:
      case GLSL_TYPE_BOOL:
      case GLSL_TYPE_ARRAY: {
         deref_type = deref_type->array_element;

         nir_deref_array *deref_arr = nir_deref_array_create(b);
         deref_arr->deref.type = deref_type->type;

         if (chain->link[i].mode == vtn_access_mode_literal) {
            deref_arr->deref_array_type = nir_deref_array_type_direct;
            deref_arr->base_offset = chain->link[i].id;
         } else {
            vtn_assert(chain->link[i].mode == vtn_access_mode_id);
            deref_arr->deref_array_type = nir_deref_array_type_indirect;
            deref_arr->base_offset = 0;
            deref_arr->indirect =
               nir_src_for_ssa(vtn_ssa_value(b, chain->link[i].id)->def);
         }
         tail->child = &deref_arr->deref;
         tail = tail->child;
         break;
      }

      case GLSL_TYPE_STRUCT: {
         vtn_assert(chain->link[i].mode == vtn_access_mode_literal);
         unsigned idx = chain->link[i].id;
         deref_type = deref_type->members[idx];
         if (members) {
            /* This is a pre-split structure. */
            deref_var->var = members[idx];
            rewrite_deref_types(b, &deref_var->deref, members[idx]->type);
            vtn_assert(tail->type == deref_type->type);
            members = NULL;
         } else {
            nir_deref_struct *deref_struct = nir_deref_struct_create(b, idx);
            deref_struct->deref.type = deref_type->type;
            tail->child = &deref_struct->deref;
            tail = tail->child;
         }
         break;
      }
      default:
         vtn_fail("Invalid type for deref");
      }
   }

   vtn_assert(members == NULL);
   return deref_var;
}

static void
_vtn_local_load_store(struct vtn_builder *b, bool load, nir_deref_var *deref,
                      nir_deref *tail, struct vtn_ssa_value *inout)
{
   /* The deref tail may contain a deref to select a component of a vector (in
    * other words, it might not be an actual tail) so we have to save it away
    * here since we overwrite it later.
    */
   nir_deref *old_child = tail->child;

   if (glsl_type_is_vector_or_scalar(tail->type)) {
      /* Terminate the deref chain in case there is one more link to pick
       * off a component of the vector.
       */
      tail->child = NULL;

      nir_intrinsic_op op = load ? nir_intrinsic_load_var :
                                   nir_intrinsic_store_var;

      nir_intrinsic_instr *intrin = nir_intrinsic_instr_create(b->shader, op);
      intrin->variables[0] = nir_deref_var_clone(deref, intrin);
      intrin->num_components = glsl_get_vector_elements(tail->type);

      if (load) {
         nir_ssa_dest_init(&intrin->instr, &intrin->dest,
                           intrin->num_components,
                           glsl_get_bit_size(tail->type),
                           NULL);
         inout->def = &intrin->dest.ssa;
      } else {
         nir_intrinsic_set_write_mask(intrin, (1 << intrin->num_components) - 1);
         intrin->src[0] = nir_src_for_ssa(inout->def);
      }

      nir_builder_instr_insert(&b->nb, &intrin->instr);
   } else if (glsl_get_base_type(tail->type) == GLSL_TYPE_ARRAY ||
              glsl_type_is_matrix(tail->type)) {
      unsigned elems = glsl_get_length(tail->type);
      nir_deref_array *deref_arr = nir_deref_array_create(b);
      deref_arr->deref_array_type = nir_deref_array_type_direct;
      deref_arr->deref.type = glsl_get_array_element(tail->type);
      tail->child = &deref_arr->deref;
      for (unsigned i = 0; i < elems; i++) {
         deref_arr->base_offset = i;
         _vtn_local_load_store(b, load, deref, tail->child, inout->elems[i]);
      }
   } else {
      vtn_assert(glsl_get_base_type(tail->type) == GLSL_TYPE_STRUCT);
      unsigned elems = glsl_get_length(tail->type);
      nir_deref_struct *deref_struct = nir_deref_struct_create(b, 0);
      tail->child = &deref_struct->deref;
      for (unsigned i = 0; i < elems; i++) {
         deref_struct->index = i;
         deref_struct->deref.type = glsl_get_struct_field(tail->type, i);
         _vtn_local_load_store(b, load, deref, tail->child, inout->elems[i]);
      }
   }

   tail->child = old_child;
}

nir_deref_var *
vtn_nir_deref(struct vtn_builder *b, uint32_t id)
{
   struct vtn_pointer *ptr = vtn_value(b, id, vtn_value_type_pointer)->pointer;
   return vtn_pointer_to_deref(b, ptr);
}

/*
 * Gets the NIR-level deref tail, which may have as a child an array deref
 * selecting which component due to OpAccessChain supporting per-component
 * indexing in SPIR-V.
 */
static nir_deref *
get_deref_tail(nir_deref_var *deref)
{
   nir_deref *cur = &deref->deref;
   while (!glsl_type_is_vector_or_scalar(cur->type) && cur->child)
      cur = cur->child;

   return cur;
}

struct vtn_ssa_value *
vtn_local_load(struct vtn_builder *b, nir_deref_var *src)
{
   nir_deref *src_tail = get_deref_tail(src);
   struct vtn_ssa_value *val = vtn_create_ssa_value(b, src_tail->type);
   _vtn_local_load_store(b, true, src, src_tail, val);

   if (src_tail->child) {
      nir_deref_array *vec_deref = nir_deref_as_array(src_tail->child);
      vtn_assert(vec_deref->deref.child == NULL);
      val->type = vec_deref->deref.type;
      if (vec_deref->deref_array_type == nir_deref_array_type_direct)
         val->def = vtn_vector_extract(b, val->def, vec_deref->base_offset);
      else
         val->def = vtn_vector_extract_dynamic(b, val->def,
                                               vec_deref->indirect.ssa);
   }

   return val;
}

void
vtn_local_store(struct vtn_builder *b, struct vtn_ssa_value *src,
                nir_deref_var *dest)
{
   nir_deref *dest_tail = get_deref_tail(dest);

   if (dest_tail->child) {
      struct vtn_ssa_value *val = vtn_create_ssa_value(b, dest_tail->type);
      _vtn_local_load_store(b, true, dest, dest_tail, val);
      nir_deref_array *deref = nir_deref_as_array(dest_tail->child);
      vtn_assert(deref->deref.child == NULL);
      if (deref->deref_array_type == nir_deref_array_type_direct)
         val->def = vtn_vector_insert(b, val->def, src->def,
                                      deref->base_offset);
      else
         val->def = vtn_vector_insert_dynamic(b, val->def, src->def,
                                              deref->indirect.ssa);
      _vtn_local_load_store(b, false, dest, dest_tail, val);
   } else {
      _vtn_local_load_store(b, false, dest, dest_tail, src);
   }
}

nir_ssa_def *
vtn_pointer_to_offset(struct vtn_builder *b, struct vtn_pointer *ptr,
                      nir_ssa_def **index_out, unsigned *end_idx_out)
{
   if (vtn_pointer_uses_ssa_offset(b, ptr)) {
      if (!ptr->offset) {
         struct vtn_access_chain chain = {
            .length = 0,
         };
         ptr = vtn_ssa_offset_pointer_dereference(b, ptr, &chain);
      }
      *index_out = ptr->block_index;
      return ptr->offset;
   }

   vtn_assert(ptr->mode == vtn_variable_mode_push_constant);
   *index_out = NULL;

   unsigned idx = 0;
   struct vtn_type *type = ptr->var->type;
   nir_ssa_def *offset = nir_imm_int(&b->nb, 0);

   if (ptr->chain) {
      for (; idx < ptr->chain->length; idx++) {
         enum glsl_base_type base_type = glsl_get_base_type(type->type);
         switch (base_type) {
         case GLSL_TYPE_UINT:
         case GLSL_TYPE_INT:
         case GLSL_TYPE_UINT16:
         case GLSL_TYPE_INT16:
         case GLSL_TYPE_UINT64:
         case GLSL_TYPE_INT64:
         case GLSL_TYPE_FLOAT:
         case GLSL_TYPE_FLOAT16:
         case GLSL_TYPE_DOUBLE:
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_ARRAY:
            offset = nir_iadd(&b->nb, offset,
                              vtn_access_link_as_ssa(b, ptr->chain->link[idx],
                                                     type->stride));

            type = type->array_element;
            break;

         case GLSL_TYPE_STRUCT: {
            vtn_assert(ptr->chain->link[idx].mode == vtn_access_mode_literal);
            unsigned member = ptr->chain->link[idx].id;
            offset = nir_iadd(&b->nb, offset,
                              nir_imm_int(&b->nb, type->offsets[member]));
            type = type->members[member];
            break;
         }

         default:
            vtn_fail("Invalid type for deref");
         }
      }
   }

   vtn_assert(type == ptr->type);
   if (end_idx_out)
      *end_idx_out = idx;

   return offset;
}

/* Tries to compute the size of an interface block based on the strides and
 * offsets that are provided to us in the SPIR-V source.
 */
static unsigned
vtn_type_block_size(struct vtn_builder *b, struct vtn_type *type)
{
   enum glsl_base_type base_type = glsl_get_base_type(type->type);
   switch (base_type) {
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_INT:
   case GLSL_TYPE_UINT16:
   case GLSL_TYPE_INT16:
   case GLSL_TYPE_UINT64:
   case GLSL_TYPE_INT64:
   case GLSL_TYPE_FLOAT:
   case GLSL_TYPE_FLOAT16:
   case GLSL_TYPE_BOOL:
   case GLSL_TYPE_DOUBLE: {
      unsigned cols = type->row_major ? glsl_get_vector_elements(type->type) :
                                        glsl_get_matrix_columns(type->type);
      if (cols > 1) {
         vtn_assert(type->stride > 0);
         return type->stride * cols;
      } else {
         unsigned type_size = glsl_get_bit_size(type->type) / 8;
         return glsl_get_vector_elements(type->type) * type_size;
      }
   }

   case GLSL_TYPE_STRUCT:
   case GLSL_TYPE_INTERFACE: {
      unsigned size = 0;
      unsigned num_fields = glsl_get_length(type->type);
      for (unsigned f = 0; f < num_fields; f++) {
         unsigned field_end = type->offsets[f] +
                              vtn_type_block_size(b, type->members[f]);
         size = MAX2(size, field_end);
      }
      return size;
   }

   case GLSL_TYPE_ARRAY:
      vtn_assert(type->stride > 0);
      vtn_assert(glsl_get_length(type->type) > 0);
      return type->stride * glsl_get_length(type->type);

   default:
      vtn_fail("Invalid block type");
      return 0;
   }
}

static void
vtn_access_chain_get_offset_size(struct vtn_builder *b,
                                 struct vtn_access_chain *chain,
                                 struct vtn_type *type,
                                 unsigned *access_offset,
                                 unsigned *access_size)
{
   *access_offset = 0;

   for (unsigned i = 0; i < chain->length; i++) {
      if (chain->link[i].mode != vtn_access_mode_literal)
         break;

      if (glsl_type_is_struct(type->type)) {
         *access_offset += type->offsets[chain->link[i].id];
         type = type->members[chain->link[i].id];
      } else {
         *access_offset += type->stride * chain->link[i].id;
         type = type->array_element;
      }
   }

   *access_size = vtn_type_block_size(b, type);
}

static void
_vtn_load_store_tail(struct vtn_builder *b, nir_intrinsic_op op, bool load,
                     nir_ssa_def *index, nir_ssa_def *offset,
                     unsigned access_offset, unsigned access_size,
                     struct vtn_ssa_value **inout, const struct glsl_type *type)
{
   nir_intrinsic_instr *instr = nir_intrinsic_instr_create(b->nb.shader, op);
   instr->num_components = glsl_get_vector_elements(type);

   int src = 0;
   if (!load) {
      nir_intrinsic_set_write_mask(instr, (1 << instr->num_components) - 1);
      instr->src[src++] = nir_src_for_ssa((*inout)->def);
   }

   if (op == nir_intrinsic_load_push_constant) {
      nir_intrinsic_set_base(instr, access_offset);
      nir_intrinsic_set_range(instr, access_size);
   }

   if (index)
      instr->src[src++] = nir_src_for_ssa(index);

   if (op == nir_intrinsic_load_push_constant) {
      /* We need to subtract the offset from where the intrinsic will load the
       * data. */
      instr->src[src++] =
         nir_src_for_ssa(nir_isub(&b->nb, offset,
                                  nir_imm_int(&b->nb, access_offset)));
   } else {
      instr->src[src++] = nir_src_for_ssa(offset);
   }

   if (load) {
      nir_ssa_dest_init(&instr->instr, &instr->dest,
                        instr->num_components,
                        glsl_get_bit_size(type), NULL);
      (*inout)->def = &instr->dest.ssa;
   }

   nir_builder_instr_insert(&b->nb, &instr->instr);

   if (load && glsl_get_base_type(type) == GLSL_TYPE_BOOL)
      (*inout)->def = nir_ine(&b->nb, (*inout)->def, nir_imm_int(&b->nb, 0));
}

static void
_vtn_block_load_store(struct vtn_builder *b, nir_intrinsic_op op, bool load,
                      nir_ssa_def *index, nir_ssa_def *offset,
                      unsigned access_offset, unsigned access_size,
                      struct vtn_access_chain *chain, unsigned chain_idx,
                      struct vtn_type *type, struct vtn_ssa_value **inout)
{
   if (chain && chain_idx >= chain->length)
      chain = NULL;

   if (load && chain == NULL && *inout == NULL)
      *inout = vtn_create_ssa_value(b, type->type);

   enum glsl_base_type base_type = glsl_get_base_type(type->type);
   switch (base_type) {
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_INT:
   case GLSL_TYPE_UINT16:
   case GLSL_TYPE_INT16:
   case GLSL_TYPE_UINT64:
   case GLSL_TYPE_INT64:
   case GLSL_TYPE_FLOAT:
   case GLSL_TYPE_FLOAT16:
   case GLSL_TYPE_DOUBLE:
   case GLSL_TYPE_BOOL:
      /* This is where things get interesting.  At this point, we've hit
       * a vector, a scalar, or a matrix.
       */
      if (glsl_type_is_matrix(type->type)) {
         /* Loading the whole matrix */
         struct vtn_ssa_value *transpose;
         unsigned num_ops, vec_width, col_stride;
         if (type->row_major) {
            num_ops = glsl_get_vector_elements(type->type);
            vec_width = glsl_get_matrix_columns(type->type);
            col_stride = type->array_element->stride;
            if (load) {
               const struct glsl_type *transpose_type =
                  glsl_matrix_type(base_type, vec_width, num_ops);
               *inout = vtn_create_ssa_value(b, transpose_type);
            } else {
               transpose = vtn_ssa_transpose(b, *inout);
               inout = &transpose;
            }
         } else {
            num_ops = glsl_get_matrix_columns(type->type);
            vec_width = glsl_get_vector_elements(type->type);
            col_stride = type->stride;
         }

         for (unsigned i = 0; i < num_ops; i++) {
            nir_ssa_def *elem_offset =
               nir_iadd(&b->nb, offset, nir_imm_int(&b->nb, i * col_stride));
            _vtn_load_store_tail(b, op, load, index, elem_offset,
                                 access_offset, access_size,
                                 &(*inout)->elems[i],
                                 glsl_vector_type(base_type, vec_width));
         }

         if (load && type->row_major)
            *inout = vtn_ssa_transpose(b, *inout);
      } else {
         unsigned elems = glsl_get_vector_elements(type->type);
         unsigned type_size = glsl_get_bit_size(type->type) / 8;
         if (elems == 1 || type->stride == type_size) {
            /* This is a tightly-packed normal scalar or vector load */
            vtn_assert(glsl_type_is_vector_or_scalar(type->type));
            _vtn_load_store_tail(b, op, load, index, offset,
                                 access_offset, access_size,
                                 inout, type->type);
         } else {
            /* This is a strided load.  We have to load N things separately.
             * This is the single column of a row-major matrix case.
             */
            vtn_assert(type->stride > type_size);
            vtn_assert(type->stride % type_size == 0);

            nir_ssa_def *per_comp[4];
            for (unsigned i = 0; i < elems; i++) {
               nir_ssa_def *elem_offset =
                  nir_iadd(&b->nb, offset,
                                   nir_imm_int(&b->nb, i * type->stride));
               struct vtn_ssa_value *comp, temp_val;
               if (!load) {
                  temp_val.def = nir_channel(&b->nb, (*inout)->def, i);
                  temp_val.type = glsl_scalar_type(base_type);
               }
               comp = &temp_val;
               _vtn_load_store_tail(b, op, load, index, elem_offset,
                                    access_offset, access_size,
                                    &comp, glsl_scalar_type(base_type));
               per_comp[i] = comp->def;
            }

            if (load) {
               if (*inout == NULL)
                  *inout = vtn_create_ssa_value(b, type->type);
               (*inout)->def = nir_vec(&b->nb, per_comp, elems);
            }
         }
      }
      return;

   case GLSL_TYPE_ARRAY: {
      unsigned elems = glsl_get_length(type->type);
      for (unsigned i = 0; i < elems; i++) {
         nir_ssa_def *elem_off =
            nir_iadd(&b->nb, offset, nir_imm_int(&b->nb, i * type->stride));
         _vtn_block_load_store(b, op, load, index, elem_off,
                               access_offset, access_size,
                               NULL, 0,
                               type->array_element, &(*inout)->elems[i]);
      }
      return;
   }

   case GLSL_TYPE_STRUCT: {
      unsigned elems = glsl_get_length(type->type);
      for (unsigned i = 0; i < elems; i++) {
         nir_ssa_def *elem_off =
            nir_iadd(&b->nb, offset, nir_imm_int(&b->nb, type->offsets[i]));
         _vtn_block_load_store(b, op, load, index, elem_off,
                               access_offset, access_size,
                               NULL, 0,
                               type->members[i], &(*inout)->elems[i]);
      }
      return;
   }

   default:
      vtn_fail("Invalid block member type");
   }
}

static struct vtn_ssa_value *
vtn_block_load(struct vtn_builder *b, struct vtn_pointer *src)
{
   nir_intrinsic_op op;
   unsigned access_offset = 0, access_size = 0;
   switch (src->mode) {
   case vtn_variable_mode_ubo:
      op = nir_intrinsic_load_ubo;
      break;
   case vtn_variable_mode_ssbo:
      op = nir_intrinsic_load_ssbo;
      break;
   case vtn_variable_mode_push_constant:
      op = nir_intrinsic_load_push_constant;
      vtn_access_chain_get_offset_size(b, src->chain, src->var->type,
                                       &access_offset, &access_size);
      break;
   case vtn_variable_mode_workgroup:
      op = nir_intrinsic_load_shared;
      break;
   default:
      vtn_fail("Invalid block variable mode");
   }

   nir_ssa_def *offset, *index = NULL;
   unsigned chain_idx;
   offset = vtn_pointer_to_offset(b, src, &index, &chain_idx);

   struct vtn_ssa_value *value = NULL;
   _vtn_block_load_store(b, op, true, index, offset,
                         access_offset, access_size,
                         src->chain, chain_idx, src->type, &value);
   return value;
}

static void
vtn_block_store(struct vtn_builder *b, struct vtn_ssa_value *src,
                struct vtn_pointer *dst)
{
   nir_intrinsic_op op;
   switch (dst->mode) {
   case vtn_variable_mode_ssbo:
      op = nir_intrinsic_store_ssbo;
      break;
   case vtn_variable_mode_workgroup:
      op = nir_intrinsic_store_shared;
      break;
   default:
      vtn_fail("Invalid block variable mode");
   }

   nir_ssa_def *offset, *index = NULL;
   unsigned chain_idx;
   offset = vtn_pointer_to_offset(b, dst, &index, &chain_idx);

   _vtn_block_load_store(b, op, false, index, offset,
                         0, 0, dst->chain, chain_idx, dst->type, &src);
}

static void
_vtn_variable_load_store(struct vtn_builder *b, bool load,
                         struct vtn_pointer *ptr,
                         struct vtn_ssa_value **inout)
{
   enum glsl_base_type base_type = glsl_get_base_type(ptr->type->type);
   switch (base_type) {
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_INT:
   case GLSL_TYPE_UINT16:
   case GLSL_TYPE_INT16:
   case GLSL_TYPE_UINT64:
   case GLSL_TYPE_INT64:
   case GLSL_TYPE_FLOAT:
   case GLSL_TYPE_FLOAT16:
   case GLSL_TYPE_BOOL:
   case GLSL_TYPE_DOUBLE:
      /* At this point, we have a scalar, vector, or matrix so we know that
       * there cannot be any structure splitting still in the way.  By
       * stopping at the matrix level rather than the vector level, we
       * ensure that matrices get loaded in the optimal way even if they
       * are storred row-major in a UBO.
       */
      if (load) {
         *inout = vtn_local_load(b, vtn_pointer_to_deref(b, ptr));
      } else {
         vtn_local_store(b, *inout, vtn_pointer_to_deref(b, ptr));
      }
      return;

   case GLSL_TYPE_ARRAY:
   case GLSL_TYPE_STRUCT: {
      unsigned elems = glsl_get_length(ptr->type->type);
      if (load) {
         vtn_assert(*inout == NULL);
         *inout = rzalloc(b, struct vtn_ssa_value);
         (*inout)->type = ptr->type->type;
         (*inout)->elems = rzalloc_array(b, struct vtn_ssa_value *, elems);
      }

      struct vtn_access_chain chain = {
         .length = 1,
         .link = {
            { .mode = vtn_access_mode_literal, },
         }
      };
      for (unsigned i = 0; i < elems; i++) {
         chain.link[0].id = i;
         struct vtn_pointer *elem = vtn_pointer_dereference(b, ptr, &chain);
         _vtn_variable_load_store(b, load, elem, &(*inout)->elems[i]);
      }
      return;
   }

   default:
      vtn_fail("Invalid access chain type");
   }
}

struct vtn_ssa_value *
vtn_variable_load(struct vtn_builder *b, struct vtn_pointer *src)
{
   if (vtn_pointer_is_external_block(b, src)) {
      return vtn_block_load(b, src);
   } else {
      struct vtn_ssa_value *val = NULL;
      _vtn_variable_load_store(b, true, src, &val);
      return val;
   }
}

void
vtn_variable_store(struct vtn_builder *b, struct vtn_ssa_value *src,
                   struct vtn_pointer *dest)
{
   if (vtn_pointer_is_external_block(b, dest)) {
      vtn_assert(dest->mode == vtn_variable_mode_ssbo ||
                 dest->mode == vtn_variable_mode_workgroup);
      vtn_block_store(b, src, dest);
   } else {
      _vtn_variable_load_store(b, false, dest, &src);
   }
}

static void
_vtn_variable_copy(struct vtn_builder *b, struct vtn_pointer *dest,
                   struct vtn_pointer *src)
{
   vtn_assert(src->type->type == dest->type->type);
   enum glsl_base_type base_type = glsl_get_base_type(src->type->type);
   switch (base_type) {
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_INT:
   case GLSL_TYPE_UINT16:
   case GLSL_TYPE_INT16:
   case GLSL_TYPE_UINT64:
   case GLSL_TYPE_INT64:
   case GLSL_TYPE_FLOAT:
   case GLSL_TYPE_FLOAT16:
   case GLSL_TYPE_DOUBLE:
   case GLSL_TYPE_BOOL:
      /* At this point, we have a scalar, vector, or matrix so we know that
       * there cannot be any structure splitting still in the way.  By
       * stopping at the matrix level rather than the vector level, we
       * ensure that matrices get loaded in the optimal way even if they
       * are storred row-major in a UBO.
       */
      vtn_variable_store(b, vtn_variable_load(b, src), dest);
      return;

   case GLSL_TYPE_ARRAY:
   case GLSL_TYPE_STRUCT: {
      struct vtn_access_chain chain = {
         .length = 1,
         .link = {
            { .mode = vtn_access_mode_literal, },
         }
      };
      unsigned elems = glsl_get_length(src->type->type);
      for (unsigned i = 0; i < elems; i++) {
         chain.link[0].id = i;
         struct vtn_pointer *src_elem =
            vtn_pointer_dereference(b, src, &chain);
         struct vtn_pointer *dest_elem =
            vtn_pointer_dereference(b, dest, &chain);

         _vtn_variable_copy(b, dest_elem, src_elem);
      }
      return;
   }

   default:
      vtn_fail("Invalid access chain type");
   }
}

static void
vtn_variable_copy(struct vtn_builder *b, struct vtn_pointer *dest,
                  struct vtn_pointer *src)
{
   /* TODO: At some point, we should add a special-case for when we can
    * just emit a copy_var intrinsic.
    */
   _vtn_variable_copy(b, dest, src);
}

static void
set_mode_system_value(struct vtn_builder *b, nir_variable_mode *mode)
{
   vtn_assert(*mode == nir_var_system_value || *mode == nir_var_shader_in);
   *mode = nir_var_system_value;
}

static void
vtn_get_builtin_location(struct vtn_builder *b,
                         SpvBuiltIn builtin, int *location,
                         nir_variable_mode *mode)
{
   switch (builtin) {
   case SpvBuiltInPosition:
      *location = VARYING_SLOT_POS;
      break;
   case SpvBuiltInPointSize:
      *location = VARYING_SLOT_PSIZ;
      break;
   case SpvBuiltInClipDistance:
      *location = VARYING_SLOT_CLIP_DIST0; /* XXX CLIP_DIST1? */
      break;
   case SpvBuiltInCullDistance:
      *location = VARYING_SLOT_CULL_DIST0;
      break;
   case SpvBuiltInVertexIndex:
      *location = SYSTEM_VALUE_VERTEX_ID;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInVertexId:
      /* Vulkan defines VertexID to be zero-based and reserves the new
       * builtin keyword VertexIndex to indicate the non-zero-based value.
       */
      *location = SYSTEM_VALUE_VERTEX_ID_ZERO_BASE;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInInstanceIndex:
      *location = SYSTEM_VALUE_INSTANCE_INDEX;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInInstanceId:
      *location = SYSTEM_VALUE_INSTANCE_ID;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInPrimitiveId:
      if (b->shader->info.stage == MESA_SHADER_FRAGMENT) {
         vtn_assert(*mode == nir_var_shader_in);
         *location = VARYING_SLOT_PRIMITIVE_ID;
      } else if (*mode == nir_var_shader_out) {
         *location = VARYING_SLOT_PRIMITIVE_ID;
      } else {
         *location = SYSTEM_VALUE_PRIMITIVE_ID;
         set_mode_system_value(b, mode);
      }
      break;
   case SpvBuiltInInvocationId:
      *location = SYSTEM_VALUE_INVOCATION_ID;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInLayer:
      *location = VARYING_SLOT_LAYER;
      if (b->shader->info.stage == MESA_SHADER_FRAGMENT)
         *mode = nir_var_shader_in;
      else if (b->shader->info.stage == MESA_SHADER_GEOMETRY)
         *mode = nir_var_shader_out;
      else
         vtn_fail("invalid stage for SpvBuiltInLayer");
      break;
   case SpvBuiltInViewportIndex:
      *location = VARYING_SLOT_VIEWPORT;
      if (b->shader->info.stage == MESA_SHADER_GEOMETRY)
         *mode = nir_var_shader_out;
      else if (b->shader->info.stage == MESA_SHADER_FRAGMENT)
         *mode = nir_var_shader_in;
      else
         vtn_fail("invalid stage for SpvBuiltInViewportIndex");
      break;
   case SpvBuiltInTessLevelOuter:
      *location = VARYING_SLOT_TESS_LEVEL_OUTER;
      break;
   case SpvBuiltInTessLevelInner:
      *location = VARYING_SLOT_TESS_LEVEL_INNER;
      break;
   case SpvBuiltInTessCoord:
      *location = SYSTEM_VALUE_TESS_COORD;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInPatchVertices:
      *location = SYSTEM_VALUE_VERTICES_IN;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInFragCoord:
      *location = VARYING_SLOT_POS;
      vtn_assert(*mode == nir_var_shader_in);
      break;
   case SpvBuiltInPointCoord:
      *location = VARYING_SLOT_PNTC;
      vtn_assert(*mode == nir_var_shader_in);
      break;
   case SpvBuiltInFrontFacing:
      *location = SYSTEM_VALUE_FRONT_FACE;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInSampleId:
      *location = SYSTEM_VALUE_SAMPLE_ID;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInSamplePosition:
      *location = SYSTEM_VALUE_SAMPLE_POS;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInSampleMask:
      if (*mode == nir_var_shader_out) {
         *location = FRAG_RESULT_SAMPLE_MASK;
      } else {
         *location = SYSTEM_VALUE_SAMPLE_MASK_IN;
         set_mode_system_value(b, mode);
      }
      break;
   case SpvBuiltInFragDepth:
      *location = FRAG_RESULT_DEPTH;
      vtn_assert(*mode == nir_var_shader_out);
      break;
   case SpvBuiltInHelperInvocation:
      *location = SYSTEM_VALUE_HELPER_INVOCATION;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInNumWorkgroups:
      *location = SYSTEM_VALUE_NUM_WORK_GROUPS;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInWorkgroupSize:
      /* This should already be handled */
      vtn_fail("unsupported builtin");
      break;
   case SpvBuiltInWorkgroupId:
      *location = SYSTEM_VALUE_WORK_GROUP_ID;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInLocalInvocationId:
      *location = SYSTEM_VALUE_LOCAL_INVOCATION_ID;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInLocalInvocationIndex:
      *location = SYSTEM_VALUE_LOCAL_INVOCATION_INDEX;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInGlobalInvocationId:
      *location = SYSTEM_VALUE_GLOBAL_INVOCATION_ID;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInBaseVertex:
      *location = SYSTEM_VALUE_BASE_VERTEX;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInBaseInstance:
      *location = SYSTEM_VALUE_BASE_INSTANCE;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInDrawIndex:
      *location = SYSTEM_VALUE_DRAW_ID;
      set_mode_system_value(b, mode);
      break;
   case SpvBuiltInViewIndex:
      *location = SYSTEM_VALUE_VIEW_INDEX;
      set_mode_system_value(b, mode);
      break;
   default:
      vtn_fail("unsupported builtin");
   }
}

static void
apply_var_decoration(struct vtn_builder *b, nir_variable *nir_var,
                     const struct vtn_decoration *dec)
{
   switch (dec->decoration) {
   case SpvDecorationRelaxedPrecision:
      break; /* FIXME: Do nothing with this for now. */
   case SpvDecorationNoPerspective:
      nir_var->data.interpolation = INTERP_MODE_NOPERSPECTIVE;
      break;
   case SpvDecorationFlat:
      nir_var->data.interpolation = INTERP_MODE_FLAT;
      break;
   case SpvDecorationCentroid:
      nir_var->data.centroid = true;
      break;
   case SpvDecorationSample:
      nir_var->data.sample = true;
      break;
   case SpvDecorationInvariant:
      nir_var->data.invariant = true;
      break;
   case SpvDecorationConstant:
      vtn_assert(nir_var->constant_initializer != NULL);
      nir_var->data.read_only = true;
      break;
   case SpvDecorationNonReadable:
      nir_var->data.image.write_only = true;
      break;
   case SpvDecorationNonWritable:
      nir_var->data.read_only = true;
      nir_var->data.image.read_only = true;
      break;
   case SpvDecorationRestrict:
      nir_var->data.image.restrict_flag = true;
      break;
   case SpvDecorationVolatile:
      nir_var->data.image._volatile = true;
      break;
   case SpvDecorationCoherent:
      nir_var->data.image.coherent = true;
      break;
   case SpvDecorationComponent:
      nir_var->data.location_frac = dec->literals[0];
      break;
   case SpvDecorationIndex:
      nir_var->data.index = dec->literals[0];
      break;
   case SpvDecorationBuiltIn: {
      SpvBuiltIn builtin = dec->literals[0];

      if (builtin == SpvBuiltInWorkgroupSize) {
         /* This shouldn't be a builtin.  It's actually a constant. */
         nir_var->data.mode = nir_var_global;
         nir_var->data.read_only = true;

         nir_constant *c = rzalloc(nir_var, nir_constant);
         c->values[0].u32[0] = b->shader->info.cs.local_size[0];
         c->values[0].u32[1] = b->shader->info.cs.local_size[1];
         c->values[0].u32[2] = b->shader->info.cs.local_size[2];
         nir_var->constant_initializer = c;
         break;
      }

      nir_variable_mode mode = nir_var->data.mode;
      vtn_get_builtin_location(b, builtin, &nir_var->data.location, &mode);
      nir_var->data.mode = mode;

      switch (builtin) {
      case SpvBuiltInTessLevelOuter:
      case SpvBuiltInTessLevelInner:
         nir_var->data.compact = true;
         break;
      case SpvBuiltInSamplePosition:
         nir_var->data.origin_upper_left = b->origin_upper_left;
         /* fallthrough */
      case SpvBuiltInFragCoord:
         nir_var->data.pixel_center_integer = b->pixel_center_integer;
         break;
      default:
         break;
      }
   }

   case SpvDecorationSpecId:
   case SpvDecorationRowMajor:
   case SpvDecorationColMajor:
   case SpvDecorationMatrixStride:
   case SpvDecorationAliased:
   case SpvDecorationUniform:
   case SpvDecorationStream:
   case SpvDecorationOffset:
   case SpvDecorationLinkageAttributes:
      break; /* Do nothing with these here */

   case SpvDecorationPatch:
      nir_var->data.patch = true;
      break;

   case SpvDecorationLocation:
      vtn_fail("Handled above");

   case SpvDecorationBlock:
   case SpvDecorationBufferBlock:
   case SpvDecorationArrayStride:
   case SpvDecorationGLSLShared:
   case SpvDecorationGLSLPacked:
      break; /* These can apply to a type but we don't care about them */

   case SpvDecorationBinding:
   case SpvDecorationDescriptorSet:
   case SpvDecorationNoContraction:
   case SpvDecorationInputAttachmentIndex:
      vtn_warn("Decoration not allowed for variable or structure member: %s",
               spirv_decoration_to_string(dec->decoration));
      break;

   case SpvDecorationXfbBuffer:
   case SpvDecorationXfbStride:
      vtn_warn("Vulkan does not have transform feedback: %s",
               spirv_decoration_to_string(dec->decoration));
      break;

   case SpvDecorationCPacked:
   case SpvDecorationSaturatedConversion:
   case SpvDecorationFuncParamAttr:
   case SpvDecorationFPRoundingMode:
   case SpvDecorationFPFastMathMode:
   case SpvDecorationAlignment:
      vtn_warn("Decoration only allowed for CL-style kernels: %s",
               spirv_decoration_to_string(dec->decoration));
      break;

   default:
      vtn_fail("Unhandled decoration");
   }
}

static void
var_is_patch_cb(struct vtn_builder *b, struct vtn_value *val, int member,
                const struct vtn_decoration *dec, void *out_is_patch)
{
   if (dec->decoration == SpvDecorationPatch) {
      *((bool *) out_is_patch) = true;
   }
}

static void
var_decoration_cb(struct vtn_builder *b, struct vtn_value *val, int member,
                  const struct vtn_decoration *dec, void *void_var)
{
   struct vtn_variable *vtn_var = void_var;

   /* Handle decorations that apply to a vtn_variable as a whole */
   switch (dec->decoration) {
   case SpvDecorationBinding:
      vtn_var->binding = dec->literals[0];
      return;
   case SpvDecorationDescriptorSet:
      vtn_var->descriptor_set = dec->literals[0];
      return;
   case SpvDecorationInputAttachmentIndex:
      vtn_var->input_attachment_index = dec->literals[0];
      return;
   case SpvDecorationPatch:
      vtn_var->patch = true;
      break;
   default:
      break;
   }

   if (val->value_type == vtn_value_type_pointer) {
      assert(val->pointer->var == void_var);
      assert(val->pointer->chain == NULL);
      assert(member == -1);
   } else {
      assert(val->value_type == vtn_value_type_type);
   }

   /* Location is odd.  If applied to a split structure, we have to walk the
    * whole thing and accumulate the location.  It's easier to handle as a
    * special case.
    */
   if (dec->decoration == SpvDecorationLocation) {
      unsigned location = dec->literals[0];
      bool is_vertex_input;
      if (b->shader->info.stage == MESA_SHADER_FRAGMENT &&
          vtn_var->mode == vtn_variable_mode_output) {
         is_vertex_input = false;
         location += FRAG_RESULT_DATA0;
      } else if (b->shader->info.stage == MESA_SHADER_VERTEX &&
                 vtn_var->mode == vtn_variable_mode_input) {
         is_vertex_input = true;
         location += VERT_ATTRIB_GENERIC0;
      } else if (vtn_var->mode == vtn_variable_mode_input ||
                 vtn_var->mode == vtn_variable_mode_output) {
         is_vertex_input = false;
         location += vtn_var->patch ? VARYING_SLOT_PATCH0 : VARYING_SLOT_VAR0;
      } else {
         vtn_warn("Location must be on input or output variable");
         return;
      }

      if (vtn_var->var) {
         /* This handles the member and lone variable cases */
         vtn_var->var->data.location = location;
      } else {
         /* This handles the structure member case */
         assert(vtn_var->members);
         unsigned length =
            glsl_get_length(glsl_without_array(vtn_var->type->type));
         for (unsigned i = 0; i < length; i++) {
            vtn_var->members[i]->data.location = location;
            location +=
               glsl_count_attribute_slots(vtn_var->members[i]->interface_type,
                                          is_vertex_input);
         }
      }
      return;
   } else {
      if (vtn_var->var) {
         assert(member == -1);
         apply_var_decoration(b, vtn_var->var, dec);
      } else if (vtn_var->members) {
         if (member >= 0) {
            /* Member decorations must come from a type */
            assert(val->value_type == vtn_value_type_type);
            apply_var_decoration(b, vtn_var->members[member], dec);
         } else {
            unsigned length =
               glsl_get_length(glsl_without_array(vtn_var->type->type));
            for (unsigned i = 0; i < length; i++)
               apply_var_decoration(b, vtn_var->members[i], dec);
         }
      } else {
         /* A few variables, those with external storage, have no actual
          * nir_variables associated with them.  Fortunately, all decorations
          * we care about for those variables are on the type only.
          */
         vtn_assert(vtn_var->mode == vtn_variable_mode_ubo ||
                    vtn_var->mode == vtn_variable_mode_ssbo ||
                    vtn_var->mode == vtn_variable_mode_push_constant ||
                    (vtn_var->mode == vtn_variable_mode_workgroup &&
                     b->options->lower_workgroup_access_to_offsets));
      }
   }
}

static enum vtn_variable_mode
vtn_storage_class_to_mode(struct vtn_builder *b,
                          SpvStorageClass class,
                          struct vtn_type *interface_type,
                          nir_variable_mode *nir_mode_out)
{
   enum vtn_variable_mode mode;
   nir_variable_mode nir_mode;
   switch (class) {
   case SpvStorageClassUniform:
      if (interface_type->block) {
         mode = vtn_variable_mode_ubo;
         nir_mode = 0;
      } else if (interface_type->buffer_block) {
         mode = vtn_variable_mode_ssbo;
         nir_mode = 0;
      } else {
         vtn_fail("Invalid uniform variable type");
      }
      break;
   case SpvStorageClassStorageBuffer:
      mode = vtn_variable_mode_ssbo;
      nir_mode = 0;
      break;
   case SpvStorageClassUniformConstant:
      if (glsl_type_is_image(interface_type->type)) {
         mode = vtn_variable_mode_image;
         nir_mode = nir_var_uniform;
      } else if (glsl_type_is_sampler(interface_type->type)) {
         mode = vtn_variable_mode_sampler;
         nir_mode = nir_var_uniform;
      } else {
         vtn_fail("Invalid uniform constant variable type");
      }
      break;
   case SpvStorageClassPushConstant:
      mode = vtn_variable_mode_push_constant;
      nir_mode = nir_var_uniform;
      break;
   case SpvStorageClassInput:
      mode = vtn_variable_mode_input;
      nir_mode = nir_var_shader_in;
      break;
   case SpvStorageClassOutput:
      mode = vtn_variable_mode_output;
      nir_mode = nir_var_shader_out;
      break;
   case SpvStorageClassPrivate:
      mode = vtn_variable_mode_global;
      nir_mode = nir_var_global;
      break;
   case SpvStorageClassFunction:
      mode = vtn_variable_mode_local;
      nir_mode = nir_var_local;
      break;
   case SpvStorageClassWorkgroup:
      mode = vtn_variable_mode_workgroup;
      nir_mode = nir_var_shared;
      break;
   case SpvStorageClassCrossWorkgroup:
   case SpvStorageClassGeneric:
   case SpvStorageClassAtomicCounter:
   default:
      vtn_fail("Unhandled variable storage class");
   }

   if (nir_mode_out)
      *nir_mode_out = nir_mode;

   return mode;
}

nir_ssa_def *
vtn_pointer_to_ssa(struct vtn_builder *b, struct vtn_pointer *ptr)
{
   /* This pointer needs to have a pointer type with actual storage */
   vtn_assert(ptr->ptr_type);
   vtn_assert(ptr->ptr_type->type);

   if (!ptr->offset) {
      /* If we don't have an offset then we must be a pointer to the variable
       * itself.
       */
      vtn_assert(!ptr->offset && !ptr->block_index);

      struct vtn_access_chain chain = {
         .length = 0,
      };
      ptr = vtn_ssa_offset_pointer_dereference(b, ptr, &chain);
   }

   vtn_assert(ptr->offset);
   if (ptr->block_index) {
      vtn_assert(ptr->mode == vtn_variable_mode_ubo ||
                 ptr->mode == vtn_variable_mode_ssbo);
      return nir_vec2(&b->nb, ptr->block_index, ptr->offset);
   } else {
      vtn_assert(ptr->mode == vtn_variable_mode_workgroup);
      return ptr->offset;
   }
}

struct vtn_pointer *
vtn_pointer_from_ssa(struct vtn_builder *b, nir_ssa_def *ssa,
                     struct vtn_type *ptr_type)
{
   vtn_assert(ssa->num_components <= 2 && ssa->bit_size == 32);
   vtn_assert(ptr_type->base_type == vtn_base_type_pointer);
   vtn_assert(ptr_type->deref->base_type != vtn_base_type_pointer);
   /* This pointer type needs to have actual storage */
   vtn_assert(ptr_type->type);

   struct vtn_pointer *ptr = rzalloc(b, struct vtn_pointer);
   ptr->mode = vtn_storage_class_to_mode(b, ptr_type->storage_class,
                                         ptr_type, NULL);
   ptr->type = ptr_type->deref;
   ptr->ptr_type = ptr_type;

   if (ssa->num_components > 1) {
      vtn_assert(ssa->num_components == 2);
      vtn_assert(ptr->mode == vtn_variable_mode_ubo ||
                 ptr->mode == vtn_variable_mode_ssbo);
      ptr->block_index = nir_channel(&b->nb, ssa, 0);
      ptr->offset = nir_channel(&b->nb, ssa, 1);
   } else {
      vtn_assert(ssa->num_components == 1);
      vtn_assert(ptr->mode == vtn_variable_mode_workgroup);
      ptr->block_index = NULL;
      ptr->offset = ssa;
   }

   return ptr;
}

static bool
is_per_vertex_inout(const struct vtn_variable *var, gl_shader_stage stage)
{
   if (var->patch || !glsl_type_is_array(var->type->type))
      return false;

   if (var->mode == vtn_variable_mode_input) {
      return stage == MESA_SHADER_TESS_CTRL ||
             stage == MESA_SHADER_TESS_EVAL ||
             stage == MESA_SHADER_GEOMETRY;
   }

   if (var->mode == vtn_variable_mode_output)
      return stage == MESA_SHADER_TESS_CTRL;

   return false;
}

static void
vtn_create_variable(struct vtn_builder *b, struct vtn_value *val,
                    struct vtn_type *ptr_type, SpvStorageClass storage_class,
                    nir_constant *initializer)
{
   vtn_assert(ptr_type->base_type == vtn_base_type_pointer);
   struct vtn_type *type = ptr_type->deref;

   struct vtn_type *without_array = type;
   while(glsl_type_is_array(without_array->type))
      without_array = without_array->array_element;

   enum vtn_variable_mode mode;
   nir_variable_mode nir_mode;
   mode = vtn_storage_class_to_mode(b, storage_class, without_array, &nir_mode);

   switch (mode) {
   case vtn_variable_mode_ubo:
      b->shader->info.num_ubos++;
      break;
   case vtn_variable_mode_ssbo:
      b->shader->info.num_ssbos++;
      break;
   case vtn_variable_mode_image:
      b->shader->info.num_images++;
      break;
   case vtn_variable_mode_sampler:
      b->shader->info.num_textures++;
      break;
   case vtn_variable_mode_push_constant:
      b->shader->num_uniforms = vtn_type_block_size(b, type);
      break;
   default:
      /* No tallying is needed */
      break;
   }

   struct vtn_variable *var = rzalloc(b, struct vtn_variable);
   var->type = type;
   var->mode = mode;

   vtn_assert(val->value_type == vtn_value_type_pointer);
   val->pointer = vtn_pointer_for_variable(b, var, ptr_type);

   switch (var->mode) {
   case vtn_variable_mode_local:
   case vtn_variable_mode_global:
   case vtn_variable_mode_image:
   case vtn_variable_mode_sampler:
      /* For these, we create the variable normally */
      var->var = rzalloc(b->shader, nir_variable);
      var->var->name = ralloc_strdup(var->var, val->name);
      var->var->type = var->type->type;
      var->var->data.mode = nir_mode;

      switch (var->mode) {
      case vtn_variable_mode_image:
      case vtn_variable_mode_sampler:
         var->var->interface_type = without_array->type;
         break;
      default:
         var->var->interface_type = NULL;
         break;
      }
      break;

   case vtn_variable_mode_workgroup:
      if (b->options->lower_workgroup_access_to_offsets) {
         var->shared_location = -1;
      } else {
         /* Create the variable normally */
         var->var = rzalloc(b->shader, nir_variable);
         var->var->name = ralloc_strdup(var->var, val->name);
         var->var->type = var->type->type;
         var->var->data.mode = nir_var_shared;
      }
      break;

   case vtn_variable_mode_input:
   case vtn_variable_mode_output: {
      /* In order to know whether or not we're a per-vertex inout, we need
       * the patch qualifier.  This means walking the variable decorations
       * early before we actually create any variables.  Not a big deal.
       *
       * GLSLang really likes to place decorations in the most interior
       * thing it possibly can.  In particular, if you have a struct, it
       * will place the patch decorations on the struct members.  This
       * should be handled by the variable splitting below just fine.
       *
       * If you have an array-of-struct, things get even more weird as it
       * will place the patch decorations on the struct even though it's
       * inside an array and some of the members being patch and others not
       * makes no sense whatsoever.  Since the only sensible thing is for
       * it to be all or nothing, we'll call it patch if any of the members
       * are declared patch.
       */
      var->patch = false;
      vtn_foreach_decoration(b, val, var_is_patch_cb, &var->patch);
      if (glsl_type_is_array(var->type->type) &&
          glsl_type_is_struct(without_array->type)) {
         vtn_foreach_decoration(b, vtn_value(b, without_array->id,
                                             vtn_value_type_type),
                                var_is_patch_cb, &var->patch);
      }

      /* For inputs and outputs, we immediately split structures.  This
       * is for a couple of reasons.  For one, builtins may all come in
       * a struct and we really want those split out into separate
       * variables.  For another, interpolation qualifiers can be
       * applied to members of the top-level struct ane we need to be
       * able to preserve that information.
       */

      int array_length = -1;
      struct vtn_type *interface_type = var->type;
      if (is_per_vertex_inout(var, b->shader->info.stage)) {
         /* In Geometry shaders (and some tessellation), inputs come
          * in per-vertex arrays.  However, some builtins come in
          * non-per-vertex, hence the need for the is_array check.  In
          * any case, there are no non-builtin arrays allowed so this
          * check should be sufficient.
          */
         interface_type = var->type->array_element;
         array_length = glsl_get_length(var->type->type);
      }

      if (glsl_type_is_struct(interface_type->type)) {
         /* It's a struct.  Split it. */
         unsigned num_members = glsl_get_length(interface_type->type);
         var->members = ralloc_array(b, nir_variable *, num_members);

         for (unsigned i = 0; i < num_members; i++) {
            const struct glsl_type *mtype = interface_type->members[i]->type;
            if (array_length >= 0)
               mtype = glsl_array_type(mtype, array_length);

            var->members[i] = rzalloc(b->shader, nir_variable);
            var->members[i]->name =
               ralloc_asprintf(var->members[i], "%s.%d", val->name, i);
            var->members[i]->type = mtype;
            var->members[i]->interface_type =
               interface_type->members[i]->type;
            var->members[i]->data.mode = nir_mode;
            var->members[i]->data.patch = var->patch;

            if (initializer) {
               assert(i < initializer->num_elements);
               var->members[i]->constant_initializer =
                  nir_constant_clone(initializer->elements[i], var->members[i]);
            }
         }

         initializer = NULL;
      } else {
         var->var = rzalloc(b->shader, nir_variable);
         var->var->name = ralloc_strdup(var->var, val->name);
         var->var->type = var->type->type;
         var->var->interface_type = interface_type->type;
         var->var->data.mode = nir_mode;
         var->var->data.patch = var->patch;
      }

      /* For inputs and outputs, we need to grab locations and builtin
       * information from the interface type.
       */
      vtn_foreach_decoration(b, vtn_value(b, interface_type->id,
                                          vtn_value_type_type),
                             var_decoration_cb, var);
      break;
   }

   case vtn_variable_mode_param:
      vtn_fail("Not created through OpVariable");

   case vtn_variable_mode_ubo:
   case vtn_variable_mode_ssbo:
   case vtn_variable_mode_push_constant:
      /* These don't need actual variables. */
      break;
   }

   if (initializer) {
      var->var->constant_initializer =
         nir_constant_clone(initializer, var->var);
   }

   vtn_foreach_decoration(b, val, var_decoration_cb, var);

   if (var->mode == vtn_variable_mode_image ||
       var->mode == vtn_variable_mode_sampler) {
      /* XXX: We still need the binding information in the nir_variable
       * for these. We should fix that.
       */
      var->var->data.binding = var->binding;
      var->var->data.descriptor_set = var->descriptor_set;
      var->var->data.index = var->input_attachment_index;

      if (var->mode == vtn_variable_mode_image)
         var->var->data.image.format = without_array->image_format;
   }

   if (var->mode == vtn_variable_mode_local) {
      vtn_assert(var->members == NULL && var->var != NULL);
      nir_function_impl_add_variable(b->nb.impl, var->var);
   } else if (var->var) {
      nir_shader_add_variable(b->shader, var->var);
   } else if (var->members) {
      unsigned count = glsl_get_length(without_array->type);
      for (unsigned i = 0; i < count; i++) {
         vtn_assert(var->members[i]->data.mode != nir_var_local);
         nir_shader_add_variable(b->shader, var->members[i]);
      }
   } else {
      vtn_assert(vtn_pointer_is_external_block(b, val->pointer));
   }
}

static void
vtn_assert_types_equal(struct vtn_builder *b, SpvOp opcode,
                       struct vtn_type *dst_type,
                       struct vtn_type *src_type)
{
   if (dst_type->id == src_type->id)
      return;

   if (vtn_types_compatible(b, dst_type, src_type)) {
      /* Early versions of GLSLang would re-emit types unnecessarily and you
       * would end up with OpLoad, OpStore, or OpCopyMemory opcodes which have
       * mismatched source and destination types.
       *
       * https://github.com/KhronosGroup/glslang/issues/304
       * https://github.com/KhronosGroup/glslang/issues/307
       * https://bugs.freedesktop.org/show_bug.cgi?id=104338
       * https://bugs.freedesktop.org/show_bug.cgi?id=104424
       */
      vtn_warn("Source and destination types of %s do not have the same "
               "ID (but are compatible): %u vs %u",
                spirv_op_to_string(opcode), dst_type->id, src_type->id);
      return;
   }

   vtn_fail("Source and destination types of %s do not match: %s vs. %s",
            spirv_op_to_string(opcode),
            glsl_get_type_name(dst_type->type),
            glsl_get_type_name(src_type->type));
}

void
vtn_handle_variables(struct vtn_builder *b, SpvOp opcode,
                     const uint32_t *w, unsigned count)
{
   switch (opcode) {
   case SpvOpUndef: {
      struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_undef);
      val->type = vtn_value(b, w[1], vtn_value_type_type)->type;
      break;
   }

   case SpvOpVariable: {
      struct vtn_type *ptr_type = vtn_value(b, w[1], vtn_value_type_type)->type;

      struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_pointer);

      SpvStorageClass storage_class = w[3];
      nir_constant *initializer = NULL;
      if (count > 4)
         initializer = vtn_value(b, w[4], vtn_value_type_constant)->constant;

      vtn_create_variable(b, val, ptr_type, storage_class, initializer);
      break;
   }

   case SpvOpAccessChain:
   case SpvOpPtrAccessChain:
   case SpvOpInBoundsAccessChain: {
      struct vtn_access_chain *chain = vtn_access_chain_create(b, count - 4);
      chain->ptr_as_array = (opcode == SpvOpPtrAccessChain);

      unsigned idx = 0;
      for (int i = 4; i < count; i++) {
         struct vtn_value *link_val = vtn_untyped_value(b, w[i]);
         if (link_val->value_type == vtn_value_type_constant) {
            chain->link[idx].mode = vtn_access_mode_literal;
            chain->link[idx].id = link_val->constant->values[0].u32[0];
         } else {
            chain->link[idx].mode = vtn_access_mode_id;
            chain->link[idx].id = w[i];

         }
         idx++;
      }

      struct vtn_type *ptr_type = vtn_value(b, w[1], vtn_value_type_type)->type;
      struct vtn_value *base_val = vtn_untyped_value(b, w[3]);
      if (base_val->value_type == vtn_value_type_sampled_image) {
         /* This is rather insane.  SPIR-V allows you to use OpSampledImage
          * to combine an array of images with a single sampler to get an
          * array of sampled images that all share the same sampler.
          * Fortunately, this means that we can more-or-less ignore the
          * sampler when crawling the access chain, but it does leave us
          * with this rather awkward little special-case.
          */
         struct vtn_value *val =
            vtn_push_value(b, w[2], vtn_value_type_sampled_image);
         val->sampled_image = ralloc(b, struct vtn_sampled_image);
         val->sampled_image->type = base_val->sampled_image->type;
         val->sampled_image->image =
            vtn_pointer_dereference(b, base_val->sampled_image->image, chain);
         val->sampled_image->sampler = base_val->sampled_image->sampler;
      } else {
         vtn_assert(base_val->value_type == vtn_value_type_pointer);
         struct vtn_value *val =
            vtn_push_value(b, w[2], vtn_value_type_pointer);
         val->pointer = vtn_pointer_dereference(b, base_val->pointer, chain);
         val->pointer->ptr_type = ptr_type;
      }
      break;
   }

   case SpvOpCopyMemory: {
      struct vtn_value *dest = vtn_value(b, w[1], vtn_value_type_pointer);
      struct vtn_value *src = vtn_value(b, w[2], vtn_value_type_pointer);

      vtn_assert_types_equal(b, opcode, dest->type->deref, src->type->deref);

      vtn_variable_copy(b, dest->pointer, src->pointer);
      break;
   }

   case SpvOpLoad: {
      struct vtn_type *res_type =
         vtn_value(b, w[1], vtn_value_type_type)->type;
      struct vtn_value *src_val = vtn_value(b, w[3], vtn_value_type_pointer);
      struct vtn_pointer *src = src_val->pointer;

      vtn_assert_types_equal(b, opcode, res_type, src_val->type->deref);

      if (src->mode == vtn_variable_mode_image ||
          src->mode == vtn_variable_mode_sampler) {
         vtn_push_value(b, w[2], vtn_value_type_pointer)->pointer = src;
         return;
      }

      vtn_push_ssa(b, w[2], res_type, vtn_variable_load(b, src));
      break;
   }

   case SpvOpStore: {
      struct vtn_value *dest_val = vtn_value(b, w[1], vtn_value_type_pointer);
      struct vtn_pointer *dest = dest_val->pointer;
      struct vtn_value *src_val = vtn_untyped_value(b, w[2]);

      /* OpStore requires us to actually have a storage type */
      vtn_fail_if(dest->type->type == NULL,
                  "Invalid destination type for OpStore");

      if (glsl_get_base_type(dest->type->type) == GLSL_TYPE_BOOL &&
          glsl_get_base_type(src_val->type->type) == GLSL_TYPE_UINT) {
         /* Early versions of GLSLang would use uint types for UBOs/SSBOs but
          * would then store them to a local variable as bool.  Work around
          * the issue by doing an implicit conversion.
          *
          * https://github.com/KhronosGroup/glslang/issues/170
          * https://bugs.freedesktop.org/show_bug.cgi?id=104424
          */
         vtn_warn("OpStore of value of type OpTypeInt to a pointer to type "
                  "OpTypeBool.  Doing an implicit conversion to work around "
                  "the problem.");
         struct vtn_ssa_value *bool_ssa =
            vtn_create_ssa_value(b, dest->type->type);
         bool_ssa->def = nir_i2b(&b->nb, vtn_ssa_value(b, w[2])->def);
         vtn_variable_store(b, bool_ssa, dest);
         break;
      }

      vtn_assert_types_equal(b, opcode, dest_val->type->deref, src_val->type);

      if (glsl_type_is_sampler(dest->type->type)) {
         vtn_warn("OpStore of a sampler detected.  Doing on-the-fly copy "
                  "propagation to workaround the problem.");
         vtn_assert(dest->var->copy_prop_sampler == NULL);
         dest->var->copy_prop_sampler =
            vtn_value(b, w[2], vtn_value_type_pointer)->pointer;
         break;
      }

      struct vtn_ssa_value *src = vtn_ssa_value(b, w[2]);
      vtn_variable_store(b, src, dest);
      break;
   }

   case SpvOpArrayLength: {
      struct vtn_pointer *ptr =
         vtn_value(b, w[3], vtn_value_type_pointer)->pointer;

      const uint32_t offset = ptr->var->type->offsets[w[4]];
      const uint32_t stride = ptr->var->type->members[w[4]]->stride;

      if (!ptr->block_index) {
         struct vtn_access_chain chain = {
            .length = 0,
         };
         ptr = vtn_ssa_offset_pointer_dereference(b, ptr, &chain);
         vtn_assert(ptr->block_index);
      }

      nir_intrinsic_instr *instr =
         nir_intrinsic_instr_create(b->nb.shader,
                                    nir_intrinsic_get_buffer_size);
      instr->src[0] = nir_src_for_ssa(ptr->block_index);
      nir_ssa_dest_init(&instr->instr, &instr->dest, 1, 32, NULL);
      nir_builder_instr_insert(&b->nb, &instr->instr);
      nir_ssa_def *buf_size = &instr->dest.ssa;

      /* array_length = max(buffer_size - offset, 0) / stride */
      nir_ssa_def *array_length =
         nir_idiv(&b->nb,
                  nir_imax(&b->nb,
                           nir_isub(&b->nb,
                                    buf_size,
                                    nir_imm_int(&b->nb, offset)),
                           nir_imm_int(&b->nb, 0u)),
                  nir_imm_int(&b->nb, stride));

      struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
      val->ssa = vtn_create_ssa_value(b, glsl_uint_type());
      val->ssa->def = array_length;
      break;
   }

   case SpvOpCopyMemorySized:
   default:
      vtn_fail("Unhandled opcode");
   }
}
