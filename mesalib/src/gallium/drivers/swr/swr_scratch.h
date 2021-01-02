/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ***************************************************************************/

#ifndef SWR_SCRATCH_H
#define SWR_SCRATCH_H

struct swr_scratch_space {
   void *head;
   unsigned int current_size;
   /* TODO XXX: Add a fence for wrap condition. */

   void *base;
};

struct swr_scratch_buffers {
   struct swr_scratch_space vs_constants;
   struct swr_scratch_space fs_constants;
   struct swr_scratch_space gs_constants;
   struct swr_scratch_space tcs_constants;
   struct swr_scratch_space tes_constants;
   struct swr_scratch_space vertex_buffer;
   struct swr_scratch_space index_buffer;
};


/*
 * swr_copy_to_scratch_space
 * Copies size bytes of user_buffer into the scratch ring buffer.
 * Used to store temporary data such as client arrays and constants.
 *
 * Inputs:
 *   space ptr to scratch pool (vs_constants, fs_constants)
 *   user_buffer, data to copy into scratch space
 *   size to be copied
 * Returns:
 *   pointer to data copied to scratch space.
 */
void *swr_copy_to_scratch_space(struct swr_context *ctx,
                                struct swr_scratch_space *space,
                                const void *user_buffer,
                                unsigned int size);

void swr_init_scratch_buffers(struct swr_context *ctx);
void swr_destroy_scratch_buffers(struct swr_context *ctx);

#endif
