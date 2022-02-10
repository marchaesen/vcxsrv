/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2019 Collabora LTD
 *
 * Author: Gert Wollny <gert.wollny@collabora.com>
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

#include "sfn_nir.h"
#include "nir_builder.h"

#include "../r600_pipe.h"
#include "../r600_shader.h"

#include "util/u_prim.h"

#include "sfn_instruction_tex.h"

#include "sfn_shader_vertex.h"
#include "sfn_shader_fragment.h"
#include "sfn_shader_geometry.h"
#include "sfn_shader_compute.h"
#include "sfn_shader_tcs.h"
#include "sfn_shader_tess_eval.h"
#include "sfn_nir_lower_fs_out_to_vector.h"
#include "sfn_ir_to_assembly.h"
#include "sfn_nir_lower_alu.h"

#include <vector>

namespace r600 {

using std::vector;


NirLowerInstruction::NirLowerInstruction():
	b(nullptr)
{

}

bool NirLowerInstruction::filter_instr(const nir_instr *instr, const void *data)
{
   auto me = reinterpret_cast<const NirLowerInstruction*>(data);
   return me->filter(instr);
}

nir_ssa_def *NirLowerInstruction::lower_instr(nir_builder *b, nir_instr *instr, void *data)
{
   auto me = reinterpret_cast<NirLowerInstruction*>(data);
   me->set_builder(b);
   return me->lower(instr);
}

bool NirLowerInstruction::run(nir_shader *shader)
{
   return nir_shader_lower_instructions(shader,
                                        filter_instr,
                                        lower_instr,
                                        (void *)this);
}


ShaderFromNir::ShaderFromNir():sh(nullptr),
   chip_class(CLASS_UNKNOWN),
   m_current_if_id(0),
   m_current_loop_id(0),
   scratch_size(0)
{
}

bool ShaderFromNir::lower(const nir_shader *shader, r600_pipe_shader *pipe_shader,
                          r600_pipe_shader_selector *sel, r600_shader_key& key,
                          struct r600_shader* gs_shader, enum chip_class _chip_class)
{
   sh = shader;
   chip_class = _chip_class;
   assert(sh);

   switch (shader->info.stage) {
   case MESA_SHADER_VERTEX:
      impl.reset(new VertexShaderFromNir(pipe_shader, *sel, key, gs_shader, chip_class));
      break;
   case MESA_SHADER_TESS_CTRL:
      sfn_log << SfnLog::trans << "Start TCS\n";
      impl.reset(new TcsShaderFromNir(pipe_shader, *sel, key, chip_class));
      break;
   case MESA_SHADER_TESS_EVAL:
      sfn_log << SfnLog::trans << "Start TESS_EVAL\n";
      impl.reset(new TEvalShaderFromNir(pipe_shader, *sel, key, gs_shader, chip_class));
      break;
   case MESA_SHADER_GEOMETRY:
      sfn_log << SfnLog::trans << "Start GS\n";
      impl.reset(new GeometryShaderFromNir(pipe_shader, *sel, key, chip_class));
      break;
   case MESA_SHADER_FRAGMENT:
      sfn_log << SfnLog::trans << "Start FS\n";
      impl.reset(new FragmentShaderFromNir(*shader, pipe_shader->shader, *sel, key, chip_class));
      break;
   case MESA_SHADER_COMPUTE:
      sfn_log << SfnLog::trans << "Start CS\n";
      impl.reset(new ComputeShaderFromNir(pipe_shader, *sel, key, chip_class));
      break;
   default:
      return false;
   }

   sfn_log << SfnLog::trans << "Process declarations\n";
   if (!process_declaration())
      return false;

   // at this point all functions should be inlined
   const nir_function *func = reinterpret_cast<const nir_function *>(exec_list_get_head_const(&sh->functions));

   sfn_log << SfnLog::trans << "Scan shader\n";

   if (sfn_log.has_debug_flag(SfnLog::instr))
      nir_print_shader(const_cast<nir_shader *>(shader), stderr);

   nir_foreach_block(block, func->impl) {
      nir_foreach_instr(instr, block) {
         if (!impl->scan_instruction(instr)) {
            fprintf(stderr, "Unhandled sysvalue access ");
            nir_print_instr(instr, stderr);
            fprintf(stderr, "\n");
            return false;
         }
      }
   }

   sfn_log << SfnLog::trans << "Reserve registers\n";
   if (!impl->allocate_reserved_registers()) {
      return false;
   }

   ValuePool::array_list arrays;
   sfn_log << SfnLog::trans << "Allocate local registers\n";
   foreach_list_typed(nir_register, reg, node, &func->impl->registers) {
      impl->allocate_local_register(*reg, arrays);
   }

   sfn_log << SfnLog::trans << "Emit shader start\n";
   impl->allocate_arrays(arrays);

   impl->emit_shader_start();

   sfn_log << SfnLog::trans << "Process shader \n";
   foreach_list_typed(nir_cf_node, node, node, &func->impl->body) {
      if (!process_cf_node(node))
         return false;
   }

   // Add optimizations here
   sfn_log << SfnLog::trans << "Finalize\n";
   impl->finalize();

   impl->get_array_info(pipe_shader->shader);

   if (!sfn_log.has_debug_flag(SfnLog::nomerge)) {
      sfn_log << SfnLog::trans << "Merge registers\n";
      impl->remap_registers();
   }

   sfn_log << SfnLog::trans << "Finished translating to R600 IR\n";
   return true;
}

Shader ShaderFromNir::shader() const
{
   return Shader{impl->m_output, impl->get_temp_registers()};
}


bool ShaderFromNir::process_cf_node(nir_cf_node *node)
{
   SFN_TRACE_FUNC(SfnLog::flow, "CF");
   switch (node->type) {
   case nir_cf_node_block:
      return process_block(nir_cf_node_as_block(node));
   case nir_cf_node_if:
      return process_if(nir_cf_node_as_if(node));
   case nir_cf_node_loop:
      return process_loop(nir_cf_node_as_loop(node));
   default:
      return false;
   }
}

bool ShaderFromNir::process_if(nir_if *if_stmt)
{
   SFN_TRACE_FUNC(SfnLog::flow, "IF");

   if (!impl->emit_if_start(m_current_if_id, if_stmt))
      return false;

   int if_id = m_current_if_id++;
   m_if_stack.push(if_id);

   foreach_list_typed(nir_cf_node, n, node, &if_stmt->then_list)
         if (!process_cf_node(n)) return false;

   if (!if_stmt->then_list.is_empty()) {
      if (!impl->emit_else_start(if_id))
         return false;

      foreach_list_typed(nir_cf_node, n, node, &if_stmt->else_list)
            if (!process_cf_node(n)) return false;
   }

   if (!impl->emit_ifelse_end(if_id))
      return false;

   m_if_stack.pop();
   return true;
}

bool ShaderFromNir::process_loop(nir_loop *node)
{
   SFN_TRACE_FUNC(SfnLog::flow, "LOOP");
   int loop_id = m_current_loop_id++;

   if (!impl->emit_loop_start(loop_id))
      return false;

   foreach_list_typed(nir_cf_node, n, node, &node->body)
         if (!process_cf_node(n)) return false;

   if (!impl->emit_loop_end(loop_id))
      return false;

   return true;
}

bool ShaderFromNir::process_block(nir_block *block)
{
   SFN_TRACE_FUNC(SfnLog::flow, "BLOCK");
   nir_foreach_instr(instr, block) {
      int r = emit_instruction(instr);
      if (!r) {
         sfn_log << SfnLog::err << "R600: Unsupported instruction: "
                 << *instr << "\n";
         return false;
      }
   }
   return true;
}


ShaderFromNir::~ShaderFromNir()
{
}

pipe_shader_type ShaderFromNir::processor_type() const
{
   return impl->m_processor_type;
}


bool ShaderFromNir::emit_instruction(nir_instr *instr)
{
   assert(impl);

   sfn_log << SfnLog::instr << "Read instruction " << *instr << "\n";

   switch (instr->type) {
   case nir_instr_type_alu:
      return impl->emit_alu_instruction(instr);
   case nir_instr_type_deref:
      return impl->emit_deref_instruction(nir_instr_as_deref(instr));
   case nir_instr_type_intrinsic:
      return impl->emit_intrinsic_instruction(nir_instr_as_intrinsic(instr));
   case nir_instr_type_load_const: /* const values are loaded when needed */
      return true;
   case nir_instr_type_tex:
      return impl->emit_tex_instruction(instr);
   case nir_instr_type_jump:
      return impl->emit_jump_instruction(nir_instr_as_jump(instr));
   default:
      fprintf(stderr, "R600: %s: ShaderFromNir Unsupported instruction: type %d:'", __func__, instr->type);
      nir_print_instr(instr, stderr);
      fprintf(stderr, "'\n");
      return false;
   case nir_instr_type_ssa_undef:
      return impl->create_undef(nir_instr_as_ssa_undef(instr));
      return true;
   }
}

bool ShaderFromNir::process_declaration()
{
   impl->set_shader_info(sh);

   if (!impl->scan_inputs_read(sh))
      return false;

   // scan declarations
   nir_foreach_variable_with_modes(variable, sh, nir_var_uniform |
                                                 nir_var_mem_ubo |
                                                 nir_var_mem_ssbo) {
      if (!impl->process_uniforms(variable)) {
         fprintf(stderr, "R600: error parsing outputs variable %s\n", variable->name);
         return false;
      }
   }

   return true;
}

const std::vector<InstructionBlock>& ShaderFromNir::shader_ir() const
{
   assert(impl);
   return impl->m_output;
}


AssemblyFromShader::~AssemblyFromShader()
{
}

bool AssemblyFromShader::lower(const std::vector<InstructionBlock>& ir)
{
   return do_lower(ir);
}

static void
r600_nir_lower_scratch_address_impl(nir_builder *b, nir_intrinsic_instr *instr)
{
   b->cursor = nir_before_instr(&instr->instr);

   int address_index = 0;
   int align;

   if (instr->intrinsic == nir_intrinsic_store_scratch) {
      align  = instr->src[0].ssa->num_components;
      address_index = 1;
   } else{
      align = instr->dest.ssa.num_components;
   }

   nir_ssa_def *address = instr->src[address_index].ssa;
   nir_ssa_def *new_address = nir_ishr(b, address,  nir_imm_int(b, 4 * align));

   nir_instr_rewrite_src(&instr->instr, &instr->src[address_index],
                         nir_src_for_ssa(new_address));
}

bool r600_lower_scratch_addresses(nir_shader *shader)
{
   bool progress = false;
   nir_foreach_function(function, shader) {
      nir_builder build;
      nir_builder_init(&build, function->impl);

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *op = nir_instr_as_intrinsic(instr);
            if (op->intrinsic != nir_intrinsic_load_scratch &&
                op->intrinsic != nir_intrinsic_store_scratch)
               continue;
            r600_nir_lower_scratch_address_impl(&build, op);
            progress = true;
         }
      }
   }
   return progress;
}

static void
insert_uniform_sorted(struct exec_list *var_list, nir_variable *new_var)
{
   nir_foreach_variable_in_list(var, var_list) {
      if (var->data.binding > new_var->data.binding ||
          (var->data.binding == new_var->data.binding &&
           var->data.offset > new_var->data.offset)) {
         exec_node_insert_node_before(&var->node, &new_var->node);
         return;
      }
   }
   exec_list_push_tail(var_list, &new_var->node);
}

void sort_uniforms(nir_shader *shader)
{
   struct exec_list new_list;
   exec_list_make_empty(&new_list);

   nir_foreach_uniform_variable_safe(var, shader) {
      exec_node_remove(&var->node);
      insert_uniform_sorted(&new_list, var);
   }
   exec_list_append(&shader->variables, &new_list);
}

static void
insert_fsoutput_sorted(struct exec_list *var_list, nir_variable *new_var)
{

   nir_foreach_variable_in_list(var, var_list) {
      if (var->data.location > new_var->data.location ||
          (var->data.location == new_var->data.location &&
           var->data.index > new_var->data.index)) {
         exec_node_insert_node_before(&var->node, &new_var->node);
         return;
      }
   }

   exec_list_push_tail(var_list, &new_var->node);
}

void sort_fsoutput(nir_shader *shader)
{
   struct exec_list new_list;
   exec_list_make_empty(&new_list);

   nir_foreach_shader_out_variable_safe(var, shader) {
      exec_node_remove(&var->node);
      insert_fsoutput_sorted(&new_list, var);
   }

   unsigned driver_location = 0;
   nir_foreach_variable_in_list(var, &new_list)
      var->data.driver_location = driver_location++;

   exec_list_append(&shader->variables, &new_list);
}

}

static nir_intrinsic_op
r600_map_atomic(nir_intrinsic_op op)
{
   switch (op) {
   case nir_intrinsic_atomic_counter_read_deref:
      return nir_intrinsic_atomic_counter_read;
   case nir_intrinsic_atomic_counter_inc_deref:
      return nir_intrinsic_atomic_counter_inc;
   case nir_intrinsic_atomic_counter_pre_dec_deref:
      return nir_intrinsic_atomic_counter_pre_dec;
   case nir_intrinsic_atomic_counter_post_dec_deref:
      return nir_intrinsic_atomic_counter_post_dec;
   case nir_intrinsic_atomic_counter_add_deref:
      return nir_intrinsic_atomic_counter_add;
   case nir_intrinsic_atomic_counter_min_deref:
      return nir_intrinsic_atomic_counter_min;
   case nir_intrinsic_atomic_counter_max_deref:
      return nir_intrinsic_atomic_counter_max;
   case nir_intrinsic_atomic_counter_and_deref:
      return nir_intrinsic_atomic_counter_and;
   case nir_intrinsic_atomic_counter_or_deref:
      return nir_intrinsic_atomic_counter_or;
   case nir_intrinsic_atomic_counter_xor_deref:
      return nir_intrinsic_atomic_counter_xor;
   case nir_intrinsic_atomic_counter_exchange_deref:
      return nir_intrinsic_atomic_counter_exchange;
   case nir_intrinsic_atomic_counter_comp_swap_deref:
      return nir_intrinsic_atomic_counter_comp_swap;
   default:
      return nir_num_intrinsics;
   }
}

static bool
r600_lower_deref_instr(nir_builder *b, nir_instr *instr_, UNUSED void *cb_data)
{
   if (instr_->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *instr = nir_instr_as_intrinsic(instr_);

   nir_intrinsic_op op = r600_map_atomic(instr->intrinsic);
   if (nir_num_intrinsics == op)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(instr->src[0]);
   nir_variable *var = nir_deref_instr_get_variable(deref);

   if (var->data.mode != nir_var_uniform &&
       var->data.mode != nir_var_mem_ssbo &&
       var->data.mode != nir_var_mem_shared)
      return false; /* atomics passed as function arguments can't be lowered */

   const unsigned idx = var->data.binding;

   b->cursor = nir_before_instr(&instr->instr);

   nir_ssa_def *offset = nir_imm_int(b, var->data.index);
   for (nir_deref_instr *d = deref; d->deref_type != nir_deref_type_var;
        d = nir_deref_instr_parent(d)) {
      assert(d->deref_type == nir_deref_type_array);
      assert(d->arr.index.is_ssa);

      unsigned array_stride = 1;
      if (glsl_type_is_array(d->type))
         array_stride *= glsl_get_aoa_size(d->type);

      offset = nir_iadd(b, offset, nir_imul(b, d->arr.index.ssa,
                                            nir_imm_int(b, array_stride)));
   }

   /* Since the first source is a deref and the first source in the lowered
    * instruction is the offset, we can just swap it out and change the
    * opcode.
    */
   instr->intrinsic = op;
   nir_instr_rewrite_src(&instr->instr, &instr->src[0],
                         nir_src_for_ssa(offset));
   nir_intrinsic_set_base(instr, idx);

   nir_deref_instr_remove_if_unused(deref);

   return true;
}

static bool
r600_nir_lower_atomics(nir_shader *shader)
{
   /* First re-do the offsets, in Hardware we start at zero for each new
    * binding, and we use an offset of one per counter */
   int current_binding = -1;
   int current_offset = 0;
   nir_foreach_variable_with_modes(var, shader, nir_var_uniform) {
      if (!var->type->contains_atomic())
         continue;

      if (current_binding == (int)var->data.binding) {
         var->data.index = current_offset;
         current_offset += var->type->atomic_size() / ATOMIC_COUNTER_SIZE;
      } else {
         current_binding = var->data.binding;
         var->data.index = 0;
         current_offset = var->type->atomic_size() / ATOMIC_COUNTER_SIZE;
      }
   }

   return nir_shader_instructions_pass(shader, r600_lower_deref_instr,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance,
                                       NULL);
}
using r600::r600_nir_lower_int_tg4;
using r600::r600_lower_scratch_addresses;
using r600::r600_lower_fs_out_to_vector;
using r600::r600_lower_ubo_to_align16;

int
r600_glsl_type_size(const struct glsl_type *type, bool is_bindless)
{
   return glsl_count_vec4_slots(type, false, is_bindless);
}

void
r600_get_natural_size_align_bytes(const struct glsl_type *type,
                                  unsigned *size, unsigned *align)
{
   if (type->base_type != GLSL_TYPE_ARRAY) {
      *align = 1;
      *size = 1;
   } else {
      unsigned elem_size, elem_align;
      glsl_get_natural_size_align_bytes(type->fields.array,
                                        &elem_size, &elem_align);
      *align = 1;
      *size = type->length;
   }
}

static bool
r600_lower_shared_io_impl(nir_function *func)
{
   nir_builder b;
   nir_builder_init(&b, func->impl);

   bool progress = false;
   nir_foreach_block(block, func->impl) {
      nir_foreach_instr_safe(instr, block) {

         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *op = nir_instr_as_intrinsic(instr);
         if (op->intrinsic != nir_intrinsic_load_shared &&
             op->intrinsic != nir_intrinsic_store_shared)
            continue;

         b.cursor = nir_before_instr(instr);

         if (op->intrinsic == nir_intrinsic_load_shared) {
            nir_ssa_def *addr = op->src[0].ssa;

            switch (nir_dest_num_components(op->dest)) {
            case 2: {
               auto addr2 = nir_iadd_imm(&b, addr, 4);
               addr = nir_vec2(&b, addr, addr2);
               break;
            }
            case 3: {
               auto addr2 = nir_iadd(&b, addr, nir_imm_ivec2(&b, 4, 8));
               addr = nir_vec3(&b, addr,
                               nir_channel(&b, addr2, 0),
                               nir_channel(&b, addr2, 1));
               break;
            }
            case 4: {
               addr = nir_iadd(&b, addr, nir_imm_ivec4(&b, 0, 4, 8, 12));
               break;
            }
            }

            auto load = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_local_shared_r600);
            load->num_components = nir_dest_num_components(op->dest);
            load->src[0] = nir_src_for_ssa(addr);
            nir_ssa_dest_init(&load->instr, &load->dest,
                              load->num_components, 32, NULL);
            nir_ssa_def_rewrite_uses(&op->dest.ssa, &load->dest.ssa);
            nir_builder_instr_insert(&b, &load->instr);
         } else {
            nir_ssa_def *addr = op->src[1].ssa;
            for (int i = 0; i < 2; ++i) {
               unsigned test_mask = (0x3 << 2 * i);
               if (!(nir_intrinsic_write_mask(op) & test_mask))
                  continue;

               auto store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_local_shared_r600);
               unsigned writemask = nir_intrinsic_write_mask(op) & test_mask;
               nir_intrinsic_set_write_mask(store, writemask);
               store->src[0] = nir_src_for_ssa(op->src[0].ssa);
               store->num_components = store->src[0].ssa->num_components;
               bool start_even = (writemask & (1u << (2 * i)));

               auto addr2 = nir_iadd(&b, addr, nir_imm_int(&b, 8 * i + (start_even ? 0 : 4)));
               store->src[1] = nir_src_for_ssa(addr2);

               nir_builder_instr_insert(&b, &store->instr);
            }
         }
         nir_instr_remove(instr);
         progress = true;
      }
   }
   return progress;
}

static bool
r600_lower_shared_io(nir_shader *nir)
{
	bool progress=false;
	nir_foreach_function(function, nir) {
		if (function->impl &&
			 r600_lower_shared_io_impl(function))
			progress = true;
	}
	return progress;
}


static nir_ssa_def *
r600_lower_fs_pos_input_impl(nir_builder *b, nir_instr *instr, void *_options)
{
   auto old_ir = nir_instr_as_intrinsic(instr);
   auto load = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_input);
   nir_ssa_dest_init(&load->instr, &load->dest,
                     old_ir->dest.ssa.num_components, old_ir->dest.ssa.bit_size, NULL);
   nir_intrinsic_set_io_semantics(load, nir_intrinsic_io_semantics(old_ir));

   nir_intrinsic_set_base(load, nir_intrinsic_base(old_ir));
   nir_intrinsic_set_component(load, nir_intrinsic_component(old_ir));
   nir_intrinsic_set_dest_type(load, nir_type_float32);
   load->num_components = old_ir->num_components;
   load->src[0] = old_ir->src[1];
   nir_builder_instr_insert(b, &load->instr);
   return &load->dest.ssa;
}

bool r600_lower_fs_pos_input_filter(const nir_instr *instr, const void *_options)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   auto ir = nir_instr_as_intrinsic(instr);
   if (ir->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   return nir_intrinsic_io_semantics(ir).location == VARYING_SLOT_POS;
}

/* Strip the interpolator specification, it is not needed and irritates */
bool r600_lower_fs_pos_input(nir_shader *shader)
{
   return nir_shader_lower_instructions(shader,
                                        r600_lower_fs_pos_input_filter,
                                        r600_lower_fs_pos_input_impl,
                                        nullptr);
};

static bool
optimize_once(nir_shader *shader, bool vectorize)
{
   bool progress = false;
   NIR_PASS(progress, shader, nir_lower_vars_to_ssa);
   NIR_PASS(progress, shader, nir_copy_prop);
   NIR_PASS(progress, shader, nir_opt_dce);
   NIR_PASS(progress, shader, nir_opt_algebraic);
   NIR_PASS(progress, shader, nir_opt_constant_folding);
   NIR_PASS(progress, shader, nir_opt_copy_prop_vars);
   if (vectorize)
      NIR_PASS(progress, shader, nir_opt_vectorize, NULL, NULL);

   NIR_PASS(progress, shader, nir_opt_remove_phis);

   if (nir_opt_trivial_continues(shader)) {
           progress = true;
           NIR_PASS(progress, shader, nir_copy_prop);
           NIR_PASS(progress, shader, nir_opt_dce);
   }

   NIR_PASS(progress, shader, nir_opt_if, false);
   NIR_PASS(progress, shader, nir_opt_dead_cf);
   NIR_PASS(progress, shader, nir_opt_cse);
   NIR_PASS(progress, shader, nir_opt_peephole_select, 200, true, true);

   NIR_PASS(progress, shader, nir_opt_conditional_discard);
   NIR_PASS(progress, shader, nir_opt_dce);
   NIR_PASS(progress, shader, nir_opt_undef);
   return progress;
}

bool has_saturate(const nir_function *func)
{
   nir_foreach_block(block, func->impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_alu) {
            auto alu = nir_instr_as_alu(instr);
            if (alu->dest.saturate)
               return true;
         }
      }
   }
   return false;
}

extern "C"
bool r600_lower_to_scalar_instr_filter(const nir_instr *instr, const void *)
{
   if (instr->type != nir_instr_type_alu)
      return true;

   auto alu = nir_instr_as_alu(instr);
   switch (alu->op) {
   case nir_op_bany_fnequal3:
   case nir_op_bany_fnequal4:
   case nir_op_ball_fequal3:
   case nir_op_ball_fequal4:
   case nir_op_bany_inequal3:
   case nir_op_bany_inequal4:
   case nir_op_ball_iequal3:
   case nir_op_ball_iequal4:
   case nir_op_fdot2:
   case nir_op_fdot3:
   case nir_op_fdot4:
   case nir_op_cube_r600:
      return false;
   case nir_op_bany_fnequal2:
   case nir_op_ball_fequal2:
   case nir_op_bany_inequal2:
   case nir_op_ball_iequal2:
      return nir_src_bit_size(alu->src[0].src) != 64;
   default:
      return true;
   }
}

int r600_shader_from_nir(struct r600_context *rctx,
                         struct r600_pipe_shader *pipeshader,
                         r600_shader_key *key)
{
   char filename[4000];
   struct r600_pipe_shader_selector *sel = pipeshader->selector;

   bool lower_64bit = ((sel->nir->options->lower_int64_options ||
                        sel->nir->options->lower_doubles_options) &&
                       (sel->nir->info.bit_sizes_float | sel->nir->info.bit_sizes_int) & 64);

   r600::ShaderFromNir convert;

   if (rctx->screen->b.debug_flags & DBG_PREOPT_IR) {
      fprintf(stderr, "PRE-OPT-NIR-----------.------------------------------\n");
      nir_print_shader(sel->nir, stderr);
      fprintf(stderr, "END PRE-OPT-NIR--------------------------------------\n\n");
   }

   r600::sort_uniforms(sel->nir);

   /* Cayman seems very crashy about accessing images that don't exists or are
    * accessed out of range, this lowering seems to help (but it can also be
    * another problem */
   if (sel->nir->info.num_images > 0 && rctx->b.chip_class == CAYMAN)
       NIR_PASS_V(sel->nir, r600_legalize_image_load_store);

   NIR_PASS_V(sel->nir, nir_lower_vars_to_ssa);
   NIR_PASS_V(sel->nir, nir_lower_regs_to_ssa);
   nir_lower_idiv_options idiv_options = {0};
   idiv_options.imprecise_32bit_lowering = sel->nir->info.stage != MESA_SHADER_COMPUTE;
   idiv_options.allow_fp16 = true;

   NIR_PASS_V(sel->nir, nir_lower_idiv, &idiv_options);
   NIR_PASS_V(sel->nir, r600_nir_lower_trigen);
   NIR_PASS_V(sel->nir, nir_lower_phis_to_scalar, false);

   if (lower_64bit)
      NIR_PASS_V(sel->nir, nir_lower_int64);
   while(optimize_once(sel->nir, false));

   NIR_PASS_V(sel->nir, r600_lower_shared_io);
   NIR_PASS_V(sel->nir, r600_nir_lower_atomics);

   struct nir_lower_tex_options lower_tex_options = {0};
   lower_tex_options.lower_txp = ~0u;
   lower_tex_options.lower_txf_offset = true;

   NIR_PASS_V(sel->nir, nir_lower_tex, &lower_tex_options);
   NIR_PASS_V(sel->nir, r600::r600_nir_lower_txl_txf_array_or_cube);
   NIR_PASS_V(sel->nir, r600::r600_nir_lower_cube_to_2darray);

   NIR_PASS_V(sel->nir, r600_nir_lower_pack_unpack_2x16);

   if (sel->nir->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS_V(sel->nir, r600_vectorize_vs_inputs);

   if (sel->nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS_V(sel->nir, nir_lower_fragcoord_wtrans);
      NIR_PASS_V(sel->nir, r600_lower_fs_out_to_vector);
   }

   nir_variable_mode io_modes = nir_var_uniform | nir_var_shader_in;

   //if (sel->nir->info.stage != MESA_SHADER_FRAGMENT)
      io_modes |= nir_var_shader_out;

   if (sel->nir->info.stage == MESA_SHADER_FRAGMENT) {

      /* Lower IO to temporaries late, because otherwise we get into trouble
       * with the glsl 4.40 interpolateAt swizzle tests. There seems to be a bug
       * somewhere that results in the input alweas reading from the same temp
       * regardless of interpolation when the lowering is done early */
      NIR_PASS_V(sel->nir, nir_lower_io_to_temporaries, nir_shader_get_entrypoint(sel->nir),
              true, true);

      /* Since we're doing nir_lower_io_to_temporaries late, we need
       * to lower all the copy_deref's introduced by
       * lower_io_to_temporaries before calling nir_lower_io.
       */
      NIR_PASS_V(sel->nir, nir_split_var_copies);
      NIR_PASS_V(sel->nir, nir_lower_var_copies);
      NIR_PASS_V(sel->nir, nir_lower_global_vars_to_local);
   }

   NIR_PASS_V(sel->nir, nir_lower_io, io_modes, r600_glsl_type_size,
                 nir_lower_io_lower_64bit_to_32);

   if (sel->nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS_V(sel->nir, r600_lower_fs_pos_input);

   /**/
   if (lower_64bit)
      NIR_PASS_V(sel->nir, nir_lower_indirect_derefs, nir_var_function_temp, 10);

   NIR_PASS_V(sel->nir, nir_opt_constant_folding);
   NIR_PASS_V(sel->nir, nir_io_add_const_offset_to_base, io_modes);

   NIR_PASS_V(sel->nir, nir_lower_alu_to_scalar, r600_lower_to_scalar_instr_filter, NULL);
   NIR_PASS_V(sel->nir, nir_lower_phis_to_scalar, false);
   if (lower_64bit)
      NIR_PASS_V(sel->nir, r600::r600_nir_split_64bit_io);
   NIR_PASS_V(sel->nir, nir_lower_alu_to_scalar, r600_lower_to_scalar_instr_filter, NULL);
   NIR_PASS_V(sel->nir, nir_lower_phis_to_scalar, false);
   NIR_PASS_V(sel->nir, nir_lower_alu_to_scalar, r600_lower_to_scalar_instr_filter, NULL);
   NIR_PASS_V(sel->nir, nir_copy_prop);
   NIR_PASS_V(sel->nir, nir_opt_dce);

   auto sh = nir_shader_clone(sel->nir, sel->nir);

   if (sh->info.stage == MESA_SHADER_TESS_CTRL ||
       sh->info.stage == MESA_SHADER_TESS_EVAL ||
       (sh->info.stage == MESA_SHADER_VERTEX && key->vs.as_ls)) {
      auto prim_type = sh->info.stage == MESA_SHADER_TESS_EVAL ?
	 u_tess_prim_from_shader(sh->info.tess._primitive_mode) : key->tcs.prim_mode;
      NIR_PASS_V(sh, r600_lower_tess_io, static_cast<pipe_prim_type>(prim_type));
   }

   if (sh->info.stage == MESA_SHADER_TESS_CTRL)
      NIR_PASS_V(sh, r600_append_tcs_TF_emission,
                 (pipe_prim_type)key->tcs.prim_mode);

   if (sh->info.stage == MESA_SHADER_TESS_EVAL) {
      NIR_PASS_V(sh, r600_lower_tess_coord, u_tess_prim_from_shader(sh->info.tess._primitive_mode));
   }

   NIR_PASS_V(sh, nir_lower_ubo_vec4);
   if (lower_64bit)
      NIR_PASS_V(sh, r600::r600_nir_64_to_vec2);

   /* Lower to scalar to let some optimization work out better */
   while(optimize_once(sh, false));

   NIR_PASS_V(sh, r600::r600_merge_vec2_stores);

   NIR_PASS_V(sh, nir_remove_dead_variables, nir_var_shader_in, NULL);
   NIR_PASS_V(sh, nir_remove_dead_variables,  nir_var_shader_out, NULL);


   NIR_PASS_V(sh, nir_lower_vars_to_scratch,
              nir_var_function_temp,
              40,
              r600_get_natural_size_align_bytes);

   while (optimize_once(sh, true));

   NIR_PASS_V(sh, nir_lower_bool_to_int32);
   NIR_PASS_V(sh, r600_nir_lower_int_tg4);
   NIR_PASS_V(sh, nir_opt_algebraic_late);

   if (sh->info.stage == MESA_SHADER_FRAGMENT)
      r600::sort_fsoutput(sh);

   NIR_PASS_V(sh, nir_lower_locals_to_regs);

   //NIR_PASS_V(sh, nir_opt_algebraic);
   //NIR_PASS_V(sh, nir_copy_prop);
   NIR_PASS_V(sh, nir_lower_to_source_mods,
	      (nir_lower_to_source_mods_flags)(nir_lower_float_source_mods |
					       nir_lower_64bit_source_mods));
   NIR_PASS_V(sh, nir_convert_from_ssa, true);
   NIR_PASS_V(sh, nir_opt_dce);

   if ((rctx->screen->b.debug_flags & DBG_NIR_PREFERRED) &&
       (rctx->screen->b.debug_flags & DBG_ALL_SHADERS)) {
      fprintf(stderr, "-- NIR --------------------------------------------------------\n");
      struct nir_function *func = (struct nir_function *)exec_list_get_head(&sh->functions);
      nir_index_ssa_defs(func->impl);
      nir_print_shader(sh, stderr);
      fprintf(stderr, "-- END --------------------------------------------------------\n");
   }

   memset(&pipeshader->shader, 0, sizeof(r600_shader));
   pipeshader->scratch_space_needed = sh->scratch_size;

   if (sh->info.stage == MESA_SHADER_TESS_EVAL ||
       sh->info.stage == MESA_SHADER_VERTEX ||
       sh->info.stage == MESA_SHADER_GEOMETRY) {
      pipeshader->shader.clip_dist_write |= ((1 << sh->info.clip_distance_array_size) - 1);
      pipeshader->shader.cull_dist_write = ((1 << sh->info.cull_distance_array_size) - 1)
                                           << sh->info.clip_distance_array_size;
      pipeshader->shader.cc_dist_mask = (1 <<  (sh->info.cull_distance_array_size +
                                                sh->info.clip_distance_array_size)) - 1;
   }

   struct r600_shader* gs_shader = nullptr;
   if (rctx->gs_shader)
      gs_shader = &rctx->gs_shader->current->shader;
   r600_screen *rscreen = rctx->screen;

   bool r = convert.lower(sh, pipeshader, sel, *key, gs_shader, rscreen->b.chip_class);
   if (!r || rctx->screen->b.debug_flags & DBG_ALL_SHADERS) {
      static int shnr = 0;

      snprintf(filename, 4000, "nir-%s_%d.inc", sh->info.name, shnr++);

      if (access(filename, F_OK) == -1) {
         FILE *f = fopen(filename, "w");

         if (f) {
            fprintf(f, "const char *shader_blob_%s = {\nR\"(", sh->info.name);
            nir_print_shader(sh, f);
            fprintf(f, ")\";\n");
            fclose(f);
         }
      }
      if (!r)
         return -2;
   }

   auto shader = convert.shader();

   r600_bytecode_init(&pipeshader->shader.bc, rscreen->b.chip_class, rscreen->b.family,
                      rscreen->has_compressed_msaa_texturing);

   r600::sfn_log << r600::SfnLog::shader_info
                 << "pipeshader->shader.processor_type = "
                 << pipeshader->shader.processor_type << "\n";

   pipeshader->shader.bc.type = pipeshader->shader.processor_type;
   pipeshader->shader.bc.isa = rctx->isa;

   r600::AssemblyFromShaderLegacy afs(&pipeshader->shader, key);
   if (!afs.lower(shader.m_ir)) {
      R600_ERR("%s: Lowering to assembly failed\n", __func__);
      return -1;
   }

   if (sh->info.stage == MESA_SHADER_GEOMETRY) {
      r600::sfn_log << r600::SfnLog::shader_info << "Geometry shader, create copy shader\n";
      generate_gs_copy_shader(rctx, pipeshader, &sel->so);
      assert(pipeshader->gs_copy_shader);
   } else {
      r600::sfn_log << r600::SfnLog::shader_info << "This is not a Geometry shader\n";
   }
   if (pipeshader->shader.bc.ngpr < 6)
      pipeshader->shader.bc.ngpr = 6;

   return 0;
}
