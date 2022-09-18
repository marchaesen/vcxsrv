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
agxdecode_decode_segment_list(void *segment_list)
{
   unsigned nr_handles = 0;

   /* First, mark everything unmapped */
   for (unsigned i = 0; i < mmap_count; ++i)
      mmap_array[i].mapped = false;

   /* Check the header */
   struct agx_map_header *hdr = segment_list;
   if (hdr->resource_group_count == 0) {
      fprintf(agxdecode_dump_stream, "ERROR - empty map\n");
      return;
   }

   if (hdr->segment_count != 1) {
      fprintf(agxdecode_dump_stream, "ERROR - can't handle segment count %u\n",
            hdr->segment_count);
   }

   fprintf(agxdecode_dump_stream, "Segment list:\n");
   fprintf(agxdecode_dump_stream, "  Command buffer shmem ID: %" PRIx64 "\n", hdr->cmdbuf_id);
   fprintf(agxdecode_dump_stream, "  Encoder ID: %" PRIx64 "\n", hdr->encoder_id);
   fprintf(agxdecode_dump_stream, "  Kernel commands start offset: %u\n",
         hdr->kernel_commands_start_offset);
   fprintf(agxdecode_dump_stream, "  Kernel commands end offset: %u\n",
         hdr->kernel_commands_end_offset);
   fprintf(agxdecode_dump_stream, "  Unknown: 0x%X\n", hdr->unk);

   /* Expected structure: header followed by resource groups */
   size_t length = sizeof(struct agx_map_header);
   length += sizeof(struct agx_map_entry) * hdr->resource_group_count;

   if (length != hdr->length) {
      fprintf(agxdecode_dump_stream, "ERROR: expected length %zu, got %u\n",
              length, hdr->length);
   }

   if (hdr->padding[0] || hdr->padding[1])
      fprintf(agxdecode_dump_stream, "ERROR - padding tripped\n");

   /* Check the entries */
   struct agx_map_entry *groups = ((void *) hdr) + sizeof(*hdr);
   for (unsigned i = 0; i < hdr->resource_group_count; ++i) {
      struct agx_map_entry group = groups[i];
      unsigned count = group.resource_count;

      STATIC_ASSERT(ARRAY_SIZE(group.resource_id) == 6);
      STATIC_ASSERT(ARRAY_SIZE(group.resource_unk) == 6);
      STATIC_ASSERT(ARRAY_SIZE(group.resource_flags) == 6);

      if ((count < 1) || (count > 6)) {
         fprintf(agxdecode_dump_stream, "ERROR - invalid count %u\n", count);
         continue;
      }
      
      for (unsigned j = 0; j < count; ++j) {
         unsigned handle = group.resource_id[j];
         unsigned unk = group.resource_unk[j];
         unsigned flags = group.resource_flags[j];

         if (!handle) {
            fprintf(agxdecode_dump_stream, "ERROR - invalid handle %u\n", handle);
            continue;
         }

         agxdecode_mark_mapped(handle);
         nr_handles++;

         fprintf(agxdecode_dump_stream, "%u (0x%X, 0x%X)\n", handle, unk, flags);
      }

      if (group.unka)
         fprintf(agxdecode_dump_stream, "ERROR - unknown 0x%X\n", group.unka);

      /* Visual separator for resource groups */
      fprintf(agxdecode_dump_stream, "\n");
   }

   /* Check the handle count */
   if (nr_handles != hdr->total_resources) {
      fprintf(agxdecode_dump_stream, "ERROR - wrong handle count, got %u, expected %u (%u entries)\n",
            nr_handles, hdr->total_resources, hdr->resource_group_count);
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

static void
agxdecode_dump_bo(struct agx_bo *bo, const char *name)
{
   fprintf(agxdecode_dump_stream, "%s %s (%u)\n", name, bo->name ?: "", bo->handle);
   hexdump(agxdecode_dump_stream, bo->ptr.cpu, bo->size, false);
}

/* Abstraction for command stream parsing */
typedef unsigned (*decode_cmd)(const uint8_t *map, uint64_t *link, bool verbose);

#define STATE_DONE (0xFFFFFFFFu)
#define STATE_LINK (0xFFFFFFFEu)

static void
agxdecode_stateful(uint64_t va, const char *label, decode_cmd decoder, bool verbose)
{
   struct agx_bo *alloc = agxdecode_find_mapped_gpu_mem_containing(va);
   assert(alloc != NULL && "nonexistant object");
   fprintf(agxdecode_dump_stream, "%s (%" PRIx64 ", handle %u)\n", label, va, alloc->handle);
   fflush(agxdecode_dump_stream);

   uint8_t *map = agxdecode_fetch_gpu_mem(va, 64);
   uint8_t *end = (uint8_t *) alloc->ptr.cpu + alloc->size;
   uint64_t link = 0;

   if (verbose)
      agxdecode_dump_bo(alloc, label);
   fflush(agxdecode_dump_stream);

   while (map < end) {
      unsigned count = decoder(map, &link, verbose);

      /* If we fail to decode, default to a hexdump (don't hang) */
      if (count == 0) {
         hexdump(agxdecode_dump_stream, map, 8, false);
         count = 8;
      }

      map += count;
      fflush(agxdecode_dump_stream);

      if (count == STATE_DONE) {
         break;
      } else if (count == STATE_LINK) {
         alloc = agxdecode_find_mapped_gpu_mem_containing(link);
         map = agxdecode_fetch_gpu_mem(link, 64);
         end = (uint8_t *) alloc->ptr.cpu + alloc->size;
      }
   }
}

unsigned COUNTER = 0;
static unsigned
agxdecode_pipeline(const uint8_t *map, uint64_t *link, UNUSED bool verbose)
{
   uint8_t zeroes[16] = { 0 };

   if (map[0] == 0x4D && (map[11] & BITFIELD_BIT(5))) {
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

      uint8_t *tex = agxdecode_fetch_gpu_mem(temp.buffer,
            AGX_TEXTURE_LENGTH * temp.count);

      /* Note: samplers only need 8 byte alignment? */
      for (unsigned i = 0; i < temp.count; ++i) {
         agx_unpack(agxdecode_dump_stream, tex, TEXTURE, t);
         DUMP_CL(TEXTURE, tex, "Texture");
         DUMP_CL(RENDER_TARGET, tex, "Render target");

         tex += AGX_TEXTURE_LENGTH;
      }

      return AGX_BIND_TEXTURE_LENGTH;
   } else if (map[0] == 0x9D) {
      agx_unpack(agxdecode_dump_stream, map, BIND_SAMPLER, temp);
      DUMP_UNPACKED(BIND_SAMPLER, temp, "Bind sampler\n");

      uint8_t *samp = agxdecode_fetch_gpu_mem(temp.buffer,
            AGX_SAMPLER_LENGTH * temp.count);

      for (unsigned i = 0; i < temp.count; ++i) {
         DUMP_CL(SAMPLER, samp, "Sampler");
         samp += AGX_SAMPLER_LENGTH;
      }

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

#define PPP_PRINT(map, header_name, struct_name, human) \
   if (hdr.header_name) { \
      assert(((map + AGX_##struct_name##_LENGTH) <= (base + size)) && \
             "buffer overrun in PPP update"); \
      DUMP_CL(struct_name, map, human); \
      map += AGX_##struct_name##_LENGTH; \
   }

static void
agxdecode_record(uint64_t va, size_t size, bool verbose)
{
   uint8_t *base = agxdecode_fetch_gpu_mem(va, size);
   uint8_t *map = base;

   agx_unpack(agxdecode_dump_stream, map, PPP_HEADER, hdr);
   map += AGX_PPP_HEADER_LENGTH;

   PPP_PRINT(map, fragment_control, FRAGMENT_CONTROL, "Fragment control");
   PPP_PRINT(map, fragment_control_2, FRAGMENT_CONTROL_2, "Fragment control 2");
   PPP_PRINT(map, fragment_front_face, FRAGMENT_FACE, "Front face");
   PPP_PRINT(map, fragment_front_face_2, FRAGMENT_FACE_2, "Front face 2");
   PPP_PRINT(map, fragment_front_stencil, FRAGMENT_STENCIL, "Front stencil");
   PPP_PRINT(map, fragment_back_face, FRAGMENT_FACE, "Back face");
   PPP_PRINT(map, fragment_back_face_2, FRAGMENT_FACE_2, "Back face 2");
   PPP_PRINT(map, fragment_back_stencil, FRAGMENT_STENCIL, "Back stencil");
   PPP_PRINT(map, depth_bias_scissor, DEPTH_BIAS_SCISSOR, "Depth bias/scissor");
   PPP_PRINT(map, region_clip, REGION_CLIP, "Region clip");
   PPP_PRINT(map, viewport, VIEWPORT, "Viewport");
   PPP_PRINT(map, w_clamp, W_CLAMP, "W clamp");
   PPP_PRINT(map, output_select, OUTPUT_SELECT, "Output select");
   PPP_PRINT(map, varying_word_0, VARYING_0, "Varying word 0");
   PPP_PRINT(map, varying_word_1, VARYING_1, "Varying word 1");
   PPP_PRINT(map, cull, CULL, "Cull");
   PPP_PRINT(map, cull_2, CULL_2, "Cull 2");

   if (hdr.fragment_shader) {
      agx_unpack(agxdecode_dump_stream, map, FRAGMENT_SHADER, frag);
      agxdecode_stateful(frag.pipeline, "Fragment pipeline", agxdecode_pipeline, verbose);

      if (frag.cf_bindings) {
         uint8_t *cf = agxdecode_fetch_gpu_mem(frag.cf_bindings, 128);
         hexdump(agxdecode_dump_stream, cf, 128, false);

         DUMP_CL(CF_BINDING_HEADER, cf, "Coefficient binding header:");
         cf += AGX_CF_BINDING_HEADER_LENGTH;

         for (unsigned i = 0; i < frag.cf_binding_count; ++i) {
            DUMP_CL(CF_BINDING, cf, "Coefficient binding:");
            cf += AGX_CF_BINDING_LENGTH;
         }
      }

      DUMP_UNPACKED(FRAGMENT_SHADER, frag, "Fragment shader\n");
      map += AGX_FRAGMENT_SHADER_LENGTH;
   }

   PPP_PRINT(map, occlusion_query, FRAGMENT_OCCLUSION_QUERY, "Occlusion query");
   PPP_PRINT(map, occlusion_query_2, FRAGMENT_OCCLUSION_QUERY_2, "Occlusion query 2");
   PPP_PRINT(map, output_unknown, OUTPUT_UNKNOWN, "Output unknown");
   PPP_PRINT(map, output_size, OUTPUT_SIZE, "Output size");
   PPP_PRINT(map, varying_word_2, VARYING_2, "Varying word 2");

   /* PPP print checks we don't read too much, now check we read enough */
   assert(map == (base + size) && "invalid size of PPP update");
}

static unsigned
agxdecode_cmd(const uint8_t *map, uint64_t *link, bool verbose)
{
   if (map[0] == 0x02 && map[1] == 0x10 && map[2] == 0x00 && map[3] == 0x00) {
      /* XXX: This is a CDM command not a VDM one */
      agx_unpack(agxdecode_dump_stream, map, LAUNCH, cmd);
      agxdecode_stateful(cmd.pipeline, "Pipeline", agxdecode_pipeline, verbose);
      DUMP_UNPACKED(LAUNCH, cmd, "Launch\n");
      return AGX_LAUNCH_LENGTH;
   }

   /* Bits 29-31 contain the block type */
   enum agx_vdm_block_type block_type = (map[3] >> 5);

   switch (block_type) {
   case AGX_VDM_BLOCK_TYPE_PPP_STATE_UPDATE: {
      agx_unpack(agxdecode_dump_stream, map, PPP_STATE, cmd);

      uint64_t address = (((uint64_t) cmd.pointer_hi) << 32) | cmd.pointer_lo;
      struct agx_bo *mem = agxdecode_find_mapped_gpu_mem_containing(address);

      if (mem)
         agxdecode_record(address, cmd.size_words * 4, verbose);
      else
         DUMP_UNPACKED(PPP_STATE, cmd, "Non-existant record (XXX)\n");

      return AGX_PPP_STATE_LENGTH;
   }

   case AGX_VDM_BLOCK_TYPE_VDM_STATE_UPDATE: {
      size_t length = AGX_VDM_STATE_LENGTH;
      agx_unpack(agxdecode_dump_stream, map, VDM_STATE, hdr);
      map += AGX_VDM_STATE_LENGTH;

#define VDM_PRINT(header_name, STRUCT_NAME, human) \
      if (hdr.header_name##_present) { \
         DUMP_CL(VDM_STATE_##STRUCT_NAME, map, human); \
         map += AGX_VDM_STATE_##STRUCT_NAME##_LENGTH; \
         length += AGX_VDM_STATE_##STRUCT_NAME##_LENGTH; \
      }

      VDM_PRINT(restart_index, RESTART_INDEX, "Restart index");
      VDM_PRINT(vertex_shader_word_0, VERTEX_SHADER_WORD_0, "Vertex shader word 0");

      if (hdr.vertex_shader_word_1_present) {
         agx_unpack(agxdecode_dump_stream, map, VDM_STATE_VERTEX_SHADER_WORD_1,
                    word_1);
         fprintf(agxdecode_dump_stream, "Pipeline %X\n", (uint32_t) word_1.pipeline);
         agxdecode_stateful(word_1.pipeline, "Pipeline", agxdecode_pipeline, verbose);
      }

      VDM_PRINT(vertex_shader_word_1, VERTEX_SHADER_WORD_1, "Vertex shader word 1");
      VDM_PRINT(vertex_outputs, VERTEX_OUTPUTS, "Vertex outputs");
      VDM_PRINT(vertex_unknown, VERTEX_UNKNOWN, "Vertex unknown");

#undef VDM_PRINT
      return ALIGN_POT(length, 8);
   }

   case AGX_VDM_BLOCK_TYPE_INDEX_LIST: {
      size_t length = AGX_INDEX_LIST_LENGTH;
      agx_unpack(agxdecode_dump_stream, map, INDEX_LIST, hdr);
      DUMP_UNPACKED(INDEX_LIST, hdr, "Index List\n");
      map += AGX_INDEX_LIST_LENGTH;

#define IDX_PRINT(header_name, STRUCT_NAME, human) \
      if (hdr.header_name##_present) { \
         DUMP_CL(INDEX_LIST_##STRUCT_NAME, map, human); \
         map += AGX_INDEX_LIST_##STRUCT_NAME##_LENGTH; \
         length += AGX_INDEX_LIST_##STRUCT_NAME##_LENGTH; \
      }

      IDX_PRINT(index_buffer, BUFFER_LO, "Index buffer");
      IDX_PRINT(index_buffer_size, BUFFER_SIZE, "Index buffer size");
      IDX_PRINT(index_count, COUNT, "Index count");
      IDX_PRINT(instance_count, INSTANCES, "Instance count");
      IDX_PRINT(start, START, "Start");

#undef IDX_PRINT
      return ALIGN_POT(length, 8);
   }

   case AGX_VDM_BLOCK_TYPE_STREAM_LINK: {
      agx_unpack(agxdecode_dump_stream, map, STREAM_LINK, hdr);
      DUMP_UNPACKED(STREAM_LINK, hdr, "Stream Link\n");
      *link = hdr.target_lo | (((uint64_t) hdr.target_hi) << 32);
      return STATE_LINK;
   }

   case AGX_VDM_BLOCK_TYPE_STREAM_TERMINATE: {
      DUMP_CL(STREAM_TERMINATE, map, "Stream Terminate");
      return STATE_DONE;
   }

   default:
      fprintf(agxdecode_dump_stream, "Unknown VDM block type: %u\n",
              block_type);
      hexdump(agxdecode_dump_stream, map, 8, false);
      return 8;
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

   /* Before decoding anything, validate the map. Set bo->mapped fields */
   agxdecode_decode_segment_list(map->ptr.cpu);

   /* Print the IOGPU stuff */
   agx_unpack(agxdecode_dump_stream, cmdbuf->ptr.cpu, IOGPU_HEADER, cmd);
   DUMP_UNPACKED(IOGPU_HEADER, cmd, "IOGPU Header\n");
   agx_unpack(agxdecode_dump_stream, ((uint32_t *) cmdbuf->ptr.cpu) + 160,
              IOGPU_INTERNAL_PIPELINES, pip);

   DUMP_CL(IOGPU_INTERNAL_PIPELINES, ((uint32_t *) cmdbuf->ptr.cpu) + 160, "Internal pipelines");
   DUMP_CL(IOGPU_AUX_FRAMEBUFFER, ((uint32_t *) cmdbuf->ptr.cpu) + 228, "Aux Framebuffer");

   agx_unpack(agxdecode_dump_stream, ((uint32_t *) cmdbuf->ptr.cpu) + 292,
         IOGPU_CLEAR_Z_S, clearzs);
   DUMP_UNPACKED(IOGPU_CLEAR_Z_S, clearzs, "Clear Z/S");

   /* Guard against changes */
   uint32_t zeroes[356 - 344] = { 0 };
   assert(memcmp(((uint32_t *) cmdbuf->ptr.cpu) + 344, zeroes, 4 * (356 - 344)) == 0);

   DUMP_CL(IOGPU_MISC, ((uint32_t *) cmdbuf->ptr.cpu) + 356, "Misc");

   /* Should be unused, we think */
   for (unsigned i = (0x6B0 / 4); i < (cmd.attachment_offset / 4); ++i) {
      assert(((uint32_t *) cmdbuf->ptr.cpu)[i] == 0);
   }

   DUMP_CL(IOGPU_ATTACHMENT_COUNT, ((uint8_t *) cmdbuf->ptr.cpu +
            cmd.attachment_offset), "Attachment count");

   uint32_t *attachments = (uint32_t *) ((uint8_t *) cmdbuf->ptr.cpu + cmd.attachment_offset);
   unsigned attachment_count = attachments[3];
   for (unsigned i = 0; i < attachment_count; ++i) {
      uint32_t *ptr = attachments + 4 + (i * AGX_IOGPU_ATTACHMENT_LENGTH / 4);
      DUMP_CL(IOGPU_ATTACHMENT, ptr, "Attachment");
   }

   uint64_t *encoder = ((uint64_t *) cmdbuf->ptr.cpu) + 7;
   agxdecode_stateful(*encoder, "Encoder", agxdecode_cmd, verbose);

   if (pip.clear_pipeline_unk) {
      fprintf(agxdecode_dump_stream, "Unk: %X\n", pip.clear_pipeline_unk);
      agxdecode_stateful(pip.clear_pipeline, "Clear pipeline",
            agxdecode_pipeline, verbose);
   }

   if (pip.store_pipeline_unk) {
      assert(pip.store_pipeline_unk == 0x4);
      agxdecode_stateful(pip.store_pipeline, "Store pipeline",
            agxdecode_pipeline, verbose);
   }

   assert((clearzs.partial_reload_pipeline_unk & 0xF) == 0x4);
   if (clearzs.partial_reload_pipeline) {
      agxdecode_stateful(clearzs.partial_reload_pipeline,
            "Partial reload pipeline", agxdecode_pipeline, verbose);
   }

   if (clearzs.partial_store_pipeline) {
      agxdecode_stateful(clearzs.partial_store_pipeline,
            "Partial store pipeline", agxdecode_pipeline, verbose);
   }

   agxdecode_map_read_write();
}

void
agxdecode_dump_mappings(unsigned map_handle)
{
   agxdecode_dump_file_open();

   struct agx_bo *map = agxdecode_find_handle(map_handle, AGX_ALLOC_MEMMAP);
   assert(map != NULL && "nonexistant mapping");
   agxdecode_decode_segment_list(map->ptr.cpu);

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
   const char *dump_file_base = getenv("AGXDECODE_DUMP_FILE") ?: "agxdecode.dump";
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
