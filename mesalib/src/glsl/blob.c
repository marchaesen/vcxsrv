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

#include <string.h>

#include "main/macros.h"
#include "util/ralloc.h"
#include "blob.h"

#define BLOB_INITIAL_SIZE 4096

/* Ensure that \blob will be able to fit an additional object of size
 * \additional.  The growing (if any) will occur by doubling the existing
 * allocation.
 */
static bool
grow_to_fit(struct blob *blob, size_t additional)
{
   size_t to_allocate;
   uint8_t *new_data;

   if (blob->size + additional <= blob->allocated)
      return true;

   if (blob->allocated == 0)
      to_allocate = BLOB_INITIAL_SIZE;
   else
      to_allocate = blob->allocated * 2;

   to_allocate = MAX2(to_allocate, blob->allocated + additional);

   new_data = reralloc_size(blob, blob->data, to_allocate);
   if (new_data == NULL)
      return false;

   blob->data = new_data;
   blob->allocated = to_allocate;

   return true;
}

/* Align the blob->size so that reading or writing a value at (blob->data +
 * blob->size) will result in an access aligned to a granularity of \alignment
 * bytes.
 *
 * \return True unless allocation fails
 */
static bool
align_blob(struct blob *blob, size_t alignment)
{
   const size_t new_size = ALIGN(blob->size, alignment);

   if (! grow_to_fit (blob, new_size - blob->size))
      return false;

   blob->size = new_size;

   return true;
}

static void
align_blob_reader(struct blob_reader *blob, size_t alignment)
{
   blob->current = blob->data + ALIGN(blob->current - blob->data, alignment);
}

struct blob *
blob_create(void *mem_ctx)
{
   struct blob *blob;

   blob = ralloc(mem_ctx, struct blob);
   if (blob == NULL)
      return NULL;

   blob->data = NULL;
   blob->allocated = 0;
   blob->size = 0;

   return blob;
}

bool
blob_overwrite_bytes(struct blob *blob,
                     size_t offset,
                     const void *bytes,
                     size_t to_write)
{
   /* Detect an attempt to overwrite data out of bounds. */
   if (offset < 0 || blob->size - offset < to_write)
      return false;

   memcpy(blob->data + offset, bytes, to_write);

   return true;
}

bool
blob_write_bytes(struct blob *blob, const void *bytes, size_t to_write)
{
   if (! grow_to_fit(blob, to_write))
       return false;

   memcpy(blob->data + blob->size, bytes, to_write);
   blob->size += to_write;

   return true;
}

uint8_t *
blob_reserve_bytes(struct blob *blob, size_t to_write)
{
   uint8_t *ret;

   if (! grow_to_fit (blob, to_write))
      return NULL;

   ret = blob->data + blob->size;
   blob->size += to_write;

   return ret;
}

bool
blob_write_uint32(struct blob *blob, uint32_t value)
{
   align_blob(blob, sizeof(value));

   return blob_write_bytes(blob, &value, sizeof(value));
}

bool
blob_overwrite_uint32 (struct blob *blob,
                       size_t offset,
                       uint32_t value)
{
   return blob_overwrite_bytes(blob, offset, &value, sizeof(value));
}

bool
blob_write_uint64(struct blob *blob, uint64_t value)
{
   align_blob(blob, sizeof(value));

   return blob_write_bytes(blob, &value, sizeof(value));
}

bool
blob_write_intptr(struct blob *blob, intptr_t value)
{
   align_blob(blob, sizeof(value));

   return blob_write_bytes(blob, &value, sizeof(value));
}

bool
blob_write_string(struct blob *blob, const char *str)
{
   return blob_write_bytes(blob, str, strlen(str) + 1);
}

void
blob_reader_init(struct blob_reader *blob, uint8_t *data, size_t size)
{
   blob->data = data;
   blob->end = data + size;
   blob->current = data;
   blob->overrun = false;
}

/* Check that an object of size \size can be read from this blob.
 *
 * If not, set blob->overrun to indicate that we attempted to read too far.
 */
static bool
ensure_can_read(struct blob_reader *blob, size_t size)
{
   if (blob->current < blob->end && blob->end - blob->current >= size)
      return true;

   blob->overrun = true;

   return false;
}

void *
blob_read_bytes(struct blob_reader *blob, size_t size)
{
   void *ret;

   if (! ensure_can_read (blob, size))
      return NULL;

   ret = blob->current;

   blob->current += size;

   return ret;
}

void
blob_copy_bytes(struct blob_reader *blob, uint8_t *dest, size_t size)
{
   uint8_t *bytes;

   bytes = blob_read_bytes(blob, size);
   if (bytes == NULL)
      return;

   memcpy(dest, bytes, size);
}

/* These next three read functions have identical form. If we add any beyond
 * these first three we should probably switch to generating these with a
 * preprocessor macro.
*/
uint32_t
blob_read_uint32(struct blob_reader *blob)
{
   uint32_t ret;
   int size = sizeof(ret);

   align_blob_reader(blob, size);

   if (! ensure_can_read(blob, size))
      return 0;

   ret = *((uint32_t*) blob->current);

   blob->current += size;

   return ret;
}

uint64_t
blob_read_uint64(struct blob_reader *blob)
{
   uint64_t ret;
   int size = sizeof(ret);

   align_blob_reader(blob, size);

   if (! ensure_can_read(blob, size))
      return 0;

   ret = *((uint64_t*) blob->current);

   blob->current += size;

   return ret;
}

intptr_t
blob_read_intptr(struct blob_reader *blob)
{
   intptr_t ret;
   int size = sizeof(ret);

   align_blob_reader(blob, size);

   if (! ensure_can_read(blob, size))
      return 0;

   ret = *((intptr_t *) blob->current);

   blob->current += size;

   return ret;
}

char *
blob_read_string(struct blob_reader *blob)
{
   int size;
   char *ret;
   uint8_t *nul;

   /* If we're already at the end, then this is an overrun. */
   if (blob->current >= blob->end) {
      blob->overrun = true;
      return NULL;
   }

   /* Similarly, if there is no zero byte in the data remaining in this blob,
    * we also consider that an overrun.
    */
   nul = memchr(blob->current, 0, blob->end - blob->current);

   if (nul == NULL) {
      blob->overrun = true;
      return NULL;
   }

   size = nul - blob->current + 1;

   assert(ensure_can_read(blob, size));

   ret = (char *) blob->current;

   blob->current += size;

   return ret;
}
