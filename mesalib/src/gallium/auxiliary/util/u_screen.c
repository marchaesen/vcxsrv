/*
 * Copyright © 2018 Broadcom
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

#include <sys/stat.h>

#include "pipe/p_screen.h"
#include "util/u_screen.h"
#include "util/u_debug.h"
#include "util/os_file.h"
#include "util/os_time.h"
#include "util/simple_mtx.h"
#include "util/u_hash_table.h"
#include "util/u_pointer.h"
#include "util/macros.h"

#ifdef HAVE_LIBDRM
#include <xf86drm.h>
#endif

void
u_init_pipe_screen_caps(struct pipe_screen *pscreen, int accel)
{
   struct pipe_caps *caps = (struct pipe_caps *)&pscreen->caps;

   caps->accelerated = accel;
   caps->graphics = true;
   caps->gl_clamp = true;
   caps->max_render_targets = true;
   caps->mixed_colorbuffer_formats = true;
   caps->dithering = true;

   caps->supported_prim_modes_with_restart =
   caps->supported_prim_modes = BITFIELD_MASK(MESA_PRIM_COUNT);

   /* GL 3.x minimum value. */
   caps->min_texel_offset = -8;
   caps->max_texel_offset = 7;

   /* GL_EXT_transform_feedback minimum value. */
   caps->max_stream_output_separate_components = 4;
   caps->max_stream_output_interleaved_components = 64;

   /* Minimum GLSL level implemented by gallium drivers. */
   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility = 120;

   caps->vertex_input_alignment = PIPE_VERTEX_INPUT_ALIGNMENT_NONE;

   /* GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT default value. */
   caps->constant_buffer_offset_alignment = 1;

   /* GL_ARB_map_buffer_alignment minimum value. All drivers expose the extension. */
   caps->min_map_buffer_alignment = 64;

   /* GL_EXT_texture_buffer minimum value. */
   caps->texture_buffer_offset_alignment = 256;

   caps->texture_transfer_modes = PIPE_TEXTURE_TRANSFER_BLIT;

   /* GL_EXT_texture_buffer minimum value. */
   caps->max_texel_buffer_elements = 65536;

   caps->max_viewports = 1;

   caps->endianness = PIPE_ENDIAN_LITTLE;

   /* All new drivers should support persistent/coherent mappings. This CAP
    * should only be unset by layered drivers whose host drivers cannot support
    * coherent mappings.
    */
   caps->buffer_map_persistent_coherent = true;

   caps->min_texture_gather_offset = -8;
   caps->max_texture_gather_offset = 7;

   caps->vendor_id =
   caps->device_id = 0xffffffff;

   /* GL minimum value */
   caps->max_vertex_attrib_stride = 2048;

   /* All drivers should expose this cap, as it is required for applications to
    * be able to efficiently compile GL shaders from multiple threads during
    * load.
    */
   caps->shareable_shaders = true;

   caps->multi_draw_indirect_partial_stride = true;

   /* GLES 2.0 minimum value */
   caps->rasterizer_subpixel_bits = 4;

   caps->prefer_back_buffer_reuse = true;

   /* Drivers generally support this, and it reduces GL overhead just to
    * throw an error when buffers are mapped.
    */
   caps->allow_mapped_buffers_during_execution = true;

   /* Don't unset this unless your driver can do better, like using nir_opt_large_constants() */
   caps->prefer_imm_arrays_as_constbuf = true;

   caps->max_gs_invocations = 32;

   caps->max_shader_buffer_size = 1 << 27;

   caps->max_vertex_element_src_offset = 2047;

   caps->dest_surface_srgb_control = true;

   caps->max_varyings = 8;

   caps->throttle = true;

#if defined(HAVE_LIBDRM) && (DETECT_OS_LINUX || DETECT_OS_BSD || DETECT_OS_MANAGARM)
   if (pscreen->get_screen_fd) {
      uint64_t cap;
      int fd = pscreen->get_screen_fd(pscreen);
      if (fd != -1 && (drmGetCap(fd, DRM_CAP_PRIME, &cap) == 0))
         caps->dmabuf = cap;
   }
#endif

   /* Enables ARB_shadow */
   caps->texture_shadow_map = true;

   caps->flatshade = true;
   caps->alpha_test = true;
   caps->point_size_fixed = true;
   caps->two_sided_color = true;
   caps->clip_planes = 1;

   caps->max_vertex_buffers = 16;

   caps->nir_images_as_deref = true;

   caps->packed_stream_output = true;

   caps->gl_begin_end_buffer_size = 512 * 1024;

   caps->texrect = true;

   caps->allow_dynamic_vao_fastpath = true;

   caps->max_constant_buffer_size =
      pscreen->shader_caps[PIPE_SHADER_FRAGMENT].max_const_buffer0_size;

   /* accel=0: on CPU, always disabled
    * accel>0: on GPU, enable by default, user can disable it manually
    * accel<0: unknown, disable by default, user can enable it manually
    */
   caps->hardware_gl_select =
      !!accel && debug_get_bool_option("MESA_HW_ACCEL_SELECT", accel > 0) &&
      /* internal geometry shader need indirect array access */
      pscreen->shader_caps[PIPE_SHADER_GEOMETRY].indirect_temp_addr &&
      /* internal geometry shader need SSBO support */
      pscreen->shader_caps[PIPE_SHADER_GEOMETRY].max_shader_buffers;

   caps->query_timestamp_bits = 64;

   /* this is expected of gallium drivers, but some just don't support it */
   caps->texture_sampler_independent = true;

   caps->performance_monitor =
      pscreen->get_driver_query_info && pscreen->get_driver_query_group_info &&
      pscreen->get_driver_query_group_info(pscreen, 0, NULL) != 0;
}

uint64_t u_default_get_timestamp(UNUSED struct pipe_screen *screen)
{
   return os_time_get_nano();
}

static uint32_t
hash_file_description(const void *key)
{
   int fd = pointer_to_intptr(key);
   struct stat stat;

   // File descriptions can't be hashed, but it should be safe to assume
   // that the same file description will always refer to he same file
   if (fstat(fd, &stat) == -1)
      return ~0; // Make sure fstat failing won't result in a random hash

   return stat.st_dev ^ stat.st_ino ^ stat.st_rdev;
}


static bool
equal_file_description(const void *key1, const void *key2)
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

static struct hash_table *fd_tab = NULL;

static simple_mtx_t screen_mutex = SIMPLE_MTX_INITIALIZER;

static void
drm_screen_destroy(struct pipe_screen *pscreen)
{
   bool destroy;

   simple_mtx_lock(&screen_mutex);
   destroy = --pscreen->refcnt == 0;
   if (destroy) {
      int fd = pscreen->get_screen_fd(pscreen);
      _mesa_hash_table_remove_key(fd_tab, intptr_to_pointer(fd));

      if (!fd_tab->entries) {
         _mesa_hash_table_destroy(fd_tab, NULL);
         fd_tab = NULL;
      }
   }
   simple_mtx_unlock(&screen_mutex);

   if (destroy) {
      pscreen->destroy = pscreen->winsys_priv;
      pscreen->destroy(pscreen);
   }
}

struct pipe_screen *
u_pipe_screen_lookup_or_create(int gpu_fd,
                               const struct pipe_screen_config *config,
                               struct renderonly *ro,
                               pipe_screen_create_function screen_create)
{
   struct pipe_screen *pscreen = NULL;

   simple_mtx_lock(&screen_mutex);
   if (!fd_tab) {
      fd_tab = hash_table_create_file_description_keys();
      if (!fd_tab)
         goto unlock;
   }

   pscreen = util_hash_table_get(fd_tab, intptr_to_pointer(gpu_fd));
   if (pscreen) {
      pscreen->refcnt++;
   } else {
      pscreen = screen_create(gpu_fd, config, ro);
      if (pscreen) {
         pscreen->refcnt = 1;
         _mesa_hash_table_insert(fd_tab, intptr_to_pointer(gpu_fd), pscreen);

         /* Bit of a hack, to avoid circular linkage dependency,
          * ie. pipe driver having to call in to winsys, we
          * override the pipe drivers screen->destroy() */
         pscreen->winsys_priv = pscreen->destroy;
         pscreen->destroy = drm_screen_destroy;
      }
   }

unlock:
   simple_mtx_unlock(&screen_mutex);
   return pscreen;
}
