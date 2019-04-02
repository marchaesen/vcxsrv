/*
 * Copyright © 2019 Google LLC
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tu_private.h"

#include "spirv/nir_spirv.h"
#include "util/mesa-sha1.h"

#include "ir3/ir3_nir.h"

static nir_function *
tu_spirv_to_nir(struct ir3_compiler *compiler,
                const uint32_t *words,
                size_t word_count,
                gl_shader_stage stage,
                const char *entry_point_name,
                const VkSpecializationInfo *spec_info)
{
   /* TODO these are made-up */
   const struct spirv_to_nir_options spirv_options = {
      .lower_workgroup_access_to_offsets = true,
      .lower_ubo_ssbo_access_to_offsets = true,
      .caps = { false },
   };
   const nir_shader_compiler_options *nir_options =
      ir3_get_compiler_options(compiler);

   /* convert VkSpecializationInfo */
   struct nir_spirv_specialization *spec = NULL;
   uint32_t num_spec = 0;
   if (spec_info && spec_info->mapEntryCount) {
      spec = malloc(sizeof(*spec) * spec_info->mapEntryCount);
      if (!spec)
         return NULL;

      for (uint32_t i = 0; i < spec_info->mapEntryCount; i++) {
         const VkSpecializationMapEntry *entry = &spec_info->pMapEntries[i];
         const void *data = spec_info->pData + entry->offset;
         assert(data + entry->size <= spec_info->pData + spec_info->dataSize);
         spec[i].id = entry->constantID;
         if (entry->size == 8)
            spec[i].data64 = *(const uint64_t *) data;
         else
            spec[i].data32 = *(const uint32_t *) data;
         spec[i].defined_on_module = false;
      }

      num_spec = spec_info->mapEntryCount;
   }

   nir_function *entry_point =
      spirv_to_nir(words, word_count, spec, num_spec, stage, entry_point_name,
                   &spirv_options, nir_options);

   free(spec);

   assert(entry_point->shader->info.stage == stage);
   nir_validate_shader(entry_point->shader, "after spirv_to_nir");

   return entry_point;
}

static void
tu_sort_variables_by_location(struct exec_list *variables)
{
   struct exec_list sorted;
   exec_list_make_empty(&sorted);

   nir_foreach_variable_safe(var, variables)
   {
      exec_node_remove(&var->node);

      /* insert the variable into the sorted list */
      nir_variable *next = NULL;
      nir_foreach_variable(tmp, &sorted)
      {
         if (var->data.location < tmp->data.location) {
            next = tmp;
            break;
         }
      }
      if (next)
         exec_node_insert_node_before(&next->node, &var->node);
      else
         exec_list_push_tail(&sorted, &var->node);
   }

   exec_list_move_nodes_to(&sorted, variables);
}

struct tu_shader *
tu_shader_create(struct tu_device *dev,
                 gl_shader_stage stage,
                 const VkPipelineShaderStageCreateInfo *stage_info,
                 const VkAllocationCallbacks *alloc)
{
   const struct tu_shader_module *module =
      tu_shader_module_from_handle(stage_info->module);
   struct tu_shader *shader;

   const uint32_t max_variant_count = (stage == MESA_SHADER_VERTEX) ? 2 : 1;
   shader = vk_zalloc2(
      &dev->alloc, alloc,
      sizeof(*shader) + sizeof(struct ir3_shader_variant) * max_variant_count,
      8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!shader)
      return NULL;

   /* translate SPIR-V to NIR */
   assert(module->code_size % 4 == 0);
   nir_function *entry_point = tu_spirv_to_nir(
      dev->compiler, (const uint32_t *) module->code, module->code_size / 4,
      stage, stage_info->pName, stage_info->pSpecializationInfo);
   if (!entry_point) {
      vk_free2(&dev->alloc, alloc, shader);
      return NULL;
   }

   nir_shader *nir = entry_point->shader;

   if (unlikely(dev->physical_device->instance->debug_flags & TU_DEBUG_NIR)) {
      fprintf(stderr, "translated nir:\n");
      nir_print_shader(nir, stderr);
   }

   /* TODO what needs to happen? */

   switch (stage) {
   case MESA_SHADER_VERTEX:
      tu_sort_variables_by_location(&nir->outputs);
      break;
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_TESS_EVAL:
   case MESA_SHADER_GEOMETRY:
      tu_sort_variables_by_location(&nir->inputs);
      tu_sort_variables_by_location(&nir->outputs);
      break;
   case MESA_SHADER_FRAGMENT:
      tu_sort_variables_by_location(&nir->inputs);
      break;
   case MESA_SHADER_COMPUTE:
      break;
   default:
      unreachable("invalid gl_shader_stage");
      break;
   }

   nir_assign_var_locations(&nir->inputs, &nir->num_inputs,
                            ir3_glsl_type_size);
   nir_assign_var_locations(&nir->outputs, &nir->num_outputs,
                            ir3_glsl_type_size);
   nir_assign_var_locations(&nir->uniforms, &nir->num_uniforms,
                            ir3_glsl_type_size);

   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_frexp);
   NIR_PASS_V(nir, nir_lower_io, nir_var_all, ir3_glsl_type_size, 0);

   nir_shader_gather_info(nir, entry_point->impl);

   shader->ir3_shader.compiler = dev->compiler;
   shader->ir3_shader.type = stage;
   shader->ir3_shader.nir = nir;

   return shader;
}

void
tu_shader_destroy(struct tu_device *dev,
                  struct tu_shader *shader,
                  const VkAllocationCallbacks *alloc)
{
   if (shader->ir3_shader.nir)
      ralloc_free(shader->ir3_shader.nir);

   for (uint32_t i = 0; i < 1 + shader->has_binning_pass; i++) {
      if (shader->variants[i].ir)
         ir3_destroy(shader->variants[i].ir);
      if (shader->variants[i].immediates)
         free(shader->variants[i].immediates);
   }

   if (shader->binary)
      free(shader->binary);
   if (shader->binning_binary)
      free(shader->binning_binary);

   vk_free2(&dev->alloc, alloc, shader);
}

void
tu_shader_compile_options_init(
   struct tu_shader_compile_options *options,
   const VkGraphicsPipelineCreateInfo *pipeline_info)
{
   *options = (struct tu_shader_compile_options) {
      /* TODO ir3_key */

      .optimize = !(pipeline_info->flags &
                    VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT),
      .include_binning_pass = true,
   };
}

static uint32_t *
tu_compile_shader_variant(struct ir3_shader *shader,
                          const struct ir3_shader_key *key,
                          bool binning_pass,
                          struct ir3_shader_variant *variant)
{
   variant->shader = shader;
   variant->type = shader->type;
   variant->key = *key;
   variant->binning_pass = binning_pass;

   int ret = ir3_compile_shader_nir(shader->compiler, variant);
   if (ret)
      return NULL;

   /* when assemble fails, we rely on tu_shader_destroy to clean up the
    * variant
    */
   return ir3_shader_assemble(variant, shader->compiler->gpu_id);
}

VkResult
tu_shader_compile(struct tu_device *dev,
                  struct tu_shader *shader,
                  const struct tu_shader *next_stage,
                  const struct tu_shader_compile_options *options,
                  const VkAllocationCallbacks *alloc)
{
   if (options->optimize) {
      /* ignore the key for the first pass of optimization */
      ir3_optimize_nir(&shader->ir3_shader, shader->ir3_shader.nir, NULL);

      if (unlikely(dev->physical_device->instance->debug_flags &
                   TU_DEBUG_NIR)) {
         fprintf(stderr, "optimized nir:\n");
         nir_print_shader(shader->ir3_shader.nir, stderr);
      }
   }

   shader->binary = tu_compile_shader_variant(
      &shader->ir3_shader, &options->key, false, &shader->variants[0]);
   if (!shader->binary)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* compile another variant for the binning pass */
   if (options->include_binning_pass &&
       shader->ir3_shader.type == MESA_SHADER_VERTEX) {
      shader->binning_binary = tu_compile_shader_variant(
         &shader->ir3_shader, &options->key, true, &shader->variants[1]);
      if (!shader->binning_binary)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      shader->has_binning_pass = true;
   }

   if (unlikely(dev->physical_device->instance->debug_flags & TU_DEBUG_IR3)) {
      fprintf(stderr, "disassembled ir3:\n");
      fprintf(stderr, "shader: %s\n",
              gl_shader_stage_name(shader->ir3_shader.type));
      ir3_shader_disasm(&shader->variants[0], shader->binary, stderr);

      if (shader->has_binning_pass) {
         fprintf(stderr, "disassembled ir3:\n");
         fprintf(stderr, "shader: %s (binning)\n",
                 gl_shader_stage_name(shader->ir3_shader.type));
         ir3_shader_disasm(&shader->variants[1], shader->binning_binary,
                           stderr);
      }
   }

   return VK_SUCCESS;
}

VkResult
tu_CreateShaderModule(VkDevice _device,
                      const VkShaderModuleCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkShaderModule *pShaderModule)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_shader_module *module;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
   assert(pCreateInfo->flags == 0);
   assert(pCreateInfo->codeSize % 4 == 0);

   module = vk_alloc2(&device->alloc, pAllocator,
                      sizeof(*module) + pCreateInfo->codeSize, 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (module == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   module->code_size = pCreateInfo->codeSize;
   memcpy(module->code, pCreateInfo->pCode, pCreateInfo->codeSize);

   _mesa_sha1_compute(module->code, module->code_size, module->sha1);

   *pShaderModule = tu_shader_module_to_handle(module);

   return VK_SUCCESS;
}

void
tu_DestroyShaderModule(VkDevice _device,
                       VkShaderModule _module,
                       const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_shader_module, module, _module);

   if (!module)
      return;

   vk_free2(&device->alloc, pAllocator, module);
}
