/*
 * Copyright © 2011 Intel Corporation
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Benjamin Franzke <benjaminfranzke@googlemail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>
#include <xf86drm.h>
#include "drm-uapi/drm_fourcc.h"
#include <GL/gl.h> /* mesa_interface needs GL types */

#include "mesa_interface.h"
#include "gbm_driint.h"
#include "gbmint.h"
#include "loader_dri_helper.h"
#include "kopper_interface.h"
#include "loader.h"
#include "util/u_debug.h"
#include "util/macros.h"
#include "dri_util.h"
#include "pipe/p_screen.h"
#include "dri_screen.h"

#include "gbm_backend_abi.h"

/* For importing wl_buffer */
#if HAVE_WAYLAND_PLATFORM
#include "wayland-drm.h"
#endif

static const struct gbm_core *core;

static GLboolean
dri_validate_egl_image(void *image, void *data)
{
   struct gbm_dri_device *dri = data;

   if (dri->validate_image == NULL)
      return false;

   return dri->validate_image(image, dri->lookup_user_data);
}

static __DRIimage *
dri_lookup_egl_image_validated(void *image, void *data)
{
   struct gbm_dri_device *dri = data;

   if (dri->lookup_image_validated == NULL)
      return NULL;

   return dri->lookup_image_validated(image, dri->lookup_user_data);
}

static void
dri_flush_front_buffer(__DRIdrawable * driDrawable, void *data)
{
   struct gbm_dri_surface *surf = data;
   struct gbm_dri_device *dri = gbm_dri_device(surf->base.gbm);

   if (dri->flush_front_buffer != NULL)
      dri->flush_front_buffer(driDrawable, surf->dri_private);
}

static unsigned
dri_get_capability(void *loaderPrivate, enum dri_loader_cap cap)
{
   /* Note: loaderPrivate is _EGLDisplay* */
   switch (cap) {
   case DRI_LOADER_CAP_FP16:
      return 1;
   case DRI_LOADER_CAP_RGBA_ORDERING:
      return 1;
   default:
      return 0;
   }
}

static int
image_get_buffers(__DRIdrawable *driDrawable,
                  unsigned int format,
                  uint32_t *stamp,
                  void *loaderPrivate,
                  uint32_t buffer_mask,
                  struct __DRIimageList *buffers)
{
   struct gbm_dri_surface *surf = loaderPrivate;
   struct gbm_dri_device *dri = gbm_dri_device(surf->base.gbm);

   if (dri->image_get_buffers == NULL)
      return 0;

   return dri->image_get_buffers(driDrawable, format, stamp,
                                 surf->dri_private, buffer_mask, buffers);
}

static void
swrast_get_drawable_info(__DRIdrawable *driDrawable,
                         int           *x,
                         int           *y,
                         int           *width,
                         int           *height,
                         void          *loaderPrivate)
{
   struct gbm_dri_surface *surf = loaderPrivate;

   *x = 0;
   *y = 0;
   *width = surf->base.v0.width;
   *height = surf->base.v0.height;
}

static void
swrast_put_image2(__DRIdrawable *driDrawable,
                  int            op,
                  int            x,
                  int            y,
                  int            width,
                  int            height,
                  int            stride,
                  char          *data,
                  void          *loaderPrivate)
{
   struct gbm_dri_surface *surf = loaderPrivate;
   struct gbm_dri_device *dri = gbm_dri_device(surf->base.gbm);

   dri->swrast_put_image2(driDrawable,
                          op, x, y,
                          width, height, stride,
                          data, surf->dri_private);
}

static void
swrast_put_image(__DRIdrawable *driDrawable,
                 int            op,
                 int            x,
                 int            y,
                 int            width,
                 int            height,
                 char          *data,
                 void          *loaderPrivate)
{
   swrast_put_image2(driDrawable, op, x, y, width, height,
                            width * 4, data, loaderPrivate);
}

static void
swrast_get_image(__DRIdrawable *driDrawable,
                 int            x,
                 int            y,
                 int            width,
                 int            height,
                 char          *data,
                 void          *loaderPrivate)
{
   struct gbm_dri_surface *surf = loaderPrivate;
   struct gbm_dri_device *dri = gbm_dri_device(surf->base.gbm);

   dri->swrast_get_image(driDrawable,
                         x, y,
                         width, height,
                         data, surf->dri_private);
}

static const __DRIuseInvalidateExtension use_invalidate = {
   .base = { __DRI_USE_INVALIDATE, 1 }
};

static const __DRIimageLookupExtension image_lookup_extension = {
   .base = { __DRI_IMAGE_LOOKUP, 2 },

   .validateEGLImage        = dri_validate_egl_image,
   .lookupEGLImageValidated = dri_lookup_egl_image_validated,
};

static const __DRIimageLoaderExtension image_loader_extension = {
   .base = { __DRI_IMAGE_LOADER, 2 },

   .getBuffers          = image_get_buffers,
   .flushFrontBuffer    = dri_flush_front_buffer,
   .getCapability       = dri_get_capability,
};

static const __DRIswrastLoaderExtension swrast_loader_extension = {
   .base = { __DRI_SWRAST_LOADER, 2 },

   .getDrawableInfo = swrast_get_drawable_info,
   .putImage        = swrast_put_image,
   .getImage        = swrast_get_image,
   .putImage2       = swrast_put_image2
};

static const __DRIkopperLoaderExtension kopper_loader_extension = {
    .base = { __DRI_KOPPER_LOADER, 1 },

    .SetSurfaceCreateInfo   = NULL,
};

static const __DRIextension *gbm_dri_screen_extensions[] = {
   &image_lookup_extension.base,
   &use_invalidate.base,
   &image_loader_extension.base,
   &swrast_loader_extension.base,
   &kopper_loader_extension.base,
   NULL,
};

static int
dri_screen_create_for_driver(struct gbm_dri_device *dri, char *driver_name, bool driver_name_is_inferred)
{
   bool swrast = driver_name == NULL; /* If it's pure swrast, not just swkms. */
   enum dri_screen_type type = DRI_SCREEN_SWRAST;
   if (!swrast) {
      if (!strcmp(driver_name, "zink"))
         type = DRI_SCREEN_KOPPER;
      else if (!strcmp(driver_name, "kms_swrast"))
         type = DRI_SCREEN_KMS_SWRAST;
      else
         type = DRI_SCREEN_DRI3;
   }

   dri->driver_name = swrast ? strdup("swrast") : driver_name;

   dri->swrast = swrast;

   dri->loader_extensions = gbm_dri_screen_extensions;
   dri->screen = driCreateNewScreen3(0, swrast ? -1 : dri->base.v0.fd,
                                             dri->loader_extensions,
                                             type,
                                             &dri->driver_configs, driver_name_is_inferred, true, dri);
   if (dri->screen == NULL)
      goto fail;

   dri->lookup_user_data = NULL;

   return 0;

fail:
   free(dri->driver_name);
   return -1;
}

static int
dri_screen_create(struct gbm_dri_device *dri, bool driver_name_is_inferred)
{
   char *driver_name;

   driver_name = loader_get_driver_for_fd(dri->base.v0.fd);
   if (!driver_name)
      return -1;

   return dri_screen_create_for_driver(dri, driver_name, driver_name_is_inferred);
}

static int
dri_screen_create_sw(struct gbm_dri_device *dri, bool driver_name_is_inferred)
{
   char *driver_name;
   int ret;

   driver_name = strdup("kms_swrast");
   if (!driver_name)
      return -errno;

   ret = dri_screen_create_for_driver(dri, driver_name, driver_name_is_inferred);
   if (ret != 0)
      ret = dri_screen_create_for_driver(dri, NULL, driver_name_is_inferred);
   if (ret != 0)
      return ret;

   dri->software = true;
   return 0;
}

static const struct gbm_dri_visual gbm_dri_visuals_table[] = {
   { GBM_FORMAT_R8, __DRI_IMAGE_FORMAT_R8 },
   { GBM_FORMAT_R16, __DRI_IMAGE_FORMAT_R16 },
   { GBM_FORMAT_GR88, __DRI_IMAGE_FORMAT_GR88 },
   { GBM_FORMAT_GR1616, __DRI_IMAGE_FORMAT_GR1616 },
   { GBM_FORMAT_ARGB1555, __DRI_IMAGE_FORMAT_ARGB1555 },
   { GBM_FORMAT_RGB565, __DRI_IMAGE_FORMAT_RGB565 },
   { GBM_FORMAT_XRGB8888, __DRI_IMAGE_FORMAT_XRGB8888 },
   { GBM_FORMAT_ARGB8888, __DRI_IMAGE_FORMAT_ARGB8888 },
   { GBM_FORMAT_XBGR8888, __DRI_IMAGE_FORMAT_XBGR8888 },
   { GBM_FORMAT_ABGR8888, __DRI_IMAGE_FORMAT_ABGR8888 },
   { GBM_FORMAT_XRGB2101010, __DRI_IMAGE_FORMAT_XRGB2101010 },
   { GBM_FORMAT_ARGB2101010, __DRI_IMAGE_FORMAT_ARGB2101010 },
   { GBM_FORMAT_XBGR2101010, __DRI_IMAGE_FORMAT_XBGR2101010 },
   { GBM_FORMAT_ABGR2101010, __DRI_IMAGE_FORMAT_ABGR2101010 },
   { GBM_FORMAT_XBGR16161616, __DRI_IMAGE_FORMAT_XBGR16161616 },
   { GBM_FORMAT_ABGR16161616, __DRI_IMAGE_FORMAT_ABGR16161616 },
   { GBM_FORMAT_XBGR16161616F, __DRI_IMAGE_FORMAT_XBGR16161616F },
   { GBM_FORMAT_ABGR16161616F, __DRI_IMAGE_FORMAT_ABGR16161616F },
};

static int
gbm_format_to_dri_format(uint32_t gbm_format)
{
   gbm_format = core->v0.format_canonicalize(gbm_format);
   for (size_t i = 0; i < ARRAY_SIZE(gbm_dri_visuals_table); i++) {
      if (gbm_dri_visuals_table[i].gbm_format == gbm_format)
         return gbm_dri_visuals_table[i].dri_image_format;
   }

   return 0;
}

static int
gbm_dri_is_format_supported(struct gbm_device *gbm,
                            uint32_t format,
                            uint32_t usage)
{
   struct gbm_dri_device *dri = gbm_dri_device(gbm);
   int count;

   if ((usage & GBM_BO_USE_CURSOR) && (usage & GBM_BO_USE_RENDERING))
      return 0;

   format = core->v0.format_canonicalize(format);
   if (gbm_format_to_dri_format(format) == 0)
      return 0;

   /* If there is no query, fall back to the small table which was originally
    * here. */
   if (!dri->has_dmabuf_import) {
      switch (format) {
      case GBM_FORMAT_XRGB8888:
      case GBM_FORMAT_ARGB8888:
      case GBM_FORMAT_XBGR8888:
         return 1;
      default:
         return 0;
      }
   }

   /* This returns false if the format isn't supported */
   if (!dri_query_dma_buf_modifiers(dri->screen, format, 0, NULL, NULL,
                                         &count))
      return 0;

   return 1;
}

static int
gbm_dri_get_format_modifier_plane_count(struct gbm_device *gbm,
                                        uint32_t format,
                                        uint64_t modifier)
{
   struct gbm_dri_device *dri = gbm_dri_device(gbm);
   uint64_t plane_count;

   if (!dri->has_dmabuf_import)
      return -1;

   format = core->v0.format_canonicalize(format);
   if (gbm_format_to_dri_format(format) == 0)
      return -1;

   if (!dri2_query_dma_buf_format_modifier_attribs(dri->screen, format, modifier,
         __DRI_IMAGE_FORMAT_MODIFIER_ATTRIB_PLANE_COUNT, &plane_count))
      return -1;

   return plane_count;
}

static int
gbm_dri_bo_write(struct gbm_bo *_bo, const void *buf, size_t count)
{
   struct gbm_dri_bo *bo = gbm_dri_bo(_bo);

   if (bo->image != NULL) {
      errno = EINVAL;
      return -1;
   }

   memcpy(bo->map, buf, count);

   return 0;
}

static int
gbm_dri_bo_get_fd(struct gbm_bo *_bo)
{
   struct gbm_dri_bo *bo = gbm_dri_bo(_bo);
   int fd;

   if (bo->image == NULL)
      return -1;

   if (!dri2_query_image(bo->image, __DRI_IMAGE_ATTRIB_FD, &fd))
      return -1;

   return fd;
}

static int
get_number_planes(struct gbm_dri_device *dri, __DRIimage *image)
{
   int num_planes = 0;

   /* Dumb buffers are single-plane only. */
   if (!image)
      return 1;

   dri2_query_image(image, __DRI_IMAGE_ATTRIB_NUM_PLANES, &num_planes);

   if (num_planes <= 0)
      num_planes = 1;

   return num_planes;
}

static int
gbm_dri_bo_get_planes(struct gbm_bo *_bo)
{
   struct gbm_dri_device *dri = gbm_dri_device(_bo->gbm);
   struct gbm_dri_bo *bo = gbm_dri_bo(_bo);

   return get_number_planes(dri, bo->image);
}

static union gbm_bo_handle
gbm_dri_bo_get_handle_for_plane(struct gbm_bo *_bo, int plane)
{
   struct gbm_dri_device *dri = gbm_dri_device(_bo->gbm);
   struct gbm_dri_bo *bo = gbm_dri_bo(_bo);
   union gbm_bo_handle ret;
   ret.s32 = -1;

   if (plane >= get_number_planes(dri, bo->image)) {
      errno = EINVAL;
      return ret;
   }

   /* dumb BOs can only utilize non-planar formats */
   if (!bo->image) {
      assert(plane == 0);
      ret.s32 = bo->handle;
      return ret;
   }

   __DRIimage *image = dri2_from_planar(bo->image, plane, NULL);
   if (image) {
      dri2_query_image(image, __DRI_IMAGE_ATTRIB_HANDLE, &ret.s32);
      dri2_destroy_image(image);
   } else {
      assert(plane == 0);
      dri2_query_image(bo->image, __DRI_IMAGE_ATTRIB_HANDLE, &ret.s32);
   }

   return ret;
}

static int
gbm_dri_bo_get_plane_fd(struct gbm_bo *_bo, int plane)
{
   struct gbm_dri_device *dri = gbm_dri_device(_bo->gbm);
   struct gbm_dri_bo *bo = gbm_dri_bo(_bo);
   int fd = -1;

   if (!dri->has_dmabuf_import) {
      /* Preserve legacy behavior if plane is 0 */
      if (plane == 0)
         return gbm_dri_bo_get_fd(_bo);

      errno = ENOSYS;
      return -1;
   }

   /* dumb BOs can only utilize non-planar formats */
   if (!bo->image) {
      errno = EINVAL;
      return -1;
   }

   if (plane >= get_number_planes(dri, bo->image)) {
      errno = EINVAL;
      return -1;
   }

   __DRIimage *image = dri2_from_planar(bo->image, plane, NULL);
   if (image) {
      dri2_query_image(image, __DRI_IMAGE_ATTRIB_FD, &fd);
      dri2_destroy_image(image);
   } else {
      assert(plane == 0);
      dri2_query_image(bo->image, __DRI_IMAGE_ATTRIB_FD, &fd);
   }

   return fd;
}

static uint32_t
gbm_dri_bo_get_stride(struct gbm_bo *_bo, int plane)
{
   struct gbm_dri_device *dri = gbm_dri_device(_bo->gbm);
   struct gbm_dri_bo *bo = gbm_dri_bo(_bo);
   __DRIimage *image;
   int stride = 0;

   if (!dri->has_dmabuf_import) {
      /* Preserve legacy behavior if plane is 0 */
      if (plane == 0)
         return _bo->v0.stride;

      errno = ENOSYS;
      return 0;
   }

   if (plane >= get_number_planes(dri, bo->image)) {
      errno = EINVAL;
      return 0;
   }

   if (bo->image == NULL) {
      assert(plane == 0);
      return _bo->v0.stride;
   }

   image = dri2_from_planar(bo->image, plane, NULL);
   if (image) {
      dri2_query_image(image, __DRI_IMAGE_ATTRIB_STRIDE, &stride);
      dri2_destroy_image(image);
   } else {
      assert(plane == 0);
      dri2_query_image(bo->image, __DRI_IMAGE_ATTRIB_STRIDE, &stride);
   }

   return (uint32_t)stride;
}

static uint32_t
gbm_dri_bo_get_offset(struct gbm_bo *_bo, int plane)
{
   struct gbm_dri_device *dri = gbm_dri_device(_bo->gbm);
   struct gbm_dri_bo *bo = gbm_dri_bo(_bo);
   int offset = 0;

   if (plane >= get_number_planes(dri, bo->image))
      return 0;

    /* Dumb images have no offset */
   if (bo->image == NULL) {
      assert(plane == 0);
      return 0;
   }

   __DRIimage *image = dri2_from_planar(bo->image, plane, NULL);
   if (image) {
      dri2_query_image(image, __DRI_IMAGE_ATTRIB_OFFSET, &offset);
      dri2_destroy_image(image);
   } else {
      assert(plane == 0);
      dri2_query_image(bo->image, __DRI_IMAGE_ATTRIB_OFFSET, &offset);
   }

   return (uint32_t)offset;
}

static uint64_t
gbm_dri_bo_get_modifier(struct gbm_bo *_bo)
{
   struct gbm_dri_bo *bo = gbm_dri_bo(_bo);

   /* Dumb buffers have no modifiers */
   if (!bo->image)
      return DRM_FORMAT_MOD_LINEAR;

   uint64_t ret = 0;
   int mod;
   if (!dri2_query_image(bo->image, __DRI_IMAGE_ATTRIB_MODIFIER_UPPER,
                               &mod))
      return DRM_FORMAT_MOD_INVALID;

   ret = (uint64_t)mod << 32;

   if (!dri2_query_image(bo->image, __DRI_IMAGE_ATTRIB_MODIFIER_LOWER,
                               &mod))
      return DRM_FORMAT_MOD_INVALID;

   ret |= (uint64_t)(mod & 0xffffffff);

   return ret;
}

static void
gbm_dri_bo_destroy(struct gbm_bo *_bo)
{
   struct gbm_dri_device *dri = gbm_dri_device(_bo->gbm);
   struct gbm_dri_bo *bo = gbm_dri_bo(_bo);
   struct drm_mode_destroy_dumb arg;

   if (bo->image != NULL) {
      dri2_destroy_image(bo->image);
   } else {
      gbm_dri_bo_unmap_dumb(bo);
      memset(&arg, 0, sizeof(arg));
      arg.handle = bo->handle;
      drmIoctl(dri->base.v0.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &arg);
   }

   free(bo);
}

static struct gbm_bo *
gbm_dri_bo_import(struct gbm_device *gbm,
                  uint32_t type, void *buffer, uint32_t usage)
{
   struct gbm_dri_device *dri = gbm_dri_device(gbm);
   struct gbm_dri_bo *bo;
   __DRIimage *image;
   unsigned dri_use = 0;
   int gbm_format;

   if (!dri->has_dmabuf_import) {
      errno = ENOSYS;
      return NULL;
   }

   switch (type) {
#if HAVE_WAYLAND_PLATFORM
   case GBM_BO_IMPORT_WL_BUFFER:
   {
      struct wl_drm_buffer *wb;

      if (!dri->wl_drm) {
         errno = EINVAL;
         return NULL;
      }

      wb = wayland_drm_buffer_get(dri->wl_drm, (struct wl_resource *) buffer);
      if (!wb) {
         errno = EINVAL;
         return NULL;
      }

      image = dri2_dup_image(wb->driver_buffer, NULL);

      /* GBM_FORMAT_* is identical to WL_DRM_FORMAT_*, so no conversion
       * required. */
      gbm_format = wb->format;
      break;
   }
#endif

   case GBM_BO_IMPORT_EGL_IMAGE:
   {
      if (dri->lookup_image_validated == NULL) {
         errno = EINVAL;
         return NULL;
      }

      if (!dri->validate_image(buffer, dri->lookup_user_data)) {
         errno = EINVAL;
         return NULL;
      }
      image = dri->lookup_image_validated(buffer, dri->lookup_user_data);
      image = dri2_dup_image(image, NULL);
      dri2_query_image(image, __DRI_IMAGE_ATTRIB_FOURCC, &gbm_format);
      if (gbm_format == DRM_FORMAT_INVALID) {
         errno = EINVAL;
         dri2_destroy_image(image);
         return NULL;
      }
      break;
   }

   case GBM_BO_IMPORT_FD:
   {
      struct gbm_import_fd_data *fd_data = buffer;
      int stride = fd_data->stride, offset = 0;
      int fourcc;

      /* GBM's GBM_FORMAT_* tokens are a strict superset of the DRI FourCC
       * tokens accepted by createImageFromDmaBufs, except for not supporting
       * the sARGB format. */
      fourcc = core->v0.format_canonicalize(fd_data->format);

      image = dri2_from_dma_bufs(dri->screen,
                                 fd_data->width,
                                 fd_data->height,
                                 fourcc,
                                 DRM_FORMAT_MOD_INVALID,
                                 &fd_data->fd, 1,
                                 &stride, &offset,
                                 0, 0, 0, 0, 0,
                                 NULL, NULL);
      if (image == NULL) {
         errno = EINVAL;
         return NULL;
      }
      gbm_format = fd_data->format;
      break;
   }

   case GBM_BO_IMPORT_FD_MODIFIER:
   {
      struct gbm_import_fd_modifier_data *fd_data = buffer;
      unsigned int error;
      int fourcc;

      /* GBM's GBM_FORMAT_* tokens are a strict superset of the DRI FourCC
       * tokens accepted by createImageFromDmaBufs, except for not supporting
       * the sARGB format. */
      fourcc = core->v0.format_canonicalize(fd_data->format);

      image = dri2_from_dma_bufs(dri->screen, fd_data->width,
                                                 fd_data->height, fourcc,
                                                 fd_data->modifier,
                                                 fd_data->fds,
                                                 fd_data->num_fds,
                                                 fd_data->strides,
                                                 fd_data->offsets,
                                                 0, 0, 0, 0,
                                                 0, &error, NULL);
      if (image == NULL) {
         errno = ENOSYS;
         return NULL;
      }

      gbm_format = fourcc;
      break;
   }

   default:
      errno = ENOSYS;
      return NULL;
   }


   bo = calloc(1, sizeof *bo);
   if (bo == NULL) {
      dri2_destroy_image(image);
      return NULL;
   }

   bo->image = image;

   if (usage & GBM_BO_USE_SCANOUT)
      dri_use |= __DRI_IMAGE_USE_SCANOUT;
   if (usage & GBM_BO_USE_CURSOR)
      dri_use |= __DRI_IMAGE_USE_CURSOR;
   if (!dri2_validate_usage(bo->image, dri_use)) {
      errno = EINVAL;
      dri2_destroy_image(bo->image);
      free(bo);
      return NULL;
   }

   bo->base.gbm = gbm;
   bo->base.v0.format = gbm_format;

   dri2_query_image(bo->image, __DRI_IMAGE_ATTRIB_WIDTH,
                          (int*)&bo->base.v0.width);
   dri2_query_image(bo->image, __DRI_IMAGE_ATTRIB_HEIGHT,
                          (int*)&bo->base.v0.height);
   dri2_query_image(bo->image, __DRI_IMAGE_ATTRIB_STRIDE,
                          (int*)&bo->base.v0.stride);
   dri2_query_image(bo->image, __DRI_IMAGE_ATTRIB_HANDLE,
                          &bo->base.v0.handle.s32);

   return &bo->base;
}

static struct gbm_bo *
create_dumb(struct gbm_device *gbm,
                  uint32_t width, uint32_t height,
                  uint32_t format, uint32_t usage)
{
   struct gbm_dri_device *dri = gbm_dri_device(gbm);
   struct drm_mode_create_dumb create_arg;
   struct gbm_dri_bo *bo;
   struct drm_mode_destroy_dumb destroy_arg;
   int ret;
   int is_cursor, is_scanout;

   is_cursor = (usage & GBM_BO_USE_CURSOR) != 0 &&
      format == GBM_FORMAT_ARGB8888;
   is_scanout = (usage & GBM_BO_USE_SCANOUT) != 0 &&
      (format == GBM_FORMAT_XRGB8888 || format == GBM_FORMAT_XBGR8888);
   if (!is_cursor && !is_scanout) {
      errno = EINVAL;
      return NULL;
   }

   bo = calloc(1, sizeof *bo);
   if (bo == NULL)
      return NULL;

   memset(&create_arg, 0, sizeof(create_arg));
   create_arg.bpp = 32;
   create_arg.width = width;
   create_arg.height = height;

   ret = drmIoctl(dri->base.v0.fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg);
   if (ret)
      goto free_bo;

   bo->base.gbm = gbm;
   bo->base.v0.width = width;
   bo->base.v0.height = height;
   bo->base.v0.stride = create_arg.pitch;
   bo->base.v0.format = format;
   bo->base.v0.handle.u32 = create_arg.handle;
   bo->handle = create_arg.handle;
   bo->size = create_arg.size;

   if (gbm_dri_bo_map_dumb(bo) == NULL)
      goto destroy_dumb;

   return &bo->base;

destroy_dumb:
   memset(&destroy_arg, 0, sizeof destroy_arg);
   destroy_arg.handle = create_arg.handle;
   drmIoctl(dri->base.v0.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
free_bo:
   free(bo);

   return NULL;
}

static struct gbm_bo *
gbm_dri_bo_create(struct gbm_device *gbm,
                  uint32_t width, uint32_t height,
                  uint32_t format, uint32_t usage,
                  const uint64_t *modifiers,
                  const unsigned int count)
{
   struct gbm_dri_device *dri = gbm_dri_device(gbm);
   struct gbm_dri_bo *bo;
   int dri_format;
   unsigned dri_use = 0;
   uint64_t *mods_comp = NULL;
   uint64_t *mods_filtered = NULL;
   unsigned int count_filtered = 0;

   format = core->v0.format_canonicalize(format);

   if (usage & GBM_BO_USE_WRITE || !dri->has_dmabuf_export)
      return create_dumb(gbm, width, height, format, usage);

   bo = calloc(1, sizeof *bo);
   if (bo == NULL)
      return NULL;

   bo->base.gbm = gbm;
   bo->base.v0.width = width;
   bo->base.v0.height = height;
   bo->base.v0.format = format;

   dri_format = gbm_format_to_dri_format(format);
   if (dri_format == 0) {
      errno = EINVAL;
      goto failed;
   }

   if (usage & GBM_BO_USE_SCANOUT)
      dri_use |= __DRI_IMAGE_USE_SCANOUT;
   if (usage & GBM_BO_USE_CURSOR)
      dri_use |= __DRI_IMAGE_USE_CURSOR;
   if (usage & GBM_BO_USE_LINEAR)
      dri_use |= __DRI_IMAGE_USE_LINEAR;
   if (usage & GBM_BO_USE_PROTECTED)
      dri_use |= __DRI_IMAGE_USE_PROTECTED;
   if (usage & GBM_BO_USE_FRONT_RENDERING) {
      assert (!(usage & GBM_BO_EXPLICIT_FLUSH));
      dri_use |= __DRI_IMAGE_USE_FRONT_RENDERING;
   }
   if (usage & GBM_BO_EXPLICIT_FLUSH) {
      assert (!(usage & GBM_BO_USE_FRONT_RENDERING));
      dri_use |= __DRI_IMAGE_USE_BACKBUFFER;
   }

   /* Gallium drivers requires shared in order to get the handle/stride */
   dri_use |= __DRI_IMAGE_USE_SHARE;

   /* If the driver supports fixed-rate compression, filter the acceptable
    * modifiers by the compression rate. */
   if (modifiers && dri->has_compression_modifiers) {
      enum __DRIFixedRateCompression comp = __DRI_FIXED_RATE_COMPRESSION_NONE;

      switch (usage & GBM_BO_FIXED_COMPRESSION_MASK) {
#define CASE(x) case GBM_BO_FIXED_COMPRESSION_ ## x: comp = __DRI_FIXED_RATE_COMPRESSION_ ## x; break;
      CASE(DEFAULT);
      CASE(1BPC);
      CASE(2BPC);
      CASE(3BPC);
      CASE(4BPC);
      CASE(5BPC);
      CASE(6BPC);
      CASE(7BPC);
      CASE(8BPC);
      CASE(9BPC);
      CASE(10BPC);
      CASE(11BPC);
      CASE(12BPC);
#undef CASE
      default:
         break;
      }

      int count_comp = 0;

      /* Find how many acceptable modifiers there are for our rate. If there
       * are none, fall back to no compression, as it is not mandatory to use
       * the specified compression rate. */
      if (!dri2_query_compression_modifiers(dri->screen, format, comp,
                                                 0, NULL, &count_comp) ||
         count_comp == 0) {
         if (comp == __DRI_FIXED_RATE_COMPRESSION_NONE) {
            errno = EINVAL;
            goto failed;
         }

         comp = __DRI_FIXED_RATE_COMPRESSION_NONE;
         if (!dri2_query_compression_modifiers(dri->screen, format, comp,
                                                    0, NULL, &count_comp)) {
            errno = EINVAL;
            goto failed;
         }
      }

      if (count_comp == 0) {
         errno = EINVAL;
         goto failed;
      }

      mods_comp = malloc(count_comp * sizeof(uint64_t));
      mods_filtered = malloc(count_comp * sizeof(uint64_t));
      if (!mods_comp || !mods_filtered) {
         errno = ENOMEM;
         goto failed;
      }

      if (!dri2_query_compression_modifiers(dri->screen, format, comp,
                                                 count_comp, mods_comp,
                                                 &count_comp)) {
         errno = ENOMEM;
         goto failed;
      }


      /* Intersect the list of user-supplied acceptable modifiers with the set
       * of modifiers acceptable for this compression rate. */
      for (unsigned int i = 0; i < count_comp; i++) {
         for (unsigned int j = 0; j < count; j++) {
            if (mods_comp[i] == modifiers[j]) {
               mods_filtered[count_filtered++] = mods_comp[i];
               break;
            }
         }
      }

      free(mods_comp);
      mods_comp = NULL;
   }

   bo->image = dri_create_image_with_modifiers(dri->screen, width, height,
                                       dri_format, dri_use,
                                       mods_filtered ? mods_filtered : modifiers,
                                       mods_filtered ? count_filtered : count,
                                       bo);
   if (bo->image == NULL)
      goto failed;

   free(mods_filtered);
   mods_filtered = NULL;

   dri2_query_image(bo->image, __DRI_IMAGE_ATTRIB_HANDLE,
                          &bo->base.v0.handle.s32);
   dri2_query_image(bo->image, __DRI_IMAGE_ATTRIB_STRIDE,
                          (int *) &bo->base.v0.stride);

   return &bo->base;

failed:
   free(mods_comp);
   free(mods_filtered);
   free(bo);
   return NULL;
}

static void *
gbm_dri_bo_map(struct gbm_bo *_bo,
              uint32_t x, uint32_t y,
              uint32_t width, uint32_t height,
              uint32_t flags, uint32_t *stride, void **map_data)
{
   struct gbm_dri_device *dri = gbm_dri_device(_bo->gbm);
   struct gbm_dri_bo *bo = gbm_dri_bo(_bo);

   /* If it's a dumb buffer, we already have a mapping */
   if (bo->map) {
      *map_data = (char *)bo->map + (bo->base.v0.stride * y) + (x * 4);
      *stride = bo->base.v0.stride;
      return *map_data;
   }

   mtx_lock(&dri->mutex);
   if (!dri->context) {
      unsigned error;

      dri->context = driCreateContextAttribs(dri->screen,
                                             __DRI_API_OPENGL,
                                             NULL, NULL, 0, NULL,
                                             &error, NULL);
   }
   assert(dri->context);
   mtx_unlock(&dri->mutex);

   /* GBM flags and DRI flags are the same, so just pass them on */
   return dri2_map_image(dri->context, bo->image, x, y,
                               width, height, flags, (int *)stride,
                               map_data);
}

static void
gbm_dri_bo_unmap(struct gbm_bo *_bo, void *map_data)
{
   struct gbm_dri_device *dri = gbm_dri_device(_bo->gbm);
   struct gbm_dri_bo *bo = gbm_dri_bo(_bo);

   /* Check if it's a dumb buffer and check the pointer is in range */
   if (bo->map) {
      assert(map_data >= bo->map);
      assert(map_data < (bo->map + bo->size));
      return;
   }

   if (!dri->context)
      return;

   dri2_unmap_image(dri->context, bo->image, map_data);

   /*
    * Not all DRI drivers use direct maps. They may queue up DMA operations
    * on the mapping context. Since there is no explicit gbm flush
    * mechanism, we need to flush here.
    */
   dri_flush(dri->context, NULL, __DRI2_FLUSH_CONTEXT, 0);
}


static struct gbm_surface *
gbm_dri_surface_create(struct gbm_device *gbm,
                       uint32_t width, uint32_t height,
		       uint32_t format, uint32_t flags,
                       const uint64_t *modifiers, const unsigned count)
{
   struct gbm_dri_surface *surf;

   if (count)
      assert(modifiers);

   /* It's acceptable to create an image with INVALID modifier in the list,
    * but it cannot be on the only modifier (since it will certainly fail
    * later). While we could easily catch this after modifier creation, doing
    * the check here is a convenient debug check likely pointing at whatever
    * interface the client is using to build its modifier list.
    */
   if (count == 1 && modifiers[0] == DRM_FORMAT_MOD_INVALID) {
      fprintf(stderr, "Only invalid modifier specified\n");
      errno = EINVAL;
   }

   surf = calloc(1, sizeof *surf);
   if (surf == NULL) {
      errno = ENOMEM;
      return NULL;
   }

   surf->base.gbm = gbm;
   surf->base.v0.width = width;
   surf->base.v0.height = height;
   surf->base.v0.format = core->v0.format_canonicalize(format);
   surf->base.v0.flags = flags | GBM_BO_EXPLICIT_FLUSH;
   if (!modifiers) {
      assert(!count);
      return &surf->base;
   }

   surf->base.v0.modifiers = calloc(count, sizeof(*modifiers));
   if (count && !surf->base.v0.modifiers) {
      errno = ENOMEM;
      free(surf);
      return NULL;
   }

   /* TODO: We are deferring validation of modifiers until the image is actually
    * created. This deferred creation can fail due to a modifier-format
    * mismatch. The result is the client has a surface but no object to back it.
    */
   surf->base.v0.count = count;
   memcpy(surf->base.v0.modifiers, modifiers, count * sizeof(*modifiers));

   return &surf->base;
}

static void
gbm_dri_surface_destroy(struct gbm_surface *_surf)
{
   struct gbm_dri_surface *surf = gbm_dri_surface(_surf);

   free(surf->base.v0.modifiers);
   free(surf);
}

static void
dri_destroy(struct gbm_device *gbm)
{
   struct gbm_dri_device *dri = gbm_dri_device(gbm);
   unsigned i;

   if (dri->context)
      driDestroyContext(dri->context);

   driDestroyScreen(dri->screen);
   for (i = 0; dri->driver_configs[i]; i++)
      free((__DRIconfig *) dri->driver_configs[i]);
   free(dri->driver_configs);
   free(dri->driver_name);

   free(dri);
}

static struct gbm_device *
dri_device_create(int fd, uint32_t gbm_backend_version)
{
   struct gbm_dri_device *dri;
   int ret;
   bool force_sw;

   dri = calloc(1, sizeof *dri);
   if (!dri)
      return NULL;

   dri->base.v0.fd = fd;
   dri->base.v0.backend_version = gbm_backend_version;
   dri->base.v0.bo_create = gbm_dri_bo_create;
   dri->base.v0.bo_import = gbm_dri_bo_import;
   dri->base.v0.bo_map = gbm_dri_bo_map;
   dri->base.v0.bo_unmap = gbm_dri_bo_unmap;
   dri->base.v0.is_format_supported = gbm_dri_is_format_supported;
   dri->base.v0.get_format_modifier_plane_count =
      gbm_dri_get_format_modifier_plane_count;
   dri->base.v0.bo_write = gbm_dri_bo_write;
   dri->base.v0.bo_get_fd = gbm_dri_bo_get_fd;
   dri->base.v0.bo_get_planes = gbm_dri_bo_get_planes;
   dri->base.v0.bo_get_handle = gbm_dri_bo_get_handle_for_plane;
   dri->base.v0.bo_get_plane_fd = gbm_dri_bo_get_plane_fd;
   dri->base.v0.bo_get_stride = gbm_dri_bo_get_stride;
   dri->base.v0.bo_get_offset = gbm_dri_bo_get_offset;
   dri->base.v0.bo_get_modifier = gbm_dri_bo_get_modifier;
   dri->base.v0.bo_destroy = gbm_dri_bo_destroy;
   dri->base.v0.destroy = dri_destroy;
   dri->base.v0.surface_create = gbm_dri_surface_create;
   dri->base.v0.surface_destroy = gbm_dri_surface_destroy;

   dri->base.v0.name = "drm";

   dri->visual_table = gbm_dri_visuals_table;
   dri->num_visuals = ARRAY_SIZE(gbm_dri_visuals_table);

   mtx_init(&dri->mutex, mtx_plain);

   force_sw = debug_get_bool_option("GBM_ALWAYS_SOFTWARE", false);
   if (!force_sw) {
      ret = dri_screen_create(dri, false);
      if (ret)
         ret = dri_screen_create_sw(dri, true);
   } else {
      ret = dri_screen_create_sw(dri, false);
   }

   if (ret)
      goto err_dri;

   struct dri_screen *screen = dri_screen(dri->screen);
   struct pipe_screen *pscreen = screen->base.screen;
#ifdef HAVE_LIBDRM
   if (pscreen->get_param(pscreen, PIPE_CAP_DMABUF) & DRM_PRIME_CAP_IMPORT)
      dri->has_dmabuf_import = true;
   if (pscreen->get_param(pscreen, PIPE_CAP_DMABUF) & DRM_PRIME_CAP_EXPORT)
      dri->has_dmabuf_export = true;
#endif
   dri->has_compression_modifiers = pscreen->query_compression_rates &&
                                    pscreen->query_compression_modifiers;

   return &dri->base;

err_dri:
   free(dri);

   return NULL;
}

struct gbm_backend gbm_dri_backend = {
   .v0.backend_version = GBM_BACKEND_ABI_VERSION,
   .v0.backend_name = "dri",
   .v0.create_device = dri_device_create,
};

struct gbm_backend * gbmint_get_backend(const struct gbm_core *gbm_core);

PUBLIC struct gbm_backend *
gbmint_get_backend(const struct gbm_core *gbm_core) {
   core = gbm_core;
   return &gbm_dri_backend;
};
