#ifndef SFN_OPTIMIZERS_H
#define SFN_OPTIMIZERS_H

#include "sfn_instruction_base.h"

namespace r600 {

std::vector<PInstruction>
flatten_alu_ops(const std::vector<InstructionBlock> &ir);


}

#endif // SFN_OPTIMIZERS_H
