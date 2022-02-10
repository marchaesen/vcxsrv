/*
 * Copyright © 2020 Valve Corporation
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

/* This is a basic c implementation of a fossilize db like format intended for
 * use with the Mesa shader cache.
 *
 * The format is compatible enough to allow the fossilize db tools to be used
 * to do things like merge db collections.
 */

#include "fossilize_db.h"

#ifdef FOZ_DB_UTIL

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

#include "crc32.h"
#include "hash_table.h"
#include "mesa-sha1.h"
#include "ralloc.h"

#define FOZ_REF_MAGIC_SIZE 16

static const uint8_t stream_reference_magic_and_version[FOZ_REF_MAGIC_SIZE] = {
   0x81, 'F', 'O', 'S',
   'S', 'I', 'L', 'I',
   'Z', 'E', 'D', 'B',
   0, 0, 0, FOSSILIZE_FORMAT_VERSION, /* 4 bytes to use for versioning. */
};

/* Mesa uses 160bit hashes to identify cache entries, a hash of this size
 * makes collisions virtually impossible for our use case. However the foz db
 * format uses a 64bit hash table to lookup file offsets for reading cache
 * entries so we must shorten our hash.
 */
static uint64_t
truncate_hash_to_64bits(const uint8_t *cache_key)
{
   uint64_t hash = 0;
   unsigned shift = 7;
   for (unsigned i = 0; i < 8; i++) {
      hash |= ((uint64_t)cache_key[i]) << shift * 8;
      shift--;
   }
   return hash;
}

static bool
check_files_opened_successfully(FILE *file, FILE *db_idx)
{
   if (!file) {
      if (db_idx)
         fclose(db_idx);
      return false;
   }

   if (!db_idx) {
      if (file)
         fclose(file);
      return false;
   }

   return true;
}

static bool
create_foz_db_filenames(char *cache_path, char *name, char **filename,
                        char **idx_filename)
{
   if (asprintf(filename, "%s/%s.foz", cache_path, name) == -1)
      return false;

   if (asprintf(idx_filename, "%s/%s_idx.foz", cache_path, name) == -1) {
      free(*filename);
      return false;
   }

   return true;
}


/* This looks at stuff that was added to the index since the last time we looked at it. This is safe
 * to do without locking the file as we assume the file is append only */
static void
update_foz_index(struct foz_db *foz_db, FILE *db_idx, unsigned file_idx)
{
   uint64_t offset = ftell(db_idx);
   fseek(db_idx, 0, SEEK_END);
   uint64_t len = ftell(db_idx);
   uint64_t parsed_offset = offset;

   if (offset == len)
      return;

   fseek(db_idx, offset, SEEK_SET);
   while (offset < len) {
      char bytes_to_read[FOSSILIZE_BLOB_HASH_LENGTH + sizeof(struct foz_payload_header)];
      struct foz_payload_header *header;

      /* Corrupt entry. Our process might have been killed before we
       * could write all data.
       */
      if (offset + sizeof(bytes_to_read) > len)
         break;

      /* NAME + HEADER in one read */
      if (fread(bytes_to_read, 1, sizeof(bytes_to_read), db_idx) !=
          sizeof(bytes_to_read))
         break;

      offset += sizeof(bytes_to_read);
      header = (struct foz_payload_header*)&bytes_to_read[FOSSILIZE_BLOB_HASH_LENGTH];

      /* Corrupt entry. Our process might have been killed before we
       * could write all data.
       */
      if (offset + header->payload_size > len ||
          header->payload_size != sizeof(uint64_t))
         break;

      char hash_str[FOSSILIZE_BLOB_HASH_LENGTH + 1] = {0};
      memcpy(hash_str, bytes_to_read, FOSSILIZE_BLOB_HASH_LENGTH);

      /* read cache item offset from index file */
      uint64_t cache_offset;
      if (fread(&cache_offset, 1, sizeof(cache_offset), db_idx) !=
          sizeof(cache_offset))
         break;

      offset += header->payload_size;
      parsed_offset = offset;

      struct foz_db_entry *entry = ralloc(foz_db->mem_ctx,
                                          struct foz_db_entry);
      entry->header = *header;
      entry->file_idx = file_idx;
      _mesa_sha1_hex_to_sha1(entry->key, hash_str);

      /* Truncate the entry's hash string to a 64bit hash for use with a
       * 64bit hash table for looking up file offsets.
       */
      hash_str[16] = '\0';
      uint64_t key = strtoull(hash_str, NULL, 16);

      entry->offset = cache_offset;

      _mesa_hash_table_u64_insert(foz_db->index_db, key, entry);
   }


   fseek(db_idx, parsed_offset, SEEK_SET);
}

/* exclusive flock with timeout. timeout is in nanoseconds */
static int lock_file_with_timeout(FILE *f, int64_t timeout)
{
   int err;
   int fd = fileno(f);
   int64_t iterations = MAX2(DIV_ROUND_UP(timeout, 1000000), 1);

   /* Since there is no blocking flock with timeout and we don't want to totally spin on getting the
    * lock, use a nonblocking method and retry every millisecond. */
   for (int64_t iter = 0; iter < iterations; ++iter) {
      err = flock(fd, LOCK_EX | LOCK_NB);
      if (err == 0 || errno != EAGAIN)
         break;
      usleep(1000);
   }
   return err;
}

static bool
load_foz_dbs(struct foz_db *foz_db, FILE *db_idx, uint8_t file_idx,
             bool read_only)
{
   /* Scan through the archive and get the list of cache entries. */
   fseek(db_idx, 0, SEEK_END);
   size_t len = ftell(db_idx);
   rewind(db_idx);

   /* Try not to take the lock if len >= the size of the header, but if it is smaller we take the
    * lock to potentially initialize the files. */
   if (len < sizeof(stream_reference_magic_and_version)) {
      /* Wait for 100 ms in case of contention, after that we prioritize getting the app started. */
      int err = lock_file_with_timeout(foz_db->file[file_idx], 100000000);
      if (err == -1)
         goto fail;

      /* Compute length again so we know nobody else did it in the meantime */
      fseek(db_idx, 0, SEEK_END);
      len = ftell(db_idx);
      rewind(db_idx);
   }

   if (len != 0) {
      uint8_t magic[FOZ_REF_MAGIC_SIZE];
      if (fread(magic, 1, FOZ_REF_MAGIC_SIZE, db_idx) != FOZ_REF_MAGIC_SIZE)
         goto fail;

      if (memcmp(magic, stream_reference_magic_and_version,
                 FOZ_REF_MAGIC_SIZE - 1))
         goto fail;

      int version = magic[FOZ_REF_MAGIC_SIZE - 1];
      if (version > FOSSILIZE_FORMAT_VERSION ||
          version < FOSSILIZE_FORMAT_MIN_COMPAT_VERSION)
         goto fail;

   } else {
      /* Appending to a fresh file. Make sure we have the magic. */
      if (fwrite(stream_reference_magic_and_version, 1,
                 sizeof(stream_reference_magic_and_version), foz_db->file[file_idx]) !=
          sizeof(stream_reference_magic_and_version))
         goto fail;

      if (fwrite(stream_reference_magic_and_version, 1,
                 sizeof(stream_reference_magic_and_version), db_idx) !=
          sizeof(stream_reference_magic_and_version))
         goto fail;

      fflush(foz_db->file[file_idx]);
      fflush(db_idx);
   }

   flock(fileno(foz_db->file[file_idx]), LOCK_UN);

   update_foz_index(foz_db, db_idx, file_idx);

   foz_db->alive = true;
   return true;

fail:
   flock(fileno(foz_db->file[file_idx]), LOCK_UN);
   foz_destroy(foz_db);
   return false;
}

/* Here we open mesa cache foz dbs files. If the files exist we load the index
 * db into a hash table. The index db contains the offsets needed to later
 * read cache entries from the foz db containing the actual cache entries.
 */
bool
foz_prepare(struct foz_db *foz_db, char *cache_path)
{
   char *filename = NULL;
   char *idx_filename = NULL;
   if (!create_foz_db_filenames(cache_path, "foz_cache", &filename, &idx_filename))
      return false;

   /* Open the default foz dbs for read/write. If the files didn't already exist
    * create them.
    */
   foz_db->file[0] = fopen(filename, "a+b");
   foz_db->db_idx = fopen(idx_filename, "a+b");

   free(filename);
   free(idx_filename);

   if (!check_files_opened_successfully(foz_db->file[0], foz_db->db_idx))
      return false;

   simple_mtx_init(&foz_db->mtx, mtx_plain);
   simple_mtx_init(&foz_db->flock_mtx, mtx_plain);
   foz_db->mem_ctx = ralloc_context(NULL);
   foz_db->index_db = _mesa_hash_table_u64_create(NULL);

   if (!load_foz_dbs(foz_db, foz_db->db_idx, 0, false))
      return false;

   uint8_t file_idx = 1;
   char *foz_dbs = getenv("MESA_DISK_CACHE_READ_ONLY_FOZ_DBS");
   if (!foz_dbs)
      return true;

   for (unsigned n; n = strcspn(foz_dbs, ","), *foz_dbs;
        foz_dbs += MAX2(1, n)) {
      char *foz_db_filename = strndup(foz_dbs, n);

      filename = NULL;
      idx_filename = NULL;
      if (!create_foz_db_filenames(cache_path, foz_db_filename, &filename,
                                   &idx_filename)) {
         free(foz_db_filename);
         continue; /* Ignore invalid user provided filename and continue */
      }
      free(foz_db_filename);

      /* Open files as read only */
      foz_db->file[file_idx] = fopen(filename, "rb");
      FILE *db_idx = fopen(idx_filename, "rb");

      free(filename);
      free(idx_filename);

      if (!check_files_opened_successfully(foz_db->file[file_idx], db_idx)) {
         /* Prevent foz_destroy from destroying it a second time. */
         foz_db->file[file_idx] = NULL;

         continue; /* Ignore invalid user provided filename and continue */
      }

      if (!load_foz_dbs(foz_db, db_idx, file_idx, true)) {
         fclose(db_idx);
         return false;
      }

      fclose(db_idx);
      file_idx++;

      if (file_idx >= FOZ_MAX_DBS)
         break;
   }

   return true;
}

void
foz_destroy(struct foz_db *foz_db)
{
   if (foz_db->db_idx)
      fclose(foz_db->db_idx);
   for (unsigned i = 0; i < FOZ_MAX_DBS; i++) {
      if (foz_db->file[i])
         fclose(foz_db->file[i]);
   }

   if (foz_db->mem_ctx) {
      _mesa_hash_table_u64_destroy(foz_db->index_db);
      ralloc_free(foz_db->mem_ctx);
      simple_mtx_destroy(&foz_db->flock_mtx);
      simple_mtx_destroy(&foz_db->mtx);
   }
}

/* Here we lookup a cache entry in the index hash table. If an entry is found
 * we use the retrieved offset to read the cache entry from disk.
 */
void *
foz_read_entry(struct foz_db *foz_db, const uint8_t *cache_key_160bit,
               size_t *size)
{
   uint64_t hash = truncate_hash_to_64bits(cache_key_160bit);

   void *data = NULL;

   if (!foz_db->alive)
      return NULL;

   simple_mtx_lock(&foz_db->mtx);

   struct foz_db_entry *entry =
      _mesa_hash_table_u64_search(foz_db->index_db, hash);
   if (!entry) {
      update_foz_index(foz_db, foz_db->db_idx, 0);
      entry = _mesa_hash_table_u64_search(foz_db->index_db, hash);
   }
   if (!entry) {
      simple_mtx_unlock(&foz_db->mtx);
      return NULL;
   }

   uint8_t file_idx = entry->file_idx;
   if (fseek(foz_db->file[file_idx], entry->offset, SEEK_SET) < 0)
      goto fail;

   uint32_t header_size = sizeof(struct foz_payload_header);
   if (fread(&entry->header, 1, header_size, foz_db->file[file_idx]) !=
       header_size)
      goto fail;

   /* Check for collision using full 160bit hash for increased assurance
    * against potential collisions.
    */
   for (int i = 0; i < 20; i++) {
      if (cache_key_160bit[i] != entry->key[i])
         goto fail;
   }

   uint32_t data_sz = entry->header.payload_size;
   data = malloc(data_sz);
   if (fread(data, 1, data_sz, foz_db->file[file_idx]) != data_sz)
      goto fail;

   /* verify checksum */
   if (entry->header.crc != 0) {
      if (util_hash_crc32(data, data_sz) != entry->header.crc)
         goto fail;
   }

   simple_mtx_unlock(&foz_db->mtx);

   if (size)
      *size = data_sz;

   return data;

fail:
   free(data);

   /* reading db entry failed. reset the file offset */
   simple_mtx_unlock(&foz_db->mtx);

   return NULL;
}

/* Here we write the cache entry to disk and store its offset in the index db.
 */
bool
foz_write_entry(struct foz_db *foz_db, const uint8_t *cache_key_160bit,
                const void *blob, size_t blob_size)
{
   uint64_t hash = truncate_hash_to_64bits(cache_key_160bit);

   if (!foz_db->alive)
      return false;

   /* The flock is per-fd, not per thread, we do it outside of the main mutex to avoid having to
    * wait in the mutex potentially blocking reads. We use the secondary flock_mtx to stop race
    * conditions between the write threads sharing the same file descriptor. */
   simple_mtx_lock(&foz_db->flock_mtx);

   /* Wait for 1 second. This is done outside of the main mutex as I believe there is more potential
    * for file contention than mtx contention of significant length. */
   int err = lock_file_with_timeout(foz_db->file[0], 1000000000);
   if (err == -1)
      goto fail_file;

   simple_mtx_lock(&foz_db->mtx);

   update_foz_index(foz_db, foz_db->db_idx, 0);

   struct foz_db_entry *entry =
      _mesa_hash_table_u64_search(foz_db->index_db, hash);
   if (entry) {
      simple_mtx_unlock(&foz_db->mtx);
      flock(fileno(foz_db->file[0]), LOCK_UN);
      simple_mtx_unlock(&foz_db->flock_mtx);
      return NULL;
   }

   /* Prepare db entry header and blob ready for writing */
   struct foz_payload_header header;
   header.uncompressed_size = blob_size;
   header.format = FOSSILIZE_COMPRESSION_NONE;
   header.payload_size = blob_size;
   header.crc = util_hash_crc32(blob, blob_size);

   fseek(foz_db->file[0], 0, SEEK_END);

   /* Write hash header to db */
   char hash_str[FOSSILIZE_BLOB_HASH_LENGTH + 1]; /* 40 digits + null */
   _mesa_sha1_format(hash_str, cache_key_160bit);
   if (fwrite(hash_str, 1, FOSSILIZE_BLOB_HASH_LENGTH, foz_db->file[0]) !=
       FOSSILIZE_BLOB_HASH_LENGTH)
      goto fail;

   off_t offset = ftell(foz_db->file[0]);

   /* Write db entry header */
   if (fwrite(&header, 1, sizeof(header), foz_db->file[0]) != sizeof(header))
      goto fail;

   /* Now write the db entry blob */
   if (fwrite(blob, 1, blob_size, foz_db->file[0]) != blob_size)
      goto fail;

   /* Flush everything to file to reduce chance of cache corruption */
   fflush(foz_db->file[0]);

   /* Write hash header to index db */
   if (fwrite(hash_str, 1, FOSSILIZE_BLOB_HASH_LENGTH, foz_db->db_idx) !=
       FOSSILIZE_BLOB_HASH_LENGTH)
      goto fail;

   header.uncompressed_size = sizeof(uint64_t);
   header.format = FOSSILIZE_COMPRESSION_NONE;
   header.payload_size = sizeof(uint64_t);
   header.crc = 0;

   if (fwrite(&header, 1, sizeof(header), foz_db->db_idx) !=
       sizeof(header))
      goto fail;

   if (fwrite(&offset, 1, sizeof(uint64_t), foz_db->db_idx) !=
       sizeof(uint64_t))
      goto fail;

   /* Flush everything to file to reduce chance of cache corruption */
   fflush(foz_db->db_idx);

   entry = ralloc(foz_db->mem_ctx, struct foz_db_entry);
   entry->header = header;
   entry->offset = offset;
   entry->file_idx = 0;
   _mesa_sha1_hex_to_sha1(entry->key, hash_str);
   _mesa_hash_table_u64_insert(foz_db->index_db, hash, entry);

   simple_mtx_unlock(&foz_db->mtx);
   flock(fileno(foz_db->file[0]), LOCK_UN);
   simple_mtx_unlock(&foz_db->flock_mtx);

   return true;

fail:
   simple_mtx_unlock(&foz_db->mtx);
fail_file:
   flock(fileno(foz_db->file[0]), LOCK_UN);
   simple_mtx_unlock(&foz_db->flock_mtx);
   return false;
}
#else

bool
foz_prepare(struct foz_db *foz_db, char *filename)
{
   fprintf(stderr, "Warning: Mesa single file cache selected but Mesa wasn't "
           "built with single cache file support. Shader cache will be disabled"
           "!\n");
   return false;
}

void
foz_destroy(struct foz_db *foz_db)
{
}

void *
foz_read_entry(struct foz_db *foz_db, const uint8_t *cache_key_160bit,
               size_t *size)
{
   return false;
}

bool
foz_write_entry(struct foz_db *foz_db, const uint8_t *cache_key_160bit,
                const void *blob, size_t size)
{
   return false;
}

#endif
