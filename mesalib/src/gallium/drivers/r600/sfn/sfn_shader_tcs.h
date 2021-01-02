#ifndef TCSSHADERFROMNIR_H
#define TCSSHADERFROMNIR_H

#include "sfn_shader_base.h"

namespace r600 {

class TcsShaderFromNir : public ShaderFromNirProcessor
{
public:
   TcsShaderFromNir(r600_pipe_shader *sh, r600_pipe_shader_selector& sel, const r600_shader_key& key, enum chip_class chip_class);
   bool scan_sysvalue_access(nir_instr *instr) override;

private:
   bool do_allocate_reserved_registers() override;
   bool emit_intrinsic_instruction_override(nir_intrinsic_instr* instr) override;
   bool store_tess_factor(nir_intrinsic_instr* instr);

   bool do_process_inputs(nir_variable *input) override { return true;}
   bool do_process_outputs(nir_variable *output) override;
   bool do_emit_load_deref(const nir_variable *in_var, nir_intrinsic_instr* instr) override { return true;}
   bool do_emit_store_deref(const nir_variable *out_var, nir_intrinsic_instr* instr) override { return true;}
   void do_finalize() override {}

   int m_reserved_registers;
   PValue m_patch_id;
   PValue m_rel_patch_id;
   PValue m_invocation_id;
   PValue m_primitive_id;
   PValue m_tess_factor_base;


};

}

#endif // TCSSHADERFROMNIR_H
