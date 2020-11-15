/**********************************************************
 * Copyright 2010 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/


#ifndef _API_H_
#define _API_H_

#include "pipe/p_format.h"

/**
 * \file API for communication between gallium frontends and supporting
 * frontends such as DRI.
 *
 * This file defines an API to be implemented by both gallium frontends and
 * their managers.
 */

/**
 * The supported rendering API.
 */
enum st_api_type {
   ST_API_OPENGL,
   ST_API_OPENVG,

   ST_API_COUNT
};

/**
 * The profile of a context.
 */
enum st_profile_type
{
   ST_PROFILE_DEFAULT,			/**< OpenGL compatibility profile */
   ST_PROFILE_OPENGL_CORE,		/**< OpenGL 3.2+ core profile */
   ST_PROFILE_OPENGL_ES1,		/**< OpenGL ES 1.x */
   ST_PROFILE_OPENGL_ES2		/**< OpenGL ES 2.0 */
};

/* for profile_mask in st_api */
#define ST_PROFILE_DEFAULT_MASK      (1 << ST_PROFILE_DEFAULT)
#define ST_PROFILE_OPENGL_CORE_MASK  (1 << ST_PROFILE_OPENGL_CORE)
#define ST_PROFILE_OPENGL_ES1_MASK   (1 << ST_PROFILE_OPENGL_ES1)
#define ST_PROFILE_OPENGL_ES2_MASK   (1 << ST_PROFILE_OPENGL_ES2)

/**
 * Optional API features.
 */
enum st_api_feature
{
   ST_API_FEATURE_MS_VISUALS  /**< support for multisample visuals */
};

/* for feature_mask in st_api */
#define ST_API_FEATURE_MS_VISUALS_MASK (1 << ST_API_FEATURE_MS_VISUALS)

/**
 * New context flags for GL 3.0 and beyond.
 *
 * Profile information (core vs. compatibilty for OpenGL 3.2+) is communicated
 * through the \c st_profile_type, not through flags.
 */
#define ST_CONTEXT_FLAG_DEBUG               (1 << 0)
#define ST_CONTEXT_FLAG_FORWARD_COMPATIBLE  (1 << 1)
#define ST_CONTEXT_FLAG_ROBUST_ACCESS       (1 << 2)
#define ST_CONTEXT_FLAG_RESET_NOTIFICATION_ENABLED (1 << 3)
#define ST_CONTEXT_FLAG_NO_ERROR            (1 << 4)
#define ST_CONTEXT_FLAG_RELEASE_NONE	    (1 << 5)
#define ST_CONTEXT_FLAG_HIGH_PRIORITY       (1 << 6)
#define ST_CONTEXT_FLAG_LOW_PRIORITY        (1 << 7)

/**
 * Reasons that context creation might fail.
 */
enum st_context_error {
   ST_CONTEXT_SUCCESS = 0,
   ST_CONTEXT_ERROR_NO_MEMORY,
   ST_CONTEXT_ERROR_BAD_API,
   ST_CONTEXT_ERROR_BAD_VERSION,
   ST_CONTEXT_ERROR_BAD_FLAG,
   ST_CONTEXT_ERROR_UNKNOWN_ATTRIBUTE,
   ST_CONTEXT_ERROR_UNKNOWN_FLAG
};

/**
 * Used in st_context_iface->teximage.
 */
enum st_texture_type {
   ST_TEXTURE_1D,
   ST_TEXTURE_2D,
   ST_TEXTURE_3D,
   ST_TEXTURE_RECT
};

/**
 * Available attachments of framebuffer.
 */
enum st_attachment_type {
   ST_ATTACHMENT_FRONT_LEFT,
   ST_ATTACHMENT_BACK_LEFT,
   ST_ATTACHMENT_FRONT_RIGHT,
   ST_ATTACHMENT_BACK_RIGHT,
   ST_ATTACHMENT_DEPTH_STENCIL,
   ST_ATTACHMENT_ACCUM,
   ST_ATTACHMENT_SAMPLE,

   ST_ATTACHMENT_COUNT,
   ST_ATTACHMENT_INVALID = -1
};

/* for buffer_mask in st_visual */
#define ST_ATTACHMENT_FRONT_LEFT_MASK     (1 << ST_ATTACHMENT_FRONT_LEFT)
#define ST_ATTACHMENT_BACK_LEFT_MASK      (1 << ST_ATTACHMENT_BACK_LEFT)
#define ST_ATTACHMENT_FRONT_RIGHT_MASK    (1 << ST_ATTACHMENT_FRONT_RIGHT)
#define ST_ATTACHMENT_BACK_RIGHT_MASK     (1 << ST_ATTACHMENT_BACK_RIGHT)
#define ST_ATTACHMENT_DEPTH_STENCIL_MASK  (1 << ST_ATTACHMENT_DEPTH_STENCIL)
#define ST_ATTACHMENT_ACCUM_MASK          (1 << ST_ATTACHMENT_ACCUM)
#define ST_ATTACHMENT_SAMPLE_MASK         (1 << ST_ATTACHMENT_SAMPLE)

/**
 * Flush flags.
 */
#define ST_FLUSH_FRONT                    (1 << 0)
#define ST_FLUSH_END_OF_FRAME             (1 << 1)
#define ST_FLUSH_WAIT                     (1 << 2)
#define ST_FLUSH_FENCE_FD                 (1 << 3)

/**
 * Value to st_manager->get_param function.
 */
enum st_manager_param {
   /**
    * The DRI frontend on old libGL's doesn't do the right thing
    * with regards to invalidating the framebuffers.
    *
    * For the GL gallium frontend that means that it needs to invalidate
    * the framebuffer in glViewport itself.
    */
   ST_MANAGER_BROKEN_INVALIDATE
};

struct pipe_context;
struct pipe_resource;
struct pipe_fence_handle;
struct util_queue_monitoring;

/**
 * Used in st_manager_iface->get_egl_image.
 */
struct st_egl_image
{
   /* this is owned by the caller */
   struct pipe_resource *texture;

   /* format only differs from texture->format for multi-planar (YUV): */
   enum pipe_format format;

   unsigned level;
   unsigned layer;
   /* GL internal format. */
   unsigned internalformat;
};

/**
 * Represent the visual of a framebuffer.
 */
struct st_visual
{
   bool no_config;

   /**
    * Available buffers.  Bitfield of ST_ATTACHMENT_*_MASK bits.
    */
   unsigned buffer_mask;

   /**
    * Buffer formats.  The formats are always set even when the buffer is
    * not available.
    */
   enum pipe_format color_format;
   enum pipe_format depth_stencil_format;
   enum pipe_format accum_format;
   unsigned samples;

   /**
    * Desired render buffer.
    */
   enum st_attachment_type render_buffer;
};


/**
 * Configuration options from driconf
 */
struct st_config_options
{
   bool disable_blend_func_extended;
   bool disable_glsl_line_continuations;
   bool disable_arb_gpu_shader5;
   bool force_glsl_extensions_warn;
   unsigned force_glsl_version;
   bool allow_extra_pp_tokens;
   bool allow_glsl_extension_directive_midshader;
   bool allow_glsl_120_subset_in_110;
   bool allow_glsl_builtin_const_expression;
   bool allow_glsl_relaxed_es;
   bool allow_glsl_builtin_variable_redeclaration;
   bool allow_higher_compat_version;
   bool glsl_zero_init;
   bool vs_position_always_invariant;
   bool force_glsl_abs_sqrt;
   bool allow_glsl_cross_stage_interpolation_mismatch;
   bool allow_draw_out_of_order;
   bool force_integer_tex_nearest;
   bool force_gl_names_reuse;
   char *force_gl_vendor;
   unsigned char config_options_sha1[20];
};

/**
 * Represent the attributes of a context.
 */
struct st_context_attribs
{
   /**
    * The profile and minimal version to support.
    *
    * The valid profiles and versions are rendering API dependent.  The latest
    * version satisfying the request should be returned.
    */
   enum st_profile_type profile;
   int major, minor;

   /** Mask of ST_CONTEXT_FLAG_x bits */
   unsigned flags;

   /**
    * The visual of the framebuffers the context will be bound to.
    */
   struct st_visual visual;

   /**
    * Configuration options.
    */
   struct st_config_options options;
};

struct st_context_iface;
struct st_manager;

/**
 * Represent a windowing system drawable.
 *
 * The framebuffer is implemented by the frontend manager and
 * used by gallium frontends.
 *
 * Instead of the winsys poking into the frontend context to figure
 * out what buffers that might be needed in the future by the frontend
 * context, it calls into the framebuffer to get the textures.
 *
 * This structure along with the notify_invalid_framebuffer
 * allows framebuffers to be shared between different threads
 * but at the same make the API context free from thread
 * synchronization primitves, with the exception of a small
 * atomic flag used for notification of framebuffer dirty status.
 *
 * The thread synchronization is put inside the framebuffer
 * and only called once the framebuffer has become dirty.
 */
struct st_framebuffer_iface
{
   /**
    * Atomic stamp which changes when framebuffers need to be updated.
    */
   int32_t stamp;

   /**
    * Identifier that uniquely identifies the framebuffer interface object.
    */
   uint32_t ID;

   /**
    * The frontend manager that manages this object.
    */
   struct st_manager *state_manager;

   /**
    * Available for the frontend manager to use.
    */
   void *st_manager_private;

   /**
    * The visual of a framebuffer.
    */
   const struct st_visual *visual;

   /**
    * Flush the front buffer.
    *
    * On some window systems, changes to the front buffers are not immediately
    * visible.  They need to be flushed.
    *
    * @att is one of the front buffer attachments.
    */
   bool (*flush_front)(struct st_context_iface *stctx,
                       struct st_framebuffer_iface *stfbi,
                       enum st_attachment_type statt);

   /**
    * the gallium frontend asks for the textures it needs.
    *
    * It should try to only ask for attachments that it currently renders
    * to, thus allowing the winsys to delay the allocation of textures not
    * needed. For example front buffer attachments are not needed if you
    * only do back buffer rendering.
    *
    * The implementor of this function needs to also ensure
    * thread safty as this call might be done from multiple threads.
    *
    * The returned textures are owned by the caller.  They should be
    * unreferenced when no longer used.  If this function is called multiple
    * times with different sets of attachments, those buffers not included in
    * the last call might be destroyed.  This behavior might change in the
    * future.
    */
   bool (*validate)(struct st_context_iface *stctx,
                    struct st_framebuffer_iface *stfbi,
                    const enum st_attachment_type *statts,
                    unsigned count,
                    struct pipe_resource **out);
   bool (*flush_swapbuffers) (struct st_context_iface *stctx,
                              struct st_framebuffer_iface *stfbi);
};

/**
 * Represent a rendering context.
 *
 * This entity is created from st_api and used by the frontend manager.
 */
struct st_context_iface
{
   /**
    * Available for the gallium frontend and the manager to use.
    */
   void *st_context_private;
   void *st_manager_private;

   /**
    * The frontend manager that manages this object.
    */
   struct st_manager *state_manager;

   /**
    * The CSO context associated with this context in case we need to draw
    * something before swap buffers.
    */
   struct cso_context *cso_context;

   /**
    * The gallium context.
    */
   struct pipe_context *pipe;

   /**
    * Destroy the context.
    */
   void (*destroy)(struct st_context_iface *stctxi);

   /**
    * Flush all drawing from context to the pipe also flushes the pipe.
    */
   void (*flush)(struct st_context_iface *stctxi, unsigned flags,
                 struct pipe_fence_handle **fence,
                 void (*notify_before_flush_cb) (void*),
                 void* notify_before_flush_cb_args);

   /**
    * Replace the texture image of a texture object at the specified level.
    *
    * This function is optional.
    */
   bool (*teximage)(struct st_context_iface *stctxi,
                    enum st_texture_type target,
                    int level, enum pipe_format internal_format,
                    struct pipe_resource *tex, bool mipmap);

   /**
    * Used to implement glXCopyContext.
    */
   void (*copy)(struct st_context_iface *stctxi,
                struct st_context_iface *stsrci, unsigned mask);

   /**
    * Used to implement wglShareLists.
    */
   bool (*share)(struct st_context_iface *stctxi,
                 struct st_context_iface *stsrci);

   /**
    * Start the thread if the API has a worker thread.
    * Called after the context has been created and fully initialized on both
    * sides (e.g. st/mesa and st/dri).
    */
   void (*start_thread)(struct st_context_iface *stctxi);

   /**
    * If the API is multithreaded, wait for all queued commands to complete.
    * Called from the main thread.
    */
   void (*thread_finish)(struct st_context_iface *stctxi);
};


/**
 * Represent a frontend manager.
 *
 * This interface is implemented by the frontend manager.  It corresponds
 * to a "display" in the window system.
 */
struct st_manager
{
   struct pipe_screen *screen;

   /**
    * Look up and return the info of an EGLImage.
    *
    * This is used to implement for example EGLImageTargetTexture2DOES.
    * The GLeglImageOES agrument of that call is passed directly to this
    * function call and the information needed to access this is returned
    * in the given struct out.
    *
    * @smapi: manager owning the caller context
    * @stctx: caller context
    * @egl_image: EGLImage that caller recived
    * @out: return struct filled out with access information.
    *
    * This function is optional.
    */
   bool (*get_egl_image)(struct st_manager *smapi,
                         void *egl_image,
                         struct st_egl_image *out);

   /**
    * Query an manager param.
    */
   int (*get_param)(struct st_manager *smapi,
                    enum st_manager_param param);

   /**
    * Call the loader function setBackgroundContext. Called from the worker
    * thread.
    */
   void (*set_background_context)(struct st_context_iface *stctxi,
                                  struct util_queue_monitoring *queue_info);

   /**
    * Destroy any private data used by the frontend manager.
    */
   void (*destroy)(struct st_manager *smapi);

   /**
    * Available for the frontend manager to use.
    */
   void *st_manager_private;
};

/**
 * Represent a rendering API such as OpenGL or OpenVG.
 *
 * Implemented by the gallium frontend and used by the frontend manager.
 */
struct st_api
{
   /**
    * The name of the rendering API.  This is informative.
    */
   const char *name;

   /**
    * The supported rendering API.
    */
   enum st_api_type api;

   /**
    * The supported profiles.  Tested with ST_PROFILE_*_MASK.
    */
   unsigned profile_mask;

   /**
    * The supported optional features.  Tested with ST_FEATURE_*_MASK.
    */
   unsigned feature_mask;

   /**
    * Destroy the API.
    */
   void (*destroy)(struct st_api *stapi);

   /**
    * Query supported OpenGL versions. (if applicable)
    * The format is (major*10+minor).
    */
   void (*query_versions)(struct st_api *stapi, struct st_manager *sm,
                          struct st_config_options *options,
                          int *gl_core_version,
                          int *gl_compat_version,
                          int *gl_es1_version,
                          int *gl_es2_version);

   /**
    * Create a rendering context.
    */
   struct st_context_iface *(*create_context)(struct st_api *stapi,
                                              struct st_manager *smapi,
                                              const struct st_context_attribs *attribs,
                                              enum st_context_error *error,
                                              struct st_context_iface *stsharei);

   /**
    * Bind the context to the calling thread with draw and read as drawables.
    *
    * The framebuffers might be NULL, or might have different visuals than the
    * context does.
    */
   bool (*make_current)(struct st_api *stapi,
                        struct st_context_iface *stctxi,
                        struct st_framebuffer_iface *stdrawi,
                        struct st_framebuffer_iface *streadi);

   /**
    * Get the currently bound context in the calling thread.
    */
   struct st_context_iface *(*get_current)(struct st_api *stapi);

   /**
    * Notify the st manager the framebuffer interface object
    * is no longer valid.
    */
   void (*destroy_drawable)(struct st_api *stapi,
                            struct st_framebuffer_iface *stfbi);
};

/**
 * Return true if the visual has the specified buffers.
 */
static inline bool
st_visual_have_buffers(const struct st_visual *visual, unsigned mask)
{
   return ((visual->buffer_mask & mask) == mask);
}

#endif /* _API_H_ */
