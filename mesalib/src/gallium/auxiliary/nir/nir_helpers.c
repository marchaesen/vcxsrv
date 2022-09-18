/**************************************************************************
 *
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include "nir_helpers.h"
#include "nir_xfb_info.h"

void
nir_gather_stream_output_info(nir_shader *nir,
                              struct pipe_stream_output_info *so)
{
   int slot_to_register[NUM_TOTAL_VARYING_SLOTS];
   nir_xfb_info *info = nir_gather_xfb_info_from_intrinsics(nir, slot_to_register);

   memset(so, 0, sizeof(*so));

   if (!info)
      return;

   so->num_outputs = info->output_count;

   for (unsigned i = 0; i < info->output_count; i++) {
      so->output[i].start_component = info->outputs[i].component_offset;
      so->output[i].num_components = util_bitcount(info->outputs[i].component_mask);
      so->output[i].output_buffer = info->outputs[i].buffer;
      so->output[i].dst_offset = info->outputs[i].offset / 4;
      so->output[i].stream = info->buffer_to_stream[info->outputs[i].buffer];
      so->output[i].register_index = slot_to_register[info->outputs[i].location];
   }

   for (unsigned i = 0; i < MAX_XFB_BUFFERS; i++)
      so->stride[i] = info->buffers[i].stride;

   free(info);
}
