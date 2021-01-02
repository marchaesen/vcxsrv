/*
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include <sys/stat.h>

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_hash_table.h"
#include "util/u_pointer.h"
#include "os/os_thread.h"

#include "freedreno_drm_public.h"

#include "freedreno/freedreno_screen.h"

static struct hash_table *fd_tab = NULL;

static mtx_t fd_screen_mutex = _MTX_INITIALIZER_NP;

static void
fd_drm_screen_destroy(struct pipe_screen *pscreen)
{
	struct fd_screen *screen = fd_screen(pscreen);
	boolean destroy;

	mtx_lock(&fd_screen_mutex);
	destroy = --screen->refcnt == 0;
	if (destroy) {
		int fd = fd_device_fd(screen->dev);
		_mesa_hash_table_remove_key(fd_tab, intptr_to_pointer(fd));
	}
	mtx_unlock(&fd_screen_mutex);

	if (destroy) {
		pscreen->destroy = screen->winsys_priv;
		pscreen->destroy(pscreen);
	}
}

struct pipe_screen *
fd_drm_screen_create(int fd, struct renderonly *ro)
{
	struct pipe_screen *pscreen = NULL;

	mtx_lock(&fd_screen_mutex);
	if (!fd_tab) {
		fd_tab = util_hash_table_create_fd_keys();
		if (!fd_tab)
			goto unlock;
	}

	pscreen = util_hash_table_get(fd_tab, intptr_to_pointer(fd));
	if (pscreen) {
		fd_screen(pscreen)->refcnt++;
	} else {
		struct fd_device *dev = fd_device_new_dup(fd);
		if (!dev)
			goto unlock;

		pscreen = fd_screen_create(dev, ro);
		if (pscreen) {
			int fd = fd_device_fd(dev);

			_mesa_hash_table_insert(fd_tab, intptr_to_pointer(fd), pscreen);

			/* Bit of a hack, to avoid circular linkage dependency,
			 * ie. pipe driver having to call in to winsys, we
			 * override the pipe drivers screen->destroy():
			 */
			fd_screen(pscreen)->winsys_priv = pscreen->destroy;
			pscreen->destroy = fd_drm_screen_destroy;
		}
	}

unlock:
	mtx_unlock(&fd_screen_mutex);
	return pscreen;
}
