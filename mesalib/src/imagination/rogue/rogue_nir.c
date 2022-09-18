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

#include "compiler/spirv/nir_spirv.h"
#include "nir/nir.h"
#include "nir/nir_schedule.h"
#include "rogue_nir.h"
#include "rogue_operand.h"

/**
 * \file rogue_nir.c
 *
 * \brief Contains NIR-specific functions.
 */

/**
 * \brief SPIR-V to NIR compilation options.
 */
static const struct spirv_to_nir_options spirv_options = {
   .environment = NIR_SPIRV_VULKAN,

   /* Buffer address: (descriptor_set, binding), offset. */
   .ubo_addr_format = nir_address_format_vec2_index_32bit_offset,
};

static const nir_shader_compiler_options nir_options = {
   .lower_fsat = true,
   .fuse_ffma32 = true,
};

const struct spirv_to_nir_options *
rogue_get_spirv_options(const struct rogue_compiler *compiler)
{
   return &spirv_options;
}

const nir_shader_compiler_options *
rogue_get_compiler_options(const struct rogue_compiler *compiler)
{
   return &nir_options;
}

static int rogue_glsl_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

/**
 * \brief Applies optimizations and passes required to lower the NIR shader into
 * a form suitable for lowering to Rogue IR.
 *
 * \param[in] ctx Shared multi-stage build context.
 * \param[in] shader Rogue shader.
 * \param[in] stage Shader stage.
 * \return true if successful, otherwise false.
 */
bool rogue_nir_passes(struct rogue_build_ctx *ctx,
                      nir_shader *nir,
                      gl_shader_stage stage)
{
   bool progress;

   nir_validate_shader(nir, "after spirv_to_nir");

   /* Splitting. */
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_split_per_member_structs);

   /* Ensure fs outputs are in the [0.0f...1.0f] range. */
   NIR_PASS_V(nir, nir_lower_clamp_color_outputs);

   /* Replace references to I/O variables with intrinsics. */
   NIR_PASS_V(nir,
              nir_lower_io,
              nir_var_shader_in | nir_var_shader_out,
              rogue_glsl_type_size,
              (nir_lower_io_options)0);

   /* Load inputs to scalars (single registers later). */
   NIR_PASS_V(nir, nir_lower_io_to_scalar, nir_var_shader_in);

   /* Optimize GL access qualifiers. */
   const nir_opt_access_options opt_access_options = {
      .is_vulkan = true,
      .infer_non_readable = true,
   };
   NIR_PASS_V(nir, nir_opt_access, &opt_access_options);

   /* Apply PFO code to the fragment shader output. */
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS_V(nir, rogue_nir_pfo);

   /* Load outputs to scalars (single registers later). */
   NIR_PASS_V(nir, nir_lower_io_to_scalar, nir_var_shader_out);

   /* Lower ALU operations to scalars. */
   NIR_PASS_V(nir, nir_lower_alu_to_scalar, NULL, NULL);

   /* Algebraic opts. */
   do {
      progress = false;

      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS_V(nir, nir_opt_gcm, false);
   } while (progress);

   /* Additional I/O lowering. */
   NIR_PASS_V(nir,
              nir_lower_explicit_io,
              nir_var_mem_ubo,
              spirv_options.ubo_addr_format);
   NIR_PASS_V(nir, rogue_nir_lower_io, NULL);

   /* Late algebraic opts. */
   do {
      progress = false;

      NIR_PASS(progress, nir, nir_opt_algebraic_late);
      NIR_PASS_V(nir, nir_opt_constant_folding);
      NIR_PASS_V(nir, nir_copy_prop);
      NIR_PASS_V(nir, nir_opt_dce);
      NIR_PASS_V(nir, nir_opt_cse);
   } while (progress);

   /* Replace SSA constant references with a register that loads the value. */
   NIR_PASS_V(nir, rogue_nir_constreg);
   /* Remove unused constant registers. */
   NIR_PASS_V(nir, nir_opt_dce);

   /* Move loads to just before they're needed. */
   NIR_PASS_V(nir, nir_opt_move, nir_move_load_ubo | nir_move_load_input);

   /* Convert vecNs to movs so we can sequentially allocate them later. */
   NIR_PASS_V(nir, nir_lower_vec_to_movs, NULL, NULL);

   /* Out of SSA pass. */
   NIR_PASS_V(nir, nir_convert_from_ssa, false);

   /* TODO: Re-enable scheduling after register pressure tweaks. */
#if 0
	/* Instruction scheduling. */
	struct nir_schedule_options schedule_options = {
		.threshold = ROGUE_MAX_REG_TEMP / 2,
	};
	NIR_PASS_V(nir, nir_schedule, &schedule_options);
#endif

   /* Assign I/O locations. */
   nir_assign_io_var_locations(nir,
                               nir_var_shader_in,
                               &nir->num_inputs,
                               nir->info.stage);
   nir_assign_io_var_locations(nir,
                               nir_var_shader_out,
                               &nir->num_outputs,
                               nir->info.stage);

   /* Gather info into nir shader struct. */
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   /* Clean-up after passes. */
   nir_sweep(nir);

   nir_validate_shader(nir, "after passes");

   return true;
}
