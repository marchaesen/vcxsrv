/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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
 *
 */

#ifndef AC_MSGPACK_H
#define AC_MSGPACK_H

struct ac_msgpack {
   uint8_t *mem;
   uint32_t mem_size;
   uint32_t offset;
};

void ac_msgpack_init(struct ac_msgpack *msgpack);
void ac_msgpack_destroy(struct ac_msgpack *msgpack);
int ac_msgpack_resize_if_required(struct ac_msgpack *msgpack,
                                  uint32_t data_size);
void ac_msgpack_add_fixmap_op(struct ac_msgpack *msgpack, uint32_t n);
void ac_msgpack_add_fixarray_op(struct ac_msgpack *msgpack, uint32_t n);
void ac_msgpack_add_fixstr(struct ac_msgpack *msgpack, char *str);
void ac_msgpack_add_uint(struct ac_msgpack *msgpack, uint64_t val);
void ac_msgpack_add_int(struct ac_msgpack *msgpack, int64_t val);

#endif
