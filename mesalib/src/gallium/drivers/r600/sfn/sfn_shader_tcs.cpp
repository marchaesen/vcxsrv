#include "sfn_shader_tcs.h"
#include "sfn_instruction_gds.h"
#include "tgsi/tgsi_from_mesa.h"

namespace r600 {

TcsShaderFromNir::TcsShaderFromNir(r600_pipe_shader *sh,
                                   r600_pipe_shader_selector& sel,
                                   const r600_shader_key& key,
                                   enum chip_class chip_class):
   ShaderFromNirProcessor (PIPE_SHADER_TESS_CTRL, sel, sh->shader,
                           sh->scratch_space_needed, chip_class, key.tcs.first_atomic_counter),
   m_reserved_registers(0)
{
   sh_info().tcs_prim_mode = key.tcs.prim_mode;
}

bool TcsShaderFromNir::scan_sysvalue_access(nir_instr *instr)
{
   if (instr->type != nir_instr_type_intrinsic)
      return true;

   auto intr = nir_instr_as_intrinsic(instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_primitive_id:
      m_sv_values.set(es_primitive_id);
      break;
   case nir_intrinsic_load_invocation_id:
      m_sv_values.set(es_invocation_id);
      break;
   case nir_intrinsic_load_tcs_rel_patch_id_r600:
      m_sv_values.set(es_rel_patch_id);
      break;
   case nir_intrinsic_load_tcs_tess_factor_base_r600:
      m_sv_values.set(es_tess_factor_base);
      break;
   default:

      ;
   }
   return true;
}

bool TcsShaderFromNir::do_process_outputs(nir_variable *output)
{
   unsigned name, sid;

   tgsi_get_gl_varying_semantic(static_cast<gl_varying_slot>(output->data.location),
                                true, &name, &sid);

   auto& io = sh_info().output[sh_info().noutput++];
   io.name = name;
   io.write_mask = ((1 << output->type->components()) - 1)
                   << output->data.location_frac;
   return true;
}

bool TcsShaderFromNir::do_allocate_reserved_registers()
{
   if (m_sv_values.test(es_primitive_id)) {
      m_reserved_registers = 1;
      auto gpr = new GPRValue(0,0);
      gpr->set_as_input();
      m_primitive_id.reset(gpr);
   }

   if (m_sv_values.test(es_invocation_id)) {
      m_reserved_registers = 1;
      auto gpr = new GPRValue(0,2);
      gpr->set_as_input();
      m_invocation_id.reset(gpr);
   }

   if (m_sv_values.test(es_rel_patch_id)) {
      m_reserved_registers = 1;
      auto gpr = new GPRValue(0,1);
      gpr->set_as_input();
      m_rel_patch_id.reset(gpr);
   }

   if (m_sv_values.test(es_tess_factor_base)) {
      m_reserved_registers = 1;
      auto gpr = new GPRValue(0,3);
      gpr->set_as_input();
      m_tess_factor_base.reset(gpr);
   }

   set_reserved_registers(m_reserved_registers);

   return true;
}

bool TcsShaderFromNir::emit_intrinsic_instruction_override(nir_intrinsic_instr* instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_tcs_rel_patch_id_r600:
      return load_preloaded_value(instr->dest, 0, m_rel_patch_id);
   case nir_intrinsic_load_invocation_id:
      return load_preloaded_value(instr->dest, 0, m_invocation_id);
   case nir_intrinsic_load_primitive_id:
      return load_preloaded_value(instr->dest, 0, m_primitive_id);
   case nir_intrinsic_load_tcs_tess_factor_base_r600:
      return load_preloaded_value(instr->dest, 0, m_tess_factor_base);
   case nir_intrinsic_store_tf_r600:
      return store_tess_factor(instr);
   default:
      return false;
   }
}

bool TcsShaderFromNir::store_tess_factor(nir_intrinsic_instr* instr)
{
   const GPRVector::Swizzle& swizzle = (instr->src[0].ssa->num_components == 4) ?
            GPRVector::Swizzle({0, 1, 2, 3}) : GPRVector::Swizzle({0, 1, 7, 7});
   auto val = vec_from_nir_with_fetch_constant(instr->src[0],
         (1 << instr->src[0].ssa->num_components) - 1, swizzle);
   emit_instruction(new GDSStoreTessFactor(val));
   return true;
}

}
