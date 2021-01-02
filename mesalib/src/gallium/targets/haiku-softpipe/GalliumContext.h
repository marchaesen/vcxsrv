/*
 * Copyright 2009, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 */
#ifndef GALLIUMCONTEXT_H
#define GALLIUMCONTEXT_H


#include <stddef.h>
#include <kernel/image.h>

#include "pipe/p_compiler.h"
#include "pipe/p_screen.h"
#include "postprocess/filters.h"
#include "hgl_context.h"

#include "bitmap_wrapper.h"



class GalliumContext {
public:
							GalliumContext(ulong options);
							~GalliumContext();

		void				Lock();
		void				Unlock();

		context_id			CreateContext(Bitmap* bitmap);
		void				DestroyContext(context_id contextID);
		context_id			GetCurrentContext() { return fCurrentContext; };
		status_t			SetCurrentContext(Bitmap *bitmap,
								context_id contextID);

		status_t			SwapBuffers(context_id contextID);

		bool				Validate(uint32 width, uint32 height);
		void				Invalidate(uint32 width, uint32 height);

private:
		status_t			CreateScreen();
		void				Flush();

		ulong				fOptions;
		struct pipe_screen*	fScreen;

		// Context Management
		struct hgl_context*	fContext[CONTEXT_MAX];
		context_id			fCurrentContext;
		mtx_t			fMutex;
};


#endif /* GALLIUMCONTEXT_H */
