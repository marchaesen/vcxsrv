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

static nir_shader *
tu_spirv_to_nir(struct ir3_compiler *compiler,
                const uint32_t *words,
                size_t word_count,
                gl_shader_stage stage,
                const char *entry_point_name,
                const VkSpecializationInfo *spec_info)
{
   /* TODO these are made-up */
   const struct spirv_to_nir_options spirv_options = {
      .frag_coord_is_sysval = true,
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

   nir_shader *nir =
      spirv_to_nir(words, word_count, spec, num_spec, stage, entry_point_name,
                   &spirv_options, nir_options);

   free(spec);

   assert(nir->info.stage == stage);
   nir_validate_shader(nir, "after spirv_to_nir");

   return nir;
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

static unsigned
map_add(struct tu_descriptor_map *map, int set, int binding)
{
   unsigned index;
   for (index = 0; index < map->num; index++) {
      if (set == map->set[index] && binding == map->binding[index])
         break;
   }

   assert(index < ARRAY_SIZE(map->set));

   map->set[index] = set;
   map->binding[index] = binding;
   map->num = MAX2(map->num, index + 1);
   return index;
}

static void
lower_tex_src_to_offset(nir_builder *b, nir_tex_instr *instr, unsigned src_idx,
                        struct tu_shader *shader)
{
   nir_ssa_def *index = NULL;
   unsigned base_index = 0;
   unsigned array_elements = 1;
   nir_tex_src *src = &instr->src[src_idx];
   bool is_sampler = src->src_type == nir_tex_src_sampler_deref;

   /* We compute first the offsets */
   nir_deref_instr *deref = nir_instr_as_deref(src->src.ssa->parent_instr);
   while (deref->deref_type != nir_deref_type_var) {
      assert(deref->parent.is_ssa);
      nir_deref_instr *parent =
         nir_instr_as_deref(deref->parent.ssa->parent_instr);

      assert(deref->deref_type == nir_deref_type_array);

      if (nir_src_is_const(deref->arr.index) && index == NULL) {
         /* We're still building a direct index */
         base_index += nir_src_as_uint(deref->arr.index) * array_elements;
      } else {
         if (index == NULL) {
            /* We used to be direct but not anymore */
            index = nir_imm_int(b, base_index);
            base_index = 0;
         }

         index = nir_iadd(b, index,
                          nir_imul(b, nir_imm_int(b, array_elements),
                                   nir_ssa_for_src(b, deref->arr.index, 1)));
      }

      array_elements *= glsl_get_length(parent->type);

      deref = parent;
   }

   if (index)
      index = nir_umin(b, index, nir_imm_int(b, array_elements - 1));

   /* We have the offsets, we apply them, rewriting the source or removing
    * instr if needed
    */
   if (index) {
      nir_instr_rewrite_src(&instr->instr, &src->src,
                            nir_src_for_ssa(index));

      src->src_type = is_sampler ?
         nir_tex_src_sampler_offset :
         nir_tex_src_texture_offset;

      instr->texture_array_size = array_elements;
   } else {
      nir_tex_instr_remove_src(instr, src_idx);
   }

   if (array_elements > 1)
      tu_finishme("texture/sampler array");

   if (is_sampler) {
      instr->sampler_index = map_add(&shader->sampler_map,
                                     deref->var->data.descriptor_set,
                                     deref->var->data.binding);
      instr->sampler_index += base_index;
   } else {
      instr->texture_index = map_add(&shader->texture_map,
                                     deref->var->data.descriptor_set,
                                     deref->var->data.binding);
      instr->texture_index += base_index;
      instr->texture_array_size = array_elements;
   }
}

static bool
lower_sampler(nir_builder *b, nir_tex_instr *instr, struct tu_shader *shader)
{
   int texture_idx =
      nir_tex_instr_src_index(instr, nir_tex_src_texture_deref);

   if (texture_idx >= 0)
      lower_tex_src_to_offset(b, instr, texture_idx, shader);

   int sampler_idx =
      nir_tex_instr_src_index(instr, nir_tex_src_sampler_deref);

   if (sampler_idx >= 0)
      lower_tex_src_to_offset(b, instr, sampler_idx, shader);

   if (texture_idx < 0 && sampler_idx < 0)
      return false;

   return true;
}

static bool
lower_intrinsic(nir_builder *b, nir_intrinsic_instr *instr,
                struct tu_shader *shader)
{
   if (instr->intrinsic == nir_intrinsic_load_push_constant) {
      /* note: ir3 wants load_ubo, not load_uniform */
      assert(nir_intrinsic_base(instr) == 0);

      nir_intrinsic_instr *load =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_ubo);
      load->num_components = instr->num_components;
      load->src[0] = nir_src_for_ssa(nir_imm_int(b, 0));
      load->src[1] = instr->src[0];
      nir_ssa_dest_init(&load->instr, &load->dest,
                        load->num_components, instr->dest.ssa.bit_size,
                        instr->dest.ssa.name);
      nir_builder_instr_insert(b, &load->instr);
      nir_ssa_def_rewrite_uses(&instr->dest.ssa, nir_src_for_ssa(&load->dest.ssa));

      nir_instr_remove(&instr->instr);

      return true;
   }

   if (instr->intrinsic != nir_intrinsic_vulkan_resource_index)
      return false;

   nir_const_value *const_val = nir_src_as_const_value(instr->src[0]);
   if (!const_val || const_val->u32 != 0)
      tu_finishme("non-zero vulkan_resource_index array index");


   unsigned set = nir_intrinsic_desc_set(instr);
   unsigned binding = nir_intrinsic_binding(instr);
   unsigned index = 0;

   switch (nir_intrinsic_desc_type(instr)) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      /* skip index 0 which is used for push constants */
      index = map_add(&shader->ubo_map, set, binding) + 1;
      break;
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      index = map_add(&shader->ssbo_map, set, binding);
      break;
   default:
      tu_finishme("unsupported desc_type for vulkan_resource_index");
      break;
   }

   nir_ssa_def_rewrite_uses(&instr->dest.ssa,
                            nir_src_for_ssa(nir_imm_int(b, index)));
   nir_instr_remove(&instr->instr);

   return true;
}

static bool
lower_impl(nir_function_impl *impl, struct tu_shader *shader)
{
   nir_builder b;
   nir_builder_init(&b, impl);
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         b.cursor = nir_before_instr(instr);
         switch (instr->type) {
         case nir_instr_type_tex:
            progress |= lower_sampler(&b, nir_instr_as_tex(instr), shader);
            break;
         case nir_instr_type_intrinsic:
            progress |= lower_intrinsic(&b, nir_instr_as_intrinsic(instr), shader);
            break;
         default:
            break;
         }
      }
   }

   return progress;
}

static bool
tu_lower_io(nir_shader *shader, struct tu_shader *tu_shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= lower_impl(function->impl, tu_shader);
   }

   return progress;
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
   nir_shader *nir = tu_spirv_to_nir(
      dev->compiler, (const uint32_t *) module->code, module->code_size / 4,
      stage, stage_info->pName, stage_info->pSpecializationInfo);
   if (!nir) {
      vk_free2(&dev->alloc, alloc, shader);
      return NULL;
   }

   if (unlikely(dev->physical_device->instance->debug_flags & TU_DEBUG_NIR)) {
      fprintf(stderr, "translated nir:\n");
      nir_print_shader(nir, stderr);
   }

   /* multi step inlining procedure */
   NIR_PASS_V(nir, nir_lower_constant_initializers, nir_var_function_temp);
   NIR_PASS_V(nir, nir_lower_returns);
   NIR_PASS_V(nir, nir_inline_functions);
   NIR_PASS_V(nir, nir_opt_deref);
   foreach_list_typed_safe(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         exec_node_remove(&func->node);
   }
   assert(exec_list_length(&nir->functions) == 1);
   NIR_PASS_V(nir, nir_lower_constant_initializers, ~nir_var_function_temp);

   /* Split member structs.  We do this before lower_io_to_temporaries so that
    * it doesn't lower system values to temporaries by accident.
    */
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_split_per_member_structs);

   NIR_PASS_V(nir, nir_remove_dead_variables,
              nir_var_shader_in | nir_var_shader_out | nir_var_system_value | nir_var_mem_shared);

   NIR_PASS_V(nir, nir_propagate_invariant);

   NIR_PASS_V(nir, nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir), true, true);

   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);

   NIR_PASS_V(nir, nir_opt_copy_prop_vars);
   NIR_PASS_V(nir, nir_opt_combine_stores, nir_var_all);

   /* ir3 doesn't support indirect input/output */
   NIR_PASS_V(nir, nir_lower_indirect_derefs, nir_var_shader_in | nir_var_shader_out);

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

   nir_assign_io_var_locations(&nir->inputs, &nir->num_inputs, stage);
   nir_assign_io_var_locations(&nir->outputs, &nir->num_outputs, stage);

   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_frexp);

   NIR_PASS_V(nir, tu_lower_io, shader);

   NIR_PASS_V(nir, nir_lower_io, nir_var_all, ir3_glsl_type_size, 0);

   if (stage == MESA_SHADER_FRAGMENT) {
      /* NOTE: lower load_barycentric_at_sample first, since it
       * produces load_barycentric_at_offset:
       */
      NIR_PASS_V(nir, ir3_nir_lower_load_barycentric_at_sample);
      NIR_PASS_V(nir, ir3_nir_lower_load_barycentric_at_offset);

      NIR_PASS_V(nir, ir3_nir_move_varying_inputs);
   }

   NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   /* num_uniforms only used by ir3 for size of ubo 0 (push constants) */
   nir->num_uniforms = MAX_PUSH_CONSTANTS_SIZE / 16;

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
   }

   if (shader->ir3_shader.const_state.immediates)
	   free(shader->ir3_shader.const_state.immediates);
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

      /* TODO: VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT
       * some optimizations need to happen otherwise shader might not compile
       */
      .optimize = true,
      .include_binning_pass = true,
   };
}

static uint32_t *
tu_compile_shader_variant(struct ir3_shader *shader,
                          const struct ir3_shader_key *key,
                          struct ir3_shader_variant *nonbinning,
                          struct ir3_shader_variant *variant)
{
   variant->shader = shader;
   variant->type = shader->type;
   variant->key = *key;
   variant->binning_pass = !!nonbinning;
   variant->nonbinning = nonbinning;

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
      &shader->ir3_shader, &options->key, NULL, &shader->variants[0]);
   if (!shader->binary)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* compile another variant for the binning pass */
   if (options->include_binning_pass &&
       shader->ir3_shader.type == MESA_SHADER_VERTEX) {
      shader->binning_binary = tu_compile_shader_variant(
         &shader->ir3_shader, &options->key, &shader->variants[0],
         &shader->variants[1]);
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
