/*
 * Copyright 2006-2012, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Philippe Houdoin <philippe.houdoin@free.fr>
 */
#ifndef _GLRENDERER_ROSTER_H
#define _GLRENDERER_ROSTER_H


#include <GLRenderer.h>

#include <vector>


typedef BGLRenderer* (*InstantiateRenderer) (BGLView* view, ulong options);

struct renderer_item {
	InstantiateRenderer entry;
	entry_ref	ref;
	ino_t		node;
	image_id	image;
};

typedef std::vector<renderer_item> RendererMap;


class GLRendererRoster {
	public:
		static GLRendererRoster *Roster();
		BGLRenderer* GetRenderer(BGLView *view, ulong options);

	private:
		GLRendererRoster();
		virtual ~GLRendererRoster();

		void AddDefaultPaths();
		status_t AddPath(const char* path);
		status_t AddRenderer(InstantiateRenderer entry, image_id image,
			const entry_ref* ref, ino_t node);
		status_t CreateRenderer(const entry_ref& ref);

		static GLRendererRoster* fInstance;
		bool		fSafeMode;
		const char*	fABISubDirectory;

		RendererMap fRenderers;
};


#endif	/* _GLRENDERER_ROSTER_H */
