/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"

/*
 * Lower calls to functions prefixed "nir_*" to the NIR ALU instruction or
 * intrinsic represented. This matches functions of the form:
 *
 *    nir_[op name](__optional mangling suffix)
 *
 * These functions return a value if the instruction has a destination. They
 * take all instruction sources as parameters, followed by parameters for each
 * ordered intrinsic index if any.
 *
 * Mangling allows for multiple definitions of the same instruction with
 * different vector lengths and bit sizes. This could be combined with
 * __attribute_((overloadable)) for seamless overloads.
 *
 * In effect, this pass re-implements nir_builder dynamically. This exposes
 * low-level hardware intrinsics to internal driver programs. It is intended for
 * use with internal OpenCL but should theoretically work for GLSL too.
 */

static void
lower_builtin_alu(nir_builder *b, nir_call_instr *call, nir_op op)
{
   const nir_op_info info = nir_op_infos[op];
   nir_def *srcs[NIR_ALU_MAX_INPUTS];

   for (unsigned s = 0; s < info.num_inputs; ++s) {
      srcs[s] = call->params[1 + s].ssa;
   }

   nir_def *res = nir_build_alu_src_arr(b, op, srcs);
   nir_store_deref(b, nir_src_as_deref(call->params[0]), res,
                   nir_component_mask(res->num_components));
}

static void
lower_builtin_intr(nir_builder *b, nir_call_instr *call, nir_intrinsic_op op)
{
   nir_intrinsic_instr *intr = nir_intrinsic_instr_create(b->shader, op);
   const nir_intrinsic_info info = nir_intrinsic_infos[op];

   /* If there is a destination, the first parameter is the return deref */
   unsigned src = info.has_dest ? 1 : 0;
   assert(call->num_params == (src + info.num_srcs + info.num_indices));

   /* The next parameters are the intrinsic sources */
   for (unsigned s = 0; s < info.num_srcs; ++s) {
      intr->src[s] = nir_src_for_ssa(call->params[src++].ssa);
   }

   /* The remaining parameters are the intrinsic indices */
   for (unsigned s = 0; s < info.num_indices; ++s) {
      uint64_t val = nir_src_as_uint(call->params[src++]);
      intr->const_index[info.index_map[info.indices[s]] - 1] = val;
   }

   /* Some intrinsics must infer num_components from a particular source. */
   for (unsigned s = 0; s < info.num_srcs; ++s) {
      if (info.src_components[s] == 0) {
         intr->num_components = intr->src[s].ssa->num_components;
         break;
      }
   }

   /* Insert the instruction before any store_deref */
   nir_builder_instr_insert(b, &intr->instr);

   /* If there is a destination, plumb it through the return deref */
   if (info.has_dest) {
      nir_deref_instr *deref = nir_src_as_deref(call->params[0]);

      unsigned bit_size = glsl_get_bit_size(deref->type);
      unsigned num_components = MAX2(glsl_get_length(deref->type), 1);

      nir_def_init(&intr->instr, &intr->def, num_components, bit_size);
      nir_store_deref(b, deref, &intr->def, nir_component_mask(num_components));

      if (info.dest_components == 0 && intr->num_components == 0) {
         intr->num_components = num_components;
      }
   }
}

static bool
lower(nir_builder *b, nir_instr *instr, void *data)
{
   /* All builtins are exposed as function calls */
   if (instr->type != nir_instr_type_call)
      return false;

   nir_call_instr *call = nir_instr_as_call(instr);
   nir_function *func = call->callee;

   /* We reserve all functions prefixed nir_* as builtins needing lowering. */
   if (strncmp("nir_", func->name, strlen("nir_")) != 0)
      return false;

   /* Strip the nir_ prefix to give the name of an ALU opcode or intrinsic. Also
    * strip the __* suffix if present: we don't need mangling information, we
    * can recover vector lengths / bit sizes from the NIR.  This implements a
    * crude form of function overloading.
    */
   const char *intr_name = func->name + strlen("nir_");
   const char *suffix = strstr(intr_name, "__");
   unsigned len = (suffix != NULL) ? (suffix - intr_name) : strlen(intr_name);

   /* From this point on, we must not fail. Remove the call. */
   b->cursor = nir_instr_remove(&call->instr);

   /* Look for an ALU opcode */
   for (unsigned i = 0; i < ARRAY_SIZE(nir_op_infos); ++i) {
      if (strncmp(intr_name, nir_op_infos[i].name, len) == 0 &&
          strlen(nir_op_infos[i].name) == len) {

         lower_builtin_alu(b, call, i);
         return true;
      }
   }

   /* Look for an intrinsic */
   for (unsigned i = 0; i < ARRAY_SIZE(nir_intrinsic_infos); ++i) {
      if (strncmp(intr_name, nir_intrinsic_infos[i].name, len) == 0 &&
          strlen(nir_intrinsic_infos[i].name) == len) {

         lower_builtin_intr(b, call, i);
         return true;
      }
   }

   /* We must have matched something! */
   fprintf(stderr, "unknown opcode %s\n", func->name);
   unreachable("invalid nir opcode/intrinsic");
}

bool
nir_lower_calls_to_builtins(nir_shader *s)
{
   return nir_shader_instructions_pass(s, lower, nir_metadata_none, NULL);
}
