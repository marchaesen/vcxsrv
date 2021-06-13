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

static bool
load_foz_dbs(struct foz_db *foz_db, FILE *db_idx, uint8_t file_idx,
             bool read_only)
{
   int err = flock(fileno(foz_db->file[file_idx]), LOCK_EX | LOCK_NB);
   if (err == -1)
      goto fail;

   err = flock(fileno(db_idx), LOCK_EX | LOCK_NB);
   if (err == -1)
      goto fail;

   /* Scan through the archive and get the list of cache entries. */
   fseek(db_idx, 0, SEEK_END);
   size_t len = ftell(db_idx);
   rewind(db_idx);

   if (!read_only)
       fseek(foz_db->file[file_idx], 0, SEEK_END);

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

      size_t offset = FOZ_REF_MAGIC_SIZE;
      size_t begin_append_offset = len;

      while (offset < len) {
         begin_append_offset = offset;

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
            goto fail;

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

         struct foz_db_entry *entry = ralloc(foz_db->mem_ctx,
                                             struct foz_db_entry);
         entry->header = *header;
         entry->file_idx = file_idx;
         _mesa_sha1_hex_to_sha1(entry->key, hash_str);

         /* read cache item offset from index file */
         uint64_t cache_offset;
         if (fread(&cache_offset, 1, sizeof(cache_offset), db_idx) !=
             sizeof(cache_offset))
            return false;

         entry->offset = cache_offset;

         /* Truncate the entry's hash string to a 64bit hash for use with a
          * 64bit hash table for looking up file offsets.
          */
         hash_str[16] = '\0';
         uint64_t key = strtoull(hash_str, NULL, 16);
         _mesa_hash_table_u64_insert(foz_db->index_db, key, entry);

         offset += header->payload_size;
      }

      if (!read_only && offset != len) {
         if (fseek(db_idx, begin_append_offset, SEEK_SET) < 0)
            goto fail;
      }
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
   }

   foz_db->alive = true;
   return true;

fail:
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

      if (!check_files_opened_successfully(foz_db->file[file_idx], db_idx))
         continue; /* Ignore invalid user provided filename and continue */

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
   fclose(foz_db->db_idx);
   for (unsigned i = 0; i < FOZ_MAX_DBS; i++) {
      if (foz_db->file[i])
         fclose(foz_db->file[i]);
   }

   if (foz_db->mem_ctx) {
      _mesa_hash_table_u64_destroy(foz_db->index_db);
      ralloc_free(foz_db->mem_ctx);
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
      simple_mtx_unlock(&foz_db->mtx);
      return NULL;
   }

   uint8_t file_idx = entry->file_idx;
   off_t offset = ftell(foz_db->file[file_idx]);
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

   /* Reset file offset to the end of the file ready for writing */
   fseek(foz_db->file[file_idx], offset, SEEK_SET);

   return data;

fail:
   free(data);

   /* reading db entry failed. reset the file offset */
   fseek(foz_db->file[file_idx], offset, SEEK_SET);
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

   simple_mtx_lock(&foz_db->mtx);

   struct foz_db_entry *entry =
      _mesa_hash_table_u64_search(foz_db->index_db, hash);
   if (entry) {
      simple_mtx_unlock(&foz_db->mtx);
      return NULL;
   }

   /* Prepare db entry header and blob ready for writing */
   struct foz_payload_header header;
   header.uncompressed_size = blob_size;
   header.format = FOSSILIZE_COMPRESSION_NONE;
   header.payload_size = blob_size;
   header.crc = util_hash_crc32(blob, blob_size);

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

   return true;

fail:
   simple_mtx_unlock(&foz_db->mtx);
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
