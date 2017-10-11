/*
 * Copyright © 2016 Broadcom
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
 */

struct v3d_compiler *
v3d_compiler_init(void)
{
        struct v3d_compile *c = rzalloc(struct v3d_compile);

        return c;
}

void
v3d_add_qpu_inst(struct v3d_compiler *c, uint64_t inst)
{
        if (c->qpu_inst_count >= c->qpu_inst_size) {
                c->qpu_inst_size = MAX2(c->qpu_inst_size * 2, 16);
                c->qpu_insts = reralloc(c, c->qpu_insts, uint64_t,
                                        c->qpu_inst_size_array_size);

        }

        c->qpu_insts[c->qpu_inst_count++] = inst;
}
