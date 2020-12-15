/*
 * Copyright © 2019 Raspberry Pi
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

#include "v3dv_private.h"

#include "common/v3d_debug.h"

#include "broadcom/cle/v3dx_pack.h"

#include "compiler/v3d_compiler.h"
#include "compiler/glsl_types.h"

#include "drm-uapi/v3d_drm.h"
#include "format/u_format.h"
#include "vk_util.h"

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

#ifdef USE_V3D_SIMULATOR
#include "drm-uapi/i915_drm.h"
#endif

static void *
default_alloc_func(void *pUserData, size_t size, size_t align,
                   VkSystemAllocationScope allocationScope)
{
   return malloc(size);
}

static void *
default_realloc_func(void *pUserData, void *pOriginal, size_t size,
                     size_t align, VkSystemAllocationScope allocationScope)
{
   return realloc(pOriginal, size);
}

static void
default_free_func(void *pUserData, void *pMemory)
{
   free(pMemory);
}

static const VkAllocationCallbacks default_alloc = {
   .pUserData = NULL,
   .pfnAllocation = default_alloc_func,
   .pfnReallocation = default_realloc_func,
   .pfnFree = default_free_func,
};

VkResult
v3dv_EnumerateInstanceExtensionProperties(const char *pLayerName,
                                          uint32_t *pPropertyCount,
                                          VkExtensionProperties *pProperties)
{
   /* We don't support any layers  */
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);

   for (int i = 0; i < V3DV_INSTANCE_EXTENSION_COUNT; i++) {
      if (v3dv_instance_extensions_supported.extensions[i]) {
         vk_outarray_append(&out, prop) {
            *prop = v3dv_instance_extensions[i];
         }
      }
   }

   return vk_outarray_status(&out);
}

VkResult
v3dv_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkInstance *pInstance)
{
   struct v3dv_instance *instance;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   struct v3dv_instance_extension_table enabled_extensions = {};
   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      int idx;
      for (idx = 0; idx < V3DV_INSTANCE_EXTENSION_COUNT; idx++) {
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    v3dv_instance_extensions[idx].extensionName) == 0)
            break;
      }

      if (idx >= V3DV_INSTANCE_EXTENSION_COUNT)
         return vk_error(NULL, VK_ERROR_EXTENSION_NOT_PRESENT);

      if (!v3dv_instance_extensions_supported.extensions[idx])
         return vk_error(NULL, VK_ERROR_EXTENSION_NOT_PRESENT);

      enabled_extensions.extensions[idx] = true;
   }

   instance = vk_alloc2(&default_alloc, pAllocator, sizeof(*instance), 8,
                        VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(NULL, &instance->base, VK_OBJECT_TYPE_INSTANCE);

   if (pAllocator)
      instance->alloc = *pAllocator;
   else
      instance->alloc = default_alloc;

   v3d_process_debug_variable();

   instance->app_info = (struct v3dv_app_info) { .api_version = 0 };
   if (pCreateInfo->pApplicationInfo) {
      const VkApplicationInfo *app = pCreateInfo->pApplicationInfo;

      instance->app_info.app_name =
         vk_strdup(&instance->alloc, app->pApplicationName,
                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
      instance->app_info.app_version = app->applicationVersion;

      instance->app_info.engine_name =
         vk_strdup(&instance->alloc, app->pEngineName,
                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
      instance->app_info.engine_version = app->engineVersion;

      instance->app_info.api_version = app->apiVersion;
   }

   if (instance->app_info.api_version == 0)
      instance->app_info.api_version = VK_API_VERSION_1_0;

   instance->enabled_extensions = enabled_extensions;

   for (unsigned i = 0; i < ARRAY_SIZE(instance->dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have not been
       * enabled must not be advertised.
       */
      if (!v3dv_instance_entrypoint_is_enabled(i,
                                              instance->app_info.api_version,
                                              &instance->enabled_extensions)) {
         instance->dispatch.entrypoints[i] = NULL;
      } else {
         instance->dispatch.entrypoints[i] =
            v3dv_instance_dispatch_table.entrypoints[i];
      }
   }

   struct v3dv_physical_device *pdevice = &instance->physicalDevice;
   for (unsigned i = 0; i < ARRAY_SIZE(pdevice->dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have not been
       * enabled must not be advertised.
       */
      if (!v3dv_physical_device_entrypoint_is_enabled(i,
                                                     instance->app_info.api_version,
                                                     &instance->enabled_extensions)) {
         pdevice->dispatch.entrypoints[i] = NULL;
      } else {
         pdevice->dispatch.entrypoints[i] =
            v3dv_physical_device_dispatch_table.entrypoints[i];
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(instance->device_dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have not been
       * enabled must not be advertised.
       */
      if (!v3dv_device_entrypoint_is_enabled(i,
                                            instance->app_info.api_version,
                                            &instance->enabled_extensions,
                                            NULL)) {
         instance->device_dispatch.entrypoints[i] = NULL;
      } else {
         instance->device_dispatch.entrypoints[i] =
            v3dv_device_dispatch_table.entrypoints[i];
      }
   }

   instance->physicalDeviceCount = -1;

   result = vk_debug_report_instance_init(&instance->debug_report_callbacks);
   if (result != VK_SUCCESS) {
      vk_object_base_finish(&instance->base);
      vk_free2(&default_alloc, pAllocator, instance);
      return vk_error(NULL, result);
   }


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

   glsl_type_singleton_init_or_ref();

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   *pInstance = v3dv_instance_to_handle(instance);

   return VK_SUCCESS;
}

static void
physical_device_finish(struct v3dv_physical_device *device)
{
   v3dv_wsi_finish(device);

   v3d_compiler_free(device->compiler);

   close(device->render_fd);
   if (device->display_fd >= 0)
      close(device->display_fd);
   if (device->master_fd >= 0)
      close(device->master_fd);

   free(device->name);

#if using_v3d_simulator
   v3d_simulator_destroy(device->sim_file);
#endif

   vk_object_base_finish(&device->base);
   mtx_destroy(&device->mutex);
}

void
v3dv_DestroyInstance(VkInstance _instance,
                     const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_instance, instance, _instance);

   if (!instance)
      return;

   if (instance->physicalDeviceCount > 0) {
      /* We support at most one physical device. */
      assert(instance->physicalDeviceCount == 1);
      physical_device_finish(&instance->physicalDevice);
   }

   vk_free(&instance->alloc, (char *)instance->app_info.app_name);
   vk_free(&instance->alloc, (char *)instance->app_info.engine_name);

   VG(VALGRIND_DESTROY_MEMPOOL(instance));

   vk_debug_report_instance_destroy(&instance->debug_report_callbacks);

   glsl_type_singleton_decref();

   vk_object_base_finish(&instance->base);
   vk_free(&instance->alloc, instance);
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

   /* We don't want to burn too much ram with the GPU.  If the user has 4GiB
    * or less, we use at most half.  If they have more than 4GiB, we use 3/4.
    */
   uint64_t available_ram;
   if (total_ram <= 4ull * 1024ull * 1024ull * 1024ull)
      available_ram = total_ram / 2;
   else
      available_ram = total_ram * 3 / 4;

   return available_ram;
}

#if !using_v3d_simulator
#ifdef VK_USE_PLATFORM_XCB_KHR
static int
create_display_fd_xcb(VkIcdSurfaceBase *surface)
{
   int fd = -1;

   xcb_connection_t *conn;
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
   xcb_dri3_open_reply_t *reply;
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
   if (strcmp(interface, "wl_drm") == 0) {
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
      return vk_errorf(device->instance,
                       VK_ERROR_INITIALIZATION_FAILED,
                       "Failed to find build-id");
   }

   unsigned build_id_len = build_id_length(note);
   if (build_id_len < 20) {
      return vk_errorf(device->instance,
                       VK_ERROR_INITIALIZATION_FAILED,
                       "build-id too short.  It needs to be a SHA");
   }

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

static VkResult
physical_device_init(struct v3dv_physical_device *device,
                     struct v3dv_instance *instance,
                     drmDevicePtr drm_render_device,
                     drmDevicePtr drm_primary_device)
{
   VkResult result = VK_SUCCESS;
   int32_t master_fd = -1;

   vk_object_base_init(NULL, &device->base, VK_OBJECT_TYPE_PHYSICAL_DEVICE);
   device->instance = instance;

   assert(drm_render_device);
   const char *path = drm_render_device->nodes[DRM_NODE_RENDER];
   int32_t render_fd = open(path, O_RDWR | O_CLOEXEC);
   if (render_fd < 0)
      return vk_error(instance, VK_ERROR_INCOMPATIBLE_DRIVER);

   /* If we are running on VK_KHR_display we need to acquire the master
    * display device now for the v3dv_wsi_init() call below. For anything else
    * we postpone that until a swapchain is created.
    */

   if (instance->enabled_extensions.KHR_display) {
#if !using_v3d_simulator
      /* Open the primary node on the vc4 display device */
      assert(drm_primary_device);
      const char *primary_path = drm_primary_device->nodes[DRM_NODE_PRIMARY];
      master_fd = open(primary_path, O_RDWR | O_CLOEXEC);
#else
      /* There is only one device with primary and render nodes.
       * Open its primary node.
       */
      const char *primary_path = drm_render_device->nodes[DRM_NODE_PRIMARY];
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
      result = VK_ERROR_INCOMPATIBLE_DRIVER;
      goto fail;
   }

   if (device->devinfo.ver < 42) {
      result = VK_ERROR_INCOMPATIBLE_DRIVER;
      goto fail;
   }

   if (!device_has_expected_features(device)) {
      result = VK_ERROR_INCOMPATIBLE_DRIVER;
      goto fail;
   }

   result = init_uuids(device);
   if (result != VK_SUCCESS)
      goto fail;

   device->compiler = v3d_compiler_init(&device->devinfo);
   device->next_program_id = 0;

   asprintf(&device->name, "V3D %d.%d",
            device->devinfo.ver / 10, device->devinfo.ver % 10);

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

   device->options.merge_jobs = getenv("V3DV_NO_MERGE_JOBS") == NULL;

   result = v3dv_wsi_init(device);
   if (result != VK_SUCCESS) {
      vk_error(instance, result);
      goto fail;
   }

   v3dv_physical_device_get_supported_extensions(device,
                                                 &device->supported_extensions);

   pthread_mutex_init(&device->mutex, NULL);

   return VK_SUCCESS;

fail:
   if (render_fd >= 0)
      close(render_fd);
   if (master_fd >= 0)
      close(master_fd);

   return result;
}

static VkResult
enumerate_devices(struct v3dv_instance *instance)
{
   /* TODO: Check for more devices? */
   drmDevicePtr devices[8];
   VkResult result = VK_ERROR_INCOMPATIBLE_DRIVER;
   int max_devices;

   instance->physicalDeviceCount = 0;

   max_devices = drmGetDevices2(0, devices, ARRAY_SIZE(devices));
   if (max_devices < 1)
      return VK_ERROR_INCOMPATIBLE_DRIVER;

#if !using_v3d_simulator
   int32_t v3d_idx = -1;
   int32_t vc4_idx = -1;
#endif
   for (unsigned i = 0; i < (unsigned)max_devices; i++) {
#if using_v3d_simulator
      /* In the simulator, we look for an Intel render node */
      const int required_nodes = (1 << DRM_NODE_RENDER) | (1 << DRM_NODE_PRIMARY);
      if ((devices[i]->available_nodes & required_nodes) == required_nodes &&
           devices[i]->bustype == DRM_BUS_PCI &&
           devices[i]->deviceinfo.pci->vendor_id == 0x8086) {
         result = physical_device_init(&instance->physicalDevice, instance,
                                       devices[i], NULL);
         if (result != VK_ERROR_INCOMPATIBLE_DRIVER)
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
   if (v3d_idx == -1 || vc4_idx == -1)
      result = VK_ERROR_INCOMPATIBLE_DRIVER;
   else
      result = physical_device_init(&instance->physicalDevice, instance,
                                    devices[v3d_idx], devices[vc4_idx]);
#endif

   drmFreeDevices(devices, max_devices);

   if (result == VK_SUCCESS)
      instance->physicalDeviceCount = 1;

   return result;
}

static VkResult
instance_ensure_physical_device(struct v3dv_instance *instance)
{
   if (instance->physicalDeviceCount < 0) {
      VkResult result = enumerate_devices(instance);
      if (result != VK_SUCCESS &&
          result != VK_ERROR_INCOMPATIBLE_DRIVER)
         return result;
   }

   return VK_SUCCESS;
}

VkResult
v3dv_EnumeratePhysicalDevices(VkInstance _instance,
                              uint32_t *pPhysicalDeviceCount,
                              VkPhysicalDevice *pPhysicalDevices)
{
   V3DV_FROM_HANDLE(v3dv_instance, instance, _instance);
   VK_OUTARRAY_MAKE(out, pPhysicalDevices, pPhysicalDeviceCount);
 
   VkResult result = instance_ensure_physical_device(instance);
   if (result != VK_SUCCESS)
      return result;

   if (instance->physicalDeviceCount == 0)
      return VK_SUCCESS;

   assert(instance->physicalDeviceCount == 1);
   vk_outarray_append(&out, i) {
      *i = v3dv_physical_device_to_handle(&instance->physicalDevice);
   }

   return vk_outarray_status(&out);
}

void
v3dv_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                               VkPhysicalDeviceFeatures *pFeatures)
{
   memset(pFeatures, 0, sizeof(*pFeatures));

   *pFeatures = (VkPhysicalDeviceFeatures) {
      .robustBufferAccess = true, /* This feature is mandatory */
      .fullDrawIndexUint32 = false, /* Only available since V3D 4.4.9.1 */
      .imageCubeArray = true,
      .independentBlend = true,
      .geometryShader = false,
      .tessellationShader = false,
      .sampleRateShading = true,
      .dualSrcBlend = false,
      .logicOp = true,
      .multiDrawIndirect = false,
      .drawIndirectFirstInstance = true,
      .depthClamp = false,
      .depthBiasClamp = false,
      .fillModeNonSolid = true,
      .depthBounds = false, /* Only available since V3D 4.3.16.2 */
      .wideLines = true,
      .largePoints = true,
      .alphaToOne = true,
      .multiViewport = false,
      .samplerAnisotropy = true,
      .textureCompressionETC2 = true,
      .textureCompressionASTC_LDR = false,
      .textureCompressionBC = false,
      .occlusionQueryPrecise = true,
      .pipelineStatisticsQuery = false,
      .vertexPipelineStoresAndAtomics = true,
      .fragmentStoresAndAtomics = true,
      .shaderTessellationAndGeometryPointSize = false,
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

void
v3dv_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                VkPhysicalDeviceFeatures2 *pFeatures)
{
   v3dv_GetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);

   vk_foreach_struct(ext, pFeatures->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES_EXT: {
         VkPhysicalDevicePrivateDataFeaturesEXT *features =
            (VkPhysicalDevicePrivateDataFeaturesEXT *)ext;
         features->privateData = true;
         break;
      }

      default:
         v3dv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

uint32_t
v3dv_physical_device_vendor_id(struct v3dv_physical_device *dev)
{
   return 0x14E4; /* Broadcom */
}


#if using_v3d_simulator
static bool
get_i915_param(int fd, uint32_t param, int *value)
{
   int tmp;

   struct drm_i915_getparam gp = {
      .param = param,
      .value = &tmp,
   };

   int ret = drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
   if (ret != 0)
      return false;

   *value = tmp;
   return true;
}
#endif

uint32_t
v3dv_physical_device_device_id(struct v3dv_physical_device *dev)
{
#if using_v3d_simulator
   int devid = 0;

   if (!get_i915_param(dev->render_fd, I915_PARAM_CHIPSET_ID, &devid))
      fprintf(stderr, "Error getting device_id\n");

   return devid;
#else
   return dev->devinfo.ver;
#endif
}

void
v3dv_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                 VkPhysicalDeviceProperties *pProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physicalDevice);

   const uint32_t page_size = 4096;
   const uint32_t mem_size = compute_heap_size();

   /* Per-stage limits */
   const uint32_t max_samplers = 16;
   const uint32_t max_uniform_buffers = 12;
   const uint32_t max_storage_buffers = 12;
   const uint32_t max_dynamic_storage_buffers = 6;
   const uint32_t max_sampled_images = 16;
   const uint32_t max_storage_images = 4;
   const uint32_t max_input_attachments = 4;
   assert(max_sampled_images + max_storage_images + max_input_attachments
          <= V3D_MAX_TEXTURE_SAMPLERS);

   const uint32_t max_varying_components = 16 * 4;
   const uint32_t max_render_targets = 4;

   const uint32_t v3d_coord_shift = 6;

   const uint32_t v3d_point_line_granularity = 2.0f / (1 << v3d_coord_shift);
   const uint32_t max_fb_size = 4096;

   const VkSampleCountFlags supported_sample_counts =
      VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;

   struct timespec clock_res;
   clock_getres(CLOCK_MONOTONIC, &clock_res);
   const float timestamp_period =
      clock_res.tv_sec * 1000000000.0f + clock_res.tv_nsec;

   /* FIXME: this will probably require an in-depth review */
   VkPhysicalDeviceLimits limits = {
      .maxImageDimension1D                      = 4096,
      .maxImageDimension2D                      = 4096,
      .maxImageDimension3D                      = 4096,
      .maxImageDimensionCube                    = 4096,
      .maxImageArrayLayers                      = 2048,
      .maxTexelBufferElements                   = (1ul << 28),
      .maxUniformBufferRange                    = (1ul << 27),
      .maxStorageBufferRange                    = (1ul << 27),
      .maxPushConstantsSize                     = MAX_PUSH_CONSTANTS_SIZE,
      .maxMemoryAllocationCount                 = mem_size / page_size,
      .maxSamplerAllocationCount                = 64 * 1024,
      .bufferImageGranularity                   = 256, /* A cache line */
      .sparseAddressSpaceSize                   = 0,
      .maxBoundDescriptorSets                   = MAX_SETS,
      .maxPerStageDescriptorSamplers            = max_samplers,
      .maxPerStageDescriptorUniformBuffers      = max_uniform_buffers,
      .maxPerStageDescriptorStorageBuffers      = max_storage_buffers,
      .maxPerStageDescriptorSampledImages       = max_sampled_images,
      .maxPerStageDescriptorStorageImages       = max_storage_images,
      .maxPerStageDescriptorInputAttachments    = max_input_attachments,
      .maxPerStageResources                     = 128,

      /* We multiply some limits by 6 to account for all shader stages */
      .maxDescriptorSetSamplers                 = 6 * max_samplers,
      .maxDescriptorSetUniformBuffers           = 6 * max_uniform_buffers,
      .maxDescriptorSetUniformBuffersDynamic    = 8,
      .maxDescriptorSetStorageBuffers           = 6 * max_storage_buffers,
      .maxDescriptorSetStorageBuffersDynamic    = 6 * max_dynamic_storage_buffers,
      .maxDescriptorSetSampledImages            = 6 * max_sampled_images,
      .maxDescriptorSetStorageImages            = 6 * max_storage_images,
      .maxDescriptorSetInputAttachments         = 4,

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
      .maxGeometryShaderInvocations             = 0,
      .maxGeometryInputComponents               = 0,
      .maxGeometryOutputComponents              = 0,
      .maxGeometryOutputVertices                = 0,
      .maxGeometryTotalOutputComponents         = 0,

      /* Fragment limits */
      .maxFragmentInputComponents               = max_varying_components,
      .maxFragmentOutputAttachments             = 4,
      .maxFragmentDualSrcAttachments            = 0,
      .maxFragmentCombinedOutputResources       = max_render_targets +
                                                  max_storage_buffers +
                                                  max_storage_images,

      /* Compute limits */
      .maxComputeSharedMemorySize               = 16384,
      .maxComputeWorkGroupCount                 = { 65535, 65535, 65535 },
      .maxComputeWorkGroupInvocations           = 256,
      .maxComputeWorkGroupSize                  = { 256, 256, 256 },

      .subPixelPrecisionBits                    = v3d_coord_shift,
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
      .minTexelBufferOffsetAlignment            = VC5_UIFBLOCK_SIZE,
      .minUniformBufferOffsetAlignment          = 32,
      .minStorageBufferOffsetAlignment          = 32,
      .minTexelOffset                           = -8,
      .maxTexelOffset                           = 7,
      .minTexelGatherOffset                     = -8,
      .maxTexelGatherOffset                     = 7,
      .minInterpolationOffset                   = -0.5,
      .maxInterpolationOffset                   = 0.5,
      .subPixelInterpolationOffsetBits          = v3d_coord_shift,
      .maxFramebufferWidth                      = max_fb_size,
      .maxFramebufferHeight                     = max_fb_size,
      .maxFramebufferLayers                     = 256,
      .framebufferColorSampleCounts             = supported_sample_counts,
      .framebufferDepthSampleCounts             = supported_sample_counts,
      .framebufferStencilSampleCounts           = supported_sample_counts,
      .framebufferNoAttachmentsSampleCounts     = supported_sample_counts,
      .maxColorAttachments                      = max_render_targets,
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
      .nonCoherentAtomSize                      = 256,
   };

   *pProperties = (VkPhysicalDeviceProperties) {
      .apiVersion = v3dv_physical_device_api_version(pdevice),
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

void
v3dv_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                  VkPhysicalDeviceProperties2 *pProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physicalDevice);

   v3dv_GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);

   vk_foreach_struct(ext, pProperties->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
         VkPhysicalDeviceIDProperties *id_props =
            (VkPhysicalDeviceIDProperties *)ext;
         memcpy(id_props->deviceUUID, pdevice->device_uuid, VK_UUID_SIZE);
         memcpy(id_props->driverUUID, pdevice->driver_uuid, VK_UUID_SIZE);
         /* The LUID is for Windows. */
         id_props->deviceLUIDValid = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT:
         /* Do nothing, not even logging. This is a non-PCI device, so we will
          * never provide this extension.
          */
         break;
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

void
v3dv_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice,
                                            uint32_t *pCount,
                                            VkQueueFamilyProperties *pQueueFamilyProperties)
{
   VK_OUTARRAY_MAKE(out, pQueueFamilyProperties, pCount);

   vk_outarray_append(&out, p) {
      *p = v3dv_queue_family_properties;
   }
}

void
v3dv_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice,
                                             uint32_t *pQueueFamilyPropertyCount,
                                             VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   VK_OUTARRAY_MAKE(out, pQueueFamilyProperties, pQueueFamilyPropertyCount);

   vk_outarray_append(&out, p) {
      p->queueFamilyProperties = v3dv_queue_family_properties;

      vk_foreach_struct(s, p->pNext) {
         v3dv_debug_ignored_stype(s->sType);
      }
   }
}

void
v3dv_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                       VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, device, physicalDevice);
   *pMemoryProperties = device->memory;
}

void
v3dv_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice,
                                        VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   v3dv_GetPhysicalDeviceMemoryProperties(physicalDevice,
                                          &pMemoryProperties->memoryProperties);

   vk_foreach_struct(ext, pMemoryProperties->pNext) {
      switch (ext->sType) {
      default:
         v3dv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

PFN_vkVoidFunction
v3dv_GetInstanceProcAddr(VkInstance _instance,
                         const char *pName)
{
   V3DV_FROM_HANDLE(v3dv_instance, instance, _instance);

   /* The Vulkan 1.0 spec for vkGetInstanceProcAddr has a table of exactly
    * when we have to return valid function pointers, NULL, or it's left
    * undefined.  See the table for exact details.
    */
   if (pName == NULL)
      return NULL;

#define LOOKUP_V3DV_ENTRYPOINT(entrypoint)              \
   if (strcmp(pName, "vk" #entrypoint) == 0)            \
      return (PFN_vkVoidFunction)v3dv_##entrypoint

   LOOKUP_V3DV_ENTRYPOINT(EnumerateInstanceExtensionProperties);
   LOOKUP_V3DV_ENTRYPOINT(CreateInstance);

#undef LOOKUP_V3DV_ENTRYPOINT

   if (instance == NULL)
      return NULL;

   int idx = v3dv_get_instance_entrypoint_index(pName);
   if (idx >= 0)
      return instance->dispatch.entrypoints[idx];

   idx = v3dv_get_physical_device_entrypoint_index(pName);
   if (idx >= 0)
      return instance->physicalDevice.dispatch.entrypoints[idx];

   idx = v3dv_get_device_entrypoint_index(pName);
   if (idx >= 0)
      return instance->device_dispatch.entrypoints[idx];

   return NULL;
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

PFN_vkVoidFunction
v3dv_GetDeviceProcAddr(VkDevice _device,
                       const char *pName)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);

   if (!device || !pName)
      return NULL;

   int idx = v3dv_get_device_entrypoint_index(pName);
   if (idx < 0)
      return NULL;

   return device->dispatch.entrypoints[idx];
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

   if (!pName || !instance)
      return NULL;

   int idx = v3dv_get_physical_device_entrypoint_index(pName);
   if (idx < 0)
      return NULL;

   return instance->physicalDevice.dispatch.entrypoints[idx];
}

VkResult
v3dv_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                        const char *pLayerName,
                                        uint32_t *pPropertyCount,
                                        VkExtensionProperties *pProperties)
{
   /* We don't support any layers */
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   V3DV_FROM_HANDLE(v3dv_physical_device, device, physicalDevice);
   VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);

   for (int i = 0; i < V3DV_DEVICE_EXTENSION_COUNT; i++) {
      if (device->supported_extensions.extensions[i]) {
         vk_outarray_append(&out, prop) {
            *prop = v3dv_device_extensions[i];
         }
      }
   }

   return vk_outarray_status(&out);
}

VkResult
v3dv_EnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                      VkLayerProperties *pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}

VkResult
v3dv_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                    uint32_t *pPropertyCount,
                                    VkLayerProperties *pProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, physical_device, physicalDevice);

   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   return vk_error(physical_device->instance, VK_ERROR_LAYER_NOT_PRESENT);
}

static VkResult
queue_init(struct v3dv_device *device, struct v3dv_queue *queue)
{
   vk_object_base_init(&device->vk, &queue->base, VK_OBJECT_TYPE_QUEUE);
   queue->device = device;
   queue->flags = 0;
   queue->noop_job = NULL;
   list_inithead(&queue->submit_wait_list);
   pthread_mutex_init(&queue->mutex, NULL);
   return VK_SUCCESS;
}

static void
queue_finish(struct v3dv_queue *queue)
{
   vk_object_base_finish(&queue->base);
   assert(list_is_empty(&queue->submit_wait_list));
   if (queue->noop_job)
      v3dv_job_destroy(queue->noop_job);
   pthread_mutex_destroy(&queue->mutex);
}

static void
init_device_dispatch(struct v3dv_device *device)
{
   for (unsigned i = 0; i < ARRAY_SIZE(device->dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have not been
       * enabled must not be advertised.
       */
      if (!v3dv_device_entrypoint_is_enabled(i, device->instance->app_info.api_version,
                                             &device->instance->enabled_extensions,
                                             &device->enabled_extensions)) {
         device->dispatch.entrypoints[i] = NULL;
      } else {
         device->dispatch.entrypoints[i] =
            v3dv_device_dispatch_table.entrypoints[i];
      }
   }
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

VkResult
v3dv_CreateDevice(VkPhysicalDevice physicalDevice,
                  const VkDeviceCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkDevice *pDevice)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, physical_device, physicalDevice);
   struct v3dv_instance *instance = physical_device->instance;
   VkResult result;
   struct v3dv_device *device;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);

   /* Check enabled extensions */
   struct v3dv_device_extension_table enabled_extensions = { };
   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      int idx;
      for (idx = 0; idx < V3DV_DEVICE_EXTENSION_COUNT; idx++) {
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    v3dv_device_extensions[idx].extensionName) == 0)
            break;
      }

      if (idx >= V3DV_DEVICE_EXTENSION_COUNT)
         return vk_error(instance, VK_ERROR_EXTENSION_NOT_PRESENT);

      if (!physical_device->supported_extensions.extensions[idx])
         return vk_error(instance, VK_ERROR_EXTENSION_NOT_PRESENT);

      enabled_extensions.extensions[idx] = true;
   }

   /* Check enabled features */
   if (pCreateInfo->pEnabledFeatures) {
      VkPhysicalDeviceFeatures supported_features;
      v3dv_GetPhysicalDeviceFeatures(physicalDevice, &supported_features);
      VkBool32 *supported_feature = (VkBool32 *)&supported_features;
      VkBool32 *enabled_feature = (VkBool32 *)pCreateInfo->pEnabledFeatures;
      unsigned num_features = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
      for (uint32_t i = 0; i < num_features; i++) {
         if (enabled_feature[i] && !supported_feature[i])
            return vk_error(instance, VK_ERROR_FEATURE_NOT_PRESENT);
      }
   }

   /* Check requested queues (we only expose one queue ) */
   assert(pCreateInfo->queueCreateInfoCount == 1);
   for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      assert(pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex == 0);
      assert(pCreateInfo->pQueueCreateInfos[i].queueCount == 1);
      if (pCreateInfo->pQueueCreateInfos[i].flags != 0)
         return vk_error(instance, VK_ERROR_INITIALIZATION_FAILED);
   }

   device = vk_zalloc2(&physical_device->instance->alloc, pAllocator,
                       sizeof(*device), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_device_init(&device->vk, pCreateInfo,
                  &physical_device->instance->alloc, pAllocator);

   device->instance = instance;
   device->pdevice = physical_device;

   if (pAllocator)
      device->vk.alloc = *pAllocator;
   else
      device->vk.alloc = physical_device->instance->alloc;

   pthread_mutex_init(&device->mutex, NULL);

   result = queue_init(device, &device->queue);
   if (result != VK_SUCCESS)
      goto fail;

   device->devinfo = physical_device->devinfo;
   device->enabled_extensions = enabled_extensions;

   if (pCreateInfo->pEnabledFeatures) {
      memcpy(&device->features, pCreateInfo->pEnabledFeatures,
             sizeof(device->features));
   }

   int ret = drmSyncobjCreate(physical_device->render_fd,
                              DRM_SYNCOBJ_CREATE_SIGNALED,
                              &device->last_job_sync);
   if (ret) {
      result = VK_ERROR_INITIALIZATION_FAILED;
      goto fail;
   }

   init_device_dispatch(device);
   init_device_meta(device);
   v3dv_bo_cache_init(device);
   v3dv_pipeline_cache_init(&device->default_pipeline_cache, device,
                            device->instance->default_pipeline_cache_enabled);

   *pDevice = v3dv_device_to_handle(device);

   return VK_SUCCESS;

fail:
   vk_free(&device->vk.alloc, device);

   return result;
}

void
v3dv_DestroyDevice(VkDevice _device,
                   const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);

   v3dv_DeviceWaitIdle(_device);
   queue_finish(&device->queue);
   pthread_mutex_destroy(&device->mutex);
   drmSyncobjDestroy(device->pdevice->render_fd, device->last_job_sync);
   destroy_device_meta(device);
   v3dv_pipeline_cache_finish(&device->default_pipeline_cache);

   /* Bo cache should be removed the last, as any other object could be
    * freeing their private bos
    */
   v3dv_bo_cache_destroy(device);

   vk_free2(&default_alloc, pAllocator, device);
}

void
v3dv_GetDeviceQueue(VkDevice _device,
                    uint32_t queueFamilyIndex,
                    uint32_t queueIndex,
                    VkQueue *pQueue)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);

   assert(queueIndex == 0);
   assert(queueFamilyIndex == 0);

   *pQueue = v3dv_queue_to_handle(&device->queue);
}

VkResult
v3dv_DeviceWaitIdle(VkDevice _device)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   return v3dv_QueueWaitIdle(v3dv_queue_to_handle(&device->queue));
}

VkResult
v3dv_CreateDebugReportCallbackEXT(VkInstance _instance,
                                 const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkDebugReportCallbackEXT* pCallback)
{
   V3DV_FROM_HANDLE(v3dv_instance, instance, _instance);
   return vk_create_debug_report_callback(&instance->debug_report_callbacks,
                                          pCreateInfo, pAllocator, &instance->alloc,
                                          pCallback);
}

void
v3dv_DestroyDebugReportCallbackEXT(VkInstance _instance,
                                  VkDebugReportCallbackEXT _callback,
                                  const VkAllocationCallbacks* pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_instance, instance, _instance);
   vk_destroy_debug_report_callback(&instance->debug_report_callbacks,
                                    _callback, pAllocator, &instance->alloc);
}

static VkResult
device_alloc(struct v3dv_device *device,
             struct v3dv_device_memory *mem,
             VkDeviceSize size)
{
   /* Our kernel interface is 32-bit */
   if (size > UINT32_MAX)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

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
   v3dv_ioctl(display_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
}

static void
device_free(struct v3dv_device *device, struct v3dv_device_memory *mem)
{
   /* If this memory allocation was for WSI, then we need to use the
    * display device to free the allocated dumb BO.
    */
   if (mem->is_for_wsi) {
      assert(mem->has_bo_ownership);
      device_free_wsi_dumb(device->instance->physicalDevice.display_fd,
                           mem->bo->dumb_handle);
   }

   if (mem->has_bo_ownership)
      v3dv_bo_free(device, mem->bo);
   else if (mem->bo)
      vk_free(&device->vk.alloc, mem->bo);
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
   VkResult result;

   *bo = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(struct v3dv_bo), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (*bo == NULL) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   off_t real_size = lseek(fd, 0, SEEK_END);
   lseek(fd, 0, SEEK_SET);
   if (real_size < 0 || (uint64_t) real_size < size) {
      result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
      goto fail;
   }

   int render_fd = device->pdevice->render_fd;
   assert(render_fd >= 0);

   int ret;
   uint32_t handle;
   ret = drmPrimeFDToHandle(render_fd, fd, &handle);
   if (ret) {
      result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
      goto fail;
   }

   struct drm_v3d_get_bo_offset get_offset = {
      .handle = handle,
   };
   ret = v3dv_ioctl(render_fd, DRM_IOCTL_V3D_GET_BO_OFFSET, &get_offset);
   if (ret) {
      result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
      goto fail;
   }
   assert(get_offset.offset != 0);

   v3dv_bo_init(*bo, handle, size, get_offset.offset, "import", false);

   return VK_SUCCESS;

fail:
   if (*bo) {
      vk_free2(&device->vk.alloc, pAllocator, *bo);
      *bo = NULL;
   }
   return result;
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
   struct v3dv_physical_device *pdevice = &device->instance->physicalDevice;
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

VkResult
v3dv_AllocateMemory(VkDevice _device,
                    const VkMemoryAllocateInfo *pAllocateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkDeviceMemory *pMem)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_device_memory *mem;
   struct v3dv_physical_device *pdevice = &device->instance->physicalDevice;

   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

   /* The Vulkan 1.0.33 spec says "allocationSize must be greater than 0". */
   assert(pAllocateInfo->allocationSize > 0);

   mem = vk_object_zalloc(&device->vk, pAllocator, sizeof(*mem),
                          VK_OBJECT_TYPE_DEVICE_MEMORY);
   if (mem == NULL)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   assert(pAllocateInfo->memoryTypeIndex < pdevice->memory.memoryTypeCount);
   mem->type = &pdevice->memory.memoryTypes[pAllocateInfo->memoryTypeIndex];
   mem->has_bo_ownership = true;
   mem->is_for_wsi = false;

   const struct wsi_memory_allocate_info *wsi_info = NULL;
   const VkImportMemoryFdInfoKHR *fd_info = NULL;
   vk_foreach_struct_const(ext, pAllocateInfo->pNext) {
      switch ((unsigned)ext->sType) {
      case VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA:
         wsi_info = (void *)ext;
         break;
      case VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR:
         fd_info = (void *)ext;
         break;
      default:
         v3dv_debug_ignored_stype(ext->sType);
         break;
      }
   }

   VkResult result = VK_SUCCESS;
   if (wsi_info) {
      result = device_alloc_for_wsi(device, pAllocator, mem,
                                    pAllocateInfo->allocationSize);
   } else if (fd_info && fd_info->handleType) {
      assert(fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
             fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
      result = device_import_bo(device, pAllocator,
                                fd_info->fd, pAllocateInfo->allocationSize,
                                &mem->bo);
      mem->has_bo_ownership = false;
      if (result == VK_SUCCESS)
         close(fd_info->fd);
   } else {
      result = device_alloc(device, mem, pAllocateInfo->allocationSize);
   }

   if (result != VK_SUCCESS) {
      vk_object_free(&device->vk, pAllocator, mem);
      return vk_error(device->instance, result);
   }

   *pMem = v3dv_device_memory_to_handle(mem);
   return result;
}

void
v3dv_FreeMemory(VkDevice _device,
                VkDeviceMemory _mem,
                const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_device_memory, mem, _mem);

   if (mem == NULL)
      return;

   if (mem->bo->map)
      v3dv_UnmapMemory(_device, _mem);

   device_free(device, mem);

   vk_object_free(&device->vk, pAllocator, mem);
}

VkResult
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
      return vk_error(device->instance, result);

   *ppData = ((uint8_t *) mem->bo->map) + offset;
   return VK_SUCCESS;
}

void
v3dv_UnmapMemory(VkDevice _device,
                 VkDeviceMemory _memory)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_device_memory, mem, _memory);

   if (mem == NULL)
      return;

   device_unmap(device, mem);
}

VkResult
v3dv_FlushMappedMemoryRanges(VkDevice _device,
                             uint32_t memoryRangeCount,
                             const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VkResult
v3dv_InvalidateMappedMemoryRanges(VkDevice _device,
                                  uint32_t memoryRangeCount,
                                  const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

void
v3dv_GetImageMemoryRequirements(VkDevice _device,
                                VkImage _image,
                                VkMemoryRequirements *pMemoryRequirements)
{
   V3DV_FROM_HANDLE(v3dv_image, image, _image);

   assert(image->size > 0);

   pMemoryRequirements->size = image->size;
   pMemoryRequirements->alignment = image->alignment;
   pMemoryRequirements->memoryTypeBits = 0x1;
}

VkResult
v3dv_BindImageMemory(VkDevice _device,
                     VkImage _image,
                     VkDeviceMemory _memory,
                     VkDeviceSize memoryOffset)
{
   V3DV_FROM_HANDLE(v3dv_device_memory, mem, _memory);
   V3DV_FROM_HANDLE(v3dv_image, image, _image);

   /* Valid usage:
    *
    *   "memoryOffset must be an integer multiple of the alignment member of
    *    the VkMemoryRequirements structure returned from a call to
    *    vkGetImageMemoryRequirements with image"
    */
   assert(memoryOffset % image->alignment == 0);
   assert(memoryOffset < mem->bo->size);

   image->mem = mem;
   image->mem_offset = memoryOffset;

   return VK_SUCCESS;
}

void
v3dv_GetBufferMemoryRequirements(VkDevice _device,
                                 VkBuffer _buffer,
                                 VkMemoryRequirements* pMemoryRequirements)
{
   V3DV_FROM_HANDLE(v3dv_buffer, buffer, _buffer);

   pMemoryRequirements->memoryTypeBits = 0x1;
   pMemoryRequirements->alignment = buffer->alignment;
   pMemoryRequirements->size =
      align64(buffer->size, pMemoryRequirements->alignment);
}

VkResult
v3dv_BindBufferMemory(VkDevice _device,
                      VkBuffer _buffer,
                      VkDeviceMemory _memory,
                      VkDeviceSize memoryOffset)
{
   V3DV_FROM_HANDLE(v3dv_device_memory, mem, _memory);
   V3DV_FROM_HANDLE(v3dv_buffer, buffer, _buffer);

   /* Valid usage:
    *
    *   "memoryOffset must be an integer multiple of the alignment member of
    *    the VkMemoryRequirements structure returned from a call to
    *    vkGetBufferMemoryRequirements with buffer"
    */
   assert(memoryOffset % buffer->alignment == 0);
   assert(memoryOffset < mem->bo->size);

   buffer->mem = mem;
   buffer->mem_offset = memoryOffset;

   return VK_SUCCESS;
}

VkResult
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   buffer->size = pCreateInfo->size;
   buffer->usage = pCreateInfo->usage;
   buffer->alignment = 256; /* nonCoherentAtomSize */

   /* Limit allocations to 32-bit */
   const VkDeviceSize aligned_size = align64(buffer->size, buffer->alignment);
   if (aligned_size > UINT32_MAX || aligned_size < buffer->size)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   *pBuffer = v3dv_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

void
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

/**
 * This computes the maximum bpp used by any of the render targets used by
 * a particular subpass and checks if any of those render targets are
 * multisampled. If we don't have a subpass (when we are not inside a
 * render pass), then we assume that all framebuffer attachments are used.
 */
void
v3dv_framebuffer_compute_internal_bpp_msaa(
   const struct v3dv_framebuffer *framebuffer,
   const struct v3dv_subpass *subpass,
   uint8_t *max_bpp,
   bool *msaa)
{
   STATIC_ASSERT(RENDER_TARGET_MAXIMUM_32BPP == 0);
   *max_bpp = RENDER_TARGET_MAXIMUM_32BPP;
   *msaa = false;

   if (subpass) {
      for (uint32_t i = 0; i < subpass->color_count; i++) {
         uint32_t att_idx = subpass->color_attachments[i].attachment;
         if (att_idx == VK_ATTACHMENT_UNUSED)
            continue;

         const struct v3dv_image_view *att = framebuffer->attachments[att_idx];
         assert(att);

         if (att->aspects & VK_IMAGE_ASPECT_COLOR_BIT)
            *max_bpp = MAX2(*max_bpp, att->internal_bpp);

         if (att->image->samples > VK_SAMPLE_COUNT_1_BIT)
            *msaa = true;
      }

      if (!*msaa && subpass->ds_attachment.attachment != VK_ATTACHMENT_UNUSED) {
         const struct v3dv_image_view *att =
            framebuffer->attachments[subpass->ds_attachment.attachment];
         assert(att);

         if (att->image->samples > VK_SAMPLE_COUNT_1_BIT)
            *msaa = true;
      }

      return;
   }

   assert(framebuffer->attachment_count <= 4);
   for (uint32_t i = 0; i < framebuffer->attachment_count; i++) {
      const struct v3dv_image_view *att = framebuffer->attachments[i];
      assert(att);

      if (att->aspects & VK_IMAGE_ASPECT_COLOR_BIT)
         *max_bpp = MAX2(*max_bpp, att->internal_bpp);

      if (att->image->samples > VK_SAMPLE_COUNT_1_BIT)
         *msaa = true;
   }

   return;
}

VkResult
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   framebuffer->width = pCreateInfo->width;
   framebuffer->height = pCreateInfo->height;
   framebuffer->layers = pCreateInfo->layers;
   framebuffer->has_edge_padding = true;

   framebuffer->attachment_count = pCreateInfo->attachmentCount;
   framebuffer->color_attachment_count = 0;
   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
      framebuffer->attachments[i] =
         v3dv_image_view_from_handle(pCreateInfo->pAttachments[i]);
      if (framebuffer->attachments[i]->aspects & VK_IMAGE_ASPECT_COLOR_BIT)
         framebuffer->color_attachment_count++;
   }

   *pFramebuffer = v3dv_framebuffer_to_handle(framebuffer);

   return VK_SUCCESS;
}

void
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

VkResult
v3dv_GetMemoryFdPropertiesKHR(VkDevice _device,
                              VkExternalMemoryHandleTypeFlagBits handleType,
                              int fd,
                              VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_physical_device *pdevice = &device->instance->physicalDevice;

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      pMemoryFdProperties->memoryTypeBits =
         (1 << pdevice->memory.memoryTypeCount) - 1;
      return VK_SUCCESS;
   default:
      return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
}

VkResult
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   *pFd = fd;

   return VK_SUCCESS;
}

VkResult
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Events are created in the unsignaled state */
   event->state = false;
   *pEvent = v3dv_event_to_handle(event);

   return VK_SUCCESS;
}

void
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

VkResult
v3dv_GetEventStatus(VkDevice _device, VkEvent _event)
{
   V3DV_FROM_HANDLE(v3dv_event, event, _event);
   return p_atomic_read(&event->state) ? VK_EVENT_SET : VK_EVENT_RESET;
}

VkResult
v3dv_SetEvent(VkDevice _device, VkEvent _event)
{
   V3DV_FROM_HANDLE(v3dv_event, event, _event);
   p_atomic_set(&event->state, 1);
   return VK_SUCCESS;
}

VkResult
v3dv_ResetEvent(VkDevice _device, VkEvent _event)
{
   V3DV_FROM_HANDLE(v3dv_event, event, _event);
   p_atomic_set(&event->state, 0);
   return VK_SUCCESS;
}

static const enum V3DX(Wrap_Mode) vk_to_v3d_wrap_mode[] = {
   [VK_SAMPLER_ADDRESS_MODE_REPEAT]          = V3D_WRAP_MODE_REPEAT,
   [VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT] = V3D_WRAP_MODE_MIRROR,
   [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE]   = V3D_WRAP_MODE_CLAMP,
   [VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE] = V3D_WRAP_MODE_MIRROR_ONCE,
   [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER] = V3D_WRAP_MODE_BORDER,
};

static const enum V3DX(Compare_Function)
vk_to_v3d_compare_func[] = {
   [VK_COMPARE_OP_NEVER]                        = V3D_COMPARE_FUNC_NEVER,
   [VK_COMPARE_OP_LESS]                         = V3D_COMPARE_FUNC_LESS,
   [VK_COMPARE_OP_EQUAL]                        = V3D_COMPARE_FUNC_EQUAL,
   [VK_COMPARE_OP_LESS_OR_EQUAL]                = V3D_COMPARE_FUNC_LEQUAL,
   [VK_COMPARE_OP_GREATER]                      = V3D_COMPARE_FUNC_GREATER,
   [VK_COMPARE_OP_NOT_EQUAL]                    = V3D_COMPARE_FUNC_NOTEQUAL,
   [VK_COMPARE_OP_GREATER_OR_EQUAL]             = V3D_COMPARE_FUNC_GEQUAL,
   [VK_COMPARE_OP_ALWAYS]                       = V3D_COMPARE_FUNC_ALWAYS,
};

static void
pack_sampler_state(struct v3dv_sampler *sampler,
                   const VkSamplerCreateInfo *pCreateInfo)
{
   enum V3DX(Border_Color_Mode) border_color_mode;

   /* For now we only support the preset Vulkan border color modes. If we
    * want to implement VK_EXT_custom_border_color in the future we would have
    * to use V3D_BORDER_COLOR_FOLLOWS, and fill up border_color_word_[0/1/2/3]
    * SAMPLER_STATE.
    */
   switch (pCreateInfo->borderColor) {
   case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
   case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
      border_color_mode = V3D_BORDER_COLOR_0000;
      break;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
   case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
      border_color_mode = V3D_BORDER_COLOR_0001;
      break;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
   case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
      border_color_mode = V3D_BORDER_COLOR_1111;
      break;
   default:
      unreachable("Unknown border color");
      break;
   }

   /* For some texture formats, when clamping to transparent black border the
    * CTS expects alpha to be set to 1 instead of 0, but the border color mode
    * will take priority over the texture state swizzle, so the only way to
    * fix that is to apply a swizzle in the shader. Here we keep track of
    * whether we are activating that mode and we will decide if we need to
    * activate the texture swizzle lowering in the shader key at compile time
    * depending on the actual texture format.
    */
   if ((pCreateInfo->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
        pCreateInfo->addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
        pCreateInfo->addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER) &&
       border_color_mode == V3D_BORDER_COLOR_0000) {
      sampler->clamp_to_transparent_black_border = true;
   }

   v3dv_pack(sampler->sampler_state, SAMPLER_STATE, s) {
      if (pCreateInfo->anisotropyEnable) {
         s.anisotropy_enable = true;
         if (pCreateInfo->maxAnisotropy > 8)
            s.maximum_anisotropy = 3;
         else if (pCreateInfo->maxAnisotropy > 4)
            s.maximum_anisotropy = 2;
         else if (pCreateInfo->maxAnisotropy > 2)
            s.maximum_anisotropy = 1;
      }

      s.border_color_mode = border_color_mode;

      s.wrap_i_border = false; /* Also hardcoded on v3d */
      s.wrap_s = vk_to_v3d_wrap_mode[pCreateInfo->addressModeU];
      s.wrap_t = vk_to_v3d_wrap_mode[pCreateInfo->addressModeV];
      s.wrap_r = vk_to_v3d_wrap_mode[pCreateInfo->addressModeW];
      s.fixed_bias = pCreateInfo->mipLodBias;
      s.max_level_of_detail = MIN2(MAX2(0, pCreateInfo->maxLod), 15);
      s.min_level_of_detail = MIN2(MAX2(0, pCreateInfo->minLod), 15);
      s.srgb_disable = 0; /* Not even set by v3d */
      s.depth_compare_function =
         vk_to_v3d_compare_func[pCreateInfo->compareEnable ?
                                pCreateInfo->compareOp : VK_COMPARE_OP_NEVER];
      s.mip_filter_nearest = pCreateInfo->mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST;
      s.min_filter_nearest = pCreateInfo->minFilter == VK_FILTER_NEAREST;
      s.mag_filter_nearest = pCreateInfo->magFilter == VK_FILTER_NEAREST;
   }
}

VkResult
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   sampler->compare_enable = pCreateInfo->compareEnable;
   sampler->unnormalized_coordinates = pCreateInfo->unnormalizedCoordinates;
   pack_sampler_state(sampler, pCreateInfo);

   *pSampler = v3dv_sampler_to_handle(sampler);

   return VK_SUCCESS;
}

void
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

void
v3dv_GetDeviceMemoryCommitment(VkDevice device,
                               VkDeviceMemory memory,
                               VkDeviceSize *pCommittedMemoryInBytes)
{
   *pCommittedMemoryInBytes = 0;
}

void
v3dv_GetImageSparseMemoryRequirements(
   VkDevice device,
   VkImage image,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements *pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0;
}

void
v3dv_GetImageSparseMemoryRequirements2(
   VkDevice device,
   const VkImageSparseMemoryRequirementsInfo2 *pInfo,
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
    */
   *pSupportedVersion = MIN2(*pSupportedVersion, 3u);
   return VK_SUCCESS;
}

VkResult
v3dv_CreatePrivateDataSlotEXT(VkDevice _device,
                            const VkPrivateDataSlotCreateInfoEXT* pCreateInfo,
                            const VkAllocationCallbacks* pAllocator,
                            VkPrivateDataSlotEXT* pPrivateDataSlot)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   return vk_private_data_slot_create(&device->vk,
                                      pCreateInfo,
                                      pAllocator,
                                      pPrivateDataSlot);
}

void
v3dv_DestroyPrivateDataSlotEXT(VkDevice _device,
                             VkPrivateDataSlotEXT privateDataSlot,
                             const VkAllocationCallbacks* pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   vk_private_data_slot_destroy(&device->vk, privateDataSlot, pAllocator);
}

VkResult
v3dv_SetPrivateDataEXT(VkDevice _device,
                     VkObjectType objectType,
                     uint64_t objectHandle,
                     VkPrivateDataSlotEXT privateDataSlot,
                     uint64_t data)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   return vk_object_base_set_private_data(&device->vk,
                                          objectType,
                                          objectHandle,
                                          privateDataSlot,
                                          data);
}

void
v3dv_GetPrivateDataEXT(VkDevice _device,
                     VkObjectType objectType,
                     uint64_t objectHandle,
                     VkPrivateDataSlotEXT privateDataSlot,
                     uint64_t* pData)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   vk_object_base_get_private_data(&device->vk,
                                   objectType,
                                   objectHandle,
                                   privateDataSlot,
                                   pData);
}
