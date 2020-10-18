#include <stdbool.h>
#include "nir.h"

bool midgard_nir_lower_algebraic_early(nir_shader *shader);
bool midgard_nir_lower_algebraic_late(nir_shader *shader);
bool midgard_nir_scale_trig(nir_shader *shader);
bool midgard_nir_cancel_inot(nir_shader *shader);
