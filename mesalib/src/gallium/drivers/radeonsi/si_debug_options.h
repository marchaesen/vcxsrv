OPT_BOOL(inline_uniforms, true, "Optimize shaders by replacing uniforms with literals")
OPT_BOOL(aux_debug, false, "Generate ddebug_dumps for the auxiliary context")
OPT_BOOL(sync_compile, false, "Always compile synchronously (will cause stalls)")
OPT_BOOL(dump_shader_binary, false, "Dump shader binary as part of ddebug_dumps")
OPT_BOOL(debug_disassembly, false,
         "Report shader disassembly as part of driver debug messages (for shader db)")
OPT_BOOL(halt_shaders, false, "Halt shaders at the start (will hang)")
OPT_BOOL(vs_fetch_always_opencode, false,
         "Always open code vertex fetches (less efficient, purely for testing)")
OPT_BOOL(prim_restart_tri_strips_only, false, "Only enable primitive restart for triangle strips")
OPT_BOOL(no_infinite_interp, false, "Kill PS with infinite interp coeff")
OPT_BOOL(clamp_div_by_zero, false, "Clamp div by zero (x / 0 becomes FLT_MAX instead of NaN)")
OPT_BOOL(shader_culling, false, "Cull primitives in shaders when benefical (without tess and GS)")
OPT_BOOL(vrs2x2, false, "Enable 2x2 coarse shading for non-GUI elements")
OPT_BOOL(enable_sam, false, "Enable Smart Access Memory with Above 4G Decoding for unvalidated platforms.")
OPT_BOOL(disable_sam, false, "Disable Smart Access Memory.")
OPT_BOOL(fp16, false, "Enable FP16 for mediump.")

#undef OPT_BOOL
