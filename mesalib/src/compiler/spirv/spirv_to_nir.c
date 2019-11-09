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
#include "nir/nir_vla.h"
#include "nir/nir_control_flow.h"
#include "nir/nir_constant_expressions.h"
#include "nir/nir_deref.h"
#include "spirv_info.h"

#include "util/u_math.h"

#include <stdio.h>

void
vtn_log(struct vtn_builder *b, enum nir_spirv_debug_level level,
        size_t spirv_offset, const char *message)
{
   if (b->options->debug.func) {
      b->options->debug.func(b->options->debug.private_data,
                             level, spirv_offset, message);
   }

#ifndef NDEBUG
   if (level >= NIR_SPIRV_DEBUG_LEVEL_WARNING)
      fprintf(stderr, "%s\n", message);
#endif
}

void
vtn_logf(struct vtn_builder *b, enum nir_spirv_debug_level level,
         size_t spirv_offset, const char *fmt, ...)
{
   va_list args;
   char *msg;

   va_start(args, fmt);
   msg = ralloc_vasprintf(NULL, fmt, args);
   va_end(args);

   vtn_log(b, level, spirv_offset, msg);

   ralloc_free(msg);
}

static void
vtn_log_err(struct vtn_builder *b,
            enum nir_spirv_debug_level level, const char *prefix,
            const char *file, unsigned line,
            const char *fmt, va_list args)
{
   char *msg;

   msg = ralloc_strdup(NULL, prefix);

#ifndef NDEBUG
   ralloc_asprintf_append(&msg, "    In file %s:%u\n", file, line);
#endif

   ralloc_asprintf_append(&msg, "    ");

   ralloc_vasprintf_append(&msg, fmt, args);

   ralloc_asprintf_append(&msg, "\n    %zu bytes into the SPIR-V binary",
                          b->spirv_offset);

   if (b->file) {
      ralloc_asprintf_append(&msg,
                             "\n    in SPIR-V source file %s, line %d, col %d",
                             b->file, b->line, b->col);
   }

   vtn_log(b, level, b->spirv_offset, msg);

   ralloc_free(msg);
}

static void
vtn_dump_shader(struct vtn_builder *b, const char *path, const char *prefix)
{
   static int idx = 0;

   char filename[1024];
   int len = snprintf(filename, sizeof(filename), "%s/%s-%d.spirv",
                      path, prefix, idx++);
   if (len < 0 || len >= sizeof(filename))
      return;

   FILE *f = fopen(filename, "w");
   if (f == NULL)
      return;

   fwrite(b->spirv, sizeof(*b->spirv), b->spirv_word_count, f);
   fclose(f);

   vtn_info("SPIR-V shader dumped to %s", filename);
}

void
_vtn_warn(struct vtn_builder *b, const char *file, unsigned line,
          const char *fmt, ...)
{
   va_list args;

   va_start(args, fmt);
   vtn_log_err(b, NIR_SPIRV_DEBUG_LEVEL_WARNING, "SPIR-V WARNING:\n",
               file, line, fmt, args);
   va_end(args);
}

void
_vtn_err(struct vtn_builder *b, const char *file, unsigned line,
          const char *fmt, ...)
{
   va_list args;

   va_start(args, fmt);
   vtn_log_err(b, NIR_SPIRV_DEBUG_LEVEL_ERROR, "SPIR-V ERROR:\n",
               file, line, fmt, args);
   va_end(args);
}

void
_vtn_fail(struct vtn_builder *b, const char *file, unsigned line,
          const char *fmt, ...)
{
   va_list args;

   va_start(args, fmt);
   vtn_log_err(b, NIR_SPIRV_DEBUG_LEVEL_ERROR, "SPIR-V parsing FAILED:\n",
               file, line, fmt, args);
   va_end(args);

   const char *dump_path = getenv("MESA_SPIRV_FAIL_DUMP_PATH");
   if (dump_path)
      vtn_dump_shader(b, dump_path, "fail");

   longjmp(b->fail_jump, 1);
}

struct spec_constant_value {
   bool is_double;
   union {
      uint32_t data32;
      uint64_t data64;
   };
};

static struct vtn_ssa_value *
vtn_undef_ssa_value(struct vtn_builder *b, const struct glsl_type *type)
{
   struct vtn_ssa_value *val = rzalloc(b, struct vtn_ssa_value);
   val->type = type;

   if (glsl_type_is_vector_or_scalar(type)) {
      unsigned num_components = glsl_get_vector_elements(val->type);
      unsigned bit_size = glsl_get_bit_size(val->type);
      val->def = nir_ssa_undef(&b->nb, num_components, bit_size);
   } else {
      unsigned elems = glsl_get_length(val->type);
      val->elems = ralloc_array(b, struct vtn_ssa_value *, elems);
      if (glsl_type_is_matrix(type)) {
         const struct glsl_type *elem_type =
            glsl_vector_type(glsl_get_base_type(type),
                             glsl_get_vector_elements(type));

         for (unsigned i = 0; i < elems; i++)
            val->elems[i] = vtn_undef_ssa_value(b, elem_type);
      } else if (glsl_type_is_array(type)) {
         const struct glsl_type *elem_type = glsl_get_array_element(type);
         for (unsigned i = 0; i < elems; i++)
            val->elems[i] = vtn_undef_ssa_value(b, elem_type);
      } else {
         for (unsigned i = 0; i < elems; i++) {
            const struct glsl_type *elem_type = glsl_get_struct_field(type, i);
            val->elems[i] = vtn_undef_ssa_value(b, elem_type);
         }
      }
   }

   return val;
}

static struct vtn_ssa_value *
vtn_const_ssa_value(struct vtn_builder *b, nir_constant *constant,
                    const struct glsl_type *type)
{
   struct hash_entry *entry = _mesa_hash_table_search(b->const_table, constant);

   if (entry)
      return entry->data;

   struct vtn_ssa_value *val = rzalloc(b, struct vtn_ssa_value);
   val->type = type;

   switch (glsl_get_base_type(type)) {
   case GLSL_TYPE_INT:
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_INT16:
   case GLSL_TYPE_UINT16:
   case GLSL_TYPE_UINT8:
   case GLSL_TYPE_INT8:
   case GLSL_TYPE_INT64:
   case GLSL_TYPE_UINT64:
   case GLSL_TYPE_BOOL:
   case GLSL_TYPE_FLOAT:
   case GLSL_TYPE_FLOAT16:
   case GLSL_TYPE_DOUBLE: {
      int bit_size = glsl_get_bit_size(type);
      if (glsl_type_is_vector_or_scalar(type)) {
         unsigned num_components = glsl_get_vector_elements(val->type);
         nir_load_const_instr *load =
            nir_load_const_instr_create(b->shader, num_components, bit_size);

         memcpy(load->value, constant->values,
                sizeof(nir_const_value) * load->def.num_components);

         nir_instr_insert_before_cf_list(&b->nb.impl->body, &load->instr);
         val->def = &load->def;
      } else {
         assert(glsl_type_is_matrix(type));
         unsigned columns = glsl_get_matrix_columns(val->type);
         val->elems = ralloc_array(b, struct vtn_ssa_value *, columns);
         const struct glsl_type *column_type = glsl_get_column_type(val->type);
         for (unsigned i = 0; i < columns; i++)
            val->elems[i] = vtn_const_ssa_value(b, constant->elements[i],
                                                column_type);
      }
      break;
   }

   case GLSL_TYPE_ARRAY: {
      unsigned elems = glsl_get_length(val->type);
      val->elems = ralloc_array(b, struct vtn_ssa_value *, elems);
      const struct glsl_type *elem_type = glsl_get_array_element(val->type);
      for (unsigned i = 0; i < elems; i++)
         val->elems[i] = vtn_const_ssa_value(b, constant->elements[i],
                                             elem_type);
      break;
   }

   case GLSL_TYPE_STRUCT: {
      unsigned elems = glsl_get_length(val->type);
      val->elems = ralloc_array(b, struct vtn_ssa_value *, elems);
      for (unsigned i = 0; i < elems; i++) {
         const struct glsl_type *elem_type =
            glsl_get_struct_field(val->type, i);
         val->elems[i] = vtn_const_ssa_value(b, constant->elements[i],
                                             elem_type);
      }
      break;
   }

   default:
      vtn_fail("bad constant type");
   }

   return val;
}

struct vtn_ssa_value *
vtn_ssa_value(struct vtn_builder *b, uint32_t value_id)
{
   struct vtn_value *val = vtn_untyped_value(b, value_id);
   switch (val->value_type) {
   case vtn_value_type_undef:
      return vtn_undef_ssa_value(b, val->type->type);

   case vtn_value_type_constant:
      return vtn_const_ssa_value(b, val->constant, val->type->type);

   case vtn_value_type_ssa:
      return val->ssa;

   case vtn_value_type_pointer:
      vtn_assert(val->pointer->ptr_type && val->pointer->ptr_type->type);
      struct vtn_ssa_value *ssa =
         vtn_create_ssa_value(b, val->pointer->ptr_type->type);
      ssa->def = vtn_pointer_to_ssa(b, val->pointer);
      return ssa;

   default:
      vtn_fail("Invalid type for an SSA value");
   }
}

static char *
vtn_string_literal(struct vtn_builder *b, const uint32_t *words,
                   unsigned word_count, unsigned *words_used)
{
   char *dup = ralloc_strndup(b, (char *)words, word_count * sizeof(*words));
   if (words_used) {
      /* Ammount of space taken by the string (including the null) */
      unsigned len = strlen(dup) + 1;
      *words_used = DIV_ROUND_UP(len, sizeof(*words));
   }
   return dup;
}

const uint32_t *
vtn_foreach_instruction(struct vtn_builder *b, const uint32_t *start,
                        const uint32_t *end, vtn_instruction_handler handler)
{
   b->file = NULL;
   b->line = -1;
   b->col = -1;

   const uint32_t *w = start;
   while (w < end) {
      SpvOp opcode = w[0] & SpvOpCodeMask;
      unsigned count = w[0] >> SpvWordCountShift;
      vtn_assert(count >= 1 && w + count <= end);

      b->spirv_offset = (uint8_t *)w - (uint8_t *)b->spirv;

      switch (opcode) {
      case SpvOpNop:
         break; /* Do nothing */

      case SpvOpLine:
         b->file = vtn_value(b, w[1], vtn_value_type_string)->str;
         b->line = w[2];
         b->col = w[3];
         break;

      case SpvOpNoLine:
         b->file = NULL;
         b->line = -1;
         b->col = -1;
         break;

      default:
         if (!handler(b, opcode, w, count))
            return w;
         break;
      }

      w += count;
   }

   b->spirv_offset = 0;
   b->file = NULL;
   b->line = -1;
   b->col = -1;

   assert(w == end);
   return w;
}

static void
vtn_handle_extension(struct vtn_builder *b, SpvOp opcode,
                     const uint32_t *w, unsigned count)
{
   const char *ext = (const char *)&w[2];
   switch (opcode) {
   case SpvOpExtInstImport: {
      struct vtn_value *val = vtn_push_value(b, w[1], vtn_value_type_extension);
      if (strcmp(ext, "GLSL.std.450") == 0) {
         val->ext_handler = vtn_handle_glsl450_instruction;
      } else if ((strcmp(ext, "SPV_AMD_gcn_shader") == 0)
                && (b->options && b->options->caps.amd_gcn_shader)) {
         val->ext_handler = vtn_handle_amd_gcn_shader_instruction;
      } else if ((strcmp(ext, "SPV_AMD_shader_ballot") == 0)
                && (b->options && b->options->caps.amd_shader_ballot)) {
         val->ext_handler = vtn_handle_amd_shader_ballot_instruction;
      } else if ((strcmp(ext, "SPV_AMD_shader_trinary_minmax") == 0)
                && (b->options && b->options->caps.amd_trinary_minmax)) {
         val->ext_handler = vtn_handle_amd_shader_trinary_minmax_instruction;
      } else if (strcmp(ext, "OpenCL.std") == 0) {
         val->ext_handler = vtn_handle_opencl_instruction;
      } else {
         vtn_fail("Unsupported extension: %s", ext);
      }
      break;
   }

   case SpvOpExtInst: {
      struct vtn_value *val = vtn_value(b, w[3], vtn_value_type_extension);
      bool handled = val->ext_handler(b, w[4], w, count);
      vtn_assert(handled);
      break;
   }

   default:
      vtn_fail_with_opcode("Unhandled opcode", opcode);
   }
}

static void
_foreach_decoration_helper(struct vtn_builder *b,
                           struct vtn_value *base_value,
                           int parent_member,
                           struct vtn_value *value,
                           vtn_decoration_foreach_cb cb, void *data)
{
   for (struct vtn_decoration *dec = value->decoration; dec; dec = dec->next) {
      int member;
      if (dec->scope == VTN_DEC_DECORATION) {
         member = parent_member;
      } else if (dec->scope >= VTN_DEC_STRUCT_MEMBER0) {
         vtn_fail_if(value->value_type != vtn_value_type_type ||
                     value->type->base_type != vtn_base_type_struct,
                     "OpMemberDecorate and OpGroupMemberDecorate are only "
                     "allowed on OpTypeStruct");
         /* This means we haven't recursed yet */
         assert(value == base_value);

         member = dec->scope - VTN_DEC_STRUCT_MEMBER0;

         vtn_fail_if(member >= base_value->type->length,
                     "OpMemberDecorate specifies member %d but the "
                     "OpTypeStruct has only %u members",
                     member, base_value->type->length);
      } else {
         /* Not a decoration */
         assert(dec->scope == VTN_DEC_EXECUTION_MODE);
         continue;
      }

      if (dec->group) {
         assert(dec->group->value_type == vtn_value_type_decoration_group);
         _foreach_decoration_helper(b, base_value, member, dec->group,
                                    cb, data);
      } else {
         cb(b, base_value, member, dec, data);
      }
   }
}

/** Iterates (recursively if needed) over all of the decorations on a value
 *
 * This function iterates over all of the decorations applied to a given
 * value.  If it encounters a decoration group, it recurses into the group
 * and iterates over all of those decorations as well.
 */
void
vtn_foreach_decoration(struct vtn_builder *b, struct vtn_value *value,
                       vtn_decoration_foreach_cb cb, void *data)
{
   _foreach_decoration_helper(b, value, -1, value, cb, data);
}

void
vtn_foreach_execution_mode(struct vtn_builder *b, struct vtn_value *value,
                           vtn_execution_mode_foreach_cb cb, void *data)
{
   for (struct vtn_decoration *dec = value->decoration; dec; dec = dec->next) {
      if (dec->scope != VTN_DEC_EXECUTION_MODE)
         continue;

      assert(dec->group == NULL);
      cb(b, value, dec, data);
   }
}

void
vtn_handle_decoration(struct vtn_builder *b, SpvOp opcode,
                      const uint32_t *w, unsigned count)
{
   const uint32_t *w_end = w + count;
   const uint32_t target = w[1];
   w += 2;

   switch (opcode) {
   case SpvOpDecorationGroup:
      vtn_push_value(b, target, vtn_value_type_decoration_group);
      break;

   case SpvOpDecorate:
   case SpvOpDecorateId:
   case SpvOpMemberDecorate:
   case SpvOpDecorateString:
   case SpvOpMemberDecorateString:
   case SpvOpExecutionMode:
   case SpvOpExecutionModeId: {
      struct vtn_value *val = vtn_untyped_value(b, target);

      struct vtn_decoration *dec = rzalloc(b, struct vtn_decoration);
      switch (opcode) {
      case SpvOpDecorate:
      case SpvOpDecorateId:
      case SpvOpDecorateString:
         dec->scope = VTN_DEC_DECORATION;
         break;
      case SpvOpMemberDecorate:
      case SpvOpMemberDecorateString:
         dec->scope = VTN_DEC_STRUCT_MEMBER0 + *(w++);
         vtn_fail_if(dec->scope < VTN_DEC_STRUCT_MEMBER0, /* overflow */
                     "Member argument of OpMemberDecorate too large");
         break;
      case SpvOpExecutionMode:
      case SpvOpExecutionModeId:
         dec->scope = VTN_DEC_EXECUTION_MODE;
         break;
      default:
         unreachable("Invalid decoration opcode");
      }
      dec->decoration = *(w++);
      dec->operands = w;

      /* Link into the list */
      dec->next = val->decoration;
      val->decoration = dec;
      break;
   }

   case SpvOpGroupMemberDecorate:
   case SpvOpGroupDecorate: {
      struct vtn_value *group =
         vtn_value(b, target, vtn_value_type_decoration_group);

      for (; w < w_end; w++) {
         struct vtn_value *val = vtn_untyped_value(b, *w);
         struct vtn_decoration *dec = rzalloc(b, struct vtn_decoration);

         dec->group = group;
         if (opcode == SpvOpGroupDecorate) {
            dec->scope = VTN_DEC_DECORATION;
         } else {
            dec->scope = VTN_DEC_STRUCT_MEMBER0 + *(++w);
            vtn_fail_if(dec->scope < 0, /* Check for overflow */
                        "Member argument of OpGroupMemberDecorate too large");
         }

         /* Link into the list */
         dec->next = val->decoration;
         val->decoration = dec;
      }
      break;
   }

   default:
      unreachable("Unhandled opcode");
   }
}

struct member_decoration_ctx {
   unsigned num_fields;
   struct glsl_struct_field *fields;
   struct vtn_type *type;
};

/**
 * Returns true if the given type contains a struct decorated Block or
 * BufferBlock
 */
bool
vtn_type_contains_block(struct vtn_builder *b, struct vtn_type *type)
{
   switch (type->base_type) {
   case vtn_base_type_array:
      return vtn_type_contains_block(b, type->array_element);
   case vtn_base_type_struct:
      if (type->block || type->buffer_block)
         return true;
      for (unsigned i = 0; i < type->length; i++) {
         if (vtn_type_contains_block(b, type->members[i]))
            return true;
      }
      return false;
   default:
      return false;
   }
}

/** Returns true if two types are "compatible", i.e. you can do an OpLoad,
 * OpStore, or OpCopyMemory between them without breaking anything.
 * Technically, the SPIR-V rules require the exact same type ID but this lets
 * us internally be a bit looser.
 */
bool
vtn_types_compatible(struct vtn_builder *b,
                     struct vtn_type *t1, struct vtn_type *t2)
{
   if (t1->id == t2->id)
      return true;

   if (t1->base_type != t2->base_type)
      return false;

   switch (t1->base_type) {
   case vtn_base_type_void:
   case vtn_base_type_scalar:
   case vtn_base_type_vector:
   case vtn_base_type_matrix:
   case vtn_base_type_image:
   case vtn_base_type_sampler:
   case vtn_base_type_sampled_image:
      return t1->type == t2->type;

   case vtn_base_type_array:
      return t1->length == t2->length &&
             vtn_types_compatible(b, t1->array_element, t2->array_element);

   case vtn_base_type_pointer:
      return vtn_types_compatible(b, t1->deref, t2->deref);

   case vtn_base_type_struct:
      if (t1->length != t2->length)
         return false;

      for (unsigned i = 0; i < t1->length; i++) {
         if (!vtn_types_compatible(b, t1->members[i], t2->members[i]))
            return false;
      }
      return true;

   case vtn_base_type_function:
      /* This case shouldn't get hit since you can't copy around function
       * types.  Just require them to be identical.
       */
      return false;
   }

   vtn_fail("Invalid base type");
}

struct vtn_type *
vtn_type_without_array(struct vtn_type *type)
{
   while (type->base_type == vtn_base_type_array)
      type = type->array_element;
   return type;
}

/* does a shallow copy of a vtn_type */

static struct vtn_type *
vtn_type_copy(struct vtn_builder *b, struct vtn_type *src)
{
   struct vtn_type *dest = ralloc(b, struct vtn_type);
   *dest = *src;

   switch (src->base_type) {
   case vtn_base_type_void:
   case vtn_base_type_scalar:
   case vtn_base_type_vector:
   case vtn_base_type_matrix:
   case vtn_base_type_array:
   case vtn_base_type_pointer:
   case vtn_base_type_image:
   case vtn_base_type_sampler:
   case vtn_base_type_sampled_image:
      /* Nothing more to do */
      break;

   case vtn_base_type_struct:
      dest->members = ralloc_array(b, struct vtn_type *, src->length);
      memcpy(dest->members, src->members,
             src->length * sizeof(src->members[0]));

      dest->offsets = ralloc_array(b, unsigned, src->length);
      memcpy(dest->offsets, src->offsets,
             src->length * sizeof(src->offsets[0]));
      break;

   case vtn_base_type_function:
      dest->params = ralloc_array(b, struct vtn_type *, src->length);
      memcpy(dest->params, src->params, src->length * sizeof(src->params[0]));
      break;
   }

   return dest;
}

static struct vtn_type *
mutable_matrix_member(struct vtn_builder *b, struct vtn_type *type, int member)
{
   type->members[member] = vtn_type_copy(b, type->members[member]);
   type = type->members[member];

   /* We may have an array of matrices.... Oh, joy! */
   while (glsl_type_is_array(type->type)) {
      type->array_element = vtn_type_copy(b, type->array_element);
      type = type->array_element;
   }

   vtn_assert(glsl_type_is_matrix(type->type));

   return type;
}

static void
vtn_handle_access_qualifier(struct vtn_builder *b, struct vtn_type *type,
                            int member, enum gl_access_qualifier access)
{
   type->members[member] = vtn_type_copy(b, type->members[member]);
   type = type->members[member];

   type->access |= access;
}

static void
array_stride_decoration_cb(struct vtn_builder *b,
                           struct vtn_value *val, int member,
                           const struct vtn_decoration *dec, void *void_ctx)
{
   struct vtn_type *type = val->type;

   if (dec->decoration == SpvDecorationArrayStride) {
      if (vtn_type_contains_block(b, type)) {
         vtn_warn("The ArrayStride decoration cannot be applied to an array "
                  "type which contains a structure type decorated Block "
                  "or BufferBlock");
         /* Ignore the decoration */
      } else {
         vtn_fail_if(dec->operands[0] == 0, "ArrayStride must be non-zero");
         type->stride = dec->operands[0];
      }
   }
}

static void
struct_member_decoration_cb(struct vtn_builder *b,
                            struct vtn_value *val, int member,
                            const struct vtn_decoration *dec, void *void_ctx)
{
   struct member_decoration_ctx *ctx = void_ctx;

   if (member < 0)
      return;

   assert(member < ctx->num_fields);

   switch (dec->decoration) {
   case SpvDecorationRelaxedPrecision:
   case SpvDecorationUniform:
   case SpvDecorationUniformId:
      break; /* FIXME: Do nothing with this for now. */
   case SpvDecorationNonWritable:
      vtn_handle_access_qualifier(b, ctx->type, member, ACCESS_NON_WRITEABLE);
      break;
   case SpvDecorationNonReadable:
      vtn_handle_access_qualifier(b, ctx->type, member, ACCESS_NON_READABLE);
      break;
   case SpvDecorationVolatile:
      vtn_handle_access_qualifier(b, ctx->type, member, ACCESS_VOLATILE);
      break;
   case SpvDecorationCoherent:
      vtn_handle_access_qualifier(b, ctx->type, member, ACCESS_COHERENT);
      break;
   case SpvDecorationNoPerspective:
      ctx->fields[member].interpolation = INTERP_MODE_NOPERSPECTIVE;
      break;
   case SpvDecorationFlat:
      ctx->fields[member].interpolation = INTERP_MODE_FLAT;
      break;
   case SpvDecorationCentroid:
      ctx->fields[member].centroid = true;
      break;
   case SpvDecorationSample:
      ctx->fields[member].sample = true;
      break;
   case SpvDecorationStream:
      /* Vulkan only allows one GS stream */
      vtn_assert(dec->operands[0] == 0);
      break;
   case SpvDecorationLocation:
      ctx->fields[member].location = dec->operands[0];
      break;
   case SpvDecorationComponent:
      break; /* FIXME: What should we do with these? */
   case SpvDecorationBuiltIn:
      ctx->type->members[member] = vtn_type_copy(b, ctx->type->members[member]);
      ctx->type->members[member]->is_builtin = true;
      ctx->type->members[member]->builtin = dec->operands[0];
      ctx->type->builtin_block = true;
      break;
   case SpvDecorationOffset:
      ctx->type->offsets[member] = dec->operands[0];
      ctx->fields[member].offset = dec->operands[0];
      break;
   case SpvDecorationMatrixStride:
      /* Handled as a second pass */
      break;
   case SpvDecorationColMajor:
      break; /* Nothing to do here.  Column-major is the default. */
   case SpvDecorationRowMajor:
      mutable_matrix_member(b, ctx->type, member)->row_major = true;
      break;

   case SpvDecorationPatch:
      break;

   case SpvDecorationSpecId:
   case SpvDecorationBlock:
   case SpvDecorationBufferBlock:
   case SpvDecorationArrayStride:
   case SpvDecorationGLSLShared:
   case SpvDecorationGLSLPacked:
   case SpvDecorationInvariant:
   case SpvDecorationRestrict:
   case SpvDecorationAliased:
   case SpvDecorationConstant:
   case SpvDecorationIndex:
   case SpvDecorationBinding:
   case SpvDecorationDescriptorSet:
   case SpvDecorationLinkageAttributes:
   case SpvDecorationNoContraction:
   case SpvDecorationInputAttachmentIndex:
      vtn_warn("Decoration not allowed on struct members: %s",
               spirv_decoration_to_string(dec->decoration));
      break;

   case SpvDecorationXfbBuffer:
   case SpvDecorationXfbStride:
      vtn_warn("Vulkan does not have transform feedback");
      break;

   case SpvDecorationCPacked:
      if (b->shader->info.stage != MESA_SHADER_KERNEL)
         vtn_warn("Decoration only allowed for CL-style kernels: %s",
                  spirv_decoration_to_string(dec->decoration));
      else
         ctx->type->packed = true;
      break;

   case SpvDecorationSaturatedConversion:
   case SpvDecorationFuncParamAttr:
   case SpvDecorationFPRoundingMode:
   case SpvDecorationFPFastMathMode:
   case SpvDecorationAlignment:
      if (b->shader->info.stage != MESA_SHADER_KERNEL) {
         vtn_warn("Decoration only allowed for CL-style kernels: %s",
                  spirv_decoration_to_string(dec->decoration));
      }
      break;

   case SpvDecorationUserSemantic:
      /* User semantic decorations can safely be ignored by the driver. */
      break;

   default:
      vtn_fail_with_decoration("Unhandled decoration", dec->decoration);
   }
}

/** Chases the array type all the way down to the tail and rewrites the
 * glsl_types to be based off the tail's glsl_type.
 */
static void
vtn_array_type_rewrite_glsl_type(struct vtn_type *type)
{
   if (type->base_type != vtn_base_type_array)
      return;

   vtn_array_type_rewrite_glsl_type(type->array_element);

   type->type = glsl_array_type(type->array_element->type,
                                type->length, type->stride);
}

/* Matrix strides are handled as a separate pass because we need to know
 * whether the matrix is row-major or not first.
 */
static void
struct_member_matrix_stride_cb(struct vtn_builder *b,
                               struct vtn_value *val, int member,
                               const struct vtn_decoration *dec,
                               void *void_ctx)
{
   if (dec->decoration != SpvDecorationMatrixStride)
      return;

   vtn_fail_if(member < 0,
               "The MatrixStride decoration is only allowed on members "
               "of OpTypeStruct");
   vtn_fail_if(dec->operands[0] == 0, "MatrixStride must be non-zero");

   struct member_decoration_ctx *ctx = void_ctx;

   struct vtn_type *mat_type = mutable_matrix_member(b, ctx->type, member);
   if (mat_type->row_major) {
      mat_type->array_element = vtn_type_copy(b, mat_type->array_element);
      mat_type->stride = mat_type->array_element->stride;
      mat_type->array_element->stride = dec->operands[0];

      mat_type->type = glsl_explicit_matrix_type(mat_type->type,
                                                 dec->operands[0], true);
      mat_type->array_element->type = glsl_get_column_type(mat_type->type);
   } else {
      vtn_assert(mat_type->array_element->stride > 0);
      mat_type->stride = dec->operands[0];

      mat_type->type = glsl_explicit_matrix_type(mat_type->type,
                                                 dec->operands[0], false);
   }

   /* Now that we've replaced the glsl_type with a properly strided matrix
    * type, rewrite the member type so that it's an array of the proper kind
    * of glsl_type.
    */
   vtn_array_type_rewrite_glsl_type(ctx->type->members[member]);
   ctx->fields[member].type = ctx->type->members[member]->type;
}

static void
struct_block_decoration_cb(struct vtn_builder *b,
                           struct vtn_value *val, int member,
                           const struct vtn_decoration *dec, void *ctx)
{
   if (member != -1)
      return;

   struct vtn_type *type = val->type;
   if (dec->decoration == SpvDecorationBlock)
      type->block = true;
   else if (dec->decoration == SpvDecorationBufferBlock)
      type->buffer_block = true;
}

static void
type_decoration_cb(struct vtn_builder *b,
                   struct vtn_value *val, int member,
                    const struct vtn_decoration *dec, void *ctx)
{
   struct vtn_type *type = val->type;

   if (member != -1) {
      /* This should have been handled by OpTypeStruct */
      assert(val->type->base_type == vtn_base_type_struct);
      assert(member >= 0 && member < val->type->length);
      return;
   }

   switch (dec->decoration) {
   case SpvDecorationArrayStride:
      vtn_assert(type->base_type == vtn_base_type_array ||
                 type->base_type == vtn_base_type_pointer);
      break;
   case SpvDecorationBlock:
      vtn_assert(type->base_type == vtn_base_type_struct);
      vtn_assert(type->block);
      break;
   case SpvDecorationBufferBlock:
      vtn_assert(type->base_type == vtn_base_type_struct);
      vtn_assert(type->buffer_block);
      break;
   case SpvDecorationGLSLShared:
   case SpvDecorationGLSLPacked:
      /* Ignore these, since we get explicit offsets anyways */
      break;

   case SpvDecorationRowMajor:
   case SpvDecorationColMajor:
   case SpvDecorationMatrixStride:
   case SpvDecorationBuiltIn:
   case SpvDecorationNoPerspective:
   case SpvDecorationFlat:
   case SpvDecorationPatch:
   case SpvDecorationCentroid:
   case SpvDecorationSample:
   case SpvDecorationVolatile:
   case SpvDecorationCoherent:
   case SpvDecorationNonWritable:
   case SpvDecorationNonReadable:
   case SpvDecorationUniform:
   case SpvDecorationUniformId:
   case SpvDecorationLocation:
   case SpvDecorationComponent:
   case SpvDecorationOffset:
   case SpvDecorationXfbBuffer:
   case SpvDecorationXfbStride:
   case SpvDecorationUserSemantic:
      vtn_warn("Decoration only allowed for struct members: %s",
               spirv_decoration_to_string(dec->decoration));
      break;

   case SpvDecorationStream:
      /* We don't need to do anything here, as stream is filled up when
       * aplying the decoration to a variable, just check that if it is not a
       * struct member, it should be a struct.
       */
      vtn_assert(type->base_type == vtn_base_type_struct);
      break;

   case SpvDecorationRelaxedPrecision:
   case SpvDecorationSpecId:
   case SpvDecorationInvariant:
   case SpvDecorationRestrict:
   case SpvDecorationAliased:
   case SpvDecorationConstant:
   case SpvDecorationIndex:
   case SpvDecorationBinding:
   case SpvDecorationDescriptorSet:
   case SpvDecorationLinkageAttributes:
   case SpvDecorationNoContraction:
   case SpvDecorationInputAttachmentIndex:
      vtn_warn("Decoration not allowed on types: %s",
               spirv_decoration_to_string(dec->decoration));
      break;

   case SpvDecorationCPacked:
      if (b->shader->info.stage != MESA_SHADER_KERNEL)
         vtn_warn("Decoration only allowed for CL-style kernels: %s",
                  spirv_decoration_to_string(dec->decoration));
      else
         type->packed = true;
      break;

   case SpvDecorationSaturatedConversion:
   case SpvDecorationFuncParamAttr:
   case SpvDecorationFPRoundingMode:
   case SpvDecorationFPFastMathMode:
   case SpvDecorationAlignment:
      vtn_warn("Decoration only allowed for CL-style kernels: %s",
               spirv_decoration_to_string(dec->decoration));
      break;

   default:
      vtn_fail_with_decoration("Unhandled decoration", dec->decoration);
   }
}

static unsigned
translate_image_format(struct vtn_builder *b, SpvImageFormat format)
{
   switch (format) {
   case SpvImageFormatUnknown:      return 0;      /* GL_NONE */
   case SpvImageFormatRgba32f:      return 0x8814; /* GL_RGBA32F */
   case SpvImageFormatRgba16f:      return 0x881A; /* GL_RGBA16F */
   case SpvImageFormatR32f:         return 0x822E; /* GL_R32F */
   case SpvImageFormatRgba8:        return 0x8058; /* GL_RGBA8 */
   case SpvImageFormatRgba8Snorm:   return 0x8F97; /* GL_RGBA8_SNORM */
   case SpvImageFormatRg32f:        return 0x8230; /* GL_RG32F */
   case SpvImageFormatRg16f:        return 0x822F; /* GL_RG16F */
   case SpvImageFormatR11fG11fB10f: return 0x8C3A; /* GL_R11F_G11F_B10F */
   case SpvImageFormatR16f:         return 0x822D; /* GL_R16F */
   case SpvImageFormatRgba16:       return 0x805B; /* GL_RGBA16 */
   case SpvImageFormatRgb10A2:      return 0x8059; /* GL_RGB10_A2 */
   case SpvImageFormatRg16:         return 0x822C; /* GL_RG16 */
   case SpvImageFormatRg8:          return 0x822B; /* GL_RG8 */
   case SpvImageFormatR16:          return 0x822A; /* GL_R16 */
   case SpvImageFormatR8:           return 0x8229; /* GL_R8 */
   case SpvImageFormatRgba16Snorm:  return 0x8F9B; /* GL_RGBA16_SNORM */
   case SpvImageFormatRg16Snorm:    return 0x8F99; /* GL_RG16_SNORM */
   case SpvImageFormatRg8Snorm:     return 0x8F95; /* GL_RG8_SNORM */
   case SpvImageFormatR16Snorm:     return 0x8F98; /* GL_R16_SNORM */
   case SpvImageFormatR8Snorm:      return 0x8F94; /* GL_R8_SNORM */
   case SpvImageFormatRgba32i:      return 0x8D82; /* GL_RGBA32I */
   case SpvImageFormatRgba16i:      return 0x8D88; /* GL_RGBA16I */
   case SpvImageFormatRgba8i:       return 0x8D8E; /* GL_RGBA8I */
   case SpvImageFormatR32i:         return 0x8235; /* GL_R32I */
   case SpvImageFormatRg32i:        return 0x823B; /* GL_RG32I */
   case SpvImageFormatRg16i:        return 0x8239; /* GL_RG16I */
   case SpvImageFormatRg8i:         return 0x8237; /* GL_RG8I */
   case SpvImageFormatR16i:         return 0x8233; /* GL_R16I */
   case SpvImageFormatR8i:          return 0x8231; /* GL_R8I */
   case SpvImageFormatRgba32ui:     return 0x8D70; /* GL_RGBA32UI */
   case SpvImageFormatRgba16ui:     return 0x8D76; /* GL_RGBA16UI */
   case SpvImageFormatRgba8ui:      return 0x8D7C; /* GL_RGBA8UI */
   case SpvImageFormatR32ui:        return 0x8236; /* GL_R32UI */
   case SpvImageFormatRgb10a2ui:    return 0x906F; /* GL_RGB10_A2UI */
   case SpvImageFormatRg32ui:       return 0x823C; /* GL_RG32UI */
   case SpvImageFormatRg16ui:       return 0x823A; /* GL_RG16UI */
   case SpvImageFormatRg8ui:        return 0x8238; /* GL_RG8UI */
   case SpvImageFormatR16ui:        return 0x8234; /* GL_R16UI */
   case SpvImageFormatR8ui:         return 0x8232; /* GL_R8UI */
   default:
      vtn_fail("Invalid image format: %s (%u)",
               spirv_imageformat_to_string(format), format);
   }
}

static void
vtn_handle_type(struct vtn_builder *b, SpvOp opcode,
                const uint32_t *w, unsigned count)
{
   struct vtn_value *val = NULL;

   /* In order to properly handle forward declarations, we have to defer
    * allocation for pointer types.
    */
   if (opcode != SpvOpTypePointer && opcode != SpvOpTypeForwardPointer) {
      val = vtn_push_value(b, w[1], vtn_value_type_type);
      vtn_fail_if(val->type != NULL,
                  "Only pointers can have forward declarations");
      val->type = rzalloc(b, struct vtn_type);
      val->type->id = w[1];
   }

   switch (opcode) {
   case SpvOpTypeVoid:
      val->type->base_type = vtn_base_type_void;
      val->type->type = glsl_void_type();
      break;
   case SpvOpTypeBool:
      val->type->base_type = vtn_base_type_scalar;
      val->type->type = glsl_bool_type();
      val->type->length = 1;
      break;
   case SpvOpTypeInt: {
      int bit_size = w[2];
      const bool signedness = w[3];
      val->type->base_type = vtn_base_type_scalar;
      switch (bit_size) {
      case 64:
         val->type->type = (signedness ? glsl_int64_t_type() : glsl_uint64_t_type());
         break;
      case 32:
         val->type->type = (signedness ? glsl_int_type() : glsl_uint_type());
         break;
      case 16:
         val->type->type = (signedness ? glsl_int16_t_type() : glsl_uint16_t_type());
         break;
      case 8:
         val->type->type = (signedness ? glsl_int8_t_type() : glsl_uint8_t_type());
         break;
      default:
         vtn_fail("Invalid int bit size: %u", bit_size);
      }
      val->type->length = 1;
      break;
   }

   case SpvOpTypeFloat: {
      int bit_size = w[2];
      val->type->base_type = vtn_base_type_scalar;
      switch (bit_size) {
      case 16:
         val->type->type = glsl_float16_t_type();
         break;
      case 32:
         val->type->type = glsl_float_type();
         break;
      case 64:
         val->type->type = glsl_double_type();
         break;
      default:
         vtn_fail("Invalid float bit size: %u", bit_size);
      }
      val->type->length = 1;
      break;
   }

   case SpvOpTypeVector: {
      struct vtn_type *base = vtn_value(b, w[2], vtn_value_type_type)->type;
      unsigned elems = w[3];

      vtn_fail_if(base->base_type != vtn_base_type_scalar,
                  "Base type for OpTypeVector must be a scalar");
      vtn_fail_if((elems < 2 || elems > 4) && (elems != 8) && (elems != 16),
                  "Invalid component count for OpTypeVector");

      val->type->base_type = vtn_base_type_vector;
      val->type->type = glsl_vector_type(glsl_get_base_type(base->type), elems);
      val->type->length = elems;
      val->type->stride = glsl_type_is_boolean(val->type->type)
         ? 4 : glsl_get_bit_size(base->type) / 8;
      val->type->array_element = base;
      break;
   }

   case SpvOpTypeMatrix: {
      struct vtn_type *base = vtn_value(b, w[2], vtn_value_type_type)->type;
      unsigned columns = w[3];

      vtn_fail_if(base->base_type != vtn_base_type_vector,
                  "Base type for OpTypeMatrix must be a vector");
      vtn_fail_if(columns < 2 || columns > 4,
                  "Invalid column count for OpTypeMatrix");

      val->type->base_type = vtn_base_type_matrix;
      val->type->type = glsl_matrix_type(glsl_get_base_type(base->type),
                                         glsl_get_vector_elements(base->type),
                                         columns);
      vtn_fail_if(glsl_type_is_error(val->type->type),
                  "Unsupported base type for OpTypeMatrix");
      assert(!glsl_type_is_error(val->type->type));
      val->type->length = columns;
      val->type->array_element = base;
      val->type->row_major = false;
      val->type->stride = 0;
      break;
   }

   case SpvOpTypeRuntimeArray:
   case SpvOpTypeArray: {
      struct vtn_type *array_element =
         vtn_value(b, w[2], vtn_value_type_type)->type;

      if (opcode == SpvOpTypeRuntimeArray) {
         /* A length of 0 is used to denote unsized arrays */
         val->type->length = 0;
      } else {
         val->type->length = vtn_constant_uint(b, w[3]);
      }

      val->type->base_type = vtn_base_type_array;
      val->type->array_element = array_element;
      if (b->shader->info.stage == MESA_SHADER_KERNEL)
         val->type->stride = glsl_get_cl_size(array_element->type);

      vtn_foreach_decoration(b, val, array_stride_decoration_cb, NULL);
      val->type->type = glsl_array_type(array_element->type, val->type->length,
                                        val->type->stride);
      break;
   }

   case SpvOpTypeStruct: {
      unsigned num_fields = count - 2;
      val->type->base_type = vtn_base_type_struct;
      val->type->length = num_fields;
      val->type->members = ralloc_array(b, struct vtn_type *, num_fields);
      val->type->offsets = ralloc_array(b, unsigned, num_fields);
      val->type->packed = false;

      NIR_VLA(struct glsl_struct_field, fields, count);
      for (unsigned i = 0; i < num_fields; i++) {
         val->type->members[i] =
            vtn_value(b, w[i + 2], vtn_value_type_type)->type;
         fields[i] = (struct glsl_struct_field) {
            .type = val->type->members[i]->type,
            .name = ralloc_asprintf(b, "field%d", i),
            .location = -1,
            .offset = -1,
         };
      }

      if (b->shader->info.stage == MESA_SHADER_KERNEL) {
         unsigned offset = 0;
         for (unsigned i = 0; i < num_fields; i++) {
            offset = align(offset, glsl_get_cl_alignment(fields[i].type));
            fields[i].offset = offset;
            offset += glsl_get_cl_size(fields[i].type);
         }
      }

      struct member_decoration_ctx ctx = {
         .num_fields = num_fields,
         .fields = fields,
         .type = val->type
      };

      vtn_foreach_decoration(b, val, struct_member_decoration_cb, &ctx);
      vtn_foreach_decoration(b, val, struct_member_matrix_stride_cb, &ctx);

      vtn_foreach_decoration(b, val, struct_block_decoration_cb, NULL);

      const char *name = val->name;

      if (val->type->block || val->type->buffer_block) {
         /* Packing will be ignored since types coming from SPIR-V are
          * explicitly laid out.
          */
         val->type->type = glsl_interface_type(fields, num_fields,
                                               /* packing */ 0, false,
                                               name ? name : "block");
      } else {
         val->type->type = glsl_struct_type(fields, num_fields,
                                            name ? name : "struct", false);
      }
      break;
   }

   case SpvOpTypeFunction: {
      val->type->base_type = vtn_base_type_function;
      val->type->type = NULL;

      val->type->return_type = vtn_value(b, w[2], vtn_value_type_type)->type;

      const unsigned num_params = count - 3;
      val->type->length = num_params;
      val->type->params = ralloc_array(b, struct vtn_type *, num_params);
      for (unsigned i = 0; i < count - 3; i++) {
         val->type->params[i] =
            vtn_value(b, w[i + 3], vtn_value_type_type)->type;
      }
      break;
   }

   case SpvOpTypePointer:
   case SpvOpTypeForwardPointer: {
      /* We can't blindly push the value because it might be a forward
       * declaration.
       */
      val = vtn_untyped_value(b, w[1]);

      SpvStorageClass storage_class = w[2];

      if (val->value_type == vtn_value_type_invalid) {
         val->value_type = vtn_value_type_type;
         val->type = rzalloc(b, struct vtn_type);
         val->type->id = w[1];
         val->type->base_type = vtn_base_type_pointer;
         val->type->storage_class = storage_class;

         /* These can actually be stored to nir_variables and used as SSA
          * values so they need a real glsl_type.
          */
         enum vtn_variable_mode mode = vtn_storage_class_to_mode(
            b, storage_class, NULL, NULL);
         val->type->type = nir_address_format_to_glsl_type(
            vtn_mode_to_address_format(b, mode));
      } else {
         vtn_fail_if(val->type->storage_class != storage_class,
                     "The storage classes of an OpTypePointer and any "
                     "OpTypeForwardPointers that provide forward "
                     "declarations of it must match.");
      }

      if (opcode == SpvOpTypePointer) {
         vtn_fail_if(val->type->deref != NULL,
                     "While OpTypeForwardPointer can be used to provide a "
                     "forward declaration of a pointer, OpTypePointer can "
                     "only be used once for a given id.");

         val->type->deref = vtn_value(b, w[3], vtn_value_type_type)->type;

         /* Only certain storage classes use ArrayStride.  The others (in
          * particular Workgroup) are expected to be laid out by the driver.
          */
         switch (storage_class) {
         case SpvStorageClassUniform:
         case SpvStorageClassPushConstant:
         case SpvStorageClassStorageBuffer:
         case SpvStorageClassPhysicalStorageBufferEXT:
            vtn_foreach_decoration(b, val, array_stride_decoration_cb, NULL);
            break;
         default:
            /* Nothing to do. */
            break;
         }

         if (b->physical_ptrs) {
            switch (storage_class) {
            case SpvStorageClassFunction:
            case SpvStorageClassWorkgroup:
            case SpvStorageClassCrossWorkgroup:
               val->type->stride = align(glsl_get_cl_size(val->type->deref->type),
                                         glsl_get_cl_alignment(val->type->deref->type));
               break;
            default:
               break;
            }
         }
      }
      break;
   }

   case SpvOpTypeImage: {
      val->type->base_type = vtn_base_type_image;

      const struct vtn_type *sampled_type =
         vtn_value(b, w[2], vtn_value_type_type)->type;

      vtn_fail_if(sampled_type->base_type != vtn_base_type_scalar ||
                  glsl_get_bit_size(sampled_type->type) != 32,
                  "Sampled type of OpTypeImage must be a 32-bit scalar");

      enum glsl_sampler_dim dim;
      switch ((SpvDim)w[3]) {
      case SpvDim1D:       dim = GLSL_SAMPLER_DIM_1D;    break;
      case SpvDim2D:       dim = GLSL_SAMPLER_DIM_2D;    break;
      case SpvDim3D:       dim = GLSL_SAMPLER_DIM_3D;    break;
      case SpvDimCube:     dim = GLSL_SAMPLER_DIM_CUBE;  break;
      case SpvDimRect:     dim = GLSL_SAMPLER_DIM_RECT;  break;
      case SpvDimBuffer:   dim = GLSL_SAMPLER_DIM_BUF;   break;
      case SpvDimSubpassData: dim = GLSL_SAMPLER_DIM_SUBPASS; break;
      default:
         vtn_fail("Invalid SPIR-V image dimensionality: %s (%u)",
                  spirv_dim_to_string((SpvDim)w[3]), w[3]);
      }

      /* w[4]: as per Vulkan spec "Validation Rules within a Module",
       *       The “Depth” operand of OpTypeImage is ignored.
       */
      bool is_array = w[5];
      bool multisampled = w[6];
      unsigned sampled = w[7];
      SpvImageFormat format = w[8];

      if (count > 9)
         val->type->access_qualifier = w[9];
      else
         val->type->access_qualifier = SpvAccessQualifierReadWrite;

      if (multisampled) {
         if (dim == GLSL_SAMPLER_DIM_2D)
            dim = GLSL_SAMPLER_DIM_MS;
         else if (dim == GLSL_SAMPLER_DIM_SUBPASS)
            dim = GLSL_SAMPLER_DIM_SUBPASS_MS;
         else
            vtn_fail("Unsupported multisampled image type");
      }

      val->type->image_format = translate_image_format(b, format);

      enum glsl_base_type sampled_base_type =
         glsl_get_base_type(sampled_type->type);
      if (sampled == 1) {
         val->type->sampled = true;
         val->type->type = glsl_sampler_type(dim, false, is_array,
                                             sampled_base_type);
      } else if (sampled == 2) {
         val->type->sampled = false;
         val->type->type = glsl_image_type(dim, is_array, sampled_base_type);
      } else {
         vtn_fail("We need to know if the image will be sampled");
      }
      break;
   }

   case SpvOpTypeSampledImage:
      val->type->base_type = vtn_base_type_sampled_image;
      val->type->image = vtn_value(b, w[2], vtn_value_type_type)->type;
      val->type->type = val->type->image->type;
      break;

   case SpvOpTypeSampler:
      /* The actual sampler type here doesn't really matter.  It gets
       * thrown away the moment you combine it with an image.  What really
       * matters is that it's a sampler type as opposed to an integer type
       * so the backend knows what to do.
       */
      val->type->base_type = vtn_base_type_sampler;
      val->type->type = glsl_bare_sampler_type();
      break;

   case SpvOpTypeOpaque:
   case SpvOpTypeEvent:
   case SpvOpTypeDeviceEvent:
   case SpvOpTypeReserveId:
   case SpvOpTypeQueue:
   case SpvOpTypePipe:
   default:
      vtn_fail_with_opcode("Unhandled opcode", opcode);
   }

   vtn_foreach_decoration(b, val, type_decoration_cb, NULL);

   if (val->type->base_type == vtn_base_type_struct &&
       (val->type->block || val->type->buffer_block)) {
      for (unsigned i = 0; i < val->type->length; i++) {
         vtn_fail_if(vtn_type_contains_block(b, val->type->members[i]),
                     "Block and BufferBlock decorations cannot decorate a "
                     "structure type that is nested at any level inside "
                     "another structure type decorated with Block or "
                     "BufferBlock.");
      }
   }
}

static nir_constant *
vtn_null_constant(struct vtn_builder *b, struct vtn_type *type)
{
   nir_constant *c = rzalloc(b, nir_constant);

   switch (type->base_type) {
   case vtn_base_type_scalar:
   case vtn_base_type_vector:
      /* Nothing to do here.  It's already initialized to zero */
      break;

   case vtn_base_type_pointer: {
      enum vtn_variable_mode mode = vtn_storage_class_to_mode(
         b, type->storage_class, type->deref, NULL);
      nir_address_format addr_format = vtn_mode_to_address_format(b, mode);

      const nir_const_value *null_value = nir_address_format_null_value(addr_format);
      memcpy(c->values, null_value,
             sizeof(nir_const_value) * nir_address_format_num_components(addr_format));
      break;
   }

   case vtn_base_type_void:
   case vtn_base_type_image:
   case vtn_base_type_sampler:
   case vtn_base_type_sampled_image:
   case vtn_base_type_function:
      /* For those we have to return something but it doesn't matter what. */
      break;

   case vtn_base_type_matrix:
   case vtn_base_type_array:
      vtn_assert(type->length > 0);
      c->num_elements = type->length;
      c->elements = ralloc_array(b, nir_constant *, c->num_elements);

      c->elements[0] = vtn_null_constant(b, type->array_element);
      for (unsigned i = 1; i < c->num_elements; i++)
         c->elements[i] = c->elements[0];
      break;

   case vtn_base_type_struct:
      c->num_elements = type->length;
      c->elements = ralloc_array(b, nir_constant *, c->num_elements);
      for (unsigned i = 0; i < c->num_elements; i++)
         c->elements[i] = vtn_null_constant(b, type->members[i]);
      break;

   default:
      vtn_fail("Invalid type for null constant");
   }

   return c;
}

static void
spec_constant_decoration_cb(struct vtn_builder *b, struct vtn_value *v,
                             int member, const struct vtn_decoration *dec,
                             void *data)
{
   vtn_assert(member == -1);
   if (dec->decoration != SpvDecorationSpecId)
      return;

   struct spec_constant_value *const_value = data;

   for (unsigned i = 0; i < b->num_specializations; i++) {
      if (b->specializations[i].id == dec->operands[0]) {
         if (const_value->is_double)
            const_value->data64 = b->specializations[i].data64;
         else
            const_value->data32 = b->specializations[i].data32;
         return;
      }
   }
}

static uint32_t
get_specialization(struct vtn_builder *b, struct vtn_value *val,
                   uint32_t const_value)
{
   struct spec_constant_value data;
   data.is_double = false;
   data.data32 = const_value;
   vtn_foreach_decoration(b, val, spec_constant_decoration_cb, &data);
   return data.data32;
}

static uint64_t
get_specialization64(struct vtn_builder *b, struct vtn_value *val,
                   uint64_t const_value)
{
   struct spec_constant_value data;
   data.is_double = true;
   data.data64 = const_value;
   vtn_foreach_decoration(b, val, spec_constant_decoration_cb, &data);
   return data.data64;
}

static void
handle_workgroup_size_decoration_cb(struct vtn_builder *b,
                                    struct vtn_value *val,
                                    int member,
                                    const struct vtn_decoration *dec,
                                    void *data)
{
   vtn_assert(member == -1);
   if (dec->decoration != SpvDecorationBuiltIn ||
       dec->operands[0] != SpvBuiltInWorkgroupSize)
      return;

   vtn_assert(val->type->type == glsl_vector_type(GLSL_TYPE_UINT, 3));
   b->workgroup_size_builtin = val;
}

static void
vtn_handle_constant(struct vtn_builder *b, SpvOp opcode,
                    const uint32_t *w, unsigned count)
{
   struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_constant);
   val->constant = rzalloc(b, nir_constant);
   switch (opcode) {
   case SpvOpConstantTrue:
   case SpvOpConstantFalse:
   case SpvOpSpecConstantTrue:
   case SpvOpSpecConstantFalse: {
      vtn_fail_if(val->type->type != glsl_bool_type(),
                  "Result type of %s must be OpTypeBool",
                  spirv_op_to_string(opcode));

      uint32_t int_val = (opcode == SpvOpConstantTrue ||
                          opcode == SpvOpSpecConstantTrue);

      if (opcode == SpvOpSpecConstantTrue ||
          opcode == SpvOpSpecConstantFalse)
         int_val = get_specialization(b, val, int_val);

      val->constant->values[0].b = int_val != 0;
      break;
   }

   case SpvOpConstant: {
      vtn_fail_if(val->type->base_type != vtn_base_type_scalar,
                  "Result type of %s must be a scalar",
                  spirv_op_to_string(opcode));
      int bit_size = glsl_get_bit_size(val->type->type);
      switch (bit_size) {
      case 64:
         val->constant->values[0].u64 = vtn_u64_literal(&w[3]);
         break;
      case 32:
         val->constant->values[0].u32 = w[3];
         break;
      case 16:
         val->constant->values[0].u16 = w[3];
         break;
      case 8:
         val->constant->values[0].u8 = w[3];
         break;
      default:
         vtn_fail("Unsupported SpvOpConstant bit size: %u", bit_size);
      }
      break;
   }

   case SpvOpSpecConstant: {
      vtn_fail_if(val->type->base_type != vtn_base_type_scalar,
                  "Result type of %s must be a scalar",
                  spirv_op_to_string(opcode));
      int bit_size = glsl_get_bit_size(val->type->type);
      switch (bit_size) {
      case 64:
         val->constant->values[0].u64 =
            get_specialization64(b, val, vtn_u64_literal(&w[3]));
         break;
      case 32:
         val->constant->values[0].u32 = get_specialization(b, val, w[3]);
         break;
      case 16:
         val->constant->values[0].u16 = get_specialization(b, val, w[3]);
         break;
      case 8:
         val->constant->values[0].u8 = get_specialization(b, val, w[3]);
         break;
      default:
         vtn_fail("Unsupported SpvOpSpecConstant bit size");
      }
      break;
   }

   case SpvOpSpecConstantComposite:
   case SpvOpConstantComposite: {
      unsigned elem_count = count - 3;
      vtn_fail_if(elem_count != val->type->length,
                  "%s has %u constituents, expected %u",
                  spirv_op_to_string(opcode), elem_count, val->type->length);

      nir_constant **elems = ralloc_array(b, nir_constant *, elem_count);
      for (unsigned i = 0; i < elem_count; i++) {
         struct vtn_value *val = vtn_untyped_value(b, w[i + 3]);

         if (val->value_type == vtn_value_type_constant) {
            elems[i] = val->constant;
         } else {
            vtn_fail_if(val->value_type != vtn_value_type_undef,
                        "only constants or undefs allowed for "
                        "SpvOpConstantComposite");
            /* to make it easier, just insert a NULL constant for now */
            elems[i] = vtn_null_constant(b, val->type);
         }
      }

      switch (val->type->base_type) {
      case vtn_base_type_vector: {
         assert(glsl_type_is_vector(val->type->type));
         for (unsigned i = 0; i < elem_count; i++)
            val->constant->values[i] = elems[i]->values[0];
         break;
      }

      case vtn_base_type_matrix:
      case vtn_base_type_struct:
      case vtn_base_type_array:
         ralloc_steal(val->constant, elems);
         val->constant->num_elements = elem_count;
         val->constant->elements = elems;
         break;

      default:
         vtn_fail("Result type of %s must be a composite type",
                  spirv_op_to_string(opcode));
      }
      break;
   }

   case SpvOpSpecConstantOp: {
      SpvOp opcode = get_specialization(b, val, w[3]);
      switch (opcode) {
      case SpvOpVectorShuffle: {
         struct vtn_value *v0 = &b->values[w[4]];
         struct vtn_value *v1 = &b->values[w[5]];

         vtn_assert(v0->value_type == vtn_value_type_constant ||
                    v0->value_type == vtn_value_type_undef);
         vtn_assert(v1->value_type == vtn_value_type_constant ||
                    v1->value_type == vtn_value_type_undef);

         unsigned len0 = glsl_get_vector_elements(v0->type->type);
         unsigned len1 = glsl_get_vector_elements(v1->type->type);

         vtn_assert(len0 + len1 < 16);

         unsigned bit_size = glsl_get_bit_size(val->type->type);
         unsigned bit_size0 = glsl_get_bit_size(v0->type->type);
         unsigned bit_size1 = glsl_get_bit_size(v1->type->type);

         vtn_assert(bit_size == bit_size0 && bit_size == bit_size1);
         (void)bit_size0; (void)bit_size1;

         nir_const_value undef = { .u64 = 0xdeadbeefdeadbeef };
         nir_const_value combined[NIR_MAX_VEC_COMPONENTS * 2];

         if (v0->value_type == vtn_value_type_constant) {
            for (unsigned i = 0; i < len0; i++)
               combined[i] = v0->constant->values[i];
         }
         if (v1->value_type == vtn_value_type_constant) {
            for (unsigned i = 0; i < len1; i++)
               combined[len0 + i] = v1->constant->values[i];
         }

         for (unsigned i = 0, j = 0; i < count - 6; i++, j++) {
            uint32_t comp = w[i + 6];
            if (comp == (uint32_t)-1) {
               /* If component is not used, set the value to a known constant
                * to detect if it is wrongly used.
                */
               val->constant->values[j] = undef;
            } else {
               vtn_fail_if(comp >= len0 + len1,
                           "All Component literals must either be FFFFFFFF "
                           "or in [0, N - 1] (inclusive).");
               val->constant->values[j] = combined[comp];
            }
         }
         break;
      }

      case SpvOpCompositeExtract:
      case SpvOpCompositeInsert: {
         struct vtn_value *comp;
         unsigned deref_start;
         struct nir_constant **c;
         if (opcode == SpvOpCompositeExtract) {
            comp = vtn_value(b, w[4], vtn_value_type_constant);
            deref_start = 5;
            c = &comp->constant;
         } else {
            comp = vtn_value(b, w[5], vtn_value_type_constant);
            deref_start = 6;
            val->constant = nir_constant_clone(comp->constant,
                                               (nir_variable *)b);
            c = &val->constant;
         }

         int elem = -1;
         const struct vtn_type *type = comp->type;
         for (unsigned i = deref_start; i < count; i++) {
            vtn_fail_if(w[i] > type->length,
                        "%uth index of %s is %u but the type has only "
                        "%u elements", i - deref_start,
                        spirv_op_to_string(opcode), w[i], type->length);

            switch (type->base_type) {
            case vtn_base_type_vector:
               elem = w[i];
               type = type->array_element;
               break;

            case vtn_base_type_matrix:
            case vtn_base_type_array:
               c = &(*c)->elements[w[i]];
               type = type->array_element;
               break;

            case vtn_base_type_struct:
               c = &(*c)->elements[w[i]];
               type = type->members[w[i]];
               break;

            default:
               vtn_fail("%s must only index into composite types",
                        spirv_op_to_string(opcode));
            }
         }

         if (opcode == SpvOpCompositeExtract) {
            if (elem == -1) {
               val->constant = *c;
            } else {
               unsigned num_components = type->length;
               for (unsigned i = 0; i < num_components; i++)
                  val->constant->values[i] = (*c)->values[elem + i];
            }
         } else {
            struct vtn_value *insert =
               vtn_value(b, w[4], vtn_value_type_constant);
            vtn_assert(insert->type == type);
            if (elem == -1) {
               *c = insert->constant;
            } else {
               unsigned num_components = type->length;
               for (unsigned i = 0; i < num_components; i++)
                  (*c)->values[elem + i] = insert->constant->values[i];
            }
         }
         break;
      }

      default: {
         bool swap;
         nir_alu_type dst_alu_type = nir_get_nir_type_for_glsl_type(val->type->type);
         nir_alu_type src_alu_type = dst_alu_type;
         unsigned num_components = glsl_get_vector_elements(val->type->type);
         unsigned bit_size;

         vtn_assert(count <= 7);

         switch (opcode) {
         case SpvOpSConvert:
         case SpvOpFConvert:
         case SpvOpUConvert:
            /* We have a source in a conversion */
            src_alu_type =
               nir_get_nir_type_for_glsl_type(
                  vtn_value(b, w[4], vtn_value_type_constant)->type->type);
            /* We use the bitsize of the conversion source to evaluate the opcode later */
            bit_size = glsl_get_bit_size(
               vtn_value(b, w[4], vtn_value_type_constant)->type->type);
            break;
         default:
            bit_size = glsl_get_bit_size(val->type->type);
         };

         nir_op op = vtn_nir_alu_op_for_spirv_opcode(b, opcode, &swap,
                                                     nir_alu_type_get_type_size(src_alu_type),
                                                     nir_alu_type_get_type_size(dst_alu_type));
         nir_const_value src[3][NIR_MAX_VEC_COMPONENTS];

         for (unsigned i = 0; i < count - 4; i++) {
            struct vtn_value *src_val =
               vtn_value(b, w[4 + i], vtn_value_type_constant);

            /* If this is an unsized source, pull the bit size from the
             * source; otherwise, we'll use the bit size from the destination.
             */
            if (!nir_alu_type_get_type_size(nir_op_infos[op].input_types[i]))
               bit_size = glsl_get_bit_size(src_val->type->type);

            unsigned src_comps = nir_op_infos[op].input_sizes[i] ?
                                 nir_op_infos[op].input_sizes[i] :
                                 num_components;

            unsigned j = swap ? 1 - i : i;
            for (unsigned c = 0; c < src_comps; c++)
               src[j][c] = src_val->constant->values[c];
         }

         /* fix up fixed size sources */
         switch (op) {
         case nir_op_ishl:
         case nir_op_ishr:
         case nir_op_ushr: {
            if (bit_size == 32)
               break;
            for (unsigned i = 0; i < num_components; ++i) {
               switch (bit_size) {
               case 64: src[1][i].u32 = src[1][i].u64; break;
               case 16: src[1][i].u32 = src[1][i].u16; break;
               case  8: src[1][i].u32 = src[1][i].u8;  break;
               }
            }
            break;
         }
         default:
            break;
         }

         nir_const_value *srcs[3] = {
            src[0], src[1], src[2],
         };
         nir_eval_const_opcode(op, val->constant->values,
                               num_components, bit_size, srcs,
                               b->shader->info.float_controls_execution_mode);
         break;
      } /* default */
      }
      break;
   }

   case SpvOpConstantNull:
      val->constant = vtn_null_constant(b, val->type);
      break;

   case SpvOpConstantSampler:
      vtn_fail("OpConstantSampler requires Kernel Capability");
      break;

   default:
      vtn_fail_with_opcode("Unhandled opcode", opcode);
   }

   /* Now that we have the value, update the workgroup size if needed */
   vtn_foreach_decoration(b, val, handle_workgroup_size_decoration_cb, NULL);
}

SpvMemorySemanticsMask
vtn_storage_class_to_memory_semantics(SpvStorageClass sc)
{
   switch (sc) {
   case SpvStorageClassStorageBuffer:
   case SpvStorageClassPhysicalStorageBufferEXT:
      return SpvMemorySemanticsUniformMemoryMask;
   case SpvStorageClassWorkgroup:
      return SpvMemorySemanticsWorkgroupMemoryMask;
   default:
      return SpvMemorySemanticsMaskNone;
   }
}

static void
vtn_split_barrier_semantics(struct vtn_builder *b,
                            SpvMemorySemanticsMask semantics,
                            SpvMemorySemanticsMask *before,
                            SpvMemorySemanticsMask *after)
{
   /* For memory semantics embedded in operations, we split them into up to
    * two barriers, to be added before and after the operation.  This is less
    * strict than if we propagated until the final backend stage, but still
    * result in correct execution.
    *
    * A further improvement could be pipe this information (and use!) into the
    * next compiler layers, at the expense of making the handling of barriers
    * more complicated.
    */

   *before = SpvMemorySemanticsMaskNone;
   *after = SpvMemorySemanticsMaskNone;

   SpvMemorySemanticsMask order_semantics =
      semantics & (SpvMemorySemanticsAcquireMask |
                   SpvMemorySemanticsReleaseMask |
                   SpvMemorySemanticsAcquireReleaseMask |
                   SpvMemorySemanticsSequentiallyConsistentMask);

   if (util_bitcount(order_semantics) > 1) {
      /* Old GLSLang versions incorrectly set all the ordering bits.  This was
       * fixed in c51287d744fb6e7e9ccc09f6f8451e6c64b1dad6 of glslang repo,
       * and it is in GLSLang since revision "SPIRV99.1321" (from Jul-2016).
       */
      vtn_warn("Multiple memory ordering semantics specified, "
               "assuming AcquireRelease.");
      order_semantics = SpvMemorySemanticsAcquireReleaseMask;
   }

   const SpvMemorySemanticsMask av_vis_semantics =
      semantics & (SpvMemorySemanticsMakeAvailableMask |
                   SpvMemorySemanticsMakeVisibleMask);

   const SpvMemorySemanticsMask storage_semantics =
      semantics & (SpvMemorySemanticsUniformMemoryMask |
                   SpvMemorySemanticsSubgroupMemoryMask |
                   SpvMemorySemanticsWorkgroupMemoryMask |
                   SpvMemorySemanticsCrossWorkgroupMemoryMask |
                   SpvMemorySemanticsAtomicCounterMemoryMask |
                   SpvMemorySemanticsImageMemoryMask |
                   SpvMemorySemanticsOutputMemoryMask);

   const SpvMemorySemanticsMask other_semantics =
      semantics & ~(order_semantics | av_vis_semantics | storage_semantics);

   if (other_semantics)
      vtn_warn("Ignoring unhandled memory semantics: %u\n", other_semantics);

   /* SequentiallyConsistent is treated as AcquireRelease. */

   /* The RELEASE barrier happens BEFORE the operation, and it is usually
    * associated with a Store.  All the write operations with a matching
    * semantics will not be reordered after the Store.
    */
   if (order_semantics & (SpvMemorySemanticsReleaseMask |
                          SpvMemorySemanticsAcquireReleaseMask |
                          SpvMemorySemanticsSequentiallyConsistentMask)) {
      *before |= SpvMemorySemanticsReleaseMask | storage_semantics;
   }

   /* The ACQUIRE barrier happens AFTER the operation, and it is usually
    * associated with a Load.  All the operations with a matching semantics
    * will not be reordered before the Load.
    */
   if (order_semantics & (SpvMemorySemanticsAcquireMask |
                          SpvMemorySemanticsAcquireReleaseMask |
                          SpvMemorySemanticsSequentiallyConsistentMask)) {
      *after |= SpvMemorySemanticsAcquireMask | storage_semantics;
   }

   if (av_vis_semantics & SpvMemorySemanticsMakeVisibleMask)
      *before |= SpvMemorySemanticsMakeVisibleMask | storage_semantics;

   if (av_vis_semantics & SpvMemorySemanticsMakeAvailableMask)
      *after |= SpvMemorySemanticsMakeAvailableMask | storage_semantics;
}

static void
vtn_emit_scoped_memory_barrier(struct vtn_builder *b, SpvScope scope,
                               SpvMemorySemanticsMask semantics)
{
   nir_memory_semantics nir_semantics = 0;

   SpvMemorySemanticsMask order_semantics =
      semantics & (SpvMemorySemanticsAcquireMask |
                   SpvMemorySemanticsReleaseMask |
                   SpvMemorySemanticsAcquireReleaseMask |
                   SpvMemorySemanticsSequentiallyConsistentMask);

   if (util_bitcount(order_semantics) > 1) {
      /* Old GLSLang versions incorrectly set all the ordering bits.  This was
       * fixed in c51287d744fb6e7e9ccc09f6f8451e6c64b1dad6 of glslang repo,
       * and it is in GLSLang since revision "SPIRV99.1321" (from Jul-2016).
       */
      vtn_warn("Multiple memory ordering semantics bits specified, "
               "assuming AcquireRelease.");
      order_semantics = SpvMemorySemanticsAcquireReleaseMask;
   }

   switch (order_semantics) {
   case 0:
      /* Not an ordering barrier. */
      break;

   case SpvMemorySemanticsAcquireMask:
      nir_semantics = NIR_MEMORY_ACQUIRE;
      break;

   case SpvMemorySemanticsReleaseMask:
      nir_semantics = NIR_MEMORY_RELEASE;
      break;

   case SpvMemorySemanticsSequentiallyConsistentMask:
      /* Fall through.  Treated as AcquireRelease in Vulkan. */
   case SpvMemorySemanticsAcquireReleaseMask:
      nir_semantics = NIR_MEMORY_ACQUIRE | NIR_MEMORY_RELEASE;
      break;

   default:
      unreachable("Invalid memory order semantics");
   }

   if (semantics & SpvMemorySemanticsMakeAvailableMask) {
      vtn_fail_if(!b->options->caps.vk_memory_model,
                  "To use MakeAvailable memory semantics the VulkanMemoryModel "
                  "capability must be declared.");
      nir_semantics |= NIR_MEMORY_MAKE_AVAILABLE;
   }

   if (semantics & SpvMemorySemanticsMakeVisibleMask) {
      vtn_fail_if(!b->options->caps.vk_memory_model,
                  "To use MakeVisible memory semantics the VulkanMemoryModel "
                  "capability must be declared.");
      nir_semantics |= NIR_MEMORY_MAKE_VISIBLE;
   }

   /* Vulkan Environment for SPIR-V says "SubgroupMemory, CrossWorkgroupMemory,
    * and AtomicCounterMemory are ignored".
    */
   semantics &= ~(SpvMemorySemanticsSubgroupMemoryMask |
                  SpvMemorySemanticsCrossWorkgroupMemoryMask |
                  SpvMemorySemanticsAtomicCounterMemoryMask);

   /* TODO: Consider adding nir_var_mem_image mode to NIR so it can be used
    * for SpvMemorySemanticsImageMemoryMask.
    */

   nir_variable_mode modes = 0;
   if (semantics & (SpvMemorySemanticsUniformMemoryMask |
                    SpvMemorySemanticsImageMemoryMask))
      modes |= nir_var_mem_ubo | nir_var_mem_ssbo | nir_var_uniform;
   if (semantics & SpvMemorySemanticsWorkgroupMemoryMask)
      modes |= nir_var_mem_shared;
   if (semantics & SpvMemorySemanticsOutputMemoryMask) {
      vtn_fail_if(!b->options->caps.vk_memory_model,
                  "To use Output memory semantics, the VulkanMemoryModel "
                  "capability must be declared.");
      modes |= nir_var_shader_out;
   }

   /* No barrier to add. */
   if (nir_semantics == 0 || modes == 0)
      return;

   nir_scope nir_scope;
   switch (scope) {
   case SpvScopeDevice:
      vtn_fail_if(b->options->caps.vk_memory_model &&
                  !b->options->caps.vk_memory_model_device_scope,
                  "If the Vulkan memory model is declared and any instruction "
                  "uses Device scope, the VulkanMemoryModelDeviceScope "
                  "capability must be declared.");
      nir_scope = NIR_SCOPE_DEVICE;
      break;

   case SpvScopeQueueFamily:
      vtn_fail_if(!b->options->caps.vk_memory_model,
                  "To use Queue Family scope, the VulkanMemoryModel capability "
                  "must be declared.");
      nir_scope = NIR_SCOPE_QUEUE_FAMILY;
      break;

   case SpvScopeWorkgroup:
      nir_scope = NIR_SCOPE_WORKGROUP;
      break;

   case SpvScopeSubgroup:
      nir_scope = NIR_SCOPE_SUBGROUP;
      break;

   case SpvScopeInvocation:
      nir_scope = NIR_SCOPE_INVOCATION;
      break;

   default:
      vtn_fail("Invalid memory scope");
   }

   nir_intrinsic_instr *intrin =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_scoped_memory_barrier);
   nir_intrinsic_set_memory_semantics(intrin, nir_semantics);

   nir_intrinsic_set_memory_modes(intrin, modes);
   nir_intrinsic_set_memory_scope(intrin, nir_scope);
   nir_builder_instr_insert(&b->nb, &intrin->instr);
}

struct vtn_ssa_value *
vtn_create_ssa_value(struct vtn_builder *b, const struct glsl_type *type)
{
   struct vtn_ssa_value *val = rzalloc(b, struct vtn_ssa_value);
   val->type = type;

   if (!glsl_type_is_vector_or_scalar(type)) {
      unsigned elems = glsl_get_length(type);
      val->elems = ralloc_array(b, struct vtn_ssa_value *, elems);
      for (unsigned i = 0; i < elems; i++) {
         const struct glsl_type *child_type;

         switch (glsl_get_base_type(type)) {
         case GLSL_TYPE_INT:
         case GLSL_TYPE_UINT:
         case GLSL_TYPE_INT16:
         case GLSL_TYPE_UINT16:
         case GLSL_TYPE_UINT8:
         case GLSL_TYPE_INT8:
         case GLSL_TYPE_INT64:
         case GLSL_TYPE_UINT64:
         case GLSL_TYPE_BOOL:
         case GLSL_TYPE_FLOAT:
         case GLSL_TYPE_FLOAT16:
         case GLSL_TYPE_DOUBLE:
            child_type = glsl_get_column_type(type);
            break;
         case GLSL_TYPE_ARRAY:
            child_type = glsl_get_array_element(type);
            break;
         case GLSL_TYPE_STRUCT:
         case GLSL_TYPE_INTERFACE:
            child_type = glsl_get_struct_field(type, i);
            break;
         default:
            vtn_fail("unkown base type");
         }

         val->elems[i] = vtn_create_ssa_value(b, child_type);
      }
   }

   return val;
}

static nir_tex_src
vtn_tex_src(struct vtn_builder *b, unsigned index, nir_tex_src_type type)
{
   nir_tex_src src;
   src.src = nir_src_for_ssa(vtn_ssa_value(b, index)->def);
   src.src_type = type;
   return src;
}

static uint32_t
image_operand_arg(struct vtn_builder *b, const uint32_t *w, uint32_t count,
                  uint32_t mask_idx, SpvImageOperandsMask op)
{
   static const SpvImageOperandsMask ops_with_arg =
      SpvImageOperandsBiasMask |
      SpvImageOperandsLodMask |
      SpvImageOperandsGradMask |
      SpvImageOperandsConstOffsetMask |
      SpvImageOperandsOffsetMask |
      SpvImageOperandsConstOffsetsMask |
      SpvImageOperandsSampleMask |
      SpvImageOperandsMinLodMask |
      SpvImageOperandsMakeTexelAvailableMask |
      SpvImageOperandsMakeTexelVisibleMask;

   assert(util_bitcount(op) == 1);
   assert(w[mask_idx] & op);
   assert(op & ops_with_arg);

   uint32_t idx = util_bitcount(w[mask_idx] & (op - 1) & ops_with_arg) + 1;

   /* Adjust indices for operands with two arguments. */
   static const SpvImageOperandsMask ops_with_two_args =
      SpvImageOperandsGradMask;
   idx += util_bitcount(w[mask_idx] & (op - 1) & ops_with_two_args);

   idx += mask_idx;

   vtn_fail_if(idx + (op & ops_with_two_args ? 1 : 0) >= count,
               "Image op claims to have %s but does not enough "
               "following operands", spirv_imageoperands_to_string(op));

   return idx;
}

static void
vtn_handle_texture(struct vtn_builder *b, SpvOp opcode,
                   const uint32_t *w, unsigned count)
{
   if (opcode == SpvOpSampledImage) {
      struct vtn_value *val =
         vtn_push_value(b, w[2], vtn_value_type_sampled_image);
      val->sampled_image = ralloc(b, struct vtn_sampled_image);
      val->sampled_image->type =
         vtn_value(b, w[1], vtn_value_type_type)->type;
      val->sampled_image->image =
         vtn_value(b, w[3], vtn_value_type_pointer)->pointer;
      val->sampled_image->sampler =
         vtn_value(b, w[4], vtn_value_type_pointer)->pointer;
      return;
   } else if (opcode == SpvOpImage) {
      struct vtn_value *src_val = vtn_untyped_value(b, w[3]);
      if (src_val->value_type == vtn_value_type_sampled_image) {
         vtn_push_value_pointer(b, w[2], src_val->sampled_image->image);
      } else {
         vtn_assert(src_val->value_type == vtn_value_type_pointer);
         vtn_push_value_pointer(b, w[2], src_val->pointer);
      }
      return;
   }

   struct vtn_type *ret_type = vtn_value(b, w[1], vtn_value_type_type)->type;

   struct vtn_sampled_image sampled;
   struct vtn_value *sampled_val = vtn_untyped_value(b, w[3]);
   if (sampled_val->value_type == vtn_value_type_sampled_image) {
      sampled = *sampled_val->sampled_image;
   } else {
      vtn_assert(sampled_val->value_type == vtn_value_type_pointer);
      sampled.type = sampled_val->pointer->type;
      sampled.image = NULL;
      sampled.sampler = sampled_val->pointer;
   }

   const struct glsl_type *image_type = sampled.type->type;
   const enum glsl_sampler_dim sampler_dim = glsl_get_sampler_dim(image_type);
   const bool is_array = glsl_sampler_type_is_array(image_type);
   nir_alu_type dest_type = nir_type_invalid;

   /* Figure out the base texture operation */
   nir_texop texop;
   switch (opcode) {
   case SpvOpImageSampleImplicitLod:
   case SpvOpImageSampleDrefImplicitLod:
   case SpvOpImageSampleProjImplicitLod:
   case SpvOpImageSampleProjDrefImplicitLod:
      texop = nir_texop_tex;
      break;

   case SpvOpImageSampleExplicitLod:
   case SpvOpImageSampleDrefExplicitLod:
   case SpvOpImageSampleProjExplicitLod:
   case SpvOpImageSampleProjDrefExplicitLod:
      texop = nir_texop_txl;
      break;

   case SpvOpImageFetch:
      if (glsl_get_sampler_dim(image_type) == GLSL_SAMPLER_DIM_MS) {
         texop = nir_texop_txf_ms;
      } else {
         texop = nir_texop_txf;
      }
      break;

   case SpvOpImageGather:
   case SpvOpImageDrefGather:
      texop = nir_texop_tg4;
      break;

   case SpvOpImageQuerySizeLod:
   case SpvOpImageQuerySize:
      texop = nir_texop_txs;
      dest_type = nir_type_int;
      break;

   case SpvOpImageQueryLod:
      texop = nir_texop_lod;
      dest_type = nir_type_float;
      break;

   case SpvOpImageQueryLevels:
      texop = nir_texop_query_levels;
      dest_type = nir_type_int;
      break;

   case SpvOpImageQuerySamples:
      texop = nir_texop_texture_samples;
      dest_type = nir_type_int;
      break;

   default:
      vtn_fail_with_opcode("Unhandled opcode", opcode);
   }

   nir_tex_src srcs[10]; /* 10 should be enough */
   nir_tex_src *p = srcs;

   nir_deref_instr *sampler = vtn_pointer_to_deref(b, sampled.sampler);
   nir_deref_instr *texture =
      sampled.image ? vtn_pointer_to_deref(b, sampled.image) : sampler;

   p->src = nir_src_for_ssa(&texture->dest.ssa);
   p->src_type = nir_tex_src_texture_deref;
   p++;

   switch (texop) {
   case nir_texop_tex:
   case nir_texop_txb:
   case nir_texop_txl:
   case nir_texop_txd:
   case nir_texop_tg4:
   case nir_texop_lod:
      /* These operations require a sampler */
      p->src = nir_src_for_ssa(&sampler->dest.ssa);
      p->src_type = nir_tex_src_sampler_deref;
      p++;
      break;
   case nir_texop_txf:
   case nir_texop_txf_ms:
   case nir_texop_txs:
   case nir_texop_query_levels:
   case nir_texop_texture_samples:
   case nir_texop_samples_identical:
      /* These don't */
      break;
   case nir_texop_txf_ms_fb:
      vtn_fail("unexpected nir_texop_txf_ms_fb");
      break;
   case nir_texop_txf_ms_mcs:
      vtn_fail("unexpected nir_texop_txf_ms_mcs");
   case nir_texop_tex_prefetch:
      vtn_fail("unexpected nir_texop_tex_prefetch");
   }

   unsigned idx = 4;

   struct nir_ssa_def *coord;
   unsigned coord_components;
   switch (opcode) {
   case SpvOpImageSampleImplicitLod:
   case SpvOpImageSampleExplicitLod:
   case SpvOpImageSampleDrefImplicitLod:
   case SpvOpImageSampleDrefExplicitLod:
   case SpvOpImageSampleProjImplicitLod:
   case SpvOpImageSampleProjExplicitLod:
   case SpvOpImageSampleProjDrefImplicitLod:
   case SpvOpImageSampleProjDrefExplicitLod:
   case SpvOpImageFetch:
   case SpvOpImageGather:
   case SpvOpImageDrefGather:
   case SpvOpImageQueryLod: {
      /* All these types have the coordinate as their first real argument */
      switch (sampler_dim) {
      case GLSL_SAMPLER_DIM_1D:
      case GLSL_SAMPLER_DIM_BUF:
         coord_components = 1;
         break;
      case GLSL_SAMPLER_DIM_2D:
      case GLSL_SAMPLER_DIM_RECT:
      case GLSL_SAMPLER_DIM_MS:
         coord_components = 2;
         break;
      case GLSL_SAMPLER_DIM_3D:
      case GLSL_SAMPLER_DIM_CUBE:
         coord_components = 3;
         break;
      default:
         vtn_fail("Invalid sampler type");
      }

      if (is_array && texop != nir_texop_lod)
         coord_components++;

      coord = vtn_ssa_value(b, w[idx++])->def;
      p->src = nir_src_for_ssa(nir_channels(&b->nb, coord,
                                            (1 << coord_components) - 1));
      p->src_type = nir_tex_src_coord;
      p++;
      break;
   }

   default:
      coord = NULL;
      coord_components = 0;
      break;
   }

   switch (opcode) {
   case SpvOpImageSampleProjImplicitLod:
   case SpvOpImageSampleProjExplicitLod:
   case SpvOpImageSampleProjDrefImplicitLod:
   case SpvOpImageSampleProjDrefExplicitLod:
      /* These have the projector as the last coordinate component */
      p->src = nir_src_for_ssa(nir_channel(&b->nb, coord, coord_components));
      p->src_type = nir_tex_src_projector;
      p++;
      break;

   default:
      break;
   }

   bool is_shadow = false;
   unsigned gather_component = 0;
   switch (opcode) {
   case SpvOpImageSampleDrefImplicitLod:
   case SpvOpImageSampleDrefExplicitLod:
   case SpvOpImageSampleProjDrefImplicitLod:
   case SpvOpImageSampleProjDrefExplicitLod:
   case SpvOpImageDrefGather:
      /* These all have an explicit depth value as their next source */
      is_shadow = true;
      (*p++) = vtn_tex_src(b, w[idx++], nir_tex_src_comparator);
      break;

   case SpvOpImageGather:
      /* This has a component as its next source */
      gather_component = vtn_constant_uint(b, w[idx++]);
      break;

   default:
      break;
   }

   /* For OpImageQuerySizeLod, we always have an LOD */
   if (opcode == SpvOpImageQuerySizeLod)
      (*p++) = vtn_tex_src(b, w[idx++], nir_tex_src_lod);

   /* Now we need to handle some number of optional arguments */
   struct vtn_value *gather_offsets = NULL;
   if (idx < count) {
      uint32_t operands = w[idx];

      if (operands & SpvImageOperandsBiasMask) {
         vtn_assert(texop == nir_texop_tex);
         texop = nir_texop_txb;
         uint32_t arg = image_operand_arg(b, w, count, idx,
                                          SpvImageOperandsBiasMask);
         (*p++) = vtn_tex_src(b, w[arg], nir_tex_src_bias);
      }

      if (operands & SpvImageOperandsLodMask) {
         vtn_assert(texop == nir_texop_txl || texop == nir_texop_txf ||
                    texop == nir_texop_txs);
         uint32_t arg = image_operand_arg(b, w, count, idx,
                                          SpvImageOperandsLodMask);
         (*p++) = vtn_tex_src(b, w[arg], nir_tex_src_lod);
      }

      if (operands & SpvImageOperandsGradMask) {
         vtn_assert(texop == nir_texop_txl);
         texop = nir_texop_txd;
         uint32_t arg = image_operand_arg(b, w, count, idx,
                                          SpvImageOperandsGradMask);
         (*p++) = vtn_tex_src(b, w[arg], nir_tex_src_ddx);
         (*p++) = vtn_tex_src(b, w[arg + 1], nir_tex_src_ddy);
      }

      vtn_fail_if(util_bitcount(operands & (SpvImageOperandsConstOffsetsMask |
                                            SpvImageOperandsOffsetMask |
                                            SpvImageOperandsConstOffsetMask)) > 1,
                  "At most one of the ConstOffset, Offset, and ConstOffsets "
                  "image operands can be used on a given instruction.");

      if (operands & SpvImageOperandsOffsetMask) {
         uint32_t arg = image_operand_arg(b, w, count, idx,
                                          SpvImageOperandsOffsetMask);
         (*p++) = vtn_tex_src(b, w[arg], nir_tex_src_offset);
      }

      if (operands & SpvImageOperandsConstOffsetMask) {
         uint32_t arg = image_operand_arg(b, w, count, idx,
                                          SpvImageOperandsConstOffsetMask);
         (*p++) = vtn_tex_src(b, w[arg], nir_tex_src_offset);
      }

      if (operands & SpvImageOperandsConstOffsetsMask) {
         vtn_assert(texop == nir_texop_tg4);
         uint32_t arg = image_operand_arg(b, w, count, idx,
                                          SpvImageOperandsConstOffsetsMask);
         gather_offsets = vtn_value(b, w[arg], vtn_value_type_constant);
      }

      if (operands & SpvImageOperandsSampleMask) {
         vtn_assert(texop == nir_texop_txf_ms);
         uint32_t arg = image_operand_arg(b, w, count, idx,
                                          SpvImageOperandsSampleMask);
         texop = nir_texop_txf_ms;
         (*p++) = vtn_tex_src(b, w[arg], nir_tex_src_ms_index);
      }

      if (operands & SpvImageOperandsMinLodMask) {
         vtn_assert(texop == nir_texop_tex ||
                    texop == nir_texop_txb ||
                    texop == nir_texop_txd);
         uint32_t arg = image_operand_arg(b, w, count, idx,
                                          SpvImageOperandsMinLodMask);
         (*p++) = vtn_tex_src(b, w[arg], nir_tex_src_min_lod);
      }
   }

   nir_tex_instr *instr = nir_tex_instr_create(b->shader, p - srcs);
   instr->op = texop;

   memcpy(instr->src, srcs, instr->num_srcs * sizeof(*instr->src));

   instr->coord_components = coord_components;
   instr->sampler_dim = sampler_dim;
   instr->is_array = is_array;
   instr->is_shadow = is_shadow;
   instr->is_new_style_shadow =
      is_shadow && glsl_get_components(ret_type->type) == 1;
   instr->component = gather_component;

   if (sampled.image && (sampled.image->access & ACCESS_NON_UNIFORM))
      instr->texture_non_uniform = true;

   if (sampled.sampler && (sampled.sampler->access & ACCESS_NON_UNIFORM))
      instr->sampler_non_uniform = true;

   /* for non-query ops, get dest_type from sampler type */
   if (dest_type == nir_type_invalid) {
      switch (glsl_get_sampler_result_type(image_type)) {
      case GLSL_TYPE_FLOAT:   dest_type = nir_type_float;   break;
      case GLSL_TYPE_INT:     dest_type = nir_type_int;     break;
      case GLSL_TYPE_UINT:    dest_type = nir_type_uint;    break;
      case GLSL_TYPE_BOOL:    dest_type = nir_type_bool;    break;
      default:
         vtn_fail("Invalid base type for sampler result");
      }
   }

   instr->dest_type = dest_type;

   nir_ssa_dest_init(&instr->instr, &instr->dest,
                     nir_tex_instr_dest_size(instr), 32, NULL);

   vtn_assert(glsl_get_vector_elements(ret_type->type) ==
              nir_tex_instr_dest_size(instr));

   if (gather_offsets) {
      vtn_fail_if(gather_offsets->type->base_type != vtn_base_type_array ||
                  gather_offsets->type->length != 4,
                  "ConstOffsets must be an array of size four of vectors "
                  "of two integer components");

      struct vtn_type *vec_type = gather_offsets->type->array_element;
      vtn_fail_if(vec_type->base_type != vtn_base_type_vector ||
                  vec_type->length != 2 ||
                  !glsl_type_is_integer(vec_type->type),
                  "ConstOffsets must be an array of size four of vectors "
                  "of two integer components");

      unsigned bit_size = glsl_get_bit_size(vec_type->type);
      for (uint32_t i = 0; i < 4; i++) {
         const nir_const_value *cvec =
            gather_offsets->constant->elements[i]->values;
         for (uint32_t j = 0; j < 2; j++) {
            switch (bit_size) {
            case 8:  instr->tg4_offsets[i][j] = cvec[j].i8;    break;
            case 16: instr->tg4_offsets[i][j] = cvec[j].i16;   break;
            case 32: instr->tg4_offsets[i][j] = cvec[j].i32;   break;
            case 64: instr->tg4_offsets[i][j] = cvec[j].i64;   break;
            default:
               vtn_fail("Unsupported bit size: %u", bit_size);
            }
         }
      }
   }

   struct vtn_ssa_value *ssa = vtn_create_ssa_value(b, ret_type->type);
   ssa->def = &instr->dest.ssa;
   vtn_push_ssa(b, w[2], ret_type, ssa);

   nir_builder_instr_insert(&b->nb, &instr->instr);
}

static void
fill_common_atomic_sources(struct vtn_builder *b, SpvOp opcode,
                           const uint32_t *w, nir_src *src)
{
   switch (opcode) {
   case SpvOpAtomicIIncrement:
      src[0] = nir_src_for_ssa(nir_imm_int(&b->nb, 1));
      break;

   case SpvOpAtomicIDecrement:
      src[0] = nir_src_for_ssa(nir_imm_int(&b->nb, -1));
      break;

   case SpvOpAtomicISub:
      src[0] =
         nir_src_for_ssa(nir_ineg(&b->nb, vtn_ssa_value(b, w[6])->def));
      break;

   case SpvOpAtomicCompareExchange:
   case SpvOpAtomicCompareExchangeWeak:
      src[0] = nir_src_for_ssa(vtn_ssa_value(b, w[8])->def);
      src[1] = nir_src_for_ssa(vtn_ssa_value(b, w[7])->def);
      break;

   case SpvOpAtomicExchange:
   case SpvOpAtomicIAdd:
   case SpvOpAtomicSMin:
   case SpvOpAtomicUMin:
   case SpvOpAtomicSMax:
   case SpvOpAtomicUMax:
   case SpvOpAtomicAnd:
   case SpvOpAtomicOr:
   case SpvOpAtomicXor:
      src[0] = nir_src_for_ssa(vtn_ssa_value(b, w[6])->def);
      break;

   default:
      vtn_fail_with_opcode("Invalid SPIR-V atomic", opcode);
   }
}

static nir_ssa_def *
get_image_coord(struct vtn_builder *b, uint32_t value)
{
   struct vtn_ssa_value *coord = vtn_ssa_value(b, value);

   /* The image_load_store intrinsics assume a 4-dim coordinate */
   unsigned dim = glsl_get_vector_elements(coord->type);
   unsigned swizzle[4];
   for (unsigned i = 0; i < 4; i++)
      swizzle[i] = MIN2(i, dim - 1);

   return nir_swizzle(&b->nb, coord->def, swizzle, 4);
}

static nir_ssa_def *
expand_to_vec4(nir_builder *b, nir_ssa_def *value)
{
   if (value->num_components == 4)
      return value;

   unsigned swiz[4];
   for (unsigned i = 0; i < 4; i++)
      swiz[i] = i < value->num_components ? i : 0;
   return nir_swizzle(b, value, swiz, 4);
}

static void
vtn_handle_image(struct vtn_builder *b, SpvOp opcode,
                 const uint32_t *w, unsigned count)
{
   /* Just get this one out of the way */
   if (opcode == SpvOpImageTexelPointer) {
      struct vtn_value *val =
         vtn_push_value(b, w[2], vtn_value_type_image_pointer);
      val->image = ralloc(b, struct vtn_image_pointer);

      val->image->image = vtn_value(b, w[3], vtn_value_type_pointer)->pointer;
      val->image->coord = get_image_coord(b, w[4]);
      val->image->sample = vtn_ssa_value(b, w[5])->def;
      return;
   }

   struct vtn_image_pointer image;
   SpvScope scope = SpvScopeInvocation;
   SpvMemorySemanticsMask semantics = 0;

   switch (opcode) {
   case SpvOpAtomicExchange:
   case SpvOpAtomicCompareExchange:
   case SpvOpAtomicCompareExchangeWeak:
   case SpvOpAtomicIIncrement:
   case SpvOpAtomicIDecrement:
   case SpvOpAtomicIAdd:
   case SpvOpAtomicISub:
   case SpvOpAtomicLoad:
   case SpvOpAtomicSMin:
   case SpvOpAtomicUMin:
   case SpvOpAtomicSMax:
   case SpvOpAtomicUMax:
   case SpvOpAtomicAnd:
   case SpvOpAtomicOr:
   case SpvOpAtomicXor:
      image = *vtn_value(b, w[3], vtn_value_type_image_pointer)->image;
      scope = vtn_constant_uint(b, w[4]);
      semantics = vtn_constant_uint(b, w[5]);
      break;

   case SpvOpAtomicStore:
      image = *vtn_value(b, w[1], vtn_value_type_image_pointer)->image;
      scope = vtn_constant_uint(b, w[2]);
      semantics = vtn_constant_uint(b, w[3]);
      break;

   case SpvOpImageQuerySize:
      image.image = vtn_value(b, w[3], vtn_value_type_pointer)->pointer;
      image.coord = NULL;
      image.sample = NULL;
      break;

   case SpvOpImageRead: {
      image.image = vtn_value(b, w[3], vtn_value_type_pointer)->pointer;
      image.coord = get_image_coord(b, w[4]);

      const SpvImageOperandsMask operands =
         count > 5 ? w[5] : SpvImageOperandsMaskNone;

      if (operands & SpvImageOperandsSampleMask) {
         uint32_t arg = image_operand_arg(b, w, count, 5,
                                          SpvImageOperandsSampleMask);
         image.sample = vtn_ssa_value(b, w[arg])->def;
      } else {
         image.sample = nir_ssa_undef(&b->nb, 1, 32);
      }

      if (operands & SpvImageOperandsMakeTexelVisibleMask) {
         vtn_fail_if((operands & SpvImageOperandsNonPrivateTexelMask) == 0,
                     "MakeTexelVisible requires NonPrivateTexel to also be set.");
         uint32_t arg = image_operand_arg(b, w, count, 5,
                                          SpvImageOperandsMakeTexelVisibleMask);
         semantics = SpvMemorySemanticsMakeVisibleMask;
         scope = vtn_constant_uint(b, w[arg]);
      }

      /* TODO: Volatile. */

      break;
   }

   case SpvOpImageWrite: {
      image.image = vtn_value(b, w[1], vtn_value_type_pointer)->pointer;
      image.coord = get_image_coord(b, w[2]);

      /* texel = w[3] */

      const SpvImageOperandsMask operands =
         count > 4 ? w[4] : SpvImageOperandsMaskNone;

      if (operands & SpvImageOperandsSampleMask) {
         uint32_t arg = image_operand_arg(b, w, count, 4,
                                          SpvImageOperandsSampleMask);
         image.sample = vtn_ssa_value(b, w[arg])->def;
      } else {
         image.sample = nir_ssa_undef(&b->nb, 1, 32);
      }

      if (operands & SpvImageOperandsMakeTexelAvailableMask) {
         vtn_fail_if((operands & SpvImageOperandsNonPrivateTexelMask) == 0,
                     "MakeTexelAvailable requires NonPrivateTexel to also be set.");
         uint32_t arg = image_operand_arg(b, w, count, 4,
                                          SpvImageOperandsMakeTexelAvailableMask);
         semantics = SpvMemorySemanticsMakeAvailableMask;
         scope = vtn_constant_uint(b, w[arg]);
      }

      /* TODO: Volatile. */

      break;
   }

   default:
      vtn_fail_with_opcode("Invalid image opcode", opcode);
   }

   nir_intrinsic_op op;
   switch (opcode) {
#define OP(S, N) case SpvOp##S: op = nir_intrinsic_image_deref_##N; break;
   OP(ImageQuerySize,            size)
   OP(ImageRead,                 load)
   OP(ImageWrite,                store)
   OP(AtomicLoad,                load)
   OP(AtomicStore,               store)
   OP(AtomicExchange,            atomic_exchange)
   OP(AtomicCompareExchange,     atomic_comp_swap)
   OP(AtomicCompareExchangeWeak, atomic_comp_swap)
   OP(AtomicIIncrement,          atomic_add)
   OP(AtomicIDecrement,          atomic_add)
   OP(AtomicIAdd,                atomic_add)
   OP(AtomicISub,                atomic_add)
   OP(AtomicSMin,                atomic_imin)
   OP(AtomicUMin,                atomic_umin)
   OP(AtomicSMax,                atomic_imax)
   OP(AtomicUMax,                atomic_umax)
   OP(AtomicAnd,                 atomic_and)
   OP(AtomicOr,                  atomic_or)
   OP(AtomicXor,                 atomic_xor)
#undef OP
   default:
      vtn_fail_with_opcode("Invalid image opcode", opcode);
   }

   nir_intrinsic_instr *intrin = nir_intrinsic_instr_create(b->shader, op);

   nir_deref_instr *image_deref = vtn_pointer_to_deref(b, image.image);
   intrin->src[0] = nir_src_for_ssa(&image_deref->dest.ssa);

   /* ImageQuerySize doesn't take any extra parameters */
   if (opcode != SpvOpImageQuerySize) {
      /* The image coordinate is always 4 components but we may not have that
       * many.  Swizzle to compensate.
       */
      intrin->src[1] = nir_src_for_ssa(expand_to_vec4(&b->nb, image.coord));
      intrin->src[2] = nir_src_for_ssa(image.sample);
   }

   nir_intrinsic_set_access(intrin, image.image->access);

   switch (opcode) {
   case SpvOpAtomicLoad:
   case SpvOpImageQuerySize:
   case SpvOpImageRead:
      break;
   case SpvOpAtomicStore:
   case SpvOpImageWrite: {
      const uint32_t value_id = opcode == SpvOpAtomicStore ? w[4] : w[3];
      nir_ssa_def *value = vtn_ssa_value(b, value_id)->def;
      /* nir_intrinsic_image_deref_store always takes a vec4 value */
      assert(op == nir_intrinsic_image_deref_store);
      intrin->num_components = 4;
      intrin->src[3] = nir_src_for_ssa(expand_to_vec4(&b->nb, value));
      break;
   }

   case SpvOpAtomicCompareExchange:
   case SpvOpAtomicCompareExchangeWeak:
   case SpvOpAtomicIIncrement:
   case SpvOpAtomicIDecrement:
   case SpvOpAtomicExchange:
   case SpvOpAtomicIAdd:
   case SpvOpAtomicISub:
   case SpvOpAtomicSMin:
   case SpvOpAtomicUMin:
   case SpvOpAtomicSMax:
   case SpvOpAtomicUMax:
   case SpvOpAtomicAnd:
   case SpvOpAtomicOr:
   case SpvOpAtomicXor:
      fill_common_atomic_sources(b, opcode, w, &intrin->src[3]);
      break;

   default:
      vtn_fail_with_opcode("Invalid image opcode", opcode);
   }

   /* Image operations implicitly have the Image storage memory semantics. */
   semantics |= SpvMemorySemanticsImageMemoryMask;

   SpvMemorySemanticsMask before_semantics;
   SpvMemorySemanticsMask after_semantics;
   vtn_split_barrier_semantics(b, semantics, &before_semantics, &after_semantics);

   if (before_semantics)
      vtn_emit_memory_barrier(b, scope, before_semantics);

   if (opcode != SpvOpImageWrite && opcode != SpvOpAtomicStore) {
      struct vtn_type *type = vtn_value(b, w[1], vtn_value_type_type)->type;

      unsigned dest_components = glsl_get_vector_elements(type->type);
      intrin->num_components = nir_intrinsic_infos[op].dest_components;
      if (intrin->num_components == 0)
         intrin->num_components = dest_components;

      nir_ssa_dest_init(&intrin->instr, &intrin->dest,
                        intrin->num_components, 32, NULL);

      nir_builder_instr_insert(&b->nb, &intrin->instr);

      nir_ssa_def *result = &intrin->dest.ssa;
      if (intrin->num_components != dest_components)
         result = nir_channels(&b->nb, result, (1 << dest_components) - 1);

      struct vtn_value *val =
         vtn_push_ssa(b, w[2], type, vtn_create_ssa_value(b, type->type));
      val->ssa->def = result;
   } else {
      nir_builder_instr_insert(&b->nb, &intrin->instr);
   }

   if (after_semantics)
      vtn_emit_memory_barrier(b, scope, after_semantics);
}

static nir_intrinsic_op
get_ssbo_nir_atomic_op(struct vtn_builder *b, SpvOp opcode)
{
   switch (opcode) {
   case SpvOpAtomicLoad:         return nir_intrinsic_load_ssbo;
   case SpvOpAtomicStore:        return nir_intrinsic_store_ssbo;
#define OP(S, N) case SpvOp##S: return nir_intrinsic_ssbo_##N;
   OP(AtomicExchange,            atomic_exchange)
   OP(AtomicCompareExchange,     atomic_comp_swap)
   OP(AtomicCompareExchangeWeak, atomic_comp_swap)
   OP(AtomicIIncrement,          atomic_add)
   OP(AtomicIDecrement,          atomic_add)
   OP(AtomicIAdd,                atomic_add)
   OP(AtomicISub,                atomic_add)
   OP(AtomicSMin,                atomic_imin)
   OP(AtomicUMin,                atomic_umin)
   OP(AtomicSMax,                atomic_imax)
   OP(AtomicUMax,                atomic_umax)
   OP(AtomicAnd,                 atomic_and)
   OP(AtomicOr,                  atomic_or)
   OP(AtomicXor,                 atomic_xor)
#undef OP
   default:
      vtn_fail_with_opcode("Invalid SSBO atomic", opcode);
   }
}

static nir_intrinsic_op
get_uniform_nir_atomic_op(struct vtn_builder *b, SpvOp opcode)
{
   switch (opcode) {
#define OP(S, N) case SpvOp##S: return nir_intrinsic_atomic_counter_ ##N;
   OP(AtomicLoad,                read_deref)
   OP(AtomicExchange,            exchange)
   OP(AtomicCompareExchange,     comp_swap)
   OP(AtomicCompareExchangeWeak, comp_swap)
   OP(AtomicIIncrement,          inc_deref)
   OP(AtomicIDecrement,          post_dec_deref)
   OP(AtomicIAdd,                add_deref)
   OP(AtomicISub,                add_deref)
   OP(AtomicUMin,                min_deref)
   OP(AtomicUMax,                max_deref)
   OP(AtomicAnd,                 and_deref)
   OP(AtomicOr,                  or_deref)
   OP(AtomicXor,                 xor_deref)
#undef OP
   default:
      /* We left the following out: AtomicStore, AtomicSMin and
       * AtomicSmax. Right now there are not nir intrinsics for them. At this
       * moment Atomic Counter support is needed for ARB_spirv support, so is
       * only need to support GLSL Atomic Counters that are uints and don't
       * allow direct storage.
       */
      unreachable("Invalid uniform atomic");
   }
}

static nir_intrinsic_op
get_deref_nir_atomic_op(struct vtn_builder *b, SpvOp opcode)
{
   switch (opcode) {
   case SpvOpAtomicLoad:         return nir_intrinsic_load_deref;
   case SpvOpAtomicStore:        return nir_intrinsic_store_deref;
#define OP(S, N) case SpvOp##S: return nir_intrinsic_deref_##N;
   OP(AtomicExchange,            atomic_exchange)
   OP(AtomicCompareExchange,     atomic_comp_swap)
   OP(AtomicCompareExchangeWeak, atomic_comp_swap)
   OP(AtomicIIncrement,          atomic_add)
   OP(AtomicIDecrement,          atomic_add)
   OP(AtomicIAdd,                atomic_add)
   OP(AtomicISub,                atomic_add)
   OP(AtomicSMin,                atomic_imin)
   OP(AtomicUMin,                atomic_umin)
   OP(AtomicSMax,                atomic_imax)
   OP(AtomicUMax,                atomic_umax)
   OP(AtomicAnd,                 atomic_and)
   OP(AtomicOr,                  atomic_or)
   OP(AtomicXor,                 atomic_xor)
#undef OP
   default:
      vtn_fail_with_opcode("Invalid shared atomic", opcode);
   }
}

/*
 * Handles shared atomics, ssbo atomics and atomic counters.
 */
static void
vtn_handle_atomics(struct vtn_builder *b, SpvOp opcode,
                   const uint32_t *w, unsigned count)
{
   struct vtn_pointer *ptr;
   nir_intrinsic_instr *atomic;

   SpvScope scope = SpvScopeInvocation;
   SpvMemorySemanticsMask semantics = 0;

   switch (opcode) {
   case SpvOpAtomicLoad:
   case SpvOpAtomicExchange:
   case SpvOpAtomicCompareExchange:
   case SpvOpAtomicCompareExchangeWeak:
   case SpvOpAtomicIIncrement:
   case SpvOpAtomicIDecrement:
   case SpvOpAtomicIAdd:
   case SpvOpAtomicISub:
   case SpvOpAtomicSMin:
   case SpvOpAtomicUMin:
   case SpvOpAtomicSMax:
   case SpvOpAtomicUMax:
   case SpvOpAtomicAnd:
   case SpvOpAtomicOr:
   case SpvOpAtomicXor:
      ptr = vtn_value(b, w[3], vtn_value_type_pointer)->pointer;
      scope = vtn_constant_uint(b, w[4]);
      semantics = vtn_constant_uint(b, w[5]);
      break;

   case SpvOpAtomicStore:
      ptr = vtn_value(b, w[1], vtn_value_type_pointer)->pointer;
      scope = vtn_constant_uint(b, w[2]);
      semantics = vtn_constant_uint(b, w[3]);
      break;

   default:
      vtn_fail_with_opcode("Invalid SPIR-V atomic", opcode);
   }

   /* uniform as "atomic counter uniform" */
   if (ptr->mode == vtn_variable_mode_uniform) {
      nir_deref_instr *deref = vtn_pointer_to_deref(b, ptr);
      const struct glsl_type *deref_type = deref->type;
      nir_intrinsic_op op = get_uniform_nir_atomic_op(b, opcode);
      atomic = nir_intrinsic_instr_create(b->nb.shader, op);
      atomic->src[0] = nir_src_for_ssa(&deref->dest.ssa);

      /* SSBO needs to initialize index/offset. In this case we don't need to,
       * as that info is already stored on the ptr->var->var nir_variable (see
       * vtn_create_variable)
       */

      switch (opcode) {
      case SpvOpAtomicLoad:
         atomic->num_components = glsl_get_vector_elements(deref_type);
         break;

      case SpvOpAtomicStore:
         atomic->num_components = glsl_get_vector_elements(deref_type);
         nir_intrinsic_set_write_mask(atomic, (1 << atomic->num_components) - 1);
         break;

      case SpvOpAtomicExchange:
      case SpvOpAtomicCompareExchange:
      case SpvOpAtomicCompareExchangeWeak:
      case SpvOpAtomicIIncrement:
      case SpvOpAtomicIDecrement:
      case SpvOpAtomicIAdd:
      case SpvOpAtomicISub:
      case SpvOpAtomicSMin:
      case SpvOpAtomicUMin:
      case SpvOpAtomicSMax:
      case SpvOpAtomicUMax:
      case SpvOpAtomicAnd:
      case SpvOpAtomicOr:
      case SpvOpAtomicXor:
         /* Nothing: we don't need to call fill_common_atomic_sources here, as
          * atomic counter uniforms doesn't have sources
          */
         break;

      default:
         unreachable("Invalid SPIR-V atomic");

      }
   } else if (vtn_pointer_uses_ssa_offset(b, ptr)) {
      nir_ssa_def *offset, *index;
      offset = vtn_pointer_to_offset(b, ptr, &index);

      assert(ptr->mode == vtn_variable_mode_ssbo);

      nir_intrinsic_op op  = get_ssbo_nir_atomic_op(b, opcode);
      atomic = nir_intrinsic_instr_create(b->nb.shader, op);

      int src = 0;
      switch (opcode) {
      case SpvOpAtomicLoad:
         atomic->num_components = glsl_get_vector_elements(ptr->type->type);
         nir_intrinsic_set_align(atomic, 4, 0);
         if (ptr->mode == vtn_variable_mode_ssbo)
            atomic->src[src++] = nir_src_for_ssa(index);
         atomic->src[src++] = nir_src_for_ssa(offset);
         break;

      case SpvOpAtomicStore:
         atomic->num_components = glsl_get_vector_elements(ptr->type->type);
         nir_intrinsic_set_write_mask(atomic, (1 << atomic->num_components) - 1);
         nir_intrinsic_set_align(atomic, 4, 0);
         atomic->src[src++] = nir_src_for_ssa(vtn_ssa_value(b, w[4])->def);
         if (ptr->mode == vtn_variable_mode_ssbo)
            atomic->src[src++] = nir_src_for_ssa(index);
         atomic->src[src++] = nir_src_for_ssa(offset);
         break;

      case SpvOpAtomicExchange:
      case SpvOpAtomicCompareExchange:
      case SpvOpAtomicCompareExchangeWeak:
      case SpvOpAtomicIIncrement:
      case SpvOpAtomicIDecrement:
      case SpvOpAtomicIAdd:
      case SpvOpAtomicISub:
      case SpvOpAtomicSMin:
      case SpvOpAtomicUMin:
      case SpvOpAtomicSMax:
      case SpvOpAtomicUMax:
      case SpvOpAtomicAnd:
      case SpvOpAtomicOr:
      case SpvOpAtomicXor:
         if (ptr->mode == vtn_variable_mode_ssbo)
            atomic->src[src++] = nir_src_for_ssa(index);
         atomic->src[src++] = nir_src_for_ssa(offset);
         fill_common_atomic_sources(b, opcode, w, &atomic->src[src]);
         break;

      default:
         vtn_fail_with_opcode("Invalid SPIR-V atomic", opcode);
      }
   } else {
      nir_deref_instr *deref = vtn_pointer_to_deref(b, ptr);
      const struct glsl_type *deref_type = deref->type;
      nir_intrinsic_op op = get_deref_nir_atomic_op(b, opcode);
      atomic = nir_intrinsic_instr_create(b->nb.shader, op);
      atomic->src[0] = nir_src_for_ssa(&deref->dest.ssa);

      switch (opcode) {
      case SpvOpAtomicLoad:
         atomic->num_components = glsl_get_vector_elements(deref_type);
         break;

      case SpvOpAtomicStore:
         atomic->num_components = glsl_get_vector_elements(deref_type);
         nir_intrinsic_set_write_mask(atomic, (1 << atomic->num_components) - 1);
         atomic->src[1] = nir_src_for_ssa(vtn_ssa_value(b, w[4])->def);
         break;

      case SpvOpAtomicExchange:
      case SpvOpAtomicCompareExchange:
      case SpvOpAtomicCompareExchangeWeak:
      case SpvOpAtomicIIncrement:
      case SpvOpAtomicIDecrement:
      case SpvOpAtomicIAdd:
      case SpvOpAtomicISub:
      case SpvOpAtomicSMin:
      case SpvOpAtomicUMin:
      case SpvOpAtomicSMax:
      case SpvOpAtomicUMax:
      case SpvOpAtomicAnd:
      case SpvOpAtomicOr:
      case SpvOpAtomicXor:
         fill_common_atomic_sources(b, opcode, w, &atomic->src[1]);
         break;

      default:
         vtn_fail_with_opcode("Invalid SPIR-V atomic", opcode);
      }
   }

   /* Atomic ordering operations will implicitly apply to the atomic operation
    * storage class, so include that too.
    */
   semantics |= vtn_storage_class_to_memory_semantics(ptr->ptr_type->storage_class);

   SpvMemorySemanticsMask before_semantics;
   SpvMemorySemanticsMask after_semantics;
   vtn_split_barrier_semantics(b, semantics, &before_semantics, &after_semantics);

   if (before_semantics)
      vtn_emit_memory_barrier(b, scope, before_semantics);

   if (opcode != SpvOpAtomicStore) {
      struct vtn_type *type = vtn_value(b, w[1], vtn_value_type_type)->type;

      nir_ssa_dest_init(&atomic->instr, &atomic->dest,
                        glsl_get_vector_elements(type->type),
                        glsl_get_bit_size(type->type), NULL);

      struct vtn_ssa_value *ssa = rzalloc(b, struct vtn_ssa_value);
      ssa->def = &atomic->dest.ssa;
      ssa->type = type->type;
      vtn_push_ssa(b, w[2], type, ssa);
   }

   nir_builder_instr_insert(&b->nb, &atomic->instr);

   if (after_semantics)
      vtn_emit_memory_barrier(b, scope, after_semantics);
}

static nir_alu_instr *
create_vec(struct vtn_builder *b, unsigned num_components, unsigned bit_size)
{
   nir_op op = nir_op_vec(num_components);
   nir_alu_instr *vec = nir_alu_instr_create(b->shader, op);
   nir_ssa_dest_init(&vec->instr, &vec->dest.dest, num_components,
                     bit_size, NULL);
   vec->dest.write_mask = (1 << num_components) - 1;

   return vec;
}

struct vtn_ssa_value *
vtn_ssa_transpose(struct vtn_builder *b, struct vtn_ssa_value *src)
{
   if (src->transposed)
      return src->transposed;

   struct vtn_ssa_value *dest =
      vtn_create_ssa_value(b, glsl_transposed_type(src->type));

   for (unsigned i = 0; i < glsl_get_matrix_columns(dest->type); i++) {
      nir_alu_instr *vec = create_vec(b, glsl_get_matrix_columns(src->type),
                                         glsl_get_bit_size(src->type));
      if (glsl_type_is_vector_or_scalar(src->type)) {
          vec->src[0].src = nir_src_for_ssa(src->def);
          vec->src[0].swizzle[0] = i;
      } else {
         for (unsigned j = 0; j < glsl_get_matrix_columns(src->type); j++) {
            vec->src[j].src = nir_src_for_ssa(src->elems[j]->def);
            vec->src[j].swizzle[0] = i;
         }
      }
      nir_builder_instr_insert(&b->nb, &vec->instr);
      dest->elems[i]->def = &vec->dest.dest.ssa;
   }

   dest->transposed = src;

   return dest;
}

nir_ssa_def *
vtn_vector_extract(struct vtn_builder *b, nir_ssa_def *src, unsigned index)
{
   return nir_channel(&b->nb, src, index);
}

nir_ssa_def *
vtn_vector_insert(struct vtn_builder *b, nir_ssa_def *src, nir_ssa_def *insert,
                  unsigned index)
{
   nir_alu_instr *vec = create_vec(b, src->num_components,
                                   src->bit_size);

   for (unsigned i = 0; i < src->num_components; i++) {
      if (i == index) {
         vec->src[i].src = nir_src_for_ssa(insert);
      } else {
         vec->src[i].src = nir_src_for_ssa(src);
         vec->src[i].swizzle[0] = i;
      }
   }

   nir_builder_instr_insert(&b->nb, &vec->instr);

   return &vec->dest.dest.ssa;
}

static nir_ssa_def *
nir_ieq_imm(nir_builder *b, nir_ssa_def *x, uint64_t i)
{
   return nir_ieq(b, x, nir_imm_intN_t(b, i, x->bit_size));
}

nir_ssa_def *
vtn_vector_extract_dynamic(struct vtn_builder *b, nir_ssa_def *src,
                           nir_ssa_def *index)
{
   return nir_vector_extract(&b->nb, src, nir_i2i(&b->nb, index, 32));
}

nir_ssa_def *
vtn_vector_insert_dynamic(struct vtn_builder *b, nir_ssa_def *src,
                          nir_ssa_def *insert, nir_ssa_def *index)
{
   nir_ssa_def *dest = vtn_vector_insert(b, src, insert, 0);
   for (unsigned i = 1; i < src->num_components; i++)
      dest = nir_bcsel(&b->nb, nir_ieq_imm(&b->nb, index, i),
                       vtn_vector_insert(b, src, insert, i), dest);

   return dest;
}

static nir_ssa_def *
vtn_vector_shuffle(struct vtn_builder *b, unsigned num_components,
                   nir_ssa_def *src0, nir_ssa_def *src1,
                   const uint32_t *indices)
{
   nir_alu_instr *vec = create_vec(b, num_components, src0->bit_size);

   for (unsigned i = 0; i < num_components; i++) {
      uint32_t index = indices[i];
      if (index == 0xffffffff) {
         vec->src[i].src =
            nir_src_for_ssa(nir_ssa_undef(&b->nb, 1, src0->bit_size));
      } else if (index < src0->num_components) {
         vec->src[i].src = nir_src_for_ssa(src0);
         vec->src[i].swizzle[0] = index;
      } else {
         vec->src[i].src = nir_src_for_ssa(src1);
         vec->src[i].swizzle[0] = index - src0->num_components;
      }
   }

   nir_builder_instr_insert(&b->nb, &vec->instr);

   return &vec->dest.dest.ssa;
}

/*
 * Concatentates a number of vectors/scalars together to produce a vector
 */
static nir_ssa_def *
vtn_vector_construct(struct vtn_builder *b, unsigned num_components,
                     unsigned num_srcs, nir_ssa_def **srcs)
{
   nir_alu_instr *vec = create_vec(b, num_components, srcs[0]->bit_size);

   /* From the SPIR-V 1.1 spec for OpCompositeConstruct:
    *
    *    "When constructing a vector, there must be at least two Constituent
    *    operands."
    */
   vtn_assert(num_srcs >= 2);

   unsigned dest_idx = 0;
   for (unsigned i = 0; i < num_srcs; i++) {
      nir_ssa_def *src = srcs[i];
      vtn_assert(dest_idx + src->num_components <= num_components);
      for (unsigned j = 0; j < src->num_components; j++) {
         vec->src[dest_idx].src = nir_src_for_ssa(src);
         vec->src[dest_idx].swizzle[0] = j;
         dest_idx++;
      }
   }

   /* From the SPIR-V 1.1 spec for OpCompositeConstruct:
    *
    *    "When constructing a vector, the total number of components in all
    *    the operands must equal the number of components in Result Type."
    */
   vtn_assert(dest_idx == num_components);

   nir_builder_instr_insert(&b->nb, &vec->instr);

   return &vec->dest.dest.ssa;
}

static struct vtn_ssa_value *
vtn_composite_copy(void *mem_ctx, struct vtn_ssa_value *src)
{
   struct vtn_ssa_value *dest = rzalloc(mem_ctx, struct vtn_ssa_value);
   dest->type = src->type;

   if (glsl_type_is_vector_or_scalar(src->type)) {
      dest->def = src->def;
   } else {
      unsigned elems = glsl_get_length(src->type);

      dest->elems = ralloc_array(mem_ctx, struct vtn_ssa_value *, elems);
      for (unsigned i = 0; i < elems; i++)
         dest->elems[i] = vtn_composite_copy(mem_ctx, src->elems[i]);
   }

   return dest;
}

static struct vtn_ssa_value *
vtn_composite_insert(struct vtn_builder *b, struct vtn_ssa_value *src,
                     struct vtn_ssa_value *insert, const uint32_t *indices,
                     unsigned num_indices)
{
   struct vtn_ssa_value *dest = vtn_composite_copy(b, src);

   struct vtn_ssa_value *cur = dest;
   unsigned i;
   for (i = 0; i < num_indices - 1; i++) {
      cur = cur->elems[indices[i]];
   }

   if (glsl_type_is_vector_or_scalar(cur->type)) {
      /* According to the SPIR-V spec, OpCompositeInsert may work down to
       * the component granularity. In that case, the last index will be
       * the index to insert the scalar into the vector.
       */

      cur->def = vtn_vector_insert(b, cur->def, insert->def, indices[i]);
   } else {
      cur->elems[indices[i]] = insert;
   }

   return dest;
}

static struct vtn_ssa_value *
vtn_composite_extract(struct vtn_builder *b, struct vtn_ssa_value *src,
                      const uint32_t *indices, unsigned num_indices)
{
   struct vtn_ssa_value *cur = src;
   for (unsigned i = 0; i < num_indices; i++) {
      if (glsl_type_is_vector_or_scalar(cur->type)) {
         vtn_assert(i == num_indices - 1);
         /* According to the SPIR-V spec, OpCompositeExtract may work down to
          * the component granularity. The last index will be the index of the
          * vector to extract.
          */

         struct vtn_ssa_value *ret = rzalloc(b, struct vtn_ssa_value);
         ret->type = glsl_scalar_type(glsl_get_base_type(cur->type));
         ret->def = vtn_vector_extract(b, cur->def, indices[i]);
         return ret;
      } else {
         cur = cur->elems[indices[i]];
      }
   }

   return cur;
}

static void
vtn_handle_composite(struct vtn_builder *b, SpvOp opcode,
                     const uint32_t *w, unsigned count)
{
   struct vtn_type *type = vtn_value(b, w[1], vtn_value_type_type)->type;
   struct vtn_ssa_value *ssa = vtn_create_ssa_value(b, type->type);

   switch (opcode) {
   case SpvOpVectorExtractDynamic:
      ssa->def = vtn_vector_extract_dynamic(b, vtn_ssa_value(b, w[3])->def,
                                            vtn_ssa_value(b, w[4])->def);
      break;

   case SpvOpVectorInsertDynamic:
      ssa->def = vtn_vector_insert_dynamic(b, vtn_ssa_value(b, w[3])->def,
                                           vtn_ssa_value(b, w[4])->def,
                                           vtn_ssa_value(b, w[5])->def);
      break;

   case SpvOpVectorShuffle:
      ssa->def = vtn_vector_shuffle(b, glsl_get_vector_elements(type->type),
                                    vtn_ssa_value(b, w[3])->def,
                                    vtn_ssa_value(b, w[4])->def,
                                    w + 5);
      break;

   case SpvOpCompositeConstruct: {
      unsigned elems = count - 3;
      assume(elems >= 1);
      if (glsl_type_is_vector_or_scalar(type->type)) {
         nir_ssa_def *srcs[NIR_MAX_VEC_COMPONENTS];
         for (unsigned i = 0; i < elems; i++)
            srcs[i] = vtn_ssa_value(b, w[3 + i])->def;
         ssa->def =
            vtn_vector_construct(b, glsl_get_vector_elements(type->type),
                                 elems, srcs);
      } else {
         ssa->elems = ralloc_array(b, struct vtn_ssa_value *, elems);
         for (unsigned i = 0; i < elems; i++)
            ssa->elems[i] = vtn_ssa_value(b, w[3 + i]);
      }
      break;
   }
   case SpvOpCompositeExtract:
      ssa = vtn_composite_extract(b, vtn_ssa_value(b, w[3]),
                                  w + 4, count - 4);
      break;

   case SpvOpCompositeInsert:
      ssa = vtn_composite_insert(b, vtn_ssa_value(b, w[4]),
                                 vtn_ssa_value(b, w[3]),
                                 w + 5, count - 5);
      break;

   case SpvOpCopyLogical:
   case SpvOpCopyObject:
      ssa = vtn_composite_copy(b, vtn_ssa_value(b, w[3]));
      break;

   default:
      vtn_fail_with_opcode("unknown composite operation", opcode);
   }

   vtn_push_ssa(b, w[2], type, ssa);
}

static void
vtn_emit_barrier(struct vtn_builder *b, nir_intrinsic_op op)
{
   nir_intrinsic_instr *intrin = nir_intrinsic_instr_create(b->shader, op);
   nir_builder_instr_insert(&b->nb, &intrin->instr);
}

void
vtn_emit_memory_barrier(struct vtn_builder *b, SpvScope scope,
                        SpvMemorySemanticsMask semantics)
{
   if (b->options->use_scoped_memory_barrier) {
      vtn_emit_scoped_memory_barrier(b, scope, semantics);
      return;
   }

   static const SpvMemorySemanticsMask all_memory_semantics =
      SpvMemorySemanticsUniformMemoryMask |
      SpvMemorySemanticsWorkgroupMemoryMask |
      SpvMemorySemanticsAtomicCounterMemoryMask |
      SpvMemorySemanticsImageMemoryMask;

   /* If we're not actually doing a memory barrier, bail */
   if (!(semantics & all_memory_semantics))
      return;

   /* GL and Vulkan don't have these */
   vtn_assert(scope != SpvScopeCrossDevice);

   if (scope == SpvScopeSubgroup)
      return; /* Nothing to do here */

   if (scope == SpvScopeWorkgroup) {
      vtn_emit_barrier(b, nir_intrinsic_group_memory_barrier);
      return;
   }

   /* There's only two scopes thing left */
   vtn_assert(scope == SpvScopeInvocation || scope == SpvScopeDevice);

   if ((semantics & all_memory_semantics) == all_memory_semantics) {
      vtn_emit_barrier(b, nir_intrinsic_memory_barrier);
      return;
   }

   /* Issue a bunch of more specific barriers */
   uint32_t bits = semantics;
   while (bits) {
      SpvMemorySemanticsMask semantic = 1 << u_bit_scan(&bits);
      switch (semantic) {
      case SpvMemorySemanticsUniformMemoryMask:
         vtn_emit_barrier(b, nir_intrinsic_memory_barrier_buffer);
         break;
      case SpvMemorySemanticsWorkgroupMemoryMask:
         vtn_emit_barrier(b, nir_intrinsic_memory_barrier_shared);
         break;
      case SpvMemorySemanticsAtomicCounterMemoryMask:
         vtn_emit_barrier(b, nir_intrinsic_memory_barrier_atomic_counter);
         break;
      case SpvMemorySemanticsImageMemoryMask:
         vtn_emit_barrier(b, nir_intrinsic_memory_barrier_image);
         break;
      default:
         break;;
      }
   }
}

static void
vtn_handle_barrier(struct vtn_builder *b, SpvOp opcode,
                   const uint32_t *w, unsigned count)
{
   switch (opcode) {
   case SpvOpEmitVertex:
   case SpvOpEmitStreamVertex:
   case SpvOpEndPrimitive:
   case SpvOpEndStreamPrimitive: {
      nir_intrinsic_op intrinsic_op;
      switch (opcode) {
      case SpvOpEmitVertex:
      case SpvOpEmitStreamVertex:
         intrinsic_op = nir_intrinsic_emit_vertex;
         break;
      case SpvOpEndPrimitive:
      case SpvOpEndStreamPrimitive:
         intrinsic_op = nir_intrinsic_end_primitive;
         break;
      default:
         unreachable("Invalid opcode");
      }

      nir_intrinsic_instr *intrin =
         nir_intrinsic_instr_create(b->shader, intrinsic_op);

      switch (opcode) {
      case SpvOpEmitStreamVertex:
      case SpvOpEndStreamPrimitive: {
         unsigned stream = vtn_constant_uint(b, w[1]);
         nir_intrinsic_set_stream_id(intrin, stream);
         break;
      }

      default:
         break;
      }

      nir_builder_instr_insert(&b->nb, &intrin->instr);
      break;
   }

   case SpvOpMemoryBarrier: {
      SpvScope scope = vtn_constant_uint(b, w[1]);
      SpvMemorySemanticsMask semantics = vtn_constant_uint(b, w[2]);
      vtn_emit_memory_barrier(b, scope, semantics);
      return;
   }

   case SpvOpControlBarrier: {
      SpvScope memory_scope = vtn_constant_uint(b, w[2]);
      SpvMemorySemanticsMask memory_semantics = vtn_constant_uint(b, w[3]);
      vtn_emit_memory_barrier(b, memory_scope, memory_semantics);

      SpvScope execution_scope = vtn_constant_uint(b, w[1]);
      if (execution_scope == SpvScopeWorkgroup)
         vtn_emit_barrier(b, nir_intrinsic_barrier);
      break;
   }

   default:
      unreachable("unknown barrier instruction");
   }
}

static unsigned
gl_primitive_from_spv_execution_mode(struct vtn_builder *b,
                                     SpvExecutionMode mode)
{
   switch (mode) {
   case SpvExecutionModeInputPoints:
   case SpvExecutionModeOutputPoints:
      return 0; /* GL_POINTS */
   case SpvExecutionModeInputLines:
      return 1; /* GL_LINES */
   case SpvExecutionModeInputLinesAdjacency:
      return 0x000A; /* GL_LINE_STRIP_ADJACENCY_ARB */
   case SpvExecutionModeTriangles:
      return 4; /* GL_TRIANGLES */
   case SpvExecutionModeInputTrianglesAdjacency:
      return 0x000C; /* GL_TRIANGLES_ADJACENCY_ARB */
   case SpvExecutionModeQuads:
      return 7; /* GL_QUADS */
   case SpvExecutionModeIsolines:
      return 0x8E7A; /* GL_ISOLINES */
   case SpvExecutionModeOutputLineStrip:
      return 3; /* GL_LINE_STRIP */
   case SpvExecutionModeOutputTriangleStrip:
      return 5; /* GL_TRIANGLE_STRIP */
   default:
      vtn_fail("Invalid primitive type: %s (%u)",
               spirv_executionmode_to_string(mode), mode);
   }
}

static unsigned
vertices_in_from_spv_execution_mode(struct vtn_builder *b,
                                    SpvExecutionMode mode)
{
   switch (mode) {
   case SpvExecutionModeInputPoints:
      return 1;
   case SpvExecutionModeInputLines:
      return 2;
   case SpvExecutionModeInputLinesAdjacency:
      return 4;
   case SpvExecutionModeTriangles:
      return 3;
   case SpvExecutionModeInputTrianglesAdjacency:
      return 6;
   default:
      vtn_fail("Invalid GS input mode: %s (%u)",
               spirv_executionmode_to_string(mode), mode);
   }
}

static gl_shader_stage
stage_for_execution_model(struct vtn_builder *b, SpvExecutionModel model)
{
   switch (model) {
   case SpvExecutionModelVertex:
      return MESA_SHADER_VERTEX;
   case SpvExecutionModelTessellationControl:
      return MESA_SHADER_TESS_CTRL;
   case SpvExecutionModelTessellationEvaluation:
      return MESA_SHADER_TESS_EVAL;
   case SpvExecutionModelGeometry:
      return MESA_SHADER_GEOMETRY;
   case SpvExecutionModelFragment:
      return MESA_SHADER_FRAGMENT;
   case SpvExecutionModelGLCompute:
      return MESA_SHADER_COMPUTE;
   case SpvExecutionModelKernel:
      return MESA_SHADER_KERNEL;
   default:
      vtn_fail("Unsupported execution model: %s (%u)",
               spirv_executionmodel_to_string(model), model);
   }
}

#define spv_check_supported(name, cap) do {                 \
      if (!(b->options && b->options->caps.name))           \
         vtn_warn("Unsupported SPIR-V capability: %s (%u)", \
                  spirv_capability_to_string(cap), cap);    \
   } while(0)


void
vtn_handle_entry_point(struct vtn_builder *b, const uint32_t *w,
                       unsigned count)
{
   struct vtn_value *entry_point = &b->values[w[2]];
   /* Let this be a name label regardless */
   unsigned name_words;
   entry_point->name = vtn_string_literal(b, &w[3], count - 3, &name_words);

   if (strcmp(entry_point->name, b->entry_point_name) != 0 ||
       stage_for_execution_model(b, w[1]) != b->entry_point_stage)
      return;

   vtn_assert(b->entry_point == NULL);
   b->entry_point = entry_point;
}

static bool
vtn_handle_preamble_instruction(struct vtn_builder *b, SpvOp opcode,
                                const uint32_t *w, unsigned count)
{
   switch (opcode) {
   case SpvOpSource: {
      const char *lang;
      switch (w[1]) {
      default:
      case SpvSourceLanguageUnknown:      lang = "unknown";    break;
      case SpvSourceLanguageESSL:         lang = "ESSL";       break;
      case SpvSourceLanguageGLSL:         lang = "GLSL";       break;
      case SpvSourceLanguageOpenCL_C:     lang = "OpenCL C";   break;
      case SpvSourceLanguageOpenCL_CPP:   lang = "OpenCL C++"; break;
      case SpvSourceLanguageHLSL:         lang = "HLSL";       break;
      }

      uint32_t version = w[2];

      const char *file =
         (count > 3) ? vtn_value(b, w[3], vtn_value_type_string)->str : "";

      vtn_info("Parsing SPIR-V from %s %u source file %s", lang, version, file);
      break;
   }

   case SpvOpSourceExtension:
   case SpvOpSourceContinued:
   case SpvOpExtension:
   case SpvOpModuleProcessed:
      /* Unhandled, but these are for debug so that's ok. */
      break;

   case SpvOpCapability: {
      SpvCapability cap = w[1];
      switch (cap) {
      case SpvCapabilityMatrix:
      case SpvCapabilityShader:
      case SpvCapabilityGeometry:
      case SpvCapabilityGeometryPointSize:
      case SpvCapabilityUniformBufferArrayDynamicIndexing:
      case SpvCapabilitySampledImageArrayDynamicIndexing:
      case SpvCapabilityStorageBufferArrayDynamicIndexing:
      case SpvCapabilityStorageImageArrayDynamicIndexing:
      case SpvCapabilityImageRect:
      case SpvCapabilitySampledRect:
      case SpvCapabilitySampled1D:
      case SpvCapabilityImage1D:
      case SpvCapabilitySampledCubeArray:
      case SpvCapabilityImageCubeArray:
      case SpvCapabilitySampledBuffer:
      case SpvCapabilityImageBuffer:
      case SpvCapabilityImageQuery:
      case SpvCapabilityDerivativeControl:
      case SpvCapabilityInterpolationFunction:
      case SpvCapabilityMultiViewport:
      case SpvCapabilitySampleRateShading:
      case SpvCapabilityClipDistance:
      case SpvCapabilityCullDistance:
      case SpvCapabilityInputAttachment:
      case SpvCapabilityImageGatherExtended:
      case SpvCapabilityStorageImageExtendedFormats:
         break;

      case SpvCapabilityLinkage:
      case SpvCapabilityVector16:
      case SpvCapabilityFloat16Buffer:
      case SpvCapabilitySparseResidency:
         vtn_warn("Unsupported SPIR-V capability: %s",
                  spirv_capability_to_string(cap));
         break;

      case SpvCapabilityMinLod:
         spv_check_supported(min_lod, cap);
         break;

      case SpvCapabilityAtomicStorage:
         spv_check_supported(atomic_storage, cap);
         break;

      case SpvCapabilityFloat64:
         spv_check_supported(float64, cap);
         break;
      case SpvCapabilityInt64:
         spv_check_supported(int64, cap);
         break;
      case SpvCapabilityInt16:
         spv_check_supported(int16, cap);
         break;
      case SpvCapabilityInt8:
         spv_check_supported(int8, cap);
         break;

      case SpvCapabilityTransformFeedback:
         spv_check_supported(transform_feedback, cap);
         break;

      case SpvCapabilityGeometryStreams:
         spv_check_supported(geometry_streams, cap);
         break;

      case SpvCapabilityInt64Atomics:
         spv_check_supported(int64_atomics, cap);
         break;

      case SpvCapabilityStorageImageMultisample:
         spv_check_supported(storage_image_ms, cap);
         break;

      case SpvCapabilityAddresses:
         spv_check_supported(address, cap);
         break;

      case SpvCapabilityKernel:
         spv_check_supported(kernel, cap);
         break;

      case SpvCapabilityImageBasic:
      case SpvCapabilityImageReadWrite:
      case SpvCapabilityImageMipmap:
      case SpvCapabilityPipes:
      case SpvCapabilityDeviceEnqueue:
      case SpvCapabilityLiteralSampler:
      case SpvCapabilityGenericPointer:
         vtn_warn("Unsupported OpenCL-style SPIR-V capability: %s",
                  spirv_capability_to_string(cap));
         break;

      case SpvCapabilityImageMSArray:
         spv_check_supported(image_ms_array, cap);
         break;

      case SpvCapabilityTessellation:
      case SpvCapabilityTessellationPointSize:
         spv_check_supported(tessellation, cap);
         break;

      case SpvCapabilityDrawParameters:
         spv_check_supported(draw_parameters, cap);
         break;

      case SpvCapabilityStorageImageReadWithoutFormat:
         spv_check_supported(image_read_without_format, cap);
         break;

      case SpvCapabilityStorageImageWriteWithoutFormat:
         spv_check_supported(image_write_without_format, cap);
         break;

      case SpvCapabilityDeviceGroup:
         spv_check_supported(device_group, cap);
         break;

      case SpvCapabilityMultiView:
         spv_check_supported(multiview, cap);
         break;

      case SpvCapabilityGroupNonUniform:
         spv_check_supported(subgroup_basic, cap);
         break;

      case SpvCapabilitySubgroupVoteKHR:
      case SpvCapabilityGroupNonUniformVote:
         spv_check_supported(subgroup_vote, cap);
         break;

      case SpvCapabilitySubgroupBallotKHR:
      case SpvCapabilityGroupNonUniformBallot:
         spv_check_supported(subgroup_ballot, cap);
         break;

      case SpvCapabilityGroupNonUniformShuffle:
      case SpvCapabilityGroupNonUniformShuffleRelative:
         spv_check_supported(subgroup_shuffle, cap);
         break;

      case SpvCapabilityGroupNonUniformQuad:
         spv_check_supported(subgroup_quad, cap);
         break;

      case SpvCapabilityGroupNonUniformArithmetic:
      case SpvCapabilityGroupNonUniformClustered:
         spv_check_supported(subgroup_arithmetic, cap);
         break;

      case SpvCapabilityGroups:
         spv_check_supported(amd_shader_ballot, cap);
         break;

      case SpvCapabilityVariablePointersStorageBuffer:
      case SpvCapabilityVariablePointers:
         spv_check_supported(variable_pointers, cap);
         b->variable_pointers = true;
         break;

      case SpvCapabilityStorageUniformBufferBlock16:
      case SpvCapabilityStorageUniform16:
      case SpvCapabilityStoragePushConstant16:
      case SpvCapabilityStorageInputOutput16:
         spv_check_supported(storage_16bit, cap);
         break;

      case SpvCapabilityShaderLayer:
      case SpvCapabilityShaderViewportIndex:
      case SpvCapabilityShaderViewportIndexLayerEXT:
         spv_check_supported(shader_viewport_index_layer, cap);
         break;

      case SpvCapabilityStorageBuffer8BitAccess:
      case SpvCapabilityUniformAndStorageBuffer8BitAccess:
      case SpvCapabilityStoragePushConstant8:
         spv_check_supported(storage_8bit, cap);
         break;

      case SpvCapabilityShaderNonUniformEXT:
         spv_check_supported(descriptor_indexing, cap);
         break;

      case SpvCapabilityInputAttachmentArrayDynamicIndexingEXT:
      case SpvCapabilityUniformTexelBufferArrayDynamicIndexingEXT:
      case SpvCapabilityStorageTexelBufferArrayDynamicIndexingEXT:
         spv_check_supported(descriptor_array_dynamic_indexing, cap);
         break;

      case SpvCapabilityUniformBufferArrayNonUniformIndexingEXT:
      case SpvCapabilitySampledImageArrayNonUniformIndexingEXT:
      case SpvCapabilityStorageBufferArrayNonUniformIndexingEXT:
      case SpvCapabilityStorageImageArrayNonUniformIndexingEXT:
      case SpvCapabilityInputAttachmentArrayNonUniformIndexingEXT:
      case SpvCapabilityUniformTexelBufferArrayNonUniformIndexingEXT:
      case SpvCapabilityStorageTexelBufferArrayNonUniformIndexingEXT:
         spv_check_supported(descriptor_array_non_uniform_indexing, cap);
         break;

      case SpvCapabilityRuntimeDescriptorArrayEXT:
         spv_check_supported(runtime_descriptor_array, cap);
         break;

      case SpvCapabilityStencilExportEXT:
         spv_check_supported(stencil_export, cap);
         break;

      case SpvCapabilitySampleMaskPostDepthCoverage:
         spv_check_supported(post_depth_coverage, cap);
         break;

      case SpvCapabilityDenormFlushToZero:
      case SpvCapabilityDenormPreserve:
      case SpvCapabilitySignedZeroInfNanPreserve:
      case SpvCapabilityRoundingModeRTE:
      case SpvCapabilityRoundingModeRTZ:
         spv_check_supported(float_controls, cap);
         break;

      case SpvCapabilityPhysicalStorageBufferAddressesEXT:
         spv_check_supported(physical_storage_buffer_address, cap);
         break;

      case SpvCapabilityComputeDerivativeGroupQuadsNV:
      case SpvCapabilityComputeDerivativeGroupLinearNV:
         spv_check_supported(derivative_group, cap);
         break;

      case SpvCapabilityFloat16:
         spv_check_supported(float16, cap);
         break;

      case SpvCapabilityFragmentShaderSampleInterlockEXT:
         spv_check_supported(fragment_shader_sample_interlock, cap);
         break;

      case SpvCapabilityFragmentShaderPixelInterlockEXT:
         spv_check_supported(fragment_shader_pixel_interlock, cap);
         break;

      case SpvCapabilityDemoteToHelperInvocationEXT:
         spv_check_supported(demote_to_helper_invocation, cap);
         break;

      case SpvCapabilityShaderClockKHR:
         spv_check_supported(shader_clock, cap);
	 break;

      case SpvCapabilityVulkanMemoryModel:
         spv_check_supported(vk_memory_model, cap);
         break;

      case SpvCapabilityVulkanMemoryModelDeviceScope:
         spv_check_supported(vk_memory_model_device_scope, cap);
         break;

      default:
         vtn_fail("Unhandled capability: %s (%u)",
                  spirv_capability_to_string(cap), cap);
      }
      break;
   }

   case SpvOpExtInstImport:
      vtn_handle_extension(b, opcode, w, count);
      break;

   case SpvOpMemoryModel:
      switch (w[1]) {
      case SpvAddressingModelPhysical32:
         vtn_fail_if(b->shader->info.stage != MESA_SHADER_KERNEL,
                     "AddressingModelPhysical32 only supported for kernels");
         b->shader->info.cs.ptr_size = 32;
         b->physical_ptrs = true;
         b->options->shared_addr_format = nir_address_format_32bit_global;
         b->options->global_addr_format = nir_address_format_32bit_global;
         b->options->temp_addr_format = nir_address_format_32bit_global;
         break;
      case SpvAddressingModelPhysical64:
         vtn_fail_if(b->shader->info.stage != MESA_SHADER_KERNEL,
                     "AddressingModelPhysical64 only supported for kernels");
         b->shader->info.cs.ptr_size = 64;
         b->physical_ptrs = true;
         b->options->shared_addr_format = nir_address_format_64bit_global;
         b->options->global_addr_format = nir_address_format_64bit_global;
         b->options->temp_addr_format = nir_address_format_64bit_global;
         break;
      case SpvAddressingModelLogical:
         vtn_fail_if(b->shader->info.stage >= MESA_SHADER_STAGES,
                     "AddressingModelLogical only supported for shaders");
         b->shader->info.cs.ptr_size = 0;
         b->physical_ptrs = false;
         break;
      case SpvAddressingModelPhysicalStorageBuffer64EXT:
         vtn_fail_if(!b->options ||
                     !b->options->caps.physical_storage_buffer_address,
                     "AddressingModelPhysicalStorageBuffer64EXT not supported");
         break;
      default:
         vtn_fail("Unknown addressing model: %s (%u)",
                  spirv_addressingmodel_to_string(w[1]), w[1]);
         break;
      }

      switch (w[2]) {
      case SpvMemoryModelSimple:
      case SpvMemoryModelGLSL450:
      case SpvMemoryModelOpenCL:
         break;
      case SpvMemoryModelVulkan:
         vtn_fail_if(!b->options->caps.vk_memory_model,
                     "Vulkan memory model is unsupported by this driver");
         break;
      default:
         vtn_fail("Unsupported memory model: %s",
                  spirv_memorymodel_to_string(w[2]));
         break;
      }
      break;

   case SpvOpEntryPoint:
      vtn_handle_entry_point(b, w, count);
      break;

   case SpvOpString:
      vtn_push_value(b, w[1], vtn_value_type_string)->str =
         vtn_string_literal(b, &w[2], count - 2, NULL);
      break;

   case SpvOpName:
      b->values[w[1]].name = vtn_string_literal(b, &w[2], count - 2, NULL);
      break;

   case SpvOpMemberName:
      /* TODO */
      break;

   case SpvOpExecutionMode:
   case SpvOpExecutionModeId:
   case SpvOpDecorationGroup:
   case SpvOpDecorate:
   case SpvOpDecorateId:
   case SpvOpMemberDecorate:
   case SpvOpGroupDecorate:
   case SpvOpGroupMemberDecorate:
   case SpvOpDecorateString:
   case SpvOpMemberDecorateString:
      vtn_handle_decoration(b, opcode, w, count);
      break;

   default:
      return false; /* End of preamble */
   }

   return true;
}

static void
vtn_handle_execution_mode(struct vtn_builder *b, struct vtn_value *entry_point,
                          const struct vtn_decoration *mode, void *data)
{
   vtn_assert(b->entry_point == entry_point);

   switch(mode->exec_mode) {
   case SpvExecutionModeOriginUpperLeft:
   case SpvExecutionModeOriginLowerLeft:
      vtn_assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      b->shader->info.fs.origin_upper_left =
         (mode->exec_mode == SpvExecutionModeOriginUpperLeft);
      break;

   case SpvExecutionModeEarlyFragmentTests:
      vtn_assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      b->shader->info.fs.early_fragment_tests = true;
      break;

   case SpvExecutionModePostDepthCoverage:
      vtn_assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      b->shader->info.fs.post_depth_coverage = true;
      break;

   case SpvExecutionModeInvocations:
      vtn_assert(b->shader->info.stage == MESA_SHADER_GEOMETRY);
      b->shader->info.gs.invocations = MAX2(1, mode->operands[0]);
      break;

   case SpvExecutionModeDepthReplacing:
      vtn_assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      b->shader->info.fs.depth_layout = FRAG_DEPTH_LAYOUT_ANY;
      break;
   case SpvExecutionModeDepthGreater:
      vtn_assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      b->shader->info.fs.depth_layout = FRAG_DEPTH_LAYOUT_GREATER;
      break;
   case SpvExecutionModeDepthLess:
      vtn_assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      b->shader->info.fs.depth_layout = FRAG_DEPTH_LAYOUT_LESS;
      break;
   case SpvExecutionModeDepthUnchanged:
      vtn_assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      b->shader->info.fs.depth_layout = FRAG_DEPTH_LAYOUT_UNCHANGED;
      break;

   case SpvExecutionModeLocalSize:
      vtn_assert(gl_shader_stage_is_compute(b->shader->info.stage));
      b->shader->info.cs.local_size[0] = mode->operands[0];
      b->shader->info.cs.local_size[1] = mode->operands[1];
      b->shader->info.cs.local_size[2] = mode->operands[2];
      break;

   case SpvExecutionModeLocalSizeId:
      b->shader->info.cs.local_size[0] = vtn_constant_uint(b, mode->operands[0]);
      b->shader->info.cs.local_size[1] = vtn_constant_uint(b, mode->operands[1]);
      b->shader->info.cs.local_size[2] = vtn_constant_uint(b, mode->operands[2]);
      break;

   case SpvExecutionModeLocalSizeHint:
   case SpvExecutionModeLocalSizeHintId:
      break; /* Nothing to do with this */

   case SpvExecutionModeOutputVertices:
      if (b->shader->info.stage == MESA_SHADER_TESS_CTRL ||
          b->shader->info.stage == MESA_SHADER_TESS_EVAL) {
         b->shader->info.tess.tcs_vertices_out = mode->operands[0];
      } else {
         vtn_assert(b->shader->info.stage == MESA_SHADER_GEOMETRY);
         b->shader->info.gs.vertices_out = mode->operands[0];
      }
      break;

   case SpvExecutionModeInputPoints:
   case SpvExecutionModeInputLines:
   case SpvExecutionModeInputLinesAdjacency:
   case SpvExecutionModeTriangles:
   case SpvExecutionModeInputTrianglesAdjacency:
   case SpvExecutionModeQuads:
   case SpvExecutionModeIsolines:
      if (b->shader->info.stage == MESA_SHADER_TESS_CTRL ||
          b->shader->info.stage == MESA_SHADER_TESS_EVAL) {
         b->shader->info.tess.primitive_mode =
            gl_primitive_from_spv_execution_mode(b, mode->exec_mode);
      } else {
         vtn_assert(b->shader->info.stage == MESA_SHADER_GEOMETRY);
         b->shader->info.gs.vertices_in =
            vertices_in_from_spv_execution_mode(b, mode->exec_mode);
         b->shader->info.gs.input_primitive =
            gl_primitive_from_spv_execution_mode(b, mode->exec_mode);
      }
      break;

   case SpvExecutionModeOutputPoints:
   case SpvExecutionModeOutputLineStrip:
   case SpvExecutionModeOutputTriangleStrip:
      vtn_assert(b->shader->info.stage == MESA_SHADER_GEOMETRY);
      b->shader->info.gs.output_primitive =
         gl_primitive_from_spv_execution_mode(b, mode->exec_mode);
      break;

   case SpvExecutionModeSpacingEqual:
      vtn_assert(b->shader->info.stage == MESA_SHADER_TESS_CTRL ||
                 b->shader->info.stage == MESA_SHADER_TESS_EVAL);
      b->shader->info.tess.spacing = TESS_SPACING_EQUAL;
      break;
   case SpvExecutionModeSpacingFractionalEven:
      vtn_assert(b->shader->info.stage == MESA_SHADER_TESS_CTRL ||
                 b->shader->info.stage == MESA_SHADER_TESS_EVAL);
      b->shader->info.tess.spacing = TESS_SPACING_FRACTIONAL_EVEN;
      break;
   case SpvExecutionModeSpacingFractionalOdd:
      vtn_assert(b->shader->info.stage == MESA_SHADER_TESS_CTRL ||
                 b->shader->info.stage == MESA_SHADER_TESS_EVAL);
      b->shader->info.tess.spacing = TESS_SPACING_FRACTIONAL_ODD;
      break;
   case SpvExecutionModeVertexOrderCw:
      vtn_assert(b->shader->info.stage == MESA_SHADER_TESS_CTRL ||
                 b->shader->info.stage == MESA_SHADER_TESS_EVAL);
      b->shader->info.tess.ccw = false;
      break;
   case SpvExecutionModeVertexOrderCcw:
      vtn_assert(b->shader->info.stage == MESA_SHADER_TESS_CTRL ||
                 b->shader->info.stage == MESA_SHADER_TESS_EVAL);
      b->shader->info.tess.ccw = true;
      break;
   case SpvExecutionModePointMode:
      vtn_assert(b->shader->info.stage == MESA_SHADER_TESS_CTRL ||
                 b->shader->info.stage == MESA_SHADER_TESS_EVAL);
      b->shader->info.tess.point_mode = true;
      break;

   case SpvExecutionModePixelCenterInteger:
      vtn_assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      b->shader->info.fs.pixel_center_integer = true;
      break;

   case SpvExecutionModeXfb:
      b->shader->info.has_transform_feedback_varyings = true;
      break;

   case SpvExecutionModeVecTypeHint:
      break; /* OpenCL */

   case SpvExecutionModeContractionOff:
      if (b->shader->info.stage != MESA_SHADER_KERNEL)
         vtn_warn("ExectionMode only allowed for CL-style kernels: %s",
                  spirv_executionmode_to_string(mode->exec_mode));
      else
         b->exact = true;
      break;

   case SpvExecutionModeStencilRefReplacingEXT:
      vtn_assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      break;

   case SpvExecutionModeDerivativeGroupQuadsNV:
      vtn_assert(b->shader->info.stage == MESA_SHADER_COMPUTE);
      b->shader->info.cs.derivative_group = DERIVATIVE_GROUP_QUADS;
      break;

   case SpvExecutionModeDerivativeGroupLinearNV:
      vtn_assert(b->shader->info.stage == MESA_SHADER_COMPUTE);
      b->shader->info.cs.derivative_group = DERIVATIVE_GROUP_LINEAR;
      break;

   case SpvExecutionModePixelInterlockOrderedEXT:
      vtn_assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      b->shader->info.fs.pixel_interlock_ordered = true;
      break;

   case SpvExecutionModePixelInterlockUnorderedEXT:
      vtn_assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      b->shader->info.fs.pixel_interlock_unordered = true;
      break;

   case SpvExecutionModeSampleInterlockOrderedEXT:
      vtn_assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      b->shader->info.fs.sample_interlock_ordered = true;
      break;

   case SpvExecutionModeSampleInterlockUnorderedEXT:
      vtn_assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      b->shader->info.fs.sample_interlock_unordered = true;
      break;

   case SpvExecutionModeDenormPreserve:
   case SpvExecutionModeDenormFlushToZero:
   case SpvExecutionModeSignedZeroInfNanPreserve:
   case SpvExecutionModeRoundingModeRTE:
   case SpvExecutionModeRoundingModeRTZ:
      /* Already handled in vtn_handle_rounding_mode_in_execution_mode() */
      break;

   default:
      vtn_fail("Unhandled execution mode: %s (%u)",
               spirv_executionmode_to_string(mode->exec_mode),
               mode->exec_mode);
   }
}

static void
vtn_handle_rounding_mode_in_execution_mode(struct vtn_builder *b, struct vtn_value *entry_point,
                                           const struct vtn_decoration *mode, void *data)
{
   vtn_assert(b->entry_point == entry_point);

   unsigned execution_mode = 0;

   switch(mode->exec_mode) {
   case SpvExecutionModeDenormPreserve:
      switch (mode->operands[0]) {
      case 16: execution_mode = FLOAT_CONTROLS_DENORM_PRESERVE_FP16; break;
      case 32: execution_mode = FLOAT_CONTROLS_DENORM_PRESERVE_FP32; break;
      case 64: execution_mode = FLOAT_CONTROLS_DENORM_PRESERVE_FP64; break;
      default: vtn_fail("Floating point type not supported");
      }
      break;
   case SpvExecutionModeDenormFlushToZero:
      switch (mode->operands[0]) {
      case 16: execution_mode = FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP16; break;
      case 32: execution_mode = FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP32; break;
      case 64: execution_mode = FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP64; break;
      default: vtn_fail("Floating point type not supported");
      }
       break;
   case SpvExecutionModeSignedZeroInfNanPreserve:
      switch (mode->operands[0]) {
      case 16: execution_mode = FLOAT_CONTROLS_SIGNED_ZERO_INF_NAN_PRESERVE_FP16; break;
      case 32: execution_mode = FLOAT_CONTROLS_SIGNED_ZERO_INF_NAN_PRESERVE_FP32; break;
      case 64: execution_mode = FLOAT_CONTROLS_SIGNED_ZERO_INF_NAN_PRESERVE_FP64; break;
      default: vtn_fail("Floating point type not supported");
      }
      break;
   case SpvExecutionModeRoundingModeRTE:
      switch (mode->operands[0]) {
      case 16: execution_mode = FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP16; break;
      case 32: execution_mode = FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP32; break;
      case 64: execution_mode = FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP64; break;
      default: vtn_fail("Floating point type not supported");
      }
      break;
   case SpvExecutionModeRoundingModeRTZ:
      switch (mode->operands[0]) {
      case 16: execution_mode = FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP16; break;
      case 32: execution_mode = FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP32; break;
      case 64: execution_mode = FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP64; break;
      default: vtn_fail("Floating point type not supported");
      }
      break;

   default:
      break;
   }

   b->shader->info.float_controls_execution_mode |= execution_mode;
}

static bool
vtn_handle_variable_or_type_instruction(struct vtn_builder *b, SpvOp opcode,
                                        const uint32_t *w, unsigned count)
{
   vtn_set_instruction_result_type(b, opcode, w, count);

   switch (opcode) {
   case SpvOpSource:
   case SpvOpSourceContinued:
   case SpvOpSourceExtension:
   case SpvOpExtension:
   case SpvOpCapability:
   case SpvOpExtInstImport:
   case SpvOpMemoryModel:
   case SpvOpEntryPoint:
   case SpvOpExecutionMode:
   case SpvOpString:
   case SpvOpName:
   case SpvOpMemberName:
   case SpvOpDecorationGroup:
   case SpvOpDecorate:
   case SpvOpDecorateId:
   case SpvOpMemberDecorate:
   case SpvOpGroupDecorate:
   case SpvOpGroupMemberDecorate:
   case SpvOpDecorateString:
   case SpvOpMemberDecorateString:
      vtn_fail("Invalid opcode types and variables section");
      break;

   case SpvOpTypeVoid:
   case SpvOpTypeBool:
   case SpvOpTypeInt:
   case SpvOpTypeFloat:
   case SpvOpTypeVector:
   case SpvOpTypeMatrix:
   case SpvOpTypeImage:
   case SpvOpTypeSampler:
   case SpvOpTypeSampledImage:
   case SpvOpTypeArray:
   case SpvOpTypeRuntimeArray:
   case SpvOpTypeStruct:
   case SpvOpTypeOpaque:
   case SpvOpTypePointer:
   case SpvOpTypeForwardPointer:
   case SpvOpTypeFunction:
   case SpvOpTypeEvent:
   case SpvOpTypeDeviceEvent:
   case SpvOpTypeReserveId:
   case SpvOpTypeQueue:
   case SpvOpTypePipe:
      vtn_handle_type(b, opcode, w, count);
      break;

   case SpvOpConstantTrue:
   case SpvOpConstantFalse:
   case SpvOpConstant:
   case SpvOpConstantComposite:
   case SpvOpConstantSampler:
   case SpvOpConstantNull:
   case SpvOpSpecConstantTrue:
   case SpvOpSpecConstantFalse:
   case SpvOpSpecConstant:
   case SpvOpSpecConstantComposite:
   case SpvOpSpecConstantOp:
      vtn_handle_constant(b, opcode, w, count);
      break;

   case SpvOpUndef:
   case SpvOpVariable:
      vtn_handle_variables(b, opcode, w, count);
      break;

   default:
      return false; /* End of preamble */
   }

   return true;
}

static struct vtn_ssa_value *
vtn_nir_select(struct vtn_builder *b, struct vtn_ssa_value *src0,
               struct vtn_ssa_value *src1, struct vtn_ssa_value *src2)
{
   struct vtn_ssa_value *dest = rzalloc(b, struct vtn_ssa_value);
   dest->type = src1->type;

   if (glsl_type_is_vector_or_scalar(src1->type)) {
      dest->def = nir_bcsel(&b->nb, src0->def, src1->def, src2->def);
   } else {
      unsigned elems = glsl_get_length(src1->type);

      dest->elems = ralloc_array(b, struct vtn_ssa_value *, elems);
      for (unsigned i = 0; i < elems; i++) {
         dest->elems[i] = vtn_nir_select(b, src0,
                                         src1->elems[i], src2->elems[i]);
      }
   }

   return dest;
}

static void
vtn_handle_select(struct vtn_builder *b, SpvOp opcode,
                  const uint32_t *w, unsigned count)
{
   /* Handle OpSelect up-front here because it needs to be able to handle
    * pointers and not just regular vectors and scalars.
    */
   struct vtn_value *res_val = vtn_untyped_value(b, w[2]);
   struct vtn_value *cond_val = vtn_untyped_value(b, w[3]);
   struct vtn_value *obj1_val = vtn_untyped_value(b, w[4]);
   struct vtn_value *obj2_val = vtn_untyped_value(b, w[5]);

   vtn_fail_if(obj1_val->type != res_val->type ||
               obj2_val->type != res_val->type,
               "Object types must match the result type in OpSelect");

   vtn_fail_if((cond_val->type->base_type != vtn_base_type_scalar &&
                cond_val->type->base_type != vtn_base_type_vector) ||
               !glsl_type_is_boolean(cond_val->type->type),
               "OpSelect must have either a vector of booleans or "
               "a boolean as Condition type");

   vtn_fail_if(cond_val->type->base_type == vtn_base_type_vector &&
               (res_val->type->base_type != vtn_base_type_vector ||
                res_val->type->length != cond_val->type->length),
               "When Condition type in OpSelect is a vector, the Result "
               "type must be a vector of the same length");

   switch (res_val->type->base_type) {
   case vtn_base_type_scalar:
   case vtn_base_type_vector:
   case vtn_base_type_matrix:
   case vtn_base_type_array:
   case vtn_base_type_struct:
      /* OK. */
      break;
   case vtn_base_type_pointer:
      /* We need to have actual storage for pointer types. */
      vtn_fail_if(res_val->type->type == NULL,
                  "Invalid pointer result type for OpSelect");
      break;
   default:
      vtn_fail("Result type of OpSelect must be a scalar, composite, or pointer");
   }

   struct vtn_type *res_type = vtn_value(b, w[1], vtn_value_type_type)->type;
   struct vtn_ssa_value *ssa = vtn_nir_select(b,
      vtn_ssa_value(b, w[3]), vtn_ssa_value(b, w[4]), vtn_ssa_value(b, w[5]));

   vtn_push_ssa(b, w[2], res_type, ssa);
}

static void
vtn_handle_ptr(struct vtn_builder *b, SpvOp opcode,
               const uint32_t *w, unsigned count)
{
      struct vtn_type *type1 = vtn_untyped_value(b, w[3])->type;
      struct vtn_type *type2 = vtn_untyped_value(b, w[4])->type;
      vtn_fail_if(type1->base_type != vtn_base_type_pointer ||
                  type2->base_type != vtn_base_type_pointer,
                  "%s operands must have pointer types",
                  spirv_op_to_string(opcode));
      vtn_fail_if(type1->storage_class != type2->storage_class,
                  "%s operands must have the same storage class",
                  spirv_op_to_string(opcode));

      struct vtn_type *vtn_type =
         vtn_value(b, w[1], vtn_value_type_type)->type;
      const struct glsl_type *type = vtn_type->type;

      nir_address_format addr_format = vtn_mode_to_address_format(
         b, vtn_storage_class_to_mode(b, type1->storage_class, NULL, NULL));

      nir_ssa_def *def;

      switch (opcode) {
      case SpvOpPtrDiff: {
         /* OpPtrDiff returns the difference in number of elements (not byte offset). */
         unsigned elem_size, elem_align;
         glsl_get_natural_size_align_bytes(type1->deref->type,
                                           &elem_size, &elem_align);

         def = nir_build_addr_isub(&b->nb,
                                   vtn_ssa_value(b, w[3])->def,
                                   vtn_ssa_value(b, w[4])->def,
                                   addr_format);
         def = nir_idiv(&b->nb, def, nir_imm_intN_t(&b->nb, elem_size, def->bit_size));
         def = nir_i2i(&b->nb, def, glsl_get_bit_size(type));
         break;
      }

      case SpvOpPtrEqual:
      case SpvOpPtrNotEqual: {
         def = nir_build_addr_ieq(&b->nb,
                                  vtn_ssa_value(b, w[3])->def,
                                  vtn_ssa_value(b, w[4])->def,
                                  addr_format);
         if (opcode == SpvOpPtrNotEqual)
            def = nir_inot(&b->nb, def);
         break;
      }

      default:
         unreachable("Invalid ptr operation");
      }

      struct vtn_ssa_value *ssa_value = vtn_create_ssa_value(b, type);
      ssa_value->def = def;
      vtn_push_ssa(b, w[2], vtn_type, ssa_value);
}

static bool
vtn_handle_body_instruction(struct vtn_builder *b, SpvOp opcode,
                            const uint32_t *w, unsigned count)
{
   switch (opcode) {
   case SpvOpLabel:
      break;

   case SpvOpLoopMerge:
   case SpvOpSelectionMerge:
      /* This is handled by cfg pre-pass and walk_blocks */
      break;

   case SpvOpUndef: {
      struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_undef);
      val->type = vtn_value(b, w[1], vtn_value_type_type)->type;
      break;
   }

   case SpvOpExtInst:
      vtn_handle_extension(b, opcode, w, count);
      break;

   case SpvOpVariable:
   case SpvOpLoad:
   case SpvOpStore:
   case SpvOpCopyMemory:
   case SpvOpCopyMemorySized:
   case SpvOpAccessChain:
   case SpvOpPtrAccessChain:
   case SpvOpInBoundsAccessChain:
   case SpvOpInBoundsPtrAccessChain:
   case SpvOpArrayLength:
   case SpvOpConvertPtrToU:
   case SpvOpConvertUToPtr:
      vtn_handle_variables(b, opcode, w, count);
      break;

   case SpvOpFunctionCall:
      vtn_handle_function_call(b, opcode, w, count);
      break;

   case SpvOpSampledImage:
   case SpvOpImage:
   case SpvOpImageSampleImplicitLod:
   case SpvOpImageSampleExplicitLod:
   case SpvOpImageSampleDrefImplicitLod:
   case SpvOpImageSampleDrefExplicitLod:
   case SpvOpImageSampleProjImplicitLod:
   case SpvOpImageSampleProjExplicitLod:
   case SpvOpImageSampleProjDrefImplicitLod:
   case SpvOpImageSampleProjDrefExplicitLod:
   case SpvOpImageFetch:
   case SpvOpImageGather:
   case SpvOpImageDrefGather:
   case SpvOpImageQuerySizeLod:
   case SpvOpImageQueryLod:
   case SpvOpImageQueryLevels:
   case SpvOpImageQuerySamples:
      vtn_handle_texture(b, opcode, w, count);
      break;

   case SpvOpImageRead:
   case SpvOpImageWrite:
   case SpvOpImageTexelPointer:
      vtn_handle_image(b, opcode, w, count);
      break;

   case SpvOpImageQuerySize: {
      struct vtn_pointer *image =
         vtn_value(b, w[3], vtn_value_type_pointer)->pointer;
      if (glsl_type_is_image(image->type->type)) {
         vtn_handle_image(b, opcode, w, count);
      } else {
         vtn_assert(glsl_type_is_sampler(image->type->type));
         vtn_handle_texture(b, opcode, w, count);
      }
      break;
   }

   case SpvOpAtomicLoad:
   case SpvOpAtomicExchange:
   case SpvOpAtomicCompareExchange:
   case SpvOpAtomicCompareExchangeWeak:
   case SpvOpAtomicIIncrement:
   case SpvOpAtomicIDecrement:
   case SpvOpAtomicIAdd:
   case SpvOpAtomicISub:
   case SpvOpAtomicSMin:
   case SpvOpAtomicUMin:
   case SpvOpAtomicSMax:
   case SpvOpAtomicUMax:
   case SpvOpAtomicAnd:
   case SpvOpAtomicOr:
   case SpvOpAtomicXor: {
      struct vtn_value *pointer = vtn_untyped_value(b, w[3]);
      if (pointer->value_type == vtn_value_type_image_pointer) {
         vtn_handle_image(b, opcode, w, count);
      } else {
         vtn_assert(pointer->value_type == vtn_value_type_pointer);
         vtn_handle_atomics(b, opcode, w, count);
      }
      break;
   }

   case SpvOpAtomicStore: {
      struct vtn_value *pointer = vtn_untyped_value(b, w[1]);
      if (pointer->value_type == vtn_value_type_image_pointer) {
         vtn_handle_image(b, opcode, w, count);
      } else {
         vtn_assert(pointer->value_type == vtn_value_type_pointer);
         vtn_handle_atomics(b, opcode, w, count);
      }
      break;
   }

   case SpvOpSelect:
      vtn_handle_select(b, opcode, w, count);
      break;

   case SpvOpSNegate:
   case SpvOpFNegate:
   case SpvOpNot:
   case SpvOpAny:
   case SpvOpAll:
   case SpvOpConvertFToU:
   case SpvOpConvertFToS:
   case SpvOpConvertSToF:
   case SpvOpConvertUToF:
   case SpvOpUConvert:
   case SpvOpSConvert:
   case SpvOpFConvert:
   case SpvOpQuantizeToF16:
   case SpvOpPtrCastToGeneric:
   case SpvOpGenericCastToPtr:
   case SpvOpIsNan:
   case SpvOpIsInf:
   case SpvOpIsFinite:
   case SpvOpIsNormal:
   case SpvOpSignBitSet:
   case SpvOpLessOrGreater:
   case SpvOpOrdered:
   case SpvOpUnordered:
   case SpvOpIAdd:
   case SpvOpFAdd:
   case SpvOpISub:
   case SpvOpFSub:
   case SpvOpIMul:
   case SpvOpFMul:
   case SpvOpUDiv:
   case SpvOpSDiv:
   case SpvOpFDiv:
   case SpvOpUMod:
   case SpvOpSRem:
   case SpvOpSMod:
   case SpvOpFRem:
   case SpvOpFMod:
   case SpvOpVectorTimesScalar:
   case SpvOpDot:
   case SpvOpIAddCarry:
   case SpvOpISubBorrow:
   case SpvOpUMulExtended:
   case SpvOpSMulExtended:
   case SpvOpShiftRightLogical:
   case SpvOpShiftRightArithmetic:
   case SpvOpShiftLeftLogical:
   case SpvOpLogicalEqual:
   case SpvOpLogicalNotEqual:
   case SpvOpLogicalOr:
   case SpvOpLogicalAnd:
   case SpvOpLogicalNot:
   case SpvOpBitwiseOr:
   case SpvOpBitwiseXor:
   case SpvOpBitwiseAnd:
   case SpvOpIEqual:
   case SpvOpFOrdEqual:
   case SpvOpFUnordEqual:
   case SpvOpINotEqual:
   case SpvOpFOrdNotEqual:
   case SpvOpFUnordNotEqual:
   case SpvOpULessThan:
   case SpvOpSLessThan:
   case SpvOpFOrdLessThan:
   case SpvOpFUnordLessThan:
   case SpvOpUGreaterThan:
   case SpvOpSGreaterThan:
   case SpvOpFOrdGreaterThan:
   case SpvOpFUnordGreaterThan:
   case SpvOpULessThanEqual:
   case SpvOpSLessThanEqual:
   case SpvOpFOrdLessThanEqual:
   case SpvOpFUnordLessThanEqual:
   case SpvOpUGreaterThanEqual:
   case SpvOpSGreaterThanEqual:
   case SpvOpFOrdGreaterThanEqual:
   case SpvOpFUnordGreaterThanEqual:
   case SpvOpDPdx:
   case SpvOpDPdy:
   case SpvOpFwidth:
   case SpvOpDPdxFine:
   case SpvOpDPdyFine:
   case SpvOpFwidthFine:
   case SpvOpDPdxCoarse:
   case SpvOpDPdyCoarse:
   case SpvOpFwidthCoarse:
   case SpvOpBitFieldInsert:
   case SpvOpBitFieldSExtract:
   case SpvOpBitFieldUExtract:
   case SpvOpBitReverse:
   case SpvOpBitCount:
   case SpvOpTranspose:
   case SpvOpOuterProduct:
   case SpvOpMatrixTimesScalar:
   case SpvOpVectorTimesMatrix:
   case SpvOpMatrixTimesVector:
   case SpvOpMatrixTimesMatrix:
      vtn_handle_alu(b, opcode, w, count);
      break;

   case SpvOpBitcast:
      vtn_handle_bitcast(b, w, count);
      break;

   case SpvOpVectorExtractDynamic:
   case SpvOpVectorInsertDynamic:
   case SpvOpVectorShuffle:
   case SpvOpCompositeConstruct:
   case SpvOpCompositeExtract:
   case SpvOpCompositeInsert:
   case SpvOpCopyLogical:
   case SpvOpCopyObject:
      vtn_handle_composite(b, opcode, w, count);
      break;

   case SpvOpEmitVertex:
   case SpvOpEndPrimitive:
   case SpvOpEmitStreamVertex:
   case SpvOpEndStreamPrimitive:
   case SpvOpControlBarrier:
   case SpvOpMemoryBarrier:
      vtn_handle_barrier(b, opcode, w, count);
      break;

   case SpvOpGroupNonUniformElect:
   case SpvOpGroupNonUniformAll:
   case SpvOpGroupNonUniformAny:
   case SpvOpGroupNonUniformAllEqual:
   case SpvOpGroupNonUniformBroadcast:
   case SpvOpGroupNonUniformBroadcastFirst:
   case SpvOpGroupNonUniformBallot:
   case SpvOpGroupNonUniformInverseBallot:
   case SpvOpGroupNonUniformBallotBitExtract:
   case SpvOpGroupNonUniformBallotBitCount:
   case SpvOpGroupNonUniformBallotFindLSB:
   case SpvOpGroupNonUniformBallotFindMSB:
   case SpvOpGroupNonUniformShuffle:
   case SpvOpGroupNonUniformShuffleXor:
   case SpvOpGroupNonUniformShuffleUp:
   case SpvOpGroupNonUniformShuffleDown:
   case SpvOpGroupNonUniformIAdd:
   case SpvOpGroupNonUniformFAdd:
   case SpvOpGroupNonUniformIMul:
   case SpvOpGroupNonUniformFMul:
   case SpvOpGroupNonUniformSMin:
   case SpvOpGroupNonUniformUMin:
   case SpvOpGroupNonUniformFMin:
   case SpvOpGroupNonUniformSMax:
   case SpvOpGroupNonUniformUMax:
   case SpvOpGroupNonUniformFMax:
   case SpvOpGroupNonUniformBitwiseAnd:
   case SpvOpGroupNonUniformBitwiseOr:
   case SpvOpGroupNonUniformBitwiseXor:
   case SpvOpGroupNonUniformLogicalAnd:
   case SpvOpGroupNonUniformLogicalOr:
   case SpvOpGroupNonUniformLogicalXor:
   case SpvOpGroupNonUniformQuadBroadcast:
   case SpvOpGroupNonUniformQuadSwap:
   case SpvOpGroupAll:
   case SpvOpGroupAny:
   case SpvOpGroupBroadcast:
   case SpvOpGroupIAdd:
   case SpvOpGroupFAdd:
   case SpvOpGroupFMin:
   case SpvOpGroupUMin:
   case SpvOpGroupSMin:
   case SpvOpGroupFMax:
   case SpvOpGroupUMax:
   case SpvOpGroupSMax:
   case SpvOpSubgroupBallotKHR:
   case SpvOpSubgroupFirstInvocationKHR:
   case SpvOpSubgroupReadInvocationKHR:
   case SpvOpSubgroupAllKHR:
   case SpvOpSubgroupAnyKHR:
   case SpvOpSubgroupAllEqualKHR:
   case SpvOpGroupIAddNonUniformAMD:
   case SpvOpGroupFAddNonUniformAMD:
   case SpvOpGroupFMinNonUniformAMD:
   case SpvOpGroupUMinNonUniformAMD:
   case SpvOpGroupSMinNonUniformAMD:
   case SpvOpGroupFMaxNonUniformAMD:
   case SpvOpGroupUMaxNonUniformAMD:
   case SpvOpGroupSMaxNonUniformAMD:
      vtn_handle_subgroup(b, opcode, w, count);
      break;

   case SpvOpPtrDiff:
   case SpvOpPtrEqual:
   case SpvOpPtrNotEqual:
      vtn_handle_ptr(b, opcode, w, count);
      break;

   case SpvOpBeginInvocationInterlockEXT:
      vtn_emit_barrier(b, nir_intrinsic_begin_invocation_interlock);
      break;

   case SpvOpEndInvocationInterlockEXT:
      vtn_emit_barrier(b, nir_intrinsic_end_invocation_interlock);
      break;

   case SpvOpDemoteToHelperInvocationEXT: {
      nir_intrinsic_instr *intrin =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_demote);
      nir_builder_instr_insert(&b->nb, &intrin->instr);
      break;
   }

   case SpvOpIsHelperInvocationEXT: {
      nir_intrinsic_instr *intrin =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_is_helper_invocation);
      nir_ssa_dest_init(&intrin->instr, &intrin->dest, 1, 1, NULL);
      nir_builder_instr_insert(&b->nb, &intrin->instr);

      struct vtn_type *res_type =
         vtn_value(b, w[1], vtn_value_type_type)->type;
      struct vtn_ssa_value *val = vtn_create_ssa_value(b, res_type->type);
      val->def = &intrin->dest.ssa;

      vtn_push_ssa(b, w[2], res_type, val);
      break;
   }

   case SpvOpReadClockKHR: {
      assert(vtn_constant_uint(b, w[3]) == SpvScopeSubgroup);

      /* Operation supports two result types: uvec2 and uint64_t.  The NIR
       * intrinsic gives uvec2, so pack the result for the other case.
       */
      nir_intrinsic_instr *intrin =
         nir_intrinsic_instr_create(b->nb.shader, nir_intrinsic_shader_clock);
      nir_ssa_dest_init(&intrin->instr, &intrin->dest, 2, 32, NULL);
      nir_builder_instr_insert(&b->nb, &intrin->instr);

      struct vtn_type *type = vtn_value(b, w[1], vtn_value_type_type)->type;
      const struct glsl_type *dest_type = type->type;
      nir_ssa_def *result;

      if (glsl_type_is_vector(dest_type)) {
         assert(dest_type == glsl_vector_type(GLSL_TYPE_UINT, 2));
         result = &intrin->dest.ssa;
      } else {
         assert(glsl_type_is_scalar(dest_type));
         assert(glsl_get_base_type(dest_type) == GLSL_TYPE_UINT64);
         result = nir_pack_64_2x32(&b->nb, &intrin->dest.ssa);
      }

      struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
      val->type = type;
      val->ssa = vtn_create_ssa_value(b, dest_type);
      val->ssa->def = result;
      break;
   }

   default:
      vtn_fail_with_opcode("Unhandled opcode", opcode);
   }

   return true;
}

struct vtn_builder*
vtn_create_builder(const uint32_t *words, size_t word_count,
                   gl_shader_stage stage, const char *entry_point_name,
                   const struct spirv_to_nir_options *options)
{
   /* Initialize the vtn_builder object */
   struct vtn_builder *b = rzalloc(NULL, struct vtn_builder);
   struct spirv_to_nir_options *dup_options =
      ralloc(b, struct spirv_to_nir_options);
   *dup_options = *options;

   b->spirv = words;
   b->spirv_word_count = word_count;
   b->file = NULL;
   b->line = -1;
   b->col = -1;
   exec_list_make_empty(&b->functions);
   b->entry_point_stage = stage;
   b->entry_point_name = entry_point_name;
   b->options = dup_options;

   /*
    * Handle the SPIR-V header (first 5 dwords).
    * Can't use vtx_assert() as the setjmp(3) target isn't initialized yet.
    */
   if (word_count <= 5)
      goto fail;

   if (words[0] != SpvMagicNumber) {
      vtn_err("words[0] was 0x%x, want 0x%x", words[0], SpvMagicNumber);
      goto fail;
   }
   if (words[1] < 0x10000) {
      vtn_err("words[1] was 0x%x, want >= 0x10000", words[1]);
      goto fail;
   }

   uint16_t generator_id = words[2] >> 16;
   uint16_t generator_version = words[2];

   /* The first GLSLang version bump actually 1.5 years after #179 was fixed
    * but this should at least let us shut the workaround off for modern
    * versions of GLSLang.
    */
   b->wa_glslang_179 = (generator_id == 8 && generator_version == 1);

   /* words[2] == generator magic */
   unsigned value_id_bound = words[3];
   if (words[4] != 0) {
      vtn_err("words[4] was %u, want 0", words[4]);
      goto fail;
   }

   b->value_id_bound = value_id_bound;
   b->values = rzalloc_array(b, struct vtn_value, value_id_bound);

   return b;
 fail:
   ralloc_free(b);
   return NULL;
}

static nir_function *
vtn_emit_kernel_entry_point_wrapper(struct vtn_builder *b,
                                    nir_function *entry_point)
{
   vtn_assert(entry_point == b->entry_point->func->impl->function);
   vtn_fail_if(!entry_point->name, "entry points are required to have a name");
   const char *func_name =
      ralloc_asprintf(b->shader, "__wrapped_%s", entry_point->name);

   /* we shouldn't have any inputs yet */
   vtn_assert(!entry_point->shader->num_inputs);
   vtn_assert(b->shader->info.stage == MESA_SHADER_KERNEL);

   nir_function *main_entry_point = nir_function_create(b->shader, func_name);
   main_entry_point->impl = nir_function_impl_create(main_entry_point);
   nir_builder_init(&b->nb, main_entry_point->impl);
   b->nb.cursor = nir_after_cf_list(&main_entry_point->impl->body);
   b->func_param_idx = 0;

   nir_call_instr *call = nir_call_instr_create(b->nb.shader, entry_point);

   for (unsigned i = 0; i < entry_point->num_params; ++i) {
      struct vtn_type *param_type = b->entry_point->func->type->params[i];

      /* consider all pointers to function memory to be parameters passed
       * by value
       */
      bool is_by_val = param_type->base_type == vtn_base_type_pointer &&
         param_type->storage_class == SpvStorageClassFunction;

      /* input variable */
      nir_variable *in_var = rzalloc(b->nb.shader, nir_variable);
      in_var->data.mode = nir_var_shader_in;
      in_var->data.read_only = true;
      in_var->data.location = i;

      if (is_by_val)
         in_var->type = param_type->deref->type;
      else
         in_var->type = param_type->type;

      nir_shader_add_variable(b->nb.shader, in_var);
      b->nb.shader->num_inputs++;

      /* we have to copy the entire variable into function memory */
      if (is_by_val) {
         nir_variable *copy_var =
            nir_local_variable_create(main_entry_point->impl, in_var->type,
                                      "copy_in");
         nir_copy_var(&b->nb, copy_var, in_var);
         call->params[i] =
            nir_src_for_ssa(&nir_build_deref_var(&b->nb, copy_var)->dest.ssa);
      } else {
         call->params[i] = nir_src_for_ssa(nir_load_var(&b->nb, in_var));
      }
   }

   nir_builder_instr_insert(&b->nb, &call->instr);

   return main_entry_point;
}

nir_shader *
spirv_to_nir(const uint32_t *words, size_t word_count,
             struct nir_spirv_specialization *spec, unsigned num_spec,
             gl_shader_stage stage, const char *entry_point_name,
             const struct spirv_to_nir_options *options,
             const nir_shader_compiler_options *nir_options)

{
   const uint32_t *word_end = words + word_count;

   struct vtn_builder *b = vtn_create_builder(words, word_count,
                                              stage, entry_point_name,
                                              options);

   if (b == NULL)
      return NULL;

   /* See also _vtn_fail() */
   if (setjmp(b->fail_jump)) {
      ralloc_free(b);
      return NULL;
   }

   /* Skip the SPIR-V header, handled at vtn_create_builder */
   words+= 5;

   b->shader = nir_shader_create(b, stage, nir_options, NULL);

   /* Handle all the preamble instructions */
   words = vtn_foreach_instruction(b, words, word_end,
                                   vtn_handle_preamble_instruction);

   if (b->entry_point == NULL) {
      vtn_fail("Entry point not found");
      ralloc_free(b);
      return NULL;
   }

   /* Set shader info defaults */
   if (stage == MESA_SHADER_GEOMETRY)
      b->shader->info.gs.invocations = 1;

   /* Parse rounding mode execution modes. This has to happen earlier than
    * other changes in the execution modes since they can affect, for example,
    * the result of the floating point constants.
    */
   vtn_foreach_execution_mode(b, b->entry_point,
                              vtn_handle_rounding_mode_in_execution_mode, NULL);

   b->specializations = spec;
   b->num_specializations = num_spec;

   /* Handle all variable, type, and constant instructions */
   words = vtn_foreach_instruction(b, words, word_end,
                                   vtn_handle_variable_or_type_instruction);

   /* Parse execution modes */
   vtn_foreach_execution_mode(b, b->entry_point,
                              vtn_handle_execution_mode, NULL);

   if (b->workgroup_size_builtin) {
      vtn_assert(b->workgroup_size_builtin->type->type ==
                 glsl_vector_type(GLSL_TYPE_UINT, 3));

      nir_const_value *const_size =
         b->workgroup_size_builtin->constant->values;

      b->shader->info.cs.local_size[0] = const_size[0].u32;
      b->shader->info.cs.local_size[1] = const_size[1].u32;
      b->shader->info.cs.local_size[2] = const_size[2].u32;
   }

   /* Set types on all vtn_values */
   vtn_foreach_instruction(b, words, word_end, vtn_set_instruction_result_type);

   vtn_build_cfg(b, words, word_end);

   assert(b->entry_point->value_type == vtn_value_type_function);
   b->entry_point->func->referenced = true;

   bool progress;
   do {
      progress = false;
      foreach_list_typed(struct vtn_function, func, node, &b->functions) {
         if (func->referenced && !func->emitted) {
            b->const_table = _mesa_pointer_hash_table_create(b);

            vtn_function_emit(b, func, vtn_handle_body_instruction);
            progress = true;
         }
      }
   } while (progress);

   vtn_assert(b->entry_point->value_type == vtn_value_type_function);
   nir_function *entry_point = b->entry_point->func->impl->function;
   vtn_assert(entry_point);

   /* post process entry_points with input params */
   if (entry_point->num_params && b->shader->info.stage == MESA_SHADER_KERNEL)
      entry_point = vtn_emit_kernel_entry_point_wrapper(b, entry_point);

   entry_point->is_entrypoint = true;

   /* When multiple shader stages exist in the same SPIR-V module, we
    * generate input and output variables for every stage, in the same
    * NIR program.  These dead variables can be invalid NIR.  For example,
    * TCS outputs must be per-vertex arrays (or decorated 'patch'), while
    * VS output variables wouldn't be.
    *
    * To ensure we have valid NIR, we eliminate any dead inputs and outputs
    * right away.  In order to do so, we must lower any constant initializers
    * on outputs so nir_remove_dead_variables sees that they're written to.
    */
   nir_lower_constant_initializers(b->shader, nir_var_shader_out);
   nir_remove_dead_variables(b->shader,
                             nir_var_shader_in | nir_var_shader_out);

   /* We sometimes generate bogus derefs that, while never used, give the
    * validator a bit of heartburn.  Run dead code to get rid of them.
    */
   nir_opt_dce(b->shader);

   /* Unparent the shader from the vtn_builder before we delete the builder */
   ralloc_steal(NULL, b->shader);

   nir_shader *shader = b->shader;
   ralloc_free(b);

   return shader;
}
