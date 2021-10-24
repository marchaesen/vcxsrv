/*
 * Copyright © 2018 Google
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
 *
 */

#include "aco_interface.h"

#include "aco_ir.h"

#include "vulkan/radv_shader.h"
#include "vulkan/radv_shader_args.h"

#include "util/memstream.h"

#include <array>
#include <iostream>
#include <vector>

static const std::array<aco_compiler_statistic_info, aco::num_statistics> statistic_infos = []()
{
   std::array<aco_compiler_statistic_info, aco::num_statistics> ret{};
   ret[aco::statistic_hash] =
      aco_compiler_statistic_info{"Hash", "CRC32 hash of code and constant data"};
   ret[aco::statistic_instructions] =
      aco_compiler_statistic_info{"Instructions", "Instruction count"};
   ret[aco::statistic_copies] =
      aco_compiler_statistic_info{"Copies", "Copy instructions created for pseudo-instructions"};
   ret[aco::statistic_branches] = aco_compiler_statistic_info{"Branches", "Branch instructions"};
   ret[aco::statistic_latency] =
      aco_compiler_statistic_info{"Latency", "Issue cycles plus stall cycles"};
   ret[aco::statistic_inv_throughput] = aco_compiler_statistic_info{
      "Inverse Throughput", "Estimated busy cycles to execute one wave"};
   ret[aco::statistic_vmem_clauses] = aco_compiler_statistic_info{
      "VMEM Clause", "Number of VMEM clauses (includes 1-sized clauses)"};
   ret[aco::statistic_smem_clauses] = aco_compiler_statistic_info{
      "SMEM Clause", "Number of SMEM clauses (includes 1-sized clauses)"};
   ret[aco::statistic_sgpr_presched] =
      aco_compiler_statistic_info{"Pre-Sched SGPRs", "SGPR usage before scheduling"};
   ret[aco::statistic_vgpr_presched] =
      aco_compiler_statistic_info{"Pre-Sched VGPRs", "VGPR usage before scheduling"};
   return ret;
}();

const unsigned aco_num_statistics = aco::num_statistics;
const aco_compiler_statistic_info* aco_statistic_infos = statistic_infos.data();

static void
validate(aco::Program* program)
{
   if (!(aco::debug_flags & aco::DEBUG_VALIDATE_IR))
      return;

   ASSERTED bool is_valid = aco::validate_ir(program);
   assert(is_valid);
}

void
aco_compile_shader(const struct radv_nir_compiler_options* options,
                   const struct radv_shader_info* info,
                   unsigned shader_count, struct nir_shader* const* shaders,
                   const struct radv_shader_args *args,
                   struct radv_shader_binary** binary)
{
   aco::init();

   ac_shader_config config = {0};
   std::unique_ptr<aco::Program> program{new aco::Program};

   program->collect_statistics = options->record_stats;
   if (program->collect_statistics)
      memset(program->statistics, 0, sizeof(program->statistics));

   program->debug.func = options->debug.func;
   program->debug.private_data = options->debug.private_data;

   /* Instruction Selection */
   if (args->is_gs_copy_shader)
      aco::select_gs_copy_shader(program.get(), shaders[0], &config, options, info, args);
   else if (args->is_trap_handler_shader)
      aco::select_trap_handler_shader(program.get(), shaders[0], &config, options, info, args);
   else
      aco::select_program(program.get(), shader_count, shaders, &config, options, info, args);
   if (options->dump_preoptir)
      aco_print_program(program.get(), stderr);

   aco::live live_vars;
   if (!args->is_trap_handler_shader) {
      /* Phi lowering */
      aco::lower_phis(program.get());
      aco::dominator_tree(program.get());
      validate(program.get());

      /* Optimization */
      if (!options->key.optimisations_disabled) {
         if (!(aco::debug_flags & aco::DEBUG_NO_VN))
            aco::value_numbering(program.get());
         if (!(aco::debug_flags & aco::DEBUG_NO_OPT))
            aco::optimize(program.get());
      }

      /* cleanup and exec mask handling */
      aco::setup_reduce_temp(program.get());
      aco::insert_exec_mask(program.get());
      validate(program.get());

      /* spilling and scheduling */
      live_vars = aco::live_var_analysis(program.get());
      aco::spill(program.get(), live_vars);
   }

   std::string llvm_ir;
   if (options->record_ir) {
      char* data = NULL;
      size_t size = 0;
      u_memstream mem;
      if (u_memstream_open(&mem, &data, &size)) {
         FILE* const memf = u_memstream_get(&mem);
         aco_print_program(program.get(), memf);
         fputc(0, memf);
         u_memstream_close(&mem);
      }

      llvm_ir = std::string(data, data + size);
      free(data);
   }

   if (program->collect_statistics)
      aco::collect_presched_stats(program.get());

   if ((aco::debug_flags & aco::DEBUG_LIVE_INFO) && options->dump_shader)
      aco_print_program(program.get(), stderr, live_vars, aco::print_live_vars | aco::print_kill);

   if (!args->is_trap_handler_shader) {
      if (!options->key.optimisations_disabled && !(aco::debug_flags & aco::DEBUG_NO_SCHED))
         aco::schedule_program(program.get(), live_vars);
      validate(program.get());

      /* Register Allocation */
      aco::register_allocation(program.get(), live_vars.live_out);

      if (aco::validate_ra(program.get())) {
         aco_print_program(program.get(), stderr);
         abort();
      } else if (options->dump_shader) {
         aco_print_program(program.get(), stderr);
      }

      validate(program.get());

      /* Optimization */
      if (!options->key.optimisations_disabled && !(aco::debug_flags & aco::DEBUG_NO_OPT)) {
         aco::optimize_postRA(program.get());
         validate(program.get());
      }

      aco::ssa_elimination(program.get());
   }

   /* Lower to HW Instructions */
   aco::lower_to_hw_instr(program.get());

   /* Insert Waitcnt */
   aco::insert_wait_states(program.get());
   aco::insert_NOPs(program.get());

   if (program->chip_class >= GFX10)
      aco::form_hard_clauses(program.get());

   if (program->collect_statistics || (aco::debug_flags & aco::DEBUG_PERF_INFO))
      aco::collect_preasm_stats(program.get());

   /* Assembly */
   std::vector<uint32_t> code;
   unsigned exec_size = aco::emit_program(program.get(), code);

   if (program->collect_statistics)
      aco::collect_postasm_stats(program.get(), code);

   bool get_disasm = options->dump_shader || options->record_ir;

   size_t size = llvm_ir.size();

   std::string disasm;
   if (get_disasm) {
      if (check_print_asm_support(program.get())) {
         char* data = NULL;
         size_t disasm_size = 0;
         struct u_memstream mem;
         if (u_memstream_open(&mem, &data, &disasm_size)) {
            FILE* const memf = u_memstream_get(&mem);
            aco::print_asm(program.get(), code, exec_size / 4u, memf);
            fputc(0, memf);
            u_memstream_close(&mem);
         }

         disasm = std::string(data, data + disasm_size);
         size += disasm_size;
         free(data);
      } else {
         disasm = "Shader disassembly is not supported in the current configuration"
#ifndef LLVM_AVAILABLE
                  " (LLVM not available)"
#endif
                  ".\n";
         size += disasm.length();
      }
   }

   size_t stats_size = 0;
   if (program->collect_statistics)
      stats_size = aco::num_statistics * sizeof(uint32_t);
   size += stats_size;

   size += code.size() * sizeof(uint32_t) + sizeof(radv_shader_binary_legacy);
   /* We need to calloc to prevent unintialized data because this will be used
    * directly for the disk cache. Uninitialized data can appear because of
    * padding in the struct or because legacy_binary->data can be at an offset
    * from the start less than sizeof(radv_shader_binary_legacy). */
   radv_shader_binary_legacy* legacy_binary = (radv_shader_binary_legacy*)calloc(size, 1);

   legacy_binary->base.type = RADV_BINARY_TYPE_LEGACY;
   legacy_binary->base.stage = shaders[shader_count - 1]->info.stage;
   legacy_binary->base.is_gs_copy_shader = args->is_gs_copy_shader;
   legacy_binary->base.total_size = size;

   if (program->collect_statistics)
      memcpy(legacy_binary->data, program->statistics, aco::num_statistics * sizeof(uint32_t));
   legacy_binary->stats_size = stats_size;

   memcpy(legacy_binary->data + legacy_binary->stats_size, code.data(),
          code.size() * sizeof(uint32_t));
   legacy_binary->exec_size = exec_size;
   legacy_binary->code_size = code.size() * sizeof(uint32_t);

   legacy_binary->base.config = config;
   legacy_binary->disasm_size = 0;
   legacy_binary->ir_size = llvm_ir.size();

   llvm_ir.copy((char*)legacy_binary->data + legacy_binary->stats_size + legacy_binary->code_size,
                llvm_ir.size());

   if (get_disasm) {
      disasm.copy((char*)legacy_binary->data + legacy_binary->stats_size +
                     legacy_binary->code_size + llvm_ir.size(),
                  disasm.size());
      legacy_binary->disasm_size = disasm.size();
   }

   *binary = (radv_shader_binary*)legacy_binary;
}

void
aco_compile_vs_prolog(const struct radv_nir_compiler_options* options,
                      const struct radv_shader_info* info,
                      const struct radv_vs_prolog_key* key,
                      const struct radv_shader_args* args,
                      struct radv_prolog_binary** binary)
{
   aco::init();

   /* create program */
   ac_shader_config config = {0};
   std::unique_ptr<aco::Program> program{new aco::Program};
   program->collect_statistics = false;
   program->debug.func = NULL;
   program->debug.private_data = NULL;

   /* create IR */
   unsigned num_preserved_sgprs;
   aco::select_vs_prolog(program.get(), key, &config, options, info, args, &num_preserved_sgprs);
   aco::insert_NOPs(program.get());

   if (options->dump_shader)
      aco_print_program(program.get(), stderr);

   /* assembly */
   std::vector<uint32_t> code;
   code.reserve(align(program->blocks[0].instructions.size() * 2, 16));
   unsigned exec_size = aco::emit_program(program.get(), code);

   if (options->dump_shader) {
      aco::print_asm(program.get(), code, exec_size / 4u, stderr);
      fprintf(stderr, "\n");
   }

   /* copy into binary */
   size_t size = code.size() * sizeof(uint32_t) + sizeof(radv_prolog_binary);
   radv_prolog_binary* prolog_binary = (radv_prolog_binary*)calloc(size, 1);

   prolog_binary->num_sgprs = config.num_sgprs;
   prolog_binary->num_vgprs = config.num_vgprs;
   prolog_binary->num_preserved_sgprs = num_preserved_sgprs;
   prolog_binary->code_size = code.size() * sizeof(uint32_t);
   memcpy(prolog_binary->data, code.data(), prolog_binary->code_size);

   *binary = prolog_binary;
}
