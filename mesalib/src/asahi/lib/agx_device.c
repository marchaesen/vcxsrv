/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright 2019 Collabora, Ltd.
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
 */

#include <inttypes.h>
#include "agx_device.h"
#include "agx_bo.h"
#include "decode.h"

unsigned AGX_FAKE_HANDLE = 0;
uint64_t AGX_FAKE_LO = 0;
uint64_t AGX_FAKE_HI = (1ull << 32);

static void
agx_bo_free(struct agx_device *dev, struct agx_bo *bo)
{
#if __APPLE__
   const uint64_t handle = bo->handle;

   kern_return_t ret = IOConnectCallScalarMethod(dev->fd,
                       AGX_SELECTOR_FREE_MEM,
                       &handle, 1, NULL, NULL);

   if (ret)
      fprintf(stderr, "error freeing BO mem: %u\n", ret);
#else
   free(bo->ptr.cpu);
#endif

   /* Reset the handle */
   memset(bo, 0, sizeof(*bo));
}

void
agx_shmem_free(struct agx_device *dev, unsigned handle)
{
#if __APPLE__
	const uint64_t input = handle;
   kern_return_t ret = IOConnectCallScalarMethod(dev->fd,
                       AGX_SELECTOR_FREE_SHMEM,
                       &input, 1, NULL, NULL);

   if (ret)
      fprintf(stderr, "error freeing shmem: %u\n", ret);
#else
#endif
}

struct agx_bo
agx_shmem_alloc(struct agx_device *dev, size_t size, bool cmdbuf)
{
   struct agx_bo bo;

#if __APPLE__
   struct agx_create_shmem_resp out = {};
   size_t out_sz = sizeof(out);

   uint64_t inputs[2] = {
      size,
      cmdbuf ? 1 : 0 // 2 - error reporting, 1 - no error reporting
   };

   kern_return_t ret = IOConnectCallMethod(dev->fd,
                                           AGX_SELECTOR_CREATE_SHMEM, inputs, 2, NULL, 0, NULL,
                                           NULL, &out, &out_sz);

   assert(ret == 0);
   assert(out_sz == sizeof(out));
   assert(out.size == size);
   assert(out.map != 0);

   bo = (struct agx_bo) {
      .type = cmdbuf ? AGX_ALLOC_CMDBUF : AGX_ALLOC_MEMMAP,
      .handle = out.id,
      .ptr.cpu = out.map,
      .size = out.size,
      .guid = 0, /* TODO? */
   };
#else
   bo = (struct agx_bo) {
      .type = cmdbuf ? AGX_ALLOC_CMDBUF : AGX_ALLOC_MEMMAP,
      .handle = AGX_FAKE_HANDLE++,
      .ptr.cpu = calloc(1, size),
      .size = size,
      .guid = 0, /* TODO? */
   };
#endif

   if (dev->debug & AGX_DBG_TRACE)
      agxdecode_track_alloc(&bo);

   return bo;
}

static struct agx_bo *
agx_bo_alloc(struct agx_device *dev, size_t size,
             uint32_t flags)
{
   struct agx_bo *bo;
   unsigned handle = 0;

#if __APPLE__
   bool write_combine = false;
   uint32_t mode = 0x430; // shared, ?

   uint32_t args_in[24] = { 0 };
   args_in[1] = write_combine ? 0x400 : 0x0;
   args_in[2] = 0x2580320; //0x18000; // unk
   args_in[3] = 0x1; // unk;
   args_in[4] = 0x4000101; //0x1000101; // unk
   args_in[5] = mode;
   args_in[16] = size;
   args_in[20] = flags;
   args_in[21] = 0x3;

   uint64_t out[10] = { 0 };
   size_t out_sz = sizeof(out);

   kern_return_t ret = IOConnectCallMethod(dev->fd,
                                           AGX_SELECTOR_ALLOCATE_MEM, NULL, 0, args_in,
                                           sizeof(args_in), NULL, 0, out, &out_sz);

   assert(ret == 0);
   assert(out_sz == sizeof(out));
   handle = (out[3] >> 32ull);
#else
   /* Faked software path until we have a DRM driver */
   handle = (++AGX_FAKE_HANDLE);
#endif

   pthread_mutex_lock(&dev->bo_map_lock);
   bo = agx_lookup_bo(dev, handle);
   pthread_mutex_unlock(&dev->bo_map_lock);

   /* Fresh handle */
   assert(!memcmp(bo, &((struct agx_bo) {}), sizeof(*bo)));

   bo->type = AGX_ALLOC_REGULAR;
   bo->size = size;
   bo->flags = flags;
   bo->dev = dev;
   bo->handle = handle;

   ASSERTED bool lo = (flags & 0x08000000);

#if __APPLE__
   bo->ptr.gpu = out[0];
   bo->ptr.cpu = (void *) out[1];
   bo->guid = out[5];
#else
   if (lo) {
      bo->ptr.gpu = AGX_FAKE_LO;
      AGX_FAKE_LO += bo->size;
   } else {
      bo->ptr.gpu = AGX_FAKE_HI;
      AGX_FAKE_HI += bo->size;
   }

   bo->ptr.gpu = (((uint64_t) bo->handle) << (lo ? 16 : 24));
   bo->ptr.cpu = calloc(1, bo->size);
#endif

   assert(bo->ptr.gpu < (1ull << (lo ? 32 : 40)));

   return bo;
}

void
agx_bo_reference(struct agx_bo *bo)
{
   if (bo) {
      ASSERTED int count = p_atomic_inc_return(&bo->refcnt);
      assert(count != 1);
   }
}

void
agx_bo_unreference(struct agx_bo *bo)
{
   if (!bo)
      return;

   /* Don't return to cache if there are still references */
   if (p_atomic_dec_return(&bo->refcnt))
      return;

   struct agx_device *dev = bo->dev;

   pthread_mutex_lock(&dev->bo_map_lock);

   /* Someone might have imported this BO while we were waiting for the
    * lock, let's make sure it's still not referenced before freeing it.
    */
   if (p_atomic_read(&bo->refcnt) == 0) {
      if (dev->debug & AGX_DBG_TRACE)
         agxdecode_track_free(bo);

      /* TODO: cache */
      agx_bo_free(dev, bo);

   }
   pthread_mutex_unlock(&dev->bo_map_lock);
}

struct agx_bo *
agx_bo_create(struct agx_device *dev, unsigned size, unsigned flags)
{
   struct agx_bo *bo;
   assert(size > 0);

   /* To maximize BO cache usage, don't allocate tiny BOs */
   size = ALIGN_POT(size, 4096);

   /* TODO: Cache fetch */
   bo = agx_bo_alloc(dev, size, flags);

   if (!bo) {
      fprintf(stderr, "BO creation failed\n");
      return NULL;
   }

   p_atomic_set(&bo->refcnt, 1);

   if (dev->debug & AGX_DBG_TRACE)
      agxdecode_track_alloc(bo);

   return bo;
}

static void
agx_get_global_ids(struct agx_device *dev)
{
#if __APPLE__
   uint64_t out[2] = {};
   size_t out_sz = sizeof(out);

   ASSERTED kern_return_t ret = IOConnectCallStructMethod(dev->fd,
                       AGX_SELECTOR_GET_GLOBAL_IDS,
                       NULL, 0, &out, &out_sz);

   assert(ret == 0);
   assert(out_sz == sizeof(out));
   assert(out[1] > out[0]);

   dev->next_global_id = out[0];
   dev->last_global_id = out[1];
#else
   dev->next_global_id = 0;
   dev->last_global_id = 0x1000000;
#endif
}

uint64_t
agx_get_global_id(struct agx_device *dev)
{
   if (unlikely(dev->next_global_id >= dev->last_global_id)) {
      agx_get_global_ids(dev);
   }

   return dev->next_global_id++;
}

/* Tries to open an AGX device, returns true if successful */

bool
agx_open_device(void *memctx, struct agx_device *dev)
{
#if __APPLE__
   kern_return_t ret;

   /* TODO: Support other models */
   CFDictionaryRef matching = IOServiceNameMatching("AGXAcceleratorG13G_B0");

   io_service_t service =
      IOServiceGetMatchingService(kIOMasterPortDefault, matching);

   if (!service)
      return false;

   ret = IOServiceOpen(service, mach_task_self(), AGX_SERVICE_TYPE, &dev->fd);

   if (ret)
      return false;

   const char *api = "Equestria";
   char in[16] = { 0 };
   assert(strlen(api) < sizeof(in));
   memcpy(in, api, strlen(api));

   ret = IOConnectCallStructMethod(dev->fd, AGX_SELECTOR_SET_API, in,
                                   sizeof(in), NULL, NULL);

   /* Oddly, the return codes are flipped for SET_API */
   if (ret != 1)
      return false;
#else
   /* Only open a fake AGX device on other operating systems if forced */
   if (!getenv("AGX_FAKE_DEVICE"))
      return false;
#endif

   dev->memctx = memctx;
   util_sparse_array_init(&dev->bo_map, sizeof(struct agx_bo), 512);

   /* XXX: why do BO ids below 6 mess things up..? */
   for (unsigned i = 0; i < 6; ++i)
      agx_bo_alloc(dev, 4096, AGX_MEMORY_TYPE_FRAMEBUFFER);

   dev->queue = agx_create_command_queue(dev);
   dev->cmdbuf = agx_shmem_alloc(dev, 0x4000, true); // length becomes kernelCommandDataSize
   dev->memmap = agx_shmem_alloc(dev, 0x4000, false);
   agx_get_global_ids(dev);

   return true;
}

void
agx_close_device(struct agx_device *dev)
{
   util_sparse_array_finish(&dev->bo_map);

#if __APPLE__
   kern_return_t ret = IOServiceClose(dev->fd);

   if (ret)
      fprintf(stderr, "Error from IOServiceClose: %u\n", ret);
#endif
}

#if __APPLE__
static struct agx_notification_queue
agx_create_notification_queue(mach_port_t connection)
{
   struct agx_create_notification_queue_resp resp;
   size_t resp_size = sizeof(resp);
   assert(resp_size == 0x10);

   ASSERTED kern_return_t ret = IOConnectCallStructMethod(connection,
                       AGX_SELECTOR_CREATE_NOTIFICATION_QUEUE,
                       NULL, 0, &resp, &resp_size);

   assert(resp_size == sizeof(resp));
   assert(ret == 0);

   mach_port_t notif_port = IODataQueueAllocateNotificationPort();
   IOConnectSetNotificationPort(connection, 0, notif_port, resp.unk2);

   return (struct agx_notification_queue) {
      .port = notif_port,
      .queue = resp.queue,
      .id = resp.unk2
   };
}
#endif

struct agx_command_queue
agx_create_command_queue(struct agx_device *dev)
{
#if __APPLE__
   struct agx_command_queue queue = {};

   {
      uint8_t buffer[1024 + 8] = { 0 };
      const char *path = "/tmp/a.out";
      assert(strlen(path) < 1022);
      memcpy(buffer + 0, path, strlen(path));

      /* Copy to the end */
      unsigned END_LEN = MIN2(strlen(path), 1024 - strlen(path));
      unsigned SKIP = strlen(path) - END_LEN;
      unsigned OFFS = 1024 - END_LEN;
      memcpy(buffer + OFFS, path + SKIP, END_LEN);

      buffer[1024] = 0x2;

      struct agx_create_command_queue_resp out = {};
      size_t out_sz = sizeof(out);

      ASSERTED kern_return_t ret = IOConnectCallStructMethod(dev->fd,
                          AGX_SELECTOR_CREATE_COMMAND_QUEUE,
                          buffer, sizeof(buffer),
                          &out, &out_sz);

      assert(ret == 0);
      assert(out_sz == sizeof(out));

      queue.id = out.id;
      assert(queue.id);
   }

   queue.notif = agx_create_notification_queue(dev->fd);

   {
      uint64_t scalars[2] = {
         queue.id,
         queue.notif.id
      };

      ASSERTED kern_return_t ret = IOConnectCallScalarMethod(dev->fd,
                          0x1D,
                          scalars, 2, NULL, NULL);

      assert(ret == 0);
   }

   {
      uint64_t scalars[2] = {
         queue.id,
         0x1ffffffffull
      };

      ASSERTED kern_return_t ret = IOConnectCallScalarMethod(dev->fd,
                          0x29,
                          scalars, 2, NULL, NULL);

      assert(ret == 0);
   }

   return queue;
#else
   return (struct agx_command_queue) {
      0
   };
#endif
}

void
agx_submit_cmdbuf(struct agx_device *dev, unsigned cmdbuf, unsigned mappings, uint64_t scalar)
{
#if __APPLE__
   struct agx_submit_cmdbuf_req req = {
      .unk0 = 0x10,
      .unk1 = 0x1,
      .cmdbuf = cmdbuf,
      .mappings = mappings,
      .user_0 = (void *) ((uintptr_t) 0xABCD), // Passed in the notif queue
      .user_1 = (void *) ((uintptr_t) 0x1234), // Maybe pick better
      .unk2 = 0x0,
      .unk3 = 0x1,
   };

   assert(sizeof(req) == 40);

   ASSERTED kern_return_t ret = IOConnectCallMethod(dev->fd,
                                           AGX_SELECTOR_SUBMIT_COMMAND_BUFFERS,
                                           &scalar, 1,
                                           &req, sizeof(req),
                                           NULL, 0, NULL, 0);
   assert(ret == 0);
   return;
#endif
}

void
agx_wait_queue(struct agx_command_queue queue)
{
#if __APPLE__
   IOReturn ret = IODataQueueWaitForAvailableData(queue.notif.queue, queue.notif.port);

	   uint64_t data[4];
	   unsigned sz = sizeof(data);
      ret = IODataQueueDequeue(queue.notif.queue, data, &sz);
      assert(sz == sizeof(data));
      assert(data[0] == 0xABCD);

      ret = IODataQueueWaitForAvailableData(queue.notif.queue, queue.notif.port);
      ret = IODataQueueDequeue(queue.notif.queue, data, &sz);
      assert(sz == sizeof(data));
      assert(data[0] == 0x1234);

   assert(!IODataQueueDataAvailable(queue.notif.queue));
#endif
}
