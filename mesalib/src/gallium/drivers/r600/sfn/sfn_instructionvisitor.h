#ifndef INSTRUCTIONVISITOR_H
#define INSTRUCTIONVISITOR_H

namespace r600 {


class AluInstruction;
class ExportInstruction;
class TexInstruction;
class FetchInstruction;
class IfInstruction;
class ElseInstruction;
class IfElseEndInstruction;
class LoopBeginInstruction;
class LoopEndInstruction;
class LoopBreakInstruction;
class LoopContInstruction;
class StreamOutIntruction;
class MemRingOutIntruction;
class EmitVertex;
class WaitAck;
class WriteScratchInstruction;
class GDSInstr;
class RatInstruction;
class LDSWriteInstruction;
class LDSReadInstruction;
class LDSAtomicInstruction;
class GDSStoreTessFactor;
class InstructionBlock;

class InstructionVisitor
{
public:
   virtual ~InstructionVisitor() {};
   virtual bool visit(AluInstruction& i) = 0;
   virtual bool visit(ExportInstruction& i) = 0;
   virtual bool visit(TexInstruction& i) = 0;
   virtual bool visit(FetchInstruction& i) = 0;
   virtual bool visit(IfInstruction& i) = 0;
   virtual bool visit(ElseInstruction& i) = 0;
   virtual bool visit(IfElseEndInstruction& i) = 0;
   virtual bool visit(LoopBeginInstruction& i) = 0;
   virtual bool visit(LoopEndInstruction& i) = 0;
   virtual bool visit(LoopBreakInstruction& i) = 0;
   virtual bool visit(LoopContInstruction& i) = 0;
   virtual bool visit(StreamOutIntruction& i) = 0;
   virtual bool visit(MemRingOutIntruction& i) = 0;
   virtual bool visit(EmitVertex& i) = 0;
   virtual bool visit(WaitAck& i) = 0;
   virtual bool visit(WriteScratchInstruction& i) = 0;
   virtual bool visit(GDSInstr& i) = 0;
   virtual bool visit(RatInstruction& i) = 0;
   virtual bool visit(LDSWriteInstruction& i) = 0;
   virtual bool visit(LDSReadInstruction& i) = 0;
   virtual bool visit(LDSAtomicInstruction& i) = 0;
   virtual bool visit(GDSStoreTessFactor& i) = 0;
   virtual bool visit(InstructionBlock& i) = 0;
};

class ConstInstructionVisitor
{
public:
   virtual ~ConstInstructionVisitor() {};
   virtual bool visit(const AluInstruction& i) = 0;
   virtual bool visit(const ExportInstruction& i) = 0;
   virtual bool visit(const TexInstruction& i) = 0;
   virtual bool visit(const FetchInstruction& i) = 0;
   virtual bool visit(const IfInstruction& i) = 0;
   virtual bool visit(const ElseInstruction& i) = 0;
   virtual bool visit(const IfElseEndInstruction& i) = 0;
   virtual bool visit(const LoopBeginInstruction& i) = 0;
   virtual bool visit(const LoopEndInstruction& i) = 0;
   virtual bool visit(const LoopBreakInstruction& i) = 0;
   virtual bool visit(const LoopContInstruction& i) = 0;
   virtual bool visit(const StreamOutIntruction& i) = 0;
   virtual bool visit(const MemRingOutIntruction& i) = 0;
   virtual bool visit(const EmitVertex& i) = 0;
   virtual bool visit(const WaitAck& i) = 0;
   virtual bool visit(const WriteScratchInstruction& i) = 0;
   virtual bool visit(const GDSInstr& i) = 0;
   virtual bool visit(const RatInstruction& i) = 0;
   virtual bool visit(const LDSWriteInstruction& i) = 0;
   virtual bool visit(const LDSReadInstruction& i) = 0;
   virtual bool visit(const LDSAtomicInstruction& i) = 0;
   virtual bool visit(const GDSStoreTessFactor& i) = 0;
   virtual bool visit(const InstructionBlock& i) = 0;
};

}

#endif // INSTRUCTIONVISITOR_H
