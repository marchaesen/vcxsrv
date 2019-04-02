/*
 * Copyright © 2019 Intel Corporation
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

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include "imgui.h"

#include "overlay_params.h"

#include "util/debug.h"
#include "util/hash_table.h"
#include "util/ralloc.h"
#include "util/os_time.h"
#include "util/simple_mtx.h"

#include "vk_enum_to_str.h"
#include "vk_util.h"

/* Mapped from VkInstace/VkPhysicalDevice */
struct instance_data {
   struct vk_instance_dispatch_table vtable;
   VkInstance instance;

   struct overlay_params params;
};

struct frame_stat {
   uint32_t stats[OVERLAY_PARAM_ENABLED_MAX];
};

/* Mapped from VkDevice/VkCommandBuffer */
struct queue_data;
struct device_data {
   struct instance_data *instance;

   PFN_vkSetDeviceLoaderData set_device_loader_data;

   struct vk_device_dispatch_table vtable;
   VkPhysicalDevice physical_device;
   VkDevice device;

   VkPhysicalDeviceProperties properties;

   struct queue_data *graphic_queue;

   struct queue_data **queues;
   uint32_t n_queues;

   struct frame_stat stats;
};

/* Mapped from VkQueue */
struct queue_data {
   struct device_data *device;

   VkQueue queue;
   VkQueueFlags flags;
   uint32_t family_index;
};

/* Mapped from VkSwapchainKHR */
struct swapchain_data {
   struct device_data *device;

   VkSwapchainKHR swapchain;
   unsigned width, height;
   VkFormat format;

   uint32_t n_images;
   VkImage *images;
   VkImageView *image_views;
   VkFramebuffer *framebuffers;

   VkRenderPass render_pass;

   VkDescriptorPool descriptor_pool;
   VkDescriptorSetLayout descriptor_layout;
   VkDescriptorSet descriptor_set;

   VkSampler font_sampler;

   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;

   VkCommandPool command_pool;

   struct {
      VkCommandBuffer command_buffer;

      VkBuffer vertex_buffer;
      VkDeviceMemory vertex_buffer_mem;
      VkDeviceSize vertex_buffer_size;

      VkBuffer index_buffer;
      VkDeviceMemory index_buffer_mem;
      VkDeviceSize index_buffer_size;
   } frame_data[2];

   bool font_uploaded;
   VkImage font_image;
   VkImageView font_image_view;
   VkDeviceMemory font_mem;
   VkBuffer upload_font_buffer;
   VkDeviceMemory upload_font_buffer_mem;

   VkFence fence;
   VkSemaphore submission_semaphore;

   /**/
   ImGuiContext* imgui_context;
   ImVec2 window_size;

   /**/
   uint64_t n_frames;
   uint64_t last_present_time;

   unsigned n_frames_since_update;
   uint64_t last_fps_update;
   double fps;

   double frame_times[200];

   double acquire_times[200];
   uint64_t n_acquire;

   enum overlay_param_enabled stat_selector;
   struct frame_stat stats_min, stats_max;
   struct frame_stat stats[200];
};

static struct hash_table *vk_object_to_data = NULL;
static simple_mtx_t vk_object_to_data_mutex = _SIMPLE_MTX_INITIALIZER_NP;

thread_local ImGuiContext* __MesaImGui;

static inline void ensure_vk_object_map(void)
{
   if (!vk_object_to_data) {
      vk_object_to_data = _mesa_hash_table_create(NULL,
                                                  _mesa_hash_pointer,
                                                  _mesa_key_pointer_equal);
   }
}

#define FIND_SWAPCHAIN_DATA(obj) ((struct swapchain_data *)find_object_data((void *) obj))
#define FIND_DEVICE_DATA(obj) ((struct device_data *)find_object_data((void *) obj))
#define FIND_QUEUE_DATA(obj) ((struct queue_data *)find_object_data((void *) obj))
#define FIND_PHYSICAL_DEVICE_DATA(obj) ((struct instance_data *)find_object_data((void *) obj))
#define FIND_INSTANCE_DATA(obj) ((struct instance_data *)find_object_data((void *) obj))
static void *find_object_data(void *obj)
{
   simple_mtx_lock(&vk_object_to_data_mutex);
   ensure_vk_object_map();
   struct hash_entry *entry = _mesa_hash_table_search(vk_object_to_data, obj);
   void *data = entry ? entry->data : NULL;
   simple_mtx_unlock(&vk_object_to_data_mutex);
   return data;
}

static void map_object(void *obj, void *data)
{
   simple_mtx_lock(&vk_object_to_data_mutex);
   ensure_vk_object_map();
   _mesa_hash_table_insert(vk_object_to_data, obj, data);
   simple_mtx_unlock(&vk_object_to_data_mutex);
}

static void unmap_object(void *obj)
{
   simple_mtx_lock(&vk_object_to_data_mutex);
   struct hash_entry *entry = _mesa_hash_table_search(vk_object_to_data, obj);
   _mesa_hash_table_remove(vk_object_to_data, entry);
   simple_mtx_unlock(&vk_object_to_data_mutex);
}

/**/

#define VK_CHECK(expr) \
   do { \
      VkResult __result = (expr); \
      if (__result != VK_SUCCESS) { \
         fprintf(stderr, "'%s' line %i failed with %s\n", \
                 #expr, __LINE__, vk_Result_to_str(__result)); \
      } \
   } while (0)

/**/

static VkLayerInstanceCreateInfo *get_instance_chain_info(const VkInstanceCreateInfo *pCreateInfo,
                                                          VkLayerFunction func)
{
   vk_foreach_struct(item, pCreateInfo->pNext) {
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
   vk_foreach_struct(item, pCreateInfo->pNext) {
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
   map_object(data->instance, data);
   return data;
}

static void destroy_instance_data(struct instance_data *data)
{
   if (data->params.output_file)
      fclose(data->params.output_file);
   unmap_object(data->instance);
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
         map_object(physicalDevices[i], instance_data);
      else
         unmap_object(physicalDevices[i]);
   }

   free(physicalDevices);
}

/**/
static struct device_data *new_device_data(VkDevice device, struct instance_data *instance)
{
   struct device_data *data = rzalloc(NULL, struct device_data);
   data->instance = instance;
   data->device = device;
   map_object(data->device, data);
   return data;
}

static struct queue_data *new_queue_data(VkQueue queue,
                                         const VkQueueFamilyProperties *family_props,
                                         uint32_t family_index,
                                         struct device_data *device_data)
{
   struct queue_data *data = rzalloc(device_data, struct queue_data);
   data->device = device_data;
   data->queue = queue;
   data->flags = family_props->queueFlags;
   data->family_index = family_index;
   map_object(data->queue, data);

   if (data->flags & VK_QUEUE_GRAPHICS_BIT)
      device_data->graphic_queue = data;

   return data;
}

static void device_map_queues(struct device_data *data,
                              const VkDeviceCreateInfo *pCreateInfo)
{
   for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
      data->n_queues += pCreateInfo->pQueueCreateInfos[i].queueCount;
   data->queues = ralloc_array(data, struct queue_data *, data->n_queues);

   struct instance_data *instance_data = data->instance;
   uint32_t n_family_props;
   instance_data->vtable.GetPhysicalDeviceQueueFamilyProperties(data->physical_device,
                                                                &n_family_props,
                                                                NULL);
   VkQueueFamilyProperties *family_props =
      (VkQueueFamilyProperties *)malloc(sizeof(VkQueueFamilyProperties) * n_family_props);
   instance_data->vtable.GetPhysicalDeviceQueueFamilyProperties(data->physical_device,
                                                                &n_family_props,
                                                                family_props);

   uint32_t queue_index = 0;
   for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      for (uint32_t j = 0; j < pCreateInfo->pQueueCreateInfos[i].queueCount; j++) {
         VkQueue queue;
         data->vtable.GetDeviceQueue(data->device,
                                     pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex,
                                     j, &queue);

         VK_CHECK(data->set_device_loader_data(data->device, queue));

         data->queues[queue_index++] =
            new_queue_data(queue, &family_props[pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex],
                           pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex, data);
      }
   }

   free(family_props);
}

static void device_unmap_queues(struct device_data *data)
{
   for (uint32_t i = 0; i < data->n_queues; i++)
      unmap_object(data->queues[i]->queue);
}

static void destroy_device_data(struct device_data *data)
{
   unmap_object(data->device);
   ralloc_free(data);
}

/**/
static struct swapchain_data *new_swapchain_data(VkSwapchainKHR swapchain,
                                                 struct device_data *device_data)
{
   struct swapchain_data *data = rzalloc(NULL, struct swapchain_data);
   data->device = device_data;
   data->swapchain = swapchain;
   data->window_size = ImVec2(300, 300);
   map_object((void *) data->swapchain, data);
   return data;
}

static void destroy_swapchain_data(struct swapchain_data *data)
{
   unmap_object((void *) data->swapchain);
   ralloc_free(data);
}

static void snapshot_swapchain_frame(struct swapchain_data *data)
{
   struct instance_data *instance_data = data->device->instance;
   uint64_t now = os_time_get(); /* us */

   if (data->last_present_time) {
      data->frame_times[(data->n_frames - 1) % ARRAY_SIZE(data->frame_times)] =
         ((double)now - (double)data->last_present_time) / 1000.0;
   }

   if (data->last_fps_update) {
      double elapsed = (double)(now - data->last_fps_update); /* us */
      if (elapsed >= instance_data->params.fps_sampling_period) {
         data->fps = 1000000.0f * data->n_frames_since_update / elapsed;
         data->n_frames_since_update = 0;
         data->last_fps_update = now;
         if (instance_data->params.output_file) {
            fprintf(instance_data->params.output_file, "%.2f\n", data->fps);
            fflush(instance_data->params.output_file);
         }
      }
   } else {
      data->last_fps_update = now;
   }

   struct device_data *device_data = data->device;
   data->stats[data->n_frames % ARRAY_SIZE(data->frame_times)] = device_data->stats;
   memset(&device_data->stats, 0, sizeof(device_data->stats));

   data->last_present_time = now;
   data->n_frames++;
   data->n_frames_since_update++;
}

static float get_frame_timing(void *_data, int _idx)
{
   struct swapchain_data *data = (struct swapchain_data *) _data;
   if ((ARRAY_SIZE(data->frame_times) - _idx) > (data->n_frames - 2))
      return 0.0f;
   int idx = ARRAY_SIZE(data->frame_times) +
      (data->n_frames - 2) < ARRAY_SIZE(data->frame_times) ?
      _idx - (data->n_frames - 2) :
      _idx + (data->n_frames - 2);
   idx %= ARRAY_SIZE(data->frame_times);
   return data->frame_times[idx];
}

static float get_acquire_timing(void *_data, int _idx)
{
   struct swapchain_data *data = (struct swapchain_data *) _data;
   if ((ARRAY_SIZE(data->acquire_times) - _idx) > data->n_acquire)
      return 0.0f;
   int idx = ARRAY_SIZE(data->acquire_times) +
      data->n_acquire < ARRAY_SIZE(data->acquire_times) ?
      _idx - data->n_acquire :
      _idx + data->n_acquire;
   idx %= ARRAY_SIZE(data->acquire_times);
   return data->acquire_times[idx];
}

static float get_stat(void *_data, int _idx)
{
   struct swapchain_data *data = (struct swapchain_data *) _data;
   if ((ARRAY_SIZE(data->stats) - _idx) > data->n_frames)
      return 0.0f;
   int idx = ARRAY_SIZE(data->stats) +
      data->n_frames < ARRAY_SIZE(data->stats) ?
      _idx - data->n_frames :
      _idx + data->n_frames;
   idx %= ARRAY_SIZE(data->stats);
   return data->stats[idx].stats[data->stat_selector];
}

static void position_layer(struct swapchain_data *data)

{
   struct device_data *device_data = data->device;
   struct instance_data *instance_data = device_data->instance;

   ImGui::SetNextWindowBgAlpha(0.5);
   ImGui::SetNextWindowSize(data->window_size, ImGuiCond_Always);
   switch (instance_data->params.position) {
   case LAYER_POSITION_TOP_LEFT:
      ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
      break;
   case LAYER_POSITION_TOP_RIGHT:
      ImGui::SetNextWindowPos(ImVec2(data->width - data->window_size.x, 0),
                              ImGuiCond_Always);
      break;
   case LAYER_POSITION_BOTTOM_LEFT:
      ImGui::SetNextWindowPos(ImVec2(0, data->height - data->window_size.y),
                              ImGuiCond_Always);
      break;
   case LAYER_POSITION_BOTTOM_RIGHT:
      ImGui::SetNextWindowPos(ImVec2(data->width - data->window_size.x,
                                     data->height - data->window_size.y),
                              ImGuiCond_Always);
      break;
   }
}

static void compute_swapchain_display(struct swapchain_data *data)
{
   struct device_data *device_data = data->device;
   struct instance_data *instance_data = device_data->instance;

   ImGui::SetCurrentContext(data->imgui_context);
   ImGui::NewFrame();
   position_layer(data);
   ImGui::Begin("Mesa overlay");
   ImGui::Text("Device: %s", device_data->properties.deviceName);

   const char *format_name = vk_Format_to_str(data->format);
   format_name = format_name ? (format_name + strlen("VK_FORMAT_")) : "unknown";
   ImGui::Text("Swapchain format: %s", format_name);
   ImGui::Text("Frames: %" PRIu64, data->n_frames);
   if (instance_data->params.enabled[OVERLAY_PARAM_ENABLED_fps])
      ImGui::Text("FPS: %.2f" , data->fps);

   if (instance_data->params.enabled[OVERLAY_PARAM_ENABLED_frame_timing]){
      double min_time = FLT_MAX, max_time = 0.0f;
      for (uint32_t i = 0; i < MIN2(data->n_frames - 2, ARRAY_SIZE(data->frame_times)); i++) {
         min_time = MIN2(min_time, data->frame_times[i]);
         max_time = MAX2(max_time, data->frame_times[i]);
      }
      ImGui::PlotHistogram("##Frame timings", get_frame_timing, data,
                           ARRAY_SIZE(data->frame_times), 0,
                           NULL, min_time, max_time,
                           ImVec2(ImGui::GetContentRegionAvailWidth(), 30));
      ImGui::Text("Frame timing: %.3fms [%.3f, %.3f]",
                  get_frame_timing(data, ARRAY_SIZE(data->frame_times) - 1),
                  min_time, max_time);
   }

   if (instance_data->params.enabled[OVERLAY_PARAM_ENABLED_acquire_timing]) {
      double min_time = FLT_MAX, max_time = 0.0f;
      for (uint32_t i = 0; i < MIN2(data->n_acquire - 2, ARRAY_SIZE(data->acquire_times)); i++) {
         min_time = MIN2(min_time, data->acquire_times[i]);
      max_time = MAX2(max_time, data->acquire_times[i]);
      }
      ImGui::PlotHistogram("##Acquire timings", get_acquire_timing, data,
                           ARRAY_SIZE(data->acquire_times), 0,
                           NULL, min_time, max_time,
                           ImVec2(ImGui::GetContentRegionAvailWidth(), 30));
      ImGui::Text("Acquire timing: %.3fms [%.3f, %.3f]",
                  get_acquire_timing(data, ARRAY_SIZE(data->acquire_times) - 1),
                  min_time, max_time);
   }

   for (uint32_t i = 0; i < ARRAY_SIZE(data->stats_min.stats); i++) {
      data->stats_min.stats[i] = UINT32_MAX;
      data->stats_max.stats[i] = 0;
   }
   for (uint32_t i = 0; i < MIN2(data->n_frames - 1, ARRAY_SIZE(data->stats)); i++) {
      for (uint32_t j = 0; j < ARRAY_SIZE(data->stats[0].stats); j++) {
         data->stats_min.stats[j] = MIN2(data->stats[i].stats[j],
                                         data->stats_min.stats[j]);
         data->stats_max.stats[j] = MAX2(data->stats[i].stats[j],
                                         data->stats_max.stats[j]);
      }
   }

   for (uint32_t i = 0; i < ARRAY_SIZE(device_data->stats.stats); i++) {
      if (!instance_data->params.enabled[i] ||
          i == OVERLAY_PARAM_ENABLED_fps ||
          i == OVERLAY_PARAM_ENABLED_frame_timing ||
          i == OVERLAY_PARAM_ENABLED_acquire_timing)
         continue;

      char hash[40];
      snprintf(hash, sizeof(hash), "##%s", overlay_param_names[i]);
      data->stat_selector = (enum overlay_param_enabled) i;

      ImGui::PlotHistogram(hash, get_stat, data,
                           ARRAY_SIZE(data->stats), 0,
                           NULL,
                           data->stats_min.stats[i],
                           data->stats_max.stats[i],
                           ImVec2(ImGui::GetContentRegionAvailWidth(), 30));
      ImGui::Text("%s: %.0f [%u, %u]", overlay_param_names[i],
                  get_stat(data, ARRAY_SIZE(data->stats) - 1),
                  data->stats_min.stats[i], data->stats_max.stats[i]);
   }
   data->window_size = ImVec2(data->window_size.x, ImGui::GetCursorPosY() + 10.0f);
   ImGui::End();
   ImGui::EndFrame();
   ImGui::Render();
}

static uint32_t vk_memory_type(struct device_data *data,
                               VkMemoryPropertyFlags properties,
                               uint32_t type_bits)
{
    VkPhysicalDeviceMemoryProperties prop;
    data->instance->vtable.GetPhysicalDeviceMemoryProperties(data->physical_device, &prop);
    for (uint32_t i = 0; i < prop.memoryTypeCount; i++)
        if ((prop.memoryTypes[i].propertyFlags & properties) == properties && type_bits & (1<<i))
            return i;
    return 0xFFFFFFFF; // Unable to find memoryType
}

static void ensure_swapchain_fonts(struct swapchain_data *data,
                                   VkCommandBuffer command_buffer)
{
   if (data->font_uploaded)
      return;

   data->font_uploaded = true;

   struct device_data *device_data = data->device;
   ImGuiIO& io = ImGui::GetIO();
   unsigned char* pixels;
   int width, height;
   io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
   size_t upload_size = width * height * 4 * sizeof(char);

   /* Upload buffer */
   VkBufferCreateInfo buffer_info = {};
   buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   buffer_info.size = upload_size;
   buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
   buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   VK_CHECK(device_data->vtable.CreateBuffer(device_data->device, &buffer_info,
                                             NULL, &data->upload_font_buffer));
   VkMemoryRequirements upload_buffer_req;
   device_data->vtable.GetBufferMemoryRequirements(device_data->device,
                                                   data->upload_font_buffer,
                                                   &upload_buffer_req);
   VkMemoryAllocateInfo upload_alloc_info = {};
   upload_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   upload_alloc_info.allocationSize = upload_buffer_req.size;
   upload_alloc_info.memoryTypeIndex = vk_memory_type(device_data,
                                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                                      upload_buffer_req.memoryTypeBits);
   VK_CHECK(device_data->vtable.AllocateMemory(device_data->device,
                                               &upload_alloc_info,
                                               NULL,
                                               &data->upload_font_buffer_mem));
   VK_CHECK(device_data->vtable.BindBufferMemory(device_data->device,
                                                 data->upload_font_buffer,
                                                 data->upload_font_buffer_mem, 0));

   /* Upload to Buffer */
   char* map = NULL;
   VK_CHECK(device_data->vtable.MapMemory(device_data->device,
                                          data->upload_font_buffer_mem,
                                          0, upload_size, 0, (void**)(&map)));
   memcpy(map, pixels, upload_size);
   VkMappedMemoryRange range[1] = {};
   range[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
   range[0].memory = data->upload_font_buffer_mem;
   range[0].size = upload_size;
   VK_CHECK(device_data->vtable.FlushMappedMemoryRanges(device_data->device, 1, range));
   device_data->vtable.UnmapMemory(device_data->device,
                                   data->upload_font_buffer_mem);

   /* Copy buffer to image */
   VkImageMemoryBarrier copy_barrier[1] = {};
   copy_barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
   copy_barrier[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   copy_barrier[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   copy_barrier[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   copy_barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   copy_barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   copy_barrier[0].image = data->font_image;
   copy_barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   copy_barrier[0].subresourceRange.levelCount = 1;
   copy_barrier[0].subresourceRange.layerCount = 1;
   device_data->vtable.CmdPipelineBarrier(command_buffer,
                                          VK_PIPELINE_STAGE_HOST_BIT,
                                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                                          0, 0, NULL, 0, NULL,
                                          1, copy_barrier);

   VkBufferImageCopy region = {};
   region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   region.imageSubresource.layerCount = 1;
   region.imageExtent.width = width;
   region.imageExtent.height = height;
   region.imageExtent.depth = 1;
   device_data->vtable.CmdCopyBufferToImage(command_buffer,
                                            data->upload_font_buffer,
                                            data->font_image,
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            1, &region);

   VkImageMemoryBarrier use_barrier[1] = {};
   use_barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
   use_barrier[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   use_barrier[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   use_barrier[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   use_barrier[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   use_barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   use_barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   use_barrier[0].image = data->font_image;
   use_barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   use_barrier[0].subresourceRange.levelCount = 1;
   use_barrier[0].subresourceRange.layerCount = 1;
   device_data->vtable.CmdPipelineBarrier(command_buffer,
                                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                          0,
                                          0, NULL,
                                          0, NULL,
                                          1, use_barrier);

   /* Store our identifier */
   io.Fonts->TexID = (ImTextureID)(intptr_t)data->font_image;
}

static void CreateOrResizeBuffer(struct device_data *data,
                                 VkBuffer *buffer,
                                 VkDeviceMemory *buffer_memory,
                                 VkDeviceSize *buffer_size,
                                 size_t new_size, VkBufferUsageFlagBits usage)
{
    if (*buffer != VK_NULL_HANDLE)
        data->vtable.DestroyBuffer(data->device, *buffer, NULL);
    if (*buffer_memory)
        data->vtable.FreeMemory(data->device, *buffer_memory, NULL);

    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = new_size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(data->vtable.CreateBuffer(data->device, &buffer_info, NULL, buffer));

    VkMemoryRequirements req;
    data->vtable.GetBufferMemoryRequirements(data->device, *buffer, &req);
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = req.size;
    alloc_info.memoryTypeIndex =
       vk_memory_type(data, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, req.memoryTypeBits);
    VK_CHECK(data->vtable.AllocateMemory(data->device, &alloc_info, NULL, buffer_memory));

    VK_CHECK(data->vtable.BindBufferMemory(data->device, *buffer, *buffer_memory, 0));
    *buffer_size = new_size;
}

static void render_swapchain_display(struct swapchain_data *data, unsigned image_index)
{
   ImDrawData* draw_data = ImGui::GetDrawData();
   if (draw_data->TotalVtxCount == 0)
      return;

   struct device_data *device_data = data->device;
   uint32_t idx = data->n_frames % ARRAY_SIZE(data->frame_data);
   VkCommandBuffer command_buffer = data->frame_data[idx].command_buffer;

   device_data->vtable.ResetCommandBuffer(command_buffer, 0);

   VkRenderPassBeginInfo render_pass_info = {};
   render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   render_pass_info.renderPass = data->render_pass;
   render_pass_info.framebuffer = data->framebuffers[image_index];
   render_pass_info.renderArea.extent.width = data->width;
   render_pass_info.renderArea.extent.height = data->height;

   VkCommandBufferBeginInfo buffer_begin_info = {};
   buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

   device_data->vtable.BeginCommandBuffer(command_buffer, &buffer_begin_info);

   ensure_swapchain_fonts(data, command_buffer);

   /* Bounce the image to display back to color attachment layout for
    * rendering on top of it.
    */
   VkImageMemoryBarrier imb;
   imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
   imb.pNext = nullptr;
   imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   imb.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   imb.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
   imb.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   imb.image = data->images[image_index];
   imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   imb.subresourceRange.baseMipLevel = 0;
   imb.subresourceRange.levelCount = 1;
   imb.subresourceRange.baseArrayLayer = 0;
   imb.subresourceRange.layerCount = 1;
   imb.srcQueueFamilyIndex = device_data->graphic_queue->family_index;
   imb.dstQueueFamilyIndex = device_data->graphic_queue->family_index;
   device_data->vtable.CmdPipelineBarrier(command_buffer,
                                          VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                          VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                          0,          /* dependency flags */
                                          0, nullptr, /* memory barriers */
                                          0, nullptr, /* buffer memory barriers */
                                          1, &imb);   /* image memory barriers */

   device_data->vtable.CmdBeginRenderPass(command_buffer, &render_pass_info,
                                          VK_SUBPASS_CONTENTS_INLINE);

   /* Create/Resize vertex & index buffers */
   size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
   size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
   if (data->frame_data[idx].vertex_buffer_size < vertex_size) {
      CreateOrResizeBuffer(device_data,
                           &data->frame_data[idx].vertex_buffer,
                           &data->frame_data[idx].vertex_buffer_mem,
                           &data->frame_data[idx].vertex_buffer_size,
                           vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
   }
   if (data->frame_data[idx].index_buffer_size < index_size) {
      CreateOrResizeBuffer(device_data,
                           &data->frame_data[idx].index_buffer,
                           &data->frame_data[idx].index_buffer_mem,
                           &data->frame_data[idx].index_buffer_size,
                           index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
   }

    /* Upload vertex & index data */
    VkBuffer vertex_buffer = data->frame_data[idx].vertex_buffer;
    VkDeviceMemory vertex_mem = data->frame_data[idx].vertex_buffer_mem;
    VkBuffer index_buffer = data->frame_data[idx].index_buffer;
    VkDeviceMemory index_mem = data->frame_data[idx].index_buffer_mem;
    ImDrawVert* vtx_dst = NULL;
    ImDrawIdx* idx_dst = NULL;
    VK_CHECK(device_data->vtable.MapMemory(device_data->device, vertex_mem,
                                           0, vertex_size, 0, (void**)(&vtx_dst)));
    VK_CHECK(device_data->vtable.MapMemory(device_data->device, index_mem,
                                           0, index_size, 0, (void**)(&idx_dst)));
    for (int n = 0; n < draw_data->CmdListsCount; n++)
        {
           const ImDrawList* cmd_list = draw_data->CmdLists[n];
           memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
           memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
           vtx_dst += cmd_list->VtxBuffer.Size;
           idx_dst += cmd_list->IdxBuffer.Size;
        }
    VkMappedMemoryRange range[2] = {};
    range[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range[0].memory = vertex_mem;
    range[0].size = VK_WHOLE_SIZE;
    range[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range[1].memory = index_mem;
    range[1].size = VK_WHOLE_SIZE;
    VK_CHECK(device_data->vtable.FlushMappedMemoryRanges(device_data->device, 2, range));
    device_data->vtable.UnmapMemory(device_data->device, vertex_mem);
    device_data->vtable.UnmapMemory(device_data->device, index_mem);

    /* Bind pipeline and descriptor sets */
    device_data->vtable.CmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, data->pipeline);
    VkDescriptorSet desc_set[1] = { data->descriptor_set };
    device_data->vtable.CmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                              data->pipeline_layout, 0, 1, desc_set, 0, NULL);

    /* Bind vertex & index buffers */
    VkBuffer vertex_buffers[1] = { vertex_buffer };
    VkDeviceSize vertex_offset[1] = { 0 };
    device_data->vtable.CmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, vertex_offset);
    device_data->vtable.CmdBindIndexBuffer(command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT16);

    /* Setup viewport */
    VkViewport viewport;
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = draw_data->DisplaySize.x;
    viewport.height = draw_data->DisplaySize.y;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    device_data->vtable.CmdSetViewport(command_buffer, 0, 1, &viewport);


    /* Setup scale and translation through push constants :
     *
     * Our visible imgui space lies from draw_data->DisplayPos (top left) to
     * draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayMin
     * is typically (0,0) for single viewport apps.
     */
    float scale[2];
    scale[0] = 2.0f / draw_data->DisplaySize.x;
    scale[1] = 2.0f / draw_data->DisplaySize.y;
    float translate[2];
    translate[0] = -1.0f - draw_data->DisplayPos.x * scale[0];
    translate[1] = -1.0f - draw_data->DisplayPos.y * scale[1];
    device_data->vtable.CmdPushConstants(command_buffer, data->pipeline_layout,
                                         VK_SHADER_STAGE_VERTEX_BIT,
                                         sizeof(float) * 0, sizeof(float) * 2, scale);
    device_data->vtable.CmdPushConstants(command_buffer, data->pipeline_layout,
                                         VK_SHADER_STAGE_VERTEX_BIT,
                                         sizeof(float) * 2, sizeof(float) * 2, translate);

    // Render the command lists:
    int vtx_offset = 0;
    int idx_offset = 0;
    ImVec2 display_pos = draw_data->DisplayPos;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            // Apply scissor/clipping rectangle
            // FIXME: We could clamp width/height based on clamped min/max values.
            VkRect2D scissor;
            scissor.offset.x = (int32_t)(pcmd->ClipRect.x - display_pos.x) > 0 ? (int32_t)(pcmd->ClipRect.x - display_pos.x) : 0;
            scissor.offset.y = (int32_t)(pcmd->ClipRect.y - display_pos.y) > 0 ? (int32_t)(pcmd->ClipRect.y - display_pos.y) : 0;
            scissor.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
            scissor.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y + 1); // FIXME: Why +1 here?
            device_data->vtable.CmdSetScissor(command_buffer, 0, 1, &scissor);

            // Draw
            device_data->vtable.CmdDrawIndexed(command_buffer, pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);

            idx_offset += pcmd->ElemCount;
        }
        vtx_offset += cmd_list->VtxBuffer.Size;
    }

   device_data->vtable.CmdEndRenderPass(command_buffer);
   device_data->vtable.EndCommandBuffer(command_buffer);

   if (data->submission_semaphore) {
      device_data->vtable.DestroySemaphore(device_data->device,
                                           data->submission_semaphore,
                                           NULL);
   }
   /* Submission semaphore */
   VkSemaphoreCreateInfo semaphore_info = {};
   semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
   VK_CHECK(device_data->vtable.CreateSemaphore(device_data->device, &semaphore_info,
                                                NULL, &data->submission_semaphore));

   VkSubmitInfo submit_info = {};
   VkPipelineStageFlags stage_wait = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
   submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   submit_info.commandBufferCount = 1;
   submit_info.pCommandBuffers = &command_buffer;
   submit_info.pWaitDstStageMask = &stage_wait;
   submit_info.signalSemaphoreCount = 1;
   submit_info.pSignalSemaphores = &data->submission_semaphore;

   device_data->vtable.WaitForFences(device_data->device, 1, &data->fence, VK_TRUE, UINT64_MAX);
   device_data->vtable.ResetFences(device_data->device, 1, &data->fence);
   device_data->vtable.QueueSubmit(device_data->graphic_queue->queue, 1, &submit_info, data->fence);
}

static const uint32_t overlay_vert_spv[] = {
#include "overlay.vert.spv.h"
};
static const uint32_t overlay_frag_spv[] = {
#include "overlay.frag.spv.h"
};

static void setup_swapchain_data_pipeline(struct swapchain_data *data)
{
   struct device_data *device_data = data->device;
   VkShaderModule vert_module, frag_module;

   /* Create shader modules */
   VkShaderModuleCreateInfo vert_info = {};
   vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   vert_info.codeSize = sizeof(overlay_vert_spv);
   vert_info.pCode = overlay_vert_spv;
   VK_CHECK(device_data->vtable.CreateShaderModule(device_data->device,
                                                   &vert_info, NULL, &vert_module));
   VkShaderModuleCreateInfo frag_info = {};
   frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   frag_info.codeSize = sizeof(overlay_frag_spv);
   frag_info.pCode = (uint32_t*)overlay_frag_spv;
   VK_CHECK(device_data->vtable.CreateShaderModule(device_data->device,
                                                   &frag_info, NULL, &frag_module));

   /* Font sampler */
   VkSamplerCreateInfo sampler_info = {};
   sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
   sampler_info.magFilter = VK_FILTER_LINEAR;
   sampler_info.minFilter = VK_FILTER_LINEAR;
   sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
   sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
   sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
   sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
   sampler_info.minLod = -1000;
   sampler_info.maxLod = 1000;
   sampler_info.maxAnisotropy = 1.0f;
   VK_CHECK(device_data->vtable.CreateSampler(device_data->device, &sampler_info,
                                              NULL, &data->font_sampler));

   /* Descriptor pool */
   VkDescriptorPoolSize sampler_pool_size = {};
   sampler_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
   sampler_pool_size.descriptorCount = 1;
   VkDescriptorPoolCreateInfo desc_pool_info = {};
   desc_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
   desc_pool_info.maxSets = 1;
   desc_pool_info.poolSizeCount = 1;
   desc_pool_info.pPoolSizes = &sampler_pool_size;
   VK_CHECK(device_data->vtable.CreateDescriptorPool(device_data->device,
                                                     &desc_pool_info,
                                                     NULL, &data->descriptor_pool));

   /* Descriptor layout */
   VkSampler sampler[1] = { data->font_sampler };
   VkDescriptorSetLayoutBinding binding[1] = {};
   binding[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
   binding[0].descriptorCount = 1;
   binding[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
   binding[0].pImmutableSamplers = sampler;
   VkDescriptorSetLayoutCreateInfo set_layout_info = {};
   set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   set_layout_info.bindingCount = 1;
   set_layout_info.pBindings = binding;
   VK_CHECK(device_data->vtable.CreateDescriptorSetLayout(device_data->device,
                                                          &set_layout_info,
                                                          NULL, &data->descriptor_layout));

   /* Descriptor set */
   VkDescriptorSetAllocateInfo alloc_info = {};
   alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   alloc_info.descriptorPool = data->descriptor_pool;
   alloc_info.descriptorSetCount = 1;
   alloc_info.pSetLayouts = &data->descriptor_layout;
   VK_CHECK(device_data->vtable.AllocateDescriptorSets(device_data->device,
                                                       &alloc_info,
                                                       &data->descriptor_set));

   /* Constants: we are using 'vec2 offset' and 'vec2 scale' instead of a full
    * 3d projection matrix
    */
   VkPushConstantRange push_constants[1] = {};
   push_constants[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
   push_constants[0].offset = sizeof(float) * 0;
   push_constants[0].size = sizeof(float) * 4;
   VkPipelineLayoutCreateInfo layout_info = {};
   layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   layout_info.setLayoutCount = 1;
   layout_info.pSetLayouts = &data->descriptor_layout;
   layout_info.pushConstantRangeCount = 1;
   layout_info.pPushConstantRanges = push_constants;
   VK_CHECK(device_data->vtable.CreatePipelineLayout(device_data->device,
                                                     &layout_info,
                                                     NULL, &data->pipeline_layout));

   VkPipelineShaderStageCreateInfo stage[2] = {};
   stage[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   stage[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
   stage[0].module = vert_module;
   stage[0].pName = "main";
   stage[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   stage[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
   stage[1].module = frag_module;
   stage[1].pName = "main";

   VkVertexInputBindingDescription binding_desc[1] = {};
   binding_desc[0].stride = sizeof(ImDrawVert);
   binding_desc[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

   VkVertexInputAttributeDescription attribute_desc[3] = {};
   attribute_desc[0].location = 0;
   attribute_desc[0].binding = binding_desc[0].binding;
   attribute_desc[0].format = VK_FORMAT_R32G32_SFLOAT;
   attribute_desc[0].offset = IM_OFFSETOF(ImDrawVert, pos);
   attribute_desc[1].location = 1;
   attribute_desc[1].binding = binding_desc[0].binding;
   attribute_desc[1].format = VK_FORMAT_R32G32_SFLOAT;
   attribute_desc[1].offset = IM_OFFSETOF(ImDrawVert, uv);
   attribute_desc[2].location = 2;
   attribute_desc[2].binding = binding_desc[0].binding;
   attribute_desc[2].format = VK_FORMAT_R8G8B8A8_UNORM;
   attribute_desc[2].offset = IM_OFFSETOF(ImDrawVert, col);

   VkPipelineVertexInputStateCreateInfo vertex_info = {};
   vertex_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
   vertex_info.vertexBindingDescriptionCount = 1;
   vertex_info.pVertexBindingDescriptions = binding_desc;
   vertex_info.vertexAttributeDescriptionCount = 3;
   vertex_info.pVertexAttributeDescriptions = attribute_desc;

   VkPipelineInputAssemblyStateCreateInfo ia_info = {};
   ia_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
   ia_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

   VkPipelineViewportStateCreateInfo viewport_info = {};
   viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
   viewport_info.viewportCount = 1;
   viewport_info.scissorCount = 1;

   VkPipelineRasterizationStateCreateInfo raster_info = {};
   raster_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
   raster_info.polygonMode = VK_POLYGON_MODE_FILL;
   raster_info.cullMode = VK_CULL_MODE_NONE;
   raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
   raster_info.lineWidth = 1.0f;

   VkPipelineMultisampleStateCreateInfo ms_info = {};
   ms_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
   ms_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

   VkPipelineColorBlendAttachmentState color_attachment[1] = {};
   color_attachment[0].blendEnable = VK_TRUE;
   color_attachment[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
   color_attachment[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
   color_attachment[0].colorBlendOp = VK_BLEND_OP_ADD;
   color_attachment[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
   color_attachment[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
   color_attachment[0].alphaBlendOp = VK_BLEND_OP_ADD;
   color_attachment[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
      VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

   VkPipelineDepthStencilStateCreateInfo depth_info = {};
   depth_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

   VkPipelineColorBlendStateCreateInfo blend_info = {};
   blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
   blend_info.attachmentCount = 1;
   blend_info.pAttachments = color_attachment;

   VkDynamicState dynamic_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
   VkPipelineDynamicStateCreateInfo dynamic_state = {};
   dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
   dynamic_state.dynamicStateCount = (uint32_t)IM_ARRAYSIZE(dynamic_states);
   dynamic_state.pDynamicStates = dynamic_states;

   VkGraphicsPipelineCreateInfo info = {};
   info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
   info.flags = 0;
   info.stageCount = 2;
   info.pStages = stage;
   info.pVertexInputState = &vertex_info;
   info.pInputAssemblyState = &ia_info;
   info.pViewportState = &viewport_info;
   info.pRasterizationState = &raster_info;
   info.pMultisampleState = &ms_info;
   info.pDepthStencilState = &depth_info;
   info.pColorBlendState = &blend_info;
   info.pDynamicState = &dynamic_state;
   info.layout = data->pipeline_layout;
   info.renderPass = data->render_pass;
   VK_CHECK(
      device_data->vtable.CreateGraphicsPipelines(device_data->device, VK_NULL_HANDLE,
                                                  1, &info,
                                                  NULL, &data->pipeline));

   device_data->vtable.DestroyShaderModule(device_data->device, vert_module, NULL);
   device_data->vtable.DestroyShaderModule(device_data->device, frag_module, NULL);

   ImGuiIO& io = ImGui::GetIO();
   unsigned char* pixels;
   int width, height;
   io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

   /* Font image */
   VkImageCreateInfo image_info = {};
   image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   image_info.imageType = VK_IMAGE_TYPE_2D;
   image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
   image_info.extent.width = width;
   image_info.extent.height = height;
   image_info.extent.depth = 1;
   image_info.mipLevels = 1;
   image_info.arrayLayers = 1;
   image_info.samples = VK_SAMPLE_COUNT_1_BIT;
   image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
   image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   VK_CHECK(device_data->vtable.CreateImage(device_data->device, &image_info,
                                            NULL, &data->font_image));
   VkMemoryRequirements font_image_req;
   device_data->vtable.GetImageMemoryRequirements(device_data->device,
                                                  data->font_image, &font_image_req);
   VkMemoryAllocateInfo image_alloc_info = {};
   image_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   image_alloc_info.allocationSize = font_image_req.size;
   image_alloc_info.memoryTypeIndex = vk_memory_type(device_data,
                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                     font_image_req.memoryTypeBits);
   VK_CHECK(device_data->vtable.AllocateMemory(device_data->device, &image_alloc_info,
                                               NULL, &data->font_mem));
   VK_CHECK(device_data->vtable.BindImageMemory(device_data->device,
                                                data->font_image,
                                                data->font_mem, 0));

   /* Font image view */
   VkImageViewCreateInfo view_info = {};
   view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   view_info.image = data->font_image;
   view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
   view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
   view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   view_info.subresourceRange.levelCount = 1;
   view_info.subresourceRange.layerCount = 1;
   VK_CHECK(device_data->vtable.CreateImageView(device_data->device, &view_info,
                                                NULL, &data->font_image_view));

   /* Descriptor set */
   VkDescriptorImageInfo desc_image[1] = {};
   desc_image[0].sampler = data->font_sampler;
   desc_image[0].imageView = data->font_image_view;
   desc_image[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   VkWriteDescriptorSet write_desc[1] = {};
   write_desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
   write_desc[0].dstSet = data->descriptor_set;
   write_desc[0].descriptorCount = 1;
   write_desc[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
   write_desc[0].pImageInfo = desc_image;
   device_data->vtable.UpdateDescriptorSets(device_data->device, 1, write_desc, 0, NULL);
}

static void setup_swapchain_data(struct swapchain_data *data,
                                 const VkSwapchainCreateInfoKHR *pCreateInfo)
{
   data->width = pCreateInfo->imageExtent.width;
   data->height = pCreateInfo->imageExtent.height;
   data->format = pCreateInfo->imageFormat;

   data->imgui_context = ImGui::CreateContext();
   ImGui::SetCurrentContext(data->imgui_context);

   ImGui::GetIO().IniFilename = NULL;
   ImGui::GetIO().DisplaySize = ImVec2((float)data->width, (float)data->height);

   struct device_data *device_data = data->device;

   /* Render pass */
   VkAttachmentDescription attachment_desc = {};
   attachment_desc.format = pCreateInfo->imageFormat;
   attachment_desc.samples = VK_SAMPLE_COUNT_1_BIT;
   attachment_desc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
   attachment_desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachment_desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachment_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachment_desc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   attachment_desc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
   VkAttachmentReference color_attachment = {};
   color_attachment.attachment = 0;
   color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   VkSubpassDescription subpass = {};
   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass.colorAttachmentCount = 1;
   subpass.pColorAttachments = &color_attachment;
   VkSubpassDependency dependency = {};
   dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
   dependency.dstSubpass = 0;
   dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
   dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
   dependency.srcAccessMask = 0;
   dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   VkRenderPassCreateInfo render_pass_info = {};
   render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   render_pass_info.attachmentCount = 1;
   render_pass_info.pAttachments = &attachment_desc;
   render_pass_info.subpassCount = 1;
   render_pass_info.pSubpasses = &subpass;
   render_pass_info.dependencyCount = 1;
   render_pass_info.pDependencies = &dependency;
   VK_CHECK(device_data->vtable.CreateRenderPass(device_data->device,
                                                 &render_pass_info,
                                                 NULL, &data->render_pass));

   setup_swapchain_data_pipeline(data);

   VK_CHECK(device_data->vtable.GetSwapchainImagesKHR(device_data->device,
                                                      data->swapchain,
                                                      &data->n_images,
                                                      NULL));

   data->images = ralloc_array(data, VkImage, data->n_images);
   data->image_views = ralloc_array(data, VkImageView, data->n_images);
   data->framebuffers = ralloc_array(data, VkFramebuffer, data->n_images);

   VK_CHECK(device_data->vtable.GetSwapchainImagesKHR(device_data->device,
                                                      data->swapchain,
                                                      &data->n_images,
                                                      data->images));

   /* Image views */
   VkImageViewCreateInfo view_info = {};
   view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
   view_info.format = pCreateInfo->imageFormat;
   view_info.components.r = VK_COMPONENT_SWIZZLE_R;
   view_info.components.g = VK_COMPONENT_SWIZZLE_G;
   view_info.components.b = VK_COMPONENT_SWIZZLE_B;
   view_info.components.a = VK_COMPONENT_SWIZZLE_A;
   view_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
   for (uint32_t i = 0; i < data->n_images; i++) {
      view_info.image = data->images[i];
      VK_CHECK(device_data->vtable.CreateImageView(device_data->device,
                                                   &view_info, NULL,
                                                   &data->image_views[i]));
   }

   /* Framebuffers */
   VkImageView attachment[1];
   VkFramebufferCreateInfo fb_info = {};
   fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
   fb_info.renderPass = data->render_pass;
   fb_info.attachmentCount = 1;
   fb_info.pAttachments = attachment;
   fb_info.width = data->width;
   fb_info.height = data->height;
   fb_info.layers = 1;
   for (uint32_t i = 0; i < data->n_images; i++) {
      attachment[0] = data->image_views[i];
      VK_CHECK(device_data->vtable.CreateFramebuffer(device_data->device, &fb_info,
                                                     NULL, &data->framebuffers[i]));
   }

   /* Command buffer */
   VkCommandPoolCreateInfo cmd_buffer_pool_info = {};
   cmd_buffer_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   cmd_buffer_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
   cmd_buffer_pool_info.queueFamilyIndex = device_data->graphic_queue->family_index;
   VK_CHECK(device_data->vtable.CreateCommandPool(device_data->device,
                                                  &cmd_buffer_pool_info,
                                                  NULL, &data->command_pool));

   VkCommandBuffer cmd_bufs[ARRAY_SIZE(data->frame_data)];

   VkCommandBufferAllocateInfo cmd_buffer_info = {};
   cmd_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   cmd_buffer_info.commandPool = data->command_pool;
   cmd_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
   cmd_buffer_info.commandBufferCount = 2;
   VK_CHECK(device_data->vtable.AllocateCommandBuffers(device_data->device,
                                                       &cmd_buffer_info,
                                                       cmd_bufs));
   for (uint32_t i = 0; i < ARRAY_SIZE(data->frame_data); i++) {
      VK_CHECK(device_data->set_device_loader_data(device_data->device,
                                                   cmd_bufs[i]));

      data->frame_data[i].command_buffer = cmd_bufs[i];
   }

   /* Submission fence */
   VkFenceCreateInfo fence_info = {};
   fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
   fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
   VK_CHECK(device_data->vtable.CreateFence(device_data->device, &fence_info,
                                            NULL, &data->fence));
}

static void shutdown_swapchain_data(struct swapchain_data *data)
{
   struct device_data *device_data = data->device;

   for (uint32_t i = 0; i < data->n_images; i++) {
      device_data->vtable.DestroyImageView(device_data->device, data->image_views[i], NULL);
      device_data->vtable.DestroyFramebuffer(device_data->device, data->framebuffers[i], NULL);
   }

   device_data->vtable.DestroyRenderPass(device_data->device, data->render_pass, NULL);

   for (uint32_t i = 0; i < ARRAY_SIZE(data->frame_data); i++) {
      device_data->vtable.FreeCommandBuffers(device_data->device,
                                             data->command_pool,
                                             1, &data->frame_data[i].command_buffer);
      if (data->frame_data[i].vertex_buffer)
         device_data->vtable.DestroyBuffer(device_data->device, data->frame_data[i].vertex_buffer, NULL);
      if (data->frame_data[i].index_buffer)
         device_data->vtable.DestroyBuffer(device_data->device, data->frame_data[i].index_buffer, NULL);
      if (data->frame_data[i].vertex_buffer_mem)
         device_data->vtable.FreeMemory(device_data->device, data->frame_data[i].vertex_buffer_mem, NULL);
      if (data->frame_data[i].index_buffer_mem)
         device_data->vtable.FreeMemory(device_data->device, data->frame_data[i].index_buffer_mem, NULL);
   }
   device_data->vtable.DestroyCommandPool(device_data->device, data->command_pool, NULL);

   device_data->vtable.DestroyFence(device_data->device, data->fence, NULL);
   if (data->submission_semaphore)
      device_data->vtable.DestroySemaphore(device_data->device, data->submission_semaphore, NULL);

   device_data->vtable.DestroyPipeline(device_data->device, data->pipeline, NULL);
   device_data->vtable.DestroyPipelineLayout(device_data->device, data->pipeline_layout, NULL);

   device_data->vtable.DestroyDescriptorPool(device_data->device,
                                             data->descriptor_pool, NULL);
   device_data->vtable.DestroyDescriptorSetLayout(device_data->device,
                                                  data->descriptor_layout, NULL);

   device_data->vtable.DestroySampler(device_data->device, data->font_sampler, NULL);
   device_data->vtable.DestroyImageView(device_data->device, data->font_image_view, NULL);
   device_data->vtable.DestroyImage(device_data->device, data->font_image, NULL);
   device_data->vtable.FreeMemory(device_data->device, data->font_mem, NULL);

   device_data->vtable.DestroyBuffer(device_data->device, data->upload_font_buffer, NULL);
   device_data->vtable.FreeMemory(device_data->device, data->upload_font_buffer_mem, NULL);

   ImGui::DestroyContext(data->imgui_context);
}

static void before_present(struct swapchain_data *swapchain_data,
                           unsigned imageIndex)
{
   snapshot_swapchain_frame(swapchain_data);

   compute_swapchain_display(swapchain_data);
   render_swapchain_display(swapchain_data, imageIndex);
}

VKAPI_ATTR VkResult VKAPI_CALL overlay_CreateSwapchainKHR(
    VkDevice                                    device,
    const VkSwapchainCreateInfoKHR*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSwapchainKHR*                             pSwapchain)
{
   struct device_data *device_data = FIND_DEVICE_DATA(device);
   VkResult result = device_data->vtable.CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
   if (result != VK_SUCCESS) return result;

   struct swapchain_data *swapchain_data = new_swapchain_data(*pSwapchain, device_data);
   setup_swapchain_data(swapchain_data, pCreateInfo);
   return result;
}

VKAPI_ATTR void VKAPI_CALL overlay_DestroySwapchainKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkAllocationCallbacks*                pAllocator)
{
   struct swapchain_data *swapchain_data = FIND_SWAPCHAIN_DATA(swapchain);

   shutdown_swapchain_data(swapchain_data);
   swapchain_data->device->vtable.DestroySwapchainKHR(device, swapchain, pAllocator);
   destroy_swapchain_data(swapchain_data);
}

VKAPI_ATTR VkResult VKAPI_CALL overlay_QueuePresentKHR(
    VkQueue                                     queue,
    const VkPresentInfoKHR*                     pPresentInfo)
{
   struct queue_data *queue_data = FIND_QUEUE_DATA(queue);
   struct device_data *device_data = queue_data->device;

   /* If we present on the graphic queue this layer is using to draw an
    * overlay, we don't need more than submitting the overlay draw prior to
    * present.
    */
   if (queue_data == device_data->graphic_queue) {
      for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
         struct swapchain_data *swapchain_data = FIND_SWAPCHAIN_DATA(pPresentInfo->pSwapchains[i]);
         before_present(swapchain_data, pPresentInfo->pImageIndices[i]);
      }
      return queue_data->device->vtable.QueuePresentKHR(queue, pPresentInfo);
   }

   /* Otherwise we need to do cross queue synchronization to tie the overlay
    * draw into the present queue.
    */
   VkPresentInfoKHR present_info = *pPresentInfo;
   VkSemaphore *semaphores =
      (VkSemaphore *)malloc(sizeof(VkSemaphore) * (pPresentInfo->waitSemaphoreCount + pPresentInfo->swapchainCount));
   for (uint32_t i = 0; i < pPresentInfo->waitSemaphoreCount; i++)
      semaphores[i] = pPresentInfo->pWaitSemaphores[i];
   for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
      struct swapchain_data *swapchain_data = FIND_SWAPCHAIN_DATA(pPresentInfo->pSwapchains[i]);
      before_present(swapchain_data, pPresentInfo->pImageIndices[i]);
      semaphores[pPresentInfo->waitSemaphoreCount + i] = swapchain_data->submission_semaphore;
   }
   present_info.pWaitSemaphores = semaphores;
   present_info.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount + pPresentInfo->swapchainCount;
   VkResult result = queue_data->device->vtable.QueuePresentKHR(queue, &present_info);
   free(semaphores);
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL overlay_AcquireNextImageKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    timeout,
    VkSemaphore                                 semaphore,
    VkFence                                     fence,
    uint32_t*                                   pImageIndex)
{
   struct swapchain_data *swapchain_data = FIND_SWAPCHAIN_DATA(swapchain);
   struct device_data *device_data = swapchain_data->device;

   uint64_t ts0 = os_time_get();
   VkResult result = device_data->vtable.AcquireNextImageKHR(device, swapchain, timeout,
                                                             semaphore, fence, pImageIndex);
   uint64_t ts1 = os_time_get();

   swapchain_data->acquire_times[swapchain_data->n_acquire %
                                 ARRAY_SIZE(swapchain_data->acquire_times)] =
      ((double)ts1 - (double)ts0) / 1000.0;
   swapchain_data->n_acquire++;

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL overlay_AcquireNextImage2KHR(
    VkDevice                                    device,
    const VkAcquireNextImageInfoKHR*            pAcquireInfo,
    uint32_t*                                   pImageIndex)
{
   struct swapchain_data *swapchain_data = FIND_SWAPCHAIN_DATA(pAcquireInfo->swapchain);
   struct device_data *device_data = swapchain_data->device;

   uint64_t ts0 = os_time_get();
   VkResult result = device_data->vtable.AcquireNextImage2KHR(device, pAcquireInfo, pImageIndex);
   uint64_t ts1 = os_time_get();

   swapchain_data->acquire_times[swapchain_data->n_acquire %
                                 ARRAY_SIZE(swapchain_data->acquire_times)] =
      ((double)ts1 - (double)ts0) / 1000.0;
   swapchain_data->n_acquire++;

   return result;
}

VKAPI_ATTR void VKAPI_CALL overlay_CmdDraw(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    vertexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstVertex,
    uint32_t                                    firstInstance)
{
   struct device_data *device_data = FIND_DEVICE_DATA(commandBuffer);
   device_data->vtable.CmdDraw(commandBuffer, vertexCount, instanceCount,
                               firstVertex, firstInstance);
   device_data->stats.stats[OVERLAY_PARAM_ENABLED_draw]++;
}

VKAPI_ATTR void VKAPI_CALL overlay_CmdDrawIndexed(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    indexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstIndex,
    int32_t                                     vertexOffset,
    uint32_t                                    firstInstance)
{
   struct device_data *device_data = FIND_DEVICE_DATA(commandBuffer);
   device_data->vtable.CmdDrawIndexed(commandBuffer, indexCount, instanceCount,
                                      firstIndex, vertexOffset, firstInstance);
   device_data->stats.stats[OVERLAY_PARAM_ENABLED_draw_indexed]++;
}

VKAPI_ATTR void VKAPI_CALL overlay_CmdDrawIndirect(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride)
{
   struct device_data *device_data = FIND_DEVICE_DATA(commandBuffer);
   device_data->vtable.CmdDrawIndirect(commandBuffer, buffer, offset, drawCount, stride);
   device_data->stats.stats[OVERLAY_PARAM_ENABLED_draw_indirect]++;
}

VKAPI_ATTR void VKAPI_CALL overlay_CmdDrawIndexedIndirect(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride)
{
   struct device_data *device_data = FIND_DEVICE_DATA(commandBuffer);
   device_data->vtable.CmdDrawIndexedIndirect(commandBuffer, buffer, offset, drawCount, stride);
   device_data->stats.stats[OVERLAY_PARAM_ENABLED_draw_indexed_indirect]++;
}

VKAPI_ATTR void VKAPI_CALL overlay_CmdDrawIndirectCountKHR(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride)
{
   struct device_data *device_data = FIND_DEVICE_DATA(commandBuffer);
   device_data->vtable.CmdDrawIndirectCountKHR(commandBuffer, buffer, offset,
                                               countBuffer, countBufferOffset,
                                               maxDrawCount, stride);
   device_data->stats.stats[OVERLAY_PARAM_ENABLED_draw_indirect_count]++;
}

VKAPI_ATTR void VKAPI_CALL overlay_CmdDrawIndexedIndirectCountKHR(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride)
{
   struct device_data *device_data = FIND_DEVICE_DATA(commandBuffer);
   device_data->vtable.CmdDrawIndexedIndirectCountKHR(commandBuffer, buffer, offset,
                                                      countBuffer, countBufferOffset,
                                                      maxDrawCount, stride);
   device_data->stats.stats[OVERLAY_PARAM_ENABLED_draw_indexed_indirect_count]++;
}

VKAPI_ATTR void VKAPI_CALL overlay_CmdDispatch(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ)
{
   struct device_data *device_data = FIND_DEVICE_DATA(commandBuffer);
   device_data->vtable.CmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
   device_data->stats.stats[OVERLAY_PARAM_ENABLED_dispatch]++;
}

VKAPI_ATTR void VKAPI_CALL overlay_CmdDispatchIndirect(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset)
{
   struct device_data *device_data = FIND_DEVICE_DATA(commandBuffer);
   device_data->vtable.CmdDispatchIndirect(commandBuffer, buffer, offset);
   device_data->stats.stats[OVERLAY_PARAM_ENABLED_dispatch_indirect]++;
}

VKAPI_ATTR void VKAPI_CALL overlay_CmdBindPipeline(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  pipeline)
{
   struct device_data *device_data = FIND_DEVICE_DATA(commandBuffer);
   device_data->vtable.CmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
   switch (pipelineBindPoint) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS: device_data->stats.stats[OVERLAY_PARAM_ENABLED_pipeline_graphics]++; break;
   case VK_PIPELINE_BIND_POINT_COMPUTE: device_data->stats.stats[OVERLAY_PARAM_ENABLED_pipeline_compute]++; break;
   case VK_PIPELINE_BIND_POINT_RAY_TRACING_NV: device_data->stats.stats[OVERLAY_PARAM_ENABLED_pipeline_raytracing]++; break;
   default: break;
   }
}

VKAPI_ATTR VkResult VKAPI_CALL overlay_AllocateCommandBuffers(VkDevice device,
                                                              const VkCommandBufferAllocateInfo* pAllocateInfo,
                                                              VkCommandBuffer* pCommandBuffers)
{
   struct device_data *device_data = FIND_DEVICE_DATA(device);

   VkResult result =
      device_data->vtable.AllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
   if (result != VK_SUCCESS) return result;

   for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++)
      map_object(pCommandBuffers[i], device_data);

   return result;
}

VKAPI_ATTR void VKAPI_CALL overlay_FreeCommandBuffers(VkDevice device,
                                                      VkCommandPool commandPool,
                                                      uint32_t commandBufferCount,
                                                      const VkCommandBuffer* pCommandBuffers)
{
   struct device_data *device_data = FIND_DEVICE_DATA(device);

   for (uint32_t i = 0; i < commandBufferCount; i++)
      unmap_object(pCommandBuffers[i]);

   device_data->vtable.FreeCommandBuffers(device, commandPool,
                                          commandBufferCount, pCommandBuffers);
}

VKAPI_ATTR VkResult VKAPI_CALL overlay_QueueSubmit(
    VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     fence)
{
   struct queue_data *queue_data = FIND_QUEUE_DATA(queue);
   struct device_data *device_data = queue_data->device;

   device_data->stats.stats[OVERLAY_PARAM_ENABLED_submit]++;

   return device_data->vtable.QueueSubmit(queue, submitCount, pSubmits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL overlay_CreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice)
{
   struct instance_data *instance_data = FIND_PHYSICAL_DEVICE_DATA(physicalDevice);
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

   VkResult result = fpCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
   if (result != VK_SUCCESS) return result;

   struct device_data *device_data = new_device_data(*pDevice, instance_data);
   device_data->physical_device = physicalDevice;
   vk_load_device_commands(*pDevice, fpGetDeviceProcAddr, &device_data->vtable);

   instance_data->vtable.GetPhysicalDeviceProperties(device_data->physical_device,
                                                     &device_data->properties);

   VkLayerDeviceCreateInfo *load_data_info =
      get_device_chain_info(pCreateInfo, VK_LOADER_DATA_CALLBACK);
   device_data->set_device_loader_data = load_data_info->u.pfnSetDeviceLoaderData;

   device_map_queues(device_data, pCreateInfo);

   return result;
}

VKAPI_ATTR void VKAPI_CALL overlay_DestroyDevice(
    VkDevice                                    device,
    const VkAllocationCallbacks*                pAllocator)
{
   struct device_data *device_data = FIND_DEVICE_DATA(device);
   device_unmap_queues(device_data);
   device_data->vtable.DestroyDevice(device, pAllocator);
   destroy_device_data(device_data);
}

VKAPI_ATTR VkResult VKAPI_CALL overlay_CreateInstance(
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
   vk_load_instance_commands(instance_data->instance,
                             fpGetInstanceProcAddr,
                             &instance_data->vtable);
   instance_data_map_physical_devices(instance_data, true);

   parse_overlay_env(&instance_data->params, getenv("VK_LAYER_MESA_OVERLAY_CONFIG"));

   return result;
}

VKAPI_ATTR void VKAPI_CALL overlay_DestroyInstance(
    VkInstance                                  instance,
    const VkAllocationCallbacks*                pAllocator)
{
   struct instance_data *instance_data = FIND_INSTANCE_DATA(instance);
   instance_data_map_physical_devices(instance_data, false);
   instance_data->vtable.DestroyInstance(instance, pAllocator);
   destroy_instance_data(instance_data);
}

static const struct {
   const char *name;
   void *ptr;
} name_to_funcptr_map[] = {
   { "vkGetDeviceProcAddr", (void *) vkGetDeviceProcAddr },
#define ADD_HOOK(fn) { "vk" # fn, (void *) overlay_ ## fn }
   ADD_HOOK(AllocateCommandBuffers),

   ADD_HOOK(CmdDraw),
   ADD_HOOK(CmdDrawIndexed),
   ADD_HOOK(CmdDrawIndexedIndirect),
   ADD_HOOK(CmdDispatch),
   ADD_HOOK(CmdDispatchIndirect),
   ADD_HOOK(CmdDrawIndirectCountKHR),
   ADD_HOOK(CmdDrawIndexedIndirectCountKHR),

   ADD_HOOK(CmdBindPipeline),

   ADD_HOOK(CreateSwapchainKHR),
   ADD_HOOK(QueuePresentKHR),
   ADD_HOOK(DestroySwapchainKHR),
   ADD_HOOK(AcquireNextImageKHR),
   ADD_HOOK(AcquireNextImage2KHR),

   ADD_HOOK(QueueSubmit),
   ADD_HOOK(CreateInstance),
   ADD_HOOK(DestroyInstance),
   ADD_HOOK(CreateDevice),
   ADD_HOOK(DestroyDevice),
#undef ADD_HOOK
};

static void *find_ptr(const char *name)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(name_to_funcptr_map); i++) {
      if (strcmp(name, name_to_funcptr_map[i].name) == 0)
         return name_to_funcptr_map[i].ptr;
   }

   return NULL;
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev,
                                                                             const char *funcName)
{
   void *ptr = find_ptr(funcName);
   if (ptr) return reinterpret_cast<PFN_vkVoidFunction>(ptr);

   if (dev == NULL) return NULL;

   struct device_data *device_data = FIND_DEVICE_DATA(dev);
   if (device_data->vtable.GetDeviceProcAddr == NULL) return NULL;
   return device_data->vtable.GetDeviceProcAddr(dev, funcName);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,
                                                                               const char *funcName)
{
   void *ptr = find_ptr(funcName);
   if (ptr) return reinterpret_cast<PFN_vkVoidFunction>(ptr);

   if (instance == NULL) return NULL;

   struct instance_data *instance_data = FIND_INSTANCE_DATA(instance);
   if (instance_data->vtable.GetInstanceProcAddr == NULL) return NULL;
   return instance_data->vtable.GetInstanceProcAddr(instance, funcName);
}
