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

#pragma once
#ifndef BLOB_H
#define BLOB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The blob functions implement a simple, low-level API for serializing and
 * deserializing.
 *
 * All objects written to a blob will be serialized directly, (without any
 * additional meta-data to describe the data written). Therefore, it is the
 * caller's responsibility to ensure that any data can be read later, (either
 * by knowing exactly what data is expected, or by writing to the blob
 * sufficient meta-data to describe what has been written).
 *
 * A blob is efficient in that it dynamically grows by doubling in size, so
 * allocation costs are logarithmic.
 */

struct blob {
   /* The data actually written to the blob. */
   uint8_t *data;

   /** Number of bytes that have been allocated for \c data. */
   size_t allocated;

   /** The number of bytes that have actual data written to them. */
   size_t size;
};

/* When done reading, the caller can ensure that everything was consumed by
 * checking the following:
 *
 *   1. blob->current should be equal to blob->end, (if not, too little was
 *      read).
 *
 *   2. blob->overrun should be false, (otherwise, too much was read).
 */
struct blob_reader {
   uint8_t *data;
   uint8_t *end;
   uint8_t *current;
   bool overrun;
};

/**
 * Create a new, empty blob, belonging to \mem_ctx.
 *
 * \return The new blob, (or NULL in case of allocation failure).
 */
struct blob *
blob_create (void *mem_ctx);

/**
 * Add some unstructured, fixed-size data to a blob.
 *
 * \return True unless allocation failed.
 */
bool
blob_write_bytes (struct blob *blob, const void *bytes, size_t to_write);

/**
 * Reserve space in \blob for a number of bytes.
 *
 * Space will be allocated within the blob for these byes, but the bytes will
 * be left uninitialized. The caller is expected to use the return value to
 * write directly (and immediately) to these bytes.
 *
 * \note The return value is valid immediately upon return, but can be
 * invalidated by any other call to a blob function. So the caller should call
 * blob_reserve_byes immediately before writing through the returned pointer.
 *
 * This function is intended to be used when interfacing with an existing API
 * that is not aware of the blob API, (so that blob_write_bytes cannot be
 * called).
 *
 * \return A pointer to space allocated within \blob to which \to_write bytes
 * can be written, (or NULL in case of any allocation error).
 */
uint8_t *
blob_reserve_bytes (struct blob *blob, size_t to_write);

/**
 * Overwrite some data previously written to the blob.
 *
 * Writes data to an existing portion of the blob at an offset of \offset.
 * This data range must have previously been written to the blob by one of the
 * blob_write_* calls.
 *
 * For example usage, see blob_overwrite_uint32
 *
 * \return True unless the requested offset or offset+to_write lie outside
 * the current blob's size.
 */
bool
blob_overwrite_bytes (struct blob *blob,
                      size_t offset,
                      const void *bytes,
                      size_t to_write);

/**
 * Add a uint32_t to a blob.
 *
 * \note This function will only write to a uint32_t-aligned offset from the
 * beginning of the blob's data, so some padding bytes may be added to the
 * blob if this write follows some unaligned write (such as
 * blob_write_string).
 *
 * \return True unless allocation failed.
 */
bool
blob_write_uint32 (struct blob *blob, uint32_t value);

/**
 * Overwrite a uint32_t previously written to the blob.
 *
 * Writes a uint32_t value to an existing portion of the blob at an offset of
 * \offset.  This data range must have previously been written to the blob by
 * one of the blob_write_* calls.
 *
 *
 * The expected usage is something like the following pattern:
 *
 *	size_t offset;
 *
 *	offset = blob->size;
 *	blob_write_uint32 (blob, 0); // placeholder
 *	... various blob write calls, writing N items ...
 *	blob_overwrite_uint32 (blob, offset, N);
 *
 * \return True unless the requested position or position+to_write lie outside
 * the current blob's size.
 */
bool
blob_overwrite_uint32 (struct blob *blob,
                       size_t offset,
                       uint32_t value);

/**
 * Add a uint64_t to a blob.
 *
 * \note This function will only write to a uint64_t-aligned offset from the
 * beginning of the blob's data, so some padding bytes may be added to the
 * blob if this write follows some unaligned write (such as
 * blob_write_string).
 *
 * \return True unless allocation failed.
 */
bool
blob_write_uint64 (struct blob *blob, uint64_t value);

/**
 * Add an intptr_t to a blob.
 *
 * \note This function will only write to an intptr_t-aligned offset from the
 * beginning of the blob's data, so some padding bytes may be added to the
 * blob if this write follows some unaligned write (such as
 * blob_write_string).
 *
 * \return True unless allocation failed.
 */
bool
blob_write_intptr (struct blob *blob, intptr_t value);

/**
 * Add a NULL-terminated string to a blob, (including the NULL terminator).
 *
 * \return True unless allocation failed.
 */
bool
blob_write_string (struct blob *blob, const char *str);

/**
 * Start reading a blob, (initializing the contents of \blob for reading).
 *
 * After this call, the caller can use the various blob_read_* functions to
 * read elements from the data array.
 *
 * For all of the blob_read_* functions, if there is insufficient data
 * remaining, the functions will do nothing, (perhaps returning default values
 * such as 0). The caller can detect this by noting that the blob_reader's
 * current value is unchanged before and after the call.
 */
void
blob_reader_init (struct blob_reader *blob, uint8_t *data, size_t size);

/**
 * Read some unstructured, fixed-size data from the current location, (and
 * update the current location to just past this data).
 *
 * \note The memory returned belongs to the data underlying the blob reader. The
 * caller must copy the data in order to use it after the lifetime of the data
 * underlying the blob reader.
 *
 * \return The bytes read (see note above about memory lifetime).
 */
void *
blob_read_bytes (struct blob_reader *blob, size_t size);

/**
 * Read some unstructured, fixed-size data from the current location, copying
 * it to \dest (and update the current location to just past this data)
 */
void
blob_copy_bytes (struct blob_reader *blob, uint8_t *dest, size_t size);

/**
 * Read a uint32_t from the current location, (and update the current location
 * to just past this uint32_t).
 *
 * \note This function will only read from a uint32_t-aligned offset from the
 * beginning of the blob's data, so some padding bytes may be skipped.
 *
 * \return The uint32_t read
 */
uint32_t
blob_read_uint32 (struct blob_reader *blob);

/**
 * Read a uint64_t from the current location, (and update the current location
 * to just past this uint64_t).
 *
 * \note This function will only read from a uint64_t-aligned offset from the
 * beginning of the blob's data, so some padding bytes may be skipped.
 *
 * \return The uint64_t read
 */
uint64_t
blob_read_uint64 (struct blob_reader *blob);

/**
 * Read an intptr_t value from the current location, (and update the
 * current location to just past this intptr_t).
 *
 * \note This function will only read from an intptr_t-aligned offset from the
 * beginning of the blob's data, so some padding bytes may be skipped.
 *
 * \return The intptr_t read
 */
intptr_t
blob_read_intptr (struct blob_reader *blob);

/**
 * Read a NULL-terminated string from the current location, (and update the
 * current location to just past this string).
 *
 * \note The memory returned belongs to the data underlying the blob reader. The
 * caller must copy the string in order to use the string after the lifetime
 * of the data underlying the blob reader.
 *
 * \return The string read (see note above about memory lifetime). However, if
 * there is no NULL byte remaining within the blob, this function returns
 * NULL.
 */
char *
blob_read_string (struct blob_reader *blob);

#ifdef __cplusplus
}
#endif

#endif /* BLOB_H */
