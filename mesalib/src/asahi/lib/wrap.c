/*
 * Copyright 2021-2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <IOKit/IODataQueueClient.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>

#include "util/compiler.h"
#include "util/u_hexdump.h"
#include "decode.h"
#include "dyld_interpose.h"
#include "util.h"

#define HANDLE(x) (x ^ (1 << 29))

/*
 * This section contains the minimal set of definitions to trace the macOS
 * (IOKit) interface to the AGX accelerator.
 * They are not used under Linux.
 *
 * Information is this file was originally determined independently. More
 * recently, names have been augmented via the oob_timestamp code sample from
 * Project Zero [1]
 *
 * [1] https://bugs.chromium.org/p/project-zero/issues/detail?id=1986
 */

#define AGX_SERVICE_TYPE 0x100005

enum agx_selector {
   AGX_SELECTOR_GET_GLOBAL_IDS = 0x6,
   AGX_SELECTOR_SET_API = 0x7,
   AGX_SELECTOR_CREATE_COMMAND_QUEUE = 0x8,
   AGX_SELECTOR_FREE_COMMAND_QUEUE = 0x9,
   AGX_SELECTOR_ALLOCATE_MEM = 0xA,
   AGX_SELECTOR_FREE_MEM = 0xB,
   AGX_SELECTOR_CREATE_SHMEM = 0xF,
   AGX_SELECTOR_FREE_SHMEM = 0x10,
   AGX_SELECTOR_CREATE_NOTIFICATION_QUEUE = 0x11,
   AGX_SELECTOR_FREE_NOTIFICATION_QUEUE = 0x12,
   AGX_SELECTOR_SUBMIT_COMMAND_BUFFERS = 0x1E,
   AGX_SELECTOR_GET_VERSION = 0x2A,
   AGX_NUM_SELECTORS = 0x33
};

struct IOAccelCommandQueueSubmitArgs_Command {
   uint32_t command_buffer_shmem_id;
   uint32_t segment_list_shmem_id;
   uint64_t unk1B; // 0, new in 12.x
   uint64_t notify_1;
   uint64_t notify_2;
   uint32_t unk2;
   uint32_t unk3;
} __attribute__((packed));

struct agx_allocate_resource_resp {
   /* Returned GPU virtual address */
   uint64_t gpu_va;

   /* Returned CPU virtual address */
   uint64_t cpu;

   uint32_t unk4[3];

   /* Handle used to identify the resource in the segment list */
   uint32_t handle;

   /* Size of the root resource from which we are allocated. If this is not a
    * suballocation, this is equal to the size.
    */
   uint64_t root_size;

   /* Globally unique identifier for the resource, shown in Instruments */
   uint32_t guid;

   uint32_t unk11[7];

   /* Maximum size of the suballocation. For a suballocation, this equals:
    *
    *    sub_size = root_size - (sub_cpu - root_cpu)
    *
    * For root allocations, this equals the size.
    */
   uint64_t sub_size;
} __attribute__((packed));

/*
 * Wrap IOKit entrypoints to intercept communication between the AGX kernel
 * extension and userspace clients. IOKit prototypes are public from the IOKit
 * source release.
 */

mach_port_t metal_connection = 0;

struct agxdecode_ctx *decode_ctx = NULL;

kern_return_t
wrap_Method(mach_port_t connection, uint32_t selector, const uint64_t *input,
            uint32_t inputCnt, const void *inputStruct, size_t inputStructCnt,
            uint64_t *output, uint32_t *outputCnt, void *outputStruct,
            size_t *outputStructCntP)
{
   if (!decode_ctx) {
      decode_ctx = agxdecode_new_context(0);
   }

   /* Heuristic guess which connection is Metal, skip over I/O from everything
    * else. This is technically wrong but it works in practice, and reduces the
    * surface area we need to wrap.
    */
   if (selector == AGX_SELECTOR_SET_API) {
      metal_connection = connection;
   } else if (metal_connection != connection) {
      return IOConnectCallMethod(connection, selector, input, inputCnt,
                                 inputStruct, inputStructCnt, output, outputCnt,
                                 outputStruct, outputStructCntP);
   }

   printf("Selector %u, %X, %X\n", selector, connection, metal_connection);

   /* Check the arguments make sense */
   assert((input != NULL) == (inputCnt != 0));
   assert((inputStruct != NULL) == (inputStructCnt != 0));
   assert((output != NULL) == (outputCnt != 0));
   assert((outputStruct != NULL) == (outputStructCntP != 0));

   /* Dump inputs */
   switch (selector) {
   case AGX_SELECTOR_SET_API:
      assert(input == NULL && output == NULL && outputStruct == NULL);
      assert(inputStruct != NULL && inputStructCnt == 16);
      assert(((uint8_t *)inputStruct)[15] == 0x0);

      printf("%X: SET_API(%s)\n", connection, (const char *)inputStruct);
      break;

   case AGX_SELECTOR_SUBMIT_COMMAND_BUFFERS: {
      assert(output == NULL && outputStruct == NULL);
      // assert(inputCnt == 1);

      printf("%X: SUBMIT_COMMAND_BUFFERS command queue id:%llx %p\n",
             connection, input[0], inputStruct);

      u_hexdump(stdout, inputStruct, inputStructCnt, true);
      const struct IOAccelCommandQueueSubmitArgs_Command *cmds =
         (void *)(inputStruct + 0);

      //  for (unsigned i = 0; i < hdr->count; ++i) {
      const struct IOAccelCommandQueueSubmitArgs_Command *req = &cmds[0];
      agxdecode_cmdstream(decode_ctx, HANDLE(req->command_buffer_shmem_id),
                          HANDLE(req->segment_list_shmem_id), true);
      // }

      agxdecode_next_frame();
      FALLTHROUGH;
   }

   default:
      printf("%X: call %X (out %p, %zu)", connection, selector,
             outputStructCntP, outputStructCntP ? *outputStructCntP : 0);

      for (uint64_t u = 0; u < inputCnt; ++u)
         printf(" %llx", input[u]);

      if (inputStructCnt) {
         printf(", struct:\n");
         u_hexdump(stdout, inputStruct, inputStructCnt, true);
      } else {
         printf("\n");
      }

      break;
   }

   /* Invoke the real method */
   kern_return_t ret = IOConnectCallMethod(
      connection, selector, input, inputCnt, inputStruct, inputStructCnt,
      output, outputCnt, outputStruct, outputStructCntP);

   if (ret != 0)
      printf("return %u\n", ret);

   /* Track allocations for later analysis (dumping, disassembly, etc) */
   switch (selector) {
   case AGX_SELECTOR_CREATE_SHMEM: {
      assert(inputCnt == 2);
      assert((*outputStructCntP) == 0x10);
      uint64_t *inp = (uint64_t *)input;

      uint8_t type = inp[1];

      assert(type <= 2);
      if (type == 2)
         printf("(cmdbuf with error reporting)\n");

      uint64_t *ptr = (uint64_t *)outputStruct;
      uint32_t *words = (uint32_t *)(ptr + 1);

      /* Construct a synthetic GEM handle for the shmem */
      agxdecode_track_alloc(decode_ctx, &(struct agx_bo){
                                           .handle = HANDLE(words[1]),
                                           ._map = (void *)*ptr,
                                           .size = words[0],
                                        });

      break;
   }

   case AGX_SELECTOR_ALLOCATE_MEM: {
      assert((*outputStructCntP) == 0x50);
      const struct agx_allocate_resource_req *req = inputStruct;
      struct agx_allocate_resource_resp *resp = outputStruct;

      struct agx_va *va = malloc(sizeof(struct agx_va));
      va->addr = resp->gpu_va;
      va->size_B = resp->sub_size;

      agxdecode_track_alloc(decode_ctx, &(struct agx_bo){
                                           .size = resp->sub_size,
                                           .handle = resp->handle,
                                           .va = va,
                                           ._map = (void *)resp->cpu,
                                        });

      break;
   }

   case AGX_SELECTOR_FREE_MEM: {
      assert(inputCnt == 1);
      assert(inputStruct == NULL);
      assert(output == NULL);
      assert(outputStruct == NULL);

      agxdecode_track_free(decode_ctx, &(struct agx_bo){.handle = input[0]});

      break;
   }

   case AGX_SELECTOR_FREE_SHMEM: {
      assert(inputCnt == 1);
      assert(inputStruct == NULL);
      assert(output == NULL);
      assert(outputStruct == NULL);

      agxdecode_track_free(decode_ctx,
                           &(struct agx_bo){.handle = HANDLE(input[0])});

      break;
   }

   default:
      /* Dump the outputs */
      if (outputCnt) {
         printf("%u scalars: ", *outputCnt);

         for (uint64_t u = 0; u < *outputCnt; ++u)
            printf("%llx ", output[u]);

         printf("\n");
      }

      if (outputStructCntP) {
         printf(" struct\n");
         u_hexdump(stdout, outputStruct, *outputStructCntP, true);

         if (selector == 2) {
            /* Dump linked buffer as well */
            void **o = outputStruct;
            u_hexdump(stdout, *o, 64, true);
         }
      }

      printf("\n");
      break;
   }

   return ret;
}

kern_return_t
wrap_AsyncMethod(mach_port_t connection, uint32_t selector,
                 mach_port_t wakePort, uint64_t *reference,
                 uint32_t referenceCnt, const uint64_t *input,
                 uint32_t inputCnt, const void *inputStruct,
                 size_t inputStructCnt, uint64_t *output, uint32_t *outputCnt,
                 void *outputStruct, size_t *outputStructCntP)
{
   /* Check the arguments make sense */
   assert((input != NULL) == (inputCnt != 0));
   assert((inputStruct != NULL) == (inputStructCnt != 0));
   assert((output != NULL) == (outputCnt != 0));
   assert((outputStruct != NULL) == (outputStructCntP != 0));

   printf("%X: call %X, wake port %X (out %p, %zu)", connection, selector,
          wakePort, outputStructCntP, outputStructCntP ? *outputStructCntP : 0);

   for (uint64_t u = 0; u < inputCnt; ++u)
      printf(" %llx", input[u]);

   if (inputStructCnt) {
      printf(", struct:\n");
      u_hexdump(stdout, inputStruct, inputStructCnt, true);
   } else {
      printf("\n");
   }

   printf(", references: ");
   for (unsigned i = 0; i < referenceCnt; ++i)
      printf(" %llx", reference[i]);
   printf("\n");

   kern_return_t ret = IOConnectCallAsyncMethod(
      connection, selector, wakePort, reference, referenceCnt, input, inputCnt,
      inputStruct, inputStructCnt, output, outputCnt, outputStruct,
      outputStructCntP);

   printf("return %u", ret);

   if (outputCnt) {
      printf("%u scalars: ", *outputCnt);

      for (uint64_t u = 0; u < *outputCnt; ++u)
         printf("%llx ", output[u]);

      printf("\n");
   }

   if (outputStructCntP) {
      printf(" struct\n");
      u_hexdump(stdout, outputStruct, *outputStructCntP, true);

      if (selector == 2) {
         /* Dump linked buffer as well */
         void **o = outputStruct;
         u_hexdump(stdout, *o, 64, true);
      }
   }

   printf("\n");
   return ret;
}

kern_return_t
wrap_StructMethod(mach_port_t connection, uint32_t selector,
                  const void *inputStruct, size_t inputStructCnt,
                  void *outputStruct, size_t *outputStructCntP)
{
   return wrap_Method(connection, selector, NULL, 0, inputStruct,
                      inputStructCnt, NULL, NULL, outputStruct,
                      outputStructCntP);
}

kern_return_t
wrap_AsyncStructMethod(mach_port_t connection, uint32_t selector,
                       mach_port_t wakePort, uint64_t *reference,
                       uint32_t referenceCnt, const void *inputStruct,
                       size_t inputStructCnt, void *outputStruct,
                       size_t *outputStructCnt)
{
   return wrap_AsyncMethod(connection, selector, wakePort, reference,
                           referenceCnt, NULL, 0, inputStruct, inputStructCnt,
                           NULL, NULL, outputStruct, outputStructCnt);
}

kern_return_t
wrap_ScalarMethod(mach_port_t connection, uint32_t selector,
                  const uint64_t *input, uint32_t inputCnt, uint64_t *output,
                  uint32_t *outputCnt)
{
   return wrap_Method(connection, selector, input, inputCnt, NULL, 0, output,
                      outputCnt, NULL, NULL);
}

kern_return_t
wrap_AsyncScalarMethod(mach_port_t connection, uint32_t selector,
                       mach_port_t wakePort, uint64_t *reference,
                       uint32_t referenceCnt, const uint64_t *input,
                       uint32_t inputCnt, uint64_t *output, uint32_t *outputCnt)
{
   return wrap_AsyncMethod(connection, selector, wakePort, reference,
                           referenceCnt, input, inputCnt, NULL, 0, output,
                           outputCnt, NULL, NULL);
}

mach_port_t
wrap_DataQueueAllocateNotificationPort()
{
   mach_port_t ret = IODataQueueAllocateNotificationPort();
   printf("Allocated notif port %X\n", ret);
   return ret;
}

kern_return_t
wrap_SetNotificationPort(io_connect_t connect, uint32_t type, mach_port_t port,
                         uintptr_t reference)
{
   printf(
      "Set noficiation port connect=%X, type=%X, port=%X, reference=%" PRIx64
      "\n",
      connect, type, port, (uint64_t)reference);

   return IOConnectSetNotificationPort(connect, type, port, reference);
}

IOReturn
wrap_DataQueueWaitForAvailableData(IODataQueueMemory *dataQueue,
                                   mach_port_t notificationPort)
{
   printf("Waiting for data queue at notif port %X\n", notificationPort);
   IOReturn ret = IODataQueueWaitForAvailableData(dataQueue, notificationPort);
   printf("ret=%X\n", ret);
   return ret;
}

IODataQueueEntry *
wrap_DataQueuePeek(IODataQueueMemory *dataQueue)
{
   printf("Peeking data queue\n");
   return IODataQueuePeek(dataQueue);
}

IOReturn
wrap_DataQueueDequeue(IODataQueueMemory *dataQueue, void *data,
                      uint32_t *dataSize)
{
   printf("Dequeueing (dataQueue=%p, data=%p, buffer %u)\n", dataQueue, data,
          *dataSize);
   IOReturn ret = IODataQueueDequeue(dataQueue, data, dataSize);
   printf("Return \"%s\", got %u bytes\n", mach_error_string(ret), *dataSize);

   uint8_t *data8 = data;
   for (unsigned i = 0; i < *dataSize; ++i) {
      printf("%02X ", data8[i]);
   }
   printf("\n");

   return ret;
}

DYLD_INTERPOSE(wrap_Method, IOConnectCallMethod);
DYLD_INTERPOSE(wrap_AsyncMethod, IOConnectCallAsyncMethod);
DYLD_INTERPOSE(wrap_StructMethod, IOConnectCallStructMethod);
DYLD_INTERPOSE(wrap_AsyncStructMethod, IOConnectCallAsyncStructMethod);
DYLD_INTERPOSE(wrap_ScalarMethod, IOConnectCallScalarMethod);
DYLD_INTERPOSE(wrap_AsyncScalarMethod, IOConnectCallAsyncScalarMethod);
DYLD_INTERPOSE(wrap_SetNotificationPort, IOConnectSetNotificationPort);
DYLD_INTERPOSE(wrap_DataQueueAllocateNotificationPort,
               IODataQueueAllocateNotificationPort);
DYLD_INTERPOSE(wrap_DataQueueWaitForAvailableData,
               IODataQueueWaitForAvailableData);
DYLD_INTERPOSE(wrap_DataQueuePeek, IODataQueuePeek);
DYLD_INTERPOSE(wrap_DataQueueDequeue, IODataQueueDequeue);
