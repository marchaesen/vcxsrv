#ifndef SFN_EMITSSBOINSTRUCTION_H
#define SFN_EMITSSBOINSTRUCTION_H

#include "sfn_emitinstruction.h"
#include "sfn_instruction_gds.h"
#include "sfn_value_gpr.h"

namespace r600 {

class EmitSSBOInstruction: public EmitInstruction {
public:
   EmitSSBOInstruction(ShaderFromNirProcessor& processor);

   void set_ssbo_offset(int offset);

   void set_require_rat_return_address();
   bool load_rat_return_address();
   bool load_atomic_inc_limits();

private:
   bool do_emit(nir_instr *instr);

   bool emit_atomic(const nir_intrinsic_instr* instr);
   bool emit_unary_atomic(const nir_intrinsic_instr* instr);
   bool emit_atomic_add(const nir_intrinsic_instr* instr);
   bool emit_atomic_inc(const nir_intrinsic_instr* instr);
   bool emit_atomic_pre_dec(const nir_intrinsic_instr* instr);

   bool emit_load_ssbo(const nir_intrinsic_instr* instr);
   bool emit_store_ssbo(const nir_intrinsic_instr* instr);

   bool emit_image_size(const nir_intrinsic_instr *intrin);
   bool emit_image_load(const nir_intrinsic_instr *intrin);
   bool emit_image_store(const nir_intrinsic_instr *intrin);
   bool emit_ssbo_atomic_op(const nir_intrinsic_instr *intrin);
   bool emit_buffer_size(const nir_intrinsic_instr *intrin);

   bool fetch_return_value(const nir_intrinsic_instr *intrin);

   bool make_stores_ack_and_waitack();

   ESDOp get_opcode(nir_intrinsic_op opcode);
   RatInstruction::ERatOp get_rat_opcode(const nir_intrinsic_op opcode, pipe_format format) const;

   GPRVector make_dest(const nir_intrinsic_instr* instr);

   PGPRValue m_atomic_update;

   bool m_require_rat_return_address;
   GPRVector m_rat_return_address;
   int m_ssbo_image_offset;
   std::vector<RatInstruction *> m_store_ops;
};

}

#endif // SFN_EMITSSBOINSTRUCTION_H
