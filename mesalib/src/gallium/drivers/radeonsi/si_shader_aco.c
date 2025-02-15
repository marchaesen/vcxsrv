/*
 * Copyright 2023 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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

#include "si_shader_internal.h"
#include "si_pipe.h"
#include "ac_hw_stage.h"
#include "aco_interface.h"
#include "nir.h"

static void
si_aco_compiler_debug(void *private_data, enum aco_compiler_debug_level level,
                      const char *message)
{
   struct util_debug_callback *debug = private_data;

   util_debug_message(debug, SHADER_INFO, "%s\n", message);
}

static void
si_fill_aco_options(struct si_screen *screen, gl_shader_stage stage,
                    struct aco_compiler_options *options,
                    struct util_debug_callback *debug)
{
   options->dump_ir = si_can_dump_shader(screen, stage, SI_DUMP_ACO_IR);
   options->dump_preoptir = si_can_dump_shader(screen, stage, SI_DUMP_INIT_ACO_IR);
   options->record_asm = si_can_dump_shader(screen, stage, SI_DUMP_ASM) ||
                         screen->options.debug_disassembly;
   options->record_ir = screen->record_llvm_ir;
   options->is_opengl = true;

   options->has_ls_vgpr_init_bug = screen->info.has_ls_vgpr_init_bug;
   options->load_grid_size_from_user_sgpr = true;
   options->family = screen->info.family;
   options->gfx_level = screen->info.gfx_level;
   options->address32_hi = screen->info.address32_hi;

   options->debug.func = si_aco_compiler_debug;
   options->debug.private_data = debug;
}

static void
si_fill_aco_shader_info(struct si_shader *shader, struct aco_shader_info *info,
                        struct si_shader_args *args)
{
   const struct si_shader_selector *sel = shader->selector;
   const union si_shader_key *key = &shader->key;
   const enum amd_gfx_level gfx_level = sel->screen->info.gfx_level;
   gl_shader_stage stage = shader->is_gs_copy_shader ? MESA_SHADER_VERTEX : sel->stage;

   info->wave_size = shader->wave_size;
   info->workgroup_size = si_get_max_workgroup_size(shader);
   info->merged_shader_compiled_separately = !shader->is_gs_copy_shader &&
      si_is_multi_part_shader(shader) && !shader->is_monolithic;

   info->image_2d_view_of_3d = gfx_level == GFX9;
   info->hw_stage = si_select_hw_stage(stage, key, gfx_level);

   if (stage <= MESA_SHADER_GEOMETRY && key->ge.as_ngg && !key->ge.as_es) {
      info->schedule_ngg_pos_exports = sel->screen->info.gfx_level < GFX11 &&
                                       si_shader_culling_enabled(shader) &&
                                       gfx10_ngg_export_prim_early(shader);
   }

   switch (stage) {
   case MESA_SHADER_TESS_CTRL:
      info->vs.tcs_in_out_eq = key->ge.opt.same_patch_vertices;
      info->vs.any_tcs_inputs_via_lds = sel->info.tcs_inputs_via_lds ||
                                        (!shader->key.ge.opt.same_patch_vertices &&
                                         sel->info.tcs_inputs_via_temp);
      info->tcs.tcs_offchip_layout = args->tcs_offchip_layout;
      break;
   case MESA_SHADER_FRAGMENT:
      info->ps.num_inputs = si_get_ps_num_interp(shader);
      info->ps.spi_ps_input_ena = shader->config.spi_ps_input_ena;
      info->ps.spi_ps_input_addr = shader->config.spi_ps_input_addr;
      info->ps.alpha_reference = args->alpha_reference;
      info->ps.has_prolog = !shader->is_monolithic;
      info->ps.has_epilog = !shader->is_monolithic;
      break;
   default:
      break;
   }
}

static void
si_aco_build_shader_binary(void **data, const struct ac_shader_config *config,
                           const char *llvm_ir_str, unsigned llvm_ir_size, const char *disasm_str,
                           unsigned disasm_size, uint32_t *statistics, uint32_t stats_size,
                           uint32_t exec_size, const uint32_t *code, uint32_t code_dw,
                           const struct aco_symbol *symbols, unsigned num_symbols,
                           const struct ac_shader_debug_info *debug_info, unsigned debug_info_count)
{
   struct si_shader *shader = (struct si_shader *)data;

   unsigned code_size = code_dw * 4;
   char *buffer = MALLOC(code_size + disasm_size);
   memcpy(buffer, code, code_size);

   shader->binary.type = SI_SHADER_BINARY_RAW;
   shader->binary.code_buffer = buffer;
   shader->binary.code_size = code_size;
   shader->binary.exec_size = exec_size;

   if (disasm_size) {
      memcpy(buffer + code_size, disasm_str, disasm_size);
      shader->binary.disasm_string = buffer + code_size;
      shader->binary.disasm_size = disasm_size;
   }

   if (llvm_ir_size) {
      shader->binary.llvm_ir_string = MALLOC(llvm_ir_size);
      memcpy(shader->binary.llvm_ir_string, llvm_ir_str, llvm_ir_size);
   }

   if (num_symbols) {
      unsigned symbol_size = num_symbols * sizeof(*symbols);
      void *data = MALLOC(symbol_size);
      memcpy(data, symbols, symbol_size);
      shader->binary.symbols = data;
      shader->binary.num_symbols = num_symbols;
   }

   shader->config = *config;
}

bool
si_aco_compile_shader(struct si_shader *shader, struct si_linked_shaders *linked,
                      struct util_debug_callback *debug)
{
   const struct si_shader_selector *sel = shader->selector;
   nir_shader *nir = linked->consumer.nir;

   struct aco_compiler_options options = {0};
   si_fill_aco_options(sel->screen, nir->info.stage, &options, debug);

   struct aco_shader_info info = {0};
   si_fill_aco_shader_info(shader, &info, &linked->consumer.args);

   const struct ac_shader_args *args = &linked->consumer.args.ac;
   nir_shader *shaders[2];
   unsigned num_shaders = 0;

   /* For merged shader stage. */
   if (linked->producer.nir) {
      shaders[num_shaders++] = linked->producer.nir;
      args = &linked->producer.args.ac;
   }

   shaders[num_shaders++] = nir;

   aco_compile_shader(&options, &info, num_shaders, shaders, args,
                      si_aco_build_shader_binary, (void **)shader);

   return true;
}

void
si_aco_resolve_symbols(struct si_shader *shader, uint32_t *code_for_write,
                       const uint32_t *code_for_read, uint64_t scratch_va, uint32_t const_offset)
{
   const struct aco_symbol *symbols = (struct aco_symbol *)shader->binary.symbols;
   const struct si_shader_selector *sel = shader->selector;
   const union si_shader_key *key = &shader->key;

   for (int i = 0; i < shader->binary.num_symbols; i++) {
      uint32_t value = 0;

      switch (symbols[i].id) {
      case aco_symbol_scratch_addr_lo:
         value = scratch_va;
         break;
      case aco_symbol_scratch_addr_hi:
         value = S_008F04_BASE_ADDRESS_HI(scratch_va >> 32);

         if (sel->screen->info.gfx_level >= GFX11)
            value |= S_008F04_SWIZZLE_ENABLE_GFX11(1);
         else
            value |= S_008F04_SWIZZLE_ENABLE_GFX6(1);
         break;
      case aco_symbol_lds_ngg_scratch_base:
         assert(sel->stage <= MESA_SHADER_GEOMETRY && key->ge.as_ngg);
         value = shader->gs_info.esgs_ring_size * 4;
         if (sel->stage == MESA_SHADER_GEOMETRY)
            value += shader->ngg.ngg_emit_size * 4;
         value = ALIGN(value, 8);
         break;
      case aco_symbol_lds_ngg_gs_out_vertex_base:
         assert(sel->stage == MESA_SHADER_GEOMETRY && key->ge.as_ngg);
         value = shader->gs_info.esgs_ring_size * 4;
         break;
      case aco_symbol_const_data_addr:
         if (!const_offset)
            continue;
         value = code_for_read[symbols[i].offset] + const_offset;
         break;
      default:
         unreachable("invalid aco symbol");
         break;
      }

      code_for_write[symbols[i].offset] = value;
   }
}

static void
si_aco_build_shader_part_binary(void** priv_ptr, uint32_t num_sgprs, uint32_t num_vgprs,
                                const uint32_t* code, uint32_t code_dw_size,
                                const char* disasm_str, uint32_t disasm_size)
{
   struct si_shader_part *result = (struct si_shader_part *)priv_ptr;
   unsigned code_size = code_dw_size * 4;

   char *buffer = MALLOC(code_size + disasm_size);
   memcpy(buffer, code, code_size);

   result->binary.type = SI_SHADER_BINARY_RAW;
   result->binary.code_buffer = buffer;
   result->binary.code_size = code_size;
   result->binary.exec_size = code_size;

   if (disasm_size) {
      memcpy(buffer + code_size, disasm_str, disasm_size);
      result->binary.disasm_string = buffer + code_size;
      result->binary.disasm_size = disasm_size;
   }

   result->num_sgprs = num_sgprs;
   result->num_vgprs = num_vgprs;
}

static bool
si_aco_build_ps_prolog(struct aco_compiler_options *options,
                       struct si_shader_part *result)
{
   const union si_shader_part_key *key = &result->key;

   struct si_shader_args args;
   si_get_ps_prolog_args(&args, key);

   struct aco_ps_prolog_info pinfo = {
      .poly_stipple = key->ps_prolog.states.poly_stipple,
      .poly_stipple_buf_offset = SI_PS_CONST_POLY_STIPPLE * 16,

      .bc_optimize_for_persp = key->ps_prolog.states.bc_optimize_for_persp,
      .bc_optimize_for_linear = key->ps_prolog.states.bc_optimize_for_linear,
      .force_persp_sample_interp = key->ps_prolog.states.force_persp_sample_interp,
      .force_linear_sample_interp = key->ps_prolog.states.force_linear_sample_interp,
      .force_persp_center_interp = key->ps_prolog.states.force_persp_center_interp,
      .force_linear_center_interp = key->ps_prolog.states.force_linear_center_interp,

      .samplemask_log_ps_iter = key->ps_prolog.states.samplemask_log_ps_iter,
      .get_frag_coord_from_pixel_coord = key->ps_prolog.states.get_frag_coord_from_pixel_coord,
      .pixel_center_integer = key->ps_prolog.pixel_center_integer,
      .force_samplemask_to_helper_invocation = key->ps_prolog.states.force_samplemask_to_helper_invocation,
      .num_interp_inputs = key->ps_prolog.num_interp_inputs,
      .colors_read = key->ps_prolog.colors_read,
      .color_interp_vgpr_index[0] = key->ps_prolog.color_interp_vgpr_index[0],
      .color_interp_vgpr_index[1] = key->ps_prolog.color_interp_vgpr_index[1],
      .color_attr_index[0] = key->ps_prolog.color_attr_index[0],
      .color_attr_index[1] = key->ps_prolog.color_attr_index[1],
      .color_two_side = key->ps_prolog.states.color_two_side,
      .needs_wqm = key->ps_prolog.wqm,

      .internal_bindings = args.internal_bindings,
   };

   struct aco_shader_info info = {0};
   info.hw_stage = AC_HW_PIXEL_SHADER;
   info.workgroup_size = info.wave_size = key->ps_prolog.wave32 ? 32 : 64,

   aco_compile_ps_prolog(options, &info, &pinfo, &args.ac,
                         si_aco_build_shader_part_binary, (void **)result);
   return true;
}

static bool
si_aco_build_ps_epilog(struct aco_compiler_options *options,
                       struct si_shader_part *result)
{
   const union si_shader_part_key *key = &result->key;

   struct aco_ps_epilog_info pinfo = {
      .spi_shader_col_format = key->ps_epilog.states.spi_shader_col_format,
      .color_is_int8 = key->ps_epilog.states.color_is_int8,
      .color_is_int10 = key->ps_epilog.states.color_is_int10,
      .writes_all_cbufs = key->ps_epilog.writes_all_cbufs,
      .alpha_func = key->ps_epilog.states.alpha_func,
      .alpha_to_one = key->ps_epilog.states.alpha_to_one,
      .alpha_to_coverage_via_mrtz = key->ps_epilog.states.alpha_to_coverage_via_mrtz,
      .clamp_color = key->ps_epilog.states.clamp_color,
      .mrt0_is_dual_src = key->ps_epilog.states.dual_src_blend_swizzle,
      /* rbplus_depth_only_opt only affects registers, not the shader */
      .kill_depth = key->ps_epilog.states.kill_z,
      .kill_stencil = key->ps_epilog.states.kill_stencil,
      .kill_samplemask = key->ps_epilog.states.kill_samplemask,
      .skip_null_export = options->gfx_level >= GFX10 && !key->ps_epilog.uses_discard,
      .color_types = key->ps_epilog.color_types,
      .color_map = { 0, 1, 2, 3, 4, 5, 6, 7 },
   };

   struct si_shader_args args;
   si_get_ps_epilog_args(&args, key, pinfo.colors, &pinfo.depth, &pinfo.stencil,
                         &pinfo.samplemask);
   pinfo.alpha_reference = args.alpha_reference;

   struct aco_shader_info info = {0};
   info.hw_stage = AC_HW_PIXEL_SHADER;
   info.workgroup_size = info.wave_size = key->ps_epilog.wave32 ? 32 : 64,

   aco_compile_ps_epilog(options, &info, &pinfo, &args.ac,
                         si_aco_build_shader_part_binary, (void **)result);
   return true;
}

bool
si_aco_build_shader_part(struct si_screen *screen, gl_shader_stage stage, bool prolog,
                         struct util_debug_callback *debug, const char *name,
                         struct si_shader_part *result)
{
   struct aco_compiler_options options = {0};
   si_fill_aco_options(screen, stage, &options, debug);

   switch (stage) {
   case MESA_SHADER_FRAGMENT:
      if (prolog)
         return si_aco_build_ps_prolog(&options, result);
      else
         return si_aco_build_ps_epilog(&options, result);
   default:
      unreachable("bad shader part");
   }

   return false;
}
