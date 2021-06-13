/*
 * Copyright 2006-2012 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Philippe Houdoin <philippe.houdoin@free.fr>
 *		Alexander von Gluck IV <kallisti5@unixzen.com>
 */


#include <driver_settings.h>
#include <image.h>

#include <kernel/image.h>
#include <private/system/safemode_defs.h>

#include <Directory.h>
#include <FindDirectory.h>
#include <Path.h>
#include <strings.h>
#include "GLRendererRoster.h"

#include <new>
#include <string.h>
#include <stdio.h>


extern "C" status_t _kern_get_safemode_option(const char* parameter,
	char* buffer, size_t* _bufferSize);

GLRendererRoster *GLRendererRoster::fInstance = NULL;

GLRendererRoster *GLRendererRoster::Roster()
{
	if (fInstance == NULL) {
		fInstance = new GLRendererRoster();
	}
	return fInstance;
}

GLRendererRoster::GLRendererRoster()
	:
	fSafeMode(false),
	fABISubDirectory(NULL)
{
	char parameter[32];
	size_t parameterLength = sizeof(parameter);

	if (_kern_get_safemode_option(B_SAFEMODE_SAFE_MODE,
		parameter, &parameterLength) == B_OK) {
		if (!strcasecmp(parameter, "enabled") || !strcasecmp(parameter, "on")
			|| !strcasecmp(parameter, "true") || !strcasecmp(parameter, "yes")
			|| !strcasecmp(parameter, "enable") || !strcmp(parameter, "1"))
			fSafeMode = true;
	}

	if (_kern_get_safemode_option(B_SAFEMODE_DISABLE_USER_ADD_ONS,
		parameter, &parameterLength) == B_OK) {
		if (!strcasecmp(parameter, "enabled") || !strcasecmp(parameter, "on")
			|| !strcasecmp(parameter, "true") || !strcasecmp(parameter, "yes")
			|| !strcasecmp(parameter, "enable") || !strcmp(parameter, "1"))
			fSafeMode = true;
	}

	// We might run in compatibility mode on a system with a different ABI. The
	// renderers matching our ABI can usually be found in respective
	// subdirectories of the opengl add-ons directories.
	system_info info;
	if (get_system_info(&info) == B_OK
		&& (info.abi & B_HAIKU_ABI_MAJOR)
			!= (B_HAIKU_ABI & B_HAIKU_ABI_MAJOR)) {
			switch (B_HAIKU_ABI & B_HAIKU_ABI_MAJOR) {
				case B_HAIKU_ABI_GCC_2:
					fABISubDirectory = "gcc2";
					break;
				case B_HAIKU_ABI_GCC_4:
					fABISubDirectory = "gcc4";
					break;
			}
	}

	AddDefaultPaths();
}


GLRendererRoster::~GLRendererRoster()
{

}


BGLRenderer*
GLRendererRoster::GetRenderer(BGLView *view, ulong options)
{
	for (
		RendererMap::const_iterator iterator = fRenderers.begin();
		iterator != fRenderers.end();
		iterator++
	) {
		renderer_item item = *iterator;
		BGLRenderer* renderer;
		renderer = item.entry(view, options);
		return renderer;
	}
	return NULL;
}


void
GLRendererRoster::AddDefaultPaths()
{
	// add user directories first, so that they can override system renderers
	const directory_which paths[] = {
		B_USER_NONPACKAGED_ADDONS_DIRECTORY,
		B_USER_ADDONS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		B_SYSTEM_ADDONS_DIRECTORY,
	};

	for (uint32 i = fSafeMode ? 4 : 0;
		i < sizeof(paths) / sizeof(paths[0]); i++) {
		BPath path;
		status_t status = find_directory(paths[i], &path, true);
		if (status == B_OK && path.Append("opengl") == B_OK)
			AddPath(path.Path());
	}
}


status_t
GLRendererRoster::AddPath(const char* path)
{
	BDirectory directory(path);
	status_t status = directory.InitCheck();
	if (status < B_OK)
		return status;

	// if a subdirectory for our ABI exists, use that instead
	if (fABISubDirectory != NULL) {
		BEntry entry(&directory, fABISubDirectory);
		if (entry.IsDirectory()) {
			status = directory.SetTo(&entry);
			if (status != B_OK)
				return status;
		}
	}

	node_ref nodeRef;
	status = directory.GetNodeRef(&nodeRef);
	if (status < B_OK)
		return status;

	int32 count = 0;
	int32 files = 0;

	entry_ref ref;
	BEntry entry;
	while (directory.GetNextRef(&ref) == B_OK) {
		entry.SetTo(&ref, true);
		if (entry.InitCheck() == B_OK && !entry.IsFile())
			continue;

		if (CreateRenderer(ref) == B_OK)
			count++;

		files++;
	}

	if (files != 0 && count == 0)
		return B_BAD_VALUE;

	return B_OK;
}


status_t
GLRendererRoster::AddRenderer(InstantiateRenderer entry,
	image_id image, const entry_ref* ref, ino_t node)
{
	renderer_item item;
	item.entry = entry;
	item.image = image;
	item.node = node;
	if (ref != NULL)
		item.ref = *ref;

	try {
		fRenderers.push_back(item);
	} catch (...) {
		return B_NO_MEMORY;
	}

	return B_OK;
}


status_t
GLRendererRoster::CreateRenderer(const entry_ref& ref)
{
	BEntry entry(&ref, true);
	node_ref nodeRef;
	status_t status = entry.GetNodeRef(&nodeRef);
	if (status < B_OK)
		return status;

	BPath path(&ref);
	printf("OpenGL load add-on: %s\n", path.Path());

	image_id image = load_add_on(path.Path());
	if (image < B_OK)
		return image;

	InstantiateRenderer instantiate_renderer;

	status = get_image_symbol(
		image, "instantiate_gl_renderer",
		B_SYMBOL_TYPE_TEXT, (void**)&instantiate_renderer
	);

	if (status == B_OK) {
		if ((status = AddRenderer(instantiate_renderer, image, &ref, nodeRef.node)) != B_OK) {
			unload_add_on(image);
			return status;
		}
		printf("OpenGL add-on registered: %s\n", path.Path());
		return B_OK;
	}

	printf("OpenGL add-on failed to instantiate: %s\n", path.Path());
	unload_add_on(image);

	return status;
}
