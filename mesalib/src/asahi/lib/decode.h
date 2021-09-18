/*
 * Copyright (C) 2017-2019 Lyude Paul
 * Copyright (C) 2017-2019 Alyssa Rosenzweig
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __AGX_DECODE_H__
#define __AGX_DECODE_H__

#include "agx_bo.h"

void agxdecode_next_frame(void);

void agxdecode_close(void);

void agxdecode_cmdstream(unsigned cmdbuf_index, unsigned map_index, bool verbose);

void agxdecode_dump_file_open(void);

void agxdecode_track_alloc(struct agx_bo *alloc);

void agxdecode_dump_mappings(unsigned map_index);

void agxdecode_track_free(struct agx_bo *bo);

#endif /* __MMAP_TRACE_H__ */
