/*
 * Copyright 2012, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Artur Wyszynski, harakash@gmail.com
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 */


#include "GalliumContext.h"

#include <stdio.h>
#include <algorithm>

#include "GLView.h"

#include "bitmap_wrapper.h"

#include "glapi/glapi.h"
#include "pipe/p_format.h"
//#include "state_tracker/st_cb_fbo.h"
//#include "state_tracker/st_cb_flush.h"
#include "state_tracker/st_context.h"
#include "state_tracker/st_gl_api.h"
#include "frontend/sw_winsys.h"
#include "sw/hgl/hgl_sw_winsys.h"
#include "util/u_atomic.h"
#include "util/u_memory.h"
#include "util/u_framebuffer.h"

#include "target-helpers/inline_sw_helper.h"
#include "target-helpers/inline_debug_helper.h"


#ifdef DEBUG
#	define TRACE(x...) printf("GalliumContext: " x)
#	define CALLED() TRACE("CALLED: %s\n", __PRETTY_FUNCTION__)
#else
#	define TRACE(x...)
#	define CALLED()
#endif
#define ERROR(x...) printf("GalliumContext: " x)

int32 GalliumContext::fDisplayRefCount = 0;
hgl_display* GalliumContext::fDisplay = NULL;

GalliumContext::GalliumContext(ulong options)
	:
	fOptions(options),
	fCurrentContext(0)
{
	CALLED();

	// Make all contexts a known value
	for (context_id i = 0; i < CONTEXT_MAX; i++)
		fContext[i] = NULL;

	CreateDisplay();

	(void) mtx_init(&fMutex, mtx_plain);
}


GalliumContext::~GalliumContext()
{
	CALLED();

	// Destroy our contexts
	Lock();
	for (context_id i = 0; i < CONTEXT_MAX; i++)
		DestroyContext(i);
	Unlock();

	DestroyDisplay();

	mtx_destroy(&fMutex);
}


status_t
GalliumContext::CreateDisplay()
{
	CALLED();

	if (atomic_add(&fDisplayRefCount, 1) > 0)
		return B_OK;

	// Allocate winsys and attach callback hooks
	struct sw_winsys* winsys = hgl_create_sw_winsys();

	if (!winsys) {
		ERROR("%s: Couldn't allocate sw_winsys!\n", __func__);
		return B_ERROR;
	}

	struct pipe_screen* screen = sw_screen_create(winsys);

	if (screen == NULL) {
		ERROR("%s: Couldn't create screen!\n", __FUNCTION__);
		winsys->destroy(winsys);
		return B_ERROR;
	}

	debug_screen_wrap(screen);

	const char* driverName = screen->get_name(screen);
	ERROR("%s: Using %s driver.\n", __func__, driverName);

	fDisplay = hgl_create_display(screen);

	if (fDisplay == NULL) {
		ERROR("%s: Couldn't create display!\n", __FUNCTION__);
		screen->destroy(screen); // will also destroy winsys
		return B_ERROR;
	}

	return B_OK;
}


void
GalliumContext::DestroyDisplay()
{
	if (atomic_add(&fDisplayRefCount, -1) > 1)
		return;

	if (fDisplay != NULL) {
		struct pipe_screen* screen = fDisplay->manager->screen;
		hgl_destroy_display(fDisplay); fDisplay = NULL;
		screen->destroy(screen); // destroy will deallocate object
	}
}


context_id
GalliumContext::CreateContext(HGLWinsysContext *wsContext)
{
	CALLED();

	struct hgl_context* context = CALLOC_STRUCT(hgl_context);

	if (!context) {
		ERROR("%s: Couldn't create pipe context!\n", __FUNCTION__);
		return 0;
	}

	// Set up the initial things our context needs
	context->display = fDisplay;

	// Create state tracker visual
	context->stVisual = hgl_create_st_visual(fOptions);

	// Create state tracker framebuffers
	context->buffer = hgl_create_st_framebuffer(context, wsContext);

	if (!context->buffer) {
		ERROR("%s: Problem allocating framebuffer!\n", __func__);
		FREE(context->stVisual);
		return -1;
	}

	// Build state tracker attributes
	struct st_context_attribs attribs;
	memset(&attribs, 0, sizeof(attribs));
	attribs.options.force_glsl_extensions_warn = false;
	attribs.profile = ST_PROFILE_DEFAULT;
	attribs.visual = *context->stVisual;
	attribs.major = 1;
	attribs.minor = 0;
	//attribs.flags |= ST_CONTEXT_FLAG_DEBUG;

	struct st_context_iface* shared = NULL;

	if (fOptions & BGL_SHARE_CONTEXT) {
		shared = fDisplay->api->get_current(fDisplay->api);
		TRACE("shared context: %p\n", shared);
	}

	// Create context using state tracker api call
	enum st_context_error result;
	context->st = fDisplay->api->create_context(fDisplay->api, fDisplay->manager,
		&attribs, &result, shared);

	if (!context->st) {
		ERROR("%s: Couldn't create mesa state tracker context!\n",
			__func__);
		switch (result) {
			case ST_CONTEXT_SUCCESS:
				ERROR("%s: State tracker error: SUCCESS?\n", __func__);
				break;
			case ST_CONTEXT_ERROR_NO_MEMORY:
				ERROR("%s: State tracker error: NO_MEMORY\n", __func__);
				break;
			case ST_CONTEXT_ERROR_BAD_API:
				ERROR("%s: State tracker error: BAD_API\n", __func__);
				break;
			case ST_CONTEXT_ERROR_BAD_VERSION:
				ERROR("%s: State tracker error: BAD_VERSION\n", __func__);
				break;
			case ST_CONTEXT_ERROR_BAD_FLAG:
				ERROR("%s: State tracker error: BAD_FLAG\n", __func__);
				break;
			case ST_CONTEXT_ERROR_UNKNOWN_ATTRIBUTE:
				ERROR("%s: State tracker error: BAD_ATTRIBUTE\n", __func__);
				break;
			case ST_CONTEXT_ERROR_UNKNOWN_FLAG:
				ERROR("%s: State tracker error: UNKNOWN_FLAG\n", __func__);
				break;
		}

		hgl_destroy_st_visual(context->stVisual);
		FREE(context);
		return -1;
	}

	assert(!context->st->st_manager_private);
	context->st->st_manager_private = (void*)context;

	struct st_context *stContext = (struct st_context*)context->st;

	// Init Gallium3D Post Processing
	// TODO: no pp filters are enabled yet through postProcessEnable
	context->postProcess = pp_init(stContext->pipe, context->postProcessEnable,
		stContext->cso_context, &stContext->iface);

	context_id contextNext = -1;
	Lock();
	for (context_id i = 0; i < CONTEXT_MAX; i++) {
		if (fContext[i] == NULL) {
			fContext[i] = context;
			contextNext = i;
			break;
		}
	}
	Unlock();

	if (contextNext < 0) {
		ERROR("%s: The next context is invalid... something went wrong!\n",
			__func__);
		//st_destroy_context(context->st);
		FREE(context->stVisual);
		FREE(context);
		return -1;
	}

	TRACE("%s: context #%" B_PRIu64 " is the next available context\n",
		__func__, contextNext);

	return contextNext;
}


void
GalliumContext::DestroyContext(context_id contextID)
{
	// fMutex should be locked *before* calling DestoryContext

	// See if context is used
	if (!fContext[contextID])
		return;

	if (fContext[contextID]->st) {
		fContext[contextID]->st->flush(fContext[contextID]->st, 0, NULL, NULL, NULL);
		fContext[contextID]->st->destroy(fContext[contextID]->st);
	}

	if (fContext[contextID]->postProcess)
		pp_free(fContext[contextID]->postProcess);

	// Delete state tracker framebuffer objects
	if (fContext[contextID]->buffer)
		hgl_destroy_st_framebuffer(fContext[contextID]->buffer);

	if (fContext[contextID]->stVisual)
		hgl_destroy_st_visual(fContext[contextID]->stVisual);

	FREE(fContext[contextID]);
}


status_t
GalliumContext::SetCurrentContext(bool set, context_id contextID)
{
	CALLED();

	if (contextID < 0 || contextID > CONTEXT_MAX) {
		ERROR("%s: Invalid context ID range!\n", __func__);
		return B_ERROR;
	}

	Lock();
	context_id oldContextID = fCurrentContext;
	struct hgl_context* context = fContext[contextID];

	if (!context) {
		ERROR("%s: Invalid context provided (#%" B_PRIu64 ")!\n",
			__func__, contextID);
		Unlock();
		return B_ERROR;
	}

	if (!set) {
		fDisplay->api->make_current(fDisplay->api, NULL, NULL, NULL);
		Unlock();
		return B_OK;
	}

	// Everything seems valid, lets set the new context.
	fCurrentContext = contextID;

	if (oldContextID > 0 && oldContextID != contextID) {
		fContext[oldContextID]->st->flush(fContext[oldContextID]->st,
			ST_FLUSH_FRONT, NULL, NULL, NULL);
	}

	// We need to lock and unlock framebuffers before accessing them
	fDisplay->api->make_current(fDisplay->api, context->st, context->buffer->stfbi,
		context->buffer->stfbi);
	Unlock();

	return B_OK;
}


status_t
GalliumContext::SwapBuffers(context_id contextID)
{
	CALLED();

	Lock();
	struct hgl_context* context = fContext[contextID];

	if (!context) {
		ERROR("%s: context not found\n", __func__);
		Unlock();
		return B_ERROR;
	}

	// will flush front buffer if no double buffering is used
	context->st->flush(context->st, ST_FLUSH_FRONT, NULL, NULL, NULL);

	struct hgl_buffer* buffer = context->buffer;

	// flush back buffer and swap buffers if double buffering is used
	if (buffer->textures[ST_ATTACHMENT_BACK_LEFT] != NULL) {
		buffer->screen->flush_frontbuffer(buffer->screen, NULL, buffer->textures[ST_ATTACHMENT_BACK_LEFT],
			0, 0, buffer->winsysContext, NULL);
		std::swap(buffer->textures[ST_ATTACHMENT_FRONT_LEFT], buffer->textures[ST_ATTACHMENT_BACK_LEFT]);
		p_atomic_inc(&buffer->stfbi->stamp);
	}

	Unlock();
	return B_OK;
}


void
GalliumContext::Draw(context_id contextID, BRect updateRect)
{
	struct hgl_context *context = fContext[contextID];

	if (!context) {
		ERROR("%s: context not found\n", __func__);
		return;
	}

	struct hgl_buffer* buffer = context->buffer;

	if (buffer->textures[ST_ATTACHMENT_FRONT_LEFT] == NULL)
		return;

	buffer->screen->flush_frontbuffer(buffer->screen, NULL, buffer->textures[ST_ATTACHMENT_FRONT_LEFT],
		0, 0, buffer->winsysContext, NULL);
}


bool
GalliumContext::Validate(uint32 width, uint32 height)
{
	CALLED();

	if (!fContext[fCurrentContext])
		return false;

	if (fContext[fCurrentContext]->width != width + 1
		|| fContext[fCurrentContext]->height != height + 1) {
		Invalidate(width, height);
		return false;
	}
	return true;
}


void
GalliumContext::Invalidate(uint32 width, uint32 height)
{
	CALLED();

	assert(fContext[fCurrentContext]);

	// Update st_context dimensions 
	fContext[fCurrentContext]->width = width + 1;
	fContext[fCurrentContext]->height = height + 1;

	// Is this the best way to invalidate?
	p_atomic_inc(&fContext[fCurrentContext]->buffer->stfbi->stamp);
}


void
GalliumContext::Lock()
{
	CALLED();
	mtx_lock(&fMutex);
}


void
GalliumContext::Unlock()
{
	CALLED();
	mtx_unlock(&fMutex);
}
/* vim: set tabstop=4: */
