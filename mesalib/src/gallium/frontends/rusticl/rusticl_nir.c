#include "CL/cl.h"

#include "nir.h"
#include "nir_builder.h"

#include "rusticl_nir.h"

static bool
rusticl_lower_intrinsics_filter(const nir_instr* instr, const void* state)
{
    return instr->type == nir_instr_type_intrinsic;
}

static nir_ssa_def*
rusticl_lower_intrinsics_instr(
    nir_builder *b,
    nir_instr *instr,
    void* _state
) {
    nir_intrinsic_instr *intrins = nir_instr_as_intrinsic(instr);
    struct rusticl_lower_state *state = _state;

    switch (intrins->intrinsic) {
    case nir_intrinsic_image_deref_format:
    case nir_intrinsic_image_deref_order: {
        assert(intrins->src[0].is_ssa);

        int32_t offset;
        nir_deref_instr *deref;
        nir_ssa_def *val;
        nir_variable *var;

        if (intrins->intrinsic == nir_intrinsic_image_deref_format) {
            offset = CL_SNORM_INT8;
            var = state->format_arr;
        } else {
            offset = CL_R;
            var = state->order_arr;
        }

        val = intrins->src[0].ssa;
        // we put write images after read images
        if (nir_intrinsic_access(intrins) & ACCESS_NON_WRITEABLE) {
            val = nir_iadd_imm(b, val, b->shader->info.num_textures);
        }

        deref = nir_build_deref_var(b, var);
        deref = nir_build_deref_array(b, deref, val);
        val = nir_u2u(b, nir_load_deref(b, deref), 32);

        // we have to fix up the value base
        val = nir_iadd_imm(b, val, -offset);

        return val;
    }
    case nir_intrinsic_load_base_global_invocation_id:
        return nir_load_var(b, state->base_global_invoc_id);
    case nir_intrinsic_load_constant_base_ptr:
        return nir_load_var(b, state->const_buf);
    case nir_intrinsic_load_printf_buffer_address:
        return nir_load_var(b, state->printf_buf);
    default:
        return NULL;
    }
}

bool
rusticl_lower_intrinsics(nir_shader *nir, struct rusticl_lower_state* state)
{
    return nir_shader_lower_instructions(
        nir,
        rusticl_lower_intrinsics_filter,
        rusticl_lower_intrinsics_instr,
        state
    );
}
