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
hgl_st_framebuffer_flush_front(struct st_context_iface* stctxi,
	struct st_framebuffer_iface* stfbi, enum st_attachment_type statt)
{
	CALLED();

	struct hgl_buffer* buffer = hgl_st_framebuffer(stfbi);
	struct pipe_resource* ptex = buffer->textures[statt];

	if (statt != ST_ATTACHMENT_FRONT_LEFT)
		return false;

	if (!ptex)
		return true;

	// TODO: pipe_context here??? Might be needed for hw renderers
	buffer->screen->flush_frontbuffer(buffer->screen, NULL, ptex, 0, 0,
		buffer->winsysContext, NULL);

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
		TRACE("validate_textures: size changed: %d, %d -> %d, %d\n",
			buffer->width, buffer->height, width, height);
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

		if (((1 << i) & buffer->visual->buffer_mask) && buffer->textures[i] == NULL) {
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
				TRACE("resource_create(%d, %d, %d)\n", i, format, bind);
				buffer->textures[i] = buffer->screen->resource_create(buffer->screen,
					&templat);
				if (!buffer->textures[i])
					return FALSE;
			}
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


static uint32_t hgl_fb_ID = 0;

/**
 * Create new framebuffer
 */
struct hgl_buffer *
hgl_create_st_framebuffer(struct hgl_context* context, void *winsysContext)
{
	struct hgl_buffer *buffer;
	CALLED();

	// Our requires before creating a framebuffer
	assert(context);
	assert(context->display);
	assert(context->stVisual);

	buffer = CALLOC_STRUCT(hgl_buffer);
	assert(buffer);

	// calloc and configure our st_framebuffer interface
	buffer->stfbi = CALLOC_STRUCT(st_framebuffer_iface);
	assert(buffer->stfbi);

	// Prepare our buffer
	buffer->visual = context->stVisual;
	buffer->screen = context->display->manager->screen;
	buffer->winsysContext = winsysContext;

	if (buffer->screen->get_param(buffer->screen, PIPE_CAP_NPOT_TEXTURES))
		buffer->target = PIPE_TEXTURE_2D;
	else
		buffer->target = PIPE_TEXTURE_RECT;

	// Prepare our frontend interface
	buffer->stfbi->flush_front = hgl_st_framebuffer_flush_front;
	buffer->stfbi->validate = hgl_st_framebuffer_validate;
	buffer->stfbi->visual = context->stVisual;

	p_atomic_set(&buffer->stfbi->stamp, 1);
	buffer->stfbi->st_manager_private = (void*)buffer;
	buffer->stfbi->ID = p_atomic_inc_return(&hgl_fb_ID);
	buffer->stfbi->state_manager = context->display->manager;

	return buffer;
}


void
hgl_destroy_st_framebuffer(struct hgl_buffer *buffer)
{
	CALLED();

	int i;
	for (i = 0; i < ST_ATTACHMENT_COUNT; i++)
		pipe_resource_reference(&buffer->textures[i], NULL);

	FREE(buffer->stfbi);
	FREE(buffer);
}


struct st_api*
hgl_create_st_api()
{
	CALLED();
	return st_gl_api_create();
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

	if ((options & BGL_DOUBLE) != 0) {
		TRACE("double buffer enabled\n");
		visual->buffer_mask |= ST_ATTACHMENT_BACK_LEFT_MASK;
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


struct hgl_display*
hgl_create_display(struct pipe_screen* screen)
{
	struct hgl_display* display;

	display = CALLOC_STRUCT(hgl_display);
	assert(display);
	display->api = st_gl_api_create();
	display->manager = CALLOC_STRUCT(st_manager);
	assert(display->manager);
	display->manager->screen = screen;
	display->manager->get_param = hgl_st_manager_get_param;
	// display->manager->st_manager_private is used by llvmpipe

	return display;
}


void
hgl_destroy_display(struct hgl_display *display)
{
	if (display->manager->destroy)
		display->manager->destroy(display->manager);
	FREE(display->manager);
	if (display->api->destroy)
		display->api->destroy(display->api);
	FREE(display);
}
