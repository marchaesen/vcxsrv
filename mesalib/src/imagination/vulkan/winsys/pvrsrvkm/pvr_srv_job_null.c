/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include "pvr_srv_job_null.h"
#include "pvr_srv_sync.h"
#include "pvr_winsys.h"
#include "util/libsync.h"
#include "vk_log.h"
#include "vk_sync.h"

VkResult pvr_srv_winsys_null_job_submit(struct pvr_winsys *ws,
                                        struct vk_sync **waits,
                                        uint32_t wait_count,
                                        struct vk_sync *signal_sync)
{
   struct pvr_srv_sync *srv_signal_sync = to_srv_sync(signal_sync);
   int fd = -1;

   assert(signal_sync);

   for (uint32_t i = 0; i < wait_count; i++) {
      struct pvr_srv_sync *srv_wait_sync = to_srv_sync(waits[i]);
      int ret;

      if (!waits[i] || srv_wait_sync->fd < 0)
         continue;

      ret = sync_accumulate("", &fd, srv_wait_sync->fd);
      if (ret) {
         if (fd >= 0)
            close(fd);

         return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   }

   pvr_srv_set_sync_payload(srv_signal_sync, fd);

   return VK_SUCCESS;
}
