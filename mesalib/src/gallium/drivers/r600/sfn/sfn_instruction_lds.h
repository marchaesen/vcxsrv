#ifndef LDSINSTRUCTION_H
#define LDSINSTRUCTION_H

#include "sfn_instruction_base.h"

namespace r600 {

class LDSReadInstruction : public Instruction {
public:
   LDSReadInstruction(std::vector<PValue>& value, std::vector<PValue>& address);
   void replace_values(const ValueSet& candidates, PValue new_value) override;

   unsigned num_values() const { return m_dest_value.size();}
   const Value& address(unsigned i) const { return *m_address[i];}
   const Value& dest(unsigned i) const { return *m_dest_value[i];}
private:
   void do_print(std::ostream& os) const override;
   bool is_equal_to(const Instruction& lhs) const override;

   std::vector<PValue> m_address;
   std::vector<PValue> m_dest_value;
};

class LDSAtomicInstruction : public Instruction {
public:
   LDSAtomicInstruction(PValue& dest, PValue& src0, PValue src1, PValue& address, unsigned op);
   LDSAtomicInstruction(PValue& dest, PValue& src0, PValue& address, unsigned op);

   const Value& address() const { return *m_address;}
   const Value& dest() const { return *m_dest_value;}
   const Value& src0() const { return *m_src0_value;}
   const PValue& src1() const { return m_src1_value;}
   unsigned op() const {return m_opcode;}

private:
   void do_print(std::ostream& os) const override;
   bool is_equal_to(const Instruction& lhs) const override;

   PValue m_address;
   PValue m_dest_value;
   PValue m_src0_value;
   PValue m_src1_value;
   unsigned m_opcode;
};

class LDSWriteInstruction : public Instruction {
public:
   LDSWriteInstruction(PValue address, unsigned idx_offset, PValue value0);
   LDSWriteInstruction(PValue address, unsigned idx_offset, PValue value0, PValue value1);

   const Value& address() const {return *m_address;};
   const Value& value0() const { return *m_value0;}
   const Value& value1() const { return *m_value1;}
   unsigned num_components() const { return m_value1 ? 2 : 1;}
   unsigned idx_offset() const {return m_idx_offset;};

   void replace_values(const ValueSet& candidates, PValue new_value) override;

private:
   void do_print(std::ostream& os) const override;
   bool is_equal_to(const Instruction& lhs) const override;

   PValue m_address;
   PValue m_value0;
   PValue m_value1;
   unsigned m_idx_offset;

};

}

#endif // LDSINSTRUCTION_H
