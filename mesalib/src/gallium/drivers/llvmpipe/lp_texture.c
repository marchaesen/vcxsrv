/**************************************************************************
 *
 * Copyright 2006 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
 /*
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  *   Michel Dänzer <daenzer@vmware.com>
  */

#include <stdio.h>

#include "pipe/p_context.h"
#include "pipe/p_defines.h"

#include "util/detect_os.h"
#include "util/simple_mtx.h"
#include "util/u_inlines.h"
#include "util/u_cpu_detect.h"
#include "util/format/u_format.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_transfer.h"

#if DETECT_OS_POSIX
#include "util/os_mman.h"
#endif

#include "lp_context.h"
#include "lp_flush.h"
#include "lp_screen.h"
#include "lp_texture.h"
#include "lp_setup.h"
#include "lp_state.h"
#include "lp_rast.h"

#include "frontend/sw_winsys.h"
#include "git_sha1.h"

#ifndef _WIN32
#include "drm-uapi/drm_fourcc.h"
#endif

#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/udmabuf.h>
#include <util/os_file.h>
#endif

#if MESA_DEBUG
static struct llvmpipe_resource resource_list;
static simple_mtx_t resource_list_mutex = SIMPLE_MTX_INITIALIZER;
#endif
static unsigned id_counter = 0;


#ifdef PIPE_MEMORY_FD

static const char *driver_id = "llvmpipe" MESA_GIT_SHA1;

#endif

/**
 * Conventional allocation path for non-display textures:
 * Compute strides and allocate data (unless asked not to).
 */
static bool
llvmpipe_texture_layout(struct llvmpipe_screen *screen,
                        struct llvmpipe_resource *lpr,
                        bool allocate)
{
   struct pipe_resource *pt = &lpr->base;
   unsigned width = pt->width0;
   unsigned height = pt->height0;
   unsigned depth = pt->depth0;
   uint64_t total_size = 0;
   unsigned layers = pt->array_size;
   unsigned num_samples = util_res_sample_count(pt);

   /* XXX: This alignment here (same for displaytarget) was added for the
    * purpose of ARB_map_buffer_alignment. I am not convinced it's needed for
    * non-buffer resources. Otherwise we'd want the max of cacheline size and
    * 16 (max size of a block for all formats) though this should not be
    * strictly necessary neither. In any case it can only affect compressed or
    * 1d textures.
    */
   uint64_t mip_align = MAX2(64, util_get_cpu_caps()->cacheline);

   /* KVM on Linux requires memory mapping to be aligned to the page size,
    * otherwise Linux kernel errors out on trying to map host GPU mapping
    * to guest (ARB_map_buffer_range). The improper alignment creates trouble
    * for the virgl driver when host uses llvmpipe, causing Qemu and crosvm to
    * bail out on the KVM error.
    */
   if (lpr->base.flags & PIPE_RESOURCE_FLAG_SPARSE)
      mip_align = 64 * 1024;
   else if (lpr->base.flags & PIPE_RESOURCE_FLAG_MAP_PERSISTENT)
      os_get_page_size(&mip_align);

   assert(LP_MAX_TEXTURE_2D_LEVELS <= LP_MAX_TEXTURE_LEVELS);
   assert(LP_MAX_TEXTURE_3D_LEVELS <= LP_MAX_TEXTURE_LEVELS);

   uint32_t dimensions = 1;
   switch (pt->target) {
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D_ARRAY:
      dimensions = 2;
      break;
   case PIPE_TEXTURE_3D:
      dimensions = 3;
      break;
   default:
      break;
   }

   uint32_t sparse_tile_size[3] = {
      util_format_get_tilesize(pt->format, dimensions, pt->nr_samples, 0),
      util_format_get_tilesize(pt->format, dimensions, pt->nr_samples, 1),
      util_format_get_tilesize(pt->format, dimensions, pt->nr_samples, 2),
   };

   for (unsigned level = 0; level <= pt->last_level; level++) {
      uint64_t mipsize;
      unsigned align_x, align_y, align_z, nblocksx, nblocksy, block_size, num_slices;

      /* Row stride and image stride */

      /* For non-compressed formats we need 4x4 pixel alignment
       * so we can read/write LP_RASTER_BLOCK_SIZE when rendering to them.
       * We also want cache line size in x direction,
       * otherwise same cache line could end up in multiple threads.
       * For explicit 1d resources however we reduce this to 4x1 and
       * handle specially in render output code (as we need to do special
       * handling there for buffers in any case).
       */
      if (util_format_is_compressed(pt->format)) {
         align_x = align_y = 1;
      } else {
         align_x = LP_RASTER_BLOCK_SIZE;
         if (llvmpipe_resource_is_1d(&lpr->base))
            align_y = 1;
         else
            align_y = LP_RASTER_BLOCK_SIZE;
      }
      align_z = 1;

      nblocksx = util_format_get_nblocksx(pt->format,
                                          align(width, align_x));
      nblocksy = util_format_get_nblocksy(pt->format,
                                          align(height, align_y));
      block_size = util_format_get_blocksize(pt->format);

      if (pt->flags & PIPE_RESOURCE_FLAG_SPARSE) {
         nblocksx = align(nblocksx, sparse_tile_size[0]);
         nblocksy = align(nblocksy, sparse_tile_size[1]);
         align_z = MAX2(align_z, sparse_tile_size[2]);
      }

      if (util_format_is_compressed(pt->format))
         lpr->row_stride[level] = nblocksx * block_size;
      else
         lpr->row_stride[level] = align(nblocksx * block_size,
                                        util_get_cpu_caps()->cacheline);

      lpr->img_stride[level] = (uint64_t)lpr->row_stride[level] * nblocksy;

      /* Number of 3D image slices, cube faces or texture array layers */
      if (lpr->base.target == PIPE_TEXTURE_CUBE) {
         assert(layers == 6);
      }

      if (lpr->base.target == PIPE_TEXTURE_3D)
         num_slices = align(depth, align_z);
      else if (lpr->base.target == PIPE_TEXTURE_1D_ARRAY ||
               lpr->base.target == PIPE_TEXTURE_2D_ARRAY ||
               lpr->base.target == PIPE_TEXTURE_CUBE ||
               lpr->base.target == PIPE_TEXTURE_CUBE_ARRAY)
         num_slices = layers;
      else
         num_slices = 1;

      mipsize = lpr->img_stride[level] * num_slices;
      lpr->mip_offsets[level] = total_size;

      total_size += align64(mipsize, mip_align);

      /* Compute size of next mipmap level */
      width = u_minify(width, 1);
      height = u_minify(height, 1);
      depth = u_minify(depth, 1);
   }

   lpr->sample_stride = total_size;
   total_size *= num_samples;

   lpr->size_required = total_size;
   if (allocate) {
      if (total_size > LP_MAX_TEXTURE_SIZE)
         goto fail;

      lpr->tex_data = align_malloc(total_size, mip_align);
      if (!lpr->tex_data) {
         return false;
      } else {
         memset(lpr->tex_data, 0, total_size);
      }
   }
   if (lpr->base.flags & PIPE_RESOURCE_FLAG_SPARSE) {
      uint64_t page_align;
      os_get_page_size(&page_align);
      lpr->size_required = align64(lpr->size_required, page_align);
   }

   return true;

fail:
   return false;
}


/**
 * Check the size of the texture specified by 'res'.
 * \return TRUE if OK, FALSE if too large.
 */
static bool
llvmpipe_can_create_resource(struct pipe_screen *screen,
                             const struct pipe_resource *res)
{
   struct llvmpipe_resource lpr;
   memset(&lpr, 0, sizeof(lpr));
   lpr.base = *res;
   if (!llvmpipe_texture_layout(llvmpipe_screen(screen), &lpr, false))
      return false;

   return lpr.size_required <= LP_MAX_TEXTURE_SIZE;
}


static bool
llvmpipe_displaytarget_layout(struct llvmpipe_screen *screen,
                              struct llvmpipe_resource *lpr,
                              const void *map_front_private)
{
   struct sw_winsys *winsys = screen->winsys;

   /* Round up the surface size to a multiple of the tile size to
    * avoid tile clipping.
    */
   const unsigned width = MAX2(1, align(lpr->base.width0, TILE_SIZE));
   const unsigned height = MAX2(1, align(lpr->base.height0, TILE_SIZE));

   lpr->dt = winsys->displaytarget_create(winsys,
                                          lpr->base.bind,
                                          lpr->base.format,
                                          width, height,
                                          64,
                                          map_front_private,
                                          &lpr->row_stride[0] );

   return lpr->dt != NULL;
}


static struct pipe_resource *
llvmpipe_resource_create_all(struct pipe_screen *_screen,
                             const struct pipe_resource *templat,
                             const void *map_front_private,
                             bool alloc_backing)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   struct llvmpipe_resource *lpr = CALLOC_STRUCT(llvmpipe_resource);
   if (!lpr)
      return NULL;

   lpr->base = *templat;
   lpr->screen = screen;
   pipe_reference_init(&lpr->base.reference, 1);
   lpr->base.screen = &screen->base;

#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
   lpr->dmabuf_alloc = NULL;
#endif

   /* assert(lpr->base.bind); */

   if (llvmpipe_resource_is_texture(&lpr->base)) {
      if (lpr->base.bind & (PIPE_BIND_DISPLAY_TARGET |
                            PIPE_BIND_SCANOUT |
                            PIPE_BIND_SHARED)) {
         /* displayable surface */
         if (!llvmpipe_displaytarget_layout(screen, lpr, map_front_private))
            goto fail;
      } else {
         /* texture map */
         if (!llvmpipe_texture_layout(screen, lpr, alloc_backing))
            goto fail;

         if (templat->flags & PIPE_RESOURCE_FLAG_SPARSE) {
#if DETECT_OS_LINUX
            lpr->tex_data = os_mmap(NULL, lpr->size_required, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED,
                                    -1, 0);
            madvise(lpr->tex_data, lpr->size_required, MADV_DONTNEED);
#endif

            lpr->residency = calloc(DIV_ROUND_UP(lpr->size_required, 64 * 1024 * sizeof(uint32_t) * 8), sizeof(uint32_t));
         }
      }
   } else {
      /* other data (vertex buffer, const buffer, etc) */
      const uint bytes = templat->width0;
      assert(util_format_get_blocksize(templat->format) == 1);
      assert(templat->height0 == 1);
      assert(templat->depth0 == 1);
      assert(templat->last_level == 0);
      /*
       * Reserve some extra storage since if we'd render to a buffer we
       * read/write always LP_RASTER_BLOCK_SIZE pixels, but the element
       * offset doesn't need to be aligned to LP_RASTER_BLOCK_SIZE.
       */
      /*
       * buffers don't really have stride but it's probably safer
       * (for code doing same calculations for buffers and textures)
       * to put something sane in there.
       */
      lpr->row_stride[0] = bytes;

      lpr->size_required = bytes;
      if (!(templat->flags & PIPE_RESOURCE_FLAG_DONT_OVER_ALLOCATE))
         lpr->size_required += (LP_RASTER_BLOCK_SIZE - 1) * 4 * sizeof(float);

      uint64_t alignment = sizeof(uint64_t) * 16;
      if (alloc_backing) {
         if (templat->flags & PIPE_RESOURCE_FLAG_MAP_PERSISTENT)
            os_get_page_size(&alignment);

         lpr->data = align_malloc(lpr->size_required, alignment);

         if (!lpr->data)
            goto fail;
         memset(lpr->data, 0, bytes);
      }

      if (templat->flags & PIPE_RESOURCE_FLAG_SPARSE) {
         os_get_page_size(&alignment);
         lpr->size_required = align64(lpr->size_required, alignment);
#if DETECT_OS_LINUX
         lpr->data = os_mmap(NULL, lpr->size_required, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED,
                             -1, 0);
         madvise(lpr->data, lpr->size_required, MADV_DONTNEED);
#endif
      }
   }

   lpr->id = id_counter++;

#if MESA_DEBUG
   simple_mtx_lock(&resource_list_mutex);
   list_addtail(&lpr->list, &resource_list.list);
   simple_mtx_unlock(&resource_list_mutex);
#endif

   return &lpr->base;

 fail:
   FREE(lpr);
   return NULL;
}


static struct pipe_resource *
llvmpipe_resource_create_front(struct pipe_screen *_screen,
                               const struct pipe_resource *templat,
                               const void *map_front_private)
{
   return llvmpipe_resource_create_all(_screen, templat,
                                       map_front_private, true);
}


static struct pipe_resource *
llvmpipe_resource_create(struct pipe_screen *_screen,
                         const struct pipe_resource *templat)
{
   return llvmpipe_resource_create_front(_screen, templat, NULL);
}

#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
static struct pipe_resource *
llvmpipe_resource_create_with_modifiers(struct pipe_screen *_screen,
                                        const struct pipe_resource *templat,
                                        const uint64_t *modifiers, int count)
{
   bool has_linear = false;
   for (unsigned i = 0; i < count; i++)
      if (modifiers[i] == DRM_FORMAT_MOD_LINEAR)
         has_linear = true;
   if (!has_linear)
      return NULL;
   return llvmpipe_resource_create_front(_screen, templat, NULL);
}
#endif

static struct pipe_resource *
llvmpipe_resource_create_unbacked(struct pipe_screen *_screen,
                                  const struct pipe_resource *templat,
                                  uint64_t *size_required)
{
   struct pipe_resource *pt =
      llvmpipe_resource_create_all(_screen, templat, NULL, false);
   if (!pt)
      return pt;
   struct llvmpipe_resource *lpr = llvmpipe_resource(pt);
   lpr->backable = true;
   *size_required = lpr->size_required;
   return pt;
}


static struct pipe_memory_object *
llvmpipe_memobj_create_from_handle(struct pipe_screen *pscreen,
                                   struct winsys_handle *handle,
                                   bool dedicated)
{
#ifdef PIPE_MEMORY_FD
   struct llvmpipe_memory_object *memobj = CALLOC_STRUCT(llvmpipe_memory_object);
   pipe_reference_init(&memobj->reference, 1);

   if (handle->type == WINSYS_HANDLE_TYPE_FD &&
       pscreen->import_memory_fd(pscreen,
                                 handle->handle,
                                 (struct pipe_memory_allocation **)&memobj->mem_alloc,
                                 &memobj->size,
                                 false)) {
      return &memobj->b;
   }
   free(memobj);
#endif
   return NULL;
}


static void
llvmpipe_memobj_destroy(struct pipe_screen *pscreen,
                        struct pipe_memory_object *memobj)
{
   if (!memobj)
      return;
   struct llvmpipe_memory_object *lpmo = llvmpipe_memory_object(memobj);
   if (pipe_reference(&lpmo->reference, NULL))
   {
#ifdef PIPE_MEMORY_FD
      pscreen->free_memory_fd(pscreen, (struct pipe_memory_allocation *)lpmo->mem_alloc);
#endif
      free(lpmo);
   }
}


static struct pipe_resource *
llvmpipe_resource_from_memobj(struct pipe_screen *pscreen,
                              const struct pipe_resource *templat,
                              struct pipe_memory_object *memobj,
                              uint64_t offset)
{
   if (!memobj)
      return NULL;
   struct llvmpipe_screen *screen = llvmpipe_screen(pscreen);
   struct llvmpipe_memory_object *lpmo = llvmpipe_memory_object(memobj);
   struct llvmpipe_resource *lpr = CALLOC_STRUCT(llvmpipe_resource);
   lpr->base = *templat;

   lpr->screen = screen;
   pipe_reference_init(&lpr->base.reference, 1);
   lpr->base.screen = &screen->base;

   if (llvmpipe_resource_is_texture(&lpr->base)) {
      /* texture map */
      if (!llvmpipe_texture_layout(screen, lpr, false))
         goto fail;
      if (lpmo->size < lpr->size_required)
         goto fail;
      lpr->tex_data = lpmo->mem_alloc->cpu_addr;
   } else {
      /* other data (vertex buffer, const buffer, etc) */
      const uint bytes = templat->width0;
      assert(util_format_get_blocksize(templat->format) == 1);
      assert(templat->height0 == 1);
      assert(templat->depth0 == 1);
      assert(templat->last_level == 0);
      /*
       * Reserve some extra storage since if we'd render to a buffer we
       * read/write always LP_RASTER_BLOCK_SIZE pixels, but the element
       * offset doesn't need to be aligned to LP_RASTER_BLOCK_SIZE.
       */
      /*
       * buffers don't really have stride but it's probably safer
       * (for code doing same calculations for buffers and textures)
       * to put something reasonable in there.
       */
      lpr->row_stride[0] = bytes;

      lpr->size_required = bytes;
      if (!(templat->flags & PIPE_RESOURCE_FLAG_DONT_OVER_ALLOCATE))
         lpr->size_required += (LP_RASTER_BLOCK_SIZE - 1) * 4 * sizeof(float);

      if (lpmo->size < lpr->size_required)
         goto fail;
      lpr->data = lpmo->mem_alloc->cpu_addr;
   }
   lpr->id = id_counter++;
   lpr->imported_memory = &lpmo->b;
   pipe_reference(NULL, &lpmo->reference);

#if MESA_DEBUG
   simple_mtx_lock(&resource_list_mutex);
   list_addtail(&lpr->list, &resource_list.list);
   simple_mtx_unlock(&resource_list_mutex);
#endif

   return &lpr->base;

fail:
   free(lpr);
   return NULL;
}

static void
llvmpipe_resource_destroy(struct pipe_screen *pscreen,
                          struct pipe_resource *pt)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(pscreen);
   struct llvmpipe_resource *lpr = llvmpipe_resource(pt);

   if (!lpr->backable && !lpr->user_ptr) {
      if (lpr->dt) {
         /* display target */
         struct sw_winsys *winsys = screen->winsys;
         if (lpr->dmabuf)
            winsys->displaytarget_unmap(winsys, lpr->dt);
         winsys->displaytarget_destroy(winsys, lpr->dt);
      } else if (llvmpipe_resource_is_texture(pt)) {
         /* free linear image data */
         if (lpr->tex_data) {
            if (lpr->imported_memory)
               llvmpipe_memobj_destroy(pscreen, lpr->imported_memory);
            else
               align_free(lpr->tex_data);
            lpr->tex_data = NULL;
            lpr->imported_memory = NULL;
         }
      } else if (lpr->data) {
         if (lpr->imported_memory)
            llvmpipe_memobj_destroy(pscreen, lpr->imported_memory);
         else
             align_free(lpr->data);
         lpr->imported_memory = NULL;
      }
   }

#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
   if (lpr->dmabuf_alloc)
      pscreen->free_memory_fd(pscreen, (struct pipe_memory_allocation*)lpr->dmabuf_alloc);
#endif

   if (lpr->base.flags & PIPE_RESOURCE_FLAG_SPARSE) {
#if DETECT_OS_LINUX
      if (llvmpipe_resource_is_texture(pt))
         munmap(lpr->tex_data, lpr->size_required);
      else
         munmap(lpr->data, lpr->size_required);
#endif
   }

   free(lpr->residency);

#if MESA_DEBUG
   simple_mtx_lock(&resource_list_mutex);
   if (!list_is_empty(&lpr->list))
      list_del(&lpr->list);
   simple_mtx_unlock(&resource_list_mutex);
#endif

   FREE(lpr);
}


/**
 * Map a resource for read/write.
 */
void *
llvmpipe_resource_map(struct pipe_resource *resource,
                      unsigned level,
                      unsigned layer,
                      enum lp_texture_usage tex_usage)
{
   struct llvmpipe_resource *lpr = llvmpipe_resource(resource);
   uint8_t *map;

   assert(level < LP_MAX_TEXTURE_LEVELS);
   assert(layer < (u_minify(resource->depth0, level) + resource->array_size - 1));

   assert(tex_usage == LP_TEX_USAGE_READ ||
          tex_usage == LP_TEX_USAGE_READ_WRITE ||
          tex_usage == LP_TEX_USAGE_WRITE_ALL);

   if (lpr->dt) {
      if (lpr->dmabuf)
         return lpr->tex_data;
      /* display target */
      struct llvmpipe_screen *screen = lpr->screen;
      struct sw_winsys *winsys = screen->winsys;
      unsigned dt_usage;

      if (tex_usage == LP_TEX_USAGE_READ) {
         dt_usage = PIPE_MAP_READ;
      } else {
         dt_usage = PIPE_MAP_READ_WRITE;
      }

      assert(level == 0);
      assert(layer == 0);

      /* FIXME: keep map count? */
      map = winsys->displaytarget_map(winsys, lpr->dt, dt_usage);

      /* install this linear image in texture data structure */
      lpr->tex_data = map;

      return map;
   } else if (llvmpipe_resource_is_texture(resource)) {

      map = llvmpipe_get_texture_image_address(lpr, layer, level);
      return map;
   } else {
      return lpr->data;
   }
}


/**
 * Unmap a resource.
 */
void
llvmpipe_resource_unmap(struct pipe_resource *resource,
                        unsigned level,
                        unsigned layer)
{
   struct llvmpipe_resource *lpr = llvmpipe_resource(resource);

   if (lpr->dt) {
      if (lpr->dmabuf)
         return;
      /* display target */
      struct llvmpipe_screen *lp_screen = lpr->screen;
      struct sw_winsys *winsys = lp_screen->winsys;

      assert(level == 0);
      assert(layer == 0);

      winsys->displaytarget_unmap(winsys, lpr->dt);
   }
}


void *
llvmpipe_resource_data(struct pipe_resource *resource)
{
   struct llvmpipe_resource *lpr = llvmpipe_resource(resource);

   assert(!llvmpipe_resource_is_texture(resource));

   return lpr->data;
}


static struct pipe_resource *
llvmpipe_resource_from_handle(struct pipe_screen *_screen,
                              const struct pipe_resource *template,
                              struct winsys_handle *whandle,
                              unsigned usage)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   struct sw_winsys *winsys = screen->winsys;
   struct llvmpipe_resource *lpr;

   /* no multisampled */
   assert(template->nr_samples < 2);
   /* no miplevels */
   assert(template->last_level == 0);

   /* Multiplanar surfaces are not supported */
   if (whandle->plane > 0)
      return NULL;

   lpr = CALLOC_STRUCT(llvmpipe_resource);
   if (!lpr) {
      goto no_lpr;
   }

   lpr->base = *template;
   lpr->screen = screen;
   lpr->dt_format = whandle->format;
   pipe_reference_init(&lpr->base.reference, 1);
   lpr->base.screen = _screen;

   /*
    * Looks like unaligned displaytargets work just fine,
    * at least sampler/render ones.
    */
#if 0
   assert(lpr->base.width0 == width);
   assert(lpr->base.height0 == height);
#endif

   unsigned nblocksy = util_format_get_nblocksy(template->format, align(template->height0, LP_RASTER_BLOCK_SIZE));
   if (whandle->type == WINSYS_HANDLE_TYPE_UNBACKED && whandle->image_stride)
      lpr->img_stride[0] = whandle->image_stride;
   else
      lpr->img_stride[0] = whandle->stride * nblocksy;
   lpr->sample_stride = lpr->img_stride[0];
   lpr->size_required = lpr->sample_stride;

   if (whandle->type != WINSYS_HANDLE_TYPE_UNBACKED) {
#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
      struct llvmpipe_memory_allocation *alloc;
      uint64_t size;
      /* Not all winsys implement displaytarget_create_mapped so we need to check
       * that is available (not null).
       */
      if (winsys->displaytarget_create_mapped &&
          _screen->import_memory_fd(_screen, whandle->handle,
                                    (struct pipe_memory_allocation**)&alloc,
                                    &size, true)) {
         void *data = alloc->cpu_addr;
         lpr->dt = winsys->displaytarget_create_mapped(winsys, template->bind,
                                                       template->format, template->width0, template->height0,
                                                       whandle->stride, data);
         if (!lpr->dt)
            goto no_dt;
         lpr->dmabuf_alloc = alloc;
         lpr->dmabuf = true;
         lpr->tex_data = data;
         lpr->row_stride[0] = whandle->stride;
         whandle->size = size;
      } else
#endif
      {
         lpr->dt = winsys->displaytarget_from_handle(winsys,
                                                     template,
                                                     whandle,
                                                     &lpr->row_stride[0]);
         if (!lpr->dt)
            goto no_dt;
      }

      assert(llvmpipe_resource_is_texture(&lpr->base));
   } else {
      whandle->size = lpr->size_required;
      lpr->row_stride[0] = whandle->stride;
      lpr->backable = true;
   }


   lpr->id = id_counter++;

#if MESA_DEBUG
   simple_mtx_lock(&resource_list_mutex);
   list_addtail(&lpr->list, &resource_list.list);
   simple_mtx_unlock(&resource_list_mutex);
#endif

   return &lpr->base;

no_dt:
   FREE(lpr);
no_lpr:
   return NULL;
}


static bool
llvmpipe_resource_get_handle(struct pipe_screen *_screen,
                             struct pipe_context *ctx,
                             struct pipe_resource *pt,
                             struct winsys_handle *whandle,
                             unsigned usage)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   struct sw_winsys *winsys = screen->winsys;
   struct llvmpipe_resource *lpr = llvmpipe_resource(pt);

#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
   if (!lpr->dt && whandle->type == WINSYS_HANDLE_TYPE_FD) {
      if (!lpr->dmabuf_alloc) {
         lpr->dmabuf_alloc = (struct llvmpipe_memory_allocation*)_screen->allocate_memory_fd(_screen, lpr->size_required, (int*)&whandle->handle, true);
         if (!lpr->dmabuf_alloc)
            return false;

         /* replace existing backing with fd backing */
         bool is_tex = llvmpipe_resource_is_texture(pt);
         if (is_tex) {
            if (lpr->tex_data)
               memcpy(lpr->dmabuf_alloc->cpu_addr, lpr->tex_data, lpr->size_required);
         } else {
            if (lpr->data)
               memcpy(lpr->dmabuf_alloc->cpu_addr, lpr->data, lpr->size_required);
         }
         if (!lpr->imported_memory)
            align_free(is_tex ? lpr->tex_data : lpr->data);
         if (is_tex)
            lpr->tex_data = lpr->dmabuf_alloc->cpu_addr;
         else
            lpr->data = lpr->dmabuf_alloc->cpu_addr;
         /* reuse lavapipe codepath to handle destruction */
         lpr->backable = true;
      } else {
         whandle->handle = os_dupfd_cloexec(lpr->dmabuf_alloc->dmabuf_fd);
      }
      whandle->modifier = DRM_FORMAT_MOD_LINEAR;
      whandle->stride = lpr->row_stride[0];
      return true;
   } else if (!lpr->dt && whandle->type == WINSYS_HANDLE_TYPE_KMS) {
      /* dri winsys code will use this to query the drm modifiers
       * We can just return an null handle and return DRM_FORMAT_MOD_LINEAR */
      whandle->handle = 0;
      whandle->modifier = DRM_FORMAT_MOD_LINEAR;
      return true;
    }
#endif
   assert(lpr->dt);
   if (!lpr->dt)
      return false;

   return winsys->displaytarget_get_handle(winsys, lpr->dt, whandle);
}


static struct pipe_resource *
llvmpipe_resource_from_user_memory(struct pipe_screen *_screen,
                                   const struct pipe_resource *resource,
                                   void *user_memory)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   struct llvmpipe_resource *lpr;

   lpr = CALLOC_STRUCT(llvmpipe_resource);
   if (!lpr) {
      return NULL;
   }

   lpr->base = *resource;
   lpr->screen = screen;
   pipe_reference_init(&lpr->base.reference, 1);
   lpr->base.screen = _screen;

   if (llvmpipe_resource_is_texture(&lpr->base)) {
      if (!llvmpipe_texture_layout(screen, lpr, false))
         goto fail;

      lpr->tex_data = user_memory;
   } else
      lpr->data = user_memory;
   lpr->user_ptr = true;
#if MESA_DEBUG
   simple_mtx_lock(&resource_list_mutex);
   list_addtail(&lpr->list, &resource_list.list);
   simple_mtx_unlock(&resource_list_mutex);
#endif
   return &lpr->base;
fail:
   FREE(lpr);
   return NULL;
}


void *
llvmpipe_transfer_map_ms(struct pipe_context *pipe,
                         struct pipe_resource *resource,
                         unsigned level,
                         unsigned usage,
                         unsigned sample,
                         const struct pipe_box *box,
                         struct pipe_transfer **transfer)
{
   struct llvmpipe_context *llvmpipe = llvmpipe_context(pipe);
   struct llvmpipe_screen *screen = llvmpipe_screen(pipe->screen);
   struct llvmpipe_resource *lpr = llvmpipe_resource(resource);
   struct llvmpipe_transfer *lpt;
   struct pipe_transfer *pt;
   uint8_t *map;
   enum pipe_format format;

   assert(resource);
   assert(level <= resource->last_level);

   /*
    * Transfers, like other pipe operations, must happen in order, so flush
    * the context if necessary.
    */
   if (!(usage & PIPE_MAP_UNSYNCHRONIZED)) {
      bool read_only = !(usage & PIPE_MAP_WRITE);
      bool do_not_block = !!(usage & PIPE_MAP_DONTBLOCK);
      if (!llvmpipe_flush_resource(pipe, resource,
                                   level,
                                   read_only,
                                   true, /* cpu_access */
                                   do_not_block,
                                   __func__)) {
         /*
          * It would have blocked, but gallium frontend requested no to.
          */
         assert(do_not_block);
         return NULL;
      }
   }

   /* Check if we're mapping a current constant buffer */
   if ((usage & PIPE_MAP_WRITE) &&
       (resource->bind & PIPE_BIND_CONSTANT_BUFFER)) {
      unsigned i;
      for (i = 0; i < ARRAY_SIZE(llvmpipe->constants[PIPE_SHADER_FRAGMENT]); ++i) {
         if (resource == llvmpipe->constants[PIPE_SHADER_FRAGMENT][i].buffer) {
            /* constants may have changed */
            llvmpipe->dirty |= LP_NEW_FS_CONSTANTS;
            break;
         }
      }
   }

   lpt = CALLOC_STRUCT(llvmpipe_transfer);
   if (!lpt)
      return NULL;
   pt = &lpt->base;
   pipe_resource_reference(&pt->resource, resource);
   pt->box = *box;
   pt->level = level;
   pt->stride = lpr->row_stride[level];
   pt->layer_stride = lpr->img_stride[level];
   pt->usage = usage;
   *transfer = pt;

   assert(level < LP_MAX_TEXTURE_LEVELS);

   /*
   printf("tex_transfer_map(%d, %d  %d x %d of %d x %d,  usage %d)\n",
          transfer->x, transfer->y, transfer->width, transfer->height,
          transfer->texture->width0,
          transfer->texture->height0,
          transfer->usage);
   */

   enum lp_texture_usage tex_usage;
   const char *mode;
   if (usage == PIPE_MAP_READ) {
      tex_usage = LP_TEX_USAGE_READ;
      mode = "read";
   } else {
      tex_usage = LP_TEX_USAGE_READ_WRITE;
      mode = "read/write";
   }

   if (0) {
      printf("transfer map tex %u  mode %s\n", lpr->id, mode);
   }

   format = lpr->base.format;

   if (llvmpipe_resource_is_texture(resource) && (resource->flags & PIPE_RESOURCE_FLAG_SPARSE)) {
      map = llvmpipe_resource_map(resource, 0, 0, tex_usage);

      lpt->block_box = (struct pipe_box) {
         .x = box->x / util_format_get_blockwidth(format),
         .width = DIV_ROUND_UP(box->x + box->width, util_format_get_blockwidth(format)),
         .y = box->y / util_format_get_blockheight(format),
         .height = DIV_ROUND_UP(box->y + box->height, util_format_get_blockheight(format)),
         .z = box->z / util_format_get_blockdepth(format),
         .depth = DIV_ROUND_UP(box->z + box->depth, util_format_get_blockdepth(format)),
      };

      lpt->block_box.width -= lpt->block_box.x;
      lpt->block_box.height -= lpt->block_box.y;
      lpt->block_box.depth -= lpt->block_box.z;

      uint32_t block_stride = util_format_get_blocksize(format);
      pt->stride = lpt->block_box.width * block_stride;
      pt->layer_stride = pt->stride * lpt->block_box.height;

      uint8_t *staging_map = malloc(pt->layer_stride * lpt->block_box.depth);
      lpt->map = staging_map;

      if (usage & PIPE_MAP_READ) {
         for (uint32_t z = 0; z < lpt->block_box.depth; z++) {
            for (uint32_t y = 0; y < lpt->block_box.height; y++) {
               for (uint32_t x = 0; x < lpt->block_box.width; x++) {
                  memcpy(staging_map,
                         map + llvmpipe_get_texel_offset(resource, level,
                                                         lpt->block_box.x + x,
                                                         lpt->block_box.y + y,
                                                         lpt->block_box.z + z),
                         block_stride);
                  staging_map += block_stride;
               }
            }
         }
      }

      return lpt->map;
   }

   map = llvmpipe_resource_map(resource, level, box->z, tex_usage);


   /* May want to do different things here depending on read/write nature
    * of the map:
    */
   if (usage & PIPE_MAP_WRITE) {
      /* Do something to notify sharing contexts of a texture change.
       */
      screen->timestamp++;
   }

   map +=
      box->y / util_format_get_blockheight(format) * pt->stride +
      box->x / util_format_get_blockwidth(format) * util_format_get_blocksize(format);

   map += sample * lpr->sample_stride;
   return map;
}

uint32_t
llvmpipe_get_texel_offset(struct pipe_resource *resource,
                          uint32_t level, uint32_t x,
                          uint32_t y, uint32_t z)
{
   struct llvmpipe_resource *lpr = llvmpipe_resource(resource);

   uint32_t layer = 0;
   if (resource->target != PIPE_TEXTURE_3D) {
      layer = z;
      z = 0;
   }

   uint32_t dimensions = 1;
   switch (resource->target) {
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D_ARRAY:
      dimensions = 2;
      break;
   case PIPE_TEXTURE_3D:
      dimensions = 3;
      break;
   default:
      break;
   }

   uint32_t sparse_tile_size[3] = {
      util_format_get_tilesize(resource->format, dimensions, resource->nr_samples, 0),
      util_format_get_tilesize(resource->format, dimensions, resource->nr_samples, 1),
      util_format_get_tilesize(resource->format, dimensions, resource->nr_samples, 2),
   };

   uint32_t num_tiles_x = DIV_ROUND_UP(u_minify(resource->width0, level),
                                       sparse_tile_size[0] * util_format_get_blockwidth(resource->format));
   uint32_t num_tiles_y = DIV_ROUND_UP(u_minify(resource->height0, level),
                                       sparse_tile_size[1] * util_format_get_blockheight(resource->format));

   uint32_t offset = (
      x / sparse_tile_size[0] +
      y / sparse_tile_size[1] * num_tiles_x +
      z / sparse_tile_size[2] * num_tiles_x * num_tiles_y
   ) * 64 * 1024;

   offset += (
      x % sparse_tile_size[0] + 
      (y % sparse_tile_size[1]) * sparse_tile_size[0] +
      (z % sparse_tile_size[2]) * sparse_tile_size[0] * sparse_tile_size[1]
   ) * util_format_get_blocksize(resource->format);

   return offset + lpr->mip_offsets[level] + lpr->img_stride[level] * layer;   
}


static void *
llvmpipe_transfer_map(struct pipe_context *pipe,
                      struct pipe_resource *resource,
                      unsigned level,
                      unsigned usage,
                      const struct pipe_box *box,
                      struct pipe_transfer **transfer)
{
   return llvmpipe_transfer_map_ms(pipe, resource, level, usage, 0,
                                   box, transfer);
}


static void
llvmpipe_transfer_unmap(struct pipe_context *pipe,
                        struct pipe_transfer *transfer)
{
   struct llvmpipe_transfer *lpt = (struct llvmpipe_transfer *)transfer;
   struct pipe_resource *resource = transfer->resource;
   struct llvmpipe_resource *lpr = llvmpipe_resource(resource);

   assert(resource);

   if (llvmpipe_resource_is_texture(resource) && (resource->flags & PIPE_RESOURCE_FLAG_SPARSE) &&
       (transfer->usage & PIPE_MAP_WRITE)) {
      uint32_t block_stride = util_format_get_blocksize(resource->format);

      const uint8_t *src = lpt->map;
      uint8_t *dst = lpr->tex_data;

      for (uint32_t z = 0; z < lpt->block_box.depth; z++) {
         for (uint32_t y = 0; y < lpt->block_box.height; y++) {
            for (uint32_t x = 0; x < lpt->block_box.width; x++) {
               memcpy(dst + llvmpipe_get_texel_offset(resource, transfer->level,
                                                      lpt->block_box.x + x,
                                                      lpt->block_box.y + y,
                                                      lpt->block_box.z + z),
                      src, block_stride);
               src += block_stride;
            }
         }
      }
   }

   llvmpipe_resource_unmap(resource,
                           transfer->level,
                           transfer->box.z);

   pipe_resource_reference(&resource, NULL);
   free(lpt->map);
   FREE(transfer);
}


unsigned int
llvmpipe_is_resource_referenced(struct pipe_context *pipe,
                                struct pipe_resource *presource,
                                unsigned level)
{
   struct llvmpipe_context *llvmpipe = llvmpipe_context(pipe);
   if (!(presource->bind & (PIPE_BIND_DEPTH_STENCIL |
                            PIPE_BIND_RENDER_TARGET |
                            PIPE_BIND_SAMPLER_VIEW |
                            PIPE_BIND_CONSTANT_BUFFER |
                            PIPE_BIND_SHADER_BUFFER |
                            PIPE_BIND_SHADER_IMAGE)))
      return LP_UNREFERENCED;

   return lp_setup_is_resource_referenced(llvmpipe->setup, presource);
}


/**
 * Returns the largest possible alignment for a format in llvmpipe
 */
unsigned
llvmpipe_get_format_alignment(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   unsigned size = 0;

   for (unsigned i = 0; i < desc->nr_channels; ++i) {
      size += desc->channel[i].size;
   }

   unsigned bytes = size / 8;

   if (!util_is_power_of_two_or_zero(bytes)) {
      bytes /= desc->nr_channels;
   }

   if (bytes % 2 || bytes < 1) {
      return 1;
   } else {
      return bytes;
   }
}


/**
 * Create buffer which wraps user-space data.
 * XXX unreachable.
 */
struct pipe_resource *
llvmpipe_user_buffer_create(struct pipe_screen *screen,
                            void *ptr,
                            unsigned bytes,
                            unsigned bind_flags)
{
   struct llvmpipe_resource *buffer;

   buffer = CALLOC_STRUCT(llvmpipe_resource);
   if (!buffer)
      return NULL;

   buffer->screen = llvmpipe_screen(screen);
   pipe_reference_init(&buffer->base.reference, 1);
   buffer->base.screen = screen;
   buffer->base.format = PIPE_FORMAT_R8_UNORM; /* ?? */
   buffer->base.bind = bind_flags;
   buffer->base.usage = PIPE_USAGE_IMMUTABLE;
   buffer->base.flags = 0;
   buffer->base.width0 = bytes;
   buffer->base.height0 = 1;
   buffer->base.depth0 = 1;
   buffer->base.array_size = 1;
   buffer->user_ptr = true;
   buffer->data = ptr;

   return &buffer->base;
}


/**
 * Compute size (in bytes) need to store a texture image / mipmap level,
 * for just one cube face, one array layer or one 3D texture slice
 */
static unsigned
tex_image_face_size(const struct llvmpipe_resource *lpr, unsigned level)
{
   return lpr->img_stride[level];
}


/**
 * Return pointer to a 2D texture image/face/slice.
 * No tiled/linear conversion is done.
 */
uint8_t *
llvmpipe_get_texture_image_address(struct llvmpipe_resource *lpr,
                                   unsigned face_slice, unsigned level)
{
   assert(llvmpipe_resource_is_texture(&lpr->base));

   unsigned offset = lpr->mip_offsets[level];

   if (face_slice > 0)
      offset += face_slice * tex_image_face_size(lpr, level);

   return (uint8_t *) lpr->tex_data + offset;
}


/**
 * Return size of resource in bytes
 */
unsigned
llvmpipe_resource_size(const struct pipe_resource *resource)
{
   const struct llvmpipe_resource *lpr = llvmpipe_resource_const(resource);
   unsigned size = 0;

   if (llvmpipe_resource_is_texture(resource)) {
      /* Note this will always return 0 for displaytarget resources */
      size = lpr->total_alloc_size;
   } else {
      size = resource->width0;
   }
   return size;
}


static void
llvmpipe_memory_barrier(struct pipe_context *pipe,
                        unsigned flags)
{
   /* this may be an overly large hammer for this nut. */
   llvmpipe_finish(pipe, "barrier");
}


static struct pipe_memory_allocation *
llvmpipe_allocate_memory(struct pipe_screen *_screen, uint64_t size)
{
   struct llvmpipe_memory_allocation *mem = CALLOC_STRUCT(llvmpipe_memory_allocation);
   uint64_t alignment;
   if (!os_get_page_size(&alignment))
      alignment = 256;

   mem->size = align64(size, alignment);

#if DETECT_OS_LINUX
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);

   mem->cpu_addr = MAP_FAILED;
   mem->fd = screen->fd_mem_alloc;

   mtx_lock(&screen->mem_mutex);

   mem->offset = util_vma_heap_alloc(&screen->mem_heap, mem->size, alignment);
   if (!mem->offset) {
      mtx_unlock(&screen->mem_mutex);
      FREE(mem);
      return NULL;
   }

   if (mem->offset + mem->size > screen->mem_file_size) {
      /* expand the anonymous file */
      screen->mem_file_size = mem->offset + mem->size;
      ftruncate(screen->fd_mem_alloc, screen->mem_file_size);
   }

   mtx_unlock(&screen->mem_mutex);
#else
   mem->cpu_addr = malloc(mem->size);
#endif

   return (struct pipe_memory_allocation *)mem;
}


static void
llvmpipe_free_memory(struct pipe_screen *pscreen,
                     struct pipe_memory_allocation *pmem)
{
   struct llvmpipe_memory_allocation *mem = (struct llvmpipe_memory_allocation *)pmem;

#if DETECT_OS_LINUX
   struct llvmpipe_screen *screen = llvmpipe_screen(pscreen);

   if (mem->fd) {
      mtx_lock(&screen->mem_mutex);
      util_vma_heap_free(&screen->mem_heap, mem->offset, mem->size);
      mtx_unlock(&screen->mem_mutex);
   }

   if (mem->cpu_addr != MAP_FAILED)
      munmap(mem->cpu_addr, mem->size);
#else
   free(mem->cpu_addr);
#endif

   FREE(mem);
}


#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
static void*
llvmpipe_resource_alloc_udmabuf(struct llvmpipe_screen *screen,
                                struct llvmpipe_memory_allocation *alloc,
                                size_t size)
{
   int mem_fd = -1;
   int dmabuf_fd = -1;
   if (screen->udmabuf_fd != -1) {
      uint64_t alignment;
      if (!os_get_page_size(&alignment))
         alignment = 256;

      size = align(size, alignment);

      int mem_fd = memfd_create("lp_dma_buf", MFD_ALLOW_SEALING);
      if (mem_fd == -1)
         goto fail;

      int res = ftruncate(mem_fd, size);
      if (res == -1)
         goto fail;

      /* udmabuf create requires that the memfd have
       * have the F_SEAL_SHRINK seal added and must not
       * have the F_SEAL_WRITE seal added */
      if (fcntl(mem_fd, F_ADD_SEALS, F_SEAL_SHRINK) < 0)
         goto fail;

      struct udmabuf_create create = {
         .memfd = mem_fd,
         .flags = UDMABUF_FLAGS_CLOEXEC,
         .offset = 0,
         .size = size
      };

      int dmabuf_fd = ioctl(screen->udmabuf_fd, UDMABUF_CREATE, &create);
      if (dmabuf_fd < 0)
         goto fail;

      struct pipe_memory_allocation *data =
         mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_SHARED, mem_fd, 0);

      if (!data)
         goto fail;

      alloc->mem_fd = mem_fd;
      alloc->dmabuf_fd = dmabuf_fd;
      alloc->size = size;
      return data;
   }

fail:
   if (mem_fd != -1)
      close(mem_fd);
   if (dmabuf_fd != -1)
      close(dmabuf_fd);
   /* If we don't have access to the udmabuf device
    * or something else fails we return NULL */
   return NULL;
}
#endif

#ifdef PIPE_MEMORY_FD
static struct pipe_memory_allocation *
llvmpipe_allocate_memory_fd(struct pipe_screen *pscreen,
                            uint64_t size,
                            int *fd,
                            bool dmabuf)
{
   struct llvmpipe_memory_allocation *alloc = CALLOC_STRUCT(llvmpipe_memory_allocation);
   if (!alloc)
      goto fail;

   alloc->mem_fd = -1;
   alloc->dmabuf_fd = -1;
#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
   if (dmabuf) {
      struct llvmpipe_screen *screen = llvmpipe_screen(pscreen);
      alloc->type = LLVMPIPE_MEMORY_FD_TYPE_DMA_BUF;
      alloc->cpu_addr = llvmpipe_resource_alloc_udmabuf(screen, alloc, size);

      if (alloc->cpu_addr)
         *fd = os_dupfd_cloexec(alloc->dmabuf_fd);
   } else
#endif
   {
      alloc->type = LLVMPIPE_MEMORY_FD_TYPE_OPAQUE;
      uint64_t alignment;
      if (!os_get_page_size(&alignment))
         alignment = 256;
      alloc->cpu_addr = os_malloc_aligned_fd(size, alignment, fd,
            "llvmpipe memory fd", driver_id);
   }

   if(alloc && !alloc->cpu_addr) {
      free(alloc);
      alloc = NULL;
   }

fail:
   return (struct pipe_memory_allocation*)alloc;
}


static bool
llvmpipe_import_memory_fd(struct pipe_screen *screen,
                          int fd,
                          struct pipe_memory_allocation **ptr,
                          uint64_t *size,
                          bool dmabuf)
{
   struct llvmpipe_memory_allocation *alloc = CALLOC_STRUCT(llvmpipe_memory_allocation);
   alloc->mem_fd = -1;
   alloc->dmabuf_fd = -1;
#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
   if (dmabuf) {
      off_t mmap_size = lseek(fd, 0, SEEK_END);
      lseek(fd, 0, SEEK_SET);
      void *cpu_addr = mmap(0, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      if (cpu_addr == MAP_FAILED) {
         free(alloc);
         *ptr = NULL;
         return false;
      }

      alloc->type = LLVMPIPE_MEMORY_FD_TYPE_DMA_BUF;
      alloc->cpu_addr = cpu_addr;
      alloc->size = mmap_size;
      alloc->dmabuf_fd = os_dupfd_cloexec(fd);
      *ptr = (struct pipe_memory_allocation*)alloc;
      *size = mmap_size;

      return true;
   } else
#endif
   {
      bool ret = os_import_memory_fd(fd, (void**)&alloc->cpu_addr, size, driver_id);

      if (!ret) {
         free(alloc);
         *ptr = NULL;
      } else {
         *ptr = (struct pipe_memory_allocation*)alloc;
      }

      alloc->type = LLVMPIPE_MEMORY_FD_TYPE_OPAQUE;
      return ret;
   }
}


static void
llvmpipe_free_memory_fd(struct pipe_screen *screen,
                        struct pipe_memory_allocation *pmem)
{
   struct llvmpipe_memory_allocation *alloc = (struct llvmpipe_memory_allocation*)pmem;
   if (alloc->type == LLVMPIPE_MEMORY_FD_TYPE_OPAQUE) {
      os_free_fd(alloc->cpu_addr);
   }
#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
   else {
      munmap(alloc->cpu_addr, alloc->size);
      if (alloc->dmabuf_fd >= 0)
         close(alloc->dmabuf_fd);
      if (alloc->mem_fd >= 0)
         close(alloc->mem_fd);
   }
#endif

   free(alloc);
}

#endif

static void *
llvmpipe_map_memory(struct pipe_screen *screen,
                    struct pipe_memory_allocation *pmem)
{
   struct llvmpipe_memory_allocation *mem = (struct llvmpipe_memory_allocation *)pmem;

#if DETECT_OS_LINUX
   if (mem->cpu_addr != MAP_FAILED)
      return mem->cpu_addr;

   /* create a "CPU" mapping */
   mem->cpu_addr = mmap(NULL, mem->size, PROT_READ|PROT_WRITE, MAP_SHARED,
                        mem->fd, mem->offset);
   assert(mem->cpu_addr != MAP_FAILED);
#endif

   return mem->cpu_addr;
}

static void
llvmpipe_unmap_memory(struct pipe_screen *screen,
                      struct pipe_memory_allocation *pmem)
{
}

static bool
llvmpipe_resource_bind_backing(struct pipe_screen *pscreen,
                               struct pipe_resource *pt,
                               struct pipe_memory_allocation *pmem,
                               uint64_t fd_offset,
                               uint64_t size,
                               uint64_t offset)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(pscreen);
   struct llvmpipe_resource *lpr = llvmpipe_resource(pt);
   struct sw_winsys *winsys = screen->winsys;

   void *addr;
   if (!lpr->backable)
      return false;

   if ((lpr->base.flags & PIPE_RESOURCE_FLAG_SPARSE) && offset < lpr->size_required) {
#if DETECT_OS_LINUX
      struct llvmpipe_memory_allocation *mem = (struct llvmpipe_memory_allocation *)pmem;
      if (mem) {
         if (llvmpipe_resource_is_texture(&lpr->base)) {
            mmap((char *)lpr->tex_data + offset, size, PROT_READ|PROT_WRITE,
                 MAP_SHARED|MAP_FIXED, mem->fd, mem->offset + fd_offset);
            BITSET_SET(lpr->residency, offset / (64 * 1024));
         } else {
            mmap((char *)lpr->data + offset, size, PROT_READ|PROT_WRITE,
                 MAP_SHARED|MAP_FIXED, mem->fd, mem->offset + fd_offset);
         }
      } else {
         if (llvmpipe_resource_is_texture(&lpr->base)) {
            mmap((char *)lpr->tex_data + offset, size, PROT_READ|PROT_WRITE,
                 MAP_SHARED|MAP_FIXED|MAP_ANONYMOUS, -1, 0);
            BITSET_CLEAR(lpr->residency, offset / (64 * 1024));
         } else {
            mmap((char *)lpr->data + offset, size, PROT_READ|PROT_WRITE,
                 MAP_SHARED|MAP_FIXED|MAP_ANONYMOUS, -1, 0);
         }
      }
#endif

      return true;
   }

   addr = llvmpipe_map_memory(pscreen, pmem);

   if (llvmpipe_resource_is_texture(&lpr->base)) {
      if (lpr->size_required > LP_MAX_TEXTURE_SIZE)
         return false;

      lpr->tex_data = (char *)addr + offset;

      if (lpr->dmabuf) {
         if (lpr->dt)
         {
            winsys->displaytarget_unmap(winsys, lpr->dt);
            winsys->displaytarget_destroy(winsys, lpr->dt);
         }
         if (pmem) {
            /* Round up the surface size to a multiple of the tile size to
             * avoid tile clipping.
             */
            const unsigned width = MAX2(1, align(lpr->base.width0, TILE_SIZE));
            const unsigned height = MAX2(1, align(lpr->base.height0, TILE_SIZE));

            lpr->dt = winsys->displaytarget_create_mapped(winsys,
                                                          lpr->base.bind,
                                                          lpr->base.format,
                                                          width, height,
                                                          lpr->row_stride[0],
                                                          lpr->tex_data);
         }
      }
   } else
      lpr->data = (char *)addr + offset;
   lpr->backing_offset = offset;

   return true;
}



#if MESA_DEBUG
void
llvmpipe_print_resources(void)
{
   struct llvmpipe_resource *lpr;
   unsigned n = 0, total = 0;

   debug_printf("LLVMPIPE: current resources:\n");
   simple_mtx_lock(&resource_list_mutex);
   LIST_FOR_EACH_ENTRY(lpr, &resource_list.list, list) {
      unsigned size = llvmpipe_resource_size(&lpr->base);
      debug_printf("resource %u at %p, size %ux%ux%u: %u bytes, refcount %u\n",
                   lpr->id, (void *) lpr,
                   lpr->base.width0, lpr->base.height0, lpr->base.depth0,
                   size, lpr->base.reference.count);
      total += size;
      n++;
   }
   simple_mtx_unlock(&resource_list_mutex);
   debug_printf("LLVMPIPE: total size of %u resources: %u\n", n, total);
}
#endif


static void
llvmpipe_get_resource_info(struct pipe_screen *screen,
                           struct pipe_resource *resource,
                           unsigned *stride,
                           unsigned *offset)
{
   struct llvmpipe_resource *lpr = llvmpipe_resource(resource);

   *stride = lpr->row_stride[0];
   *offset = 0;
}


static bool
llvmpipe_resource_get_param(struct pipe_screen *screen,
                            struct pipe_context *context,
                            struct pipe_resource *resource,
                            unsigned plane,
                            unsigned layer,
                            unsigned level,
                            enum pipe_resource_param param,
                            unsigned handle_usage,
                            uint64_t *value)
{
   struct llvmpipe_resource *lpr = llvmpipe_resource(resource);
   struct winsys_handle whandle;

   switch (param) {
   case PIPE_RESOURCE_PARAM_NPLANES:
      *value = lpr->dmabuf ? util_format_get_num_planes(lpr->dt_format) : 1;
      return true;
   case PIPE_RESOURCE_PARAM_STRIDE:
      *value = lpr->row_stride[level];
      return true;
   case PIPE_RESOURCE_PARAM_OFFSET:
      *value = lpr->mip_offsets[level] + (lpr->img_stride[level] * layer);
      return true;
   case PIPE_RESOURCE_PARAM_LAYER_STRIDE:
      *value = lpr->img_stride[level];
      return true;
#ifndef _WIN32
   case PIPE_RESOURCE_PARAM_MODIFIER:
      *value = lpr->dmabuf ? DRM_FORMAT_MOD_LINEAR : DRM_FORMAT_MOD_INVALID;
      return true;
#endif
   case PIPE_RESOURCE_PARAM_HANDLE_TYPE_SHARED:
   case PIPE_RESOURCE_PARAM_HANDLE_TYPE_KMS:
   case PIPE_RESOURCE_PARAM_HANDLE_TYPE_FD:
      if (!lpr->dt)
         return false;

      memset(&whandle, 0, sizeof(whandle));
      if (param == PIPE_RESOURCE_PARAM_HANDLE_TYPE_SHARED)
         whandle.type = WINSYS_HANDLE_TYPE_SHARED;
      else if (param == PIPE_RESOURCE_PARAM_HANDLE_TYPE_KMS)
         whandle.type = WINSYS_HANDLE_TYPE_KMS;
      else if (param == PIPE_RESOURCE_PARAM_HANDLE_TYPE_FD)
         whandle.type = WINSYS_HANDLE_TYPE_FD;

      if (!llvmpipe_resource_get_handle(screen, context, resource,
                                        &whandle, handle_usage)) {
         return false;
      }
      *value = (uint64_t)(uintptr_t)whandle.handle;
      return true;
   default:
      break;
   }
   assert(0);
   *value = 0;
   return false;
}

#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
static void
llvmpipe_query_dmabuf_modifiers(struct pipe_screen *pscreen, enum pipe_format format, int max, uint64_t *modifiers, unsigned int *external_only, int *count)
{
   *count = 1;

   if (max)
      *modifiers = DRM_FORMAT_MOD_LINEAR;
}

static bool
llvmpipe_is_dmabuf_modifier_supported(struct pipe_screen *pscreen, uint64_t modifier, enum pipe_format format, bool *external_only)
{
   return modifier == DRM_FORMAT_MOD_LINEAR;
}

static unsigned
llvmpipe_get_dmabuf_modifier_planes(struct pipe_screen *pscreen, uint64_t modifier, enum pipe_format format)
{
   return modifier == DRM_FORMAT_MOD_LINEAR;
}
#endif

void
llvmpipe_init_screen_resource_funcs(struct pipe_screen *screen)
{
#if MESA_DEBUG
   /* init linked list for tracking resources */
   {
      static bool first_call = true;
      if (first_call) {
         memset(&resource_list, 0, sizeof(resource_list));
         list_inithead(&resource_list.list);
         first_call = false;
      }
   }
#endif

   screen->resource_create = llvmpipe_resource_create;
/*   screen->resource_create_front = llvmpipe_resource_create_front; */
   screen->resource_destroy = llvmpipe_resource_destroy;
   screen->resource_from_handle = llvmpipe_resource_from_handle;
   screen->resource_from_memobj = llvmpipe_resource_from_memobj;
   screen->resource_get_handle = llvmpipe_resource_get_handle;
   screen->can_create_resource = llvmpipe_can_create_resource;

   screen->resource_create_unbacked = llvmpipe_resource_create_unbacked;

   screen->memobj_create_from_handle = llvmpipe_memobj_create_from_handle;
   screen->memobj_destroy = llvmpipe_memobj_destroy;

   screen->resource_get_info = llvmpipe_get_resource_info;
   screen->resource_get_param = llvmpipe_resource_get_param;
   screen->resource_from_user_memory = llvmpipe_resource_from_user_memory;
   screen->allocate_memory = llvmpipe_allocate_memory;
   screen->free_memory = llvmpipe_free_memory;
#ifdef PIPE_MEMORY_FD
   screen->allocate_memory_fd = llvmpipe_allocate_memory_fd;
   screen->import_memory_fd = llvmpipe_import_memory_fd;
   screen->free_memory_fd = llvmpipe_free_memory_fd;
#endif
#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
   screen->query_dmabuf_modifiers = llvmpipe_query_dmabuf_modifiers;
   screen->is_dmabuf_modifier_supported = llvmpipe_is_dmabuf_modifier_supported;
   screen->get_dmabuf_modifier_planes = llvmpipe_get_dmabuf_modifier_planes;
   screen->resource_create_with_modifiers = llvmpipe_resource_create_with_modifiers;
#endif
   screen->map_memory = llvmpipe_map_memory;
   screen->unmap_memory = llvmpipe_unmap_memory;

   screen->resource_bind_backing = llvmpipe_resource_bind_backing;
}


void
llvmpipe_init_context_resource_funcs(struct pipe_context *pipe)
{
   pipe->buffer_map = llvmpipe_transfer_map;
   pipe->buffer_unmap = llvmpipe_transfer_unmap;
   pipe->texture_map = llvmpipe_transfer_map;
   pipe->texture_unmap = llvmpipe_transfer_unmap;

   pipe->transfer_flush_region = u_default_transfer_flush_region;
   pipe->buffer_subdata = u_default_buffer_subdata;
   pipe->texture_subdata = u_default_texture_subdata;

   pipe->memory_barrier = llvmpipe_memory_barrier;
}
