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
vtn_access_chain_extend(struct vtn_builder *b, struct vtn_access_chain *old,
                        unsigned new_ids)
{
   struct vtn_access_chain *chain;

   unsigned new_len = old->length + new_ids;
   chain = ralloc_size(b, sizeof(*chain) + new_len * sizeof(chain->link[0]));

   chain->var = old->var;
   chain->length = new_len;

   for (unsigned i = 0; i < old->length; i++)
      chain->link[i] = old->link[i];

   return chain;
}

static nir_ssa_def *
vtn_access_link_as_ssa(struct vtn_builder *b, struct vtn_access_link link,
                       unsigned stride)
{
   assert(stride > 0);
   if (link.mode == vtn_access_mode_literal) {
      return nir_imm_int(&b->nb, link.id * stride);
   } else if (stride == 1) {
      return vtn_ssa_value(b, link.id)->def;
   } else {
      return nir_imul(&b->nb, vtn_ssa_value(b, link.id)->def,
                              nir_imm_int(&b->nb, stride));
   }
}

static struct vtn_type *
vtn_access_chain_tail_type(struct vtn_builder *b,
                           struct vtn_access_chain *chain)
{
   struct vtn_type *type = chain->var->type;
   for (unsigned i = 0; i < chain->length; i++) {
      if (glsl_type_is_struct(type->type)) {
         assert(chain->link[i].mode == vtn_access_mode_literal);
         type = type->members[chain->link[i].id];
      } else {
         type = type->array_element;
      }
   }
   return type;
}

/* Crawls a chain of array derefs and rewrites the types so that the
 * lengths stay the same but the terminal type is the one given by
 * tail_type.  This is useful for split structures.
 */
static void
rewrite_deref_types(nir_deref *deref, const struct glsl_type *type)
{
   deref->type = type;
   if (deref->child) {
      assert(deref->child->deref_type == nir_deref_type_array);
      assert(glsl_type_is_array(deref->type));
      rewrite_deref_types(deref->child, glsl_get_array_element(type));
   }
}

nir_deref_var *
vtn_access_chain_to_deref(struct vtn_builder *b, struct vtn_access_chain *chain)
{
   nir_deref_var *deref_var;
   if (chain->var->var) {
      deref_var = nir_deref_var_create(b, chain->var->var);
   } else {
      assert(chain->var->members);
      /* Create the deref_var manually.  It will get filled out later. */
      deref_var = rzalloc(b, nir_deref_var);
      deref_var->deref.deref_type = nir_deref_type_var;
   }

   struct vtn_type *deref_type = chain->var->type;
   nir_deref *tail = &deref_var->deref;
   nir_variable **members = chain->var->members;

   for (unsigned i = 0; i < chain->length; i++) {
      enum glsl_base_type base_type = glsl_get_base_type(deref_type->type);
      switch (base_type) {
      case GLSL_TYPE_UINT:
      case GLSL_TYPE_INT:
      case GLSL_TYPE_FLOAT:
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
            assert(chain->link[i].mode == vtn_access_mode_id);
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
         assert(chain->link[i].mode == vtn_access_mode_literal);
         unsigned idx = chain->link[i].id;
         deref_type = deref_type->members[idx];
         if (members) {
            /* This is a pre-split structure. */
            deref_var->var = members[idx];
            rewrite_deref_types(&deref_var->deref, members[idx]->type);
            assert(tail->type == deref_type->type);
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
         unreachable("Invalid type for deref");
      }
   }

   assert(members == NULL);
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
      intrin->variables[0] =
         nir_deref_as_var(nir_copy_deref(intrin, &deref->deref));
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
      assert(glsl_get_base_type(tail->type) == GLSL_TYPE_STRUCT);
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
   struct vtn_access_chain *chain =
      vtn_value(b, id, vtn_value_type_access_chain)->access_chain;

   return vtn_access_chain_to_deref(b, chain);
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
      assert(vec_deref->deref.child == NULL);
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
      assert(deref->deref.child == NULL);
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

static nir_ssa_def *
get_vulkan_resource_index(struct vtn_builder *b, struct vtn_access_chain *chain,
                          struct vtn_type **type, unsigned *chain_idx)
{
   /* Push constants have no explicit binding */
   if (chain->var->mode == vtn_variable_mode_push_constant) {
      *chain_idx = 0;
      *type = chain->var->type;
      return NULL;
   }

   nir_ssa_def *array_index;
   if (glsl_type_is_array(chain->var->type->type)) {
      assert(chain->length > 0);
      array_index = vtn_access_link_as_ssa(b, chain->link[0], 1);
      *chain_idx = 1;
      *type = chain->var->type->array_element;
   } else {
      array_index = nir_imm_int(&b->nb, 0);
      *chain_idx = 0;
      *type = chain->var->type;
   }

   nir_intrinsic_instr *instr =
      nir_intrinsic_instr_create(b->nb.shader,
                                 nir_intrinsic_vulkan_resource_index);
   instr->src[0] = nir_src_for_ssa(array_index);
   nir_intrinsic_set_desc_set(instr, chain->var->descriptor_set);
   nir_intrinsic_set_binding(instr, chain->var->binding);

   nir_ssa_dest_init(&instr->instr, &instr->dest, 1, 32, NULL);
   nir_builder_instr_insert(&b->nb, &instr->instr);

   return &instr->dest.ssa;
}

nir_ssa_def *
vtn_access_chain_to_offset(struct vtn_builder *b,
                           struct vtn_access_chain *chain,
                           nir_ssa_def **index_out, struct vtn_type **type_out,
                           unsigned *end_idx_out, bool stop_at_matrix)
{
   unsigned idx = 0;
   struct vtn_type *type;
   *index_out = get_vulkan_resource_index(b, chain, &type, &idx);

   nir_ssa_def *offset = nir_imm_int(&b->nb, 0);
   for (; idx < chain->length; idx++) {
      enum glsl_base_type base_type = glsl_get_base_type(type->type);
      switch (base_type) {
      case GLSL_TYPE_UINT:
      case GLSL_TYPE_INT:
      case GLSL_TYPE_FLOAT:
      case GLSL_TYPE_DOUBLE:
      case GLSL_TYPE_BOOL:
         /* Some users may not want matrix or vector derefs */
         if (stop_at_matrix)
            goto end;
         /* Fall through */

      case GLSL_TYPE_ARRAY:
         offset = nir_iadd(&b->nb, offset,
                           vtn_access_link_as_ssa(b, chain->link[idx],
                                                  type->stride));

         type = type->array_element;
         break;

      case GLSL_TYPE_STRUCT: {
         assert(chain->link[idx].mode == vtn_access_mode_literal);
         unsigned member = chain->link[idx].id;
         offset = nir_iadd(&b->nb, offset,
                           nir_imm_int(&b->nb, type->offsets[member]));
         type = type->members[member];
         break;
      }

      default:
         unreachable("Invalid type for deref");
      }
   }

end:
   *type_out = type;
   if (end_idx_out)
      *end_idx_out = idx;

   return offset;
}

static void
_vtn_load_store_tail(struct vtn_builder *b, nir_intrinsic_op op, bool load,
                     nir_ssa_def *index, nir_ssa_def *offset,
                     struct vtn_ssa_value **inout, const struct glsl_type *type)
{
   nir_intrinsic_instr *instr = nir_intrinsic_instr_create(b->nb.shader, op);
   instr->num_components = glsl_get_vector_elements(type);

   int src = 0;
   if (!load) {
      nir_intrinsic_set_write_mask(instr, (1 << instr->num_components) - 1);
      instr->src[src++] = nir_src_for_ssa((*inout)->def);
   }

   /* We set the base and size for push constant load to the entire push
    * constant block for now.
    */
   if (op == nir_intrinsic_load_push_constant) {
      nir_intrinsic_set_base(instr, 0);
      nir_intrinsic_set_range(instr, 128);
   }

   if (index)
      instr->src[src++] = nir_src_for_ssa(index);

   instr->src[src++] = nir_src_for_ssa(offset);

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
   case GLSL_TYPE_FLOAT:
   case GLSL_TYPE_BOOL:
      /* This is where things get interesting.  At this point, we've hit
       * a vector, a scalar, or a matrix.
       */
      if (glsl_type_is_matrix(type->type)) {
         if (chain == NULL) {
            /* Loading the whole matrix */
            struct vtn_ssa_value *transpose;
            unsigned num_ops, vec_width;
            if (type->row_major) {
               num_ops = glsl_get_vector_elements(type->type);
               vec_width = glsl_get_matrix_columns(type->type);
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
            }

            for (unsigned i = 0; i < num_ops; i++) {
               nir_ssa_def *elem_offset =
                  nir_iadd(&b->nb, offset,
                           nir_imm_int(&b->nb, i * type->stride));
               _vtn_load_store_tail(b, op, load, index, elem_offset,
                                    &(*inout)->elems[i],
                                    glsl_vector_type(base_type, vec_width));
            }

            if (load && type->row_major)
               *inout = vtn_ssa_transpose(b, *inout);
         } else if (type->row_major) {
            /* Row-major but with an access chiain. */
            nir_ssa_def *col_offset =
               vtn_access_link_as_ssa(b, chain->link[chain_idx],
                                      type->array_element->stride);
            offset = nir_iadd(&b->nb, offset, col_offset);

            if (chain_idx + 1 < chain->length) {
               /* Picking off a single element */
               nir_ssa_def *row_offset =
                  vtn_access_link_as_ssa(b, chain->link[chain_idx + 1],
                                         type->stride);
               offset = nir_iadd(&b->nb, offset, row_offset);
               if (load)
                  *inout = vtn_create_ssa_value(b, glsl_scalar_type(base_type));
               _vtn_load_store_tail(b, op, load, index, offset, inout,
                                    glsl_scalar_type(base_type));
            } else {
               /* Grabbing a column; picking one element off each row */
               unsigned num_comps = glsl_get_vector_elements(type->type);
               const struct glsl_type *column_type =
                  glsl_get_column_type(type->type);

               nir_ssa_def *comps[4];
               for (unsigned i = 0; i < num_comps; i++) {
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
                                       &comp, glsl_scalar_type(base_type));
                  comps[i] = comp->def;
               }

               if (load) {
                  if (*inout == NULL)
                     *inout = vtn_create_ssa_value(b, column_type);

                  (*inout)->def = nir_vec(&b->nb, comps, num_comps);
               }
            }
         } else {
            /* Column-major with a deref. Fall through to array case. */
            nir_ssa_def *col_offset =
               vtn_access_link_as_ssa(b, chain->link[chain_idx], type->stride);
            offset = nir_iadd(&b->nb, offset, col_offset);

            _vtn_block_load_store(b, op, load, index, offset,
                                  chain, chain_idx + 1,
                                  type->array_element, inout);
         }
      } else if (chain == NULL) {
         /* Single whole vector */
         assert(glsl_type_is_vector_or_scalar(type->type));
         _vtn_load_store_tail(b, op, load, index, offset, inout, type->type);
      } else {
         /* Single component of a vector. Fall through to array case. */
         nir_ssa_def *elem_offset =
            vtn_access_link_as_ssa(b, chain->link[chain_idx], type->stride);
         offset = nir_iadd(&b->nb, offset, elem_offset);

         _vtn_block_load_store(b, op, load, index, offset, NULL, 0,
                               type->array_element, inout);
      }
      return;

   case GLSL_TYPE_ARRAY: {
      unsigned elems = glsl_get_length(type->type);
      for (unsigned i = 0; i < elems; i++) {
         nir_ssa_def *elem_off =
            nir_iadd(&b->nb, offset, nir_imm_int(&b->nb, i * type->stride));
         _vtn_block_load_store(b, op, load, index, elem_off, NULL, 0,
                               type->array_element, &(*inout)->elems[i]);
      }
      return;
   }

   case GLSL_TYPE_STRUCT: {
      unsigned elems = glsl_get_length(type->type);
      for (unsigned i = 0; i < elems; i++) {
         nir_ssa_def *elem_off =
            nir_iadd(&b->nb, offset, nir_imm_int(&b->nb, type->offsets[i]));
         _vtn_block_load_store(b, op, load, index, elem_off, NULL, 0,
                               type->members[i], &(*inout)->elems[i]);
      }
      return;
   }

   default:
      unreachable("Invalid block member type");
   }
}

static struct vtn_ssa_value *
vtn_block_load(struct vtn_builder *b, struct vtn_access_chain *src)
{
   nir_intrinsic_op op;
   switch (src->var->mode) {
   case vtn_variable_mode_ubo:
      op = nir_intrinsic_load_ubo;
      break;
   case vtn_variable_mode_ssbo:
      op = nir_intrinsic_load_ssbo;
      break;
   case vtn_variable_mode_push_constant:
      op = nir_intrinsic_load_push_constant;
      break;
   default:
      assert(!"Invalid block variable mode");
   }

   nir_ssa_def *offset, *index = NULL;
   struct vtn_type *type;
   unsigned chain_idx;
   offset = vtn_access_chain_to_offset(b, src, &index, &type, &chain_idx, true);

   struct vtn_ssa_value *value = NULL;
   _vtn_block_load_store(b, op, true, index, offset,
                         src, chain_idx, type, &value);
   return value;
}

static void
vtn_block_store(struct vtn_builder *b, struct vtn_ssa_value *src,
                struct vtn_access_chain *dst)
{
   nir_ssa_def *offset, *index = NULL;
   struct vtn_type *type;
   unsigned chain_idx;
   offset = vtn_access_chain_to_offset(b, dst, &index, &type, &chain_idx, true);

   _vtn_block_load_store(b, nir_intrinsic_store_ssbo, false, index, offset,
                         dst, chain_idx, type, &src);
}

static bool
vtn_variable_is_external_block(struct vtn_variable *var)
{
   return var->mode == vtn_variable_mode_ssbo ||
          var->mode == vtn_variable_mode_ubo ||
          var->mode == vtn_variable_mode_push_constant;
}

static void
_vtn_variable_load_store(struct vtn_builder *b, bool load,
                         struct vtn_access_chain *chain,
                         struct vtn_type *tail_type,
                         struct vtn_ssa_value **inout)
{
   enum glsl_base_type base_type = glsl_get_base_type(tail_type->type);
   switch (base_type) {
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_INT:
   case GLSL_TYPE_FLOAT:
   case GLSL_TYPE_BOOL:
      /* At this point, we have a scalar, vector, or matrix so we know that
       * there cannot be any structure splitting still in the way.  By
       * stopping at the matrix level rather than the vector level, we
       * ensure that matrices get loaded in the optimal way even if they
       * are storred row-major in a UBO.
       */
      if (load) {
         *inout = vtn_local_load(b, vtn_access_chain_to_deref(b, chain));
      } else {
         vtn_local_store(b, *inout, vtn_access_chain_to_deref(b, chain));
      }
      return;

   case GLSL_TYPE_ARRAY:
   case GLSL_TYPE_STRUCT: {
      struct vtn_access_chain *new_chain =
         vtn_access_chain_extend(b, chain, 1);
      new_chain->link[chain->length].mode = vtn_access_mode_literal;
      unsigned elems = glsl_get_length(tail_type->type);
      if (load) {
         assert(*inout == NULL);
         *inout = rzalloc(b, struct vtn_ssa_value);
         (*inout)->type = tail_type->type;
         (*inout)->elems = rzalloc_array(b, struct vtn_ssa_value *, elems);
      }
      for (unsigned i = 0; i < elems; i++) {
         new_chain->link[chain->length].id = i;
         struct vtn_type *elem_type = base_type == GLSL_TYPE_ARRAY ?
            tail_type->array_element : tail_type->members[i];
         _vtn_variable_load_store(b, load, new_chain, elem_type,
                                  &(*inout)->elems[i]);
      }
      return;
   }

   default:
      unreachable("Invalid access chain type");
   }
}

struct vtn_ssa_value *
vtn_variable_load(struct vtn_builder *b, struct vtn_access_chain *src)
{
   if (vtn_variable_is_external_block(src->var)) {
      return vtn_block_load(b, src);
   } else {
      struct vtn_type *tail_type = vtn_access_chain_tail_type(b, src);
      struct vtn_ssa_value *val = NULL;
      _vtn_variable_load_store(b, true, src, tail_type, &val);
      return val;
   }
}

void
vtn_variable_store(struct vtn_builder *b, struct vtn_ssa_value *src,
                   struct vtn_access_chain *dest)
{
   if (vtn_variable_is_external_block(dest->var)) {
      assert(dest->var->mode == vtn_variable_mode_ssbo);
      vtn_block_store(b, src, dest);
   } else {
      struct vtn_type *tail_type = vtn_access_chain_tail_type(b, dest);
      _vtn_variable_load_store(b, false, dest, tail_type, &src);
   }
}

static void
_vtn_variable_copy(struct vtn_builder *b, struct vtn_access_chain *dest,
                   struct vtn_access_chain *src, struct vtn_type *tail_type)
{
   enum glsl_base_type base_type = glsl_get_base_type(tail_type->type);
   switch (base_type) {
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_INT:
   case GLSL_TYPE_FLOAT:
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
      struct vtn_access_chain *new_src, *new_dest;
      new_src = vtn_access_chain_extend(b, src, 1);
      new_dest = vtn_access_chain_extend(b, dest, 1);
      new_src->link[src->length].mode = vtn_access_mode_literal;
      new_dest->link[dest->length].mode = vtn_access_mode_literal;
      unsigned elems = glsl_get_length(tail_type->type);
      for (unsigned i = 0; i < elems; i++) {
         new_src->link[src->length].id = i;
         new_dest->link[dest->length].id = i;
         struct vtn_type *elem_type = base_type == GLSL_TYPE_ARRAY ?
            tail_type->array_element : tail_type->members[i];
         _vtn_variable_copy(b, new_dest, new_src, elem_type);
      }
      return;
   }

   default:
      unreachable("Invalid access chain type");
   }
}

static void
vtn_variable_copy(struct vtn_builder *b, struct vtn_access_chain *dest,
                  struct vtn_access_chain *src)
{
   struct vtn_type *tail_type = vtn_access_chain_tail_type(b, src);
   assert(vtn_access_chain_tail_type(b, dest)->type == tail_type->type);

   /* TODO: At some point, we should add a special-case for when we can
    * just emit a copy_var intrinsic.
    */
   _vtn_variable_copy(b, dest, src, tail_type);
}

static void
set_mode_system_value(nir_variable_mode *mode)
{
   assert(*mode == nir_var_system_value || *mode == nir_var_shader_in);
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
      /* XXX figure this out */
      break;
   case SpvBuiltInVertexIndex:
      *location = SYSTEM_VALUE_VERTEX_ID;
      set_mode_system_value(mode);
      break;
   case SpvBuiltInVertexId:
      /* Vulkan defines VertexID to be zero-based and reserves the new
       * builtin keyword VertexIndex to indicate the non-zero-based value.
       */
      *location = SYSTEM_VALUE_VERTEX_ID_ZERO_BASE;
      set_mode_system_value(mode);
      break;
   case SpvBuiltInInstanceIndex:
      *location = SYSTEM_VALUE_INSTANCE_INDEX;
      set_mode_system_value(mode);
      break;
   case SpvBuiltInInstanceId:
      *location = SYSTEM_VALUE_INSTANCE_ID;
      set_mode_system_value(mode);
      break;
   case SpvBuiltInPrimitiveId:
      *location = VARYING_SLOT_PRIMITIVE_ID;
      *mode = nir_var_shader_out;
      break;
   case SpvBuiltInInvocationId:
      *location = SYSTEM_VALUE_INVOCATION_ID;
      set_mode_system_value(mode);
      break;
   case SpvBuiltInLayer:
      *location = VARYING_SLOT_LAYER;
      *mode = nir_var_shader_out;
      break;
   case SpvBuiltInViewportIndex:
      *location = VARYING_SLOT_VIEWPORT;
      if (b->shader->stage == MESA_SHADER_GEOMETRY)
         *mode = nir_var_shader_out;
      else if (b->shader->stage == MESA_SHADER_FRAGMENT)
         *mode = nir_var_shader_in;
      else
         unreachable("invalid stage for SpvBuiltInViewportIndex");
      break;
   case SpvBuiltInTessLevelOuter:
   case SpvBuiltInTessLevelInner:
   case SpvBuiltInTessCoord:
   case SpvBuiltInPatchVertices:
      unreachable("no tessellation support");
   case SpvBuiltInFragCoord:
      *location = VARYING_SLOT_POS;
      assert(*mode == nir_var_shader_in);
      break;
   case SpvBuiltInPointCoord:
      *location = VARYING_SLOT_PNTC;
      assert(*mode == nir_var_shader_in);
      break;
   case SpvBuiltInFrontFacing:
      *location = SYSTEM_VALUE_FRONT_FACE;
      set_mode_system_value(mode);
      break;
   case SpvBuiltInSampleId:
      *location = SYSTEM_VALUE_SAMPLE_ID;
      set_mode_system_value(mode);
      break;
   case SpvBuiltInSamplePosition:
      *location = SYSTEM_VALUE_SAMPLE_POS;
      set_mode_system_value(mode);
      break;
   case SpvBuiltInSampleMask:
      *location = SYSTEM_VALUE_SAMPLE_MASK_IN; /* XXX out? */
      set_mode_system_value(mode);
      break;
   case SpvBuiltInFragDepth:
      *location = FRAG_RESULT_DEPTH;
      assert(*mode == nir_var_shader_out);
      break;
   case SpvBuiltInNumWorkgroups:
      *location = SYSTEM_VALUE_NUM_WORK_GROUPS;
      set_mode_system_value(mode);
      break;
   case SpvBuiltInWorkgroupSize:
      /* This should already be handled */
      unreachable("unsupported builtin");
      break;
   case SpvBuiltInWorkgroupId:
      *location = SYSTEM_VALUE_WORK_GROUP_ID;
      set_mode_system_value(mode);
      break;
   case SpvBuiltInLocalInvocationId:
      *location = SYSTEM_VALUE_LOCAL_INVOCATION_ID;
      set_mode_system_value(mode);
      break;
   case SpvBuiltInLocalInvocationIndex:
      *location = SYSTEM_VALUE_LOCAL_INVOCATION_INDEX;
      set_mode_system_value(mode);
      break;
   case SpvBuiltInGlobalInvocationId:
      *location = SYSTEM_VALUE_GLOBAL_INVOCATION_ID;
      set_mode_system_value(mode);
      break;
   case SpvBuiltInHelperInvocation:
   default:
      unreachable("unsupported builtin");
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
   default:
      break;
   }

   /* Now we handle decorations that apply to a particular nir_variable */
   nir_variable *nir_var = vtn_var->var;
   if (val->value_type == vtn_value_type_access_chain) {
      assert(val->access_chain->length == 0);
      assert(val->access_chain->var == void_var);
      assert(member == -1);
   } else {
      assert(val->value_type == vtn_value_type_type);
      if (member != -1)
         nir_var = vtn_var->members[member];
   }

   /* Location is odd in that it can apply in three different cases: To a
    * non-split variable, to a whole split variable, or to one structure
    * member of a split variable.
    */
   if (dec->decoration == SpvDecorationLocation) {
      unsigned location = dec->literals[0];
      bool is_vertex_input;
      if (b->shader->stage == MESA_SHADER_FRAGMENT &&
          vtn_var->mode == vtn_variable_mode_output) {
         is_vertex_input = false;
         location += FRAG_RESULT_DATA0;
      } else if (b->shader->stage == MESA_SHADER_VERTEX &&
                 vtn_var->mode == vtn_variable_mode_input) {
         is_vertex_input = true;
         location += VERT_ATTRIB_GENERIC0;
      } else if (vtn_var->mode == vtn_variable_mode_input ||
                 vtn_var->mode == vtn_variable_mode_output) {
         is_vertex_input = false;
         location += VARYING_SLOT_VAR0;
      } else {
         assert(!"Location must be on input or output variable");
      }

      if (nir_var) {
         /* This handles the member and lone variable cases */
         nir_var->data.location = location;
         nir_var->data.explicit_location = true;
      } else {
         /* This handles the structure member case */
         assert(vtn_var->members);
         unsigned length =
            glsl_get_length(glsl_without_array(vtn_var->type->type));
         for (unsigned i = 0; i < length; i++) {
            vtn_var->members[i]->data.location = location;
            vtn_var->members[i]->data.explicit_location = true;
            location +=
               glsl_count_attribute_slots(vtn_var->members[i]->interface_type,
                                          is_vertex_input);
         }
      }
      return;
   }

   if (nir_var == NULL)
      return;

   switch (dec->decoration) {
   case SpvDecorationRelaxedPrecision:
      break; /* FIXME: Do nothing with this for now. */
   case SpvDecorationNoPerspective:
      nir_var->data.interpolation = INTERP_QUALIFIER_NOPERSPECTIVE;
      break;
   case SpvDecorationFlat:
      nir_var->data.interpolation = INTERP_QUALIFIER_FLAT;
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
      assert(nir_var->constant_initializer != NULL);
      nir_var->data.read_only = true;
      break;
   case SpvDecorationNonWritable:
      nir_var->data.read_only = true;
      break;
   case SpvDecorationComponent:
      nir_var->data.location_frac = dec->literals[0];
      break;
   case SpvDecorationIndex:
      nir_var->data.explicit_index = true;
      nir_var->data.index = dec->literals[0];
      break;
   case SpvDecorationBuiltIn: {
      SpvBuiltIn builtin = dec->literals[0];

      if (builtin == SpvBuiltInWorkgroupSize) {
         /* This shouldn't be a builtin.  It's actually a constant. */
         nir_var->data.mode = nir_var_global;
         nir_var->data.read_only = true;

         nir_constant *c = rzalloc(nir_var, nir_constant);
         c->value.u[0] = b->shader->info.cs.local_size[0];
         c->value.u[1] = b->shader->info.cs.local_size[1];
         c->value.u[2] = b->shader->info.cs.local_size[2];
         nir_var->constant_initializer = c;
         break;
      }

      nir_variable_mode mode = nir_var->data.mode;
      vtn_get_builtin_location(b, builtin, &nir_var->data.location, &mode);
      nir_var->data.explicit_location = true;
      nir_var->data.mode = mode;

      if (builtin == SpvBuiltInFragCoord || builtin == SpvBuiltInSamplePosition)
         nir_var->data.origin_upper_left = b->origin_upper_left;

      if (builtin == SpvBuiltInFragCoord)
         nir_var->data.pixel_center_integer = b->pixel_center_integer;
      break;
   }

   case SpvDecorationSpecId:
   case SpvDecorationRowMajor:
   case SpvDecorationColMajor:
   case SpvDecorationMatrixStride:
   case SpvDecorationRestrict:
   case SpvDecorationAliased:
   case SpvDecorationVolatile:
   case SpvDecorationCoherent:
   case SpvDecorationNonReadable:
   case SpvDecorationUniform:
   case SpvDecorationStream:
   case SpvDecorationOffset:
   case SpvDecorationLinkageAttributes:
      break; /* Do nothing with these here */

   case SpvDecorationPatch:
      vtn_warn("Tessellation not yet supported");
      break;

   case SpvDecorationLocation:
      unreachable("Handled above");

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
      vtn_warn("Decoraiton only allowed for CL-style kernels: %s",
               spirv_decoration_to_string(dec->decoration));
      break;
   }
}

/* Tries to compute the size of an interface block based on the strides and
 * offsets that are provided to us in the SPIR-V source.
 */
static unsigned
vtn_type_block_size(struct vtn_type *type)
{
   enum glsl_base_type base_type = glsl_get_base_type(type->type);
   switch (base_type) {
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_INT:
   case GLSL_TYPE_FLOAT:
   case GLSL_TYPE_BOOL:
   case GLSL_TYPE_DOUBLE: {
      unsigned cols = type->row_major ? glsl_get_vector_elements(type->type) :
                                        glsl_get_matrix_columns(type->type);
      if (cols > 1) {
         assert(type->stride > 0);
         return type->stride * cols;
      } else if (base_type == GLSL_TYPE_DOUBLE) {
         return glsl_get_vector_elements(type->type) * 8;
      } else {
         return glsl_get_vector_elements(type->type) * 4;
      }
   }

   case GLSL_TYPE_STRUCT:
   case GLSL_TYPE_INTERFACE: {
      unsigned size = 0;
      unsigned num_fields = glsl_get_length(type->type);
      for (unsigned f = 0; f < num_fields; f++) {
         unsigned field_end = type->offsets[f] +
                              vtn_type_block_size(type->members[f]);
         size = MAX2(size, field_end);
      }
      return size;
   }

   case GLSL_TYPE_ARRAY:
      assert(type->stride > 0);
      assert(glsl_get_length(type->type) > 0);
      return type->stride * glsl_get_length(type->type);

   default:
      assert(!"Invalid block type");
      return 0;
   }
}

void
vtn_handle_variables(struct vtn_builder *b, SpvOp opcode,
                     const uint32_t *w, unsigned count)
{
   switch (opcode) {
   case SpvOpVariable: {
      struct vtn_variable *var = rzalloc(b, struct vtn_variable);
      var->type = vtn_value(b, w[1], vtn_value_type_type)->type;

      var->chain.var = var;
      var->chain.length = 0;

      struct vtn_value *val =
         vtn_push_value(b, w[2], vtn_value_type_access_chain);
      val->access_chain = &var->chain;

      struct vtn_type *without_array = var->type;
      while(glsl_type_is_array(without_array->type))
         without_array = without_array->array_element;

      nir_variable_mode nir_mode;
      switch ((SpvStorageClass)w[3]) {
      case SpvStorageClassUniform:
      case SpvStorageClassUniformConstant:
         if (without_array->block) {
            var->mode = vtn_variable_mode_ubo;
            b->shader->info.num_ubos++;
         } else if (without_array->buffer_block) {
            var->mode = vtn_variable_mode_ssbo;
            b->shader->info.num_ssbos++;
         } else if (glsl_type_is_image(without_array->type)) {
            var->mode = vtn_variable_mode_image;
            nir_mode = nir_var_uniform;
            b->shader->info.num_images++;
         } else if (glsl_type_is_sampler(without_array->type)) {
            var->mode = vtn_variable_mode_sampler;
            nir_mode = nir_var_uniform;
            b->shader->info.num_textures++;
         } else {
            assert(!"Invalid uniform variable type");
         }
         break;
      case SpvStorageClassPushConstant:
         var->mode = vtn_variable_mode_push_constant;
         assert(b->shader->num_uniforms == 0);
         b->shader->num_uniforms = vtn_type_block_size(var->type) * 4;
         break;
      case SpvStorageClassInput:
         var->mode = vtn_variable_mode_input;
         nir_mode = nir_var_shader_in;
         break;
      case SpvStorageClassOutput:
         var->mode = vtn_variable_mode_output;
         nir_mode = nir_var_shader_out;
         break;
      case SpvStorageClassPrivate:
         var->mode = vtn_variable_mode_global;
         nir_mode = nir_var_global;
         break;
      case SpvStorageClassFunction:
         var->mode = vtn_variable_mode_local;
         nir_mode = nir_var_local;
         break;
      case SpvStorageClassWorkgroup:
         var->mode = vtn_variable_mode_workgroup;
         nir_mode = nir_var_shared;
         break;
      case SpvStorageClassCrossWorkgroup:
      case SpvStorageClassGeneric:
      case SpvStorageClassAtomicCounter:
      default:
         unreachable("Unhandled variable storage class");
      }

      switch (var->mode) {
      case vtn_variable_mode_local:
      case vtn_variable_mode_global:
      case vtn_variable_mode_image:
      case vtn_variable_mode_sampler:
      case vtn_variable_mode_workgroup:
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

      case vtn_variable_mode_input:
      case vtn_variable_mode_output: {
         /* For inputs and outputs, we immediately split structures.  This
          * is for a couple of reasons.  For one, builtins may all come in
          * a struct and we really want those split out into separate
          * variables.  For another, interpolation qualifiers can be
          * applied to members of the top-level struct ane we need to be
          * able to preserve that information.
          */

         int array_length = -1;
         struct vtn_type *interface_type = var->type;
         if (b->shader->stage == MESA_SHADER_GEOMETRY &&
             glsl_type_is_array(var->type->type)) {
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
            }
         } else {
            var->var = rzalloc(b->shader, nir_variable);
            var->var->name = ralloc_strdup(var->var, val->name);
            var->var->type = var->type->type;
            var->var->interface_type = interface_type->type;
            var->var->data.mode = nir_mode;
         }

         /* For inputs and outputs, we need to grab locations and builtin
          * information from the interface type.
          */
         vtn_foreach_decoration(b, interface_type->val, var_decoration_cb, var);
         break;

      case vtn_variable_mode_param:
         unreachable("Not created through OpVariable");
      }

      case vtn_variable_mode_ubo:
      case vtn_variable_mode_ssbo:
      case vtn_variable_mode_push_constant:
         /* These don't need actual variables. */
         break;
      }

      if (count > 4) {
         assert(count == 5);
         nir_constant *constant =
            vtn_value(b, w[4], vtn_value_type_constant)->constant;
         var->var->constant_initializer =
            nir_constant_clone(constant, var->var);
      }

      vtn_foreach_decoration(b, val, var_decoration_cb, var);

      if (var->mode == vtn_variable_mode_image ||
          var->mode == vtn_variable_mode_sampler) {
         /* XXX: We still need the binding information in the nir_variable
          * for these. We should fix that.
          */
         var->var->data.binding = var->binding;
         var->var->data.descriptor_set = var->descriptor_set;

         if (var->mode == vtn_variable_mode_image)
            var->var->data.image.format = without_array->image_format;
      }

      if (var->mode == vtn_variable_mode_local) {
         assert(var->members == NULL && var->var != NULL);
         nir_function_impl_add_variable(b->impl, var->var);
      } else if (var->var) {
         nir_shader_add_variable(b->shader, var->var);
      } else if (var->members) {
         unsigned count = glsl_get_length(without_array->type);
         for (unsigned i = 0; i < count; i++) {
            assert(var->members[i]->data.mode != nir_var_local);
            nir_shader_add_variable(b->shader, var->members[i]);
         }
      } else {
         assert(var->mode == vtn_variable_mode_ubo ||
                var->mode == vtn_variable_mode_ssbo ||
                var->mode == vtn_variable_mode_push_constant);
      }
      break;
   }

   case SpvOpAccessChain:
   case SpvOpInBoundsAccessChain: {
      struct vtn_access_chain *base, *chain;
      struct vtn_value *base_val = vtn_untyped_value(b, w[3]);
      if (base_val->value_type == vtn_value_type_sampled_image) {
         /* This is rather insane.  SPIR-V allows you to use OpSampledImage
          * to combine an array of images with a single sampler to get an
          * array of sampled images that all share the same sampler.
          * Fortunately, this means that we can more-or-less ignore the
          * sampler when crawling the access chain, but it does leave us
          * with this rather awkward little special-case.
          */
         base = base_val->sampled_image->image;
      } else {
         assert(base_val->value_type == vtn_value_type_access_chain);
         base = base_val->access_chain;
      }

      chain = vtn_access_chain_extend(b, base, count - 4);

      unsigned idx = base->length;
      for (int i = 4; i < count; i++) {
         struct vtn_value *link_val = vtn_untyped_value(b, w[i]);
         if (link_val->value_type == vtn_value_type_constant) {
            chain->link[idx].mode = vtn_access_mode_literal;
            chain->link[idx].id = link_val->constant->value.u[0];
         } else {
            chain->link[idx].mode = vtn_access_mode_id;
            chain->link[idx].id = w[i];
         }
         idx++;
      }

      if (base_val->value_type == vtn_value_type_sampled_image) {
         struct vtn_value *val =
            vtn_push_value(b, w[2], vtn_value_type_sampled_image);
         val->sampled_image = ralloc(b, struct vtn_sampled_image);
         val->sampled_image->image = chain;
         val->sampled_image->sampler = base_val->sampled_image->sampler;
      } else {
         struct vtn_value *val =
            vtn_push_value(b, w[2], vtn_value_type_access_chain);
         val->access_chain = chain;
      }
      break;
   }

   case SpvOpCopyMemory: {
      struct vtn_value *dest = vtn_value(b, w[1], vtn_value_type_access_chain);
      struct vtn_value *src = vtn_value(b, w[2], vtn_value_type_access_chain);

      vtn_variable_copy(b, dest->access_chain, src->access_chain);
      break;
   }

   case SpvOpLoad: {
      struct vtn_access_chain *src =
         vtn_value(b, w[3], vtn_value_type_access_chain)->access_chain;

      if (src->var->mode == vtn_variable_mode_image ||
          src->var->mode == vtn_variable_mode_sampler) {
         vtn_push_value(b, w[2], vtn_value_type_access_chain)->access_chain = src;
         return;
      }

      struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
      val->ssa = vtn_variable_load(b, src);
      break;
   }

   case SpvOpStore: {
      struct vtn_access_chain *dest =
         vtn_value(b, w[1], vtn_value_type_access_chain)->access_chain;
      struct vtn_ssa_value *src = vtn_ssa_value(b, w[2]);
      vtn_variable_store(b, src, dest);
      break;
   }

   case SpvOpArrayLength: {
      struct vtn_access_chain *chain =
         vtn_value(b, w[3], vtn_value_type_access_chain)->access_chain;

      const uint32_t offset = chain->var->type->offsets[w[4]];
      const uint32_t stride = chain->var->type->members[w[4]]->stride;

      unsigned chain_idx;
      struct vtn_type *type;
      nir_ssa_def *index =
         get_vulkan_resource_index(b, chain, &type, &chain_idx);

      nir_intrinsic_instr *instr =
         nir_intrinsic_instr_create(b->nb.shader,
                                    nir_intrinsic_get_buffer_size);
      instr->src[0] = nir_src_for_ssa(index);
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
      unreachable("Unhandled opcode");
   }
}
