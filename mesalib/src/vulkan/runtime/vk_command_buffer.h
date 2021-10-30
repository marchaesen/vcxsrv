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

#ifndef VK_COMMAND_BUFFER_H
#define VK_COMMAND_BUFFER_H

#include "vk_object.h"
#include "util/u_dynarray.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vk_command_buffer {
   struct vk_object_base base;

   /**
    * VK_EXT_debug_utils
    *
    * The next two fields represent debug labels storage.
    *
    * VK_EXT_debug_utils spec requires that upon triggering a debug message
    * with a command buffer attached to it, all "active" labels will also be
    * provided to the callback. The spec describes two distinct ways of
    * attaching a debug label to the command buffer: opening a label region
    * and inserting a single label.
    *
    * Label region is active between the corresponding `*BeginDebugUtilsLabel`
    * and `*EndDebugUtilsLabel` calls. The spec doesn't mention any limits on
    * nestedness of label regions. This implementation assumes that there
    * aren't any.
    *
    * The spec, however, doesn't explain the lifetime of a label submitted by
    * an `*InsertDebugUtilsLabel` call. The LunarG whitepaper [1] (pp 12-15)
    * provides a more detailed explanation along with some examples. According
    * to those, such label remains active until the next `*DebugUtilsLabel`
    * call. This means that there can be no more than one such label at a
    * time.
    *
    * \c labels contains all active labels at this point in order of submission
    * \c region_begin denotes whether the most recent label opens a new region
    * If \t labels is empty \t region_begin must be true.
    *
    * Anytime we modify labels, we first check for \c region_begin. If it's
    * false, it means that the most recent label was submitted by
    * `*InsertDebugUtilsLabel` and we need to remove it before doing anything
    * else.
    *
    * See the discussion here:
    * https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/10318#note_1061317
    *
    * [1] https://www.lunarg.com/wp-content/uploads/2018/05/Vulkan-Debug-Utils_05_18_v1.pdf
    */
   struct util_dynarray labels;
   bool region_begin;
};

VK_DEFINE_HANDLE_CASTS(vk_command_buffer, base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

VkResult MUST_CHECK
vk_command_buffer_init(struct vk_command_buffer *command_buffer,
                       struct vk_device *device);

void
vk_command_buffer_reset(struct vk_command_buffer *command_buffer);

void
vk_command_buffer_finish(struct vk_command_buffer *command_buffer);

#ifdef __cplusplus
}
#endif

#endif  /* VK_COMMAND_BUFFER_H */
