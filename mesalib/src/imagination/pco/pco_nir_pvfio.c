/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_nir_pvfio.c
 *
 * \brief PCO NIR per-vertex/fragment input/output passes.
 */

#include "compiler/glsl_types.h"
#include "compiler/shader_enums.h"
#include "nir.h"
#include "nir_builder.h"
#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "util/macros.h"
#include "util/u_dynarray.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

/** Per-fragment output pass state. */
struct pfo_state {
   struct util_dynarray stores; /** List of fragment stores. */
   pco_fs_data *fs; /** Fragment-specific data. */
};

/**
 * \brief Returns a NIR intrinsic instruction if a NIR instruction matches the
 *        provided intrinsic op.
 *
 * \param[in] instr NIR instruction.
 * \param[in] op Desired intrinsic op.
 * \return The intrinsic instruction, else NULL.
 */
static inline nir_intrinsic_instr *is_intr(nir_instr *instr,
                                           nir_intrinsic_op op)
{
   nir_intrinsic_instr *intr = NULL;

   if (instr->type != nir_instr_type_intrinsic)
      return NULL;

   intr = nir_instr_as_intrinsic(instr);

   if (intr->intrinsic != op)
      return NULL;

   return intr;
}

/**
 * \brief Returns the GLSL base type equivalent of a pipe format.
 *
 * \param[in] format Pipe format.
 * \return The GLSL base type, or GLSL_TYPE_ERROR if unsupported/invalid.
 */
static inline enum glsl_base_type base_type_from_fmt(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   int chan = util_format_get_first_non_void_channel(format);
   if (chan < 0)
      return GLSL_TYPE_ERROR;

   switch (desc->channel[chan].type) {
   case UTIL_FORMAT_TYPE_UNSIGNED:
      return GLSL_TYPE_UINT;

   case UTIL_FORMAT_TYPE_SIGNED:
      return GLSL_TYPE_INT;

   case UTIL_FORMAT_TYPE_FLOAT:
      return GLSL_TYPE_FLOAT;

   default:
      break;
   }

   return GLSL_TYPE_ERROR;
}

/**
 * \brief Lowers a PFO-related instruction.
 *
 * \param[in] b NIR builder.
 * \param[in] instr NIR instruction.
 * \param[in] cb_data User callback data.
 * \return True if the instruction was lowered.
 */
static bool lower_pfo(nir_builder *b, nir_instr *instr, void *cb_data)
{
   struct pfo_state *state = cb_data;

   /* TODO NEXT: move into separate function (pack_to_pbe),
    * and use data from driver to actually figure out format stuff!
    */
   nir_intrinsic_instr *intr;
   if ((intr = is_intr(instr, nir_intrinsic_store_output))) {
      /* Skip stores we've already processed. */
      util_dynarray_foreach (&state->stores, nir_intrinsic_instr *, store) {
         if (intr == *store)
            return false;
      }

      nir_src *value = &intr->src[0];
      nir_src *offset = &intr->src[1];

      /* TODO: more accurate way of detecting this */
      /* Already in expected format. */
      if (b->shader->info.internal && nir_src_num_components(*value) == 1) {
         util_dynarray_append(&state->stores, nir_intrinsic_instr *, intr);
         return false;
      }

      assert(nir_src_as_uint(*offset) == 0);

      assert(nir_src_num_components(*value) == 4);
      assert(nir_src_bit_size(*value) == 32);

      struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
      gl_frag_result location = io_semantics.location;

      enum pipe_format format = state->fs->output_formats[location];

      unsigned format_bits = util_format_get_blocksizebits(format);
      assert(!(format_bits % 32));

      /* Update the type of the stored variable. */
      nir_variable *var = nir_find_variable_with_location(b->shader,
                                                          nir_var_shader_out,
                                                          location);
      assert(var);

      var->type = glsl_simple_explicit_type(base_type_from_fmt(format),
                                            format_bits / 32,
                                            1,
                                            0,
                                            false,
                                            0);

      b->cursor = nir_after_block(
         nir_impl_last_block(nir_shader_get_entrypoint(b->shader)));

      /* Emit and track the new store. */
      /* TODO: support other formats. */
      if (format == PIPE_FORMAT_R8G8B8A8_UNORM) {
         nir_intrinsic_instr *store =
            nir_store_output(b,
                             nir_pack_unorm_4x8(b, value->ssa),
                             offset->ssa,
                             .base = nir_intrinsic_base(intr),
                             .write_mask = 1,
                             .component = 0,
                             .src_type = nir_type_uint32,
                             .io_semantics = io_semantics,
                             .io_xfb = nir_intrinsic_io_xfb(intr),
                             .io_xfb2 = nir_intrinsic_io_xfb2(intr));
         util_dynarray_append(&state->stores, nir_intrinsic_instr *, store);
      } else {
         unreachable();
      }

      /* Remove the old store. */
      b->cursor = nir_instr_remove(instr);

      return true;
   }

   return false;
}

/**
 * \brief Per-fragment output pass.
 *
 * \param[in,out] nir NIR shader.
 * \param[in,out] fs Fragment shader-specific data.
 * \return True if the pass made progress.
 */
bool pco_nir_pfo(nir_shader *nir, pco_fs_data *fs)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   struct pfo_state state = { .fs = fs };
   util_dynarray_init(&state.stores, NULL);

   bool progress =
      nir_shader_instructions_pass(nir, lower_pfo, nir_metadata_none, &state);

   util_dynarray_fini(&state.stores);

   return progress;
}

/**
 * \brief Per-vertex input pass.
 *
 * \param[in,out] nir NIR shader.
 * \param[in,out] vs Vertex shader-specific data.
 * \return True if the pass made progress.
 */
bool pco_nir_pvi(nir_shader *nir, pco_vs_data *vs)
{
   assert(nir->info.stage == MESA_SHADER_VERTEX);

   puts("finishme: pco_nir_pvi");

   /* TODO: format conversion and inserting unspecified/missing components. */

   return false;
}
