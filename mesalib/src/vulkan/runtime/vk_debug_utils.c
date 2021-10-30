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

#include "vk_debug_utils.h"

#include "vk_common_entrypoints.h"
#include "vk_command_buffer.h"
#include "vk_device.h"
#include "vk_queue.h"
#include "vk_object.h"
#include "vk_alloc.h"
#include "vk_util.h"
#include "stdarg.h"
#include "u_dynarray.h"

void
vk_debug_message(struct vk_instance *instance,
                 VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                 VkDebugUtilsMessageTypeFlagsEXT types,
                 const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData)
{
   mtx_lock(&instance->debug_utils.callbacks_mutex);

   list_for_each_entry(struct vk_debug_utils_messenger, messenger,
                       &instance->debug_utils.callbacks, link) {
      if ((messenger->severity & severity) &&
          (messenger->type & types))
         messenger->callback(severity, types, pCallbackData, messenger->data);
   }

   mtx_unlock(&instance->debug_utils.callbacks_mutex);
}

/* This function intended to be used by the drivers to report a
 * message to the special messenger, provided in the pNext chain while
 * creating an instance. It's only meant to be used during
 * vkCreateInstance or vkDestroyInstance calls.
 */
void
vk_debug_message_instance(struct vk_instance *instance,
                          VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                          VkDebugUtilsMessageTypeFlagsEXT types,
                          const char *pMessageIdName,
                          int32_t messageIdNumber,
                          const char *pMessage)
{
   if (list_is_empty(&instance->debug_utils.instance_callbacks))
      return;

   const VkDebugUtilsMessengerCallbackDataEXT cbData = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT,
      .pMessageIdName = pMessageIdName,
      .messageIdNumber = messageIdNumber,
      .pMessage = pMessage,
   };

   list_for_each_entry(struct vk_debug_utils_messenger, messenger,
                       &instance->debug_utils.instance_callbacks, link) {
      if ((messenger->severity & severity) &&
          (messenger->type & types))
         messenger->callback(severity, types, &cbData, messenger->data);
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_CreateDebugUtilsMessengerEXT(
   VkInstance _instance,
   const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkDebugUtilsMessengerEXT *pMessenger)
{
   VK_FROM_HANDLE(vk_instance, instance, _instance);

   struct vk_debug_utils_messenger *messenger =
      vk_alloc2(&instance->alloc, pAllocator,
                sizeof(struct vk_debug_utils_messenger), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (!messenger)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   if (pAllocator)
      messenger->alloc = *pAllocator;
   else
      messenger->alloc = instance->alloc;

   vk_object_base_init(NULL, &messenger->base,
                       VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT);

   messenger->severity = pCreateInfo->messageSeverity;
   messenger->type = pCreateInfo->messageType;
   messenger->callback = pCreateInfo->pfnUserCallback;
   messenger->data = pCreateInfo->pUserData;

   mtx_lock(&instance->debug_utils.callbacks_mutex);
   list_addtail(&messenger->link, &instance->debug_utils.callbacks);
   mtx_unlock(&instance->debug_utils.callbacks_mutex);

   *pMessenger = vk_debug_utils_messenger_to_handle(messenger);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_SubmitDebugUtilsMessageEXT(
   VkInstance _instance,
   VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
   VkDebugUtilsMessageTypeFlagsEXT messageTypes,
   const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData)
{
   VK_FROM_HANDLE(vk_instance, instance, _instance);

   vk_debug_message(instance, messageSeverity, messageTypes, pCallbackData);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_DestroyDebugUtilsMessengerEXT(
   VkInstance _instance,
   VkDebugUtilsMessengerEXT _messenger,
   const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(vk_instance, instance, _instance);
   VK_FROM_HANDLE(vk_debug_utils_messenger, messenger, _messenger);

   if (messenger == NULL)
      return;

   mtx_lock(&instance->debug_utils.callbacks_mutex);
   list_del(&messenger->link);
   mtx_unlock(&instance->debug_utils.callbacks_mutex);

   vk_object_base_finish(&messenger->base);
   vk_free2(&instance->alloc, pAllocator, messenger);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_SetDebugUtilsObjectNameEXT(
   VkDevice _device,
   const VkDebugUtilsObjectNameInfoEXT *pNameInfo)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   struct vk_object_base *object =
      vk_object_base_from_u64_handle(pNameInfo->objectHandle,
                                     pNameInfo->objectType);

   if (object->object_name) {
      vk_free(&device->alloc, object->object_name);
      object->object_name = NULL;
   }
   object->object_name = vk_strdup(&device->alloc, pNameInfo->pObjectName,
                                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!object->object_name)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_SetDebugUtilsObjectTagEXT(
   VkDevice _device,
   const VkDebugUtilsObjectTagInfoEXT *pTagInfo)
{
   /* no-op */
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdBeginDebugUtilsLabelEXT(
   VkCommandBuffer _commandBuffer,
   const VkDebugUtilsLabelEXT *pLabelInfo)
{
   VK_FROM_HANDLE(vk_command_buffer, command_buffer, _commandBuffer);

   /* If the latest label was submitted by CmdInsertDebugUtilsLabelEXT, we
    * should remove it first.
    */
   if (!command_buffer->region_begin)
      (void)util_dynarray_pop(&command_buffer->labels, VkDebugUtilsLabelEXT);

   util_dynarray_append(&command_buffer->labels, VkDebugUtilsLabelEXT,
                        *pLabelInfo);
   command_buffer->region_begin = true;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdEndDebugUtilsLabelEXT(VkCommandBuffer _commandBuffer)
{
   VK_FROM_HANDLE(vk_command_buffer, command_buffer, _commandBuffer);

   /* If the latest label was submitted by CmdInsertDebugUtilsLabelEXT, we
    * should remove it first.
    */
   if (!command_buffer->region_begin)
      (void)util_dynarray_pop(&command_buffer->labels, VkDebugUtilsLabelEXT);

   (void)util_dynarray_pop(&command_buffer->labels, VkDebugUtilsLabelEXT);
   command_buffer->region_begin = true;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdInsertDebugUtilsLabelEXT(
   VkCommandBuffer _commandBuffer,
   const VkDebugUtilsLabelEXT *pLabelInfo)
{
   VK_FROM_HANDLE(vk_command_buffer, command_buffer, _commandBuffer);

   /* If the latest label was submitted by CmdInsertDebugUtilsLabelEXT, we
    * should remove it first.
    */
   if (!command_buffer->region_begin)
      (void)util_dynarray_pop(&command_buffer->labels, VkDebugUtilsLabelEXT);

   util_dynarray_append(&command_buffer->labels, VkDebugUtilsLabelEXT,
                        *pLabelInfo);
   command_buffer->region_begin = false;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_QueueBeginDebugUtilsLabelEXT(
   VkQueue _queue,
   const VkDebugUtilsLabelEXT *pLabelInfo)
{
   VK_FROM_HANDLE(vk_queue, queue, _queue);

   /* If the latest label was submitted by QueueInsertDebugUtilsLabelEXT, we
    * should remove it first.
    */
   if (!queue->region_begin)
      (void)util_dynarray_pop(&queue->labels, VkDebugUtilsLabelEXT);

   util_dynarray_append(&queue->labels, VkDebugUtilsLabelEXT, *pLabelInfo);
   queue->region_begin = true;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_QueueEndDebugUtilsLabelEXT(VkQueue _queue)
{
   VK_FROM_HANDLE(vk_queue, queue, _queue);

   /* If the latest label was submitted by QueueInsertDebugUtilsLabelEXT, we
    * should remove it first.
    */
   if (!queue->region_begin)
      (void)util_dynarray_pop(&queue->labels, VkDebugUtilsLabelEXT);

   (void)util_dynarray_pop(&queue->labels, VkDebugUtilsLabelEXT);
   queue->region_begin = true;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_QueueInsertDebugUtilsLabelEXT(
   VkQueue _queue,
   const VkDebugUtilsLabelEXT *pLabelInfo)
{
   VK_FROM_HANDLE(vk_queue, queue, _queue);

   /* If the latest label was submitted by QueueInsertDebugUtilsLabelEXT, we
    * should remove it first.
    */
   if (!queue->region_begin)
      (void)util_dynarray_pop(&queue->labels, VkDebugUtilsLabelEXT);

   util_dynarray_append(&queue->labels, VkDebugUtilsLabelEXT, *pLabelInfo);
   queue->region_begin = false;
}
