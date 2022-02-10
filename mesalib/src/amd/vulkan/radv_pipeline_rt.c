/*
 * Copyright © 2021 Google
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

#include "radv_acceleration_structure.h"
#include "radv_debug.h"
#include "radv_private.h"
#include "radv_shader.h"

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "nir/nir_builtin_builder.h"

static VkRayTracingPipelineCreateInfoKHR
radv_create_merged_rt_create_info(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo)
{
   VkRayTracingPipelineCreateInfoKHR local_create_info = *pCreateInfo;
   uint32_t total_stages = pCreateInfo->stageCount;
   uint32_t total_groups = pCreateInfo->groupCount;

   if (pCreateInfo->pLibraryInfo) {
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         RADV_FROM_HANDLE(radv_pipeline, library, pCreateInfo->pLibraryInfo->pLibraries[i]);
         total_stages += library->library.stage_count;
         total_groups += library->library.group_count;
      }
   }
   VkPipelineShaderStageCreateInfo *stages = NULL;
   VkRayTracingShaderGroupCreateInfoKHR *groups = NULL;
   local_create_info.stageCount = total_stages;
   local_create_info.groupCount = total_groups;
   local_create_info.pStages = stages =
      malloc(sizeof(VkPipelineShaderStageCreateInfo) * total_stages);
   local_create_info.pGroups = groups =
      malloc(sizeof(VkRayTracingShaderGroupCreateInfoKHR) * total_groups);
   if (!local_create_info.pStages || !local_create_info.pGroups)
      return local_create_info;

   total_stages = pCreateInfo->stageCount;
   total_groups = pCreateInfo->groupCount;
   for (unsigned j = 0; j < pCreateInfo->stageCount; ++j)
      stages[j] = pCreateInfo->pStages[j];
   for (unsigned j = 0; j < pCreateInfo->groupCount; ++j)
      groups[j] = pCreateInfo->pGroups[j];

   if (pCreateInfo->pLibraryInfo) {
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         RADV_FROM_HANDLE(radv_pipeline, library, pCreateInfo->pLibraryInfo->pLibraries[i]);
         for (unsigned j = 0; j < library->library.stage_count; ++j)
            stages[total_stages + j] = library->library.stages[j];
         for (unsigned j = 0; j < library->library.group_count; ++j) {
            VkRayTracingShaderGroupCreateInfoKHR *dst = &groups[total_groups + j];
            *dst = library->library.groups[j];
            if (dst->generalShader != VK_SHADER_UNUSED_KHR)
               dst->generalShader += total_stages;
            if (dst->closestHitShader != VK_SHADER_UNUSED_KHR)
               dst->closestHitShader += total_stages;
            if (dst->anyHitShader != VK_SHADER_UNUSED_KHR)
               dst->anyHitShader += total_stages;
            if (dst->intersectionShader != VK_SHADER_UNUSED_KHR)
               dst->intersectionShader += total_stages;
         }
         total_stages += library->library.stage_count;
         total_groups += library->library.group_count;
      }
   }
   return local_create_info;
}

static VkResult
radv_rt_pipeline_library_create(VkDevice _device, VkPipelineCache _cache,
                                const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_pipeline *pipeline;

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pipeline->base, VK_OBJECT_TYPE_PIPELINE);
   pipeline->type = RADV_PIPELINE_LIBRARY;

   VkRayTracingPipelineCreateInfoKHR local_create_info =
      radv_create_merged_rt_create_info(pCreateInfo);
   if (!local_create_info.pStages || !local_create_info.pGroups)
      goto fail;

   if (local_create_info.stageCount) {
      size_t size = sizeof(VkPipelineShaderStageCreateInfo) * local_create_info.stageCount;
      pipeline->library.stage_count = local_create_info.stageCount;
      pipeline->library.stages = malloc(size);
      if (!pipeline->library.stages)
         goto fail;
      memcpy(pipeline->library.stages, local_create_info.pStages, size);
   }

   if (local_create_info.groupCount) {
      size_t size = sizeof(VkRayTracingShaderGroupCreateInfoKHR) * local_create_info.groupCount;
      pipeline->library.group_count = local_create_info.groupCount;
      pipeline->library.groups = malloc(size);
      if (!pipeline->library.groups)
         goto fail;
      memcpy(pipeline->library.groups, local_create_info.pGroups, size);
   }

   *pPipeline = radv_pipeline_to_handle(pipeline);

   free((void *)local_create_info.pGroups);
   free((void *)local_create_info.pStages);
   return VK_SUCCESS;
fail:
   free(pipeline->library.groups);
   free(pipeline->library.stages);
   free((void *)local_create_info.pGroups);
   free((void *)local_create_info.pStages);
   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

/*
 * Global variables for an RT pipeline
 */
struct rt_variables {
   /* idx of the next shader to run in the next iteration of the main loop */
   nir_variable *idx;

   /* scratch offset of the argument area relative to stack_ptr */
   nir_variable *arg;

   nir_variable *stack_ptr;

   /* global address of the SBT entry used for the shader */
   nir_variable *shader_record_ptr;

   /* trace_ray arguments */
   nir_variable *accel_struct;
   nir_variable *flags;
   nir_variable *cull_mask;
   nir_variable *sbt_offset;
   nir_variable *sbt_stride;
   nir_variable *miss_index;
   nir_variable *origin;
   nir_variable *tmin;
   nir_variable *direction;
   nir_variable *tmax;

   /* from the BTAS instance currently being visited */
   nir_variable *custom_instance_and_mask;

   /* Properties of the primitive currently being visited. */
   nir_variable *primitive_id;
   nir_variable *geometry_id_and_flags;
   nir_variable *instance_id;
   nir_variable *instance_addr;
   nir_variable *hit_kind;
   nir_variable *opaque;

   /* Safeguard to ensure we don't end up in an infinite loop of non-existing case. Should not be
    * needed but is extra anti-hang safety during bring-up. */
   nir_variable *main_loop_case_visited;

   /* Output variable for intersection & anyhit shaders. */
   nir_variable *ahit_status;

   /* Array of stack size struct for recording the max stack size for each group. */
   struct radv_pipeline_shader_stack_size *stack_sizes;
   unsigned group_idx;
};

static struct rt_variables
create_rt_variables(nir_shader *shader, struct radv_pipeline_shader_stack_size *stack_sizes)
{
   struct rt_variables vars = {
      NULL,
   };
   vars.idx = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "idx");
   vars.arg = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "arg");
   vars.stack_ptr = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "stack_ptr");
   vars.shader_record_ptr =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "shader_record_ptr");

   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);
   vars.accel_struct =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "accel_struct");
   vars.flags = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "ray_flags");
   vars.cull_mask = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "cull_mask");
   vars.sbt_offset =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "sbt_offset");
   vars.sbt_stride =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "sbt_stride");
   vars.miss_index =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "miss_index");
   vars.origin = nir_variable_create(shader, nir_var_shader_temp, vec3_type, "ray_origin");
   vars.tmin = nir_variable_create(shader, nir_var_shader_temp, glsl_float_type(), "ray_tmin");
   vars.direction = nir_variable_create(shader, nir_var_shader_temp, vec3_type, "ray_direction");
   vars.tmax = nir_variable_create(shader, nir_var_shader_temp, glsl_float_type(), "ray_tmax");

   vars.custom_instance_and_mask = nir_variable_create(
      shader, nir_var_shader_temp, glsl_uint_type(), "custom_instance_and_mask");
   vars.primitive_id =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "primitive_id");
   vars.geometry_id_and_flags =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "geometry_id_and_flags");
   vars.instance_id =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "instance_id");
   vars.instance_addr =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "instance_addr");
   vars.hit_kind = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "hit_kind");
   vars.opaque = nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "opaque");

   vars.main_loop_case_visited =
      nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "main_loop_case_visited");
   vars.ahit_status =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "ahit_status");

   vars.stack_sizes = stack_sizes;
   return vars;
}

/*
 * Remap all the variables between the two rt_variables struct for inlining.
 */
static void
map_rt_variables(struct hash_table *var_remap, struct rt_variables *src,
                 const struct rt_variables *dst)
{
   _mesa_hash_table_insert(var_remap, src->idx, dst->idx);
   _mesa_hash_table_insert(var_remap, src->arg, dst->arg);
   _mesa_hash_table_insert(var_remap, src->stack_ptr, dst->stack_ptr);
   _mesa_hash_table_insert(var_remap, src->shader_record_ptr, dst->shader_record_ptr);

   _mesa_hash_table_insert(var_remap, src->accel_struct, dst->accel_struct);
   _mesa_hash_table_insert(var_remap, src->flags, dst->flags);
   _mesa_hash_table_insert(var_remap, src->cull_mask, dst->cull_mask);
   _mesa_hash_table_insert(var_remap, src->sbt_offset, dst->sbt_offset);
   _mesa_hash_table_insert(var_remap, src->sbt_stride, dst->sbt_stride);
   _mesa_hash_table_insert(var_remap, src->miss_index, dst->miss_index);
   _mesa_hash_table_insert(var_remap, src->origin, dst->origin);
   _mesa_hash_table_insert(var_remap, src->tmin, dst->tmin);
   _mesa_hash_table_insert(var_remap, src->direction, dst->direction);
   _mesa_hash_table_insert(var_remap, src->tmax, dst->tmax);

   _mesa_hash_table_insert(var_remap, src->custom_instance_and_mask, dst->custom_instance_and_mask);
   _mesa_hash_table_insert(var_remap, src->primitive_id, dst->primitive_id);
   _mesa_hash_table_insert(var_remap, src->geometry_id_and_flags, dst->geometry_id_and_flags);
   _mesa_hash_table_insert(var_remap, src->instance_id, dst->instance_id);
   _mesa_hash_table_insert(var_remap, src->instance_addr, dst->instance_addr);
   _mesa_hash_table_insert(var_remap, src->hit_kind, dst->hit_kind);
   _mesa_hash_table_insert(var_remap, src->opaque, dst->opaque);
   _mesa_hash_table_insert(var_remap, src->ahit_status, dst->ahit_status);

   src->stack_sizes = dst->stack_sizes;
   src->group_idx = dst->group_idx;
}

/*
 * Create a copy of the global rt variables where the primitive/instance related variables are
 * independent.This is needed as we need to keep the old values of the global variables around
 * in case e.g. an anyhit shader reject the collision. So there are inner variables that get copied
 * to the outer variables once we commit to a better hit.
 */
static struct rt_variables
create_inner_vars(nir_builder *b, const struct rt_variables *vars)
{
   struct rt_variables inner_vars = *vars;
   inner_vars.idx =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "inner_idx");
   inner_vars.shader_record_ptr = nir_variable_create(
      b->shader, nir_var_shader_temp, glsl_uint64_t_type(), "inner_shader_record_ptr");
   inner_vars.primitive_id =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "inner_primitive_id");
   inner_vars.geometry_id_and_flags = nir_variable_create(
      b->shader, nir_var_shader_temp, glsl_uint_type(), "inner_geometry_id_and_flags");
   inner_vars.tmax =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_float_type(), "inner_tmax");
   inner_vars.instance_id =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "inner_instance_id");
   inner_vars.instance_addr = nir_variable_create(b->shader, nir_var_shader_temp,
                                                  glsl_uint64_t_type(), "inner_instance_addr");
   inner_vars.hit_kind =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "inner_hit_kind");
   inner_vars.custom_instance_and_mask = nir_variable_create(
      b->shader, nir_var_shader_temp, glsl_uint_type(), "inner_custom_instance_and_mask");

   return inner_vars;
}

/* The hit attributes are stored on the stack. This is the offset compared to the current stack
 * pointer of where the hit attrib is stored. */
const uint32_t RADV_HIT_ATTRIB_OFFSET = -(16 + RADV_MAX_HIT_ATTRIB_SIZE);

static void
insert_rt_return(nir_builder *b, const struct rt_variables *vars)
{
   nir_store_var(b, vars->stack_ptr,
                 nir_iadd(b, nir_load_var(b, vars->stack_ptr), nir_imm_int(b, -16)), 1);
   nir_store_var(b, vars->idx,
                 nir_load_scratch(b, 1, 32, nir_load_var(b, vars->stack_ptr), .align_mul = 16), 1);
}

enum sbt_type {
   SBT_RAYGEN,
   SBT_MISS,
   SBT_HIT,
   SBT_CALLABLE,
};

static nir_ssa_def *
get_sbt_ptr(nir_builder *b, nir_ssa_def *idx, enum sbt_type binding)
{
   nir_ssa_def *desc = nir_load_sbt_amd(b, 4, .binding = binding);
   nir_ssa_def *base_addr = nir_pack_64_2x32(b, nir_channels(b, desc, 0x3));
   nir_ssa_def *stride = nir_channel(b, desc, 2);

   nir_ssa_def *ret = nir_imul(b, idx, stride);
   ret = nir_iadd(b, base_addr, nir_u2u64(b, ret));

   return ret;
}

static void
load_sbt_entry(nir_builder *b, const struct rt_variables *vars, nir_ssa_def *idx,
               enum sbt_type binding, unsigned offset)
{
   nir_ssa_def *addr = get_sbt_ptr(b, idx, binding);

   nir_ssa_def *load_addr = addr;
   if (offset)
      load_addr = nir_iadd(b, load_addr, nir_imm_int64(b, offset));
   nir_ssa_def *v_idx = nir_build_load_global(b, 1, 32, load_addr);

   nir_store_var(b, vars->idx, v_idx, 1);

   nir_ssa_def *record_addr = nir_iadd(b, addr, nir_imm_int64(b, RADV_RT_HANDLE_SIZE));
   nir_store_var(b, vars->shader_record_ptr, record_addr, 1);
}

static nir_ssa_def *
nir_build_vec3_mat_mult(nir_builder *b, nir_ssa_def *vec, nir_ssa_def *matrix[], bool translation)
{
   nir_ssa_def *result_components[3] = {
      nir_channel(b, matrix[0], 3),
      nir_channel(b, matrix[1], 3),
      nir_channel(b, matrix[2], 3),
   };
   for (unsigned i = 0; i < 3; ++i) {
      for (unsigned j = 0; j < 3; ++j) {
         nir_ssa_def *v =
            nir_fmul(b, nir_channels(b, vec, 1 << j), nir_channels(b, matrix[i], 1 << j));
         result_components[i] = (translation || j) ? nir_fadd(b, result_components[i], v) : v;
      }
   }
   return nir_vec(b, result_components, 3);
}

static nir_ssa_def *
nir_build_vec3_mat_mult_pre(nir_builder *b, nir_ssa_def *vec, nir_ssa_def *matrix[])
{
   nir_ssa_def *result_components[3] = {
      nir_channel(b, matrix[0], 3),
      nir_channel(b, matrix[1], 3),
      nir_channel(b, matrix[2], 3),
   };
   return nir_build_vec3_mat_mult(b, nir_fsub(b, vec, nir_vec(b, result_components, 3)), matrix,
                                  false);
}

static void
nir_build_wto_matrix_load(nir_builder *b, nir_ssa_def *instance_addr, nir_ssa_def **out)
{
   unsigned offset = offsetof(struct radv_bvh_instance_node, wto_matrix);
   for (unsigned i = 0; i < 3; ++i) {
      out[i] = nir_build_load_global(b, 4, 32,
                                     nir_iadd(b, instance_addr, nir_imm_int64(b, offset + i * 16)),
                                     .align_mul = 64, .align_offset = offset + i * 16);
   }
}

/* This lowers all the RT instructions that we do not want to pass on to the combined shader and
 * that we can implement using the variables from the shader we are going to inline into. */
static void
lower_rt_instructions(nir_shader *shader, struct rt_variables *vars, unsigned call_idx_base)
{
   nir_builder b_shader;
   nir_builder_init(&b_shader, nir_shader_get_entrypoint(shader));

   nir_foreach_block (block, nir_shader_get_entrypoint(shader)) {
      nir_foreach_instr_safe (instr, block) {
         switch (instr->type) {
         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            switch (intr->intrinsic) {
            case nir_intrinsic_rt_execute_callable: {
               uint32_t size = align(nir_intrinsic_stack_size(intr), 16) + RADV_MAX_HIT_ATTRIB_SIZE;
               uint32_t ret = call_idx_base + nir_intrinsic_call_idx(intr) + 1;
               b_shader.cursor = nir_instr_remove(instr);

               nir_store_var(&b_shader, vars->stack_ptr,
                             nir_iadd(&b_shader, nir_load_var(&b_shader, vars->stack_ptr),
                                      nir_imm_int(&b_shader, size)),
                             1);
               nir_store_scratch(&b_shader, nir_imm_int(&b_shader, ret),
                                 nir_load_var(&b_shader, vars->stack_ptr), .align_mul = 16);

               nir_store_var(&b_shader, vars->stack_ptr,
                             nir_iadd(&b_shader, nir_load_var(&b_shader, vars->stack_ptr),
                                      nir_imm_int(&b_shader, 16)),
                             1);
               load_sbt_entry(&b_shader, vars, intr->src[0].ssa, SBT_CALLABLE, 0);

               nir_store_var(
                  &b_shader, vars->arg,
                  nir_isub(&b_shader, intr->src[1].ssa, nir_imm_int(&b_shader, size + 16)), 1);

               vars->stack_sizes[vars->group_idx].recursive_size =
                  MAX2(vars->stack_sizes[vars->group_idx].recursive_size, size + 16);
               break;
            }
            case nir_intrinsic_rt_trace_ray: {
               uint32_t size = align(nir_intrinsic_stack_size(intr), 16) + RADV_MAX_HIT_ATTRIB_SIZE;
               uint32_t ret = call_idx_base + nir_intrinsic_call_idx(intr) + 1;
               b_shader.cursor = nir_instr_remove(instr);

               nir_store_var(&b_shader, vars->stack_ptr,
                             nir_iadd(&b_shader, nir_load_var(&b_shader, vars->stack_ptr),
                                      nir_imm_int(&b_shader, size)),
                             1);
               nir_store_scratch(&b_shader, nir_imm_int(&b_shader, ret),
                                 nir_load_var(&b_shader, vars->stack_ptr), .align_mul = 16);

               nir_store_var(&b_shader, vars->stack_ptr,
                             nir_iadd(&b_shader, nir_load_var(&b_shader, vars->stack_ptr),
                                      nir_imm_int(&b_shader, 16)),
                             1);

               nir_store_var(&b_shader, vars->idx, nir_imm_int(&b_shader, 1), 1);
               nir_store_var(
                  &b_shader, vars->arg,
                  nir_isub(&b_shader, intr->src[10].ssa, nir_imm_int(&b_shader, size + 16)), 1);

               vars->stack_sizes[vars->group_idx].recursive_size =
                  MAX2(vars->stack_sizes[vars->group_idx].recursive_size, size + 16);

               /* Per the SPIR-V extension spec we have to ignore some bits for some arguments. */
               nir_store_var(&b_shader, vars->accel_struct, intr->src[0].ssa, 0x1);
               nir_store_var(&b_shader, vars->flags, intr->src[1].ssa, 0x1);
               nir_store_var(&b_shader, vars->cull_mask,
                             nir_iand(&b_shader, intr->src[2].ssa, nir_imm_int(&b_shader, 0xff)),
                             0x1);
               nir_store_var(&b_shader, vars->sbt_offset,
                             nir_iand(&b_shader, intr->src[3].ssa, nir_imm_int(&b_shader, 0xf)),
                             0x1);
               nir_store_var(&b_shader, vars->sbt_stride,
                             nir_iand(&b_shader, intr->src[4].ssa, nir_imm_int(&b_shader, 0xf)),
                             0x1);
               nir_store_var(&b_shader, vars->miss_index,
                             nir_iand(&b_shader, intr->src[5].ssa, nir_imm_int(&b_shader, 0xffff)),
                             0x1);
               nir_store_var(&b_shader, vars->origin, intr->src[6].ssa, 0x7);
               nir_store_var(&b_shader, vars->tmin, intr->src[7].ssa, 0x1);
               nir_store_var(&b_shader, vars->direction, intr->src[8].ssa, 0x7);
               nir_store_var(&b_shader, vars->tmax, intr->src[9].ssa, 0x1);
               break;
            }
            case nir_intrinsic_rt_resume: {
               uint32_t size = align(nir_intrinsic_stack_size(intr), 16) + RADV_MAX_HIT_ATTRIB_SIZE;
               b_shader.cursor = nir_instr_remove(instr);

               nir_store_var(&b_shader, vars->stack_ptr,
                             nir_iadd(&b_shader, nir_load_var(&b_shader, vars->stack_ptr),
                                      nir_imm_int(&b_shader, -size)),
                             1);
               break;
            }
            case nir_intrinsic_rt_return_amd: {
               b_shader.cursor = nir_instr_remove(instr);

               if (shader->info.stage == MESA_SHADER_RAYGEN) {
                  nir_store_var(&b_shader, vars->idx, nir_imm_int(&b_shader, 0), 1);
                  break;
               }
               insert_rt_return(&b_shader, vars);
               break;
            }
            case nir_intrinsic_load_scratch: {
               b_shader.cursor = nir_before_instr(instr);
               nir_instr_rewrite_src_ssa(
                  instr, &intr->src[0],
                  nir_iadd(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), intr->src[0].ssa));
               break;
            }
            case nir_intrinsic_store_scratch: {
               b_shader.cursor = nir_before_instr(instr);
               nir_instr_rewrite_src_ssa(
                  instr, &intr->src[1],
                  nir_iadd(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), intr->src[1].ssa));
               break;
            }
            case nir_intrinsic_load_rt_arg_scratch_offset_amd: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_var(&b_shader, vars->arg);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_load_shader_record_ptr: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_var(&b_shader, vars->shader_record_ptr);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_load_ray_launch_id: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_global_invocation_id(&b_shader, 32);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_load_ray_t_min: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_var(&b_shader, vars->tmin);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_load_ray_t_max: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_var(&b_shader, vars->tmax);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_load_ray_world_origin: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_var(&b_shader, vars->origin);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_load_ray_world_direction: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_var(&b_shader, vars->direction);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_load_ray_instance_custom_index: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_var(&b_shader, vars->custom_instance_and_mask);
               ret = nir_iand(&b_shader, ret, nir_imm_int(&b_shader, 0xFFFFFF));
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_load_primitive_id: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_var(&b_shader, vars->primitive_id);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_load_ray_geometry_index: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_var(&b_shader, vars->geometry_id_and_flags);
               ret = nir_iand(&b_shader, ret, nir_imm_int(&b_shader, 0xFFFFFFF));
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_load_instance_id: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_var(&b_shader, vars->instance_id);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_load_ray_flags: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_var(&b_shader, vars->flags);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_load_ray_hit_kind: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_var(&b_shader, vars->hit_kind);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_load_ray_world_to_object: {
               unsigned c = nir_intrinsic_column(intr);
               nir_ssa_def *instance_node_addr = nir_load_var(&b_shader, vars->instance_addr);
               nir_ssa_def *wto_matrix[3];
               nir_build_wto_matrix_load(&b_shader, instance_node_addr, wto_matrix);

               nir_ssa_def *vals[3];
               for (unsigned i = 0; i < 3; ++i)
                  vals[i] = nir_channel(&b_shader, wto_matrix[i], c);

               nir_ssa_def *val = nir_vec(&b_shader, vals, 3);
               if (c == 3)
                  val = nir_fneg(&b_shader,
                                 nir_build_vec3_mat_mult(&b_shader, val, wto_matrix, false));
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, val);
               break;
            }
            case nir_intrinsic_load_ray_object_to_world: {
               unsigned c = nir_intrinsic_column(intr);
               nir_ssa_def *instance_node_addr = nir_load_var(&b_shader, vars->instance_addr);
               nir_ssa_def *val;
               if (c == 3) {
                  nir_ssa_def *wto_matrix[3];
                  nir_build_wto_matrix_load(&b_shader, instance_node_addr, wto_matrix);

                  nir_ssa_def *vals[3];
                  for (unsigned i = 0; i < 3; ++i)
                     vals[i] = nir_channel(&b_shader, wto_matrix[i], c);

                  val = nir_vec(&b_shader, vals, 3);
               } else {
                  val = nir_build_load_global(&b_shader, 3, 32,
                                              nir_iadd(&b_shader, instance_node_addr,
                                                       nir_imm_int64(&b_shader, 92 + c * 12)));
               }
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, val);
               break;
            }
            case nir_intrinsic_load_ray_object_origin: {
               nir_ssa_def *instance_node_addr = nir_load_var(&b_shader, vars->instance_addr);
               nir_ssa_def *wto_matrix[] = {
                  nir_build_load_global(
                     &b_shader, 4, 32,
                     nir_iadd(&b_shader, instance_node_addr, nir_imm_int64(&b_shader, 16)),
                     .align_mul = 64, .align_offset = 16),
                  nir_build_load_global(
                     &b_shader, 4, 32,
                     nir_iadd(&b_shader, instance_node_addr, nir_imm_int64(&b_shader, 32)),
                     .align_mul = 64, .align_offset = 32),
                  nir_build_load_global(
                     &b_shader, 4, 32,
                     nir_iadd(&b_shader, instance_node_addr, nir_imm_int64(&b_shader, 48)),
                     .align_mul = 64, .align_offset = 48)};
               nir_ssa_def *val = nir_build_vec3_mat_mult_pre(
                  &b_shader, nir_load_var(&b_shader, vars->origin), wto_matrix);
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, val);
               break;
            }
            case nir_intrinsic_load_ray_object_direction: {
               nir_ssa_def *instance_node_addr = nir_load_var(&b_shader, vars->instance_addr);
               nir_ssa_def *wto_matrix[3];
               nir_build_wto_matrix_load(&b_shader, instance_node_addr, wto_matrix);
               nir_ssa_def *val = nir_build_vec3_mat_mult(
                  &b_shader, nir_load_var(&b_shader, vars->direction), wto_matrix, false);
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, val);
               break;
            }
            case nir_intrinsic_load_intersection_opaque_amd: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_ssa_def *ret = nir_load_var(&b_shader, vars->opaque);
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
               break;
            }
            case nir_intrinsic_ignore_ray_intersection: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_store_var(&b_shader, vars->ahit_status, nir_imm_int(&b_shader, 1), 1);

               /* The if is a workaround to avoid having to fix up control flow manually */
               nir_push_if(&b_shader, nir_imm_true(&b_shader));
               nir_jump(&b_shader, nir_jump_return);
               nir_pop_if(&b_shader, NULL);
               break;
            }
            case nir_intrinsic_terminate_ray: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_store_var(&b_shader, vars->ahit_status, nir_imm_int(&b_shader, 2), 1);

               /* The if is a workaround to avoid having to fix up control flow manually */
               nir_push_if(&b_shader, nir_imm_true(&b_shader));
               nir_jump(&b_shader, nir_jump_return);
               nir_pop_if(&b_shader, NULL);
               break;
            }
            case nir_intrinsic_report_ray_intersection: {
               b_shader.cursor = nir_instr_remove(instr);
               nir_push_if(
                  &b_shader,
                  nir_iand(
                     &b_shader,
                     nir_flt(&b_shader, intr->src[0].ssa, nir_load_var(&b_shader, vars->tmax)),
                     nir_fge(&b_shader, intr->src[0].ssa, nir_load_var(&b_shader, vars->tmin))));
               {
                  nir_store_var(&b_shader, vars->ahit_status, nir_imm_int(&b_shader, 0), 1);
                  nir_store_var(&b_shader, vars->tmax, intr->src[0].ssa, 1);
                  nir_store_var(&b_shader, vars->hit_kind, intr->src[1].ssa, 1);
               }
               nir_pop_if(&b_shader, NULL);
               break;
            }
            default:
               break;
            }
            break;
         }
         case nir_instr_type_jump: {
            nir_jump_instr *jump = nir_instr_as_jump(instr);
            if (jump->type == nir_jump_halt) {
               b_shader.cursor = nir_instr_remove(instr);
               nir_jump(&b_shader, nir_jump_return);
            }
            break;
         }
         default:
            break;
         }
      }
   }

   nir_metadata_preserve(nir_shader_get_entrypoint(shader), nir_metadata_none);
}

static void
insert_rt_case(nir_builder *b, nir_shader *shader, const struct rt_variables *vars,
               nir_ssa_def *idx, uint32_t call_idx_base, uint32_t call_idx)
{
   struct hash_table *var_remap = _mesa_pointer_hash_table_create(NULL);

   nir_opt_dead_cf(shader);

   struct rt_variables src_vars = create_rt_variables(shader, vars->stack_sizes);
   map_rt_variables(var_remap, &src_vars, vars);

   NIR_PASS_V(shader, lower_rt_instructions, &src_vars, call_idx_base);

   NIR_PASS_V(shader, nir_opt_remove_phis);
   NIR_PASS_V(shader, nir_lower_returns);
   NIR_PASS_V(shader, nir_opt_dce);

   if (b->shader->info.stage == MESA_SHADER_ANY_HIT ||
       b->shader->info.stage == MESA_SHADER_INTERSECTION) {
      src_vars.stack_sizes[src_vars.group_idx].non_recursive_size =
         MAX2(src_vars.stack_sizes[src_vars.group_idx].non_recursive_size, shader->scratch_size);
   } else {
      src_vars.stack_sizes[src_vars.group_idx].recursive_size =
         MAX2(src_vars.stack_sizes[src_vars.group_idx].recursive_size, shader->scratch_size);
   }

   nir_push_if(b, nir_ieq(b, idx, nir_imm_int(b, call_idx)));
   nir_store_var(b, vars->main_loop_case_visited, nir_imm_bool(b, true), 1);
   nir_inline_function_impl(b, nir_shader_get_entrypoint(shader), NULL, var_remap);
   nir_pop_if(b, NULL);

   /* Adopt the instructions from the source shader, since they are merely moved, not cloned. */
   ralloc_adopt(ralloc_context(b->shader), ralloc_context(shader));

   ralloc_free(var_remap);
}

static bool
lower_rt_derefs(nir_shader *shader)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   bool progress = false;

   nir_builder b;
   nir_builder_init(&b, impl);

   b.cursor = nir_before_cf_list(&impl->body);
   nir_ssa_def *arg_offset = nir_load_rt_arg_scratch_offset_amd(&b);

   nir_foreach_block (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         switch (instr->type) {
         case nir_instr_type_deref: {
            if (instr->type != nir_instr_type_deref)
               continue;

            nir_deref_instr *deref = nir_instr_as_deref(instr);
            if (nir_deref_mode_is(deref, nir_var_shader_call_data)) {
               deref->modes = nir_var_function_temp;
               if (deref->deref_type == nir_deref_type_var) {
                  b.cursor = nir_before_instr(&deref->instr);
                  nir_deref_instr *cast = nir_build_deref_cast(
                     &b, arg_offset, nir_var_function_temp, deref->var->type, 0);
                  nir_ssa_def_rewrite_uses(&deref->dest.ssa, &cast->dest.ssa);
                  nir_instr_remove(&deref->instr);
               }
               progress = true;
            } else if (nir_deref_mode_is(deref, nir_var_ray_hit_attrib)) {
               deref->modes = nir_var_function_temp;
               if (deref->deref_type == nir_deref_type_var) {
                  b.cursor = nir_before_instr(&deref->instr);
                  nir_deref_instr *cast =
                     nir_build_deref_cast(&b, nir_imm_int(&b, RADV_HIT_ATTRIB_OFFSET),
                                          nir_var_function_temp, deref->type, 0);
                  nir_ssa_def_rewrite_uses(&deref->dest.ssa, &cast->dest.ssa);
                  nir_instr_remove(&deref->instr);
               }
               progress = true;
            }
            break;
         }
         default:
            break;
         }
      }
   }

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);
   } else {
      nir_metadata_preserve(impl, nir_metadata_all);
   }

   return progress;
}

static gl_shader_stage
convert_rt_stage(VkShaderStageFlagBits vk_stage)
{
   switch (vk_stage) {
   case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
      return MESA_SHADER_RAYGEN;
   case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:
      return MESA_SHADER_ANY_HIT;
   case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
      return MESA_SHADER_CLOSEST_HIT;
   case VK_SHADER_STAGE_MISS_BIT_KHR:
      return MESA_SHADER_MISS;
   case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:
      return MESA_SHADER_INTERSECTION;
   case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
      return MESA_SHADER_CALLABLE;
   default:
      unreachable("Unhandled RT stage");
   }
}

static nir_shader *
parse_rt_stage(struct radv_device *device, struct radv_pipeline_layout *layout,
               const VkPipelineShaderStageCreateInfo *stage)
{
   struct radv_pipeline_key key;
   memset(&key, 0, sizeof(key));

   nir_shader *shader = radv_shader_compile_to_nir(
      device, vk_shader_module_from_handle(stage->module), stage->pName,
      convert_rt_stage(stage->stage), stage->pSpecializationInfo, layout, &key);

   if (shader->info.stage == MESA_SHADER_RAYGEN || shader->info.stage == MESA_SHADER_CLOSEST_HIT ||
       shader->info.stage == MESA_SHADER_CALLABLE || shader->info.stage == MESA_SHADER_MISS) {
      nir_block *last_block = nir_impl_last_block(nir_shader_get_entrypoint(shader));
      nir_builder b_inner;
      nir_builder_init(&b_inner, nir_shader_get_entrypoint(shader));
      b_inner.cursor = nir_after_block(last_block);
      nir_rt_return_amd(&b_inner);
   }

   NIR_PASS_V(shader, nir_lower_vars_to_explicit_types,
              nir_var_function_temp | nir_var_shader_call_data | nir_var_ray_hit_attrib,
              glsl_get_natural_size_align_bytes);

   NIR_PASS_V(shader, lower_rt_derefs);

   NIR_PASS_V(shader, nir_lower_explicit_io, nir_var_function_temp,
              nir_address_format_32bit_offset);

   return shader;
}

static nir_function_impl *
lower_any_hit_for_intersection(nir_shader *any_hit)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(any_hit);

   /* Any-hit shaders need three parameters */
   assert(impl->function->num_params == 0);
   nir_parameter params[] = {
      {
         /* A pointer to a boolean value for whether or not the hit was
          * accepted.
          */
         .num_components = 1,
         .bit_size = 32,
      },
      {
         /* The hit T value */
         .num_components = 1,
         .bit_size = 32,
      },
      {
         /* The hit kind */
         .num_components = 1,
         .bit_size = 32,
      },
   };
   impl->function->num_params = ARRAY_SIZE(params);
   impl->function->params = ralloc_array(any_hit, nir_parameter, ARRAY_SIZE(params));
   memcpy(impl->function->params, params, sizeof(params));

   nir_builder build;
   nir_builder_init(&build, impl);
   nir_builder *b = &build;

   b->cursor = nir_before_cf_list(&impl->body);

   nir_ssa_def *commit_ptr = nir_load_param(b, 0);
   nir_ssa_def *hit_t = nir_load_param(b, 1);
   nir_ssa_def *hit_kind = nir_load_param(b, 2);

   nir_deref_instr *commit =
      nir_build_deref_cast(b, commit_ptr, nir_var_function_temp, glsl_bool_type(), 0);

   nir_foreach_block_safe (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         switch (instr->type) {
         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            switch (intrin->intrinsic) {
            case nir_intrinsic_ignore_ray_intersection:
               b->cursor = nir_instr_remove(&intrin->instr);
               /* We put the newly emitted code inside a dummy if because it's
                * going to contain a jump instruction and we don't want to
                * deal with that mess here.  It'll get dealt with by our
                * control-flow optimization passes.
                */
               nir_store_deref(b, commit, nir_imm_false(b), 0x1);
               nir_push_if(b, nir_imm_true(b));
               nir_jump(b, nir_jump_halt);
               nir_pop_if(b, NULL);
               break;

            case nir_intrinsic_terminate_ray:
               /* The "normal" handling of terminateRay works fine in
                * intersection shaders.
                */
               break;

            case nir_intrinsic_load_ray_t_max:
               nir_ssa_def_rewrite_uses(&intrin->dest.ssa, hit_t);
               nir_instr_remove(&intrin->instr);
               break;

            case nir_intrinsic_load_ray_hit_kind:
               nir_ssa_def_rewrite_uses(&intrin->dest.ssa, hit_kind);
               nir_instr_remove(&intrin->instr);
               break;

            default:
               break;
            }
            break;
         }
         case nir_instr_type_jump: {
            nir_jump_instr *jump = nir_instr_as_jump(instr);
            if (jump->type == nir_jump_halt) {
               b->cursor = nir_instr_remove(instr);
               nir_jump(b, nir_jump_return);
            }
            break;
         }

         default:
            break;
         }
      }
   }

   nir_validate_shader(any_hit, "after initial any-hit lowering");

   nir_lower_returns_impl(impl);

   nir_validate_shader(any_hit, "after lowering returns");

   return impl;
}

/* Inline the any_hit shader into the intersection shader so we don't have
 * to implement yet another shader call interface here. Neither do any recursion.
 */
static void
nir_lower_intersection_shader(nir_shader *intersection, nir_shader *any_hit)
{
   void *dead_ctx = ralloc_context(intersection);

   nir_function_impl *any_hit_impl = NULL;
   struct hash_table *any_hit_var_remap = NULL;
   if (any_hit) {
      any_hit = nir_shader_clone(dead_ctx, any_hit);
      NIR_PASS_V(any_hit, nir_opt_dce);
      any_hit_impl = lower_any_hit_for_intersection(any_hit);
      any_hit_var_remap = _mesa_pointer_hash_table_create(dead_ctx);
   }

   nir_function_impl *impl = nir_shader_get_entrypoint(intersection);

   nir_builder build;
   nir_builder_init(&build, impl);
   nir_builder *b = &build;

   b->cursor = nir_before_cf_list(&impl->body);

   nir_variable *commit = nir_local_variable_create(impl, glsl_bool_type(), "ray_commit");
   nir_store_var(b, commit, nir_imm_false(b), 0x1);

   nir_foreach_block_safe (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_report_ray_intersection)
            continue;

         b->cursor = nir_instr_remove(&intrin->instr);
         nir_ssa_def *hit_t = nir_ssa_for_src(b, intrin->src[0], 1);
         nir_ssa_def *hit_kind = nir_ssa_for_src(b, intrin->src[1], 1);
         nir_ssa_def *min_t = nir_load_ray_t_min(b);
         nir_ssa_def *max_t = nir_load_ray_t_max(b);

         /* bool commit_tmp = false; */
         nir_variable *commit_tmp = nir_local_variable_create(impl, glsl_bool_type(), "commit_tmp");
         nir_store_var(b, commit_tmp, nir_imm_false(b), 0x1);

         nir_push_if(b, nir_iand(b, nir_fge(b, hit_t, min_t), nir_fge(b, max_t, hit_t)));
         {
            /* Any-hit defaults to commit */
            nir_store_var(b, commit_tmp, nir_imm_true(b), 0x1);

            if (any_hit_impl != NULL) {
               nir_push_if(b, nir_inot(b, nir_load_intersection_opaque_amd(b)));
               {
                  nir_ssa_def *params[] = {
                     &nir_build_deref_var(b, commit_tmp)->dest.ssa,
                     hit_t,
                     hit_kind,
                  };
                  nir_inline_function_impl(b, any_hit_impl, params, any_hit_var_remap);
               }
               nir_pop_if(b, NULL);
            }

            nir_push_if(b, nir_load_var(b, commit_tmp));
            {
               nir_report_ray_intersection(b, 1, hit_t, hit_kind);
            }
            nir_pop_if(b, NULL);
         }
         nir_pop_if(b, NULL);

         nir_ssa_def *accepted = nir_load_var(b, commit_tmp);
         nir_ssa_def_rewrite_uses(&intrin->dest.ssa, accepted);
      }
   }

   /* We did some inlining; have to re-index SSA defs */
   nir_index_ssa_defs(impl);

   /* Eliminate the casts introduced for the commit return of the any-hit shader. */
   NIR_PASS_V(intersection, nir_opt_deref);

   ralloc_free(dead_ctx);
}

/* Variables only used internally to ray traversal. This is data that describes
 * the current state of the traversal vs. what we'd give to a shader.  e.g. what
 * is the instance we're currently visiting vs. what is the instance of the
 * closest hit. */
struct rt_traversal_vars {
   nir_variable *origin;
   nir_variable *dir;
   nir_variable *inv_dir;
   nir_variable *sbt_offset_and_flags;
   nir_variable *instance_id;
   nir_variable *custom_instance_and_mask;
   nir_variable *instance_addr;
   nir_variable *should_return;
   nir_variable *bvh_base;
   nir_variable *stack;
   nir_variable *top_stack;
};

static struct rt_traversal_vars
init_traversal_vars(nir_builder *b)
{
   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);
   struct rt_traversal_vars ret;

   ret.origin = nir_variable_create(b->shader, nir_var_shader_temp, vec3_type, "traversal_origin");
   ret.dir = nir_variable_create(b->shader, nir_var_shader_temp, vec3_type, "traversal_dir");
   ret.inv_dir =
      nir_variable_create(b->shader, nir_var_shader_temp, vec3_type, "traversal_inv_dir");
   ret.sbt_offset_and_flags = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(),
                                                  "traversal_sbt_offset_and_flags");
   ret.instance_id = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(),
                                         "traversal_instance_id");
   ret.custom_instance_and_mask = nir_variable_create(
      b->shader, nir_var_shader_temp, glsl_uint_type(), "traversal_custom_instance_and_mask");
   ret.instance_addr =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint64_t_type(), "instance_addr");
   ret.should_return = nir_variable_create(b->shader, nir_var_shader_temp, glsl_bool_type(),
                                           "traversal_should_return");
   ret.bvh_base = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint64_t_type(),
                                      "traversal_bvh_base");
   ret.stack =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "traversal_stack_ptr");
   ret.top_stack = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(),
                                       "traversal_top_stack_ptr");
   return ret;
}

static nir_ssa_def *
build_addr_to_node(nir_builder *b, nir_ssa_def *addr)
{
   const uint64_t bvh_size = 1ull << 42;
   nir_ssa_def *node = nir_ushr(b, addr, nir_imm_int(b, 3));
   return nir_iand(b, node, nir_imm_int64(b, (bvh_size - 1) << 3));
}

static nir_ssa_def *
build_node_to_addr(struct radv_device *device, nir_builder *b, nir_ssa_def *node)
{
   nir_ssa_def *addr = nir_iand(b, node, nir_imm_int64(b, ~7ull));
   addr = nir_ishl(b, addr, nir_imm_int(b, 3));
   /* Assumes everything is in the top half of address space, which is true in
    * GFX9+ for now. */
   return device->physical_device->rad_info.chip_class >= GFX9
      ? nir_ior(b, addr, nir_imm_int64(b, 0xffffull << 48))
      : addr;
}

/* When a hit is opaque the any_hit shader is skipped for this hit and the hit
 * is assumed to be an actual hit. */
static nir_ssa_def *
hit_is_opaque(nir_builder *b, const struct rt_variables *vars,
              const struct rt_traversal_vars *trav_vars, nir_ssa_def *geometry_id_and_flags)
{
   nir_ssa_def *geom_force_opaque = nir_ine(
      b, nir_iand(b, geometry_id_and_flags, nir_imm_int(b, 1u << 28 /* VK_GEOMETRY_OPAQUE_BIT */)),
      nir_imm_int(b, 0));
   nir_ssa_def *instance_force_opaque =
      nir_ine(b,
              nir_iand(b, nir_load_var(b, trav_vars->sbt_offset_and_flags),
                       nir_imm_int(b, 4 << 24 /* VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT */)),
              nir_imm_int(b, 0));
   nir_ssa_def *instance_force_non_opaque =
      nir_ine(b,
              nir_iand(b, nir_load_var(b, trav_vars->sbt_offset_and_flags),
                       nir_imm_int(b, 8 << 24 /* VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT */)),
              nir_imm_int(b, 0));

   nir_ssa_def *opaque = geom_force_opaque;
   opaque = nir_bcsel(b, instance_force_opaque, nir_imm_bool(b, true), opaque);
   opaque = nir_bcsel(b, instance_force_non_opaque, nir_imm_bool(b, false), opaque);

   nir_ssa_def *ray_force_opaque =
      nir_ine(b, nir_iand(b, nir_load_var(b, vars->flags), nir_imm_int(b, 1 /* RayFlagsOpaque */)),
              nir_imm_int(b, 0));
   nir_ssa_def *ray_force_non_opaque = nir_ine(
      b, nir_iand(b, nir_load_var(b, vars->flags), nir_imm_int(b, 2 /* RayFlagsNoOpaque */)),
      nir_imm_int(b, 0));

   opaque = nir_bcsel(b, ray_force_opaque, nir_imm_bool(b, true), opaque);
   opaque = nir_bcsel(b, ray_force_non_opaque, nir_imm_bool(b, false), opaque);
   return opaque;
}

static void
visit_any_hit_shaders(struct radv_device *device,
                      const VkRayTracingPipelineCreateInfoKHR *pCreateInfo, nir_builder *b,
                      struct rt_variables *vars)
{
   RADV_FROM_HANDLE(radv_pipeline_layout, layout, pCreateInfo->layout);
   nir_ssa_def *sbt_idx = nir_load_var(b, vars->idx);

   nir_push_if(b, nir_ine(b, sbt_idx, nir_imm_int(b, 0)));
   for (unsigned i = 0; i < pCreateInfo->groupCount; ++i) {
      const VkRayTracingShaderGroupCreateInfoKHR *group_info = &pCreateInfo->pGroups[i];
      uint32_t shader_id = VK_SHADER_UNUSED_KHR;

      switch (group_info->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR:
         shader_id = group_info->anyHitShader;
         break;
      default:
         break;
      }
      if (shader_id == VK_SHADER_UNUSED_KHR)
         continue;

      const VkPipelineShaderStageCreateInfo *stage = &pCreateInfo->pStages[shader_id];
      nir_shader *nir_stage = parse_rt_stage(device, layout, stage);

      vars->group_idx = i;
      insert_rt_case(b, nir_stage, vars, sbt_idx, 0, i + 2);
   }
   nir_pop_if(b, NULL);
}

static void
insert_traversal_triangle_case(struct radv_device *device,
                               const VkRayTracingPipelineCreateInfoKHR *pCreateInfo, nir_builder *b,
                               nir_ssa_def *result, const struct rt_variables *vars,
                               const struct rt_traversal_vars *trav_vars, nir_ssa_def *bvh_node)
{
   nir_ssa_def *dist = nir_vector_extract(b, result, nir_imm_int(b, 0));
   nir_ssa_def *div = nir_vector_extract(b, result, nir_imm_int(b, 1));
   dist = nir_fdiv(b, dist, div);
   nir_ssa_def *frontface = nir_flt(b, nir_imm_float(b, 0), div);
   nir_ssa_def *switch_ccw = nir_ine(
      b,
      nir_iand(
         b, nir_load_var(b, trav_vars->sbt_offset_and_flags),
         nir_imm_int(b, 2 << 24 /* VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT */)),
      nir_imm_int(b, 0));
   frontface = nir_ixor(b, frontface, switch_ccw);

   nir_ssa_def *not_cull = nir_ieq(
      b, nir_iand(b, nir_load_var(b, vars->flags), nir_imm_int(b, 256 /* RayFlagsSkipTriangles */)),
      nir_imm_int(b, 0));
   nir_ssa_def *not_facing_cull = nir_ieq(
      b,
      nir_iand(b, nir_load_var(b, vars->flags),
               nir_bcsel(b, frontface, nir_imm_int(b, 32 /* RayFlagsCullFrontFacingTriangles */),
                         nir_imm_int(b, 16 /* RayFlagsCullBackFacingTriangles */))),
      nir_imm_int(b, 0));

   not_cull = nir_iand(
      b, not_cull,
      nir_ior(
         b, not_facing_cull,
         nir_ine(
            b,
            nir_iand(
               b, nir_load_var(b, trav_vars->sbt_offset_and_flags),
               nir_imm_int(b, 1 << 24 /* VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT */)),
            nir_imm_int(b, 0))));

   nir_push_if(b, nir_iand(b,
                           nir_iand(b, nir_flt(b, dist, nir_load_var(b, vars->tmax)),
                                    nir_fge(b, dist, nir_load_var(b, vars->tmin))),
                           not_cull));
   {

      nir_ssa_def *triangle_info = nir_build_load_global(
         b, 2, 32,
         nir_iadd(b, build_node_to_addr(device, b, bvh_node),
                  nir_imm_int64(b, offsetof(struct radv_bvh_triangle_node, triangle_id))));
      nir_ssa_def *primitive_id = nir_channel(b, triangle_info, 0);
      nir_ssa_def *geometry_id_and_flags = nir_channel(b, triangle_info, 1);
      nir_ssa_def *geometry_id = nir_iand(b, geometry_id_and_flags, nir_imm_int(b, 0xfffffff));
      nir_ssa_def *is_opaque = hit_is_opaque(b, vars, trav_vars, geometry_id_and_flags);

      not_cull =
         nir_ieq(b,
                 nir_iand(b, nir_load_var(b, vars->flags),
                          nir_bcsel(b, is_opaque, nir_imm_int(b, 0x40), nir_imm_int(b, 0x80))),
                 nir_imm_int(b, 0));
      nir_push_if(b, not_cull);
      {
         nir_ssa_def *sbt_idx =
            nir_iadd(b,
                     nir_iadd(b, nir_load_var(b, vars->sbt_offset),
                              nir_iand(b, nir_load_var(b, trav_vars->sbt_offset_and_flags),
                                       nir_imm_int(b, 0xffffff))),
                     nir_imul(b, nir_load_var(b, vars->sbt_stride), geometry_id));
         nir_ssa_def *divs[2] = {div, div};
         nir_ssa_def *ij = nir_fdiv(b, nir_channels(b, result, 0xc), nir_vec(b, divs, 2));
         nir_ssa_def *hit_kind =
            nir_bcsel(b, frontface, nir_imm_int(b, 0xFE), nir_imm_int(b, 0xFF));

         nir_store_scratch(
            b, ij,
            nir_iadd(b, nir_load_var(b, vars->stack_ptr), nir_imm_int(b, RADV_HIT_ATTRIB_OFFSET)),
            .align_mul = 16);

         nir_store_var(b, vars->ahit_status, nir_imm_int(b, 0), 1);

         nir_push_if(b, nir_ine(b, is_opaque, nir_imm_bool(b, true)));
         {
            struct rt_variables inner_vars = create_inner_vars(b, vars);

            nir_store_var(b, inner_vars.primitive_id, primitive_id, 1);
            nir_store_var(b, inner_vars.geometry_id_and_flags, geometry_id_and_flags, 1);
            nir_store_var(b, inner_vars.tmax, dist, 0x1);
            nir_store_var(b, inner_vars.instance_id, nir_load_var(b, trav_vars->instance_id), 0x1);
            nir_store_var(b, inner_vars.instance_addr, nir_load_var(b, trav_vars->instance_addr),
                          0x1);
            nir_store_var(b, inner_vars.hit_kind, hit_kind, 0x1);
            nir_store_var(b, inner_vars.custom_instance_and_mask,
                          nir_load_var(b, trav_vars->custom_instance_and_mask), 0x1);

            load_sbt_entry(b, &inner_vars, sbt_idx, SBT_HIT, 4);

            visit_any_hit_shaders(device, pCreateInfo, b, &inner_vars);

            nir_push_if(b, nir_ieq(b, nir_load_var(b, vars->ahit_status), nir_imm_int(b, 1)));
            {
               nir_jump(b, nir_jump_continue);
            }
            nir_pop_if(b, NULL);
         }
         nir_pop_if(b, NULL);

         nir_store_var(b, vars->primitive_id, primitive_id, 1);
         nir_store_var(b, vars->geometry_id_and_flags, geometry_id_and_flags, 1);
         nir_store_var(b, vars->tmax, dist, 0x1);
         nir_store_var(b, vars->instance_id, nir_load_var(b, trav_vars->instance_id), 0x1);
         nir_store_var(b, vars->instance_addr, nir_load_var(b, trav_vars->instance_addr), 0x1);
         nir_store_var(b, vars->hit_kind, hit_kind, 0x1);
         nir_store_var(b, vars->custom_instance_and_mask,
                       nir_load_var(b, trav_vars->custom_instance_and_mask), 0x1);

         load_sbt_entry(b, vars, sbt_idx, SBT_HIT, 0);

         nir_store_var(b, trav_vars->should_return,
                       nir_ior(b,
                               nir_ine(b,
                                       nir_iand(b, nir_load_var(b, vars->flags),
                                                nir_imm_int(b, 8 /* SkipClosestHitShader */)),
                                       nir_imm_int(b, 0)),
                               nir_ieq(b, nir_load_var(b, vars->idx), nir_imm_int(b, 0))),
                       1);

         nir_ssa_def *terminate_on_first_hit =
            nir_ine(b,
                    nir_iand(b, nir_load_var(b, vars->flags),
                             nir_imm_int(b, 4 /* TerminateOnFirstHitKHR */)),
                    nir_imm_int(b, 0));
         nir_ssa_def *ray_terminated =
            nir_ieq(b, nir_load_var(b, vars->ahit_status), nir_imm_int(b, 2));
         nir_push_if(b, nir_ior(b, terminate_on_first_hit, ray_terminated));
         {
            nir_jump(b, nir_jump_break);
         }
         nir_pop_if(b, NULL);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static void
insert_traversal_aabb_case(struct radv_device *device,
                           const VkRayTracingPipelineCreateInfoKHR *pCreateInfo, nir_builder *b,
                           const struct rt_variables *vars,
                           const struct rt_traversal_vars *trav_vars, nir_ssa_def *bvh_node)
{
   RADV_FROM_HANDLE(radv_pipeline_layout, layout, pCreateInfo->layout);

   nir_ssa_def *node_addr = build_node_to_addr(device, b, bvh_node);
   nir_ssa_def *triangle_info =
      nir_build_load_global(b, 2, 32, nir_iadd(b, node_addr, nir_imm_int64(b, 24)));
   nir_ssa_def *primitive_id = nir_channel(b, triangle_info, 0);
   nir_ssa_def *geometry_id_and_flags = nir_channel(b, triangle_info, 1);
   nir_ssa_def *geometry_id = nir_iand(b, geometry_id_and_flags, nir_imm_int(b, 0xfffffff));
   nir_ssa_def *is_opaque = hit_is_opaque(b, vars, trav_vars, geometry_id_and_flags);

   nir_ssa_def *not_cull =
      nir_ieq(b,
              nir_iand(b, nir_load_var(b, vars->flags),
                       nir_bcsel(b, is_opaque, nir_imm_int(b, 0x40), nir_imm_int(b, 0x80))),
              nir_imm_int(b, 0));
   nir_push_if(b, not_cull);
   {
      nir_ssa_def *sbt_idx =
         nir_iadd(b,
                  nir_iadd(b, nir_load_var(b, vars->sbt_offset),
                           nir_iand(b, nir_load_var(b, trav_vars->sbt_offset_and_flags),
                                    nir_imm_int(b, 0xffffff))),
                  nir_imul(b, nir_load_var(b, vars->sbt_stride), geometry_id));

      struct rt_variables inner_vars = create_inner_vars(b, vars);

      /* For AABBs the intersection shader writes the hit kind, and only does it if it is the
       * next closest hit candidate. */
      inner_vars.hit_kind = vars->hit_kind;

      nir_store_var(b, inner_vars.primitive_id, primitive_id, 1);
      nir_store_var(b, inner_vars.geometry_id_and_flags, geometry_id_and_flags, 1);
      nir_store_var(b, inner_vars.tmax, nir_load_var(b, vars->tmax), 0x1);
      nir_store_var(b, inner_vars.instance_id, nir_load_var(b, trav_vars->instance_id), 0x1);
      nir_store_var(b, inner_vars.instance_addr, nir_load_var(b, trav_vars->instance_addr), 0x1);
      nir_store_var(b, inner_vars.custom_instance_and_mask,
                    nir_load_var(b, trav_vars->custom_instance_and_mask), 0x1);
      nir_store_var(b, inner_vars.opaque, is_opaque, 1);

      load_sbt_entry(b, &inner_vars, sbt_idx, SBT_HIT, 4);

      nir_store_var(b, vars->ahit_status, nir_imm_int(b, 1), 1);

      nir_push_if(b, nir_ine(b, nir_load_var(b, inner_vars.idx), nir_imm_int(b, 0)));
      for (unsigned i = 0; i < pCreateInfo->groupCount; ++i) {
         const VkRayTracingShaderGroupCreateInfoKHR *group_info = &pCreateInfo->pGroups[i];
         uint32_t shader_id = VK_SHADER_UNUSED_KHR;
         uint32_t any_hit_shader_id = VK_SHADER_UNUSED_KHR;

         switch (group_info->type) {
         case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR:
            shader_id = group_info->intersectionShader;
            any_hit_shader_id = group_info->anyHitShader;
            break;
         default:
            break;
         }
         if (shader_id == VK_SHADER_UNUSED_KHR)
            continue;

         const VkPipelineShaderStageCreateInfo *stage = &pCreateInfo->pStages[shader_id];
         nir_shader *nir_stage = parse_rt_stage(device, layout, stage);

         nir_shader *any_hit_stage = NULL;
         if (any_hit_shader_id != VK_SHADER_UNUSED_KHR) {
            stage = &pCreateInfo->pStages[any_hit_shader_id];
            any_hit_stage = parse_rt_stage(device, layout, stage);

            nir_lower_intersection_shader(nir_stage, any_hit_stage);
            ralloc_free(any_hit_stage);
         }

         inner_vars.group_idx = i;
         insert_rt_case(b, nir_stage, &inner_vars, nir_load_var(b, inner_vars.idx), 0, i + 2);
      }
      nir_push_else(b, NULL);
      {
         nir_ssa_def *vec3_zero = nir_channels(b, nir_imm_vec4(b, 0, 0, 0, 0), 0x7);
         nir_ssa_def *vec3_inf =
            nir_channels(b, nir_imm_vec4(b, INFINITY, INFINITY, INFINITY, 0), 0x7);

         nir_ssa_def *bvh_lo =
            nir_build_load_global(b, 3, 32, nir_iadd(b, node_addr, nir_imm_int64(b, 0)));
         nir_ssa_def *bvh_hi =
            nir_build_load_global(b, 3, 32, nir_iadd(b, node_addr, nir_imm_int64(b, 12)));

         bvh_lo = nir_fsub(b, bvh_lo, nir_load_var(b, trav_vars->origin));
         bvh_hi = nir_fsub(b, bvh_hi, nir_load_var(b, trav_vars->origin));
         nir_ssa_def *t_vec = nir_fmin(b, nir_fmul(b, bvh_lo, nir_load_var(b, trav_vars->inv_dir)),
                                       nir_fmul(b, bvh_hi, nir_load_var(b, trav_vars->inv_dir)));
         nir_ssa_def *t2_vec = nir_fmax(b, nir_fmul(b, bvh_lo, nir_load_var(b, trav_vars->inv_dir)),
                                        nir_fmul(b, bvh_hi, nir_load_var(b, trav_vars->inv_dir)));
         /* If we run parallel to one of the edges the range should be [0, inf) not [0,0] */
         t2_vec =
            nir_bcsel(b, nir_feq(b, nir_load_var(b, trav_vars->dir), vec3_zero), vec3_inf, t2_vec);

         nir_ssa_def *t_min = nir_fmax(b, nir_channel(b, t_vec, 0), nir_channel(b, t_vec, 1));
         t_min = nir_fmax(b, t_min, nir_channel(b, t_vec, 2));

         nir_ssa_def *t_max = nir_fmin(b, nir_channel(b, t2_vec, 0), nir_channel(b, t2_vec, 1));
         t_max = nir_fmin(b, t_max, nir_channel(b, t2_vec, 2));

         nir_push_if(b, nir_iand(b, nir_flt(b, t_min, nir_load_var(b, vars->tmax)),
                                 nir_fge(b, t_max, nir_load_var(b, vars->tmin))));
         {
            nir_store_var(b, vars->ahit_status, nir_imm_int(b, 0), 1);
            nir_store_var(b, vars->tmax, nir_fmax(b, t_min, nir_load_var(b, vars->tmin)), 1);
         }
         nir_pop_if(b, NULL);
      }
      nir_pop_if(b, NULL);

      nir_push_if(b, nir_ine(b, nir_load_var(b, vars->ahit_status), nir_imm_int(b, 1)));
      {
         nir_store_var(b, vars->primitive_id, primitive_id, 1);
         nir_store_var(b, vars->geometry_id_and_flags, geometry_id_and_flags, 1);
         nir_store_var(b, vars->tmax, nir_load_var(b, inner_vars.tmax), 0x1);
         nir_store_var(b, vars->instance_id, nir_load_var(b, trav_vars->instance_id), 0x1);
         nir_store_var(b, vars->instance_addr, nir_load_var(b, trav_vars->instance_addr), 0x1);
         nir_store_var(b, vars->custom_instance_and_mask,
                       nir_load_var(b, trav_vars->custom_instance_and_mask), 0x1);

         load_sbt_entry(b, vars, sbt_idx, SBT_HIT, 0);

         nir_store_var(b, trav_vars->should_return,
                       nir_ior(b,
                               nir_ine(b,
                                       nir_iand(b, nir_load_var(b, vars->flags),
                                                nir_imm_int(b, 8 /* SkipClosestHitShader */)),
                                       nir_imm_int(b, 0)),
                               nir_ieq(b, nir_load_var(b, vars->idx), nir_imm_int(b, 0))),
                       1);

         nir_ssa_def *terminate_on_first_hit =
            nir_ine(b,
                    nir_iand(b, nir_load_var(b, vars->flags),
                             nir_imm_int(b, 4 /* TerminateOnFirstHitKHR */)),
                    nir_imm_int(b, 0));
         nir_ssa_def *ray_terminated =
            nir_ieq(b, nir_load_var(b, vars->ahit_status), nir_imm_int(b, 2));
         nir_push_if(b, nir_ior(b, terminate_on_first_hit, ray_terminated));
         {
            nir_jump(b, nir_jump_break);
         }
         nir_pop_if(b, NULL);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static void
nir_sort_hit_pair(nir_builder *b, nir_variable *var_distances, nir_variable *var_indices, uint32_t chan_1, uint32_t chan_2)
{
   nir_ssa_def *ssa_distances = nir_load_var(b, var_distances);
   nir_ssa_def *ssa_indices = nir_load_var(b, var_indices);
   /* if (distances[chan_2] < distances[chan_1]) { */
   nir_push_if(b, nir_flt(b, nir_channel(b, ssa_distances, chan_2), nir_channel(b, ssa_distances, chan_1)));
   {
      /* swap(distances[chan_2], distances[chan_1]); */
      nir_ssa_def *new_distances[4] = {nir_ssa_undef(b, 1, 32), nir_ssa_undef(b, 1, 32), nir_ssa_undef(b, 1, 32), nir_ssa_undef(b, 1, 32)};
      nir_ssa_def *new_indices[4]   = {nir_ssa_undef(b, 1, 32), nir_ssa_undef(b, 1, 32), nir_ssa_undef(b, 1, 32), nir_ssa_undef(b, 1, 32)};
      new_distances[chan_2] = nir_channel(b, ssa_distances, chan_1);
      new_distances[chan_1] = nir_channel(b, ssa_distances, chan_2);
      new_indices[chan_2] = nir_channel(b, ssa_indices, chan_1);
      new_indices[chan_1] = nir_channel(b, ssa_indices, chan_2);
      nir_store_var(b, var_distances, nir_vec(b, new_distances, 4), (1u << chan_1) | (1u << chan_2));
      nir_store_var(b, var_indices, nir_vec(b, new_indices, 4), (1u << chan_1) | (1u << chan_2));
   }
   /* } */
   nir_pop_if(b, NULL);
}

static nir_ssa_def *
intersect_ray_amd_software_box(struct radv_device *device,
                               nir_builder *b, nir_ssa_def *bvh_node,
                               nir_ssa_def *ray_tmax, nir_ssa_def *origin,
                               nir_ssa_def *dir, nir_ssa_def *inv_dir)
{
   const struct glsl_type *vec4_type = glsl_vector_type(GLSL_TYPE_FLOAT, 4);
   const struct glsl_type *uvec4_type = glsl_vector_type(GLSL_TYPE_UINT, 4);

   nir_ssa_def *node_addr = build_node_to_addr(device, b, bvh_node);

   /* vec4 distances = vec4(INF, INF, INF, INF); */
   nir_variable *distances = nir_variable_create(b->shader, nir_var_shader_temp, vec4_type, "distances");
   nir_store_var(b, distances, nir_imm_vec4(b, INFINITY, INFINITY, INFINITY, INFINITY), 0xf);

   /* uvec4 child_indices = uvec4(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff); */
   nir_variable *child_indices = nir_variable_create(b->shader, nir_var_shader_temp, uvec4_type, "child_indices");
   nir_store_var(b, child_indices, nir_imm_ivec4(b, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu), 0xf);

   /* Need to remove infinities here because otherwise we get nasty NaN propogation
    * if the direction has 0s in it. */
   /* inv_dir = clamp(inv_dir, -FLT_MAX, FLT_MAX); */
   inv_dir = nir_fclamp(b, inv_dir, nir_imm_float(b, -FLT_MAX), nir_imm_float(b, FLT_MAX));

   for (int i = 0; i < 4; i++) {
      const uint32_t child_offset  = offsetof(struct radv_bvh_box32_node, children[i]);
      const uint32_t coord_offsets[2] = {
         offsetof(struct radv_bvh_box32_node, coords[i][0][0]),
         offsetof(struct radv_bvh_box32_node, coords[i][1][0]),
      };

      /* node->children[i] -> uint */
      nir_ssa_def *child_index = nir_build_load_global(b, 1, 32, nir_iadd(b, node_addr, nir_imm_int64(b, child_offset)),  .align_mul = 64, .align_offset = child_offset  % 64 );
      /* node->coords[i][0], node->coords[i][1] -> vec3 */
      nir_ssa_def *node_coords[2] = {
         nir_build_load_global(b, 3, 32, nir_iadd(b, node_addr, nir_imm_int64(b, coord_offsets[0])), .align_mul = 64, .align_offset = coord_offsets[0] % 64 ),
         nir_build_load_global(b, 3, 32, nir_iadd(b, node_addr, nir_imm_int64(b, coord_offsets[1])), .align_mul = 64, .align_offset = coord_offsets[1] % 64 ),
      };

      /* If x of the aabb min is NaN, then this is an inactive aabb.
       * We don't need to care about any other components being NaN as that is UB.
       * https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/chap36.html#VkAabbPositionsKHR */
      nir_ssa_def *min_x = nir_channel(b, node_coords[0], 0);
      nir_ssa_def *min_x_is_not_nan = nir_inot(b, nir_fneu(b, min_x, min_x)); /* NaN != NaN -> true */

      /* vec3 bound0 = (node->coords[i][0] - origin) * inv_dir; */
      nir_ssa_def *bound0 = nir_fmul(b, nir_fsub(b, node_coords[0], origin), inv_dir);
      /* vec3 bound1 = (node->coords[i][1] - origin) * inv_dir; */
      nir_ssa_def *bound1 = nir_fmul(b, nir_fsub(b, node_coords[1], origin), inv_dir);

      /* float tmin = max(max(min(bound0.x, bound1.x), min(bound0.y, bound1.y)), min(bound0.z, bound1.z)); */
      nir_ssa_def *tmin = nir_fmax(b, nir_fmax(b,
         nir_fmin(b, nir_channel(b, bound0, 0), nir_channel(b, bound1, 0)),
         nir_fmin(b, nir_channel(b, bound0, 1), nir_channel(b, bound1, 1))),
         nir_fmin(b, nir_channel(b, bound0, 2), nir_channel(b, bound1, 2)));

      /* float tmax = min(min(max(bound0.x, bound1.x), max(bound0.y, bound1.y)), max(bound0.z, bound1.z)); */
      nir_ssa_def *tmax = nir_fmin(b, nir_fmin(b,
         nir_fmax(b, nir_channel(b, bound0, 0), nir_channel(b, bound1, 0)),
         nir_fmax(b, nir_channel(b, bound0, 1), nir_channel(b, bound1, 1))),
         nir_fmax(b, nir_channel(b, bound0, 2), nir_channel(b, bound1, 2)));

      /* if (!isnan(node->coords[i][0].x) && tmax >= max(0.0f, tmin) && tmin < ray_tmax) { */
      nir_push_if(b,
         nir_iand(b,
            min_x_is_not_nan,
            nir_iand(b,
               nir_fge(b, tmax, nir_fmax(b, nir_imm_float(b, 0.0f), tmin)),
               nir_flt(b, tmin, ray_tmax))));
      {
         /* child_indices[i] = node->children[i]; */
         nir_ssa_def *new_child_indices[4] = {child_index, child_index, child_index, child_index};
         nir_store_var(b, child_indices, nir_vec(b, new_child_indices, 4), 1u << i);

         /* distances[i] = tmin; */
         nir_ssa_def *new_distances[4] = {tmin, tmin, tmin, tmin};
         nir_store_var(b, distances, nir_vec(b, new_distances, 4), 1u << i);

      }
      /* } */
      nir_pop_if(b, NULL);
   }

   /* Sort our distances with a sorting network. */
   nir_sort_hit_pair(b, distances, child_indices, 0, 1);
   nir_sort_hit_pair(b, distances, child_indices, 2, 3);
   nir_sort_hit_pair(b, distances, child_indices, 0, 2);
   nir_sort_hit_pair(b, distances, child_indices, 1, 3);
   nir_sort_hit_pair(b, distances, child_indices, 1, 2);

   return nir_load_var(b, child_indices);
}

static nir_ssa_def *
intersect_ray_amd_software_tri(struct radv_device *device,
                               nir_builder *b, nir_ssa_def *bvh_node,
                               nir_ssa_def *ray_tmax, nir_ssa_def *origin,
                               nir_ssa_def *dir, nir_ssa_def *inv_dir)
{
   const struct glsl_type *vec4_type = glsl_vector_type(GLSL_TYPE_FLOAT, 4);

   nir_ssa_def *node_addr = build_node_to_addr(device, b, bvh_node);

   const uint32_t coord_offsets[3] = {
      offsetof(struct radv_bvh_triangle_node, coords[0]),
      offsetof(struct radv_bvh_triangle_node, coords[1]),
      offsetof(struct radv_bvh_triangle_node, coords[2]),
   };

   /* node->coords[0], node->coords[1], node->coords[2] -> vec3 */
   nir_ssa_def *node_coords[3] = {
      nir_build_load_global(b, 3, 32, nir_iadd(b, node_addr, nir_imm_int64(b, coord_offsets[0])), .align_mul = 64, .align_offset = coord_offsets[0] % 64 ),
      nir_build_load_global(b, 3, 32, nir_iadd(b, node_addr, nir_imm_int64(b, coord_offsets[1])), .align_mul = 64, .align_offset = coord_offsets[1] % 64 ),
      nir_build_load_global(b, 3, 32, nir_iadd(b, node_addr, nir_imm_int64(b, coord_offsets[2])), .align_mul = 64, .align_offset = coord_offsets[2] % 64 ),
   };

   nir_variable *result = nir_variable_create(b->shader, nir_var_shader_temp, vec4_type, "result");
   nir_store_var(b, result, nir_imm_vec4(b, INFINITY, 1.0f, 0.0f, 0.0f), 0xf);

   /* Based on watertight Ray/Triangle intersection from
    * http://jcgt.org/published/0002/01/05/paper.pdf */

   /* Calculate the dimension where the ray direction is largest */
   nir_ssa_def *abs_dir = nir_fabs(b, dir);

   nir_ssa_def *abs_dirs[3] = {
      nir_channel(b, abs_dir, 0),
      nir_channel(b, abs_dir, 1),
      nir_channel(b, abs_dir, 2),
   };
   /* Find index of greatest value of abs_dir and put that as kz. */
   nir_ssa_def *kz = nir_bcsel(b, nir_fge(b, abs_dirs[0], abs_dirs[1]),
         nir_bcsel(b, nir_fge(b, abs_dirs[0], abs_dirs[2]),
            nir_imm_int(b, 0), nir_imm_int(b, 2)),
         nir_bcsel(b, nir_fge(b, abs_dirs[1], abs_dirs[2]),
            nir_imm_int(b, 1), nir_imm_int(b, 2)));
   nir_ssa_def *kx = nir_imod(b, nir_iadd(b, kz, nir_imm_int(b, 1)), nir_imm_int(b, 3));
   nir_ssa_def *ky = nir_imod(b, nir_iadd(b, kx, nir_imm_int(b, 1)), nir_imm_int(b, 3));
   nir_ssa_def *k_indices[3] = { kx, ky, kz };
   nir_ssa_def *k = nir_vec(b, k_indices, 3);

   /* Swap kx and ky dimensions to preseve winding order */
   unsigned swap_xy_swizzle[4] = {1, 0, 2, 3};
   k = nir_bcsel(b,
      nir_flt(b, nir_vector_extract(b, dir, kz), nir_imm_float(b, 0.0f)),
      nir_swizzle(b, k, swap_xy_swizzle, 3),
      k);

   kx = nir_channel(b, k, 0);
   ky = nir_channel(b, k, 1);
   kz = nir_channel(b, k, 2);

   /* Calculate shear constants */
   nir_ssa_def *sz = nir_frcp(b, nir_vector_extract(b, dir, kz));
   nir_ssa_def *sx = nir_fmul(b, nir_vector_extract(b, dir, kx), sz);
   nir_ssa_def *sy = nir_fmul(b, nir_vector_extract(b, dir, ky), sz);

   /* Calculate vertices relative to ray origin */
   nir_ssa_def *v_a = nir_fsub(b, node_coords[0], origin);
   nir_ssa_def *v_b = nir_fsub(b, node_coords[1], origin);
   nir_ssa_def *v_c = nir_fsub(b, node_coords[2], origin);

   /* Perform shear and scale */
   nir_ssa_def *ax = nir_fsub(b, nir_vector_extract(b, v_a, kx), nir_fmul(b, sx, nir_vector_extract(b, v_a, kz)));
   nir_ssa_def *ay = nir_fsub(b, nir_vector_extract(b, v_a, ky), nir_fmul(b, sy, nir_vector_extract(b, v_a, kz)));
   nir_ssa_def *bx = nir_fsub(b, nir_vector_extract(b, v_b, kx), nir_fmul(b, sx, nir_vector_extract(b, v_b, kz)));
   nir_ssa_def *by = nir_fsub(b, nir_vector_extract(b, v_b, ky), nir_fmul(b, sy, nir_vector_extract(b, v_b, kz)));
   nir_ssa_def *cx = nir_fsub(b, nir_vector_extract(b, v_c, kx), nir_fmul(b, sx, nir_vector_extract(b, v_c, kz)));
   nir_ssa_def *cy = nir_fsub(b, nir_vector_extract(b, v_c, ky), nir_fmul(b, sy, nir_vector_extract(b, v_c, kz)));

   nir_ssa_def *u = nir_fsub(b, nir_fmul(b, cx, by), nir_fmul(b, cy, bx));
   nir_ssa_def *v = nir_fsub(b, nir_fmul(b, ax, cy), nir_fmul(b, ay, cx));
   nir_ssa_def *w = nir_fsub(b, nir_fmul(b, bx, ay), nir_fmul(b, by, ax));

   nir_variable *u_var = nir_variable_create(b->shader, nir_var_shader_temp, glsl_float_type(), "u");
   nir_variable *v_var = nir_variable_create(b->shader, nir_var_shader_temp, glsl_float_type(), "v");
   nir_variable *w_var = nir_variable_create(b->shader, nir_var_shader_temp, glsl_float_type(), "w");
   nir_store_var(b, u_var, u, 0x1);
   nir_store_var(b, v_var, v, 0x1);
   nir_store_var(b, w_var, w, 0x1);

   /* Fallback to testing edges with double precision...
    *
    * The Vulkan spec states it only needs single precision watertightness
    * but we fail dEQP-VK.ray_tracing_pipeline.watertightness.closedFan2.1024 with
    * failures = 1 without doing this. :( */
   nir_ssa_def *cond_retest = nir_ior(b, nir_ior(b,
      nir_feq(b, u, nir_imm_float(b, 0.0f)),
      nir_feq(b, v, nir_imm_float(b, 0.0f))),
      nir_feq(b, w, nir_imm_float(b, 0.0f)));

   nir_push_if(b, cond_retest);
   {
      ax = nir_f2f64(b, ax); ay = nir_f2f64(b, ay);
      bx = nir_f2f64(b, bx); by = nir_f2f64(b, by);
      cx = nir_f2f64(b, cx); cy = nir_f2f64(b, cy);

      nir_store_var(b, u_var, nir_f2f32(b, nir_fsub(b, nir_fmul(b, cx, by), nir_fmul(b, cy, bx))), 0x1);
      nir_store_var(b, v_var, nir_f2f32(b, nir_fsub(b, nir_fmul(b, ax, cy), nir_fmul(b, ay, cx))), 0x1);
      nir_store_var(b, w_var, nir_f2f32(b, nir_fsub(b, nir_fmul(b, bx, ay), nir_fmul(b, by, ax))), 0x1);
   }
   nir_pop_if(b, NULL);

   u = nir_load_var(b, u_var);
   v = nir_load_var(b, v_var);
   w = nir_load_var(b, w_var);

   /* Perform edge tests. */
   nir_ssa_def *cond_back = nir_ior(b, nir_ior(b,
      nir_flt(b, u, nir_imm_float(b, 0.0f)),
      nir_flt(b, v, nir_imm_float(b, 0.0f))),
      nir_flt(b, w, nir_imm_float(b, 0.0f)));

   nir_ssa_def *cond_front = nir_ior(b, nir_ior(b,
      nir_flt(b, nir_imm_float(b, 0.0f), u),
      nir_flt(b, nir_imm_float(b, 0.0f), v)),
      nir_flt(b, nir_imm_float(b, 0.0f), w));

   nir_ssa_def *cond = nir_inot(b, nir_iand(b, cond_back, cond_front));

   nir_push_if(b, cond);
   {
      nir_ssa_def *det = nir_fadd(b, u, nir_fadd(b, v, w));

      nir_ssa_def *az = nir_fmul(b, sz, nir_vector_extract(b, v_a, kz));
      nir_ssa_def *bz = nir_fmul(b, sz, nir_vector_extract(b, v_b, kz));
      nir_ssa_def *cz = nir_fmul(b, sz, nir_vector_extract(b, v_c, kz));

      nir_ssa_def *t = nir_fadd(b, nir_fadd(b, nir_fmul(b, u, az), nir_fmul(b, v, bz)), nir_fmul(b, w, cz));

      nir_ssa_def *t_signed = nir_fmul(b, nir_fsign(b, det), t);

      nir_ssa_def *det_cond_front = nir_inot(b, nir_flt(b, t_signed, nir_imm_float(b, 0.0f)));

      nir_push_if(b, det_cond_front);
      {
         nir_ssa_def *indices[4] = {
            t, det,
            v, w
         };
         nir_store_var(b, result, nir_vec(b, indices, 4), 0xf);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);

   return nir_load_var(b, result);
}

static void
insert_traversal(struct radv_device *device, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                 nir_builder *b, const struct rt_variables *vars)
{
   unsigned stack_entry_size = 4;
   unsigned lanes = b->shader->info.workgroup_size[0] * b->shader->info.workgroup_size[1] *
                    b->shader->info.workgroup_size[2];
   unsigned stack_entry_stride = stack_entry_size * lanes;
   nir_ssa_def *stack_entry_stride_def = nir_imm_int(b, stack_entry_stride);
   nir_ssa_def *stack_base =
      nir_iadd(b, nir_imm_int(b, b->shader->info.shared_size),
               nir_imul(b, nir_load_local_invocation_index(b), nir_imm_int(b, stack_entry_size)));

   /*
    * A top-level AS can contain 2^24 children and a bottom-level AS can contain 2^24 triangles. At
    * a branching factor of 4, that means we may need up to 24 levels of box nodes + 1 triangle node
    * + 1 instance node. Furthermore, when processing a box node, worst case we actually push all 4
    * children and remove one, so the DFS stack depth is box nodes * 3 + 2.
    */
   b->shader->info.shared_size += stack_entry_stride * 76;
   assert(b->shader->info.shared_size <= 32768);

   nir_ssa_def *accel_struct = nir_load_var(b, vars->accel_struct);

   struct rt_traversal_vars trav_vars = init_traversal_vars(b);

   /* Initialize the follow-up shader idx to 0, to be replaced by the miss shader
    * if we actually miss. */
   nir_store_var(b, vars->idx, nir_imm_int(b, 0), 1);

   nir_store_var(b, trav_vars.should_return, nir_imm_bool(b, false), 1);

   nir_push_if(b, nir_ine(b, accel_struct, nir_imm_int64(b, 0)));
   {
      nir_store_var(b, trav_vars.bvh_base, build_addr_to_node(b, accel_struct), 1);

      nir_ssa_def *bvh_root = nir_build_load_global(
         b, 1, 32, accel_struct, .access = ACCESS_NON_WRITEABLE, .align_mul = 64);

      /* We create a BVH descriptor that covers the entire memory range. That way we can always
       * use the same descriptor, which avoids divergence when different rays hit different
       * instances at the cost of having to use 64-bit node ids. */
      const uint64_t bvh_size = 1ull << 42;
      nir_ssa_def *desc = nir_imm_ivec4(
         b, 0, 1u << 31 /* Enable box sorting */, (bvh_size - 1) & 0xFFFFFFFFu,
         ((bvh_size - 1) >> 32) | (1u << 24 /* Return IJ for triangles */) | (1u << 31));

      nir_ssa_def *vec3ones = nir_channels(b, nir_imm_vec4(b, 1.0, 1.0, 1.0, 1.0), 0x7);
      nir_store_var(b, trav_vars.origin, nir_load_var(b, vars->origin), 7);
      nir_store_var(b, trav_vars.dir, nir_load_var(b, vars->direction), 7);
      nir_store_var(b, trav_vars.inv_dir, nir_fdiv(b, vec3ones, nir_load_var(b, trav_vars.dir)), 7);
      nir_store_var(b, trav_vars.sbt_offset_and_flags, nir_imm_int(b, 0), 1);
      nir_store_var(b, trav_vars.instance_addr, nir_imm_int64(b, 0), 1);

      nir_store_var(b, trav_vars.stack, nir_iadd(b, stack_base, stack_entry_stride_def), 1);
      nir_store_shared(b, bvh_root, stack_base, .base = 0, .align_mul = stack_entry_size);

      nir_store_var(b, trav_vars.top_stack, nir_imm_int(b, 0), 1);

      nir_push_loop(b);

      nir_push_if(b, nir_ieq(b, nir_load_var(b, trav_vars.stack), stack_base));
      nir_jump(b, nir_jump_break);
      nir_pop_if(b, NULL);

      nir_push_if(
         b, nir_uge(b, nir_load_var(b, trav_vars.top_stack), nir_load_var(b, trav_vars.stack)));
      nir_store_var(b, trav_vars.top_stack, nir_imm_int(b, 0), 1);
      nir_store_var(b, trav_vars.bvh_base,
                    build_addr_to_node(b, nir_load_var(b, vars->accel_struct)), 1);
      nir_store_var(b, trav_vars.origin, nir_load_var(b, vars->origin), 7);
      nir_store_var(b, trav_vars.dir, nir_load_var(b, vars->direction), 7);
      nir_store_var(b, trav_vars.inv_dir, nir_fdiv(b, vec3ones, nir_load_var(b, trav_vars.dir)), 7);
      nir_store_var(b, trav_vars.instance_addr, nir_imm_int64(b, 0), 1);

      nir_pop_if(b, NULL);

      nir_store_var(b, trav_vars.stack,
                    nir_isub(b, nir_load_var(b, trav_vars.stack), stack_entry_stride_def), 1);

      nir_ssa_def *bvh_node = nir_load_shared(b, 1, 32, nir_load_var(b, trav_vars.stack), .base = 0,
                                              .align_mul = stack_entry_size);
      nir_ssa_def *bvh_node_type = nir_iand(b, bvh_node, nir_imm_int(b, 7));

      bvh_node = nir_iadd(b, nir_load_var(b, trav_vars.bvh_base), nir_u2u(b, bvh_node, 64));
      nir_ssa_def *intrinsic_result = NULL;
      if (device->physical_device->rad_info.chip_class >= GFX10_3
       && !(device->instance->perftest_flags & RADV_PERFTEST_FORCE_EMULATE_RT)) {
         intrinsic_result = nir_bvh64_intersect_ray_amd(
            b, 32, desc, nir_unpack_64_2x32(b, bvh_node), nir_load_var(b, vars->tmax),
            nir_load_var(b, trav_vars.origin), nir_load_var(b, trav_vars.dir),
            nir_load_var(b, trav_vars.inv_dir));
      }

      nir_push_if(b, nir_ine(b, nir_iand(b, bvh_node_type, nir_imm_int(b, 4)), nir_imm_int(b, 0)));
      {
         nir_push_if(b,
                     nir_ine(b, nir_iand(b, bvh_node_type, nir_imm_int(b, 2)), nir_imm_int(b, 0)));
         {
            /* custom */
            nir_push_if(
               b, nir_ine(b, nir_iand(b, bvh_node_type, nir_imm_int(b, 1)), nir_imm_int(b, 0)));
            {
               insert_traversal_aabb_case(device, pCreateInfo, b, vars, &trav_vars, bvh_node);
            }
            nir_push_else(b, NULL);
            {
               /* instance */
               nir_ssa_def *instance_node_addr = build_node_to_addr(device, b, bvh_node);
               nir_ssa_def *instance_data =
                  nir_build_load_global(b, 4, 32, instance_node_addr, .align_mul = 64);
               nir_ssa_def *wto_matrix[] = {
                  nir_build_load_global(b, 4, 32,
                                        nir_iadd(b, instance_node_addr, nir_imm_int64(b, 16)),
                                        .align_mul = 64, .align_offset = 16),
                  nir_build_load_global(b, 4, 32,
                                        nir_iadd(b, instance_node_addr, nir_imm_int64(b, 32)),
                                        .align_mul = 64, .align_offset = 32),
                  nir_build_load_global(b, 4, 32,
                                        nir_iadd(b, instance_node_addr, nir_imm_int64(b, 48)),
                                        .align_mul = 64, .align_offset = 48)};
               nir_ssa_def *instance_id = nir_build_load_global(
                  b, 1, 32, nir_iadd(b, instance_node_addr, nir_imm_int64(b, 88)));
               nir_ssa_def *instance_and_mask = nir_channel(b, instance_data, 2);
               nir_ssa_def *instance_mask = nir_ushr(b, instance_and_mask, nir_imm_int(b, 24));

               nir_push_if(b,
                           nir_ieq(b, nir_iand(b, instance_mask, nir_load_var(b, vars->cull_mask)),
                                   nir_imm_int(b, 0)));
               nir_jump(b, nir_jump_continue);
               nir_pop_if(b, NULL);

               nir_store_var(b, trav_vars.top_stack, nir_load_var(b, trav_vars.stack), 1);
               nir_store_var(b, trav_vars.bvh_base,
                             build_addr_to_node(
                                b, nir_pack_64_2x32(b, nir_channels(b, instance_data, 0x3))),
                             1);
               nir_store_shared(
                  b, nir_iand(b, nir_channel(b, instance_data, 0), nir_imm_int(b, 63)),
                  nir_load_var(b, trav_vars.stack), .base = 0, .align_mul = stack_entry_size);
               nir_store_var(b, trav_vars.stack,
                             nir_iadd(b, nir_load_var(b, trav_vars.stack), stack_entry_stride_def),
                             1);

               nir_store_var(
                  b, trav_vars.origin,
                  nir_build_vec3_mat_mult_pre(b, nir_load_var(b, vars->origin), wto_matrix), 7);
               nir_store_var(
                  b, trav_vars.dir,
                  nir_build_vec3_mat_mult(b, nir_load_var(b, vars->direction), wto_matrix, false),
                  7);
               nir_store_var(b, trav_vars.inv_dir,
                             nir_fdiv(b, vec3ones, nir_load_var(b, trav_vars.dir)), 7);
               nir_store_var(b, trav_vars.custom_instance_and_mask, instance_and_mask, 1);
               nir_store_var(b, trav_vars.sbt_offset_and_flags, nir_channel(b, instance_data, 3),
                             1);
               nir_store_var(b, trav_vars.instance_id, instance_id, 1);
               nir_store_var(b, trav_vars.instance_addr, instance_node_addr, 1);
            }
            nir_pop_if(b, NULL);
         }
         nir_push_else(b, NULL);
         {
            /* box */
            nir_ssa_def *result = intrinsic_result;
            if (!result) {
               /* If we didn't run the intrinsic cause the hardware didn't support it,
                * emulate ray/box intersection here */
               result = intersect_ray_amd_software_box(device,
                  b, bvh_node, nir_load_var(b, vars->tmax), nir_load_var(b, trav_vars.origin),
                  nir_load_var(b, trav_vars.dir), nir_load_var(b, trav_vars.inv_dir));
            }

            for (unsigned i = 4; i-- > 0; ) {
               nir_ssa_def *new_node = nir_vector_extract(b, result, nir_imm_int(b, i));
               nir_push_if(b, nir_ine(b, new_node, nir_imm_int(b, 0xffffffff)));
               {
                  nir_store_shared(b, new_node, nir_load_var(b, trav_vars.stack), .base = 0,
                                   .align_mul = stack_entry_size);
                  nir_store_var(
                     b, trav_vars.stack,
                     nir_iadd(b, nir_load_var(b, trav_vars.stack), stack_entry_stride_def), 1);
               }
               nir_pop_if(b, NULL);
            }
         }
         nir_pop_if(b, NULL);
      }
      nir_push_else(b, NULL);
      {
         nir_ssa_def *result = intrinsic_result;
         if (!result) {
            /* If we didn't run the intrinsic cause the hardware didn't support it,
             * emulate ray/tri intersection here */
            result = intersect_ray_amd_software_tri(device,
               b, bvh_node, nir_load_var(b, vars->tmax), nir_load_var(b, trav_vars.origin),
               nir_load_var(b, trav_vars.dir), nir_load_var(b, trav_vars.inv_dir));
         }
         insert_traversal_triangle_case(device, pCreateInfo, b, result, vars, &trav_vars, bvh_node);
      }
      nir_pop_if(b, NULL);

      nir_pop_loop(b, NULL);
   }
   nir_pop_if(b, NULL);

   /* should_return is set if we had a hit but we won't be calling the closest hit shader and hence
    * need to return immediately to the calling shader. */
   nir_push_if(b, nir_load_var(b, trav_vars.should_return));
   {
      insert_rt_return(b, vars);
   }
   nir_push_else(b, NULL);
   {
      /* Only load the miss shader if we actually miss, which we determining by not having set
       * a closest hit shader. It is valid to not specify an SBT pointer for miss shaders if none
       * of the rays miss. */
      nir_push_if(b, nir_ieq(b, nir_load_var(b, vars->idx), nir_imm_int(b, 0)));
      {
         load_sbt_entry(b, vars, nir_load_var(b, vars->miss_index), SBT_MISS, 0);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static unsigned
compute_rt_stack_size(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                      const struct radv_pipeline_shader_stack_size *stack_sizes)
{
   unsigned raygen_size = 0;
   unsigned callable_size = 0;
   unsigned chit_size = 0;
   unsigned miss_size = 0;
   unsigned non_recursive_size = 0;

   for (unsigned i = 0; i < pCreateInfo->groupCount; ++i) {
      non_recursive_size = MAX2(stack_sizes[i].non_recursive_size, non_recursive_size);

      const VkRayTracingShaderGroupCreateInfoKHR *group_info = &pCreateInfo->pGroups[i];
      uint32_t shader_id = VK_SHADER_UNUSED_KHR;
      unsigned size = stack_sizes[i].recursive_size;

      switch (group_info->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR:
         shader_id = group_info->generalShader;
         break;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR:
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR:
         shader_id = group_info->closestHitShader;
         break;
      default:
         break;
      }
      if (shader_id == VK_SHADER_UNUSED_KHR)
         continue;

      const VkPipelineShaderStageCreateInfo *stage = &pCreateInfo->pStages[shader_id];
      switch (stage->stage) {
      case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
         raygen_size = MAX2(raygen_size, size);
         break;
      case VK_SHADER_STAGE_MISS_BIT_KHR:
         miss_size = MAX2(miss_size, size);
         break;
      case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
         chit_size = MAX2(chit_size, size);
         break;
      case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
         callable_size = MAX2(callable_size, size);
         break;
      default:
         unreachable("Invalid stage type in RT shader");
      }
   }
   return raygen_size +
          MIN2(pCreateInfo->maxPipelineRayRecursionDepth, 1) *
             MAX2(MAX2(chit_size, miss_size), non_recursive_size) +
          MAX2(0, (int)(pCreateInfo->maxPipelineRayRecursionDepth) - 1) *
             MAX2(chit_size, miss_size) +
          2 * callable_size;
}

bool
radv_rt_pipeline_has_dynamic_stack_size(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo)
{
   if (!pCreateInfo->pDynamicState)
      return false;

   for (unsigned i = 0; i < pCreateInfo->pDynamicState->dynamicStateCount; ++i) {
      if (pCreateInfo->pDynamicState->pDynamicStates[i] ==
          VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR)
         return true;
   }

   return false;
}

static nir_shader *
create_rt_shader(struct radv_device *device, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                 struct radv_pipeline_shader_stack_size *stack_sizes)
{
   RADV_FROM_HANDLE(radv_pipeline_layout, layout, pCreateInfo->layout);
   struct radv_pipeline_key key;
   memset(&key, 0, sizeof(key));

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL, "rt_combined");

   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = device->physical_device->rt_wave_size == 64 ? 8 : 4;
   b.shader->info.workgroup_size[2] = 1;

   struct rt_variables vars = create_rt_variables(b.shader, stack_sizes);
   load_sbt_entry(&b, &vars, nir_imm_int(&b, 0), SBT_RAYGEN, 0);
   nir_store_var(&b, vars.stack_ptr, nir_imm_int(&b, 0), 0x1);

   nir_store_var(&b, vars.main_loop_case_visited, nir_imm_bool(&b, true), 1);

   nir_loop *loop = nir_push_loop(&b);

   nir_push_if(&b, nir_ior(&b, nir_ieq(&b, nir_load_var(&b, vars.idx), nir_imm_int(&b, 0)),
                           nir_ine(&b, nir_load_var(&b, vars.main_loop_case_visited),
                                   nir_imm_bool(&b, true))));
   nir_jump(&b, nir_jump_break);
   nir_pop_if(&b, NULL);

   nir_store_var(&b, vars.main_loop_case_visited, nir_imm_bool(&b, false), 1);

   nir_push_if(&b, nir_ieq(&b, nir_load_var(&b, vars.idx), nir_imm_int(&b, 1)));
   nir_store_var(&b, vars.main_loop_case_visited, nir_imm_bool(&b, true), 1);
   insert_traversal(device, pCreateInfo, &b, &vars);
   nir_pop_if(&b, NULL);

   nir_ssa_def *idx = nir_load_var(&b, vars.idx);

   /* We do a trick with the indexing of the resume shaders so that the first
    * shader of group x always gets id x and the resume shader ids then come after
    * groupCount. This makes the shadergroup handles independent of compilation. */
   unsigned call_idx_base = pCreateInfo->groupCount + 1;
   for (unsigned i = 0; i < pCreateInfo->groupCount; ++i) {
      const VkRayTracingShaderGroupCreateInfoKHR *group_info = &pCreateInfo->pGroups[i];
      uint32_t shader_id = VK_SHADER_UNUSED_KHR;

      switch (group_info->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR:
         shader_id = group_info->generalShader;
         break;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR:
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR:
         shader_id = group_info->closestHitShader;
         break;
      default:
         break;
      }
      if (shader_id == VK_SHADER_UNUSED_KHR)
         continue;

      const VkPipelineShaderStageCreateInfo *stage = &pCreateInfo->pStages[shader_id];
      nir_shader *nir_stage = parse_rt_stage(device, layout, stage);

      b.shader->options = nir_stage->options;

      uint32_t num_resume_shaders = 0;
      nir_shader **resume_shaders = NULL;
      nir_lower_shader_calls(nir_stage, nir_address_format_32bit_offset, 16, &resume_shaders,
                             &num_resume_shaders, nir_stage);

      vars.group_idx = i;
      insert_rt_case(&b, nir_stage, &vars, idx, call_idx_base, i + 2);
      for (unsigned j = 0; j < num_resume_shaders; ++j) {
         insert_rt_case(&b, resume_shaders[j], &vars, idx, call_idx_base, call_idx_base + 1 + j);
      }
      call_idx_base += num_resume_shaders;
   }

   nir_pop_loop(&b, loop);

   if (radv_rt_pipeline_has_dynamic_stack_size(pCreateInfo)) {
      /* Put something so scratch gets enabled in the shader. */
      b.shader->scratch_size = 16;
   } else
      b.shader->scratch_size = compute_rt_stack_size(pCreateInfo, stack_sizes);

   /* Deal with all the inline functions. */
   nir_index_ssa_defs(nir_shader_get_entrypoint(b.shader));
   nir_metadata_preserve(nir_shader_get_entrypoint(b.shader), nir_metadata_none);

   return b.shader;
}

static VkResult
radv_rt_pipeline_create(VkDevice _device, VkPipelineCache _cache,
                        const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VkResult result;
   struct radv_pipeline *pipeline = NULL;
   struct radv_pipeline_shader_stack_size *stack_sizes = NULL;
   uint8_t hash[20];
   nir_shader *shader = NULL;
   bool keep_statistic_info =
      (pCreateInfo->flags & VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR) ||
      (device->instance->debug_flags & RADV_DEBUG_DUMP_SHADER_STATS) || device->keep_shader_info;

   if (pCreateInfo->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR)
      return radv_rt_pipeline_library_create(_device, _cache, pCreateInfo, pAllocator, pPipeline);

   VkRayTracingPipelineCreateInfoKHR local_create_info =
      radv_create_merged_rt_create_info(pCreateInfo);
   if (!local_create_info.pStages || !local_create_info.pGroups) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   radv_hash_rt_shaders(hash, &local_create_info, radv_get_hash_flags(device, keep_statistic_info));
   struct vk_shader_module module = {.base.type = VK_OBJECT_TYPE_SHADER_MODULE};

   VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT subgroup_size = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT,
      .pNext = NULL,
      .requiredSubgroupSize = device->physical_device->rt_wave_size,
   };

   VkComputePipelineCreateInfo compute_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .pNext = NULL,
      .flags = pCreateInfo->flags | VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT,
      .stage =
         {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = &subgroup_size,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = vk_shader_module_to_handle(&module),
            .pName = "main",
         },
      .layout = pCreateInfo->layout,
   };

   /* First check if we can get things from the cache before we take the expensive step of
    * generating the nir. */
   result = radv_compute_pipeline_create(_device, _cache, &compute_info, pAllocator, hash,
                                         stack_sizes, local_create_info.groupCount, pPipeline);
   if (result == VK_PIPELINE_COMPILE_REQUIRED_EXT) {
      stack_sizes = calloc(sizeof(*stack_sizes), local_create_info.groupCount);
      if (!stack_sizes) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      shader = create_rt_shader(device, &local_create_info, stack_sizes);
      module.nir = shader;
      compute_info.flags = pCreateInfo->flags;
      result = radv_compute_pipeline_create(_device, _cache, &compute_info, pAllocator, hash,
                                            stack_sizes, local_create_info.groupCount, pPipeline);
      stack_sizes = NULL;

      if (result != VK_SUCCESS)
         goto shader_fail;
   }
   pipeline = radv_pipeline_from_handle(*pPipeline);

   pipeline->compute.rt_group_handles =
      calloc(sizeof(*pipeline->compute.rt_group_handles), local_create_info.groupCount);
   if (!pipeline->compute.rt_group_handles) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto shader_fail;
   }

   pipeline->compute.dynamic_stack_size = radv_rt_pipeline_has_dynamic_stack_size(pCreateInfo);

   for (unsigned i = 0; i < local_create_info.groupCount; ++i) {
      const VkRayTracingShaderGroupCreateInfoKHR *group_info = &local_create_info.pGroups[i];
      switch (group_info->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR:
         if (group_info->generalShader != VK_SHADER_UNUSED_KHR)
            pipeline->compute.rt_group_handles[i].handles[0] = i + 2;
         break;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR:
         if (group_info->intersectionShader != VK_SHADER_UNUSED_KHR)
            pipeline->compute.rt_group_handles[i].handles[1] = i + 2;
         FALLTHROUGH;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR:
         if (group_info->closestHitShader != VK_SHADER_UNUSED_KHR)
            pipeline->compute.rt_group_handles[i].handles[0] = i + 2;
         if (group_info->anyHitShader != VK_SHADER_UNUSED_KHR)
            pipeline->compute.rt_group_handles[i].handles[1] = i + 2;
         break;
      case VK_SHADER_GROUP_SHADER_MAX_ENUM_KHR:
         unreachable("VK_SHADER_GROUP_SHADER_MAX_ENUM_KHR");
      }
   }

shader_fail:
   if (result != VK_SUCCESS && pipeline)
      radv_pipeline_destroy(device, pipeline, pAllocator);
   ralloc_free(shader);
fail:
   free((void *)local_create_info.pGroups);
   free((void *)local_create_info.pStages);
   free(stack_sizes);
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateRayTracingPipelinesKHR(VkDevice _device, VkDeferredOperationKHR deferredOperation,
                                  VkPipelineCache pipelineCache, uint32_t count,
                                  const VkRayTracingPipelineCreateInfoKHR *pCreateInfos,
                                  const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   VkResult result = VK_SUCCESS;

   unsigned i = 0;
   for (; i < count; i++) {
      VkResult r;
      r = radv_rt_pipeline_create(_device, pipelineCache, &pCreateInfos[i], pAllocator,
                                  &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT_EXT)
            break;
      }
   }

   for (; i < count; ++i)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetRayTracingShaderGroupHandlesKHR(VkDevice device, VkPipeline _pipeline, uint32_t firstGroup,
                                        uint32_t groupCount, size_t dataSize, void *pData)
{
   RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);
   char *data = pData;

   STATIC_ASSERT(sizeof(*pipeline->compute.rt_group_handles) <= RADV_RT_HANDLE_SIZE);

   memset(data, 0, groupCount * RADV_RT_HANDLE_SIZE);

   for (uint32_t i = 0; i < groupCount; ++i) {
      memcpy(data + i * RADV_RT_HANDLE_SIZE, &pipeline->compute.rt_group_handles[firstGroup + i],
             sizeof(*pipeline->compute.rt_group_handles));
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkDeviceSize VKAPI_CALL
radv_GetRayTracingShaderGroupStackSizeKHR(VkDevice device, VkPipeline _pipeline, uint32_t group,
                                          VkShaderGroupShaderKHR groupShader)
{
   RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);
   const struct radv_pipeline_shader_stack_size *stack_size =
      &pipeline->compute.rt_stack_sizes[group];

   if (groupShader == VK_SHADER_GROUP_SHADER_ANY_HIT_KHR ||
       groupShader == VK_SHADER_GROUP_SHADER_INTERSECTION_KHR)
      return stack_size->non_recursive_size;
   else
      return stack_size->recursive_size;
}
