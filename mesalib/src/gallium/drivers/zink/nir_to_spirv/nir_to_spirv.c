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
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/hash_table.h"

#define SLOT_UNSET ((unsigned char) -1)

struct ntv_context {
   void *mem_ctx;

   /* SPIR-V 1.4 and later requires entrypoints to list all global
    * variables in the interface.
    */
   bool spirv_1_4_interfaces;

   bool explicit_lod; //whether to set lod=0 for texture()

   struct spirv_builder builder;

   struct hash_table *glsl_types;
   struct hash_table *bo_types;

   SpvId GLSL_std_450;

   gl_shader_stage stage;
   const struct zink_shader_info *sinfo;

   SpvId ubos[PIPE_MAX_CONSTANT_BUFFERS][5]; //8, 16, 32, unused, 64
   nir_variable *ubo_vars[PIPE_MAX_CONSTANT_BUFFERS];

   SpvId ssbos[PIPE_MAX_SHADER_BUFFERS][5]; //8, 16, 32, unused, 64
   nir_variable *ssbo_vars[PIPE_MAX_SHADER_BUFFERS];
   SpvId image_types[PIPE_MAX_SAMPLERS];
   SpvId images[PIPE_MAX_SAMPLERS];
   SpvId sampler_types[PIPE_MAX_SAMPLERS];
   SpvId samplers[PIPE_MAX_SAMPLERS];
   unsigned char sampler_array_sizes[PIPE_MAX_SAMPLERS];
   unsigned samplers_used : PIPE_MAX_SAMPLERS;
   SpvId entry_ifaces[PIPE_MAX_SHADER_INPUTS * 4 + PIPE_MAX_SHADER_OUTPUTS * 4];
   size_t num_entry_ifaces;

   SpvId *defs;
   SpvId *resident_defs;
   size_t num_defs;

   SpvId *regs;
   size_t num_regs;

   struct hash_table *vars; /* nir_variable -> SpvId */
   struct hash_table *image_vars; /* SpvId -> nir_variable */
   struct hash_table *so_outputs; /* pipe_stream_output -> SpvId */
   unsigned outputs[VARYING_SLOT_MAX * 4];
   const struct glsl_type *so_output_gl_types[VARYING_SLOT_MAX * 4];
   SpvId so_output_types[VARYING_SLOT_MAX * 4];

   const SpvId *block_ids;
   size_t num_blocks;
   bool block_started;
   SpvId loop_break, loop_cont;

   SpvId front_face_var, instance_id_var, vertex_id_var,
         primitive_id_var, invocation_id_var, // geometry
         sample_mask_type, sample_id_var, sample_pos_var, sample_mask_in_var,
         tess_patch_vertices_in, tess_coord_var, // tess
         push_const_var,
         workgroup_id_var, num_workgroups_var,
         local_invocation_id_var, global_invocation_id_var,
         local_invocation_index_var, helper_invocation_var,
         local_group_size_var,
         shared_block_var,
         base_vertex_var, base_instance_var, draw_id_var;

   SpvId subgroup_eq_mask_var,
         subgroup_ge_mask_var,
         subgroup_gt_mask_var,
         subgroup_id_var,
         subgroup_invocation_var,
         subgroup_le_mask_var,
         subgroup_lt_mask_var,
         subgroup_size_var;
};

static SpvId
get_fvec_constant(struct ntv_context *ctx, unsigned bit_size,
                  unsigned num_components, double value);

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

static SpvScope
get_scope(nir_scope scope)
{
   SpvScope conv[] = {
      [NIR_SCOPE_NONE] = 0,
      [NIR_SCOPE_INVOCATION] = SpvScopeInvocation,
      [NIR_SCOPE_SUBGROUP] = SpvScopeSubgroup,
      [NIR_SCOPE_SHADER_CALL] = SpvScopeShaderCallKHR,
      [NIR_SCOPE_WORKGROUP] = SpvScopeWorkgroup,
      [NIR_SCOPE_QUEUE_FAMILY] = SpvScopeQueueFamily,
      [NIR_SCOPE_DEVICE] = SpvScopeDevice,
   };
   return conv[scope];
}

static SpvId
block_label(struct ntv_context *ctx, nir_block *block)
{
   assert(block->index < ctx->num_blocks);
   return ctx->block_ids[block->index];
}

static void
emit_access_decorations(struct ntv_context *ctx, nir_variable *var, SpvId var_id)
{
    unsigned access = var->data.access;
    while (access) {
       unsigned bit = u_bit_scan(&access);
       switch (1 << bit) {
       case ACCESS_COHERENT:
          /* SpvDecorationCoherent can't be used with vulkan memory model */
          break;
       case ACCESS_RESTRICT:
          spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationRestrict);
          break;
       case ACCESS_VOLATILE:
          /* SpvDecorationVolatile can't be used with vulkan memory model */
          break;
       case ACCESS_NON_READABLE:
          spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationNonReadable);
          break;
       case ACCESS_NON_WRITEABLE:
          spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationNonWritable);
          break;
       case ACCESS_NON_UNIFORM:
          spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationNonUniform);
          break;
       case ACCESS_CAN_REORDER:
       case ACCESS_STREAM_CACHE_POLICY:
          /* no equivalent */
          break;
       default:
          unreachable("unknown access bit");
       }
    }
}

static SpvOp
get_atomic_op(nir_intrinsic_op op)
{
   switch (op) {
#define CASE_ATOMIC_OP(type) \
   case nir_intrinsic_ssbo_atomic_##type: \
   case nir_intrinsic_image_deref_atomic_##type: \
   case nir_intrinsic_shared_atomic_##type

   CASE_ATOMIC_OP(add):
      return SpvOpAtomicIAdd;
   CASE_ATOMIC_OP(umin):
      return SpvOpAtomicUMin;
   CASE_ATOMIC_OP(imin):
      return SpvOpAtomicSMin;
   CASE_ATOMIC_OP(umax):
      return SpvOpAtomicUMax;
   CASE_ATOMIC_OP(imax):
      return SpvOpAtomicSMax;
   CASE_ATOMIC_OP(and):
      return SpvOpAtomicAnd;
   CASE_ATOMIC_OP(or):
      return SpvOpAtomicOr;
   CASE_ATOMIC_OP(xor):
      return SpvOpAtomicXor;
   CASE_ATOMIC_OP(exchange):
      return SpvOpAtomicExchange;
   CASE_ATOMIC_OP(comp_swap):
      return SpvOpAtomicCompareExchange;
   default:
      debug_printf("%s - ", nir_intrinsic_infos[op].name);
      unreachable("unhandled atomic op");
   }
   return 0;
}
#undef CASE_ATOMIC_OP
static SpvId
emit_float_const(struct ntv_context *ctx, int bit_size, double value)
{
   assert(bit_size == 16 || bit_size == 32 || bit_size == 64);
   return spirv_builder_const_float(&ctx->builder, bit_size, value);
}

static SpvId
emit_uint_const(struct ntv_context *ctx, int bit_size, uint64_t value)
{
   assert(bit_size == 8 || bit_size == 16 || bit_size == 32 || bit_size == 64);
   return spirv_builder_const_uint(&ctx->builder, bit_size, value);
}

static SpvId
emit_int_const(struct ntv_context *ctx, int bit_size, int64_t value)
{
   assert(bit_size == 8 || bit_size == 16 || bit_size == 32 || bit_size == 64);
   return spirv_builder_const_int(&ctx->builder, bit_size, value);
}

static SpvId
get_fvec_type(struct ntv_context *ctx, unsigned bit_size, unsigned num_components)
{
   assert(bit_size == 16 || bit_size == 32 || bit_size == 64);

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
   assert(bit_size == 8 || bit_size == 16 || bit_size == 32 || bit_size == 64);

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
   assert(bit_size == 8 || bit_size == 16 || bit_size == 32 || bit_size == 64);

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
   case nir_var_uniform:
   case nir_var_image:
      return SpvStorageClassUniformConstant;
   case nir_var_mem_ubo:
      return SpvStorageClassUniform;
   case nir_var_mem_ssbo:
      return SpvStorageClassStorageBuffer;
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

   case GLSL_TYPE_FLOAT16:
      return spirv_builder_type_float(&ctx->builder, 16);

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

   case GLSL_TYPE_UINT16:
      return spirv_builder_type_uint(&ctx->builder, 16);
   case GLSL_TYPE_INT16:
      return spirv_builder_type_int(&ctx->builder, 16);
   case GLSL_TYPE_INT8:
      return spirv_builder_type_int(&ctx->builder, 8);
   case GLSL_TYPE_UINT8:
      return spirv_builder_type_uint(&ctx->builder, 8);
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

   if (glsl_type_is_matrix(type))
      return spirv_builder_type_matrix(&ctx->builder,
                                       spirv_builder_type_vector(&ctx->builder,
                                                                 get_glsl_basetype(ctx, glsl_get_base_type(type)),
                                                                 glsl_get_vector_elements(type)),
                                       glsl_get_matrix_columns(type));

   /* Aggregate types aren't cached in spirv_builder, so let's cache
    * them here instead.
    */

   struct hash_entry *entry =
      _mesa_hash_table_search(ctx->glsl_types, type);
   if (entry)
      return (SpvId)(uintptr_t)entry->data;

   SpvId ret;
   if (glsl_type_is_array(type)) {
      SpvId element_type = get_glsl_type(ctx, glsl_get_array_element(type));
      if (glsl_type_is_unsized_array(type))
         ret = spirv_builder_type_runtime_array(&ctx->builder, element_type);
      else
         ret = spirv_builder_type_array(&ctx->builder,
                                        element_type,
                                        emit_uint_const(ctx, 32, glsl_get_length(type)));
      uint32_t stride = glsl_get_explicit_stride(type);
      if (!stride && glsl_type_is_scalar(glsl_get_array_element(type))) {
         stride = MAX2(glsl_get_bit_size(glsl_get_array_element(type)) / 8, 1);
      }
      if (stride)
         spirv_builder_emit_array_stride(&ctx->builder, ret, stride);
   } else if (glsl_type_is_struct_or_ifc(type)) {
      const unsigned length = glsl_get_length(type);

      /* allocate some SpvId on the stack, falling back to the heap if the array is too long */
      SpvId *types, types_stack[16];

      if (length <= ARRAY_SIZE(types_stack)) {
         types = types_stack;
      } else {
         types = ralloc_array_size(ctx->mem_ctx, sizeof(SpvId), length);
         assert(types != NULL);
      }

      for (unsigned i = 0; i < glsl_get_length(type); i++)
         types[i] = get_glsl_type(ctx, glsl_get_struct_field(type, i));
      ret = spirv_builder_type_struct(&ctx->builder, types,
                                      glsl_get_length(type));
      for (unsigned i = 0; i < glsl_get_length(type); i++)
         spirv_builder_emit_member_offset(&ctx->builder, ret, i, glsl_get_struct_field_offset(type, i));
   } else
      unreachable("Unhandled GLSL type");

   _mesa_hash_table_insert(ctx->glsl_types, type, (void *)(uintptr_t)ret);
   return ret;
}

static void
create_shared_block(struct ntv_context *ctx, unsigned shared_size)
{
   SpvId type = spirv_builder_type_uint(&ctx->builder, 32);
   SpvId array = spirv_builder_type_array(&ctx->builder, type, emit_uint_const(ctx, 32, shared_size / 4));
   spirv_builder_emit_array_stride(&ctx->builder, array, 4);
   SpvId ptr_type = spirv_builder_type_pointer(&ctx->builder,
                                               SpvStorageClassWorkgroup,
                                               array);
   ctx->shared_block_var = spirv_builder_emit_var(&ctx->builder, ptr_type, SpvStorageClassWorkgroup);
   if (ctx->spirv_1_4_interfaces) {
      assert(ctx->num_entry_ifaces < ARRAY_SIZE(ctx->entry_ifaces));
      ctx->entry_ifaces[ctx->num_entry_ifaces++] = ctx->shared_block_var;
   }
}

#define HANDLE_EMIT_BUILTIN(SLOT, BUILTIN) \
      case VARYING_SLOT_##SLOT: \
         spirv_builder_emit_builtin(&ctx->builder, var_id, SpvBuiltIn##BUILTIN); \
         break


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

   if (var->data.mode == nir_var_mem_push_const) {
      ctx->push_const_var = var_id;

      if (ctx->spirv_1_4_interfaces) {
         assert(ctx->num_entry_ifaces < ARRAY_SIZE(ctx->entry_ifaces));
         ctx->entry_ifaces[ctx->num_entry_ifaces++] = var_id;
      }
   }
   return var_id;
}

static void
emit_interpolation(struct ntv_context *ctx, SpvId var_id,
                   enum glsl_interp_mode mode)
{
   switch (mode) {
   case INTERP_MODE_NONE:
   case INTERP_MODE_SMOOTH:
      /* XXX spirv doesn't seem to have anything for this */
      break;
   case INTERP_MODE_FLAT:
      spirv_builder_emit_decoration(&ctx->builder, var_id,
                                    SpvDecorationFlat);
      break;
   case INTERP_MODE_EXPLICIT:
      spirv_builder_emit_decoration(&ctx->builder, var_id,
                                    SpvDecorationExplicitInterpAMD);
      break;
   case INTERP_MODE_NOPERSPECTIVE:
      spirv_builder_emit_decoration(&ctx->builder, var_id,
                                    SpvDecorationNoPerspective);
      break;
   default:
      unreachable("unknown interpolation value");
   }
}

static void
emit_input(struct ntv_context *ctx, struct nir_variable *var)
{
   SpvId var_id = input_var_init(ctx, var);
   if (ctx->stage == MESA_SHADER_VERTEX)
      spirv_builder_emit_location(&ctx->builder, var_id,
                                  var->data.driver_location);
   else if (ctx->stage == MESA_SHADER_FRAGMENT) {
      switch (var->data.location) {
      HANDLE_EMIT_BUILTIN(POS, FragCoord);
      HANDLE_EMIT_BUILTIN(PNTC, PointCoord);
      HANDLE_EMIT_BUILTIN(LAYER, Layer);
      HANDLE_EMIT_BUILTIN(PRIMITIVE_ID, PrimitiveId);
      HANDLE_EMIT_BUILTIN(CLIP_DIST0, ClipDistance);
      HANDLE_EMIT_BUILTIN(CULL_DIST0, CullDistance);
      HANDLE_EMIT_BUILTIN(VIEWPORT, ViewportIndex);
      HANDLE_EMIT_BUILTIN(FACE, FrontFacing);

      default:
         spirv_builder_emit_location(&ctx->builder, var_id,
                                     var->data.driver_location);
      }
      if (var->data.centroid)
         spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationCentroid);
      else if (var->data.sample)
         spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationSample);
   } else if (ctx->stage < MESA_SHADER_FRAGMENT) {
      switch (var->data.location) {
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
         spirv_builder_emit_location(&ctx->builder, var_id,
                                     var->data.driver_location);
      }
   }

   if (var->data.location_frac)
      spirv_builder_emit_component(&ctx->builder, var_id,
                                   var->data.location_frac);

   if (var->data.patch)
      spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationPatch);

   emit_interpolation(ctx, var_id, var->data.interpolation);

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

   if (ctx->stage != MESA_SHADER_FRAGMENT) {
      switch (var->data.location) {
      HANDLE_EMIT_BUILTIN(POS, Position);
      HANDLE_EMIT_BUILTIN(PSIZ, PointSize);
      HANDLE_EMIT_BUILTIN(LAYER, Layer);
      HANDLE_EMIT_BUILTIN(PRIMITIVE_ID, PrimitiveId);
      HANDLE_EMIT_BUILTIN(CLIP_DIST0, ClipDistance);
      HANDLE_EMIT_BUILTIN(CULL_DIST0, CullDistance);
      HANDLE_EMIT_BUILTIN(VIEWPORT, ViewportIndex);
      HANDLE_EMIT_BUILTIN(TESS_LEVEL_OUTER, TessLevelOuter);
      HANDLE_EMIT_BUILTIN(TESS_LEVEL_INNER, TessLevelInner);

      default:
         spirv_builder_emit_location(&ctx->builder, var_id,
                                     var->data.driver_location);
      }
      /* tcs can't do xfb */
      if (ctx->stage != MESA_SHADER_TESS_CTRL) {
         unsigned idx = var->data.location << 2 | var->data.location_frac;
         ctx->outputs[idx] = var_id;
         ctx->so_output_gl_types[idx] = var->type;
         ctx->so_output_types[idx] = var_type;
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

         case FRAG_RESULT_STENCIL:
            spirv_builder_emit_builtin(&ctx->builder, var_id, SpvBuiltInFragStencilRefEXT);
            break;

         default:
            spirv_builder_emit_location(&ctx->builder, var_id,
                                        var->data.location);
            spirv_builder_emit_index(&ctx->builder, var_id, var->data.index);
         }
      }
      if (var->data.sample)
         spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationSample);
   }

   if (var->data.location_frac)
      spirv_builder_emit_component(&ctx->builder, var_id,
                                   var->data.location_frac);

   emit_interpolation(ctx, var_id, var->data.interpolation);

   if (var->data.patch)
      spirv_builder_emit_decoration(&ctx->builder, var_id, SpvDecorationPatch);

   if (var->data.explicit_xfb_buffer) {
      spirv_builder_emit_offset(&ctx->builder, var_id, var->data.offset);
      spirv_builder_emit_xfb_buffer(&ctx->builder, var_id, var->data.xfb.buffer);
      spirv_builder_emit_xfb_stride(&ctx->builder, var_id, var->data.xfb.stride);
      if (var->data.stream)
         spirv_builder_emit_stream(&ctx->builder, var_id, var->data.stream);
   }

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
   case GLSL_SAMPLER_DIM_SUBPASS:
      return SpvDimSubpassData;
   default:
      fprintf(stderr, "unknown sampler type %d\n", gdim);
      break;
   }
   return SpvDim2D;
}

static inline SpvImageFormat
get_shader_image_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R32G32B32A32_FLOAT:
      return SpvImageFormatRgba32f;
   case PIPE_FORMAT_R16G16B16A16_FLOAT:
      return SpvImageFormatRgba16f;
   case PIPE_FORMAT_R32_FLOAT:
      return SpvImageFormatR32f;
   case PIPE_FORMAT_R8G8B8A8_UNORM:
      return SpvImageFormatRgba8;
   case PIPE_FORMAT_R8G8B8A8_SNORM:
      return SpvImageFormatRgba8Snorm;
   case PIPE_FORMAT_R32G32B32A32_SINT:
      return SpvImageFormatRgba32i;
   case PIPE_FORMAT_R16G16B16A16_SINT:
      return SpvImageFormatRgba16i;
   case PIPE_FORMAT_R8G8B8A8_SINT:
      return SpvImageFormatRgba8i;
   case PIPE_FORMAT_R32_SINT:
      return SpvImageFormatR32i;
   case PIPE_FORMAT_R32G32B32A32_UINT:
      return SpvImageFormatRgba32ui;
   case PIPE_FORMAT_R16G16B16A16_UINT:
      return SpvImageFormatRgba16ui;
   case PIPE_FORMAT_R8G8B8A8_UINT:
      return SpvImageFormatRgba8ui;
   case PIPE_FORMAT_R32_UINT:
      return SpvImageFormatR32ui;
   default:
      return SpvImageFormatUnknown;
   }
}

static inline SpvImageFormat
get_extended_image_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R32G32_FLOAT:
      return SpvImageFormatRg32f;
   case PIPE_FORMAT_R16G16_FLOAT:
      return SpvImageFormatRg16f;
   case PIPE_FORMAT_R11G11B10_FLOAT:
      return SpvImageFormatR11fG11fB10f;
   case PIPE_FORMAT_R16_FLOAT:
      return SpvImageFormatR16f;
   case PIPE_FORMAT_R16G16B16A16_UNORM:
      return SpvImageFormatRgba16;
   case PIPE_FORMAT_R10G10B10A2_UNORM:
      return SpvImageFormatRgb10A2;
   case PIPE_FORMAT_R16G16_UNORM:
      return SpvImageFormatRg16;
   case PIPE_FORMAT_R8G8_UNORM:
      return SpvImageFormatRg8;
   case PIPE_FORMAT_R16_UNORM:
      return SpvImageFormatR16;
   case PIPE_FORMAT_R8_UNORM:
      return SpvImageFormatR8;
   case PIPE_FORMAT_R16G16B16A16_SNORM:
      return SpvImageFormatRgba16Snorm;
   case PIPE_FORMAT_R16G16_SNORM:
      return SpvImageFormatRg16Snorm;
   case PIPE_FORMAT_R8G8_SNORM:
      return SpvImageFormatRg8Snorm;
   case PIPE_FORMAT_R16_SNORM:
      return SpvImageFormatR16Snorm;
   case PIPE_FORMAT_R8_SNORM:
      return SpvImageFormatR8Snorm;
   case PIPE_FORMAT_R32G32_SINT:
      return SpvImageFormatRg32i;
   case PIPE_FORMAT_R16G16_SINT:
      return SpvImageFormatRg16i;
   case PIPE_FORMAT_R8G8_SINT:
      return SpvImageFormatRg8i;
   case PIPE_FORMAT_R16_SINT:
      return SpvImageFormatR16i;
   case PIPE_FORMAT_R8_SINT:
      return SpvImageFormatR8i;
   case PIPE_FORMAT_R10G10B10A2_UINT:
      return SpvImageFormatRgb10a2ui;
   case PIPE_FORMAT_R32G32_UINT:
      return SpvImageFormatRg32ui;
   case PIPE_FORMAT_R16G16_UINT:
      return SpvImageFormatRg16ui;
   case PIPE_FORMAT_R8G8_UINT:
      return SpvImageFormatRg8ui;
   case PIPE_FORMAT_R16_UINT:
      return SpvImageFormatR16ui;
   case PIPE_FORMAT_R8_UINT:
      return SpvImageFormatR8ui;

   default:
      return SpvImageFormatUnknown;
   }
}

static inline SpvImageFormat
get_image_format(struct ntv_context *ctx, enum pipe_format format)
{
   /* always supported */
   if (format == PIPE_FORMAT_NONE)
      return SpvImageFormatUnknown;

   SpvImageFormat ret = get_shader_image_format(format);
   if (ret != SpvImageFormatUnknown) {
      /* requires the shader-cap, but we already emit that */
      return ret;
   }

   ret = get_extended_image_format(format);
   assert(ret != SpvImageFormatUnknown);
   spirv_builder_emit_cap(&ctx->builder,
                          SpvCapabilityStorageImageExtendedFormats);
   return ret;
}

static SpvId
get_bare_image_type(struct ntv_context *ctx, struct nir_variable *var, bool is_sampler)
{
   const struct glsl_type *type = glsl_without_array(var->type);

   bool is_ms;

   if (var->data.fb_fetch_output) {
      spirv_builder_emit_cap(&ctx->builder, SpvCapabilityInputAttachment);
   } else if (!is_sampler && !var->data.image.format) {
      if (!(var->data.access & ACCESS_NON_WRITEABLE))
         spirv_builder_emit_cap(&ctx->builder, SpvCapabilityStorageImageWriteWithoutFormat);
      if (!(var->data.access & ACCESS_NON_READABLE))
         spirv_builder_emit_cap(&ctx->builder, SpvCapabilityStorageImageReadWithoutFormat);
   }

   SpvDim dimension = type_to_dim(glsl_get_sampler_dim(type), &is_ms);
   bool arrayed = glsl_sampler_type_is_array(type);
   if (dimension == SpvDimCube && arrayed)
      spirv_builder_emit_cap(&ctx->builder, SpvCapabilityImageCubeArray);

   SpvId result_type = get_glsl_basetype(ctx, glsl_get_sampler_result_type(type));
   return spirv_builder_type_image(&ctx->builder, result_type,
                                               dimension, false,
                                               arrayed,
                                               is_ms, is_sampler ? 1 : 2,
                                               get_image_format(ctx, var->data.image.format));
}

static SpvId
get_image_type(struct ntv_context *ctx, struct nir_variable *var, bool is_sampler)
{
   SpvId image_type = get_bare_image_type(ctx, var, is_sampler);
   return is_sampler ? spirv_builder_type_sampled_image(&ctx->builder, image_type) : image_type;
}

static SpvId
emit_image(struct ntv_context *ctx, struct nir_variable *var, bool bindless)
{
   if (var->data.bindless)
      return 0;
   const struct glsl_type *type = glsl_without_array(var->type);

   bool is_sampler = glsl_type_is_sampler(type);
   SpvId image_type = get_bare_image_type(ctx, var, is_sampler);
   SpvId var_type = is_sampler ? spirv_builder_type_sampled_image(&ctx->builder, image_type) : image_type;

   int index = var->data.driver_location;
   assert(!is_sampler || (!(ctx->samplers_used & (1 << index))));
   assert(!is_sampler || !ctx->sampler_types[index]);
   assert(is_sampler || !ctx->image_types[index]);

   if (!bindless && glsl_type_is_array(var->type)) {
      var_type = spirv_builder_type_array(&ctx->builder, var_type,
                                              emit_uint_const(ctx, 32, glsl_get_aoa_size(var->type)));
      spirv_builder_emit_array_stride(&ctx->builder, var_type, sizeof(void*));
      ctx->sampler_array_sizes[index] = glsl_get_aoa_size(var->type);
   }
   SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                   SpvStorageClassUniformConstant,
                                                   var_type);

   SpvId var_id = spirv_builder_emit_var(&ctx->builder, pointer_type,
                                         SpvStorageClassUniformConstant);

   if (var->name)
      spirv_builder_emit_name(&ctx->builder, var_id, var->name);

   if (var->data.fb_fetch_output)
      spirv_builder_emit_input_attachment_index(&ctx->builder, var_id, var->data.index);

   if (bindless)
      return var_id;

   _mesa_hash_table_insert(ctx->vars, var, (void *)(intptr_t)var_id);
   if (is_sampler) {
      ctx->sampler_types[index] = image_type;
      ctx->samplers[index] = var_id;
      ctx->samplers_used |= 1 << index;
   } else {
      ctx->image_types[index] = image_type;
      ctx->images[index] = var_id;
      uint32_t *key = ralloc_size(ctx->mem_ctx, sizeof(uint32_t));
      *key = var_id;
      _mesa_hash_table_insert(ctx->image_vars, key, var);
      emit_access_decorations(ctx, var, var_id);
   }
   if (ctx->spirv_1_4_interfaces) {
      assert(ctx->num_entry_ifaces < ARRAY_SIZE(ctx->entry_ifaces));
      ctx->entry_ifaces[ctx->num_entry_ifaces++] = var_id;
   }

   spirv_builder_emit_descriptor_set(&ctx->builder, var_id, var->data.descriptor_set);
   spirv_builder_emit_binding(&ctx->builder, var_id, var->data.binding);
   return var_id;
}

static SpvId
get_sized_uint_array_type(struct ntv_context *ctx, unsigned array_size, unsigned bitsize)
{
   SpvId array_length = emit_uint_const(ctx, 32, array_size);
   SpvId array_type = spirv_builder_type_array(&ctx->builder, get_uvec_type(ctx, bitsize, 1),
                                            array_length);
   spirv_builder_emit_array_stride(&ctx->builder, array_type, bitsize / 8);
   return array_type;
}

static SpvId
get_bo_array_type(struct ntv_context *ctx, struct nir_variable *var)
{
   struct hash_entry *he = _mesa_hash_table_search(ctx->bo_types, var);
   if (he)
      return (SpvId)(uintptr_t)he->data;
   unsigned bitsize = glsl_get_bit_size(glsl_get_array_element(glsl_get_struct_field(var->type, 0)));
   assert(bitsize);
   SpvId array_type;
   const struct glsl_type *type = var->type;
   if (!glsl_type_is_unsized_array(type)) {
      type = glsl_get_struct_field(var->interface_type, 0);
      if (!glsl_type_is_unsized_array(type)) {
         uint32_t array_size = glsl_get_length(type) * (bitsize / 4);
         assert(array_size);
         return get_sized_uint_array_type(ctx, array_size, bitsize);
      }
   }
   SpvId uint_type = spirv_builder_type_uint(&ctx->builder, bitsize);
   array_type = spirv_builder_type_runtime_array(&ctx->builder, uint_type);
   spirv_builder_emit_array_stride(&ctx->builder, array_type, bitsize / 8);
   return array_type;
}

static SpvId
get_bo_struct_type(struct ntv_context *ctx, struct nir_variable *var)
{
   unsigned bitsize = glsl_get_bit_size(glsl_get_array_element(glsl_get_struct_field(var->type, 0)));
   SpvId array_type = get_bo_array_type(ctx, var);
   _mesa_hash_table_insert(ctx->bo_types, var, (void *)(uintptr_t)array_type);
   bool ssbo = var->data.mode == nir_var_mem_ssbo;

   // wrap UBO-array in a struct
   SpvId runtime_array = 0;
   if (ssbo && glsl_get_length(var->interface_type) > 1) {
       const struct glsl_type *last_member = glsl_get_struct_field(var->interface_type, glsl_get_length(var->interface_type) - 1);
       if (glsl_type_is_unsized_array(last_member)) {
          bool is_64bit = glsl_type_is_64bit(glsl_without_array(last_member));
          runtime_array = spirv_builder_type_runtime_array(&ctx->builder, get_uvec_type(ctx, is_64bit ? 64 : bitsize, 1));
          spirv_builder_emit_array_stride(&ctx->builder, runtime_array, glsl_get_explicit_stride(last_member));
       }
   }
   SpvId types[] = {array_type, runtime_array};
   SpvId struct_type = spirv_builder_type_struct(&ctx->builder, types, 1 + !!runtime_array);
   if (var->name) {
      char struct_name[100];
      snprintf(struct_name, sizeof(struct_name), "struct_%s", var->name);
      spirv_builder_emit_name(&ctx->builder, struct_type, struct_name);
   }

   spirv_builder_emit_decoration(&ctx->builder, struct_type,
                                 SpvDecorationBlock);
   spirv_builder_emit_member_offset(&ctx->builder, struct_type, 0, 0);
   if (runtime_array) {
      spirv_builder_emit_member_offset(&ctx->builder, struct_type, 1,
                                      glsl_get_struct_field_offset(var->interface_type,
                                                                   glsl_get_length(var->interface_type) - 1));
   }

   return spirv_builder_type_pointer(&ctx->builder,
                                                   ssbo ? SpvStorageClassStorageBuffer : SpvStorageClassUniform,
                                                   struct_type);
}

static void
emit_bo(struct ntv_context *ctx, struct nir_variable *var)
{
   unsigned bitsize = glsl_get_bit_size(glsl_get_array_element(glsl_get_struct_field(var->type, 0)));
   bool ssbo = var->data.mode == nir_var_mem_ssbo;
   SpvId pointer_type = get_bo_struct_type(ctx, var);

   SpvId var_id = spirv_builder_emit_var(&ctx->builder, pointer_type,
                                         ssbo ? SpvStorageClassStorageBuffer : SpvStorageClassUniform);
   if (var->name)
      spirv_builder_emit_name(&ctx->builder, var_id, var->name);

   unsigned idx = bitsize >> 4;
   assert(idx < ARRAY_SIZE(ctx->ssbos[0]));
   if (ssbo) {
      assert(!ctx->ssbos[var->data.driver_location][idx]);
      ctx->ssbos[var->data.driver_location][idx] = var_id;
      ctx->ssbo_vars[var->data.driver_location] = var;
   } else {
      assert(!ctx->ubos[var->data.driver_location][idx]);
      ctx->ubos[var->data.driver_location][idx] = var_id;
      ctx->ubo_vars[var->data.driver_location] = var;
   }
   if (ctx->spirv_1_4_interfaces) {
      assert(ctx->num_entry_ifaces < ARRAY_SIZE(ctx->entry_ifaces));
      ctx->entry_ifaces[ctx->num_entry_ifaces++] = var_id;
   }
   _mesa_hash_table_insert(ctx->vars, var, (void *)(intptr_t)var_id);

   spirv_builder_emit_descriptor_set(&ctx->builder, var_id, var->data.descriptor_set);
   spirv_builder_emit_binding(&ctx->builder, var_id, var->data.binding);
}

static void
emit_uniform(struct ntv_context *ctx, struct nir_variable *var)
{
   if (var->data.mode == nir_var_mem_ubo || var->data.mode == nir_var_mem_ssbo)
      emit_bo(ctx, var);
   else {
      assert(var->data.mode == nir_var_uniform ||
             var->data.mode == nir_var_image);
      const struct glsl_type *type = glsl_without_array(var->type);
      if (glsl_type_is_sampler(type) || glsl_type_is_image(type))
         emit_image(ctx, var, false);
   }
}

static SpvId
get_vec_from_bit_size(struct ntv_context *ctx, uint32_t bit_size, uint32_t num_components)
{
   if (bit_size == 1)
      return get_bvec_type(ctx, num_components);
   if (bit_size == 8 || bit_size == 16 || bit_size == 32 || bit_size == 64)
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
   assert(bit_size == 1 || bit_size == 8 || bit_size == 16 || bit_size == 32 || bit_size == 64);

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
         break;

      case nir_type_uint:
      case nir_type_uint8:
      case nir_type_uint16:
      case nir_type_uint64:
         break; /* nothing to do! */

      case nir_type_int:
      case nir_type_int8:
      case nir_type_int16:
      case nir_type_int64:
      case nir_type_float:
      case nir_type_float16:
      case nir_type_float64:
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
   const struct glsl_type *out_type = NULL;
   /* index is based on component, so we might have to go back a few slots to get to the base */
   while (!out_type)
      out_type = ctx->so_output_gl_types[register_index--];
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
emit_so_info(struct ntv_context *ctx, const struct zink_shader_info *so_info,
             unsigned first_so)
{
   unsigned output = 0;
   for (unsigned i = 0; i < so_info->so_info.num_outputs; i++) {
      struct pipe_stream_output so_output = so_info->so_info.output[i];
      unsigned slot = so_info->so_info_slots[i] << 2 | so_output.start_component;
      SpvId out_type = get_output_type(ctx, slot, so_output.num_components);
      SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                      SpvStorageClassOutput,
                                                      out_type);
      SpvId var_id = spirv_builder_emit_var(&ctx->builder, pointer_type,
                                            SpvStorageClassOutput);
      char name[10];

      snprintf(name, 10, "xfb%d", output);
      spirv_builder_emit_name(&ctx->builder, var_id, name);
      spirv_builder_emit_offset(&ctx->builder, var_id, (so_output.dst_offset * 4));
      spirv_builder_emit_xfb_buffer(&ctx->builder, var_id, so_output.output_buffer);
      spirv_builder_emit_xfb_stride(&ctx->builder, var_id, so_info->so_info.stride[so_output.output_buffer] * 4);
      if (so_output.stream)
         spirv_builder_emit_stream(&ctx->builder, var_id, so_output.stream);

      /* output location is incremented by VARYING_SLOT_VAR0 for non-builtins in vtn,
       * so we need to ensure that the new xfb location slot doesn't conflict with any previously-emitted
       * outputs.
       */
      uint32_t location = first_so + i;
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
      output += align(so_output.num_components, 4) / 4;
   }
}

static void
emit_so_outputs(struct ntv_context *ctx,
                const struct zink_shader_info *so_info)
{
   for (unsigned i = 0; i < so_info->so_info.num_outputs; i++) {
      uint32_t components[NIR_MAX_VEC_COMPONENTS];
      unsigned slot = so_info->so_info_slots[i];
      struct pipe_stream_output so_output = so_info->so_info.output[i];
      uint32_t so_key = (uint32_t) so_output.register_index << 2 | so_output.start_component;
      uint32_t location = (uint32_t) slot << 2 | so_output.start_component;
      struct hash_entry *he = _mesa_hash_table_search(ctx->so_outputs, &so_key);
      assert(he);
      SpvId so_output_var_id = (SpvId)(intptr_t)he->data;

      SpvId type = get_output_type(ctx, location, so_output.num_components);
      SpvId output = 0;
      /* index is based on component, so we might have to go back a few slots to get to the base */
      UNUSED uint32_t orig_location = location;
      while (!output)
         output = ctx->outputs[location--];
      location++;
      SpvId output_type = ctx->so_output_types[location];
      const struct glsl_type *out_type = ctx->so_output_gl_types[location];

      SpvId src = spirv_builder_emit_load(&ctx->builder, output_type, output);

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
                uint32_t member[2];
                unsigned member_idx = 0;
                if (glsl_type_is_matrix(out_type)) {
                   member_idx = 1;
                   member[0] = so_output.register_index;
                }
                member[member_idx] = so_output.start_component + c;
                SpvId base_type = get_glsl_basetype(ctx, glsl_get_base_type(glsl_without_array_or_matrix(out_type)));

                if (slot == VARYING_SLOT_CLIP_DIST1)
                   member[member_idx] += 4;
                components[c] = spirv_builder_emit_composite_extract(&ctx->builder, base_type, src, member, 1 + member_idx);
             }
             result = spirv_builder_emit_composite_construct(&ctx->builder, type, components, so_output.num_components);
         }
      }

      spirv_builder_emit_store(&ctx->builder, so_output_var_id, result);
   }
}

static SpvId
emit_atomic(struct ntv_context *ctx, SpvId op, SpvId type, SpvId src0, SpvId src1, SpvId src2)
{
   if (op == SpvOpAtomicLoad)
      return spirv_builder_emit_triop(&ctx->builder, op, type, src0, emit_uint_const(ctx, 32, SpvScopeDevice),
                                       emit_uint_const(ctx, 32, 0));
   if (op == SpvOpAtomicCompareExchange)
      return spirv_builder_emit_hexop(&ctx->builder, op, type, src0, emit_uint_const(ctx, 32, SpvScopeDevice),
                                       emit_uint_const(ctx, 32, 0),
                                       emit_uint_const(ctx, 32, 0),
                                       /* these params are intentionally swapped */
                                       src2, src1);

   return spirv_builder_emit_quadop(&ctx->builder, op, type, src0, emit_uint_const(ctx, 32, SpvScopeDevice),
                                    emit_uint_const(ctx, 32, 0), src1);
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
   assert(bit_size == 16 || bit_size == 32 || bit_size == 64);

   SpvId result = emit_float_const(ctx, bit_size, value);
   if (num_components == 1)
      return result;

   assert(num_components > 1);
   SpvId components[NIR_MAX_VEC_COMPONENTS];
   for (int i = 0; i < num_components; i++)
      components[i] = result;

   SpvId type = get_fvec_type(ctx, bit_size, num_components);
   return spirv_builder_const_composite(&ctx->builder, type, components,
                                        num_components);
}

static SpvId
get_ivec_constant(struct ntv_context *ctx, unsigned bit_size,
                  unsigned num_components, int64_t value)
{
   assert(bit_size == 8 || bit_size == 16 || bit_size == 32 || bit_size == 64);

   SpvId result = emit_int_const(ctx, bit_size, value);
   if (num_components == 1)
      return result;

   assert(num_components > 1);
   SpvId components[NIR_MAX_VEC_COMPONENTS];
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
store_alu_result(struct ntv_context *ctx, nir_alu_instr *alu, SpvId result, bool force_float)
{
   assert(!alu->dest.saturate);
   return store_dest(ctx, &alu->dest.dest, result,
                     force_float ? nir_type_float : nir_op_infos[alu->op].output_type);
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
   case nir_type_int8:
   case nir_type_int16:
   case nir_type_int64:
      return get_ivec_type(ctx, bit_size, num_components);

   case nir_type_uint:
   case nir_type_uint8:
   case nir_type_uint16:
   case nir_type_uint64:
      return get_uvec_type(ctx, bit_size, num_components);

   case nir_type_float:
   case nir_type_float16:
   case nir_type_float64:
      return get_fvec_type(ctx, bit_size, num_components);

   default:
      unreachable("unsupported nir_alu_type");
   }
}

static bool
needs_derivative_control(nir_alu_instr *alu)
{
   switch (alu->op) {
   case nir_op_fddx_coarse:
   case nir_op_fddx_fine:
   case nir_op_fddy_coarse:
   case nir_op_fddy_fine:
      return true;

   default:
      return false;
   }
}

static SpvId
unswizzle_src(struct ntv_context *ctx, nir_ssa_def *ssa, SpvId src, unsigned num_components)
{
   /* value may have already been cast to ivec, so cast back */
   SpvId cast_type = get_uvec_type(ctx, ssa->bit_size, num_components);
   src = emit_bitcast(ctx, cast_type, src);

   /* extract from swizzled vec */
   SpvId type = spirv_builder_type_uint(&ctx->builder, ssa->bit_size);
   uint32_t idx = 0;
   return spirv_builder_emit_composite_extract(&ctx->builder, type, src, &idx, 1);
}

static void
emit_alu(struct ntv_context *ctx, nir_alu_instr *alu)
{
   SpvId src[NIR_MAX_VEC_COMPONENTS];
   for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++)
      src[i] = get_alu_src(ctx, alu, i);

   SpvId dest_type = get_dest_type(ctx, &alu->dest.dest,
                                   nir_op_infos[alu->op].output_type);
   bool force_float = false;
   unsigned bit_size = nir_dest_bit_size(alu->dest.dest);
   unsigned num_components = nir_dest_num_components(alu->dest.dest);

   if (needs_derivative_control(alu))
      spirv_builder_emit_cap(&ctx->builder, SpvCapabilityDerivativeControl);

   /* modify params here */
   switch (alu->op) {
   /* Offset must be an integer type scalar.
    * Offset is the lowest-order bit of the bit field.
    * It is consumed as an unsigned value.
    *
    * Count must be an integer type scalar.
    *
    * if these ops have more than one component in the dest, then their offset and count
    * are swizzled like ssa_1.xxx, but only a single scalar can be provided
    */
   case nir_op_ubitfield_extract:
   case nir_op_ibitfield_extract:
      if (num_components > 1) {
         src[1] = unswizzle_src(ctx, alu->src[1].src.ssa, src[1], num_components);
         src[2] = unswizzle_src(ctx, alu->src[2].src.ssa, src[2], num_components);
      }
      break;
   case nir_op_bitfield_insert:
      if (num_components > 1) {
         src[2] = unswizzle_src(ctx, alu->src[2].src.ssa, src[2], num_components);
         src[3] = unswizzle_src(ctx, alu->src[3].src.ssa, src[3], num_components);
      }
      break;
   default:
      break;
   }

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
   UNOP(nir_op_f2i16, SpvOpConvertFToS)
   UNOP(nir_op_f2u16, SpvOpConvertFToU)
   UNOP(nir_op_f2i32, SpvOpConvertFToS)
   UNOP(nir_op_f2u32, SpvOpConvertFToU)
   UNOP(nir_op_i2f16, SpvOpConvertSToF)
   UNOP(nir_op_i2f32, SpvOpConvertSToF)
   UNOP(nir_op_u2f16, SpvOpConvertUToF)
   UNOP(nir_op_u2f32, SpvOpConvertUToF)
   UNOP(nir_op_i2i16, SpvOpSConvert)
   UNOP(nir_op_i2i32, SpvOpSConvert)
   UNOP(nir_op_u2u8, SpvOpUConvert)
   UNOP(nir_op_u2u16, SpvOpUConvert)
   UNOP(nir_op_u2u32, SpvOpUConvert)
   UNOP(nir_op_f2f16, SpvOpFConvert)
   UNOP(nir_op_f2f32, SpvOpFConvert)
   UNOP(nir_op_f2i64, SpvOpConvertFToS)
   UNOP(nir_op_f2u64, SpvOpConvertFToU)
   UNOP(nir_op_u2f64, SpvOpConvertUToF)
   UNOP(nir_op_i2f64, SpvOpConvertSToF)
   UNOP(nir_op_i2i64, SpvOpSConvert)
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

   case nir_op_b2i16:
   case nir_op_b2i32:
   case nir_op_b2i64:
      assert(nir_op_infos[alu->op].num_inputs == 1);
      result = emit_select(ctx, dest_type, src[0],
                           get_ivec_constant(ctx, bit_size, num_components, 1),
                           get_ivec_constant(ctx, bit_size, num_components, 0));
      break;

   case nir_op_b2f16:
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

#define BUILTIN_UNOPF(nir_op, spirv_op) \
   case nir_op: \
      assert(nir_op_infos[alu->op].num_inputs == 1); \
      result = emit_builtin_unop(ctx, spirv_op, get_dest_type(ctx, &alu->dest.dest, nir_type_float), src[0]); \
      force_float = true; \
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

   case nir_op_pack_half_2x16:
      assert(nir_op_infos[alu->op].num_inputs == 1);
      result = emit_builtin_unop(ctx, GLSLstd450PackHalf2x16, get_dest_type(ctx, &alu->dest.dest, nir_type_uint), src[0]);
      break;

   BUILTIN_UNOPF(nir_op_unpack_half_2x16, GLSLstd450UnpackHalf2x16)
   BUILTIN_UNOPF(nir_op_pack_64_2x32, GLSLstd450PackDouble2x32)
#undef BUILTIN_UNOP
#undef BUILTIN_UNOPF

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
   case nir_op_seq:
   case nir_op_sne:
   case nir_op_slt:
   case nir_op_sge:
      unreachable("should already be lowered away");

   case nir_op_flrp:
      assert(nir_op_infos[alu->op].num_inputs == 3);
      result = emit_builtin_triop(ctx, GLSLstd450FMix, dest_type,
                                  src[0], src[1], src[2]);
      break;

   case nir_op_bcsel:
      assert(nir_op_infos[alu->op].num_inputs == 3);
      result = emit_select(ctx, dest_type, src[0], src[1], src[2]);
      break;

   case nir_op_pack_half_2x16_split: {
      SpvId fvec = spirv_builder_emit_composite_construct(&ctx->builder, get_fvec_type(ctx, 32, 2),
                                                          src, 2);
      result = emit_builtin_unop(ctx, GLSLstd450PackHalf2x16, dest_type, fvec);
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
   if (alu->exact)
      spirv_builder_emit_decoration(&ctx->builder, result, SpvDecorationNoContraction);

   store_alu_result(ctx, alu, result, force_float);
}

static void
emit_load_const(struct ntv_context *ctx, nir_load_const_instr *load_const)
{
   unsigned bit_size = load_const->def.bit_size;
   unsigned num_components = load_const->def.num_components;

   SpvId components[NIR_MAX_VEC_COMPONENTS];
   if (bit_size == 1) {
      for (int i = 0; i < num_components; i++)
         components[i] = spirv_builder_const_bool(&ctx->builder,
                                                  load_const->value[i].b);
   } else {
      for (int i = 0; i < num_components; i++) {
         uint64_t tmp = nir_const_value_as_uint(load_const->value[i],
                                                bit_size);
         components[i] = emit_uint_const(ctx, bit_size, tmp);
      }
   }

   if (num_components > 1) {
      SpvId type = get_vec_from_bit_size(ctx, bit_size,
                                         num_components);
      SpvId value = spirv_builder_const_composite(&ctx->builder,
                                                  type, components,
                                                  num_components);
      store_ssa_def(ctx, &load_const->def, value);
   } else {
      assert(num_components == 1);
      store_ssa_def(ctx, &load_const->def, components[0]);
   }
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

   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   SpvId type;
   if (glsl_type_is_image(deref->type)) {
      nir_variable *var = nir_deref_instr_get_variable(deref);
      type = get_image_type(ctx, var, glsl_type_is_sampler(glsl_without_array(var->type)));
   } else {
      type = get_glsl_type(ctx, deref->type);
   }
   SpvId result;

   if (nir_intrinsic_access(intr) & ACCESS_COHERENT)
      result = emit_atomic(ctx, SpvOpAtomicLoad, type, ptr, 0, 0);
   else
      result = spirv_builder_emit_load(&ctx->builder, type, ptr);
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
   if (nir_intrinsic_access(intr) & ACCESS_COHERENT)
      spirv_builder_emit_atomic_store(&ctx->builder, ptr, SpvScopeDevice, 0, result);
   else
      spirv_builder_emit_store(&ctx->builder, ptr, result);
}

static void
emit_load_shared(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId dest_type = get_dest_type(ctx, &intr->dest, nir_type_uint);
   unsigned num_components = nir_dest_num_components(intr->dest);
   unsigned bit_size = nir_dest_bit_size(intr->dest);
   SpvId uint_type = get_uvec_type(ctx, 32, 1);
   SpvId ptr_type = spirv_builder_type_pointer(&ctx->builder,
                                               SpvStorageClassWorkgroup,
                                               uint_type);
   SpvId offset = get_src(ctx, &intr->src[0]);
   SpvId constituents[NIR_MAX_VEC_COMPONENTS];
   /* need to convert array -> vec */
   for (unsigned i = 0; i < num_components; i++) {
      SpvId member = spirv_builder_emit_access_chain(&ctx->builder, ptr_type,
                                                     ctx->shared_block_var, &offset, 1);
      constituents[i] = spirv_builder_emit_load(&ctx->builder, uint_type, member);
      offset = emit_binop(ctx, SpvOpIAdd, uint_type, offset, emit_uint_const(ctx, 32, 1));
   }
   SpvId result;
   if (num_components > 1)
      result = spirv_builder_emit_composite_construct(&ctx->builder, dest_type, constituents, num_components);
   else
      result = bitcast_to_uvec(ctx, constituents[0], bit_size, num_components);
   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static void
emit_store_shared(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId src = get_src(ctx, &intr->src[0]);
   bool qword = nir_src_bit_size(intr->src[0]) == 64;

   unsigned num_writes = util_bitcount(nir_intrinsic_write_mask(intr));
   unsigned wrmask = nir_intrinsic_write_mask(intr);
   /* this is a partial write, so we have to loop and do a per-component write */
   SpvId uint_type = get_uvec_type(ctx, 32, 1);
   SpvId ptr_type = spirv_builder_type_pointer(&ctx->builder,
                                               SpvStorageClassWorkgroup,
                                               uint_type);
   SpvId offset = get_src(ctx, &intr->src[1]);

   for (unsigned i = 0; num_writes; i++) {
      if ((wrmask >> i) & 1) {
         for (unsigned j = 0; j < 1 + !!qword; j++) {
            unsigned comp = ((1 + !!qword) * i) + j;
            SpvId shared_offset = emit_binop(ctx, SpvOpIAdd, uint_type, offset, emit_uint_const(ctx, 32, comp));
            SpvId val = src;
            if (nir_src_num_components(intr->src[0]) != 1 || qword)
               val = spirv_builder_emit_composite_extract(&ctx->builder, uint_type, src, &comp, 1);
            SpvId member = spirv_builder_emit_access_chain(&ctx->builder, ptr_type,
                                                           ctx->shared_block_var, &shared_offset, 1);
            spirv_builder_emit_store(&ctx->builder, member, val);
         }
         num_writes--;
      }
   }
}

static void
emit_load_push_const(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId uint_type = get_uvec_type(ctx, 32, 1);
   SpvId load_type = get_uvec_type(ctx, 32, 1);

   /* number of components being loaded */
   unsigned num_components = nir_dest_num_components(intr->dest);
   SpvId constituents[NIR_MAX_VEC_COMPONENTS * 2];
   SpvId result;

   /* destination type for the load */
   SpvId type = get_dest_uvec_type(ctx, &intr->dest);
   SpvId one = emit_uint_const(ctx, 32, 1);

   /* we grab a single array member at a time, so it's a pointer to a uint */
   SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                   SpvStorageClassPushConstant,
                                                   load_type);

   SpvId member = get_src(ctx, &intr->src[0]);
   /* reuse the offset from ZINK_PUSH_CONST_OFFSET */
   SpvId offset = emit_uint_const(ctx, 32, 0);
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
   if (!*var_id) {
      if (builtin == SpvBuiltInSampleMask) {
         /* gl_SampleMaskIn is an array[1] in spirv... */
         var_type = spirv_builder_type_array(&ctx->builder, var_type, emit_uint_const(ctx, 32, 1));
         spirv_builder_emit_array_stride(&ctx->builder, var_type, sizeof(uint32_t));
      }
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
emit_interpolate(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId op;
   spirv_builder_emit_cap(&ctx->builder, SpvCapabilityInterpolationFunction);
   switch (intr->intrinsic) {
   case nir_intrinsic_interp_deref_at_centroid:
      op = GLSLstd450InterpolateAtCentroid;
      break;
   case nir_intrinsic_interp_deref_at_sample:
      op = GLSLstd450InterpolateAtSample;
      break;
   case nir_intrinsic_interp_deref_at_offset:
      op = GLSLstd450InterpolateAtOffset;
      break;
   default:
      unreachable("unknown interp op");
   }
   SpvId ptr = get_src(ctx, &intr->src[0]);
   SpvId result;
   if (intr->intrinsic == nir_intrinsic_interp_deref_at_centroid)
      result = emit_builtin_unop(ctx, op, get_glsl_type(ctx, nir_src_as_deref(intr->src[0])->type), ptr);
   else
      result = emit_builtin_binop(ctx, op, get_glsl_type(ctx, nir_src_as_deref(intr->src[0])->type),
                                  ptr, get_src(ctx, &intr->src[1]));
   unsigned num_components = nir_dest_num_components(intr->dest);
   unsigned bit_size = nir_dest_bit_size(intr->dest);
   result = bitcast_to_uvec(ctx, result, bit_size, num_components);
   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static void
handle_atomic_op(struct ntv_context *ctx, nir_intrinsic_instr *intr, SpvId ptr, SpvId param, SpvId param2, nir_alu_type type)
{
   SpvId dest_type = get_dest_type(ctx, &intr->dest, type);
   SpvId result = emit_atomic(ctx, get_atomic_op(intr->intrinsic), dest_type, ptr, param, param2);
   assert(result);
   store_dest(ctx, &intr->dest, result, type);
}

static void
emit_ssbo_atomic_intrinsic(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId ssbo;
   SpvId param;
   SpvId dest_type = get_dest_type(ctx, &intr->dest, nir_type_uint32);

   nir_const_value *const_block_index = nir_src_as_const_value(intr->src[0]);
   assert(const_block_index); // no dynamic indexing for now
   unsigned bit_size = MIN2(nir_src_bit_size(intr->src[0]), 32);
   unsigned idx = bit_size >> 4;
   assert(idx < ARRAY_SIZE(ctx->ssbos[0]));
   assert(ctx->ssbos[const_block_index->u32][idx]);
   ssbo = ctx->ssbos[const_block_index->u32][idx];
   param = get_src(ctx, &intr->src[2]);

   SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                   SpvStorageClassStorageBuffer,
                                                   dest_type);
   SpvId uint_type = get_uvec_type(ctx, 32, 1);
   /* an id of the array stride in bytes */
   SpvId uint_size = emit_uint_const(ctx, 32, bit_size / 8);
   SpvId member = emit_uint_const(ctx, 32, 0);
   SpvId offset = get_src(ctx, &intr->src[1]);
   SpvId vec_offset = emit_binop(ctx, SpvOpUDiv, uint_type, offset, uint_size);
   SpvId indices[] = { member, vec_offset };
   SpvId ptr = spirv_builder_emit_access_chain(&ctx->builder, pointer_type,
                                               ssbo, indices,
                                               ARRAY_SIZE(indices));

   SpvId param2 = 0;

   if (intr->intrinsic == nir_intrinsic_ssbo_atomic_comp_swap)
      param2 = get_src(ctx, &intr->src[3]);

   handle_atomic_op(ctx, intr, ptr, param, param2, nir_type_uint32);
}

static void
emit_shared_atomic_intrinsic(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId dest_type = get_dest_type(ctx, &intr->dest, nir_type_uint32);
   SpvId param = get_src(ctx, &intr->src[1]);

   SpvId pointer_type = spirv_builder_type_pointer(&ctx->builder,
                                                   SpvStorageClassWorkgroup,
                                                   dest_type);
   SpvId offset = emit_binop(ctx, SpvOpUDiv, get_uvec_type(ctx, 32, 1), get_src(ctx, &intr->src[0]), emit_uint_const(ctx, 32, 4));
   SpvId ptr = spirv_builder_emit_access_chain(&ctx->builder, pointer_type,
                                               ctx->shared_block_var, &offset, 1);

   SpvId param2 = 0;

   if (intr->intrinsic == nir_intrinsic_shared_atomic_comp_swap)
      param2 = get_src(ctx, &intr->src[2]);

   handle_atomic_op(ctx, intr, ptr, param, param2, nir_type_uint32);
}

static void
emit_get_ssbo_size(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId uint_type = get_uvec_type(ctx, 32, 1);
   nir_const_value *const_block_index = nir_src_as_const_value(intr->src[0]);
   assert(const_block_index); // no dynamic indexing for now
   nir_variable *var = ctx->ssbo_vars[const_block_index->u32];
   SpvId result = spirv_builder_emit_binop(&ctx->builder, SpvOpArrayLength, uint_type,
                                             ctx->ssbos[const_block_index->u32][2], 1);
   /* this is going to be converted by nir to:

      length = (buffer_size - offset) / stride

      * so we need to un-convert it to avoid having the calculation performed twice
      */
   unsigned last_member_idx = glsl_get_length(var->interface_type) - 1;
   const struct glsl_type *last_member = glsl_get_struct_field(var->interface_type, last_member_idx);
   /* multiply by stride */
   result = emit_binop(ctx, SpvOpIMul, uint_type, result, emit_uint_const(ctx, 32, glsl_get_explicit_stride(last_member)));
   /* get total ssbo size by adding offset */
   result = emit_binop(ctx, SpvOpIAdd, uint_type, result,
                        emit_uint_const(ctx, 32,
                                       glsl_get_struct_field_offset(var->interface_type, last_member_idx)));
   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static inline nir_variable *
get_var_from_image(struct ntv_context *ctx, SpvId var_id)
{
   struct hash_entry *he = _mesa_hash_table_search(ctx->image_vars, &var_id);
   assert(he);
   return he->data;
}

static SpvId
get_image_coords(struct ntv_context *ctx, const struct glsl_type *type, nir_src *src)
{
   uint32_t num_coords = glsl_get_sampler_coordinate_components(type);
   uint32_t src_components = nir_src_num_components(*src);

   SpvId spv = get_src(ctx, src);
   if (num_coords == src_components)
      return spv;

   /* need to extract the coord dimensions that the image can use */
   SpvId vec_type = get_uvec_type(ctx, 32, num_coords);
   if (num_coords == 1)
      return spirv_builder_emit_vector_extract(&ctx->builder, vec_type, spv, 0);
   uint32_t constituents[4];
   SpvId zero = emit_uint_const(ctx, nir_src_bit_size(*src), 0);
   assert(num_coords < ARRAY_SIZE(constituents));
   for (unsigned i = 0; i < num_coords; i++)
      constituents[i] = i < src_components ? i : zero;
   return spirv_builder_emit_vector_shuffle(&ctx->builder, vec_type, spv, spv, constituents, num_coords);
}

static void
emit_image_deref_store(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId img_var = get_src(ctx, &intr->src[0]);
   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   nir_variable *var = deref->deref_type == nir_deref_type_var ? deref->var : get_var_from_image(ctx, img_var);
   SpvId img_type = var->data.bindless ? get_bare_image_type(ctx, var, false) : ctx->image_types[var->data.driver_location];
   const struct glsl_type *type = glsl_without_array(var->type);
   SpvId base_type = get_glsl_basetype(ctx, glsl_get_sampler_result_type(type));
   SpvId img = spirv_builder_emit_load(&ctx->builder, img_type, img_var);
   SpvId coord = get_image_coords(ctx, type, &intr->src[1]);
   SpvId texel = get_src(ctx, &intr->src[3]);
   SpvId sample = glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_MS ? get_src(ctx, &intr->src[2]) : 0;
   assert(nir_src_bit_size(intr->src[3]) == glsl_base_type_bit_size(glsl_get_sampler_result_type(type)));
   /* texel type must match image type */
   texel = emit_bitcast(ctx,
                        spirv_builder_type_vector(&ctx->builder, base_type, 4),
                        texel);
   spirv_builder_emit_image_write(&ctx->builder, img, coord, texel, 0, sample, 0);
}

static SpvId
extract_sparse_load(struct ntv_context *ctx, SpvId result, SpvId dest_type, nir_ssa_def *dest_ssa)
{
   /* Result Type must be an OpTypeStruct with two members.
    * The first member’s type must be an integer type scalar.
    * It holds a Residency Code that can be passed to OpImageSparseTexelsResident
    * - OpImageSparseRead spec
    */
   uint32_t idx = 0;
   SpvId resident = spirv_builder_emit_composite_extract(&ctx->builder, spirv_builder_type_uint(&ctx->builder, 32), result, &idx, 1);
   idx = 1;
   result = spirv_builder_emit_composite_extract(&ctx->builder, dest_type, result, &idx, 1);
   assert(resident != 0);
   assert(dest_ssa->index < ctx->num_defs);
   ctx->resident_defs[dest_ssa->index] = resident;
   return result;
}

static void
emit_image_deref_load(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   bool sparse = intr->intrinsic == nir_intrinsic_image_deref_sparse_load;
   SpvId img_var = get_src(ctx, &intr->src[0]);
   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   nir_variable *var = deref->deref_type == nir_deref_type_var ? deref->var : get_var_from_image(ctx, img_var);
   SpvId img_type = var->data.bindless ? get_bare_image_type(ctx, var, false) : ctx->image_types[var->data.driver_location];
   const struct glsl_type *type = glsl_without_array(var->type);
   SpvId base_type = get_glsl_basetype(ctx, glsl_get_sampler_result_type(type));
   SpvId img = spirv_builder_emit_load(&ctx->builder, img_type, img_var);
   SpvId coord = get_image_coords(ctx, type, &intr->src[1]);
   SpvId sample = glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_MS ? get_src(ctx, &intr->src[2]) : 0;
   SpvId dest_type = spirv_builder_type_vector(&ctx->builder, base_type, nir_dest_num_components(intr->dest));
   SpvId result = spirv_builder_emit_image_read(&ctx->builder,
                                 dest_type,
                                 img, coord, 0, sample, 0, sparse);
   if (sparse)
      result = extract_sparse_load(ctx, result, dest_type, &intr->dest.ssa);
   store_dest(ctx, &intr->dest, result, nir_type_float);
}

static void
emit_image_deref_size(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId img_var = get_src(ctx, &intr->src[0]);
   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   nir_variable *var = deref->deref_type == nir_deref_type_var ? deref->var : get_var_from_image(ctx, img_var);
   SpvId img_type = var->data.bindless ? get_bare_image_type(ctx, var, false) : ctx->image_types[var->data.driver_location];
   const struct glsl_type *type = glsl_without_array(var->type);
   SpvId img = spirv_builder_emit_load(&ctx->builder, img_type, img_var);
   SpvId result = spirv_builder_emit_image_query_size(&ctx->builder, get_uvec_type(ctx, 32, glsl_get_sampler_coordinate_components(type)), img, 0);
   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static void
emit_image_deref_samples(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId img_var = get_src(ctx, &intr->src[0]);
   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   nir_variable *var = deref->deref_type == nir_deref_type_var ? deref->var : get_var_from_image(ctx, img_var);
   SpvId img_type = var->data.bindless ? get_bare_image_type(ctx, var, false) : ctx->image_types[var->data.driver_location];
   SpvId img = spirv_builder_emit_load(&ctx->builder, img_type, img_var);
   SpvId result = spirv_builder_emit_unop(&ctx->builder, SpvOpImageQuerySamples, get_dest_type(ctx, &intr->dest, nir_type_uint), img);
   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static void
emit_image_intrinsic(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvId param = get_src(ctx, &intr->src[3]);
   SpvId img_var = get_src(ctx, &intr->src[0]);
   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   nir_variable *var = deref->deref_type == nir_deref_type_var ? deref->var : get_var_from_image(ctx, img_var);
   const struct glsl_type *type = glsl_without_array(var->type);
   bool is_ms;
   type_to_dim(glsl_get_sampler_dim(type), &is_ms);
   SpvId sample = is_ms ? get_src(ctx, &intr->src[2]) : emit_uint_const(ctx, 32, 0);
   SpvId coord = get_image_coords(ctx, type, &intr->src[1]);
   enum glsl_base_type glsl_type = glsl_get_sampler_result_type(type);
   SpvId base_type = get_glsl_basetype(ctx, glsl_type);
   SpvId texel = spirv_builder_emit_image_texel_pointer(&ctx->builder, base_type, img_var, coord, sample);
   SpvId param2 = 0;

   /* The type of Value must be the same as Result Type.
    * The type of the value pointed to by Pointer must be the same as Result Type.
    */
   nir_alu_type ntype = nir_get_nir_type_for_glsl_base_type(glsl_type);
   SpvId cast_type = get_dest_type(ctx, &intr->dest, ntype);
   param = emit_bitcast(ctx, cast_type, param);

   if (intr->intrinsic == nir_intrinsic_image_deref_atomic_comp_swap) {
      param2 = get_src(ctx, &intr->src[4]);
      param2 = emit_bitcast(ctx, cast_type, param2);
   }

   handle_atomic_op(ctx, intr, texel, param, param2, ntype);
}

static void
emit_ballot(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   spirv_builder_emit_cap(&ctx->builder, SpvCapabilitySubgroupBallotKHR);
   spirv_builder_emit_extension(&ctx->builder, "SPV_KHR_shader_ballot");
   SpvId type = get_dest_uvec_type(ctx, &intr->dest);
   SpvId result = emit_unop(ctx, SpvOpSubgroupBallotKHR, type, get_src(ctx, &intr->src[0]));
   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static void
emit_read_first_invocation(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   spirv_builder_emit_cap(&ctx->builder, SpvCapabilitySubgroupBallotKHR);
   spirv_builder_emit_extension(&ctx->builder, "SPV_KHR_shader_ballot");
   SpvId type = get_dest_type(ctx, &intr->dest, nir_type_uint);
   SpvId result = emit_unop(ctx, SpvOpSubgroupFirstInvocationKHR, type, get_src(ctx, &intr->src[0]));
   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static void
emit_read_invocation(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   spirv_builder_emit_cap(&ctx->builder, SpvCapabilitySubgroupBallotKHR);
   spirv_builder_emit_extension(&ctx->builder, "SPV_KHR_shader_ballot");
   SpvId type = get_dest_type(ctx, &intr->dest, nir_type_uint);
   SpvId result = emit_binop(ctx, SpvOpSubgroupReadInvocationKHR, type,
                              get_src(ctx, &intr->src[0]),
                              get_src(ctx, &intr->src[1]));
   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static void
emit_shader_clock(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   spirv_builder_emit_cap(&ctx->builder, SpvCapabilityShaderClockKHR);
   spirv_builder_emit_extension(&ctx->builder, "SPV_KHR_shader_clock");

   SpvScope scope = get_scope(nir_intrinsic_memory_scope(intr));
   SpvId type = get_dest_type(ctx, &intr->dest, nir_type_uint);
   SpvId result = spirv_builder_emit_unop_const(&ctx->builder, SpvOpReadClockKHR, type, scope);
   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static void
emit_is_sparse_texels_resident(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   spirv_builder_emit_cap(&ctx->builder, SpvCapabilitySparseResidency);

   SpvId type = get_dest_type(ctx, &intr->dest, nir_type_uint);

   /* this will always be stored with the ssa index of the parent instr */
   assert(intr->src[0].is_ssa);
   nir_ssa_def *ssa = intr->src[0].ssa;
   assert(ssa->parent_instr->type == nir_instr_type_alu);
   nir_alu_instr *alu = nir_instr_as_alu(ssa->parent_instr);
   assert(alu->src[0].src.is_ssa);
   unsigned index = alu->src[0].src.ssa->index;
   assert(index < ctx->num_defs);
   assert(ctx->resident_defs[index] != 0);
   SpvId resident = ctx->resident_defs[index];

   SpvId result = spirv_builder_emit_unop(&ctx->builder, SpvOpImageSparseTexelsResident, type, resident);
   store_dest(ctx, &intr->dest, result, nir_type_uint);
}

static void
emit_vote(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   SpvOp op;

   switch (intr->intrinsic) {
   case nir_intrinsic_vote_all:
      op = SpvOpGroupNonUniformAll;
      break;
   case nir_intrinsic_vote_any:
      op = SpvOpGroupNonUniformAny;
      break;
   case nir_intrinsic_vote_ieq:
   case nir_intrinsic_vote_feq:
      op = SpvOpGroupNonUniformAllEqual;
      break;
   default:
      unreachable("unknown vote intrinsic");
   }
   SpvId result = spirv_builder_emit_vote(&ctx->builder, op, get_src(ctx, &intr->src[0]));
   store_dest_raw(ctx, &intr->dest, result);
}

static void
emit_intrinsic(struct ntv_context *ctx, nir_intrinsic_instr *intr)
{
   switch (intr->intrinsic) {
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

   case nir_intrinsic_load_base_instance:
      emit_load_uint_input(ctx, intr, &ctx->base_instance_var, "gl_BaseInstance", SpvBuiltInBaseInstance);
      break;

   case nir_intrinsic_load_instance_id:
      emit_load_uint_input(ctx, intr, &ctx->instance_id_var, "gl_InstanceId", SpvBuiltInInstanceIndex);
      break;

   case nir_intrinsic_load_base_vertex:
      emit_load_uint_input(ctx, intr, &ctx->base_vertex_var, "gl_BaseVertex", SpvBuiltInBaseVertex);
      break;

   case nir_intrinsic_load_draw_id:
      emit_load_uint_input(ctx, intr, &ctx->draw_id_var, "gl_DrawID", SpvBuiltInDrawIndex);
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

   case nir_intrinsic_load_sample_mask_in:
      emit_load_uint_input(ctx, intr, &ctx->sample_mask_in_var, "gl_SampleMaskIn", SpvBuiltInSampleMask);
      break;

   case nir_intrinsic_emit_vertex_with_counter:
      /* geometry shader emits copied xfb outputs just prior to EmitVertex(),
       * since that's the end of the shader
       */
      if (ctx->sinfo)
         emit_so_outputs(ctx, ctx->sinfo);
      spirv_builder_emit_vertex(&ctx->builder, nir_intrinsic_stream_id(intr));
      break;

   case nir_intrinsic_set_vertex_and_primitive_count:
      /* do nothing */
      break;

   case nir_intrinsic_end_primitive_with_counter:
      spirv_builder_end_primitive(&ctx->builder, nir_intrinsic_stream_id(intr));
      break;

   case nir_intrinsic_load_helper_invocation:
      emit_load_vec_input(ctx, intr, &ctx->helper_invocation_var, "gl_HelperInvocation", SpvBuiltInHelperInvocation, nir_type_bool);
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

   case nir_intrinsic_memory_barrier:
      spirv_builder_emit_memory_barrier(&ctx->builder, SpvScopeWorkgroup,
                                        SpvMemorySemanticsImageMemoryMask | SpvMemorySemanticsUniformMemoryMask |
                                        SpvMemorySemanticsAcquireReleaseMask);
      break;

   case nir_intrinsic_memory_barrier_image:
      spirv_builder_emit_memory_barrier(&ctx->builder, SpvScopeDevice,
                                        SpvMemorySemanticsImageMemoryMask |
                                        SpvMemorySemanticsAcquireReleaseMask);
      break;

   case nir_intrinsic_group_memory_barrier:
      spirv_builder_emit_memory_barrier(&ctx->builder, SpvScopeWorkgroup,
                                        SpvMemorySemanticsWorkgroupMemoryMask |
                                        SpvMemorySemanticsAcquireReleaseMask);
      break;

   case nir_intrinsic_memory_barrier_shared:
      spirv_builder_emit_memory_barrier(&ctx->builder, SpvScopeWorkgroup,
                                        SpvMemorySemanticsWorkgroupMemoryMask |
                                        SpvMemorySemanticsAcquireReleaseMask);
      break;

   case nir_intrinsic_control_barrier:
      spirv_builder_emit_control_barrier(&ctx->builder, SpvScopeWorkgroup,
                                         SpvScopeWorkgroup,
                                         SpvMemorySemanticsWorkgroupMemoryMask | SpvMemorySemanticsAcquireMask);
      break;

   case nir_intrinsic_interp_deref_at_centroid:
   case nir_intrinsic_interp_deref_at_sample:
   case nir_intrinsic_interp_deref_at_offset:
      emit_interpolate(ctx, intr);
      break;

   case nir_intrinsic_memory_barrier_buffer:
      spirv_builder_emit_memory_barrier(&ctx->builder, SpvScopeDevice,
                                        SpvMemorySemanticsUniformMemoryMask |
                                        SpvMemorySemanticsAcquireReleaseMask);
      break;

   case nir_intrinsic_ssbo_atomic_add:
   case nir_intrinsic_ssbo_atomic_umin:
   case nir_intrinsic_ssbo_atomic_imin:
   case nir_intrinsic_ssbo_atomic_umax:
   case nir_intrinsic_ssbo_atomic_imax:
   case nir_intrinsic_ssbo_atomic_and:
   case nir_intrinsic_ssbo_atomic_or:
   case nir_intrinsic_ssbo_atomic_xor:
   case nir_intrinsic_ssbo_atomic_exchange:
   case nir_intrinsic_ssbo_atomic_comp_swap:
      emit_ssbo_atomic_intrinsic(ctx, intr);
      break;

   case nir_intrinsic_shared_atomic_add:
   case nir_intrinsic_shared_atomic_umin:
   case nir_intrinsic_shared_atomic_imin:
   case nir_intrinsic_shared_atomic_umax:
   case nir_intrinsic_shared_atomic_imax:
   case nir_intrinsic_shared_atomic_and:
   case nir_intrinsic_shared_atomic_or:
   case nir_intrinsic_shared_atomic_xor:
   case nir_intrinsic_shared_atomic_exchange:
   case nir_intrinsic_shared_atomic_comp_swap:
      emit_shared_atomic_intrinsic(ctx, intr);
      break;

   case nir_intrinsic_begin_invocation_interlock:
   case nir_intrinsic_end_invocation_interlock:
      spirv_builder_emit_interlock(&ctx->builder, intr->intrinsic == nir_intrinsic_end_invocation_interlock);
      break;

   case nir_intrinsic_get_ssbo_size:
      emit_get_ssbo_size(ctx, intr);
      break;

   case nir_intrinsic_image_deref_store:
      emit_image_deref_store(ctx, intr);
      break;

   case nir_intrinsic_image_deref_sparse_load:
   case nir_intrinsic_image_deref_load:
      emit_image_deref_load(ctx, intr);
      break;

   case nir_intrinsic_image_deref_size:
      emit_image_deref_size(ctx, intr);
      break;

   case nir_intrinsic_image_deref_samples:
      emit_image_deref_samples(ctx, intr);
      break;

   case nir_intrinsic_image_deref_atomic_add:
   case nir_intrinsic_image_deref_atomic_umin:
   case nir_intrinsic_image_deref_atomic_imin:
   case nir_intrinsic_image_deref_atomic_umax:
   case nir_intrinsic_image_deref_atomic_imax:
   case nir_intrinsic_image_deref_atomic_and:
   case nir_intrinsic_image_deref_atomic_or:
   case nir_intrinsic_image_deref_atomic_xor:
   case nir_intrinsic_image_deref_atomic_exchange:
   case nir_intrinsic_image_deref_atomic_comp_swap:
      emit_image_intrinsic(ctx, intr);
      break;

   case nir_intrinsic_load_workgroup_id:
      emit_load_vec_input(ctx, intr, &ctx->workgroup_id_var, "gl_WorkGroupID", SpvBuiltInWorkgroupId, nir_type_uint);
      break;

   case nir_intrinsic_load_num_workgroups:
      emit_load_vec_input(ctx, intr, &ctx->num_workgroups_var, "gl_NumWorkGroups", SpvBuiltInNumWorkgroups, nir_type_uint);
      break;

   case nir_intrinsic_load_local_invocation_id:
      emit_load_vec_input(ctx, intr, &ctx->local_invocation_id_var, "gl_LocalInvocationID", SpvBuiltInLocalInvocationId, nir_type_uint);
      break;

   case nir_intrinsic_load_global_invocation_id:
      emit_load_vec_input(ctx, intr, &ctx->global_invocation_id_var, "gl_GlobalInvocationID", SpvBuiltInGlobalInvocationId, nir_type_uint);
      break;

   case nir_intrinsic_load_local_invocation_index:
      emit_load_uint_input(ctx, intr, &ctx->local_invocation_index_var, "gl_LocalInvocationIndex", SpvBuiltInLocalInvocationIndex);
      break;

#define LOAD_SHADER_BALLOT(lowercase, camelcase) \
   case nir_intrinsic_load_##lowercase: \
      emit_load_uint_input(ctx, intr, &ctx->lowercase##_var, "gl_"#camelcase, SpvBuiltIn##camelcase); \
      break

   LOAD_SHADER_BALLOT(subgroup_id, SubgroupId);
   LOAD_SHADER_BALLOT(subgroup_eq_mask, SubgroupEqMask);
   LOAD_SHADER_BALLOT(subgroup_ge_mask, SubgroupGeMask);
   LOAD_SHADER_BALLOT(subgroup_invocation, SubgroupLocalInvocationId);
   LOAD_SHADER_BALLOT(subgroup_le_mask, SubgroupLeMask);
   LOAD_SHADER_BALLOT(subgroup_lt_mask, SubgroupLtMask);
   LOAD_SHADER_BALLOT(subgroup_size, SubgroupSize);

   case nir_intrinsic_ballot:
      emit_ballot(ctx, intr);
      break;

   case nir_intrinsic_read_first_invocation:
      emit_read_first_invocation(ctx, intr);
      break;

   case nir_intrinsic_read_invocation:
      emit_read_invocation(ctx, intr);
      break;

   case nir_intrinsic_load_workgroup_size:
      assert(ctx->local_group_size_var);
      store_dest(ctx, &intr->dest, ctx->local_group_size_var, nir_type_uint);
      break;

   case nir_intrinsic_load_shared:
      emit_load_shared(ctx, intr);
      break;

   case nir_intrinsic_store_shared:
      emit_store_shared(ctx, intr);
      break;

   case nir_intrinsic_shader_clock:
      emit_shader_clock(ctx, intr);
      break;

   case nir_intrinsic_vote_all:
   case nir_intrinsic_vote_any:
   case nir_intrinsic_vote_ieq:
   case nir_intrinsic_vote_feq:
      emit_vote(ctx, intr);
      break;

   case nir_intrinsic_is_sparse_texels_resident:
      emit_is_sparse_texels_resident(ctx, intr);
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
   SpvId type = undef->def.bit_size == 1 ? get_bvec_type(ctx, undef->def.num_components) :
                                           get_uvec_type(ctx, undef->def.bit_size,
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
          tex->op == nir_texop_lod ||
          tex->op == nir_texop_tg4 ||
          tex->op == nir_texop_texture_samples ||
          tex->op == nir_texop_query_levels);
   assert(tex->texture_index == tex->sampler_index);

   SpvId coord = 0, proj = 0, bias = 0, lod = 0, dref = 0, dx = 0, dy = 0,
         const_offset = 0, offset = 0, sample = 0, tex_offset = 0, bindless = 0, min_lod = 0;
   unsigned coord_components = 0;
   nir_variable *bindless_var = NULL;
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      nir_const_value *cv;
      switch (tex->src[i].src_type) {
      case nir_tex_src_coord:
         if (tex->op == nir_texop_txf ||
             tex->op == nir_texop_txf_ms)
            coord = get_src_int(ctx, &tex->src[i].src);
         else
            coord = get_src_float(ctx, &tex->src[i].src);
         coord_components = nir_src_num_components(tex->src[i].src);
         break;

      case nir_tex_src_projector:
         assert(nir_src_num_components(tex->src[i].src) == 1);
         proj = get_src_float(ctx, &tex->src[i].src);
         assert(proj != 0);
         break;

      case nir_tex_src_offset:
         cv = nir_src_as_const_value(tex->src[i].src);
         if (cv) {
            unsigned bit_size = nir_src_bit_size(tex->src[i].src);
            unsigned num_components = nir_src_num_components(tex->src[i].src);

            SpvId components[NIR_MAX_VEC_COMPONENTS];
            for (int i = 0; i < num_components; ++i) {
               int64_t tmp = nir_const_value_as_int(cv[i], bit_size);
               components[i] = emit_int_const(ctx, bit_size, tmp);
            }

            if (num_components > 1) {
               SpvId type = get_ivec_type(ctx, bit_size, num_components);
               const_offset = spirv_builder_const_composite(&ctx->builder,
                                                            type,
                                                            components,
                                                            num_components);
            } else
               const_offset = components[0];
         } else
            offset = get_src_int(ctx, &tex->src[i].src);
         break;

      case nir_tex_src_bias:
         assert(tex->op == nir_texop_txb);
         bias = get_src_float(ctx, &tex->src[i].src);
         assert(bias != 0);
         break;

      case nir_tex_src_min_lod:
         assert(nir_src_num_components(tex->src[i].src) == 1);
         min_lod = get_src_float(ctx, &tex->src[i].src);
         assert(min_lod != 0);
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

      case nir_tex_src_texture_offset:
         tex_offset = get_src_int(ctx, &tex->src[i].src);
         break;

      case nir_tex_src_sampler_offset:
      case nir_tex_src_sampler_handle:
         /* don't care */
         break;

      case nir_tex_src_texture_handle:
         bindless = get_src(ctx, &tex->src[i].src);
         bindless_var = nir_deref_instr_get_variable(nir_src_as_deref(tex->src[i].src));
         break;

      default:
         fprintf(stderr, "texture source: %d\n", tex->src[i].src_type);
         unreachable("unknown texture source");
      }
   }

   unsigned texture_index = tex->texture_index;
   if (!tex_offset) {
      /* convert constant index back to base + offset */
      unsigned last_sampler = util_last_bit(ctx->samplers_used);
      for (unsigned i = 0; i < last_sampler; i++) {
         if (!ctx->sampler_array_sizes[i]) {
            if (i == texture_index)
               /* this is a non-array sampler, so we don't need an access chain */
               break;
         } else if (texture_index <= i + ctx->sampler_array_sizes[i] - 1) {
            /* this is the first member of a sampler array */
            tex_offset = emit_uint_const(ctx, 32, texture_index - i);
            texture_index = i;
            break;
         }
      }
   }
   SpvId image_type = bindless ? get_bare_image_type(ctx, bindless_var, true) : ctx->sampler_types[texture_index];
   assert(image_type);
   SpvId sampled_type = spirv_builder_type_sampled_image(&ctx->builder,
                                                         image_type);
   assert(sampled_type);
   assert(bindless || ctx->samplers_used & (1u << texture_index));
   SpvId sampler_id = bindless ? bindless : ctx->samplers[texture_index];
   if (tex_offset) {
       SpvId ptr = spirv_builder_type_pointer(&ctx->builder, SpvStorageClassUniformConstant, sampled_type);
       sampler_id = spirv_builder_emit_access_chain(&ctx->builder, ptr, sampler_id, &tex_offset, 1);
   }
   SpvId load = spirv_builder_emit_load(&ctx->builder, sampled_type, sampler_id);

   if (tex->is_sparse)
      tex->dest.ssa.num_components--;
   SpvId dest_type = get_dest_type(ctx, &tex->dest, tex->dest_type);

   if (!tex_instr_is_lod_allowed(tex))
      lod = 0;
   else if (ctx->stage != MESA_SHADER_FRAGMENT &&
            tex->op == nir_texop_tex && ctx->explicit_lod && !lod)
      lod = emit_float_const(ctx, 32, 0.0);
   if (tex->op == nir_texop_txs) {
      SpvId image = spirv_builder_emit_image(&ctx->builder, image_type, load);
      /* Its Dim operand must be one of 1D, 2D, 3D, or Cube
       * - OpImageQuerySizeLod specification
       *
       * Additionally, if its Dim is 1D, 2D, 3D, or Cube,
       * it must also have either an MS of 1 or a Sampled of 0 or 2.
       * - OpImageQuerySize specification
       *
       * all spirv samplers use these types
       */
      if (!lod && tex_instr_is_lod_allowed(tex))
         lod = emit_uint_const(ctx, 32, 0);
      SpvId result = spirv_builder_emit_image_query_size(&ctx->builder,
                                                         dest_type, image,
                                                         lod);
      store_dest(ctx, &tex->dest, result, tex->dest_type);
      return;
   }
   if (tex->op == nir_texop_query_levels) {
      SpvId image = spirv_builder_emit_image(&ctx->builder, image_type, load);
      SpvId result = spirv_builder_emit_image_query_levels(&ctx->builder,
                                                         dest_type, image);
      store_dest(ctx, &tex->dest, result, tex->dest_type);
      return;
   }
   if (tex->op == nir_texop_texture_samples) {
      SpvId image = spirv_builder_emit_image(&ctx->builder, image_type, load);
      SpvId result = spirv_builder_emit_unop(&ctx->builder, SpvOpImageQuerySamples,
                                             dest_type, image);
      store_dest(ctx, &tex->dest, result, tex->dest_type);
      return;
   }

   if (proj && coord_components > 0) {
      SpvId constituents[NIR_MAX_VEC_COMPONENTS + 1];
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
   SpvId actual_dest_type;
   if (dref)
      actual_dest_type =
         spirv_builder_type_float(&ctx->builder,
                                  nir_dest_bit_size(tex->dest));
   else {
      unsigned num_components = nir_dest_num_components(tex->dest);
      switch (nir_alu_type_get_base_type(tex->dest_type)) {
      case nir_type_int:
         actual_dest_type = get_ivec_type(ctx, 32, num_components);
         break;

      case nir_type_uint:
         actual_dest_type = get_uvec_type(ctx, 32, num_components);
         break;

      case nir_type_float:
         actual_dest_type = get_fvec_type(ctx, 32, num_components);
         break;

      default:
         unreachable("unexpected nir_alu_type");
      }
   }

   SpvId result;
   if (offset)
      spirv_builder_emit_cap(&ctx->builder, SpvCapabilityImageGatherExtended);
   if (min_lod)
      spirv_builder_emit_cap(&ctx->builder, SpvCapabilityMinLod);
   if (tex->op == nir_texop_txf ||
       tex->op == nir_texop_txf_ms ||
       tex->op == nir_texop_tg4) {
      SpvId image = spirv_builder_emit_image(&ctx->builder, image_type, load);

      if (tex->op == nir_texop_tg4) {
         if (const_offset)
            spirv_builder_emit_cap(&ctx->builder, SpvCapabilityImageGatherExtended);
         actual_dest_type = dest_type;
         result = spirv_builder_emit_image_gather(&ctx->builder, dest_type,
                                                 load, coord, emit_uint_const(ctx, 32, tex->component),
                                                 lod, sample, const_offset, offset, dref, tex->is_sparse);
      } else
         result = spirv_builder_emit_image_fetch(&ctx->builder, actual_dest_type,
                                                 image, coord, lod, sample, const_offset, offset, tex->is_sparse);
   } else {
      result = spirv_builder_emit_image_sample(&ctx->builder,
                                               actual_dest_type, load,
                                               coord,
                                               proj != 0,
                                               lod, bias, dref, dx, dy,
                                               const_offset, offset, min_lod, tex->is_sparse);
   }

   spirv_builder_emit_decoration(&ctx->builder, result,
                                 SpvDecorationRelaxedPrecision);

   if (tex->is_sparse)
      result = extract_sparse_load(ctx, result, actual_dest_type, &tex->dest.ssa);
   if (dref && nir_dest_num_components(tex->dest) > 1 && tex->op != nir_texop_tg4) {
      SpvId components[4] = { result, result, result, result };
      result = spirv_builder_emit_composite_construct(&ctx->builder,
                                                      dest_type,
                                                      components,
                                                      4);
   }

   if (nir_dest_bit_size(tex->dest) != 32) {
      /* convert FP32 to FP16 */
      result = emit_unop(ctx, SpvOpFConvert, dest_type, result);
   }

   store_dest(ctx, &tex->dest, result, tex->dest_type);
   if (tex->is_sparse)
      tex->dest.ssa.num_components++;
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
   SpvId base, type;
   switch (var->data.mode) {
   case nir_var_shader_in:
   case nir_var_shader_out:
   case nir_var_mem_ubo:
   case nir_var_mem_ssbo:
      base = get_src(ctx, &deref->parent);
      type = get_glsl_type(ctx, deref->type);
      break;

   case nir_var_uniform:
   case nir_var_image: {
      struct hash_entry *he = _mesa_hash_table_search(ctx->vars, var);
      assert(he);
      base = (SpvId)(intptr_t)he->data;
      type = get_image_type(ctx, var, glsl_type_is_sampler(glsl_without_array(var->type)));
      break;
   }

   default:
      unreachable("Unsupported nir_variable_mode\n");
   }

   SpvId index = get_src(ctx, &deref->arr.index);

   SpvId ptr_type = spirv_builder_type_pointer(&ctx->builder,
                                               storage_class,
                                               type);

   SpvId result = spirv_builder_emit_access_chain(&ctx->builder,
                                                  ptr_type,
                                                  base,
                                                  &index, 1);
   /* uint is a bit of a lie here, it's really just an opaque type */
   store_dest(ctx, &deref->dest, result, nir_type_uint);

   /* image ops always need to be able to get the variable to check out sampler types and such */
   if (glsl_type_is_image(glsl_without_array(var->type))) {
      uint32_t *key = ralloc_size(ctx->mem_ctx, sizeof(uint32_t));
      *key = result;
      _mesa_hash_table_insert(ctx->image_vars, key, var);
   }
}

static void
emit_deref_struct(struct ntv_context *ctx, nir_deref_instr *deref)
{
   assert(deref->deref_type == nir_deref_type_struct);
   nir_variable *var = nir_deref_instr_get_variable(deref);

   SpvStorageClass storage_class = get_storage_class(var);

   SpvId index = emit_uint_const(ctx, 32, deref->strct.index);
   SpvId type = (var->data.mode & (nir_var_mem_ubo | nir_var_mem_ssbo)) ?
                get_bo_array_type(ctx, var) :
                get_glsl_type(ctx, deref->type);

   SpvId ptr_type = spirv_builder_type_pointer(&ctx->builder,
                                               storage_class,
                                               type);

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
get_input_prim_type_mode(enum shader_prim type)
{
   switch (type) {
   case SHADER_PRIM_POINTS:
      return SpvExecutionModeInputPoints;
   case SHADER_PRIM_LINES:
   case SHADER_PRIM_LINE_LOOP:
   case SHADER_PRIM_LINE_STRIP:
      return SpvExecutionModeInputLines;
   case SHADER_PRIM_TRIANGLE_STRIP:
   case SHADER_PRIM_TRIANGLES:
   case SHADER_PRIM_TRIANGLE_FAN:
      return SpvExecutionModeTriangles;
   case SHADER_PRIM_QUADS:
   case SHADER_PRIM_QUAD_STRIP:
      return SpvExecutionModeQuads;
      break;
   case SHADER_PRIM_POLYGON:
      unreachable("handle polygons in gs");
      break;
   case SHADER_PRIM_LINES_ADJACENCY:
   case SHADER_PRIM_LINE_STRIP_ADJACENCY:
      return SpvExecutionModeInputLinesAdjacency;
   case SHADER_PRIM_TRIANGLES_ADJACENCY:
   case SHADER_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return SpvExecutionModeInputTrianglesAdjacency;
      break;
   default:
      debug_printf("unknown geometry shader input mode %u\n", type);
      unreachable("error!");
      break;
   }

   return 0;
}
static SpvExecutionMode
get_output_prim_type_mode(enum shader_prim type)
{
   switch (type) {
   case SHADER_PRIM_POINTS:
      return SpvExecutionModeOutputPoints;
   case SHADER_PRIM_LINES:
   case SHADER_PRIM_LINE_LOOP:
      unreachable("SHADER_PRIM_LINES/LINE_LOOP passed as gs output");
      break;
   case SHADER_PRIM_LINE_STRIP:
      return SpvExecutionModeOutputLineStrip;
   case SHADER_PRIM_TRIANGLE_STRIP:
      return SpvExecutionModeOutputTriangleStrip;
   case SHADER_PRIM_TRIANGLES:
   case SHADER_PRIM_TRIANGLE_FAN: //FIXME: not sure if right for output
      return SpvExecutionModeTriangles;
   case SHADER_PRIM_QUADS:
   case SHADER_PRIM_QUAD_STRIP:
      return SpvExecutionModeQuads;
   case SHADER_PRIM_POLYGON:
      unreachable("handle polygons in gs");
      break;
   case SHADER_PRIM_LINES_ADJACENCY:
   case SHADER_PRIM_LINE_STRIP_ADJACENCY:
      unreachable("handle line adjacency in gs");
      break;
   case SHADER_PRIM_TRIANGLES_ADJACENCY:
   case SHADER_PRIM_TRIANGLE_STRIP_ADJACENCY:
      unreachable("handle triangle adjacency in gs");
      break;
   default:
      debug_printf("unknown geometry shader output mode %u\n", type);
      unreachable("error!");
      break;
   }

   return 0;
}

static SpvExecutionMode
get_depth_layout_mode(enum gl_frag_depth_layout depth_layout)
{
   switch (depth_layout) {
   case FRAG_DEPTH_LAYOUT_NONE:
   case FRAG_DEPTH_LAYOUT_ANY:
      return SpvExecutionModeDepthReplacing;
   case FRAG_DEPTH_LAYOUT_GREATER:
      return SpvExecutionModeDepthGreater;
   case FRAG_DEPTH_LAYOUT_LESS:
      return SpvExecutionModeDepthLess;
   case FRAG_DEPTH_LAYOUT_UNCHANGED:
      return SpvExecutionModeDepthUnchanged;
   default:
      unreachable("unexpected depth layout");
   }
}

static SpvExecutionMode
get_primitive_mode(enum tess_primitive_mode primitive_mode)
{
   switch (primitive_mode) {
   case TESS_PRIMITIVE_TRIANGLES: return SpvExecutionModeTriangles;
   case TESS_PRIMITIVE_QUADS: return SpvExecutionModeQuads;
   case TESS_PRIMITIVE_ISOLINES: return SpvExecutionModeIsolines;
   default:
      unreachable("unknown tess prim type!");
   }
}

static SpvExecutionMode
get_spacing(enum gl_tess_spacing spacing)
{
   switch (spacing) {
   case TESS_SPACING_EQUAL:
      return SpvExecutionModeSpacingEqual;
   case TESS_SPACING_FRACTIONAL_ODD:
      return SpvExecutionModeSpacingFractionalOdd;
   case TESS_SPACING_FRACTIONAL_EVEN:
      return SpvExecutionModeSpacingFractionalEven;
   default:
      unreachable("unknown tess spacing!");
   }
}

struct spirv_shader *
nir_to_spirv(struct nir_shader *s, const struct zink_shader_info *sinfo, uint32_t spirv_version)
{
   struct spirv_shader *ret = NULL;

   struct ntv_context ctx = {0};
   ctx.mem_ctx = ralloc_context(NULL);
   ctx.builder.mem_ctx = ctx.mem_ctx;
   assert(spirv_version >= SPIRV_VERSION(1, 0));
   ctx.spirv_1_4_interfaces = spirv_version >= SPIRV_VERSION(1, 4);

   ctx.glsl_types = _mesa_pointer_hash_table_create(ctx.mem_ctx);
   ctx.bo_types = _mesa_pointer_hash_table_create(ctx.mem_ctx);
   if (!ctx.glsl_types || !ctx.bo_types)
      goto fail;

   spirv_builder_emit_cap(&ctx.builder, SpvCapabilityShader);
   if (s->info.image_buffers != 0)
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityImageBuffer);
   spirv_builder_emit_cap(&ctx.builder, SpvCapabilitySampledBuffer);

   switch (s->info.stage) {
   case MESA_SHADER_FRAGMENT:
      if (s->info.fs.post_depth_coverage &&
          BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_SAMPLE_MASK_IN))
         spirv_builder_emit_cap(&ctx.builder, SpvCapabilitySampleMaskPostDepthCoverage);
      if (s->info.fs.uses_sample_shading)
         spirv_builder_emit_cap(&ctx.builder, SpvCapabilitySampleRateShading);
      break;

   case MESA_SHADER_VERTEX:
      if (BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID) ||
          BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_BASE_INSTANCE) ||
          BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_BASE_VERTEX)) {
         spirv_builder_emit_extension(&ctx.builder, "SPV_KHR_shader_draw_parameters");
         spirv_builder_emit_cap(&ctx.builder, SpvCapabilityDrawParameters);
      }
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

   default: ;
   }

   if (s->info.stage < MESA_SHADER_GEOMETRY) {
      if (s->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_LAYER) ||
          s->info.inputs_read & BITFIELD64_BIT(VARYING_SLOT_LAYER)) {
         if (spirv_version >= SPIRV_VERSION(1, 5))
            spirv_builder_emit_cap(&ctx.builder, SpvCapabilityShaderLayer);
         else {
            spirv_builder_emit_extension(&ctx.builder, "SPV_EXT_shader_viewport_index_layer");
            spirv_builder_emit_cap(&ctx.builder, SpvCapabilityShaderViewportIndexLayerEXT);
         }
      }
   }

   if (s->info.num_ssbos)
      spirv_builder_emit_extension(&ctx.builder, "SPV_KHR_storage_buffer_storage_class");

   if (s->info.stage < MESA_SHADER_FRAGMENT &&
       s->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_VIEWPORT)) {
      if (s->info.stage < MESA_SHADER_GEOMETRY)
         spirv_builder_emit_cap(&ctx.builder, SpvCapabilityShaderViewportIndex);
      else
         spirv_builder_emit_cap(&ctx.builder, SpvCapabilityMultiViewport);
   }

   if (s->info.num_textures) {
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilitySampled1D);
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityImageQuery);
   }

   if (s->info.num_images) {
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityImage1D);
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityImageQuery);
   }

   ctx.stage = s->info.stage;
   ctx.sinfo = sinfo;
   ctx.GLSL_std_450 = spirv_builder_import(&ctx.builder, "GLSL.std.450");
   ctx.explicit_lod = true;
   spirv_builder_emit_source(&ctx.builder, SpvSourceLanguageUnknown, 0);

   if (s->info.stage == MESA_SHADER_COMPUTE) {
      SpvAddressingModel model;
      if (s->info.cs.ptr_size == 32)
         model = SpvAddressingModelPhysical32;
      else if (s->info.cs.ptr_size == 64)
         model = SpvAddressingModelPhysical64;
      else
         model = SpvAddressingModelLogical;
      spirv_builder_emit_mem_model(&ctx.builder, model,
                                   SpvMemoryModelGLSL450);
   } else
      spirv_builder_emit_mem_model(&ctx.builder, SpvAddressingModelLogical,
                                   SpvMemoryModelGLSL450);

   if (s->info.stage == MESA_SHADER_FRAGMENT &&
       s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL)) {
      spirv_builder_emit_extension(&ctx.builder, "SPV_EXT_shader_stencil_export");
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilityStencilExportEXT);
   }

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

   ctx.image_vars = _mesa_hash_table_create(ctx.mem_ctx, _mesa_hash_u32,
                                      _mesa_key_u32_equal);

   ctx.so_outputs = _mesa_hash_table_create(ctx.mem_ctx, _mesa_hash_u32,
                                            _mesa_key_u32_equal);

   nir_foreach_variable_with_modes(var, s, nir_var_mem_push_const)
      input_var_init(&ctx, var);

   nir_foreach_shader_in_variable(var, s)
      emit_input(&ctx, var);

   int max_output = -1;
   nir_foreach_shader_out_variable(var, s) {
      /* ignore SPIR-V built-ins, tagged with a sentinel value */
      if (var->data.driver_location != UINT_MAX) {
         assert(var->data.driver_location < INT_MAX);
         max_output = MAX2(max_output, (int)var->data.driver_location);
      }
      emit_output(&ctx, var);
   }


   if (sinfo->last_vertex)
      emit_so_info(&ctx, sinfo, max_output + 1);

   /* we have to reverse iterate to match what's done in zink_compiler.c */
   foreach_list_typed_reverse(nir_variable, var, node, &s->variables)
      if (_nir_shader_variable_has_mode(var, nir_var_uniform |
                                        nir_var_image |
                                        nir_var_mem_ubo |
                                        nir_var_mem_ssbo))
         emit_uniform(&ctx, var);

   switch (s->info.stage) {
   case MESA_SHADER_FRAGMENT:
      spirv_builder_emit_exec_mode(&ctx.builder, entry_point,
                                   SpvExecutionModeOriginUpperLeft);
      if (s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH))
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point,
                                      get_depth_layout_mode(s->info.fs.depth_layout));
      if (s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL))
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point,
                                      SpvExecutionModeStencilRefReplacingEXT);
      if (s->info.fs.early_fragment_tests)
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point,
                                      SpvExecutionModeEarlyFragmentTests);
      if (s->info.fs.post_depth_coverage) {
         spirv_builder_emit_extension(&ctx.builder, "SPV_KHR_post_depth_coverage");
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point,
                                      SpvExecutionModePostDepthCoverage);
      }

      if (s->info.fs.pixel_interlock_ordered || s->info.fs.pixel_interlock_unordered ||
          s->info.fs.sample_interlock_ordered || s->info.fs.sample_interlock_unordered)
         spirv_builder_emit_extension(&ctx.builder, "SPV_EXT_fragment_shader_interlock");
      if (s->info.fs.pixel_interlock_ordered || s->info.fs.pixel_interlock_unordered)
         spirv_builder_emit_cap(&ctx.builder, SpvCapabilityFragmentShaderPixelInterlockEXT);
      if (s->info.fs.sample_interlock_ordered || s->info.fs.sample_interlock_unordered)
         spirv_builder_emit_cap(&ctx.builder, SpvCapabilityFragmentShaderSampleInterlockEXT);
      if (s->info.fs.pixel_interlock_ordered)
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModePixelInterlockOrderedEXT);
      if (s->info.fs.pixel_interlock_unordered)
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModePixelInterlockUnorderedEXT);
      if (s->info.fs.sample_interlock_ordered)
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModeSampleInterlockOrderedEXT);
      if (s->info.fs.sample_interlock_unordered)
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModeSampleInterlockUnorderedEXT);
      break;
   case MESA_SHADER_TESS_CTRL:
      spirv_builder_emit_exec_mode_literal(&ctx.builder, entry_point,
                                           SpvExecutionModeOutputVertices,
                                           s->info.tess.tcs_vertices_out);
      break;
   case MESA_SHADER_TESS_EVAL:
      spirv_builder_emit_exec_mode(&ctx.builder, entry_point,
                                   get_primitive_mode(s->info.tess._primitive_mode));
      spirv_builder_emit_exec_mode(&ctx.builder, entry_point,
                                   s->info.tess.ccw ? SpvExecutionModeVertexOrderCcw
                                                    : SpvExecutionModeVertexOrderCw);
      spirv_builder_emit_exec_mode(&ctx.builder, entry_point,
                                   get_spacing(s->info.tess.spacing));
      if (s->info.tess.point_mode)
         spirv_builder_emit_exec_mode(&ctx.builder, entry_point, SpvExecutionModePointMode);
      break;
   case MESA_SHADER_GEOMETRY:
      spirv_builder_emit_exec_mode(&ctx.builder, entry_point,
                                   get_input_prim_type_mode(s->info.gs.input_primitive));
      spirv_builder_emit_exec_mode(&ctx.builder, entry_point,
                                   get_output_prim_type_mode(s->info.gs.output_primitive));
      spirv_builder_emit_exec_mode_literal(&ctx.builder, entry_point,
                                           SpvExecutionModeInvocations,
                                           s->info.gs.invocations);
      spirv_builder_emit_exec_mode_literal(&ctx.builder, entry_point,
                                           SpvExecutionModeOutputVertices,
                                           s->info.gs.vertices_out);
      break;
   case MESA_SHADER_COMPUTE:
      if (s->info.shared_size)
         create_shared_block(&ctx, s->info.shared_size);

      if (s->info.workgroup_size[0] || s->info.workgroup_size[1] || s->info.workgroup_size[2])
         spirv_builder_emit_exec_mode_literal3(&ctx.builder, entry_point, SpvExecutionModeLocalSize,
                                               (uint32_t[3]){(uint32_t)s->info.workgroup_size[0], (uint32_t)s->info.workgroup_size[1],
                                               (uint32_t)s->info.workgroup_size[2]});
      else {
         SpvId sizes[3];
         uint32_t ids[] = {ZINK_WORKGROUP_SIZE_X, ZINK_WORKGROUP_SIZE_Y, ZINK_WORKGROUP_SIZE_Z};
         const char *names[] = {"x", "y", "z"};
         for (int i = 0; i < 3; i ++) {
            sizes[i] = spirv_builder_spec_const_uint(&ctx.builder, 32);
            spirv_builder_emit_specid(&ctx.builder, sizes[i], ids[i]);
            spirv_builder_emit_name(&ctx.builder, sizes[i], names[i]);
         }
         SpvId var_type = get_uvec_type(&ctx, 32, 3);
         ctx.local_group_size_var = spirv_builder_spec_const_composite(&ctx.builder, var_type, sizes, 3);
         spirv_builder_emit_name(&ctx.builder, ctx.local_group_size_var, "gl_LocalGroupSize");
         spirv_builder_emit_builtin(&ctx.builder, ctx.local_group_size_var, SpvBuiltInWorkgroupSize);
      }
      break;
   default:
      break;
   }
   if (BITSET_TEST_RANGE(s->info.system_values_read, SYSTEM_VALUE_SUBGROUP_SIZE, SYSTEM_VALUE_SUBGROUP_LT_MASK)) {
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilitySubgroupBallotKHR);
      spirv_builder_emit_extension(&ctx.builder, "SPV_KHR_shader_ballot");
   }
   if (s->info.has_transform_feedback_varyings) {
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
   if (sinfo->have_sparse) {
      spirv_builder_emit_cap(&ctx.builder, SpvCapabilitySparseResidency);
      /* this could be huge, so only alloc if needed since it's extremely unlikely to
       * ever be used by anything except cts
       */
      ctx.resident_defs = ralloc_array_size(ctx.mem_ctx,
                                            sizeof(SpvId), entry->ssa_alloc);
      if (!ctx.resident_defs)
         goto fail;
   }
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

   /* vertex/tess shader emits copied xfb outputs at the end of the shader */
   if (sinfo->last_vertex && (ctx.stage == MESA_SHADER_VERTEX || ctx.stage == MESA_SHADER_TESS_EVAL))
      emit_so_outputs(&ctx, sinfo);

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

   ret->num_words = spirv_builder_get_words(&ctx.builder, ret->words, num_words, spirv_version);
   assert(ret->num_words == num_words);

   ralloc_free(ctx.mem_ctx);

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
   ralloc_free(s);
}
