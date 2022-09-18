/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef ROGUE_BUILD_DATA_H
#define ROGUE_BUILD_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "compiler/shader_enums.h"
#include "nir/nir.h"
#include "rogue.h"

/* Max number of I/O varying variables.
 * Fragment shader: MAX_VARYING + 1 (W coefficient).
 * Vertex shader: MAX_VARYING + 1 (position slot).
 */
#define ROGUE_MAX_IO_VARYING_VARS (MAX_VARYING + 1)

/* VERT_ATTRIB_GENERIC0-15 */
#define ROGUE_MAX_IO_ATTRIB_VARS 16

/* Max buffers entries that can be used. */
/* TODO: Currently UBOs are the only supported buffers. */
#define ROGUE_MAX_BUFFERS 24

struct rogue_compiler;
struct rogue_shader;
struct rogue_shader_binary;

/**
 * \brief UBO data.
 */
struct rogue_ubo_data {
   size_t num_ubo_entries;
   size_t desc_set[ROGUE_MAX_BUFFERS];
   size_t binding[ROGUE_MAX_BUFFERS];
   size_t dest[ROGUE_MAX_BUFFERS];
   size_t size[ROGUE_MAX_BUFFERS];
};

/**
 * \brief Compile time constants that need uploading.
 */
struct rogue_compile_time_consts_data {
   /* TODO: Output these from the compiler. */
   /* TODO: Add the other types. */
   struct {
      size_t num;
      size_t dest;
      /* TODO: This should probably be bigger. Big enough to account for all
       * available writable special constant regs.
       */
      uint32_t value[ROGUE_MAX_BUFFERS];
   } static_consts;
};

/**
 * \brief Per-stage common build data.
 */
struct rogue_common_build_data {
   size_t temps;
   size_t internals;
   size_t coeffs;
   size_t shareds;

   struct rogue_ubo_data ubo_data;
   struct rogue_compile_time_consts_data compile_time_consts_data;
};

/**
 * \brief Arguments for the FPU iterator(s)
 * (produces varyings for the fragment shader).
 */
struct rogue_iterator_args {
   uint32_t num_fpu_iterators;
   uint32_t fpu_iterators[ROGUE_MAX_IO_VARYING_VARS];
   uint32_t destination[ROGUE_MAX_IO_VARYING_VARS];
   size_t base[ROGUE_MAX_IO_VARYING_VARS];
   size_t components[ROGUE_MAX_IO_VARYING_VARS];
};

/**
 * \brief Vertex input register allocations.
 */
struct rogue_vertex_inputs {
   size_t num_input_vars;
   size_t base[ROGUE_MAX_IO_ATTRIB_VARS];
   size_t components[ROGUE_MAX_IO_ATTRIB_VARS];
};

/**
 * \brief Vertex output allocations.
 */
struct rogue_vertex_outputs {
   size_t num_output_vars;
   size_t base[ROGUE_MAX_IO_VARYING_VARS];
   size_t components[ROGUE_MAX_IO_VARYING_VARS];
};

/**
 * \brief Stage-specific build data.
 */
struct rogue_build_data {
   struct rogue_fs_build_data {
      struct rogue_iterator_args iterator_args;
      enum rogue_msaa_mode msaa_mode;
      bool phas; /* Indicates the presence of PHAS instruction. */
   } fs;
   struct rogue_vs_build_data {
      struct rogue_vertex_inputs inputs;
      size_t num_vertex_input_regs; /* Final number of inputs. */

      struct rogue_vertex_outputs outputs;
      size_t num_vertex_outputs; /* Final number of outputs. */

      size_t num_varyings; /* Final number of varyings. */
   } vs;
};

/**
 * \brief Shared multi-stage build context.
 */
struct rogue_build_ctx {
   struct rogue_compiler *compiler;

   /* Shaders in various stages of compilations. */
   nir_shader *nir[MESA_SHADER_FRAGMENT + 1];
   struct rogue_shader *rogue[MESA_SHADER_FRAGMENT + 1];
   struct rogue_shader_binary *binary[MESA_SHADER_FRAGMENT + 1];

   struct rogue_common_build_data common_data[MESA_SHADER_FRAGMENT + 1];
   struct rogue_build_data stage_data;
};

PUBLIC
struct rogue_build_ctx *
rogue_create_build_context(struct rogue_compiler *compiler);

PUBLIC
bool rogue_collect_io_data(struct rogue_build_ctx *ctx, nir_shader *nir);

PUBLIC
size_t rogue_coeff_index_fs(struct rogue_iterator_args *args,
                            gl_varying_slot location,
                            size_t component);

PUBLIC
size_t rogue_output_index_vs(struct rogue_vertex_outputs *outputs,
                             gl_varying_slot location,
                             size_t component);

PUBLIC
size_t rogue_ubo_reg(struct rogue_ubo_data *ubo_data,
                     size_t desc_set,
                     size_t binding,
                     size_t offset_bytes);

#endif /* ROGUE_BUILD_DATA_H */
