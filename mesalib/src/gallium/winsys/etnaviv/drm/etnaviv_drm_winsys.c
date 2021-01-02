/*
 * Copyright (c) 2015 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Christian Gmeiner <christian.gmeiner@gmail.com>
 */

#include <sys/stat.h>

#include "util/os_file.h"
#include "util/u_hash_table.h"
#include "util/u_memory.h"
#include "util/u_pointer.h"

#include "etnaviv/etnaviv_screen.h"
#include "etnaviv/hw/common.xml.h"
#include "etnaviv_drm_public.h"

#include <stdio.h>

static uint32_t hash_file_description(const void *key)
{
   int fd = pointer_to_intptr(key);
   struct stat stat;

   // File descriptions can't be hashed, but it should be safe to assume
   // that the same file description will always refer to he same file
   if(fstat(fd, &stat) == -1)
      return ~0; // Make sure fstat failing won't result in a random hash

   return stat.st_dev ^ stat.st_ino ^ stat.st_rdev;
}


static bool equal_file_description(const void *key1, const void *key2)
{
   int ret;
   int fd1 = pointer_to_intptr(key1);
   int fd2 = pointer_to_intptr(key2);
   struct stat stat1, stat2;

   // If the file descriptors are the same, the file description will be too
   // This will also catch sentinels, such as -1
   if (fd1 == fd2)
      return true;

   ret = os_same_file_description(fd1, fd2);
   if (ret >= 0)
      return (ret == 0);

   {
      static bool has_warned;
      if (!has_warned)
         fprintf(stderr, "os_same_file_description couldn't determine if "
                 "two DRM fds reference the same file description. (%s)\n"
                 "Let's just assume that file descriptors for the same file probably"
                 "share the file description instead. This may cause problems when"
                 "that isn't the case.\n", strerror(errno));
      has_warned = true;
   }

   // Let's at least check that it's the same file, different files can't
   // have the same file descriptions
   fstat(fd1, &stat1);
   fstat(fd2, &stat2);

   return stat1.st_dev == stat2.st_dev &&
          stat1.st_ino == stat2.st_ino &&
          stat1.st_rdev == stat2.st_rdev;
}


static struct hash_table *
hash_table_create_file_description_keys(void)
{
   return _mesa_hash_table_create(NULL, hash_file_description, equal_file_description);
}

static struct pipe_screen *
screen_create(struct renderonly *ro)
{
   struct etna_device *dev;
   struct etna_gpu *gpu;
   uint64_t val;
   int i;

   dev = etna_device_new_dup(ro->gpu_fd);
   if (!dev) {
      fprintf(stderr, "Error creating device\n");
      return NULL;
   }

   for (i = 0;; i++) {
      gpu = etna_gpu_new(dev, i);
      if (!gpu) {
         fprintf(stderr, "Error creating gpu\n");
         return NULL;
      }

      /* Look for a 3D capable GPU */
      int ret = etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_0, &val);
      if (ret == 0 && (val & chipFeatures_PIPE_3D))
         break;

      etna_gpu_del(gpu);
   }

   return etna_screen_create(dev, gpu, ro);
}

static struct hash_table *etna_tab = NULL;

static mtx_t etna_screen_mutex = _MTX_INITIALIZER_NP;

static void
etna_drm_screen_destroy(struct pipe_screen *pscreen)
{
   struct etna_screen *screen = etna_screen(pscreen);
   boolean destroy;

   mtx_lock(&etna_screen_mutex);
   destroy = --screen->refcnt == 0;
   if (destroy) {
      int fd = etna_device_fd(screen->dev);
      _mesa_hash_table_remove_key(etna_tab, intptr_to_pointer(fd));
   }
   mtx_unlock(&etna_screen_mutex);

   if (destroy) {
      pscreen->destroy = screen->winsys_priv;
      pscreen->destroy(pscreen);
   }
}

struct pipe_screen *
etna_drm_screen_create_renderonly(struct renderonly *ro)
{
   struct pipe_screen *pscreen = NULL;

   mtx_lock(&etna_screen_mutex);
   if (!etna_tab) {
      etna_tab = hash_table_create_file_description_keys();
      if (!etna_tab)
         goto unlock;
   }

   pscreen = util_hash_table_get(etna_tab, intptr_to_pointer(ro->gpu_fd));
   if (pscreen) {
      etna_screen(pscreen)->refcnt++;
   } else {
      pscreen = screen_create(ro);
      if (pscreen) {
         int fd = etna_device_fd(etna_screen(pscreen)->dev);
         _mesa_hash_table_insert(etna_tab, intptr_to_pointer(fd), pscreen);

         /* Bit of a hack, to avoid circular linkage dependency,
         * ie. pipe driver having to call in to winsys, we
         * override the pipe drivers screen->destroy() */
         etna_screen(pscreen)->winsys_priv = pscreen->destroy;
      pscreen->destroy = etna_drm_screen_destroy;
      }
   }

unlock:
   mtx_unlock(&etna_screen_mutex);
   return pscreen;
}

struct pipe_screen *
etna_drm_screen_create(int fd)
{
   struct renderonly ro = {
      .create_for_resource = renderonly_create_gpu_import_for_resource,
      .kms_fd = -1,
      .gpu_fd = fd
   };

   return etna_drm_screen_create_renderonly(&ro);
}
