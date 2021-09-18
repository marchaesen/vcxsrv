/*
 * Copyright (C) 2017-2019 Alyssa Rosenzweig
 * Copyright (C) 2017-2019 Connor Abbott
 * Copyright (C) 2019 Collabora, Ltd.
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
 */

#include <agx_pack.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/mman.h>

#include "decode.h"
#include "io.h"
#include "hexdump.h"

static const char *agx_alloc_types[AGX_NUM_ALLOC] = { "mem", "map", "cmd" };

static void
agx_disassemble(void *_code, size_t maxlen, FILE *fp)
{
   /* stub */
}

FILE *agxdecode_dump_stream;

#define MAX_MAPPINGS 4096

struct agx_bo mmap_array[MAX_MAPPINGS];
unsigned mmap_count = 0;

struct agx_bo *ro_mappings[MAX_MAPPINGS];
unsigned ro_mapping_count = 0;

static struct agx_bo *
agxdecode_find_mapped_gpu_mem_containing_rw(uint64_t addr)
{
   for (unsigned i = 0; i < mmap_count; ++i) {
      if (mmap_array[i].type == AGX_ALLOC_REGULAR && addr >= mmap_array[i].ptr.gpu && (addr - mmap_array[i].ptr.gpu) < mmap_array[i].size)
         return mmap_array + i;
   }

   return NULL;
}

static struct agx_bo *
agxdecode_find_mapped_gpu_mem_containing(uint64_t addr)
{
   struct agx_bo *mem = agxdecode_find_mapped_gpu_mem_containing_rw(addr);

   if (mem && mem->ptr.cpu && !mem->ro) {
      mprotect(mem->ptr.cpu, mem->size, PROT_READ);
      mem->ro = true;
      ro_mappings[ro_mapping_count++] = mem;
      assert(ro_mapping_count < MAX_MAPPINGS);
   }

   if (mem && !mem->mapped) {
      fprintf(stderr, "[ERROR] access to memory not mapped (GPU %" PRIx64 ", handle %u)\n", mem->ptr.gpu, mem->handle);
   }

   return mem;
}

static struct agx_bo *
agxdecode_find_handle(unsigned handle, unsigned type)
{
   for (unsigned i = 0; i < mmap_count; ++i) {
      if (mmap_array[i].type != type)
         continue;

      if (mmap_array[i].handle != handle)
         continue;

      return &mmap_array[i];
   }

   return NULL;
}

static void
agxdecode_mark_mapped(unsigned handle)
{
   struct agx_bo *bo = agxdecode_find_handle(handle, AGX_ALLOC_REGULAR);

   if (!bo) {
      fprintf(stderr, "ERROR - unknown BO mapped with handle %u\n", handle);
      return;
   }

   /* Mark mapped for future consumption */
   bo->mapped = true;
}

static void
agxdecode_validate_map(void *map)
{
   unsigned nr_handles = 0;

   /* First, mark everything unmapped */
   for (unsigned i = 0; i < mmap_count; ++i)
      mmap_array[i].mapped = false;

   /* Check the header */
   struct agx_map_header *hdr = map;
   if (hdr->nr_entries == 0) {
      fprintf(stderr, "ERROR - empty map\n");
      return;
   }

   for (unsigned i = 0; i < 6; ++i) {
      unsigned handle = hdr->indices[i];
      if (handle) {
         agxdecode_mark_mapped(handle);
         nr_handles++;
      }
   }

   /* Check the entries */
   struct agx_map_entry *entries = (struct agx_map_entry *) (&hdr[1]);
   for (unsigned i = 0; i < hdr->nr_entries - 1; ++i) {
      struct agx_map_entry entry = entries[i];
      
      for (unsigned j = 0; j < 6; ++j) {
         unsigned handle = entry.indices[j];
         if (handle) {
            agxdecode_mark_mapped(handle);
            nr_handles++;
         }
      }
   }

   /* Check the sentinel */
   if (entries[hdr->nr_entries - 1].indices[0]) {
      fprintf(stderr, "ERROR - last entry nonzero %u\n", entries[hdr->nr_entries - 1].indices[0]);
      return;
   }

   /* Check the handle count */
   if (nr_handles != hdr->nr_handles) {
      fprintf(stderr, "ERROR - wrong handle count, got %u, expected %u\n",
            nr_handles, hdr->nr_handles);
   }
}

static inline void *
__agxdecode_fetch_gpu_mem(const struct agx_bo *mem,
                          uint64_t gpu_va, size_t size,
                          int line, const char *filename)
{
   if (!mem)
      mem = agxdecode_find_mapped_gpu_mem_containing(gpu_va);

   if (!mem) {
      fprintf(stderr, "Access to unknown memory %" PRIx64 " in %s:%d\n",
              gpu_va, filename, line);
      fflush(agxdecode_dump_stream);
      assert(0);
   }

   assert(mem);
   assert(size + (gpu_va - mem->ptr.gpu) <= mem->size);

   return mem->ptr.cpu + gpu_va - mem->ptr.gpu;
}

#define agxdecode_fetch_gpu_mem(gpu_va, size) \
	__agxdecode_fetch_gpu_mem(NULL, gpu_va, size, __LINE__, __FILE__)

static void
agxdecode_map_read_write(void)
{
   for (unsigned i = 0; i < ro_mapping_count; ++i) {
      ro_mappings[i]->ro = false;
      mprotect(ro_mappings[i]->ptr.cpu, ro_mappings[i]->size,
               PROT_READ | PROT_WRITE);
   }

   ro_mapping_count = 0;
}

/* Helpers for parsing the cmdstream */

#define DUMP_UNPACKED(T, var, str) { \
        agxdecode_log(str); \
        agx_print(agxdecode_dump_stream, T, var, (agxdecode_indent + 1) * 2); \
}

#define DUMP_CL(T, cl, str) {\
        agx_unpack(agxdecode_dump_stream, cl, T, temp); \
        DUMP_UNPACKED(T, temp, str "\n"); \
}

#define agxdecode_log(str) fputs(str, agxdecode_dump_stream)
#define agxdecode_msg(str) fprintf(agxdecode_dump_stream, "// %s", str)

unsigned agxdecode_indent = 0;
uint64_t pipeline_base = 0;

static void
agxdecode_dump_bo(struct agx_bo *bo, const char *name)
{
   fprintf(agxdecode_dump_stream, "%s %s (%u)\n", name, bo->name ?: "", bo->handle);
   hexdump(agxdecode_dump_stream, bo->ptr.cpu, bo->size, false);
}

/* Abstraction for command stream parsing */
typedef unsigned (*decode_cmd)(const uint8_t *map, bool verbose);

#define STATE_DONE (0xFFFFFFFFu)

static void
agxdecode_stateful(uint64_t va, const char *label, decode_cmd decoder, bool verbose)
{
   struct agx_bo *alloc = agxdecode_find_mapped_gpu_mem_containing(va);
   assert(alloc != NULL && "nonexistant object");
   fprintf(agxdecode_dump_stream, "%s (%" PRIx64 ", handle %u)\n", label, va, alloc->handle);
   fflush(agxdecode_dump_stream);

   uint8_t *map = agxdecode_fetch_gpu_mem(va, 64);
   uint8_t *end = (uint8_t *) alloc->ptr.cpu + alloc->size;

   if (verbose)
      agxdecode_dump_bo(alloc, label);
   fflush(agxdecode_dump_stream);

   while (map < end) {
      unsigned count = decoder(map, verbose);

      /* If we fail to decode, default to a hexdump (don't hang) */
      if (count == 0) {
         hexdump(agxdecode_dump_stream, map, 8, false);
         count = 8;
      }

      map += count;
      fflush(agxdecode_dump_stream);

      if (count == STATE_DONE)
         break;
   }
}

unsigned COUNTER = 0;
static unsigned
agxdecode_pipeline(const uint8_t *map, UNUSED bool verbose)
{
   uint8_t zeroes[16] = { 0 };

   if (map[0] == 0x4D && map[1] == 0xbd) {
      /* TODO: Disambiguation for extended is a guess */
      agx_unpack(agxdecode_dump_stream, map, SET_SHADER_EXTENDED, cmd);
      DUMP_UNPACKED(SET_SHADER_EXTENDED, cmd, "Set shader\n");

      if (cmd.preshader_mode == AGX_PRESHADER_MODE_PRESHADER) {
         agxdecode_log("Preshader\n");
         agx_disassemble(agxdecode_fetch_gpu_mem(cmd.preshader_code, 2048),
                         2048, agxdecode_dump_stream);
         agxdecode_log("\n---\n");
      }

      agxdecode_log("\n");
      agx_disassemble(agxdecode_fetch_gpu_mem(cmd.code, 2048),
                      2048, agxdecode_dump_stream);
      agxdecode_log("\n");

      char *name;
      asprintf(&name, "file%u.bin", COUNTER++);
      FILE *fp = fopen(name, "wb");
      fwrite(agxdecode_fetch_gpu_mem(cmd.code, 2048), 1, 2048, fp);
      fclose(fp);
      free(name);
      agxdecode_log("\n");

      return AGX_SET_SHADER_EXTENDED_LENGTH;
   } else if (map[0] == 0x4D) {
      agx_unpack(agxdecode_dump_stream, map, SET_SHADER, cmd);
      DUMP_UNPACKED(SET_SHADER, cmd, "Set shader\n");
      fflush(agxdecode_dump_stream);

      if (cmd.preshader_mode == AGX_PRESHADER_MODE_PRESHADER) {
         agxdecode_log("Preshader\n");
         agx_disassemble(agxdecode_fetch_gpu_mem(cmd.preshader_code, 2048),
                         2048, agxdecode_dump_stream);
         agxdecode_log("\n---\n");
      }

      agxdecode_log("\n");
      agx_disassemble(agxdecode_fetch_gpu_mem(cmd.code, 2048),
                      2048, agxdecode_dump_stream);
      char *name;
      asprintf(&name, "file%u.bin", COUNTER++);
      FILE *fp = fopen(name, "wb");
      fwrite(agxdecode_fetch_gpu_mem(cmd.code, 2048), 1, 2048, fp);
      fclose(fp);
      free(name);
      agxdecode_log("\n");

      return AGX_SET_SHADER_LENGTH;
   } else if (map[0] == 0xDD) {
      agx_unpack(agxdecode_dump_stream, map, BIND_TEXTURE, temp);
      DUMP_UNPACKED(BIND_TEXTURE, temp, "Bind texture\n");

      uint8_t *tex = agxdecode_fetch_gpu_mem(temp.buffer, 64);
      DUMP_CL(TEXTURE, tex, "Texture");
      hexdump(agxdecode_dump_stream, tex + AGX_TEXTURE_LENGTH, 64 - AGX_TEXTURE_LENGTH, false);

      return AGX_BIND_TEXTURE_LENGTH;
   } else if (map[0] == 0x9D) {
      agx_unpack(agxdecode_dump_stream, map, BIND_SAMPLER, temp);
      DUMP_UNPACKED(BIND_SAMPLER, temp, "Bind sampler\n");

      uint8_t *samp = agxdecode_fetch_gpu_mem(temp.buffer, 64);
      DUMP_CL(SAMPLER, samp, "Sampler");
      hexdump(agxdecode_dump_stream, samp + AGX_SAMPLER_LENGTH, 64 - AGX_SAMPLER_LENGTH, false);

      return AGX_BIND_SAMPLER_LENGTH;
   } else if (map[0] == 0x1D) {
      DUMP_CL(BIND_UNIFORM, map, "Bind uniform");
      return AGX_BIND_UNIFORM_LENGTH;
   } else if (memcmp(map, zeroes, 16) == 0) {
      /* TODO: Termination */
      return STATE_DONE;
   } else {
      return 0;
   }
}

static void
agxdecode_record(uint64_t va, size_t size, bool verbose)
{
   uint8_t *map = agxdecode_fetch_gpu_mem(va, size);
   uint32_t tag = 0;
   memcpy(&tag, map, 4);

   if (tag == 0x00000C00) {
      assert(size == AGX_VIEWPORT_LENGTH);
      DUMP_CL(VIEWPORT, map, "Viewport");
   } else if (tag == 0x0C020000) {
      assert(size == AGX_LINKAGE_LENGTH);
      DUMP_CL(LINKAGE, map, "Linkage");
   } else if (tag == 0x10000b5) {
      assert(size == AGX_RASTERIZER_LENGTH);
      DUMP_CL(RASTERIZER, map, "Rasterizer");
   } else if (tag == 0x200000) {
      assert(size == AGX_CULL_LENGTH);
      DUMP_CL(CULL, map, "Cull");
   } else if (tag == 0x800000) {
      assert(size == (AGX_BIND_PIPELINE_LENGTH + 4));

      agx_unpack(agxdecode_dump_stream, map, BIND_PIPELINE, cmd);
      agxdecode_stateful(cmd.pipeline, "Pipeline", agxdecode_pipeline, verbose);

      /* TODO: parse */
      if (cmd.fs_varyings) {
         uint8_t *map = agxdecode_fetch_gpu_mem(cmd.fs_varyings, 128);
         hexdump(agxdecode_dump_stream, map, 128, false);

         DUMP_CL(VARYING_HEADER, map, "Varying header:");
         map += AGX_VARYING_HEADER_LENGTH;

         for (unsigned i = 0; i < cmd.input_count; ++i) {
            DUMP_CL(VARYING, map, "Varying:");
            map += AGX_VARYING_LENGTH;
         }
      }

      DUMP_UNPACKED(BIND_PIPELINE, cmd, "Bind fragment pipeline\n");
   } else if (size == 0) {
      pipeline_base = va;
   } else {
      fprintf(agxdecode_dump_stream, "Record %" PRIx64 "\n", va);
      hexdump(agxdecode_dump_stream, map, size, false);
   }
}

static unsigned
agxdecode_cmd(const uint8_t *map, bool verbose)
{
   if (map[0] == 0x02 && map[1] == 0x10 && map[2] == 0x00 && map[3] == 0x00) {
      agx_unpack(agxdecode_dump_stream, map, LAUNCH, cmd);
      agxdecode_stateful(cmd.pipeline, "Pipeline", agxdecode_pipeline, verbose);
      DUMP_UNPACKED(LAUNCH, cmd, "Launch\n");
      return AGX_LAUNCH_LENGTH;
   } else if (map[0] == 0x2E && map[1] == 0x00 && map[2] == 0x00 && map[3] == 0x40) {
      agx_unpack(agxdecode_dump_stream, map, BIND_PIPELINE, cmd);
      agxdecode_stateful(cmd.pipeline, "Pipeline", agxdecode_pipeline, verbose);
      DUMP_UNPACKED(BIND_PIPELINE, cmd, "Bind vertex pipeline\n");

      /* Random unaligned null byte, it's pretty awful.. */
      if (map[AGX_BIND_PIPELINE_LENGTH]) {
         fprintf(agxdecode_dump_stream, "Unk unaligned %X\n",
               map[AGX_BIND_PIPELINE_LENGTH]);
      }

      return AGX_BIND_PIPELINE_LENGTH + 1;
   } else if (map[1] == 0xc0 && map[2] == 0x61) {
      DUMP_CL(DRAW, map - 1, "Draw");
      return AGX_DRAW_LENGTH;
   } else if (map[1] == 0x00 && map[2] == 0x00) {
      /* No need to explicitly dump the record */
      agx_unpack(agxdecode_dump_stream, map, RECORD, cmd);

      /* XXX: Why? */
      if (pipeline_base && ((cmd.data >> 32) == 0)) {
         cmd.data |= pipeline_base & 0xFF00000000ull;
      }

      struct agx_bo *mem = agxdecode_find_mapped_gpu_mem_containing(cmd.data);

      if (mem)
         agxdecode_record(cmd.data, cmd.size_words * 4, verbose);
      else
         DUMP_UNPACKED(RECORD, cmd, "Non-existant record (XXX)\n");

      return AGX_RECORD_LENGTH;
   } else if (map[0] == 0 && map[1] == 0 && map[2] == 0xC0 && map[3] == 0x00) {
      ASSERTED unsigned zero[4] = { 0 };
      assert(memcmp(map + 4, zero, sizeof(zero)) == 0);
      return STATE_DONE;
   } else {
      return 0;
   }
}

void
agxdecode_cmdstream(unsigned cmdbuf_handle, unsigned map_handle, bool verbose)
{
   agxdecode_dump_file_open();

   struct agx_bo *cmdbuf = agxdecode_find_handle(cmdbuf_handle, AGX_ALLOC_CMDBUF);
   struct agx_bo *map = agxdecode_find_handle(map_handle, AGX_ALLOC_MEMMAP);
   assert(cmdbuf != NULL && "nonexistant command buffer");
   assert(map != NULL && "nonexistant mapping");

   if (verbose) {
      agxdecode_dump_bo(cmdbuf, "Command buffer");
      agxdecode_dump_bo(map, "Mapping");
   }

   /* Before decoding anything, validate the map. Set bo->mapped fields */
   agxdecode_validate_map(map->ptr.cpu);

   /* Print the IOGPU stuff */
   agx_unpack(agxdecode_dump_stream, cmdbuf->ptr.cpu, IOGPU_HEADER, cmd);
   DUMP_UNPACKED(IOGPU_HEADER, cmd, "IOGPU Header\n");
   assert(cmd.attachment_offset_1 == cmd.attachment_offset_2);

   uint32_t *attachments = (uint32_t *) ((uint8_t *) cmdbuf->ptr.cpu + cmd.attachment_offset_1);
   unsigned attachment_count = attachments[3];
   for (unsigned i = 0; i < attachment_count; ++i) {
      uint32_t *ptr = attachments + 4 + (i * AGX_IOGPU_ATTACHMENT_LENGTH / 4);
      DUMP_CL(IOGPU_ATTACHMENT, ptr, "Attachment");
   }

   /* TODO: What else is in here? */
   uint64_t *encoder = ((uint64_t *) cmdbuf->ptr.cpu) + 7;
   agxdecode_stateful(*encoder, "Encoder", agxdecode_cmd, verbose);

   uint64_t *clear_pipeline = ((uint64_t *) cmdbuf->ptr.cpu) + 79;
   if (*clear_pipeline) {
      assert(((*clear_pipeline) & 0xF) == 0x4);
      agxdecode_stateful((*clear_pipeline) & ~0xF, "Clear pipeline",
            agxdecode_pipeline, verbose);
   }

   uint64_t *store_pipeline = ((uint64_t *) cmdbuf->ptr.cpu) + 82;
   if (*store_pipeline) {
      assert(((*store_pipeline) & 0xF) == 0x4);
      agxdecode_stateful((*store_pipeline) & ~0xF, "Store pipeline",
            agxdecode_pipeline, verbose);
   }

   agxdecode_map_read_write();
}

void
agxdecode_dump_mappings(unsigned map_handle)
{
   agxdecode_dump_file_open();

   struct agx_bo *map = agxdecode_find_handle(map_handle, AGX_ALLOC_MEMMAP);
   assert(map != NULL && "nonexistant mapping");
   agxdecode_validate_map(map->ptr.cpu);

   for (unsigned i = 0; i < mmap_count; ++i) {
      if (!mmap_array[i].ptr.cpu || !mmap_array[i].size || !mmap_array[i].mapped)
         continue;

      assert(mmap_array[i].type < AGX_NUM_ALLOC);

      fprintf(agxdecode_dump_stream, "Buffer: type %s, gpu %" PRIx64 ", handle %u.bin:\n\n",
              agx_alloc_types[mmap_array[i].type],
              mmap_array[i].ptr.gpu, mmap_array[i].handle);

      hexdump(agxdecode_dump_stream, mmap_array[i].ptr.cpu, mmap_array[i].size, false);
      fprintf(agxdecode_dump_stream, "\n");
   }
}

void
agxdecode_track_alloc(struct agx_bo *alloc)
{
   assert((mmap_count + 1) < MAX_MAPPINGS);

   for (unsigned i = 0; i < mmap_count; ++i) {
      struct agx_bo *bo = &mmap_array[i];
      bool match = (bo->handle == alloc->handle && bo->type == alloc->type);
      assert(!match && "tried to alloc already allocated BO");
   }

   mmap_array[mmap_count++] = *alloc;
}

void
agxdecode_track_free(struct agx_bo *bo)
{
   bool found = false;

   for (unsigned i = 0; i < mmap_count; ++i) {
      if (mmap_array[i].handle == bo->handle && mmap_array[i].type == bo->type) {
         assert(!found && "mapped multiple times!");
         found = true;

         memset(&mmap_array[i], 0, sizeof(mmap_array[i]));
      }
   }

   assert(found && "freed unmapped memory");
}

static int agxdecode_dump_frame_count = 0;

void
agxdecode_dump_file_open(void)
{
   if (agxdecode_dump_stream)
      return;

   /* This does a getenv every frame, so it is possible to use
    * setenv to change the base at runtime.
    */
   const char *dump_file_base = getenv("PANDECODE_DUMP_FILE") ?: "agxdecode.dump";
   if (!strcmp(dump_file_base, "stderr"))
      agxdecode_dump_stream = stderr;
   else {
      char buffer[1024];
      snprintf(buffer, sizeof(buffer), "%s.%04d", dump_file_base, agxdecode_dump_frame_count);
      printf("agxdecode: dump command stream to file %s\n", buffer);
      agxdecode_dump_stream = fopen(buffer, "w");
      if (!agxdecode_dump_stream)
         fprintf(stderr,
                 "agxdecode: failed to open command stream log file %s\n",
                 buffer);
   }
}

static void
agxdecode_dump_file_close(void)
{
   if (agxdecode_dump_stream && agxdecode_dump_stream != stderr) {
      fclose(agxdecode_dump_stream);
      agxdecode_dump_stream = NULL;
   }
}

void
agxdecode_next_frame(void)
{
   agxdecode_dump_file_close();
   agxdecode_dump_frame_count++;
}

void
agxdecode_close(void)
{
   agxdecode_dump_file_close();
}
