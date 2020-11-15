/*
 * Copyright © 2017 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "util/macros.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <math.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "drm-uapi/drm_fourcc.h"
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
#include <xcb/randr.h>
#include <X11/Xlib-xcb.h>
#endif
#include "util/hash_table.h"
#include "util/list.h"

#include "vk_util.h"
#include "wsi_common_private.h"
#include "wsi_common_display.h"
#include "wsi_common_queue.h"

#if 0
#define wsi_display_debug(...) fprintf(stderr, __VA_ARGS__)
#define wsi_display_debug_code(...)     __VA_ARGS__
#else
#define wsi_display_debug(...)
#define wsi_display_debug_code(...)
#endif

/* These have lifetime equal to the instance, so they effectively
 * never go away. This means we must keep track of them separately
 * from all other resources.
 */
typedef struct wsi_display_mode {
   struct list_head             list;
   struct wsi_display_connector *connector;
   bool                         valid; /* was found in most recent poll */
   bool                         preferred;
   uint32_t                     clock; /* in kHz */
   uint16_t                     hdisplay, hsync_start, hsync_end, htotal, hskew;
   uint16_t                     vdisplay, vsync_start, vsync_end, vtotal, vscan;
   uint32_t                     flags;
} wsi_display_mode;

typedef struct wsi_display_connector {
   struct list_head             list;
   struct wsi_display           *wsi;
   uint32_t                     id;
   uint32_t                     crtc_id;
   char                         *name;
   bool                         connected;
   bool                         active;
   struct list_head             display_modes;
   wsi_display_mode             *current_mode;
   drmModeModeInfo              current_drm_mode;
   uint32_t                     dpms_property;
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
   xcb_randr_output_t           output;
#endif
} wsi_display_connector;

struct wsi_display {
   struct wsi_interface         base;

   const VkAllocationCallbacks  *alloc;

   int                          fd;

   pthread_mutex_t              wait_mutex;
   pthread_cond_t               wait_cond;
   pthread_t                    wait_thread;

   struct list_head             connectors; /* list of all discovered connectors */
};

#define wsi_for_each_display_mode(_mode, _conn)                 \
   list_for_each_entry_safe(struct wsi_display_mode, _mode,     \
                            &(_conn)->display_modes, list)

#define wsi_for_each_connector(_conn, _dev)                             \
   list_for_each_entry_safe(struct wsi_display_connector, _conn,        \
                            &(_dev)->connectors, list)

enum wsi_image_state {
   WSI_IMAGE_IDLE,
   WSI_IMAGE_DRAWING,
   WSI_IMAGE_QUEUED,
   WSI_IMAGE_FLIPPING,
   WSI_IMAGE_DISPLAYING
};

struct wsi_display_image {
   struct wsi_image             base;
   struct wsi_display_swapchain *chain;
   enum wsi_image_state         state;
   uint32_t                     fb_id;
   uint32_t                     buffer[4];
   uint64_t                     flip_sequence;
};

struct wsi_display_swapchain {
   struct wsi_swapchain         base;
   struct wsi_display           *wsi;
   VkIcdSurfaceDisplay          *surface;
   uint64_t                     flip_sequence;
   VkResult                     status;
   struct wsi_display_image     images[0];
};

struct wsi_display_fence {
   struct wsi_fence             base;
   bool                         event_received;
   bool                         destroyed;
   uint32_t                     syncobj; /* syncobj to signal on event */
   uint64_t                     sequence;
};

static uint64_t fence_sequence;

ICD_DEFINE_NONDISP_HANDLE_CASTS(wsi_display_mode, VkDisplayModeKHR)
ICD_DEFINE_NONDISP_HANDLE_CASTS(wsi_display_connector, VkDisplayKHR)

static bool
wsi_display_mode_matches_drm(wsi_display_mode *wsi,
                             drmModeModeInfoPtr drm)
{
   return wsi->clock == drm->clock &&
      wsi->hdisplay == drm->hdisplay &&
      wsi->hsync_start == drm->hsync_start &&
      wsi->hsync_end == drm->hsync_end &&
      wsi->htotal == drm->htotal &&
      wsi->hskew == drm->hskew &&
      wsi->vdisplay == drm->vdisplay &&
      wsi->vsync_start == drm->vsync_start &&
      wsi->vsync_end == drm->vsync_end &&
      wsi->vtotal == drm->vtotal &&
      MAX2(wsi->vscan, 1) == MAX2(drm->vscan, 1) &&
      wsi->flags == drm->flags;
}

static double
wsi_display_mode_refresh(struct wsi_display_mode *wsi)
{
   return (double) wsi->clock * 1000.0 / ((double) wsi->htotal *
                                          (double) wsi->vtotal *
                                          (double) MAX2(wsi->vscan, 1));
}

static uint64_t wsi_rel_to_abs_time(uint64_t rel_time)
{
   uint64_t current_time = wsi_common_get_current_time();

   /* check for overflow */
   if (rel_time > UINT64_MAX - current_time)
      return UINT64_MAX;

   return current_time + rel_time;
}

static struct wsi_display_mode *
wsi_display_find_drm_mode(struct wsi_device *wsi_device,
                          struct wsi_display_connector *connector,
                          drmModeModeInfoPtr mode)
{
   wsi_for_each_display_mode(display_mode, connector) {
      if (wsi_display_mode_matches_drm(display_mode, mode))
         return display_mode;
   }
   return NULL;
}

static void
wsi_display_invalidate_connector_modes(struct wsi_device *wsi_device,
                                       struct wsi_display_connector *connector)
{
   wsi_for_each_display_mode(display_mode, connector) {
      display_mode->valid = false;
   }
}

static VkResult
wsi_display_register_drm_mode(struct wsi_device *wsi_device,
                              struct wsi_display_connector *connector,
                              drmModeModeInfoPtr drm_mode)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];
   struct wsi_display_mode *display_mode =
      wsi_display_find_drm_mode(wsi_device, connector, drm_mode);

   if (display_mode) {
      display_mode->valid = true;
      return VK_SUCCESS;
   }

   display_mode = vk_zalloc(wsi->alloc, sizeof (struct wsi_display_mode),
                            8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!display_mode)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   display_mode->connector = connector;
   display_mode->valid = true;
   display_mode->preferred = (drm_mode->type & DRM_MODE_TYPE_PREFERRED) != 0;
   display_mode->clock = drm_mode->clock; /* kHz */
   display_mode->hdisplay = drm_mode->hdisplay;
   display_mode->hsync_start = drm_mode->hsync_start;
   display_mode->hsync_end = drm_mode->hsync_end;
   display_mode->htotal = drm_mode->htotal;
   display_mode->hskew = drm_mode->hskew;
   display_mode->vdisplay = drm_mode->vdisplay;
   display_mode->vsync_start = drm_mode->vsync_start;
   display_mode->vsync_end = drm_mode->vsync_end;
   display_mode->vtotal = drm_mode->vtotal;
   display_mode->vscan = drm_mode->vscan;
   display_mode->flags = drm_mode->flags;

   list_addtail(&display_mode->list, &connector->display_modes);
   return VK_SUCCESS;
}

/*
 * Update our information about a specific connector
 */

static struct wsi_display_connector *
wsi_display_find_connector(struct wsi_device *wsi_device,
                          uint32_t connector_id)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];

   wsi_for_each_connector(connector, wsi) {
      if (connector->id == connector_id)
         return connector;
   }

   return NULL;
}

static struct wsi_display_connector *
wsi_display_alloc_connector(struct wsi_display *wsi,
                            uint32_t connector_id)
{
   struct wsi_display_connector *connector =
      vk_zalloc(wsi->alloc, sizeof (struct wsi_display_connector),
                8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

   connector->id = connector_id;
   connector->wsi = wsi;
   connector->active = false;
   /* XXX use EDID name */
   connector->name = "monitor";
   list_inithead(&connector->display_modes);
   return connector;
}

static struct wsi_display_connector *
wsi_display_get_connector(struct wsi_device *wsi_device,
                          uint32_t connector_id)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];

   if (wsi->fd < 0)
      return NULL;

   drmModeConnectorPtr drm_connector =
      drmModeGetConnector(wsi->fd, connector_id);

   if (!drm_connector)
      return NULL;

   struct wsi_display_connector *connector =
      wsi_display_find_connector(wsi_device, connector_id);

   if (!connector) {
      connector = wsi_display_alloc_connector(wsi, connector_id);
      if (!connector) {
         drmModeFreeConnector(drm_connector);
         return NULL;
      }
      list_addtail(&connector->list, &wsi->connectors);
   }

   connector->connected = drm_connector->connection != DRM_MODE_DISCONNECTED;

   /* Look for a DPMS property if we haven't already found one */
   for (int p = 0; connector->dpms_property == 0 &&
           p < drm_connector->count_props; p++)
   {
      drmModePropertyPtr prop = drmModeGetProperty(wsi->fd,
                                                   drm_connector->props[p]);
      if (!prop)
         continue;
      if (prop->flags & DRM_MODE_PROP_ENUM) {
         if (!strcmp(prop->name, "DPMS"))
            connector->dpms_property = drm_connector->props[p];
      }
      drmModeFreeProperty(prop);
   }

   /* Mark all connector modes as invalid */
   wsi_display_invalidate_connector_modes(wsi_device, connector);

   /*
    * List current modes, adding new ones and marking existing ones as
    * valid
    */
   for (int m = 0; m < drm_connector->count_modes; m++) {
      VkResult result = wsi_display_register_drm_mode(wsi_device,
                                                      connector,
                                                      &drm_connector->modes[m]);
      if (result != VK_SUCCESS) {
         drmModeFreeConnector(drm_connector);
         return NULL;
      }
   }

   drmModeFreeConnector(drm_connector);

   return connector;
}

#define MM_PER_PIXEL     (1.0/96.0 * 25.4)

static uint32_t
mode_size(struct wsi_display_mode *mode)
{
   /* fortunately, these are both uint16_t, so this is easy */
   return (uint32_t) mode->hdisplay * (uint32_t) mode->vdisplay;
}

static void
wsi_display_fill_in_display_properties(struct wsi_device *wsi_device,
                                       struct wsi_display_connector *connector,
                                       VkDisplayProperties2KHR *properties2)
{
   assert(properties2->sType == VK_STRUCTURE_TYPE_DISPLAY_PROPERTIES_2_KHR);
   VkDisplayPropertiesKHR *properties = &properties2->displayProperties;

   properties->display = wsi_display_connector_to_handle(connector);
   properties->displayName = connector->name;

   /* Find the first preferred mode and assume that's the physical
    * resolution. If there isn't a preferred mode, find the largest mode and
    * use that.
    */

   struct wsi_display_mode *preferred_mode = NULL, *largest_mode = NULL;
   wsi_for_each_display_mode(display_mode, connector) {
      if (!display_mode->valid)
         continue;
      if (display_mode->preferred) {
         preferred_mode = display_mode;
         break;
      }
      if (largest_mode == NULL ||
          mode_size(display_mode) > mode_size(largest_mode))
      {
         largest_mode = display_mode;
      }
   }

   if (preferred_mode) {
      properties->physicalResolution.width = preferred_mode->hdisplay;
      properties->physicalResolution.height = preferred_mode->vdisplay;
   } else if (largest_mode) {
      properties->physicalResolution.width = largest_mode->hdisplay;
      properties->physicalResolution.height = largest_mode->vdisplay;
   } else {
      properties->physicalResolution.width = 1024;
      properties->physicalResolution.height = 768;
   }

   /* Make up physical size based on 96dpi */
   properties->physicalDimensions.width =
      floor(properties->physicalResolution.width * MM_PER_PIXEL + 0.5);
   properties->physicalDimensions.height =
      floor(properties->physicalResolution.height * MM_PER_PIXEL + 0.5);

   properties->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   properties->planeReorderPossible = VK_FALSE;
   properties->persistentContent = VK_FALSE;
}

/*
 * Implement vkGetPhysicalDeviceDisplayPropertiesKHR (VK_KHR_display)
 */
VkResult
wsi_display_get_physical_device_display_properties(
   VkPhysicalDevice physical_device,
   struct wsi_device *wsi_device,
   uint32_t *property_count,
   VkDisplayPropertiesKHR *properties)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];

   if (properties == NULL) {
      return wsi_display_get_physical_device_display_properties2(
            physical_device, wsi_device, property_count, NULL);
   } else {
      /* If we're actually returning properties, allocate a temporary array of
       * VkDisplayProperties2KHR structs, call properties2 to fill them out,
       * and then copy them to the client.  This seems a bit expensive but
       * wsi_display_get_physical_device_display_properties2() calls
       * drmModeGetResources() which does an ioctl and then a bunch of
       * allocations so this should get lost in the noise.
       */
      VkDisplayProperties2KHR *props2 =
         vk_zalloc(wsi->alloc, sizeof(*props2) * *property_count, 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (props2 == NULL)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      for (uint32_t i = 0; i < *property_count; i++)
         props2[i].sType = VK_STRUCTURE_TYPE_DISPLAY_PROPERTIES_2_KHR;

      VkResult result = wsi_display_get_physical_device_display_properties2(
            physical_device, wsi_device, property_count, props2);

      if (result == VK_SUCCESS || result == VK_INCOMPLETE) {
         for (uint32_t i = 0; i < *property_count; i++)
            properties[i] = props2[i].displayProperties;
      }

      vk_free(wsi->alloc, props2);

      return result;
   }
}

VkResult
wsi_display_get_physical_device_display_properties2(
   VkPhysicalDevice physical_device,
   struct wsi_device *wsi_device,
   uint32_t *property_count,
   VkDisplayProperties2KHR *properties)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];

   if (wsi->fd < 0)
      goto bail;

   drmModeResPtr mode_res = drmModeGetResources(wsi->fd);

   if (!mode_res)
      goto bail;

   VK_OUTARRAY_MAKE(conn, properties, property_count);

   /* Get current information */

   for (int c = 0; c < mode_res->count_connectors; c++) {
      struct wsi_display_connector *connector =
         wsi_display_get_connector(wsi_device, mode_res->connectors[c]);

      if (!connector) {
         drmModeFreeResources(mode_res);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      if (connector->connected) {
         vk_outarray_append(&conn, prop) {
            wsi_display_fill_in_display_properties(wsi_device,
                                                   connector,
                                                   prop);
         }
      }
   }

   drmModeFreeResources(mode_res);

   return vk_outarray_status(&conn);

bail:
   *property_count = 0;
   return VK_SUCCESS;
}

/*
 * Implement vkGetPhysicalDeviceDisplayPlanePropertiesKHR (VK_KHR_display
 */
static void
wsi_display_fill_in_display_plane_properties(
   struct wsi_device *wsi_device,
   struct wsi_display_connector *connector,
   VkDisplayPlaneProperties2KHR *properties)
{
   assert(properties->sType == VK_STRUCTURE_TYPE_DISPLAY_PLANE_PROPERTIES_2_KHR);
   VkDisplayPlanePropertiesKHR *prop = &properties->displayPlaneProperties;

   if (connector && connector->active) {
      prop->currentDisplay = wsi_display_connector_to_handle(connector);
      prop->currentStackIndex = 0;
   } else {
      prop->currentDisplay = VK_NULL_HANDLE;
      prop->currentStackIndex = 0;
   }
}

VkResult
wsi_display_get_physical_device_display_plane_properties(
   VkPhysicalDevice physical_device,
   struct wsi_device *wsi_device,
   uint32_t *property_count,
   VkDisplayPlanePropertiesKHR *properties)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];

   VK_OUTARRAY_MAKE(conn, properties, property_count);

   wsi_for_each_connector(connector, wsi) {
      vk_outarray_append(&conn, prop) {
         VkDisplayPlaneProperties2KHR prop2 = {
            .sType = VK_STRUCTURE_TYPE_DISPLAY_PLANE_PROPERTIES_2_KHR,
         };
         wsi_display_fill_in_display_plane_properties(wsi_device, connector,
                                                      &prop2);
         *prop = prop2.displayPlaneProperties;
      }
   }
   return vk_outarray_status(&conn);
}

VkResult
wsi_display_get_physical_device_display_plane_properties2(
   VkPhysicalDevice physical_device,
   struct wsi_device *wsi_device,
   uint32_t *property_count,
   VkDisplayPlaneProperties2KHR *properties)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];

   VK_OUTARRAY_MAKE(conn, properties, property_count);

   wsi_for_each_connector(connector, wsi) {
      vk_outarray_append(&conn, prop) {
         wsi_display_fill_in_display_plane_properties(wsi_device, connector,
                                                      prop);
      }
   }
   return vk_outarray_status(&conn);
}

/*
 * Implement vkGetDisplayPlaneSupportedDisplaysKHR (VK_KHR_display)
 */

VkResult
wsi_display_get_display_plane_supported_displays(
   VkPhysicalDevice physical_device,
   struct wsi_device *wsi_device,
   uint32_t plane_index,
   uint32_t *display_count,
   VkDisplayKHR *displays)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];

   VK_OUTARRAY_MAKE(conn, displays, display_count);

   int c = 0;

   wsi_for_each_connector(connector, wsi) {
      if (c == plane_index && connector->connected) {
         vk_outarray_append(&conn, display) {
            *display = wsi_display_connector_to_handle(connector);
         }
      }
      c++;
   }
   return vk_outarray_status(&conn);
}

/*
 * Implement vkGetDisplayModePropertiesKHR (VK_KHR_display)
 */

static void
wsi_display_fill_in_display_mode_properties(
   struct wsi_device *wsi_device,
   struct wsi_display_mode *display_mode,
   VkDisplayModeProperties2KHR *properties)
{
   assert(properties->sType == VK_STRUCTURE_TYPE_DISPLAY_MODE_PROPERTIES_2_KHR);
   VkDisplayModePropertiesKHR *prop = &properties->displayModeProperties;

   prop->displayMode = wsi_display_mode_to_handle(display_mode);
   prop->parameters.visibleRegion.width = display_mode->hdisplay;
   prop->parameters.visibleRegion.height = display_mode->vdisplay;
   prop->parameters.refreshRate =
      (uint32_t) (wsi_display_mode_refresh(display_mode) * 1000 + 0.5);
}

VkResult
wsi_display_get_display_mode_properties(VkPhysicalDevice physical_device,
                                        struct wsi_device *wsi_device,
                                        VkDisplayKHR display,
                                        uint32_t *property_count,
                                        VkDisplayModePropertiesKHR *properties)
{
   struct wsi_display_connector *connector =
      wsi_display_connector_from_handle(display);

   VK_OUTARRAY_MAKE(conn, properties, property_count);

   wsi_for_each_display_mode(display_mode, connector) {
      if (!display_mode->valid)
         continue;

      vk_outarray_append(&conn, prop) {
         VkDisplayModeProperties2KHR prop2 = {
            .sType = VK_STRUCTURE_TYPE_DISPLAY_MODE_PROPERTIES_2_KHR,
         };
         wsi_display_fill_in_display_mode_properties(wsi_device,
                                                     display_mode, &prop2);
         *prop = prop2.displayModeProperties;
      }
   }
   return vk_outarray_status(&conn);
}

VkResult
wsi_display_get_display_mode_properties2(VkPhysicalDevice physical_device,
                                         struct wsi_device *wsi_device,
                                         VkDisplayKHR display,
                                         uint32_t *property_count,
                                         VkDisplayModeProperties2KHR *properties)
{
   struct wsi_display_connector *connector =
      wsi_display_connector_from_handle(display);

   VK_OUTARRAY_MAKE(conn, properties, property_count);

   wsi_for_each_display_mode(display_mode, connector) {
      if (!display_mode->valid)
         continue;

      vk_outarray_append(&conn, prop) {
         wsi_display_fill_in_display_mode_properties(wsi_device,
                                                     display_mode, prop);
      }
   }
   return vk_outarray_status(&conn);
}

static bool
wsi_display_mode_matches_vk(wsi_display_mode *wsi,
                            const VkDisplayModeParametersKHR *vk)
{
   return (vk->visibleRegion.width == wsi->hdisplay &&
           vk->visibleRegion.height == wsi->vdisplay &&
           fabs(wsi_display_mode_refresh(wsi) * 1000.0 - vk->refreshRate) < 10);
}

/*
 * Implement vkCreateDisplayModeKHR (VK_KHR_display)
 */
VkResult
wsi_display_create_display_mode(VkPhysicalDevice physical_device,
                                struct wsi_device *wsi_device,
                                VkDisplayKHR display,
                                const VkDisplayModeCreateInfoKHR *create_info,
                                const VkAllocationCallbacks *allocator,
                                VkDisplayModeKHR *mode)
{
   struct wsi_display_connector *connector =
      wsi_display_connector_from_handle(display);

   if (create_info->flags != 0)
      return VK_ERROR_INITIALIZATION_FAILED;

   /* Check and see if the requested mode happens to match an existing one and
    * return that. This makes the conformance suite happy. Doing more than
    * this would involve embedding the CVT function into the driver, which seems
    * excessive.
    */
   wsi_for_each_display_mode(display_mode, connector) {
      if (display_mode->valid) {
         if (wsi_display_mode_matches_vk(display_mode, &create_info->parameters)) {
            *mode = wsi_display_mode_to_handle(display_mode);
            return VK_SUCCESS;
         }
      }
   }
   return VK_ERROR_INITIALIZATION_FAILED;
}

/*
 * Implement vkGetDisplayPlaneCapabilities
 */
VkResult
wsi_get_display_plane_capabilities(VkPhysicalDevice physical_device,
                                   struct wsi_device *wsi_device,
                                   VkDisplayModeKHR mode_khr,
                                   uint32_t plane_index,
                                   VkDisplayPlaneCapabilitiesKHR *capabilities)
{
   struct wsi_display_mode *mode = wsi_display_mode_from_handle(mode_khr);

   /* XXX use actual values */
   capabilities->supportedAlpha = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
   capabilities->minSrcPosition.x = 0;
   capabilities->minSrcPosition.y = 0;
   capabilities->maxSrcPosition.x = 0;
   capabilities->maxSrcPosition.y = 0;
   capabilities->minSrcExtent.width = mode->hdisplay;
   capabilities->minSrcExtent.height = mode->vdisplay;
   capabilities->maxSrcExtent.width = mode->hdisplay;
   capabilities->maxSrcExtent.height = mode->vdisplay;
   capabilities->minDstPosition.x = 0;
   capabilities->minDstPosition.y = 0;
   capabilities->maxDstPosition.x = 0;
   capabilities->maxDstPosition.y = 0;
   capabilities->minDstExtent.width = mode->hdisplay;
   capabilities->minDstExtent.height = mode->vdisplay;
   capabilities->maxDstExtent.width = mode->hdisplay;
   capabilities->maxDstExtent.height = mode->vdisplay;
   return VK_SUCCESS;
}

VkResult
wsi_get_display_plane_capabilities2(
   VkPhysicalDevice physical_device,
   struct wsi_device *wsi_device,
   const VkDisplayPlaneInfo2KHR *pDisplayPlaneInfo,
   VkDisplayPlaneCapabilities2KHR *capabilities)
{
   assert(capabilities->sType ==
          VK_STRUCTURE_TYPE_DISPLAY_PLANE_CAPABILITIES_2_KHR);

   VkResult result =
      wsi_get_display_plane_capabilities(physical_device, wsi_device,
                                         pDisplayPlaneInfo->mode,
                                         pDisplayPlaneInfo->planeIndex,
                                         &capabilities->capabilities);

   vk_foreach_struct(ext, capabilities->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_SURFACE_PROTECTED_CAPABILITIES_KHR: {
         VkSurfaceProtectedCapabilitiesKHR *protected = (void *)ext;
         protected->supportsProtected = VK_FALSE;
         break;
      }

      default:
         /* Ignored */
         break;
      }
   }

   return result;
}

VkResult
wsi_create_display_surface(VkInstance instance,
                           const VkAllocationCallbacks *allocator,
                           const VkDisplaySurfaceCreateInfoKHR *create_info,
                           VkSurfaceKHR *surface_khr)
{
   VkIcdSurfaceDisplay *surface = vk_zalloc(allocator, sizeof *surface, 8,
                                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (surface == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   surface->base.platform = VK_ICD_WSI_PLATFORM_DISPLAY;

   surface->displayMode = create_info->displayMode;
   surface->planeIndex = create_info->planeIndex;
   surface->planeStackIndex = create_info->planeStackIndex;
   surface->transform = create_info->transform;
   surface->globalAlpha = create_info->globalAlpha;
   surface->alphaMode = create_info->alphaMode;
   surface->imageExtent = create_info->imageExtent;

   *surface_khr = VkIcdSurfaceBase_to_handle(&surface->base);
   return VK_SUCCESS;
}


static VkResult
wsi_display_surface_get_support(VkIcdSurfaceBase *surface,
                                struct wsi_device *wsi_device,
                                uint32_t queueFamilyIndex,
                                VkBool32* pSupported)
{
   *pSupported = VK_TRUE;
   return VK_SUCCESS;
}

static VkResult
wsi_display_surface_get_capabilities(VkIcdSurfaceBase *surface_base,
                                     struct wsi_device *wsi_device,
                                     VkSurfaceCapabilitiesKHR* caps)
{
   VkIcdSurfaceDisplay *surface = (VkIcdSurfaceDisplay *) surface_base;
   wsi_display_mode *mode = wsi_display_mode_from_handle(surface->displayMode);

   caps->currentExtent.width = mode->hdisplay;
   caps->currentExtent.height = mode->vdisplay;

   caps->minImageExtent = (VkExtent2D) { 1, 1 };
   caps->maxImageExtent = (VkExtent2D) {
      wsi_device->maxImageDimension2D,
      wsi_device->maxImageDimension2D,
   };

   caps->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

   caps->minImageCount = 2;
   caps->maxImageCount = 0;

   caps->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   caps->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   caps->maxImageArrayLayers = 1;
   caps->supportedUsageFlags =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
      VK_IMAGE_USAGE_SAMPLED_BIT |
      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      VK_IMAGE_USAGE_STORAGE_BIT |
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

   return VK_SUCCESS;
}

static VkResult
wsi_display_surface_get_surface_counters(
   VkIcdSurfaceBase *surface_base,
   VkSurfaceCounterFlagsEXT *counters)
{
   *counters = VK_SURFACE_COUNTER_VBLANK_EXT;
   return VK_SUCCESS;
}

static VkResult
wsi_display_surface_get_capabilities2(VkIcdSurfaceBase *icd_surface,
                                      struct wsi_device *wsi_device,
                                      const void *info_next,
                                      VkSurfaceCapabilities2KHR *caps)
{
   assert(caps->sType == VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR);
   VkResult result;

   result = wsi_display_surface_get_capabilities(icd_surface, wsi_device,
                                                 &caps->surfaceCapabilities);
   if (result != VK_SUCCESS)
      return result;

   struct wsi_surface_supported_counters *counters =
      vk_find_struct( caps->pNext, WSI_SURFACE_SUPPORTED_COUNTERS_MESA);

   if (counters) {
      result = wsi_display_surface_get_surface_counters(
         icd_surface,
         &counters->supported_surface_counters);
   }

   return result;
}

static const struct {
   VkFormat     format;
   uint32_t     drm_format;
} available_surface_formats[] = {
   { .format = VK_FORMAT_B8G8R8A8_SRGB, .drm_format = DRM_FORMAT_XRGB8888 },
   { .format = VK_FORMAT_B8G8R8A8_UNORM, .drm_format = DRM_FORMAT_XRGB8888 },
};

static void
get_sorted_vk_formats(struct wsi_device *wsi_device, VkFormat *sorted_formats)
{
   for (unsigned i = 0; i < ARRAY_SIZE(available_surface_formats); i++)
      sorted_formats[i] = available_surface_formats[i].format;

   if (wsi_device->force_bgra8_unorm_first) {
      for (unsigned i = 0; i < ARRAY_SIZE(available_surface_formats); i++) {
         if (sorted_formats[i] == VK_FORMAT_B8G8R8A8_UNORM) {
            sorted_formats[i] = sorted_formats[0];
            sorted_formats[0] = VK_FORMAT_B8G8R8A8_UNORM;
            break;
         }
      }
   }
}

static VkResult
wsi_display_surface_get_formats(VkIcdSurfaceBase *icd_surface,
                                struct wsi_device *wsi_device,
                                uint32_t *surface_format_count,
                                VkSurfaceFormatKHR *surface_formats)
{
   VK_OUTARRAY_MAKE(out, surface_formats, surface_format_count);

   VkFormat sorted_formats[ARRAY_SIZE(available_surface_formats)];
   get_sorted_vk_formats(wsi_device, sorted_formats);

   for (unsigned i = 0; i < ARRAY_SIZE(sorted_formats); i++) {
      vk_outarray_append(&out, f) {
         f->format = sorted_formats[i];
         f->colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
      }
   }

   return vk_outarray_status(&out);
}

static VkResult
wsi_display_surface_get_formats2(VkIcdSurfaceBase *surface,
                                 struct wsi_device *wsi_device,
                                 const void *info_next,
                                 uint32_t *surface_format_count,
                                 VkSurfaceFormat2KHR *surface_formats)
{
   VK_OUTARRAY_MAKE(out, surface_formats, surface_format_count);

   VkFormat sorted_formats[ARRAY_SIZE(available_surface_formats)];
   get_sorted_vk_formats(wsi_device, sorted_formats);

   for (unsigned i = 0; i < ARRAY_SIZE(sorted_formats); i++) {
      vk_outarray_append(&out, f) {
         assert(f->sType == VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR);
         f->surfaceFormat.format = sorted_formats[i];
         f->surfaceFormat.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
      }
   }

   return vk_outarray_status(&out);
}

static VkResult
wsi_display_surface_get_present_modes(VkIcdSurfaceBase *surface,
                                      uint32_t *present_mode_count,
                                      VkPresentModeKHR *present_modes)
{
   VK_OUTARRAY_MAKE(conn, present_modes, present_mode_count);

   vk_outarray_append(&conn, present) {
      *present = VK_PRESENT_MODE_FIFO_KHR;
   }

   return vk_outarray_status(&conn);
}

static VkResult
wsi_display_surface_get_present_rectangles(VkIcdSurfaceBase *surface_base,
                                           struct wsi_device *wsi_device,
                                           uint32_t* pRectCount,
                                           VkRect2D* pRects)
{
   VkIcdSurfaceDisplay *surface = (VkIcdSurfaceDisplay *) surface_base;
   wsi_display_mode *mode = wsi_display_mode_from_handle(surface->displayMode);
   VK_OUTARRAY_MAKE(out, pRects, pRectCount);

   if (wsi_device_matches_drm_fd(wsi_device, mode->connector->wsi->fd)) {
      vk_outarray_append(&out, rect) {
         *rect = (VkRect2D) {
            .offset = { 0, 0 },
            .extent = { mode->hdisplay, mode->vdisplay },
         };
      }
   }

   return vk_outarray_status(&out);
}

static void
wsi_display_destroy_buffer(struct wsi_display *wsi,
                           uint32_t buffer)
{
   (void) drmIoctl(wsi->fd, DRM_IOCTL_GEM_CLOSE,
                   &((struct drm_gem_close) { .handle = buffer }));
}

static VkResult
wsi_display_image_init(VkDevice device_h,
                       struct wsi_swapchain *drv_chain,
                       const VkSwapchainCreateInfoKHR *create_info,
                       const VkAllocationCallbacks *allocator,
                       struct wsi_display_image *image)
{
   struct wsi_display_swapchain *chain =
      (struct wsi_display_swapchain *) drv_chain;
   struct wsi_display *wsi = chain->wsi;
   uint32_t drm_format = 0;

   for (unsigned i = 0; i < ARRAY_SIZE(available_surface_formats); i++) {
      if (create_info->imageFormat == available_surface_formats[i].format) {
         drm_format = available_surface_formats[i].drm_format;
         break;
      }
   }

   /* the application provided an invalid format, bail */
   if (drm_format == 0)
      return VK_ERROR_DEVICE_LOST;

   VkResult result = wsi_create_native_image(&chain->base, create_info,
                                             0, NULL, NULL,
                                             &image->base);
   if (result != VK_SUCCESS)
      return result;

   memset(image->buffer, 0, sizeof (image->buffer));

   for (unsigned int i = 0; i < image->base.num_planes; i++) {
      int ret = drmPrimeFDToHandle(wsi->fd, image->base.fds[i],
                                   &image->buffer[i]);

      close(image->base.fds[i]);
      image->base.fds[i] = -1;
      if (ret < 0)
         goto fail_handle;
   }

   image->chain = chain;
   image->state = WSI_IMAGE_IDLE;
   image->fb_id = 0;

   int ret = drmModeAddFB2(wsi->fd,
                           create_info->imageExtent.width,
                           create_info->imageExtent.height,
                           drm_format,
                           image->buffer,
                           image->base.row_pitches,
                           image->base.offsets,
                           &image->fb_id, 0);

   if (ret)
      goto fail_fb;

   return VK_SUCCESS;

fail_fb:
fail_handle:
   for (unsigned int i = 0; i < image->base.num_planes; i++) {
      if (image->buffer[i])
         wsi_display_destroy_buffer(wsi, image->buffer[i]);
      if (image->base.fds[i] != -1) {
         close(image->base.fds[i]);
         image->base.fds[i] = -1;
      }
   }

   wsi_destroy_image(&chain->base, &image->base);

   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

static void
wsi_display_image_finish(struct wsi_swapchain *drv_chain,
                         const VkAllocationCallbacks *allocator,
                         struct wsi_display_image *image)
{
   struct wsi_display_swapchain *chain =
      (struct wsi_display_swapchain *) drv_chain;
   struct wsi_display *wsi = chain->wsi;

   drmModeRmFB(wsi->fd, image->fb_id);
   for (unsigned int i = 0; i < image->base.num_planes; i++)
      wsi_display_destroy_buffer(wsi, image->buffer[i]);
   wsi_destroy_image(&chain->base, &image->base);
}

static VkResult
wsi_display_swapchain_destroy(struct wsi_swapchain *drv_chain,
                              const VkAllocationCallbacks *allocator)
{
   struct wsi_display_swapchain *chain =
      (struct wsi_display_swapchain *) drv_chain;

   for (uint32_t i = 0; i < chain->base.image_count; i++)
      wsi_display_image_finish(drv_chain, allocator, &chain->images[i]);

   wsi_swapchain_finish(&chain->base);
   vk_free(allocator, chain);
   return VK_SUCCESS;
}

static struct wsi_image *
wsi_display_get_wsi_image(struct wsi_swapchain *drv_chain,
                          uint32_t image_index)
{
   struct wsi_display_swapchain *chain =
      (struct wsi_display_swapchain *) drv_chain;

   return &chain->images[image_index].base;
}

static void
wsi_display_idle_old_displaying(struct wsi_display_image *active_image)
{
   struct wsi_display_swapchain *chain = active_image->chain;

   wsi_display_debug("idle everyone but %ld\n",
                     active_image - &(chain->images[0]));
   for (uint32_t i = 0; i < chain->base.image_count; i++)
      if (chain->images[i].state == WSI_IMAGE_DISPLAYING &&
          &chain->images[i] != active_image)
      {
         wsi_display_debug("idle %d\n", i);
         chain->images[i].state = WSI_IMAGE_IDLE;
      }
}

static VkResult
_wsi_display_queue_next(struct wsi_swapchain *drv_chain);

static void
wsi_display_page_flip_handler2(int fd,
                               unsigned int frame,
                               unsigned int sec,
                               unsigned int usec,
                               uint32_t crtc_id,
                               void *data)
{
   struct wsi_display_image *image = data;
   struct wsi_display_swapchain *chain = image->chain;

   wsi_display_debug("image %ld displayed at %d\n",
                     image - &(image->chain->images[0]), frame);
   image->state = WSI_IMAGE_DISPLAYING;
   wsi_display_idle_old_displaying(image);
   VkResult result = _wsi_display_queue_next(&(chain->base));
   if (result != VK_SUCCESS)
      chain->status = result;
}

static void wsi_display_fence_event_handler(struct wsi_display_fence *fence);

static void wsi_display_page_flip_handler(int fd,
                                          unsigned int frame,
                                          unsigned int sec,
                                          unsigned int usec,
                                          void *data)
{
   wsi_display_page_flip_handler2(fd, frame, sec, usec, 0, data);
}

static void wsi_display_vblank_handler(int fd, unsigned int frame,
                                       unsigned int sec, unsigned int usec,
                                       void *data)
{
   struct wsi_display_fence *fence = data;

   wsi_display_fence_event_handler(fence);
}

static void wsi_display_sequence_handler(int fd, uint64_t frame,
                                         uint64_t nsec, uint64_t user_data)
{
   struct wsi_display_fence *fence =
      (struct wsi_display_fence *) (uintptr_t) user_data;

   wsi_display_fence_event_handler(fence);
}

static drmEventContext event_context = {
   .version = DRM_EVENT_CONTEXT_VERSION,
   .page_flip_handler = wsi_display_page_flip_handler,
#if DRM_EVENT_CONTEXT_VERSION >= 3
   .page_flip_handler2 = wsi_display_page_flip_handler2,
#endif
   .vblank_handler = wsi_display_vblank_handler,
   .sequence_handler = wsi_display_sequence_handler,
};

static void *
wsi_display_wait_thread(void *data)
{
   struct wsi_display *wsi = data;
   struct pollfd pollfd = {
      .fd = wsi->fd,
      .events = POLLIN
   };

   pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
   for (;;) {
      int ret = poll(&pollfd, 1, -1);
      if (ret > 0) {
         pthread_mutex_lock(&wsi->wait_mutex);
         (void) drmHandleEvent(wsi->fd, &event_context);
         pthread_cond_broadcast(&wsi->wait_cond);
         pthread_mutex_unlock(&wsi->wait_mutex);
      }
   }
   return NULL;
}

static int
wsi_display_start_wait_thread(struct wsi_display *wsi)
{
   if (!wsi->wait_thread) {
      int ret = pthread_create(&wsi->wait_thread, NULL,
                               wsi_display_wait_thread, wsi);
      if (ret)
         return ret;
   }
   return 0;
}

static void
wsi_display_stop_wait_thread(struct wsi_display *wsi)
{
   pthread_mutex_lock(&wsi->wait_mutex);
   if (wsi->wait_thread) {
      pthread_cancel(wsi->wait_thread);
      pthread_join(wsi->wait_thread, NULL);
      wsi->wait_thread = 0;
   }
   pthread_mutex_unlock(&wsi->wait_mutex);
}

/*
 * Wait for at least one event from the kernel to be processed.
 * Call with wait_mutex held
 */
static int
wsi_display_wait_for_event(struct wsi_display *wsi,
                           uint64_t timeout_ns)
{
   int ret;

   ret = wsi_display_start_wait_thread(wsi);

   if (ret)
      return ret;

   struct timespec abs_timeout = {
      .tv_sec = timeout_ns / 1000000000ULL,
      .tv_nsec = timeout_ns % 1000000000ULL,
   };

   ret = pthread_cond_timedwait(&wsi->wait_cond, &wsi->wait_mutex,
                                &abs_timeout);

   wsi_display_debug("%9ld done waiting for event %d\n", pthread_self(), ret);
   return ret;
}

static VkResult
wsi_display_acquire_next_image(struct wsi_swapchain *drv_chain,
                               const VkAcquireNextImageInfoKHR *info,
                               uint32_t *image_index)
{
   struct wsi_display_swapchain *chain =
      (struct wsi_display_swapchain *)drv_chain;
   struct wsi_display *wsi = chain->wsi;
   int ret = 0;
   VkResult result = VK_SUCCESS;

   /* Bail early if the swapchain is broken */
   if (chain->status != VK_SUCCESS)
      return chain->status;

   uint64_t timeout = info->timeout;
   if (timeout != 0 && timeout != UINT64_MAX)
      timeout = wsi_rel_to_abs_time(timeout);

   pthread_mutex_lock(&wsi->wait_mutex);
   for (;;) {
      for (uint32_t i = 0; i < chain->base.image_count; i++) {
         if (chain->images[i].state == WSI_IMAGE_IDLE) {
            *image_index = i;
            wsi_display_debug("image %d available\n", i);
            chain->images[i].state = WSI_IMAGE_DRAWING;
            result = VK_SUCCESS;
            goto done;
         }
         wsi_display_debug("image %d state %d\n", i, chain->images[i].state);
      }

      if (ret == ETIMEDOUT) {
         result = VK_TIMEOUT;
         goto done;
      }

      ret = wsi_display_wait_for_event(wsi, timeout);

      if (ret && ret != ETIMEDOUT) {
         result = VK_ERROR_SURFACE_LOST_KHR;
         goto done;
      }
   }
done:
   pthread_mutex_unlock(&wsi->wait_mutex);

   if (result != VK_SUCCESS)
      return result;

   return chain->status;
}

/*
 * Check whether there are any other connectors driven by this crtc
 */
static bool
wsi_display_crtc_solo(struct wsi_display *wsi,
                      drmModeResPtr mode_res,
                      drmModeConnectorPtr connector,
                      uint32_t crtc_id)
{
   /* See if any other connectors share the same encoder */
   for (int c = 0; c < mode_res->count_connectors; c++) {
      if (mode_res->connectors[c] == connector->connector_id)
         continue;

      drmModeConnectorPtr other_connector =
         drmModeGetConnector(wsi->fd, mode_res->connectors[c]);

      if (other_connector) {
         bool match = (other_connector->encoder_id == connector->encoder_id);
         drmModeFreeConnector(other_connector);
         if (match)
            return false;
      }
   }

   /* See if any other encoders share the same crtc */
   for (int e = 0; e < mode_res->count_encoders; e++) {
      if (mode_res->encoders[e] == connector->encoder_id)
         continue;

      drmModeEncoderPtr other_encoder =
         drmModeGetEncoder(wsi->fd, mode_res->encoders[e]);

      if (other_encoder) {
         bool match = (other_encoder->crtc_id == crtc_id);
         drmModeFreeEncoder(other_encoder);
         if (match)
            return false;
      }
   }
   return true;
}

/*
 * Pick a suitable CRTC to drive this connector. Prefer a CRTC which is
 * currently driving this connector and not any others. Settle for a CRTC
 * which is currently idle.
 */
static uint32_t
wsi_display_select_crtc(const struct wsi_display_connector *connector,
                        drmModeResPtr mode_res,
                        drmModeConnectorPtr drm_connector)
{
   struct wsi_display *wsi = connector->wsi;

   /* See what CRTC is currently driving this connector */
   if (drm_connector->encoder_id) {
      drmModeEncoderPtr encoder =
         drmModeGetEncoder(wsi->fd, drm_connector->encoder_id);

      if (encoder) {
         uint32_t crtc_id = encoder->crtc_id;
         drmModeFreeEncoder(encoder);
         if (crtc_id) {
            if (wsi_display_crtc_solo(wsi, mode_res, drm_connector, crtc_id))
               return crtc_id;
         }
      }
   }
   uint32_t crtc_id = 0;
   for (int c = 0; crtc_id == 0 && c < mode_res->count_crtcs; c++) {
      drmModeCrtcPtr crtc = drmModeGetCrtc(wsi->fd, mode_res->crtcs[c]);
      if (crtc && crtc->buffer_id == 0)
         crtc_id = crtc->crtc_id;
      drmModeFreeCrtc(crtc);
   }
   return crtc_id;
}

static VkResult
wsi_display_setup_connector(wsi_display_connector *connector,
                            wsi_display_mode *display_mode)
{
   struct wsi_display *wsi = connector->wsi;

   if (connector->current_mode == display_mode && connector->crtc_id)
      return VK_SUCCESS;

   VkResult result = VK_SUCCESS;

   drmModeResPtr mode_res = drmModeGetResources(wsi->fd);
   if (!mode_res) {
      if (errno == ENOMEM)
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
      else
         result = VK_ERROR_SURFACE_LOST_KHR;
      goto bail;
   }

   drmModeConnectorPtr drm_connector =
      drmModeGetConnectorCurrent(wsi->fd, connector->id);

   if (!drm_connector) {
      if (errno == ENOMEM)
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
      else
         result = VK_ERROR_SURFACE_LOST_KHR;
      goto bail_mode_res;
   }

   /* Pick a CRTC if we don't have one */
   if (!connector->crtc_id) {
      connector->crtc_id = wsi_display_select_crtc(connector,
                                                   mode_res, drm_connector);
      if (!connector->crtc_id) {
         result = VK_ERROR_SURFACE_LOST_KHR;
         goto bail_connector;
      }
   }

   if (connector->current_mode != display_mode) {

      /* Find the drm mode corresponding to the requested VkDisplayMode */
      drmModeModeInfoPtr drm_mode = NULL;

      for (int m = 0; m < drm_connector->count_modes; m++) {
         drm_mode = &drm_connector->modes[m];
         if (wsi_display_mode_matches_drm(display_mode, drm_mode))
            break;
         drm_mode = NULL;
      }

      if (!drm_mode) {
         result = VK_ERROR_SURFACE_LOST_KHR;
         goto bail_connector;
      }

      connector->current_mode = display_mode;
      connector->current_drm_mode = *drm_mode;
   }

bail_connector:
   drmModeFreeConnector(drm_connector);
bail_mode_res:
   drmModeFreeResources(mode_res);
bail:
   return result;

}

static VkResult
wsi_display_fence_wait(struct wsi_fence *fence_wsi, uint64_t timeout)
{
   const struct wsi_device *wsi_device = fence_wsi->wsi_device;
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];
   struct wsi_display_fence *fence = (struct wsi_display_fence *) fence_wsi;

   wsi_display_debug("%9lu wait fence %lu %ld\n",
                     pthread_self(), fence->sequence,
                     (int64_t) (timeout - wsi_common_get_current_time()));
   wsi_display_debug_code(uint64_t start_ns = wsi_common_get_current_time());
   pthread_mutex_lock(&wsi->wait_mutex);

   VkResult result;
   int ret = 0;
   for (;;) {
      if (fence->event_received) {
         wsi_display_debug("%9lu fence %lu passed\n",
                           pthread_self(), fence->sequence);
         result = VK_SUCCESS;
         break;
      }

      if (ret == ETIMEDOUT) {
         wsi_display_debug("%9lu fence %lu timeout\n",
                           pthread_self(), fence->sequence);
         result = VK_TIMEOUT;
         break;
      }

      ret = wsi_display_wait_for_event(wsi, timeout);

      if (ret && ret != ETIMEDOUT) {
         wsi_display_debug("%9lu fence %lu error\n",
                           pthread_self(), fence->sequence);
         result = VK_ERROR_DEVICE_LOST;
         break;
      }
   }
   pthread_mutex_unlock(&wsi->wait_mutex);
   wsi_display_debug("%9lu fence wait %f ms\n",
                     pthread_self(),
                     ((int64_t) (wsi_common_get_current_time() - start_ns)) /
                     1.0e6);
   return result;
}

static void
wsi_display_fence_check_free(struct wsi_display_fence *fence)
{
   if (fence->event_received && fence->destroyed)
      vk_free(fence->base.alloc, fence);
}

static void wsi_display_fence_event_handler(struct wsi_display_fence *fence)
{
   struct wsi_display *wsi =
      (struct wsi_display *) fence->base.wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];

   if (fence->syncobj) {
      (void) drmSyncobjSignal(wsi->fd, &fence->syncobj, 1);
      (void) drmSyncobjDestroy(wsi->fd, fence->syncobj);
   }

   fence->event_received = true;
   wsi_display_fence_check_free(fence);
}

static void
wsi_display_fence_destroy(struct wsi_fence *fence_wsi)
{
   struct wsi_display_fence *fence = (struct wsi_display_fence *) fence_wsi;

   assert(!fence->destroyed);
   fence->destroyed = true;
   wsi_display_fence_check_free(fence);
}

static struct wsi_display_fence *
wsi_display_fence_alloc(VkDevice device,
                        const struct wsi_device *wsi_device,
                        VkDisplayKHR display,
                        const VkAllocationCallbacks *allocator,
                        int sync_fd)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];
   struct wsi_display_fence *fence =
      vk_zalloc2(wsi->alloc, allocator, sizeof (*fence),
                8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (!fence)
      return NULL;

   if (sync_fd >= 0) {
      int ret = drmSyncobjFDToHandle(wsi->fd, sync_fd, &fence->syncobj);
      if (ret) {
         vk_free2(wsi->alloc, allocator, fence);
         return NULL;
      }
   }

   fence->base.device = device;
   fence->base.display = display;
   fence->base.wsi_device = wsi_device;
   fence->base.alloc = allocator ? allocator : wsi->alloc;
   fence->base.wait = wsi_display_fence_wait;
   fence->base.destroy = wsi_display_fence_destroy;
   fence->event_received = false;
   fence->destroyed = false;
   fence->sequence = ++fence_sequence;
   return fence;
}

static VkResult
wsi_register_vblank_event(struct wsi_display_fence *fence,
                          const struct wsi_device *wsi_device,
                          VkDisplayKHR display,
                          uint32_t flags,
                          uint64_t frame_requested,
                          uint64_t *frame_queued)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];
   struct wsi_display_connector *connector =
      wsi_display_connector_from_handle(display);

   if (wsi->fd < 0)
      return VK_ERROR_INITIALIZATION_FAILED;

   for (;;) {
      int ret = drmCrtcQueueSequence(wsi->fd, connector->crtc_id,
                                     flags,
                                     frame_requested,
                                     frame_queued,
                                     (uintptr_t) fence);

      if (!ret)
         return VK_SUCCESS;

      if (errno != ENOMEM) {

         /* Something unexpected happened. Pause for a moment so the
          * application doesn't just spin and then return a failure indication
          */

         wsi_display_debug("queue vblank event %lu failed\n", fence->sequence);
         struct timespec delay = {
            .tv_sec = 0,
            .tv_nsec = 100000000ull,
         };
         nanosleep(&delay, NULL);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      /* The kernel event queue is full. Wait for some events to be
       * processed and try again
       */

      pthread_mutex_lock(&wsi->wait_mutex);
      ret = wsi_display_wait_for_event(wsi, wsi_rel_to_abs_time(100000000ull));
      pthread_mutex_unlock(&wsi->wait_mutex);

      if (ret) {
         wsi_display_debug("vblank queue full, event wait failed\n");
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }
}

/*
 * Check to see if the kernel has no flip queued and if there's an image
 * waiting to be displayed.
 */
static VkResult
_wsi_display_queue_next(struct wsi_swapchain *drv_chain)
{
   struct wsi_display_swapchain *chain =
      (struct wsi_display_swapchain *) drv_chain;
   struct wsi_display *wsi = chain->wsi;
   VkIcdSurfaceDisplay *surface = chain->surface;
   wsi_display_mode *display_mode =
      wsi_display_mode_from_handle(surface->displayMode);
   wsi_display_connector *connector = display_mode->connector;

   if (wsi->fd < 0)
      return VK_ERROR_SURFACE_LOST_KHR;

   if (display_mode != connector->current_mode)
      connector->active = false;

   for (;;) {

      /* Check to see if there is an image to display, or if some image is
       * already queued */

      struct wsi_display_image *image = NULL;

      for (uint32_t i = 0; i < chain->base.image_count; i++) {
         struct wsi_display_image *tmp_image = &chain->images[i];

         switch (tmp_image->state) {
         case WSI_IMAGE_FLIPPING:
            /* already flipping, don't send another to the kernel yet */
            return VK_SUCCESS;
         case WSI_IMAGE_QUEUED:
            /* find the oldest queued */
            if (!image || tmp_image->flip_sequence < image->flip_sequence)
               image = tmp_image;
            break;
         default:
            break;
         }
      }

      if (!image)
         return VK_SUCCESS;

      int ret;
      if (connector->active) {
         ret = drmModePageFlip(wsi->fd, connector->crtc_id, image->fb_id,
                                   DRM_MODE_PAGE_FLIP_EVENT, image);
         if (ret == 0) {
            image->state = WSI_IMAGE_FLIPPING;
            return VK_SUCCESS;
         }
         wsi_display_debug("page flip err %d %s\n", ret, strerror(-ret));
      } else {
         ret = -EINVAL;
      }

      if (ret == -EINVAL) {
         VkResult result = wsi_display_setup_connector(connector, display_mode);

         if (result != VK_SUCCESS) {
            image->state = WSI_IMAGE_IDLE;
            return result;
         }

         /* XXX allow setting of position */
         ret = drmModeSetCrtc(wsi->fd, connector->crtc_id,
                              image->fb_id, 0, 0,
                              &connector->id, 1,
                              &connector->current_drm_mode);
         if (ret == 0) {
            /* Disable the HW cursor as the app doesn't have a mechanism
             * to control it.
             * Refer to question 12 of the VK_KHR_display spec.
             */
            ret = drmModeSetCursor(wsi->fd, connector->crtc_id, 0, 0, 0 );
            if (ret != 0) {
               wsi_display_debug("failed to hide cursor err %d %s\n", ret, strerror(-ret));
            }

            /* Assume that the mode set is synchronous and that any
             * previous image is now idle.
             */
            image->state = WSI_IMAGE_DISPLAYING;
            wsi_display_idle_old_displaying(image);
            connector->active = true;
            return VK_SUCCESS;
         }
      }

      if (ret != -EACCES) {
         connector->active = false;
         image->state = WSI_IMAGE_IDLE;
         return VK_ERROR_SURFACE_LOST_KHR;
      }

      /* Some other VT is currently active. Sit here waiting for
       * our VT to become active again by polling once a second
       */
      usleep(1000 * 1000);
      connector->active = false;
   }
}

static VkResult
wsi_display_queue_present(struct wsi_swapchain *drv_chain,
                          uint32_t image_index,
                          const VkPresentRegionKHR *damage)
{
   struct wsi_display_swapchain *chain =
      (struct wsi_display_swapchain *) drv_chain;
   struct wsi_display *wsi = chain->wsi;
   struct wsi_display_image *image = &chain->images[image_index];
   VkResult result;

   /* Bail early if the swapchain is broken */
   if (chain->status != VK_SUCCESS)
      return chain->status;

   assert(image->state == WSI_IMAGE_DRAWING);
   wsi_display_debug("present %d\n", image_index);

   pthread_mutex_lock(&wsi->wait_mutex);

   image->flip_sequence = ++chain->flip_sequence;
   image->state = WSI_IMAGE_QUEUED;

   result = _wsi_display_queue_next(drv_chain);
   if (result != VK_SUCCESS)
      chain->status = result;

   pthread_mutex_unlock(&wsi->wait_mutex);

   if (result != VK_SUCCESS)
      return result;

   return chain->status;
}

static VkResult
wsi_display_surface_create_swapchain(
   VkIcdSurfaceBase *icd_surface,
   VkDevice device,
   struct wsi_device *wsi_device,
   const VkSwapchainCreateInfoKHR *create_info,
   const VkAllocationCallbacks *allocator,
   struct wsi_swapchain **swapchain_out)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];

   assert(create_info->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);

   const unsigned num_images = create_info->minImageCount;
   struct wsi_display_swapchain *chain =
      vk_zalloc(allocator,
                sizeof(*chain) + num_images * sizeof(chain->images[0]),
                8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (chain == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   VkResult result = wsi_swapchain_init(wsi_device, &chain->base, device,
                                        create_info, allocator);
   if (result != VK_SUCCESS) {
      vk_free(allocator, chain);
      return result;
   }

   chain->base.destroy = wsi_display_swapchain_destroy;
   chain->base.get_wsi_image = wsi_display_get_wsi_image;
   chain->base.acquire_next_image = wsi_display_acquire_next_image;
   chain->base.queue_present = wsi_display_queue_present;
   chain->base.present_mode = wsi_swapchain_get_present_mode(wsi_device, create_info);
   chain->base.image_count = num_images;

   chain->wsi = wsi;
   chain->status = VK_SUCCESS;

   chain->surface = (VkIcdSurfaceDisplay *) icd_surface;

   for (uint32_t image = 0; image < chain->base.image_count; image++) {
      result = wsi_display_image_init(device, &chain->base,
                                      create_info, allocator,
                                      &chain->images[image]);
      if (result != VK_SUCCESS) {
         while (image > 0) {
            --image;
            wsi_display_image_finish(&chain->base, allocator,
                                     &chain->images[image]);
         }
         vk_free(allocator, chain);
         goto fail_init_images;
      }
   }

   *swapchain_out = &chain->base;

   return VK_SUCCESS;

fail_init_images:
   return result;
}

static bool
wsi_init_pthread_cond_monotonic(pthread_cond_t *cond)
{
   pthread_condattr_t condattr;
   bool ret = false;

   if (pthread_condattr_init(&condattr) != 0)
      goto fail_attr_init;

   if (pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC) != 0)
      goto fail_attr_set;

   if (pthread_cond_init(cond, &condattr) != 0)
      goto fail_cond_init;

   ret = true;

fail_cond_init:
fail_attr_set:
   pthread_condattr_destroy(&condattr);
fail_attr_init:
   return ret;
}


/*
 * Local version fo the libdrm helper. Added to avoid depending on bleeding
 * edge version of the library.
 */
static int
local_drmIsMaster(int fd)
{
   /* Detect master by attempting something that requires master.
    *
    * Authenticating magic tokens requires master and 0 is an
    * internal kernel detail which we could use. Attempting this on
    * a master fd would fail therefore fail with EINVAL because 0
    * is invalid.
    *
    * A non-master fd will fail with EACCES, as the kernel checks
    * for master before attempting to do anything else.
    *
    * Since we don't want to leak implementation details, use
    * EACCES.
    */
   return drmAuthMagic(fd, 0) != -EACCES;
}

VkResult
wsi_display_init_wsi(struct wsi_device *wsi_device,
                     const VkAllocationCallbacks *alloc,
                     int display_fd)
{
   struct wsi_display *wsi = vk_zalloc(alloc, sizeof(*wsi), 8,
                                       VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   VkResult result;

   if (!wsi) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   wsi->fd = display_fd;
   if (wsi->fd != -1 && !local_drmIsMaster(wsi->fd))
      wsi->fd = -1;

   wsi->alloc = alloc;

   list_inithead(&wsi->connectors);

   int ret = pthread_mutex_init(&wsi->wait_mutex, NULL);
   if (ret) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail_mutex;
   }

   if (!wsi_init_pthread_cond_monotonic(&wsi->wait_cond)) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail_cond;
   }

   wsi->base.get_support = wsi_display_surface_get_support;
   wsi->base.get_capabilities2 = wsi_display_surface_get_capabilities2;
   wsi->base.get_formats = wsi_display_surface_get_formats;
   wsi->base.get_formats2 = wsi_display_surface_get_formats2;
   wsi->base.get_present_modes = wsi_display_surface_get_present_modes;
   wsi->base.get_present_rectangles = wsi_display_surface_get_present_rectangles;
   wsi->base.create_swapchain = wsi_display_surface_create_swapchain;

   wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY] = &wsi->base;

   return VK_SUCCESS;

fail_cond:
   pthread_mutex_destroy(&wsi->wait_mutex);
fail_mutex:
   vk_free(alloc, wsi);
fail:
   return result;
}

void
wsi_display_finish_wsi(struct wsi_device *wsi_device,
                       const VkAllocationCallbacks *alloc)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];

   if (wsi) {
      wsi_for_each_connector(connector, wsi) {
         wsi_for_each_display_mode(mode, connector) {
            vk_free(wsi->alloc, mode);
         }
         vk_free(wsi->alloc, connector);
      }

      wsi_display_stop_wait_thread(wsi);
      pthread_mutex_destroy(&wsi->wait_mutex);
      pthread_cond_destroy(&wsi->wait_cond);

      vk_free(alloc, wsi);
   }
}

/*
 * Implement vkReleaseDisplay
 */
VkResult
wsi_release_display(VkPhysicalDevice            physical_device,
                    struct wsi_device           *wsi_device,
                    VkDisplayKHR                display)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];

   if (wsi->fd >= 0) {
      wsi_display_stop_wait_thread(wsi);

      close(wsi->fd);
      wsi->fd = -1;
   }

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
   wsi_display_connector_from_handle(display)->output = None;
#endif

   return VK_SUCCESS;
}

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT

static struct wsi_display_connector *
wsi_display_find_output(struct wsi_device *wsi_device,
                        xcb_randr_output_t output)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];

   wsi_for_each_connector(connector, wsi) {
      if (connector->output == output)
         return connector;
   }

   return NULL;
}

/*
 * Given a RandR output, find the associated kernel connector_id by
 * looking at the CONNECTOR_ID property provided by the X server
 */

static uint32_t
wsi_display_output_to_connector_id(xcb_connection_t *connection,
                                   xcb_atom_t *connector_id_atom_p,
                                   xcb_randr_output_t output)
{
   uint32_t connector_id = 0;
   xcb_atom_t connector_id_atom = *connector_id_atom_p;

   if (connector_id_atom == 0) {
   /* Go dig out the CONNECTOR_ID property */
      xcb_intern_atom_cookie_t ia_c = xcb_intern_atom(connection,
                                                          true,
                                                          12,
                                                          "CONNECTOR_ID");
      xcb_intern_atom_reply_t *ia_r = xcb_intern_atom_reply(connection,
                                                                 ia_c,
                                                                 NULL);
      if (ia_r) {
         *connector_id_atom_p = connector_id_atom = ia_r->atom;
         free(ia_r);
      }
   }

   /* If there's an CONNECTOR_ID atom in the server, then there may be a
    * CONNECTOR_ID property. Otherwise, there will not be and we don't even
    * need to bother.
    */
   if (connector_id_atom) {

      xcb_randr_query_version_cookie_t qv_c =
         xcb_randr_query_version(connection, 1, 6);
      xcb_randr_get_output_property_cookie_t gop_c =
         xcb_randr_get_output_property(connection,
                                       output,
                                       connector_id_atom,
                                       0,
                                       0,
                                       0xffffffffUL,
                                       0,
                                       0);
      xcb_randr_query_version_reply_t *qv_r =
         xcb_randr_query_version_reply(connection, qv_c, NULL);
      free(qv_r);
      xcb_randr_get_output_property_reply_t *gop_r =
         xcb_randr_get_output_property_reply(connection, gop_c, NULL);
      if (gop_r) {
         if (gop_r->num_items == 1 && gop_r->format == 32)
            memcpy(&connector_id, xcb_randr_get_output_property_data(gop_r), 4);
         free(gop_r);
      }
   }
   return connector_id;
}

static bool
wsi_display_check_randr_version(xcb_connection_t *connection)
{
   xcb_randr_query_version_cookie_t qv_c =
      xcb_randr_query_version(connection, 1, 6);
   xcb_randr_query_version_reply_t *qv_r =
      xcb_randr_query_version_reply(connection, qv_c, NULL);
   bool ret = false;

   if (!qv_r)
      return false;

   /* Check for version 1.6 or newer */
   ret = (qv_r->major_version > 1 ||
          (qv_r->major_version == 1 && qv_r->minor_version >= 6));

   free(qv_r);
   return ret;
}

/*
 * Given a kernel connector id, find the associated RandR output using the
 * CONNECTOR_ID property
 */

static xcb_randr_output_t
wsi_display_connector_id_to_output(xcb_connection_t *connection,
                                   uint32_t connector_id)
{
   if (!wsi_display_check_randr_version(connection))
      return 0;

   const xcb_setup_t *setup = xcb_get_setup(connection);

   xcb_atom_t connector_id_atom = 0;
   xcb_randr_output_t output = 0;

   /* Search all of the screens for the provided output */
   xcb_screen_iterator_t iter;
   for (iter = xcb_setup_roots_iterator(setup);
        output == 0 && iter.rem;
        xcb_screen_next(&iter))
   {
      xcb_randr_get_screen_resources_cookie_t gsr_c =
         xcb_randr_get_screen_resources(connection, iter.data->root);
      xcb_randr_get_screen_resources_reply_t *gsr_r =
         xcb_randr_get_screen_resources_reply(connection, gsr_c, NULL);

      if (!gsr_r)
         return 0;

      xcb_randr_output_t *ro = xcb_randr_get_screen_resources_outputs(gsr_r);
      int o;

      for (o = 0; o < gsr_r->num_outputs; o++) {
         if (wsi_display_output_to_connector_id(connection,
                                                &connector_id_atom, ro[o])
             == connector_id)
         {
            output = ro[o];
            break;
         }
      }
      free(gsr_r);
   }
   return output;
}

/*
 * Given a RandR output, find out which screen it's associated with
 */
static xcb_window_t
wsi_display_output_to_root(xcb_connection_t *connection,
                           xcb_randr_output_t output)
{
   if (!wsi_display_check_randr_version(connection))
      return 0;

   const xcb_setup_t *setup = xcb_get_setup(connection);
   xcb_window_t root = 0;

   /* Search all of the screens for the provided output */
   for (xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
        root == 0 && iter.rem;
        xcb_screen_next(&iter))
   {
      xcb_randr_get_screen_resources_cookie_t gsr_c =
         xcb_randr_get_screen_resources(connection, iter.data->root);
      xcb_randr_get_screen_resources_reply_t *gsr_r =
         xcb_randr_get_screen_resources_reply(connection, gsr_c, NULL);

      if (!gsr_r)
         return 0;

      xcb_randr_output_t *ro = xcb_randr_get_screen_resources_outputs(gsr_r);

      for (int o = 0; o < gsr_r->num_outputs; o++) {
         if (ro[o] == output) {
            root = iter.data->root;
            break;
         }
      }
      free(gsr_r);
   }
   return root;
}

static bool
wsi_display_mode_matches_x(struct wsi_display_mode *wsi,
                           xcb_randr_mode_info_t *xcb)
{
   return wsi->clock == (xcb->dot_clock + 500) / 1000 &&
      wsi->hdisplay == xcb->width &&
      wsi->hsync_start == xcb->hsync_start &&
      wsi->hsync_end == xcb->hsync_end &&
      wsi->htotal == xcb->htotal &&
      wsi->hskew == xcb->hskew &&
      wsi->vdisplay == xcb->height &&
      wsi->vsync_start == xcb->vsync_start &&
      wsi->vsync_end == xcb->vsync_end &&
      wsi->vtotal == xcb->vtotal &&
      wsi->vscan <= 1 &&
      wsi->flags == xcb->mode_flags;
}

static struct wsi_display_mode *
wsi_display_find_x_mode(struct wsi_device *wsi_device,
                        struct wsi_display_connector *connector,
                        xcb_randr_mode_info_t *mode)
{
   wsi_for_each_display_mode(display_mode, connector) {
      if (wsi_display_mode_matches_x(display_mode, mode))
         return display_mode;
   }
   return NULL;
}

static VkResult
wsi_display_register_x_mode(struct wsi_device *wsi_device,
                            struct wsi_display_connector *connector,
                            xcb_randr_mode_info_t *x_mode,
                            bool preferred)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];
   struct wsi_display_mode *display_mode =
      wsi_display_find_x_mode(wsi_device, connector, x_mode);

   if (display_mode) {
      display_mode->valid = true;
      return VK_SUCCESS;
   }

   display_mode = vk_zalloc(wsi->alloc, sizeof (struct wsi_display_mode),
                            8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!display_mode)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   display_mode->connector = connector;
   display_mode->valid = true;
   display_mode->preferred = preferred;
   display_mode->clock = (x_mode->dot_clock + 500) / 1000; /* kHz */
   display_mode->hdisplay = x_mode->width;
   display_mode->hsync_start = x_mode->hsync_start;
   display_mode->hsync_end = x_mode->hsync_end;
   display_mode->htotal = x_mode->htotal;
   display_mode->hskew = x_mode->hskew;
   display_mode->vdisplay = x_mode->height;
   display_mode->vsync_start = x_mode->vsync_start;
   display_mode->vsync_end = x_mode->vsync_end;
   display_mode->vtotal = x_mode->vtotal;
   display_mode->vscan = 0;
   display_mode->flags = x_mode->mode_flags;

   list_addtail(&display_mode->list, &connector->display_modes);
   return VK_SUCCESS;
}

static struct wsi_display_connector *
wsi_display_get_output(struct wsi_device *wsi_device,
                       xcb_connection_t *connection,
                       xcb_randr_output_t output)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];
   struct wsi_display_connector *connector;
   uint32_t connector_id;

   xcb_window_t root = wsi_display_output_to_root(connection, output);
   if (!root)
      return NULL;

   /* See if we already have a connector for this output */
   connector = wsi_display_find_output(wsi_device, output);

   if (!connector) {
      xcb_atom_t connector_id_atom = 0;

      /*
       * Go get the kernel connector ID for this X output
       */
      connector_id = wsi_display_output_to_connector_id(connection,
                                                        &connector_id_atom,
                                                        output);

      /* Any X server with lease support will have this atom */
      if (!connector_id) {
         return NULL;
      }

      /* See if we already have a connector for this id */
      connector = wsi_display_find_connector(wsi_device, connector_id);

      if (connector == NULL) {
         connector = wsi_display_alloc_connector(wsi, connector_id);
         if (!connector) {
            return NULL;
         }
         list_addtail(&connector->list, &wsi->connectors);
      }
      connector->output = output;
   }

   xcb_randr_get_screen_resources_cookie_t src =
      xcb_randr_get_screen_resources(connection, root);
   xcb_randr_get_output_info_cookie_t oic =
      xcb_randr_get_output_info(connection, output, XCB_CURRENT_TIME);
   xcb_randr_get_screen_resources_reply_t *srr =
      xcb_randr_get_screen_resources_reply(connection, src, NULL);
   xcb_randr_get_output_info_reply_t *oir =
      xcb_randr_get_output_info_reply(connection, oic, NULL);

   if (oir && srr) {
      /* Get X modes and add them */

      connector->connected =
         oir->connection != XCB_RANDR_CONNECTION_DISCONNECTED;

      wsi_display_invalidate_connector_modes(wsi_device, connector);

      xcb_randr_mode_t *x_modes = xcb_randr_get_output_info_modes(oir);
      for (int m = 0; m < oir->num_modes; m++) {
         xcb_randr_mode_info_iterator_t i =
            xcb_randr_get_screen_resources_modes_iterator(srr);
         while (i.rem) {
            xcb_randr_mode_info_t *mi = i.data;
            if (mi->id == x_modes[m]) {
               VkResult result = wsi_display_register_x_mode(
                  wsi_device, connector, mi, m < oir->num_preferred);
               if (result != VK_SUCCESS) {
                  free(oir);
                  free(srr);
                  return NULL;
               }
               break;
            }
            xcb_randr_mode_info_next(&i);
         }
      }
   }

   free(oir);
   free(srr);
   return connector;
}

static xcb_randr_crtc_t
wsi_display_find_crtc_for_output(xcb_connection_t *connection,
                                 xcb_window_t root,
                                 xcb_randr_output_t output)
{
   xcb_randr_get_screen_resources_cookie_t gsr_c =
      xcb_randr_get_screen_resources(connection, root);
   xcb_randr_get_screen_resources_reply_t *gsr_r =
      xcb_randr_get_screen_resources_reply(connection, gsr_c, NULL);

   if (!gsr_r)
      return 0;

   xcb_randr_crtc_t *rc = xcb_randr_get_screen_resources_crtcs(gsr_r);
   xcb_randr_crtc_t idle_crtc = 0;
   xcb_randr_crtc_t active_crtc = 0;

   /* Find either a crtc already connected to the desired output or idle */
   for (int c = 0; active_crtc == 0 && c < gsr_r->num_crtcs; c++) {
      xcb_randr_get_crtc_info_cookie_t gci_c =
         xcb_randr_get_crtc_info(connection, rc[c], gsr_r->config_timestamp);
      xcb_randr_get_crtc_info_reply_t *gci_r =
         xcb_randr_get_crtc_info_reply(connection, gci_c, NULL);

      if (gci_r) {
         if (gci_r->mode) {
            int num_outputs = xcb_randr_get_crtc_info_outputs_length(gci_r);
            xcb_randr_output_t *outputs =
               xcb_randr_get_crtc_info_outputs(gci_r);

            if (num_outputs == 1 && outputs[0] == output)
               active_crtc = rc[c];

         } else if (idle_crtc == 0) {
            int num_possible = xcb_randr_get_crtc_info_possible_length(gci_r);
            xcb_randr_output_t *possible =
               xcb_randr_get_crtc_info_possible(gci_r);

            for (int p = 0; p < num_possible; p++)
               if (possible[p] == output) {
                  idle_crtc = rc[c];
                  break;
               }
         }
         free(gci_r);
      }
   }
   free(gsr_r);

   if (active_crtc)
      return active_crtc;
   return idle_crtc;
}

VkResult
wsi_acquire_xlib_display(VkPhysicalDevice physical_device,
                         struct wsi_device *wsi_device,
                         Display *dpy,
                         VkDisplayKHR display)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];
   xcb_connection_t *connection = XGetXCBConnection(dpy);
   struct wsi_display_connector *connector =
      wsi_display_connector_from_handle(display);
   xcb_window_t root;

   /* XXX no support for multiple leases yet */
   if (wsi->fd >= 0)
      return VK_ERROR_INITIALIZATION_FAILED;

   if (!connector->output) {
      connector->output = wsi_display_connector_id_to_output(connection,
                                                             connector->id);

      /* Check and see if we found the output */
      if (!connector->output)
         return VK_ERROR_INITIALIZATION_FAILED;
   }

   root = wsi_display_output_to_root(connection, connector->output);
   if (!root)
      return VK_ERROR_INITIALIZATION_FAILED;

   xcb_randr_crtc_t crtc = wsi_display_find_crtc_for_output(connection,
                                                            root,
                                                            connector->output);

   if (!crtc)
      return VK_ERROR_INITIALIZATION_FAILED;

#ifdef HAVE_DRI3_MODIFIERS
   xcb_randr_lease_t lease = xcb_generate_id(connection);
   xcb_randr_create_lease_cookie_t cl_c =
      xcb_randr_create_lease(connection, root, lease, 1, 1,
                             &crtc, &connector->output);
   xcb_randr_create_lease_reply_t *cl_r =
      xcb_randr_create_lease_reply(connection, cl_c, NULL);
   if (!cl_r)
      return VK_ERROR_INITIALIZATION_FAILED;

   int fd = -1;
   if (cl_r->nfd > 0) {
      int *rcl_f = xcb_randr_create_lease_reply_fds(connection, cl_r);

      fd = rcl_f[0];
   }
   free (cl_r);
   if (fd < 0)
      return VK_ERROR_INITIALIZATION_FAILED;

   wsi->fd = fd;
#endif

   return VK_SUCCESS;
}

VkResult
wsi_get_randr_output_display(VkPhysicalDevice physical_device,
                             struct wsi_device *wsi_device,
                             Display *dpy,
                             RROutput output,
                             VkDisplayKHR *display)
{
   xcb_connection_t *connection = XGetXCBConnection(dpy);
   struct wsi_display_connector *connector =
      wsi_display_get_output(wsi_device, connection, (xcb_randr_output_t) output);

   if (connector)
      *display = wsi_display_connector_to_handle(connector);
   else
      *display = VK_NULL_HANDLE;
   return VK_SUCCESS;
}

#endif

/* VK_EXT_display_control */
VkResult
wsi_display_power_control(VkDevice device,
                          struct wsi_device *wsi_device,
                          VkDisplayKHR display,
                          const VkDisplayPowerInfoEXT *display_power_info)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];
   struct wsi_display_connector *connector =
      wsi_display_connector_from_handle(display);
   int mode;

   if (wsi->fd < 0)
      return VK_ERROR_INITIALIZATION_FAILED;

   switch (display_power_info->powerState) {
   case VK_DISPLAY_POWER_STATE_OFF_EXT:
      mode = DRM_MODE_DPMS_OFF;
      break;
   case VK_DISPLAY_POWER_STATE_SUSPEND_EXT:
      mode = DRM_MODE_DPMS_SUSPEND;
      break;
   default:
      mode = DRM_MODE_DPMS_ON;
      break;
   }
   drmModeConnectorSetProperty(wsi->fd,
                               connector->id,
                               connector->dpms_property,
                               mode);
   return VK_SUCCESS;
}

VkResult
wsi_register_device_event(VkDevice device,
                          struct wsi_device *wsi_device,
                          const VkDeviceEventInfoEXT *device_event_info,
                          const VkAllocationCallbacks *allocator,
                          struct wsi_fence **fence_p,
                          int sync_fd)
{
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

VkResult
wsi_register_display_event(VkDevice device,
                           struct wsi_device *wsi_device,
                           VkDisplayKHR display,
                           const VkDisplayEventInfoEXT *display_event_info,
                           const VkAllocationCallbacks *allocator,
                           struct wsi_fence **fence_p,
                           int sync_fd)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];
   struct wsi_display_fence *fence;
   VkResult ret;

   switch (display_event_info->displayEvent) {
   case VK_DISPLAY_EVENT_TYPE_FIRST_PIXEL_OUT_EXT:

      fence = wsi_display_fence_alloc(device, wsi_device, display, allocator, sync_fd);

      if (!fence)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      ret = wsi_register_vblank_event(fence, wsi_device, display,
                                      DRM_CRTC_SEQUENCE_RELATIVE, 1, NULL);

      if (ret == VK_SUCCESS) {
         if (fence_p)
            *fence_p = &fence->base;
         else
            fence->base.destroy(&fence->base);
      } else if (fence != NULL) {
         if (fence->syncobj)
            drmSyncobjDestroy(wsi->fd, fence->syncobj);
         vk_free2(wsi->alloc, allocator, fence);
      }

      break;
   default:
      ret = VK_ERROR_FEATURE_NOT_PRESENT;
      break;
   }

   return ret;
}


VkResult
wsi_get_swapchain_counter(VkDevice device,
                          struct wsi_device *wsi_device,
                          VkSwapchainKHR _swapchain,
                          VkSurfaceCounterFlagBitsEXT flag_bits,
                          uint64_t *value)
{
   struct wsi_display *wsi =
      (struct wsi_display *) wsi_device->wsi[VK_ICD_WSI_PLATFORM_DISPLAY];
   struct wsi_display_swapchain *swapchain =
      (struct wsi_display_swapchain *) wsi_swapchain_from_handle(_swapchain);
   struct wsi_display_connector *connector =
      wsi_display_mode_from_handle(swapchain->surface->displayMode)->connector;

   if (wsi->fd < 0)
      return VK_ERROR_INITIALIZATION_FAILED;

   if (!connector->active) {
      *value = 0;
      return VK_SUCCESS;
   }

   int ret = drmCrtcGetSequence(wsi->fd, connector->crtc_id, value, NULL);
   if (ret)
      *value = 0;

   return VK_SUCCESS;
}

