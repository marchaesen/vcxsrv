#include "sfn_nir_lower_alu.h"
#include "sfn_nir.h"

namespace r600 {

class Lower2x16 : public NirLowerInstruction {
private:
   bool filter(const nir_instr *instr) const override;
   nir_ssa_def *lower(nir_instr *instr) override;
};


bool Lower2x16::filter(const nir_instr *instr) const
{
   if (instr->type != nir_instr_type_alu)
      return false;
   auto alu = nir_instr_as_alu(instr);
   switch (alu->op) {
   case nir_op_unpack_half_2x16:
   case nir_op_pack_half_2x16:
      return true;
   default:
      return false;
   }
}

nir_ssa_def *Lower2x16::lower(nir_instr *instr)
{
   nir_alu_instr *alu = nir_instr_as_alu(instr);

   switch (alu->op) {
   case nir_op_unpack_half_2x16: {
      nir_ssa_def *packed = nir_ssa_for_alu_src(b, alu, 0);
      return  nir_vec2(b, nir_unpack_half_2x16_split_x(b, packed),
                       nir_unpack_half_2x16_split_y(b, packed));

   }
   case nir_op_pack_half_2x16: {
      nir_ssa_def *src_vec2 = nir_ssa_for_alu_src(b, alu, 0);
      return nir_pack_half_2x16_split(b, nir_channel(b, src_vec2, 0),
                                      nir_channel(b, src_vec2, 1));
   }
   default:
      unreachable("Lower2x16 filter doesn't filter correctly");
   }
}

class LowerSinCos : public NirLowerInstruction {
private:
   bool filter(const nir_instr *instr) const override;
   nir_ssa_def *lower(nir_instr *instr) override;
};

bool LowerSinCos::filter(const nir_instr *instr) const
{
   if (instr->type != nir_instr_type_alu)
      return false;

   auto alu = nir_instr_as_alu(instr);
   switch (alu->op) {
   case nir_op_fsin:
   case nir_op_fcos:
      return true;
   default:
      return false;
   }
}

nir_ssa_def *LowerSinCos::lower(nir_instr *instr)
{
   auto alu = nir_instr_as_alu(instr);

   assert(alu->op == nir_op_fsin ||
          alu->op == nir_op_fcos);

   auto normalized =
         nir_fadd(b,
                  nir_ffract(b,
                             nir_ffma(b,
                                      nir_ssa_for_alu_src(b, alu, 0),
                                      nir_imm_float(b, 0.15915494),
                                      nir_imm_float(b, 0.5))),
                              nir_imm_float(b, -0.5));

   if (alu->op == nir_op_fsin)
      return nir_fsin_r600(b, normalized);
   else
      return nir_fcos_r600(b, normalized);
}


} // namespace r600


bool r600_nir_lower_pack_unpack_2x16(nir_shader *shader)
{
   return r600::Lower2x16().run(shader);
}

bool r600_nir_lower_trigen(nir_shader *shader)
{
   return r600::LowerSinCos().run(shader);
}
