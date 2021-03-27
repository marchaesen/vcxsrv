/*
 * Copyright 2009-2014, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 */
#ifndef HGL_CONTEXT_H
#define HGL_CONTEXT_H

#include "os/os_thread.h"
#include "pipe/p_format.h"
#include "pipe/p_compiler.h"
#include "pipe/p_screen.h"
#include "postprocess/filters.h"

#include "frontend/api.h"

#include "bitmap_wrapper.h"


#ifdef __cplusplus
extern "C" {
#endif


#define CONTEXT_MAX 32

typedef int64 context_id;


struct hgl_buffer
{
	struct st_framebuffer_iface *stfbi;
	struct st_visual* visual;

	unsigned width;
	unsigned height;
	unsigned mask;

	struct pipe_screen* screen;
	void* winsysContext;

	enum pipe_texture_target target;
	struct pipe_resource* textures[ST_ATTACHMENT_COUNT];

	void *map;
};


struct hgl_display
{
	mtx_t mutex;

	struct st_api* api;
	struct st_manager* manager;
};


struct hgl_context
{
	struct hgl_display* display;
	struct st_context_iface* st;
	struct st_visual* stVisual;

	// Post processing
	struct pp_queue_t* postProcess;
	unsigned int postProcessEnable[PP_FILTERS];

	// Desired viewport size
	unsigned width;
	unsigned height;

	mtx_t fbMutex;

	struct hgl_buffer* buffer;
};

// hgl_buffer from statetracker interface
struct hgl_buffer* hgl_st_framebuffer(struct st_framebuffer_iface *stfbi);

// hgl frontend
struct st_api* hgl_create_st_api(void);

// hgl framebuffer
struct hgl_buffer* hgl_create_st_framebuffer(struct hgl_context* context, void *winsysContext);
void hgl_destroy_st_framebuffer(struct hgl_buffer *buffer);

// hgl manager
struct st_manager* hgl_create_st_manager(struct hgl_context* screen);
void hgl_destroy_st_manager(struct st_manager *manager);

// hgl visual
struct st_visual* hgl_create_st_visual(ulong options);
void hgl_destroy_st_visual(struct st_visual* visual);

// hgl display
struct hgl_display* hgl_create_display(struct pipe_screen* screen);
void hgl_destroy_display(struct hgl_display *display);


#ifdef __cplusplus
}
#endif

#endif /* HGL_CONTEXT_H */
