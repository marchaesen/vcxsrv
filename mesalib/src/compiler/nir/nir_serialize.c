/*
 * Copyright © 2017 Connor Abbott
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

#include "nir_serialize.h"
#include "nir_control_flow.h"
#include "util/u_dynarray.h"

typedef struct {
   size_t blob_offset;
   nir_ssa_def *src;
   nir_block *block;
} write_phi_fixup;

typedef struct {
   const nir_shader *nir;

   struct blob *blob;

   /* maps pointer to index */
   struct hash_table *remap_table;

   /* the next index to assign to a NIR in-memory object */
   uintptr_t next_idx;

   /* Array of write_phi_fixup structs representing phi sources that need to
    * be resolved in the second pass.
    */
   struct util_dynarray phi_fixups;
} write_ctx;

typedef struct {
   nir_shader *nir;

   struct blob_reader *blob;

   /* the next index to assign to a NIR in-memory object */
   uintptr_t next_idx;

   /* The length of the index -> object table */
   uintptr_t idx_table_len;

   /* map from index to deserialized pointer */
   void **idx_table;

   /* List of phi sources. */
   struct list_head phi_srcs;

} read_ctx;

static void
write_add_object(write_ctx *ctx, const void *obj)
{
   uintptr_t index = ctx->next_idx++;
   _mesa_hash_table_insert(ctx->remap_table, obj, (void *) index);
}

static uintptr_t
write_lookup_object(write_ctx *ctx, const void *obj)
{
   struct hash_entry *entry = _mesa_hash_table_search(ctx->remap_table, obj);
   assert(entry);
   return (uintptr_t) entry->data;
}

static void
write_object(write_ctx *ctx, const void *obj)
{
   blob_write_intptr(ctx->blob, write_lookup_object(ctx, obj));
}

static void
read_add_object(read_ctx *ctx, void *obj)
{
   assert(ctx->next_idx < ctx->idx_table_len);
   ctx->idx_table[ctx->next_idx++] = obj;
}

static void *
read_lookup_object(read_ctx *ctx, uintptr_t idx)
{
   assert(idx < ctx->idx_table_len);
   return ctx->idx_table[idx];
}

static void *
read_object(read_ctx *ctx)
{
   return read_lookup_object(ctx, blob_read_intptr(ctx->blob));
}

static void
write_constant(write_ctx *ctx, const nir_constant *c)
{
   blob_write_bytes(ctx->blob, c->values, sizeof(c->values));
   blob_write_uint32(ctx->blob, c->num_elements);
   for (unsigned i = 0; i < c->num_elements; i++)
      write_constant(ctx, c->elements[i]);
}

static nir_constant *
read_constant(read_ctx *ctx, nir_variable *nvar)
{
   nir_constant *c = ralloc(nvar, nir_constant);

   blob_copy_bytes(ctx->blob, (uint8_t *)c->values, sizeof(c->values));
   c->num_elements = blob_read_uint32(ctx->blob);
   c->elements = ralloc_array(ctx->nir, nir_constant *, c->num_elements);
   for (unsigned i = 0; i < c->num_elements; i++)
      c->elements[i] = read_constant(ctx, nvar);

   return c;
}

static void
write_variable(write_ctx *ctx, const nir_variable *var)
{
   write_add_object(ctx, var);
   encode_type_to_blob(ctx->blob, var->type);
   blob_write_uint32(ctx->blob, !!(var->name));
   blob_write_string(ctx->blob, var->name);
   blob_write_bytes(ctx->blob, (uint8_t *) &var->data, sizeof(var->data));
   blob_write_uint32(ctx->blob, var->num_state_slots);
   blob_write_bytes(ctx->blob, (uint8_t *) var->state_slots,
                    var->num_state_slots * sizeof(nir_state_slot));
   blob_write_uint32(ctx->blob, !!(var->constant_initializer));
   if (var->constant_initializer)
      write_constant(ctx, var->constant_initializer);
   blob_write_uint32(ctx->blob, !!(var->interface_type));
   if (var->interface_type)
      encode_type_to_blob(ctx->blob, var->interface_type);
}

static nir_variable *
read_variable(read_ctx *ctx)
{
   nir_variable *var = rzalloc(ctx->nir, nir_variable);
   read_add_object(ctx, var);

   var->type = decode_type_from_blob(ctx->blob);
   bool has_name = blob_read_uint32(ctx->blob);
   if (has_name) {
      const char *name = blob_read_string(ctx->blob);
      var->name = ralloc_strdup(var, name);
   } else {
      var->name = NULL;
   }
   blob_copy_bytes(ctx->blob, (uint8_t *) &var->data, sizeof(var->data));
   var->num_state_slots = blob_read_uint32(ctx->blob);
   var->state_slots = ralloc_array(var, nir_state_slot, var->num_state_slots);
   blob_copy_bytes(ctx->blob, (uint8_t *) var->state_slots,
                   var->num_state_slots * sizeof(nir_state_slot));
   bool has_const_initializer = blob_read_uint32(ctx->blob);
   if (has_const_initializer)
      var->constant_initializer = read_constant(ctx, var);
   else
      var->constant_initializer = NULL;
   bool has_interface_type = blob_read_uint32(ctx->blob);
   if (has_interface_type)
      var->interface_type = decode_type_from_blob(ctx->blob);
   else
      var->interface_type = NULL;

   return var;
}

static void
write_var_list(write_ctx *ctx, const struct exec_list *src)
{
   blob_write_uint32(ctx->blob, exec_list_length(src));
   foreach_list_typed(nir_variable, var, node, src) {
      write_variable(ctx, var);
   }
}

static void
read_var_list(read_ctx *ctx, struct exec_list *dst)
{
   exec_list_make_empty(dst);
   unsigned num_vars = blob_read_uint32(ctx->blob);
   for (unsigned i = 0; i < num_vars; i++) {
      nir_variable *var = read_variable(ctx);
      exec_list_push_tail(dst, &var->node);
   }
}

static void
write_register(write_ctx *ctx, const nir_register *reg)
{
   write_add_object(ctx, reg);
   blob_write_uint32(ctx->blob, reg->num_components);
   blob_write_uint32(ctx->blob, reg->bit_size);
   blob_write_uint32(ctx->blob, reg->num_array_elems);
   blob_write_uint32(ctx->blob, reg->index);
   blob_write_uint32(ctx->blob, !!(reg->name));
   if (reg->name)
      blob_write_string(ctx->blob, reg->name);
   blob_write_uint32(ctx->blob, reg->is_global << 1 | reg->is_packed);
}

static nir_register *
read_register(read_ctx *ctx)
{
   nir_register *reg = ralloc(ctx->nir, nir_register);
   read_add_object(ctx, reg);
   reg->num_components = blob_read_uint32(ctx->blob);
   reg->bit_size = blob_read_uint32(ctx->blob);
   reg->num_array_elems = blob_read_uint32(ctx->blob);
   reg->index = blob_read_uint32(ctx->blob);
   bool has_name = blob_read_uint32(ctx->blob);
   if (has_name) {
      const char *name = blob_read_string(ctx->blob);
      reg->name = ralloc_strdup(reg, name);
   } else {
      reg->name = NULL;
   }
   unsigned flags = blob_read_uint32(ctx->blob);
   reg->is_global = flags & 0x2;
   reg->is_packed = flags & 0x1;

   list_inithead(&reg->uses);
   list_inithead(&reg->defs);
   list_inithead(&reg->if_uses);

   return reg;
}

static void
write_reg_list(write_ctx *ctx, const struct exec_list *src)
{
   blob_write_uint32(ctx->blob, exec_list_length(src));
   foreach_list_typed(nir_register, reg, node, src)
      write_register(ctx, reg);
}

static void
read_reg_list(read_ctx *ctx, struct exec_list *dst)
{
   exec_list_make_empty(dst);
   unsigned num_regs = blob_read_uint32(ctx->blob);
   for (unsigned i = 0; i < num_regs; i++) {
      nir_register *reg = read_register(ctx);
      exec_list_push_tail(dst, &reg->node);
   }
}

static void
write_src(write_ctx *ctx, const nir_src *src)
{
   /* Since sources are very frequent, we try to save some space when storing
    * them. In particular, we store whether the source is a register and
    * whether the register has an indirect index in the low two bits. We can
    * assume that the high two bits of the index are zero, since otherwise our
    * address space would've been exhausted allocating the remap table!
    */
   if (src->is_ssa) {
      uintptr_t idx = write_lookup_object(ctx, src->ssa) << 2;
      idx |= 1;
      blob_write_intptr(ctx->blob, idx);
   } else {
      uintptr_t idx = write_lookup_object(ctx, src->reg.reg) << 2;
      if (src->reg.indirect)
         idx |= 2;
      blob_write_intptr(ctx->blob, idx);
      blob_write_uint32(ctx->blob, src->reg.base_offset);
      if (src->reg.indirect) {
         write_src(ctx, src->reg.indirect);
      }
   }
}

static void
read_src(read_ctx *ctx, nir_src *src, void *mem_ctx)
{
   uintptr_t val = blob_read_intptr(ctx->blob);
   uintptr_t idx = val >> 2;
   src->is_ssa = val & 0x1;
   if (src->is_ssa) {
      src->ssa = read_lookup_object(ctx, idx);
   } else {
      bool is_indirect = val & 0x2;
      src->reg.reg = read_lookup_object(ctx, idx);
      src->reg.base_offset = blob_read_uint32(ctx->blob);
      if (is_indirect) {
         src->reg.indirect = ralloc(mem_ctx, nir_src);
         read_src(ctx, src->reg.indirect, mem_ctx);
      } else {
         src->reg.indirect = NULL;
      }
   }
}

static void
write_dest(write_ctx *ctx, const nir_dest *dst)
{
   uint32_t val = dst->is_ssa;
   if (dst->is_ssa) {
      val |= !!(dst->ssa.name) << 1;
      val |= dst->ssa.num_components << 2;
      val |= dst->ssa.bit_size << 5;
   } else {
      val |= !!(dst->reg.indirect) << 1;
   }
   blob_write_uint32(ctx->blob, val);
   if (dst->is_ssa) {
      write_add_object(ctx, &dst->ssa);
      if (dst->ssa.name)
         blob_write_string(ctx->blob, dst->ssa.name);
   } else {
      blob_write_intptr(ctx->blob, write_lookup_object(ctx, dst->reg.reg));
      blob_write_uint32(ctx->blob, dst->reg.base_offset);
      if (dst->reg.indirect)
         write_src(ctx, dst->reg.indirect);
   }
}

static void
read_dest(read_ctx *ctx, nir_dest *dst, nir_instr *instr)
{
   uint32_t val = blob_read_uint32(ctx->blob);
   bool is_ssa = val & 0x1;
   if (is_ssa) {
      bool has_name = val & 0x2;
      unsigned num_components = (val >> 2) & 0x7;
      unsigned bit_size = val >> 5;
      char *name = has_name ? blob_read_string(ctx->blob) : NULL;
      nir_ssa_dest_init(instr, dst, num_components, bit_size, name);
      read_add_object(ctx, &dst->ssa);
   } else {
      bool is_indirect = val & 0x2;
      dst->reg.reg = read_object(ctx);
      dst->reg.base_offset = blob_read_uint32(ctx->blob);
      if (is_indirect) {
         dst->reg.indirect = ralloc(instr, nir_src);
         read_src(ctx, dst->reg.indirect, instr);
      }
   }
}

static void
write_deref_chain(write_ctx *ctx, const nir_deref_var *deref_var)
{
   write_object(ctx, deref_var->var);

   uint32_t len = 0;
   for (const nir_deref *d = deref_var->deref.child; d; d = d->child)
      len++;
   blob_write_uint32(ctx->blob, len);

   for (const nir_deref *d = deref_var->deref.child; d; d = d->child) {
      blob_write_uint32(ctx->blob, d->deref_type);
      switch (d->deref_type) {
      case nir_deref_type_array: {
         const nir_deref_array *deref_array = nir_deref_as_array(d);
         blob_write_uint32(ctx->blob, deref_array->deref_array_type);
         blob_write_uint32(ctx->blob, deref_array->base_offset);
         if (deref_array->deref_array_type == nir_deref_array_type_indirect)
            write_src(ctx, &deref_array->indirect);
         break;
      }
      case nir_deref_type_struct: {
         const nir_deref_struct *deref_struct = nir_deref_as_struct(d);
         blob_write_uint32(ctx->blob, deref_struct->index);
         break;
      }
      case nir_deref_type_var:
         unreachable("Invalid deref type");
      }

      encode_type_to_blob(ctx->blob, d->type);
   }
}

static nir_deref_var *
read_deref_chain(read_ctx *ctx, void *mem_ctx)
{
   nir_variable *var = read_object(ctx);
   nir_deref_var *deref_var = nir_deref_var_create(mem_ctx, var);

   uint32_t len = blob_read_uint32(ctx->blob);

   nir_deref *tail = &deref_var->deref;
   for (uint32_t i = 0; i < len; i++) {
      nir_deref_type deref_type = blob_read_uint32(ctx->blob);
      nir_deref *deref = NULL;
      switch (deref_type) {
      case nir_deref_type_array: {
         nir_deref_array *deref_array = nir_deref_array_create(tail);
         deref_array->deref_array_type = blob_read_uint32(ctx->blob);
         deref_array->base_offset = blob_read_uint32(ctx->blob);
         if (deref_array->deref_array_type == nir_deref_array_type_indirect)
            read_src(ctx, &deref_array->indirect, mem_ctx);
         deref = &deref_array->deref;
         break;
      }
      case nir_deref_type_struct: {
         uint32_t index = blob_read_uint32(ctx->blob);
         nir_deref_struct *deref_struct = nir_deref_struct_create(tail, index);
         deref = &deref_struct->deref;
         break;
      }
      case nir_deref_type_var:
         unreachable("Invalid deref type");
      }

      deref->type = decode_type_from_blob(ctx->blob);

      tail->child = deref;
      tail = deref;
   }

   return deref_var;
}

static void
write_alu(write_ctx *ctx, const nir_alu_instr *alu)
{
   blob_write_uint32(ctx->blob, alu->op);
   uint32_t flags = alu->exact;
   flags |= alu->dest.saturate << 1;
   flags |= alu->dest.write_mask << 2;
   blob_write_uint32(ctx->blob, flags);

   write_dest(ctx, &alu->dest.dest);

   for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
      write_src(ctx, &alu->src[i].src);
      flags = alu->src[i].negate;
      flags |= alu->src[i].abs << 1;
      for (unsigned j = 0; j < 4; j++)
         flags |= alu->src[i].swizzle[j] << (2 + 2 * j);
      blob_write_uint32(ctx->blob, flags);
   }
}

static nir_alu_instr *
read_alu(read_ctx *ctx)
{
   nir_op op = blob_read_uint32(ctx->blob);
   nir_alu_instr *alu = nir_alu_instr_create(ctx->nir, op);

   uint32_t flags = blob_read_uint32(ctx->blob);
   alu->exact = flags & 1;
   alu->dest.saturate = flags & 2;
   alu->dest.write_mask = flags >> 2;

   read_dest(ctx, &alu->dest.dest, &alu->instr);

   for (unsigned i = 0; i < nir_op_infos[op].num_inputs; i++) {
      read_src(ctx, &alu->src[i].src, &alu->instr);
      flags = blob_read_uint32(ctx->blob);
      alu->src[i].negate = flags & 1;
      alu->src[i].abs = flags & 2;
      for (unsigned j = 0; j < 4; j++)
         alu->src[i].swizzle[j] = (flags >> (2 * j + 2)) & 3;
   }

   return alu;
}

static void
write_intrinsic(write_ctx *ctx, const nir_intrinsic_instr *intrin)
{
   blob_write_uint32(ctx->blob, intrin->intrinsic);

   unsigned num_variables = nir_intrinsic_infos[intrin->intrinsic].num_variables;
   unsigned num_srcs = nir_intrinsic_infos[intrin->intrinsic].num_srcs;
   unsigned num_indices = nir_intrinsic_infos[intrin->intrinsic].num_indices;

   blob_write_uint32(ctx->blob, intrin->num_components);

   if (nir_intrinsic_infos[intrin->intrinsic].has_dest)
      write_dest(ctx, &intrin->dest);

   for (unsigned i = 0; i < num_variables; i++)
      write_deref_chain(ctx, intrin->variables[i]);

   for (unsigned i = 0; i < num_srcs; i++)
      write_src(ctx, &intrin->src[i]);

   for (unsigned i = 0; i < num_indices; i++)
      blob_write_uint32(ctx->blob, intrin->const_index[i]);
}

static nir_intrinsic_instr *
read_intrinsic(read_ctx *ctx)
{
   nir_intrinsic_op op = blob_read_uint32(ctx->blob);

   nir_intrinsic_instr *intrin = nir_intrinsic_instr_create(ctx->nir, op);

   unsigned num_variables = nir_intrinsic_infos[op].num_variables;
   unsigned num_srcs = nir_intrinsic_infos[op].num_srcs;
   unsigned num_indices = nir_intrinsic_infos[op].num_indices;

   intrin->num_components = blob_read_uint32(ctx->blob);

   if (nir_intrinsic_infos[op].has_dest)
      read_dest(ctx, &intrin->dest, &intrin->instr);

   for (unsigned i = 0; i < num_variables; i++)
      intrin->variables[i] = read_deref_chain(ctx, &intrin->instr);

   for (unsigned i = 0; i < num_srcs; i++)
      read_src(ctx, &intrin->src[i], &intrin->instr);

   for (unsigned i = 0; i < num_indices; i++)
      intrin->const_index[i] = blob_read_uint32(ctx->blob);

   return intrin;
}

static void
write_load_const(write_ctx *ctx, const nir_load_const_instr *lc)
{
   uint32_t val = lc->def.num_components;
   val |= lc->def.bit_size << 3;
   blob_write_uint32(ctx->blob, val);
   blob_write_bytes(ctx->blob, (uint8_t *) &lc->value, sizeof(lc->value));
   write_add_object(ctx, &lc->def);
}

static nir_load_const_instr *
read_load_const(read_ctx *ctx)
{
   uint32_t val = blob_read_uint32(ctx->blob);

   nir_load_const_instr *lc =
      nir_load_const_instr_create(ctx->nir, val & 0x7, val >> 3);

   blob_copy_bytes(ctx->blob, (uint8_t *) &lc->value, sizeof(lc->value));
   read_add_object(ctx, &lc->def);
   return lc;
}

static void
write_ssa_undef(write_ctx *ctx, const nir_ssa_undef_instr *undef)
{
   uint32_t val = undef->def.num_components;
   val |= undef->def.bit_size << 3;
   blob_write_uint32(ctx->blob, val);
   write_add_object(ctx, &undef->def);
}

static nir_ssa_undef_instr *
read_ssa_undef(read_ctx *ctx)
{
   uint32_t val = blob_read_uint32(ctx->blob);

   nir_ssa_undef_instr *undef =
      nir_ssa_undef_instr_create(ctx->nir, val & 0x7, val >> 3);

   read_add_object(ctx, &undef->def);
   return undef;
}

union packed_tex_data {
   uint32_t u32;
   struct {
      enum glsl_sampler_dim sampler_dim:4;
      nir_alu_type dest_type:8;
      unsigned coord_components:3;
      unsigned is_array:1;
      unsigned is_shadow:1;
      unsigned is_new_style_shadow:1;
      unsigned component:2;
      unsigned has_texture_deref:1;
      unsigned has_sampler_deref:1;
      unsigned unused:10; /* Mark unused for valgrind. */
   } u;
};

static void
write_tex(write_ctx *ctx, const nir_tex_instr *tex)
{
   blob_write_uint32(ctx->blob, tex->num_srcs);
   blob_write_uint32(ctx->blob, tex->op);
   blob_write_uint32(ctx->blob, tex->texture_index);
   blob_write_uint32(ctx->blob, tex->texture_array_size);
   blob_write_uint32(ctx->blob, tex->sampler_index);

   STATIC_ASSERT(sizeof(union packed_tex_data) == sizeof(uint32_t));
   union packed_tex_data packed = {
      .u.sampler_dim = tex->sampler_dim,
      .u.dest_type = tex->dest_type,
      .u.coord_components = tex->coord_components,
      .u.is_array = tex->is_array,
      .u.is_shadow = tex->is_shadow,
      .u.is_new_style_shadow = tex->is_new_style_shadow,
      .u.component = tex->component,
      .u.has_texture_deref = tex->texture != NULL,
      .u.has_sampler_deref = tex->sampler != NULL,
   };
   blob_write_uint32(ctx->blob, packed.u32);

   write_dest(ctx, &tex->dest);
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      blob_write_uint32(ctx->blob, tex->src[i].src_type);
      write_src(ctx, &tex->src[i].src);
   }

   if (tex->texture)
      write_deref_chain(ctx, tex->texture);
   if (tex->sampler)
      write_deref_chain(ctx, tex->sampler);
}

static nir_tex_instr *
read_tex(read_ctx *ctx)
{
   unsigned num_srcs = blob_read_uint32(ctx->blob);
   nir_tex_instr *tex = nir_tex_instr_create(ctx->nir, num_srcs);

   tex->op = blob_read_uint32(ctx->blob);
   tex->texture_index = blob_read_uint32(ctx->blob);
   tex->texture_array_size = blob_read_uint32(ctx->blob);
   tex->sampler_index = blob_read_uint32(ctx->blob);

   union packed_tex_data packed;
   packed.u32 = blob_read_uint32(ctx->blob);
   tex->sampler_dim = packed.u.sampler_dim;
   tex->dest_type = packed.u.dest_type;
   tex->coord_components = packed.u.coord_components;
   tex->is_array = packed.u.is_array;
   tex->is_shadow = packed.u.is_shadow;
   tex->is_new_style_shadow = packed.u.is_new_style_shadow;
   tex->component = packed.u.component;

   read_dest(ctx, &tex->dest, &tex->instr);
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      tex->src[i].src_type = blob_read_uint32(ctx->blob);
      read_src(ctx, &tex->src[i].src, &tex->instr);
   }

   tex->texture = packed.u.has_texture_deref ?
                  read_deref_chain(ctx, &tex->instr) : NULL;
   tex->sampler = packed.u.has_sampler_deref ?
                  read_deref_chain(ctx, &tex->instr) : NULL;

   return tex;
}

static void
write_phi(write_ctx *ctx, const nir_phi_instr *phi)
{
   /* Phi nodes are special, since they may reference SSA definitions and
    * basic blocks that don't exist yet. We leave two empty uintptr_t's here,
    * and then store enough information so that a later fixup pass can fill
    * them in correctly.
    */
   write_dest(ctx, &phi->dest);

   blob_write_uint32(ctx->blob, exec_list_length(&phi->srcs));

   nir_foreach_phi_src(src, phi) {
      assert(src->src.is_ssa);
      size_t blob_offset = blob_reserve_intptr(ctx->blob);
      MAYBE_UNUSED size_t blob_offset2 = blob_reserve_intptr(ctx->blob);
      assert(blob_offset + sizeof(uintptr_t) == blob_offset2);
      write_phi_fixup fixup = {
         .blob_offset = blob_offset,
         .src = src->src.ssa,
         .block = src->pred,
      };
      util_dynarray_append(&ctx->phi_fixups, write_phi_fixup, fixup);
   }
}

static void
write_fixup_phis(write_ctx *ctx)
{
   util_dynarray_foreach(&ctx->phi_fixups, write_phi_fixup, fixup) {
      uintptr_t *blob_ptr = (uintptr_t *)(ctx->blob->data + fixup->blob_offset);
      blob_ptr[0] = write_lookup_object(ctx, fixup->src);
      blob_ptr[1] = write_lookup_object(ctx, fixup->block);
   }

   util_dynarray_clear(&ctx->phi_fixups);
}

static nir_phi_instr *
read_phi(read_ctx *ctx, nir_block *blk)
{
   nir_phi_instr *phi = nir_phi_instr_create(ctx->nir);

   read_dest(ctx, &phi->dest, &phi->instr);

   unsigned num_srcs = blob_read_uint32(ctx->blob);

   /* For similar reasons as before, we just store the index directly into the
    * pointer, and let a later pass resolve the phi sources.
    *
    * In order to ensure that the copied sources (which are just the indices
    * from the blob for now) don't get inserted into the old shader's use-def
    * lists, we have to add the phi instruction *before* we set up its
    * sources.
    */
   nir_instr_insert_after_block(blk, &phi->instr);

   for (unsigned i = 0; i < num_srcs; i++) {
      nir_phi_src *src = ralloc(phi, nir_phi_src);

      src->src.is_ssa = true;
      src->src.ssa = (nir_ssa_def *) blob_read_intptr(ctx->blob);
      src->pred = (nir_block *) blob_read_intptr(ctx->blob);

      /* Since we're not letting nir_insert_instr handle use/def stuff for us,
       * we have to set the parent_instr manually.  It doesn't really matter
       * when we do it, so we might as well do it here.
       */
      src->src.parent_instr = &phi->instr;

      /* Stash it in the list of phi sources.  We'll walk this list and fix up
       * sources at the very end of read_function_impl.
       */
      list_add(&src->src.use_link, &ctx->phi_srcs);

      exec_list_push_tail(&phi->srcs, &src->node);
   }

   return phi;
}

static void
read_fixup_phis(read_ctx *ctx)
{
   list_for_each_entry_safe(nir_phi_src, src, &ctx->phi_srcs, src.use_link) {
      src->pred = read_lookup_object(ctx, (uintptr_t)src->pred);
      src->src.ssa = read_lookup_object(ctx, (uintptr_t)src->src.ssa);

      /* Remove from this list */
      list_del(&src->src.use_link);

      list_addtail(&src->src.use_link, &src->src.ssa->uses);
   }
   assert(list_empty(&ctx->phi_srcs));
}

static void
write_jump(write_ctx *ctx, const nir_jump_instr *jmp)
{
   blob_write_uint32(ctx->blob, jmp->type);
}

static nir_jump_instr *
read_jump(read_ctx *ctx)
{
   nir_jump_type type = blob_read_uint32(ctx->blob);
   nir_jump_instr *jmp = nir_jump_instr_create(ctx->nir, type);
   return jmp;
}

static void
write_call(write_ctx *ctx, const nir_call_instr *call)
{
   blob_write_intptr(ctx->blob, write_lookup_object(ctx, call->callee));

   for (unsigned i = 0; i < call->num_params; i++)
      write_deref_chain(ctx, call->params[i]);

   write_deref_chain(ctx, call->return_deref);
}

static nir_call_instr *
read_call(read_ctx *ctx)
{
   nir_function *callee = read_object(ctx);
   nir_call_instr *call = nir_call_instr_create(ctx->nir, callee);

   for (unsigned i = 0; i < call->num_params; i++)
      call->params[i] = read_deref_chain(ctx, &call->instr);

   call->return_deref = read_deref_chain(ctx, &call->instr);

   return call;
}

static void
write_instr(write_ctx *ctx, const nir_instr *instr)
{
   blob_write_uint32(ctx->blob, instr->type);
   switch (instr->type) {
   case nir_instr_type_alu:
      write_alu(ctx, nir_instr_as_alu(instr));
      break;
   case nir_instr_type_intrinsic:
      write_intrinsic(ctx, nir_instr_as_intrinsic(instr));
      break;
   case nir_instr_type_load_const:
      write_load_const(ctx, nir_instr_as_load_const(instr));
      break;
   case nir_instr_type_ssa_undef:
      write_ssa_undef(ctx, nir_instr_as_ssa_undef(instr));
      break;
   case nir_instr_type_tex:
      write_tex(ctx, nir_instr_as_tex(instr));
      break;
   case nir_instr_type_phi:
      write_phi(ctx, nir_instr_as_phi(instr));
      break;
   case nir_instr_type_jump:
      write_jump(ctx, nir_instr_as_jump(instr));
      break;
   case nir_instr_type_call:
      write_call(ctx, nir_instr_as_call(instr));
      break;
   case nir_instr_type_parallel_copy:
      unreachable("Cannot write parallel copies");
   default:
      unreachable("bad instr type");
   }
}

static void
read_instr(read_ctx *ctx, nir_block *block)
{
   nir_instr_type type = blob_read_uint32(ctx->blob);
   nir_instr *instr;
   switch (type) {
   case nir_instr_type_alu:
      instr = &read_alu(ctx)->instr;
      break;
   case nir_instr_type_intrinsic:
      instr = &read_intrinsic(ctx)->instr;
      break;
   case nir_instr_type_load_const:
      instr = &read_load_const(ctx)->instr;
      break;
   case nir_instr_type_ssa_undef:
      instr = &read_ssa_undef(ctx)->instr;
      break;
   case nir_instr_type_tex:
      instr = &read_tex(ctx)->instr;
      break;
   case nir_instr_type_phi:
      /* Phi instructions are a bit of a special case when reading because we
       * don't want inserting the instruction to automatically handle use/defs
       * for us.  Instead, we need to wait until all the blocks/instructions
       * are read so that we can set their sources up.
       */
      read_phi(ctx, block);
      return;
   case nir_instr_type_jump:
      instr = &read_jump(ctx)->instr;
      break;
   case nir_instr_type_call:
      instr = &read_call(ctx)->instr;
      break;
   case nir_instr_type_parallel_copy:
      unreachable("Cannot read parallel copies");
   default:
      unreachable("bad instr type");
   }

   nir_instr_insert_after_block(block, instr);
}

static void
write_block(write_ctx *ctx, const nir_block *block)
{
   write_add_object(ctx, block);
   blob_write_uint32(ctx->blob, exec_list_length(&block->instr_list));
   nir_foreach_instr(instr, block)
      write_instr(ctx, instr);
}

static void
read_block(read_ctx *ctx, struct exec_list *cf_list)
{
   /* Don't actually create a new block.  Just use the one from the tail of
    * the list.  NIR guarantees that the tail of the list is a block and that
    * no two blocks are side-by-side in the IR;  It should be empty.
    */
   nir_block *block =
      exec_node_data(nir_block, exec_list_get_tail(cf_list), cf_node.node);

   read_add_object(ctx, block);
   unsigned num_instrs = blob_read_uint32(ctx->blob);
   for (unsigned i = 0; i < num_instrs; i++) {
      read_instr(ctx, block);
   }
}

static void
write_cf_list(write_ctx *ctx, const struct exec_list *cf_list);

static void
read_cf_list(read_ctx *ctx, struct exec_list *cf_list);

static void
write_if(write_ctx *ctx, nir_if *nif)
{
   write_src(ctx, &nif->condition);

   write_cf_list(ctx, &nif->then_list);
   write_cf_list(ctx, &nif->else_list);
}

static void
read_if(read_ctx *ctx, struct exec_list *cf_list)
{
   nir_if *nif = nir_if_create(ctx->nir);

   read_src(ctx, &nif->condition, nif);

   nir_cf_node_insert_end(cf_list, &nif->cf_node);

   read_cf_list(ctx, &nif->then_list);
   read_cf_list(ctx, &nif->else_list);
}

static void
write_loop(write_ctx *ctx, nir_loop *loop)
{
   write_cf_list(ctx, &loop->body);
}

static void
read_loop(read_ctx *ctx, struct exec_list *cf_list)
{
   nir_loop *loop = nir_loop_create(ctx->nir);

   nir_cf_node_insert_end(cf_list, &loop->cf_node);

   read_cf_list(ctx, &loop->body);
}

static void
write_cf_node(write_ctx *ctx, nir_cf_node *cf)
{
   blob_write_uint32(ctx->blob, cf->type);

   switch (cf->type) {
   case nir_cf_node_block:
      write_block(ctx, nir_cf_node_as_block(cf));
      break;
   case nir_cf_node_if:
      write_if(ctx, nir_cf_node_as_if(cf));
      break;
   case nir_cf_node_loop:
      write_loop(ctx, nir_cf_node_as_loop(cf));
      break;
   default:
      unreachable("bad cf type");
   }
}

static void
read_cf_node(read_ctx *ctx, struct exec_list *list)
{
   nir_cf_node_type type = blob_read_uint32(ctx->blob);

   switch (type) {
   case nir_cf_node_block:
      read_block(ctx, list);
      break;
   case nir_cf_node_if:
      read_if(ctx, list);
      break;
   case nir_cf_node_loop:
      read_loop(ctx, list);
      break;
   default:
      unreachable("bad cf type");
   }
}

static void
write_cf_list(write_ctx *ctx, const struct exec_list *cf_list)
{
   blob_write_uint32(ctx->blob, exec_list_length(cf_list));
   foreach_list_typed(nir_cf_node, cf, node, cf_list) {
      write_cf_node(ctx, cf);
   }
}

static void
read_cf_list(read_ctx *ctx, struct exec_list *cf_list)
{
   uint32_t num_cf_nodes = blob_read_uint32(ctx->blob);
   for (unsigned i = 0; i < num_cf_nodes; i++)
      read_cf_node(ctx, cf_list);
}

static void
write_function_impl(write_ctx *ctx, const nir_function_impl *fi)
{
   write_var_list(ctx, &fi->locals);
   write_reg_list(ctx, &fi->registers);
   blob_write_uint32(ctx->blob, fi->reg_alloc);

   blob_write_uint32(ctx->blob, fi->num_params);
   for (unsigned i = 0; i < fi->num_params; i++) {
      write_variable(ctx, fi->params[i]);
   }

   blob_write_uint32(ctx->blob, !!(fi->return_var));
   if (fi->return_var)
      write_variable(ctx, fi->return_var);

   write_cf_list(ctx, &fi->body);
   write_fixup_phis(ctx);
}

static nir_function_impl *
read_function_impl(read_ctx *ctx, nir_function *fxn)
{
   nir_function_impl *fi = nir_function_impl_create_bare(ctx->nir);
   fi->function = fxn;

   read_var_list(ctx, &fi->locals);
   read_reg_list(ctx, &fi->registers);
   fi->reg_alloc = blob_read_uint32(ctx->blob);

   fi->num_params = blob_read_uint32(ctx->blob);
   for (unsigned i = 0; i < fi->num_params; i++) {
      fi->params[i] = read_variable(ctx);
   }

   bool has_return = blob_read_uint32(ctx->blob);
   if (has_return)
      fi->return_var = read_variable(ctx);
   else
      fi->return_var = NULL;

   read_cf_list(ctx, &fi->body);
   read_fixup_phis(ctx);

   fi->valid_metadata = 0;

   return fi;
}

static void
write_function(write_ctx *ctx, const nir_function *fxn)
{
   blob_write_uint32(ctx->blob, !!(fxn->name));
   if (fxn->name)
      blob_write_string(ctx->blob, fxn->name);

   write_add_object(ctx, fxn);

   blob_write_uint32(ctx->blob, fxn->num_params);
   for (unsigned i = 0; i < fxn->num_params; i++) {
      blob_write_uint32(ctx->blob, fxn->params[i].param_type);
      encode_type_to_blob(ctx->blob, fxn->params[i].type);
   }

   encode_type_to_blob(ctx->blob, fxn->return_type);

   /* At first glance, it looks like we should write the function_impl here.
    * However, call instructions need to be able to reference at least the
    * function and those will get processed as we write the function_impls.
    * We stop here and write function_impls as a second pass.
    */
}

static void
read_function(read_ctx *ctx)
{
   bool has_name = blob_read_uint32(ctx->blob);
   char *name = has_name ? blob_read_string(ctx->blob) : NULL;

   nir_function *fxn = nir_function_create(ctx->nir, name);

   read_add_object(ctx, fxn);

   fxn->num_params = blob_read_uint32(ctx->blob);
   for (unsigned i = 0; i < fxn->num_params; i++) {
      fxn->params[i].param_type = blob_read_uint32(ctx->blob);
      fxn->params[i].type = decode_type_from_blob(ctx->blob);
   }

   fxn->return_type = decode_type_from_blob(ctx->blob);
}

void
nir_serialize(struct blob *blob, const nir_shader *nir)
{
   write_ctx ctx;
   ctx.remap_table = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                             _mesa_key_pointer_equal);
   ctx.next_idx = 0;
   ctx.blob = blob;
   ctx.nir = nir;
   util_dynarray_init(&ctx.phi_fixups, NULL);

   size_t idx_size_offset = blob_reserve_intptr(blob);

   struct shader_info info = nir->info;
   uint32_t strings = 0;
   if (info.name)
      strings |= 0x1;
   if (info.label)
      strings |= 0x2;
   blob_write_uint32(blob, strings);
   if (info.name)
      blob_write_string(blob, info.name);
   if (info.label)
      blob_write_string(blob, info.label);
   info.name = info.label = NULL;
   blob_write_bytes(blob, (uint8_t *) &info, sizeof(info));

   write_var_list(&ctx, &nir->uniforms);
   write_var_list(&ctx, &nir->inputs);
   write_var_list(&ctx, &nir->outputs);
   write_var_list(&ctx, &nir->shared);
   write_var_list(&ctx, &nir->globals);
   write_var_list(&ctx, &nir->system_values);

   write_reg_list(&ctx, &nir->registers);
   blob_write_uint32(blob, nir->reg_alloc);
   blob_write_uint32(blob, nir->num_inputs);
   blob_write_uint32(blob, nir->num_uniforms);
   blob_write_uint32(blob, nir->num_outputs);
   blob_write_uint32(blob, nir->num_shared);

   blob_write_uint32(blob, exec_list_length(&nir->functions));
   nir_foreach_function(fxn, nir) {
      write_function(&ctx, fxn);
   }

   nir_foreach_function(fxn, nir) {
      write_function_impl(&ctx, fxn->impl);
   }

   *(uintptr_t *)(blob->data + idx_size_offset) = ctx.next_idx;

   _mesa_hash_table_destroy(ctx.remap_table, NULL);
   util_dynarray_fini(&ctx.phi_fixups);
}

nir_shader *
nir_deserialize(void *mem_ctx,
                const struct nir_shader_compiler_options *options,
                struct blob_reader *blob)
{
   read_ctx ctx;
   ctx.blob = blob;
   list_inithead(&ctx.phi_srcs);
   ctx.idx_table_len = blob_read_intptr(blob);
   ctx.idx_table = calloc(ctx.idx_table_len, sizeof(uintptr_t));
   ctx.next_idx = 0;

   uint32_t strings = blob_read_uint32(blob);
   char *name = (strings & 0x1) ? blob_read_string(blob) : NULL;
   char *label = (strings & 0x2) ? blob_read_string(blob) : NULL;

   struct shader_info info;
   blob_copy_bytes(blob, (uint8_t *) &info, sizeof(info));

   ctx.nir = nir_shader_create(mem_ctx, info.stage, options, NULL);

   info.name = name ? ralloc_strdup(ctx.nir, name) : NULL;
   info.label = label ? ralloc_strdup(ctx.nir, label) : NULL;

   ctx.nir->info = info;

   read_var_list(&ctx, &ctx.nir->uniforms);
   read_var_list(&ctx, &ctx.nir->inputs);
   read_var_list(&ctx, &ctx.nir->outputs);
   read_var_list(&ctx, &ctx.nir->shared);
   read_var_list(&ctx, &ctx.nir->globals);
   read_var_list(&ctx, &ctx.nir->system_values);

   read_reg_list(&ctx, &ctx.nir->registers);
   ctx.nir->reg_alloc = blob_read_uint32(blob);
   ctx.nir->num_inputs = blob_read_uint32(blob);
   ctx.nir->num_uniforms = blob_read_uint32(blob);
   ctx.nir->num_outputs = blob_read_uint32(blob);
   ctx.nir->num_shared = blob_read_uint32(blob);

   unsigned num_functions = blob_read_uint32(blob);
   for (unsigned i = 0; i < num_functions; i++)
      read_function(&ctx);

   nir_foreach_function(fxn, ctx.nir)
      fxn->impl = read_function_impl(&ctx, fxn);

   free(ctx.idx_table);

   return ctx.nir;
}

nir_shader *
nir_shader_serialize_deserialize(void *mem_ctx, nir_shader *s)
{
   const struct nir_shader_compiler_options *options = s->options;

   struct blob writer;
   blob_init(&writer);
   nir_serialize(&writer, s);
   ralloc_free(s);

   struct blob_reader reader;
   blob_reader_init(&reader, writer.data, writer.size);
   nir_shader *ns = nir_deserialize(mem_ctx, options, &reader);

   blob_finish(&writer);

   return ns;
}
