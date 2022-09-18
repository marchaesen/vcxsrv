struct rusticl_lower_state {
    nir_variable *base_global_invoc_id;
    nir_variable *const_buf;
    nir_variable *printf_buf;
    nir_variable *format_arr;
    nir_variable *order_arr;
};

bool rusticl_lower_intrinsics(nir_shader *nir, struct rusticl_lower_state *state);
