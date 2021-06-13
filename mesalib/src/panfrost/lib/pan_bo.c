/*
 * Copyright 2019 Collabora, Ltd.
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
 * Authors (Collabora):
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <pthread.h>
#include "drm-uapi/panfrost_drm.h"

#include "pan_bo.h"
#include "pan_util.h"
#include "wrap.h"

#include "os/os_mman.h"

#include "util/u_inlines.h"
#include "util/u_math.h"

/* This file implements a userspace BO cache. Allocating and freeing
 * GPU-visible buffers is very expensive, and even the extra kernel roundtrips
 * adds more work than we would like at this point. So caching BOs in userspace
 * solves both of these problems and does not require kernel updates.
 *
 * Cached BOs are sorted into a bucket based on rounding their size down to the
 * nearest power-of-two. Each bucket contains a linked list of free panfrost_bo
 * objects. Putting a BO into the cache is accomplished by adding it to the
 * corresponding bucket. Getting a BO from the cache consists of finding the
 * appropriate bucket and sorting. A cache eviction is a kernel-level free of a
 * BO and removing it from the bucket. We special case evicting all BOs from
 * the cache, since that's what helpful in practice and avoids extra logic
 * around the linked list.
 */

static struct panfrost_bo *
panfrost_bo_alloc(struct panfrost_device *dev, size_t size,
                  uint32_t flags)
{
        struct drm_panfrost_create_bo create_bo = { .size = size };
        struct panfrost_bo *bo;
        int ret;

        if (dev->kernel_version->version_major > 1 ||
            dev->kernel_version->version_minor >= 1) {
                if (flags & PAN_BO_GROWABLE)
                        create_bo.flags |= PANFROST_BO_HEAP;
                if (!(flags & PAN_BO_EXECUTE))
                        create_bo.flags |= PANFROST_BO_NOEXEC;
        }

        ret = drmIoctl(dev->fd, DRM_IOCTL_PANFROST_CREATE_BO, &create_bo);
        if (ret) {
                fprintf(stderr, "DRM_IOCTL_PANFROST_CREATE_BO failed: %m\n");
                return NULL;
        }

        bo = pan_lookup_bo(dev, create_bo.handle);
        assert(!memcmp(bo, &((struct panfrost_bo){}), sizeof(*bo)));

        bo->size = create_bo.size;
        bo->ptr.gpu = create_bo.offset;
        bo->gem_handle = create_bo.handle;
        bo->flags = flags;
        bo->dev = dev;
        return bo;
}

static void
panfrost_bo_free(struct panfrost_bo *bo)
{
        struct drm_gem_close gem_close = { .handle = bo->gem_handle };
        int ret;

        ret = drmIoctl(bo->dev->fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
        if (ret) {
                fprintf(stderr, "DRM_IOCTL_GEM_CLOSE failed: %m\n");
                assert(0);
        }

        /* BO will be freed with the sparse array, but zero to indicate free */
        memset(bo, 0, sizeof(*bo));
}

/* Returns true if the BO is ready, false otherwise.
 * access_type is encoding the type of access one wants to ensure is done.
 * Waiting is always done for writers, but if wait_readers is set then readers
 * are also waited for.
 */
bool
panfrost_bo_wait(struct panfrost_bo *bo, int64_t timeout_ns, bool wait_readers)
{
        struct drm_panfrost_wait_bo req = {
                .handle = bo->gem_handle,
		.timeout_ns = timeout_ns,
        };
        int ret;

        /* If the BO has been exported or imported we can't rely on the cached
         * state, we need to call the WAIT_BO ioctl.
         */
        if (!(bo->flags & PAN_BO_SHARED)) {
                /* If ->gpu_access is 0, the BO is idle, no need to wait. */
                if (!bo->gpu_access)
                        return true;

                /* If the caller only wants to wait for writers and no
                 * writes are pending, we don't have to wait.
                 */
                if (!wait_readers && !(bo->gpu_access & PAN_BO_ACCESS_WRITE))
                        return true;
        }

        /* The ioctl returns >= 0 value when the BO we are waiting for is ready
         * -1 otherwise.
         */
        ret = drmIoctl(bo->dev->fd, DRM_IOCTL_PANFROST_WAIT_BO, &req);
        if (ret != -1) {
                /* Set gpu_access to 0 so that the next call to bo_wait()
                 * doesn't have to call the WAIT_BO ioctl.
                 */
                bo->gpu_access = 0;
                return true;
        }

        /* If errno is not ETIMEDOUT or EBUSY that means the handle we passed
         * is invalid, which shouldn't happen here.
         */
        assert(errno == ETIMEDOUT || errno == EBUSY);
        return false;
}

/* Helper to calculate the bucket index of a BO */

static unsigned
pan_bucket_index(unsigned size)
{
        /* Round down to POT to compute a bucket index */

        unsigned bucket_index = util_logbase2(size);

        /* Clamp the bucket index; all huge allocations will be
         * sorted into the largest bucket */

        bucket_index = MIN2(bucket_index, MAX_BO_CACHE_BUCKET);

        /* The minimum bucket size must equal the minimum allocation
         * size; the maximum we clamped */

        assert(bucket_index >= MIN_BO_CACHE_BUCKET);
        assert(bucket_index <= MAX_BO_CACHE_BUCKET);

        /* Reindex from 0 */
        return (bucket_index - MIN_BO_CACHE_BUCKET);
}

static struct list_head *
pan_bucket(struct panfrost_device *dev, unsigned size)
{
        return &dev->bo_cache.buckets[pan_bucket_index(size)];
}

/* Tries to fetch a BO of sufficient size with the appropriate flags from the
 * BO cache. If it succeeds, it returns that BO and removes the BO from the
 * cache. If it fails, it returns NULL signaling the caller to allocate a new
 * BO. */

static struct panfrost_bo *
panfrost_bo_cache_fetch(struct panfrost_device *dev,
                        size_t size, uint32_t flags, bool dontwait)
{
        pthread_mutex_lock(&dev->bo_cache.lock);
        struct list_head *bucket = pan_bucket(dev, size);
        struct panfrost_bo *bo = NULL;

        /* Iterate the bucket looking for something suitable */
        list_for_each_entry_safe(struct panfrost_bo, entry, bucket,
                                 bucket_link) {
                if (entry->size < size || entry->flags != flags)
                        continue;

                if (!panfrost_bo_wait(entry, dontwait ? 0 : INT64_MAX,
                                      PAN_BO_ACCESS_RW))
                        continue;

                struct drm_panfrost_madvise madv = {
                        .handle = entry->gem_handle,
                        .madv = PANFROST_MADV_WILLNEED,
                };
                int ret;

                /* This one works, splice it out of the cache */
                list_del(&entry->bucket_link);
                list_del(&entry->lru_link);

                ret = drmIoctl(dev->fd, DRM_IOCTL_PANFROST_MADVISE, &madv);
                if (!ret && !madv.retained) {
                        panfrost_bo_free(entry);
                        continue;
                }
                /* Let's go! */
                bo = entry;
                break;
        }
        pthread_mutex_unlock(&dev->bo_cache.lock);

        return bo;
}

static void
panfrost_bo_cache_evict_stale_bos(struct panfrost_device *dev)
{
        struct timespec time;

        clock_gettime(CLOCK_MONOTONIC, &time);
        list_for_each_entry_safe(struct panfrost_bo, entry,
                                 &dev->bo_cache.lru, lru_link) {
                /* We want all entries that have been used more than 1 sec
                 * ago to be dropped, others can be kept.
                 * Note the <= 2 check and not <= 1. It's here to account for
                 * the fact that we're only testing ->tv_sec, not ->tv_nsec.
                 * That means we might keep entries that are between 1 and 2
                 * seconds old, but we don't really care, as long as unused BOs
                 * are dropped at some point.
                 */
                if (time.tv_sec - entry->last_used <= 2)
                        break;

                list_del(&entry->bucket_link);
                list_del(&entry->lru_link);
                panfrost_bo_free(entry);
        }
}

/* Tries to add a BO to the cache. Returns if it was
 * successful */

static bool
panfrost_bo_cache_put(struct panfrost_bo *bo)
{
        struct panfrost_device *dev = bo->dev;

        if (bo->flags & PAN_BO_SHARED)
                return false;

        pthread_mutex_lock(&dev->bo_cache.lock);
        struct list_head *bucket = pan_bucket(dev, MAX2(bo->size, 4096));
        struct drm_panfrost_madvise madv;
        struct timespec time;

        madv.handle = bo->gem_handle;
        madv.madv = PANFROST_MADV_DONTNEED;
	madv.retained = 0;

        drmIoctl(dev->fd, DRM_IOCTL_PANFROST_MADVISE, &madv);

        /* Add us to the bucket */
        list_addtail(&bo->bucket_link, bucket);

        /* Add us to the LRU list and update the last_used field. */
        list_addtail(&bo->lru_link, &dev->bo_cache.lru);
        clock_gettime(CLOCK_MONOTONIC, &time);
        bo->last_used = time.tv_sec;

        /* Let's do some cleanup in the BO cache while we hold the
         * lock.
         */
        panfrost_bo_cache_evict_stale_bos(dev);
        pthread_mutex_unlock(&dev->bo_cache.lock);

        return true;
}

/* Evicts all BOs from the cache. Called during context
 * destroy or during low-memory situations (to free up
 * memory that may be unused by us just sitting in our
 * cache, but still reserved from the perspective of the
 * OS) */

void
panfrost_bo_cache_evict_all(
                struct panfrost_device *dev)
{
        pthread_mutex_lock(&dev->bo_cache.lock);
        for (unsigned i = 0; i < ARRAY_SIZE(dev->bo_cache.buckets); ++i) {
                struct list_head *bucket = &dev->bo_cache.buckets[i];

                list_for_each_entry_safe(struct panfrost_bo, entry, bucket,
                                         bucket_link) {
                        list_del(&entry->bucket_link);
                        list_del(&entry->lru_link);
                        panfrost_bo_free(entry);
                }
        }
        pthread_mutex_unlock(&dev->bo_cache.lock);
}

void
panfrost_bo_mmap(struct panfrost_bo *bo)
{
        struct drm_panfrost_mmap_bo mmap_bo = { .handle = bo->gem_handle };
        int ret;

        if (bo->ptr.cpu)
                return;

        ret = drmIoctl(bo->dev->fd, DRM_IOCTL_PANFROST_MMAP_BO, &mmap_bo);
        if (ret) {
                fprintf(stderr, "DRM_IOCTL_PANFROST_MMAP_BO failed: %m\n");
                assert(0);
        }

        bo->ptr.cpu = os_mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                              bo->dev->fd, mmap_bo.offset);
        if (bo->ptr.cpu == MAP_FAILED) {
                fprintf(stderr, "mmap failed: %p %m\n", bo->ptr.cpu);
                assert(0);
        }
}

static void
panfrost_bo_munmap(struct panfrost_bo *bo)
{
        if (!bo->ptr.cpu)
                return;

        if (os_munmap((void *) (uintptr_t)bo->ptr.cpu, bo->size)) {
                perror("munmap");
                abort();
        }

        bo->ptr.cpu = NULL;
}

struct panfrost_bo *
panfrost_bo_create(struct panfrost_device *dev, size_t size,
                   uint32_t flags)
{
        struct panfrost_bo *bo;

        /* Kernel will fail (confusingly) with EPERM otherwise */
        assert(size > 0);

        /* To maximize BO cache usage, don't allocate tiny BOs */
        size = MAX2(size, 4096);

        /* GROWABLE BOs cannot be mmapped */
        if (flags & PAN_BO_GROWABLE)
                assert(flags & PAN_BO_INVISIBLE);

        /* Before creating a BO, we first want to check the cache but without
         * waiting for BO readiness (BOs in the cache can still be referenced
         * by jobs that are not finished yet).
         * If the cached allocation fails we fall back on fresh BO allocation,
         * and if that fails too, we try one more time to allocate from the
         * cache, but this time we accept to wait.
         */
        bo = panfrost_bo_cache_fetch(dev, size, flags, true);
        if (!bo)
                bo = panfrost_bo_alloc(dev, size, flags);
        if (!bo)
                bo = panfrost_bo_cache_fetch(dev, size, flags, false);

        if (!bo)
                fprintf(stderr, "BO creation failed\n");

        assert(bo);

        /* Only mmap now if we know we need to. For CPU-invisible buffers, we
         * never map since we don't care about their contents; they're purely
         * for GPU-internal use. But we do trace them anyway. */

        if (!(flags & (PAN_BO_INVISIBLE | PAN_BO_DELAY_MMAP)))
                panfrost_bo_mmap(bo);

        p_atomic_set(&bo->refcnt, 1);

        if (dev->debug & (PAN_DBG_TRACE | PAN_DBG_SYNC)) {
                if (flags & PAN_BO_INVISIBLE)
                        pandecode_inject_mmap(bo->ptr.gpu, NULL, bo->size, NULL);
                else if (!(flags & PAN_BO_DELAY_MMAP))
                        pandecode_inject_mmap(bo->ptr.gpu, bo->ptr.cpu, bo->size, NULL);
        }

        return bo;
}

void
panfrost_bo_reference(struct panfrost_bo *bo)
{
        if (bo) {
                ASSERTED int count = p_atomic_inc_return(&bo->refcnt);
                assert(count != 1);
        }
}

void
panfrost_bo_unreference(struct panfrost_bo *bo)
{
        if (!bo)
                return;

        /* Don't return to cache if there are still references */
        if (p_atomic_dec_return(&bo->refcnt))
                return;

        struct panfrost_device *dev = bo->dev;

        pthread_mutex_lock(&dev->bo_map_lock);

        /* Someone might have imported this BO while we were waiting for the
         * lock, let's make sure it's still not referenced before freeing it.
         */
        if (p_atomic_read(&bo->refcnt) == 0) {
                /* When the reference count goes to zero, we need to cleanup */
                panfrost_bo_munmap(bo);

                if (dev->debug & (PAN_DBG_TRACE | PAN_DBG_SYNC))
                        pandecode_inject_free(bo->ptr.gpu, bo->size);

                /* Rather than freeing the BO now, we'll cache the BO for later
                 * allocations if we're allowed to.
                 */
                if (!panfrost_bo_cache_put(bo))
                        panfrost_bo_free(bo);

        }
        pthread_mutex_unlock(&dev->bo_map_lock);
}

struct panfrost_bo *
panfrost_bo_import(struct panfrost_device *dev, int fd)
{
        struct panfrost_bo *bo;
        struct drm_panfrost_get_bo_offset get_bo_offset = {0,};
        ASSERTED int ret;
        unsigned gem_handle;

        ret = drmPrimeFDToHandle(dev->fd, fd, &gem_handle);
        assert(!ret);

        pthread_mutex_lock(&dev->bo_map_lock);
        bo = pan_lookup_bo(dev, gem_handle);

        if (!bo->dev) {
                get_bo_offset.handle = gem_handle;
                ret = drmIoctl(dev->fd, DRM_IOCTL_PANFROST_GET_BO_OFFSET, &get_bo_offset);
                assert(!ret);

                bo->dev = dev;
                bo->ptr.gpu = (mali_ptr) get_bo_offset.offset;
                bo->size = lseek(fd, 0, SEEK_END);
                bo->flags = PAN_BO_SHARED;
                bo->gem_handle = gem_handle;
                assert(bo->size > 0);
                p_atomic_set(&bo->refcnt, 1);
                // TODO map and unmap on demand?
                panfrost_bo_mmap(bo);
        } else {
                /* bo->refcnt == 0 can happen if the BO
                 * was being released but panfrost_bo_import() acquired the
                 * lock before panfrost_bo_unreference(). In that case, refcnt
                 * is 0 and we can't use panfrost_bo_reference() directly, we
                 * have to re-initialize the refcnt().
                 * Note that panfrost_bo_unreference() checks
                 * refcnt value just after acquiring the lock to
                 * make sure the object is not freed if panfrost_bo_import()
                 * acquired it in the meantime.
                 */
                if (p_atomic_read(&bo->refcnt) == 0)
                        p_atomic_set(&bo->refcnt, 1);
                else
                        panfrost_bo_reference(bo);
                assert(bo->ptr.cpu);
        }
        pthread_mutex_unlock(&dev->bo_map_lock);

        return bo;
}

int
panfrost_bo_export(struct panfrost_bo *bo)
{
        struct drm_prime_handle args = {
                .handle = bo->gem_handle,
                .flags = DRM_CLOEXEC,
        };

        int ret = drmIoctl(bo->dev->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
        if (ret == -1)
                return -1;

        bo->flags |= PAN_BO_SHARED;
        return args.fd;
}

