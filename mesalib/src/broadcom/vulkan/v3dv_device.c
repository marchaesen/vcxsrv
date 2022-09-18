/*
 * Copyright © 2019 Raspberry Pi Ltd
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

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <xf86drm.h>

#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif

#include "v3dv_private.h"

#include "common/v3d_debug.h"

#include "compiler/v3d_compiler.h"

#include "drm-uapi/v3d_drm.h"
#include "format/u_format.h"
#include "vk_drm_syncobj.h"
#include "vk_util.h"
#include "git_sha1.h"

#include "util/build_id.h"
#include "util/debug.h"

#ifdef VK_USE_PLATFORM_XCB_KHR
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <X11/Xlib-xcb.h>
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
#include <wayland-client.h>
#include "wayland-drm-client-protocol.h"
#endif

#ifndef ANDROID
#   define V3DV_API_VERSION VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION)
#else
/* Android CDD require additional extensions for API v1.1+ */
#   define V3DV_API_VERSION VK_MAKE_VERSION(1, 0, VK_HEADER_VERSION)
#endif

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
    *pApiVersion = V3DV_API_VERSION;
    return VK_SUCCESS;
}

#if defined(VK_USE_PLATFORM_WIN32_KHR) ||   \
    defined(VK_USE_PLATFORM_WAYLAND_KHR) || \
    defined(VK_USE_PLATFORM_XCB_KHR) ||     \
    defined(VK_USE_PLATFORM_XLIB_KHR) ||    \
    defined(VK_USE_PLATFORM_DISPLAY_KHR)
#define V3DV_USE_WSI_PLATFORM
#endif

static const struct vk_instance_extension_table instance_extensions = {
   .KHR_device_group_creation           = true,
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .KHR_display                         = true,
   .KHR_get_display_properties2         = true,
#endif
   .KHR_external_fence_capabilities     = true,
   .KHR_external_memory_capabilities    = true,
   .KHR_external_semaphore_capabilities = true,
   .KHR_get_physical_device_properties2 = true,
#ifdef V3DV_USE_WSI_PLATFORM
   .KHR_get_surface_capabilities2       = true,
   .KHR_surface                         = true,
   .KHR_surface_protected_capabilities  = true,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .KHR_wayland_surface                 = true,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
   .KHR_xcb_surface                     = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
   .KHR_xlib_surface                    = true,
#endif
   .EXT_debug_report                    = true,
   .EXT_debug_utils                     = true,
};

static void
get_device_extensions(const struct v3dv_physical_device *device,
                      struct vk_device_extension_table *ext)
{
   *ext = (struct vk_device_extension_table) {
      .KHR_8bit_storage                     = true,
      .KHR_16bit_storage                    = true,
      .KHR_bind_memory2                     = true,
      .KHR_buffer_device_address            = true,
      .KHR_copy_commands2                   = true,
      .KHR_create_renderpass2               = true,
      .KHR_dedicated_allocation             = true,
      .KHR_device_group                     = true,
      .KHR_driver_properties                = true,
      .KHR_descriptor_update_template       = true,
      .KHR_depth_stencil_resolve            = true,
      .KHR_external_fence                   = true,
      .KHR_external_fence_fd                = true,
      .KHR_external_memory                  = true,
      .KHR_external_memory_fd               = true,
      .KHR_external_semaphore               = true,
      .KHR_external_semaphore_fd            = true,
      .KHR_format_feature_flags2            = true,
      .KHR_get_memory_requirements2         = true,
      .KHR_image_format_list                = true,
      .KHR_imageless_framebuffer            = true,
      .KHR_performance_query                = device->caps.perfmon,
      .KHR_relaxed_block_layout             = true,
      .KHR_maintenance1                     = true,
      .KHR_maintenance2                     = true,
      .KHR_maintenance3                     = true,
      .KHR_maintenance4                     = true,
      .KHR_multiview                        = true,
      .KHR_pipeline_executable_properties   = true,
      .KHR_separate_depth_stencil_layouts   = true,
      .KHR_shader_float_controls            = true,
      .KHR_shader_non_semantic_info         = true,
      .KHR_sampler_mirror_clamp_to_edge     = true,
      .KHR_spirv_1_4                        = true,
      .KHR_storage_buffer_storage_class     = true,
      .KHR_timeline_semaphore               = true,
      .KHR_uniform_buffer_standard_layout   = true,
      .KHR_synchronization2                 = true,
      .KHR_workgroup_memory_explicit_layout = true,
#ifdef V3DV_USE_WSI_PLATFORM
      .KHR_swapchain                        = true,
      .KHR_swapchain_mutable_format         = true,
      .KHR_incremental_present              = true,
#endif
      .KHR_variable_pointers                = true,
      .KHR_vulkan_memory_model              = true,
      .KHR_zero_initialize_workgroup_memory = true,
      .EXT_4444_formats                     = true,
      .EXT_attachment_feedback_loop_layout  = true,
      .EXT_border_color_swizzle             = true,
      .EXT_color_write_enable               = true,
      .EXT_custom_border_color              = true,
      .EXT_depth_clip_control               = true,
      .EXT_load_store_op_none               = true,
      .EXT_inline_uniform_block             = true,
      .EXT_external_memory_dma_buf          = true,
      .EXT_host_query_reset                 = true,
      .EXT_image_drm_format_modifier        = true,
      .EXT_index_type_uint8                 = true,
      .EXT_line_rasterization               = true,
      .EXT_memory_budget                    = true,
      .EXT_physical_device_drm              = true,
      .EXT_pipeline_creation_cache_control  = true,
      .EXT_pipeline_creation_feedback       = true,
      .EXT_primitive_topology_list_restart  = true,
      .EXT_private_data                     = true,
      .EXT_provoking_vertex                 = true,
      .EXT_separate_stencil_usage           = true,
      .EXT_shader_module_identifier         = true,
      .EXT_texel_buffer_alignment           = true,
      .EXT_tooling_info                     = true,
      .EXT_vertex_attribute_divisor         = true,
#ifdef ANDROID
      .ANDROID_native_buffer                = true,
#endif
   };
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_EnumerateInstanceExtensionProperties(const char *pLayerName,
                                          uint32_t *pPropertyCount,
                                          VkExtensionProperties *pProperties)
{
   /* We don't support any layers  */
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(
      &instance_extensions, pPropertyCount, pProperties);
}

static VkResult enumerate_devices(struct vk_instance *vk_instance);

static void destroy_physical_device(struct vk_physical_device *device);

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkInstance *pInstance)
{
   struct v3dv_instance *instance;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   if (pAllocator == NULL)
      pAllocator = vk_default_allocator();

   instance = vk_alloc(pAllocator, sizeof(*instance), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_instance_dispatch_table dispatch_table;
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &v3dv_instance_entrypoints, true);
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_instance_entrypoints, false);

   result = vk_instance_init(&instance->vk,
                             &instance_extensions,
                             &dispatch_table,
                             pCreateInfo, pAllocator);

   if (result != VK_SUCCESS) {
      vk_free(pAllocator, instance);
      return vk_error(NULL, result);
   }

   v3d_process_debug_variable();

   instance->vk.physical_devices.enumerate = enumerate_devices;
   instance->vk.physical_devices.destroy = destroy_physical_device;

   /* We start with the default values for the pipeline_cache envvars */
   instance->pipeline_cache_enabled = true;
   instance->default_pipeline_cache_enabled = true;
   const char *pipeline_cache_str = getenv("V3DV_ENABLE_PIPELINE_CACHE");
   if (pipeline_cache_str != NULL) {
      if (strncmp(pipeline_cache_str, "full", 4) == 0) {
         /* nothing to do, just to filter correct values */
      } else if (strncmp(pipeline_cache_str, "no-default-cache", 16) == 0) {
         instance->default_pipeline_cache_enabled = false;
      } else if (strncmp(pipeline_cache_str, "off", 3) == 0) {
         instance->pipeline_cache_enabled = false;
         instance->default_pipeline_cache_enabled = false;
      } else {
         fprintf(stderr, "Wrong value for envvar V3DV_ENABLE_PIPELINE_CACHE. "
                 "Allowed values are: full, no-default-cache, off\n");
      }
   }

   if (instance->pipeline_cache_enabled == false) {
      fprintf(stderr, "WARNING: v3dv pipeline cache is disabled. Performance "
              "can be affected negatively\n");
   } else {
      if (instance->default_pipeline_cache_enabled == false) {
        fprintf(stderr, "WARNING: default v3dv pipeline cache is disabled. "
                "Performance can be affected negatively\n");
      }
   }

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   *pInstance = v3dv_instance_to_handle(instance);

   return VK_SUCCESS;
}

static void
v3dv_physical_device_free_disk_cache(struct v3dv_physical_device *device)
{
#ifdef ENABLE_SHADER_CACHE
   if (device->disk_cache)
      disk_cache_destroy(device->disk_cache);
#else
   assert(device->disk_cache == NULL);
#endif
}

static void
physical_device_finish(struct v3dv_physical_device *device)
{
   v3dv_wsi_finish(device);
   v3dv_physical_device_free_disk_cache(device);
   v3d_compiler_free(device->compiler);

   util_sparse_array_finish(&device->bo_map);

   close(device->render_fd);
   if (device->display_fd >= 0)
      close(device->display_fd);
   if (device->master_fd >= 0)
      close(device->master_fd);

   free(device->name);

#if using_v3d_simulator
   v3d_simulator_destroy(device->sim_file);
#endif

   vk_physical_device_finish(&device->vk);
   mtx_destroy(&device->mutex);
}

static void
destroy_physical_device(struct vk_physical_device *device)
{
   physical_device_finish((struct v3dv_physical_device *)device);
   vk_free(&device->instance->alloc, device);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroyInstance(VkInstance _instance,
                     const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_instance, instance, _instance);

   if (!instance)
      return;

   VG(VALGRIND_DESTROY_MEMPOOL(instance));

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

static uint64_t
compute_heap_size()
{
#if !using_v3d_simulator
   /* Query the total ram from the system */
   struct sysinfo info;
   sysinfo(&info);

   uint64_t total_ram = (uint64_t)info.totalram * (uint64_t)info.mem_unit;
#else
   uint64_t total_ram = (uint64_t) v3d_simulator_get_mem_size();
#endif

   /* We don't want to burn too much ram with the GPU.  If the user has 4GB
    * or less, we use at most half.  If they have more than 4GB we limit it
    * to 3/4 with a max. of 4GB since the GPU cannot address more than that.
    */
   const uint64_t MAX_HEAP_SIZE = 4ull * 1024ull * 1024ull * 1024ull;
   uint64_t available;
   if (total_ram <= MAX_HEAP_SIZE)
      available = total_ram / 2;
   else
      available = MIN2(MAX_HEAP_SIZE, total_ram * 3 / 4);

   return available;
}

static uint64_t
compute_memory_budget(struct v3dv_physical_device *device)
{
   uint64_t heap_size = device->memory.memoryHeaps[0].size;
   uint64_t heap_used = device->heap_used;
   uint64_t sys_available;
#if !using_v3d_simulator
   ASSERTED bool has_available_memory =
      os_get_available_system_memory(&sys_available);
   assert(has_available_memory);
#else
   sys_available = (uint64_t) v3d_simulator_get_mem_free();
#endif

   /* Let's not incite the app to starve the system: report at most 90% of
    * available system memory.
    */
   uint64_t heap_available = sys_available * 9 / 10;
   return MIN2(heap_size, heap_used + heap_available);
}

#if !using_v3d_simulator
#ifdef VK_USE_PLATFORM_XCB_KHR
static int
create_display_fd_xcb(VkIcdSurfaceBase *surface)
{
   int fd = -1;

   xcb_connection_t *conn;
   xcb_dri3_open_reply_t *reply = NULL;
   if (surface) {
      if (surface->platform == VK_ICD_WSI_PLATFORM_XLIB)
         conn = XGetXCBConnection(((VkIcdSurfaceXlib *)surface)->dpy);
      else
         conn = ((VkIcdSurfaceXcb *)surface)->connection;
   } else {
      conn = xcb_connect(NULL, NULL);
   }

   if (xcb_connection_has_error(conn))
      goto finish;

   const xcb_setup_t *setup = xcb_get_setup(conn);
   xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
   xcb_screen_t *screen = iter.data;

   xcb_dri3_open_cookie_t cookie;
   cookie = xcb_dri3_open(conn, screen->root, None);
   reply = xcb_dri3_open_reply(conn, cookie, NULL);
   if (!reply)
      goto finish;

   if (reply->nfd != 1)
      goto finish;

   fd = xcb_dri3_open_reply_fds(conn, reply)[0];
   fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);

finish:
   if (!surface)
      xcb_disconnect(conn);
   if (reply)
      free(reply);

   return fd;
}
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
struct v3dv_wayland_info {
   struct wl_drm *wl_drm;
   int fd;
   bool is_set;
   bool authenticated;
};

static void
v3dv_drm_handle_device(void *data, struct wl_drm *drm, const char *device)
{
   struct v3dv_wayland_info *info = data;
   info->fd = open(device, O_RDWR | O_CLOEXEC);
   info->is_set = info->fd != -1;
   if (!info->is_set) {
      fprintf(stderr, "v3dv_drm_handle_device: could not open %s (%s)\n",
              device, strerror(errno));
      return;
   }

   drm_magic_t magic;
   if (drmGetMagic(info->fd, &magic)) {
      fprintf(stderr, "v3dv_drm_handle_device: drmGetMagic failed\n");
      close(info->fd);
      info->fd = -1;
      info->is_set = false;
      return;
   }
   wl_drm_authenticate(info->wl_drm, magic);
}

static void
v3dv_drm_handle_format(void *data, struct wl_drm *drm, uint32_t format)
{
}

static void
v3dv_drm_handle_authenticated(void *data, struct wl_drm *drm)
{
   struct v3dv_wayland_info *info = data;
   info->authenticated = true;
}

static void
v3dv_drm_handle_capabilities(void *data, struct wl_drm *drm, uint32_t value)
{
}

struct wl_drm_listener v3dv_drm_listener = {
   .device = v3dv_drm_handle_device,
   .format = v3dv_drm_handle_format,
   .authenticated = v3dv_drm_handle_authenticated,
   .capabilities = v3dv_drm_handle_capabilities
};

static void
v3dv_registry_global(void *data,
                     struct wl_registry *registry,
                     uint32_t name,
                     const char *interface,
                     uint32_t version)
{
   struct v3dv_wayland_info *info = data;
   if (strcmp(interface, wl_drm_interface.name) == 0) {
      info->wl_drm = wl_registry_bind(registry, name, &wl_drm_interface,
                                      MIN2(version, 2));
      wl_drm_add_listener(info->wl_drm, &v3dv_drm_listener, data);
   };
}

static void
v3dv_registry_global_remove_cb(void *data,
                               struct wl_registry *registry,
                               uint32_t name)
{
}

static int
create_display_fd_wayland(VkIcdSurfaceBase *surface)
{
   struct wl_display *display;
   struct wl_registry *registry = NULL;

   struct v3dv_wayland_info info = {
      .wl_drm = NULL,
      .fd = -1,
      .is_set = false,
      .authenticated = false
   };

   if (surface)
      display = ((VkIcdSurfaceWayland *) surface)->display;
   else
      display = wl_display_connect(NULL);

   if (!display)
      return -1;

   registry = wl_display_get_registry(display);
   if (!registry) {
      if (!surface)
         wl_display_disconnect(display);
      return -1;
   }

   static const struct wl_registry_listener registry_listener = {
      v3dv_registry_global,
      v3dv_registry_global_remove_cb
   };
   wl_registry_add_listener(registry, &registry_listener, &info);

   wl_display_roundtrip(display); /* For the registry advertisement */
   wl_display_roundtrip(display); /* For the DRM device event */
   wl_display_roundtrip(display); /* For the authentication event */

   wl_drm_destroy(info.wl_drm);
   wl_registry_destroy(registry);

   if (!surface)
      wl_display_disconnect(display);

   if (!info.is_set)
      return -1;

   if (!info.authenticated)
      return -1;

   return info.fd;
}
#endif

/* Acquire an authenticated display fd without a surface reference. This is the
 * case where the application is making WSI allocations outside the Vulkan
 * swapchain context (only Zink, for now). Since we lack information about the
 * underlying surface we just try our best to figure out the correct display
 * and platform to use. It should work in most cases.
 */
static void
acquire_display_device_no_surface(struct v3dv_instance *instance,
                                  struct v3dv_physical_device *pdevice)
{
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   pdevice->display_fd = create_display_fd_wayland(NULL);
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
   if (pdevice->display_fd == -1)
      pdevice->display_fd = create_display_fd_xcb(NULL);
#endif

#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   if (pdevice->display_fd == - 1 && pdevice->master_fd >= 0)
      pdevice->display_fd = dup(pdevice->master_fd);
#endif
}

/* Acquire an authenticated display fd from the surface. This is the regular
 * case where the application is using swapchains to create WSI allocations.
 * In this case we use the surface information to figure out the correct
 * display and platform combination.
 */
static void
acquire_display_device_surface(struct v3dv_instance *instance,
                               struct v3dv_physical_device *pdevice,
                               VkIcdSurfaceBase *surface)
{
   /* Mesa will set both of VK_USE_PLATFORM_{XCB,XLIB} when building with
    * platform X11, so only check for XCB and rely on XCB to get an
    * authenticated device also for Xlib.
    */
#ifdef VK_USE_PLATFORM_XCB_KHR
   if (surface->platform == VK_ICD_WSI_PLATFORM_XCB ||
       surface->platform == VK_ICD_WSI_PLATFORM_XLIB) {
      pdevice->display_fd = create_display_fd_xcb(surface);
   }
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   if (surface->platform == VK_ICD_WSI_PLATFORM_WAYLAND)
      pdevice->display_fd = create_display_fd_wayland(surface);
#endif

#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   if (surface->platform == VK_ICD_WSI_PLATFORM_DISPLAY &&
       pdevice->master_fd >= 0) {
      pdevice->display_fd = dup(pdevice->master_fd);
   }
#endif
}
#endif /* !using_v3d_simulator */

/* Attempts to get an authenticated display fd from the display server that
 * we can use to allocate BOs for presentable images.
 */
VkResult
v3dv_physical_device_acquire_display(struct v3dv_instance *instance,
                                     struct v3dv_physical_device *pdevice,
                                     VkIcdSurfaceBase *surface)
{
   VkResult result = VK_SUCCESS;
   mtx_lock(&pdevice->mutex);

   if (pdevice->display_fd != -1)
      goto done;

   /* When running on the simulator we do everything on a single render node so
    * we don't need to get an authenticated display fd from the display server.
    */
#if !using_v3d_simulator
   if (surface)
      acquire_display_device_surface(instance, pdevice, surface);
   else
      acquire_display_device_no_surface(instance, pdevice);

   if (pdevice->display_fd == -1)
      result = VK_ERROR_INITIALIZATION_FAILED;
#endif

done:
   mtx_unlock(&pdevice->mutex);
   return result;
}

static bool
v3d_has_feature(struct v3dv_physical_device *device, enum drm_v3d_param feature)
{
   struct drm_v3d_get_param p = {
      .param = feature,
   };
   if (v3dv_ioctl(device->render_fd, DRM_IOCTL_V3D_GET_PARAM, &p) != 0)
      return false;
   return p.value;
}

static bool
device_has_expected_features(struct v3dv_physical_device *device)
{
   return v3d_has_feature(device, DRM_V3D_PARAM_SUPPORTS_TFU) &&
          v3d_has_feature(device, DRM_V3D_PARAM_SUPPORTS_CSD) &&
          v3d_has_feature(device, DRM_V3D_PARAM_SUPPORTS_CACHE_FLUSH);
}


static VkResult
init_uuids(struct v3dv_physical_device *device)
{
   const struct build_id_note *note =
      build_id_find_nhdr_for_addr(init_uuids);
   if (!note) {
      return vk_errorf(device->vk.instance,
                       VK_ERROR_INITIALIZATION_FAILED,
                       "Failed to find build-id");
   }

   unsigned build_id_len = build_id_length(note);
   if (build_id_len < 20) {
      return vk_errorf(device->vk.instance,
                       VK_ERROR_INITIALIZATION_FAILED,
                       "build-id too short.  It needs to be a SHA");
   }

   memcpy(device->driver_build_sha1, build_id_data(note), 20);

   uint32_t vendor_id = v3dv_physical_device_vendor_id(device);
   uint32_t device_id = v3dv_physical_device_device_id(device);

   struct mesa_sha1 sha1_ctx;
   uint8_t sha1[20];
   STATIC_ASSERT(VK_UUID_SIZE <= sizeof(sha1));

   /* The pipeline cache UUID is used for determining when a pipeline cache is
    * invalid.  It needs both a driver build and the PCI ID of the device.
    */
   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, build_id_data(note), build_id_len);
   _mesa_sha1_update(&sha1_ctx, &device_id, sizeof(device_id));
   _mesa_sha1_final(&sha1_ctx, sha1);
   memcpy(device->pipeline_cache_uuid, sha1, VK_UUID_SIZE);

   /* The driver UUID is used for determining sharability of images and memory
    * between two Vulkan instances in separate processes.  People who want to
    * share memory need to also check the device UUID (below) so all this
    * needs to be is the build-id.
    */
   memcpy(device->driver_uuid, build_id_data(note), VK_UUID_SIZE);

   /* The device UUID uniquely identifies the given device within the machine.
    * Since we never have more than one device, this doesn't need to be a real
    * UUID.
    */
   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, &vendor_id, sizeof(vendor_id));
   _mesa_sha1_update(&sha1_ctx, &device_id, sizeof(device_id));
   _mesa_sha1_final(&sha1_ctx, sha1);
   memcpy(device->device_uuid, sha1, VK_UUID_SIZE);

   return VK_SUCCESS;
}

static void
v3dv_physical_device_init_disk_cache(struct v3dv_physical_device *device)
{
#ifdef ENABLE_SHADER_CACHE
   char timestamp[41];
   _mesa_sha1_format(timestamp, device->driver_build_sha1);

   assert(device->name);
   device->disk_cache = disk_cache_create(device->name, timestamp, 0);
#else
   device->disk_cache = NULL;
#endif
}

static VkResult
create_physical_device(struct v3dv_instance *instance,
                       drmDevicePtr drm_render_device,
                       drmDevicePtr drm_primary_device)
{
   VkResult result = VK_SUCCESS;
   int32_t master_fd = -1;
   int32_t render_fd = -1;

   struct v3dv_physical_device *device =
      vk_zalloc(&instance->vk.alloc, sizeof(*device), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

   if (!device)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints
      (&dispatch_table, &v3dv_physical_device_entrypoints, true);
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_physical_device_entrypoints, false);

   result = vk_physical_device_init(&device->vk, &instance->vk, NULL,
                                    &dispatch_table);

   if (result != VK_SUCCESS)
      goto fail;

   assert(drm_render_device);
   const char *path = drm_render_device->nodes[DRM_NODE_RENDER];
   render_fd = open(path, O_RDWR | O_CLOEXEC);
   if (render_fd < 0) {
      fprintf(stderr, "Opening %s failed: %s\n", path, strerror(errno));
      result = VK_ERROR_INITIALIZATION_FAILED;
      goto fail;
   }

   /* If we are running on VK_KHR_display we need to acquire the master
    * display device now for the v3dv_wsi_init() call below. For anything else
    * we postpone that until a swapchain is created.
    */

   const char *primary_path;
#if !using_v3d_simulator
   if (drm_primary_device)
      primary_path = drm_primary_device->nodes[DRM_NODE_PRIMARY];
   else
      primary_path = NULL;
#else
   primary_path = drm_render_device->nodes[DRM_NODE_PRIMARY];
#endif

   struct stat primary_stat = {0}, render_stat = {0};

   device->has_primary = primary_path;
   if (device->has_primary) {
      if (stat(primary_path, &primary_stat) != 0) {
         result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                            "failed to stat DRM primary node %s",
                            primary_path);
         goto fail;
      }

      device->primary_devid = primary_stat.st_rdev;
   }

   if (fstat(render_fd, &render_stat) != 0) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "failed to stat DRM render node %s",
                         path);
      goto fail;
   }
   device->has_render = true;
   device->render_devid = render_stat.st_rdev;

#if using_v3d_simulator
   device->device_id = drm_render_device->deviceinfo.pci->device_id;
#endif

   if (instance->vk.enabled_extensions.KHR_display) {
#if !using_v3d_simulator
      /* Open the primary node on the vc4 display device */
      assert(drm_primary_device);
      master_fd = open(primary_path, O_RDWR | O_CLOEXEC);
#else
      /* There is only one device with primary and render nodes.
       * Open its primary node.
       */
      master_fd = open(primary_path, O_RDWR | O_CLOEXEC);
#endif
   }

#if using_v3d_simulator
   device->sim_file = v3d_simulator_init(render_fd);
#endif

   device->render_fd = render_fd;    /* The v3d render node  */
   device->display_fd = -1;          /* Authenticated vc4 primary node */
   device->master_fd = master_fd;    /* Master vc4 primary node */

   if (!v3d_get_device_info(device->render_fd, &device->devinfo, &v3dv_ioctl)) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to get info from device.");
      goto fail;
   }

   if (device->devinfo.ver < 42) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "Device version < 42.");
      goto fail;
   }

   if (!device_has_expected_features(device)) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "Kernel driver doesn't have required features.");
      goto fail;
   }

   device->caps.multisync =
      v3d_has_feature(device, DRM_V3D_PARAM_SUPPORTS_MULTISYNC_EXT);

   device->caps.perfmon =
      v3d_has_feature(device, DRM_V3D_PARAM_SUPPORTS_PERFMON);

   result = init_uuids(device);
   if (result != VK_SUCCESS)
      goto fail;

   device->compiler = v3d_compiler_init(&device->devinfo,
                                        MAX_INLINE_UNIFORM_BUFFERS);
   device->next_program_id = 0;

   ASSERTED int len =
      asprintf(&device->name, "V3D %d.%d",
               device->devinfo.ver / 10, device->devinfo.ver % 10);
   assert(len != -1);

   v3dv_physical_device_init_disk_cache(device);

   /* Setup available memory heaps and types */
   VkPhysicalDeviceMemoryProperties *mem = &device->memory;
   mem->memoryHeapCount = 1;
   mem->memoryHeaps[0].size = compute_heap_size();
   mem->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

   /* This is the only combination required by the spec */
   mem->memoryTypeCount = 1;
   mem->memoryTypes[0].propertyFlags =
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   mem->memoryTypes[0].heapIndex = 0;

   /* Initialize sparse array for refcounting imported BOs */
   util_sparse_array_init(&device->bo_map, sizeof(struct v3dv_bo), 512);

   device->options.merge_jobs = !V3D_DBG(NO_MERGE_JOBS);

   device->drm_syncobj_type = vk_drm_syncobj_get_type(device->render_fd);

   /* We don't support timelines in the uAPI yet and we don't want it getting
    * suddenly turned on by vk_drm_syncobj_get_type() without us adding v3dv
    * code for it first.
    */
   device->drm_syncobj_type.features &= ~VK_SYNC_FEATURE_TIMELINE;

#ifndef ANDROID
   /* Sync file export is incompatible with the current model of execution
    * where some jobs may run on the CPU.  There are CTS tests which do the
    * following:
    *
    *  1. Create a command buffer with a vkCmdWaitEvents()
    *  2. Submit the command buffer
    *  3. vkGetSemaphoreFdKHR() to try to get a sync_file
    *  4. vkSetEvent()
    *
    * This deadlocks because we have to wait for the syncobj to get a real
    * fence in vkGetSemaphoreFdKHR() which only happens after all the work
    * from the command buffer is complete which only happens after
    * vkSetEvent().  No amount of CPU threading in userspace will ever fix
    * this.  Sadly, this is pretty explicitly allowed by the Vulkan spec:
    *
    *    VUID-vkCmdWaitEvents-pEvents-01163
    *
    *    "If pEvents includes one or more events that will be signaled by
    *    vkSetEvent after commandBuffer has been submitted to a queue, then
    *    vkCmdWaitEvents must not be called inside a render pass instance"
    *
    * Disable sync file support for now.
    */
   device->drm_syncobj_type.import_sync_file = NULL;
   device->drm_syncobj_type.export_sync_file = NULL;
#endif

   /* Multiwait is required for emulated timeline semaphores and is supported
    * by the v3d kernel interface.
    */
   device->drm_syncobj_type.features |= VK_SYNC_FEATURE_GPU_MULTI_WAIT;

   device->sync_timeline_type =
      vk_sync_timeline_get_type(&device->drm_syncobj_type);

   device->sync_types[0] = &device->drm_syncobj_type;
   device->sync_types[1] = &device->sync_timeline_type.sync;
   device->sync_types[2] = NULL;
   device->vk.supported_sync_types = device->sync_types;

   result = v3dv_wsi_init(device);
   if (result != VK_SUCCESS) {
      vk_error(instance, result);
      result = VK_ERROR_INITIALIZATION_FAILED;
      goto fail;
   }

   get_device_extensions(device, &device->vk.supported_extensions);

   mtx_init(&device->mutex, mtx_plain);

   list_addtail(&device->vk.link, &instance->vk.physical_devices.list);

   return VK_SUCCESS;

fail:
   vk_physical_device_finish(&device->vk);
   vk_free(&instance->vk.alloc, device);

   if (render_fd >= 0)
      close(render_fd);
   if (master_fd >= 0)
      close(master_fd);

   return result;
}

/* This driver hook is expected to return VK_SUCCESS (unless a memory
 * allocation error happened) if no compatible device is found. If a
 * compatible device is found, it may return an error code if device
 * inialization failed.
 */
static VkResult
enumerate_devices(struct vk_instance *vk_instance)
{
   struct v3dv_instance *instance =
      container_of(vk_instance, struct v3dv_instance, vk);

   /* TODO: Check for more devices? */
   drmDevicePtr devices[8];
   int max_devices;

   max_devices = drmGetDevices2(0, devices, ARRAY_SIZE(devices));
   if (max_devices < 1)
      return VK_SUCCESS;

   VkResult result = VK_SUCCESS;

#if !using_v3d_simulator
   int32_t v3d_idx = -1;
   int32_t vc4_idx = -1;
#endif
   for (unsigned i = 0; i < (unsigned)max_devices; i++) {
#if using_v3d_simulator
      /* In the simulator, we look for an Intel/AMD render node */
      const int required_nodes = (1 << DRM_NODE_RENDER) | (1 << DRM_NODE_PRIMARY);
      if ((devices[i]->available_nodes & required_nodes) == required_nodes &&
           devices[i]->bustype == DRM_BUS_PCI &&
          (devices[i]->deviceinfo.pci->vendor_id == 0x8086 ||
           devices[i]->deviceinfo.pci->vendor_id == 0x1002)) {
         result = create_physical_device(instance, devices[i], NULL);
         if (result == VK_SUCCESS)
            break;
      }
#else
      /* On actual hardware, we should have a render node (v3d)
       * and a primary node (vc4). We will need to use the primary
       * to allocate WSI buffers and share them with the render node
       * via prime, but that is a privileged operation so we need the
       * primary node to be authenticated, and for that we need the
       * display server to provide the device fd (with DRI3), so we
       * here we only check that the device is present but we don't
       * try to open it.
       */
      if (devices[i]->bustype != DRM_BUS_PLATFORM)
         continue;

      if (devices[i]->available_nodes & 1 << DRM_NODE_RENDER) {
         char **compat = devices[i]->deviceinfo.platform->compatible;
         while (*compat) {
            if (strncmp(*compat, "brcm,2711-v3d", 13) == 0) {
               v3d_idx = i;
               break;
            }
            compat++;
         }
      } else if (devices[i]->available_nodes & 1 << DRM_NODE_PRIMARY) {
         char **compat = devices[i]->deviceinfo.platform->compatible;
         while (*compat) {
            if (strncmp(*compat, "brcm,bcm2711-vc5", 16) == 0 ||
                strncmp(*compat, "brcm,bcm2835-vc4", 16) == 0 ) {
               vc4_idx = i;
               break;
            }
            compat++;
         }
      }
#endif
   }

#if !using_v3d_simulator
   if (v3d_idx != -1 && vc4_idx != -1) {
      result =
         create_physical_device(instance, devices[v3d_idx], devices[vc4_idx]);
   }
#endif

   drmFreeDevices(devices, max_devices);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                               VkPhysicalDeviceFeatures *pFeatures)
{
   memset(pFeatures, 0, sizeof(*pFeatures));

   *pFeatures = (VkPhysicalDeviceFeatures) {
      .robustBufferAccess = true, /* This feature is mandatory */
      .fullDrawIndexUint32 = false, /* Only available since V3D 4.4.9.1 */
      .imageCubeArray = true,
      .independentBlend = true,
      .geometryShader = true,
      .tessellationShader = false,
      .sampleRateShading = true,
      .dualSrcBlend = false,
      .logicOp = true,
      .multiDrawIndirect = false,
      .drawIndirectFirstInstance = true,
      .depthClamp = false, /* Only available since V3D 4.5.1.1 */
      .depthBiasClamp = true,
      .fillModeNonSolid = true,
      .depthBounds = false, /* Only available since V3D 4.3.16.2 */
      .wideLines = true,
      .largePoints = true,
      .alphaToOne = true,
      .multiViewport = false,
      .samplerAnisotropy = true,
      .textureCompressionETC2 = true,
      .textureCompressionASTC_LDR = true,
      /* Note that textureCompressionBC requires that the driver support all
       * the BC formats. V3D 4.2 only support the BC1-3, so we can't claim
       * that we support it.
       */
      .textureCompressionBC = false,
      .occlusionQueryPrecise = true,
      .pipelineStatisticsQuery = false,
      .vertexPipelineStoresAndAtomics = true,
      .fragmentStoresAndAtomics = true,
      .shaderTessellationAndGeometryPointSize = true,
      .shaderImageGatherExtended = false,
      .shaderStorageImageExtendedFormats = true,
      .shaderStorageImageMultisample = false,
      .shaderStorageImageReadWithoutFormat = false,
      .shaderStorageImageWriteWithoutFormat = false,
      .shaderUniformBufferArrayDynamicIndexing = false,
      .shaderSampledImageArrayDynamicIndexing = false,
      .shaderStorageBufferArrayDynamicIndexing = false,
      .shaderStorageImageArrayDynamicIndexing = false,
      .shaderClipDistance = true,
      .shaderCullDistance = false,
      .shaderFloat64 = false,
      .shaderInt64 = false,
      .shaderInt16 = false,
      .shaderResourceResidency = false,
      .shaderResourceMinLod = false,
      .sparseBinding = false,
      .sparseResidencyBuffer = false,
      .sparseResidencyImage2D = false,
      .sparseResidencyImage3D = false,
      .sparseResidency2Samples = false,
      .sparseResidency4Samples = false,
      .sparseResidency8Samples = false,
      .sparseResidency16Samples = false,
      .sparseResidencyAliased = false,
      .variableMultisampleRate = false,
      .inheritedQueries = true,
   };
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                VkPhysicalDeviceFeatures2 *pFeatures)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, physical_device, physicalDevice);
   v3dv_GetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);

   VkPhysicalDeviceVulkan13Features vk13 = {
      .inlineUniformBlock  = true,
      /* Inline buffers work like push constants, so after their are bound
       * some of their contents may be copied into the uniform stream as soon
       * as the next draw/dispatch is recorded in the command buffer. This means
       * that if the client updates the buffer contents after binding it to
       * a command buffer, the next queue submit of that command buffer may
       * not use the latest update to the buffer contents, but the data that
       * was present in the buffer at the time it was bound to the command
       * buffer.
       */
      .descriptorBindingInlineUniformBlockUpdateAfterBind = false,
      .pipelineCreationCacheControl = true,
      .privateData = true,
      .maintenance4 = true,
      .shaderZeroInitializeWorkgroupMemory = true,
      .synchronization2 = true,
   };

   VkPhysicalDeviceVulkan12Features vk12 = {
      .hostQueryReset = true,
      .uniformAndStorageBuffer8BitAccess = true,
      .uniformBufferStandardLayout = true,
      /* V3D 4.2 wraps TMU vector accesses to 16-byte boundaries, so loads and
       * stores of vectors that cross these boundaries would not work correcly
       * with scalarBlockLayout and would need to be split into smaller vectors
       * (and/or scalars) that don't cross these boundaries. For load/stores
       * with dynamic offsets where we can't identify if the offset is
       * problematic, we would always have to scalarize. Overall, this would
       * not lead to best performance so let's just not support it.
       */
      .scalarBlockLayout = false,
      /* This tells applications 2 things:
       *
       * 1. If they can select just one aspect for barriers. For us barriers
       *    decide if we need to split a job and we don't care if it is only
       *    for one of the aspects of the image or both, so we don't really
       *    benefit from seeing barriers that select just one aspect.
       *
       * 2. If they can program different layouts for each aspect. We
       *    generally don't care about layouts, so again, we don't get any
       *    benefits from this to limit the scope of image layout transitions.
       *
       * Still, Vulkan 1.2 requires this feature to be supported so we
       * advertise it even though we don't really take advantage of it.
       */
      .separateDepthStencilLayouts = true,
      .storageBuffer8BitAccess = true,
      .storagePushConstant8 = true,
      .imagelessFramebuffer = true,
      .timelineSemaphore = true,

      .samplerMirrorClampToEdge = true,

      /* These are mandatory by Vulkan 1.2, however, we don't support any of
       * the optional features affected by them (non 32-bit types for
       * shaderSubgroupExtendedTypes and additional subgroup ballot for
       * subgroupBroadcastDynamicId), so in practice setting them to true
       * doesn't have any implications for us until we implement any of these
       * optional features.
       */
      .shaderSubgroupExtendedTypes = true,
      .subgroupBroadcastDynamicId = true,

      .vulkanMemoryModel = true,
      .vulkanMemoryModelDeviceScope = true,
      .vulkanMemoryModelAvailabilityVisibilityChains = true,

      .bufferDeviceAddress = true,
      .bufferDeviceAddressCaptureReplay = false,
      .bufferDeviceAddressMultiDevice = false,
   };

   VkPhysicalDeviceVulkan11Features vk11 = {
      .storageBuffer16BitAccess = true,
      .uniformAndStorageBuffer16BitAccess = true,
      .storagePushConstant16 = true,
      .storageInputOutput16 = false,
      .multiview = true,
      .multiviewGeometryShader = false,
      .multiviewTessellationShader = false,
      .variablePointersStorageBuffer = true,
      /* FIXME: this needs support for non-constant index on UBO/SSBO */
      .variablePointers = false,
      .protectedMemory = false,
      .samplerYcbcrConversion = false,
      .shaderDrawParameters = false,
   };

   vk_foreach_struct(ext, pFeatures->pNext) {
      if (vk_get_physical_device_core_1_1_feature_ext(ext, &vk11))
         continue;
      if (vk_get_physical_device_core_1_2_feature_ext(ext, &vk12))
         continue;
      if (vk_get_physical_device_core_1_3_feature_ext(ext, &vk13))
         continue;

      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT: {
         VkPhysicalDevice4444FormatsFeaturesEXT *features =
            (VkPhysicalDevice4444FormatsFeaturesEXT *)ext;
         features->formatA4R4G4B4 = true;
         features->formatA4B4G4R4 = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT: {
         VkPhysicalDeviceCustomBorderColorFeaturesEXT *features =
            (VkPhysicalDeviceCustomBorderColorFeaturesEXT *)ext;
         features->customBorderColors = true;
         features->customBorderColorWithoutFormat = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT: {
         VkPhysicalDeviceIndexTypeUint8FeaturesEXT *features =
            (VkPhysicalDeviceIndexTypeUint8FeaturesEXT *)ext;
         features->indexTypeUint8 = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT: {
         VkPhysicalDeviceLineRasterizationFeaturesEXT *features =
            (VkPhysicalDeviceLineRasterizationFeaturesEXT *)ext;
         features->rectangularLines = true;
         features->bresenhamLines = true;
         features->smoothLines = false;
         features->stippledRectangularLines = false;
         features->stippledBresenhamLines = false;
         features->stippledSmoothLines = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
          VkPhysicalDeviceColorWriteEnableFeaturesEXT *features = (void *) ext;
          features->colorWriteEnable = true;
          break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR: {
         VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *features =
            (VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *) ext;
         features->pipelineExecutableInfo = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT: {
         VkPhysicalDeviceProvokingVertexFeaturesEXT *features = (void *) ext;
         features->provokingVertexLast = true;
         /* FIXME: update when supporting EXT_transform_feedback */
         features->transformFeedbackPreservesProvokingVertex = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT: {
         VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *features =
            (void *) ext;
         features->vertexAttributeInstanceRateDivisor = true;
         features->vertexAttributeInstanceRateZeroDivisor = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR: {
         VkPhysicalDevicePerformanceQueryFeaturesKHR *features =
            (void *) ext;

         features->performanceCounterQueryPools =
            physical_device->caps.perfmon;
         features->performanceCounterMultipleQueryPools = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT: {
         VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *features =
            (void *) ext;
         features->texelBufferAlignment = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR: {
         VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *features =
            (VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *)ext;
         features->workgroupMemoryExplicitLayout = true;
         features->workgroupMemoryExplicitLayoutScalarBlockLayout = false;
         features->workgroupMemoryExplicitLayout8BitAccess = true;
         features->workgroupMemoryExplicitLayout16BitAccess = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT: {
         VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *features =
            (void *) ext;
         features->borderColorSwizzle = true;
         features->borderColorSwizzleFromImage = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_FEATURES_EXT: {
         VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT *features =
            (VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT *)ext;
         features->shaderModuleIdentifier = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT: {
         VkPhysicalDeviceDepthClipControlFeaturesEXT *features =
            (VkPhysicalDeviceDepthClipControlFeaturesEXT *)ext;
         features->depthClipControl = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT: {
         VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT *features =
            (void *) ext;
         features->attachmentFeedbackLoopLayout = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT: {
         VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *features =
            (void *) ext;
         features->primitiveTopologyListRestart = true;
         /* FIXME: we don't support tessellation shaders yet */
         features->primitiveTopologyPatchListRestart = false;
         break;
      }

      default:
         v3dv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetDeviceGroupPeerMemoryFeatures(VkDevice device,
                                      uint32_t heapIndex,
                                      uint32_t localDeviceIndex,
                                      uint32_t remoteDeviceIndex,
                                      VkPeerMemoryFeatureFlags *pPeerMemoryFeatures)
{
   assert(localDeviceIndex == 0 && remoteDeviceIndex == 0);
   *pPeerMemoryFeatures = VK_PEER_MEMORY_FEATURE_COPY_SRC_BIT |
                          VK_PEER_MEMORY_FEATURE_COPY_DST_BIT |
                          VK_PEER_MEMORY_FEATURE_GENERIC_SRC_BIT |
                          VK_PEER_MEMORY_FEATURE_GENERIC_DST_BIT;
}

uint32_t
v3dv_physical_device_vendor_id(struct v3dv_physical_device *dev)
{
   return 0x14E4; /* Broadcom */
}

uint32_t
v3dv_physical_device_device_id(struct v3dv_physical_device *dev)
{
#if using_v3d_simulator
   return dev->device_id;
#else
   switch (dev->devinfo.ver) {
   case 42:
      return 0xBE485FD3; /* Broadcom deviceID for 2711 */
   default:
      unreachable("Unsupported V3D version");
   }
#endif
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                 VkPhysicalDeviceProperties *pProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physicalDevice);

   STATIC_ASSERT(MAX_SAMPLED_IMAGES + MAX_STORAGE_IMAGES + MAX_INPUT_ATTACHMENTS
                 <= V3D_MAX_TEXTURE_SAMPLERS);
   STATIC_ASSERT(MAX_UNIFORM_BUFFERS >= MAX_DYNAMIC_UNIFORM_BUFFERS);
   STATIC_ASSERT(MAX_STORAGE_BUFFERS >= MAX_DYNAMIC_STORAGE_BUFFERS);

   const uint32_t page_size = 4096;
   const uint64_t mem_size = compute_heap_size();

   const uint32_t max_varying_components = 16 * 4;

   const float v3d_point_line_granularity = 2.0f / (1 << V3D_COORD_SHIFT);
   const uint32_t max_fb_size = V3D_MAX_IMAGE_DIMENSION;

   const VkSampleCountFlags supported_sample_counts =
      VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;

   struct timespec clock_res;
   clock_getres(CLOCK_MONOTONIC, &clock_res);
   const float timestamp_period =
      clock_res.tv_sec * 1000000000.0f + clock_res.tv_nsec;

   /* FIXME: this will probably require an in-depth review */
   VkPhysicalDeviceLimits limits = {
      .maxImageDimension1D                      = V3D_MAX_IMAGE_DIMENSION,
      .maxImageDimension2D                      = V3D_MAX_IMAGE_DIMENSION,
      .maxImageDimension3D                      = V3D_MAX_IMAGE_DIMENSION,
      .maxImageDimensionCube                    = V3D_MAX_IMAGE_DIMENSION,
      .maxImageArrayLayers                      = V3D_MAX_ARRAY_LAYERS,
      .maxTexelBufferElements                   = (1ul << 28),
      .maxUniformBufferRange                    = V3D_MAX_BUFFER_RANGE,
      .maxStorageBufferRange                    = V3D_MAX_BUFFER_RANGE,
      .maxPushConstantsSize                     = MAX_PUSH_CONSTANTS_SIZE,
      .maxMemoryAllocationCount                 = mem_size / page_size,
      .maxSamplerAllocationCount                = 64 * 1024,
      .bufferImageGranularity                   = V3D_NON_COHERENT_ATOM_SIZE,
      .sparseAddressSpaceSize                   = 0,
      .maxBoundDescriptorSets                   = MAX_SETS,
      .maxPerStageDescriptorSamplers            = V3D_MAX_TEXTURE_SAMPLERS,
      .maxPerStageDescriptorUniformBuffers      = MAX_UNIFORM_BUFFERS,
      .maxPerStageDescriptorStorageBuffers      = MAX_STORAGE_BUFFERS,
      .maxPerStageDescriptorSampledImages       = MAX_SAMPLED_IMAGES,
      .maxPerStageDescriptorStorageImages       = MAX_STORAGE_IMAGES,
      .maxPerStageDescriptorInputAttachments    = MAX_INPUT_ATTACHMENTS,
      .maxPerStageResources                     = 128,

      /* Some of these limits are multiplied by 6 because they need to
       * include all possible shader stages (even if not supported). See
       * 'Required Limits' table in the Vulkan spec.
       */
      .maxDescriptorSetSamplers                 = 6 * V3D_MAX_TEXTURE_SAMPLERS,
      .maxDescriptorSetUniformBuffers           = 6 * MAX_UNIFORM_BUFFERS,
      .maxDescriptorSetUniformBuffersDynamic    = MAX_DYNAMIC_UNIFORM_BUFFERS,
      .maxDescriptorSetStorageBuffers           = 6 * MAX_STORAGE_BUFFERS,
      .maxDescriptorSetStorageBuffersDynamic    = MAX_DYNAMIC_STORAGE_BUFFERS,
      .maxDescriptorSetSampledImages            = 6 * MAX_SAMPLED_IMAGES,
      .maxDescriptorSetStorageImages            = 6 * MAX_STORAGE_IMAGES,
      .maxDescriptorSetInputAttachments         = MAX_INPUT_ATTACHMENTS,

      /* Vertex limits */
      .maxVertexInputAttributes                 = MAX_VERTEX_ATTRIBS,
      .maxVertexInputBindings                   = MAX_VBS,
      .maxVertexInputAttributeOffset            = 0xffffffff,
      .maxVertexInputBindingStride              = 0xffffffff,
      .maxVertexOutputComponents                = max_varying_components,

      /* Tessellation limits */
      .maxTessellationGenerationLevel           = 0,
      .maxTessellationPatchSize                 = 0,
      .maxTessellationControlPerVertexInputComponents = 0,
      .maxTessellationControlPerVertexOutputComponents = 0,
      .maxTessellationControlPerPatchOutputComponents = 0,
      .maxTessellationControlTotalOutputComponents = 0,
      .maxTessellationEvaluationInputComponents = 0,
      .maxTessellationEvaluationOutputComponents = 0,

      /* Geometry limits */
      .maxGeometryShaderInvocations             = 32,
      .maxGeometryInputComponents               = 64,
      .maxGeometryOutputComponents              = 64,
      .maxGeometryOutputVertices                = 256,
      .maxGeometryTotalOutputComponents         = 1024,

      /* Fragment limits */
      .maxFragmentInputComponents               = max_varying_components,
      .maxFragmentOutputAttachments             = 4,
      .maxFragmentDualSrcAttachments            = 0,
      .maxFragmentCombinedOutputResources       = MAX_RENDER_TARGETS +
                                                  MAX_STORAGE_BUFFERS +
                                                  MAX_STORAGE_IMAGES,

      /* Compute limits */
      .maxComputeSharedMemorySize               = 16384,
      .maxComputeWorkGroupCount                 = { 65535, 65535, 65535 },
      .maxComputeWorkGroupInvocations           = 256,
      .maxComputeWorkGroupSize                  = { 256, 256, 256 },

      .subPixelPrecisionBits                    = V3D_COORD_SHIFT,
      .subTexelPrecisionBits                    = 8,
      .mipmapPrecisionBits                      = 8,
      .maxDrawIndexedIndexValue                 = 0x00ffffff,
      .maxDrawIndirectCount                     = 0x7fffffff,
      .maxSamplerLodBias                        = 14.0f,
      .maxSamplerAnisotropy                     = 16.0f,
      .maxViewports                             = MAX_VIEWPORTS,
      .maxViewportDimensions                    = { max_fb_size, max_fb_size },
      .viewportBoundsRange                      = { -2.0 * max_fb_size,
                                                    2.0 * max_fb_size - 1 },
      .viewportSubPixelBits                     = 0,
      .minMemoryMapAlignment                    = page_size,
      .minTexelBufferOffsetAlignment            = V3D_TMU_TEXEL_ALIGN,
      .minUniformBufferOffsetAlignment          = 32,
      .minStorageBufferOffsetAlignment          = 32,
      .minTexelOffset                           = -8,
      .maxTexelOffset                           = 7,
      .minTexelGatherOffset                     = -8,
      .maxTexelGatherOffset                     = 7,
      .minInterpolationOffset                   = -0.5,
      .maxInterpolationOffset                   = 0.5,
      .subPixelInterpolationOffsetBits          = V3D_COORD_SHIFT,
      .maxFramebufferWidth                      = max_fb_size,
      .maxFramebufferHeight                     = max_fb_size,
      .maxFramebufferLayers                     = 256,
      .framebufferColorSampleCounts             = supported_sample_counts,
      .framebufferDepthSampleCounts             = supported_sample_counts,
      .framebufferStencilSampleCounts           = supported_sample_counts,
      .framebufferNoAttachmentsSampleCounts     = supported_sample_counts,
      .maxColorAttachments                      = MAX_RENDER_TARGETS,
      .sampledImageColorSampleCounts            = supported_sample_counts,
      .sampledImageIntegerSampleCounts          = supported_sample_counts,
      .sampledImageDepthSampleCounts            = supported_sample_counts,
      .sampledImageStencilSampleCounts          = supported_sample_counts,
      .storageImageSampleCounts                 = VK_SAMPLE_COUNT_1_BIT,
      .maxSampleMaskWords                       = 1,
      .timestampComputeAndGraphics              = true,
      .timestampPeriod                          = timestamp_period,
      .maxClipDistances                         = 8,
      .maxCullDistances                         = 0,
      .maxCombinedClipAndCullDistances          = 8,
      .discreteQueuePriorities                  = 2,
      .pointSizeRange                           = { v3d_point_line_granularity,
                                                    V3D_MAX_POINT_SIZE },
      .lineWidthRange                           = { 1.0f, V3D_MAX_LINE_WIDTH },
      .pointSizeGranularity                     = v3d_point_line_granularity,
      .lineWidthGranularity                     = v3d_point_line_granularity,
      .strictLines                              = true,
      .standardSampleLocations                  = false,
      .optimalBufferCopyOffsetAlignment         = 32,
      .optimalBufferCopyRowPitchAlignment       = 32,
      .nonCoherentAtomSize                      = V3D_NON_COHERENT_ATOM_SIZE,
   };

   *pProperties = (VkPhysicalDeviceProperties) {
      .apiVersion = V3DV_API_VERSION,
      .driverVersion = vk_get_driver_version(),
      .vendorID = v3dv_physical_device_vendor_id(pdevice),
      .deviceID = v3dv_physical_device_device_id(pdevice),
      .deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
      .limits = limits,
      .sparseProperties = { 0 },
   };

   snprintf(pProperties->deviceName, sizeof(pProperties->deviceName),
            "%s", pdevice->name);
   memcpy(pProperties->pipelineCacheUUID,
          pdevice->pipeline_cache_uuid, VK_UUID_SIZE);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                  VkPhysicalDeviceProperties2 *pProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physicalDevice);

   v3dv_GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);

   /* We don't really have special restrictions for the maximum
    * descriptors per set, other than maybe not exceeding the limits
    * of addressable memory in a single allocation on either the host
    * or the GPU. This will be a much larger limit than any of the
    * per-stage limits already available in Vulkan though, so in practice,
    * it is not expected to limit anything beyond what is already
    * constrained through per-stage limits.
    */
   const uint32_t max_host_descriptors =
      (UINT32_MAX - sizeof(struct v3dv_descriptor_set)) /
      sizeof(struct v3dv_descriptor);
   const uint32_t max_gpu_descriptors =
      (UINT32_MAX / v3dv_X(pdevice, max_descriptor_bo_size)());

   VkPhysicalDeviceVulkan13Properties vk13 = {
      .maxInlineUniformBlockSize = 4096,
      .maxPerStageDescriptorInlineUniformBlocks = MAX_INLINE_UNIFORM_BUFFERS,
      .maxDescriptorSetInlineUniformBlocks = MAX_INLINE_UNIFORM_BUFFERS,
      .maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks =
         MAX_INLINE_UNIFORM_BUFFERS,
      .maxDescriptorSetUpdateAfterBindInlineUniformBlocks =
         MAX_INLINE_UNIFORM_BUFFERS,
      .maxBufferSize = V3D_MAX_BUFFER_RANGE,
   };

   VkPhysicalDeviceVulkan12Properties vk12 = {
      .driverID = VK_DRIVER_ID_MESA_V3DV,
      .conformanceVersion = {
         .major = 1,
         .minor = 2,
         .subminor = 7,
         .patch = 1,
      },
      .supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
      .supportedStencilResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
      /* FIXME: if we want to support independentResolveNone then we would
       * need to honor attachment load operations on resolve attachments,
       * which we currently ignore because the resolve makes them irrelevant,
       * as it unconditionally writes all pixels in the render area. However,
       * with independentResolveNone, it is possible to have one aspect of a
       * D/S resolve attachment stay unresolved, in which case the attachment
       * load operation is relevant.
       *
       * NOTE: implementing attachment load for resolve attachments isn't
       * immediately trivial because these attachments are not part of the
       * framebuffer and therefore we can't use the same mechanism we use
       * for framebuffer attachments. Instead, we should probably have to
       * emit a meta operation for that right at the start of the render
       * pass (or subpass).
       */
      .independentResolveNone = false,
      .independentResolve = false,
      .maxTimelineSemaphoreValueDifference = UINT64_MAX,

      .denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL,
      .roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL,
      .shaderSignedZeroInfNanPreserveFloat16 = true,
      .shaderSignedZeroInfNanPreserveFloat32 = true,
      .shaderSignedZeroInfNanPreserveFloat64 = false,
      .shaderDenormPreserveFloat16 = true,
      .shaderDenormPreserveFloat32 = true,
      .shaderDenormPreserveFloat64 = false,
      .shaderDenormFlushToZeroFloat16 = false,
      .shaderDenormFlushToZeroFloat32 = false,
      .shaderDenormFlushToZeroFloat64 = false,
      .shaderRoundingModeRTEFloat16 = true,
      .shaderRoundingModeRTEFloat32 = true,
      .shaderRoundingModeRTEFloat64 = false,
      .shaderRoundingModeRTZFloat16 = false,
      .shaderRoundingModeRTZFloat32 = false,
      .shaderRoundingModeRTZFloat64 = false,

      /* V3D doesn't support min/max filtering */
      .filterMinmaxSingleComponentFormats = false,
      .filterMinmaxImageComponentMapping = false,

      .framebufferIntegerColorSampleCounts =
         VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT,
   };
   memset(vk12.driverName, 0, VK_MAX_DRIVER_NAME_SIZE);
   snprintf(vk12.driverName, VK_MAX_DRIVER_NAME_SIZE, "V3DV Mesa");
   memset(vk12.driverInfo, 0, VK_MAX_DRIVER_INFO_SIZE);
   snprintf(vk12.driverInfo, VK_MAX_DRIVER_INFO_SIZE,
            "Mesa " PACKAGE_VERSION MESA_GIT_SHA1);

   VkPhysicalDeviceVulkan11Properties vk11 = {
      .deviceLUIDValid = false,
      .subgroupSize = V3D_CHANNELS,
      .subgroupSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT,
      .subgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT,
      .subgroupQuadOperationsInAllStages = false,
      .pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES,
      .maxMultiviewViewCount = MAX_MULTIVIEW_VIEW_COUNT,
      .maxMultiviewInstanceIndex = UINT32_MAX - 1,
      .protectedNoFault = false,
      .maxPerSetDescriptors = MIN2(max_host_descriptors, max_gpu_descriptors),
      /* Minimum required by the spec */
      .maxMemoryAllocationSize = MAX_MEMORY_ALLOCATION_SIZE,
   };
   memcpy(vk11.deviceUUID, pdevice->device_uuid, VK_UUID_SIZE);
   memcpy(vk11.driverUUID, pdevice->driver_uuid, VK_UUID_SIZE);


   vk_foreach_struct(ext, pProperties->pNext) {
      if (vk_get_physical_device_core_1_1_property_ext(ext, &vk11))
         continue;
      if (vk_get_physical_device_core_1_2_property_ext(ext, &vk12))
         continue;
      if (vk_get_physical_device_core_1_3_property_ext(ext, &vk13))
         continue;

      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT: {
         VkPhysicalDeviceCustomBorderColorPropertiesEXT *props =
            (VkPhysicalDeviceCustomBorderColorPropertiesEXT *)ext;
         props->maxCustomBorderColorSamplers = V3D_MAX_TEXTURE_SAMPLERS;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_PROPERTIES_EXT: {
         VkPhysicalDeviceProvokingVertexPropertiesEXT *props =
            (VkPhysicalDeviceProvokingVertexPropertiesEXT *)ext;
         props->provokingVertexModePerPipeline = true;
         /* FIXME: update when supporting EXT_transform_feedback */
         props->transformFeedbackPreservesTriangleFanProvokingVertex = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT: {
         VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *props =
            (VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *)ext;
         props->maxVertexAttribDivisor = 0xffff;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_PROPERTIES_KHR : {
         VkPhysicalDevicePerformanceQueryPropertiesKHR *props =
            (VkPhysicalDevicePerformanceQueryPropertiesKHR *)ext;

         props->allowCommandBufferQueryCopies = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT: {
         VkPhysicalDeviceDrmPropertiesEXT *props =
            (VkPhysicalDeviceDrmPropertiesEXT *)ext;
         props->hasPrimary = pdevice->has_primary;
         if (props->hasPrimary) {
            props->primaryMajor = (int64_t) major(pdevice->primary_devid);
            props->primaryMinor = (int64_t) minor(pdevice->primary_devid);
         }
         props->hasRender = pdevice->has_render;
         if (props->hasRender) {
            props->renderMajor = (int64_t) major(pdevice->render_devid);
            props->renderMinor = (int64_t) minor(pdevice->render_devid);
         }
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES_EXT: {
         VkPhysicalDeviceLineRasterizationPropertiesEXT *props =
            (VkPhysicalDeviceLineRasterizationPropertiesEXT *)ext;
         props->lineSubPixelPrecisionBits = V3D_COORD_SHIFT;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT:
         /* Do nothing, not even logging. This is a non-PCI device, so we will
          * never provide this extension.
          */
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES_EXT: {
         VkPhysicalDeviceTexelBufferAlignmentPropertiesEXT *props =
            (VkPhysicalDeviceTexelBufferAlignmentPropertiesEXT *)ext;
         props->storageTexelBufferOffsetAlignmentBytes = V3D_TMU_TEXEL_ALIGN;
         props->storageTexelBufferOffsetSingleTexelAlignment = false;
         props->uniformTexelBufferOffsetAlignmentBytes = V3D_TMU_TEXEL_ALIGN;
         props->uniformTexelBufferOffsetSingleTexelAlignment = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_PROPERTIES_EXT: {
         VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *props =
            (VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *)ext;
         STATIC_ASSERT(sizeof(vk_shaderModuleIdentifierAlgorithmUUID) ==
                       sizeof(props->shaderModuleIdentifierAlgorithmUUID));
         memcpy(props->shaderModuleIdentifierAlgorithmUUID,
                vk_shaderModuleIdentifierAlgorithmUUID,
                sizeof(props->shaderModuleIdentifierAlgorithmUUID));
         break;
      }
      default:
         v3dv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

/* We support exactly one queue family. */
static const VkQueueFamilyProperties
v3dv_queue_family_properties = {
   .queueFlags = VK_QUEUE_GRAPHICS_BIT |
                 VK_QUEUE_COMPUTE_BIT |
                 VK_QUEUE_TRANSFER_BIT,
   .queueCount = 1,
   .timestampValidBits = 64,
   .minImageTransferGranularity = { 1, 1, 1 },
};

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice,
                                             uint32_t *pQueueFamilyPropertyCount,
                                             VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   VK_OUTARRAY_MAKE_TYPED(VkQueueFamilyProperties2, out,
                          pQueueFamilyProperties, pQueueFamilyPropertyCount);

   vk_outarray_append_typed(VkQueueFamilyProperties2, &out, p) {
      p->queueFamilyProperties = v3dv_queue_family_properties;

      vk_foreach_struct(s, p->pNext) {
         v3dv_debug_ignored_stype(s->sType);
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                       VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, device, physicalDevice);
   *pMemoryProperties = device->memory;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice,
                                        VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, device, physicalDevice);

   v3dv_GetPhysicalDeviceMemoryProperties(physicalDevice,
                                          &pMemoryProperties->memoryProperties);

   vk_foreach_struct(ext, pMemoryProperties->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT: {
         VkPhysicalDeviceMemoryBudgetPropertiesEXT *p =
            (VkPhysicalDeviceMemoryBudgetPropertiesEXT *) ext;
         p->heapUsage[0] = device->heap_used;
         p->heapBudget[0] = compute_memory_budget(device);

         /* The heapBudget and heapUsage values must be zero for array elements
          * greater than or equal to VkPhysicalDeviceMemoryProperties::memoryHeapCount
          */
         for (unsigned i = 1; i < VK_MAX_MEMORY_HEAPS; i++) {
            p->heapBudget[i] = 0u;
            p->heapUsage[i] = 0u;
         }
         break;
      }
      default:
         v3dv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
v3dv_GetInstanceProcAddr(VkInstance _instance,
                         const char *pName)
{
   V3DV_FROM_HANDLE(v3dv_instance, instance, _instance);
   return vk_instance_get_proc_addr(&instance->vk,
                                    &v3dv_instance_entrypoints,
                                    pName);
}

/* With version 1+ of the loader interface the ICD should expose
 * vk_icdGetInstanceProcAddr to work around certain LD_PRELOAD issues seen in apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction
VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance,
                                     const char *pName);

PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance,
                          const char*                                 pName)
{
   return v3dv_GetInstanceProcAddr(instance, pName);
}

/* With version 4+ of the loader interface the ICD should expose
 * vk_icdGetPhysicalDeviceProcAddr()
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance  _instance,
                                const char* pName);

PFN_vkVoidFunction
vk_icdGetPhysicalDeviceProcAddr(VkInstance  _instance,
                                const char* pName)
{
   V3DV_FROM_HANDLE(v3dv_instance, instance, _instance);

   return vk_instance_get_physical_device_proc_addr(&instance->vk, pName);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_EnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                      VkLayerProperties *pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                    uint32_t *pPropertyCount,
                                    VkLayerProperties *pProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, physical_device, physicalDevice);

   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   return vk_error(physical_device, VK_ERROR_LAYER_NOT_PRESENT);
}

static void
destroy_queue_syncs(struct v3dv_queue *queue)
{
   for (int i = 0; i < V3DV_QUEUE_COUNT; i++) {
      if (queue->last_job_syncs.syncs[i]) {
         drmSyncobjDestroy(queue->device->pdevice->render_fd,
                           queue->last_job_syncs.syncs[i]);
      }
   }
}

static VkResult
queue_init(struct v3dv_device *device, struct v3dv_queue *queue,
           const VkDeviceQueueCreateInfo *create_info,
           uint32_t index_in_family)
{
   VkResult result = vk_queue_init(&queue->vk, &device->vk, create_info,
                                   index_in_family);
   if (result != VK_SUCCESS)
      return result;

   result = vk_queue_enable_submit_thread(&queue->vk);
   if (result != VK_SUCCESS)
      goto fail_submit_thread;

   queue->device = device;
   queue->vk.driver_submit = v3dv_queue_driver_submit;

   for (int i = 0; i < V3DV_QUEUE_COUNT; i++) {
      queue->last_job_syncs.first[i] = true;
      int ret = drmSyncobjCreate(device->pdevice->render_fd,
                                 DRM_SYNCOBJ_CREATE_SIGNALED,
                                 &queue->last_job_syncs.syncs[i]);
      if (ret) {
         result = vk_errorf(device, VK_ERROR_INITIALIZATION_FAILED,
                            "syncobj create failed: %m");
         goto fail_last_job_syncs;
      }
   }

   queue->noop_job = NULL;
   return VK_SUCCESS;

fail_last_job_syncs:
   destroy_queue_syncs(queue);
fail_submit_thread:
   vk_queue_finish(&queue->vk);
   return result;
}

static void
queue_finish(struct v3dv_queue *queue)
{
   if (queue->noop_job)
      v3dv_job_destroy(queue->noop_job);
   destroy_queue_syncs(queue);
   vk_queue_finish(&queue->vk);
}

static void
init_device_meta(struct v3dv_device *device)
{
   mtx_init(&device->meta.mtx, mtx_plain);
   v3dv_meta_clear_init(device);
   v3dv_meta_blit_init(device);
   v3dv_meta_texel_buffer_copy_init(device);
}

static void
destroy_device_meta(struct v3dv_device *device)
{
   mtx_destroy(&device->meta.mtx);
   v3dv_meta_clear_finish(device);
   v3dv_meta_blit_finish(device);
   v3dv_meta_texel_buffer_copy_finish(device);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateDevice(VkPhysicalDevice physicalDevice,
                  const VkDeviceCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkDevice *pDevice)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, physical_device, physicalDevice);
   struct v3dv_instance *instance = (struct v3dv_instance*) physical_device->vk.instance;
   VkResult result;
   struct v3dv_device *device;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);

   /* Check requested queues (we only expose one queue ) */
   assert(pCreateInfo->queueCreateInfoCount == 1);
   for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      assert(pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex == 0);
      assert(pCreateInfo->pQueueCreateInfos[i].queueCount == 1);
      if (pCreateInfo->pQueueCreateInfos[i].flags != 0)
         return vk_error(instance, VK_ERROR_INITIALIZATION_FAILED);
   }

   device = vk_zalloc2(&physical_device->vk.instance->alloc, pAllocator,
                       sizeof(*device), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_device_dispatch_table dispatch_table;
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &v3dv_device_entrypoints, true);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &wsi_device_entrypoints, false);
   result = vk_device_init(&device->vk, &physical_device->vk,
                           &dispatch_table, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, device);
      return vk_error(NULL, result);
   }

   device->instance = instance;
   device->pdevice = physical_device;

   mtx_init(&device->query_mutex, mtx_plain);
   cnd_init(&device->query_ended);

   device->vk.command_buffer_ops = &v3dv_cmd_buffer_ops;

   vk_device_set_drm_fd(&device->vk, physical_device->render_fd);
   vk_device_enable_threaded_submit(&device->vk);

   result = queue_init(device, &device->queue,
                       pCreateInfo->pQueueCreateInfos, 0);
   if (result != VK_SUCCESS)
      goto fail;

   device->devinfo = physical_device->devinfo;

   /* Vulkan 1.1 and VK_KHR_get_physical_device_properties2 added
    * VkPhysicalDeviceFeatures2 which can be used in the pNext chain of
    * vkDeviceCreateInfo, in which case it should be used instead of
    * pEnabledFeatures.
    */
   const VkPhysicalDeviceFeatures2 *features2 =
      vk_find_struct_const(pCreateInfo->pNext, PHYSICAL_DEVICE_FEATURES_2);
   if (features2) {
      memcpy(&device->features, &features2->features,
             sizeof(device->features));
   } else  if (pCreateInfo->pEnabledFeatures) {
      memcpy(&device->features, pCreateInfo->pEnabledFeatures,
             sizeof(device->features));
   }

   if (device->features.robustBufferAccess)
      perf_debug("Device created with Robust Buffer Access enabled.\n");

#ifdef DEBUG
   v3dv_X(device, device_check_prepacked_sizes)();
#endif
   init_device_meta(device);
   v3dv_bo_cache_init(device);
   v3dv_pipeline_cache_init(&device->default_pipeline_cache, device, 0,
                            device->instance->default_pipeline_cache_enabled);
   device->default_attribute_float =
      v3dv_pipeline_create_default_attribute_values(device, NULL);

   device->device_address_mem_ctx = ralloc_context(NULL);
   util_dynarray_init(&device->device_address_bo_list,
                      device->device_address_mem_ctx);

   *pDevice = v3dv_device_to_handle(device);

   return VK_SUCCESS;

fail:
   cnd_destroy(&device->query_ended);
   mtx_destroy(&device->query_mutex);
   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroyDevice(VkDevice _device,
                   const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);

   device->vk.dispatch_table.DeviceWaitIdle(_device);
   queue_finish(&device->queue);
   destroy_device_meta(device);
   v3dv_pipeline_cache_finish(&device->default_pipeline_cache);

   if (device->default_attribute_float) {
      v3dv_bo_free(device, device->default_attribute_float);
      device->default_attribute_float = NULL;
   }

   ralloc_free(device->device_address_mem_ctx);

   /* Bo cache should be removed the last, as any other object could be
    * freeing their private bos
    */
   v3dv_bo_cache_destroy(device);

   cnd_destroy(&device->query_ended);
   mtx_destroy(&device->query_mutex);

   vk_device_finish(&device->vk);
   vk_free2(&device->vk.alloc, pAllocator, device);
}

static VkResult
device_alloc(struct v3dv_device *device,
             struct v3dv_device_memory *mem,
             VkDeviceSize size)
{
   /* Our kernel interface is 32-bit */
   assert(size <= UINT32_MAX);

   mem->bo = v3dv_bo_alloc(device, size, "device_alloc", false);
   if (!mem->bo)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   return VK_SUCCESS;
}

static void
device_free_wsi_dumb(int32_t display_fd, int32_t dumb_handle)
{
   assert(display_fd != -1);
   if (dumb_handle < 0)
      return;

   struct drm_mode_destroy_dumb destroy_dumb = {
      .handle = dumb_handle,
   };
   if (v3dv_ioctl(display_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb)) {
      fprintf(stderr, "destroy dumb object %d: %s\n", dumb_handle, strerror(errno));
   }
}

static void
device_free(struct v3dv_device *device, struct v3dv_device_memory *mem)
{
   /* If this memory allocation was for WSI, then we need to use the
    * display device to free the allocated dumb BO.
    */
   if (mem->is_for_wsi) {
      device_free_wsi_dumb(device->pdevice->display_fd, mem->bo->dumb_handle);
   }

   p_atomic_add(&device->pdevice->heap_used, -((int64_t)mem->bo->size));

   v3dv_bo_free(device, mem->bo);
}

static void
device_unmap(struct v3dv_device *device, struct v3dv_device_memory *mem)
{
   assert(mem && mem->bo->map && mem->bo->map_size > 0);
   v3dv_bo_unmap(device, mem->bo);
}

static VkResult
device_map(struct v3dv_device *device, struct v3dv_device_memory *mem)
{
   assert(mem && mem->bo);

   /* From the spec:
    *
    *   "After a successful call to vkMapMemory the memory object memory is
    *   considered to be currently host mapped. It is an application error to
    *   call vkMapMemory on a memory object that is already host mapped."
    *
    * We are not concerned with this ourselves (validation layers should
    * catch these errors and warn users), however, the driver may internally
    * map things (for example for debug CLIF dumps or some CPU-side operations)
    * so by the time the user calls here the buffer might already been mapped
    * internally by the driver.
    */
   if (mem->bo->map) {
      assert(mem->bo->map_size == mem->bo->size);
      return VK_SUCCESS;
   }

   bool ok = v3dv_bo_map(device, mem->bo, mem->bo->size);
   if (!ok)
      return VK_ERROR_MEMORY_MAP_FAILED;

   return VK_SUCCESS;
}

static VkResult
device_import_bo(struct v3dv_device *device,
                 const VkAllocationCallbacks *pAllocator,
                 int fd, uint64_t size,
                 struct v3dv_bo **bo)
{
   *bo = NULL;

   off_t real_size = lseek(fd, 0, SEEK_END);
   lseek(fd, 0, SEEK_SET);
   if (real_size < 0 || (uint64_t) real_size < size)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   int render_fd = device->pdevice->render_fd;
   assert(render_fd >= 0);

   int ret;
   uint32_t handle;
   ret = drmPrimeFDToHandle(render_fd, fd, &handle);
   if (ret)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   struct drm_v3d_get_bo_offset get_offset = {
      .handle = handle,
   };
   ret = v3dv_ioctl(render_fd, DRM_IOCTL_V3D_GET_BO_OFFSET, &get_offset);
   if (ret)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   assert(get_offset.offset != 0);

   *bo = v3dv_device_lookup_bo(device->pdevice, handle);
   assert(*bo);

   if ((*bo)->refcnt == 0)
      v3dv_bo_init(*bo, handle, size, get_offset.offset, "import", false);
   else
      p_atomic_inc(&(*bo)->refcnt);

   return VK_SUCCESS;
}

static VkResult
device_alloc_for_wsi(struct v3dv_device *device,
                     const VkAllocationCallbacks *pAllocator,
                     struct v3dv_device_memory *mem,
                     VkDeviceSize size)
{
   /* In the simulator we can get away with a regular allocation since both
    * allocation and rendering happen in the same DRM render node. On actual
    * hardware we need to allocate our winsys BOs on the vc4 display device
    * and import them into v3d.
    */
#if using_v3d_simulator
      return device_alloc(device, mem, size);
#else
   /* If we are allocating for WSI we should have a swapchain and thus,
    * we should've initialized the display device. However, Zink doesn't
    * use swapchains, so in that case we can get here without acquiring the
    * display device and we need to do it now.
    */
   VkResult result;
   struct v3dv_instance *instance = device->instance;
   struct v3dv_physical_device *pdevice = device->pdevice;
   if (unlikely(pdevice->display_fd < 0)) {
      result = v3dv_physical_device_acquire_display(instance, pdevice, NULL);
      if (result != VK_SUCCESS)
         return result;
   }
   assert(pdevice->display_fd != -1);

   mem->is_for_wsi = true;

   int display_fd = pdevice->display_fd;
   struct drm_mode_create_dumb create_dumb = {
      .width = 1024, /* one page */
      .height = align(size, 4096) / 4096,
      .bpp = util_format_get_blocksizebits(PIPE_FORMAT_RGBA8888_UNORM),
   };

   int err;
   err = v3dv_ioctl(display_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb);
   if (err < 0)
      goto fail_create;

   int fd;
   err =
      drmPrimeHandleToFD(display_fd, create_dumb.handle, O_CLOEXEC, &fd);
   if (err < 0)
      goto fail_export;

   result = device_import_bo(device, pAllocator, fd, size, &mem->bo);
   close(fd);
   if (result != VK_SUCCESS)
      goto fail_import;

   mem->bo->dumb_handle = create_dumb.handle;
   return VK_SUCCESS;

fail_import:
fail_export:
   device_free_wsi_dumb(display_fd, create_dumb.handle);

fail_create:
   return VK_ERROR_OUT_OF_DEVICE_MEMORY;
#endif
}

static void
device_add_device_address_bo(struct v3dv_device *device,
                                  struct v3dv_bo *bo)
{
   util_dynarray_append(&device->device_address_bo_list,
                        struct v3dv_bo *,
                        bo);
}

static void
device_remove_device_address_bo(struct v3dv_device *device,
                                struct v3dv_bo *bo)
{
   util_dynarray_delete_unordered(&device->device_address_bo_list,
                                  struct v3dv_bo *,
                                  bo);
}

static void
free_memory(struct v3dv_device *device,
            struct v3dv_device_memory *mem,
            const VkAllocationCallbacks *pAllocator)
{
   if (mem == NULL)
      return;

   if (mem->bo->map)
      device_unmap(device, mem);

   if (mem->is_for_device_address)
      device_remove_device_address_bo(device, mem->bo);

   device_free(device, mem);

   vk_object_free(&device->vk, pAllocator, mem);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_FreeMemory(VkDevice _device,
                VkDeviceMemory _mem,
                const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_device_memory, mem, _mem);
   free_memory(device, mem, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_AllocateMemory(VkDevice _device,
                    const VkMemoryAllocateInfo *pAllocateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkDeviceMemory *pMem)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_device_memory *mem;
   struct v3dv_physical_device *pdevice = device->pdevice;

   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

   /* The Vulkan 1.0.33 spec says "allocationSize must be greater than 0". */
   assert(pAllocateInfo->allocationSize > 0);

   /* We always allocate device memory in multiples of a page, so round up
    * requested size to that.
    */
   const VkDeviceSize alloc_size = ALIGN(pAllocateInfo->allocationSize, 4096);

   if (unlikely(alloc_size > MAX_MEMORY_ALLOCATION_SIZE))
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   uint64_t heap_used = p_atomic_read(&pdevice->heap_used);
   if (unlikely(heap_used + alloc_size > pdevice->memory.memoryHeaps[0].size))
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   mem = vk_object_zalloc(&device->vk, pAllocator, sizeof(*mem),
                          VK_OBJECT_TYPE_DEVICE_MEMORY);
   if (mem == NULL)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   assert(pAllocateInfo->memoryTypeIndex < pdevice->memory.memoryTypeCount);
   mem->type = &pdevice->memory.memoryTypes[pAllocateInfo->memoryTypeIndex];
   mem->is_for_wsi = false;

   const struct wsi_memory_allocate_info *wsi_info = NULL;
   const VkImportMemoryFdInfoKHR *fd_info = NULL;
   const VkMemoryAllocateFlagsInfo *flags_info = NULL;
   vk_foreach_struct_const(ext, pAllocateInfo->pNext) {
      switch ((unsigned)ext->sType) {
      case VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA:
         wsi_info = (void *)ext;
         break;
      case VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR:
         fd_info = (void *)ext;
         break;
      case VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO:
         flags_info = (void *)ext;
         break;
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO:
         /* We don't have particular optimizations associated with memory
          * allocations that won't be suballocated to multiple resources.
          */
         break;
      case VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO:
         /* The mask of handle types specified here must be supported
          * according to VkExternalImageFormatProperties, so it must be
          * fd or dmabuf, which don't have special requirements for us.
          */
         break;
      default:
         v3dv_debug_ignored_stype(ext->sType);
         break;
      }
   }

   VkResult result;
   if (wsi_info) {
      result = device_alloc_for_wsi(device, pAllocator, mem, alloc_size);
   } else if (fd_info && fd_info->handleType) {
      assert(fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
             fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
      result = device_import_bo(device, pAllocator,
                                fd_info->fd, alloc_size, &mem->bo);
      if (result == VK_SUCCESS)
         close(fd_info->fd);
   } else {
      result = device_alloc(device, mem, alloc_size);
   }

   if (result != VK_SUCCESS) {
      vk_object_free(&device->vk, pAllocator, mem);
      return vk_error(device, result);
   }

   heap_used = p_atomic_add_return(&pdevice->heap_used, mem->bo->size);
   if (heap_used > pdevice->memory.memoryHeaps[0].size) {
      free_memory(device, mem, pAllocator);
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   /* If this memory can be used via VK_KHR_buffer_device_address then we
    * will need to manually add the BO to any job submit that makes use of
    * VK_KHR_buffer_device_address, since such jobs may produde buffer
    * load/store operations that may access any buffer memory allocated with
    * this flag and we don't have any means to tell which buffers will be
    * accessed through this mechanism since they don't even have to be bound
    * through descriptor state.
    */
   if (flags_info &&
       (flags_info->flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR)) {
      mem->is_for_device_address = true;
      device_add_device_address_bo(device, mem->bo);
   }

   *pMem = v3dv_device_memory_to_handle(mem);
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_MapMemory(VkDevice _device,
               VkDeviceMemory _memory,
               VkDeviceSize offset,
               VkDeviceSize size,
               VkMemoryMapFlags flags,
               void **ppData)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_device_memory, mem, _memory);

   if (mem == NULL) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   assert(offset < mem->bo->size);

   /* Since the driver can map BOs internally as well and the mapped range
    * required by the user or the driver might not be the same, we always map
    * the entire BO and then add the requested offset to the start address
    * of the mapped region.
    */
   VkResult result = device_map(device, mem);
   if (result != VK_SUCCESS)
      return vk_error(device, result);

   *ppData = ((uint8_t *) mem->bo->map) + offset;
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_UnmapMemory(VkDevice _device,
                 VkDeviceMemory _memory)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_device_memory, mem, _memory);

   if (mem == NULL)
      return;

   device_unmap(device, mem);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_FlushMappedMemoryRanges(VkDevice _device,
                             uint32_t memoryRangeCount,
                             const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_InvalidateMappedMemoryRanges(VkDevice _device,
                                  uint32_t memoryRangeCount,
                                  const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

static void
get_image_memory_requirements(struct v3dv_image *image,
                              VkMemoryRequirements2 *pMemoryRequirements)
{
   pMemoryRequirements->memoryRequirements = (VkMemoryRequirements) {
      .memoryTypeBits = 0x1,
      .alignment = image->alignment,
      .size = image->size
   };

   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req =
            (VkMemoryDedicatedRequirements *) ext;
         req->requiresDedicatedAllocation = image->vk.external_handle_types != 0;
         req->prefersDedicatedAllocation = image->vk.external_handle_types != 0;
         break;
      }
      default:
         v3dv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetImageMemoryRequirements2(VkDevice device,
                                 const VkImageMemoryRequirementsInfo2 *pInfo,
                                 VkMemoryRequirements2 *pMemoryRequirements)
{
   V3DV_FROM_HANDLE(v3dv_image, image, pInfo->image);
   get_image_memory_requirements(image, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetDeviceImageMemoryRequirementsKHR(
    VkDevice _device,
    const VkDeviceImageMemoryRequirements *pInfo,
    VkMemoryRequirements2 *pMemoryRequirements)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);

   struct v3dv_image image = { 0 };
   vk_image_init(&device->vk, &image.vk, pInfo->pCreateInfo);

   ASSERTED VkResult result =
      v3dv_image_init(device, pInfo->pCreateInfo, NULL, &image);
   assert(result == VK_SUCCESS);

   get_image_memory_requirements(&image, pMemoryRequirements);
}

static void
bind_image_memory(const VkBindImageMemoryInfo *info)
{
   V3DV_FROM_HANDLE(v3dv_image, image, info->image);
   V3DV_FROM_HANDLE(v3dv_device_memory, mem, info->memory);

   /* Valid usage:
    *
    *   "memoryOffset must be an integer multiple of the alignment member of
    *    the VkMemoryRequirements structure returned from a call to
    *    vkGetImageMemoryRequirements with image"
    */
   assert(info->memoryOffset % image->alignment == 0);
   assert(info->memoryOffset < mem->bo->size);

   image->mem = mem;
   image->mem_offset = info->memoryOffset;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_BindImageMemory2(VkDevice _device,
                      uint32_t bindInfoCount,
                      const VkBindImageMemoryInfo *pBindInfos)
{
   for (uint32_t i = 0; i < bindInfoCount; i++) {
#ifndef ANDROID
      const VkBindImageMemorySwapchainInfoKHR *swapchain_info =
         vk_find_struct_const(pBindInfos->pNext,
                              BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR);
      if (swapchain_info && swapchain_info->swapchain) {
         struct v3dv_image *swapchain_image =
            v3dv_wsi_get_image_from_swapchain(swapchain_info->swapchain,
                                              swapchain_info->imageIndex);
         VkBindImageMemoryInfo swapchain_bind = {
            .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
            .image = pBindInfos[i].image,
            .memory = v3dv_device_memory_to_handle(swapchain_image->mem),
            .memoryOffset = swapchain_image->mem_offset,
         };
         bind_image_memory(&swapchain_bind);
      } else
#endif
      {
         bind_image_memory(&pBindInfos[i]);
      }
   }

   return VK_SUCCESS;
}

static void
buffer_init(struct v3dv_device *device,
            const VkBufferCreateInfo *pCreateInfo,
            struct v3dv_buffer *buffer)
{
   buffer->size = pCreateInfo->size;
   buffer->usage = pCreateInfo->usage;
   buffer->alignment = V3D_NON_COHERENT_ATOM_SIZE;
}

static void
get_buffer_memory_requirements(struct v3dv_buffer *buffer,
                               VkMemoryRequirements2 *pMemoryRequirements)
{
   pMemoryRequirements->memoryRequirements = (VkMemoryRequirements) {
      .memoryTypeBits = 0x1,
      .alignment = buffer->alignment,
      .size = align64(buffer->size, buffer->alignment),
   };

   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req =
            (VkMemoryDedicatedRequirements *) ext;
         req->requiresDedicatedAllocation = false;
         req->prefersDedicatedAllocation = false;
         break;
      }
      default:
         v3dv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetBufferMemoryRequirements2(VkDevice device,
                                  const VkBufferMemoryRequirementsInfo2 *pInfo,
                                  VkMemoryRequirements2 *pMemoryRequirements)
{
   V3DV_FROM_HANDLE(v3dv_buffer, buffer, pInfo->buffer);
   get_buffer_memory_requirements(buffer, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetDeviceBufferMemoryRequirementsKHR(
    VkDevice _device,
    const VkDeviceBufferMemoryRequirements *pInfo,
    VkMemoryRequirements2 *pMemoryRequirements)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);

   struct v3dv_buffer buffer = { 0 };
   buffer_init(device, pInfo->pCreateInfo, &buffer);
   get_buffer_memory_requirements(&buffer, pMemoryRequirements);
}

static void
bind_buffer_memory(const VkBindBufferMemoryInfo *info)
{
   V3DV_FROM_HANDLE(v3dv_buffer, buffer, info->buffer);
   V3DV_FROM_HANDLE(v3dv_device_memory, mem, info->memory);

   /* Valid usage:
    *
    *   "memoryOffset must be an integer multiple of the alignment member of
    *    the VkMemoryRequirements structure returned from a call to
    *    vkGetBufferMemoryRequirements with buffer"
    */
   assert(info->memoryOffset % buffer->alignment == 0);
   assert(info->memoryOffset < mem->bo->size);

   buffer->mem = mem;
   buffer->mem_offset = info->memoryOffset;
}


VKAPI_ATTR VkResult VKAPI_CALL
v3dv_BindBufferMemory2(VkDevice device,
                       uint32_t bindInfoCount,
                       const VkBindBufferMemoryInfo *pBindInfos)
{
   for (uint32_t i = 0; i < bindInfoCount; i++)
      bind_buffer_memory(&pBindInfos[i]);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateBuffer(VkDevice  _device,
                  const VkBufferCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkBuffer *pBuffer)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_buffer *buffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
   assert(pCreateInfo->usage != 0);

   /* We don't support any flags for now */
   assert(pCreateInfo->flags == 0);

   buffer = vk_object_zalloc(&device->vk, pAllocator, sizeof(*buffer),
                             VK_OBJECT_TYPE_BUFFER);
   if (buffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   buffer_init(device, pCreateInfo, buffer);

   /* Limit allocations to 32-bit */
   const VkDeviceSize aligned_size = align64(buffer->size, buffer->alignment);
   if (aligned_size > UINT32_MAX || aligned_size < buffer->size) {
      vk_free(&device->vk.alloc, buffer);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   *pBuffer = v3dv_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroyBuffer(VkDevice _device,
                   VkBuffer _buffer,
                   const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_buffer, buffer, _buffer);

   if (!buffer)
      return;

   vk_object_free(&device->vk, pAllocator, buffer);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateFramebuffer(VkDevice _device,
                       const VkFramebufferCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkFramebuffer *pFramebuffer)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_framebuffer *framebuffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);

   size_t size = sizeof(*framebuffer) +
                 sizeof(struct v3dv_image_view *) * pCreateInfo->attachmentCount;
   framebuffer = vk_object_zalloc(&device->vk, pAllocator, size,
                                  VK_OBJECT_TYPE_FRAMEBUFFER);
   if (framebuffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   framebuffer->width = pCreateInfo->width;
   framebuffer->height = pCreateInfo->height;
   framebuffer->layers = pCreateInfo->layers;
   framebuffer->has_edge_padding = true;

   const VkFramebufferAttachmentsCreateInfo *imageless =
      vk_find_struct_const(pCreateInfo->pNext,
      FRAMEBUFFER_ATTACHMENTS_CREATE_INFO);

   framebuffer->attachment_count = pCreateInfo->attachmentCount;
   framebuffer->color_attachment_count = 0;
   for (uint32_t i = 0; i < framebuffer->attachment_count; i++) {
      if (!imageless) {
         framebuffer->attachments[i] =
            v3dv_image_view_from_handle(pCreateInfo->pAttachments[i]);
         if (framebuffer->attachments[i]->vk.aspects & VK_IMAGE_ASPECT_COLOR_BIT)
            framebuffer->color_attachment_count++;
      } else {
         assert(i < imageless->attachmentImageInfoCount);
         if (imageless->pAttachmentImageInfos[i].usage &
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
            framebuffer->color_attachment_count++;
         }
      }
   }

   *pFramebuffer = v3dv_framebuffer_to_handle(framebuffer);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroyFramebuffer(VkDevice _device,
                        VkFramebuffer _fb,
                        const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_framebuffer, fb, _fb);

   if (!fb)
      return;

   vk_object_free(&device->vk, pAllocator, fb);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetMemoryFdPropertiesKHR(VkDevice _device,
                              VkExternalMemoryHandleTypeFlagBits handleType,
                              int fd,
                              VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_physical_device *pdevice = device->pdevice;

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      pMemoryFdProperties->memoryTypeBits =
         (1 << pdevice->memory.memoryTypeCount) - 1;
      return VK_SUCCESS;
   default:
      return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetMemoryFdKHR(VkDevice _device,
                    const VkMemoryGetFdInfoKHR *pGetFdInfo,
                    int *pFd)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_device_memory, mem, pGetFdInfo->memory);

   assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);
   assert(pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
          pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

   int fd, ret;
   ret = drmPrimeHandleToFD(device->pdevice->render_fd,
                            mem->bo->handle,
                            DRM_CLOEXEC, &fd);
   if (ret)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   *pFd = fd;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateEvent(VkDevice _device,
                 const VkEventCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkEvent *pEvent)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_event *event =
      vk_object_zalloc(&device->vk, pAllocator, sizeof(*event),
                       VK_OBJECT_TYPE_EVENT);
   if (!event)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Events are created in the unsignaled state */
   event->state = false;
   *pEvent = v3dv_event_to_handle(event);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroyEvent(VkDevice _device,
                  VkEvent _event,
                  const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_event, event, _event);

   if (!event)
      return;

   vk_object_free(&device->vk, pAllocator, event);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetEventStatus(VkDevice _device, VkEvent _event)
{
   V3DV_FROM_HANDLE(v3dv_event, event, _event);
   return p_atomic_read(&event->state) ? VK_EVENT_SET : VK_EVENT_RESET;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_SetEvent(VkDevice _device, VkEvent _event)
{
   V3DV_FROM_HANDLE(v3dv_event, event, _event);
   p_atomic_set(&event->state, 1);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_ResetEvent(VkDevice _device, VkEvent _event)
{
   V3DV_FROM_HANDLE(v3dv_event, event, _event);
   p_atomic_set(&event->state, 0);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateSampler(VkDevice _device,
                 const VkSamplerCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkSampler *pSampler)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_sampler *sampler;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

   sampler = vk_object_zalloc(&device->vk, pAllocator, sizeof(*sampler),
                              VK_OBJECT_TYPE_SAMPLER);
   if (!sampler)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   sampler->compare_enable = pCreateInfo->compareEnable;
   sampler->unnormalized_coordinates = pCreateInfo->unnormalizedCoordinates;

   const VkSamplerCustomBorderColorCreateInfoEXT *bc_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT);

   v3dv_X(device, pack_sampler_state)(sampler, pCreateInfo, bc_info);

   *pSampler = v3dv_sampler_to_handle(sampler);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroySampler(VkDevice _device,
                  VkSampler _sampler,
                  const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_sampler, sampler, _sampler);

   if (!sampler)
      return;

   vk_object_free(&device->vk, pAllocator, sampler);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetDeviceMemoryCommitment(VkDevice device,
                               VkDeviceMemory memory,
                               VkDeviceSize *pCommittedMemoryInBytes)
{
   *pCommittedMemoryInBytes = 0;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetImageSparseMemoryRequirements(
    VkDevice device,
    VkImage image,
    uint32_t *pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements *pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetImageSparseMemoryRequirements2(
   VkDevice device,
   const VkImageSparseMemoryRequirementsInfo2 *pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetDeviceImageSparseMemoryRequirementsKHR(
    VkDevice device,
    const VkDeviceImageMemoryRequirements *pInfo,
    uint32_t *pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0;
}

/* vk_icd.h does not declare this function, so we declare it here to
 * suppress Wmissing-prototypes.
 */
PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion);

PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion)
{
   /* For the full details on loader interface versioning, see
    * <https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers/blob/master/loader/LoaderAndLayerInterface.md>.
    * What follows is a condensed summary, to help you navigate the large and
    * confusing official doc.
    *
    *   - Loader interface v0 is incompatible with later versions. We don't
    *     support it.
    *
    *   - In loader interface v1:
    *       - The first ICD entrypoint called by the loader is
    *         vk_icdGetInstanceProcAddr(). The ICD must statically expose this
    *         entrypoint.
    *       - The ICD must statically expose no other Vulkan symbol unless it is
    *         linked with -Bsymbolic.
    *       - Each dispatchable Vulkan handle created by the ICD must be
    *         a pointer to a struct whose first member is VK_LOADER_DATA. The
    *         ICD must initialize VK_LOADER_DATA.loadMagic to ICD_LOADER_MAGIC.
    *       - The loader implements vkCreate{PLATFORM}SurfaceKHR() and
    *         vkDestroySurfaceKHR(). The ICD must be capable of working with
    *         such loader-managed surfaces.
    *
    *    - Loader interface v2 differs from v1 in:
    *       - The first ICD entrypoint called by the loader is
    *         vk_icdNegotiateLoaderICDInterfaceVersion(). The ICD must
    *         statically expose this entrypoint.
    *
    *    - Loader interface v3 differs from v2 in:
    *        - The ICD must implement vkCreate{PLATFORM}SurfaceKHR(),
    *          vkDestroySurfaceKHR(), and other API which uses VKSurfaceKHR,
    *          because the loader no longer does so.
    *
    *    - Loader interface v4 differs from v3 in:
    *        - The ICD must implement vk_icdGetPhysicalDeviceProcAddr().
    * 
    *    - Loader interface v5 differs from v4 in:
    *        - The ICD must support Vulkan API version 1.1 and must not return 
    *          VK_ERROR_INCOMPATIBLE_DRIVER from vkCreateInstance() unless a
    *          Vulkan Loader with interface v4 or smaller is being used and the
    *          application provides an API version that is greater than 1.0.
    */
   *pSupportedVersion = MIN2(*pSupportedVersion, 5u);
   return VK_SUCCESS;
}

VkDeviceAddress
v3dv_GetBufferDeviceAddress(VkDevice device,
                            const VkBufferDeviceAddressInfoKHR *pInfo)
{
   V3DV_FROM_HANDLE(v3dv_buffer, buffer, pInfo->buffer);
   return buffer->mem_offset + buffer->mem->bo->offset;
}

uint64_t
v3dv_GetBufferOpaqueCaptureAddress(VkDevice device,
                                   const VkBufferDeviceAddressInfoKHR *pInfo)
{
   /* Not implemented */
   return 0;
}

uint64_t
v3dv_GetDeviceMemoryOpaqueCaptureAddress(
    VkDevice device,
    const VkDeviceMemoryOpaqueCaptureAddressInfoKHR *pInfo)
{
   /* Not implemented */
   return 0;
}
