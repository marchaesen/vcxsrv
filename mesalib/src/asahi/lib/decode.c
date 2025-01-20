/*
 * Copyright 2017-2019 Alyssa Rosenzweig
 * Copyright 2017-2019 Connor Abbott
 * Copyright 2019 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <memory.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "util/u_dynarray.h"
#include "util/u_math.h"
#include <sys/mman.h>
#include <agx_pack.h>

#include "util/u_hexdump.h"
#include "decode.h"
#include "unstable_asahi_drm.h"
#ifdef __APPLE__
#include "agx_iokit.h"
#endif

struct libagxdecode_config lib_config;

static void
agx_disassemble(void *_code, size_t maxlen, FILE *fp)
{
   /* stub */
}

FILE *agxdecode_dump_stream;

struct agxdecode_ctx {
   struct util_dynarray mmap_array;
   uint64_t shader_base;
};

static uint64_t
decode_usc(struct agxdecode_ctx *ctx, uint64_t addr)
{
   return ctx->shader_base + addr;
}

struct agxdecode_ctx *
agxdecode_new_context(uint64_t shader_base)
{
   struct agxdecode_ctx *ctx = calloc(1, sizeof(struct agxdecode_ctx));
   ctx->shader_base = shader_base;
   return ctx;
}

void
agxdecode_destroy_context(struct agxdecode_ctx *ctx)
{
   free(ctx);
}

static struct agx_bo *
agxdecode_find_mapped_gpu_mem_containing(struct agxdecode_ctx *ctx,
                                         uint64_t addr)
{
   util_dynarray_foreach(&ctx->mmap_array, struct agx_bo, it) {
      if (it->va && addr >= it->va->addr && (addr - it->va->addr) < it->size)
         return it;
   }

   return NULL;
}

static struct agx_bo *
agxdecode_find_handle(struct agxdecode_ctx *ctx, unsigned handle, unsigned type)
{
   util_dynarray_foreach(&ctx->mmap_array, struct agx_bo, it) {
      if (it->handle == handle)
         return it;
   }

   return NULL;
}

static size_t
__agxdecode_fetch_gpu_mem(struct agxdecode_ctx *ctx, const struct agx_bo *mem,
                          uint64_t gpu_va, size_t size, void *buf, int line,
                          const char *filename)
{
   if (lib_config.read_gpu_mem)
      return lib_config.read_gpu_mem(gpu_va, size, buf);

   if (!mem)
      mem = agxdecode_find_mapped_gpu_mem_containing(ctx, gpu_va);

   if (!mem) {
      fprintf(stderr, "Access to unknown memory %" PRIx64 " in %s:%d\n", gpu_va,
              filename, line);
      fflush(agxdecode_dump_stream);
      assert(0);
   }

   assert(mem);

   if (size + (gpu_va - mem->va->addr) > mem->size) {
      fprintf(stderr,
              "Overflowing to unknown memory %" PRIx64
              " of size %zu (max size %zu) in %s:%d\n",
              gpu_va, size, (size_t)(mem->size - (gpu_va - mem->va->addr)),
              filename, line);
      fflush(agxdecode_dump_stream);
      assert(0);
   }

   memcpy(buf, mem->_map + gpu_va - mem->va->addr, size);

   return size;
}

#define agxdecode_fetch_gpu_mem(ctx, gpu_va, size, buf)                        \
   __agxdecode_fetch_gpu_mem(ctx, NULL, gpu_va, size, buf, __LINE__, __FILE__)

#define agxdecode_fetch_gpu_array(ctx, gpu_va, buf)                            \
   agxdecode_fetch_gpu_mem(ctx, gpu_va, sizeof(buf), buf)

/* Helpers for parsing the cmdstream */

#define DUMP_UNPACKED(T, var, str)                                             \
   {                                                                           \
      agxdecode_log(str);                                                      \
      agx_print(agxdecode_dump_stream, T, var, 2);                             \
   }

#define DUMP_CL(T, cl, str)                                                    \
   {                                                                           \
      agx_unpack(agxdecode_dump_stream, cl, T, temp);                          \
      DUMP_UNPACKED(T, temp, str "\n");                                        \
   }

#define DUMP_FIELD(struct, fmt, field)                                         \
   {                                                                           \
      fprintf(agxdecode_dump_stream, #field " = " fmt "\n", struct->field);    \
   }

#define agxdecode_log(str) fputs(str, agxdecode_dump_stream)
#define agxdecode_msg(str) fprintf(agxdecode_dump_stream, "// %s", str)

typedef struct drm_asahi_params_global decoder_params;

/* Abstraction for command stream parsing */
typedef unsigned (*decode_cmd)(struct agxdecode_ctx *ctx, const uint8_t *map,
                               uint64_t *link, bool verbose,
                               decoder_params *params, void *data);

#define STATE_DONE (0xFFFFFFFFu)
#define STATE_LINK (0xFFFFFFFEu)
#define STATE_CALL (0xFFFFFFFDu)
#define STATE_RET  (0xFFFFFFFCu)

static void
agxdecode_stateful(struct agxdecode_ctx *ctx, uint64_t va, const char *label,
                   decode_cmd decoder, bool verbose, decoder_params *params,
                   void *data)
{
   uint64_t stack[16];
   unsigned sp = 0;

   uint8_t buf[1024];
   size_t size = sizeof(buf);
   if (!lib_config.read_gpu_mem) {
      struct agx_bo *alloc = agxdecode_find_mapped_gpu_mem_containing(ctx, va);
      assert(alloc != NULL && "nonexistent object");
      fprintf(agxdecode_dump_stream, "%s (%" PRIx64 ", handle %u)\n", label, va,
              alloc->handle);
      size = MIN2(size, alloc->size - (va - alloc->va->addr));
   } else {
      fprintf(agxdecode_dump_stream, "%s (%" PRIx64 ")\n", label, va);
   }
   fflush(agxdecode_dump_stream);

   int len = agxdecode_fetch_gpu_mem(ctx, va, size, buf);

   int left = len;
   uint8_t *map = buf;
   uint64_t link = 0;

   fflush(agxdecode_dump_stream);

   while (left) {
      if (len <= 0) {
         fprintf(agxdecode_dump_stream, "!! Failed to read GPU memory\n");
         fflush(agxdecode_dump_stream);
         return;
      }

      unsigned count = decoder(ctx, map, &link, verbose, params, data);

      /* If we fail to decode, default to a hexdump (don't hang) */
      if (count == 0) {
         u_hexdump(agxdecode_dump_stream, map, 8, false);
         count = 8;
      }

      fflush(agxdecode_dump_stream);
      if (count == STATE_DONE) {
         break;
      } else if (count == STATE_LINK) {
         fprintf(agxdecode_dump_stream, "Linking to 0x%" PRIx64 "\n\n", link);
         va = link;
         left = len = agxdecode_fetch_gpu_array(ctx, va, buf);
         map = buf;
      } else if (count == STATE_CALL) {
         fprintf(agxdecode_dump_stream,
                 "Calling 0x%" PRIx64 " (return = 0x%" PRIx64 ")\n\n", link,
                 va + 8);
         assert(sp < ARRAY_SIZE(stack));
         stack[sp++] = va + 8;
         va = link;
         left = len = agxdecode_fetch_gpu_array(ctx, va, buf);
         map = buf;
      } else if (count == STATE_RET) {
         assert(sp > 0);
         va = stack[--sp];
         fprintf(agxdecode_dump_stream, "Returning to 0x%" PRIx64 "\n\n", va);
         left = len = agxdecode_fetch_gpu_array(ctx, va, buf);
         map = buf;
      } else {
         va += count;
         map += count;
         left -= count;

         if (left < 512 && len == sizeof(buf)) {
            left = len = agxdecode_fetch_gpu_array(ctx, va, buf);
            map = buf;
         }
      }
   }
}

static void
agxdecode_texture_pbe(struct agxdecode_ctx *ctx, const void *map)
{
   struct AGX_TEXTURE tex;
   struct AGX_PBE pbe;

   bool valid_texture = AGX_TEXTURE_unpack(NULL, map, &tex);
   bool valid_pbe = AGX_PBE_unpack(NULL, map, &pbe);

   /* Try to guess if it's texture or PBE */
   valid_texture &=
      tex.swizzle_r <= AGX_CHANNEL_0 && tex.swizzle_g <= AGX_CHANNEL_0 &&
      tex.swizzle_b <= AGX_CHANNEL_0 && tex.swizzle_a <= AGX_CHANNEL_0;

   if (valid_texture && !valid_pbe) {
      DUMP_CL(TEXTURE, map, "Texture");
   } else if (valid_pbe && !valid_texture) {
      DUMP_CL(PBE, map, "PBE");
   } else {
      if (!valid_texture) {
         assert(!valid_pbe);
         fprintf(agxdecode_dump_stream, "XXX: invalid texture/PBE\n");
      }

      DUMP_CL(TEXTURE, map, "Texture");
      DUMP_CL(PBE, map, "PBE");
   }
}

static unsigned
agxdecode_usc(struct agxdecode_ctx *ctx, const uint8_t *map,
              UNUSED uint64_t *link, UNUSED bool verbose,
              decoder_params *params, UNUSED void *data)
{
   enum agx_sampler_states *sampler_states = data;
   enum agx_usc_control type = map[0];
   uint8_t buf[3072];

   bool extended_samplers =
      (sampler_states != NULL) &&
      (((*sampler_states) == AGX_SAMPLER_STATES_8_EXTENDED) ||
       ((*sampler_states) == AGX_SAMPLER_STATES_16_EXTENDED));

#define USC_CASE(name, human)                                                  \
   case AGX_USC_CONTROL_##name: {                                              \
      DUMP_CL(USC_##name, map, human);                                         \
      return AGX_USC_##name##_LENGTH;                                          \
   }

   switch (type) {
   case AGX_USC_CONTROL_NO_PRESHADER: {
      DUMP_CL(USC_NO_PRESHADER, map, "No preshader");
      return STATE_DONE;
   }

   case AGX_USC_CONTROL_PRESHADER: {
      agx_unpack(agxdecode_dump_stream, map, USC_PRESHADER, ctrl);
      DUMP_UNPACKED(USC_PRESHADER, ctrl, "Preshader\n");

      agx_disassemble(
         buf, agxdecode_fetch_gpu_array(ctx, decode_usc(ctx, ctrl.code), buf),
         agxdecode_dump_stream);

      return STATE_DONE;
   }

   case AGX_USC_CONTROL_SHADER: {
      agx_unpack(agxdecode_dump_stream, map, USC_SHADER, ctrl);
      DUMP_UNPACKED(USC_SHADER, ctrl, "Shader\n");

      agxdecode_log("\n");
      agx_disassemble(
         buf, agxdecode_fetch_gpu_array(ctx, decode_usc(ctx, ctrl.code), buf),
         agxdecode_dump_stream);
      agxdecode_log("\n");

      return AGX_USC_SHADER_LENGTH;
   }

   case AGX_USC_CONTROL_SAMPLER: {
      agx_unpack(agxdecode_dump_stream, map, USC_SAMPLER, temp);
      DUMP_UNPACKED(USC_SAMPLER, temp, "Sampler state\n");

      size_t stride =
         AGX_SAMPLER_LENGTH + (extended_samplers ? AGX_BORDER_LENGTH : 0);
      uint8_t *samp = alloca(stride * temp.count);

      agxdecode_fetch_gpu_mem(ctx, temp.buffer, stride * temp.count, samp);

      for (unsigned i = 0; i < temp.count; ++i) {
         DUMP_CL(SAMPLER, samp, "Sampler");
         samp += AGX_SAMPLER_LENGTH;

         if (extended_samplers) {
            DUMP_CL(BORDER, samp, "Border");
            samp += AGX_BORDER_LENGTH;
         }
      }

      return AGX_USC_SAMPLER_LENGTH;
   }

   case AGX_USC_CONTROL_TEXTURE: {
      agx_unpack(agxdecode_dump_stream, map, USC_TEXTURE, temp);
      DUMP_UNPACKED(USC_TEXTURE, temp, "Texture state\n");

      uint8_t buf[AGX_TEXTURE_LENGTH * temp.count];
      uint8_t *tex = buf;

      agxdecode_fetch_gpu_array(ctx, temp.buffer, buf);

      /* Note: samplers only need 8 byte alignment? */
      for (unsigned i = 0; i < temp.count; ++i) {
         fprintf(agxdecode_dump_stream, "ts%u: \n", temp.start + i);
         agxdecode_texture_pbe(ctx, tex);

         tex += AGX_TEXTURE_LENGTH;
      }

      return AGX_USC_TEXTURE_LENGTH;
   }

   case AGX_USC_CONTROL_UNIFORM: {
      agx_unpack(agxdecode_dump_stream, map, USC_UNIFORM, temp);
      DUMP_UNPACKED(USC_UNIFORM, temp, "Uniform\n");

      uint8_t buf[2 * temp.size_halfs];
      agxdecode_fetch_gpu_array(ctx, temp.buffer, buf);
      u_hexdump(agxdecode_dump_stream, buf, 2 * temp.size_halfs, false);

      return AGX_USC_UNIFORM_LENGTH;
   }

   case AGX_USC_CONTROL_UNIFORM_HIGH: {
      agx_unpack(agxdecode_dump_stream, map, USC_UNIFORM_HIGH, temp);
      DUMP_UNPACKED(USC_UNIFORM_HIGH, temp, "Uniform (high)\n");

      uint8_t buf[2 * temp.size_halfs];
      agxdecode_fetch_gpu_array(ctx, temp.buffer, buf);
      u_hexdump(agxdecode_dump_stream, buf, 2 * temp.size_halfs, false);

      return AGX_USC_UNIFORM_HIGH_LENGTH;
   }

      USC_CASE(FRAGMENT_PROPERTIES, "Fragment properties");
      USC_CASE(SHARED, "Shared");
      USC_CASE(REGISTERS, "Registers");

   default:
      fprintf(agxdecode_dump_stream, "Unknown USC control type: %u\n", type);
      u_hexdump(agxdecode_dump_stream, map, 8, false);
      return 8;
   }

#undef USC_CASE
}

#define PPP_PRINT(map, header_name, struct_name, human)                        \
   if (hdr.header_name) {                                                      \
      if (((map + AGX_##struct_name##_LENGTH) > (base + size))) {              \
         fprintf(agxdecode_dump_stream, "Buffer overrun in PPP update\n");     \
         return;                                                               \
      }                                                                        \
      DUMP_CL(struct_name, map, human);                                        \
      map += AGX_##struct_name##_LENGTH;                                       \
      fflush(agxdecode_dump_stream);                                           \
   }

static void
agxdecode_record(struct agxdecode_ctx *ctx, uint64_t va, size_t size,
                 bool verbose, decoder_params *params)
{
   uint8_t buf[size];
   uint8_t *base = buf;
   uint8_t *map = base;

   agxdecode_fetch_gpu_array(ctx, va, buf);

   agx_unpack(agxdecode_dump_stream, map, PPP_HEADER, hdr);
   map += AGX_PPP_HEADER_LENGTH;

   PPP_PRINT(map, fragment_control, FRAGMENT_CONTROL, "Fragment control");
   PPP_PRINT(map, fragment_control_2, FRAGMENT_CONTROL, "Fragment control 2");
   PPP_PRINT(map, fragment_front_face, FRAGMENT_FACE, "Front face");
   PPP_PRINT(map, fragment_front_face_2, FRAGMENT_FACE_2, "Front face 2");
   PPP_PRINT(map, fragment_front_stencil, FRAGMENT_STENCIL, "Front stencil");
   PPP_PRINT(map, fragment_back_face, FRAGMENT_FACE, "Back face");
   PPP_PRINT(map, fragment_back_face_2, FRAGMENT_FACE_2, "Back face 2");
   PPP_PRINT(map, fragment_back_stencil, FRAGMENT_STENCIL, "Back stencil");
   PPP_PRINT(map, depth_bias_scissor, DEPTH_BIAS_SCISSOR, "Depth bias/scissor");

   if (hdr.region_clip) {
      if (((map + (AGX_REGION_CLIP_LENGTH * hdr.viewport_count)) >
           (base + size))) {
         fprintf(agxdecode_dump_stream, "Buffer overrun in PPP update\n");
         return;
      }

      for (unsigned i = 0; i < hdr.viewport_count; ++i) {
         DUMP_CL(REGION_CLIP, map, "Region clip");
         map += AGX_REGION_CLIP_LENGTH;
         fflush(agxdecode_dump_stream);
      }
   }

   if (hdr.viewport) {
      if (((map + AGX_VIEWPORT_CONTROL_LENGTH +
            (AGX_VIEWPORT_LENGTH * hdr.viewport_count)) > (base + size))) {
         fprintf(agxdecode_dump_stream, "Buffer overrun in PPP update\n");
         return;
      }

      DUMP_CL(VIEWPORT_CONTROL, map, "Viewport control");
      map += AGX_VIEWPORT_CONTROL_LENGTH;

      for (unsigned i = 0; i < hdr.viewport_count; ++i) {
         DUMP_CL(VIEWPORT, map, "Viewport");
         map += AGX_VIEWPORT_LENGTH;
         fflush(agxdecode_dump_stream);
      }
   }

   PPP_PRINT(map, w_clamp, W_CLAMP, "W clamp");
   PPP_PRINT(map, output_select, OUTPUT_SELECT, "Output select");
   PPP_PRINT(map, varying_counts_32, VARYING_COUNTS, "Varying counts 32");
   PPP_PRINT(map, varying_counts_16, VARYING_COUNTS, "Varying counts 16");
   PPP_PRINT(map, cull, CULL, "Cull");
   PPP_PRINT(map, cull_2, CULL_2, "Cull 2");

   if (hdr.fragment_shader) {
      agx_unpack(agxdecode_dump_stream, map, FRAGMENT_SHADER_WORD_0, frag_0);
      agx_unpack(agxdecode_dump_stream, map + 4, FRAGMENT_SHADER_WORD_1,
                 frag_1);
      agx_unpack(agxdecode_dump_stream, map + 8, FRAGMENT_SHADER_WORD_2,
                 frag_2);
      agxdecode_stateful(ctx, decode_usc(ctx, frag_1.pipeline),
                         "Fragment pipeline", agxdecode_usc, verbose, params,
                         &frag_0.sampler_state_register_count);

      if (frag_2.cf_bindings) {
         uint8_t buf[128];
         uint8_t *cf = buf;

         agxdecode_fetch_gpu_array(ctx, decode_usc(ctx, frag_2.cf_bindings),
                                   buf);
         u_hexdump(agxdecode_dump_stream, cf, 128, false);

         DUMP_CL(CF_BINDING_HEADER, cf, "Coefficient binding header:");
         cf += AGX_CF_BINDING_HEADER_LENGTH;

         for (unsigned i = 0; i < frag_0.cf_binding_count; ++i) {
            DUMP_CL(CF_BINDING, cf, "Coefficient binding:");
            cf += AGX_CF_BINDING_LENGTH;
         }
      }

      DUMP_CL(FRAGMENT_SHADER_WORD_0, map, "Fragment shader word 0");
      DUMP_CL(FRAGMENT_SHADER_WORD_1, map + 4, "Fragment shader word 1");
      DUMP_CL(FRAGMENT_SHADER_WORD_2, map + 8, "Fragment shader word 2");
      DUMP_CL(FRAGMENT_SHADER_WORD_3, map + 12, "Fragment shader word 3");
      map += 16;
   }

   PPP_PRINT(map, occlusion_query, FRAGMENT_OCCLUSION_QUERY, "Occlusion query");
   PPP_PRINT(map, occlusion_query_2, FRAGMENT_OCCLUSION_QUERY_2,
             "Occlusion query 2");
   PPP_PRINT(map, output_unknown, OUTPUT_UNKNOWN, "Output unknown");
   PPP_PRINT(map, output_size, OUTPUT_SIZE, "Output size");
   PPP_PRINT(map, varying_word_2, VARYING_2, "Varying word 2");

   /* PPP print checks we don't read too much, now check we read enough */
   assert(map == (base + size) && "invalid size of PPP update");
}

static unsigned
agxdecode_cdm(struct agxdecode_ctx *ctx, const uint8_t *map, uint64_t *link,
              bool verbose, decoder_params *params, UNUSED void *data)
{
   /* Bits 29-31 contain the block type */
   enum agx_cdm_block_type block_type = (map[3] >> 5);

   switch (block_type) {
   case AGX_CDM_BLOCK_TYPE_LAUNCH: {
      size_t length =
         AGX_CDM_LAUNCH_WORD_0_LENGTH + AGX_CDM_LAUNCH_WORD_1_LENGTH;

#define CDM_PRINT(STRUCT_NAME, human)                                          \
   do {                                                                        \
      DUMP_CL(CDM_##STRUCT_NAME, map, human);                                  \
      map += AGX_CDM_##STRUCT_NAME##_LENGTH;                                   \
      length += AGX_CDM_##STRUCT_NAME##_LENGTH;                                \
   } while (0);

      agx_unpack(agxdecode_dump_stream, map + 0, CDM_LAUNCH_WORD_0, hdr0);
      agx_unpack(agxdecode_dump_stream, map + 4, CDM_LAUNCH_WORD_1, hdr1);

      agxdecode_stateful(ctx, decode_usc(ctx, hdr1.pipeline), "Pipeline",
                         agxdecode_usc, verbose, params,
                         &hdr0.sampler_state_register_count);
      DUMP_UNPACKED(CDM_LAUNCH_WORD_0, hdr0, "Compute\n");
      DUMP_UNPACKED(CDM_LAUNCH_WORD_1, hdr1, "Compute\n");
      map += 8;

      /* Added in G14X */
      if (params->gpu_generation >= 14 && params->num_clusters_total > 1)
         CDM_PRINT(UNK_G14X, "Unknown G14X");

      switch (hdr0.mode) {
      case AGX_CDM_MODE_DIRECT:
         CDM_PRINT(GLOBAL_SIZE, "Global size");
         CDM_PRINT(LOCAL_SIZE, "Local size");
         break;
      case AGX_CDM_MODE_INDIRECT_GLOBAL:
         CDM_PRINT(INDIRECT, "Indirect buffer");
         CDM_PRINT(LOCAL_SIZE, "Local size");
         break;
      case AGX_CDM_MODE_INDIRECT_LOCAL:
         CDM_PRINT(INDIRECT, "Indirect buffer");
         break;
      default:
         fprintf(agxdecode_dump_stream, "Unknown CDM mode: %u\n", hdr0.mode);
         break;
      }

      return length;
   }

   case AGX_CDM_BLOCK_TYPE_STREAM_LINK: {
      agx_unpack(agxdecode_dump_stream, map, CDM_STREAM_LINK, hdr);
      DUMP_UNPACKED(CDM_STREAM_LINK, hdr, "Stream Link\n");
      *link = hdr.target_lo | (((uint64_t)hdr.target_hi) << 32);
      return hdr.with_return ? STATE_CALL : STATE_LINK;
   }

   case AGX_CDM_BLOCK_TYPE_STREAM_TERMINATE: {
      DUMP_CL(CDM_STREAM_TERMINATE, map, "Stream Terminate");
      return STATE_DONE;
   }

   case AGX_CDM_BLOCK_TYPE_STREAM_RETURN: {
      DUMP_CL(CDM_STREAM_RETURN, map, "Stream Return");
      return STATE_RET;
   }

   case AGX_CDM_BLOCK_TYPE_BARRIER: {
      DUMP_CL(CDM_BARRIER, map, "Barrier");
      return AGX_CDM_BARRIER_LENGTH;
   }

   default:
      fprintf(agxdecode_dump_stream, "Unknown CDM block type: %u\n",
              block_type);
      u_hexdump(agxdecode_dump_stream, map, 8, false);
      return 8;
   }
}

static unsigned
agxdecode_vdm(struct agxdecode_ctx *ctx, const uint8_t *map, uint64_t *link,
              bool verbose, decoder_params *params, UNUSED void *data)
{
   /* Bits 29-31 contain the block type */
   enum agx_vdm_block_type block_type = (map[3] >> 5);

   switch (block_type) {
   case AGX_VDM_BLOCK_TYPE_BARRIER: {
      agx_unpack(agxdecode_dump_stream, map, VDM_BARRIER, hdr);
      DUMP_UNPACKED(VDM_BARRIER, hdr, "Barrier\n");
      return hdr.returns ? STATE_RET : AGX_VDM_BARRIER_LENGTH;
   }

   case AGX_VDM_BLOCK_TYPE_PPP_STATE_UPDATE: {
      agx_unpack(agxdecode_dump_stream, map, PPP_STATE, cmd);

      uint64_t address = (((uint64_t)cmd.pointer_hi) << 32) | cmd.pointer_lo;

      if (!lib_config.read_gpu_mem) {
         struct agx_bo *mem =
            agxdecode_find_mapped_gpu_mem_containing(ctx, address);

         if (!mem) {
            DUMP_UNPACKED(PPP_STATE, cmd, "Non-existent record (XXX)\n");
            return AGX_PPP_STATE_LENGTH;
         }
      }

      agxdecode_record(ctx, address, cmd.size_words * 4, verbose, params);
      return AGX_PPP_STATE_LENGTH;
   }

   case AGX_VDM_BLOCK_TYPE_VDM_STATE_UPDATE: {
      size_t length = AGX_VDM_STATE_LENGTH;
      agx_unpack(agxdecode_dump_stream, map, VDM_STATE, hdr);
      map += AGX_VDM_STATE_LENGTH;

#define VDM_PRINT(header_name, STRUCT_NAME, human)                             \
   if (hdr.header_name##_present) {                                            \
      DUMP_CL(VDM_STATE_##STRUCT_NAME, map, human);                            \
      map += AGX_VDM_STATE_##STRUCT_NAME##_LENGTH;                             \
      length += AGX_VDM_STATE_##STRUCT_NAME##_LENGTH;                          \
   }

      VDM_PRINT(restart_index, RESTART_INDEX, "Restart index");

      /* If word 1 is present but word 0 is not, fallback to compact samplers */
      enum agx_sampler_states sampler_states = 0;

      if (hdr.vertex_shader_word_0_present) {
         agx_unpack(agxdecode_dump_stream, map, VDM_STATE_VERTEX_SHADER_WORD_0,
                    word_0);
         sampler_states = word_0.sampler_state_register_count;
      }

      VDM_PRINT(vertex_shader_word_0, VERTEX_SHADER_WORD_0,
                "Vertex shader word 0");

      if (hdr.vertex_shader_word_1_present) {
         agx_unpack(agxdecode_dump_stream, map, VDM_STATE_VERTEX_SHADER_WORD_1,
                    word_1);
         fprintf(agxdecode_dump_stream, "Pipeline %X\n",
                 (uint32_t)word_1.pipeline);
         agxdecode_stateful(ctx, decode_usc(ctx, word_1.pipeline), "Pipeline",
                            agxdecode_usc, verbose, params, &sampler_states);
      }

      VDM_PRINT(vertex_shader_word_1, VERTEX_SHADER_WORD_1,
                "Vertex shader word 1");
      VDM_PRINT(vertex_outputs, VERTEX_OUTPUTS, "Vertex outputs");
      VDM_PRINT(tessellation, TESSELLATION, "Tessellation");
      VDM_PRINT(vertex_unknown, VERTEX_UNKNOWN, "Vertex unknown");
      VDM_PRINT(tessellation_scale, TESSELLATION_SCALE, "Tessellation scale");

#undef VDM_PRINT
      return hdr.tessellation_scale_present ? length : ALIGN_POT(length, 8);
   }

   case AGX_VDM_BLOCK_TYPE_INDEX_LIST: {
      size_t length = AGX_INDEX_LIST_LENGTH;
      agx_unpack(agxdecode_dump_stream, map, INDEX_LIST, hdr);
      DUMP_UNPACKED(INDEX_LIST, hdr, "Index List\n");
      map += AGX_INDEX_LIST_LENGTH;

#define IDX_PRINT(header_name, STRUCT_NAME, human)                             \
   if (hdr.header_name##_present) {                                            \
      DUMP_CL(INDEX_LIST_##STRUCT_NAME, map, human);                           \
      map += AGX_INDEX_LIST_##STRUCT_NAME##_LENGTH;                            \
      length += AGX_INDEX_LIST_##STRUCT_NAME##_LENGTH;                         \
   }

      IDX_PRINT(index_buffer, BUFFER_LO, "Index buffer");
      IDX_PRINT(index_count, COUNT, "Index count");
      IDX_PRINT(instance_count, INSTANCES, "Instance count");
      IDX_PRINT(start, START, "Start");
      IDX_PRINT(indirect_buffer, INDIRECT_BUFFER, "Indirect buffer");
      IDX_PRINT(index_buffer_size, BUFFER_SIZE, "Index buffer size");

#undef IDX_PRINT
      return length;
   }

   case AGX_VDM_BLOCK_TYPE_STREAM_LINK: {
      agx_unpack(agxdecode_dump_stream, map, VDM_STREAM_LINK, hdr);
      DUMP_UNPACKED(VDM_STREAM_LINK, hdr, "Stream Link\n");
      *link = hdr.target_lo | (((uint64_t)hdr.target_hi) << 32);
      return hdr.with_return ? STATE_CALL : STATE_LINK;
   }

   case AGX_VDM_BLOCK_TYPE_STREAM_TERMINATE: {
      DUMP_CL(VDM_STREAM_TERMINATE, map, "Stream Terminate");
      return STATE_DONE;
   }

   case AGX_VDM_BLOCK_TYPE_TESSELLATE: {
      size_t length = AGX_VDM_TESSELLATE_LENGTH;
      agx_unpack(agxdecode_dump_stream, map, VDM_TESSELLATE, hdr);
      DUMP_UNPACKED(VDM_TESSELLATE, hdr, "Tessellate List\n");
      map += AGX_VDM_TESSELLATE_LENGTH;

#define TESS_PRINT(header_name, STRUCT_NAME, human)                            \
   if (hdr.header_name##_present) {                                            \
      DUMP_CL(VDM_TESSELLATE_##STRUCT_NAME, map, human);                       \
      map += AGX_VDM_TESSELLATE_##STRUCT_NAME##_LENGTH;                        \
      length += AGX_VDM_TESSELLATE_##STRUCT_NAME##_LENGTH;                     \
   }

      TESS_PRINT(factor_buffer, FACTOR_BUFFER, "Factor buffer");
      TESS_PRINT(patch_count, PATCH_COUNT, "Patch");
      TESS_PRINT(instance_count, INSTANCE_COUNT, "Instance count");
      TESS_PRINT(base_patch, BASE_PATCH, "Base patch");
      TESS_PRINT(base_instance, BASE_INSTANCE, "Base instance");
      TESS_PRINT(instance_stride, INSTANCE_STRIDE, "Instance stride");
      TESS_PRINT(indirect, INDIRECT, "Indirect");
      TESS_PRINT(factor_buffer_size, FACTOR_BUFFER_SIZE, "Factor buffer size");

#undef TESS_PRINT
      return length;
   }

   default:
      fprintf(agxdecode_dump_stream, "Unknown VDM block type: %u\n",
              block_type);
      u_hexdump(agxdecode_dump_stream, map, 8, false);
      return 8;
   }
}

#if __APPLE__
static void
agxdecode_cs(struct agxdecode_ctx *ctx, uint32_t *cmdbuf, uint64_t encoder,
             bool verbose, decoder_params *params)
{
   agx_unpack(agxdecode_dump_stream, cmdbuf + 16, IOGPU_COMPUTE, cs);
   DUMP_UNPACKED(IOGPU_COMPUTE, cs, "Compute\n");

   agxdecode_stateful(ctx, encoder, "Encoder", agxdecode_cdm, verbose, params,
                      NULL);

   fprintf(agxdecode_dump_stream, "Context switch program:\n");
   uint8_t buf[1024];
   agx_disassemble(buf,
                   agxdecode_fetch_gpu_array(
                      ctx, decode_usc(ctx, cs.context_switch_program), buf),
                   agxdecode_dump_stream);
}

static void
agxdecode_gfx(struct agxdecode_ctx *ctx, uint32_t *cmdbuf, uint64_t encoder,
              bool verbose, decoder_params *params)
{
   agx_unpack(agxdecode_dump_stream, cmdbuf + 16, IOGPU_GRAPHICS, gfx);
   DUMP_UNPACKED(IOGPU_GRAPHICS, gfx, "Graphics\n");

   agxdecode_stateful(ctx, encoder, "Encoder", agxdecode_vdm, verbose, params,
                      NULL);

   if (gfx.clear_pipeline_unk) {
      fprintf(agxdecode_dump_stream, "Unk: %X\n", gfx.clear_pipeline_unk);
      agxdecode_stateful(ctx, decode_usc(ctx, gfx.clear_pipeline),
                         "Clear pipeline", agxdecode_usc, verbose, params,
                         NULL);
   }

   if (gfx.store_pipeline_unk) {
      assert(gfx.store_pipeline_unk == 0x4);
      agxdecode_stateful(ctx, decode_usc(ctx, gfx.store_pipeline),
                         "Store pipeline", agxdecode_usc, verbose, params,
                         NULL);
   }

   assert((gfx.partial_reload_pipeline_unk & 0xF) == 0x4);
   if (gfx.partial_reload_pipeline) {
      agxdecode_stateful(ctx, decode_usc(ctx, gfx.partial_reload_pipeline),
                         "Partial reload pipeline", agxdecode_usc, verbose,
                         params, NULL);
   }

   if (gfx.partial_store_pipeline) {
      agxdecode_stateful(ctx, decode_usc(ctx, gfx.partial_store_pipeline),
                         "Partial store pipeline", agxdecode_usc, verbose,
                         params, NULL);
   }
}
#endif

static void
agxdecode_sampler_heap(struct agxdecode_ctx *ctx, uint64_t heap, unsigned count)
{
   if (!heap)
      return;

   struct agx_sampler_packed samp[1024];
   agxdecode_fetch_gpu_array(ctx, heap, samp);

   for (unsigned i = 0; i < count; ++i) {
      bool nonzero = false;
      for (unsigned j = 0; j < ARRAY_SIZE(samp[i].opaque); ++j) {
         nonzero |= samp[i].opaque[j] != 0;
      }

      if (nonzero) {
         fprintf(agxdecode_dump_stream, "Heap sampler %u\n", i);

         agx_unpack(agxdecode_dump_stream, samp + i, SAMPLER, temp);
         agx_print(agxdecode_dump_stream, SAMPLER, temp, 2);
      }
   }
}

void
agxdecode_image_heap(struct agxdecode_ctx *ctx, uint64_t heap,
                     unsigned nr_entries)
{
   agxdecode_dump_file_open();

   fprintf(agxdecode_dump_stream, "Image heap:\n");
   struct agx_texture_packed *map = calloc(nr_entries, AGX_TEXTURE_LENGTH);
   agxdecode_fetch_gpu_mem(ctx, heap, AGX_TEXTURE_LENGTH * nr_entries, map);

   for (unsigned i = 0; i < nr_entries; ++i) {
      bool nonzero = false;
      for (unsigned j = 0; j < ARRAY_SIZE(map[i].opaque); ++j) {
         nonzero |= map[i].opaque[j] != 0;
      }

      if (nonzero) {
         fprintf(agxdecode_dump_stream, "%u: \n", i);
         agxdecode_texture_pbe(ctx, map + i);
         fprintf(agxdecode_dump_stream, "\n");
      }
   }

   free(map);
}

static void
agxdecode_helper(struct agxdecode_ctx *ctx, const char *prefix, uint64_t helper)
{
   if (helper & 1) {
      fprintf(agxdecode_dump_stream, "%s helper program:\n", prefix);
      uint8_t buf[1024];
      agx_disassemble(
         buf, agxdecode_fetch_gpu_array(ctx, decode_usc(ctx, helper & ~1), buf),
         agxdecode_dump_stream);
   }
}

void
agxdecode_drm_cmd_render(struct agxdecode_ctx *ctx,
                         struct drm_asahi_params_global *params,
                         struct drm_asahi_cmd_render *c, bool verbose)
{
   agxdecode_dump_file_open();

   DUMP_FIELD(c, "%llx", flags);
   DUMP_FIELD(c, "0x%llx", encoder_ptr);
   agxdecode_stateful(ctx, c->encoder_ptr, "Encoder", agxdecode_vdm, verbose,
                      params, NULL);
   DUMP_FIELD(c, "0x%x", encoder_id);
   DUMP_FIELD(c, "0x%x", cmd_ta_id);
   DUMP_FIELD(c, "0x%x", cmd_3d_id);
   DUMP_FIELD(c, "0x%x", ppp_ctrl);
   DUMP_FIELD(c, "0x%llx", ppp_multisamplectl);
   DUMP_CL(ZLS_CONTROL, &c->zls_ctrl, "ZLS Control");
   DUMP_FIELD(c, "0x%llx", depth_buffer_load);
   DUMP_FIELD(c, "0x%llx", depth_buffer_store);
   DUMP_FIELD(c, "0x%llx", depth_buffer_partial);
   DUMP_FIELD(c, "0x%llx", stencil_buffer_load);
   DUMP_FIELD(c, "0x%llx", stencil_buffer_store);
   DUMP_FIELD(c, "0x%llx", stencil_buffer_partial);
   DUMP_FIELD(c, "0x%llx", scissor_array);
   DUMP_FIELD(c, "0x%llx", depth_bias_array);
   DUMP_FIELD(c, "%d", fb_width);
   DUMP_FIELD(c, "%d", fb_height);
   DUMP_FIELD(c, "%d", layers);
   DUMP_FIELD(c, "%d", samples);
   DUMP_FIELD(c, "%d", sample_size);
   DUMP_FIELD(c, "%d", tib_blocks);
   DUMP_FIELD(c, "%d", utile_width);
   DUMP_FIELD(c, "%d", utile_height);
   DUMP_FIELD(c, "0x%x", load_pipeline);
   DUMP_FIELD(c, "0x%x", load_pipeline_bind);
   agxdecode_stateful(ctx, decode_usc(ctx, c->load_pipeline & ~0x7),
                      "Load pipeline", agxdecode_usc, verbose, params, NULL);
   DUMP_FIELD(c, "0x%x", store_pipeline);
   DUMP_FIELD(c, "0x%x", store_pipeline_bind);
   agxdecode_stateful(ctx, decode_usc(ctx, c->store_pipeline & ~0x7),
                      "Store pipeline", agxdecode_usc, verbose, params, NULL);
   DUMP_FIELD(c, "0x%x", partial_reload_pipeline);
   DUMP_FIELD(c, "0x%x", partial_reload_pipeline_bind);
   agxdecode_stateful(ctx, decode_usc(ctx, c->partial_reload_pipeline & ~0x7),
                      "Partial reload pipeline", agxdecode_usc, verbose, params,
                      NULL);
   DUMP_FIELD(c, "0x%x", partial_store_pipeline);
   DUMP_FIELD(c, "0x%x", partial_store_pipeline_bind);
   agxdecode_stateful(ctx, decode_usc(ctx, c->partial_store_pipeline & ~0x7),
                      "Partial store pipeline", agxdecode_usc, verbose, params,
                      NULL);

   DUMP_FIELD(c, "0x%x", depth_dimensions);
   DUMP_FIELD(c, "0x%x", isp_bgobjdepth);
   DUMP_FIELD(c, "0x%x", isp_bgobjvals);

   agxdecode_sampler_heap(ctx, c->vertex_sampler_array,
                          c->vertex_sampler_count);

   /* Linux driver doesn't use this, at least for now */
   assert(c->fragment_sampler_array == c->vertex_sampler_array);
   assert(c->fragment_sampler_count == c->vertex_sampler_count);

   DUMP_FIELD(c, "%d", vertex_attachment_count);
   struct drm_asahi_attachment *vertex_attachments =
      (void *)(uintptr_t)c->vertex_attachments;
   for (unsigned i = 0; i < c->vertex_attachment_count; i++) {
      DUMP_FIELD((&vertex_attachments[i]), "0x%x", order);
      DUMP_FIELD((&vertex_attachments[i]), "0x%llx", size);
      DUMP_FIELD((&vertex_attachments[i]), "0x%llx", pointer);
   }
   DUMP_FIELD(c, "%d", fragment_attachment_count);
   struct drm_asahi_attachment *fragment_attachments =
      (void *)(uintptr_t)c->fragment_attachments;
   for (unsigned i = 0; i < c->fragment_attachment_count; i++) {
      DUMP_FIELD((&fragment_attachments[i]), "0x%x", order);
      DUMP_FIELD((&fragment_attachments[i]), "0x%llx", size);
      DUMP_FIELD((&fragment_attachments[i]), "0x%llx", pointer);
   }

   agxdecode_helper(ctx, "Vertex", c->vertex_helper_program);
   agxdecode_helper(ctx, "Fragment", c->fragment_helper_program);
}

void
agxdecode_drm_cmd_compute(struct agxdecode_ctx *ctx,
                          struct drm_asahi_params_global *params,
                          struct drm_asahi_cmd_compute *c, bool verbose)
{
   agxdecode_dump_file_open();

   DUMP_FIELD(c, "%llx", flags);
   DUMP_FIELD(c, "0x%llx", encoder_ptr);
   agxdecode_stateful(ctx, c->encoder_ptr, "Encoder", agxdecode_cdm, verbose,
                      params, NULL);
   DUMP_FIELD(c, "0x%x", encoder_id);
   DUMP_FIELD(c, "0x%x", cmd_id);

   agxdecode_sampler_heap(ctx, c->sampler_array, c->sampler_count);
   agxdecode_helper(ctx, "Compute", c->helper_program);
}

static void
chip_id_to_params(decoder_params *params, uint32_t chip_id)
{
   switch (chip_id) {
   case 0x6000 ... 0x6002:
      *params = (decoder_params){
         .gpu_generation = 13,
         .gpu_variant = "SCD"[chip_id & 15],
         .chip_id = chip_id,
         .num_clusters_total = 2 << (chip_id & 15),
      };
      break;
   case 0x6020 ... 0x6022:
      *params = (decoder_params){
         .gpu_generation = 14,
         .gpu_variant = "SCD"[chip_id & 15],
         .chip_id = chip_id,
         .num_clusters_total = 2 << (chip_id & 15),
      };
      break;
   case 0x8112:
      *params = (decoder_params){
         .gpu_generation = 14,
         .gpu_variant = 'G',
         .chip_id = chip_id,
         .num_clusters_total = 1,
      };
      break;
   case 0x8103:
   default:
      *params = (decoder_params){
         .gpu_generation = 13,
         .gpu_variant = 'G',
         .chip_id = chip_id,
         .num_clusters_total = 1,
      };
      break;
   }
}

#ifdef __APPLE__

void
agxdecode_cmdstream(struct agxdecode_ctx *ctx, unsigned cmdbuf_handle,
                    unsigned map_handle, bool verbose)
{
   agxdecode_dump_file_open();

   struct agx_bo *cmdbuf =
      agxdecode_find_handle(cmdbuf_handle, AGX_ALLOC_CMDBUF);
   struct agx_bo *map = agxdecode_find_handle(map_handle, AGX_ALLOC_MEMMAP);
   assert(cmdbuf != NULL && "nonexistent command buffer");
   assert(map != NULL && "nonexistent mapping");

   /* Print the IOGPU stuff */
   agx_unpack(agxdecode_dump_stream, cmdbuf->map, IOGPU_HEADER, cmd);
   DUMP_UNPACKED(IOGPU_HEADER, cmd, "IOGPU Header\n");

   DUMP_CL(IOGPU_ATTACHMENT_COUNT,
           ((uint8_t *)cmdbuf->map + cmd.attachment_offset),
           "Attachment count");

   uint32_t *attachments =
      (uint32_t *)((uint8_t *)cmdbuf->map + cmd.attachment_offset);
   unsigned attachment_count = attachments[3];
   for (unsigned i = 0; i < attachment_count; ++i) {
      uint32_t *ptr = attachments + 4 + (i * AGX_IOGPU_ATTACHMENT_LENGTH / 4);
      DUMP_CL(IOGPU_ATTACHMENT, ptr, "Attachment");
   }

   struct drm_asahi_params_global params;

   chip_id_to_params(&params, 0x8103);

   if (cmd.unk_5 == 3)
      agxdecode_cs((uint32_t *)cmdbuf->map, cmd.encoder, verbose, &params);
   else
      agxdecode_gfx((uint32_t *)cmdbuf->map, cmd.encoder, verbose, &params);
}

#endif

void
agxdecode_track_alloc(struct agxdecode_ctx *ctx, struct agx_bo *alloc)
{
   util_dynarray_foreach(&ctx->mmap_array, struct agx_bo, it) {
      bool match = (it->handle == alloc->handle);
      assert(!match && "tried to alloc already allocated BO");
   }

   util_dynarray_append(&ctx->mmap_array, struct agx_bo, *alloc);
}

void
agxdecode_track_free(struct agxdecode_ctx *ctx, struct agx_bo *bo)
{
   bool found = false;

   util_dynarray_foreach(&ctx->mmap_array, struct agx_bo, it) {
      if (it->handle == bo->handle) {
         assert(!found && "mapped multiple times!");
         found = true;

         memset(it, 0, sizeof(*it));
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
   const char *dump_file_base =
      getenv("AGXDECODE_DUMP_FILE") ?: "agxdecode.dump";
   if (!strcmp(dump_file_base, "stderr"))
      agxdecode_dump_stream = stderr;
   else {
      char buffer[1024];
      snprintf(buffer, sizeof(buffer), "%s.%04d", dump_file_base,
               agxdecode_dump_frame_count);
      printf("agxdecode: dump command stream to file %s\n", buffer);
      agxdecode_dump_stream = fopen(buffer, "w");
      if (!agxdecode_dump_stream) {
         fprintf(stderr,
                 "agxdecode: failed to open command stream log file %s\n",
                 buffer);
      }
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

static ssize_t
libagxdecode_writer(void *cookie, const char *buffer, size_t size)
{
   return lib_config.stream_write(buffer, size);
}

#ifdef _GNU_SOURCE
static cookie_io_functions_t funcs = {.write = libagxdecode_writer};
#endif

static decoder_params lib_params;

void
libagxdecode_init(struct libagxdecode_config *config)
{
#ifdef _GNU_SOURCE
   lib_config = *config;
   agxdecode_dump_stream = fopencookie(NULL, "w", funcs);

   chip_id_to_params(&lib_params, config->chip_id);
#else
   /* fopencookie is a glibc extension */
   unreachable("libagxdecode only available with glibc");
#endif
}

void
libagxdecode_vdm(struct agxdecode_ctx *ctx, uint64_t addr, const char *label,
                 bool verbose)
{
   agxdecode_stateful(ctx, addr, label, agxdecode_vdm, verbose, &lib_params,
                      NULL);
}

void
libagxdecode_cdm(struct agxdecode_ctx *ctx, uint64_t addr, const char *label,
                 bool verbose)
{
   agxdecode_stateful(ctx, addr, label, agxdecode_cdm, verbose, &lib_params,
                      NULL);
}
void
libagxdecode_usc(struct agxdecode_ctx *ctx, uint64_t addr, const char *label,
                 bool verbose)
{
   agxdecode_stateful(ctx, addr, label, agxdecode_usc, verbose, &lib_params,
                      NULL);
}
void
libagxdecode_shutdown(void)
{
   agxdecode_dump_file_close();
}
