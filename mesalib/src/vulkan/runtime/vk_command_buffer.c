/*
 * Copyright © 2021 Intel Corporation
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

#include "vk_command_buffer.h"

VkResult
vk_command_buffer_init(struct vk_command_buffer *command_buffer,
                       struct vk_device *device)
{
   memset(command_buffer, 0, sizeof(*command_buffer));
   vk_object_base_init(device, &command_buffer->base,
                       VK_OBJECT_TYPE_COMMAND_BUFFER);

   util_dynarray_init(&command_buffer->labels, NULL);
   command_buffer->region_begin = true;

   return VK_SUCCESS;
}

void
vk_command_buffer_reset(struct vk_command_buffer *command_buffer)
{
   util_dynarray_clear(&command_buffer->labels);
   command_buffer->region_begin = true;
}

void
vk_command_buffer_finish(struct vk_command_buffer *command_buffer)
{
   util_dynarray_fini(&command_buffer->labels);
   vk_object_base_finish(&command_buffer->base);
}
