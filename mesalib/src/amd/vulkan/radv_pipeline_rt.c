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
#include "radv_meta.h"
#include "radv_private.h"
#include "radv_rt_common.h"
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
         RADV_FROM_HANDLE(radv_pipeline, pipeline, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_library_pipeline *library_pipeline = radv_pipeline_to_library(pipeline);

         total_stages += library_pipeline->stage_count;
         total_groups += library_pipeline->group_count;
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
         RADV_FROM_HANDLE(radv_pipeline, pipeline, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_library_pipeline *library_pipeline = radv_pipeline_to_library(pipeline);

         for (unsigned j = 0; j < library_pipeline->stage_count; ++j)
            stages[total_stages + j] = library_pipeline->stages[j];
         for (unsigned j = 0; j < library_pipeline->group_count; ++j) {
            VkRayTracingShaderGroupCreateInfoKHR *dst = &groups[total_groups + j];
            *dst = library_pipeline->groups[j];
            if (dst->generalShader != VK_SHADER_UNUSED_KHR)
               dst->generalShader += total_stages;
            if (dst->closestHitShader != VK_SHADER_UNUSED_KHR)
               dst->closestHitShader += total_stages;
            if (dst->anyHitShader != VK_SHADER_UNUSED_KHR)
               dst->anyHitShader += total_stages;
            if (dst->intersectionShader != VK_SHADER_UNUSED_KHR)
               dst->intersectionShader += total_stages;
         }
         total_stages += library_pipeline->stage_count;
         total_groups += library_pipeline->group_count;
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
   struct radv_library_pipeline *pipeline;

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_pipeline_init(device, &pipeline->base, RADV_PIPELINE_LIBRARY);

   VkRayTracingPipelineCreateInfoKHR local_create_info =
      radv_create_merged_rt_create_info(pCreateInfo);
   if (!local_create_info.pStages || !local_create_info.pGroups)
      goto fail;

   if (local_create_info.stageCount) {
      pipeline->stage_count = local_create_info.stageCount;

      size_t size = sizeof(VkPipelineShaderStageCreateInfo) * local_create_info.stageCount;
      pipeline->stages = malloc(size);
      if (!pipeline->stages)
         goto fail;

      memcpy(pipeline->stages, local_create_info.pStages, size);

      pipeline->hashes = malloc(sizeof(*pipeline->hashes) * local_create_info.stageCount);
      if (!pipeline->hashes)
         goto fail;

      pipeline->identifiers = malloc(sizeof(*pipeline->identifiers) * local_create_info.stageCount);
      if (!pipeline->identifiers)
         goto fail;

      for (uint32_t i = 0; i < local_create_info.stageCount; i++) {
         RADV_FROM_HANDLE(vk_shader_module, module, pipeline->stages[i].module);

         const VkPipelineShaderStageModuleIdentifierCreateInfoEXT *iinfo =
            vk_find_struct_const(local_create_info.pStages[i].pNext,
                                 PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT);

         if (module) {
            struct vk_shader_module *new_module = vk_shader_module_clone(NULL, module);
            pipeline->stages[i].module = vk_shader_module_to_handle(new_module);
            pipeline->stages[i].pNext = NULL;
         } else {
            assert(iinfo);
            pipeline->identifiers[i].identifierSize =
               MIN2(iinfo->identifierSize, sizeof(pipeline->hashes[i].sha1));
            memcpy(pipeline->hashes[i].sha1, iinfo->pIdentifier,
                   pipeline->identifiers[i].identifierSize);
            pipeline->stages[i].module = VK_NULL_HANDLE;
            pipeline->stages[i].pNext = &pipeline->identifiers[i];
            pipeline->identifiers[i].sType =
               VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT;
            pipeline->identifiers[i].pNext = NULL;
            pipeline->identifiers[i].pIdentifier = pipeline->hashes[i].sha1;
         }
      }
   }

   if (local_create_info.groupCount) {
      size_t size = sizeof(VkRayTracingShaderGroupCreateInfoKHR) * local_create_info.groupCount;
      pipeline->group_count = local_create_info.groupCount;
      pipeline->groups = malloc(size);
      if (!pipeline->groups)
         goto fail;
      memcpy(pipeline->groups, local_create_info.pGroups, size);
   }

   *pPipeline = radv_pipeline_to_handle(&pipeline->base);

   free((void *)local_create_info.pGroups);
   free((void *)local_create_info.pStages);
   return VK_SUCCESS;
fail:
   free(pipeline->groups);
   free(pipeline->stages);
   free(pipeline->hashes);
   free(pipeline->identifiers);
   free((void *)local_create_info.pGroups);
   free((void *)local_create_info.pStages);
   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

/*
 * Global variables for an RT pipeline
 */
struct rt_variables {
   const VkRayTracingPipelineCreateInfoKHR *create_info;

   /* idx of the next shader to run in the next iteration of the main loop.
    * During traversal, idx is used to store the SBT index and will contain
    * the correct resume index upon returning.
    */
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

   /* Output variables for intersection & anyhit shaders. */
   nir_variable *ahit_accept;
   nir_variable *ahit_terminate;

   /* Array of stack size struct for recording the max stack size for each group. */
   struct radv_pipeline_shader_stack_size *stack_sizes;
   unsigned stage_idx;
};

static void
reserve_stack_size(struct rt_variables *vars, uint32_t size)
{
   for (uint32_t group_idx = 0; group_idx < vars->create_info->groupCount; group_idx++) {
      const VkRayTracingShaderGroupCreateInfoKHR *group = vars->create_info->pGroups + group_idx;

      if (vars->stage_idx == group->generalShader || vars->stage_idx == group->closestHitShader)
         vars->stack_sizes[group_idx].recursive_size =
            MAX2(vars->stack_sizes[group_idx].recursive_size, size);

      if (vars->stage_idx == group->anyHitShader || vars->stage_idx == group->intersectionShader)
         vars->stack_sizes[group_idx].non_recursive_size =
            MAX2(vars->stack_sizes[group_idx].non_recursive_size, size);
   }
}

static struct rt_variables
create_rt_variables(nir_shader *shader, const VkRayTracingPipelineCreateInfoKHR *create_info,
                    struct radv_pipeline_shader_stack_size *stack_sizes)
{
   struct rt_variables vars = {
      .create_info = create_info,
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
   vars.ahit_accept =
      nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "ahit_accept");
   vars.ahit_terminate =
      nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "ahit_terminate");

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
   src->create_info = dst->create_info;

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
   _mesa_hash_table_insert(var_remap, src->ahit_accept, dst->ahit_accept);
   _mesa_hash_table_insert(var_remap, src->ahit_terminate, dst->ahit_terminate);

   src->stack_sizes = dst->stack_sizes;
   src->stage_idx = dst->stage_idx;
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
   nir_store_var(b, vars->stack_ptr, nir_iadd_imm(b, nir_load_var(b, vars->stack_ptr), -16), 1);
   nir_store_var(b, vars->idx,
                 nir_load_scratch(b, 1, 32, nir_load_var(b, vars->stack_ptr), .align_mul = 16), 1);
}

enum sbt_type {
   SBT_RAYGEN = offsetof(VkTraceRaysIndirectCommand2KHR, raygenShaderRecordAddress),
   SBT_MISS = offsetof(VkTraceRaysIndirectCommand2KHR, missShaderBindingTableAddress),
   SBT_HIT = offsetof(VkTraceRaysIndirectCommand2KHR, hitShaderBindingTableAddress),
   SBT_CALLABLE = offsetof(VkTraceRaysIndirectCommand2KHR, callableShaderBindingTableAddress),
};

static nir_ssa_def *
get_sbt_ptr(nir_builder *b, nir_ssa_def *idx, enum sbt_type binding)
{
   nir_ssa_def *desc_base_addr = nir_load_sbt_base_amd(b);

   nir_ssa_def *desc =
      nir_pack_64_2x32(b, nir_build_load_smem_amd(b, 2, desc_base_addr, nir_imm_int(b, binding)));

   nir_ssa_def *stride_offset = nir_imm_int(b, binding + (binding == SBT_RAYGEN ? 8 : 16));
   nir_ssa_def *stride =
      nir_pack_64_2x32(b, nir_build_load_smem_amd(b, 2, desc_base_addr, stride_offset));

   return nir_iadd(b, desc, nir_imul(b, nir_u2u64(b, idx), stride));
}

static void
load_sbt_entry(nir_builder *b, const struct rt_variables *vars, nir_ssa_def *idx,
               enum sbt_type binding, unsigned offset)
{
   nir_ssa_def *addr = get_sbt_ptr(b, idx, binding);

   nir_ssa_def *load_addr = nir_iadd_imm(b, addr, offset);
   nir_ssa_def *v_idx = nir_build_load_global(b, 1, 32, load_addr);

   nir_store_var(b, vars->idx, v_idx, 1);

   nir_ssa_def *record_addr = nir_iadd_imm(b, addr, RADV_RT_HANDLE_SIZE);
   nir_store_var(b, vars->shader_record_ptr, record_addr, 1);
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
            b_shader.cursor = nir_before_instr(instr);
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            nir_ssa_def *ret = NULL;

            switch (intr->intrinsic) {
            case nir_intrinsic_rt_execute_callable: {
               uint32_t size = align(nir_intrinsic_stack_size(intr), 16) + RADV_MAX_HIT_ATTRIB_SIZE;
               uint32_t ret_idx = call_idx_base + nir_intrinsic_call_idx(intr) + 1;

               nir_store_var(
                  &b_shader, vars->stack_ptr,
                  nir_iadd_imm(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), size), 1);
               nir_store_scratch(&b_shader, nir_imm_int(&b_shader, ret_idx),
                                 nir_load_var(&b_shader, vars->stack_ptr), .align_mul = 16);

               nir_store_var(&b_shader, vars->stack_ptr,
                             nir_iadd_imm(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), 16),
                             1);
               load_sbt_entry(&b_shader, vars, intr->src[0].ssa, SBT_CALLABLE, 0);

               nir_store_var(&b_shader, vars->arg,
                             nir_iadd_imm(&b_shader, intr->src[1].ssa, -size - 16), 1);

               reserve_stack_size(vars, size + 16);
               break;
            }
            case nir_intrinsic_rt_trace_ray: {
               uint32_t size = align(nir_intrinsic_stack_size(intr), 16) + RADV_MAX_HIT_ATTRIB_SIZE;
               uint32_t ret_idx = call_idx_base + nir_intrinsic_call_idx(intr) + 1;

               nir_store_var(
                  &b_shader, vars->stack_ptr,
                  nir_iadd_imm(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), size), 1);
               nir_store_scratch(&b_shader, nir_imm_int(&b_shader, ret_idx),
                                 nir_load_var(&b_shader, vars->stack_ptr), .align_mul = 16);

               nir_store_var(&b_shader, vars->stack_ptr,
                             nir_iadd_imm(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), 16),
                             1);

               nir_store_var(&b_shader, vars->idx, nir_imm_int(&b_shader, 1), 1);
               nir_store_var(&b_shader, vars->arg,
                             nir_iadd_imm(&b_shader, intr->src[10].ssa, -size - 16), 1);

               reserve_stack_size(vars, size + 16);

               /* Per the SPIR-V extension spec we have to ignore some bits for some arguments. */
               nir_store_var(&b_shader, vars->accel_struct, intr->src[0].ssa, 0x1);
               nir_store_var(&b_shader, vars->flags, intr->src[1].ssa, 0x1);
               nir_store_var(&b_shader, vars->cull_mask,
                             nir_iand_imm(&b_shader, intr->src[2].ssa, 0xff), 0x1);
               nir_store_var(&b_shader, vars->sbt_offset,
                             nir_iand_imm(&b_shader, intr->src[3].ssa, 0xf), 0x1);
               nir_store_var(&b_shader, vars->sbt_stride,
                             nir_iand_imm(&b_shader, intr->src[4].ssa, 0xf), 0x1);
               nir_store_var(&b_shader, vars->miss_index,
                             nir_iand_imm(&b_shader, intr->src[5].ssa, 0xffff), 0x1);
               nir_store_var(&b_shader, vars->origin, intr->src[6].ssa, 0x7);
               nir_store_var(&b_shader, vars->tmin, intr->src[7].ssa, 0x1);
               nir_store_var(&b_shader, vars->direction, intr->src[8].ssa, 0x7);
               nir_store_var(&b_shader, vars->tmax, intr->src[9].ssa, 0x1);
               break;
            }
            case nir_intrinsic_rt_resume: {
               uint32_t size = align(nir_intrinsic_stack_size(intr), 16) + RADV_MAX_HIT_ATTRIB_SIZE;

               nir_store_var(
                  &b_shader, vars->stack_ptr,
                  nir_iadd_imm(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), -size), 1);
               break;
            }
            case nir_intrinsic_rt_return_amd: {
               if (shader->info.stage == MESA_SHADER_RAYGEN) {
                  nir_store_var(&b_shader, vars->idx, nir_imm_int(&b_shader, 0), 1);
                  break;
               }
               insert_rt_return(&b_shader, vars);
               break;
            }
            case nir_intrinsic_load_scratch: {
               nir_instr_rewrite_src_ssa(
                  instr, &intr->src[0],
                  nir_iadd(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), intr->src[0].ssa));
               continue;
            }
            case nir_intrinsic_store_scratch: {
               nir_instr_rewrite_src_ssa(
                  instr, &intr->src[1],
                  nir_iadd(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), intr->src[1].ssa));
               continue;
            }
            case nir_intrinsic_load_rt_arg_scratch_offset_amd: {
               ret = nir_load_var(&b_shader, vars->arg);
               break;
            }
            case nir_intrinsic_load_shader_record_ptr: {
               ret = nir_load_var(&b_shader, vars->shader_record_ptr);
               break;
            }
            case nir_intrinsic_load_ray_launch_id: {
               ret = nir_load_global_invocation_id(&b_shader, 32);
               break;
            }
            case nir_intrinsic_load_ray_launch_size: {
               nir_ssa_def *launch_size_addr =
                  nir_load_ray_launch_size_addr_amd(&b_shader);

               nir_ssa_def * xy = nir_build_load_smem_amd(
                  &b_shader, 2, launch_size_addr, nir_imm_int(&b_shader, 0));
               nir_ssa_def * z = nir_build_load_smem_amd(
                  &b_shader, 1, launch_size_addr, nir_imm_int(&b_shader, 8));

               nir_ssa_def *xyz[3] = {
                  nir_channel(&b_shader, xy, 0),
                  nir_channel(&b_shader, xy, 1),
                  z,
               };
               ret = nir_vec(&b_shader, xyz, 3);
               break;
            }
            case nir_intrinsic_load_ray_t_min: {
               ret = nir_load_var(&b_shader, vars->tmin);
               break;
            }
            case nir_intrinsic_load_ray_t_max: {
               ret = nir_load_var(&b_shader, vars->tmax);
               break;
            }
            case nir_intrinsic_load_ray_world_origin: {
               ret = nir_load_var(&b_shader, vars->origin);
               break;
            }
            case nir_intrinsic_load_ray_world_direction: {
               ret = nir_load_var(&b_shader, vars->direction);
               break;
            }
            case nir_intrinsic_load_ray_instance_custom_index: {
               ret = nir_load_var(&b_shader, vars->custom_instance_and_mask);
               ret = nir_iand_imm(&b_shader, ret, 0xFFFFFF);
               break;
            }
            case nir_intrinsic_load_primitive_id: {
               ret = nir_load_var(&b_shader, vars->primitive_id);
               break;
            }
            case nir_intrinsic_load_ray_geometry_index: {
               ret = nir_load_var(&b_shader, vars->geometry_id_and_flags);
               ret = nir_iand_imm(&b_shader, ret, 0xFFFFFFF);
               break;
            }
            case nir_intrinsic_load_instance_id: {
               ret = nir_load_var(&b_shader, vars->instance_id);
               break;
            }
            case nir_intrinsic_load_ray_flags: {
               ret = nir_load_var(&b_shader, vars->flags);
               break;
            }
            case nir_intrinsic_load_ray_hit_kind: {
               ret = nir_load_var(&b_shader, vars->hit_kind);
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

               ret = nir_vec(&b_shader, vals, 3);
               if (c == 3)
                  ret = nir_fneg(&b_shader,
                                 nir_build_vec3_mat_mult(&b_shader, ret, wto_matrix, false));
               break;
            }
            case nir_intrinsic_load_ray_object_to_world: {
               unsigned c = nir_intrinsic_column(intr);
               nir_ssa_def *instance_node_addr = nir_load_var(&b_shader, vars->instance_addr);
               if (c == 3) {
                  nir_ssa_def *wto_matrix[3];
                  nir_build_wto_matrix_load(&b_shader, instance_node_addr, wto_matrix);

                  nir_ssa_def *vals[3];
                  for (unsigned i = 0; i < 3; ++i)
                     vals[i] = nir_channel(&b_shader, wto_matrix[i], c);

                  ret = nir_vec(&b_shader, vals, 3);
               } else {
                  ret = nir_build_load_global(
                     &b_shader, 3, 32, nir_iadd_imm(&b_shader, instance_node_addr, 92 + c * 12));
               }
               break;
            }
            case nir_intrinsic_load_ray_object_origin: {
               nir_ssa_def *instance_node_addr = nir_load_var(&b_shader, vars->instance_addr);
               nir_ssa_def *wto_matrix[] = {
                  nir_build_load_global(&b_shader, 4, 32,
                                        nir_iadd_imm(&b_shader, instance_node_addr, 16),
                                        .align_mul = 64, .align_offset = 16),
                  nir_build_load_global(&b_shader, 4, 32,
                                        nir_iadd_imm(&b_shader, instance_node_addr, 32),
                                        .align_mul = 64, .align_offset = 32),
                  nir_build_load_global(&b_shader, 4, 32,
                                        nir_iadd_imm(&b_shader, instance_node_addr, 48),
                                        .align_mul = 64, .align_offset = 48)};
               ret = nir_build_vec3_mat_mult_pre(
                  &b_shader, nir_load_var(&b_shader, vars->origin), wto_matrix);
               break;
            }
            case nir_intrinsic_load_ray_object_direction: {
               nir_ssa_def *instance_node_addr = nir_load_var(&b_shader, vars->instance_addr);
               nir_ssa_def *wto_matrix[3];
               nir_build_wto_matrix_load(&b_shader, instance_node_addr, wto_matrix);
               ret = nir_build_vec3_mat_mult(
                  &b_shader, nir_load_var(&b_shader, vars->direction), wto_matrix, false);
               break;
            }
            case nir_intrinsic_load_intersection_opaque_amd: {
               ret = nir_load_var(&b_shader, vars->opaque);
               break;
            }
            case nir_intrinsic_load_cull_mask: {
               ret = nir_load_var(&b_shader, vars->cull_mask);
               break;
            }
            case nir_intrinsic_ignore_ray_intersection: {
               nir_store_var(&b_shader, vars->ahit_accept, nir_imm_false(&b_shader), 0x1);

               /* The if is a workaround to avoid having to fix up control flow manually */
               nir_push_if(&b_shader, nir_imm_true(&b_shader));
               nir_jump(&b_shader, nir_jump_return);
               nir_pop_if(&b_shader, NULL);
               break;
            }
            case nir_intrinsic_terminate_ray: {
               nir_store_var(&b_shader, vars->ahit_accept, nir_imm_true(&b_shader), 0x1);
               nir_store_var(&b_shader, vars->ahit_terminate, nir_imm_true(&b_shader), 0x1);

               /* The if is a workaround to avoid having to fix up control flow manually */
               nir_push_if(&b_shader, nir_imm_true(&b_shader));
               nir_jump(&b_shader, nir_jump_return);
               nir_pop_if(&b_shader, NULL);
               break;
            }
            case nir_intrinsic_report_ray_intersection: {
               nir_push_if(
                  &b_shader,
                  nir_iand(
                     &b_shader,
                     nir_fge(&b_shader, nir_load_var(&b_shader, vars->tmax), intr->src[0].ssa),
                     nir_fge(&b_shader, intr->src[0].ssa, nir_load_var(&b_shader, vars->tmin))));
               {
                  nir_store_var(&b_shader, vars->ahit_accept, nir_imm_true(&b_shader), 0x1);
                  nir_store_var(&b_shader, vars->tmax, intr->src[0].ssa, 1);
                  nir_store_var(&b_shader, vars->hit_kind, intr->src[1].ssa, 1);
               }
               nir_pop_if(&b_shader, NULL);
               break;
            }
            default:
               continue;
            }

            if (ret)
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
            nir_instr_remove(instr);
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
insert_rt_case(nir_builder *b, nir_shader *shader, struct rt_variables *vars, nir_ssa_def *idx,
               uint32_t call_idx_base, uint32_t call_idx)
{
   struct hash_table *var_remap = _mesa_pointer_hash_table_create(NULL);

   nir_opt_dead_cf(shader);

   struct rt_variables src_vars = create_rt_variables(shader, vars->create_info, vars->stack_sizes);
   map_rt_variables(var_remap, &src_vars, vars);

   NIR_PASS_V(shader, lower_rt_instructions, &src_vars, call_idx_base);

   NIR_PASS(_, shader, nir_opt_remove_phis);
   NIR_PASS(_, shader, nir_lower_returns);
   NIR_PASS(_, shader, nir_opt_dce);

   reserve_stack_size(vars, shader->scratch_size);

   nir_push_if(b, nir_ieq_imm(b, idx, call_idx));
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
         if (instr->type != nir_instr_type_deref)
            continue;

         nir_deref_instr *deref = nir_instr_as_deref(instr);
         b.cursor = nir_before_instr(&deref->instr);

         nir_deref_instr *replacement = NULL;
         if (nir_deref_mode_is(deref, nir_var_shader_call_data)) {
            deref->modes = nir_var_function_temp;
            progress = true;

            if (deref->deref_type == nir_deref_type_var)
               replacement =
                  nir_build_deref_cast(&b, arg_offset, nir_var_function_temp, deref->var->type, 0);
         } else if (nir_deref_mode_is(deref, nir_var_ray_hit_attrib)) {
            deref->modes = nir_var_function_temp;
            progress = true;

            if (deref->deref_type == nir_deref_type_var)
               replacement = nir_build_deref_cast(&b, nir_imm_int(&b, RADV_HIT_ATTRIB_OFFSET),
                                                  nir_var_function_temp, deref->type, 0);
         }

         if (replacement != NULL) {
            nir_ssa_def_rewrite_uses(&deref->dest.ssa, &replacement->dest.ssa);
            nir_instr_remove(&deref->instr);
         }
      }
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);
   else
      nir_metadata_preserve(impl, nir_metadata_all);

   return progress;
}

static nir_shader *
parse_rt_stage(struct radv_device *device, const VkPipelineShaderStageCreateInfo *sinfo)
{
   struct radv_pipeline_key key;
   memset(&key, 0, sizeof(key));

   struct radv_pipeline_stage rt_stage;

   radv_pipeline_stage_init(sinfo, &rt_stage, vk_to_mesa_shader_stage(sinfo->stage));

   nir_shader *shader = radv_shader_spirv_to_nir(device, &rt_stage, &key);

   if (shader->info.stage == MESA_SHADER_RAYGEN || shader->info.stage == MESA_SHADER_CLOSEST_HIT ||
       shader->info.stage == MESA_SHADER_CALLABLE || shader->info.stage == MESA_SHADER_MISS) {
      nir_block *last_block = nir_impl_last_block(nir_shader_get_entrypoint(shader));
      nir_builder b_inner;
      nir_builder_init(&b_inner, nir_shader_get_entrypoint(shader));
      b_inner.cursor = nir_after_block(last_block);
      nir_rt_return_amd(&b_inner);
   }

   NIR_PASS(_, shader, nir_lower_vars_to_explicit_types,
            nir_var_function_temp | nir_var_shader_call_data | nir_var_ray_hit_attrib,
            glsl_get_natural_size_align_bytes);

   NIR_PASS(_, shader, lower_rt_derefs);

   NIR_PASS(_, shader, nir_lower_explicit_io, nir_var_function_temp,
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
               nir_jump(b, nir_jump_return);
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
      NIR_PASS(_, any_hit, nir_opt_dce);
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
   NIR_PASS(_, intersection, nir_opt_deref);

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
   nir_variable *hit;
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
   ret.hit = nir_variable_create(b->shader, nir_var_shader_temp, glsl_bool_type(), "traversal_hit");
   ret.bvh_base = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint64_t_type(),
                                      "traversal_bvh_base");
   ret.stack =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "traversal_stack_ptr");
   ret.top_stack = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(),
                                       "traversal_top_stack_ptr");
   return ret;
}

static void
visit_any_hit_shaders(struct radv_device *device,
                      const VkRayTracingPipelineCreateInfoKHR *pCreateInfo, nir_builder *b,
                      struct rt_variables *vars)
{
   nir_ssa_def *sbt_idx = nir_load_var(b, vars->idx);

   nir_push_if(b, nir_ine_imm(b, sbt_idx, 0));
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
      nir_shader *nir_stage = parse_rt_stage(device, stage);

      vars->stage_idx = shader_id;
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
   nir_ssa_def *dist = nir_channel(b, result, 0);
   nir_ssa_def *div = nir_channel(b, result, 1);
   dist = nir_fdiv(b, dist, div);
   nir_ssa_def *frontface = nir_flt(b, nir_imm_float(b, 0), div);
   nir_ssa_def *switch_ccw =
      nir_test_mask(b, nir_load_var(b, trav_vars->sbt_offset_and_flags),
                    VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR << 24);
   frontface = nir_ixor(b, frontface, switch_ccw);

   nir_ssa_def *not_cull =
      nir_inot(b, nir_test_mask(b, nir_load_var(b, vars->flags), SpvRayFlagsSkipTrianglesKHRMask));
   nir_ssa_def *not_facing_cull = nir_ieq_imm(
      b,
      nir_iand(b, nir_load_var(b, vars->flags),
               nir_bcsel(b, frontface, nir_imm_int(b, SpvRayFlagsCullFrontFacingTrianglesKHRMask),
                         nir_imm_int(b, SpvRayFlagsCullBackFacingTrianglesKHRMask))),
      0);

   not_cull = nir_iand(
      b, not_cull,
      nir_ior(b, not_facing_cull,
              nir_test_mask(b, nir_load_var(b, trav_vars->sbt_offset_and_flags),
                            VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR << 24)));

   nir_push_if(b, nir_iand(b,
                           nir_iand(b, nir_flt(b, dist, nir_load_var(b, vars->tmax)),
                                    nir_flt(b, nir_load_var(b, vars->tmin), dist)),
                           not_cull));
   {

      nir_ssa_def *triangle_info =
         nir_build_load_global(b, 2, 32,
                               nir_iadd_imm(b, build_node_to_addr(device, b, bvh_node),
                                            offsetof(struct radv_bvh_triangle_node, triangle_id)));
      nir_ssa_def *primitive_id = nir_channel(b, triangle_info, 0);
      nir_ssa_def *geometry_id_and_flags = nir_channel(b, triangle_info, 1);
      nir_ssa_def *geometry_id = nir_iand_imm(b, geometry_id_and_flags, 0xfffffff);
      nir_ssa_def *is_opaque = hit_is_opaque(b, nir_load_var(b, trav_vars->sbt_offset_and_flags),
                                             nir_load_var(b, vars->flags), geometry_id_and_flags);

      not_cull =
         nir_ieq_imm(b,
                     nir_iand(b, nir_load_var(b, vars->flags),
                              nir_bcsel(b, is_opaque, nir_imm_int(b, SpvRayFlagsCullOpaqueKHRMask),
                                        nir_imm_int(b, SpvRayFlagsCullNoOpaqueKHRMask))),
                     0);
      nir_push_if(b, not_cull);
      {
         nir_ssa_def *sbt_idx = nir_iadd(
            b,
            nir_iadd(b, nir_load_var(b, vars->sbt_offset),
                     nir_iand_imm(b, nir_load_var(b, trav_vars->sbt_offset_and_flags), 0xffffff)),
            nir_imul(b, nir_load_var(b, vars->sbt_stride), geometry_id));
         nir_ssa_def *divs[2] = {div, div};
         nir_ssa_def *ij = nir_fdiv(b, nir_channels(b, result, 0xc), nir_vec(b, divs, 2));
         nir_ssa_def *hit_kind =
            nir_bcsel(b, frontface, nir_imm_int(b, 0xFE), nir_imm_int(b, 0xFF));

         nir_store_scratch(
            b, ij, nir_iadd_imm(b, nir_load_var(b, vars->stack_ptr), RADV_HIT_ATTRIB_OFFSET),
            .align_mul = 16);

         nir_store_var(b, vars->ahit_accept, nir_imm_true(b), 0x1);
         nir_store_var(b, vars->ahit_terminate, nir_imm_false(b), 0x1);

         nir_push_if(b, nir_inot(b, is_opaque));
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

            nir_push_if(b, nir_inot(b, nir_load_var(b, vars->ahit_accept)));
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

         nir_store_var(b, vars->idx, sbt_idx, 1);
         nir_store_var(b, trav_vars->hit, nir_imm_true(b), 1);

         nir_ssa_def *terminate_on_first_hit =
            nir_test_mask(b, nir_load_var(b, vars->flags), SpvRayFlagsTerminateOnFirstHitKHRMask);
         nir_ssa_def *ray_terminated = nir_load_var(b, vars->ahit_terminate);
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
   nir_ssa_def *node_addr = build_node_to_addr(device, b, bvh_node);
   nir_ssa_def *triangle_info = nir_build_load_global(b, 2, 32, nir_iadd_imm(b, node_addr, 24));
   nir_ssa_def *primitive_id = nir_channel(b, triangle_info, 0);
   nir_ssa_def *geometry_id_and_flags = nir_channel(b, triangle_info, 1);
   nir_ssa_def *geometry_id = nir_iand_imm(b, geometry_id_and_flags, 0xfffffff);
   nir_ssa_def *is_opaque = hit_is_opaque(b, nir_load_var(b, trav_vars->sbt_offset_and_flags),
                                          nir_load_var(b, vars->flags), geometry_id_and_flags);

   nir_ssa_def *not_skip_aabb =
      nir_inot(b, nir_test_mask(b, nir_load_var(b, vars->flags), SpvRayFlagsSkipAABBsKHRMask));
   nir_ssa_def *not_cull = nir_iand(
      b, not_skip_aabb,
      nir_ieq_imm(b,
                  nir_iand(b, nir_load_var(b, vars->flags),
                           nir_bcsel(b, is_opaque, nir_imm_int(b, SpvRayFlagsCullOpaqueKHRMask),
                                     nir_imm_int(b, SpvRayFlagsCullNoOpaqueKHRMask))),
                  0));
   nir_push_if(b, not_cull);
   {
      nir_ssa_def *sbt_idx = nir_iadd(
         b,
         nir_iadd(b, nir_load_var(b, vars->sbt_offset),
                  nir_iand_imm(b, nir_load_var(b, trav_vars->sbt_offset_and_flags), 0xffffff)),
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

      nir_store_var(b, vars->ahit_accept, nir_imm_false(b), 0x1);
      nir_store_var(b, vars->ahit_terminate, nir_imm_false(b), 0x1);

      nir_push_if(b, nir_ine_imm(b, nir_load_var(b, inner_vars.idx), 0));
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
         nir_shader *nir_stage = parse_rt_stage(device, stage);

         nir_shader *any_hit_stage = NULL;
         if (any_hit_shader_id != VK_SHADER_UNUSED_KHR) {
            stage = &pCreateInfo->pStages[any_hit_shader_id];
            any_hit_stage = parse_rt_stage(device, stage);

            nir_lower_intersection_shader(nir_stage, any_hit_stage);
            ralloc_free(any_hit_stage);
         }

         inner_vars.stage_idx = shader_id;
         insert_rt_case(b, nir_stage, &inner_vars, nir_load_var(b, inner_vars.idx), 0, i + 2);
      }
      nir_push_else(b, NULL);
      {
         nir_ssa_def *vec3_zero = nir_channels(b, nir_imm_vec4(b, 0, 0, 0, 0), 0x7);
         nir_ssa_def *vec3_inf =
            nir_channels(b, nir_imm_vec4(b, INFINITY, INFINITY, INFINITY, 0), 0x7);

         nir_ssa_def *bvh_lo = nir_build_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, 0));
         nir_ssa_def *bvh_hi = nir_build_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, 12));

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

         nir_push_if(b, nir_iand(b, nir_fge(b, nir_load_var(b, vars->tmax), t_min),
                                 nir_fge(b, t_max, nir_load_var(b, vars->tmin))));
         {
            nir_store_var(b, vars->ahit_accept, nir_imm_true(b), 0x1);
            nir_store_var(b, vars->tmax, nir_fmax(b, t_min, nir_load_var(b, vars->tmin)), 1);
         }
         nir_pop_if(b, NULL);
      }
      nir_pop_if(b, NULL);

      nir_push_if(b, nir_load_var(b, vars->ahit_accept));
      {
         nir_store_var(b, vars->primitive_id, primitive_id, 1);
         nir_store_var(b, vars->geometry_id_and_flags, geometry_id_and_flags, 1);
         nir_store_var(b, vars->tmax, nir_load_var(b, inner_vars.tmax), 0x1);
         nir_store_var(b, vars->instance_id, nir_load_var(b, trav_vars->instance_id), 0x1);
         nir_store_var(b, vars->instance_addr, nir_load_var(b, trav_vars->instance_addr), 0x1);
         nir_store_var(b, vars->custom_instance_and_mask,
                       nir_load_var(b, trav_vars->custom_instance_and_mask), 0x1);

         nir_store_var(b, vars->idx, sbt_idx, 1);
         nir_store_var(b, trav_vars->hit, nir_imm_true(b), 1);

         nir_ssa_def *terminate_on_first_hit =
            nir_test_mask(b, nir_load_var(b, vars->flags), SpvRayFlagsTerminateOnFirstHitKHRMask);
         nir_ssa_def *ray_terminated = nir_load_var(b, vars->ahit_terminate);
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

static nir_shader *
build_traversal_shader(struct radv_device *device,
                       const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                       const struct rt_variables *dst_vars,
                       struct hash_table *var_remap)
{
   nir_builder b = radv_meta_init_shader(device, MESA_SHADER_COMPUTE, "rt_traversal");
   b.shader->info.internal = false;
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = device->physical_device->rt_wave_size == 64 ? 8 : 4;
   struct rt_variables vars = create_rt_variables(b.shader, pCreateInfo, dst_vars->stack_sizes);
   map_rt_variables(var_remap, &vars, dst_vars);

   unsigned lanes = device->physical_device->rt_wave_size;
   unsigned elements = lanes * MAX_STACK_ENTRY_COUNT;
   nir_variable *stack_var = nir_variable_create(b.shader, nir_var_mem_shared,
                                                 glsl_array_type(glsl_uint_type(), elements, 0),
                                                 "trav_stack");
   nir_deref_instr *stack_deref = nir_build_deref_var(&b, stack_var);
   nir_deref_instr *stack;
   nir_ssa_def *stack_idx_stride = nir_imm_int(&b, lanes);
   nir_ssa_def *stack_idx_base = nir_load_local_invocation_index(&b);

   nir_ssa_def *accel_struct = nir_load_var(&b, vars.accel_struct);

   struct rt_traversal_vars trav_vars = init_traversal_vars(&b);

   nir_store_var(&b, trav_vars.hit, nir_imm_false(&b), 1);

   nir_push_if(&b, nir_ine_imm(&b, accel_struct, 0));
   {
      nir_store_var(&b, trav_vars.bvh_base, build_addr_to_node(&b, accel_struct), 1);

      nir_ssa_def *bvh_root = nir_build_load_global(
         &b, 1, 32, accel_struct, .access = ACCESS_NON_WRITEABLE, .align_mul = 64);

      nir_ssa_def *desc = create_bvh_descriptor(&b);
      nir_ssa_def *vec3ones = nir_channels(&b, nir_imm_vec4(&b, 1.0, 1.0, 1.0, 1.0), 0x7);

      nir_store_var(&b, trav_vars.origin, nir_load_var(&b, vars.origin), 7);
      nir_store_var(&b, trav_vars.dir, nir_load_var(&b, vars.direction), 7);
      nir_store_var(&b, trav_vars.inv_dir, nir_fdiv(&b, vec3ones, nir_load_var(&b, trav_vars.dir)), 7);
      nir_store_var(&b, trav_vars.sbt_offset_and_flags, nir_imm_int(&b, 0), 1);
      nir_store_var(&b, trav_vars.instance_addr, nir_imm_int64(&b, 0), 1);

      nir_store_var(&b, trav_vars.stack, nir_iadd(&b, stack_idx_base, stack_idx_stride), 1);
      stack = nir_build_deref_array(&b, stack_deref, stack_idx_base);
      nir_store_deref(&b, stack, bvh_root, 0x1);

      nir_store_var(&b, trav_vars.top_stack, nir_imm_int(&b, 0), 1);

      nir_push_loop(&b);

      nir_push_if(&b, nir_ieq(&b, nir_load_var(&b, trav_vars.stack), stack_idx_base));
      nir_jump(&b, nir_jump_break);
      nir_pop_if(&b, NULL);

      nir_push_if(
         &b, nir_uge(&b, nir_load_var(&b, trav_vars.top_stack), nir_load_var(&b, trav_vars.stack)));
      nir_store_var(&b, trav_vars.top_stack, nir_imm_int(&b, 0), 1);
      nir_store_var(&b, trav_vars.bvh_base,
                    build_addr_to_node(&b, nir_load_var(&b, vars.accel_struct)), 1);
      nir_store_var(&b, trav_vars.origin, nir_load_var(&b, vars.origin), 7);
      nir_store_var(&b, trav_vars.dir, nir_load_var(&b, vars.direction), 7);
      nir_store_var(&b, trav_vars.inv_dir, nir_fdiv(&b, vec3ones, nir_load_var(&b, trav_vars.dir)), 7);
      nir_store_var(&b, trav_vars.instance_addr, nir_imm_int64(&b, 0), 1);

      nir_pop_if(&b, NULL);

      nir_store_var(&b, trav_vars.stack,
                    nir_isub(&b, nir_load_var(&b, trav_vars.stack), stack_idx_stride), 1);

      stack = nir_build_deref_array(&b, stack_deref, nir_load_var(&b, trav_vars.stack));
      nir_ssa_def *bvh_node = nir_load_deref(&b, stack);
      nir_ssa_def *bvh_node_type = nir_iand_imm(&b, bvh_node, 7);

      bvh_node = nir_iadd(&b, nir_load_var(&b, trav_vars.bvh_base), nir_u2u(&b, bvh_node, 64));
      nir_ssa_def *intrinsic_result = NULL;
      if (!radv_emulate_rt(device->physical_device)) {
         intrinsic_result = nir_bvh64_intersect_ray_amd(
            &b, 32, desc, nir_unpack_64_2x32(&b, bvh_node), nir_load_var(&b, vars.tmax),
            nir_load_var(&b, trav_vars.origin), nir_load_var(&b, trav_vars.dir),
            nir_load_var(&b, trav_vars.inv_dir));
      }

      nir_push_if(&b, nir_ine_imm(&b, nir_iand_imm(&b, bvh_node_type, 4), 0));
      {
         nir_push_if(&b, nir_ine_imm(&b, nir_iand_imm(&b, bvh_node_type, 2), 0));
         {
            /* custom */
            nir_push_if(&b, nir_ine_imm(&b, nir_iand_imm(&b, bvh_node_type, 1), 0));
            if (!(pCreateInfo->flags & VK_PIPELINE_CREATE_RAY_TRACING_SKIP_AABBS_BIT_KHR)) {
               insert_traversal_aabb_case(device, pCreateInfo, &b, &vars, &trav_vars, bvh_node);
            }
            nir_push_else(&b, NULL);
            {
               /* instance */
               nir_ssa_def *instance_node_addr = build_node_to_addr(device, &b, bvh_node);
               nir_ssa_def *instance_data =
                  nir_build_load_global(&b, 4, 32, instance_node_addr, .align_mul = 64);
               nir_ssa_def *wto_matrix[] = {
                  nir_build_load_global(&b, 4, 32, nir_iadd_imm(&b, instance_node_addr, 16),
                                        .align_mul = 64, .align_offset = 16),
                  nir_build_load_global(&b, 4, 32, nir_iadd_imm(&b, instance_node_addr, 32),
                                        .align_mul = 64, .align_offset = 32),
                  nir_build_load_global(&b, 4, 32, nir_iadd_imm(&b, instance_node_addr, 48),
                                        .align_mul = 64, .align_offset = 48)};
               nir_ssa_def *instance_id =
                  nir_build_load_global(&b, 1, 32, nir_iadd_imm(&b, instance_node_addr, 88));
               nir_ssa_def *instance_and_mask = nir_channel(&b, instance_data, 2);
               nir_ssa_def *instance_mask = nir_ushr_imm(&b, instance_and_mask, 24);

               nir_push_if(
                  &b,
                  nir_ieq_imm(&b, nir_iand(&b, instance_mask, nir_load_var(&b, vars.cull_mask)), 0));
               nir_jump(&b, nir_jump_continue);
               nir_pop_if(&b, NULL);

               nir_store_var(&b, trav_vars.top_stack, nir_load_var(&b, trav_vars.stack), 1);
               nir_store_var(&b, trav_vars.bvh_base,
                             build_addr_to_node(
                                &b, nir_pack_64_2x32(&b, nir_channels(&b, instance_data, 0x3))),
                             1);
               stack = nir_build_deref_array(&b, stack_deref, nir_load_var(&b, trav_vars.stack));
               nir_store_deref(&b, stack, nir_iand_imm(&b, nir_channel(&b, instance_data, 0), 63), 0x1);

               nir_store_var(&b, trav_vars.stack,
                             nir_iadd(&b, nir_load_var(&b, trav_vars.stack), stack_idx_stride), 1);

               nir_store_var(
                  &b, trav_vars.origin,
                  nir_build_vec3_mat_mult_pre(&b, nir_load_var(&b, vars.origin), wto_matrix), 7);
               nir_store_var(
                  &b, trav_vars.dir,
                  nir_build_vec3_mat_mult(&b, nir_load_var(&b, vars.direction), wto_matrix, false),
                  7);
               nir_store_var(&b, trav_vars.inv_dir,
                             nir_fdiv(&b, vec3ones, nir_load_var(&b, trav_vars.dir)), 7);
               nir_store_var(&b, trav_vars.custom_instance_and_mask, instance_and_mask, 1);
               nir_store_var(&b, trav_vars.sbt_offset_and_flags, nir_channel(&b, instance_data, 3),
                             1);
               nir_store_var(&b, trav_vars.instance_id, instance_id, 1);
               nir_store_var(&b, trav_vars.instance_addr, instance_node_addr, 1);
            }
            nir_pop_if(&b, NULL);
         }
         nir_push_else(&b, NULL);
         {
            /* box */
            nir_ssa_def *result = intrinsic_result;
            if (!result) {
               /* If we didn't run the intrinsic cause the hardware didn't support it,
                * emulate ray/box intersection here */
               result = intersect_ray_amd_software_box(device,
                  &b, bvh_node, nir_load_var(&b, vars.tmax), nir_load_var(&b, trav_vars.origin),
                  nir_load_var(&b, trav_vars.dir), nir_load_var(&b, trav_vars.inv_dir));
            }

            for (unsigned i = 4; i-- > 0; ) {
               nir_ssa_def *new_node = nir_channel(&b, result, i);
               nir_push_if(&b, nir_ine_imm(&b, new_node, 0xffffffff));
               {
                  stack = nir_build_deref_array(&b, stack_deref, nir_load_var(&b, trav_vars.stack));
                  nir_store_deref(&b, stack, new_node, 0x1);
                  nir_store_var(
                     &b, trav_vars.stack,
                     nir_iadd(&b, nir_load_var(&b, trav_vars.stack), stack_idx_stride), 1);
               }
               nir_pop_if(&b, NULL);
            }
         }
         nir_pop_if(&b, NULL);
      }
      nir_push_else(&b, NULL);
      if (!(pCreateInfo->flags & VK_PIPELINE_CREATE_RAY_TRACING_SKIP_TRIANGLES_BIT_KHR)) {
         nir_ssa_def *result = intrinsic_result;
         if (!result) {
            /* If we didn't run the intrinsic cause the hardware didn't support it,
             * emulate ray/tri intersection here */
            result = intersect_ray_amd_software_tri(device,
               &b, bvh_node, nir_load_var(&b, vars.tmax), nir_load_var(&b, trav_vars.origin),
               nir_load_var(&b, trav_vars.dir), nir_load_var(&b, trav_vars.inv_dir));
         }
         insert_traversal_triangle_case(device, pCreateInfo, &b, result, &vars, &trav_vars, bvh_node);
      }
      nir_pop_if(&b, NULL);

      nir_pop_loop(&b, NULL);
   }
   nir_pop_if(&b, NULL);

   /* Initialize follow-up shader. */
   nir_push_if(&b, nir_load_var(&b, trav_vars.hit));
   {
      /* vars.idx contains the SBT index at this point. */
      load_sbt_entry(&b, &vars, nir_load_var(&b, vars.idx), SBT_HIT, 0);

      nir_ssa_def *should_return = nir_ior(&b,
                                           nir_test_mask(&b, nir_load_var(&b, vars.flags),
                                                         SpvRayFlagsSkipClosestHitShaderKHRMask),
                                           nir_ieq_imm(&b, nir_load_var(&b, vars.idx), 0));

      /* should_return is set if we had a hit but we won't be calling the closest hit shader and hence
       * need to return immediately to the calling shader. */
      nir_push_if(&b, should_return);
      {
         insert_rt_return(&b, &vars);
      }
      nir_pop_if(&b, NULL);
   }
   nir_push_else(&b, NULL);
   {
      /* Only load the miss shader if we actually miss. It is valid to not specify an SBT pointer
       * for miss shaders if none of the rays miss. */
      load_sbt_entry(&b, &vars, nir_load_var(&b, vars.miss_index), SBT_MISS, 0);
   }
   nir_pop_if(&b, NULL);

   return b.shader;
}


static void
insert_traversal(struct radv_device *device, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                 nir_builder *b, const struct rt_variables *vars)
{
   struct hash_table *var_remap = _mesa_pointer_hash_table_create(NULL);
   nir_shader *shader = build_traversal_shader(device, pCreateInfo, vars, var_remap);

   /* For now, just inline the traversal shader */
   nir_push_if(b, nir_ieq_imm(b, nir_load_var(b, vars->idx), 1));
   nir_store_var(b, vars->main_loop_case_visited, nir_imm_bool(b, true), 1);
   nir_inline_function_impl(b, nir_shader_get_entrypoint(shader), NULL, var_remap);
   nir_pop_if(b, NULL);

   /* Adopt the instructions from the source shader, since they are merely moved, not cloned. */
   ralloc_adopt(ralloc_context(b->shader), ralloc_context(shader));

   ralloc_free(var_remap);
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

static bool
should_move_rt_instruction(nir_intrinsic_op intrinsic)
{
   switch (intrinsic) {
   case nir_intrinsic_load_rt_arg_scratch_offset_amd:
   case nir_intrinsic_load_ray_flags:
   case nir_intrinsic_load_ray_object_origin:
   case nir_intrinsic_load_ray_world_origin:
   case nir_intrinsic_load_ray_t_min:
   case nir_intrinsic_load_ray_object_direction:
   case nir_intrinsic_load_ray_world_direction:
   case nir_intrinsic_load_ray_t_max:
      return true;
   default:
      return false;
   }
}

static void
move_rt_instructions(nir_shader *shader)
{
   nir_cursor target = nir_before_cf_list(&nir_shader_get_entrypoint(shader)->body);

   nir_foreach_block (block, nir_shader_get_entrypoint(shader)) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);

         if (!should_move_rt_instruction(intrinsic->intrinsic))
            continue;

         nir_instr_move(target, instr);
      }
   }

   nir_metadata_preserve(nir_shader_get_entrypoint(shader),
                         nir_metadata_all & (~nir_metadata_instr_index));
}

static nir_shader *
create_rt_shader(struct radv_device *device, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                 struct radv_pipeline_shader_stack_size *stack_sizes)
{
   struct radv_pipeline_key key;
   memset(&key, 0, sizeof(key));

   nir_builder b = radv_meta_init_shader(device, MESA_SHADER_COMPUTE, "rt_combined");
   b.shader->info.internal = false;
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = device->physical_device->rt_wave_size == 64 ? 8 : 4;

   struct rt_variables vars = create_rt_variables(b.shader, pCreateInfo, stack_sizes);
   load_sbt_entry(&b, &vars, nir_imm_int(&b, 0), SBT_RAYGEN, 0);
   nir_store_var(&b, vars.stack_ptr, nir_imm_int(&b, 0), 0x1);

   nir_store_var(&b, vars.main_loop_case_visited, nir_imm_bool(&b, true), 1);

   nir_loop *loop = nir_push_loop(&b);

   nir_push_if(&b, nir_ior(&b, nir_ieq_imm(&b, nir_load_var(&b, vars.idx), 0),
                           nir_inot(&b, nir_load_var(&b, vars.main_loop_case_visited))));
   nir_jump(&b, nir_jump_break);
   nir_pop_if(&b, NULL);

   nir_store_var(&b, vars.main_loop_case_visited, nir_imm_bool(&b, false), 1);

   insert_traversal(device, pCreateInfo, &b, &vars);

   nir_ssa_def *idx = nir_load_var(&b, vars.idx);

   /* We do a trick with the indexing of the resume shaders so that the first
    * shader of stage x always gets id x and the resume shader ids then come after
    * stageCount. This makes the shadergroup handles independent of compilation. */
   unsigned call_idx_base = pCreateInfo->stageCount + 1;
   for (unsigned i = 0; i < pCreateInfo->stageCount; ++i) {
      const VkPipelineShaderStageCreateInfo *stage = &pCreateInfo->pStages[i];
      gl_shader_stage type = vk_to_mesa_shader_stage(stage->stage);
      if (type != MESA_SHADER_RAYGEN && type != MESA_SHADER_CALLABLE &&
          type != MESA_SHADER_CLOSEST_HIT && type != MESA_SHADER_MISS)
         continue;

      nir_shader *nir_stage = parse_rt_stage(device, stage);

      /* Move ray tracing system values to the top that are set by rt_trace_ray
       * to prevent them from being overwritten by other rt_trace_ray calls.
       */
      NIR_PASS_V(nir_stage, move_rt_instructions);

      uint32_t num_resume_shaders = 0;
      nir_shader **resume_shaders = NULL;
      nir_lower_shader_calls(nir_stage, nir_address_format_32bit_offset, 16, &resume_shaders,
                             &num_resume_shaders, nir_stage);

      vars.stage_idx = i;
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
   struct radv_compute_pipeline *compute_pipeline = NULL;
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

   VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup_size = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO,
      .pNext = NULL,
      .requiredSubgroupSize = device->physical_device->rt_wave_size,
   };

   VkComputePipelineCreateInfo compute_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .pNext = NULL,
      .flags = pCreateInfo->flags | VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT,
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

   if (result == VK_PIPELINE_COMPILE_REQUIRED) {
      if (pCreateInfo->flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
         goto fail;

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
   compute_pipeline = radv_pipeline_to_compute(pipeline);

   compute_pipeline->rt_group_handles =
      calloc(sizeof(*compute_pipeline->rt_group_handles), local_create_info.groupCount);
   if (!compute_pipeline->rt_group_handles) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto shader_fail;
   }

   compute_pipeline->dynamic_stack_size = radv_rt_pipeline_has_dynamic_stack_size(pCreateInfo);

   /* For General and ClosestHit shaders, we can use the shader ID directly as handle.
    * As (potentially different) AnyHit shaders are inlined, for Intersection shaders
    * we use the Group ID.
    */
   for (unsigned i = 0; i < local_create_info.groupCount; ++i) {
      const VkRayTracingShaderGroupCreateInfoKHR *group_info = &local_create_info.pGroups[i];
      switch (group_info->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR:
         if (group_info->generalShader != VK_SHADER_UNUSED_KHR)
            compute_pipeline->rt_group_handles[i].handles[0] = group_info->generalShader + 2;
         break;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR:
         if (group_info->intersectionShader != VK_SHADER_UNUSED_KHR)
            compute_pipeline->rt_group_handles[i].handles[1] = i + 2;
         FALLTHROUGH;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR:
         if (group_info->closestHitShader != VK_SHADER_UNUSED_KHR)
            compute_pipeline->rt_group_handles[i].handles[0] = group_info->closestHitShader + 2;
         if (group_info->anyHitShader != VK_SHADER_UNUSED_KHR)
            compute_pipeline->rt_group_handles[i].handles[1] = i + 2;
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

         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
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
   struct radv_compute_pipeline *compute_pipeline = radv_pipeline_to_compute(pipeline);
   char *data = pData;

   STATIC_ASSERT(sizeof(*compute_pipeline->rt_group_handles) <= RADV_RT_HANDLE_SIZE);

   memset(data, 0, groupCount * RADV_RT_HANDLE_SIZE);

   for (uint32_t i = 0; i < groupCount; ++i) {
      memcpy(data + i * RADV_RT_HANDLE_SIZE, &compute_pipeline->rt_group_handles[firstGroup + i],
             sizeof(*compute_pipeline->rt_group_handles));
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkDeviceSize VKAPI_CALL
radv_GetRayTracingShaderGroupStackSizeKHR(VkDevice device, VkPipeline _pipeline, uint32_t group,
                                          VkShaderGroupShaderKHR groupShader)
{
   RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);
   struct radv_compute_pipeline *compute_pipeline = radv_pipeline_to_compute(pipeline);
   const struct radv_pipeline_shader_stack_size *stack_size =
      &compute_pipeline->rt_stack_sizes[group];

   if (groupShader == VK_SHADER_GROUP_SHADER_ANY_HIT_KHR ||
       groupShader == VK_SHADER_GROUP_SHADER_INTERSECTION_KHR)
      return stack_size->non_recursive_size;
   else
      return stack_size->recursive_size;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetRayTracingCaptureReplayShaderGroupHandlesKHR(VkDevice _device, VkPipeline pipeline,
                                                     uint32_t firstGroup, uint32_t groupCount,
                                                     size_t dataSize, void *pData)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   unreachable("Unimplemented");
   return vk_error(device, VK_ERROR_FEATURE_NOT_PRESENT);
}
