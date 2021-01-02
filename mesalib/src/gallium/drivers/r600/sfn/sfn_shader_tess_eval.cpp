#include "sfn_shader_tess_eval.h"
#include "tgsi/tgsi_from_mesa.h"

namespace r600 {

TEvalShaderFromNir::TEvalShaderFromNir(r600_pipe_shader *sh, r600_pipe_shader_selector& sel,
                                       const r600_shader_key& key, r600_shader *gs_shader,
                                       enum chip_class chip_class):
   VertexStage(PIPE_SHADER_TESS_EVAL, sel, sh->shader,
               sh->scratch_space_needed, chip_class, key.tes.first_atomic_counter),
   m_reserved_registers(0),
   m_key(key)

{
   sh->shader.tes_as_es = key.tes.as_es;
   if (key.tes.as_es)
      m_export_processor.reset(new VertexStageExportForGS(*this, gs_shader));
   else
      m_export_processor.reset(new VertexStageExportForFS(*this, &sel.so, sh, key));
}

bool TEvalShaderFromNir::do_process_inputs(nir_variable *input)
{
   if (input->data.location == VARYING_SLOT_POS ||
       input->data.location == VARYING_SLOT_PSIZ ||
       input->data.location == VARYING_SLOT_CLIP_DIST0 ||
       input->data.location == VARYING_SLOT_CLIP_DIST1 ||
       (input->data.location >= VARYING_SLOT_VAR0 &&
       input->data.location <= VARYING_SLOT_VAR31) ||
       (input->data.location >= VARYING_SLOT_TEX0 &&
       input->data.location <= VARYING_SLOT_TEX7) ||
        (input->data.location >= VARYING_SLOT_PATCH0 &&
         input->data.location <= VARYING_SLOT_TESS_MAX)) {

      r600_shader_io& io = sh_info().input[input->data.driver_location];
      tgsi_get_gl_varying_semantic(static_cast<gl_varying_slot>( input->data.location),
                                   true, &io.name, &io.sid);
      ++sh_info().ninput;
      return true;
   }

   return false;

}

bool TEvalShaderFromNir::scan_sysvalue_access(nir_instr *instr)
{
   if (instr->type != nir_instr_type_intrinsic)
      return true;

   auto ir = nir_instr_as_intrinsic(instr);

   switch (ir->intrinsic) {
   case nir_intrinsic_load_tess_coord:
      m_sv_values.set(es_tess_coord);
      break;
   case nir_intrinsic_load_primitive_id:
      m_sv_values.set(es_primitive_id);
      break;
   case nir_intrinsic_load_tcs_rel_patch_id_r600:
      m_sv_values.set(es_rel_patch_id);
      break;
   case nir_intrinsic_store_output:
      m_export_processor->scan_store_output(ir);
      break;
   default:
      ;
   }
   return true;
}

void TEvalShaderFromNir::emit_shader_start()
{
   m_export_processor->emit_shader_start();
}

bool TEvalShaderFromNir::do_allocate_reserved_registers()
{
   if (m_sv_values.test(es_tess_coord)) {
      m_reserved_registers = 1;
      auto gpr = new GPRValue(0,0);
      gpr->set_as_input();
      m_tess_coord[0].reset(gpr);
      gpr = new GPRValue(0,1);
      gpr->set_as_input();
      m_tess_coord[1].reset(gpr);
   }

   if (m_sv_values.test(es_rel_patch_id)) {
      m_reserved_registers = 1;
      auto gpr = new GPRValue(0,2);
      gpr->set_as_input();
      m_rel_patch_id.reset(gpr);
   }

   if (m_sv_values.test(es_primitive_id) ||
       m_key.vs.as_gs_a) {
      m_reserved_registers = 1;
      auto gpr = new GPRValue(0,3);
      gpr->set_as_input();
      m_primitive_id.reset(gpr);
      if (m_key.vs.as_gs_a)
         inject_register(0, 3, m_primitive_id, false);
   }
   set_reserved_registers(m_reserved_registers);
   return true;
}

bool TEvalShaderFromNir::load_tess_z_coord(nir_intrinsic_instr* instr)
{
   if (m_tess_coord[2])
      return load_preloaded_value(instr->dest, 2, m_tess_coord[2]);

   m_tess_coord[2] = from_nir(instr->dest, 2);
   emit_instruction(new AluInstruction(op2_add, m_tess_coord[2], Value::one_f, m_tess_coord[0], {alu_last_instr, alu_write, alu_src1_neg}));
   emit_instruction(new AluInstruction(op2_add, m_tess_coord[2], m_tess_coord[2], m_tess_coord[1], {alu_last_instr, alu_write, alu_src1_neg}));
   return true;
}

bool TEvalShaderFromNir::emit_intrinsic_instruction_override(nir_intrinsic_instr* instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_tess_coord:
      return load_preloaded_value(instr->dest, 0, m_tess_coord[0]) &&
            load_preloaded_value(instr->dest, 1, m_tess_coord[1]) &&
            load_tess_z_coord(instr);
   case nir_intrinsic_load_primitive_id:
      return load_preloaded_value(instr->dest, 0, m_primitive_id);
   case nir_intrinsic_load_tcs_rel_patch_id_r600:
      return load_preloaded_value(instr->dest, 0, m_rel_patch_id);
   case nir_intrinsic_store_output:
      return m_export_processor->store_output(instr);
   default:
      return false;
   }
}


bool TEvalShaderFromNir::do_process_outputs(nir_variable *output)
{
   return m_export_processor->do_process_outputs(output);
}

bool TEvalShaderFromNir::do_emit_store_deref(const nir_variable *out_var, nir_intrinsic_instr* instr)
{
   return false;
}

void TEvalShaderFromNir::do_finalize()
{
   m_export_processor->finalize_exports();
}


bool TEvalShaderFromNir::emit_load_tess_coord(nir_intrinsic_instr* instr)
{
   bool result = load_preloaded_value(instr->dest, 0, m_tess_coord[0]) &&
               load_preloaded_value(instr->dest, 1, m_tess_coord[1]);

   m_tess_coord[2] = from_nir(instr->dest, 2);


   emit_instruction(new AluInstruction(op2_add, m_tess_coord[2], m_tess_coord[2],
         m_tess_coord[0], {alu_last_instr, alu_write, alu_src0_neg}));
   emit_instruction(new AluInstruction(op2_add, m_tess_coord[2], m_tess_coord[2],
         m_tess_coord[1], {alu_last_instr, alu_write, alu_src0_neg}));
   return result;
}

}
