/*
 * Copyright © 2010 Intel Corporation
 * Copyright © 2011 Bryan Cain
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "pipe/p_defines.h"
#include "pipe/p_shader_tokens.h"
#include "main/mtypes.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gl_context;
struct gl_shader;
struct gl_shader_program;
struct glsl_to_tgsi_visitor;
struct ureg_program;

enum pipe_error st_translate_program(
   struct gl_context *ctx,
   uint procType,
   struct ureg_program *ureg,
   struct glsl_to_tgsi_visitor *program,
   const struct gl_program *proginfo,
   GLuint numInputs,
   const ubyte inputMapping[],
   const ubyte inputSlotToAttr[],
   const ubyte inputSemanticName[],
   const ubyte inputSemanticIndex[],
   const ubyte interpMode[],
   GLuint numOutputs,
   const ubyte outputMapping[],
   const ubyte outputSemanticName[],
   const ubyte outputSemanticIndex[]);

void free_glsl_to_tgsi_visitor(struct glsl_to_tgsi_visitor *v);

GLboolean st_link_shader(struct gl_context *ctx, struct gl_shader_program *prog);

void
st_translate_stream_output_info(struct glsl_to_tgsi_visitor *glsl_to_tgsi,
                                const ubyte outputMapping[],
                                struct pipe_stream_output_info *so);

void
st_translate_stream_output_info2(struct gl_transform_feedback_info *info,
                                const ubyte outputMapping[],
                                struct pipe_stream_output_info *so);

enum tgsi_semantic
_mesa_sysval_to_semantic(unsigned sysval);

#ifdef __cplusplus
}
#endif
