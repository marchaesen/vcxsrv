/*
 * Copyright 2012-2014, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *      Artur Wyszynski, harakash@gmail.com
 *      Alexander von Gluck IV, kallisti5@unixzen.com
 */

#include "hgl_context.h"

#include <stdio.h>

#include "pipe/p_format.h"
#include "util/u_atomic.h"
#include "util/format/u_format.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "state_tracker/st_gl_api.h" /* for st_gl_api_create */

#include "GLView.h"


#ifdef DEBUG
#   define TRACE(x...) printf("hgl:frontend: " x)
#   define CALLED() TRACE("CALLED: %s\n", __PRETTY_FUNCTION__)
#else
#   define TRACE(x...)
#   define CALLED()
#endif
#define ERROR(x...) printf("hgl:frontend: " x)


// Perform a safe void to hgl_context cast
static inline struct hgl_context*
hgl_st_context(struct st_context_iface *stctxi)
{
	struct hgl_context* context;
	assert(stctxi);
	context = (struct hgl_context*)stctxi->st_manager_private;
	assert(context);
	return context;
}


// Perform a safe void to hgl_buffer cast
//static inline struct hgl_buffer*
struct hgl_buffer*
hgl_st_framebuffer(struct st_framebuffer_iface *stfbi)
{
	struct hgl_buffer* buffer;
	assert(stfbi);
	buffer = (struct hgl_buffer*)stfbi->st_manager_private;
	assert(buffer);
	return buffer;
}


static bool
hgl_st_framebuffer_flush_front(struct st_context_iface *stctxi,
	struct st_framebuffer_iface* stfbi, enum st_attachment_type statt)
{
	CALLED();

	//struct hgl_context* context = hgl_st_context(stctxi);
	// struct hgl_buffer* buffer = hgl_st_context(stfbi);
	struct hgl_buffer* buffer = hgl_st_framebuffer(stfbi);
	//buffer->surface

	#if 0
	struct stw_st_framebuffer *stwfb = stw_st_framebuffer(stfb);
	mtx_lock(&stwfb->fb->mutex);

	struct pipe_resource* resource = textures[statt];
	if (resource)
		stw_framebuffer_present_locked(...);
	#endif

	return true;
}


static bool
hgl_st_framebuffer_validate_textures(struct st_framebuffer_iface *stfbi,
	unsigned width, unsigned height, unsigned mask)
{
	struct hgl_buffer* buffer;
	enum st_attachment_type i;
	struct pipe_resource templat;

	CALLED();

	buffer = hgl_st_framebuffer(stfbi);

	if (buffer->width != width || buffer->height != height) {
		for (i = 0; i < ST_ATTACHMENT_COUNT; i++)
			pipe_resource_reference(&buffer->textures[i], NULL);
	}

	memset(&templat, 0, sizeof(templat));
	templat.target = buffer->target;
	templat.width0 = width;
	templat.height0 = height;
	templat.depth0 = 1;
	templat.array_size = 1;
	templat.last_level = 0;

	for (i = 0; i < ST_ATTACHMENT_COUNT; i++) {
		enum pipe_format format;
		unsigned bind;

		switch (i) {
			case ST_ATTACHMENT_FRONT_LEFT:
			case ST_ATTACHMENT_BACK_LEFT:
			case ST_ATTACHMENT_FRONT_RIGHT:
			case ST_ATTACHMENT_BACK_RIGHT:
				format = buffer->visual->color_format;
				bind = PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_RENDER_TARGET;
				break;
			case ST_ATTACHMENT_DEPTH_STENCIL:
				format = buffer->visual->depth_stencil_format;
				bind = PIPE_BIND_DEPTH_STENCIL;
				break;
			default:
				format = PIPE_FORMAT_NONE;
				bind = 0;
				break;
		}

		if (format != PIPE_FORMAT_NONE) {
			templat.format = format;
			templat.bind = bind;
			buffer->textures[i] = buffer->screen->resource_create(buffer->screen,
				&templat);
			if (!buffer->textures[i])
				return FALSE;
		}
	}

	buffer->width = width;
	buffer->height = height;
	buffer->mask = mask;

	return true;
}


/**
 * Called by the st manager to validate the framebuffer (allocate
 * its resources).
 */
static bool
hgl_st_framebuffer_validate(struct st_context_iface *stctxi,
	struct st_framebuffer_iface *stfbi, const enum st_attachment_type *statts,
	unsigned count, struct pipe_resource **out)
{
	struct hgl_context* context;
	struct hgl_buffer* buffer;
	unsigned stAttachmentMask, newMask;
	unsigned i;
	bool resized;

	CALLED();

	context = hgl_st_context(stctxi);
	buffer = hgl_st_framebuffer(stfbi);

	//int32 width = 0;
	//int32 height = 0;
	//get_bitmap_size(context->bitmap, &width, &height);

	// Build mask of current attachments
	stAttachmentMask = 0;
	for (i = 0; i < count; i++)
		stAttachmentMask |= 1 << statts[i];

	newMask = stAttachmentMask & ~buffer->mask;

	resized = (buffer->width != context->width)
		|| (buffer->height != context->height);

	if (resized || newMask) {
		boolean ret;
		TRACE("%s: resize event. old:  %d x %d; new: %d x %d\n", __func__,
			buffer->width, buffer->height, context->width, context->height);

		ret = hgl_st_framebuffer_validate_textures(stfbi, 
			context->width, context->height, stAttachmentMask);
		
		if (!ret)
			return ret;

		// TODO: Simply update attachments
		//if (!resized) {

		//}
	}

	for (i = 0; i < count; i++)
		pipe_resource_reference(&out[i], buffer->textures[statts[i]]);

	return true;
}


static int
hgl_st_manager_get_param(struct st_manager *smapi, enum st_manager_param param)
{
	CALLED();

	switch (param) {
		case ST_MANAGER_BROKEN_INVALIDATE:
			return 1;
	}

	return 0;
}


/**
 * Create new framebuffer
 */
struct hgl_buffer *
hgl_create_st_framebuffer(struct hgl_context* context)
{
	struct hgl_buffer *buffer;
	CALLED();

	// Our requires before creating a framebuffer
	assert(context);
	assert(context->screen);
	assert(context->stVisual);

	buffer = CALLOC_STRUCT(hgl_buffer);
	assert(buffer);

	// calloc and configure our st_framebuffer interface
	buffer->stfbi = CALLOC_STRUCT(st_framebuffer_iface);
	assert(buffer->stfbi);

	// Prepare our buffer
	buffer->visual = context->stVisual;
	buffer->screen = context->screen;

	if (context->screen->get_param(buffer->screen, PIPE_CAP_NPOT_TEXTURES))
		buffer->target = PIPE_TEXTURE_2D;
	else
		buffer->target = PIPE_TEXTURE_RECT;

	// Prepare our frontend interface
	buffer->stfbi->flush_front = hgl_st_framebuffer_flush_front;
	buffer->stfbi->validate = hgl_st_framebuffer_validate;
	buffer->stfbi->visual = context->stVisual;

	p_atomic_set(&buffer->stfbi->stamp, 1);
	buffer->stfbi->st_manager_private = (void*)buffer;

	return buffer;
}


struct st_api*
hgl_create_st_api()
{
	CALLED();
	return st_gl_api_create();
}


struct st_manager *
hgl_create_st_manager(struct hgl_context* context)
{
	struct st_manager* manager;

	CALLED();

	// Required things
	assert(context);
	assert(context->screen);

	manager = CALLOC_STRUCT(st_manager);
	assert(manager);

	//manager->display = dpy;
	manager->screen = context->screen;
	manager->get_param = hgl_st_manager_get_param;
	manager->st_manager_private = (void *)context;
	
	return manager;
}


void
hgl_destroy_st_manager(struct st_manager *manager)
{
	CALLED();

	FREE(manager);
}


struct st_visual*
hgl_create_st_visual(ulong options)
{
	struct st_visual* visual;

	CALLED();

	visual = CALLOC_STRUCT(st_visual);
	assert(visual);

	// Determine color format
	if ((options & BGL_INDEX) != 0) {
		// Index color
		visual->color_format = PIPE_FORMAT_B5G6R5_UNORM;
		// TODO: Indexed color depth buffer?
		visual->depth_stencil_format = PIPE_FORMAT_NONE;
	} else {
		// RGB color
		visual->color_format = (options & BGL_ALPHA)
			? PIPE_FORMAT_BGRA8888_UNORM : PIPE_FORMAT_BGRX8888_UNORM;
		// TODO: Determine additional stencil formats
		visual->depth_stencil_format = (options & BGL_DEPTH)
			? PIPE_FORMAT_Z24_UNORM_S8_UINT : PIPE_FORMAT_NONE;
    }

	visual->accum_format = (options & BGL_ACCUM)
		? PIPE_FORMAT_R16G16B16A16_SNORM : PIPE_FORMAT_NONE;

	visual->buffer_mask |= ST_ATTACHMENT_FRONT_LEFT_MASK;
	visual->render_buffer = ST_ATTACHMENT_FRONT_LEFT;

	if ((options & BGL_DOUBLE) != 0) {
		visual->buffer_mask |= ST_ATTACHMENT_BACK_LEFT_MASK;
		visual->render_buffer = ST_ATTACHMENT_BACK_LEFT;
	}

	#if 0
	if ((options & BGL_STEREO) != 0) {
		visual->buffer_mask |= ST_ATTACHMENT_FRONT_RIGHT_MASK;
		if ((options & BGL_DOUBLE) != 0)
			visual->buffer_mask |= ST_ATTACHMENT_BACK_RIGHT_MASK;
    }
	#endif

	if ((options & BGL_DEPTH) || (options & BGL_STENCIL))
		visual->buffer_mask |= ST_ATTACHMENT_DEPTH_STENCIL_MASK;

	TRACE("%s: Visual color format: %s\n", __func__,
		util_format_name(visual->color_format));

	return visual;
}


void
hgl_destroy_st_visual(struct st_visual* visual)
{
	CALLED();

	FREE(visual);
}
