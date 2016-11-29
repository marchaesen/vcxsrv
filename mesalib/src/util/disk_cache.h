/*
 * Copyright © 2014 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef DISK_CACHE_H
#define DISK_CACHE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Size of cache keys in bytes. */
#define CACHE_KEY_SIZE 20

typedef uint8_t cache_key[CACHE_KEY_SIZE];

struct disk_cache;

/* Provide inlined stub functions if the shader cache is disabled. */

#ifdef ENABLE_SHADER_CACHE

/**
 * Create a new cache object.
 *
 * This function creates the handle necessary for all subsequent cache_*
 * functions.
 *
 * This cache provides two distinct operations:
 *
 *   o Storage and retrieval of arbitrary objects by cryptographic
 *     name (or "key").  This is provided via disk_cache_put() and
 *     disk_cache_get().
 *
 *   o The ability to store a key alone and check later whether the
 *     key was previously stored. This is provided via disk_cache_put_key()
 *     and disk_cache_has_key().
 *
 * The put_key()/has_key() operations are conceptually identical to
 * put()/get() with no data, but are provided separately to allow for
 * a more efficient implementation.
 *
 * In all cases, the keys are sequences of 20 bytes. It is anticipated
 * that callers will compute appropriate SHA-1 signatures for keys,
 * (though nothing in this implementation directly relies on how the
 * names are computed). See mesa-sha1.h and _mesa_sha1_compute for
 * assistance in computing SHA-1 signatures.
 */
struct disk_cache *
disk_cache_create(void);

/**
 * Destroy a cache object, (freeing all associated resources).
 */
void
disk_cache_destroy(struct disk_cache *cache);

/**
 * Store an item in the cache under the name \key.
 *
 * The item can be retrieved later with disk_cache_get(), (unless the item has
 * been evicted in the interim).
 *
 * Any call to disk_cache_put() may cause an existing, random item to be
 * evicted from the cache.
 */
void
disk_cache_put(struct disk_cache *cache, cache_key key,
               const void *data, size_t size);

/**
 * Retrieve an item previously stored in the cache with the name <key>.
 *
 * The item must have been previously stored with a call to disk_cache_put().
 *
 * If \size is non-NULL, then, on successful return, it will be set to the
 * size of the object.
 *
 * \return A pointer to the stored object if found. NULL if the object
 * is not found, or if any error occurs, (memory allocation failure,
 * filesystem error, etc.). The returned data is malloc'ed so the
 * caller should call free() it when finished.
 */
void *
disk_cache_get(struct disk_cache *cache, cache_key key, size_t *size);

/**
 * Store the name \key within the cache, (without any associated data).
 *
 * Later this key can be checked with disk_cache_has_key(), (unless the key
 * has been evicted in the interim).
 *
 * Any call to cache_record() may cause an existing, random key to be
 * evicted from the cache.
 */
void
disk_cache_put_key(struct disk_cache *cache, cache_key key);

/**
 * Test whether the name \key was previously recorded in the cache.
 *
 * Return value: True if disk_cache_put_key() was previously called with
 * \key, (and the key was not evicted in the interim).
 *
 * Note: disk_cache_has_key() will only return true for keys passed to
 * disk_cache_put_key(). Specifically, a call to disk_cache_put() will not cause
 * disk_cache_has_key() to return true for the same key.
 */
bool
disk_cache_has_key(struct disk_cache *cache, cache_key key);

#else

static inline struct disk_cache *
disk_cache_create(void)
{
   return NULL;
}

static inline void
disk_cache_destroy(struct disk_cache *cache) {
   return;
}

static inline void
disk_cache_put(struct disk_cache *cache, cache_key key,
          const void *data, size_t size)
{
   return;
}

static inline uint8_t *
disk_cache_get(struct disk_cache *cache, cache_key key, size_t *size)
{
   return NULL;
}

static inline void
disk_cache_put_key(struct disk_cache *cache, cache_key key)
{
   return;
}

static inline bool
disk_cache_has_key(struct disk_cache *cache, cache_key key)
{
   return false;
}

#endif /* ENABLE_SHADER_CACHE */

#ifdef __cplusplus
}
#endif

#endif /* CACHE_H */
