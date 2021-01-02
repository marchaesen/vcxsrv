/**************************************************************************
 *
 * Copyright 2020 Red Hat.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **************************************************************************/

#ifndef DRAW_TESS_H
#define DRAW_TESS_H

#include "draw_context.h"
#include "draw_private.h"

struct draw_context;
#ifdef LLVM_AVAILABLE

#define NUM_PATCH_INPUTS 32
#define NUM_TCS_INPUTS (PIPE_MAX_SHADER_INPUTS - NUM_PATCH_INPUTS)

struct draw_tcs_inputs {
  /* num vertices per prim */
  float data[32][NUM_TCS_INPUTS][4];
};

struct draw_tcs_outputs {
  /* num vertices per prim */
  float data[32][PIPE_MAX_SHADER_INPUTS][4];
};

struct draw_tes_inputs {
  /* num vertices per prim */
  float data[32][PIPE_MAX_SHADER_INPUTS][4];
};

#endif

struct draw_tess_ctrl_shader {
   struct draw_context *draw;

   struct pipe_shader_state state;
   struct tgsi_shader_info info;

   unsigned vector_length;
   unsigned vertices_out;

   unsigned input_vertex_stride;
   const float (*input)[4];
   const struct tgsi_shader_info *input_info;
#ifdef LLVM_AVAILABLE
   struct draw_tcs_inputs *tcs_input;
   struct draw_tcs_outputs *tcs_output;
   struct draw_tcs_jit_context *jit_context;
   struct draw_tcs_llvm_variant *current_variant;
#endif
};

struct draw_tess_eval_shader {
   struct draw_context *draw;
   struct pipe_shader_state state;
   struct tgsi_shader_info info;

   unsigned prim_mode;
   unsigned spacing;
   unsigned vertex_order_cw;
   unsigned point_mode;

   unsigned position_output;
   unsigned viewport_index_output;
   unsigned ccdistance_output[PIPE_MAX_CLIP_OR_CULL_DISTANCE_ELEMENT_COUNT];
   unsigned vector_length;

   unsigned input_vertex_stride;
   const float (*input)[4];
   const struct tgsi_shader_info *input_info;

#ifdef LLVM_AVAILABLE
   struct draw_tes_inputs *tes_input;
   struct draw_tes_jit_context *jit_context;
   struct draw_tes_llvm_variant *current_variant;
#endif
};

enum pipe_prim_type get_tes_output_prim(struct draw_tess_eval_shader *shader);

int draw_tess_ctrl_shader_run(struct draw_tess_ctrl_shader *shader,
                              const void *constants[PIPE_MAX_CONSTANT_BUFFERS],
                              const unsigned constants_size[PIPE_MAX_CONSTANT_BUFFERS],
                              const struct draw_vertex_info *input_verts,
                              const struct draw_prim_info *input_prim,
                              const struct tgsi_shader_info *input_info,
                              struct draw_vertex_info *output_verts,
                              struct draw_prim_info *output_prims );

int draw_tess_eval_shader_run(struct draw_tess_eval_shader *shader,
                              const void *constants[PIPE_MAX_CONSTANT_BUFFERS],
                              const unsigned constants_size[PIPE_MAX_CONSTANT_BUFFERS],
                              unsigned num_input_vertices_per_patch,
                              const struct draw_vertex_info *input_verts,
                              const struct draw_prim_info *input_prim,
                              const struct tgsi_shader_info *input_info,
                              struct draw_vertex_info *output_verts,
                              struct draw_prim_info *output_prims,
                              ushort **elts_out);

#ifdef LLVM_AVAILABLE
void draw_tcs_set_current_variant(struct draw_tess_ctrl_shader *shader,
                                  struct draw_tcs_llvm_variant *variant);
void draw_tes_set_current_variant(struct draw_tess_eval_shader *shader,
                                  struct draw_tes_llvm_variant *variant);
#endif

#endif
