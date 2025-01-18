/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_pipe.h"
#include "si_query.h"
#include "util/streaming-load-memcpy.h"

#define MIN_SIZE   512
#define MAX_SIZE   (128 * 1024 * 1024)
#define SIZE_SHIFT 1
#define WARMUP_RUNS 16
#define NUM_RUNS   32

enum {
   TEST_FILL_VRAM,
   TEST_FILL_VRAM_12B,
   TEST_FILL_GTT,
   TEST_FILL_GTT_12B,
   TEST_COPY_VRAM_VRAM,
   TEST_COPY_VRAM_GTT,
   TEST_COPY_GTT_VRAM,
   NUM_TESTS,
};

static const char *test_strings[] = {
   [TEST_FILL_VRAM] = "fill->VRAM",
   [TEST_FILL_VRAM_12B] = "fill->VRAM 12B",
   [TEST_FILL_GTT] = "fill->GTT",
   [TEST_FILL_GTT_12B] = "fill->GTT 12B",
   [TEST_COPY_VRAM_VRAM] = "VRAM->VRAM",
   [TEST_COPY_VRAM_GTT] = "VRAM->GTT",
   [TEST_COPY_GTT_VRAM] = "GTT->VRAM",
};

enum {
   METHOD_DEFAULT,
   METHOD_CP_DMA,
   METHOD_COMPUTE_2DW,
   METHOD_COMPUTE_3DW,
   METHOD_COMPUTE_4DW,
   NUM_METHODS,
};

static const char *method_strings[] = {
   [METHOD_DEFAULT] = "Default",
   [METHOD_CP_DMA] = "CP DMA",
   [METHOD_COMPUTE_2DW] = "CS 2dw",
   [METHOD_COMPUTE_3DW] = "CS 3dw",
   [METHOD_COMPUTE_4DW] = "CS 4dw",
};

enum {
   ALIGN_MAX,
   ALIGN_256,
   ALIGN_128,
   ALIGN_64,
   ALIGN_4,
   ALIGN_2,
   ALIGN_1,
   ALIGN_SRC128,
   ALIGN_SRC64,
   ALIGN_SRC4,
   ALIGN_SRC2,
   ALIGN_SRC1,
   ALIGN_DST128,
   ALIGN_DST64,
   ALIGN_DST4,
   ALIGN_DST2,
   ALIGN_DST1,
   ALIGN_SRC4_DST2,
   ALIGN_SRC4_DST1,
   ALIGN_SRC2_DST4,
   ALIGN_SRC2_DST1,
   ALIGN_SRC1_DST4,
   ALIGN_SRC1_DST2,
   NUM_ALIGNMENTS,
};

struct align_info_t {
   const char *string;
   unsigned src_offset;
   unsigned dst_offset;
};

static const struct align_info_t align_info[] = {
   [ALIGN_MAX] = {"both=max", 0, 0},
   [ALIGN_256] = {"both=256", 256, 256},
   [ALIGN_128] = {"both=128", 128, 128},
   [ALIGN_64] = {"both=64", 64, 64},
   [ALIGN_4] = {"both=4", 4, 4},
   [ALIGN_2] = {"both=2", 2, 2},
   [ALIGN_1] = {"both=1", 1, 1},
   [ALIGN_SRC128] = {"src=128", 128, 0},
   [ALIGN_SRC64] = {"src=64", 64, 0},
   [ALIGN_SRC4] = {"src=4", 4, 0},
   [ALIGN_SRC2] = {"src=2", 2, 0},
   [ALIGN_SRC1] = {"src=1", 1, 0},
   [ALIGN_DST128] = {"dst=128", 0, 128},
   [ALIGN_DST64] = {"dst=64", 0, 64},
   [ALIGN_DST4] = {"dst=4", 0, 4},
   [ALIGN_DST2] = {"dst=2", 0, 2},
   [ALIGN_DST1] = {"dst=1", 0, 1},
   [ALIGN_SRC4_DST2] = {"src=4 dst=2", 4, 2},
   [ALIGN_SRC4_DST1] = {"src=4 dst=1", 4, 1},
   [ALIGN_SRC2_DST4] = {"src=2 dst=4", 2, 4},
   [ALIGN_SRC2_DST1] = {"src=2 dst=1", 2, 1},
   [ALIGN_SRC1_DST4] = {"src=1 dst=4", 1, 4},
   [ALIGN_SRC1_DST2] = {"src=1 dst=2", 1, 2},
};

void si_test_dma_perf(struct si_screen *sscreen)
{
   struct pipe_screen *screen = &sscreen->b;
   struct pipe_context *ctx = screen->context_create(screen, NULL, 0);
   struct si_context *sctx = (struct si_context *)ctx;

   sscreen->ws->cs_set_pstate(&sctx->gfx_cs, RADEON_CTX_PSTATE_PEAK);

   printf("Test          , Method , Alignment  ,");
   for (unsigned size = MIN_SIZE; size <= MAX_SIZE; size <<= SIZE_SHIFT) {
      if (size >= 1024 * 1024)
         printf("%6uMB,", size / (1024 * 1024));
      else if (size >= 1024)
         printf("%6uKB,", size / 1024);
      else
         printf(" %6uB,", size);
   }
   printf("\n");

   /* Run benchmarks. */
   for (unsigned test_flavor = 0; test_flavor < NUM_TESTS; test_flavor++) {
      bool is_copy = test_flavor >= TEST_COPY_VRAM_VRAM;

      if (test_flavor)
         puts("");

      for (unsigned method = 0; method < NUM_METHODS; method++) {
         for (unsigned align = 0; align < NUM_ALIGNMENTS; align++) {
            unsigned dwords_per_thread, clear_value_size;
            unsigned src_offset = align_info[align].src_offset;
            unsigned dst_offset = align_info[align].dst_offset;

            /* offset > 0 && offset < 4 is the only case when the compute shader performs the same
             * as offset=0 without any alignment optimizations, so shift the offset by 4 to get
             * unaligned performance.
             */
            if (src_offset && src_offset < 4)
               src_offset += 4;
            if (dst_offset && dst_offset < 4)
               dst_offset += 4;

            if (!is_copy && dst_offset != src_offset)
               continue;

            if (test_flavor == TEST_FILL_VRAM_12B || test_flavor == TEST_FILL_GTT_12B) {
               if ((method != METHOD_DEFAULT && method != METHOD_COMPUTE_3DW &&
                    method != METHOD_COMPUTE_4DW) || dst_offset % 4)
                  continue;

               dwords_per_thread = method == METHOD_COMPUTE_3DW ? 3 : 4;
               clear_value_size = 12;
            } else {
               if (method == METHOD_COMPUTE_3DW)
                  continue;

               dwords_per_thread = method == METHOD_COMPUTE_2DW ? 2 : 4;
               clear_value_size = dst_offset % 4 ? 1 : 4;
            }

            printf("%-14s, %-7s, %-11s,", test_strings[test_flavor], method_strings[method],
                   align_info[align].string);

            for (unsigned size = MIN_SIZE; size <= MAX_SIZE; size <<= SIZE_SHIFT) {
               struct pipe_resource *dst, *src;
               enum pipe_resource_usage dst_usage = PIPE_USAGE_DEFAULT;
               enum pipe_resource_usage src_usage = PIPE_USAGE_DEFAULT;

               if (test_flavor == TEST_FILL_GTT || test_flavor == TEST_FILL_GTT_12B ||
                   test_flavor == TEST_COPY_VRAM_GTT)
                  dst_usage = PIPE_USAGE_STREAM;

               if (test_flavor == TEST_COPY_GTT_VRAM)
                  src_usage = PIPE_USAGE_STREAM;

               /* Don't test large sizes with GTT because it's slow. */
               if ((dst_usage == PIPE_USAGE_STREAM || src_usage == PIPE_USAGE_STREAM) &&
                   size > 16 * 1024 * 1024) {
                  printf("%8s,", "n/a");
                  continue;
               }

               dst = pipe_aligned_buffer_create(screen, 0, dst_usage, dst_offset + size, 256);
               src = is_copy ? pipe_aligned_buffer_create(screen, 0, src_usage, src_offset + size, 256) : NULL;

               struct pipe_query *q = ctx->create_query(ctx, PIPE_QUERY_TIME_ELAPSED, 0);
               bool success = true;

               /* Run tests. */
               for (unsigned iter = 0; iter < WARMUP_RUNS + NUM_RUNS; iter++) {
                  const uint32_t clear_value[4] = {0x12345678, 0x23456789, 0x34567890, 0x45678901};

                  if (iter == WARMUP_RUNS)
                     ctx->begin_query(ctx, q);

                  if (method == METHOD_DEFAULT) {
                     if (is_copy) {
                        si_barrier_before_simple_buffer_op(sctx, 0, dst, src);
                        si_copy_buffer(sctx, dst, src, dst_offset, src_offset, size);
                        si_barrier_after_simple_buffer_op(sctx, 0, dst, src);
                     } else {
                        sctx->b.clear_buffer(&sctx->b, dst, dst_offset, size, &clear_value,
                                             clear_value_size);
                     }
                  } else if (method == METHOD_CP_DMA) {
                     /* CP DMA */
                     if (sscreen->info.cp_sdma_ge_use_system_memory_scope) {
                        /* The CP DMA code doesn't implement this case. */
                        success = false;
                        continue;
                     }

                     if (is_copy) {
                        /* CP DMA copies are about as slow as PCIe on GFX6-8. */
                        if (sctx->gfx_level <= GFX8 && size > 16 * 1024 * 1024) {
                           success = false;
                           continue;
                        }

                        si_barrier_before_simple_buffer_op(sctx, 0, dst, src);
                        si_cp_dma_copy_buffer(sctx, dst, src, dst_offset, src_offset, size);
                        si_barrier_after_simple_buffer_op(sctx, 0, dst, src);
                     } else {
                        /* CP DMA clears must be aligned to 4 bytes. */
                        if (dst_offset % 4 || size % 4 ||
                            /* CP DMA clears are so slow on GFX6-8 that we risk getting a GPU timeout. */
                            (sctx->gfx_level <= GFX8 && size > 512 * 1024)) {
                           success = false;
                           continue;
                        }

                        assert(clear_value_size == 4);
                        si_barrier_before_simple_buffer_op(sctx, 0, dst, src);
                        si_cp_dma_clear_buffer(sctx, &sctx->gfx_cs, dst, dst_offset, size,
                                               clear_value[0]);
                        si_barrier_after_simple_buffer_op(sctx, 0, dst, src);
                     }
                  } else {
                     /* Compute */
                     si_barrier_before_simple_buffer_op(sctx, 0, dst, src);
                     success &=
                        si_compute_clear_copy_buffer(sctx, dst, dst_offset, src, src_offset,
                                                     size, clear_value, clear_value_size,
                                                     dwords_per_thread, false, false);
                     si_barrier_after_simple_buffer_op(sctx, 0, dst, src);
                  }

                  sctx->barrier_flags |= SI_BARRIER_INV_L2;
               }

               ctx->end_query(ctx, q);

               pipe_resource_reference(&dst, NULL);
               pipe_resource_reference(&src, NULL);

               /* Get results. */
               union pipe_query_result result;

               ctx->get_query_result(ctx, q, true, &result);
               ctx->destroy_query(ctx, q);

               /* Navi10 and Vega10 sometimes incorrectly return elapsed time of 0 nanoseconds
                * for very small ops.
                */
               if (success && result.u64) {
                  double GB = 1024.0 * 1024.0 * 1024.0;
                  double seconds = result.u64 / (double)NUM_RUNS / (1000.0 * 1000.0 * 1000.0);
                  double GBps = (size / GB) / seconds * (test_flavor == TEST_COPY_VRAM_VRAM ? 2 : 1);
                  printf("%8.2f,", GBps);
               } else {
                  printf("%8s,", "n/a");
               }
            }
            puts("");
         }
      }
   }

   ctx->destroy(ctx);
   exit(0);
}

void
si_test_mem_perf(struct si_screen *sscreen)
{
   struct radeon_winsys *ws = sscreen->ws;
   const size_t buffer_size = 16 * 1024 * 1024;
   const enum radeon_bo_domain domains[] = { 0, RADEON_DOMAIN_VRAM, RADEON_DOMAIN_GTT };
   const uint64_t flags[] = { 0, RADEON_FLAG_GTT_WC };
   const int n_loops = 2;
   char *title[] = { "Write To", "Read From", "Stream From" };
   char *domain_str[] = { "RAM", "VRAM", "GTT" };

   for (int i = 0; i < 3; i++) {
      printf("| %12s", title[i]);

      printf(" | Size (kB) | Flags |");
      for (int l = 0; l < n_loops; l++)
          printf(" Run %d (MB/s) |", l + 1);
      printf("\n");

      printf("|--------------|-----------|-------|");
      for (int l = 0; l < n_loops; l++)
          printf("--------------|");
      printf("\n");
      for (int j = 0; j < ARRAY_SIZE(domains); j++) {
         enum radeon_bo_domain domain = domains[j];
         for (int k = 0; k < ARRAY_SIZE(flags); k++) {
            if (k && domain != RADEON_DOMAIN_GTT)
               continue;

            struct pb_buffer_lean *bo = NULL;
            void *ptr = NULL;

            if (domains[j]) {
               bo = ws->buffer_create(ws, buffer_size, 4096, domains[j],
                                      RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_NO_SUBALLOC |
                                      flags[k]);
               if (!bo)
                  continue;

               ptr = ws->buffer_map(ws, bo, NULL, RADEON_MAP_TEMPORARY | (i ? PIPE_MAP_READ : PIPE_MAP_WRITE));
               if (!ptr) {
                  radeon_bo_reference(ws, &bo, NULL);
                  continue;
               }
            } else {
               ptr = malloc(buffer_size);
            }

            printf("| %12s |", domain_str[j]);

            printf("%10zu |", buffer_size / 1024);

            printf(" %5s |", domain == RADEON_DOMAIN_VRAM ? "(WC)" : (k == 0 ? "" : "WC "));

            int *cpu = calloc(1, buffer_size);
            memset(cpu, 'c', buffer_size);
            fflush(stdout);

            int64_t before, after;

            for (int loop = 0; loop < n_loops; loop++) {
               before = os_time_get_nano();

               switch (i) {
               case 0:
                  memcpy(ptr, cpu, buffer_size);
                  break;
               case 1:
                  memcpy(cpu, ptr, buffer_size);
                  break;
               case 2:
               default:
                  util_streaming_load_memcpy(cpu, ptr, buffer_size);
                  break;
               }

               after = os_time_get_nano();

               /* Pretend to do something with the result to make sure it's
                * not skipped.
                */
               if (debug_get_num_option("AMD_DEBUG", 0) == 0x123)
                   assert(memcmp(ptr, cpu, buffer_size));

               float dt = (after - before) / (1000000000.0);
               float bandwidth = (buffer_size / (1024 * 1024)) / dt;

               printf("%13.3f |", bandwidth);
            }
            printf("\n");

            free(cpu);
            if (bo) {
               ws->buffer_unmap(ws, bo);
               radeon_bo_reference(ws, &bo, NULL);
            } else {
               free(ptr);
            }
         }
      }
      printf("\n");
   }


   exit(0);
}

#define COLOR_RESET  "\033[0m"
#define COLOR_RED    "\033[1;31m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_CYAN   "\033[1;36m"

void si_test_clear_buffer(struct si_screen *sscreen)
{
   struct pipe_screen *screen = &sscreen->b;
   struct pipe_context *ctx = screen->context_create(screen, NULL, 0);
   struct si_context *sctx = (struct si_context *)ctx;
   unsigned buf_size = 32;
   unsigned num_tests = 0, num_passes = 0;

   srand(0x9b47d95b);

   printf("dst, si,dw, %-*s, %-*s, %-*s, %-*s\n",
          32, "clear value",
          buf_size * 2, "init dst",
          buf_size * 2, "expected dst",
          buf_size * 2, "observed dst");
   printf("off, ze,th\n");

   /* Generate an infinite number of random tests. */
   while (1) {
      struct pipe_resource *dst;

      dst = pipe_aligned_buffer_create(screen, 0, PIPE_USAGE_STAGING, buf_size, 256);

      unsigned clear_value_size = 1 << (rand() % 6);
      if (clear_value_size == 32)
         clear_value_size = 12; /* test only 1, 2, 4, 8, 16, and 12 */

      uint8_t *clear_value = (uint8_t *)malloc(buf_size);
      uint8_t *init_dst_buffer = (uint8_t *)malloc(buf_size);
      uint8_t *expected_dst_buffer = (uint8_t *)malloc(buf_size);
      uint8_t *read_dst_buffer = (uint8_t *)malloc(buf_size);

      for (unsigned i = 0; i < buf_size; i++) {
         clear_value[i] = rand();
         init_dst_buffer[i] = rand();
         expected_dst_buffer[i] = rand();
      }

      pipe_buffer_write(ctx, dst, 0, buf_size, init_dst_buffer);

      unsigned op_size = (((rand() % buf_size) + 1) / clear_value_size) * clear_value_size;
      if (!op_size)
         op_size = clear_value_size;

      unsigned dst_offset = rand() % (buf_size - op_size + 1);
      if (clear_value_size == 12)
         dst_offset &= ~0x3;

      unsigned dwords_per_thread = 1 << (rand() % 3);
      dwords_per_thread = MAX2(dwords_per_thread, DIV_ROUND_UP(clear_value_size, 4));

      memcpy(expected_dst_buffer, init_dst_buffer, buf_size);
      for (unsigned i = 0; i < op_size; i++)
         expected_dst_buffer[dst_offset + i] = clear_value[i % clear_value_size];

      printf(" %2u, %2u, %u, ", dst_offset, op_size, dwords_per_thread);

      /* Visualize the clear. */
      for (unsigned i = 0; i < clear_value_size; i++)
         printf("%02x", clear_value[i]);
      for (unsigned i = clear_value_size; i < 16; i++)
         printf("  ");

      printf("%s, %s", COLOR_RESET, COLOR_CYAN);
      for (unsigned i = 0; i < buf_size; i++) {
         printf("%s%02x",
                i < dst_offset || i >= dst_offset + op_size ? COLOR_CYAN : COLOR_RESET,
                init_dst_buffer[i]);
      }
      printf("%s, ", COLOR_RESET);
      for (unsigned i = 0; i < buf_size; i++) {
         printf("%s%02x",
                i >= dst_offset && i < dst_offset + op_size ? COLOR_YELLOW : COLOR_CYAN,
                expected_dst_buffer[i]);
      }
      printf("%s, ", COLOR_RESET);
      fflush(stdout);

      si_barrier_before_simple_buffer_op(sctx, 0, dst, NULL);
      bool done = si_compute_clear_copy_buffer(sctx, dst, dst_offset, NULL, 0, op_size,
                                               (uint32_t*)clear_value, clear_value_size,
                                               dwords_per_thread, false, false);
      si_barrier_after_simple_buffer_op(sctx, 0, dst, NULL);

      if (done) {
         pipe_buffer_read(ctx, dst, 0, buf_size, read_dst_buffer);
         bool success = !memcmp(read_dst_buffer, expected_dst_buffer, buf_size);

         num_tests++;
         if (success)
            num_passes++;

         for (unsigned i = 0; i < buf_size; i++) {
            printf("%s%02x",
                   read_dst_buffer[i] != expected_dst_buffer[i] ? COLOR_RED :
                   i >= dst_offset && i < dst_offset + op_size ? COLOR_YELLOW : COLOR_CYAN,
                   read_dst_buffer[i]);
         }

         printf("%s, %s [%u/%u]\n", COLOR_RESET, success ? "pass" : "fail", num_passes, num_tests);
      } else {
         printf("%*s, skip [%u/%u]\n", buf_size * 2, "", num_passes, num_tests);
      }

      free(clear_value);
      free(init_dst_buffer);
      free(expected_dst_buffer);
      free(read_dst_buffer);
      pipe_resource_reference(&dst, NULL);
   }

   ctx->destroy(ctx);
   exit(0);
}

void si_test_copy_buffer(struct si_screen *sscreen)
{
   struct pipe_screen *screen = &sscreen->b;
   struct pipe_context *ctx = screen->context_create(screen, NULL, 0);
   struct si_context *sctx = (struct si_context *)ctx;
   unsigned buf_size = 32;
   unsigned num_tests = 0, num_passes = 0;

   srand(0x9b47d95b);

   printf("src,dst, si,dw, %-*s, %-*s, %-*s, %-*s\n",
          MIN2(buf_size, 32) * 2, "init src",
          MIN2(buf_size, 32) * 2, "init dst",
          MIN2(buf_size, 32) * 2, "expected dst",
          MIN2(buf_size, 32) * 2, "observed dst");
   printf("off,off, ze,th\n");

   /* Generate an infinite number of random tests. */
   while (1) {
      struct pipe_resource *dst, *src;

      dst = pipe_aligned_buffer_create(screen, 0, PIPE_USAGE_STAGING, buf_size, 256);
      src = pipe_aligned_buffer_create(screen, 0, PIPE_USAGE_STAGING, buf_size, 256);

      uint8_t *init_src_buffer = (uint8_t *)malloc(buf_size);
      uint8_t *init_dst_buffer = (uint8_t *)malloc(buf_size);
      uint8_t *expected_dst_buffer = (uint8_t *)malloc(buf_size);
      uint8_t *read_dst_buffer = (uint8_t *)malloc(buf_size);

      for (unsigned i = 0; i < buf_size; i++) {
         init_src_buffer[i] = rand();
         init_dst_buffer[i] = rand();
      }

      pipe_buffer_write(ctx, src, 0, buf_size, init_src_buffer);
      pipe_buffer_write(ctx, dst, 0, buf_size, init_dst_buffer);

      unsigned dst_offset = rand() % buf_size;
      unsigned op_size = (rand() % (buf_size - dst_offset)) + 1;
      unsigned src_offset = rand() % (buf_size - op_size + 1);
      unsigned dwords_per_thread = 1 << (rand() % 3);

      memcpy(expected_dst_buffer, init_dst_buffer, buf_size);
      memcpy(expected_dst_buffer + dst_offset, init_src_buffer + src_offset, op_size);

      printf(" %2u, %2u, %2u, %u, ", src_offset, dst_offset, op_size, dwords_per_thread);

      if (buf_size <= 32) {
         /* Visualize the copy. */
         for (unsigned i = 0; i < buf_size; i++) {
            printf("%s%02x",
                   i >= src_offset && i < src_offset + op_size ? COLOR_YELLOW : COLOR_RESET,
                   init_src_buffer[i]);
         }
         printf("%s, %s", COLOR_RESET, COLOR_CYAN);
         for (unsigned i = 0; i < buf_size; i++) {
            printf("%s%02x",
                   i < dst_offset || i >= dst_offset + op_size ? COLOR_CYAN : COLOR_RESET,
                   init_dst_buffer[i]);
         }
         printf("%s, ", COLOR_RESET);
         for (unsigned i = 0; i < buf_size; i++) {
            printf("%s%02x",
                   i >= dst_offset && i < dst_offset + op_size ? COLOR_YELLOW : COLOR_CYAN,
                   expected_dst_buffer[i]);
         }
         printf("%s, ", COLOR_RESET);
      }
      fflush(stdout);

      si_barrier_before_simple_buffer_op(sctx, 0, dst, src);
      bool done = si_compute_clear_copy_buffer(sctx, dst, dst_offset, src, src_offset, op_size,
                                               NULL, 0, dwords_per_thread, false, false);
      si_barrier_after_simple_buffer_op(sctx, 0, dst, src);

      if (done) {
         pipe_buffer_read(ctx, dst, 0, buf_size, read_dst_buffer);
         bool success = !memcmp(read_dst_buffer, expected_dst_buffer, buf_size);

         num_tests++;
         if (success)
            num_passes++;

         if (buf_size <= 32) {
            for (unsigned i = 0; i < buf_size; i++) {
               printf("%s%02x",
                      read_dst_buffer[i] != expected_dst_buffer[i] ? COLOR_RED :
                      i >= dst_offset && i < dst_offset + op_size ? COLOR_YELLOW : COLOR_CYAN,
                      read_dst_buffer[i]);
            }
            printf("%s, ", COLOR_RESET);
         }

         printf("%s [%u/%u]\n", success ? "pass" : "fail", num_passes, num_tests);
      } else {
         printf("%*s, skip [%u/%u]\n", buf_size * 2, "", num_passes, num_tests);
      }

      free(init_src_buffer);
      free(init_dst_buffer);
      free(expected_dst_buffer);
      free(read_dst_buffer);
      pipe_resource_reference(&dst, NULL);
      pipe_resource_reference(&src, NULL);
   }

   ctx->destroy(ctx);
   exit(0);
}
