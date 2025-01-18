/*
 * Copyright © 2024 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef FREEDRENO_DRM_PERFETTO_H_
#define FREEDRENO_DRM_PERFETTO_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Memory events are logged as transition between allocation categories.
 * Ie. a new allocation from kernel would be NONE -> ACTIVE, while a
 * freed buffer going to the BO cache would be ACTIVE -> CACHE, and then
 * if it is eventually freed from the cache, CACHE -> NONE.
 */
enum fd_alloc_category {
   FD_ALLOC_NONE,       /* freed / not allocated */
   FD_ALLOC_HEAP,       /* unused bo heap memory */
   FD_ALLOC_CACHE,      /* unused bo cache memory */
   FD_ALLOC_ACTIVE,     /* actively used */
};

struct fd_bo;

#ifdef HAVE_PERFETTO
void fd_drm_perfetto_init(void);
void fd_alloc_log(struct fd_bo *bo, enum fd_alloc_category from, enum fd_alloc_category to);
#else
static inline void
fd_drm_perfetto_init(void)
{
}
static inline void
fd_alloc_log(struct fd_bo *bo, enum fd_alloc_category from, enum fd_alloc_category to)
{
}
#endif

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* FREEDRENO_DRM_PERFETTO_H_ */
