/*
 * Copyright © 2024 Intel Corporation
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

/*
 * Copyright (C) 2015-2021 Valve Corporation
 * Copyright (C) 2015-2021 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Cody Northrop <cody@lunarg.com>
 * Author: David Pinedo <david@lunarg.com>
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Tony Barbour <tony@lunarg.com>
 */

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <png.h>
#include <time.h>

#include <vulkan/vulkan_core.h>
#include <vulkan/vk_layer.h>

#include "git_sha1.h"

#include "screenshot_params.h"

#include "util/u_debug.h"
#include "util/hash_table.h"
#include "util/list.h"
#include "util/ralloc.h"
#include "util/os_time.h"
#include "util/os_socket.h"
#include "util/simple_mtx.h"
#include "util/u_math.h"

#include "vk_enum_to_str.h"
#include "vk_dispatch_table.h"
#include "vk_util.h"

typedef pthread_mutex_t loader_platform_thread_mutex;
static inline void loader_platform_thread_create_mutex(loader_platform_thread_mutex *pMutex) { pthread_mutex_init(pMutex, NULL); }
static inline void loader_platform_thread_lock_mutex(loader_platform_thread_mutex *pMutex) { pthread_mutex_lock(pMutex); }
static inline void loader_platform_thread_unlock_mutex(loader_platform_thread_mutex *pMutex) { pthread_mutex_unlock(pMutex); }
static inline void loader_platform_thread_delete_mutex(loader_platform_thread_mutex *pMutex) { pthread_mutex_destroy(pMutex); }

static int globalLockInitialized = 0;
static loader_platform_thread_mutex globalLock;

/* Mapped from VkInstace/VkPhysicalDevice */
struct instance_data {
   struct vk_instance_dispatch_table vtable;
   struct vk_physical_device_dispatch_table pd_vtable;
   VkInstance instance;

   struct screenshot_params params;

   int control_client;
   int socket_fd;

   /* Enabling switch for taking screenshot */
   bool screenshot_enabled;

   /* Region switch for enabling region use on a per-frame basis */
   bool region_enabled;

   /* Enabling switch for socket communications */
   bool socket_enabled;
   bool socket_setup;

   const char *filename;
};

pthread_cond_t ptCondition = PTHREAD_COND_INITIALIZER;
pthread_mutex_t ptLock = PTHREAD_MUTEX_INITIALIZER;

VkFence copyDone;
const VkPipelineStageFlags dstStageWaitBeforeSubmission = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
const VkSemaphore *pSemaphoreWaitBeforePresent;
uint32_t semaphoreWaitBeforePresentCount;
VkSemaphore semaphoreWaitAfterSubmission;

/* Mapped from VkDevice */
struct queue_data;
struct device_data {
   struct instance_data *instance;

   PFN_vkSetDeviceLoaderData set_device_loader_data;

   struct vk_device_dispatch_table vtable;
   VkPhysicalDevice physical_device;
   VkDevice device;

   VkPhysicalDeviceProperties properties;

   struct queue_data *graphic_queue;
   struct queue_data* queue_data_head;
   struct queue_data* queue_data_tail;
};

/* Mapped from VkQueue */
struct queue_data {
   struct device_data *device;
   struct queue_data *next;
   VkQueue queue;
   uint32_t familyIndex;
   uint32_t index;
};

/* Mapped from VkSwapchainKHR */
struct swapchain_data {
   struct device_data *device;

   VkSwapchainKHR swapchain;
   VkExtent2D imageExtent;
   VkFormat format;

   VkImage image;
   uint32_t imageListSize;
};

static struct hash_table_u64 *vk_object_to_data = NULL;
static simple_mtx_t vk_object_to_data_mutex = SIMPLE_MTX_INITIALIZER;

static inline void ensure_vk_object_map(void)
{
   if (!vk_object_to_data)
      vk_object_to_data = _mesa_hash_table_u64_create(NULL);
}

#define HKEY(obj) ((uint64_t)(obj))
#define FIND(type, obj) ((type *)find_object_data(HKEY(obj)))

static void *find_object_data(uint64_t obj)
{
   simple_mtx_lock(&vk_object_to_data_mutex);
   ensure_vk_object_map();
   void *data = _mesa_hash_table_u64_search(vk_object_to_data, obj);
   simple_mtx_unlock(&vk_object_to_data_mutex);
   return data;
}

static void map_object(uint64_t obj, void *data)
{
   simple_mtx_lock(&vk_object_to_data_mutex);
   ensure_vk_object_map();
   _mesa_hash_table_u64_insert(vk_object_to_data, obj, data);
   simple_mtx_unlock(&vk_object_to_data_mutex);
}

static void unmap_object(uint64_t obj)
{
   simple_mtx_lock(&vk_object_to_data_mutex);
   _mesa_hash_table_u64_remove(vk_object_to_data, obj);
   simple_mtx_unlock(&vk_object_to_data_mutex);
}

void map_images(swapchain_data *data, VkImage *imageList, uint32_t size) {
   data->imageListSize = size;
   VkImage *image;
   image = (VkImage *)malloc(sizeof(VkImage) * size);
   for (uint32_t index = 0; index < size; index++) {
      image[index] = imageList[index];
      map_object(HKEY(index), &image[index]);
   }
}

void select_image_from_map(swapchain_data *data, uint32_t index) {
   data->image = *(FIND(VkImage, index));
}

void unmap_images(swapchain_data *data) {
   VkImage *image, *first;
   first = nullptr;
   for (uint32_t index = 0; index < data->imageListSize; index++) {
      image = FIND(VkImage, index);
      if (!first)
         first = image;
      unmap_object(HKEY(index));
   }
   free(first);
   data->imageListSize = 0;
}

#define VK_CHECK(expr) \
   do { \
      VkResult __result = (expr); \
      if (__result != VK_SUCCESS) { \
         LOG(ERROR, "'%s' line %i failed with %s\n", \
             #expr, __LINE__, vk_Result_to_str(__result)); \
      } \
   } while (0)

static VkLayerInstanceCreateInfo *get_instance_chain_info(const VkInstanceCreateInfo *pCreateInfo,
                                                          VkLayerFunction func)
{
   vk_foreach_struct_const(item, pCreateInfo->pNext) {
      if (item->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
          ((VkLayerInstanceCreateInfo *) item)->function == func)
         return (VkLayerInstanceCreateInfo *) item;
   }
   unreachable("instance chain info not found");
   return NULL;
}

static VkLayerDeviceCreateInfo *get_device_chain_info(const VkDeviceCreateInfo *pCreateInfo,
                                                      VkLayerFunction func)
{
   vk_foreach_struct_const(item, pCreateInfo->pNext) {
      if (item->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
          ((VkLayerDeviceCreateInfo *) item)->function == func)
         return (VkLayerDeviceCreateInfo *)item;
   }
   unreachable("device chain info not found");
   return NULL;
}

/**/

static struct instance_data *new_instance_data(VkInstance instance)
{
   struct instance_data *data = rzalloc(NULL, struct instance_data);
   data->instance = instance;
   data->control_client = -1;
   data->socket_fd = -1;
   map_object(HKEY(data->instance), data);
   return data;
}

void destroy_instance_data(struct instance_data *data)
{
   destroy_frame_list(data->params.frames);
   if (data->socket_fd >= 0)
      os_socket_close(data->socket_fd);
   unmap_object(HKEY(data->instance));
   ralloc_free(data);
}

static void instance_data_map_physical_devices(struct instance_data *instance_data,
                                               bool map)
{
   uint32_t physicalDeviceCount = 0;
   instance_data->vtable.EnumeratePhysicalDevices(instance_data->instance,
                                                  &physicalDeviceCount,
                                                  NULL);

   VkPhysicalDevice *physicalDevices = (VkPhysicalDevice *) malloc(sizeof(VkPhysicalDevice) * physicalDeviceCount);
   instance_data->vtable.EnumeratePhysicalDevices(instance_data->instance,
                                                  &physicalDeviceCount,
                                                  physicalDevices);

   for (uint32_t i = 0; i < physicalDeviceCount; i++) {
      if (map)
         map_object(HKEY(physicalDevices[i]), instance_data);
      else
         unmap_object(HKEY(physicalDevices[i]));
   }

   free(physicalDevices);
}

/**/
static struct device_data *new_device_data(VkDevice device, struct instance_data *instance)
{
   struct device_data *data = rzalloc(NULL, struct device_data);
   data->instance = instance;
   data->device = device;
   data->graphic_queue = VK_NULL_HANDLE;
   data->queue_data_head = VK_NULL_HANDLE;
   data->queue_data_tail = VK_NULL_HANDLE;
   map_object(HKEY(data->device), data);
   return data;
}

static struct queue_data *new_queue_data(VkQueue queue,
                                         struct device_data *device_data,
                                         uint32_t index,
                                         uint32_t familyIndex)
{
   struct queue_data *data = rzalloc(device_data, struct queue_data);
   data->device = device_data;
   data->queue = queue;
   data->index = index;
   data->familyIndex = familyIndex;
   data->next = VK_NULL_HANDLE;
   map_object(HKEY(data->queue), data);
   if (device_data->queue_data_head == VK_NULL_HANDLE) {
      device_data->queue_data_head = data;
      device_data->queue_data_tail = data;
   } else {
      device_data->queue_data_tail->next = data;
      device_data->queue_data_tail = data;
   }
   return data;
}

static void destroy_queue(struct queue_data *data)
{
   struct device_data *device_data = data->device;
   unmap_object(HKEY(data->queue));
   ralloc_free(data);
}

static void device_destroy_queues(struct device_data *data)
{
   struct queue_data *tmp_queue = VK_NULL_HANDLE;
   for (auto it = data->queue_data_head; it != VK_NULL_HANDLE;) {
      tmp_queue = it->next;
      destroy_queue(it);
      it = tmp_queue;
   }
}

static void destroy_device_data(struct device_data *data)
{
   loader_platform_thread_lock_mutex(&globalLock);
   unmap_object(HKEY(data->device));
   ralloc_free(data);
   loader_platform_thread_unlock_mutex(&globalLock);
}

static struct swapchain_data *new_swapchain_data(VkSwapchainKHR swapchain,
                                                 struct device_data *device_data)
{
   struct instance_data *instance_data = device_data->instance;
   struct swapchain_data *data = rzalloc(NULL, struct swapchain_data);
   data->device = device_data;
   data->swapchain = swapchain;
   map_object(HKEY(data->swapchain), data);
   return data;
}

static void destroy_swapchain_data(struct swapchain_data *data)
{
   unmap_images(data);
   unmap_object(HKEY(data->swapchain));
   ralloc_free(data);
}

static void parse_command(struct instance_data *instance_data,
                          const char *cmd, unsigned cmdlen,
                          const char *param, unsigned paramlen)
{
   /* parse string (if any) from capture command */
   if (!strncmp(cmd, "capture", cmdlen)) {
      instance_data->screenshot_enabled = true;
      if (paramlen > 1) {
         instance_data->filename = param;
      } else {
         instance_data->filename = NULL;
      }
   } else if (!strncmp(cmd, "region", cmdlen)) {
      instance_data->params.region = getRegionFromInput(param);
      instance_data->region_enabled = instance_data->params.region.useImageRegion;
   }
}

#define BUFSIZE 4096

/**
 * This function will process commands through the control file.
 *
 * A command starts with a colon, followed by the command, and followed by an
 * option '=' and a parameter.  It has to end with a semi-colon. A full command
 * + parameter looks like:
 *
 *    :cmd=param;
 */
static void process_char(struct instance_data *instance_data, char c)
{
   static char cmd[BUFSIZE];
   static char param[BUFSIZE];

   static unsigned cmdpos = 0;
   static unsigned parampos = 0;
   static bool reading_cmd = false;
   static bool reading_param = false;

   switch (c) {
   case ':':
      cmdpos = 0;
      parampos = 0;
      reading_cmd = true;
      reading_param = false;
      break;
   case ',':
   case ';':
      if (!reading_cmd)
         break;
      cmd[cmdpos++] = '\0';
      param[parampos++] = '\0';
      parse_command(instance_data, cmd, cmdpos, param, parampos);
      if (c == ';') {
         reading_cmd = false;
      } else {
         cmdpos = 0;
         parampos = 0;
      }
      reading_param = false;
      break;
   case '=':
      if (!reading_cmd)
         break;
      reading_param = true;
      break;
   default:
      if (!reading_cmd)
         break;

      if (reading_param) {
         /* overflow means an invalid parameter */
         if (parampos >= BUFSIZE - 1) {
            reading_cmd = false;
            reading_param = false;
            break;
         }

         param[parampos++] = c;
      } else {
         /* overflow means an invalid command */
         if (cmdpos >= BUFSIZE - 1) {
            reading_cmd = false;
            break;
         }

         cmd[cmdpos++] = c;
      }
   }
}

static void control_send(struct instance_data *instance_data,
                         const char *cmd, unsigned cmdlen,
                         const char *param, unsigned paramlen)
{
   unsigned msglen = 0;
   char buffer[BUFSIZE];

   assert(cmdlen + paramlen + 3 < BUFSIZE);

   buffer[msglen++] = ':';

   memcpy(&buffer[msglen], cmd, cmdlen);
   msglen += cmdlen;

   if (paramlen > 0) {
      buffer[msglen++] = '=';
      memcpy(&buffer[msglen], param, paramlen);
      msglen += paramlen;
      buffer[msglen++] = ';';
   }

   os_socket_send(instance_data->control_client, buffer, msglen, 0);
}

static void control_send_connection_string(struct device_data *device_data)
{
   struct instance_data *instance_data = device_data->instance;

   const char *controlVersionCmd = "MesaScreenshotControlVersion";
   const char *controlVersionString = "1";

   control_send(instance_data, controlVersionCmd, strlen(controlVersionCmd),
                controlVersionString, strlen(controlVersionString));

   const char *deviceCmd = "DeviceName";
   const char *deviceName = device_data->properties.deviceName;

   control_send(instance_data, deviceCmd, strlen(deviceCmd),
                deviceName, strlen(deviceName));

   const char *mesaVersionCmd = "MesaVersion";
   const char *mesaVersionString = "Mesa " PACKAGE_VERSION MESA_GIT_SHA1;

   control_send(instance_data, mesaVersionCmd, strlen(mesaVersionCmd),
                mesaVersionString, strlen(mesaVersionString));
}

static void control_client_check(struct device_data *device_data)
{
   struct instance_data *instance_data = device_data->instance;

   /* Already connected, just return. */
   if (instance_data->control_client >= 0)
      return;

   int socket_fd = os_socket_accept(instance_data->socket_fd);
   if (socket_fd == -1) {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED)
         LOG(ERROR, "socket error: %s\n", strerror(errno));
      return;
   }

   if (socket_fd >= 0) {
      os_socket_block(socket_fd, false);
      instance_data->control_client = socket_fd;
      control_send_connection_string(device_data);
   }
}

static void control_client_disconnected(struct instance_data *instance_data)
{
   os_socket_close(instance_data->control_client);
   instance_data->control_client = -1;
}

static void process_control_socket(struct instance_data *instance_data)
{
   const int client = instance_data->control_client;
   if (client >= 0) {
      char buf[BUFSIZE];

      while (true) {
         ssize_t n = os_socket_recv(client, buf, BUFSIZE, 0);

         if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
               /* nothing to read, try again later */
               break;
            }

            if (errno != ECONNRESET)
               LOG(ERROR, "Connection failed: %s\n", strerror(errno));

            control_client_disconnected(instance_data);
         } else if (n == 0) {
            /* recv() returns 0 when the client disconnects */
            control_client_disconnected(instance_data);
         }

         for (ssize_t i = 0; i < n; i++) {
            process_char(instance_data, buf[i]);
         }

         /* If we try to read BUFSIZE and receive BUFSIZE bytes from the
          * socket, there's a good chance that there's still more data to be
          * read, so we will try again. Otherwise, simply be done for this
          * iteration and try again on the next frame.
          */
         if (n < BUFSIZE)
            break;
      }
   }
}

static void screenshot_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue) {
   struct device_data *device_data = FIND(struct device_data, device);
   device_data->vtable.GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
   loader_platform_thread_lock_mutex(&globalLock);
   struct queue_data *it = device_data->queue_data_head;
   while (it != VK_NULL_HANDLE) {
      if (it->queue == *pQueue) {
         break;
      }
      it = it->next;
   }
   if (it == VK_NULL_HANDLE) {
      new_queue_data(*pQueue, device_data, queueIndex, queueFamilyIndex);
   } else {
      it->familyIndex = queueFamilyIndex;
      it->index = queueIndex;
   }
   loader_platform_thread_unlock_mutex(&globalLock);
}

static void screenshot_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2 *pQueueInfo, VkQueue *pQueue) {
   if (pQueueInfo) screenshot_GetDeviceQueue(device, pQueueInfo->queueFamilyIndex, pQueueInfo->queueIndex, pQueue);
}

static VkResult screenshot_CreateSwapchainKHR(
    VkDevice                                    device,
    const VkSwapchainCreateInfoKHR*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSwapchainKHR*                             pSwapchain)
{
   struct device_data *device_data = FIND(struct device_data, device);

   // Turn on transfer src bit for image copy later on.
   VkSwapchainCreateInfoKHR createInfo = *pCreateInfo;
   createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
   VkResult result = device_data->vtable.CreateSwapchainKHR(device, &createInfo, pAllocator, pSwapchain);
   if (result != VK_SUCCESS) return result;

   loader_platform_thread_lock_mutex(&globalLock);

   struct swapchain_data *swapchain_data = new_swapchain_data(*pSwapchain, device_data);
   swapchain_data->imageExtent = pCreateInfo->imageExtent;
   swapchain_data->format = pCreateInfo->imageFormat;
   loader_platform_thread_unlock_mutex(&globalLock);
   return result;
}

static VkResult screenshot_GetSwapchainImagesKHR(
   VkDevice                                        device,
   VkSwapchainKHR                                  swapchain,
   uint32_t*                                       pCount,
   VkImage*                                        pSwapchainImages)
{
   struct swapchain_data *swapchain_data = FIND(struct swapchain_data, swapchain);
   struct vk_device_dispatch_table *vtable = &(swapchain_data->device->vtable);
   VkResult result = vtable->GetSwapchainImagesKHR(device, swapchain, pCount, pSwapchainImages);

   loader_platform_thread_lock_mutex(&globalLock);
   LOG(DEBUG, "Swapchain size: %d\n", *pCount);
   if (swapchain_data->imageListSize > 0)
      unmap_images(swapchain_data);
   if (result == VK_SUCCESS) {
      // Save the images produced from the swapchain in a hash table
      if (*pCount > 0) {
            if(pSwapchainImages){
               map_images(swapchain_data, pSwapchainImages, *pCount);
         }
      }
   }
   loader_platform_thread_unlock_mutex(&globalLock);
   return result;
}

static void screenshot_DestroySwapchainKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkAllocationCallbacks*                pAllocator)
{
   if (swapchain == VK_NULL_HANDLE) {
      struct device_data *device_data = FIND(struct device_data, device);
      device_data->vtable.DestroySwapchainKHR(device, swapchain, pAllocator);
      return;
   }

   struct swapchain_data *swapchain_data =
      FIND(struct swapchain_data, swapchain);

   swapchain_data->device->vtable.DestroySwapchainKHR(device, swapchain, pAllocator);
   destroy_swapchain_data(swapchain_data);
}

/* Convert long int to string */
static void itoa(uint32_t integer, char *dest_str)
{
   // Our sizes are limited to uin32_t max value: 4,294,967,295 (10 digits)
   sprintf(dest_str, "%u", integer);
}

static bool get_mem_type_from_properties(
   VkPhysicalDeviceMemoryProperties*         mem_properties,
   uint32_t                                  bits_type,
   VkFlags                                   requirements_mask,
   uint32_t*                                 type_index)
{
   for (uint32_t i = 0; i < 32; i++) {
      if ((bits_type & 1) == 1) {
         if ((mem_properties->memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask) {
            *type_index = i;
            return true;
         }
      }
      bits_type >>= 1;
   }
   return false;
}

VkQueue getQueueForScreenshot(struct device_data *device_data,
                              struct instance_data *instance_data) {
   // Find a queue that we can use for taking a screenshot
   VkQueue queue = VK_NULL_HANDLE;
   VkBool32 presentCapable = VK_FALSE;
   uint32_t n_family_props;
   instance_data->pd_vtable.GetPhysicalDeviceQueueFamilyProperties(device_data->physical_device,
                                                                   &n_family_props,
                                                                   NULL);
   if (n_family_props > 0) {
      VkQueueFamilyProperties *family_props =
      (VkQueueFamilyProperties *)malloc(sizeof(VkQueueFamilyProperties) * n_family_props);
      instance_data->pd_vtable.GetPhysicalDeviceQueueFamilyProperties(device_data->physical_device,
                                                                      &n_family_props,
                                                                      family_props);

      // Iterate over all queues for this device, searching for a queue that is graphics capable
      for (auto it = device_data->queue_data_head; it != VK_NULL_HANDLE; it = it->next) {
         queue = it->queue;
         if((family_props[it->familyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            break;
         } else {
            // Clear the queue if it's not graphics capable
            queue = VK_NULL_HANDLE;
         }
      }
      free(family_props);
   }
   return queue;
}

// Track allocated resources in writeFile()
// and clean them up when they go out of scope.
struct WriteFileCleanupData {
    device_data *dev_data;
    VkImage image2;
    VkImage image3;
    VkDeviceMemory mem2;
    VkDeviceMemory mem3;
    bool mem2mapped;
    bool mem3mapped;
    VkCommandBuffer commandBuffer;
    VkCommandPool commandPool;
    ~WriteFileCleanupData();
};

WriteFileCleanupData::~WriteFileCleanupData() {
    if (mem2mapped) dev_data->vtable.UnmapMemory(dev_data->device, mem2);
    if (mem2) dev_data->vtable.FreeMemory(dev_data->device, mem2, NULL);
    if (image2) dev_data->vtable.DestroyImage(dev_data->device, image2, NULL);

    if (mem3mapped) dev_data->vtable.UnmapMemory(dev_data->device, mem3);
    if (mem3) dev_data->vtable.FreeMemory(dev_data->device, mem3, NULL);
    if (image3) dev_data->vtable.DestroyImage(dev_data->device, image3, NULL);

    if (commandBuffer) dev_data->vtable.FreeCommandBuffers(dev_data->device, commandPool, 1, &commandBuffer);
    if (commandPool) dev_data->vtable.DestroyCommandPool(dev_data->device, commandPool, NULL);
}

static uint64_t get_time() {
   if (LOG_TYPE == DEBUG) {
      struct timespec tspec;
      long BILLION = 1000000000;
      clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tspec);
      uint64_t sec  = tspec.tv_sec;
      uint64_t nsec = tspec.tv_nsec;
      return ((sec * BILLION) + nsec);
   } else {
      return 0;
   }
}

static void print_time_difference(long int start_time, long int end_time) {
   if (end_time > 0) {
      LOG(DEBUG, "Time to copy: %u nanoseconds\n", end_time - start_time);
   }
}

// Store all data required for threading the saving to file functionality
struct ThreadSaveData {
    struct device_data *device_data;
    const char *filename;
    const char *pFramebuffer;
    VkSubresourceLayout srLayout;
    VkFence fence;
    uint32_t const width;
    uint32_t const height;
    uint32_t const numChannels;
};

/* Write the copied image to a PNG file */
void *writePNG(void *data) {
   struct ThreadSaveData *threadData = (struct ThreadSaveData*)data;
   FILE *file;
   size_t length = sizeof(char[LARGE_BUFFER_SIZE+STANDARD_BUFFER_SIZE]);
   const char *tmpStr = ".tmp";
   char *filename    = (char *)malloc(length);
   char *tmpFilename = (char *)malloc(length + 4); // Allow for ".tmp"
   VkResult res;
   png_byte *row_pointer;
   png_infop info;
   png_struct* png;
   uint64_t rowPitch = threadData->srLayout.rowPitch;
   uint64_t start_time, end_time;
   const int RGB_NUM_CHANNELS = 3;
   const int RGBA_NUM_CHANNELS = 4;
   int localHeight = threadData->height;
   int localWidth = threadData->width;
   int numChannels = threadData->numChannels;
   int matrixSize = localHeight * rowPitch;
   bool checks_failed = true;
   memcpy(filename, threadData->filename, length);
   memcpy(tmpFilename, threadData->filename, length);
   strcat(tmpFilename, tmpStr);
   file = fopen(tmpFilename, "wb"); //create file for output
   if (!file) {
      LOG(ERROR, "Failed to open output file, '%s', error(%d): %s\n", tmpFilename, errno, strerror(errno));
      goto cleanup;
   }
   // TODO: Look into runtime version mismatch issue with some VK workloads
   png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL); //create structure for write PNG_LIBPNG_VER_STRING
   if (!png) {
      LOG(ERROR, "Create write struct failed. VER_STRING=%s\n", PNG_LIBPNG_VER_STRING);
      goto cleanup;
   }
   info = png_create_info_struct(png);
   if (!info) {
      LOG(ERROR, "Create info struct failed\n");
      goto cleanup;
   }
   if (setjmp(png_jmpbuf(png))) {
      LOG(ERROR, "setjmp() failed\n");
      goto cleanup;
   }
   threadData->device_data->vtable.WaitForFences(threadData->device_data->device, 1, &threadData->fence, VK_TRUE, UINT64_MAX);
   threadData->pFramebuffer += threadData->srLayout.offset;
   start_time = get_time();
   row_pointer = (png_byte *)malloc(sizeof(png_byte) * matrixSize);
   memcpy(row_pointer, threadData->pFramebuffer, matrixSize);
   /* Ensure alpha bits are set to 'opaque' if image is of RGBA format */
   if (numChannels == RGBA_NUM_CHANNELS) {
      for (int i = 3; i < matrixSize; i += RGBA_NUM_CHANNELS) {
         row_pointer[i] = 0xFF;
      }
   }
   end_time = get_time();
   print_time_difference(start_time, end_time);
   // We've created all local copies of data,
   // so let's signal main thread to continue
   pthread_cond_signal(&ptCondition);
   png_init_io(png, file); // Initialize file output
   png_set_IHDR( // Set image properties
      png,    // Pointer to png_struct
      info,   // Pointer to info_struct
      localWidth, // Image width
      localHeight, // Image height
      8,      // Color depth
      numChannels == RGB_NUM_CHANNELS ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGBA,
      PNG_INTERLACE_NONE,
      PNG_COMPRESSION_TYPE_DEFAULT,
      PNG_FILTER_TYPE_DEFAULT
      );
   png_set_compression_level(png, 1);    // Z_BEST_SPEED=1
   png_set_compression_strategy(png, 2); // Z_HUFFMAN_ONLY=2
   png_set_filter(png, PNG_FILTER_TYPE_BASE, PNG_FILTER_SUB);
   png_set_compression_mem_level(png, 9);
   png_set_compression_buffer_size(png, 65536);
   png_write_info(png, info);         // Write png image information to file
   for (int y = 0; y < matrixSize; y+=rowPitch) {
      png_write_row(png, &row_pointer[y]);
   }
   png_write_end(png, NULL);          // End image writing
   free(row_pointer);

   // Rename file, indicating completion, client should be
   // checking for the final file exists.
   if (rename(tmpFilename, filename) != 0 )
      LOG(ERROR, "Could not rename from '%s' to '%s'\n", tmpFilename, filename);
   else
      LOG(INFO, "Successfully renamed from '%s' to '%s'\n", tmpFilename, filename);
   checks_failed = false;
cleanup:
   if (checks_failed)
      pthread_cond_signal(&ptCondition);
   if (info)
      png_destroy_write_struct(&png, &info);
   if (file)
      fclose(file);
   if (filename)
      free(filename);
   if (tmpFilename)
      free(tmpFilename);
   return nullptr;
}

/* Write an image to file. Upon encountering issues, do not impact the
   Present operation,  */
static bool write_image(
   const char*             filename,
   VkImage                 image,
   struct device_data*     device_data,
   struct instance_data*   instance_data,
   struct queue_data*      queue_data,
   struct swapchain_data*  swapchain_data)
{
   VkDevice device = device_data->device;
   VkPhysicalDevice physical_device = device_data->physical_device;
   VkInstance instance = instance_data->instance;

   uint32_t const width  = swapchain_data->imageExtent.width;
   uint32_t const height = swapchain_data->imageExtent.height;
   VkFormat const format = swapchain_data->format;

   uint32_t newWidth = width;
   uint32_t newHeight = height;
   uint32_t regionStartX = 0;
   uint32_t regionStartY = 0;
   uint32_t regionEndX = width;
   uint32_t regionEndY = height;
   if (instance_data->region_enabled) {
      regionStartX = int(instance_data->params.region.startX * width);
      regionStartY = int(instance_data->params.region.startY * height);
      regionEndX = int(instance_data->params.region.endX * width);
      regionEndY = int(instance_data->params.region.endY * height);
      newWidth = regionEndX - regionStartX;
      newHeight = regionEndY - regionStartY;
      LOG(DEBUG, "Using region: startX = %.0f% (%d), startY = %.0f% (%d), endX = %.0f% (%d), endY = %.0f% (%d)\n",
          instance_data->params.region.startX*100, regionStartX,
          instance_data->params.region.startY*100, regionStartY,
          instance_data->params.region.endX*100, regionEndX,
          instance_data->params.region.endY*100, regionEndY);
   }

   VkQueue queue = getQueueForScreenshot(device_data, instance_data);
   if (!queue) {
      LOG(ERROR, "Unable to find a valid graphics-enabled queue\n");
      return false;
   }

   VkResult err;
   /* Attempt to set destination format to RGB to make writing to file much faster.
      If not available, try to fall back to RGBA. If both fail, abort the screenshot */
   VkFormat supported_formats[] = {VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED};
   uint32_t supported_formats_count = sizeof(supported_formats) / sizeof(VkFormat);
   VkFormat destination_format;
   uint32_t numChannels = 0;
   /* If origin and destination formats are the same, no need to convert */
   bool copyOnly = false;
   bool needs_2_steps = false;
   bool blt_linear, blt_optimal;
   VkFormatProperties device_format_properties;

   for (uint32_t i = 0; i < supported_formats_count; i++) {
      destination_format = supported_formats[i];
      instance_data->pd_vtable.GetPhysicalDeviceFormatProperties(physical_device,
                                                                 destination_format,
                                                                 &device_format_properties);
      if(destination_format == VK_FORMAT_UNDEFINED) {
         LOG(ERROR, "Could not use the supported surface formats!\n");
         return false;
      }
      if (destination_format == format && not instance_data->region_enabled) {
         copyOnly = true;
         LOG(DEBUG, "Only copying since the src/dest surface formats are the same.\n");
         break;
      } else {
         blt_linear = device_format_properties.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT ? true : false;
         blt_optimal = device_format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT ? true : false;
         if (!blt_linear && !blt_optimal) {
            LOG(DEBUG, "Can't blit to linear nor optimal with surface format '%s'\n", vk_Format_to_str(supported_formats[i]));
         } else if (blt_linear) {
            break;
         } else if (blt_optimal) {
            // Can't blit to linear target, but can blit to optimal
            needs_2_steps = true;
            LOG(DEBUG, "Needs 2 steps\n");
            break;
         }
      }
   }
   LOG(DEBUG, "Using surface format '%s' for copy.\n", vk_Format_to_str(destination_format));

   switch (destination_format)
   {
   case VK_FORMAT_R8G8B8_UNORM:
      numChannels = 3;
      break;
   case VK_FORMAT_R8G8B8A8_UNORM:
      numChannels = 4;
      break;
   default:
      LOG(ERROR, "Unsupported format, aborting screenshot!\n");
      break;
   }

   WriteFileCleanupData data = {};
   data.dev_data = device_data;

   VkImageCreateInfo img_create_info2 = {
      VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      NULL,
      0,
      VK_IMAGE_TYPE_2D,
      destination_format,
      {newWidth, newHeight, 1},
      1,
      1,
      VK_SAMPLE_COUNT_1_BIT,
      VK_IMAGE_TILING_LINEAR,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      VK_SHARING_MODE_EXCLUSIVE,
      0,
      NULL,
      VK_IMAGE_LAYOUT_UNDEFINED,
   };
   VkImageCreateInfo img_create_info3 = img_create_info2;

   if (needs_2_steps) {
      img_create_info2.tiling = VK_IMAGE_TILING_OPTIMAL;
      img_create_info2.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   }
   VkMemoryAllocateInfo mem_alloc_info = {
      VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      NULL,
      0,
      0
   };
   VkMemoryRequirements mem_requirements;
   VkPhysicalDeviceMemoryProperties mem_properties;

   VK_CHECK(device_data->vtable.CreateImage(device, &img_create_info2, NULL, &data.image2));
   device_data->vtable.GetImageMemoryRequirements(device, data.image2, &mem_requirements);
   mem_alloc_info.allocationSize = mem_requirements.size;
   instance_data->pd_vtable.GetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);
   if(!get_mem_type_from_properties(&mem_properties,
                                    mem_requirements.memoryTypeBits,
                                    needs_2_steps ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                    &mem_alloc_info.memoryTypeIndex)) {
      LOG(ERROR, "Unable to get memory type from the intermediate/final image properties.\n");
      return false;
   }

   VK_CHECK(device_data->vtable.AllocateMemory(device, &mem_alloc_info, NULL, &data.mem2));
   VK_CHECK(device_data->vtable.BindImageMemory(device, data.image2, data.mem2, 0));

   if (needs_2_steps) {
      VK_CHECK(device_data->vtable.CreateImage(device, &img_create_info3, NULL, &data.image3));
      device_data->vtable.GetImageMemoryRequirements(device, data.image3, &mem_requirements);
      mem_alloc_info.allocationSize = mem_requirements.size;
      instance_data->pd_vtable.GetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

      if(!get_mem_type_from_properties(&mem_properties,
                                       mem_requirements.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                       &mem_alloc_info.memoryTypeIndex)) {
         LOG(ERROR, "Unable to get memory type from the temporary image properties.\n");
         return false;
      }
      VK_CHECK(device_data->vtable.AllocateMemory(device, &mem_alloc_info, NULL, &data.mem3));
      VK_CHECK(device_data->vtable.BindImageMemory(device, data.image3, data.mem3, 0));
   }

   /* Setup command pool */
   VkCommandPoolCreateInfo cmd_pool_info = {};
   cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   cmd_pool_info.pNext = NULL;
   cmd_pool_info.queueFamilyIndex = queue_data->familyIndex;
   cmd_pool_info.flags = 0;

   VK_CHECK(device_data->vtable.CreateCommandPool(device, &cmd_pool_info, NULL, &data.commandPool));

   /* Set up command buffer */
   const VkCommandBufferAllocateInfo cmd_buf_alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL,
                                                           data.commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
   VK_CHECK(device_data->vtable.AllocateCommandBuffers(device, &cmd_buf_alloc_info, &data.commandBuffer));

   if (device_data->set_device_loader_data) {
      VK_CHECK(device_data->set_device_loader_data(device, (void *)data.commandBuffer));
   } else {
      *((const void **)data.commandBuffer) = *(void **)device;
   }

   const VkCommandBufferBeginInfo cmd_buf_begin_info = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      NULL,
      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   VK_CHECK(device_data->vtable.BeginCommandBuffer(data.commandBuffer, &cmd_buf_begin_info));

   // This barrier is used to transition from/to present Layout
   VkImageMemoryBarrier presentMemoryBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                                NULL,
                                                VK_ACCESS_MEMORY_WRITE_BIT,
                                                VK_ACCESS_TRANSFER_READ_BIT,
                                                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                VK_QUEUE_FAMILY_IGNORED,
                                                VK_QUEUE_FAMILY_IGNORED,
                                                image,
                                                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

   // This barrier is used to transition from a newly-created layout to a blt
   // or copy destination layout.
   VkImageMemoryBarrier destMemoryBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                             NULL,
                                             0,
                                             VK_ACCESS_TRANSFER_WRITE_BIT,
                                             VK_IMAGE_LAYOUT_UNDEFINED,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             VK_QUEUE_FAMILY_IGNORED,
                                             VK_QUEUE_FAMILY_IGNORED,
                                             data.image2,
                                             {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

   // This barrier is used to transition a dest layout to general layout.
   VkImageMemoryBarrier generalMemoryBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                                NULL,
                                                VK_ACCESS_TRANSFER_WRITE_BIT,
                                                VK_ACCESS_MEMORY_READ_BIT,
                                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                VK_IMAGE_LAYOUT_GENERAL,
                                                VK_QUEUE_FAMILY_IGNORED,
                                                VK_QUEUE_FAMILY_IGNORED,
                                                data.image2,
                                                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

   VkPipelineStageFlags srcStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
   VkPipelineStageFlags dstStages = VK_PIPELINE_STAGE_TRANSFER_BIT;

   device_data->vtable.CmdPipelineBarrier(data.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                          dstStages, 0, 0, NULL, 0, NULL, 1, &presentMemoryBarrier);

   device_data->vtable.CmdPipelineBarrier(data.commandBuffer, srcStages, dstStages, 0, 0, NULL, 0, NULL, 1, &destMemoryBarrier);

   const VkImageCopy img_copy = {
      {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      {0, 0, 0},
      {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      {0, 0, 0},
      {newWidth, newHeight, 1}
   };

   if (copyOnly) {
      device_data->vtable.CmdCopyImage(data.commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, data.image2,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &img_copy);
   } else {
      VkImageBlit imageBlitRegion = {};
      imageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      imageBlitRegion.srcSubresource.baseArrayLayer = 0;
      imageBlitRegion.srcSubresource.layerCount = 1;
      imageBlitRegion.srcSubresource.mipLevel = 0;
      imageBlitRegion.srcOffsets[0].x = regionStartX;
      imageBlitRegion.srcOffsets[0].y = regionStartY;
      imageBlitRegion.srcOffsets[0].z = 0;
      imageBlitRegion.srcOffsets[1].x = regionEndX;
      imageBlitRegion.srcOffsets[1].y = regionEndY;
      imageBlitRegion.srcOffsets[1].z = 1;
      imageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      imageBlitRegion.dstSubresource.baseArrayLayer = 0;
      imageBlitRegion.dstSubresource.layerCount = 1;
      imageBlitRegion.dstSubresource.mipLevel = 0;
      imageBlitRegion.dstOffsets[1].x = newWidth;
      imageBlitRegion.dstOffsets[1].y = newHeight;
      imageBlitRegion.dstOffsets[1].z = 1;

      device_data->vtable.CmdBlitImage(data.commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, data.image2,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlitRegion, VK_FILTER_NEAREST);
      if (needs_2_steps) {
         // image 3 needs to be transitioned from its undefined state to a
         // transfer destination.
         destMemoryBarrier.image = data.image3;
         device_data->vtable.CmdPipelineBarrier(data.commandBuffer, srcStages, dstStages, 0, 0, NULL, 0, NULL, 1, &destMemoryBarrier);

         // Transition image2 so that it can be read for the upcoming copy to
         // image 3.
         destMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
         destMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
         destMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
         destMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
         destMemoryBarrier.image = data.image2;
         device_data->vtable.CmdPipelineBarrier(data.commandBuffer, srcStages, dstStages, 0, 0, NULL, 0, NULL, 1,
                              &destMemoryBarrier);

         // This step essentially untiles the image.
         device_data->vtable.CmdCopyImage(data.commandBuffer, data.image2, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, data.image3,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &img_copy);
         generalMemoryBarrier.image = data.image3;
      }
   }

   // The destination needs to be transitioned from the optimal copy format to
   // the format we can read with the CPU.
   device_data->vtable.CmdPipelineBarrier(data.commandBuffer, srcStages, dstStages, 0, 0, NULL, 0, NULL, 1, &generalMemoryBarrier);

   // Restore the swap chain image layout to what it was before.
   // This may not be strictly needed, but it is generally good to restore
   // things to original state.
   presentMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
   presentMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
   presentMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
   presentMemoryBarrier.dstAccessMask = 0;
   device_data->vtable.CmdPipelineBarrier(data.commandBuffer, srcStages, dstStages, 0, 0, NULL, 0, NULL, 1,
                        &presentMemoryBarrier);
   VK_CHECK(device_data->vtable.EndCommandBuffer(data.commandBuffer));

   VkSubmitInfo submitInfo;
   submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   submitInfo.pNext = NULL;
   submitInfo.waitSemaphoreCount = semaphoreWaitBeforePresentCount;
   submitInfo.pWaitSemaphores = pSemaphoreWaitBeforePresent;
   submitInfo.pWaitDstStageMask = &dstStageWaitBeforeSubmission;
   submitInfo.commandBufferCount = 1;
   submitInfo.pCommandBuffers = &data.commandBuffer;
   submitInfo.signalSemaphoreCount = 1;
   submitInfo.pSignalSemaphores = &semaphoreWaitAfterSubmission;
   VK_CHECK(device_data->vtable.QueueSubmit(queue, 1, &submitInfo, copyDone));

   // Map the final image so that the CPU can read it.
   const VkImageSubresource img_subresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
   VkSubresourceLayout srLayout;
   const char *pFramebuffer;
   if (!needs_2_steps) {
      device_data->vtable.GetImageSubresourceLayout(device, data.image2, &img_subresource, &srLayout);
      VK_CHECK(device_data->vtable.MapMemory(device, data.mem2, 0, VK_WHOLE_SIZE, 0, (void **)&pFramebuffer));
      data.mem2mapped = true;
    } else {
      device_data->vtable.GetImageSubresourceLayout(device, data.image3, &img_subresource, &srLayout);
      VK_CHECK(device_data->vtable.MapMemory(device, data.mem3, 0, VK_WHOLE_SIZE, 0, (void **)&pFramebuffer));
      data.mem3mapped = true;
   }

   // Thread off I/O operations
   pthread_t ioThread;
   pthread_mutex_lock(&ptLock); // Grab lock, we need to wait until thread has copied values of pointers
   struct ThreadSaveData threadData = {device_data, filename, pFramebuffer, srLayout, copyDone, newWidth, newHeight, numChannels};

   // Write the data to a PNG file.
   pthread_create(&ioThread, NULL, writePNG, (void *)&threadData);
   pthread_detach(ioThread); // Reclaim resources once thread terminates
   pthread_cond_wait(&ptCondition, &ptLock);
   pthread_mutex_unlock(&ptLock);

   return true;
}

static VkResult screenshot_QueuePresentKHR(
    VkQueue                                     queue,
    const VkPresentInfoKHR*                     pPresentInfo)
{
   struct queue_data *queue_data = FIND(struct queue_data, queue);
   struct device_data *device_data = queue_data->device;
   struct instance_data *instance_data = device_data->instance;

   VkPresentInfoKHR present_info = *pPresentInfo;

   static uint32_t frame_counter = 0;

   VkResult result = VK_SUCCESS;
   loader_platform_thread_lock_mutex(&globalLock);
   VkSemaphoreCreateInfo semaphoreInfo = {};
   VkFenceCreateInfo fenceInfo = {};

   if (pPresentInfo && pPresentInfo->swapchainCount > 0) {
      VkSwapchainKHR swapchain = pPresentInfo->pSwapchains[0];

      struct swapchain_data *swapchain_data = FIND(struct swapchain_data, swapchain);

      /* Run initial setup with client */
      if (instance_data->params.enabled[SCREENSHOT_PARAM_ENABLED_comms] && instance_data->socket_fd < 0) {
         int ret = os_socket_listen_abstract(instance_data->params.control, 1);
         if (ret >= 0) {
            os_socket_block(ret, false);
            instance_data->socket_fd = ret;
         }
         if (instance_data->socket_fd >= 0)
            LOG(INFO, "socket set! Waiting for client input...\n");
      }

      if (instance_data->socket_fd >= 0) {
         /* Check client commands first */
         control_client_check(device_data);
         process_control_socket(instance_data);
      } else if (instance_data->params.frames) {
         /* Else check parameters from env variables */
         if (instance_data->params.frames->size > 0) {
            struct frame_list *list = instance_data->params.frames;
            struct frame_node *prev = nullptr;
            for (struct frame_node *node = list->head; node!=nullptr; prev = node, node = node->next) {
               if (frame_counter < node->frame_num){
                  break;
               } else if (frame_counter == node->frame_num) {
                  instance_data->screenshot_enabled = true;
                  remove_node(list, prev, node);
                  break;
               } else {
                  LOG(ERROR, "mesa-screenshot: Somehow encountered a higher number "
                             "than what exists in the frame list. Won't capture frame!\n");
                  destroy_frame_list(list);
                  break;
               }
            }
         } else if (instance_data->params.frames->all_frames) {
            instance_data->screenshot_enabled = true;
         }
         if (instance_data->params.region.useImageRegion) {
            instance_data->region_enabled = true;
         }
      }

      if (instance_data->screenshot_enabled) {
         LOG(DEBUG, "Screenshot Authorized!\n");
         uint32_t SUFFIX_SIZE = 4; // strlen('.png') == 4;
         uint32_t path_size_used = 0;
         const char *SUFFIX = ".png";
         const char *TEMP_DIR = "/tmp/";
         char full_path[LARGE_BUFFER_SIZE+STANDARD_BUFFER_SIZE] = "";
         char filename[STANDARD_BUFFER_SIZE] = "";
         char frame_counter_str[11];
         bool rename_file = true;
         itoa(frame_counter, frame_counter_str);

         /* Check if we have an output directory given from the env options */
         if (instance_data->params.output_dir &&
               strlen(instance_data->params.output_dir) > 0) {
               strcat(full_path, instance_data->params.output_dir);
         } else {
            memcpy(full_path, TEMP_DIR, strlen(TEMP_DIR));
         }
         path_size_used += strlen(full_path);
         /* Check if we have a filename from the client */
         if (instance_data->filename && strlen(instance_data->filename) > SUFFIX_SIZE) {
            /* Confirm that filename is of form '<name>.png' */
            uint32_t name_len = strlen(instance_data->filename);
            const char *suffix_ptr = &instance_data->filename[name_len - SUFFIX_SIZE];
            if (!strcmp(suffix_ptr, SUFFIX)) {
                  rename_file = false;
                  strcpy(filename, instance_data->filename);
            }
         }
         if (rename_file) {
            strcat(filename, frame_counter_str);
            strcat(filename, SUFFIX);
         }
         path_size_used += strlen(filename);
         if(path_size_used <= LARGE_BUFFER_SIZE+STANDARD_BUFFER_SIZE) {
            strcat(full_path, filename);
            pSemaphoreWaitBeforePresent = pPresentInfo->pWaitSemaphores;
            semaphoreWaitBeforePresentCount = pPresentInfo->waitSemaphoreCount;
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            device_data->vtable.CreateSemaphore(device_data->device, &semaphoreInfo, nullptr, &semaphoreWaitAfterSubmission);
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            device_data->vtable.CreateFence(device_data->device, &fenceInfo, nullptr, &copyDone);
            if(write_image(full_path,
                           swapchain_data->image,
                           device_data,
                           instance_data,
                           queue_data,
                           swapchain_data)) {
               present_info.pWaitSemaphores = &semaphoreWaitAfterSubmission; // Make semaphore here
               present_info.waitSemaphoreCount = 1;
            }
         } else {
            LOG(DEBUG, "Cancelling screenshot due to excessive filepath size (max %u characters)\n", LARGE_BUFFER_SIZE);
         }
      }
   }
   frame_counter++;
   instance_data->screenshot_enabled = false;
   instance_data->region_enabled = false;
   loader_platform_thread_unlock_mutex(&globalLock);
   VkResult chain_result = queue_data->device->vtable.QueuePresentKHR(queue, &present_info);
   if (pPresentInfo->pResults)
      pPresentInfo->pResults[0] = chain_result;
   if (chain_result != VK_SUCCESS && result == VK_SUCCESS)
      result = chain_result;

   if (semaphoreWaitAfterSubmission != VK_NULL_HANDLE) {
      device_data->vtable.DestroySemaphore(device_data->device, semaphoreWaitAfterSubmission, nullptr);
      semaphoreWaitAfterSubmission = VK_NULL_HANDLE;
   }
   if (copyDone != VK_NULL_HANDLE) {
      device_data->vtable.DestroyFence(device_data->device, copyDone, nullptr);
      copyDone = VK_NULL_HANDLE;
   }
   return result;
}

static VkResult screenshot_AcquireNextImageKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    timeout,
    VkSemaphore                                 semaphore,
    VkFence                                     fence,
    uint32_t*                                   pImageIndex)
{
   struct swapchain_data *swapchain_data =
      FIND(struct swapchain_data, swapchain);
   struct device_data *device_data = swapchain_data->device;

   VkResult result = device_data->vtable.AcquireNextImageKHR(device, swapchain, timeout,
                                                             semaphore, fence, pImageIndex);
   loader_platform_thread_lock_mutex(&globalLock);

   if (result == VK_SUCCESS) {
      // Use the index given by AcquireNextImageKHR() to obtain the image we intend to copy.
      if(pImageIndex){
         select_image_from_map(swapchain_data, *pImageIndex);
      }
   }
   loader_platform_thread_unlock_mutex(&globalLock);
   return result;
}

static VkResult screenshot_CreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice)
{
   struct instance_data *instance_data =
      FIND(struct instance_data, physicalDevice);
   VkLayerDeviceCreateInfo *chain_info =
      get_device_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
   assert(chain_info->u.pLayerInfo);
   PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
   PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
   PFN_vkCreateDevice fpCreateDevice = (PFN_vkCreateDevice)fpGetInstanceProcAddr(NULL, "vkCreateDevice");
   if (fpCreateDevice == NULL) {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   // Advance the link info for the next element on the chain
   chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

   VkDeviceCreateInfo create_info = *pCreateInfo;

   VkResult result = fpCreateDevice(physicalDevice, &create_info, pAllocator, pDevice);
   if (result != VK_SUCCESS) return result;

   struct device_data *device_data = new_device_data(*pDevice, instance_data);
   device_data->physical_device = physicalDevice;
   vk_device_dispatch_table_load(&device_data->vtable,
                                 fpGetDeviceProcAddr, *pDevice);

   instance_data->pd_vtable.GetPhysicalDeviceProperties(device_data->physical_device,
                                                        &device_data->properties);

   VkLayerDeviceCreateInfo *load_data_info =
      get_device_chain_info(pCreateInfo, VK_LOADER_DATA_CALLBACK);

   device_data->set_device_loader_data = load_data_info->u.pfnSetDeviceLoaderData;
   return result;
}

static void screenshot_DestroyDevice(
    VkDevice                                    device,
    const VkAllocationCallbacks*                pAllocator)
{
   struct device_data *device_data = FIND(struct device_data, device);
   device_data->vtable.DestroyDevice(device, pAllocator);
   destroy_device_data(device_data);
}

static VkResult screenshot_CreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance)
{
   VkLayerInstanceCreateInfo *chain_info =
      get_instance_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);

   assert(chain_info->u.pLayerInfo);
   PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr =
      chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
   PFN_vkCreateInstance fpCreateInstance =
      (PFN_vkCreateInstance)fpGetInstanceProcAddr(NULL, "vkCreateInstance");
   if (fpCreateInstance == NULL) {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   // Advance the link info for the next element on the chain
   chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

   VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
   if (result != VK_SUCCESS) return result;

   struct instance_data *instance_data = new_instance_data(*pInstance);
   vk_instance_dispatch_table_load(&instance_data->vtable,
                                   fpGetInstanceProcAddr,
                                   instance_data->instance);
   vk_physical_device_dispatch_table_load(&instance_data->pd_vtable,
                                          fpGetInstanceProcAddr,
                                          instance_data->instance);
   instance_data_map_physical_devices(instance_data, true);

   parse_screenshot_env(&instance_data->params, getenv("VK_LAYER_MESA_SCREENSHOT_CONFIG"));

   if (!globalLockInitialized) {
      loader_platform_thread_create_mutex(&globalLock);
      globalLockInitialized = 1;
   }

   return result;
}

static void screenshot_DestroyInstance(
    VkInstance                                  instance,
    const VkAllocationCallbacks*                pAllocator)
{
   struct instance_data *instance_data = FIND(struct instance_data, instance);
   instance_data_map_physical_devices(instance_data, false);
   instance_data->vtable.DestroyInstance(instance, pAllocator);
   destroy_instance_data(instance_data);
}

static const struct {
   const char *name;
   void *ptr;
} name_to_funcptr_map[] = {
   { "vkGetInstanceProcAddr", (void *) vkGetInstanceProcAddr },
   { "vkGetDeviceProcAddr", (void *) vkGetDeviceProcAddr },
#define ADD_HOOK(fn) { "vk" # fn, (void *) screenshot_ ## fn }
#define ADD_ALIAS_HOOK(alias, fn) { "vk" # alias, (void *) screenshot_ ## fn }
   ADD_HOOK(CreateSwapchainKHR),
   ADD_HOOK(GetSwapchainImagesKHR),
   ADD_HOOK(DestroySwapchainKHR),
   ADD_HOOK(QueuePresentKHR),
   ADD_HOOK(AcquireNextImageKHR),

   ADD_HOOK(CreateDevice),
   ADD_HOOK(GetDeviceQueue),
   ADD_HOOK(GetDeviceQueue2),
   ADD_HOOK(DestroyDevice),

   ADD_HOOK(CreateInstance),
   ADD_HOOK(DestroyInstance),
#undef ADD_HOOK
#undef ADD_ALIAS_HOOK
};

static void *find_ptr(const char *name)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(name_to_funcptr_map); i++) {
      if (strcmp(name, name_to_funcptr_map[i].name) == 0)
         return name_to_funcptr_map[i].ptr;
   }

   return NULL;
}

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev,
                                                                    const char *funcName)
{
   void *ptr = find_ptr(funcName);
   if (ptr) return reinterpret_cast<PFN_vkVoidFunction>(ptr);

   if (dev == NULL) return NULL;

   struct device_data *device_data = FIND(struct device_data, dev);
   if (device_data->vtable.GetDeviceProcAddr == NULL) return NULL;
   return device_data->vtable.GetDeviceProcAddr(dev, funcName);
}

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,
                                                                      const char *funcName)
{
   void *ptr = find_ptr(funcName);
   if (ptr) return reinterpret_cast<PFN_vkVoidFunction>(ptr);

   if (instance == NULL) return NULL;

   struct instance_data *instance_data = FIND(struct instance_data, instance);
   if (instance_data->vtable.GetInstanceProcAddr == NULL) return NULL;
   return instance_data->vtable.GetInstanceProcAddr(instance, funcName);
}
