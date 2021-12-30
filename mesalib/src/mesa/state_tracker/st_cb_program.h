/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef ST_CB_PROGRAM_H
#define ST_CB_PROGRAM_H

#ifdef __cplusplus
extern "C" {
#endif

struct dd_function_table;

extern void
st_init_program_functions(struct dd_function_table *functions);
void st_delete_program(struct gl_context *ctx, struct gl_program *prog);
GLboolean st_program_string_notify(struct gl_context *ctx,
                                   GLenum target,
                                   struct gl_program *prog);
struct gl_program *st_new_ati_fs(struct gl_context *ctx, struct ati_fragment_shader *curProg);

void st_max_shader_compiler_threads(struct gl_context *ctx, unsigned count);
bool st_get_shader_program_completion_status(struct gl_context *ctx,
                                             struct gl_shader_program *shprog);

#ifdef __cplusplus
}
#endif
#endif
