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

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <errno.h>
#include <dirent.h>

#include "util/u_atomic.h"
#include "util/mesa-sha1.h"
#include "util/ralloc.h"
#include "main/errors.h"

#include "cache.h"

/* Number of bits to mask off from a cache key to get an index. */
#define CACHE_INDEX_KEY_BITS 16

/* Mask for computing an index from a key. */
#define CACHE_INDEX_KEY_MASK ((1 << CACHE_INDEX_KEY_BITS) - 1)

/* The number of keys that can be stored in the index. */
#define CACHE_INDEX_MAX_KEYS (1 << CACHE_INDEX_KEY_BITS)

struct program_cache {
   /* The path to the cache directory. */
   char *path;

   /* A pointer to the mmapped index file within the cache directory. */
   uint8_t *index_mmap;
   size_t index_mmap_size;

   /* Pointer to total size of all objects in cache (within index_mmap) */
   uint64_t *size;

   /* Pointer to stored keys, (within index_mmap). */
   uint8_t *stored_keys;

   /* Maximum size of all cached objects (in bytes). */
   uint64_t max_size;
};

/* Create a directory named 'path' if it does not already exist.
 *
 * Returns: 0 if path already exists as a directory or if created.
 *         -1 in all other cases.
 */
static int
mkdir_if_needed(char *path)
{
   struct stat sb;

   /* If the path exists already, then our work is done if it's a
    * directory, but it's an error if it is not.
    */
   if (stat(path, &sb) == 0) {
      if (S_ISDIR(sb.st_mode)) {
         return 0;
      } else {
         _mesa_warning(NULL,
                       "Cannot use %s for shader cache (not a directory)"
                       "---disabling.\n", path);
         return -1;
      }
   }

   int ret = mkdir(path, 0755);
   if (ret == 0 || (ret == -1 && errno == EEXIST))
     return 0;

   _mesa_warning(NULL,
                 "Failed to create %s for shader cache (%s)---disabling.\n",
                 path, strerror(errno));

   return -1;
}

/* Concatenate an existing path and a new name to form a new path.  If the new
 * path does not exist as a directory, create it then return the resulting
 * name of the new path (ralloc'ed off of 'ctx').
 *
 * Returns NULL on any error, such as:
 *
 *      <path> does not exist or is not a directory
 *      <path>/<name> exists but is not a directory
 *      <path>/<name> cannot be created as a directory
 */
static char *
concatenate_and_mkdir(void *ctx, char *path, char *name)
{
   char *new_path;
   struct stat sb;

   if (stat(path, &sb) != 0 || ! S_ISDIR(sb.st_mode))
      return NULL;

   new_path = ralloc_asprintf(ctx, "%s/%s", path, name);

   if (mkdir_if_needed(new_path) == 0)
      return new_path;
   else
      return NULL;
}

struct program_cache *
cache_create(void)
{
   void *local;
   struct program_cache *cache = NULL;
   char *path, *max_size_str;
   uint64_t max_size;
   int fd = -1;
   struct stat sb;
   size_t size;

   /* A ralloc context for transient data during this invocation. */
   local = ralloc_context(NULL);
   if (local == NULL)
      goto fail;

   /* At user request, disable shader cache entirely. */
   if (getenv("MESA_GLSL_CACHE_DISABLE"))
      goto fail;

   /* Determine path for cache based on the first defined name as follows:
    *
    *   $MESA_GLSL_CACHE_DIR
    *   $XDG_CACHE_HOME/mesa
    *   <pwd.pw_dir>/.cache/mesa
    */
   path = getenv("MESA_GLSL_CACHE_DIR");
   if (path && mkdir_if_needed(path) == -1) {
      goto fail;
   }

   if (path == NULL) {
      char *xdg_cache_home = getenv("XDG_CACHE_HOME");

      if (xdg_cache_home) {
         if (mkdir_if_needed(xdg_cache_home) == -1)
            goto fail;

         path = concatenate_and_mkdir(local, xdg_cache_home, "mesa");
         if (path == NULL)
            goto fail;
      }
   }

   if (path == NULL) {
      char *buf;
      size_t buf_size;
      struct passwd pwd, *result;

      buf_size = sysconf(_SC_GETPW_R_SIZE_MAX);
      if (buf_size == -1)
         buf_size = 512;

      /* Loop until buf_size is large enough to query the directory */
      while (1) {
         buf = ralloc_size(local, buf_size);

         getpwuid_r(getuid(), &pwd, buf, buf_size, &result);
         if (result)
            break;

         if (errno == ERANGE) {
            ralloc_free(buf);
            buf = NULL;
            buf_size *= 2;
         } else {
            goto fail;
         }
      }

      path = concatenate_and_mkdir(local, pwd.pw_dir, ".cache");
      if (path == NULL)
         goto fail;

      path = concatenate_and_mkdir(local, path, "mesa");
      if (path == NULL)
         goto fail;
   }

   cache = ralloc(NULL, struct program_cache);
   if (cache == NULL)
      goto fail;

   cache->path = ralloc_strdup(cache, path);
   if (cache->path == NULL)
      goto fail;

   path = ralloc_asprintf(local, "%s/index", cache->path);
   if (path == NULL)
      goto fail;

   fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
   if (fd == -1)
      goto fail;

   if (fstat(fd, &sb) == -1)
      goto fail;

   /* Force the index file to be the expected size. */
   size = sizeof(*cache->size) + CACHE_INDEX_MAX_KEYS * CACHE_KEY_SIZE;
   if (sb.st_size != size) {
      if (ftruncate(fd, size) == -1)
         goto fail;
   }

   /* We map this shared so that other processes see updates that we
    * make.
    *
    * Note: We do use atomic addition to ensure that multiple
    * processes don't scramble the cache size recorded in the
    * index. But we don't use any locking to prevent multiple
    * processes from updating the same entry simultaneously. The idea
    * is that if either result lands entirely in the index, then
    * that's equivalent to a well-ordered write followed by an
    * eviction and a write. On the other hand, if the simultaneous
    * writes result in a corrupt entry, that's not really any
    * different than both entries being evicted, (since within the
    * guarantees of the cryptographic hash, a corrupt entry is
    * unlikely to ever match a real cache key).
    */
   cache->index_mmap = mmap(NULL, size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, 0);
   if (cache->index_mmap == MAP_FAILED)
      goto fail;
   cache->index_mmap_size = size;

   close(fd);

   cache->size = (uint64_t *) cache->index_mmap;
   cache->stored_keys = cache->index_mmap + sizeof(uint64_t);

   max_size = 0;

   max_size_str = getenv("MESA_GLSL_CACHE_MAX_SIZE");
   if (max_size_str) {
      char *end;
      max_size = strtoul(max_size_str, &end, 10);
      if (end == max_size_str) {
         max_size = 0;
      } else {
         while (*end && isspace(*end))
            end++;
         switch (*end) {
         case 'K':
         case 'k':
            max_size *= 1024;
            break;
         case 'M':
         case 'm':
            max_size *= 1024*1024;
            break;
         case '\0':
         case 'G':
         case 'g':
         default:
            max_size *= 1024*1024*1024;
            break;
         }
      }
   }

   /* Default to 1GB for maximum cache size. */
   if (max_size == 0)
      max_size = 1024*1024*1024;

   cache->max_size = max_size;

   ralloc_free(local);

   return cache;

 fail:
   if (fd != -1)
      close(fd);
   if (cache)
      ralloc_free(cache);
   ralloc_free(local);

   return NULL;
}

void
cache_destroy(struct program_cache *cache)
{
   munmap(cache->index_mmap, cache->index_mmap_size);

   ralloc_free(cache);
}

/* Return a filename within the cache's directory corresponding to 'key'. The
 * returned filename is ralloced with 'cache' as the parent context.
 *
 * Returns NULL if out of memory.
 */
static char *
get_cache_file(struct program_cache *cache, cache_key key)
{
   char buf[41];

   _mesa_sha1_format(buf, key);

   return ralloc_asprintf(cache, "%s/%c%c/%s",
                          cache->path, buf[0], buf[1], buf + 2);
}

/* Create the directory that will be needed for the cache file for \key.
 *
 * Obviously, the implementation here must closely match
 * _get_cache_file above.
*/
static void
make_cache_file_directory(struct program_cache *cache, cache_key key)
{
   char *dir;
   char buf[41];

   _mesa_sha1_format(buf, key);

   dir = ralloc_asprintf(cache, "%s/%c%c", cache->path, buf[0], buf[1]);

   mkdir_if_needed(dir);

   ralloc_free(dir);
}

/* Given a directory path and predicate function, count all entries in
 * that directory for which the predicate returns true. Then choose a
 * random entry from among those counted.
 *
 * Returns: A malloc'ed string for the path to the chosen file, (or
 * NULL on any error). The caller should free the string when
 * finished.
 */
static char *
choose_random_file_matching(const char *dir_path,
                            bool (*predicate)(struct dirent *))
{
   DIR *dir;
   struct dirent *entry;
   unsigned int count, victim;
   char *filename;

   dir = opendir(dir_path);
   if (dir == NULL)
      return NULL;

   count = 0;

   while (1) {
      entry = readdir(dir);
      if (entry == NULL)
         break;
      if (! predicate(entry))
         continue;

      count++;
   }

   if (count == 0) {
      closedir(dir);
      return NULL;
   }

   victim = rand() % count;

   rewinddir(dir);
   count = 0;

   while (1) {
      entry = readdir(dir);
      if (entry == NULL)
         break;
      if (! predicate(entry))
         continue;
      if (count == victim)
         break;

      count++;
   }

   if (entry == NULL) {
      closedir(dir);
      return NULL;
   }

   asprintf(&filename, "%s/%s", dir_path, entry->d_name);

   closedir(dir);

   return filename;
}

/* Is entry a regular file, and not having a name with a trailing
 * ".tmp"
 */
static bool
is_regular_non_tmp_file(struct dirent *entry)
{
   size_t len;

   if (entry->d_type != DT_REG)
      return false;

   len = strlen (entry->d_name);
   if (len >= 4 && strcmp(&entry->d_name[len-4], ".tmp") == 0)
      return false;

   return true;
}

/* Returns the size of the deleted file, (or 0 on any error). */
static size_t
unlink_random_file_from_directory(const char *path)
{
   struct stat sb;
   char *filename;

   filename = choose_random_file_matching(path, is_regular_non_tmp_file);
   if (filename == NULL)
      return 0;

   if (stat(filename, &sb) == -1) {
      free (filename);
      return 0;
   }

   unlink(filename);

   free (filename);

   return sb.st_size;
}

/* Is entry a directory with a two-character name, (and not the
 * special name of "..")
 */
static bool
is_two_character_sub_directory(struct dirent *entry)
{
   if (entry->d_type != DT_DIR)
      return false;

   if (strlen(entry->d_name) != 2)
      return false;

   if (strcmp(entry->d_name, "..") == 0)
      return false;

   return true;
}

static void
evict_random_item(struct program_cache *cache)
{
   const char hex[] = "0123456789abcde";
   char *dir_path;
   int a, b;
   size_t size;

   /* With a reasonably-sized, full cache, (and with keys generated
    * from a cryptographic hash), we can choose two random hex digits
    * and reasonably expect the directory to exist with a file in it.
    */
   a = rand() % 16;
   b = rand() % 16;

   asprintf (&dir_path, "%s/%c%c", cache->path, hex[a], hex[b]);
   if (dir_path == NULL)
      return;

   size = unlink_random_file_from_directory(dir_path);

   free(dir_path);

   if (size) {
      p_atomic_add(cache->size, - size);
      return;
   }

   /* In the case where the random choice of directory didn't find
    * something, we choose randomly from the existing directories.
    *
    * Really, the only reason this code exists is to allow the unit
    * tests to work, (which use an artificially-small cache to be able
    * to force a single cached item to be evicted).
    */
   dir_path = choose_random_file_matching(cache->path,
                                          is_two_character_sub_directory);
   if (dir_path == NULL)
      return;

   size = unlink_random_file_from_directory(dir_path);

   free(dir_path);

   if (size)
      p_atomic_add(cache->size, - size);
}

void
cache_put(struct program_cache *cache,
          cache_key key,
          const void *data,
          size_t size)
{
   int fd = -1, fd_final = -1, err, ret;
   size_t len;
   char *filename = NULL, *filename_tmp = NULL;
   const char *p = data;

   filename = get_cache_file(cache, key);
   if (filename == NULL)
      goto done;

   /* Write to a temporary file to allow for an atomic rename to the
    * final destination filename, (to prevent any readers from seeing
    * a partially written file).
    */
   filename_tmp = ralloc_asprintf(cache, "%s.tmp", filename);
   if (filename_tmp == NULL)
      goto done;

   fd = open(filename_tmp, O_WRONLY | O_CLOEXEC | O_CREAT, 0644);

   /* Make the two-character subdirectory within the cache as needed. */
   if (fd == -1) {
      if (errno != ENOENT)
         goto done;

      make_cache_file_directory(cache, key);

      fd = open(filename_tmp, O_WRONLY | O_CLOEXEC | O_CREAT, 0644);
      if (fd == -1)
         goto done;
   }

   /* With the temporary file open, we take an exclusive flock on
    * it. If the flock fails, then another process still has the file
    * open with the flock held. So just let that file be responsible
    * for writing the file.
    */
   err = flock(fd, LOCK_EX | LOCK_NB);
   if (err == -1)
      goto done;

   /* Now that we have the lock on the open temporary file, we can
    * check to see if the destination file already exists. If so,
    * another process won the race between when we saw that the file
    * didn't exist and now. In this case, we don't do anything more,
    * (to ensure the size accounting of the cache doesn't get off).
    */
   fd_final = open(filename, O_RDONLY | O_CLOEXEC);
   if (fd_final != -1)
      goto done;

   /* OK, we're now on the hook to write out a file that we know is
    * not in the cache, and is also not being written out to the cache
    * by some other process.
    *
    * Before we do that, if the cache is too large, evict something
    * else first.
    */
   if (*cache->size + size > cache->max_size)
      evict_random_item(cache);

   /* Now, finally, write out the contents to the temporary file, then
    * rename them atomically to the destination filename, and also
    * perform an atomic increment of the total cache size.
    */
   for (len = 0; len < size; len += ret) {
      ret = write(fd, p + len, size - len);
      if (ret == -1) {
         unlink(filename_tmp);
         goto done;
      }
   }

   rename(filename_tmp, filename);

   p_atomic_add(cache->size, size);

   /* This close finally releases the flock, (now that the final dile
    * has been renamed into place and the size has been added).
    */
   close(fd);
   fd = -1;

 done:
   if (filename_tmp)
      ralloc_free(filename_tmp);
   if (filename)
      ralloc_free(filename);
   if (fd != -1)
      close(fd);
}

void *
cache_get(struct program_cache *cache, cache_key key, size_t *size)
{
   int fd = -1, ret, len;
   struct stat sb;
   char *filename = NULL;
   uint8_t *data = NULL;

   if (size)
      *size = 0;

   filename = get_cache_file(cache, key);
   if (filename == NULL)
      goto fail;

   fd = open(filename, O_RDONLY | O_CLOEXEC);
   if (fd == -1)
      goto fail;

   if (fstat(fd, &sb) == -1)
      goto fail;

   data = malloc(sb.st_size);
   if (data == NULL)
      goto fail;

   for (len = 0; len < sb.st_size; len += ret) {
      ret = read(fd, data + len, sb.st_size - len);
      if (ret == -1)
         goto fail;
   }

   ralloc_free(filename);
   close(fd);

   if (size)
      *size = sb.st_size;

   return data;

 fail:
   if (data)
      free(data);
   if (filename)
      ralloc_free(filename);
   if (fd != -1)
      close(fd);

   return NULL;
}

void
cache_put_key(struct program_cache *cache, cache_key key)
{
   uint32_t *key_chunk = (uint32_t *) key;
   int i = *key_chunk & CACHE_INDEX_KEY_MASK;
   unsigned char *entry;

   entry = &cache->stored_keys[i + CACHE_KEY_SIZE];

   memcpy(entry, key, CACHE_KEY_SIZE);
}

/* This function lets us test whether a given key was previously
 * stored in the cache with cache_put_key(). The implement is
 * efficient by not using syscalls or hitting the disk. It's not
 * race-free, but the races are benign. If we race with someone else
 * calling cache_put_key, then that's just an extra cache miss and an
 * extra recompile.
 */
bool
cache_has_key(struct program_cache *cache, cache_key key)
{
   uint32_t *key_chunk = (uint32_t *) key;
   int i = *key_chunk & CACHE_INDEX_KEY_MASK;
   unsigned char *entry;

   entry = &cache->stored_keys[i + CACHE_KEY_SIZE];

   return memcmp(entry, key, CACHE_KEY_SIZE) == 0;
}
