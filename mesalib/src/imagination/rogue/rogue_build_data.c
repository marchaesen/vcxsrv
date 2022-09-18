/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "compiler/shader_enums.h"
#include "nir/nir.h"
#include "rogue_build_data.h"
#include "rogue_nir_helpers.h"
#include "rogue_operand.h"
#include "util/macros.h"

#define __pvr_address_type uint64_t
#define __pvr_get_address(pvr_dev_addr) (pvr_dev_addr)
#define __pvr_make_address(addr_u64) (addr_u64)

#include "csbgen/rogue_pds.h"

#undef __pvr_make_address
#undef __pvr_get_address
#undef __pvr_address_type

/**
 * \brief Allocates the coefficient registers that will contain the iterator
 * data for the fragment shader input varyings.
 *
 * \param[in] args The iterator argument data.
 * \return The total number of coefficient registers required by the iterators.
 */
static size_t alloc_iterator_regs(struct rogue_iterator_args *args)
{
   size_t coeffs = 0;

   for (size_t u = 0; u < args->num_fpu_iterators; ++u) {
      /* Ensure there aren't any gaps. */
      assert(args->base[u] == ~0);

      args->base[u] = coeffs;
      coeffs += ROGUE_COEFF_ALIGN * args->components[u];
   }

   return coeffs;
}

/**
 * \brief Reserves an iterator for a fragment shader input varying,
 * and calculates its setup data.
 *
 * \param[in] args The iterator argument data.
 * \param[in] i The iterator index.
 * \param[in] type The interpolation type of the varying.
 * \param[in] f16 Whether the data type is F16 or F32.
 * \param[in] components The number of components in the varying.
 */
static void reserve_iterator(struct rogue_iterator_args *args,
                             size_t i,
                             enum glsl_interp_mode type,
                             bool f16,
                             size_t components)
{
   struct ROGUE_PDSINST_DOUT_FIELDS_DOUTI_SRC data = { 0 };

   assert(components >= 1 && components <= 4);

   /* The first iterator (W) *must* be INTERP_MODE_NOPERSPECTIVE. */
   assert(i > 0 || type == INTERP_MODE_NOPERSPECTIVE);
   assert(i < ARRAY_SIZE(args->fpu_iterators));

   switch (type) {
   /* Default interpolation is smooth. */
   case INTERP_MODE_NONE:
      data.shademodel = ROGUE_PDSINST_DOUTI_SHADEMODEL_GOURUAD;
      data.perspective = true;
      break;

   case INTERP_MODE_NOPERSPECTIVE:
      data.shademodel = ROGUE_PDSINST_DOUTI_SHADEMODEL_GOURUAD;
      data.perspective = false;
      break;

   default:
      unreachable("Unimplemented interpolation type.");
   }

   /* Number of components in this varying
    * (corresponds to ROGUE_PDSINST_DOUTI_SIZE_1..4D).
    */
   data.size = (components - 1);

   /* TODO: Investigate F16 support. */
   assert(!f16);
   data.f16 = f16;

   /* Offsets within the vertex. */
   data.f32_offset = 2 * i;
   data.f16_offset = data.f32_offset;

   ROGUE_PDSINST_DOUT_FIELDS_DOUTI_SRC_pack(&args->fpu_iterators[i], &data);
   args->destination[i] = i;
   args->base[i] = ~0;
   args->components[i] = components;
   ++args->num_fpu_iterators;
}

/**
 * \brief Collects the fragment shader I/O data to feed-back to the driver.
 *
 * \sa #collect_io_data()
 *
 * \param[in] common_data Common build data.
 * \param[in] fs_data Fragment-specific build data.
 * \param[in] nir NIR fragment shader.
 * \return true if successful, otherwise false.
 */
static bool collect_io_data_fs(struct rogue_common_build_data *common_data,
                               struct rogue_fs_build_data *fs_data,
                               nir_shader *nir)
{
   size_t num_inputs = nir_count_variables_with_modes(nir, nir_var_shader_in);
   assert(num_inputs < (ARRAY_SIZE(fs_data->iterator_args.fpu_iterators) - 1));

   /* Process inputs (if present). */
   if (num_inputs) {
      /* If the fragment shader has inputs, the first iterator
       * must be used for the W component.
       */
      reserve_iterator(&fs_data->iterator_args,
                       0,
                       INTERP_MODE_NOPERSPECTIVE,
                       false,
                       1);

      nir_foreach_shader_in_variable (var, nir) {
         size_t i = (var->data.location - VARYING_SLOT_VAR0) + 1;
         size_t components = glsl_get_components(var->type);
         enum glsl_interp_mode interp = var->data.interpolation;
         bool f16 = glsl_type_is_16bit(var->type);

         /* Check that arguments are either F16 or F32. */
         assert(glsl_get_base_type(var->type) == GLSL_TYPE_FLOAT);
         assert(f16 || glsl_type_is_32bit(var->type));

         /* Check input location. */
         assert(var->data.location >= VARYING_SLOT_VAR0 &&
                var->data.location <= VARYING_SLOT_VAR31);

         reserve_iterator(&fs_data->iterator_args, i, interp, f16, components);
      }

      common_data->coeffs = alloc_iterator_regs(&fs_data->iterator_args);
      assert(common_data->coeffs);
      assert(common_data->coeffs < ROGUE_MAX_REG_COEFF);
   }

   /* TODO: Process outputs. */

   return true;
}

/**
 * \brief Allocates the vertex shader input registers.
 *
 * \param[in] inputs The vertex shader input data.
 * \return The total number of vertex input registers required.
 */
static size_t alloc_vs_inputs(struct rogue_vertex_inputs *inputs)
{
   size_t vs_inputs = 0;

   for (size_t u = 0; u < inputs->num_input_vars; ++u) {
      /* Ensure there aren't any gaps. */
      assert(inputs->base[u] == ~0);

      inputs->base[u] = vs_inputs;
      vs_inputs += inputs->components[u];
   }

   return vs_inputs;
}

/**
 * \brief Allocates the vertex shader outputs.
 *
 * \param[in] outputs The vertex shader output data.
 * \return The total number of vertex outputs required.
 */
static size_t alloc_vs_outputs(struct rogue_vertex_outputs *outputs)
{
   size_t vs_outputs = 0;

   for (size_t u = 0; u < outputs->num_output_vars; ++u) {
      /* Ensure there aren't any gaps. */
      assert(outputs->base[u] == ~0);

      outputs->base[u] = vs_outputs;
      vs_outputs += outputs->components[u];
   }

   return vs_outputs;
}

/**
 * \brief Counts the varyings used by the vertex shader.
 *
 * \param[in] outputs The vertex shader output data.
 * \return The number of varyings used.
 */
static size_t count_vs_varyings(struct rogue_vertex_outputs *outputs)
{
   size_t varyings = 0;

   /* Skip the position. */
   for (size_t u = 1; u < outputs->num_output_vars; ++u)
      varyings += outputs->components[u];

   return varyings;
}

/**
 * \brief Reserves space for a vertex shader input.
 *
 * \param[in] inputs The vertex input data.
 * \param[in] i The vertex input index.
 * \param[in] components The number of components in the input.
 */
static void reserve_vs_input(struct rogue_vertex_inputs *inputs,
                             size_t i,
                             size_t components)
{
   assert(components >= 1 && components <= 4);

   assert(i < ARRAY_SIZE(inputs->base));

   inputs->base[i] = ~0;
   inputs->components[i] = components;
   ++inputs->num_input_vars;
}

/**
 * \brief Reserves space for a vertex shader output.
 *
 * \param[in] outputs The vertex output data.
 * \param[in] i The vertex output index.
 * \param[in] components The number of components in the output.
 */
static void reserve_vs_output(struct rogue_vertex_outputs *outputs,
                              size_t i,
                              size_t components)
{
   assert(components >= 1 && components <= 4);

   assert(i < ARRAY_SIZE(outputs->base));

   outputs->base[i] = ~0;
   outputs->components[i] = components;
   ++outputs->num_output_vars;
}

/**
 * \brief Collects the vertex shader I/O data to feed-back to the driver.
 *
 * \sa #collect_io_data()
 *
 * \param[in] common_data Common build data.
 * \param[in] vs_data Vertex-specific build data.
 * \param[in] nir NIR vertex shader.
 * \return true if successful, otherwise false.
 */
static bool collect_io_data_vs(struct rogue_common_build_data *common_data,
                               struct rogue_vs_build_data *vs_data,
                               nir_shader *nir)
{
   ASSERTED bool out_pos_present = false;
   ASSERTED size_t num_outputs =
      nir_count_variables_with_modes(nir, nir_var_shader_out);

   /* Process inputs. */
   nir_foreach_shader_in_variable (var, nir) {
      size_t components = glsl_get_components(var->type);
      size_t i = var->data.location - VERT_ATTRIB_GENERIC0;

      /* Check that inputs are F32. */
      /* TODO: Support other types. */
      assert(glsl_get_base_type(var->type) == GLSL_TYPE_FLOAT);
      assert(glsl_type_is_32bit(var->type));

      /* Check input location. */
      assert(var->data.location >= VERT_ATTRIB_GENERIC0 &&
             var->data.location <= VERT_ATTRIB_GENERIC15);

      reserve_vs_input(&vs_data->inputs, i, components);
   }

   vs_data->num_vertex_input_regs = alloc_vs_inputs(&vs_data->inputs);
   assert(vs_data->num_vertex_input_regs);
   assert(vs_data->num_vertex_input_regs < ROGUE_MAX_REG_VERTEX_IN);

   /* Process outputs. */

   /* We should always have at least a position variable. */
   assert(num_outputs > 0 && "Invalid number of vertex shader outputs.");

   nir_foreach_shader_out_variable (var, nir) {
      size_t components = glsl_get_components(var->type);

      /* Check that outputs are F32. */
      /* TODO: Support other types. */
      assert(glsl_get_base_type(var->type) == GLSL_TYPE_FLOAT);
      assert(glsl_type_is_32bit(var->type));

      if (var->data.location == VARYING_SLOT_POS) {
         assert(components == 4);
         out_pos_present = true;

         reserve_vs_output(&vs_data->outputs, 0, components);
      } else if ((var->data.location >= VARYING_SLOT_VAR0) &&
                 (var->data.location <= VARYING_SLOT_VAR31)) {
         size_t i = (var->data.location - VARYING_SLOT_VAR0) + 1;
         reserve_vs_output(&vs_data->outputs, i, components);
      } else {
         unreachable("Unsupported vertex output type.");
      }
   }

   /* Always need the output position to be present. */
   assert(out_pos_present);

   vs_data->num_vertex_outputs = alloc_vs_outputs(&vs_data->outputs);
   assert(vs_data->num_vertex_outputs);
   assert(vs_data->num_vertex_outputs < ROGUE_MAX_VERTEX_OUTPUTS);

   vs_data->num_varyings = count_vs_varyings(&vs_data->outputs);

   return true;
}

/**
 * \brief Allocates the shared registers that will contain the UBOs.
 *
 * \param[in] ubo_data The UBO data.
 * \return The total number of coefficient registers required by the iterators.
 */
static size_t alloc_ubos(struct rogue_ubo_data *ubo_data)
{
   size_t shareds = 0;

   for (size_t u = 0; u < ubo_data->num_ubo_entries; ++u) {
      /* Ensure there aren't any gaps. */
      assert(ubo_data->dest[u] == ~0);

      ubo_data->dest[u] = shareds;
      shareds += ubo_data->size[u];
   }

   return shareds;
}

/**
 * \brief Reserves a UBO and calculates its data.
 *
 * \param[in] ubo_data The UBO data.
 * \param[in] desc_set The UBO descriptor set.
 * \param[in] binding The UBO binding.
 * \param[in] size The size required by the UBO (in dwords).
 */
static void reserve_ubo(struct rogue_ubo_data *ubo_data,
                        size_t desc_set,
                        size_t binding,
                        size_t size)
{
   size_t i = ubo_data->num_ubo_entries;
   assert(i < ARRAY_SIZE(ubo_data->desc_set));

   ubo_data->desc_set[i] = desc_set;
   ubo_data->binding[i] = binding;
   ubo_data->dest[i] = ~0;
   ubo_data->size[i] = size;
   ++ubo_data->num_ubo_entries;
}

/**
 * \brief Collects UBO data to feed-back to the driver.
 *
 * \param[in] common_data Common build data.
 * \param[in] nir NIR shader.
 * \return true if successful, otherwise false.
 */
static bool collect_ubo_data(struct rogue_common_build_data *common_data,
                             nir_shader *nir)
{
   /* Iterate over each UBO. */
   nir_foreach_variable_with_modes (var, nir, nir_var_mem_ubo) {
      size_t desc_set = var->data.driver_location;
      size_t binding = var->data.binding;
      size_t ubo_size_regs = 0;

      nir_function_impl *entry = nir_shader_get_entrypoint(nir);
      /* Iterate over each load_ubo that uses this UBO. */
      nir_foreach_block (block, entry) {
         nir_foreach_instr (instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_load_ubo)
               continue;

            assert(nir_src_num_components(intr->src[0]) == 2);
            assert(nir_intr_src_is_const(intr, 0));

            size_t load_desc_set = nir_intr_src_comp_const(intr, 0, 0);
            size_t load_binding = nir_intr_src_comp_const(intr, 0, 1);

            if (load_desc_set != desc_set || load_binding != binding)
               continue;

            ASSERTED size_t size_bytes = nir_intrinsic_range(intr);
            assert(size_bytes == ROGUE_REG_SIZE_BYTES);

            size_t offset_bytes = nir_intrinsic_range_base(intr);
            assert(!(offset_bytes % ROGUE_REG_SIZE_BYTES));

            size_t offset_regs = offset_bytes / ROGUE_REG_SIZE_BYTES;

            /* TODO: Put offsets in a BITSET_DECLARE and check for gaps. */

            /* Find the largest load offset. */
            ubo_size_regs = MAX2(ubo_size_regs, offset_regs);
         }
      }

      /* UBO size = largest offset + 1. */
      ++ubo_size_regs;

      reserve_ubo(&common_data->ubo_data, desc_set, binding, ubo_size_regs);
   }

   common_data->shareds = alloc_ubos(&common_data->ubo_data);
   assert(common_data->shareds < ROGUE_MAX_REG_SHARED);

   return true;
}

/**
 * \brief Collects I/O data to feed-back to the driver.
 *
 * Collects the inputs/outputs/memory required, and feeds that back to the
 * driver. Done at this stage rather than at the start of rogue_to_binary, so
 * that all the I/O of all the shader stages is known before backend
 * compilation, which would let us do things like cull unused inputs.
 *
 * \param[in] ctx Shared multi-stage build context.
 * \param[in] nir NIR shader.
 * \return true if successful, otherwise false.
 */
bool rogue_collect_io_data(struct rogue_build_ctx *ctx, nir_shader *nir)
{
   gl_shader_stage stage = nir->info.stage;
   struct rogue_common_build_data *common_data = &ctx->common_data[stage];

   /* Collect stage-agnostic data. */
   if (!collect_ubo_data(common_data, nir))
      return false;

   /* Collect stage-specific data. */
   switch (stage) {
   case MESA_SHADER_FRAGMENT:
      return collect_io_data_fs(common_data, &ctx->stage_data.fs, nir);

   case MESA_SHADER_VERTEX:
      return collect_io_data_vs(common_data, &ctx->stage_data.vs, nir);

   default:
      break;
   }

   return false;
}

/**
 * \brief Returns the allocated coefficient register index for a component of an
 * input varying location.
 *
 * \param[in] args The allocated iterator argument data.
 * \param[in] location The input varying location, or ~0 for the W coefficient.
 * \param[in] component The requested component.
 * \return The coefficient register index.
 */
size_t rogue_coeff_index_fs(struct rogue_iterator_args *args,
                            gl_varying_slot location,
                            size_t component)
{
   size_t i;

   /* Special case: W coefficient. */
   if (location == ~0) {
      /* The W component shouldn't be the only one. */
      assert(args->num_fpu_iterators > 1);
      assert(args->destination[0] == 0);
      return 0;
   }

   i = (location - VARYING_SLOT_VAR0) + 1;
   assert(location >= VARYING_SLOT_VAR0 && location <= VARYING_SLOT_VAR31);
   assert(i < args->num_fpu_iterators);
   assert(component < args->components[i]);
   assert(args->base[i] != ~0);

   return args->base[i] + (ROGUE_COEFF_ALIGN * component);
}

/**
 * \brief Returns the allocated vertex output index for a component of an input
 * varying location.
 *
 * \param[in] outputs The vertex output data.
 * \param[in] location The output varying location.
 * \param[in] component The requested component.
 * \return The vertex output index.
 */
size_t rogue_output_index_vs(struct rogue_vertex_outputs *outputs,
                             gl_varying_slot location,
                             size_t component)
{
   size_t i;

   if (location == VARYING_SLOT_POS) {
      /* Always at location 0. */
      assert(outputs->base[0] == 0);
      i = 0;
   } else if ((location >= VARYING_SLOT_VAR0) &&
              (location <= VARYING_SLOT_VAR31)) {
      i = (location - VARYING_SLOT_VAR0) + 1;
   } else {
      unreachable("Unsupported vertex output type.");
   }

   assert(i < outputs->num_output_vars);
   assert(component < outputs->components[i]);
   assert(outputs->base[i] != ~0);

   return outputs->base[i] + component;
}

/**
 * \brief Returns the allocated shared register index for a given UBO offset.
 *
 * \param[in] ubo_data The UBO data.
 * \param[in] desc_set The UBO descriptor set.
 * \param[in] binding The UBO binding.
 * \param[in] offset_bytes The UBO offset in bytes.
 * \return The UBO offset shared register index.
 */
size_t rogue_ubo_reg(struct rogue_ubo_data *ubo_data,
                     size_t desc_set,
                     size_t binding,
                     size_t offset_bytes)
{
   size_t ubo_index = ~0;
   size_t offset_regs;

   /* Find UBO located at (desc_set, binding). */
   for (size_t u = 0; u < ubo_data->num_ubo_entries; ++u) {
      if (ubo_data->dest[u] == ~0)
         continue;

      if (ubo_data->desc_set[u] != desc_set || ubo_data->binding[u] != binding)
         continue;

      ubo_index = u;
      break;
   }

   assert(ubo_index != ~0);

   assert(!(offset_bytes % ROGUE_REG_SIZE_BYTES));
   offset_regs = offset_bytes / ROGUE_REG_SIZE_BYTES;

   return ubo_data->dest[ubo_index] + offset_regs;
}
