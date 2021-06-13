/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2018 Collabora LTD
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


#include "pipe/p_defines.h"
#include "tgsi/tgsi_from_mesa.h"
#include "sfn_shader_vertex.h"
#include "sfn_instruction_lds.h"

#include <queue>


namespace r600 {

using std::priority_queue;

VertexShaderFromNir::VertexShaderFromNir(r600_pipe_shader *sh,
                                         r600_pipe_shader_selector& sel,
                                         const r600_shader_key& key,
                                         struct r600_shader* gs_shader,
                                         enum chip_class chip_class):
   VertexStage(PIPE_SHADER_VERTEX, sel, sh->shader,
               sh->scratch_space_needed, chip_class, key.vs.first_atomic_counter),
   m_num_clip_dist(0),
   m_last_param_export(nullptr),
   m_last_pos_export(nullptr),
   m_pipe_shader(sh),
   m_enabled_stream_buffers_mask(0),
   m_so_info(&sel.so),
   m_vertex_id(),
   m_key(key),
   m_max_attrib(0)
{
   // reg 0 is used in the fetch shader
   increment_reserved_registers();

   sh_info().atomic_base = key.vs.first_atomic_counter;
   sh_info().vs_as_gs_a = m_key.vs.as_gs_a;

   if (key.vs.as_es) {
      sh->shader.vs_as_es = true;
      m_export_processor.reset(new VertexStageExportForGS(*this, gs_shader));
   } else if (key.vs.as_ls) {
      sh->shader.vs_as_ls = true;
      sfn_log << SfnLog::trans << "Start VS for GS\n";
      m_export_processor.reset(new VertexStageExportForES(*this));
   } else {
      m_export_processor.reset(new VertexStageExportForFS(*this, &sel.so, sh, key));
   }
}

bool VertexShaderFromNir::scan_inputs_read(const nir_shader *sh)
{
   uint64_t inputs = sh->info.inputs_read;

   while (inputs) {
      unsigned i = u_bit_scan64(&inputs);
      if (i < VERT_ATTRIB_MAX) {
         ++sh_info().ninput;
      }
   }
   m_max_attrib = sh_info().ninput;
   return true;
}

bool VertexShaderFromNir::do_allocate_reserved_registers()
{
   /* Since the vertex ID is nearly always used, we add it here as an input so
    * that the registers used for vertex attributes don't get clobbered by the
    * register merge step */
   auto R0x = new GPRValue(0,0);
   R0x->set_as_input();
   m_vertex_id.reset(R0x);
   inject_register(0, 0, m_vertex_id, false);

   if (m_key.vs.as_gs_a || m_sv_values.test(es_primitive_id)) {
      auto R0z = new GPRValue(0,2);
      R0x->set_as_input();
      m_primitive_id.reset(R0z);
      inject_register(0, 2, m_primitive_id, false);
   }

   if (m_sv_values.test(es_instanceid)) {
      auto R0w = new GPRValue(0,3);
      R0w->set_as_input();
      m_instance_id.reset(R0w);
      inject_register(0, 3, m_instance_id, false);
   }


   if (m_sv_values.test(es_rel_patch_id)) {
      auto R0y = new GPRValue(0,1);
      R0y->set_as_input();
      m_rel_vertex_id.reset(R0y);
      inject_register(0, 1, m_rel_vertex_id, false);
   }

   m_attribs.resize(4 * m_max_attrib + 4);
   for (unsigned i = 0; i < m_max_attrib + 1; ++i) {
      for (unsigned k = 0; k < 4; ++k) {
         auto gpr = std::make_shared<GPRValue>(i + 1, k);
         gpr->set_as_input();
         m_attribs[4 * i + k] = gpr;
         inject_register(i + 1, k, gpr, false);
      }
   }

   return true;
}

void VertexShaderFromNir::emit_shader_start()
{
   m_export_processor->emit_shader_start();
}

bool VertexShaderFromNir::scan_sysvalue_access(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *ii =  nir_instr_as_intrinsic(instr);
      switch (ii->intrinsic) {
      case nir_intrinsic_load_vertex_id:
         m_sv_values.set(es_vertexid);
         break;
      case nir_intrinsic_load_instance_id:
         m_sv_values.set(es_instanceid);
         break;
      case nir_intrinsic_load_tcs_rel_patch_id_r600:
         m_sv_values.set(es_rel_patch_id);
         break;
      case nir_intrinsic_store_output:
         m_export_processor->scan_store_output(ii);
      default:
         ;
      }
   }
   default:
      ;
   }
   return true;
}

bool VertexShaderFromNir::emit_intrinsic_instruction_override(nir_intrinsic_instr* instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_vertex_id:
      return load_preloaded_value(instr->dest, 0, m_vertex_id);
   case nir_intrinsic_load_tcs_rel_patch_id_r600:
      return load_preloaded_value(instr->dest, 0, m_rel_vertex_id);
   case nir_intrinsic_load_instance_id:
      return load_preloaded_value(instr->dest, 0, m_instance_id);
   case nir_intrinsic_store_local_shared_r600:
      return emit_store_local_shared(instr);
   case nir_intrinsic_store_output:
      return m_export_processor->store_output(instr);
   case nir_intrinsic_load_input:
      return load_input(instr);

   default:
      return false;
   }
}

bool VertexShaderFromNir::load_input(nir_intrinsic_instr* instr)
{
   unsigned location = nir_intrinsic_base(instr);

   if (location < VERT_ATTRIB_MAX) {
      for (unsigned i = 0; i < nir_dest_num_components(instr->dest); ++i) {
         auto src = m_attribs[4 * location + i];

         if (i == 0)
            set_input(location, src);

         load_preloaded_value(instr->dest, i, src, i == (unsigned)(instr->num_components - 1));
      }
      return true;
   }
   fprintf(stderr, "r600-NIR: Unimplemented load_deref for %d\n", location);
   return false;
}

bool VertexShaderFromNir::emit_store_local_shared(nir_intrinsic_instr* instr)
{
   unsigned write_mask = nir_intrinsic_write_mask(instr);

   auto address = from_nir(instr->src[1], 0);
   int swizzle_base = (write_mask & 0x3) ? 0 : 2;
   write_mask |= write_mask >> 2;

   auto value =  from_nir(instr->src[0], swizzle_base);
   if (!(write_mask & 2)) {
      emit_instruction(new LDSWriteInstruction(address, 1, value));
   } else {
      auto value1 =  from_nir(instr->src[0], swizzle_base + 1);
      emit_instruction(new LDSWriteInstruction(address, 1, value, value1));
   }

   return true;
}

void VertexShaderFromNir::do_finalize()
{
   m_export_processor->finalize_exports();
}

}
