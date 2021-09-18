/*
 * Copyright (C) 2021 Alyssa Rosenzweig
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __ASAHI_MAGIC_H
#define __ASAHI_MAGIC_H

unsigned
demo_cmdbuf(uint64_t *buf, size_t size,
            struct agx_pool *pool,
            uint64_t encoder_ptr,
            uint64_t encoder_id,
            uint64_t scissor_ptr,
            unsigned width, unsigned height,
            uint32_t pipeline_null,
            uint32_t pipeline_clear,
            uint32_t pipeline_store,
            uint64_t rt0,
            bool clear_pipeline_textures);

void
demo_mem_map(void *map, size_t size, unsigned *handles,
             unsigned count, uint64_t cmdbuf_id, uint64_t
             encoder_id, unsigned cmdbuf_size);

#endif
