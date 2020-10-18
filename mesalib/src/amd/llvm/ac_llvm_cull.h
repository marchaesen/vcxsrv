/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#ifndef AC_LLVM_CULL_H
#define AC_LLVM_CULL_H

#include "ac_llvm_build.h"

struct ac_cull_options {
   /* In general, I recommend setting all to true except view Z culling,
    * which isn't so effective because W culling is cheaper and partially
    * replaces near Z culling, and you don't need to set Position.z
    * if Z culling is disabled.
    *
    * If something doesn't work, turn some of these off to find out what.
    */
   bool cull_front;
   bool cull_back;
   bool cull_view_xy;
   bool cull_view_near_z;
   bool cull_view_far_z;
   bool cull_small_prims;
   bool cull_zero_area;
   bool cull_w; /* cull primitives with all W < 0 */

   bool use_halfz_clip_space;
};

LLVMValueRef ac_cull_triangle(struct ac_llvm_context *ctx, LLVMValueRef pos[3][4],
                              LLVMValueRef initially_accepted, LLVMValueRef vp_scale[2],
                              LLVMValueRef vp_translate[2], LLVMValueRef small_prim_precision,
                              struct ac_cull_options *options);

#endif
