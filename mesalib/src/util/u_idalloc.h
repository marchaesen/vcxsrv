/**************************************************************************
 *
 * Copyright 2017 Valve Corporation
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
 * IN NO EVENT SHALL THE AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef U_IDALLOC_H
#define U_IDALLOC_H

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

struct util_idalloc
{
   uint32_t *data;
   unsigned num_elements;
   unsigned lowest_free_idx;
};

void
util_idalloc_init(struct util_idalloc *buf);

void
util_idalloc_fini(struct util_idalloc *buf);

void
util_idalloc_resize(struct util_idalloc *buf, unsigned new_num_elements);

unsigned
util_idalloc_alloc(struct util_idalloc *buf);

void
util_idalloc_free(struct util_idalloc *buf, unsigned id);

void
util_idalloc_reserve(struct util_idalloc *buf, unsigned id);

#ifdef __cplusplus
}
#endif

#endif /* U_IDALLOC_H */
