/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "nir_to_spirv.h"
#include "spirv_builder.h"

#include "nir.h"
#include "pipe/p_state.h"
#include "util/u_memory.h"
#include "util/hash_table.h"

#define SLOT_UNSET ((unsigned char) -1)

struct ntv_context {
   void *mem_ctx;

   struct spirv_builder builder;

   SpvId GLSL_std_450;

   gl_shader_stage stage;
   const struct zink_so_info *so_info;

   SpvId ubos[128];
   size_t num_ubos;
   SpvId image_types[PIPE_MAX_SAMPLERS];
   SpvId samplers[PIPE_MAX_SAMPLERS];
   unsigned samplers_used : PIPE_MAX_SAMPLERS;
   SpvId entry_ifaces[PIPE_MAX_SHADER_INPUTS * 4 + PIPE_MAX_SHADER_OUTPUTS * 4];
   size_t num_entry_ifaces;

   SpvId *defs;
   size_t num_defs;

   SpvId *regs;
   size_t num_regs;

   struct hash_table *vars; /* nir_variable -> SpvId */
   struct hash_table *so_outputs; /* pipe_stream_output -> SpvId */
   unsigned outputs[VARYING_SLOT_MAX];
   const struct glsl_type *so_output_gl_types[VARYING_SLOT_MAX];
   SpvId so_output_types[VARYING_SLOT_MAX];

   const SpvId *block_ids;
   size_t num_blocks;
   bool block_started;
   SpvId loop_break, loop_cont;

   unsigned char *shader_slot_map;
   unsigned char shader_slots_reserved;

   SpvId front_face_var, instance_id_var, vertex_id_var,
         primitive_id_var, invocation_id_var, // geometry
         sample_mask_type, sample_id_var, sample_pos_var,
         tess_patch_vertices_in, tess_coord_var, // tess
         push_const_var;
};

static SpvId
get_fvec_constant(struct ntv_context *ctx, unsigned bit_size,
                  unsigned num_components, double value);

static SpvId
get_uvec_constant(struct ntv_context *ctx, unsigned bit_size,
                  unsigned num_components, uint64_t value);

static SpvId
get_ivec_constant(struct ntv_context *ctx, unsigned bit_size,
                  unsigned num_components, int64_t value);

static SpvId
emit_unop(struct ntv_context *ctx, SpvOp op, SpvId type, SpvId src);

static SpvId
emit_binop(struct ntv_context *ctx, SpvOp op, SpvId type,
           SpvId src0, SpvId src1);

static SpvId
emit_triop(struct ntv_context *ctx, SpvOp op, SpvId type,
           SpvId src0, SpvId src1, SpvId src2);

static SpvId
get_bvec_type(struct ntv_context *ctx, int num_components)
{
   SpvId bool_type = spirv_builder_type_bool(&ctx->builder);
   if (num_components > 1)
      return spirv_builder_type_vector(&ctx->builder, bool_type,
                                       num_components);

   assert(num_components == 1);
   return bool_type;
}

static SpvId
block_label(struct ntv_context *ctx, nir_block *block)
{
   assert(block->index < ctx->num_blocks);
   return ctx->block_ids[block->index];
}

static SpvId
emit_float_const(struct ntv_context *ctx, int bit_size, double value)
{
   assert(bit_size == 32 || bit_size == 64);
   return spirv_builder_const_float(&ctx->builder, bit_size, value);
}

static SpvId
emit_uint_const(struct ntv_context *ctx, int bit_size, uint64_t value)
{
   assert(bit_size == 32 || bit_size == 64);
   return spirv_builder_const_uint(&ctx->builder, bit_size, value);
}

static SpvId
emit_int_const(struct ntv_context *ctx, int bit_size, int64_t value)
{
   assert(bit_size == 32 || bit_size == 64);
   return spirv_builder_const_int(&ctx->builder, bit_size, value);
}

static SpvId
get_fvec_type(struct ntv_context *ctx, unsigned bit_size, unsigned num_components)
{
   assert(bit_size == 32 || bit_size == 64);

   SpvId float_type = spirv_builder_type_float(&ctx->builder, bit_size);
   if (num_components > 1)
      return spirv_builder_type_vector(&ctx->builder, float_type,
                                       num_components);

   assert(num_components == 1);
   return float_type;
}

static SpvId
get_ivec_type(struct ntv_context *ctx, unsigned bit_size, unsigned num_components)
{
   assert(bit_size == 32 || bit_size == 64);

   SpvId int_type = spirv_builder_type_int(&ctx->builder, bit_size);
   if (num_components > 1)
      return spirv_builder_type_vector(&ctx->builder, int_type,
                                       num_components);

   assert(num_components == 1);
   return int_type;
}

static SpvId
get_uvec_type(struct ntv_context *ctx, unsigned bit_size, unsigned num_components)
{
   assert(bit_size == 32 || bit_size == 64);

   SpvId uint_type = spirv_builder_type_uint(&ctx->builder, bit_size);
   if (num_components > 1)
      return spirv_builder_type_vector(&ctx->builder, uint_type,
                                       num_components);

   assert(num_components == 1);
   return uint_type;
}

static SpvStorageClass
get_storage_class(struct nir_variable *var)
{
   switch (var->data.mode) {
   case nir_var_mem_push_const:
      return SpvStorageClassPushConstant;
   case nir_var_shader_in:
      return SpvStorageClassInput;
   case nir_var_shader_out:
      return SpvStorageClassOutput;
   default:
      unreachable("Unsupported nir_variable_mode");
   }
   return 0;
}

static SpvId
get_dest_uvec_type(struct ntv_context *ctx, nir_dest *dest)
{
   unsigned bit_size = nir_dest_bit_size(*dest);
   return get_uvec_type(ctx, bit_size, nir_dest_num_components(*dest));
}

static SpvId
get_glsl_basetype(struct ntv_context *ctx, enum glsl_base_type type)
{
   switch (type) {
   case GLSL_TYPE_BOOL:
      return spirv_builder_type_bool(&ctx->builder);

   case GLSL_TYPE_FLOAT:
      return spirv_builder_type_float(&ctx->builder, 32);

   case GLSL_TYPE_INT:
      return spirv_builder_type_int(&ctx->builder, 32);

   case GLSL_TYPE_UINT:
      return spirv_builder_type_uint(&ctx->builder, 32);

   case GLSL_TYPE_DOUBLE:
      return spirv_builder_type_float(&ctx->builder, 64);

   case GLSL_TYPE_INT64:
      return spirv_builder_type_int(&ctx->builder, 64);

   case GLSL_TYPE_UINT64:
      return spirv_builder_type_uint(&ctx->builder, 64);
   /* TODO: handle more types */

   default:
      unreachable("unknown GLSL type");
   }
}

static SpvId
get_glsl_type(struct ntv_context *ctx, const struct glsl_type *type)
{
   assert(type);
   if (glsl_type_is_scalar(type))
      return get_glsl_basetype(ctx, glsl_get_base_type(type));

   if (glsl_type_is_vector(type))
      return spirv_builder_type_vector(&ctx->builder,
         get_glsl_basetype(ctx, glsl_get_base_type(type)),
         glsl_get_vector_elements(type));

   if (glsl_type_is_array(type)) {
      SpvId ret = spirv_builder_type_array(&ctx->builder,
                                           get_glsl_type(ctx, glsl_get_array_element(type)),
                                           emit_uint_const(ctx, 32, glsl_get_length(type)));
      uint32_t stride = glsl_get_explicit_stride(type);
      if (!stride && glsl_type_is_scalar(glsl_get_array_element(type))) {
         stride = MAX2(glsl_get_bit_size(glsl_get_array_element(type)) / 8, 1);
      }
      if (stride)
         spirv_builder_emit_array_stride(&ctx->builder, ret, stride);
      return ret;
   }
   if (glsl_type_is_struct_or_ifc(type)) {
      SpvId types[glsl_get_length(type)];
      for (unsigned i = 0; i < glsl_get_length(type); i++)
         types[i] = get_glsl_type(ctx, glsl_get_struct_field(type, i));
      SpvId ret = spirv_builder_type_struct(&ctx->builder,
                                           types,
                                           glsl_get_length(type));
      for (unsigned i = 0; i < glsl_get_length(type); i++)
         spirv_builder_emit_member_offset(&ctx->builder, ret, i, glsl_get_struct_field_offset(type, i));
      return ret;
   }

   if (glsl_type_is_matrix(type))
      return spirv_builder_type_matrix(&ctx->builder,
                                       spirv_builder_type_vector(&ctx->builder,
                                                                 get_glsl_basetype(ctx, glsl_get_base_type(type)),
                                                                 glsl_get_vector_elements(type)),
                                       glsl_get_matrix_columns(type));

   unreachable("we shouldn't get here, I think...");
}

static inline unsigned char
reserve_slot(struct ntv_context *ctx)
{
   /* TODO: this should actually be clamped to the limits value as in the table
    * in 14.1.4 of the vulkan spec, though there's not really any recourse
    * other than aborting if we do hit it...
    */
   assert(ctx->shader_slots_reserved < MAX_VARYING);
   return ctx->shader_slots_reserved++;
}

static inline unsigned
handle_slot(struct ntv_context *ctx, unsigned slot)
{
   if (ctx->shader_slot_map[slot] == SLOT_UNSET)
      ctx->shader_slot_map[slot] = reserve_slot(ctx);
   slot = ctx->shader_slot_map[slot];
   assert(slot < MAX_VARYING);
   return slot;
}

#define HANDLE_EMIT_BUILTIN(SLOT, BUILTIN) \
      case VARYING_SLOT_##SLOT: \
         spirv_builder_emit_builtin(&ctx->builder, var_id, SpvBuiltIn##BUILTIN); \
         break


static inline unsigned
handle_handle_slot(struct ntv_context *ctx, struct nir_variable *var, bool output)
{
   if (var->data.patch) {
      assert(var->data.location >= VARYING_SLOT_PATCH0);
      return var->data.location - VARYING_SLOT_PATCH0;
   } else if ((output && ctx->stage == MESA_SHADER_TESS_CTRL) ||
              (!output && ctx->stage == MESA_SHADER_TESS_EVAL)) {
      assert(var->data.location >= VARYING_SLOT_VAR0);
      return var->data.location - VARYING_SLOT_VAR0;
   }
   return handle_slot(ctx, var->data.location);
}

static SpvId
input_var_init(struct ntv_context *ctx, struct nir_variable *var)
{
   SpvId var_type = get_glsl_type(ctx, var->type);
   SpvStorageClass sc = get_storage_class(var);
   if (sc == SpvStorageClassPushConstant)
      spirv_builder_emit_decoration(&ctx->builder, var_type, SpvDecorationBlock);
   SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                   sc, var_type);
   SpvId var_id = spirv_builder_emit_var(&ctx->builder, pointer_type, sc);

   if (var->name)
      spirv_builder_emit_name(&ctx->builder, var_id, var->name);
   if (var->data.mode == nir_var_mem_push_const)
      ctx->push_const_var = var_id;
   return var_id;
}

static void
emit_input(struct ntv_context *ctx, struct nir_variable *var)
{
   SpvId var_id = input_var_init(ctx, var);
   unsigned slot = var->data.location;
   if (ctx->stage == MESA_SHADER_VERTEX)
      spirv_builder_emit_location(&ctx->builder, var_id,
                                  var->data.driver_location);
   else if (ctx->stage == MESA_SHADER_FRAGMENT) {
      switch (slot) {
      HANDLE_EMIT_BUILTIN(POS, FragCoord);
      HANDLE_EMIT_BUILTIN(PNTC, PointCoord);
      HANDLE_EMIT_BUILTIN(LAYER, Layer);
      HANDLE_EMIT_BUILTIN(PRIMITIVE_ID, PrimitiveId);
      HANDLE_EMIT_BUILTIN(CLIP_DIST0, ClipDistance);
      HANDLE_EMIT_BUILTIN(CULL_DIST0, CullDistance);
      HANDLE_EMIT_BUILTIN(VIEWPORT, ViewportIndex);
      HANDLE_EMIT_BUILTIN(FACE, FrontFacing);

      default:
         slot = handle_slot(ctx, slot);
         spirv_builder_emit_location(&ctx->builder, var_id, slot);
      }
      if (var->data.centroid)
         spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationCentroid);
      else if (var->data.sample)
         spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationSample);
   } else if (ctx->stage < MESA_SHADER_FRAGMENT) {
      switch (slot) {
      HANDLE_EMIT_BUILTIN(POS, Position);
      HANDLE_EMIT_BUILTIN(PSIZ, PointSize);
      HANDLE_EMIT_BUILTIN(LAYER, Layer);
      HANDLE_EMIT_BUILTIN(PRIMITIVE_ID, PrimitiveId);
      HANDLE_EMIT_BUILTIN(CULL_DIST0, CullDistance);
      HANDLE_EMIT_BUILTIN(VIEWPORT, ViewportIndex);
      HANDLE_EMIT_BUILTIN(TESS_LEVEL_OUTER, TessLevelOuter);
      HANDLE_EMIT_BUILTIN(TESS_LEVEL_INNER, TessLevelInner);

      case VARYING_SLOT_CLIP_DIST0:
         assert(glsl_type_is_array(var->type));
         spirv_builder_emit_builtin(&ctx->builder, var_id, SpvBuiltInClipDistance);
         break;

      default:
         slot = handle_handle_slot(ctx, var, false);
         spirv_builder_emit_location(&ctx->builder, var_id, slot);
      }
   }

   if (var->data.location_frac)
      spirv_builder_emit_component(&ctx->builder, var_id,
                                   var->data.location_frac);

   if (var->data.patch)
      spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationPatch);

   if (var->data.interpolation == INTERP_MODE_FLAT)
      spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationFlat);

   _mesa_hash_table_insert(ctx->vars, var, (void *)(intptr_t)var_id);

   assert(ctx->num_entry_ifaces < ARRAY_SIZE(ctx->entry_ifaces));
   ctx->entry_ifaces[ctx->num_entry_ifaces++] = var_id;
}

static void
emit_output(struct ntv_context *ctx, struct nir_variable *var)
{
   SpvId var_type = get_glsl_type(ctx, var->type);

   /* SampleMask is always an array in spirv */
   if (ctx->stage == MESA_SHADER_FRAGMENT && var->data.location == FRAG_RESULT_SAMPLE_MASK)
      ctx->sample_mask_type = var_type = spirv_builder_type_array(&ctx->builder, var_type, emit_uint_const(ctx, 32, 1));
   SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                   SpvStorageClassOutput,
                                                   var_type);
   SpvId var_id = spirv_builder_emit_var(&ctx->builder, pointer_type,
                                         SpvStorageClassOutput);
   if (var->name)
      spirv_builder_emit_name(&ctx->builder, var_id, var->name);

   unsigned slot = var->data.location;
   if (ctx->stage != MESA_SHADER_FRAGMENT) {
      switch (slot) {
      HANDLE_EMIT_BUILTIN(POS, Position);
      HANDLE_EMIT_BUILTIN(PSIZ, PointSize);
      HANDLE_EMIT_BUILTIN(LAYER, Layer);
      HANDLE_EMIT_BUILTIN(PRIMITIVE_ID, PrimitiveId);
      HANDLE_EMIT_BUILTIN(CULL_DIST0, CullDistance);
      HANDLE_EMIT_BUILTIN(VIEWPORT, ViewportIndex);
      HANDLE_EMIT_BUILTIN(TESS_LEVEL_OUTER, TessLevelOuter);
      HANDLE_EMIT_BUILTIN(TESS_LEVEL_INNER, TessLevelInner);

      case VARYING_SLOT_CLIP_DIST0:
         assert(glsl_type_is_array(var->type));
         spirv_builder_emit_builtin(&ctx->builder, var_id, SpvBuiltInClipDistance);
         /* this can be as large as 2x vec4, which requires 2 slots */
         ctx->outputs[VARYING_SLOT_CLIP_DIST1] = var_id;
         ctx->so_output_gl_types[VARYING_SLOT_CLIP_DIST1] = var->type;
         ctx->so_output_types[VARYING_SLOT_CLIP_DIST1] = var_type;
         break;

      default:
         slot = handle_handle_slot(ctx, var, true);
         spirv_builder_emit_location(&ctx->builder, var_id, slot);
      }
      /* tcs can't do xfb */
      if (ctx->stage != MESA_SHADER_TESS_CTRL) {
         ctx->outputs[var->data.location] = var_id;
         ctx->so_output_gl_types[var->data.location] = var->type;
         ctx->so_output_types[var->data.location] = var_type;
      }
   } else {
      if (var->data.location >= FRAG_RESULT_DATA0) {
         spirv_builder_emit_location(&ctx->builder, var_id,
                                     var->data.location - FRAG_RESULT_DATA0);
         spirv_builder_emit_index(&ctx->builder, var_id, var->data.index);
      } else {
         switch (var->data.location) {
         case FRAG_RESULT_COLOR:
            unreachable("gl_FragColor should be lowered by now");

         case FRAG_RESULT_DEPTH:
            spirv_builder_emit_builtin(&ctx->builder, var_id, SpvBuiltInFragDepth);
            break;

         case FRAG_RESULT_SAMPLE_MASK:
            spirv_builder_emit_builtin(&ctx->builder, var_id, SpvBuiltInSampleMask);
            break;

         default:
            slot = handle_slot(ctx, slot);
            spirv_builder_emit_location(&ctx->builder, var_id, slot);
            spirv_builder_emit_index(&ctx->builder, var_id, var->data.index);
         }
      }
      if (var->data.sample)
         spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationSample);
   }

   if (var->data.location_frac)
      spirv_builder_emit_component(&ctx->builder, var_id,
                                   var->data.location_frac);

   switch (var->data.interpolation) {
   case INTERP_MODE_NONE:
   case INTERP_MODE_SMOOTH: /* XXX spirv doesn't seem to have anything for this */
      break;
   case INTERP_MODE_FLAT:
      spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationFlat);
      break;
   case INTERP_MODE_EXPLICIT:
      spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationExplicitInterpAMD);
      break;
   case INTERP_MODE_NOPERSPECTIVE:
      spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationNoPerspective);
      break;
   default:
      unreachable("unknown interpolation value");
   }

   if (var->data.patch)
      spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationPatch);

   _mesa_hash_table_insert(ctx->vars, var, (void *)(intptr_t)var_id);

   assert(ctx->num_entry_ifaces < ARRAY_SIZE(ctx->entry_ifaces));
   ctx->entry_ifaces[ctx->num_entry_ifaces++] = var_id;
}

static SpvDim
type_to_dim(enum glsl_sampler_dim gdim, bool *is_ms)
{
   *is_ms = false;
   switch (gdim) {
   case GLSL_SAMPLER_DIM_1D:
      return SpvDim1D;
   case GLSL_SAMPLER_DIM_2D:
      return SpvDim2D;
   case GLSL_SAMPLER_DIM_3D:
      return SpvDim3D;
   case GLSL_SAMPLER_DIM_CUBE:
      return SpvDimCube;
   case GLSL_SAMPLER_DIM_RECT:
      return SpvDim2D;
   case GLSL_SAMPLER_DIM_BUF:
      return SpvDimBuffer;
   case GLSL_SAMPLER_DIM_EXTERNAL:
      return SpvDim2D; /* seems dodgy... */
   case GLSL_SAMPLER_DIM_MS:
      *is_ms = true;
      return SpvDim2D;
   default:
      fprintf(stderr, "unknown sampler type %d\n", gdim);
      break;
   }
   return SpvDim2D;
}

uint32_t
zink_binding(gl_shader_stage stage, VkDescriptorType type, int index)
{
   if (stage == MESA_SHADER_NONE ||
       stage >= MESA_SHADER_COMPUTE) {
      unreachable("not supported");
   } else {
      uint32_t stage_offset = (uint32_t)stage * (PIPE_MAX_CONSTANT_BUFFERS +
                                                 PIPE_MAX_SHADER_SAMPLER_VIEWS);

      switch (type) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         assert(index < PIPE_MAX_CONSTANT_BUFFERS);
         return stage_offset + index;

      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         assert(index < PIPE_MAX_SHADER_SAMPLER_VIEWS);
         return stage_offset + PIPE_MAX_CONSTANT_BUFFERS + index;

      default:
         unreachable("unexpected type");
      }
   }
}

static void
emit_sampler(struct ntv_context *ctx, struct nir_variable *var)
{
   const struct glsl_type *type = glsl_without_array(var->type);

   bool is_ms;
   SpvDim dimension = type_to_dim(glsl_get_sampler_dim(type), &is_ms);

   SpvId result_type = get_glsl_basetype(ctx, glsl_get_sampler_result_type(type));
   SpvId image_type = spirv_builder_type_image(&ctx->builder, result_type,
                                               dimension, false,
                                               glsl_sampler_type_is_array(type),
                                               is_ms, 1,
                                               SpvImageFormatUnknown);

   SpvId sampled_type = spirv_builder_type_sampled_image(&ctx->builder,
                                                         image_type);
   SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                   SpvStorageClassUniformConstant,
                                                   sampled_type);

   if (glsl_type_is_array(var->type)) {
      /* ARB_arrays_of_arrays from GLSL 1.30 allows nesting of arrays, so we just
       * use the total array size if we encounter a nested array
       */
      unsigned size = glsl_get_aoa_size(var->type);
      for (int i = 0; i < size; ++i) {
         SpvId var_id = spirv_builder_emit_var(&ctx->builder, pointer_type,
                                               SpvStorageClassUniformConstant);

         if (var->name) {
            char element_name[100];
            snprintf(element_name, sizeof(element_name), "%s_%d", var->name, i);
            spirv_builder_emit_name(&ctx->builder, var_id, var->name);
         }

         int index = var->data.binding + i;
         assert(!(ctx->samplers_used & (1 << index)));
         assert(!ctx->image_types[index]);
         ctx->image_types[index] = image_type;
         ctx->samplers[index] = var_id;
         ctx->samplers_used |= 1 << index;

         spirv_builder_emit_descriptor_set(&ctx->builder, var_id, 0);
         int binding = zink_binding(ctx->stage,
                                    zink_sampler_type(glsl_without_array(var->type)),
                                    var->data.binding + i);
         spirv_builder_emit_binding(&ctx->builder, var_id, binding);
      }
   } else {
      SpvId var_id = spirv_builder_emit_var(&ctx->builder, pointer_type,
                                            SpvStorageClassUniformConstant);

      if (var->name)
         spirv_builder_emit_name(&ctx->builder, var_id, var->name);

      int index = var->data.binding;
      assert(!(ctx->samplers_used & (1 << index)));
      assert(!ctx->image_types[index]);
      ctx->image_types[index] = image_type;
      ctx->samplers[index] = var_id;
      ctx->samplers_used |= 1 << index;

      spirv_builder_emit_descriptor_set(&ctx->builder, var_id, 0);
      int binding = zink_binding(ctx->stage,
                                 zink_sampler_type(var->type),
                                 var->data.binding);
      spirv_builder_emit_binding(&ctx->builder, var_id, binding);
   }
}

static void
emit_ubo(struct ntv_context *ctx, struct nir_variable *var)
{
   /* variables accessed inside a uniform block will get merged into a big
    * memory blob and accessed by offset
    */
   if (var->data.location)
      return;

   uint32_t size = glsl_count_attribute_slots(var->interface_type, false);
   SpvId vec4_type = get_uvec_type(ctx, 32, 4);
   SpvId array_length = emit_uint_const(ctx, 32, size);
   SpvId array_type = spirv_builder_type_array(&ctx->builder, vec4_type,
                                               array_length);
   spirv_builder_emit_array_stride(&ctx->builder, array_type, 16);

   // wrap UBO-array in a struct
   SpvId struct_type = spirv_builder_type_struct(&ctx->builder, &array_type, 1);
   if (var->name) {
      char struct_name[100];
      snprintf(struct_name, sizeof(struct_name), "struct_%s", var->name);
      spirv_builder_emit_name(&ctx->builder, struct_type, struct_name);
   }

   spirv_builder_emit_decoration(&ctx->builder, struct_type,
                                 SpvDecorationBlock);
   spirv_builder_emit_member_offset(&ctx->builder, struct_type, 0, 0);


   SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                   SpvStorageClassUniform,
                                                   struct_type);

   SpvId var_id = spirv_builder_emit_var(&ctx->builder, pointer_type,
                                         SpvStorageClassUniform);
   if (var->name)
      spirv_builder_emit_name(&ctx->builder, var_id, var->name);

   assert(ctx->num_ubos < ARRAY_SIZE(ctx->ubos));
   ctx->ubos[ctx->num_ubos++] = var_id;

   spirv_builder_emit_descriptor_set(&ctx->builder, var_id, 0);
   int binding = zink_binding(ctx->stage,
                              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                              var->data.binding);
   spirv_builder_emit_binding(&ctx->builder, var_id, binding);
}

static void
emit_uniform(struct ntv_context *ctx, struct nir_variable *var)
{
   if (var->data.mode == nir_var_mem_ubo)
      emit_ubo(ctx, var);
   else {
      assert(var->data.mode == nir_var_uniform);
      if (glsl_type_is_sampler(glsl_without_array(var->type)))
         emit_sampler(ctx, var);
   }
}

static SpvId
get_vec_from_bit_size(struct ntv_context *ctx, uint32_t bit_size, uint32_t num_components)
{
   if (bit_size == 1)
      return get_bvec_type(ctx, num_components);
   if (bit_size == 32 || bit_size == 64)
      return get_uvec_type(ctx, bit_size, num_components);
   unreachable("unhandled register bit size");
   return 0;
}

static SpvId
get_src_ssa(struct ntv_context *ctx, const nir_ssa_def *ssa)
{
   assert(ssa->index < ctx->num_defs);
   assert(ctx->defs[ssa->index] != 0);
   return ctx->defs[ssa->index];
}

static SpvId
get_var_from_reg(struct ntv_context *ctx, nir_register *reg)
{
   assert(reg->index < ctx->num_regs);
   assert(ctx->regs[reg->index] != 0);
   return ctx->regs[reg->index];
}

static SpvId
get_src_reg(struct ntv_context *ctx, const nir_reg_src *reg)
{
   assert(reg->reg);
   assert(!reg->indirect);
   assert(!reg->base_offset);

   SpvId var = get_var_from_reg(ctx, reg->reg);
   SpvId type = get_vec_from_bit_size(ctx, reg->reg->bit_size, reg->reg->num_components);
   return spirv_builder_emit_load(&ctx->builder, type, var);
}

static SpvId
get_src(struct ntv_context *ctx, nir_src *src)
{
   if (src->is_ssa)
      return get_src_ssa(ctx, src->ssa);
   else
      return get_src_reg(ctx, &src->reg);
}

static SpvId
get_alu_src_raw(struct ntv_context *ctx, nir_alu_instr *alu, unsigned src)
{
   assert(!alu->src[src].negate);
   assert(!alu->src[src].abs);

   SpvId def = get_src(ctx, &alu->src[src].src);

   unsigned used_channels = 0;
   bool need_swizzle = false;
   for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; i++) {
      if (!nir_alu_instr_channel_used(alu, src, i))
         continue;

      used_channels++;

      if (alu->src[src].swizzle[i] != i)
         need_swizzle = true;
   }
   assert(used_channels != 0);

   unsigned live_channels = nir_src_num_components(alu->src[src].src);
   if (used_channels != live_channels)
      need_swizzle = true;

   if (!need_swizzle)
      return def;

   int bit_size = nir_src_bit_size(alu->src[src].src);
   assert(bit_size == 1 || bit_size == 32 || bit_size == 64);

   SpvId raw_type = bit_size == 1 ? spirv_builder_type_bool(&ctx->builder) :
                                    spirv_builder_type_uint(&ctx->builder, bit_size);

   if (used_channels == 1) {
      uint32_t indices[] =  { alu->src[src].swizzle[0] };
      return spirv_builder_emit_composite_extract(&ctx->builder, raw_type,
                                                  def, indices,
                                                  ARRAY_SIZE(indices));
   } else if (live_channels == 1) {
      SpvId raw_vec_type = spirv_builder_type_vector(&ctx->builder,
                                                     raw_type,
                                                     used_channels);

      SpvId constituents[NIR_MAX_VEC_COMPONENTS] = {0};
      for (unsigned i = 0; i < used_channels; ++i)
        constituents[i] = def;

      return spirv_builder_emit_composite_construct(&ctx->builder,
                                                    raw_vec_type,
                                                    constituents,
                                                    used_channels);
   } else {
      SpvId raw_vec_type = spirv_builder_type_vector(&ctx->builder,
                                                     raw_type,
                                                     used_channels);

      uint32_t components[NIR_MAX_VEC_COMPONENTS] = {0};
      size_t num_components = 0;
      for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; i++) {
         if (!nir_alu_instr_channel_used(alu, src, i))
            continue;

         components[num_components++] = alu->src[src].swizzle[i];
      }

      return spirv_builder_emit_vector_shuffle(&ctx->builder, raw_vec_type,
                                               def, def, components,
                                               num_components);
   }
}

static void
store_ssa_def(struct ntv_context *ctx, nir_ssa_def *ssa, SpvId result)
{
   assert(result != 0);
   assert(ssa->index < ctx->num_defs);
   ctx->defs[ssa->index] = result;
}

static SpvId
emit_select(struct ntv_context *ctx, SpvId type, SpvId cond,
            SpvId if_true, SpvId if_false)
{
   return emit_triop(ctx, SpvOpSelect, type, cond, if_true, if_false);
}

static SpvId
uvec_to_bvec(struct ntv_context *ctx, SpvId value, unsigned num_components)
{
   SpvId type = get_bvec_type(ctx, num_components);
   SpvId zero = get_uvec_constant(ctx, 32, num_components, 0);
   return emit_binop(ctx, SpvOpINotEqual, type, value, zero);
}

static SpvId
emit_bitcast(struct ntv_context *ctx, SpvId type, SpvId value)
{
   return emit_unop(ctx, SpvOpBitcast, type, value);
}

static SpvId
bitcast_to_uvec(struct ntv_context *ctx, SpvId value, unsigned bit_size,
                unsigned num_components)
{
   SpvId type = get_uvec_type(ctx, bit_size, num_components);
   return emit_bitcast(ctx, type, value);
}

static SpvId
bitcast_to_ivec(struct ntv_context *ctx, SpvId value, unsigned bit_size,
                unsigned num_components)
{
   SpvId type = get_ivec_type(ctx, bit_size, num_components);
   return emit_bitcast(ctx, type, value);
}

static SpvId
bitcast_to_fvec(struct ntv_context *ctx, SpvId value, unsigned bit_size,
               unsigned num_components)
{
   SpvId type = get_fvec_type(ctx, bit_size, num_components);
   return emit_bitcast(ctx, type, value);
}

static void
store_reg_def(struct ntv_context *ctx, nir_reg_dest *reg, SpvId result)
{
   SpvId var = get_var_from_reg(ctx, reg->reg);
   assert(var);
   spirv_builder_emit_store(&ctx->builder, var, result);
}

static void
store_dest_raw(struct ntv_context *ctx, nir_dest *dest, SpvId result)
{
   if (dest->is_ssa)
      store_ssa_def(ctx, &dest->ssa, result);
   else
      store_reg_def(ctx, &dest->reg, result);
}

static SpvId
store_dest(struct ntv_context *ctx, nir_dest *dest, SpvId result, nir_alu_type type)
{
   unsigned num_components = nir_dest_num_components(*dest);
   unsigned bit_size = nir_dest_bit_size(*dest);

   if (bit_size != 1) {
      switch (nir_alu_type_get_base_type(type)) {
      case nir_type_bool:
         assert("bool should have bit-size 1");

      case nir_type_uint:
         break; /* nothing to do! */

      case nir_type_int:
      case nir_type_float:
         result = bitcast_to_uvec(ctx, result, bit_size, num_components);
         break;

      default:
         unreachable("unsupported nir_alu_type");
      }
   }

   store_dest_raw(ctx, dest, result);
   return result;
}

static SpvId
emit_unop(struct ntv_context *ctx, SpvOp op, SpvId type, SpvId src)
{
   return spirv_builder_emit_unop(&ctx->builder, op, type, src);
}

/* return the intended xfb output vec type based on base type and vector size */
static SpvId
get_output_type(struct ntv_context *ctx, unsigned register_index, unsigned num_components)
{
   const struct glsl_type *out_type = ctx->so_output_gl_types[register_index];
   enum glsl_base_type base_type = glsl_get_base_type(out_type);
   if (base_type == GLSL_TYPE_ARRAY)
      base_type = glsl_get_base_type(glsl_without_array(out_type));

   switch (base_type) {
   case GLSL_TYPE_BOOL:
      return get_bvec_type(ctx, num_components);

   case GLSL_TYPE_FLOAT:
      return get_fvec_type(ctx, 32, num_components);

   case GLSL_TYPE_INT:
      return get_ivec_type(ctx, 32, num_components);

   case GLSL_TYPE_UINT:
      return get_uvec_type(ctx, 32, num_components);

   default:
      break;
   }
   unreachable("unknown type");
   return 0;
}

/* for streamout create new outputs, as streamout can be done on individual components,
   from complete outputs, so we just can't use the created packed outputs */
static void
emit_so_info(struct ntv_context *ctx, const struct zink_so_info *so_info)
{
   for (unsigned i = 0; i < so_info->so_info.num_outputs; i++) {
      struct pipe_stream_output so_output = so_info->so_info.output[i];
      unsigned slot = so_info->so_info_slots[i];
      SpvId out_type = get_output_type(ctx, slot, so_output.num_components);
      SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                      SpvStorageClassOutput,
                                                      out_type);
      SpvId var_id = spirv_builder_emit_var(&ctx->builder, pointer_type,
                                            SpvStorageClassOutput);
      char name[10];

      snprintf(name, 10, "xfb%d", i);
      spirv_builder_emit_name(&ctx->builder, var_id, name);
      spirv_builder_emit_offset(&ctx->builder, var_id, (so_output.dst_offset * 4));
      spirv_builder_emit_xfb_buffer(&ctx->builder, var_id, so_output.output_buffer);
      spirv_builder_emit_xfb_stride(&ctx->builder, var_id, so_info->so_info.stride[so_output.output_buffer] * 4);

      /* output location is incremented by VARYING_SLOT_VAR0 for non-builtins in vtn,
       * so we need to ensure that the new xfb location slot doesn't conflict with any previously-emitted
       * outputs.
       */
      uint32_t location = reserve_slot(ctx);
      assert(location < VARYING_SLOT_VAR0);
      spirv_builder_emit_location(&ctx->builder, var_id, location);

      /* note: gl_ClipDistance[4] can the 0-indexed member of VARYING_SLOT_CLIP_DIST1 here,
       * so this is still the 0 component
       */
      if (so_output.start_component)
         spirv_builder_emit_component(&ctx->builder, var_id, so_output.start_component);

      uint32_t *key = ralloc_size(ctx->mem_ctx, sizeof(uint32_t));
      *key = (uint32_t)so_output.register_index << 2 | so_output.start_component;
      _mesa_hash_table_insert(ctx->so_outputs, key, (void *)(intptr_t)var_id);

      assert(ctx->num_entry_ifaces < ARRAY_SIZE(ctx->entry_ifaces));
      ctx->entry_ifaces[ctx->num_entry_ifaces++] = var_id;
   }
}

static void
emit_so_outputs(struct ntv_context *ctx,
                const struct zink_so_info *so_info)
{
   SpvId loaded_outputs[VARYING_SLOT_MAX] = {};
   for (unsigned i = 0; i < so_info->so_info.num_outputs; i++) {
      uint32_t components[NIR_MAX_VEC_COMPONENTS];
      unsigned slot = so_info->so_info_slots[i];
      struct pipe_stream_output so_output = so_info->so_info.output[i];
      uint32_t so_key = (uint32_t) so_output.register_index << 2 | so_output.start_component;
      struct hash_entry *he = _mesa_hash_table_search(ctx->so_outputs, &so_key);
      assert(he);
      SpvId so_output_var_id = (SpvId)(intptr_t)he->data;

      SpvId type = get_output_type(ctx, slot, so_output.num_components);
      SpvId output = ctx->outputs[slot];
      SpvId output_type = ctx->so_output_types[slot];
      const struct glsl_type *out_type = ctx->so_output_gl_types[slot];

      if (!loaded_outputs[slot])
         loaded_outputs[slot] = spirv_builder_emit_load(&ctx->builder, output_type, output);
      SpvId src = loaded_outputs[slot];

      SpvId result;

      for (unsigned c = 0; c < so_output.num_components; c++) {
         components[c] = so_output.start_component + c;
         /* this is the second half of a 2 * vec4 array */
         if (slot == VARYING_SLOT_CLIP_DIST1)
            components[c] += 4;
      }

      /* if we're emitting a scalar or the type we're emitting matches the output's original type and we're
       * emitting the same number of components, then we can skip any sort of conversion here
       */
      if (glsl_type_is_scalar(out_type) || (type == output_type && glsl_get_length(out_type) == so_output.num_components))
         result = src;
      else {
         /* OpCompositeExtract can only extract scalars for our use here */
         if (so_output.num_components == 1) {
            result = spirv_builder_emit_composite_extract(&ctx->builder, type, src, components, so_output.num_components);
         } else if (glsl_type_is_vector(out_type)) {
            /* OpVectorShuffle can select vector members into a differently-sized vector */
            result = spirv_builder_emit_vector_shuffle(&ctx->builder, type,
                                                             src, src,
                                                             components, so_output.num_components);
            result = emit_bitcast(ctx, type, result);
         } else {
             /* for arrays, we need to manually extract each desired member
              * and re-pack them into the desired output type
              */
             for (unsigned c = 0; c < so_output.num_components; c++) {
                uint32_t member[] = { so_output.start_component + c };
                SpvId base_type = get_glsl_type(ctx, glsl_without_array(out_type));

                if (slot == VARYING_SLOT_CLIP_DIST1)
                   member[0] += 4;
                components[c] = spirv_builder_emit_composite_extract(&ctx->builder, base_type, src, member, 1);
             }
             result = spirv_builder_emit_composite_construct(&ctx->builder, type, components, so_output.num_components);
         }
      }

      spirv_builder_emit_store(&ctx->builder, so_output_var_id, result);
   }
}

static SpvId
emit_binop(struct ntv_context *ctx, SpvOp op, SpvId type,
           SpvId src0, SpvId src1)
{
   return spirv_builder_emit_binop(&ctx->builder, op, type, src0, src1);
}

static SpvId
emit_triop(struct ntv_context *ctx, SpvOp op, SpvId type,
           SpvId src0, SpvId src1, SpvId src2)
{
   return spirv_builder_emit_triop(&ctx->builder, op, type, src0, src1, src2);
}

static SpvId
emit_builtin_unop(struct ntv_context *ctx, enum GLSLstd450 op, SpvId type,
                  SpvId src)
{
   SpvId args[] = { src };
   return spirv_builder_emit_ext_inst(&ctx->builder, type, ctx->GLSL_std_450,
                                      op, args, ARRAY_SIZE(args));
}

static SpvId
emit_builtin_binop(struct ntv_context *ctx, enum GLSLstd450 op, SpvId type,
                   SpvId src0, SpvId src1)
{
   SpvId args[] = { src0, src1 };
   return spirv_builder_emit_ext_inst(&ctx->builder, type, ctx->GLSL_std_450,
                                      op, args, ARRAY_SIZE(args));
}

static SpvId
emit_builtin_triop(struct ntv_context *ctx, enum GLSLstd450 op, SpvId type,
                   SpvId src0, SpvId src1, SpvId src2)
{
   SpvId args[] = { src0, src1, src2 };
   return spirv_builder_emit_ext_inst(&ctx->builder, type, ctx->GLSL_std_450,
                                      op, args, ARRAY_SIZE(args));
}

static SpvId
get_fvec_constant(struct ntv_context *ctx, unsigned bit_size,
                  unsigned num_components, double value)
{
   assert(bit_size == 32 || bit_size == 64);

   SpvId result = emit_float_const(ctx, bit_size, value);
   if (num_components == 1)
      return result;

   assert(num_components > 1);
   SpvId components[num_components];
   for (int i = 0; i < num_components; i++)
      components[i] = result;

   SpvId type = get_fvec_type(ctx, bit_size, num_components);
   return spirv_builder_const_composite(&ctx->builder, type, components,
                                        num_components);
}

static SpvId
get_uvec_constant(struct ntv_context *ctx, unsigned bit_size,
                  unsigned num_components, uint64_t value)
{
   assert(bit_size == 32 || bit_size == 64);

   SpvId result = emit_uint_const(ctx, bit_size, value);
   if (num_components == 1)
      return result;

   assert(num_components > 1);
   SpvId components[num_components];
   for (int i = 0; i < num_components; i++)
      components[i] = result;

   SpvId type = get_uvec_type(ctx, bit_size, num_components);
   return spirv_builder_const_composite(&ctx->builder, type, components,
                                        num_components);
}

static SpvId
get_ivec_constant(struct ntv_context *ctx, unsigned bit_size,
                  unsigned num_components, int64_t value)
{
   assert(bit_size == 32 || bit_size == 64);

   SpvId result = emit_int_const(ctx, bit_size, value);
   if (num_components == 1)
      return result;

   assert(num_components > 1);
   SpvId components[num_components];
   for (int i = 0; i < num_components; i++)
      components[i] = result;

   SpvId type = get_ivec_type(ctx, bit_size, num_components);
   return spirv_builder_const_composite(&ctx->builder, type, components,
                                        num_components);
}

static inline unsigned
alu_instr_src_components(const nir_alu_instr *instr, unsigned src)
{
   if (nir_op_infos[instr->op].input_sizes[src] > 0)
      return nir_op_infos[instr->op].input_sizes[src];

   if (instr->dest.dest.is_ssa)
      return instr->dest.dest.ssa.num_components;
   else
      return instr->dest.dest.reg.reg->num_components;
}

static SpvId
get_alu_src(struct ntv_context *ctx, nir_alu_instr *alu, unsigned src)
{
   SpvId raw_value = get_alu_src_raw(ctx, alu, src);

   unsigned num_components = alu_instr_src_components(alu, src);
   unsigned bit_size = nir_src_bit_size(alu->src[src].src);
   nir_alu_type type = nir_op_infos[alu->op].input_types[src];

   if (bit_size == 1)
      return raw_value;
   else {
      switch (nir_alu_type_get_base_type(type)) {
      case nir_type_bool:
         unreachable("bool should have bit-size 1");

      case nir_type_int:
         return bitcast_to_ivec(ctx, raw_value, bit_size, num_components);

      case nir_type_uint:
         return raw_value;

      case nir_type_float:
         return bitcast_to_fvec(ctx, raw_value, bit_size, num_components);

      default:
         unreachable("unknown nir_alu_type");
      }
   }
}

static SpvId
store_alu_result(struct ntv_context *ctx, nir_alu_instr *alu, SpvId result)
{
   assert(!alu->dest.saturate);
   return store_dest(ctx, &alu->dest.dest, result,
                     nir_op_infos[alu->op].output_type);
}

static SpvId
get_dest_type(struct ntv_context *ctx, nir_dest *dest, nir_alu_type type)
{
   unsigned num_components = nir_dest_num_components(*dest);
   unsigned bit_size = nir_dest_bit_size(*dest);

   if (bit_size == 1)
      return get_bvec_type(ctx, num_components);

   switch (nir_alu_type_get_base_type(type)) {
   case nir_type_bool:
      unreachable("bool should have bit-size 1");

   case nir_type_int:
      return get_ivec_type(ctx, bit_size, num_components);

   case nir_type_uint:
      return get_uvec_type(ctx, bit_size, num_components);

   case nir_type_float:
      return get_fvec_type(ctx, bit_size, num_components);

   default:
      unreachable("unsupported nir_alu_type");
   }
}

static void
emit_alu(struct ntv_context *ctx, nir_alu_instr *alu)
{
   SpvId src[nir_op_infos[alu->op].num_inputs];
   unsigned in_bit_sizes[nir_op_infos[alu->op].num_inputs];
   for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
      src[i] = get_alu_src(ctx, alu, i);
      in_bit_sizes[i] = nir_src_bit_size(alu->src[i].src);
   }

   SpvId dest_type = get_dest_type(ctx, &alu->dest.dest,
                                   nir_op_infos[alu->op].output_type);
   unsigned bit_size = nir_dest_bit_size(alu->dest.dest);
   unsigned num_components = nir_dest_num_components(alu->dest.dest);

   SpvId result = 0;
   switch (alu->op) {
   case nir_op_mov:
      assert(nir_op_infos[alu->op].num_inputs == 1);
      result = src[0];
      break;

#define UNOP(nir_op, spirv_op) \
   case nir_op: \
      assert(nir_op_infos[alu->op].num_inputs == 1); \
      result = emit_unop(ctx, spirv_op, dest_type, src[0]); \
      break;

   UNOP(nir_op_ineg, SpvOpSNegate)
   UNOP(nir_op_fneg, SpvOpFNegate)
   UNOP(nir_op_fddx, SpvOpDPdx)
   UNOP(nir_op_fddx_coarse, SpvOpDPdxCoarse)
   UNOP(nir_op_fddx_fine, SpvOpDPdxFine)
   UNOP(nir_op_fddy, SpvOpDPdy)
   UNOP(nir_op_fddy_coarse, SpvOpDPdyCoarse)
   UNOP(nir_op_fddy_fine, SpvOpDPdyFine)
   UNOP(nir_op_f2i32, SpvOpConvertFToS)
   UNOP(nir_op_f2u32, SpvOpConvertFToU)
   UNOP(nir_op_i2f32, SpvOpConvertSToF)
   UNOP(nir_op_u2f32, SpvOpConvertUToF)
   UNOP(nir_op_u2u32, SpvOpUConvert)
   UNOP(nir_op_f2f32, SpvOpFConvert)
   UNOP(nir_op_u2f64, SpvOpConvertUToF)
   UNOP(nir_op_i2f64, SpvOpConvertSToF)
   UNOP(nir_op_u2u64, SpvOpUConvert)
   UNOP(nir_op_f2f64, SpvOpFConvert)
   UNOP(nir_op_bitfield_reverse, SpvOpBitReverse)
   UNOP(nir_op_bit_count, SpvOpBitCount)
#undef UNOP

   case nir_op_inot:
      if (bit_size == 1)
         result = emit_unop(ctx, SpvOpLogicalNot, dest_type, src[0]);
      else
         result = emit_unop(ctx, SpvOpNot, dest_type, src[0]);
      break;

   case nir_op_b2i32:
      assert(nir_op_infos[alu->op].num_inputs == 1);
      result = emit_select(ctx, dest_type, src[0],
                           get_ivec_constant(ctx, 32, num_components, 1),
                           get_ivec_constant(ctx, 32, num_components, 0));
      break;

   case nir_op_b2f32:
   case nir_op_b2f64:
      assert(nir_op_infos[alu->op].num_inputs == 1);
      result = emit_select(ctx, dest_type, src[0],
                           get_fvec_constant(ctx, bit_size, num_components, 1),
                           get_fvec_constant(ctx, bit_size, num_components, 0));
      break;

#define BUILTIN_UNOP(nir_op, spirv_op) \
   case nir_op: \
      assert(nir_op_infos[alu->op].num_inputs == 1); \
      result = emit_builtin_unop(ctx, spirv_op, dest_type, src[0]); \
      break;

   BUILTIN_UNOP(nir_op_iabs, GLSLstd450SAbs)
   BUILTIN_UNOP(nir_op_fabs, GLSLstd450FAbs)
   BUILTIN_UNOP(nir_op_fsqrt, GLSLstd450Sqrt)
   BUILTIN_UNOP(nir_op_frsq, GLSLstd450InverseSqrt)
   BUILTIN_UNOP(nir_op_flog2, GLSLstd450Log2)
   BUILTIN_UNOP(nir_op_fexp2, GLSLstd450Exp2)
   BUILTIN_UNOP(nir_op_ffract, GLSLstd450Fract)
   BUILTIN_UNOP(nir_op_ffloor, GLSLstd450Floor)
   BUILTIN_UNOP(nir_op_fceil, GLSLstd450Ceil)
   BUILTIN_UNOP(nir_op_ftrunc, GLSLstd450Trunc)
   BUILTIN_UNOP(nir_op_fround_even, GLSLstd450RoundEven)
   BUILTIN_UNOP(nir_op_fsign, GLSLstd450FSign)
   BUILTIN_UNOP(nir_op_isign, GLSLstd450SSign)
   BUILTIN_UNOP(nir_op_fsin, GLSLstd450Sin)
   BUILTIN_UNOP(nir_op_fcos, GLSLstd450Cos)
   BUILTIN_UNOP(nir_op_ufind_msb, GLSLstd450FindUMsb)
   BUILTIN_UNOP(nir_op_find_lsb, GLSLstd450FindILsb)
   BUILTIN_UNOP(nir_op_ifind_msb, GLSLstd450FindSMsb)
#undef BUILTIN_UNOP

   case nir_op_frcp:
      assert(nir_op_infos[alu->op].num_inputs == 1);
      result = emit_binop(ctx, SpvOpFDiv, dest_type,
                          get_fvec_constant(ctx, bit_size, num_components, 1),
                          src[0]);
      break;

   case nir_op_f2b1:
      assert(nir_op_infos[alu->op].num_inputs == 1);
      result = emit_binop(ctx, SpvOpFOrdNotEqual, dest_type, src[0],
                          get_fvec_constant(ctx,
                                            nir_src_bit_size(alu->src[0].src),
                                            num_components, 0));
      break;
   case nir_op_i2b1:
      assert(nir_op_infos[alu->op].num_inputs == 1);
      result = emit_binop(ctx, SpvOpINotEqual, dest_type, src[0],
                          get_ivec_constant(ctx,
                                            nir_src_bit_size(alu->src[0].src),
                                            num_components, 0));
      break;


#define BINOP(nir_op, spirv_op) \
   case nir_op: \
      assert(nir_op_infos[alu->op].num_inputs == 2); \
      result = emit_binop(ctx, spirv_op, dest_type, src[0], src[1]); \
      break;

   BINOP(nir_op_iadd, SpvOpIAdd)
   BINOP(nir_op_isub, SpvOpISub)
   BINOP(nir_op_imul, SpvOpIMul)
   BINOP(nir_op_idiv, SpvOpSDiv)
   BINOP(nir_op_udiv, SpvOpUDiv)
   BINOP(nir_op_umod, SpvOpUMod)
   BINOP(nir_op_fadd, SpvOpFAdd)
   BINOP(nir_op_fsub, SpvOpFSub)
   BINOP(nir_op_fmul, SpvOpFMul)
   BINOP(nir_op_fdiv, SpvOpFDiv)
   BINOP(nir_op_fmod, SpvOpFMod)
   BINOP(nir_op_ilt, SpvOpSLessThan)
   BINOP(nir_op_ige, SpvOpSGreaterThanEqual)
   BINOP(nir_op_ult, SpvOpULessThan)
   BINOP(nir_op_uge, SpvOpUGreaterThanEqual)
   BINOP(nir_op_flt, SpvOpFOrdLessThan)
   BINOP(nir_op_fge, SpvOpFOrdGreaterThanEqual)
   BINOP(nir_op_feq, SpvOpFOrdEqual)
   BINOP(nir_op_fneu, SpvOpFUnordNotEqual)
   BINOP(nir_op_ishl, SpvOpShiftLeftLogical)
   BINOP(nir_op_ishr, SpvOpShiftRightArithmetic)
   BINOP(nir_op_ushr, SpvOpShiftRightLogical)
   BINOP(nir_op_ixor, SpvOpBitwiseXor)
   BINOP(nir_op_frem, SpvOpFRem)
#undef BINOP

#define BINOP_LOG(nir_op, spv_op, spv_log_op) \
   case nir_op: \
      assert(nir_op_infos[alu->op].num_inputs == 2); \
      if (nir_src_bit_size(alu->src[0].src) == 1) \
         result = emit_binop(ctx, spv_log_op, dest_type, src[0], src[1]); \
      else \
         result = emit_binop(ctx, spv_op, dest_type, src[0], src[1]); \
      break;

   BINOP_LOG(nir_op_iand, SpvOpBitwiseAnd, SpvOpLogicalAnd)
   BINOP_LOG(nir_op_ior, SpvOpBitwiseOr, SpvOpLogicalOr)
   BINOP_LOG(nir_op_ieq, SpvOpIEqual, SpvOpLogicalEqual)
   BINOP_LOG(nir_op_ine, SpvOpINotEqual, SpvOpLogicalNotEqual)
#undef BINOP_LOG

#define BUILTIN_BINOP(nir_op, spirv_op) \
   case nir_op: \
      assert(nir_op_infos[alu->op].num_inputs == 2); \
      result = emit_builtin_binop(ctx, spirv_op, dest_type, src[0], src[1]); \
      break;

   BUILTIN_BINOP(nir_op_fmin, GLSLstd450FMin)
   BUILTIN_BINOP(nir_op_fmax, GLSLstd450FMax)
   BUILTIN_BINOP(nir_op_imin, GLSLstd450SMin)
   BUILTIN_BINOP(nir_op_imax, GLSLstd450SMax)
   BUILTIN_BINOP(nir_op_umin, GLSLstd450UMin)
   BUILTIN_BINOP(nir_op_umax, GLSLstd450UMax)
#undef BUILTIN_BINOP

   case nir_op_fdot2:
   case nir_op_fdot3:
   case nir_op_fdot4:
      assert(nir_op_infos[alu->op].num_inputs == 2);
      result = emit_binop(ctx, SpvOpDot, dest_type, src[0], src[1]);
      break;

   case nir_op_fdph:
      unreachable("should already be lowered away");

   case nir_op_seq:
   case nir_op_sne:
   case nir_op_slt:
   case nir_op_sge: {
      assert(nir_op_infos[alu->op].num_inputs == 2);
      int num_components = nir_dest_num_components(alu->dest.dest);
      SpvId bool_type = get_bvec_type(ctx, num_components);

      SpvId zero = emit_float_const(ctx, bit_size, 0.0f);
      SpvId one = emit_float_const(ctx, bit_size, 1.0f);
      if (num_components > 1) {
         SpvId zero_comps[num_components], one_comps[num_components];
         for (int i = 0; i < num_components; i++) {
            zero_comps[i] = zero;
            one_comps[i] = one;
         }

         zero = spirv_builder_const_composite(&ctx->builder, dest_type,
                                              zero_comps, num_components);
         one = spirv_builder_const_composite(&ctx->builder, dest_type,
                                             one_comps, num_components);
      }

      SpvOp op;
      switch (alu->op) {
      case nir_op_seq: op = SpvOpFOrdEqual; break;
      case nir_op_sne: op = SpvOpFOrdNotEqual; break;
      case nir_op_slt: op = SpvOpFOrdLessThan; break;
      case nir_op_sge: op = SpvOpFOrdGreaterThanEqual; break;
      default: unreachable("unexpected op");
      }

      result = emit_binop(ctx, op, bool_type, src[0], src[1]);
      result = emit_select(ctx, dest_type, result, one, zero);
      }
      break;

   case nir_op_flrp:
      assert(nir_op_infos[alu->op].num_inputs == 3);
      result = emit_builtin_triop(ctx, GLSLstd450FMix, dest_type,
                                  src[0], src[1], src[2]);
      break;

   case nir_op_fcsel:
      result = emit_binop(ctx, SpvOpFOrdGreaterThan,
                          get_bvec_type(ctx, num_components),
                          src[0],
                          get_fvec_constant(ctx,
                                            nir_src_bit_size(alu->src[0].src),
                                            num_components, 0));
      result = emit_select(ctx, dest_type, result, src[1], src[2]);
      break;

   case nir_op_bcsel:
      assert(nir_op_infos[alu->op].num_inputs == 3);
      result = emit_select(ctx, dest_type, src[0], src[1], src[2]);
      break;

   case nir_op_bany_fnequal2:
   case nir_op_bany_fnequal3:
   case nir_op_bany_fnequal4: {
      assert(nir_op_infos[alu->op].num_inputs == 2);
      assert(alu_instr_src_components(alu, 0) ==
             alu_instr_src_components(alu, 1));
      assert(in_bit_sizes[0] == in_bit_sizes[1]);
      /* The type of Operand 1 and Operand 2 must be a scalar or vector of floating-point type. */
      SpvOp op = in_bit_sizes[0] == 1 ? SpvOpLogicalNotEqual : SpvOpFOrdNotEqual;
      result = emit_binop(ctx, op,
                          get_bvec_type(ctx, alu_instr_src_components(alu, 0)),
                          src[0], src[1]);
      result = emit_unop(ctx, SpvOpAny, dest_type, result);
      break;
   }

   case nir_op_ball_fequal2:
   case nir_op_ball_fequal3:
   case nir_op_ball_fequal4: {
      assert(nir_op_infos[alu->op].num_inputs == 2);
      assert(alu_instr_src_components(alu, 0) ==
             alu_instr_src_components(alu, 1));
      assert(in_bit_sizes[0] == in_bit_sizes[1]);
      /* The type of Operand 1 and Operand 2 must be a scalar or vector of floating-point type. */
      SpvOp op = in_bit_sizes[0] == 1 ? SpvOpLogicalEqual : SpvOpFOrdEqual;
      result = emit_binop(ctx, op,
                          get_bvec_type(ctx, alu_instr_src_components(alu, 0)),
                          src[0], src[1]);
      result = emit_unop(ctx, SpvOpAll, dest_type, result);
      break;
   }

   case nir_op_bany_inequal2:
   case nir_op_bany_inequal3:
   case nir_op_bany_inequal4: {
      assert(nir_op_infos[alu->op].num_inputs == 2);
      assert(alu_instr_src_components(alu, 0) ==
             alu_instr_src_components(alu, 1));
      assert(in_bit_sizes[0] == in_bit_sizes[1]);
      /* The type of Operand 1 and Operand 2 must be a scalar or vector of integer type. */
      SpvOp op = in_bit_sizes[0] == 1 ? SpvOpLogicalNotEqual : SpvOpINotEqual;
      result = emit_binop(ctx, op,
                          get_bvec_type(ctx, alu_instr_src_components(alu, 0)),
                          src[0], src[1]);
      result = emit_unop(ctx, SpvOpAny, dest_type, result);
      break;
   }

   case nir_op_ball_iequal2:
   case nir_op_ball_iequal3:
   case nir_op_ball_iequal4: {
      assert(nir_op_infos[alu->op].num_inputs == 2);
      assert(alu_instr_src_components(alu, 0) ==
             alu_instr_src_components(alu, 1));
      assert(in_bit_sizes[0] == in_bit_sizes[1]);
      /* The type of Operand 1 and Operand 2 must be a scalar or vector of integer type. */
      SpvOp op = in_bit_sizes[0] == 1 ? SpvOpLogicalEqual : SpvOpIEqual;
      result = emit_binop(ctx, op,
                          get_bvec_type(ctx, alu_instr_src_components(alu, 0)),
                          src[0], src[1]);
      result = emit_unop(ctx, SpvOpAll, dest_type, result);
      break;
   }

   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4: {
      int num_inputs = nir_op_infos[alu->op].num_inputs;
      assert(2 <= num_inputs && num_inputs <= 4);
      result = spirv_builder_emit_composite_construct(&ctx->builder, dest_type,
                                                      src, num_inputs);
   }
   break;

   case nir_op_ubitfield_extract:
      assert(nir_op_infos[alu->op].num_inputs == 3);
      result = emit_triop(ctx, SpvOpBitFieldUExtract, dest_type, src[0], src[1], src[2]);
      break;

   case nir_op_ibitfield_extract:
      assert(nir_op_infos[alu->op].num_inputs == 3);
      result = emit_triop(ctx, SpvOpBitFieldSExtract, dest_type, src[0], src[1], src[2]);
      break;

   case nir_op_bitfield_insert:
      assert(nir_op_infos[alu->op].num_inputs == 4);
      result = spirv_builder_emit_quadop(&ctx->builder, SpvOpBitFieldInsert, dest_type, src[0], src[1], src[2], src[3]);
      break;

   default:
      fprintf(stderr, "emit_alu: not implemented (%s)\n",
              nir_op_infos[alu->op].name);

      unreachable("unsupported opcode");
      return;
   }

   store_alu_result(ctx, alu, result);
}

static void
emit_load_const(struct ntv_context *ctx, nir_load_const_instr *load_const)
{
   unsigned bit_size = load_const->def.bit_size;
   unsigned num_components = load_const->def.num_components;

   SpvId constant;
   if (num_components > 1) {
      SpvId components[num_components];
      SpvId type = get_vec_from_bit_size(ctx, bit_size, num_components);
      if (bit_size == 1) {
         for (int i = 0; i < num_components; i++)
            components[i] = spirv_builder_const_bool(&ctx->builder,
                                                     load_const->value[i].b);

      } else {
         for (int i = 0; i < num_components; i++)
            components[i] = emit_uint_const(ctx, bit_size,
                                            bit_size == 64 ? load_const->value[i].u64 : load_const->value[i].u32);

      }
      constant = spirv_builder_const_composite(&ctx->builder, type,
                                               components, num_components);
   } else {
      assert(num_components == 1);
      if (bit_size == 1)
         constant = spirv_builder_const_bool(&ctx->builder,
                                             load_const->value[0].b);
      else if (bit_size == 32)
         constant = emit_uint_const(ctx, bit_size, load_const->value[0].u32);
      else if (bit_size == 64)
         constant = emit_uint_const(ctx, bit_size, load_const->value[0].u64);
      else
         unreachable("unhandled constant bit size!");
   }

   store_ssa_def(ctx, &load_const->def, constant);
}

static void
emit_load_ubo(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   ASSERTED nir_const_value *const_block_index = nir_src_as_const_value(intr->src[0]);
   assert(const_block_index); // no dynamic indexing for now

   unsigned bit_size = nir_dest_bit_size(intr->dest);
   SpvId uint_type = get_uvec_type(ctx, 32, 1);
   SpvId one = emit_uint_const(ctx, 32, 1);

   /* number of components being loaded */
   unsigned num_components = nir_dest_num_components(intr->dest);
   /* we need to grab 2x32 to fill the 64bit value */
   if (bit_size == 64)
      num_components *= 2;
   SpvId constituents[num_components];
   SpvId result;

   /* destination type for the load */
   SpvId type = get_dest_uvec_type(ctx, &intr->dest);
   /* an id of the array stride in bytes */
   SpvId vec4_size = emit_uint_const(ctx, 32, sizeof(uint32_t) * 4);
   /* an id of an array member in bytes */
   SpvId uint_size = emit_uint_const(ctx, 32, sizeof(uint32_t));

   /* we grab a single array member at a time, so it's a pointer to a uint */
   SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                   SpvStorageClassUniform,
                                                   uint_type);

   /* our generated uniform has a memory layout like
    *
    * struct {
    *    vec4 base[array_size];
    * };
    *
    * where 'array_size' is set as though every member of the ubo takes up a vec4,
    * even if it's only a vec2 or a float.
    *
    * first, access 'base'
    */
   SpvId member = emit_uint_const(ctx, 32, 0);
   /* this is the offset (in bytes) that we're accessing:
    * it may be a const value or it may be dynamic in the shader
    */
   SpvId offset = get_src(ctx, &intr->src[1]);
   /* convert offset to an array index for 'base' to determine which vec4 to access */
   SpvId vec_offset = emit_binop(ctx, SpvOpUDiv, uint_type, offset, vec4_size);
   /* use the remainder to calculate the byte offset in the vec, which tells us the member
    * that we're going to access
    */
   SpvId vec_member_offset = emit_binop(ctx, SpvOpUDiv, uint_type,
                                        emit_binop(ctx, SpvOpUMod, uint_type, offset, vec4_size),
                                        uint_size);
   /* OpAccessChain takes an array of indices that drill into a hierarchy based on the type:
    * index 0 is accessing 'base'
    * index 1 is accessing 'base[index 1]'
    * index 2 is accessing 'base[index 1][index 2]'
    *
    * we must perform the access this way in case src[1] is dynamic because there's
    * no other spirv method for using an id to access a member of a composite, as
    * (composite|vector)_extract both take literals
    */
   for (unsigned i = 0; i < num_components; i++) {
      SpvId indices[3] = { member, vec_offset, vec_member_offset };
      SpvId ptr = spirv_builder_emit_access_chain(&ctx->builder, pointer_type,
                                                  ctx->ubos[const_block_index->u32], indices,
                                                  ARRAY_SIZE(indices));
      /* load a single value into the constituents array */
      constituents[i] = spirv_builder_emit_load(&ctx->builder, uint_type, ptr);
      /* increment to the next vec4 member index for the next load */
      vec_member_offset = emit_binop(ctx, SpvOpIAdd, uint_type, vec_member_offset, one);
      if (i == 3 && num_components > 4) {
         vec_offset = emit_binop(ctx, SpvOpIAdd, uint_type, vec_offset, one);
         vec_member_offset = emit_uint_const(ctx, 32, 0);
      }
   }

   /* if we're loading a 64bit value, we have to reassemble all the u32 values we've loaded into u64 values
    * by creating uvec2 composites and bitcasting them to u64 values
    */
   if (bit_size == 64) {
      num_components /= 2;
      type = get_uvec_type(ctx, 64, num_components);
      SpvId u64_type = get_uvec_type(ctx, 64, 1);
      for (unsigned i = 0; i < num_components; i++) {
         constituents[i] = spirv_builder_emit_composite_construct(&ctx->builder, get_uvec_type(ctx, 32, 2), constituents + i * 2, 2);
         constituents[i] = emit_bitcast(ctx, u64_type, constituents[i]);
      }
   }
   /* if loading more than 1 value, reassemble the results into the desired type,
    * otherwise just use the loaded result
    */
   if (num_components > 1) {
      result = spirv_builder_emit_composite_construct(&ctx->builder,
                                                      type,
                                                      constituents,
                                                      num_components);
   } else
      result = constituents[0];

   /* explicitly convert to a bool vector if the destination type is a bool */
   if (nir_dest_bit_size(intr->dest) == 1)
      result = uvec_to_bvec(ctx, result, num_components);

   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static void
emit_discard(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   assert(ctx->block_started);
   spirv_builder_emit_kill(&ctx->builder);
   /* discard is weird in NIR, so let's just create an unreachable block after
      it and hope that the vulkan driver will DCE any instructinos in it. */
   spirv_builder_label(&ctx->builder, spirv_builder_new_id(&ctx->builder));
}

static void
emit_load_deref(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId ptr = get_src(ctx, intr->src);

   SpvId result = spirv_builder_emit_load(&ctx->builder,
                                          get_glsl_type(ctx, nir_src_as_deref(intr->src[0])->type),
                                          ptr);
   unsigned num_components = nir_dest_num_components(intr->dest);
   unsigned bit_size = nir_dest_bit_size(intr->dest);
   result = bitcast_to_uvec(ctx, result, bit_size, num_components);
   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static void
emit_store_deref(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId ptr = get_src(ctx, &intr->src[0]);
   SpvId src = get_src(ctx, &intr->src[1]);

   const struct glsl_type *gtype = nir_src_as_deref(intr->src[0])->type;
   SpvId type = get_glsl_type(ctx, gtype);
   nir_variable *var = nir_deref_instr_get_variable(nir_src_as_deref(intr->src[0]));
   unsigned num_writes = util_bitcount(nir_intrinsic_write_mask(intr));
   unsigned wrmask = nir_intrinsic_write_mask(intr);
   if (num_writes && num_writes != intr->num_components) {
      /* no idea what we do if this fails */
      assert(glsl_type_is_array(gtype) || glsl_type_is_vector(gtype));

      /* this is a partial write, so we have to loop and do a per-component write */
      SpvId result_type;
      SpvId member_type;
      if (glsl_type_is_vector(gtype)) {
         result_type = get_glsl_basetype(ctx, glsl_get_base_type(gtype));
         member_type = get_uvec_type(ctx, 32, 1);
      } else
         member_type = result_type = get_glsl_type(ctx, glsl_get_array_element(gtype));
      SpvId ptr_type = spirv_builder_type_pointer(&ctx->builder,
                                                  SpvStorageClassOutput,
                                                  result_type);
      for (unsigned i = 0; i < 4; i++)
         if ((wrmask >> i) & 1) {
            SpvId idx = emit_uint_const(ctx, 32, i);
            SpvId val = spirv_builder_emit_composite_extract(&ctx->builder, member_type, src, &i, 1);
            val = emit_bitcast(ctx, result_type, val);
            SpvId member = spirv_builder_emit_access_chain(&ctx->builder, ptr_type,
                                                           ptr, &idx, 1);
            spirv_builder_emit_store(&ctx->builder, member, val);
         }
      return;

   }
   SpvId result;
   if (ctx->stage == MESA_SHADER_FRAGMENT && var->data.location == FRAG_RESULT_SAMPLE_MASK) {
      src = emit_bitcast(ctx, type, src);
      /* SampleMask is always an array in spirv, so we need to construct it into one */
      result = spirv_builder_emit_composite_construct(&ctx->builder, ctx->sample_mask_type, &src, 1);
   } else
      result = emit_bitcast(ctx, type, src);
   spirv_builder_emit_store(&ctx->builder, ptr, result);
}

/* FIXME: this is currently VERY specific to injected TCS usage */
static void
emit_load_push_const(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   unsigned bit_size = nir_dest_bit_size(intr->dest);
   SpvId uint_type = get_uvec_type(ctx, 32, 1);
   SpvId load_type = get_uvec_type(ctx, 32, 1);

   /* number of components being loaded */
   unsigned num_components = nir_dest_num_components(intr->dest);
   /* we need to grab 2x32 to fill the 64bit value */
   if (bit_size == 64)
      num_components *= 2;
   SpvId constituents[num_components];
   SpvId result;

   /* destination type for the load */
   SpvId type = get_dest_uvec_type(ctx, &intr->dest);
   /* an id of an array member in bytes */
   SpvId uint_size = emit_uint_const(ctx, 32, sizeof(uint32_t));
   SpvId one = emit_uint_const(ctx, 32, 1);

   /* we grab a single array member at a time, so it's a pointer to a uint */
   SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                   SpvStorageClassPushConstant,
                                                   load_type);

   SpvId member = emit_uint_const(ctx, 32, 0);
   /* this is the offset (in bytes) that we're accessing:
    * it may be a const value or it may be dynamic in the shader
    */
   SpvId offset = get_src(ctx, &intr->src[0]);
   offset = emit_binop(ctx, SpvOpUDiv, uint_type, offset, uint_size);
   /* OpAccessChain takes an array of indices that drill into a hierarchy based on the type:
    * index 0 is accessing 'base'
    * index 1 is accessing 'base[index 1]'
    *
    */
   for (unsigned i = 0; i < num_components; i++) {
      SpvId indices[2] = { member, offset };
      SpvId ptr = spirv_builder_emit_access_chain(&ctx->builder, pointer_type,
                                                  ctx->push_const_var, indices,
                                                  ARRAY_SIZE(indices));
      /* load a single value into the constituents array */
      constituents[i] = spirv_builder_emit_load(&ctx->builder, load_type, ptr);
      /* increment to the next vec4 member index for the next load */
      offset = emit_binop(ctx, SpvOpIAdd, uint_type, offset, one);
   }

   /* if we're loading a 64bit value, we have to reassemble all the u32 values we've loaded into u64 values
    * by creating uvec2 composites and bitcasting them to u64 values
    */
   if (bit_size == 64) {
      num_components /= 2;
      type = get_uvec_type(ctx, 64, num_components);
      SpvId u64_type = get_uvec_type(ctx, 64, 1);
      for (unsigned i = 0; i < num_components; i++) {
         constituents[i] = spirv_builder_emit_composite_construct(&ctx->builder, get_uvec_type(ctx, 32, 2), constituents + i * 2, 2);
         constituents[i] = emit_bitcast(ctx, u64_type, constituents[i]);
      }
   }
   /* if loading more than 1 value, reassemble the results into the desired type,
    * otherwise just use the loaded result
    */
   if (num_components > 1) {
      result = spirv_builder_emit_composite_construct(&ctx->builder,
                                                      type,
                                                      constituents,
                                                      num_components);
   } else
      result = constituents[0];

   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static SpvId
create_builtin_var(struct ntv_context *ctx, SpvId var_type,
                   SpvStorageClass storage_class,
                   const char *name, SpvBuiltIn builtin)
{
   SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                   storage_class,
                                                   var_type);
   SpvId var = spirv_builder_emit_var(&ctx->builder, pointer_type,
                                      storage_class);
   spirv_builder_emit_name(&ctx->builder, var, name);
   spirv_builder_emit_builtin(&ctx->builder, var, builtin);

   assert(ctx->num_entry_ifaces < ARRAY_SIZE(ctx->entry_ifaces));
   ctx->entry_ifaces[ctx->num_entry_ifaces++] = var;
   return var;
}

static void
emit_load_front_face(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId var_type = spirv_builder_type_bool(&ctx->builder);
   if (!ctx->front_face_var)
      ctx->front_face_var = create_builtin_var(ctx, var_type,
                                               SpvStorageClassInput,
                                               "gl_FrontFacing",
                                               SpvBuiltInFrontFacing);

   SpvId result = spirv_builder_emit_load(&ctx->builder, var_type,
                                          ctx->front_face_var);
   assert(1 == nir_dest_num_components(intr->dest));
   store_dest(ctx, &intr->dest, result, nir_type_bool);
}

static void
emit_load_uint_input(struct ntv_context *ctx, nir_intrinsic_instr *intr, SpvId *var_id, const char *var_name, SpvBuiltIn builtin)
{
   SpvId var_type = spirv_builder_type_uint(&ctx->builder, 32);
   if (builtin == SpvBuiltInSampleMask) {
      /* gl_SampleMaskIn is an array[1] in spirv... */
      var_type = spirv_builder_type_array(&ctx->builder, var_type, emit_uint_const(ctx, 32, 1));
      spirv_builder_emit_array_stride(&ctx->builder, var_type, sizeof(uint32_t));
   }
   if (!*var_id) {
      *var_id = create_builtin_var(ctx, var_type,
                                   SpvStorageClassInput,
                                   var_name,
                                   builtin);
      if (builtin == SpvBuiltInSampleMask) {
         SpvId zero = emit_uint_const(ctx, 32, 0);
         var_type = spirv_builder_type_uint(&ctx->builder, 32);
         SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                         SpvStorageClassInput,
                                                         var_type);
         *var_id = spirv_builder_emit_access_chain(&ctx->builder, pointer_type, *var_id, &zero, 1);
      }
   }

   SpvId result = spirv_builder_emit_load(&ctx->builder, var_type, *var_id);
   assert(1 == nir_dest_num_components(intr->dest));
   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static void
emit_load_vec_input(struct ntv_context *ctx, nir_intrinsic_instr *intr, SpvId *var_id, const char *var_name, SpvBuiltIn builtin, nir_alu_type type)
{
   SpvId var_type;

   switch (type) {
   case nir_type_bool:
      var_type = get_bvec_type(ctx, nir_dest_num_components(intr->dest));
      break;
   case nir_type_int:
      var_type = get_ivec_type(ctx, nir_dest_bit_size(intr->dest), nir_dest_num_components(intr->dest));
      break;
   case nir_type_uint:
      var_type = get_uvec_type(ctx, nir_dest_bit_size(intr->dest), nir_dest_num_components(intr->dest));
      break;
   case nir_type_float:
      var_type = get_fvec_type(ctx, nir_dest_bit_size(intr->dest), nir_dest_num_components(intr->dest));
      break;
   default:
      unreachable("unknown type passed");
   }
   if (!*var_id)
      *var_id = create_builtin_var(ctx, var_type,
                                   SpvStorageClassInput,
                                   var_name,
                                   builtin);

   SpvId result = spirv_builder_emit_load(&ctx->builder, var_type, *var_id);
   store_dest(ctx, &intr->dest, result, type);
}

static void
emit_intrinsic(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_ubo:
      emit_load_ubo(ctx, intr);
      break;

   case nir_intrinsic_discard:
      emit_discard(ctx, intr);
      break;

   case nir_intrinsic_load_deref:
      emit_load_deref(ctx, intr);
      break;

   case nir_intrinsic_store_deref:
      emit_store_deref(ctx, intr);
      break;

   case nir_intrinsic_load_push_constant:
      emit_load_push_const(ctx, intr);
      break;

   case nir_intrinsic_load_front_face:
      emit_load_front_face(ctx, intr);
      break;

   case nir_intrinsic_load_instance_id:
      emit_load_uint_input(ctx, intr, &ctx->instance_id_var, "gl_InstanceId", SpvBuiltInInstanceIndex);
      break;

   case nir_intrinsic_load_vertex_id:
      emit_load_uint_input(ctx, intr, &ctx->vertex_id_var, "gl_VertexId", SpvBuiltInVertexIndex);
      break;

   case nir_intrinsic_load_primitive_id:
      emit_load_uint_input(ctx, intr, &ctx->primitive_id_var, "gl_PrimitiveIdIn", SpvBuiltInPrimitiveId);
      break;

   case nir_intrinsic_load_invocation_id:
      emit_load_uint_input(ctx, intr, &ctx->invocation_id_var, "gl_InvocationId", SpvBuiltInInvocationId);
      break;

   case nir_intrinsic_load_sample_id:
      emit_load_uint_input(ctx, intr, &ctx->sample_id_var, "gl_SampleId", SpvBuiltInSampleId);
      break;

   case nir_intrinsic_load_sample_pos:
      emit_load_vec_input(ctx, intr, &ctx->sample_pos_var, "gl_SamplePosition", SpvBuiltInSamplePosition, nir_type_float);
      break;

   case nir_intrinsic_emit_vertex_with_counter:
      /* geometry shader emits copied xfb outputs just prior to EmitVertex(),
       * since that's the end of the shader
       */
      if (ctx->so_info)
         emit_so_outputs(ctx, ctx->so_info);
      spirv_builder_emit_vertex(&ctx->builder);
      break;

   case nir_intrinsic_set_vertex_and_primitive_count:
      /* do nothing */
      break;

   case nir_intrinsic_end_primitive_with_counter:
      spirv_builder_end_primitive(&ctx->builder);
      break;

   case nir_intrinsic_load_patch_vertices_in:
      emit_load_vec_input(ctx, intr, &ctx->tess_patch_vertices_in, "gl_PatchVerticesIn",
                          SpvBuiltInPatchVertices, nir_type_int);
      break;

   case nir_intrinsic_load_tess_coord:
      emit_load_vec_input(ctx, intr, &ctx->tess_coord_var, "gl_TessCoord",
                          SpvBuiltInTessCoord, nir_type_float);
      break;

   case nir_intrinsic_memory_barrier_tcs_patch:
      spirv_builder_emit_memory_barrier(&ctx->builder, SpvScopeWorkgroup,
                                        SpvMemorySemanticsOutputMemoryMask | SpvMemorySemanticsReleaseMask);
      break;

   case nir_intrinsic_control_barrier:
      spirv_builder_emit_control_barrier(&ctx->builder, SpvScopeWorkgroup,
                                         SpvScopeWorkgroup,
                                         SpvMemorySemanticsWorkgroupMemoryMask | SpvMemorySemanticsAcquireMask);
      break;

   default:
      fprintf(stderr, "emit_intrinsic: not implemented (%s)\n",
              nir_intrinsic_infos[intr->intrinsic].name);
      unreachable("unsupported intrinsic");
   }
}

static void
emit_undef(struct ntv_context *ctx, nir_ssa_undef_instr *undef)
{
   SpvId type = get_uvec_type(ctx, undef->def.bit_size,
                              undef->def.num_components);

   store_ssa_def(ctx, &undef->def,
                 spirv_builder_emit_undef(&ctx->builder, type));
}

static SpvId
get_src_float(struct ntv_context *ctx, nir_src *src)
{
   SpvId def = get_src(ctx, src);
   unsigned num_components = nir_src_num_components(*src);
   unsigned bit_size = nir_src_bit_size(*src);
   return bitcast_to_fvec(ctx, def, bit_size, num_components);
}

static SpvId
get_src_int(struct ntv_context *ctx, nir_src *src)
{
   SpvId def = get_src(ctx, src);
   unsigned num_components = nir_src_num_components(*src);
   unsigned bit_size = nir_src_bit_size(*src);
   return bitcast_to_ivec(ctx, def, bit_size, num_components);
}

static inline bool
tex_instr_is_lod_allowed(nir_tex_instr *tex)
{
   /* This can only be used with an OpTypeImage that has a Dim operand of 1D, 2D, 3D, or Cube
    * - SPIR-V: 3.14. Image Operands
    */

   return (tex->sampler_dim == GLSL_SAMPLER_DIM_1D ||
           tex->sampler_dim == GLSL_SAMPLER_DIM_2D ||
           tex->sampler_dim == GLSL_SAMPLER_DIM_3D ||
           tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE);
}

static SpvId
pad_coord_vector(struct ntv_context *ctx, SpvId orig, unsigned old_size, unsigned new_size)
{
    SpvId int_type = spirv_builder_type_int(&ctx->builder, 32);
    SpvId type = get_ivec_type(ctx, 32, new_size);
    SpvId constituents[NIR_MAX_VEC_COMPONENTS] = {0};
    SpvId zero = emit_int_const(ctx, 32, 0);
    assert(new_size < NIR_MAX_VEC_COMPONENTS);

    if (old_size == 1)
       constituents[0] = orig;
    else {
       for (unsigned i = 0; i < old_size; i++)
          constituents[i] = spirv_builder_emit_vector_extract(&ctx->builder, int_type, orig, i);
    }

    for (unsigned i = old_size; i < new_size; i++)
       constituents[i] = zero;

    return spirv_builder_emit_composite_construct(&ctx->builder, type,
                                                  constituents, new_size);
}

static void
emit_tex(struct ntv_context *ctx, nir_tex_instr *tex)
{
   assert(tex->op == nir_texop_tex ||
          tex->op == nir_texop_txb ||
          tex->op == nir_texop_txl ||
          tex->op == nir_texop_txd ||
          tex->op == nir_texop_txf ||
          tex->op == nir_texop_txf_ms ||
          tex->op == nir_texop_txs ||
          tex->op == nir_texop_lod);
   assert(tex->texture_index == tex->sampler_index);

   SpvId coord = 0, proj = 0, bias = 0, lod = 0, dref = 0, dx = 0, dy = 0,
         offset = 0, sample = 0;
   unsigned coord_components = 0, coord_bitsize = 0, offset_components = 0;
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_coord:
         if (tex->op == nir_texop_txf ||
             tex->op == nir_texop_txf_ms)
            coord = get_src_int(ctx, &tex->src[i].src);
         else
            coord = get_src_float(ctx, &tex->src[i].src);
         coord_components = nir_src_num_components(tex->src[i].src);
         coord_bitsize = nir_src_bit_size(tex->src[i].src);
         break;

      case nir_tex_src_projector:
         assert(nir_src_num_components(tex->src[i].src) == 1);
         proj = get_src_float(ctx, &tex->src[i].src);
         assert(proj != 0);
         break;

      case nir_tex_src_offset:
         offset = get_src_int(ctx, &tex->src[i].src);
         offset_components = nir_src_num_components(tex->src[i].src);
         break;

      case nir_tex_src_bias:
         assert(tex->op == nir_texop_txb);
         bias = get_src_float(ctx, &tex->src[i].src);
         assert(bias != 0);
         break;

      case nir_tex_src_lod:
         assert(nir_src_num_components(tex->src[i].src) == 1);
         if (tex->op == nir_texop_txf ||
             tex->op == nir_texop_txf_ms ||
             tex->op == nir_texop_txs)
            lod = get_src_int(ctx, &tex->src[i].src);
         else
            lod = get_src_float(ctx, &tex->src[i].src);
         assert(lod != 0);
         break;

      case nir_tex_src_ms_index:
         assert(nir_src_num_components(tex->src[i].src) == 1);
         sample = get_src_int(ctx, &tex->src[i].src);
         break;

      case nir_tex_src_comparator:
         assert(nir_src_num_components(tex->src[i].src) == 1);
         dref = get_src_float(ctx, &tex->src[i].src);
         assert(dref != 0);
         break;

      case nir_tex_src_ddx:
         dx = get_src_float(ctx, &tex->src[i].src);
         assert(dx != 0);
         break;

      case nir_tex_src_ddy:
         dy = get_src_float(ctx, &tex->src[i].src);
         assert(dy != 0);
         break;

      default:
         fprintf(stderr, "texture source: %d\n", tex->src[i].src_type);
         unreachable("unknown texture source");
      }
   }

   if (lod == 0 && ctx->stage != MESA_SHADER_FRAGMENT) {
      lod = emit_float_const(ctx, 32, 0.0f);
      assert(lod != 0);
   }

   SpvId image_type = ctx->image_types[tex->texture_index];
   SpvId sampled_type = spirv_builder_type_sampled_image(&ctx->builder,
                                                         image_type);

   assert(ctx->samplers_used & (1u << tex->texture_index));
   SpvId load = spirv_builder_emit_load(&ctx->builder, sampled_type,
                                        ctx->samplers[tex->texture_index]);

   SpvId dest_type = get_dest_type(ctx, &tex->dest, tex->dest_type);

   if (!tex_instr_is_lod_allowed(tex))
      lod = 0;
   if (tex->op == nir_texop_txs) {
      SpvId image = spirv_builder_emit_image(&ctx->builder, image_type, load);
      SpvId result = spirv_builder_emit_image_query_size(&ctx->builder,
                                                         dest_type, image,
                                                         lod);
      store_dest(ctx, &tex->dest, result, tex->dest_type);
      return;
   }

   if (proj && coord_components > 0) {
      SpvId constituents[coord_components + 1];
      if (coord_components == 1)
         constituents[0] = coord;
      else {
         assert(coord_components > 1);
         SpvId float_type = spirv_builder_type_float(&ctx->builder, 32);
         for (uint32_t i = 0; i < coord_components; ++i)
            constituents[i] = spirv_builder_emit_composite_extract(&ctx->builder,
                                                 float_type,
                                                 coord,
                                                 &i, 1);
      }

      constituents[coord_components++] = proj;

      SpvId vec_type = get_fvec_type(ctx, 32, coord_components);
      coord = spirv_builder_emit_composite_construct(&ctx->builder,
                                                            vec_type,
                                                            constituents,
                                                            coord_components);
   }
   if (tex->op == nir_texop_lod) {
      SpvId result = spirv_builder_emit_image_query_lod(&ctx->builder,
                                                         dest_type, load,
                                                         coord);
      store_dest(ctx, &tex->dest, result, tex->dest_type);
      return;
   }
   SpvId actual_dest_type = dest_type;
   if (dref)
      actual_dest_type = spirv_builder_type_float(&ctx->builder, 32);

   SpvId result;
   if (tex->op == nir_texop_txf ||
       tex->op == nir_texop_txf_ms) {
      SpvId image = spirv_builder_emit_image(&ctx->builder, image_type, load);
      if (offset) {
         /* SPIRV requires matched length vectors for OpIAdd, so if a shader
          * uses vecs of differing sizes we need to make a new vec padded with zeroes
          * to mimic how GLSL does this implicitly
          */
         if (offset_components > coord_components)
            coord = pad_coord_vector(ctx, coord, coord_components, offset_components);
         else if (coord_components > offset_components)
            offset = pad_coord_vector(ctx, offset, offset_components, coord_components);
         coord = emit_binop(ctx, SpvOpIAdd,
                            get_ivec_type(ctx, coord_bitsize, coord_components),
                            coord, offset);
      }
      result = spirv_builder_emit_image_fetch(&ctx->builder, dest_type,
                                              image, coord, lod, sample);
   } else {
      result = spirv_builder_emit_image_sample(&ctx->builder,
                                               actual_dest_type, load,
                                               coord,
                                               proj != 0,
                                               lod, bias, dref, dx, dy,
                                               offset);
   }

   spirv_builder_emit_decoration(&ctx->builder, result,
                                 SpvDecorationRelaxedPrecision);

   if (dref && nir_dest_num_components(tex->dest) > 1) {
      SpvId components[4] = { result, result, result, result };
      result = spirv_builder_emit_composite_construct(&ctx->builder,
                                                      dest_type,
                                                      components,
                                                      4);
   }

   store_dest(ctx, &tex->dest, result, tex->dest_type);
}

static void
start_block(struct ntv_context *ctx, SpvId label)
{
   /* terminate previous block if needed */
   if (ctx->block_started)
      spirv_builder_emit_branch(&ctx->builder, label);

   /* start new block */
   spirv_builder_label(&ctx->builder, label);
   ctx->block_started = true;
}

static void
branch(struct ntv_context *ctx, SpvId label)
{
   assert(ctx->block_started);
   spirv_builder_emit_branch(&ctx->builder, label);
   ctx->block_started = false;
}

static void
branch_conditional(struct ntv_context *ctx, SpvId condition, SpvId then_id,
                   SpvId else_id)
{
   assert(ctx->block_started);
   spirv_builder_emit_branch_conditional(&ctx->builder, condition,
                                         then_id, else_id);
   ctx->block_started = false;
}

static void
emit_jump(struct ntv_context *ctx, nir_jump_instr *jump)
{
   switch (jump->type) {
   case nir_jump_break:
      assert(ctx->loop_break);
      branch(ctx, ctx->loop_break);
      break;

   case nir_jump_continue:
      assert(ctx->loop_cont);
      branch(ctx, ctx->loop_cont);
      break;

   default:
      unreachable("Unsupported jump type\n");
   }
}

static void
emit_deref_var(struct ntv_context *ctx, nir_deref_instr *deref)
{
   assert(deref->deref_type == nir_deref_type_var);

   struct hash_entry *he = _mesa_hash_table_search(ctx->vars, deref->var);
   assert(he);
   SpvId result = (SpvId)(intptr_t)he->data;
   store_dest_raw(ctx, &deref->dest, result);
}

static void
emit_deref_array(struct ntv_context *ctx, nir_deref_instr *deref)
{
   assert(deref->deref_type == nir_deref_type_array);
   nir_variable *var = nir_deref_instr_get_variable(deref);

   SpvStorageClass storage_class = get_storage_class(var);

   SpvId index = get_src(ctx, &deref->arr.index);

   SpvId ptr_type = spirv_builder_type_pointer(&ctx->builder,
                                               storage_class,
                                               get_glsl_type(ctx, deref->type));

   SpvId result = spirv_builder_emit_access_chain(&ctx->builder,
                                                  ptr_type,
                                                  get_src(ctx, &deref->parent),
                                                  &index, 1);
   /* uint is a bit of a lie here, it's really just an opaque type */
   store_dest(ctx, &deref->dest, result, nir_type_uint);
}

static void
emit_deref_struct(struct ntv_context *ctx, nir_deref_instr *deref)
{
   assert(deref->deref_type == nir_deref_type_struct);
   nir_variable *var = nir_deref_instr_get_variable(deref);

   SpvStorageClass storage_class = get_storage_class(var);

   SpvId index = emit_uint_const(ctx, 32, deref->strct.index);

   SpvId ptr_type = spirv_builder_type_pointer(&ctx->builder,
                                               storage_class,
                                               get_glsl_type(ctx, deref->type));

   SpvId result = spirv_builder_emit_access_chain(&ctx->builder,
                                                  ptr_type,
                                                  get_src(ctx, &deref->parent),
                                                  &index, 1);
   /* uint is a bit of a lie here, it's really just an opaque type */
   store_dest(ctx, &deref->dest, result, nir_type_uint);
}

static void
emit_deref(struct ntv_context *ctx, nir_deref_instr *deref)
{
   switch (deref->deref_type) {
   case nir_deref_type_var:
      emit_deref_var(ctx, deref);
      break;

   case nir_deref_type_array:
      emit_deref_array(ctx, deref);
      break;

   case nir_deref_type_struct:
      emit_deref_struct(ctx, deref);
      break;

   default:
      unreachable("unexpected deref_type");
   }
}

static void
emit_block(struct ntv_context *ctx, struct nir_block *block)
{
   start_block(ctx, block_label(ctx, block));
   nir_foreach_instr(instr, block) {
      switch (instr->type) {
      case nir_instr_type_alu:
         emit_alu(ctx, nir_instr_as_alu(instr));
         break;
      case nir_instr_type_intrinsic:
         emit_intrinsic(ctx, nir_instr_as_intrinsic(instr));
         break;
      case nir_instr_type_load_const:
         emit_load_const(ctx, nir_instr_as_load_const(instr));
         break;
      case nir_instr_type_ssa_undef:
         emit_undef(ctx, nir_instr_as_ssa_undef(instr));
         break;
      case nir_instr_type_tex:
         emit_tex(ctx, nir_instr_as_tex(instr));
         break;
      case nir_instr_type_phi:
         unreachable("nir_instr_type_phi not supported");
         break;
      case nir_instr_type_jump:
         emit_jump(ctx, nir_instr_as_jump(instr));
         break;
      case nir_instr_type_call:
         unreachable("nir_instr_type_call not supported");
         break;
      case nir_instr_type_parallel_copy:
         unreachable("nir_instr_type_parallel_copy not supported");
         break;
      case nir_instr_type_deref:
         emit_deref(ctx, nir_instr_as_deref(instr));
         break;
      }
   }
}

static void
emit_cf_list(struct ntv_context *ctx, struct exec_list *list);

static SpvId
get_src_bool(struct ntv_context *ctx, nir_src *src)
{
   assert(nir_src_bit_size(*src) == 1);
   return get_src(ctx, src);
}

static void
emit_if(struct ntv_context *ctx, nir_if *if_stmt)
{
   SpvId condition = get_src_bool(ctx, &if_stmt->condition);

   SpvId header_id = spirv_builder_new_id(&ctx->builder);
   SpvId then_id = block_label(ctx, nir_if_first_then_block(if_stmt));
   SpvId endif_id = spirv_builder_new_id(&ctx->builder);
   SpvId else_id = endif_id;

   bool has_else = !exec_list_is_empty(&if_stmt->else_list);
   if (has_else) {
      assert(nir_if_first_else_block(if_stmt)->index < ctx->num_blocks);
      else_id = block_label(ctx, nir_if_first_else_block(if_stmt));
   }

   /* create a header-block */
   start_block(ctx, header_id);
   spirv_builder_emit_selection_merge(&ctx->builder, endif_id,
                                      SpvSelectionControlMaskNone);
   branch_conditional(ctx, condition, then_id, else_id);

   emit_cf_list(ctx, &if_stmt->then_list);

   if (has_else) {
      if (ctx->block_started)
         branch(ctx, endif_id);

      emit_cf_list(ctx, &if_stmt->else_list);
   }

   start_block(ctx, endif_id);
}

static void
emit_loop(struct ntv_context *ctx, nir_loop *loop)
{
   SpvId header_id = spirv_builder_new_id(&ctx->builder);
   SpvId begin_id = block_label(ctx, nir_loop_first_block(loop));
   SpvId break_id = spirv_builder_new_id(&ctx->builder);
   SpvId cont_id = spirv_builder_new_id(&ctx->builder);

   /* create a header-block */
   start_block(ctx, header_id);
   spirv_builder_loop_merge(&ctx->builder, break_id, cont_id, SpvLoopControlMaskNone);
   branch(ctx, begin_id);

   SpvId save_break = ctx->loop_break;
   SpvId save_cont = ctx->loop_cont;
   ctx->loop_break = break_id;
   ctx->loop_cont = cont_id;

   emit_cf_list(ctx, &loop->body);

   ctx->loop_break = save_break;
   ctx->loop_cont = save_cont;

   /* loop->body may have already ended our block */
   if (ctx->block_started)
      branch(ctx, cont_id);
   start_block(ctx, cont_id);
   branch(ctx, header_id);

   start_block(ctx, break_id);
}

static void
emit_cf_list(struct ntv_context *ctx, struct exec_list *list)
{
   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block:
         emit_block(ctx, nir_cf_node_as_block(node));
         break;

      case nir_cf_node_if:
         emit_if(ctx, nir_cf_node_as_if(node));
         break;

      case nir_cf_node_loop:
         emit_loop(ctx, nir_cf_node_as_loop(node));
         break;

      case nir_cf_node_function:
         unreachable("nir_cf_node_function not supported");
         break;
      }
   }
}

static SpvExecutionMode
get_input_prim_type_mode(uint16_t type)
{
   switch (type) {
   case GL_POINTS:
      return SpvExecutionModeInputPoints;
   case GL_LINES:
   case GL_LINE_LOOP:
   case GL_LINE_STRIP:
      return SpvExecutionModeInputLines;
   case GL_TRIANGLE_STRIP:
   case GL_TRIANGLES:
   case GL_TRIANGLE_FAN:
      return SpvExecutionModeTriangles;
   case GL_QUADS:
   case GL_QUAD_STRIP:
      return SpvExecutionModeQuads;
      break;
   case GL_POLYGON:
      unreachable("handle polygons in gs");
      break;
   case GL_LINES_ADJACENCY:
   case GL_LINE_STRIP_ADJACENCY:
      return SpvExecutionModeInputLinesAdjacency;
   case GL_TRIANGLES_ADJACENCY:
   case GL_TRIANGLE_STRIP_ADJACENCY:
      return SpvExecutionModeInputTrianglesAdjacency;
      break;
   case GL_ISOLINES:
      return SpvExecutionModeIsolines;
   default:
      debug_printf("unknown geometry shader input mode %u\n", type);
      unreachable("error!");
      break;
   }

   return 0;
}
static SpvExecutionMode
get_output_prim_type_mode(uint16_t type)
{
   switch (type) {
   case GL_POINTS:
      return SpvExecutionModeOutputPoints;
   case GL_LINES:
   case GL_LINE_LOOP:
      unreachable("GL_LINES/LINE_LOOP passed as gs output");
      break;
   case GL_LINE_STRIP:
      return SpvExecutionModeOutputLineStrip;
   case GL_TRIANGLE_STRIP:
      return SpvExecutionModeOutputTriangleStrip;
   case GL_TRIANGLES:
   case GL_TRIANGLE_FAN: //FIXME: not sure if right for output
      return SpvExecutionModeTriangles;
   case GL_QUADS:
   case GL_QUAD_STRIP:
      return SpvExecutionModeQuads;
   case GL_POLYGON:
      unreachable("handle polygons in gs");
      break;
   case GL_LINES_ADJACENCY:
   case GL_LINE_STRIP_ADJACENCY:
      unreachable("handle line adjacency in gs");
      break;
   case GL_TRIANGLES_ADJACENCY:
   case GL_TRIANGLE_STRIP_ADJACENCY:
      unreachable("handle triangle adjacency in gs");
      break;
   case GL_ISOLINES:
      return SpvExecutionModeIsolines;
   default:
      debug_printf("unknown geometry shader output mode %u\n", type);
      unreachable("error!");
      break;
   }

   return 0;
}

struct spirv_shader *
nir_to_spirv(struct nir_shader *s, const struct zink_so_info *so_info,
             unsigned char *shader_slot_map, unsigned char *shader_slots_reserved)
{
   struct spirv_shader *ret = NULL;

   struct ntv_context ctx = {};
   ctx.mem_ctx = ralloc_context(NULL);
   ctx.builder.mem_ctx = ctx.mem_ctx;

   switch (s->info.stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_FRAGMENT:
   case MESA_SHADER_COMPUTE:
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityShader);
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityImageBuffer);
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilitySampledBuffer);
      break;

   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_TESS_EVAL:
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityTessellation);
      /* TODO: check features for this */
      if (s->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_PSIZ))
         spirv_builder_emit_cap(&ctx.builder, SpvCapabilityTessellationPointSize);
      break;

   case MESA_SHADER_GEOMETRY:
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityGeometry);
      if (s->info.gs.active_stream_mask)
         spirv_builder_emit_cap(&ctx.builder, SpvCapabilityGeometryStreams);
      if (s->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_PSIZ))
         spirv_builder_emit_cap(&ctx.builder, SpvCapabilityGeometryPointSize);
      break;

   default:
      unreachable("invalid stage");
   }

   if (s->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_VIEWPORT)) {
      if (s->info.stage < MESA_SHADER_GEOMETRY)
         spirv_builder_emit_cap(&ctx.builder, SpvCapabilityShaderViewportIndex);
      else
         spirv_builder_emit_cap(&ctx.builder, SpvCapabilityMultiViewport);
   }

   // TODO: only enable when needed
   if (s->info.stage == MESA_SHADER_FRAGMENT) {
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilitySampled1D);
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityImageQuery);
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityDerivativeControl);
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilitySampleRateShading);
   }
   if (s->info.bit_sizes_int & 64)
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityInt64);
   if (s->info.bit_sizes_float & 64)
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityFloat64);

   ctx.stage = s->info.stage;
   ctx.so_info = so_info;
   ctx.shader_slot_map = shader_slot_map;
   ctx.shader_slots_reserved = *shader_slots_reserved;
   ctx.GLSL_std_450 = spirv_builder_import(&ctx.builder, "GLSL.std.450");
   spirv_builder_emit_source(&ctx.builder, SpvSourceLanguageGLSL, 450);

   if (s->info.stage == MESA_SHADER_TESS_CTRL) {
      /* this is required for correct barrier and io semantics */
      spirv_builder_emit_extension(&ctx.builder, "SPV_KHR_vulkan_memory_model");
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityVulkanMemoryModel);
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityVulkanMemoryModelDeviceScope);
      spirv_builder_emit_mem_model(&ctx.builder, SpvAddressingModelLogical,
                                   SpvMemoryModelVulkan);
   } else
      spirv_builder_emit_mem_model(&ctx.builder, SpvAddressingModelLogical,
                                   SpvMemoryModelGLSL450);

   SpvExecutionModel exec_model;
   switch (s->info.stage) {
   case MESA_SHADER_VERTEX:
      exec_model = SpvExecutionModelVertex;
      break;
   case MESA_SHADER_TESS_CTRL:
      exec_model = SpvExecutionModelTessellationControl;
      break;
   case MESA_SHADER_TESS_EVAL:
      exec_model = SpvExecutionModelTessellationEvaluation;
      break;
   case MESA_SHADER_GEOMETRY:
      exec_model = SpvExecutionModelGeometry;
      break;
   case MESA_SHADER_FRAGMENT:
      exec_model = SpvExecutionModelFragment;
      break;
   case MESA_SHADER_COMPUTE:
      exec_model = SpvExecutionModelGLCompute;
      break;
   default:
      unreachable("invalid stage");
   }

   SpvId type_void = spirv_builder_type_void(&ctx.builder);
   SpvId type_main = spirv_builder_type_function(&ctx.builder, type_void,
                                                 NULL, 0);
   SpvId entry_point = spirv_builder_new_id(&ctx.builder);
   spirv_builder_emit_name(&ctx.builder, entry_point, "main");

   ctx.vars = _mesa_hash_table_create(ctx.mem_ctx, _mesa_hash_pointer,
                                      _mesa_key_pointer_equal);

   ctx.so_outputs = _mesa_hash_table_create(ctx.mem_ctx, _mesa_hash_u32,
                                            _mesa_key_u32_equal);

   nir_foreach_variable_with_modes(var, s, nir_var_mem_push_const)
      input_var_init(&ctx, var);

   nir_foreach_shader_in_variable(var, s)
      emit_input(&ctx, var);

   nir_foreach_shader_out_variable(var, s)
      emit_output(&ctx, var);


   if (so_info)
      emit_so_info(&ctx, so_info);
   /* we have to reverse iterate to match what's done in zink_compiler.c */
   foreach_list_typed_reverse(nir_variable, var, node, &s->variables)
      if (_nir_shader_variable_has_mode(var, nir_var_uniform |
                                        nir_var_mem_ubo |
                                        nir_var_mem_ssbo))
         emit_uniform(&ctx, var);

   switch (s->info.stage) {
   case MESA_SHADER_FRAGMENT:
      spirv_builder_emit_exec_mode(&ctx.builder, entry_point,
                                   SpvExecutionModeOriginUpperLeft);
      if (s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH))
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point,
                                      SpvExecutionModeDepthReplacing);
      break;
   case MESA_SHADER_TESS_CTRL:
      spirv_builder_emit_exec_mode_literal(&ctx.builder, entry_point, SpvExecutionModeOutputVertices, s->info.tess.tcs_vertices_out);
      break;
   case MESA_SHADER_TESS_EVAL:
      switch (s->info.tess.primitive_mode) {
      case GL_TRIANGLES:
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModeTriangles);
         break;
      case GL_QUADS:
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModeQuads);
         break;
      case GL_ISOLINES:
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModeIsolines);
         break;
      default:
         unreachable("unknown tess prim type!");
      }
      if (s->info.tess.ccw)
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModeVertexOrderCcw);
      else
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModeVertexOrderCw);
      switch (s->info.tess.spacing) {
      case TESS_SPACING_EQUAL:
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModeSpacingEqual);
         break;
      case TESS_SPACING_FRACTIONAL_ODD:
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModeSpacingFractionalOdd);
         break;
      case TESS_SPACING_FRACTIONAL_EVEN:
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModeSpacingFractionalEven);
         break;
      default:
         unreachable("unknown tess spacing!");
      }
      if (s->info.tess.point_mode)
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModePointMode);
      break;
   case MESA_SHADER_GEOMETRY:
      spirv_builder_emit_exec_mode(&ctx.builder, entry_point, get_input_prim_type_mode(s->info.gs.input_primitive));
      spirv_builder_emit_exec_mode(&ctx.builder, entry_point, get_output_prim_type_mode(s->info.gs.output_primitive));
      spirv_builder_emit_exec_mode_literal(&ctx.builder, entry_point, SpvExecutionModeInvocations, s->info.gs.invocations);
      spirv_builder_emit_exec_mode_literal(&ctx.builder, entry_point, SpvExecutionModeOutputVertices, s->info.gs.vertices_out);
      break;
   default:
      break;
   }
   if (so_info && so_info->so_info.num_outputs) {
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityTransformFeedback);
      spirv_builder_emit_exec_mode(&ctx.builder, entry_point,
                                   SpvExecutionModeXfb);
   }
   spirv_builder_function(&ctx.builder, entry_point, type_void,
                                            SpvFunctionControlMaskNone,
                                            type_main);

   nir_function_impl *entry = nir_shader_get_entrypoint(s);
   nir_metadata_require(entry, nir_metadata_block_index);

   ctx.defs = ralloc_array_size(ctx.mem_ctx,
                                sizeof(SpvId), entry->ssa_alloc);
   if (!ctx.defs)
      goto fail;
   ctx.num_defs = entry->ssa_alloc;

   nir_index_local_regs(entry);
   ctx.regs = ralloc_array_size(ctx.mem_ctx,
                                sizeof(SpvId), entry->reg_alloc);
   if (!ctx.regs)
      goto fail;
   ctx.num_regs = entry->reg_alloc;

   SpvId *block_ids = ralloc_array_size(ctx.mem_ctx,
                                        sizeof(SpvId), entry->num_blocks);
   if (!block_ids)
      goto fail;

   for (int i = 0; i < entry->num_blocks; ++i)
      block_ids[i] = spirv_builder_new_id(&ctx.builder);

   ctx.block_ids = block_ids;
   ctx.num_blocks = entry->num_blocks;

   /* emit a block only for the variable declarations */
   start_block(&ctx, spirv_builder_new_id(&ctx.builder));
   foreach_list_typed(nir_register, reg, node, &entry->registers) {
      SpvId type = get_vec_from_bit_size(&ctx, reg->bit_size, reg->num_components);
      SpvId pointer_type = spirv_builder_type_pointer(&ctx.builder,
                                                      SpvStorageClassFunction,
                                                      type);
      SpvId var = spirv_builder_emit_var(&ctx.builder, pointer_type,
                                         SpvStorageClassFunction);

      ctx.regs[reg->index] = var;
   }

   emit_cf_list(&ctx, &entry->body);

   /* vertex shader emits copied xfb outputs at the end of the shader */
   if (so_info && ctx.stage == MESA_SHADER_VERTEX)
      emit_so_outputs(&ctx, so_info);

   spirv_builder_return(&ctx.builder); // doesn't belong here, but whatevz
   spirv_builder_function_end(&ctx.builder);

   spirv_builder_emit_entry_point(&ctx.builder, exec_model, entry_point,
                                  "main", ctx.entry_ifaces,
                                  ctx.num_entry_ifaces);

   size_t num_words = spirv_builder_get_num_words(&ctx.builder);

   ret = ralloc(NULL, struct spirv_shader);
   if (!ret)
      goto fail;

   ret->words = ralloc_size(ret, sizeof(uint32_t) * num_words);
   if (!ret->words)
      goto fail;

   ret->num_words = spirv_builder_get_words(&ctx.builder, ret->words, num_words);
   assert(ret->num_words == num_words);

   ralloc_free(ctx.mem_ctx);
   *shader_slots_reserved = ctx.shader_slots_reserved;

   return ret;

fail:
   ralloc_free(ctx.mem_ctx);

   if (ret)
      spirv_shader_delete(ret);

   return NULL;
}

void
spirv_shader_delete(struct spirv_shader *s)
{
   FREE(s->words);
   FREE(s);
}
