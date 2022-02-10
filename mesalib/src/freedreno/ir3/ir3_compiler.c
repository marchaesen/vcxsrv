/*
 * Copyright (C) 2015 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "util/ralloc.h"

#include "freedreno_dev_info.h"

#include "ir3_compiler.h"

static const struct debug_named_value shader_debug_options[] = {
   /* clang-format off */
   {"vs",         IR3_DBG_SHADER_VS,  "Print shader disasm for vertex shaders"},
   {"tcs",        IR3_DBG_SHADER_TCS, "Print shader disasm for tess ctrl shaders"},
   {"tes",        IR3_DBG_SHADER_TES, "Print shader disasm for tess eval shaders"},
   {"gs",         IR3_DBG_SHADER_GS,  "Print shader disasm for geometry shaders"},
   {"fs",         IR3_DBG_SHADER_FS,  "Print shader disasm for fragment shaders"},
   {"cs",         IR3_DBG_SHADER_CS,  "Print shader disasm for compute shaders"},
   {"disasm",     IR3_DBG_DISASM,     "Dump NIR and adreno shader disassembly"},
   {"optmsgs",    IR3_DBG_OPTMSGS,    "Enable optimizer debug messages"},
   {"forces2en",  IR3_DBG_FORCES2EN,  "Force s2en mode for tex sampler instructions"},
   {"nouboopt",   IR3_DBG_NOUBOOPT,   "Disable lowering UBO to uniform"},
   {"nofp16",     IR3_DBG_NOFP16,     "Don't lower mediump to fp16"},
   {"nocache",    IR3_DBG_NOCACHE,    "Disable shader cache"},
   {"spillall",   IR3_DBG_SPILLALL,   "Spill as much as possible to test the spiller"},
#ifdef DEBUG
   /* DEBUG-only options: */
   {"schedmsgs",  IR3_DBG_SCHEDMSGS,  "Enable scheduler debug messages"},
   {"ramsgs",     IR3_DBG_RAMSGS,     "Enable register-allocation debug messages"},
#endif
   DEBUG_NAMED_VALUE_END
   /* clang-format on */
};

DEBUG_GET_ONCE_FLAGS_OPTION(ir3_shader_debug, "IR3_SHADER_DEBUG",
                            shader_debug_options, 0)
DEBUG_GET_ONCE_OPTION(ir3_shader_override_path, "IR3_SHADER_OVERRIDE_PATH",
                      NULL)

enum ir3_shader_debug ir3_shader_debug = 0;
const char *ir3_shader_override_path = NULL;

void
ir3_compiler_destroy(struct ir3_compiler *compiler)
{
   disk_cache_destroy(compiler->disk_cache);
   ralloc_free(compiler);
}

static const nir_shader_compiler_options options = {
   .lower_fpow = true,
   .lower_scmp = true,
   .lower_flrp16 = true,
   .lower_flrp32 = true,
   .lower_flrp64 = true,
   .lower_ffract = true,
   .lower_fmod = true,
   .lower_fdiv = true,
   .lower_isign = true,
   .lower_ldexp = true,
   .lower_uadd_carry = true,
   .lower_usub_borrow = true,
   .lower_mul_high = true,
   .lower_mul_2x32_64 = true,
   .fuse_ffma16 = true,
   .fuse_ffma32 = true,
   .fuse_ffma64 = true,
   .vertex_id_zero_based = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_insert_byte = true,
   .lower_insert_word = true,
   .lower_helper_invocation = true,
   .lower_bitfield_insert_to_shifts = true,
   .lower_bitfield_extract_to_shifts = true,
   .lower_pack_half_2x16 = true,
   .lower_pack_snorm_4x8 = true,
   .lower_pack_snorm_2x16 = true,
   .lower_pack_unorm_4x8 = true,
   .lower_pack_unorm_2x16 = true,
   .lower_unpack_half_2x16 = true,
   .lower_unpack_snorm_4x8 = true,
   .lower_unpack_snorm_2x16 = true,
   .lower_unpack_unorm_4x8 = true,
   .lower_unpack_unorm_2x16 = true,
   .lower_pack_split = true,
   .use_interpolated_input_intrinsics = true,
   .lower_rotate = true,
   .lower_to_scalar = true,
   .has_imul24 = true,
   .has_fsub = true,
   .has_isub = true,
   .lower_wpos_pntc = true,
   .lower_cs_local_index_from_id = true,

   /* Only needed for the spirv_to_nir() pass done in ir3_cmdline.c
    * but that should be harmless for GL since 64b is not
    * supported there.
    */
   .lower_int64_options = (nir_lower_int64_options)~0,
   .lower_uniforms_to_ubo = true,
   .use_scoped_barrier = true,
};

/* we don't want to lower vertex_id to _zero_based on newer gpus: */
static const nir_shader_compiler_options options_a6xx = {
   .lower_fpow = true,
   .lower_scmp = true,
   .lower_flrp16 = true,
   .lower_flrp32 = true,
   .lower_flrp64 = true,
   .lower_ffract = true,
   .lower_fmod = true,
   .lower_fdiv = true,
   .lower_isign = true,
   .lower_ldexp = true,
   .lower_uadd_carry = true,
   .lower_usub_borrow = true,
   .lower_mul_high = true,
   .lower_mul_2x32_64 = true,
   .fuse_ffma16 = true,
   .fuse_ffma32 = true,
   .fuse_ffma64 = true,
   .vertex_id_zero_based = false,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_insert_byte = true,
   .lower_insert_word = true,
   .lower_helper_invocation = true,
   .lower_bitfield_insert_to_shifts = true,
   .lower_bitfield_extract_to_shifts = true,
   .lower_pack_half_2x16 = true,
   .lower_pack_snorm_4x8 = true,
   .lower_pack_snorm_2x16 = true,
   .lower_pack_unorm_4x8 = true,
   .lower_pack_unorm_2x16 = true,
   .lower_unpack_half_2x16 = true,
   .lower_unpack_snorm_4x8 = true,
   .lower_unpack_snorm_2x16 = true,
   .lower_unpack_unorm_4x8 = true,
   .lower_unpack_unorm_2x16 = true,
   .lower_pack_split = true,
   .use_interpolated_input_intrinsics = true,
   .lower_rotate = true,
   .vectorize_io = true,
   .lower_to_scalar = true,
   .has_imul24 = true,
   .has_fsub = true,
   .has_isub = true,
   .max_unroll_iterations = 32,
   .force_indirect_unrolling = nir_var_all,
   .lower_wpos_pntc = true,
   .lower_cs_local_index_from_id = true,

   /* Only needed for the spirv_to_nir() pass done in ir3_cmdline.c
    * but that should be harmless for GL since 64b is not
    * supported there.
    */
   .lower_int64_options = (nir_lower_int64_options)~0,
   .lower_uniforms_to_ubo = true,
   .lower_device_index_to_zero = true,
   .use_scoped_barrier = true,
   .has_udot_4x8 = true,
   .has_sudot_4x8 = true,
};

struct ir3_compiler *
ir3_compiler_create(struct fd_device *dev, const struct fd_dev_id *dev_id,
                    bool robust_ubo_access)
{
   struct ir3_compiler *compiler = rzalloc(NULL, struct ir3_compiler);

   ir3_shader_debug = debug_get_option_ir3_shader_debug();
   ir3_shader_override_path =
      !__check_suid() ? debug_get_option_ir3_shader_override_path() : NULL;

   if (ir3_shader_override_path) {
      ir3_shader_debug |= IR3_DBG_NOCACHE;
   }

   compiler->dev = dev;
   compiler->dev_id = dev_id;
   compiler->gen = fd_dev_gen(dev_id);
   compiler->robust_ubo_access = robust_ubo_access;

   /* All known GPU's have 32k local memory (aka shared) */
   compiler->local_mem_size = 32 * 1024;
   /* TODO see if older GPU's were different here */
   compiler->branchstack_size = 64;
   compiler->wave_granularity = 2;
   compiler->max_waves = 16;

   compiler->max_variable_workgroup_size = 1024;

   const struct fd_dev_info *dev_info = fd_dev_info(compiler->dev_id);

   if (compiler->gen >= 6) {
      compiler->samgq_workaround = true;
      /* a6xx split the pipeline state into geometry and fragment state, in
       * order to let the VS run ahead of the FS. As a result there are now
       * separate const files for the the fragment shader and everything
       * else, and separate limits. There seems to be a shared limit, but
       * it's higher than the vert or frag limits.
       *
       * TODO: The shared limit seems to be different on different on
       * different models.
       */
      compiler->max_const_pipeline = 640;
      compiler->max_const_frag = 512;
      compiler->max_const_geom = 512;
      compiler->max_const_safe = 128;

      /* Compute shaders don't share a const file with the FS. Instead they
       * have their own file, which is smaller than the FS one.
       *
       * TODO: is this true on earlier gen's?
       */
      compiler->max_const_compute = 256;

      /* TODO: implement clip+cull distances on earlier gen's */
      compiler->has_clip_cull = true;

      /* TODO: implement private memory on earlier gen's */
      compiler->has_pvtmem = true;

      compiler->tess_use_shared = dev_info->a6xx.tess_use_shared;

      compiler->storage_16bit = dev_info->a6xx.storage_16bit;

      compiler->has_getfiberid = dev_info->a6xx.has_getfiberid;

      compiler->has_dp2acc = dev_info->a6xx.has_dp2acc;
      compiler->has_dp4acc = dev_info->a6xx.has_dp4acc;
   } else {
      compiler->max_const_pipeline = 512;
      compiler->max_const_geom = 512;
      compiler->max_const_frag = 512;
      compiler->max_const_compute = 512;

      /* Note: this will have to change if/when we support tess+GS on
       * earlier gen's.
       */
      compiler->max_const_safe = 256;
   }

   if (compiler->gen >= 6) {
      compiler->reg_size_vec4 = dev_info->a6xx.reg_size_vec4;
   } else if (compiler->gen >= 4) {
      /* On a4xx-a5xx, using r24.x and above requires using the smallest
       * threadsize.
       */
      compiler->reg_size_vec4 = 48;
   } else {
      /* TODO: confirm this */
      compiler->reg_size_vec4 = 96;
   }

   if (compiler->gen >= 6) {
      compiler->threadsize_base = 64;
   } else if (compiler->gen >= 4) {
      /* TODO: Confirm this for a4xx. For a5xx this is based on the Vulkan
       * 1.1 subgroupSize which is 32.
       */
      compiler->threadsize_base = 32;
   } else {
      compiler->threadsize_base = 8;
   }

   if (compiler->gen >= 4) {
      /* need special handling for "flat" */
      compiler->flat_bypass = true;
      compiler->levels_add_one = false;
      compiler->unminify_coords = false;
      compiler->txf_ms_with_isaml = false;
      compiler->array_index_add_half = true;
      compiler->instr_align = 16;
      compiler->const_upload_unit = 4;
   } else {
      /* no special handling for "flat" */
      compiler->flat_bypass = false;
      compiler->levels_add_one = true;
      compiler->unminify_coords = true;
      compiler->txf_ms_with_isaml = true;
      compiler->array_index_add_half = false;
      compiler->instr_align = 4;
      compiler->const_upload_unit = 8;
   }

   compiler->bool_type = (compiler->gen >= 5) ? TYPE_U16 : TYPE_U32;

   if (compiler->gen >= 6) {
      compiler->nir_options = options_a6xx;
      compiler->nir_options.has_udot_4x8 = dev_info->a6xx.has_dp2acc;
      compiler->nir_options.has_sudot_4x8 = dev_info->a6xx.has_dp2acc;
   } else {
      compiler->nir_options = options;
   }

   ir3_disk_cache_init(compiler);

   return compiler;
}

const nir_shader_compiler_options *
ir3_get_compiler_options(struct ir3_compiler *compiler)
{
   return &compiler->nir_options;
}
