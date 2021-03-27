/*
 * Copyright 2006-2012, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jérôme Duval, korli@users.berlios.de
 * 		Philippe Houdoin, philippe.houdoin@free.fr
 * 		Artur Wyszynski, harakash@gmail.com
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 */
#ifndef SOFTWARERENDERER_H
#define SOFTWARERENDERER_H


#include <kernel/image.h>

#include "GLRenderer.h"
#include "GalliumContext.h"


class SoftwareRenderer : public BGLRenderer, public HGLWinsysContext {
public:
								SoftwareRenderer(BGLView *view,
									ulong bgl_options);
	virtual						~SoftwareRenderer();

			void				LockGL();
			void				UnlockGL();

			void				Display(BBitmap* bitmap, BRect* updateRect);

			void				SwapBuffers(bool vsync = false);
			void				Draw(BRect updateRect);
			status_t			CopyPixelsOut(BPoint source, BBitmap *dest);
			status_t			CopyPixelsIn(BBitmap *source, BPoint dest);
			void				FrameResized(float width, float height);

			void				EnableDirectMode(bool enabled);
			void				DirectConnected(direct_buffer_info *info);

private:
			GalliumContext*		fContextObj;
			context_id			fContextID;

			bool				fDirectModeEnabled;
			direct_buffer_info*	fInfo;
			BLocker				fInfoLocker;
			ulong				fOptions;
			GLuint				fWidth;
			GLuint				fHeight;
			color_space			fColorSpace;
};

#endif	// SOFTPIPERENDERER_H
