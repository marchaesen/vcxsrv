/*
 * Copyright 1998-1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2007-2008 Red Hat, Inc.
 * (C) Copyright IBM Corporation 2004
 * All Rights Reserved.
 * Copyright © 2022 Google LLC
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

#ifndef MESA_INTERFACE_H
#define MESA_INTERFACE_H

#include <stdbool.h>
#include <stdint.h>

struct dri_screen;
struct dri_context;
struct dri_drawable;
struct dri_config;
struct dri_image;

/**
 * Extension struct.  Drivers 'inherit' from this struct by embedding
 * it as the first element in the extension struct.
 *
 * We never break API in for a DRI extension.  If we need to change
 * the way things work in a non-backwards compatible manner, we
 * introduce a new extension.  During a transition period, we can
 * leave both the old and the new extension in the driver, which
 * allows us to move to the new interface without having to update the
 * loader(s) in lock step.
 *
 * However, we can add entry points to an extension over time as long
 * as we don't break the old ones.  As we add entry points to an
 * extension, we increase the version number.  The corresponding
 * #define can be used to guard code that accesses the new entry
 * points at compile time and the version field in the extension
 * struct can be used at run-time to determine how to use the
 * extension.
 */
typedef struct {
    const char *name;
    int version;
} __DRIextension;

/**
 * The first set of extension are the screen extensions, returned by
 * __DRIcore::getExtensions().  This entry point will return a list of
 * extensions and the loader can use the ones it knows about by
 * casting them to more specific extensions and advertising any GLX
 * extensions the DRI extensions enables.
 */

/* Valid values for format in the setTexBuffer2 function below.  These
 * values match the GLX tokens for compatibility reasons, but we
 * define them here since the DRI interface can't depend on GLX. */
#define __DRI_TEXTURE_FORMAT_RGB         0x20D9
#define __DRI_TEXTURE_FORMAT_RGBA        0x20DA

#define __DRI_TEX_BUFFER "DRI_TexBuffer"
typedef struct {
    __DRIextension base;

    /**
     * Method to override base texture image with the contents of a
     * struct dri_drawable, including the required texture format attribute.
     *
     * For GLX_EXT_texture_from_pixmap with AIGLX.  Used by the X server since
     * 2011.
     *
     * \since 2
     */
    void (*setTexBuffer2)(struct dri_context *pDRICtx,
			  int target,
			  int format,
			  struct dri_drawable *pDraw);
} __DRItexBufferExtension;

/**
 * Used by drivers that implement DRI2.  Version 3 is used by the X server.
 */
#define __DRI2_FLUSH_DRAWABLE (1 << 0) /* the drawable should be flushed. */
#define __DRI2_FLUSH_CONTEXT  (1 << 1) /* glFlush should be called */
#define __DRI2_FLUSH_INVALIDATE_ANCILLARY (1 << 2)

enum __DRI2throttleReason {
   __DRI2_THROTTLE_SWAPBUFFER,
   __DRI2_THROTTLE_COPYSUBBUFFER,
   __DRI2_THROTTLE_FLUSHFRONT,
   __DRI2_NOTHROTTLE_SWAPBUFFER,
};

/**
 * Extension for EGL_ANDROID_blob_cache
 * *
 * Not used by the X server.
 */
typedef void
(*__DRIblobCacheSet) (const void *key, signed long keySize,
                      const void *value, signed long valueSize);

typedef signed long
(*__DRIblobCacheGet) (const void *key, signed long keySize,
                      void *value, signed long valueSize);

/**
 * Extension for fences / synchronization objects.
 * *
 * Not used by the X server.
 */

#define __DRI2_FENCE "DRI2_Fence"

#define __DRI2_FENCE_FLAG_FLUSH_COMMANDS  (1 << 0)

/**
 * \name Capabilities that might be returned by __DRI2fenceExtensionRec::get_capabilities
 */
/*@{*/
#define __DRI_FENCE_CAP_NATIVE_FD 1
/*@}*/

typedef struct {
   __DRIextension base;

   /**
    * Create and insert a fence into the command stream of the context.
    */
   void *(*create_fence)(struct dri_context *ctx);

   /**
    * Get a fence associated with the OpenCL event object.
    * This can be NULL, meaning that OpenCL interoperability is not supported.
    */
   void *(*get_fence_from_cl_event)(struct dri_screen *screen, intptr_t cl_event);

   /**
    * Destroy a fence.
    */
   void (*destroy_fence)(struct dri_screen *screen, void *fence);

   /**
    * This function waits and doesn't return until the fence is signalled
    * or the timeout expires. It returns true if the fence has been signaled.
    *
    * \param ctx     the context where commands are flushed
    * \param fence   the fence
    * \param flags   a combination of __DRI2_FENCE_FLAG_xxx flags
    * \param timeout the timeout in ns or __DRI2_FENCE_TIMEOUT_INFINITE
    */
   unsigned char (*client_wait_sync)(struct dri_context *ctx, void *fence,
                                     unsigned flags, uint64_t timeout);

   /**
    * This function enqueues a wait command into the command stream of
    * the context and then returns. When the execution reaches the wait
    * command, no further execution will be done in the context until
    * the fence is signaled. This is a no-op if the device doesn't support
    * parallel execution of contexts.
    *
    * \param ctx     the context where the waiting is done
    * \param fence   the fence
    * \param flags   a combination of __DRI2_FENCE_FLAG_xxx flags that make
    *                sense with this function (right now there are none)
    */
   void (*server_wait_sync)(struct dri_context *ctx, void *fence, unsigned flags);

   /**
    * Query for general capabilities of the driver that concern fences.
    * Returns a bitmask of __DRI_FENCE_CAP_x
    *
    * \since 2
    */
   unsigned (*get_capabilities)(struct dri_screen *screen);

   /**
    * Create an fd (file descriptor) associated fence.  If the fence fd
    * is -1, this behaves similarly to create_fence() except that when
    * rendering is flushed the driver creates a fence fd.  Otherwise,
    * the driver wraps an existing fence fd.
    *
    * This is used to implement the EGL_ANDROID_native_fence_sync extension.
    *
    * \since 2
    *
    * \param ctx     the context associated with the fence
    * \param fd      the fence fd or -1
    */
   void *(*create_fence_fd)(struct dri_context *ctx, int fd);

   /**
    * For fences created with create_fence_fd(), after rendering is flushed,
    * this retrieves the native fence fd.  Caller takes ownership of the
    * fd and will close() it when it is no longer needed.
    *
    * \since 2
    *
    * \param screen  the screen associated with the fence
    * \param fence   the fence
    */
   int (*get_fence_fd)(struct dri_screen *screen, void *fence);
} __DRI2fenceExtension;

/**
 * Extension for limiting window system back buffer rendering to user-defined
 * scissor region.
 *
 * Not used by the X server.
 */
typedef struct {
   __DRIextension base;

   /**
    * Provides an array of rectangles representing an overriding scissor region
    * for rendering operations performed to the specified drawable. These
    * rectangles do not replace client API scissor regions or draw
    * co-ordinates, but instead inform the driver of the overall bounds of all
    * operations which will be issued before the next flush.
    *
    * Any rendering operations writing pixels outside this region to the
    * drawable will have an undefined effect on the entire drawable.
    *
    * This entrypoint may only be called after the drawable has either been
    * newly created or flushed, and before any rendering operations which write
    * pixels to the drawable. Calling this entrypoint at any other time will
    * have an undefined effect on the entire drawable.
    *
    * Calling this entrypoint with @nrects 0 and @rects NULL will reset the
    * region to the buffer's full size. This entrypoint may be called once to
    * reset the region, followed by a second call with a populated region,
    * before a rendering call is made.
    *
    * Used to implement EGL_KHR_partial_update.
    *
    * \param drawable affected drawable
    * \param nrects   number of rectangles provided
    * \param rects    the array of rectangles, lower-left origin
    */
   void (*set_damage_region)(struct dri_drawable *drawable, unsigned int nrects,
                             int *rects);
} __DRI2bufferDamageExtension;

/*@}*/

/**
 * The following extensions describe loader features that the DRI
 * driver can make use of.  Some of these are mandatory, such as the
 * getDrawableInfo extension for DRI and the DRI Loader extensions for
 * DRI2, while others are optional, and if present allow the driver to
 * expose certain features.  The loader pass in a NULL terminated
 * array of these extensions to the driver in the createNewScreen
 * constructor.
 */

#define __DRI_SWRAST_IMAGE_OP_DRAW	1
#define __DRI_SWRAST_IMAGE_OP_SWAP	3

/**
 * SWRast Loader extension.
 *
 * Version 1 is advertised by the X server.
 */
#define __DRI_SWRAST_LOADER "DRI_SWRastLoader"
typedef struct {
    __DRIextension base;

    /*
     * Drawable position and size
     */
    void (*getDrawableInfo)(struct dri_drawable *drawable,
			    int *x, int *y, int *width, int *height,
			    void *loaderPrivate);

    /**
     * Put image to drawable
     */
    void (*putImage)(struct dri_drawable *drawable, int op,
		     int x, int y, int width, int height,
		     char *data, void *loaderPrivate);

    /**
     * Get image from readable
     */
    void (*getImage)(struct dri_drawable *readable,
		     int x, int y, int width, int height,
		     char *data, void *loaderPrivate);

    /**
     * Put image to drawable
     *
     * \since 2
     */
    void (*putImage2)(struct dri_drawable *drawable, int op,
                      int x, int y, int width, int height, int stride,
                      char *data, void *loaderPrivate);

   /**
     * Put image to drawable
     *
     * \since 3
     */
   void (*getImage2)(struct dri_drawable *readable,
		     int x, int y, int width, int height, int stride,
		     char *data, void *loaderPrivate);

    /**
     * Put shm image to drawable
     *
     * \since 4
     */
    void (*putImageShm)(struct dri_drawable *drawable, int op,
                        int x, int y, int width, int height, int stride,
                        int shmid, char *shmaddr, unsigned offset,
                        void *loaderPrivate);
    /**
     * Get shm image from readable
     *
     * \since 4
     */
    void (*getImageShm)(struct dri_drawable *readable,
                        int x, int y, int width, int height,
                        int shmid, void *loaderPrivate);

   /**
     * Put shm image to drawable (v2)
     *
     * The original version fixes srcx/y to 0, and expected
     * the offset to be adjusted. This version allows src x,y
     * to not be included in the offset. This is needed to
     * avoid certain overflow checks in the X server, that
     * result in lost rendering.
     *
     * \since 5
     */
    void (*putImageShm2)(struct dri_drawable *drawable, int op,
                         int x, int y,
                         int width, int height, int stride,
                         int shmid, char *shmaddr, unsigned offset,
                         void *loaderPrivate);

    /**
     * get shm image to drawable (v2)
     *
     * There are some cases where GLX can't use SHM, but DRI
     * still tries, we need to get a return type for when to
     * fallback to the non-shm path.
     *
     * \since 6
     */
    unsigned char (*getImageShm2)(struct dri_drawable *readable,
                                  int x, int y, int width, int height,
                                  int shmid, void *loaderPrivate);
} __DRIswrastLoaderExtension;

/**
 * Invalidate loader extension.  The presence of this extension
 * indicates to the DRI driver that the loader will call invalidate in
 * the __DRI2_FLUSH extension, whenever the needs to query for new
 * buffers.  This means that the DRI driver can drop the polling in
 * glViewport().
 *
 * The extension doesn't provide any functionality, it's only use to
 * indicate to the driver that it can use the new semantics.  A DRI
 * driver can use this to switch between the different semantics or
 * just refuse to initialize if this extension isn't present.
 *
 * Advertised by the X server.
 */
#define __DRI_USE_INVALIDATE "DRI_UseInvalidate"

typedef struct {
   __DRIextension base;
} __DRIuseInvalidateExtension;

/**
 * Tokens for struct dri_config attribs.  A number of attributes defined by
 * GLX or EGL standards are not in the table, as they must be provided
 * by the loader.  For example, FBConfig ID or visual ID, drawable type.
 */

#define __DRI_ATTRIB_BUFFER_SIZE		 1
#define __DRI_ATTRIB_LEVEL			 2
#define __DRI_ATTRIB_RED_SIZE			 3
#define __DRI_ATTRIB_GREEN_SIZE			 4
#define __DRI_ATTRIB_BLUE_SIZE			 5
#define __DRI_ATTRIB_LUMINANCE_SIZE		 6
#define __DRI_ATTRIB_ALPHA_SIZE			 7
#define __DRI_ATTRIB_ALPHA_MASK_SIZE		 8
#define __DRI_ATTRIB_DEPTH_SIZE			 9
#define __DRI_ATTRIB_STENCIL_SIZE		10
#define __DRI_ATTRIB_ACCUM_RED_SIZE		11
#define __DRI_ATTRIB_ACCUM_GREEN_SIZE		12
#define __DRI_ATTRIB_ACCUM_BLUE_SIZE		13
#define __DRI_ATTRIB_ACCUM_ALPHA_SIZE		14
#define __DRI_ATTRIB_SAMPLE_BUFFERS		15
#define __DRI_ATTRIB_SAMPLES			16
#define __DRI_ATTRIB_RENDER_TYPE		17
#define __DRI_ATTRIB_CONFIG_CAVEAT		18
#define __DRI_ATTRIB_CONFORMANT			19
#define __DRI_ATTRIB_DOUBLE_BUFFER		20
#define __DRI_ATTRIB_STEREO			21
#define __DRI_ATTRIB_AUX_BUFFERS		22
#define __DRI_ATTRIB_TRANSPARENT_TYPE		23
#define __DRI_ATTRIB_TRANSPARENT_INDEX_VALUE	24
#define __DRI_ATTRIB_TRANSPARENT_RED_VALUE	25
#define __DRI_ATTRIB_TRANSPARENT_GREEN_VALUE	26
#define __DRI_ATTRIB_TRANSPARENT_BLUE_VALUE	27
#define __DRI_ATTRIB_TRANSPARENT_ALPHA_VALUE	28
#define __DRI_ATTRIB_FLOAT_MODE			29
#define __DRI_ATTRIB_RED_MASK			30
#define __DRI_ATTRIB_GREEN_MASK			31
#define __DRI_ATTRIB_BLUE_MASK			32
#define __DRI_ATTRIB_ALPHA_MASK			33
#define __DRI_ATTRIB_MAX_PBUFFER_WIDTH		34
#define __DRI_ATTRIB_MAX_PBUFFER_HEIGHT		35
#define __DRI_ATTRIB_MAX_PBUFFER_PIXELS		36
#define __DRI_ATTRIB_OPTIMAL_PBUFFER_WIDTH	37
#define __DRI_ATTRIB_OPTIMAL_PBUFFER_HEIGHT	38
#define __DRI_ATTRIB_VISUAL_SELECT_GROUP	39
#define __DRI_ATTRIB_SWAP_METHOD		40 // Parsed by the X server when our visuals return it as an attrib.
#define __DRI_ATTRIB_MAX_SWAP_INTERVAL		41
#define __DRI_ATTRIB_MIN_SWAP_INTERVAL		42
#define __DRI_ATTRIB_BIND_TO_TEXTURE_RGB	43
#define __DRI_ATTRIB_BIND_TO_TEXTURE_RGBA	44
#define __DRI_ATTRIB_BIND_TO_MIPMAP_TEXTURE	45
#define __DRI_ATTRIB_BIND_TO_TEXTURE_TARGETS	46
#define __DRI_ATTRIB_YINVERTED			47
#define __DRI_ATTRIB_FRAMEBUFFER_SRGB_CAPABLE	48
#define __DRI_ATTRIB_MUTABLE_RENDER_BUFFER	49 /* EGL_MUTABLE_RENDER_BUFFER_BIT_KHR */
#define __DRI_ATTRIB_RED_SHIFT			50
#define __DRI_ATTRIB_GREEN_SHIFT		51
#define __DRI_ATTRIB_BLUE_SHIFT			52
#define __DRI_ATTRIB_ALPHA_SHIFT		53
#define __DRI_ATTRIB_MAX			54

/* __DRI_ATTRIB_RENDER_TYPE */
#define __DRI_ATTRIB_RGBA_BIT			0x01
#define __DRI_ATTRIB_COLOR_INDEX_BIT		0x02
#define __DRI_ATTRIB_LUMINANCE_BIT		0x04
#define __DRI_ATTRIB_FLOAT_BIT			0x08
#define __DRI_ATTRIB_UNSIGNED_FLOAT_BIT		0x10

/* __DRI_ATTRIB_CONFIG_CAVEAT */
#define __DRI_ATTRIB_SLOW_BIT			0x01
#define __DRI_ATTRIB_NON_CONFORMANT_CONFIG	0x02

/* __DRI_ATTRIB_TRANSPARENT_TYPE */
#define __DRI_ATTRIB_TRANSPARENT_RGB		0x00
#define __DRI_ATTRIB_TRANSPARENT_INDEX		0x01

/* __DRI_ATTRIB_BIND_TO_TEXTURE_TARGETS	 */
#define __DRI_ATTRIB_TEXTURE_1D_BIT		0x01
#define __DRI_ATTRIB_TEXTURE_2D_BIT		0x02
#define __DRI_ATTRIB_TEXTURE_RECTANGLE_BIT	0x04

/* __DRI_ATTRIB_SWAP_METHOD */
/* Note that with the exception of __DRI_ATTRIB_SWAP_NONE, we need to define
 * the same tokens as GLX. This is because old and current X servers will
 * transmit the driconf value grabbed from the AIGLX driver untranslated as
 * the GLX fbconfig value. These defines are kept for X Server suorce compatibility,
 * since Mesa no longer exposes GLX_OML_swap_method.
 */
#define __DRI_ATTRIB_SWAP_UNDEFINED             0x8063

/**
 * This extension defines the core DRI functionality.  It was introduced when
 * DRI2 and AIGLX were added.
 *
 * Version >= 2 indicates that getConfigAttrib with __DRI_ATTRIB_SWAP_METHOD
 * returns a reliable value.  The X server requires v1 and uses v2.
 */
typedef struct {
    __DRIextension base;

    /* Not used by the X server. */
    struct dri_screen *(*createNewScreen)(int screen, int fd,
				    unsigned int sarea_handle,
				    const __DRIextension **extensions,
				    const struct dri_config ***driverConfigs,
				    void *loaderPrivate);

    void (*destroyScreen)(struct dri_screen *screen);

    const __DRIextension **(*getExtensions)(struct dri_screen *screen);

    /* Not used by the X server. */
    int (*getConfigAttrib)(const struct dri_config *config,
			   unsigned int attrib,
			   unsigned int *value);

    /* Not used by the X server. */
    int (*indexConfigAttrib)(const struct dri_config *config, int index,
			     unsigned int *attrib, unsigned int *value);

    /* Not used by the X server. */
    struct dri_drawable *(*createNewDrawable)(struct dri_screen *screen,
					const struct dri_config *config,
					unsigned int drawable_id,
					unsigned int head,
					void *loaderPrivate);

    /* Used by the X server */
    void (*destroyDrawable)(struct dri_drawable *drawable);

    /* Used by the X server in swrast mode. */
    void (*swapBuffers)(struct dri_drawable *drawable);

    /* Used by the X server in swrast mode. */
    struct dri_context *(*createNewContext)(struct dri_screen *screen,
				      const struct dri_config *config,
				      struct dri_context *shared,
				      void *loaderPrivate);

    /* Used by the X server. */
    int (*copyContext)(struct dri_context *dest,
		       struct dri_context *src,
		       unsigned long mask);

    /* Used by the X server. */
    void (*destroyContext)(struct dri_context *context);

    /* Used by the X server. */
    int (*bindContext)(struct dri_context *ctx,
		       struct dri_drawable *pdraw,
		       struct dri_drawable *pread);

    /* Used by the X server. */
    int (*unbindContext)(struct dri_context *ctx);

    void (*swapBuffersWithDamage)(struct dri_drawable *drawable, int nrects, const int *rects);
} __DRIcoreExtension;

/** Common DRI function definitions, shared among DRI2 and Image extensions
 */

typedef struct dri_screen *
(*__DRIcreateNewScreen2Func)(int screen, int fd,
                             const __DRIextension **extensions,
                             const __DRIextension **driver_extensions,
                             const struct dri_config ***driver_configs,
                             void *loaderPrivate);
typedef struct dri_screen *
(*__DRIcreateNewScreen3Func)(int screen, int fd,
                             const __DRIextension **extensions,
                             const __DRIextension **driver_extensions,
                             const struct dri_config ***driver_configs,
                             bool implicit,
                             void *loaderPrivate);

typedef struct dri_drawable *
(*__DRIcreateNewDrawableFunc)(struct dri_screen *screen,
                              const struct dri_config *config,
                              void *loaderPrivate);

typedef struct dri_context *
(*__DRIcreateContextAttribsFunc)(struct dri_screen *screen,
                                 int api,
                                 const struct dri_config *config,
                                 struct dri_context *shared,
                                 unsigned num_attribs,
                                 const uint32_t *attribs,
                                 unsigned *error,
                                 void *loaderPrivate);

typedef unsigned int
(*__DRIgetAPIMaskFunc)(struct dri_screen *screen);

/**
 * DRI2 Loader extension.
 */
#define __DRI_BUFFER_FRONT_LEFT		0
#define __DRI_BUFFER_BACK_LEFT		1
#define __DRI_BUFFER_FRONT_RIGHT	2
#define __DRI_BUFFER_BACK_RIGHT		3
#define __DRI_BUFFER_DEPTH		4
#define __DRI_BUFFER_STENCIL		5
#define __DRI_BUFFER_FAKE_FRONT_LEFT	6
#define __DRI_BUFFER_FAKE_FRONT_RIGHT	7
#define __DRI_BUFFER_DEPTH_STENCIL	8  /**< Only available with DRI2 1.1 */

/* Inofficial and for internal use. Increase when adding a new buffer token. */
#define __DRI_BUFFER_COUNT		9

/* Used by the X server. */
typedef struct {
    unsigned int attachment;
    unsigned int name;
    unsigned int pitch;
    unsigned int cpp;
    unsigned int flags;
} __DRIbuffer;

/* The X server implements up to version 3 of the DRI2 loader. */
#define __DRI_DRI2_LOADER "DRI_DRI2Loader"

enum dri_loader_cap {
   /* Whether the loader handles RGBA channel ordering correctly. If not,
    * only BGRA ordering can be exposed.
    */
   DRI_LOADER_CAP_RGBA_ORDERING,
   DRI_LOADER_CAP_FP16,
};

typedef struct {
    __DRIextension base;

    __DRIbuffer *(*getBuffers)(struct dri_drawable *driDrawable,
			       int *width, int *height,
			       unsigned int *attachments, int count,
			       int *out_count, void *loaderPrivate);

    /**
     * Flush pending front-buffer rendering
     *
     * Any rendering that has been performed to the
     * \c __DRI_BUFFER_FAKE_FRONT_LEFT will be flushed to the
     * \c __DRI_BUFFER_FRONT_LEFT.
     *
     * \param driDrawable    Drawable whose front-buffer is to be flushed
     * \param loaderPrivate  Loader's private data
     *
     * \since 2
     */
    void (*flushFrontBuffer)(struct dri_drawable *driDrawable, void *loaderPrivate);


    /**
     * Get list of buffers from the server
     *
     * Gets a list of buffer for the specified set of attachments.  Unlike
     * \c ::getBuffers, this function takes a list of attachments paired with
     * opaque \c unsigned \c int value describing the format of the buffer.
     * It is the responsibility of the caller to know what the service that
     * allocates the buffers will expect to receive for the format.
     *
     * \param driDrawable    Drawable whose buffers are being queried.
     * \param width          Output where the width of the buffers is stored.
     * \param height         Output where the height of the buffers is stored.
     * \param attachments    List of pairs of attachment ID and opaque format
     *                       requested for the drawable.
     * \param count          Number of attachment / format pairs stored in
     *                       \c attachments.
     * \param loaderPrivate  Loader's private data
     *
     * \since 3
     */
    __DRIbuffer *(*getBuffersWithFormat)(struct dri_drawable *driDrawable,
					 int *width, int *height,
					 unsigned int *attachments, int count,
					 int *out_count, void *loaderPrivate);

    /**
     * Return a loader capability value. If the loader doesn't know the enum,
     * it will return 0.
     *
     * \param loaderPrivate The last parameter of createNewScreen or
     *                      createNewScreen2.
     * \param cap           See the enum.
     *
     * \since 4
     */
    unsigned (*getCapability)(void *loaderPrivate, enum dri_loader_cap cap);

    /**
     * Clean up any loader state associated with an image.
     *
     * \param loaderPrivate  Loader's private data that was previously passed
     *                       into a __DRIimageExtensionRec::createImage function
     * \since 5
     */
    void (*destroyLoaderImageState)(void *loaderPrivate);
} __DRIdri2LoaderExtension;

/**
 * This extension provides alternative screen, drawable and context
 * constructors for DRI2.  The X server uses up to version 4.
 */
#define __DRI_API_OPENGL	0	/**< OpenGL compatibility profile */
#define __DRI_API_GLES		1	/**< OpenGL ES 1.x */
#define __DRI_API_GLES2		2	/**< OpenGL ES 2.x */
#define __DRI_API_OPENGL_CORE	3	/**< OpenGL 3.2+ core profile */
#define __DRI_API_GLES3		4	/**< OpenGL ES 3.x */

#define __DRI_CTX_ATTRIB_MAJOR_VERSION		0
#define __DRI_CTX_ATTRIB_MINOR_VERSION		1

/* These must alias the GLX/EGL values. */
#define __DRI_CTX_ATTRIB_FLAGS			2
#define __DRI_CTX_FLAG_DEBUG			0x00000001
#define __DRI_CTX_FLAG_FORWARD_COMPATIBLE	0x00000002
#define __DRI_CTX_FLAG_ROBUST_BUFFER_ACCESS	0x00000004
/* Not yet implemented but placed here to reserve the alias with GLX */
#define __DRI_CTX_FLAG_RESET_ISOLATION          0x00000008

#define __DRI_CTX_ATTRIB_RESET_STRATEGY		3
#define __DRI_CTX_RESET_NO_NOTIFICATION		0
#define __DRI_CTX_RESET_LOSE_CONTEXT		1

/**
 * \name Context priority levels.
 */
#define __DRI_CTX_ATTRIB_PRIORITY		4
#define __DRI_CTX_PRIORITY_LOW			0
#define __DRI_CTX_PRIORITY_MEDIUM		1
#define __DRI_CTX_PRIORITY_HIGH			2
#define __DRI_CTX_PRIORITY_REALTIME		3

#define __DRI_CTX_ATTRIB_RELEASE_BEHAVIOR	5
#define __DRI_CTX_RELEASE_BEHAVIOR_NONE         0
#define __DRI_CTX_RELEASE_BEHAVIOR_FLUSH        1

#define __DRI_CTX_ATTRIB_NO_ERROR               6

/**
 * \requires __DRI2_RENDER_HAS_PROTECTED_CONTEXT.
 *
 */
#define __DRI_CTX_ATTRIB_PROTECTED              7


#define __DRI_CTX_NUM_ATTRIBS                   8

/**
 * \name Reasons that createContextAttribs might fail
 */
/*@{*/
/** Success! */
#define __DRI_CTX_ERROR_SUCCESS			0

/** Memory allocation failure */
#define __DRI_CTX_ERROR_NO_MEMORY		1

/** Client requested an API (e.g., OpenGL ES 2.0) that the driver can't do. */
#define __DRI_CTX_ERROR_BAD_API			2

/** Client requested an API version that the driver can't do. */
#define __DRI_CTX_ERROR_BAD_VERSION		3

/** Client requested a flag or combination of flags the driver can't do. */
#define __DRI_CTX_ERROR_BAD_FLAG		4

/** Client requested an attribute the driver doesn't understand. */
#define __DRI_CTX_ERROR_UNKNOWN_ATTRIBUTE	5

/** Client requested a flag the driver doesn't understand. */
#define __DRI_CTX_ERROR_UNKNOWN_FLAG		6
/*@}*/


/**
 * This extension provides functionality to enable various EGLImage
 * extensions.
 */
/* __DRI_IMAGE_FORMAT_* tokens are no longer exported */

#define __DRI_IMAGE_USE_SHARE		0x0001
#define __DRI_IMAGE_USE_SCANOUT		0x0002
#define __DRI_IMAGE_USE_CURSOR		0x0004 /* Deprecated */
#define __DRI_IMAGE_USE_LINEAR		0x0008
/* The buffer will only be read by an external process after SwapBuffers,
 * in contrary to gbm buffers, front buffers and fake front buffers, which
 * could be read after a flush."
 */
#define __DRI_IMAGE_USE_BACKBUFFER      0x0010
#define __DRI_IMAGE_USE_PROTECTED       0x0020
#define __DRI_IMAGE_USE_PRIME_BUFFER    0x0040
#define __DRI_IMAGE_USE_FRONT_RENDERING 0x0080


#define __DRI_IMAGE_TRANSFER_READ            0x1
#define __DRI_IMAGE_TRANSFER_WRITE           0x2
#define __DRI_IMAGE_TRANSFER_READ_WRITE      \
        (__DRI_IMAGE_TRANSFER_READ | __DRI_IMAGE_TRANSFER_WRITE)

/**
 * Extra fourcc formats used internally to Mesa with createImageFromNames.
 * The externally-available fourccs are defined by drm_fourcc.h (DRM_FORMAT_*)
 * and WL_DRM_FORMAT_* from wayland_drm.h.
 *
 * \since 5
 */

#define __DRI_IMAGE_FOURCC_SARGB8888	0x83324258
#define __DRI_IMAGE_FOURCC_SABGR8888	0x84324258
#define __DRI_IMAGE_FOURCC_SXRGB8888	0x85324258

/**
 * Queryable on images created by createImageFromNames.
 *
 * RGB and RGBA might be usable directly as images, but it's still
 * recommended to call fromPlanar with plane == 0.
 *
 * Y_U_V, Y_UV,Y_XUXV and Y_UXVX all requires call to fromPlanar to create
 * usable sub-images, sampling from images return raw YUV data and
 * color conversion needs to be done in the shader.
 *
 * \since 5
 */

#define __DRI_IMAGE_COMPONENTS_RGB	0x3001
#define __DRI_IMAGE_COMPONENTS_RGBA	0x3002
#define __DRI_IMAGE_COMPONENTS_Y_U_V	0x3003
#define __DRI_IMAGE_COMPONENTS_Y_UV	0x3004
#define __DRI_IMAGE_COMPONENTS_Y_XUXV	0x3005
#define __DRI_IMAGE_COMPONENTS_Y_UXVX	0x3008
#define __DRI_IMAGE_COMPONENTS_AYUV	0x3009
#define __DRI_IMAGE_COMPONENTS_XYUV	0x300A
#define __DRI_IMAGE_COMPONENTS_R	0x3006
#define __DRI_IMAGE_COMPONENTS_RG	0x3007


/**
 * queryImage attributes
 */

#define __DRI_IMAGE_ATTRIB_STRIDE	0x2000
#define __DRI_IMAGE_ATTRIB_HANDLE	0x2001
#define __DRI_IMAGE_ATTRIB_NAME		0x2002
#define __DRI_IMAGE_ATTRIB_FORMAT	0x2003 /* available in versions 3+ */
#define __DRI_IMAGE_ATTRIB_WIDTH	0x2004 /* available in versions 4+ */
#define __DRI_IMAGE_ATTRIB_HEIGHT	0x2005
#define __DRI_IMAGE_ATTRIB_COMPONENTS	0x2006 /* available in versions 5+ */
#define __DRI_IMAGE_ATTRIB_FD           0x2007 /* available in versions
                                                * 7+. Each query will return a
                                                * new fd. */
#define __DRI_IMAGE_ATTRIB_FOURCC       0x2008 /* available in versions 11 */
#define __DRI_IMAGE_ATTRIB_NUM_PLANES   0x2009 /* available in versions 11 */

#define __DRI_IMAGE_ATTRIB_OFFSET 0x200A /* available in versions 13 */
#define __DRI_IMAGE_ATTRIB_MODIFIER_LOWER 0x200B /* available in versions 14 */
#define __DRI_IMAGE_ATTRIB_MODIFIER_UPPER 0x200C /* available in versions 14 */
#define __DRI_IMAGE_ATTRIB_COMPRESSION_RATE 0x200D /* available in versions 22 */

enum __DRIYUVColorSpace {
   __DRI_YUV_COLOR_SPACE_UNDEFINED = 0,
   __DRI_YUV_COLOR_SPACE_ITU_REC601 = 0x327F,
   __DRI_YUV_COLOR_SPACE_ITU_REC709 = 0x3280,
   __DRI_YUV_COLOR_SPACE_ITU_REC2020 = 0x3281
};

enum __DRISampleRange {
   __DRI_YUV_RANGE_UNDEFINED = 0,
   __DRI_YUV_FULL_RANGE = 0x3282,
   __DRI_YUV_NARROW_RANGE = 0x3283
};

enum __DRIChromaSiting {
   __DRI_YUV_CHROMA_SITING_UNDEFINED = 0,
   __DRI_YUV_CHROMA_SITING_0 = 0x3284,
   __DRI_YUV_CHROMA_SITING_0_5 = 0x3285
};

enum __DRIFixedRateCompression {
  __DRI_FIXED_RATE_COMPRESSION_NONE = 0x34B1,
  __DRI_FIXED_RATE_COMPRESSION_DEFAULT = 0x34B2,

  __DRI_FIXED_RATE_COMPRESSION_1BPC = 0x34B4,
  __DRI_FIXED_RATE_COMPRESSION_2BPC = 0x34B5,
  __DRI_FIXED_RATE_COMPRESSION_3BPC = 0x34B6,
  __DRI_FIXED_RATE_COMPRESSION_4BPC = 0x34B7,
  __DRI_FIXED_RATE_COMPRESSION_5BPC = 0x34B8,
  __DRI_FIXED_RATE_COMPRESSION_6BPC = 0x34B9,
  __DRI_FIXED_RATE_COMPRESSION_7BPC = 0x34BA,
  __DRI_FIXED_RATE_COMPRESSION_8BPC = 0x34BB,
  __DRI_FIXED_RATE_COMPRESSION_9BPC = 0x34BC,
  __DRI_FIXED_RATE_COMPRESSION_10BPC = 0x34BD,
  __DRI_FIXED_RATE_COMPRESSION_11BPC = 0x34BE,
  __DRI_FIXED_RATE_COMPRESSION_12BPC = 0x34BF,
};

/**
 * \name Reasons that __DRIimageExtensionRec::createImageFromTexture or
 * __DRIimageExtensionRec::createImageFromDmaBufs might fail
 */
/*@{*/
/** Success! */
#define __DRI_IMAGE_ERROR_SUCCESS       0

/** Memory allocation failure */
#define __DRI_IMAGE_ERROR_BAD_ALLOC     1

/** Client requested an invalid attribute */
#define __DRI_IMAGE_ERROR_BAD_MATCH     2

/** Client requested an invalid texture object */
#define __DRI_IMAGE_ERROR_BAD_PARAMETER 3

/** Client requested an invalid pitch and/or offset */
#define __DRI_IMAGE_ERROR_BAD_ACCESS    4
/*@}*/

/**
 * \name Capabilities that might be returned by __DRIimageExtensionRec::getCapabilities
 */
/*@{*/
#define __DRI_IMAGE_CAP_GLOBAL_NAMES 1
/*@}*/

/**
 * blitImage flags
 */

#define __BLIT_FLAG_FLUSH		0x0001
#define __BLIT_FLAG_FINISH		0x0002

/**
 * Flags for createImageFromDmaBufs
 */
#define __DRI_IMAGE_PROTECTED_CONTENT_FLAG 0x00000001
#define __DRI_IMAGE_PRIME_LINEAR_BUFFER    0x00000002

/**
 * queryDmaBufFormatModifierAttribs attributes
 */

/* Available in version 16 */
#define __DRI_IMAGE_FORMAT_MODIFIER_ATTRIB_PLANE_COUNT   0x0001

typedef struct {
    __DRIextension base;

    void (*destroyImage)(struct dri_image *image);

   unsigned char (*queryImage)(struct dri_image *image, int attrib, int *value);

   /**
    * The new struct dri_image will share the content with the old one, see dup(2).
    */
   struct dri_image *(*dupImage)(struct dri_image *image, void *loaderPrivate);

   /**
    * Validate that a struct dri_image can be used a certain way.
    *
    * \since 2
    */
   unsigned char (*validateUsage)(struct dri_image *image, unsigned int use);

   /**
    * Create an image from a series of GEM names; uses FourCC for format
    * and byte stride.
    *
    * \since 5
    */
   struct dri_image *(*createImageFromNames)(struct dri_screen *screen,
                                       int width, int height, int fourcc,
                                       int *names, int num_names,
                                       int *strides, int *offsets,
                                       void *loaderPrivate);

   /**
    * Create an image out of a sub-region of a parent image.  This
    * entry point lets us create individual dri_image structures for different
    * planes in a planar buffer (typically yuv), for example.  While a
    * sub-image shares the underlying buffer object with the parent
    * image and other sibling sub-images, the life times of parent and
    * sub-images are not dependent.  Destroying the parent or a
    * sub-image doesn't affect other images.  The underlying buffer
    * object is free when no struct dri_image remains that references it.
    *
    * Sub-images may overlap, but rendering to overlapping sub-images
    * is undefined.
    *
    * \since 5
    */
    struct dri_image *(*fromPlanar)(struct dri_image *image, int plane,
                              void *loaderPrivate);

    /**
     * Create image from texture.
     *
     * \since 6
     */
   struct dri_image *(*createImageFromTexture)(struct dri_context *context,
                                         int target,
                                         unsigned texture,
                                         int depth,
                                         int level,
                                         unsigned *error,
                                         void *loaderPrivate);

   /**
    * Blit a part of a struct dri_image to another and flushes
    *
    * flush_flag:
    *    0:                  no flush
    *    __BLIT_FLAG_FLUSH:  flush after the blit operation
    *    __BLIT_FLAG_FINISH: flush and wait the blit finished
    *
    * \since 9
    */
   void (*blitImage)(struct dri_context *context, struct dri_image *dst, struct dri_image *src,
                     int dstx0, int dsty0, int dstwidth, int dstheight,
                     int srcx0, int srcy0, int srcwidth, int srcheight,
                     int flush_flag);

   /**
    * Query for general capabilities of the driver that concern
    * buffer sharing and image importing.
    *
    * \since 10
    */
   int (*getCapabilities)(struct dri_screen *screen);

   /**
    * Returns a map of the specified region of a struct dri_image for the specified usage.
    *
    * flags may include __DRI_IMAGE_TRANSFER_READ, which will populate the
    * mapping with the current buffer content. If __DRI_IMAGE_TRANSFER_READ
    * is not included in the flags, the buffer content at map time is
    * undefined. Users wanting to modify the mapping must include
    * __DRI_IMAGE_TRANSFER_WRITE; if __DRI_IMAGE_TRANSFER_WRITE is not
    * included, behaviour when writing the mapping is undefined.
    *
    * Returns the byte stride in *stride, and an opaque pointer to data
    * tracking the mapping in **data, which must be passed to unmapImage().
    *
    * \since 12
    */
   void *(*mapImage)(struct dri_context *context, struct dri_image *image,
                     int x0, int y0, int width, int height,
                     unsigned int flags, int *stride, void **data);

   /**
    * Unmap a previously mapped struct dri_image
    *
    * \since 12
    */
   void (*unmapImage)(struct dri_context *context, struct dri_image *image, void *data);

   /*
    * dmabuf format query to support EGL_EXT_image_dma_buf_import_modifiers.
    *
    * \param max      Maximum number of formats that can be accomodated into
    *                 \param formats. If zero, no formats are returned -
    *                 instead, the driver returns the total number of
    *                 supported dmabuf formats in \param count.
    * \param formats  Buffer to fill formats into.
    * \param count    Count of formats returned, or, total number of
    *                 supported formats in case \param max is zero.
    *
    * Returns true on success.
    *
    * \since 15
    */
   bool (*queryDmaBufFormats)(struct dri_screen *screen, int max, int *formats,
                              int *count);

   /*
    * dmabuf format modifier query for a given format to support
    * EGL_EXT_image_dma_buf_import_modifiers.
    *
    * \param fourcc    The format to query modifiers for. If this format
    *                  is not supported by the driver, return false.
    * \param max       Maximum number of modifiers that can be accomodated in
    *                  \param modifiers. If zero, no modifiers are returned -
    *                  instead, the driver returns the total number of
    *                  modifiers for \param format in \param count.
    * \param modifiers Buffer to fill modifiers into.
    * \param count     Count of the modifiers returned, or, total number of
    *                  supported modifiers for \param fourcc in case
    *                  \param max is zero.
    *
    * Returns true upon success.
    *
    * \since 15
    */
   bool (*queryDmaBufModifiers)(struct dri_screen *screen, int fourcc, int max,
                                uint64_t *modifiers,
                                unsigned int *external_only, int *count);

   /**
    * dmabuf format modifier attribute query for a given format and modifier.
    *
    * \param fourcc    The format to query. If this format is not supported by
    *                  the driver, return false.
    * \param modifier  The modifier to query. If this format+modifier is not
    *                  supported by the driver, return false.
    * \param attrib    The __DRI_IMAGE_FORMAT_MODIFIER_ATTRIB to query.
    * \param value     A pointer to where to store the result of the query.
    *
    * Returns true upon success.
    *
    * \since 16
    */
   bool (*queryDmaBufFormatModifierAttribs)(struct dri_screen *screen,
                                            uint32_t fourcc, uint64_t modifier,
                                            int attrib, uint64_t *value);

   /**
    * Create a DRI image from the given renderbuffer.
    *
    * \param context       the current DRI context
    * \param renderbuffer  the GL name of the renderbuffer
    * \param loaderPrivate for callbacks into the loader related to the image
    * \param error         will be set to one of __DRI_IMAGE_ERROR_xxx
    * \return the newly created image on success, or NULL otherwise
    */
    struct dri_image *(*createImageFromRenderbuffer)(struct dri_context *context,
                                               int renderbuffer,
                                               void *loaderPrivate,
                                               unsigned *error);

   /**
    * Creates a DRI image from an array of dmabuf fds and their modifiers.
    *
    * See __DRI_IMAGE_*_FLAG for valid definitions of flags.
    */
   struct dri_image *(*createImageFromDmaBufs)(struct dri_screen *screen,
                                         int width, int height, int fourcc,
                                         uint64_t modifier,
                                         int *fds, int num_fds,
                                         int *strides, int *offsets,
                                         enum __DRIYUVColorSpace color_space,
                                         enum __DRISampleRange sample_range,
                                         enum __DRIChromaSiting horiz_siting,
                                         enum __DRIChromaSiting vert_siting,
                                         uint32_t flags,
                                         unsigned *error,
                                         void *loaderPrivate);

   /**
    * Creates an image with implementation's favorite modifiers and the
    * provided usage flags.
    *
    * Passing either zero modifiers, or a modifier list consisting only
    * of DRM_FORMAT_MOD_INVALID, allows the implementation to select a
    * layout with implicit modifiers.
    *
    * The created image should be destroyed with destroyImage().
    *
    * Returns the new DRIimage. The chosen modifier can be obtained later on
    * and passed back to things like the kernel's AddFB2 interface.
    *
    * \since 19
    */
   struct dri_image *(*createImage)(struct dri_screen *screen,
                              int width, int height, int format,
                              const uint64_t *modifiers,
                              const unsigned int modifier_count,
                              unsigned int use,
                              void *loaderPrivate);

   /**
    * Set an in-fence-fd on the image.  If a fence-fd is already set
    * (but not yet consumed), the existing and new fence will be merged
    *
    * This does *not* take ownership of the fd.  The fd does not need
    * to be kept alive once the call has returned.
    *
    * \since 21
    */
   void (*setInFenceFd)(struct dri_image *image, int fd);

   /*
    * Query supported compression rates for a given format for
    * EGL_EXT_surface_compression.
    *
    * \param config   Config for which to query the supported compression
    *                 rates.
    * \param max      Maximum number of rates that can be accomodated into
    *                 \param rates. If zero, no rates are returned -
    *                 instead, the driver returns the total number of
    *                 supported compression rates in \param count.
    * \param rates    Buffer to fill rates into.
    * \param count    Count of rates returned, or, total number of
    *                 supported rates in case \param max is zero.
    *
    * Returns true on success.
    *
    * \since 22
    */
   bool (*queryCompressionRates)(struct dri_screen *screen, const struct dri_config *config,
                                 int max, enum __DRIFixedRateCompression *rates,
                                 int *count);

   /*
    * Query list of modifiers that are associated with given fixed-rate
    * compression bitrate.
    *
    * \param format    The format to query
    * \param rate      Compression rate to query for
    * \param max       Maximum number of modifiers that can be accomodated in
    *                  \param modifiers. If zero, no modifiers are returned -
    *                  instead, the driver returns the total number of
    *                  modifiers for \param format in \param count.
    * \param modifiers Buffer to fill modifiers into.
    * \param count     Count of the modifiers returned, or, total number of
    *                  supported modifiers for \param fourcc in case
    *                  \param max is zero.
    *
    * Returns true on success.
    *
    * \since 22
    */
   bool (*queryCompressionModifiers)(struct dri_screen *screen, uint32_t format,
                                     enum __DRIFixedRateCompression rate,
                                     int max, uint64_t *modifiers, int *count);
} __DRIimageExtension;


/**
 * This extension must be implemented by the loader and passed to the
 * driver at screen creation time.  The EGLImage entry points in the
 * various client APIs take opaque EGLImage handles and use this
 * extension to map them to a struct dri_image.  At version 1, this
 * extensions allows mapping EGLImage pointers to struct dri_image pointers,
 * but future versions could support other EGLImage-like, opaque types
 * with new lookup functions.
 */
#define __DRI_IMAGE_LOOKUP "DRI_IMAGE_LOOKUP"

typedef struct {
    __DRIextension base;

    /**
     * Check if EGLImage is associated with the EGL display before lookup with
     * lookupEGLImageValidated(). It will hold EGLDisplay.Mutex, so is separated
     * out from lookupEGLImageValidated() to avoid deadlock.
     */
    unsigned char (*validateEGLImage)(void *image, void *loaderPrivate);

    /**
     * Lookup EGLImage after validateEGLImage(). No lock in this function.
     */
    struct dri_image *(*lookupEGLImageValidated)(void *image, void *loaderPrivate);
} __DRIimageLookupExtension;

/**
 * This extension allows for common DRI2 options
 */
#define __DRI2_CONFIG_QUERY "DRI_CONFIG_QUERY"

typedef struct {
   __DRIextension base;

   int (*configQueryb)(struct dri_screen *screen, const char *var, unsigned char *val);
   int (*configQueryi)(struct dri_screen *screen, const char *var, int *val);
   int (*configQueryf)(struct dri_screen *screen, const char *var, float *val);
   int (*configQuerys)(struct dri_screen *screen, const char *var, char **val);
} __DRI2configQueryExtension;

/**
 * DRI config options extension.
 *
 * This extension provides the XML string containing driver options for use by
 * the loader in supporting the driconf application.
 *
 * v2:
 * - Add the getXml getter function which allows the driver more flexibility in
 *   how the XML is provided.
 * - Deprecate the direct xml pointer. It is only provided as a fallback for
 *   older versions of libGL and must not be used by clients that are aware of
 *   the newer version. Future driver versions may set it to NULL.
 */
#define __DRI_CONFIG_OPTIONS "DRI_ConfigOptions"

typedef struct {
   __DRIextension base;
   const char *xml; /**< deprecated since v2, use getXml instead */

   /**
    * Get an XML string that describes available driver options for use by a
    * config application.
    *
    * The returned string must be heap-allocated. The caller is responsible for
    * freeing it.
    */
   char *(*getXml)(const char *driver_name);
} __DRIconfigOptionsExtension;

/**
 * Query renderer driver extension
 *
 * This allows the window system layer (either EGL or GLX) to query aspects of
 * hardware and driver support without creating a context.
 */
#define __DRI2_RENDERER_VENDOR_ID                             0x0000
#define __DRI2_RENDERER_DEVICE_ID                             0x0001
#define __DRI2_RENDERER_VERSION                               0x0002
#define __DRI2_RENDERER_ACCELERATED                           0x0003
#define __DRI2_RENDERER_VIDEO_MEMORY                          0x0004
#define __DRI2_RENDERER_UNIFIED_MEMORY_ARCHITECTURE           0x0005
#define __DRI2_RENDERER_PREFERRED_PROFILE                     0x0006
#define __DRI2_RENDERER_OPENGL_CORE_PROFILE_VERSION           0x0007
#define __DRI2_RENDERER_OPENGL_COMPATIBILITY_PROFILE_VERSION  0x0008
#define __DRI2_RENDERER_OPENGL_ES_PROFILE_VERSION             0x0009
#define __DRI2_RENDERER_OPENGL_ES2_PROFILE_VERSION            0x000a

#define __DRI2_RENDERER_PREFER_BACK_BUFFER_REUSE              0x000f

/**
 * Image Loader extension. Drivers use this to allocate color buffers
 */

/**
 * See __DRIimageLoaderExtensionRec::getBuffers::buffer_mask.
 */
enum __DRIimageBufferMask {
   __DRI_IMAGE_BUFFER_BACK = (1 << 0),
   __DRI_IMAGE_BUFFER_FRONT = (1 << 1),

   /**
    * A buffer shared between application and compositor. The buffer may be
    * simultaneously accessed by each.
    *
    * A shared buffer is equivalent to an EGLSurface whose EGLConfig contains
    * EGL_MUTABLE_RENDER_BUFFER_BIT_KHR and whose active EGL_RENDER_BUFFER (as
    * opposed to any pending, requested change to EGL_RENDER_BUFFER) is
    * EGL_SINGLE_BUFFER.
    *
    * If buffer_mask contains __DRI_IMAGE_BUFFER_SHARED, then must contains no
    * other bits. As a corollary, a struct dri_drawable that has a "shared" buffer
    * has no front nor back buffer.
    *
    * The loader returns __DRI_IMAGE_BUFFER_SHARED in buffer_mask if and only
    * if:
    *     - The loader supports __DRI_MUTABLE_RENDER_BUFFER_LOADER.
    *     - The driver supports __DRI_MUTABLE_RENDER_BUFFER_DRIVER.
    *     - The EGLConfig of the drawable EGLSurface contains
    *       EGL_MUTABLE_RENDER_BUFFER_BIT_KHR.
    *     - The EGLContext's EGL_RENDER_BUFFER is EGL_SINGLE_BUFFER.
    *       Equivalently, the EGLSurface's active EGL_RENDER_BUFFER (as
    *       opposed to any pending, requested change to EGL_RENDER_BUFFER) is
    *       EGL_SINGLE_BUFFER. (See the EGL 1.5 and
    *       EGL_KHR_mutable_render_buffer spec for details about "pending" vs
    *       "active" EGL_RENDER_BUFFER state).
    *
    * A shared buffer is similar to a front buffer in that all rendering to the
    * buffer should appear promptly on the screen. It is different from
    * a front buffer in that its behavior is independent from the
    * GL_DRAW_BUFFER state. Specifically, if GL_DRAW_FRAMEBUFFER is 0 and the
    * struct dri_drawable's buffer_mask is __DRI_IMAGE_BUFFER_SHARED, then all
    * rendering should appear promptly on the screen if GL_DRAW_BUFFER is not
    * GL_NONE.
    *
    * The difference between a shared buffer and a front buffer is motivated
    * by the constraints of Android and OpenGL ES. OpenGL ES does not support
    * front-buffer rendering. Android's SurfaceFlinger protocol provides the
    * EGL driver only a back buffer and no front buffer. The shared buffer
    * mode introduced by EGL_KHR_mutable_render_buffer is a backdoor though
    * EGL that allows Android OpenGL ES applications to render to what is
    * effectively the front buffer, a backdoor that required no change to the
    * OpenGL ES API and little change to the SurfaceFlinger API.
    */
   __DRI_IMAGE_BUFFER_SHARED = (1 << 2),
};

struct __DRIimageList {
   uint32_t image_mask;
   struct dri_image *back;
   struct dri_image *front;
};

#define __DRI_IMAGE_LOADER "DRI_IMAGE_LOADER"

typedef struct {
    __DRIextension base;

   /**
    * Allocate color buffers.
    *
    * \param driDrawable
    * \param width              Width of allocated buffers
    * \param height             Height of allocated buffers
    * \param format             one of __DRI_IMAGE_FORMAT_*
    * \param stamp              Address of variable to be updated when
    *                           getBuffers must be called again
    * \param loaderPrivate      The loaderPrivate for driDrawable
    * \param buffer_mask        Set of buffers to allocate. A bitmask of
    *                           __DRIimageBufferMask.
    * \param buffers            Returned buffers
    */
   int (*getBuffers)(struct dri_drawable *driDrawable,
                     unsigned int format,
                     uint32_t *stamp,
                     void *loaderPrivate,
                     uint32_t buffer_mask,
                     struct __DRIimageList *buffers);

    /**
     * Flush pending front-buffer rendering
     *
     * Any rendering that has been performed to the
     * fake front will be flushed to the front
     *
     * \param driDrawable    Drawable whose front-buffer is to be flushed
     * \param loaderPrivate  Loader's private data
     */
    void (*flushFrontBuffer)(struct dri_drawable *driDrawable, void *loaderPrivate);

    /**
     * Return a loader capability value. If the loader doesn't know the enum,
     * it will return 0.
     *
     * \since 2
     */
    unsigned (*getCapability)(void *loaderPrivate, enum dri_loader_cap cap);

    /**
     * Flush swap buffers
     *
     * Make sure any outstanding swap buffers have been submitted to the
     * device.
     *
     * \param driDrawable    Drawable whose swaps need to be flushed
     * \param loaderPrivate  Loader's private data
     *
     * \since 3
     */
    void (*flushSwapBuffers)(struct dri_drawable *driDrawable, void *loaderPrivate);

    /**
     * Clean up any loader state associated with an image.
     *
     * \param loaderPrivate  Loader's private data that was previously passed
     *                       into a __DRIimageExtensionRec::createImage function
     * \since 4
     */
    void (*destroyLoaderImageState)(void *loaderPrivate);
} __DRIimageLoaderExtension;

/**
 * Main DRI3 interface extension.
 *
 * Not used by the X server.
 */

typedef struct {
   __DRIextension               base;

   /* Common DRI functions, shared with DRI2 */
   __DRIcreateNewScreen2Func            createNewScreen2;
   __DRIcreateNewDrawableFunc           createNewDrawable;
   __DRIcreateContextAttribsFunc        createContextAttribs;
   __DRIgetAPIMaskFunc                  getAPIMask;
   __DRIcreateNewScreen3Func            createNewScreen3;
} __DRIimageDriverExtension;

/**
 * Background callable loader extension.
 *
 * Loaders expose this extension to indicate to drivers that they are capable
 * of handling callbacks from the driver's background drawing threads.
 */
#define __DRI_BACKGROUND_CALLABLE "DRI_BackgroundCallable"

typedef struct {
   __DRIextension base;

   /**
    * Indicate that this thread is being used by the driver as a background
    * drawing thread which may make callbacks to the loader.
    *
    * \param loaderPrivate is the value that was passed to to the driver when
    * the context was created.  This can be used by the loader to identify
    * which context any callbacks are associated with.
    *
    * If this function is called more than once from any given thread, each
    * subsequent call overrides the loaderPrivate data that was passed in the
    * previous call.  The driver can take advantage of this to re-use a
    * background thread to perform drawing on behalf of multiple contexts.
    *
    * It is permissible for the driver to call this function from a
    * non-background thread (i.e. a thread that has already been bound to a
    * context using __DRIcoreExtension::bindContext()); when this happens,
    * the \c loaderPrivate pointer must be equal to the pointer that was
    * passed to the driver when the currently bound context was created.
    *
    * This call should execute quickly enough that the driver can call it with
    * impunity whenever a background thread starts performing drawing
    * operations (e.g. it should just set a thread-local variable).
    */
   void (*setBackgroundContext)(void *loaderPrivate);

   /**
    * Indicate that it is multithread safe to use glthread.  For GLX/EGL
    * platforms using Xlib, that involves calling XInitThreads, before
    * opening an X display.
    *
    * Note: only supported if extension version is at least 2.
    *
    * \param loaderPrivate is the value that was passed to to the driver when
    * the context was created.  This can be used by the loader to identify
    * which context any callbacks are associated with.
    */
   unsigned char (*isThreadSafe)(void *loaderPrivate);
} __DRIbackgroundCallableExtension;

/**
 * The loader portion of EGL_KHR_mutable_render_buffer.
 *
 * Requires loader extension DRI_IMAGE_LOADER, through which the loader sends
 * __DRI_IMAGE_BUFFER_SHARED to the driver.
 *
 * Not used by the X server.
 *
 * \see __DRI_MUTABLE_RENDER_BUFFER_DRIVER
 */
#define __DRI_MUTABLE_RENDER_BUFFER_LOADER "DRI_MutableRenderBufferLoader"

typedef struct {
   __DRIextension base;

   /**
    * Inform the display engine (that is, SurfaceFlinger and/or hwcomposer)
    * that the struct dri_drawable has new content.
    *
    * The display engine may ignore this call, for example, if it continually
    * refreshes and displays the buffer on every frame, as in
    * EGL_ANDROID_front_buffer_auto_refresh. On the other extreme, the display
    * engine may refresh and display the buffer only in frames in which the
    * driver calls this.
    *
    * If the fence_fd is not -1, then the display engine will display the
    * buffer only after the fence signals.
    *
    * The drawable's current __DRIimageBufferMask, as returned by
    * __DRIimageLoaderExtension::getBuffers(), must be
    * __DRI_IMAGE_BUFFER_SHARED.
    */
   void (*displaySharedBuffer)(struct dri_drawable *drawable, int fence_fd,
                               void *loaderPrivate);
} __DRImutableRenderBufferLoaderExtension;

/* Mesa-internal interface between the GLX, GBM, and EGL DRI driver loaders, and
 * the gallium dri_util.c code.
 */

#define __DRI_MESA "DRI_Mesa"

/**  Core struct that appears alongside __DRI_CORE for Mesa-internal usage.
 * Implemented in the top-level dri/drisw/kopper extension list.
 */
typedef struct {
   __DRIextension base;

   /* Version string for verifying that the DRI driver is from the same build as
    * the loader.
    */
#define MESA_INTERFACE_VERSION_STRING PACKAGE_VERSION MESA_GIT_SHA1
   const char *version_string;


   __DRIcreateContextAttribsFunc createContext;

   /* driver function for finishing initialization inside createNewScreen(). */
   const struct dri_config **(*initScreen)(struct dri_screen *screen, bool driver_name_is_inferred);

   int (*queryCompatibleRenderOnlyDeviceFd)(int kms_only_fd);

   /* Screen creation function regardless of DRI2, image, or swrast backend.
    * (Nothing uses the old __DRI_CORE screen create).
    *
    * If not associated with a DRM fd (non-swkms swrast), the fd argument should
    * be -1.
    */
   /* version 2 */
   __DRIcreateNewScreen3Func createNewScreen3;
} __DRImesaCoreExtension;

#endif /* MESA_INTERFACE_H */
