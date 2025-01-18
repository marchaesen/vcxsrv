/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"

/*
 * After going through the clang -> LLVM -> SPIR-V translator -> vtn pipes,
 * OpenCL kernel's end up translated to 2 nir_functions:
 *
 * - a "wrapper" function that is_entrypoint but not is_exported
 * - the "real" function that is_exported
 *
 * Confusingly, both functions have the same name.
 *
 * Also, workgroup size information is on the wrapper function only, so we can't
 * just ignore the wrappers. But inlining and removing non-exported would delete
 * the whole shader and lose that information.
 *
 * This pass is a silly solution to the silly problem: it looks for shadowed
 * function names, which can only come from these wrappers. It then exports the
 * wrappers and unexports the inner functions. After inlining and removing
 * non-exported functions, we're left with a single function per kernel with
 * workgroup size information preserved.
 *
 * While we're at it, we unexport _prefixed functions. This is an escape hatch
 * to allow defining `kernel`s that are not intended for export, to workaround
 * OpenCL limitations around `static kernel`s and shared local memory outside
 * `kernel`s.
 */
void
nir_fixup_is_exported(nir_shader *nir)
{
   struct set *seen =
      _mesa_set_create(NULL, _mesa_hash_string, _mesa_key_string_equal);
   struct set *shadowed =
      _mesa_set_create(NULL, _mesa_hash_string, _mesa_key_string_equal);

   nir_foreach_function(func, nir) {
      if (_mesa_set_search(seen, func->name)) {
         _mesa_set_add(shadowed, func->name);
      } else {
         _mesa_set_add(seen, func->name);
      }
   }

   nir_foreach_function(func, nir) {
      if (_mesa_set_search(shadowed, func->name)) {
         func->is_exported = func->is_entrypoint;
      }

      if (func->name[0] == '_') {
         func->is_exported = func->is_entrypoint = false;
      }
   }

   _mesa_set_destroy(seen, NULL);
   _mesa_set_destroy(shadowed, NULL);
}
