/*
 * Copyright (C) 2012-2018 Rob Clark <robclark@freedesktop.org>
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

#include "os/os_mman.h"

#include "freedreno_drmif.h"
#include "freedreno_priv.h"

pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;
void bo_del(struct fd_bo *bo);

/* set buffer name, and add to table, call w/ table_lock held: */
static void set_name(struct fd_bo *bo, uint32_t name)
{
	bo->name = name;
	/* add ourself into the handle table: */
	_mesa_hash_table_insert(bo->dev->name_table, &bo->name, bo);
}

/* lookup a buffer, call w/ table_lock held: */
static struct fd_bo * lookup_bo(struct hash_table *tbl, uint32_t key)
{
	struct fd_bo *bo = NULL;
	struct hash_entry *entry = _mesa_hash_table_search(tbl, &key);
	if (entry) {
		/* found, incr refcnt and return: */
		bo = fd_bo_ref(entry->data);

		/* don't break the bucket if this bo was found in one */
		list_delinit(&bo->list);
	}
	return bo;
}

/* allocate a new buffer object, call w/ table_lock held */
static struct fd_bo * bo_from_handle(struct fd_device *dev,
		uint32_t size, uint32_t handle)
{
	struct fd_bo *bo;

	bo = dev->funcs->bo_from_handle(dev, size, handle);
	if (!bo) {
		struct drm_gem_close req = {
				.handle = handle,
		};
		drmIoctl(dev->fd, DRM_IOCTL_GEM_CLOSE, &req);
		return NULL;
	}
	bo->dev = dev;
	bo->size = size;
	bo->handle = handle;
	bo->iova = bo->funcs->iova(bo);
	bo->flags = FD_RELOC_FLAGS_INIT;

	p_atomic_set(&bo->refcnt, 1);
	list_inithead(&bo->list);
	/* add ourself into the handle table: */
	_mesa_hash_table_insert(dev->handle_table, &bo->handle, bo);
	return bo;
}

static struct fd_bo *
bo_new(struct fd_device *dev, uint32_t size, uint32_t flags,
		struct fd_bo_cache *cache)
{
	struct fd_bo *bo = NULL;
	uint32_t handle;
	int ret;

	bo = fd_bo_cache_alloc(cache, &size, flags);
	if (bo)
		return bo;

	ret = dev->funcs->bo_new_handle(dev, size, flags, &handle);
	if (ret)
		return NULL;

	pthread_mutex_lock(&table_lock);
	bo = bo_from_handle(dev, size, handle);
	pthread_mutex_unlock(&table_lock);

	VG_BO_ALLOC(bo);

	return bo;
}

struct fd_bo *
_fd_bo_new(struct fd_device *dev, uint32_t size, uint32_t flags)
{
	struct fd_bo *bo = bo_new(dev, size, flags, &dev->bo_cache);
	if (bo)
		bo->bo_reuse = BO_CACHE;
	return bo;
}

void
_fd_bo_set_name(struct fd_bo *bo, const char *fmt, va_list ap)
{
	bo->funcs->set_name(bo, fmt, ap);
}

/* internal function to allocate bo's that use the ringbuffer cache
 * instead of the normal bo_cache.  The purpose is, because cmdstream
 * bo's get vmap'd on the kernel side, and that is expensive, we want
 * to re-use cmdstream bo's for cmdstream and not unrelated purposes.
 */
struct fd_bo *
fd_bo_new_ring(struct fd_device *dev, uint32_t size)
{
	uint32_t flags = DRM_FREEDRENO_GEM_GPUREADONLY;
	struct fd_bo *bo = bo_new(dev, size, flags, &dev->ring_cache);
	if (bo) {
		bo->bo_reuse = RING_CACHE;
		bo->flags |= FD_RELOC_DUMP;
		fd_bo_set_name(bo, "cmdstream");
	}
	return bo;
}

struct fd_bo *
fd_bo_from_handle(struct fd_device *dev, uint32_t handle, uint32_t size)
{
	struct fd_bo *bo = NULL;

	pthread_mutex_lock(&table_lock);

	bo = lookup_bo(dev->handle_table, handle);
	if (bo)
		goto out_unlock;

	bo = bo_from_handle(dev, size, handle);

	VG_BO_ALLOC(bo);

out_unlock:
	pthread_mutex_unlock(&table_lock);

	return bo;
}

struct fd_bo *
fd_bo_from_dmabuf(struct fd_device *dev, int fd)
{
	int ret, size;
	uint32_t handle;
	struct fd_bo *bo;

	pthread_mutex_lock(&table_lock);
	ret = drmPrimeFDToHandle(dev->fd, fd, &handle);
	if (ret) {
		pthread_mutex_unlock(&table_lock);
		return NULL;
	}

	bo = lookup_bo(dev->handle_table, handle);
	if (bo)
		goto out_unlock;

	/* lseek() to get bo size */
	size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_CUR);

	bo = bo_from_handle(dev, size, handle);

	VG_BO_ALLOC(bo);

out_unlock:
	pthread_mutex_unlock(&table_lock);

	return bo;
}

struct fd_bo * fd_bo_from_name(struct fd_device *dev, uint32_t name)
{
	struct drm_gem_open req = {
			.name = name,
	};
	struct fd_bo *bo;

	pthread_mutex_lock(&table_lock);

	/* check name table first, to see if bo is already open: */
	bo = lookup_bo(dev->name_table, name);
	if (bo)
		goto out_unlock;

	if (drmIoctl(dev->fd, DRM_IOCTL_GEM_OPEN, &req)) {
		ERROR_MSG("gem-open failed: %s", strerror(errno));
		goto out_unlock;
	}

	bo = lookup_bo(dev->handle_table, req.handle);
	if (bo)
		goto out_unlock;

	bo = bo_from_handle(dev, req.size, req.handle);
	if (bo) {
		set_name(bo, name);
		VG_BO_ALLOC(bo);
	}

out_unlock:
	pthread_mutex_unlock(&table_lock);

	return bo;
}

void
fd_bo_mark_for_dump(struct fd_bo *bo)
{
	bo->flags |= FD_RELOC_DUMP;
}

uint64_t fd_bo_get_iova(struct fd_bo *bo)
{
	/* ancient kernels did not support this */
	assert(bo->iova != 0);
	return bo->iova;
}

struct fd_bo * fd_bo_ref(struct fd_bo *bo)
{
	p_atomic_inc(&bo->refcnt);
	return bo;
}

void fd_bo_del(struct fd_bo *bo)
{
	struct fd_device *dev = bo->dev;

	if (!atomic_dec_and_test(&bo->refcnt))
		return;

	pthread_mutex_lock(&table_lock);

	if ((bo->bo_reuse == BO_CACHE) && (fd_bo_cache_free(&dev->bo_cache, bo) == 0))
		goto out;
	if ((bo->bo_reuse == RING_CACHE) && (fd_bo_cache_free(&dev->ring_cache, bo) == 0))
		goto out;

	bo_del(bo);

out:
	pthread_mutex_unlock(&table_lock);
}

/* Called under table_lock */
void bo_del(struct fd_bo *bo)
{
	VG_BO_FREE(bo);

	if (bo->map)
		os_munmap(bo->map, bo->size);

	/* TODO probably bo's in bucket list get removed from
	 * handle table??
	 */

	if (bo->handle) {
		struct drm_gem_close req = {
				.handle = bo->handle,
		};
		_mesa_hash_table_remove_key(bo->dev->handle_table, &bo->handle);
		if (bo->name)
			_mesa_hash_table_remove_key(bo->dev->name_table, &bo->name);
		drmIoctl(bo->dev->fd, DRM_IOCTL_GEM_CLOSE, &req);
	}

	bo->funcs->destroy(bo);
}

int fd_bo_get_name(struct fd_bo *bo, uint32_t *name)
{
	if (!bo->name) {
		struct drm_gem_flink req = {
				.handle = bo->handle,
		};
		int ret;

		ret = drmIoctl(bo->dev->fd, DRM_IOCTL_GEM_FLINK, &req);
		if (ret) {
			return ret;
		}

		pthread_mutex_lock(&table_lock);
		set_name(bo, req.name);
		pthread_mutex_unlock(&table_lock);
		bo->bo_reuse = NO_CACHE;
	}

	*name = bo->name;

	return 0;
}

uint32_t fd_bo_handle(struct fd_bo *bo)
{
	bo->bo_reuse = NO_CACHE;
	return bo->handle;
}

int fd_bo_dmabuf(struct fd_bo *bo)
{
	int ret, prime_fd;

	ret = drmPrimeHandleToFD(bo->dev->fd, bo->handle, DRM_CLOEXEC,
			&prime_fd);
	if (ret) {
		ERROR_MSG("failed to get dmabuf fd: %d", ret);
		return ret;
	}

	bo->bo_reuse = NO_CACHE;

	return prime_fd;
}

uint32_t fd_bo_size(struct fd_bo *bo)
{
	return bo->size;
}

void * fd_bo_map(struct fd_bo *bo)
{
	if (!bo->map) {
		uint64_t offset;
		int ret;

		ret = bo->funcs->offset(bo, &offset);
		if (ret) {
			return NULL;
		}

		bo->map = os_mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
				bo->dev->fd, offset);
		if (bo->map == MAP_FAILED) {
			ERROR_MSG("mmap failed: %s", strerror(errno));
			bo->map = NULL;
		}
	}
	return bo->map;
}

/* a bit odd to take the pipe as an arg, but it's a, umm, quirk of kgsl.. */
int fd_bo_cpu_prep(struct fd_bo *bo, struct fd_pipe *pipe, uint32_t op)
{
	return bo->funcs->cpu_prep(bo, pipe, op);
}

void fd_bo_cpu_fini(struct fd_bo *bo)
{
	bo->funcs->cpu_fini(bo);
}
