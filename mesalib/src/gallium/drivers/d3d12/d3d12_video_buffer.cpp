/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "d3d12_video_buffer.h"
#include "d3d12_video_enc.h"
#include "d3d12_resource.h"
#include "d3d12_video_dec.h"
#include "d3d12_residency.h"
#include "d3d12_context.h"

#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "vl/vl_video_buffer.h"
#include "util/u_sampler.h"
#include "frontend/winsys_handle.h"
#include "d3d12_format.h"
#include "d3d12_screen.h"

static struct pipe_video_buffer *
d3d12_video_buffer_create_impl(struct pipe_context *pipe,
                              const struct pipe_video_buffer *tmpl,
                              struct pipe_resource* resource_creation_info,
                              d3d12_video_buffer_creation_mode resource_creation_mode,
                              struct winsys_handle *handle,
                              unsigned usage)
{
   assert(pipe);
   assert(tmpl);

   ///
   /// Initialize d3d12_video_buffer
   ///

   // Not using new doesn't call ctor and the initializations in the class declaration are lost
   struct d3d12_video_buffer *pD3D12VideoBuffer = new d3d12_video_buffer;

   // Fill base template
   pD3D12VideoBuffer->base               = *tmpl;
   pD3D12VideoBuffer->base.buffer_format = tmpl->buffer_format;
   pD3D12VideoBuffer->base.context       = pipe;
   pD3D12VideoBuffer->base.width         = tmpl->width;
   pD3D12VideoBuffer->base.height        = tmpl->height;
   pD3D12VideoBuffer->base.interlaced    = tmpl->interlaced;
   pD3D12VideoBuffer->base.contiguous_planes = true;
   pD3D12VideoBuffer->base.associated_data = nullptr;
   pD3D12VideoBuffer->idx_texarray_slots = 0;
   pD3D12VideoBuffer->m_spVideoTexArrayDPBPoolInUse.reset();

   // Used to signal the rest of the d3d12 driver this is a video (dpb or not) texture
   pD3D12VideoBuffer->base.bind |=  PIPE_BIND_CUSTOM;
#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
   struct d3d12_screen *dscreen = (struct d3d12_screen*) pipe->screen;
   if ((dscreen->max_feature_level >= D3D_FEATURE_LEVEL_11_0) &&
      ((pD3D12VideoBuffer->base.bind & PIPE_BIND_VIDEO_DECODE_DPB) == 0) &&
      ((pD3D12VideoBuffer->base.bind & PIPE_BIND_VIDEO_ENCODE_DPB) == 0))
      pD3D12VideoBuffer->base.bind |= (PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW);
#endif // HAVE_GALLIUM_D3D12_GRAPHICS

   // Fill vtable
   pD3D12VideoBuffer->base.destroy                     = d3d12_video_buffer_destroy;
   pD3D12VideoBuffer->base.get_resources               = d3d12_video_buffer_resources;
   pD3D12VideoBuffer->base.get_sampler_view_planes     = d3d12_video_buffer_get_sampler_view_planes;
   pD3D12VideoBuffer->base.get_sampler_view_components = d3d12_video_buffer_get_sampler_view_components;
   pD3D12VideoBuffer->base.get_surfaces                = d3d12_video_buffer_get_surfaces;
   pD3D12VideoBuffer->base.destroy_associated_data     = d3d12_video_buffer_destroy_associated_data;

   ///
   /// Create, open or place underlying pipe_resource allocation
   ///

   // This calls d3d12_create_resource as the function ptr is set in d3d12_screen.resource_create
   if (resource_creation_mode == d3d12_video_buffer_creation_mode::open_shared_resource)
   {
      assert(handle);
      resource_creation_info->target     = PIPE_TEXTURE_2D;
      resource_creation_info->bind       = pD3D12VideoBuffer->base.bind;
      resource_creation_info->format     = pD3D12VideoBuffer->base.buffer_format;
      resource_creation_info->flags      = 0;
      resource_creation_info->depth0     = 1;
      if (resource_creation_info->array_size == 0) // If caller did not pass it, set as 1 default
         resource_creation_info->array_size = 1;

      // YUV 4:2:0 formats in D3D12 always require multiple of 2 dimensions
      // We must respect the input dimensions of the imported resource handle (e.g no extra aligning)
      resource_creation_info->width0     = align(pD3D12VideoBuffer->base.width, 2);
      resource_creation_info->height0    = static_cast<uint16_t>(align(pD3D12VideoBuffer->base.height, 2));

      // WINSYS_HANDLE_TYPE_D3D12_RES implies taking ownership of the reference
      if(handle->type == WINSYS_HANDLE_TYPE_D3D12_RES)
         ((IUnknown *)handle->com_obj)->AddRef();
      pD3D12VideoBuffer->texture = (struct d3d12_resource *) pipe->screen->resource_from_handle(pipe->screen, resource_creation_info, handle, usage);
   }
   else if(resource_creation_mode == d3d12_video_buffer_creation_mode::create_resource)
   {
      resource_creation_info->target     = PIPE_TEXTURE_2D;
      resource_creation_info->bind       = pD3D12VideoBuffer->base.bind;
      resource_creation_info->format     = pD3D12VideoBuffer->base.buffer_format;
      resource_creation_info->flags      = 0;
      resource_creation_info->depth0     = 1;
      if (resource_creation_info->array_size == 0) // If caller did not pass it, set as 1 default
         resource_creation_info->array_size = 1;

      // When creating (e.g not importing) resources we allocate
      // with a higher alignment to maximize HW compatibility
      resource_creation_info->width0     = align(pD3D12VideoBuffer->base.width, 2);
      resource_creation_info->height0    = static_cast<uint16_t>(align(pD3D12VideoBuffer->base.height, 16));

      pD3D12VideoBuffer->texture = (struct d3d12_resource *) pipe->screen->resource_create(pipe->screen, resource_creation_info);
   }
   else if(resource_creation_mode == d3d12_video_buffer_creation_mode::place_on_resource)
   {
      pD3D12VideoBuffer->texture = (struct d3d12_resource*) resource_creation_info; // Set directly the resource as underlying texture
   }

   if (pD3D12VideoBuffer->texture == nullptr) {
      debug_printf("[d3d12_video_buffer] d3d12_video_buffer_create_impl - failed to set a valid pD3D12VideoBuffer->texture.");
      goto failed;
   }

   d3d12_promote_to_permanent_residency((struct d3d12_screen*) pipe->screen, pD3D12VideoBuffer->texture);

   pD3D12VideoBuffer->num_planes = util_format_get_num_planes(pD3D12VideoBuffer->texture->overall_format);
   return &pD3D12VideoBuffer->base;

failed:
   d3d12_video_buffer_destroy((struct pipe_video_buffer *) pD3D12VideoBuffer);

   return nullptr;
}

/**
 * creates a video buffer from a handle
 */
struct pipe_video_buffer *
d3d12_video_buffer_from_handle(struct pipe_context *pipe,
                               const struct pipe_video_buffer *tmpl,
                               struct winsys_handle *handle,
                               unsigned usage)
{
   struct pipe_video_buffer updated_template = {};
   if ((handle->format == PIPE_FORMAT_NONE) || (tmpl == nullptr) || (tmpl->buffer_format == PIPE_FORMAT_NONE) ||
       (tmpl->width == 0) || (tmpl->height == 0)) {
      ID3D12Resource *d3d12_res = nullptr;
      if (handle->type == WINSYS_HANDLE_TYPE_D3D12_RES) {
         d3d12_res = (ID3D12Resource *) handle->com_obj;
      } else if (handle->type == WINSYS_HANDLE_TYPE_FD) {
#ifdef _WIN32
         HANDLE d3d_handle = handle->handle;
#else
         HANDLE d3d_handle = (HANDLE) (intptr_t) handle->handle;
#endif
         if (FAILED(d3d12_screen(pipe->screen)->dev->OpenSharedHandle(d3d_handle, IID_PPV_ARGS(&d3d12_res)))) {
            return NULL;
         }
      }
      D3D12_RESOURCE_DESC res_desc = GetDesc(d3d12_res);
      updated_template.width = static_cast<unsigned int>(res_desc.Width);
      updated_template.height = res_desc.Height;
      updated_template.buffer_format = d3d12_get_pipe_format(res_desc.Format);
      handle->format = updated_template.buffer_format;

      // if passed an external com_ptr (e.g WINSYS_HANDLE_TYPE_D3D12_RES) do not release it
      if (handle->type == WINSYS_HANDLE_TYPE_FD)
         d3d12_res->Release();
   } else {
      updated_template = *tmpl;
   }

   pipe_resource resource_creation_info = {};
   return d3d12_video_buffer_create_impl(pipe, &updated_template, &resource_creation_info, d3d12_video_buffer_creation_mode::open_shared_resource, handle, usage);
}

/**
 * creates a video buffer
 */
struct pipe_video_buffer *
d3d12_video_buffer_create(struct pipe_context *pipe, const struct pipe_video_buffer *tmpl)
{
   pipe_resource resource_creation_info = {};
   return d3d12_video_buffer_create_impl(pipe, tmpl, &resource_creation_info, d3d12_video_buffer_creation_mode::create_resource, NULL, 0);
}

/**
 * destroy this video buffer
 */
void
d3d12_video_buffer_destroy(struct pipe_video_buffer *buffer)
{
   struct d3d12_video_buffer *pD3D12VideoBuffer = (struct d3d12_video_buffer *) buffer;

   // For texture arrays, only delete the underlying resource allocation when
   // there are no more in use slots into it
   bool bKeepUnderlyingAlloc = false;
   if (pD3D12VideoBuffer->texture->base.b.array_size > 1)
   {
      // Mark slot used by the video buffer being destroyed as unused
      (*pD3D12VideoBuffer->m_spVideoTexArrayDPBPoolInUse) &= ~(1 << pD3D12VideoBuffer->idx_texarray_slots); // mark bit idx_texarray_slots as zero
      // Keep underlying pD3D12VideoBuffer->texture alloc if any other slots are in use.
      bKeepUnderlyingAlloc = (*pD3D12VideoBuffer->m_spVideoTexArrayDPBPoolInUse != 0); // check for any non-zero bit
   }

   // Destroy pD3D12VideoBuffer->texture underlying aloc
   if (pD3D12VideoBuffer->texture && !bKeepUnderlyingAlloc) {
      pipe_resource *pBaseResource = &pD3D12VideoBuffer->texture->base.b;
      pipe_resource_reference(&pBaseResource, NULL);
   }

   // Destroy associated data (if any)
   if (pD3D12VideoBuffer->base.associated_data != nullptr) {
      d3d12_video_buffer_destroy_associated_data(pD3D12VideoBuffer->base.associated_data);
      // Set to nullptr after cleanup, no dangling pointers
      pD3D12VideoBuffer->base.associated_data = nullptr;
   }

   for (uint i = 0; i < pD3D12VideoBuffer->surfaces.size(); ++i) {
      if (pD3D12VideoBuffer->surfaces[i] != NULL) {
         pipe_surface_reference(&pD3D12VideoBuffer->surfaces[i], NULL);
      }
   }

   for (uint i = 0; i < pD3D12VideoBuffer->sampler_view_planes.size(); ++i) {
      if (pD3D12VideoBuffer->sampler_view_planes[i] != NULL) {
         pipe_sampler_view_reference(&pD3D12VideoBuffer->sampler_view_planes[i], NULL);
      }
   }

   for (uint i = 0; i < pD3D12VideoBuffer->sampler_view_components.size(); ++i) {
      if (pD3D12VideoBuffer->sampler_view_components[i] != NULL) {
         pipe_sampler_view_reference(&pD3D12VideoBuffer->sampler_view_components[i], NULL);
      }
   }

   delete pD3D12VideoBuffer;
}

/*
 * destroy the associated data
 */
void
d3d12_video_buffer_destroy_associated_data(void *associated_data)
{ }

/**
 * get an individual surfaces for each plane
 */
struct pipe_surface **
d3d12_video_buffer_get_surfaces(struct pipe_video_buffer *buffer)
{
   assert(buffer);
   struct d3d12_video_buffer *pD3D12VideoBuffer = (struct d3d12_video_buffer *) buffer;
   struct pipe_context *      pipe              = pD3D12VideoBuffer->base.context;
   struct pipe_surface        surface_template  = {};

   // DPB buffers don't support views
   if ((pD3D12VideoBuffer->base.bind & PIPE_BIND_VIDEO_DECODE_DPB) ||
       (pD3D12VideoBuffer->base.bind & PIPE_BIND_VIDEO_ENCODE_DPB))
      return nullptr;

   if (!pipe->create_surface)
      return nullptr;

   // Some video frameworks iterate over [0..VL_MAX_SURFACES) and ignore the nullptr entries
   // So we have to null initialize the other surfaces not used from [num_planes..VL_MAX_SURFACES)
   // Like in src/gallium/frontends/va/surface.c
   pD3D12VideoBuffer->surfaces.resize(VL_MAX_SURFACES, nullptr);

   // pCurPlaneResource refers to the planar resource, not the overall resource.
   // in d3d12_resource this is handled by having a linked list of planes with
   // d3dRes->base.next ptr to next plane resource
   // starting with the plane 0 being the overall resource
   struct pipe_resource *pCurPlaneResource = &pD3D12VideoBuffer->texture->base.b;

   for (uint PlaneSlice = 0; PlaneSlice < pD3D12VideoBuffer->num_planes; ++PlaneSlice) {
      if (!pD3D12VideoBuffer->surfaces[PlaneSlice]) {
         memset(&surface_template, 0, sizeof(surface_template));
         surface_template.format =
            util_format_get_plane_format(pD3D12VideoBuffer->texture->overall_format, PlaneSlice);

         pD3D12VideoBuffer->surfaces[PlaneSlice] =
            pipe->create_surface(pipe, pCurPlaneResource, &surface_template);

         if (!pD3D12VideoBuffer->surfaces[PlaneSlice]) {
            goto error;
         }
      }
      pCurPlaneResource = pCurPlaneResource->next;
   }

   return pD3D12VideoBuffer->surfaces.data();

error:
   for (uint PlaneSlice = 0; PlaneSlice < pD3D12VideoBuffer->num_planes; ++PlaneSlice) {
      pipe_surface_reference(&pD3D12VideoBuffer->surfaces[PlaneSlice], NULL);
   }

   return nullptr;
}

/**
 * get an individual resource for each plane,
 * only returns existing resources by reference
 */
void
d3d12_video_buffer_resources(struct pipe_video_buffer *buffer,
                             struct pipe_resource **resources)
{
   struct d3d12_video_buffer *pD3D12VideoBuffer = (struct d3d12_video_buffer *) buffer;
   assert(pD3D12VideoBuffer);

   // pCurPlaneResource refers to the planar resource, not the overall resource.
   // in d3d12_resource this is handled by having a linked list of planes with
   // d3dRes->base.next ptr to next plane resource
   // starting with the plane 0 being the overall resource
   struct pipe_resource *pCurPlaneResource = &pD3D12VideoBuffer->texture->base.b;

   for (uint i = 0; i < pD3D12VideoBuffer->num_planes; ++i) {
      assert(pCurPlaneResource); // the d3d12_resource has a linked list with the exact name of number of elements
                                 // as planes

      resources[i] = pCurPlaneResource;
      pCurPlaneResource = pCurPlaneResource->next;
   }
}

/**
 * get an individual sampler view for each plane
 */
struct pipe_sampler_view **
d3d12_video_buffer_get_sampler_view_planes(struct pipe_video_buffer *buffer)
{
   assert(buffer);
   struct d3d12_video_buffer *pD3D12VideoBuffer = (struct d3d12_video_buffer *) buffer;
   struct pipe_context *      pipe              = pD3D12VideoBuffer->base.context;
   struct pipe_sampler_view   samplerViewTemplate;

   // DPB buffers don't support views
   if ((pD3D12VideoBuffer->base.bind & PIPE_BIND_VIDEO_DECODE_DPB) ||
       (pD3D12VideoBuffer->base.bind & PIPE_BIND_VIDEO_ENCODE_DPB))
      return nullptr;

   // Some video frameworks iterate over [0..VL_MAX_SURFACES) and ignore the nullptr entries
   // So we have to null initialize the other surfaces not used from [num_planes..VL_MAX_SURFACES)
   // Like in src/gallium/frontends/vdpau/surface.c
   pD3D12VideoBuffer->sampler_view_planes.resize(VL_MAX_SURFACES, nullptr);

   // pCurPlaneResource refers to the planar resource, not the overall resource.
   // in d3d12_resource this is handled by having a linked list of planes with
   // d3dRes->base.next ptr to next plane resource
   // starting with the plane 0 being the overall resource
   struct pipe_resource *pCurPlaneResource = &pD3D12VideoBuffer->texture->base.b;

   for (uint i = 0; i < pD3D12VideoBuffer->num_planes; ++i) {
      if (!pD3D12VideoBuffer->sampler_view_planes[i]) {
         assert(pCurPlaneResource);   // the d3d12_resource has a linked list with the exact name of number of elements
                                      // as planes

         memset(&samplerViewTemplate, 0, sizeof(samplerViewTemplate));
         u_sampler_view_default_template(&samplerViewTemplate, pCurPlaneResource, pCurPlaneResource->format);

         pD3D12VideoBuffer->sampler_view_planes[i] =
            pipe->create_sampler_view(pipe, pCurPlaneResource, &samplerViewTemplate);

         if (!pD3D12VideoBuffer->sampler_view_planes[i]) {
            goto error;
         }
      }

      pCurPlaneResource = pCurPlaneResource->next;
   }

   return pD3D12VideoBuffer->sampler_view_planes.data();

error:
   for (uint i = 0; i < pD3D12VideoBuffer->num_planes; ++i) {
      pipe_sampler_view_reference(&pD3D12VideoBuffer->sampler_view_planes[i], NULL);
   }

   return nullptr;
}

/**
 * get an individual sampler view for each component
 */
struct pipe_sampler_view **
d3d12_video_buffer_get_sampler_view_components(struct pipe_video_buffer *buffer)
{
   assert(buffer);
   struct d3d12_video_buffer *pD3D12VideoBuffer = (struct d3d12_video_buffer *) buffer;
   struct pipe_context *      pipe              = pD3D12VideoBuffer->base.context;
   struct pipe_sampler_view   samplerViewTemplate;

   // DPB buffers don't support views
   if ((pD3D12VideoBuffer->base.bind & PIPE_BIND_VIDEO_DECODE_DPB) ||
       (pD3D12VideoBuffer->base.bind & PIPE_BIND_VIDEO_ENCODE_DPB))
      return nullptr;

   // pCurPlaneResource refers to the planar resource, not the overall resource.
   // in d3d12_resource this is handled by having a linked list of planes with
   // d3dRes->base.next ptr to next plane resource
   // starting with the plane 0 being the overall resource
   struct pipe_resource *pCurPlaneResource = &pD3D12VideoBuffer->texture->base.b;

   const uint32_t MAX_NUM_COMPONENTS = 4; // ie. RGBA formats
   // At the end of the loop, "component" will have the total number of items valid in sampler_view_components
   // since component can end up being <= MAX_NUM_COMPONENTS, we assume MAX_NUM_COMPONENTS first and then resize/adjust to
   // fit the container size pD3D12VideoBuffer->sampler_view_components to the actual components number
   pD3D12VideoBuffer->sampler_view_components.resize(MAX_NUM_COMPONENTS, nullptr);
   uint component = 0;

   for (uint i = 0; i < pD3D12VideoBuffer->num_planes; ++i) {
      // For example num_components would be 1 for the Y plane (R8 in NV12), 2 for the UV plane (R8G8 in NV12)
      unsigned num_components = util_format_get_nr_components(pCurPlaneResource->format);

      for (uint j = 0; j < num_components; ++j, ++component) {

         if (!pD3D12VideoBuffer->sampler_view_components[component]) {
            memset(&samplerViewTemplate, 0, sizeof(samplerViewTemplate));
            u_sampler_view_default_template(&samplerViewTemplate, pCurPlaneResource, pCurPlaneResource->format);
            samplerViewTemplate.swizzle_r = samplerViewTemplate.swizzle_g = samplerViewTemplate.swizzle_b =
               PIPE_SWIZZLE_X + j;
            samplerViewTemplate.swizzle_a = PIPE_SWIZZLE_1;

            pD3D12VideoBuffer->sampler_view_components[component] =
               pipe->create_sampler_view(pipe, pCurPlaneResource, &samplerViewTemplate);
            if (!pD3D12VideoBuffer->sampler_view_components[component]) {
               goto error;
            }
         }
      }

      pCurPlaneResource = pCurPlaneResource->next;
   }

   // Adjust size to fit component <= VL_NUM_COMPONENTS
   pD3D12VideoBuffer->sampler_view_components.resize(component);

   return pD3D12VideoBuffer->sampler_view_components.data();

error:
   for (uint i = 0; i < pD3D12VideoBuffer->num_planes; ++i) {
      pipe_sampler_view_reference(&pD3D12VideoBuffer->sampler_view_components[i], NULL);
   }

   return nullptr;
}

struct pipe_video_buffer*
d3d12_video_create_dpb_buffer(struct pipe_video_codec *codec,
                              struct pipe_picture_desc *picture,
                              const struct pipe_video_buffer *templat)
{
   pipe_video_buffer tmpl = *templat;

   //
   // Check if the IHV requires texture array or opaque reference only allocations
   //
   bool bTextureArray = false;
   if (codec->entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM) {
      struct d3d12_video_decoder *pD3D12Dec = (struct d3d12_video_decoder *) codec;

      if (pD3D12Dec->m_ConfigDecoderSpecificFlags &
         d3d12_video_decode_config_specific_flag_reference_only_textures_required)
         tmpl.bind |= PIPE_BIND_VIDEO_DECODE_DPB;

      bTextureArray = ((pD3D12Dec->m_ConfigDecoderSpecificFlags &
         d3d12_video_decode_config_specific_flag_array_of_textures) == 0);

   } else if (codec->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;

      if ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
           D3D12_VIDEO_ENCODER_SUPPORT_FLAG_READABLE_RECONSTRUCTED_PICTURE_LAYOUT_AVAILABLE) == 0)
         tmpl.bind |= PIPE_BIND_VIDEO_ENCODE_DPB;

      bTextureArray = (pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
         D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RECONSTRUCTED_FRAMES_REQUIRE_TEXTURE_ARRAYS) != 0;
   }

   if (bTextureArray)
      return d3d12_video_create_dpb_buffer_texarray(codec, picture, &tmpl);
   else
      return d3d12_video_create_dpb_buffer_aot(codec, picture, &tmpl);
}

struct pipe_video_buffer*
d3d12_video_create_dpb_buffer_aot(struct pipe_video_codec *codec,
                                  struct pipe_picture_desc *picture,
                                  const struct pipe_video_buffer *templat)
{
   // For AOT, just return a new buffer with a new underlying pipe_resource
   pipe_resource resource_creation_info = {};
   return d3d12_video_buffer_create_impl(codec->context, templat, &resource_creation_info, d3d12_video_buffer_creation_mode::create_resource, NULL, 0);
}

struct pipe_video_buffer*
d3d12_video_create_dpb_buffer_texarray(struct pipe_video_codec *codec,
                                       struct pipe_picture_desc *picture,
                                       const struct pipe_video_buffer *templat)
{
   d3d12_video_buffer* buf = NULL;
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;

   // For texture array, keep a texture array pool of d3d12_video_encoder_get_current_max_dpb_capacity
   // and keep track of used/unused subresource indices to return from the pool
   if (!pD3D12Enc->m_pVideoTexArrayDPBPool)
   {
      pipe_resource resource_creation_info = {};
      resource_creation_info.array_size = static_cast<uint16_t>(d3d12_video_encoder_get_current_max_dpb_capacity(pD3D12Enc) + D3D12_VIDEO_ENC_ASYNC_DEPTH + 1u);
      assert(resource_creation_info.array_size <= 32); // uint32_t used as a usage bitmap into m_pVideoTexArrayDPBPool
      buf = (d3d12_video_buffer*) d3d12_video_buffer_create_impl(codec->context, templat, &resource_creation_info, d3d12_video_buffer_creation_mode::create_resource, NULL, 0);
      pD3D12Enc->m_pVideoTexArrayDPBPool = &buf->texture->base.b;
      pD3D12Enc->m_spVideoTexArrayDPBPoolInUse = std::make_shared<uint32_t>();
   }
   else
   {
      buf = (d3d12_video_buffer*) d3d12_video_buffer_create_impl(codec->context, templat, pD3D12Enc->m_pVideoTexArrayDPBPool, d3d12_video_buffer_creation_mode::place_on_resource, NULL, 0);
   }

   // Set and increase refcount in buf object for usage in d3d12_video_buffer_destroy()
   buf->m_spVideoTexArrayDPBPoolInUse = pD3D12Enc->m_spVideoTexArrayDPBPoolInUse;

   ASSERTED bool bFoundEmptySlot = false;
   for (unsigned i = 0; i < pD3D12Enc->m_pVideoTexArrayDPBPool->array_size; i++)
   {
      if (((*pD3D12Enc->m_spVideoTexArrayDPBPoolInUse) & (1 << i)) == 0)
      {
         buf->idx_texarray_slots = i;
         (*pD3D12Enc->m_spVideoTexArrayDPBPoolInUse) |= (1 << buf->idx_texarray_slots); // Mark i-th bit as used
         bFoundEmptySlot = true;
         break;
      }
   }

   assert(bFoundEmptySlot); // Possibly ran out of slots because the frontend is using more slots than we allocated in array_size when initializing m_pVideoTexArrayDPBPool
   return &buf->base;
}
