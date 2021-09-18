#ifndef SFN_NIR_LOWER_ALU_H
#define SFN_NIR_LOWER_ALU_H

#include "nir.h"

bool r600_nir_lower_pack_unpack_2x16(nir_shader *shader);

bool r600_nir_lower_trigen(nir_shader *shader);


#endif // SFN_NIR_LOWER_ALU_H
