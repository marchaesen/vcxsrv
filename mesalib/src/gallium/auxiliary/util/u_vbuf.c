/**************************************************************************
 *
 * Copyright 2011 Marek Olšák <maraeo@gmail.com>
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
 * IN NO EVENT SHALL AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/**
 * This module uploads user buffers and translates the vertex buffers which
 * contain incompatible vertices (i.e. not supported by the driver/hardware)
 * into compatible ones, based on the Gallium CAPs.
 *
 * It does not upload index buffers.
 *
 * The module heavily uses bitmasks to represent per-buffer and
 * per-vertex-element flags to avoid looping over the list of buffers just
 * to see if there's a non-zero stride, or user buffer, or unsupported format,
 * etc.
 *
 * There are 3 categories of vertex elements, which are processed separately:
 * - per-vertex attribs (stride != 0, instance_divisor == 0)
 * - instanced attribs (stride != 0, instance_divisor > 0)
 * - constant attribs (stride == 0)
 *
 * All needed uploads and translations are performed every draw command, but
 * only the subset of vertices needed for that draw command is uploaded or
 * translated. (the module never translates whole buffers)
 *
 *
 * The module consists of two main parts:
 *
 *
 * 1) Translate (u_vbuf_translate_begin/end)
 *
 * This is pretty much a vertex fetch fallback. It translates vertices from
 * one vertex buffer to another in an unused vertex buffer slot. It does
 * whatever is needed to make the vertices readable by the hardware (changes
 * vertex formats and aligns offsets and strides). The translate module is
 * used here.
 *
 * Each of the 3 categories is translated to a separate buffer.
 * Only the [min_index, max_index] range is translated. For instanced attribs,
 * the range is [start_instance, start_instance+instance_count]. For constant
 * attribs, the range is [0, 1].
 *
 *
 * 2) User buffer uploading (u_vbuf_upload_buffers)
 *
 * Only the [min_index, max_index] range is uploaded (just like Translate)
 * with a single memcpy.
 *
 * This method works best for non-indexed draw operations or indexed draw
 * operations where the [min_index, max_index] range is not being way bigger
 * than the vertex count.
 *
 * If the range is too big (e.g. one triangle with indices {0, 1, 10000}),
 * the per-vertex attribs are uploaded via the translate module, all packed
 * into one vertex buffer, and the indexed draw call is turned into
 * a non-indexed one in the process. This adds additional complexity
 * to the translate part, but it prevents bad apps from bringing your frame
 * rate down.
 *
 *
 * If there is nothing to do, it forwards every command to the driver.
 * The module also has its own CSO cache of vertex element states.
 */

#include "util/u_vbuf.h"

#include "util/u_dump.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_upload_mgr.h"
#include "translate/translate.h"
#include "translate/translate_cache.h"
#include "cso_cache/cso_cache.h"
#include "cso_cache/cso_hash.h"

struct u_vbuf_elements {
   unsigned count;
   struct pipe_vertex_element ve[PIPE_MAX_ATTRIBS];

   unsigned src_format_size[PIPE_MAX_ATTRIBS];

   /* If (velem[i].src_format != native_format[i]), the vertex buffer
    * referenced by the vertex element cannot be used for rendering and
    * its vertex data must be translated to native_format[i]. */
   enum pipe_format native_format[PIPE_MAX_ATTRIBS];
   unsigned native_format_size[PIPE_MAX_ATTRIBS];

   /* Which buffers are used by the vertex element state. */
   uint32_t used_vb_mask;
   /* This might mean two things:
    * - src_format != native_format, as discussed above.
    * - src_offset % 4 != 0 (if the caps don't allow such an offset). */
   uint32_t incompatible_elem_mask; /* each bit describes a corresp. attrib  */
   /* Which buffer has at least one vertex element referencing it
    * incompatible. */
   uint32_t incompatible_vb_mask_any;
   /* Which buffer has all vertex elements referencing it incompatible. */
   uint32_t incompatible_vb_mask_all;
   /* Which buffer has at least one vertex element referencing it
    * compatible. */
   uint32_t compatible_vb_mask_any;
   /* Which buffer has all vertex elements referencing it compatible. */
   uint32_t compatible_vb_mask_all;

   /* Which buffer has at least one vertex element referencing it
    * non-instanced. */
   uint32_t noninstance_vb_mask_any;

   void *driver_cso;
};

enum {
   VB_VERTEX = 0,
   VB_INSTANCE = 1,
   VB_CONST = 2,
   VB_NUM = 3
};

struct u_vbuf {
   struct u_vbuf_caps caps;
   bool has_signed_vb_offset;

   struct pipe_context *pipe;
   struct translate_cache *translate_cache;
   struct cso_cache *cso_cache;

   /* This is what was set in set_vertex_buffers.
    * May contain user buffers. */
   struct pipe_vertex_buffer vertex_buffer[PIPE_MAX_ATTRIBS];
   uint32_t enabled_vb_mask;

   /* Saved vertex buffer. */
   struct pipe_vertex_buffer vertex_buffer0_saved;

   /* Vertex buffers for the driver.
    * There are usually no user buffers. */
   struct pipe_vertex_buffer real_vertex_buffer[PIPE_MAX_ATTRIBS];
   uint32_t dirty_real_vb_mask; /* which buffers are dirty since the last
                                   call of set_vertex_buffers */

   /* Vertex elements. */
   struct u_vbuf_elements *ve, *ve_saved;

   /* Vertex elements used for the translate fallback. */
   struct pipe_vertex_element fallback_velems[PIPE_MAX_ATTRIBS];
   /* If non-NULL, this is a vertex element state used for the translate
    * fallback and therefore used for rendering too. */
   boolean using_translate;
   /* The vertex buffer slot index where translated vertices have been
    * stored in. */
   unsigned fallback_vbs[VB_NUM];

   /* Which buffer is a user buffer. */
   uint32_t user_vb_mask; /* each bit describes a corresp. buffer */
   /* Which buffer is incompatible (unaligned). */
   uint32_t incompatible_vb_mask; /* each bit describes a corresp. buffer */
   /* Which buffer has a non-zero stride. */
   uint32_t nonzero_stride_vb_mask; /* each bit describes a corresp. buffer */
};

static void *
u_vbuf_create_vertex_elements(struct u_vbuf *mgr, unsigned count,
                              const struct pipe_vertex_element *attribs);
static void u_vbuf_delete_vertex_elements(struct u_vbuf *mgr, void *cso);

static const struct {
   enum pipe_format from, to;
} vbuf_format_fallbacks[] = {
   { PIPE_FORMAT_R32_FIXED,            PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R32G32_FIXED,         PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R32G32B32_FIXED,      PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R32G32B32A32_FIXED,   PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R16_FLOAT,            PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R16G16_FLOAT,         PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R16G16B16_FLOAT,      PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R16G16B16A16_FLOAT,   PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R64_FLOAT,            PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R64G64_FLOAT,         PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R64G64B64_FLOAT,      PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R64G64B64A64_FLOAT,   PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R32_UNORM,            PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R32G32_UNORM,         PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R32G32B32_UNORM,      PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R32G32B32A32_UNORM,   PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R32_SNORM,            PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R32G32_SNORM,         PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R32G32B32_SNORM,      PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R32G32B32A32_SNORM,   PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R32_USCALED,          PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R32G32_USCALED,       PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R32G32B32_USCALED,    PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R32G32B32A32_USCALED, PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R32_SSCALED,          PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R32G32_SSCALED,       PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R32G32B32_SSCALED,    PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R32G32B32A32_SSCALED, PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R16_UNORM,            PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R16G16_UNORM,         PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R16G16B16_UNORM,      PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R16G16B16A16_UNORM,   PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R16_SNORM,            PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R16G16_SNORM,         PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R16G16B16_SNORM,      PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R16G16B16A16_SNORM,   PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R16_USCALED,          PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R16G16_USCALED,       PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R16G16B16_USCALED,    PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R16G16B16A16_USCALED, PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R16_SSCALED,          PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R16G16_SSCALED,       PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R16G16B16_SSCALED,    PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R16G16B16A16_SSCALED, PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R8_UNORM,             PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R8G8_UNORM,           PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R8G8B8_UNORM,         PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R8G8B8A8_UNORM,       PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R8_SNORM,             PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R8G8_SNORM,           PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R8G8B8_SNORM,         PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R8G8B8A8_SNORM,       PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R8_USCALED,           PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R8G8_USCALED,         PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R8G8B8_USCALED,       PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R8G8B8A8_USCALED,     PIPE_FORMAT_R32G32B32A32_FLOAT },
   { PIPE_FORMAT_R8_SSCALED,           PIPE_FORMAT_R32_FLOAT },
   { PIPE_FORMAT_R8G8_SSCALED,         PIPE_FORMAT_R32G32_FLOAT },
   { PIPE_FORMAT_R8G8B8_SSCALED,       PIPE_FORMAT_R32G32B32_FLOAT },
   { PIPE_FORMAT_R8G8B8A8_SSCALED,     PIPE_FORMAT_R32G32B32A32_FLOAT },
};

boolean u_vbuf_get_caps(struct pipe_screen *screen, struct u_vbuf_caps *caps,
                        unsigned flags)
{
   unsigned i;
   boolean fallback = FALSE;

   /* I'd rather have a bitfield of which formats are supported and a static
    * table of the translations indexed by format, but since we don't have C99
    * we can't easily make a sparsely-populated table indexed by format.  So,
    * we construct the sparse table here.
    */
   for (i = 0; i < PIPE_FORMAT_COUNT; i++)
      caps->format_translation[i] = i;

   for (i = 0; i < ARRAY_SIZE(vbuf_format_fallbacks); i++) {
      enum pipe_format format = vbuf_format_fallbacks[i].from;

      if (!screen->is_format_supported(screen, format, PIPE_BUFFER, 0,
                                       PIPE_BIND_VERTEX_BUFFER)) {
         caps->format_translation[format] = vbuf_format_fallbacks[i].to;
         fallback = TRUE;
      }
   }

   caps->buffer_offset_unaligned =
      !screen->get_param(screen,
                         PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY);
   caps->buffer_stride_unaligned =
     !screen->get_param(screen,
                        PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY);
   caps->velem_src_offset_unaligned =
      !screen->get_param(screen,
                         PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY);
   caps->user_vertex_buffers =
      screen->get_param(screen, PIPE_CAP_USER_VERTEX_BUFFERS);

   if (!caps->buffer_offset_unaligned ||
       !caps->buffer_stride_unaligned ||
       !caps->velem_src_offset_unaligned ||
       (!(flags & U_VBUF_FLAG_NO_USER_VBOS) && !caps->user_vertex_buffers)) {
      fallback = TRUE;
   }

   return fallback;
}

struct u_vbuf *
u_vbuf_create(struct pipe_context *pipe, struct u_vbuf_caps *caps)
{
   struct u_vbuf *mgr = CALLOC_STRUCT(u_vbuf);

   mgr->caps = *caps;
   mgr->pipe = pipe;
   mgr->cso_cache = cso_cache_create();
   mgr->translate_cache = translate_cache_create();
   memset(mgr->fallback_vbs, ~0, sizeof(mgr->fallback_vbs));

   mgr->has_signed_vb_offset =
      pipe->screen->get_param(pipe->screen,
                              PIPE_CAP_SIGNED_VERTEX_BUFFER_OFFSET);

   return mgr;
}

/* u_vbuf uses its own caching for vertex elements, because it needs to keep
 * its own preprocessed state per vertex element CSO. */
static struct u_vbuf_elements *
u_vbuf_set_vertex_elements_internal(struct u_vbuf *mgr, unsigned count,
                                    const struct pipe_vertex_element *states)
{
   struct pipe_context *pipe = mgr->pipe;
   unsigned key_size, hash_key;
   struct cso_hash_iter iter;
   struct u_vbuf_elements *ve;
   struct cso_velems_state velems_state;

   /* need to include the count into the stored state data too. */
   key_size = sizeof(struct pipe_vertex_element) * count + sizeof(unsigned);
   velems_state.count = count;
   memcpy(velems_state.velems, states,
          sizeof(struct pipe_vertex_element) * count);
   hash_key = cso_construct_key((void*)&velems_state, key_size);
   iter = cso_find_state_template(mgr->cso_cache, hash_key, CSO_VELEMENTS,
                                  (void*)&velems_state, key_size);

   if (cso_hash_iter_is_null(iter)) {
      struct cso_velements *cso = MALLOC_STRUCT(cso_velements);
      memcpy(&cso->state, &velems_state, key_size);
      cso->data = u_vbuf_create_vertex_elements(mgr, count, states);
      cso->delete_state = (cso_state_callback)u_vbuf_delete_vertex_elements;
      cso->context = (void*)mgr;

      iter = cso_insert_state(mgr->cso_cache, hash_key, CSO_VELEMENTS, cso);
      ve = cso->data;
   } else {
      ve = ((struct cso_velements *)cso_hash_iter_data(iter))->data;
   }

   assert(ve);

   if (ve != mgr->ve)
      pipe->bind_vertex_elements_state(pipe, ve->driver_cso);

   return ve;
}

void u_vbuf_set_vertex_elements(struct u_vbuf *mgr, unsigned count,
                               const struct pipe_vertex_element *states)
{
   mgr->ve = u_vbuf_set_vertex_elements_internal(mgr, count, states);
}

void u_vbuf_destroy(struct u_vbuf *mgr)
{
   struct pipe_screen *screen = mgr->pipe->screen;
   unsigned i;
   const unsigned num_vb = screen->get_shader_param(screen, PIPE_SHADER_VERTEX,
                                                    PIPE_SHADER_CAP_MAX_INPUTS);

   mgr->pipe->set_vertex_buffers(mgr->pipe, 0, num_vb, NULL);

   for (i = 0; i < PIPE_MAX_ATTRIBS; i++)
      pipe_vertex_buffer_unreference(&mgr->vertex_buffer[i]);
   for (i = 0; i < PIPE_MAX_ATTRIBS; i++)
      pipe_vertex_buffer_unreference(&mgr->real_vertex_buffer[i]);

   pipe_vertex_buffer_unreference(&mgr->vertex_buffer0_saved);

   translate_cache_destroy(mgr->translate_cache);
   cso_cache_delete(mgr->cso_cache);
   FREE(mgr);
}

static enum pipe_error
u_vbuf_translate_buffers(struct u_vbuf *mgr, struct translate_key *key,
                         const struct pipe_draw_info *info,
                         unsigned vb_mask, unsigned out_vb,
                         int start_vertex, unsigned num_vertices,
                         int min_index, boolean unroll_indices)
{
   struct translate *tr;
   struct pipe_transfer *vb_transfer[PIPE_MAX_ATTRIBS] = {0};
   struct pipe_resource *out_buffer = NULL;
   uint8_t *out_map;
   unsigned out_offset, mask;

   /* Get a translate object. */
   tr = translate_cache_find(mgr->translate_cache, key);

   /* Map buffers we want to translate. */
   mask = vb_mask;
   while (mask) {
      struct pipe_vertex_buffer *vb;
      unsigned offset;
      uint8_t *map;
      unsigned i = u_bit_scan(&mask);

      vb = &mgr->vertex_buffer[i];
      offset = vb->buffer_offset + vb->stride * start_vertex;

      if (vb->is_user_buffer) {
         map = (uint8_t*)vb->buffer.user + offset;
      } else {
         unsigned size = vb->stride ? num_vertices * vb->stride
                                    : sizeof(double)*4;

         if (offset + size > vb->buffer.resource->width0) {
            /* Don't try to map past end of buffer.  This often happens when
             * we're translating an attribute that's at offset > 0 from the
             * start of the vertex.  If we'd subtract attrib's offset from
             * the size, this probably wouldn't happen.
             */
            size = vb->buffer.resource->width0 - offset;

            /* Also adjust num_vertices.  A common user error is to call
             * glDrawRangeElements() with incorrect 'end' argument.  The 'end
             * value should be the max index value, but people often
             * accidentally add one to this value.  This adjustment avoids
             * crashing (by reading past the end of a hardware buffer mapping)
             * when people do that.
             */
            num_vertices = (size + vb->stride - 1) / vb->stride;
         }

         map = pipe_buffer_map_range(mgr->pipe, vb->buffer.resource, offset, size,
                                     PIPE_TRANSFER_READ, &vb_transfer[i]);
      }

      /* Subtract min_index so that indexing with the index buffer works. */
      if (unroll_indices) {
         map -= (ptrdiff_t)vb->stride * min_index;
      }

      tr->set_buffer(tr, i, map, vb->stride, info->max_index);
   }

   /* Translate. */
   if (unroll_indices) {
      struct pipe_transfer *transfer = NULL;
      const unsigned offset = info->start * info->index_size;
      uint8_t *map;

      /* Create and map the output buffer. */
      u_upload_alloc(mgr->pipe->stream_uploader, 0,
                     key->output_stride * info->count, 4,
                     &out_offset, &out_buffer,
                     (void**)&out_map);
      if (!out_buffer)
         return PIPE_ERROR_OUT_OF_MEMORY;

      if (info->has_user_indices) {
         map = (uint8_t*)info->index.user + offset;
      } else {
         map = pipe_buffer_map_range(mgr->pipe, info->index.resource, offset,
                                     info->count * info->index_size,
                                     PIPE_TRANSFER_READ, &transfer);
      }

      switch (info->index_size) {
      case 4:
         tr->run_elts(tr, (unsigned*)map, info->count, 0, 0, out_map);
         break;
      case 2:
         tr->run_elts16(tr, (uint16_t*)map, info->count, 0, 0, out_map);
         break;
      case 1:
         tr->run_elts8(tr, map, info->count, 0, 0, out_map);
         break;
      }

      if (transfer) {
         pipe_buffer_unmap(mgr->pipe, transfer);
      }
   } else {
      /* Create and map the output buffer. */
      u_upload_alloc(mgr->pipe->stream_uploader,
                     mgr->has_signed_vb_offset ?
                        0 : key->output_stride * start_vertex,
                     key->output_stride * num_vertices, 4,
                     &out_offset, &out_buffer,
                     (void**)&out_map);
      if (!out_buffer)
         return PIPE_ERROR_OUT_OF_MEMORY;

      out_offset -= key->output_stride * start_vertex;

      tr->run(tr, 0, num_vertices, 0, 0, out_map);
   }

   /* Unmap all buffers. */
   mask = vb_mask;
   while (mask) {
      unsigned i = u_bit_scan(&mask);

      if (vb_transfer[i]) {
         pipe_buffer_unmap(mgr->pipe, vb_transfer[i]);
      }
   }

   /* Setup the new vertex buffer. */
   mgr->real_vertex_buffer[out_vb].buffer_offset = out_offset;
   mgr->real_vertex_buffer[out_vb].stride = key->output_stride;

   /* Move the buffer reference. */
   pipe_vertex_buffer_unreference(&mgr->real_vertex_buffer[out_vb]);
   mgr->real_vertex_buffer[out_vb].buffer.resource = out_buffer;
   mgr->real_vertex_buffer[out_vb].is_user_buffer = false;

   return PIPE_OK;
}

static boolean
u_vbuf_translate_find_free_vb_slots(struct u_vbuf *mgr,
                                    unsigned mask[VB_NUM])
{
   unsigned type;
   unsigned fallback_vbs[VB_NUM];
   /* Set the bit for each buffer which is incompatible, or isn't set. */
   uint32_t unused_vb_mask =
      mgr->ve->incompatible_vb_mask_all | mgr->incompatible_vb_mask |
      ~mgr->enabled_vb_mask;

   memset(fallback_vbs, ~0, sizeof(fallback_vbs));

   /* Find free slots for each type if needed. */
   for (type = 0; type < VB_NUM; type++) {
      if (mask[type]) {
         uint32_t index;

         if (!unused_vb_mask) {
            return FALSE;
         }

         index = ffs(unused_vb_mask) - 1;
         fallback_vbs[type] = index;
         unused_vb_mask &= ~(1 << index);
         /*printf("found slot=%i for type=%i\n", index, type);*/
      }
   }

   for (type = 0; type < VB_NUM; type++) {
      if (mask[type]) {
         mgr->dirty_real_vb_mask |= 1 << fallback_vbs[type];
      }
   }

   memcpy(mgr->fallback_vbs, fallback_vbs, sizeof(fallback_vbs));
   return TRUE;
}

static boolean
u_vbuf_translate_begin(struct u_vbuf *mgr,
                       const struct pipe_draw_info *info,
                       int start_vertex, unsigned num_vertices,
                       int min_index, boolean unroll_indices)
{
   unsigned mask[VB_NUM] = {0};
   struct translate_key key[VB_NUM];
   unsigned elem_index[VB_NUM][PIPE_MAX_ATTRIBS]; /* ... into key.elements */
   unsigned i, type;
   const unsigned incompatible_vb_mask = mgr->incompatible_vb_mask &
                                         mgr->ve->used_vb_mask;

   const int start[VB_NUM] = {
      start_vertex,           /* VERTEX */
      info->start_instance,   /* INSTANCE */
      0                       /* CONST */
   };

   const unsigned num[VB_NUM] = {
      num_vertices,           /* VERTEX */
      info->instance_count,   /* INSTANCE */
      1                       /* CONST */
   };

   memset(key, 0, sizeof(key));
   memset(elem_index, ~0, sizeof(elem_index));

   /* See if there are vertex attribs of each type to translate and
    * which ones. */
   for (i = 0; i < mgr->ve->count; i++) {
      unsigned vb_index = mgr->ve->ve[i].vertex_buffer_index;

      if (!mgr->vertex_buffer[vb_index].stride) {
         if (!(mgr->ve->incompatible_elem_mask & (1 << i)) &&
             !(incompatible_vb_mask & (1 << vb_index))) {
            continue;
         }
         mask[VB_CONST] |= 1 << vb_index;
      } else if (mgr->ve->ve[i].instance_divisor) {
         if (!(mgr->ve->incompatible_elem_mask & (1 << i)) &&
             !(incompatible_vb_mask & (1 << vb_index))) {
            continue;
         }
         mask[VB_INSTANCE] |= 1 << vb_index;
      } else {
         if (!unroll_indices &&
             !(mgr->ve->incompatible_elem_mask & (1 << i)) &&
             !(incompatible_vb_mask & (1 << vb_index))) {
            continue;
         }
         mask[VB_VERTEX] |= 1 << vb_index;
      }
   }

   assert(mask[VB_VERTEX] || mask[VB_INSTANCE] || mask[VB_CONST]);

   /* Find free vertex buffer slots. */
   if (!u_vbuf_translate_find_free_vb_slots(mgr, mask)) {
      return FALSE;
   }

   /* Initialize the translate keys. */
   for (i = 0; i < mgr->ve->count; i++) {
      struct translate_key *k;
      struct translate_element *te;
      enum pipe_format output_format = mgr->ve->native_format[i];
      unsigned bit, vb_index = mgr->ve->ve[i].vertex_buffer_index;
      bit = 1 << vb_index;

      if (!(mgr->ve->incompatible_elem_mask & (1 << i)) &&
          !(incompatible_vb_mask & (1 << vb_index)) &&
          (!unroll_indices || !(mask[VB_VERTEX] & bit))) {
         continue;
      }

      /* Set type to what we will translate.
       * Whether vertex, instance, or constant attribs. */
      for (type = 0; type < VB_NUM; type++) {
         if (mask[type] & bit) {
            break;
         }
      }
      assert(type < VB_NUM);
      if (mgr->ve->ve[i].src_format != output_format)
         assert(translate_is_output_format_supported(output_format));
      /*printf("velem=%i type=%i\n", i, type);*/

      /* Add the vertex element. */
      k = &key[type];
      elem_index[type][i] = k->nr_elements;

      te = &k->element[k->nr_elements];
      te->type = TRANSLATE_ELEMENT_NORMAL;
      te->instance_divisor = 0;
      te->input_buffer = vb_index;
      te->input_format = mgr->ve->ve[i].src_format;
      te->input_offset = mgr->ve->ve[i].src_offset;
      te->output_format = output_format;
      te->output_offset = k->output_stride;

      k->output_stride += mgr->ve->native_format_size[i];
      k->nr_elements++;
   }

   /* Translate buffers. */
   for (type = 0; type < VB_NUM; type++) {
      if (key[type].nr_elements) {
         enum pipe_error err;
         err = u_vbuf_translate_buffers(mgr, &key[type], info, mask[type],
                                        mgr->fallback_vbs[type],
                                        start[type], num[type], min_index,
                                        unroll_indices && type == VB_VERTEX);
         if (err != PIPE_OK)
            return FALSE;

         /* Fixup the stride for constant attribs. */
         if (type == VB_CONST) {
            mgr->real_vertex_buffer[mgr->fallback_vbs[VB_CONST]].stride = 0;
         }
      }
   }

   /* Setup new vertex elements. */
   for (i = 0; i < mgr->ve->count; i++) {
      for (type = 0; type < VB_NUM; type++) {
         if (elem_index[type][i] < key[type].nr_elements) {
            struct translate_element *te = &key[type].element[elem_index[type][i]];
            mgr->fallback_velems[i].instance_divisor = mgr->ve->ve[i].instance_divisor;
            mgr->fallback_velems[i].src_format = te->output_format;
            mgr->fallback_velems[i].src_offset = te->output_offset;
            mgr->fallback_velems[i].vertex_buffer_index = mgr->fallback_vbs[type];

            /* elem_index[type][i] can only be set for one type. */
            assert(type > VB_INSTANCE || elem_index[type+1][i] == ~0u);
            assert(type > VB_VERTEX   || elem_index[type+2][i] == ~0u);
            break;
         }
      }
      /* No translating, just copy the original vertex element over. */
      if (type == VB_NUM) {
         memcpy(&mgr->fallback_velems[i], &mgr->ve->ve[i],
                sizeof(struct pipe_vertex_element));
      }
   }

   u_vbuf_set_vertex_elements_internal(mgr, mgr->ve->count,
                                       mgr->fallback_velems);
   mgr->using_translate = TRUE;
   return TRUE;
}

static void u_vbuf_translate_end(struct u_vbuf *mgr)
{
   unsigned i;

   /* Restore vertex elements. */
   mgr->pipe->bind_vertex_elements_state(mgr->pipe, mgr->ve->driver_cso);
   mgr->using_translate = FALSE;

   /* Unreference the now-unused VBOs. */
   for (i = 0; i < VB_NUM; i++) {
      unsigned vb = mgr->fallback_vbs[i];
      if (vb != ~0u) {
         pipe_resource_reference(&mgr->real_vertex_buffer[vb].buffer.resource, NULL);
         mgr->fallback_vbs[i] = ~0;

         /* This will cause the buffer to be unbound in the driver later. */
         mgr->dirty_real_vb_mask |= 1 << vb;
      }
   }
}

static void *
u_vbuf_create_vertex_elements(struct u_vbuf *mgr, unsigned count,
                              const struct pipe_vertex_element *attribs)
{
   struct pipe_context *pipe = mgr->pipe;
   unsigned i;
   struct pipe_vertex_element driver_attribs[PIPE_MAX_ATTRIBS];
   struct u_vbuf_elements *ve = CALLOC_STRUCT(u_vbuf_elements);
   uint32_t used_buffers = 0;

   ve->count = count;

   memcpy(ve->ve, attribs, sizeof(struct pipe_vertex_element) * count);
   memcpy(driver_attribs, attribs, sizeof(struct pipe_vertex_element) * count);

   /* Set the best native format in case the original format is not
    * supported. */
   for (i = 0; i < count; i++) {
      enum pipe_format format = ve->ve[i].src_format;

      ve->src_format_size[i] = util_format_get_blocksize(format);

      used_buffers |= 1 << ve->ve[i].vertex_buffer_index;

      if (!ve->ve[i].instance_divisor) {
         ve->noninstance_vb_mask_any |= 1 << ve->ve[i].vertex_buffer_index;
      }

      format = mgr->caps.format_translation[format];

      driver_attribs[i].src_format = format;
      ve->native_format[i] = format;
      ve->native_format_size[i] =
            util_format_get_blocksize(ve->native_format[i]);

      if (ve->ve[i].src_format != format ||
          (!mgr->caps.velem_src_offset_unaligned &&
           ve->ve[i].src_offset % 4 != 0)) {
         ve->incompatible_elem_mask |= 1 << i;
         ve->incompatible_vb_mask_any |= 1 << ve->ve[i].vertex_buffer_index;
      } else {
         ve->compatible_vb_mask_any |= 1 << ve->ve[i].vertex_buffer_index;
      }
   }

   ve->used_vb_mask = used_buffers;
   ve->compatible_vb_mask_all = ~ve->incompatible_vb_mask_any & used_buffers;
   ve->incompatible_vb_mask_all = ~ve->compatible_vb_mask_any & used_buffers;

   /* Align the formats and offsets to the size of DWORD if needed. */
   if (!mgr->caps.velem_src_offset_unaligned) {
      for (i = 0; i < count; i++) {
         ve->native_format_size[i] = align(ve->native_format_size[i], 4);
         driver_attribs[i].src_offset = align(ve->ve[i].src_offset, 4);
      }
   }

   ve->driver_cso =
      pipe->create_vertex_elements_state(pipe, count, driver_attribs);
   return ve;
}

static void u_vbuf_delete_vertex_elements(struct u_vbuf *mgr, void *cso)
{
   struct pipe_context *pipe = mgr->pipe;
   struct u_vbuf_elements *ve = cso;

   pipe->delete_vertex_elements_state(pipe, ve->driver_cso);
   FREE(ve);
}

void u_vbuf_set_vertex_buffers(struct u_vbuf *mgr,
                               unsigned start_slot, unsigned count,
                               const struct pipe_vertex_buffer *bufs)
{
   unsigned i;
   /* which buffers are enabled */
   uint32_t enabled_vb_mask = 0;
   /* which buffers are in user memory */
   uint32_t user_vb_mask = 0;
   /* which buffers are incompatible with the driver */
   uint32_t incompatible_vb_mask = 0;
   /* which buffers have a non-zero stride */
   uint32_t nonzero_stride_vb_mask = 0;
   const uint32_t mask = ~(((1ull << count) - 1) << start_slot);

   /* Zero out the bits we are going to rewrite completely. */
   mgr->user_vb_mask &= mask;
   mgr->incompatible_vb_mask &= mask;
   mgr->nonzero_stride_vb_mask &= mask;
   mgr->enabled_vb_mask &= mask;

   if (!bufs) {
      struct pipe_context *pipe = mgr->pipe;
      /* Unbind. */
      mgr->dirty_real_vb_mask &= mask;

      for (i = 0; i < count; i++) {
         unsigned dst_index = start_slot + i;

         pipe_vertex_buffer_unreference(&mgr->vertex_buffer[dst_index]);
         pipe_vertex_buffer_unreference(&mgr->real_vertex_buffer[dst_index]);
      }

      pipe->set_vertex_buffers(pipe, start_slot, count, NULL);
      return;
   }

   for (i = 0; i < count; i++) {
      unsigned dst_index = start_slot + i;
      const struct pipe_vertex_buffer *vb = &bufs[i];
      struct pipe_vertex_buffer *orig_vb = &mgr->vertex_buffer[dst_index];
      struct pipe_vertex_buffer *real_vb = &mgr->real_vertex_buffer[dst_index];

      if (!vb->buffer.resource) {
         pipe_vertex_buffer_unreference(orig_vb);
         pipe_vertex_buffer_unreference(real_vb);
         continue;
      }

      pipe_vertex_buffer_reference(orig_vb, vb);

      if (vb->stride) {
         nonzero_stride_vb_mask |= 1 << dst_index;
      }
      enabled_vb_mask |= 1 << dst_index;

      if ((!mgr->caps.buffer_offset_unaligned && vb->buffer_offset % 4 != 0) ||
          (!mgr->caps.buffer_stride_unaligned && vb->stride % 4 != 0)) {
         incompatible_vb_mask |= 1 << dst_index;
         real_vb->buffer_offset = vb->buffer_offset;
         real_vb->stride = vb->stride;
         pipe_vertex_buffer_unreference(real_vb);
         real_vb->is_user_buffer = false;
         continue;
      }

      if (!mgr->caps.user_vertex_buffers && vb->is_user_buffer) {
         user_vb_mask |= 1 << dst_index;
         real_vb->buffer_offset = vb->buffer_offset;
         real_vb->stride = vb->stride;
         pipe_vertex_buffer_unreference(real_vb);
         real_vb->is_user_buffer = false;
         continue;
      }

      pipe_vertex_buffer_reference(real_vb, vb);
   }

   mgr->user_vb_mask |= user_vb_mask;
   mgr->incompatible_vb_mask |= incompatible_vb_mask;
   mgr->nonzero_stride_vb_mask |= nonzero_stride_vb_mask;
   mgr->enabled_vb_mask |= enabled_vb_mask;

   /* All changed buffers are marked as dirty, even the NULL ones,
    * which will cause the NULL buffers to be unbound in the driver later. */
   mgr->dirty_real_vb_mask |= ~mask;
}

static enum pipe_error
u_vbuf_upload_buffers(struct u_vbuf *mgr,
                      int start_vertex, unsigned num_vertices,
                      int start_instance, unsigned num_instances)
{
   unsigned i;
   unsigned nr_velems = mgr->ve->count;
   const struct pipe_vertex_element *velems =
         mgr->using_translate ? mgr->fallback_velems : mgr->ve->ve;
   unsigned start_offset[PIPE_MAX_ATTRIBS];
   unsigned end_offset[PIPE_MAX_ATTRIBS];
   uint32_t buffer_mask = 0;

   /* Determine how much data needs to be uploaded. */
   for (i = 0; i < nr_velems; i++) {
      const struct pipe_vertex_element *velem = &velems[i];
      unsigned index = velem->vertex_buffer_index;
      struct pipe_vertex_buffer *vb = &mgr->vertex_buffer[index];
      unsigned instance_div, first, size, index_bit;

      /* Skip the buffers generated by translate. */
      if (index == mgr->fallback_vbs[VB_VERTEX] ||
          index == mgr->fallback_vbs[VB_INSTANCE] ||
          index == mgr->fallback_vbs[VB_CONST]) {
         continue;
      }

      if (!vb->is_user_buffer) {
         continue;
      }

      instance_div = velem->instance_divisor;
      first = vb->buffer_offset + velem->src_offset;

      if (!vb->stride) {
         /* Constant attrib. */
         size = mgr->ve->src_format_size[i];
      } else if (instance_div) {
         /* Per-instance attrib. */

         /* Figure out how many instances we'll render given instance_div.  We
          * can't use the typical div_round_up() pattern because the CTS uses
          * instance_div = ~0 for a test, which overflows div_round_up()'s
          * addition.
          */
         unsigned count = num_instances / instance_div;
         if (count * instance_div != num_instances)
            count++;

         first += vb->stride * start_instance;
         size = vb->stride * (count - 1) + mgr->ve->src_format_size[i];
      } else {
         /* Per-vertex attrib. */
         first += vb->stride * start_vertex;
         size = vb->stride * (num_vertices - 1) + mgr->ve->src_format_size[i];
      }

      index_bit = 1 << index;

      /* Update offsets. */
      if (!(buffer_mask & index_bit)) {
         start_offset[index] = first;
         end_offset[index] = first + size;
      } else {
         if (first < start_offset[index])
            start_offset[index] = first;
         if (first + size > end_offset[index])
            end_offset[index] = first + size;
      }

      buffer_mask |= index_bit;
   }

   /* Upload buffers. */
   while (buffer_mask) {
      unsigned start, end;
      struct pipe_vertex_buffer *real_vb;
      const uint8_t *ptr;

      i = u_bit_scan(&buffer_mask);

      start = start_offset[i];
      end = end_offset[i];
      assert(start < end);

      real_vb = &mgr->real_vertex_buffer[i];
      ptr = mgr->vertex_buffer[i].buffer.user;

      u_upload_data(mgr->pipe->stream_uploader,
                    mgr->has_signed_vb_offset ? 0 : start,
                    end - start, 4,
                    ptr + start, &real_vb->buffer_offset, &real_vb->buffer.resource);
      if (!real_vb->buffer.resource)
         return PIPE_ERROR_OUT_OF_MEMORY;

      real_vb->buffer_offset -= start;
   }

   return PIPE_OK;
}

static boolean u_vbuf_need_minmax_index(const struct u_vbuf *mgr)
{
   /* See if there are any per-vertex attribs which will be uploaded or
    * translated. Use bitmasks to get the info instead of looping over vertex
    * elements. */
   return (mgr->ve->used_vb_mask &
           ((mgr->user_vb_mask |
             mgr->incompatible_vb_mask |
             mgr->ve->incompatible_vb_mask_any) &
            mgr->ve->noninstance_vb_mask_any &
            mgr->nonzero_stride_vb_mask)) != 0;
}

static boolean u_vbuf_mapping_vertex_buffer_blocks(const struct u_vbuf *mgr)
{
   /* Return true if there are hw buffers which don't need to be translated.
    *
    * We could query whether each buffer is busy, but that would
    * be way more costly than this. */
   return (mgr->ve->used_vb_mask &
           (~mgr->user_vb_mask &
            ~mgr->incompatible_vb_mask &
            mgr->ve->compatible_vb_mask_all &
            mgr->ve->noninstance_vb_mask_any &
            mgr->nonzero_stride_vb_mask)) != 0;
}

static void u_vbuf_get_minmax_index(struct pipe_context *pipe,
                                    const struct pipe_draw_info *info,
                                    int *out_min_index, int *out_max_index)
{
   struct pipe_transfer *transfer = NULL;
   const void *indices;
   unsigned i;

   if (info->has_user_indices) {
      indices = (uint8_t*)info->index.user +
                info->start * info->index_size;
   } else {
      indices = pipe_buffer_map_range(pipe, info->index.resource,
                                      info->start * info->index_size,
                                      info->count * info->index_size,
                                      PIPE_TRANSFER_READ, &transfer);
   }

   switch (info->index_size) {
   case 4: {
      const unsigned *ui_indices = (const unsigned*)indices;
      unsigned max_ui = 0;
      unsigned min_ui = ~0U;
      if (info->primitive_restart) {
         for (i = 0; i < info->count; i++) {
            if (ui_indices[i] != info->restart_index) {
               if (ui_indices[i] > max_ui) max_ui = ui_indices[i];
               if (ui_indices[i] < min_ui) min_ui = ui_indices[i];
            }
         }
      }
      else {
         for (i = 0; i < info->count; i++) {
            if (ui_indices[i] > max_ui) max_ui = ui_indices[i];
            if (ui_indices[i] < min_ui) min_ui = ui_indices[i];
         }
      }
      *out_min_index = min_ui;
      *out_max_index = max_ui;
      break;
   }
   case 2: {
      const unsigned short *us_indices = (const unsigned short*)indices;
      unsigned max_us = 0;
      unsigned min_us = ~0U;
      if (info->primitive_restart) {
         for (i = 0; i < info->count; i++) {
            if (us_indices[i] != info->restart_index) {
               if (us_indices[i] > max_us) max_us = us_indices[i];
               if (us_indices[i] < min_us) min_us = us_indices[i];
            }
         }
      }
      else {
         for (i = 0; i < info->count; i++) {
            if (us_indices[i] > max_us) max_us = us_indices[i];
            if (us_indices[i] < min_us) min_us = us_indices[i];
         }
      }
      *out_min_index = min_us;
      *out_max_index = max_us;
      break;
   }
   case 1: {
      const unsigned char *ub_indices = (const unsigned char*)indices;
      unsigned max_ub = 0;
      unsigned min_ub = ~0U;
      if (info->primitive_restart) {
         for (i = 0; i < info->count; i++) {
            if (ub_indices[i] != info->restart_index) {
               if (ub_indices[i] > max_ub) max_ub = ub_indices[i];
               if (ub_indices[i] < min_ub) min_ub = ub_indices[i];
            }
         }
      }
      else {
         for (i = 0; i < info->count; i++) {
            if (ub_indices[i] > max_ub) max_ub = ub_indices[i];
            if (ub_indices[i] < min_ub) min_ub = ub_indices[i];
         }
      }
      *out_min_index = min_ub;
      *out_max_index = max_ub;
      break;
   }
   default:
      assert(0);
      *out_min_index = 0;
      *out_max_index = 0;
   }

   if (transfer) {
      pipe_buffer_unmap(pipe, transfer);
   }
}

static void u_vbuf_set_driver_vertex_buffers(struct u_vbuf *mgr)
{
   struct pipe_context *pipe = mgr->pipe;
   unsigned start_slot, count;

   start_slot = ffs(mgr->dirty_real_vb_mask) - 1;
   count = util_last_bit(mgr->dirty_real_vb_mask >> start_slot);

   pipe->set_vertex_buffers(pipe, start_slot, count,
                            mgr->real_vertex_buffer + start_slot);
   mgr->dirty_real_vb_mask = 0;
}

void u_vbuf_draw_vbo(struct u_vbuf *mgr, const struct pipe_draw_info *info)
{
   struct pipe_context *pipe = mgr->pipe;
   int start_vertex, min_index;
   unsigned num_vertices;
   boolean unroll_indices = FALSE;
   const uint32_t used_vb_mask = mgr->ve->used_vb_mask;
   uint32_t user_vb_mask = mgr->user_vb_mask & used_vb_mask;
   const uint32_t incompatible_vb_mask =
      mgr->incompatible_vb_mask & used_vb_mask;
   struct pipe_draw_info new_info;

   /* Normal draw. No fallback and no user buffers. */
   if (!incompatible_vb_mask &&
       !mgr->ve->incompatible_elem_mask &&
       !user_vb_mask) {

      /* Set vertex buffers if needed. */
      if (mgr->dirty_real_vb_mask & used_vb_mask) {
         u_vbuf_set_driver_vertex_buffers(mgr);
      }

      pipe->draw_vbo(pipe, info);
      return;
   }

   new_info = *info;

   /* Fallback. We need to know all the parameters. */
   if (new_info.indirect) {
      struct pipe_transfer *transfer = NULL;
      int *data;

      if (new_info.index_size) {
         data = pipe_buffer_map_range(pipe, new_info.indirect->buffer,
                                      new_info.indirect->offset, 20,
                                      PIPE_TRANSFER_READ, &transfer);
         new_info.index_bias = data[3];
         new_info.start_instance = data[4];
      }
      else {
         data = pipe_buffer_map_range(pipe, new_info.indirect->buffer,
                                      new_info.indirect->offset, 16,
                                      PIPE_TRANSFER_READ, &transfer);
         new_info.start_instance = data[3];
      }

      new_info.count = data[0];
      new_info.instance_count = data[1];
      new_info.start = data[2];
      pipe_buffer_unmap(pipe, transfer);
      new_info.indirect = NULL;
   }

   if (new_info.index_size) {
      /* See if anything needs to be done for per-vertex attribs. */
      if (u_vbuf_need_minmax_index(mgr)) {
         int max_index;

         if (new_info.max_index != ~0u) {
            min_index = new_info.min_index;
            max_index = new_info.max_index;
         } else {
            u_vbuf_get_minmax_index(mgr->pipe, &new_info,
                                    &min_index, &max_index);
         }

         assert(min_index <= max_index);

         start_vertex = min_index + new_info.index_bias;
         num_vertices = max_index + 1 - min_index;

         /* Primitive restart doesn't work when unrolling indices.
          * We would have to break this drawing operation into several ones. */
         /* Use some heuristic to see if unrolling indices improves
          * performance. */
         if (!new_info.primitive_restart &&
             num_vertices > new_info.count*2 &&
             num_vertices - new_info.count > 32 &&
             !u_vbuf_mapping_vertex_buffer_blocks(mgr)) {
            unroll_indices = TRUE;
            user_vb_mask &= ~(mgr->nonzero_stride_vb_mask &
                              mgr->ve->noninstance_vb_mask_any);
         }
      } else {
         /* Nothing to do for per-vertex attribs. */
         start_vertex = 0;
         num_vertices = 0;
         min_index = 0;
      }
   } else {
      start_vertex = new_info.start;
      num_vertices = new_info.count;
      min_index = 0;
   }

   /* Translate vertices with non-native layouts or formats. */
   if (unroll_indices ||
       incompatible_vb_mask ||
       mgr->ve->incompatible_elem_mask) {
      if (!u_vbuf_translate_begin(mgr, &new_info, start_vertex, num_vertices,
                                  min_index, unroll_indices)) {
         debug_warn_once("u_vbuf_translate_begin() failed");
         return;
      }

      if (unroll_indices) {
         new_info.index_size = 0;
         new_info.index_bias = 0;
         new_info.min_index = 0;
         new_info.max_index = new_info.count - 1;
         new_info.start = 0;
      }

      user_vb_mask &= ~(incompatible_vb_mask |
                        mgr->ve->incompatible_vb_mask_all);
   }

   /* Upload user buffers. */
   if (user_vb_mask) {
      if (u_vbuf_upload_buffers(mgr, start_vertex, num_vertices,
                                new_info.start_instance,
                                new_info.instance_count) != PIPE_OK) {
         debug_warn_once("u_vbuf_upload_buffers() failed");
         return;
      }

      mgr->dirty_real_vb_mask |= user_vb_mask;
   }

   /*
   if (unroll_indices) {
      printf("unrolling indices: start_vertex = %i, num_vertices = %i\n",
             start_vertex, num_vertices);
      util_dump_draw_info(stdout, info);
      printf("\n");
   }

   unsigned i;
   for (i = 0; i < mgr->nr_vertex_buffers; i++) {
      printf("input %i: ", i);
      util_dump_vertex_buffer(stdout, mgr->vertex_buffer+i);
      printf("\n");
   }
   for (i = 0; i < mgr->nr_real_vertex_buffers; i++) {
      printf("real %i: ", i);
      util_dump_vertex_buffer(stdout, mgr->real_vertex_buffer+i);
      printf("\n");
   }
   */

   u_upload_unmap(pipe->stream_uploader);
   u_vbuf_set_driver_vertex_buffers(mgr);

   pipe->draw_vbo(pipe, &new_info);

   if (mgr->using_translate) {
      u_vbuf_translate_end(mgr);
   }
}

void u_vbuf_save_vertex_elements(struct u_vbuf *mgr)
{
   assert(!mgr->ve_saved);
   mgr->ve_saved = mgr->ve;
}

void u_vbuf_restore_vertex_elements(struct u_vbuf *mgr)
{
   if (mgr->ve != mgr->ve_saved) {
      struct pipe_context *pipe = mgr->pipe;

      mgr->ve = mgr->ve_saved;
      pipe->bind_vertex_elements_state(pipe,
                                       mgr->ve ? mgr->ve->driver_cso : NULL);
   }
   mgr->ve_saved = NULL;
}

void u_vbuf_save_vertex_buffer0(struct u_vbuf *mgr)
{
   pipe_vertex_buffer_reference(&mgr->vertex_buffer0_saved,
                                &mgr->vertex_buffer[0]);
}

void u_vbuf_restore_vertex_buffer0(struct u_vbuf *mgr)
{
   u_vbuf_set_vertex_buffers(mgr, 0, 1, &mgr->vertex_buffer0_saved);
   pipe_vertex_buffer_unreference(&mgr->vertex_buffer0_saved);
}
