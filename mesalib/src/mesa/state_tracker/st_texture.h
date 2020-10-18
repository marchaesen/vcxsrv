/**************************************************************************
 *
 * Copyright 2007 VMware, Inc.
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

#ifndef ST_TEXTURE_H
#define ST_TEXTURE_H


#include "pipe/p_context.h"
#include "util/u_sampler.h"
#include "util/simple_mtx.h"

#include "main/mtypes.h"


struct pipe_resource;


struct st_texture_image_transfer
{
   struct pipe_transfer *transfer;

   /* For compressed texture fallback. */
   GLubyte *temp_data; /**< Temporary compressed texture storage. */
   unsigned temp_stride; /**< Stride of the compressed texture storage. */
   GLubyte *map; /**< Saved map pointer of the uncompressed transfer. */
};


/**
 * Container for one context's validated sampler view.
 */
struct st_sampler_view
{
   struct pipe_sampler_view *view;

   /** The context which created this view */
   struct st_context *st;

   /** The glsl version of the shader seen during validation */
   bool glsl130_or_later;
   /** Derived from the sampler's sRGBDecode state during validation */
   bool srgb_skip_decode;
};


/**
 * Container for per-context sampler views of a texture.
 */
struct st_sampler_views
{
   struct st_sampler_views *next;
   uint32_t max;
   uint32_t count;
   struct st_sampler_view views[0];
};

struct st_compressed_data
{
   struct pipe_reference reference;
   GLubyte *ptr;
};


/**
 * Subclass of gl_texure_image.
 */
struct st_texture_image
{
   struct gl_texture_image base;

   /* If stImage->pt != NULL, image data is stored here.
    * Else there is no image data.
    */
   struct pipe_resource *pt;

   /* List of transfers, allocated on demand.
    * transfer[layer] is a mapping for that layer.
    */
   struct st_texture_image_transfer *transfer;
   unsigned num_transfers;

   /* For compressed images unsupported by the driver. Keep track of
    * the original data. This is necessary for mapping/unmapping,
    * as well as image copies.
    */
   struct st_compressed_data* compressed_data;
};


/**
 * Subclass of gl_texure_object.
 */
struct st_texture_object
{
   struct gl_texture_object base;       /* The "parent" object */

   /* The texture must include at levels [0..lastLevel] once validated:
    */
   GLuint lastLevel;

   unsigned int validated_first_level;
   unsigned int validated_last_level;

   /* On validation any active images held in main memory or in other
    * textures will be copied to this texture and the old storage freed.
    */
   struct pipe_resource *pt;

   /* Protect modifications of the sampler_views array */
   simple_mtx_t validate_mutex;

   /* Container of sampler views (one per context) attached to this texture
    * object. Created lazily on first binding in context.
    *
    * Purely read-only accesses to the current context's own sampler view
    * require no locking. Another thread may simultaneously replace the
    * container object in order to grow the array, but the old container will
    * be kept alive.
    *
    * Writing to the container (even for modifying the current context's own
    * sampler view) always requires taking the validate_mutex to protect against
    * concurrent container switches.
    *
    * NULL'ing another context's sampler view is allowed only while
    * implementing an API call that modifies the texture: an application which
    * calls those while simultaneously reading the texture in another context
    * invokes undefined behavior. (TODO: a dubious violation of this rule is
    * st_finalize_texture, which is a lazy operation that corresponds to a
    * texture modification.)
    */
   struct st_sampler_views *sampler_views;

   /* Old sampler views container objects that have not been freed yet because
    * other threads/contexts may still be reading from them.
    */
   struct st_sampler_views *sampler_views_old;

   /* True if this texture comes from the window system. Such a texture
    * cannot be reallocated and the format can only be changed with a sampler
    * view or a surface.
    */
   GLboolean surface_based;

   /* If surface_based is true, this format should be used for all sampler
    * views and surfaces instead of pt->format.
    */
   enum pipe_format surface_format;

   /* When non-negative, samplers should use this level instead of the level
    * range specified by the GL state.
    *
    * This is used for EGL images, which may correspond to a single level out
    * of an imported pipe_resources with multiple mip levels.
    */
   int level_override;

   /* When non-negative, samplers should use this layer instead of the one
    * specified by the GL state.
    *
    * This is used for EGL images and VDPAU interop, where imported
    * pipe_resources may be cube, 3D, or array textures (containing layers
    * with different fields in the case of VDPAU) even though the GL state
    * describes one non-array texture per field.
    */
   int layer_override;

    /**
     * Set when the texture images of this texture object might not all be in
     * the pipe_resource *pt above.
     */
    bool needs_validation;
};


static inline struct st_texture_image *
st_texture_image(struct gl_texture_image *img)
{
   return (struct st_texture_image *) img;
}

static inline const struct st_texture_image *
st_texture_image_const(const struct gl_texture_image *img)
{
   return (const struct st_texture_image *) img;
}

static inline struct st_texture_object *
st_texture_object(struct gl_texture_object *obj)
{
   return (struct st_texture_object *) obj;
}

static inline const struct st_texture_object *
st_texture_object_const(const struct gl_texture_object *obj)
{
   return (const struct st_texture_object *) obj;
}


static inline struct pipe_resource *
st_get_texobj_resource(struct gl_texture_object *texObj)
{
   struct st_texture_object *stObj = st_texture_object(texObj);
   return stObj ? stObj->pt : NULL;
}


static inline struct pipe_resource *
st_get_stobj_resource(struct st_texture_object *stObj)
{
   return stObj ? stObj->pt : NULL;
}


static inline struct st_texture_object *
st_get_texture_object(struct gl_context *ctx,
                      const struct gl_program *prog,
                      unsigned unit)
{
   const GLuint texUnit = prog->SamplerUnits[unit];
   struct gl_texture_object *texObj = ctx->Texture.Unit[texUnit]._Current;

   if (!texObj)
      return NULL;

   return st_texture_object(texObj);
}

static inline enum pipe_format
st_get_view_format(struct st_texture_object *stObj)
{
   if (!stObj)
      return PIPE_FORMAT_NONE;
   return stObj->surface_based ? stObj->surface_format : stObj->pt->format;
}


extern struct pipe_resource *
st_texture_create(struct st_context *st,
                  enum pipe_texture_target target,
                  enum pipe_format format,
                  GLuint last_level,
                  GLuint width0,
                  GLuint height0,
                  GLuint depth0,
                  GLuint layers,
                  GLuint nr_samples,
                  GLuint tex_usage );


extern void
st_gl_texture_dims_to_pipe_dims(GLenum texture,
                                unsigned widthIn,
                                uint16_t heightIn,
                                uint16_t depthIn,
                                unsigned *widthOut,
                                uint16_t *heightOut,
                                uint16_t *depthOut,
                                uint16_t *layersOut);

/* Check if an image fits into an existing texture object.
 */
extern GLboolean
st_texture_match_image(struct st_context *st,
                       const struct pipe_resource *pt,
                       const struct gl_texture_image *image);

/* Return a pointer to an image within a texture.  Return image stride as
 * well.
 */
extern GLubyte *
st_texture_image_map(struct st_context *st, struct st_texture_image *stImage,
                     enum pipe_map_flags usage,
                     GLuint x, GLuint y, GLuint z,
                     GLuint w, GLuint h, GLuint d,
                     struct pipe_transfer **transfer);

extern void
st_texture_image_unmap(struct st_context *st,
                       struct st_texture_image *stImage, unsigned slice);


/* Return pointers to each 2d slice within an image.  Indexed by depth
 * value.
 */
extern const GLuint *
st_texture_depth_offsets(struct pipe_resource *pt, GLuint level);

/* Copy an image between two textures
 */
extern void
st_texture_image_copy(struct pipe_context *pipe,
                      struct pipe_resource *dst, GLuint dstLevel,
                      struct pipe_resource *src, GLuint srcLevel,
                      GLuint face);


extern struct pipe_resource *
st_create_color_map_texture(struct gl_context *ctx);

void
st_destroy_bound_texture_handles(struct st_context *st);

void
st_destroy_bound_image_handles(struct st_context *st);

bool
st_astc_format_fallback(const struct st_context *st, mesa_format format);

bool
st_compressed_format_fallback(struct st_context *st, mesa_format format);

void
st_convert_image(const struct st_context *st, const struct gl_image_unit *u,
                 struct pipe_image_view *img, unsigned shader_access);

void
st_convert_image_from_unit(const struct st_context *st,
                           struct pipe_image_view *img,
                           GLuint imgUnit,
                           unsigned shader_access);

void
st_convert_sampler(const struct st_context *st,
                   const struct gl_texture_object *texobj,
                   const struct gl_sampler_object *msamp,
                   float tex_unit_lod_bias,
                   struct pipe_sampler_state *sampler);

void
st_convert_sampler_from_unit(const struct st_context *st,
                             struct pipe_sampler_state *sampler,
                             GLuint texUnit);

void
st_update_single_texture(struct st_context *st,
                         struct pipe_sampler_view **sampler_view,
                         GLuint texUnit, bool glsl130_or_later,
                         bool ignore_srgb_decode);

void
st_make_bound_samplers_resident(struct st_context *st,
                                struct gl_program *prog);

void
st_make_bound_images_resident(struct st_context *st,
                              struct gl_program *prog);

#endif
