/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/* This file implements performance tests for graphics and compute blits and clears. */

#include "si_pipe.h"
#include "util/rand_xor.h"
#include "util/u_surface.h"

#define RANDOM_DATA_SIZE (611953 * 8) /* prime number * 8 */

/* For MSAA, level_or_sample_index == 0 means set all samples, while level_or_sample_index > 0
 * means set the sample equal to level_or_sample_index - 1.
 */
static void set_random_pixels(struct pipe_context *ctx, struct pipe_resource *tex,
                              unsigned level_or_sample_index, uint64_t *random_data)
{
   struct pipe_transfer *t;
   uint8_t *map = pipe_texture_map_3d(ctx, tex, level_or_sample_index, PIPE_MAP_WRITE, 0, 0, 0,
                                      tex->width0, tex->height0, tex->depth0, &t);
   assert(map);
   /* It's static because we want following calls of this function to continue from
    * the previous offset.
    */
   static unsigned random_data_offset = 0;

   for (unsigned z = 0; z < tex->depth0; z++) {
      for (unsigned y = 0; y < tex->height0; y++) {
         uint64_t *ptr = (uint64_t *)(map + t->layer_stride * z + t->stride * y);
         unsigned size = t->stride;
         assert(size % 8 == 0);

         while (size) {
            unsigned copy_size =
               random_data_offset + size <= RANDOM_DATA_SIZE ? size :
                                                               RANDOM_DATA_SIZE - random_data_offset;

            memcpy(ptr, (uint8_t*)random_data + random_data_offset, copy_size);
            size -= copy_size;
            ptr += copy_size / 8;
            random_data_offset += copy_size;
            if (random_data_offset >= RANDOM_DATA_SIZE) {
               assert(random_data_offset == RANDOM_DATA_SIZE);
               random_data_offset = 0;
            }
         }
      }
   }

   pipe_texture_unmap(ctx, t);
}

static void set_gradient_pixels(struct pipe_context *ctx, struct pipe_resource *tex)
{
   struct pipe_transfer *t;
   uint8_t *map = pipe_texture_map_3d(ctx, tex, 0, PIPE_MAP_WRITE, 0, 0, 0,
                                      tex->width0, tex->height0, tex->depth0, &t);
   assert(map);

   /* Generate just 1 line of pixels. */
   unsigned pix_size = util_format_get_blocksize(tex->format);
   unsigned line_size = tex->width0 * pix_size;
   uint8_t *line = (uint8_t*)malloc(line_size);

   if (util_format_is_pure_integer(tex->format)) {
      for (unsigned x = 0; x < tex->width0; x++) {
         union pipe_color_union color;
         color.ui[0] = color.ui[1] = color.ui[2] = color.ui[3] = x;

         util_pack_color_union(tex->format, (union util_color *)(line + x * pix_size), &color);
      }
   } else if (util_format_is_float(tex->format)) {
      for (unsigned x = 0; x < tex->width0; x++) {
         union pipe_color_union color;
         color.f[0] = color.f[1] = color.f[2] = color.f[3] = (float)x / (tex->width0 - 1);

         util_pack_color_union(tex->format, (union util_color *)(line + x * pix_size), &color);
      }
   } else {
      for (unsigned x = 0; x < tex->width0; x++)
         util_pack_color_ub(x, x, x, x, tex->format, (union util_color *)(line + x * pix_size));
   }

   /* Copy the generated line to all lines. */
   for (unsigned z = 0; z < tex->depth0; z++) {
      for (unsigned y = 0; y < tex->height0; y++)
         memcpy(map + t->layer_stride * z + t->stride * y, line, line_size);
   }

   free(line);
   pipe_texture_unmap(ctx, t);
}

static enum pipe_format formats[] = {
   PIPE_FORMAT_R8_UNORM,
   PIPE_FORMAT_R8_UINT,
   PIPE_FORMAT_R16_UINT,
   PIPE_FORMAT_R16_FLOAT,
   PIPE_FORMAT_R8G8B8A8_UNORM,
   PIPE_FORMAT_R32_UINT,
   PIPE_FORMAT_R32_FLOAT,
   PIPE_FORMAT_R32G32_UINT,
   PIPE_FORMAT_R32G32_FLOAT,
   PIPE_FORMAT_R16G16B16A16_FLOAT,
   PIPE_FORMAT_R32G32B32A32_UINT,
   PIPE_FORMAT_R32G32B32A32_FLOAT,
};

enum {
   TEST_FB_CLEAR,
   TEST_CLEAR,
   TEST_COPY,
   TEST_BLIT,
   TEST_RESOLVE,
   NUM_TESTS,
};

static const char *test_strings[] = {
   [TEST_FB_CLEAR] = "fbclear",
   [TEST_CLEAR] = "cleartex",
   [TEST_COPY] = "copy",
   [TEST_BLIT] = "blit",
   [TEST_RESOLVE] = "resolve",
};

enum {
   LAYOUT_T2T, /* tiled to tiled or clear tiled */
   LAYOUT_L2T, /* linear to tiled */
   LAYOUT_T2L, /* tiled to linear */
   LAYOUT_L2L, /* linear to linear or clear linear */
   NUM_LAYOUTS,
};

static const char *layout_strings[] = {
   [LAYOUT_T2T] = "T2T",
   [LAYOUT_L2T] = "L2T",
   [LAYOUT_T2L] = "T2L",
   [LAYOUT_L2L] = "L2L",
};

enum {
   BOX_FULL,
   BOX_FULL_YFLIP,
   BOX_PARTIAL,
   BOX_PARTIAL_UNALIGNED,
   BOX_PARTIAL_UNALIGNED_YFLIP,
   NUM_BOXES,
};

static const char *box_strings[] = {
   [BOX_FULL] = "full",
   [BOX_FULL_YFLIP] = "yflip",
   [BOX_PARTIAL] = "partial",
   [BOX_PARTIAL_UNALIGNED] = "unaligned",
   [BOX_PARTIAL_UNALIGNED_YFLIP] = "yflip/unali",
};

enum {
   FILL_BLACK,
   FILL_SOLID,
   FILL_GRADIENT,
   FILL_RANDOM,
   FILL_RANDOM_FRAGMENTED2,
   FILL_RANDOM_FRAGMENTED4,
   FILL_RANDOM_FRAGMENTED8,
   NUM_FILLS,
};

static const char *fill_strings[] = {
   [FILL_BLACK] = "black",
   [FILL_SOLID] = "solid",
   [FILL_GRADIENT] = "gradient",
   [FILL_RANDOM] = "random",
   [FILL_RANDOM_FRAGMENTED2] = "fragmented2",
   [FILL_RANDOM_FRAGMENTED4] = "fragmented4",
   [FILL_RANDOM_FRAGMENTED8] = "fragmented8",
};

enum {
   METHOD_DEFAULT,
   METHOD_GFX,
   METHOD_COMPUTE,
   METHOD_SPECIAL,
   NUM_METHODS,
};

static const union pipe_color_union black_color_float = {.f = {0, 0, 0, 0}};
static const union pipe_color_union solid_color_float = {.f = {0.2, 0.3, 0.4, 0.5}};
static const union pipe_color_union black_color_uint = {.ui = {0, 0, 0, 0}};
static const union pipe_color_union solid_color_uint = {.ui = {23, 45, 89, 107}};

void si_test_blit_perf(struct si_screen *sscreen)
{
   struct pipe_screen *screen = &sscreen->b;
   struct pipe_context *ctx = screen->context_create(screen, NULL, 0);
   struct si_context *sctx = (struct si_context *)ctx;

   uint64_t seed_xorshift128plus[2];
   uint64_t random_data[RANDOM_DATA_SIZE / 8];

   /* Set the seed for random pixel data */
   s_rand_xorshift128plus(seed_xorshift128plus, false);

   /* Pre-generate random data for initializing textures. */
   for (unsigned i = 0; i < ARRAY_SIZE(random_data); i++)
      random_data[i] = rand_xorshift128plus(seed_xorshift128plus);

   sscreen->ws->cs_set_pstate(&sctx->gfx_cs, RADEON_CTX_PSTATE_PEAK);

   printf("Op      , Special  ,Dim, Format            ,MS,Layout, Fill       , Box         ,"
          "   small   ,   small   ,   small   ,   small   ,   LARGE   ,   LARGE   ,   LARGE   ,   LARGE\n");
   printf("--------,----------,---,-------------------,--,------,------------,-------------,"
          "  Default  ,    Gfx    ,  Compute  ,  Special  ,  Default  ,    Gfx    ,  Compute  ,  Special\n");

   for (unsigned test_flavor = 0; test_flavor < NUM_TESTS; test_flavor++) {
      for (unsigned dim = 1; dim <= 3; dim++) {
         for (unsigned format_index = 0; format_index < ARRAY_SIZE(formats); format_index++) {
            for (unsigned samples = 1; samples <= 8; samples *= 2) {
               for (unsigned layout = 0; layout < NUM_LAYOUTS; layout++) {
                  /* Reject invalid combinations. */
                  if (samples >= 2 && (dim != 2 || layout != LAYOUT_T2T))
                     continue;

                  if (dim == 1 && layout != LAYOUT_L2L)
                     continue;

                  if (test_flavor != TEST_COPY &&
                      (layout == LAYOUT_L2T || layout == LAYOUT_T2L))
                     continue;

                  if (test_flavor != TEST_COPY && dim != 1 &&
                      layout != LAYOUT_T2T)
                     continue;

                  if (test_flavor == TEST_RESOLVE && samples == 1)
                     continue;

                  if (test_flavor == TEST_RESOLVE &&
                      util_format_is_pure_integer(formats[format_index]))
                     continue;

                  /* Create textures. */
                  struct pipe_resource *src[2] = {0}, *dst[2] = {0};
                  const struct pipe_resource templ = {
                     .array_size = 1,
                     .format = formats[format_index],
                     .target = PIPE_TEXTURE_1D + dim - 1,
                     .usage = PIPE_USAGE_DEFAULT,
                  };

                  unsigned bpe = util_format_get_blocksize(templ.format);
                  unsigned pix_size = bpe * samples;

                  /* TODOs:
                   * CS:
                   * - optimize for gfx10.3
                   * - move the compute blit to amd/common
                   */

                  for (unsigned size_factor = 0; size_factor <= 1; size_factor++) {
                     unsigned mb_size = (size_factor ? 256 : 8) * 1024 * 1024;
                     unsigned width = 1, height = 1, depth = 1;

                     /* Determine the size. The footprint must be exactly "mb_size" for 2D and 3D. */
                     if (dim == 1) {
                        width = size_factor ? 16384 : 2048;
                     } else if (dim == 2) {
                        width = height = util_next_power_of_two(sqrt(mb_size / pix_size));

                        for (unsigned i = 0; width * height * pix_size != mb_size; i++) {
                           if (i % 2 == 1)
                              width /= 2;
                           else
                              height /= 2;
                        }
                     } else if (dim == 3) {
                        width = height = depth = util_next_power_of_two(pow(mb_size / pix_size, 0.333333));

                        for (unsigned i = 0; width * height * depth * pix_size != mb_size; i++) {
                           if (i % 3 == 2)
                              width /= 2;
                           else if (i % 3 == 1)
                              height /= 2;
                           else
                              depth /= 2;
                        }
                     }

                     struct pipe_resource src_templ = templ;
                     src_templ.width0 = MIN2(width, 16384);
                     src_templ.height0 = MIN2(height, 16384);
                     src_templ.depth0 = MIN2(depth, 16384);
                     src_templ.nr_samples = src_templ.nr_storage_samples = samples;
                     src_templ.bind = layout == LAYOUT_L2L ||
                                      layout == LAYOUT_L2T ? PIPE_BIND_LINEAR : 0;

                     if (test_flavor != TEST_FB_CLEAR && test_flavor != TEST_CLEAR)
                        src[size_factor] = screen->resource_create(screen, &src_templ);

                     struct pipe_resource dst_templ = src_templ;
                     dst_templ.bind = layout == LAYOUT_L2L ||
                                      layout == LAYOUT_T2L ? PIPE_BIND_LINEAR : 0;
                     if (test_flavor == TEST_RESOLVE)
                        dst_templ.nr_samples = dst_templ.nr_storage_samples = 1;

                     dst[size_factor] = screen->resource_create(screen, &dst_templ);
                  }

                  for (unsigned fill_flavor = 0; fill_flavor < NUM_FILLS; fill_flavor++) {
                     const union pipe_color_union *clear_color =
                        util_format_is_pure_integer(templ.format) ?
                           (fill_flavor == FILL_BLACK ? &black_color_uint : &solid_color_uint) :
                           (fill_flavor == FILL_BLACK ? &black_color_float : &solid_color_float);

                     /* Reject invalid combinations. */
                     if ((test_flavor == TEST_FB_CLEAR || test_flavor == TEST_CLEAR) &&
                         fill_flavor != FILL_SOLID && fill_flavor != FILL_BLACK)
                        continue;

                     if ((samples == 1 && fill_flavor >= FILL_RANDOM_FRAGMENTED2) ||
                         (samples == 2 && fill_flavor >= FILL_RANDOM_FRAGMENTED4) ||
                         (samples == 4 && fill_flavor >= FILL_RANDOM_FRAGMENTED8))
                        continue;

                     /* Fill the source texture. */
                     if (test_flavor != TEST_FB_CLEAR && test_flavor != TEST_CLEAR) {
                        for (unsigned size_factor = 0; size_factor <= 1; size_factor++) {
                           switch (fill_flavor) {
                           case FILL_BLACK:
                           case FILL_SOLID: {
                              union util_color packed_color;
                              util_pack_color(clear_color->f, templ.format, &packed_color);

                              struct pipe_box box = {0};
                              box.width = src[size_factor]->width0;
                              box.height = src[size_factor]->height0;
                              box.depth = src[size_factor]->depth0;

                              ctx->clear_texture(ctx, src[size_factor], 0, &box, &packed_color);
                              break;
                           }

                           case FILL_GRADIENT:
                              set_gradient_pixels(ctx, src[size_factor]);
                              break;

                           case FILL_RANDOM:
                              set_random_pixels(ctx, src[size_factor], 0, random_data);
                              break;

                           case FILL_RANDOM_FRAGMENTED2:
                              assert(samples >= 2);
                              /* Make all samples equal. */
                              set_random_pixels(ctx, src[size_factor], 0, random_data);
                              /* Make sample 0 different. */
                              set_random_pixels(ctx, src[size_factor], 1, random_data);
                              break;

                           case FILL_RANDOM_FRAGMENTED4:
                              assert(samples >= 4);
                              /* Make all samples equal. */
                              set_random_pixels(ctx, src[size_factor], 0, random_data);
                              /* Make samples 0..2 different. */
                              for (unsigned i = 0; i <= 2; i++)
                                 set_random_pixels(ctx, src[size_factor], i + 1, random_data);
                              break;

                           case FILL_RANDOM_FRAGMENTED8:
                              assert(samples == 8);
                              /* Make all samples equal. */
                              set_random_pixels(ctx, src[size_factor], 0, random_data);
                              /* Make samples 0..6 different. */
                              for (unsigned i = 0; i <= 6; i++)
                                 set_random_pixels(ctx, src[size_factor], i + 1, random_data);
                              break;
                           }
                        }
                     }

                     for (unsigned box_flavor = 0; box_flavor < NUM_BOXES; box_flavor++) {
                        bool yflip = box_flavor == BOX_FULL_YFLIP ||
                                     box_flavor == BOX_PARTIAL_UNALIGNED_YFLIP;

                        /* Reject invalid combinations. */
                        if (test_flavor == TEST_FB_CLEAR && box_flavor != BOX_FULL)
                           continue;

                        if ((test_flavor == TEST_CLEAR || test_flavor == TEST_COPY) && yflip)
                           continue;

                        const char *special_op =
                           test_flavor == TEST_FB_CLEAR ? "cleartex" :
                           test_flavor == TEST_CLEAR && box_flavor == BOX_FULL ? "fastclear" :
                           test_flavor == TEST_BLIT && !yflip ? "copy" :
                           test_flavor == TEST_RESOLVE ? "cbresolve" : "n/a";

                        printf("%-8s, %-9s, %uD, %-18s, %u, %-5s, %-11s, %-11s",
                               test_strings[test_flavor], special_op, dim,
                               util_format_short_name(formats[format_index]), samples,
                               layout_strings[layout], fill_strings[fill_flavor],
                               box_strings[box_flavor]);

                        for (unsigned size_factor = 0; size_factor <= 1; size_factor++) {
                           /* Determine the box. */
                           struct pipe_box src_box = {0}, dst_box = {0};
                           dst_box.width = dst[size_factor]->width0;
                           dst_box.height = dst[size_factor]->height0;
                           dst_box.depth = dst[size_factor]->depth0;
                           src_box = dst_box;

                           switch (box_flavor) {
                           case BOX_FULL:
                              break;

                           case BOX_FULL_YFLIP:
                              src_box.y = src_box.height;
                              src_box.height = -src_box.height;
                              break;

                           case BOX_PARTIAL:
                              if (dim == 1) {
                                 dst_box.x = 256;
                                 dst_box.width -= 256;
                              } else if (dim == 2) {
                                 dst_box.x = 16;
                                 dst_box.y = 16;
                                 dst_box.width -= 16;
                                 dst_box.height -= 16;
                              } else {
                                 dst_box.x = 8;
                                 dst_box.y = 8;
                                 dst_box.z = 8;
                                 dst_box.width -= 8;
                                 dst_box.height -= 8;
                                 dst_box.depth -= 8;
                              }
                              src_box = dst_box;
                              break;

                           case BOX_PARTIAL_UNALIGNED:
                           case BOX_PARTIAL_UNALIGNED_YFLIP: {
                              const unsigned off = 13;
                              dst_box.x = off;
                              dst_box.width -= off;
                              if (dim >= 2) {
                                 dst_box.y = off;
                                 dst_box.height -= off;
                                 if (dim == 3) {
                                    dst_box.z = off;
                                    dst_box.depth -= off;
                                 }
                              }
                              src_box = dst_box;

                              if (box_flavor == BOX_PARTIAL_UNALIGNED_YFLIP) {
                                 src_box.y += src_box.height;
                                 src_box.height = -src_box.height;
                              }
                              break;
                           }
                           }

                           assert(dst_box.x >= 0);
                           assert(dst_box.y >= 0);
                           assert(dst_box.z >= 0);
                           assert(dst_box.width > 0);
                           assert(dst_box.height > 0);
                           assert(dst_box.depth > 0);
                           assert(dst_box.x + dst_box.width <= dst[size_factor]->width0);
                           assert(dst_box.y + dst_box.height <= dst[size_factor]->height0);
                           assert(dst_box.z + dst_box.depth <= dst[size_factor]->depth0);

                           if (src[size_factor]) {
                              assert(src_box.width);
                              assert(src_box.height);
                              assert(src_box.depth > 0);
                              if (src_box.width > 0) {
                                 assert(src_box.x >= 0);
                                 assert(src_box.x + src_box.width <= src[size_factor]->width0);
                              } else {
                                 assert(src_box.x + src_box.width >= 0);
                                 assert(src_box.x - 1 < src[size_factor]->width0);
                              }
                              if (src_box.height > 0) {
                                 assert(src_box.y >= 0);
                                 assert(src_box.y + src_box.height <= src[size_factor]->height0);
                              } else {
                                 assert(src_box.y + src_box.height >= 0);
                                 assert(src_box.y - 1 < src[size_factor]->height0);
                              }
                              assert(src_box.z >= 0);
                              assert(src_box.z + src_box.depth <= src[size_factor]->depth0);
                           }

                           for (unsigned method = 0; method < NUM_METHODS; method++) {
                              struct pipe_surface *dst_surf = NULL;

                              /* Create pipe_surface for clears. */
                              if (test_flavor == TEST_FB_CLEAR || test_flavor == TEST_CLEAR) {
                                 struct pipe_surface surf_templ;

                                 u_surface_default_template(&surf_templ, dst[size_factor]);
                                 surf_templ.u.tex.last_layer = dst[size_factor]->depth0 - 1;
                                 dst_surf = ctx->create_surface(ctx, dst[size_factor], &surf_templ);

                                 /* Bind the colorbuffer for FB clears. */
                                 if (box_flavor == BOX_FULL) {
                                    struct pipe_framebuffer_state fb = {0};
                                    fb.width = dst[size_factor]->width0;
                                    fb.height = dst[size_factor]->height0;
                                    fb.layers = dst[size_factor]->depth0;
                                    fb.samples = dst[size_factor]->nr_samples;
                                    fb.nr_cbufs = 1;
                                    fb.cbufs[0] = dst_surf;
                                    ctx->set_framebuffer_state(ctx, &fb);
                                    si_emit_barrier_direct(sctx);
                                 }
                              }

                              struct pipe_query *q = ctx->create_query(ctx, PIPE_QUERY_TIME_ELAPSED, 0);
                              unsigned num_warmup_repeats = 1, num_repeats = 4;
                              bool success = true;

                              /* Run tests. */
                              for (unsigned i = 0; i < num_warmup_repeats + num_repeats; i++) {
                                 /* The first few just warm up caches and the hw. */
                                 if (i == num_warmup_repeats)
                                    ctx->begin_query(ctx, q);

                                 switch (test_flavor) {
                                 case TEST_FB_CLEAR:
                                 case TEST_CLEAR:
                                    switch (method) {
                                    case METHOD_DEFAULT:
                                       if (test_flavor == TEST_FB_CLEAR) {
                                          ctx->clear(ctx, PIPE_CLEAR_COLOR, NULL, clear_color, 0, 0);
                                          sctx->barrier_flags |= SI_BARRIER_SYNC_AND_INV_CB | SI_BARRIER_INV_L2;
                                       } else {
                                          ctx->clear_render_target(ctx, dst_surf, clear_color,
                                                                   dst_box.x, dst_box.y,
                                                                   dst_box.width, dst_box.height,
                                                                   false);
                                       }
                                       break;
                                    case METHOD_GFX:
                                       si_gfx_clear_render_target(ctx, dst_surf, clear_color,
                                                                  dst_box.x, dst_box.y,
                                                                  dst_box.width, dst_box.height,
                                                                  false);
                                       break;
                                    case METHOD_COMPUTE:
                                       success &=
                                          si_compute_clear_image(sctx, dst_surf->texture,
                                                                 dst_surf->format, 0, &dst_box,
                                                                 clear_color, false, false);
                                       break;
                                    case METHOD_SPECIAL:
                                       if (test_flavor == TEST_CLEAR) {
                                          success &=
                                             si_compute_fast_clear_image(sctx, dst_surf->texture,
                                                                         dst_surf->format, 0,
                                                                         &dst_box, clear_color,
                                                                         false, false);
                                       } else {
                                          ctx->clear_render_target(ctx, dst_surf, clear_color,
                                                                   dst_box.x, dst_box.y,
                                                                   dst_box.width, dst_box.height,
                                                                   false);
                                       }
                                       break;
                                    }
                                    break;

                                 case TEST_COPY:
                                    switch (method) {
                                    case METHOD_DEFAULT:
                                       si_resource_copy_region(ctx, dst[size_factor], 0, dst_box.x,
                                                               dst_box.y, dst_box.z, src[size_factor],
                                                               0, &src_box);
                                       break;
                                    case METHOD_GFX:
                                       si_gfx_copy_image(sctx, dst[size_factor], 0, dst_box.x,
                                                         dst_box.y, dst_box.z, src[size_factor],
                                                         0, &src_box);
                                       break;
                                    case METHOD_COMPUTE:
                                       success &= si_compute_copy_image(sctx, dst[size_factor], 0,
                                                                        src[size_factor], 0,
                                                                        dst_box.x, dst_box.y,
                                                                        dst_box.z, &src_box, false);
                                       break;
                                    case METHOD_SPECIAL:
                                       success = false;
                                       break;
                                    }
                                    break;

                                 case TEST_BLIT:
                                 case TEST_RESOLVE: {
                                    struct pipe_blit_info info;
                                    memset(&info, 0, sizeof(info));
                                    info.dst.resource = dst[size_factor];
                                    info.dst.level = 0;
                                    info.dst.box = dst_box;
                                    info.dst.format = templ.format;
                                    info.src.resource = src[size_factor];
                                    info.src.level = 0;
                                    info.src.box = src_box;
                                    info.src.format = templ.format;
                                    info.mask = PIPE_MASK_RGBA;

                                    switch (method) {
                                    case METHOD_DEFAULT:
                                       ctx->blit(ctx, &info);
                                       break;
                                    case METHOD_GFX:
                                       si_gfx_blit(ctx, &info);
                                       break;
                                    case METHOD_COMPUTE:
                                       success &= si_compute_blit(sctx, &info, NULL, 0, 0, false);
                                       break;
                                    case METHOD_SPECIAL:
                                       if (test_flavor == TEST_BLIT && !yflip) {
                                          si_resource_copy_region(ctx, dst[size_factor], 0, dst_box.x,
                                                                  dst_box.y, dst_box.z, src[size_factor],
                                                                  0, &src_box);
                                       } else if (test_flavor == TEST_RESOLVE) {
                                          success &= si_msaa_resolve_blit_via_CB(ctx, &info, false);
                                       } else {
                                          success = false;
                                       }
                                       break;
                                    }
                                    break;
                                 }
                                 }
                              }

                              ctx->end_query(ctx, q);
                              pipe_surface_reference(&dst_surf, NULL);

                              /* Wait for idle after all tests. */
                              sctx->barrier_flags |= SI_BARRIER_SYNC_AND_INV_CB |
                                                     SI_BARRIER_SYNC_CS |
                                                     SI_BARRIER_INV_L2 | SI_BARRIER_INV_SMEM |
                                                     SI_BARRIER_INV_VMEM;
                              si_emit_barrier_direct(sctx);

                              /* Unbind the colorbuffer. */
                              if ((test_flavor == TEST_FB_CLEAR || test_flavor == TEST_CLEAR) &&
                                  box_flavor == BOX_FULL) {
                                 struct pipe_framebuffer_state fb = {0};
                                 fb.width = 64;
                                 fb.height = 64;
                                 fb.layers = 1;
                                 fb.samples = 1;
                                 ctx->set_framebuffer_state(ctx, &fb);
                              }

                              /* Get results. */
                              if (success) {
                                 union pipe_query_result result;
                                 ctx->get_query_result(ctx, q, true, &result);
                                 ctx->destroy_query(ctx, q);

                                 double sec = (double)result.u64 / (1000 * 1000 * 1000);
                                 uint64_t pixels_per_surf = num_repeats * dst_box.width *
                                                            dst_box.height * dst_box.depth;
                                 uint64_t bytes;

                                 if (test_flavor == TEST_FB_CLEAR || test_flavor == TEST_CLEAR)
                                    bytes = pixels_per_surf * pix_size;
                                 else if (test_flavor == TEST_RESOLVE)
                                    bytes = pixels_per_surf * (pix_size + bpe);
                                 else
                                    bytes = pixels_per_surf * pix_size * 2;

                                 double bytes_per_sec = bytes / sec;

                                 printf(" , %9.2f", bytes_per_sec / (1024 * 1024 * 1024));
                              } else {
                                 printf(" ,     n/a  ");
                              }
                           }
                        }

                        printf("\n");
                     }
                  }

                  for (unsigned size_factor = 0; size_factor <= 1; size_factor++) {
                     pipe_resource_reference(&dst[size_factor], NULL);
                     pipe_resource_reference(&src[size_factor], NULL);
                  }
               }
            }
         }
      }
   }

   ctx->destroy(ctx);
   exit(0);
}
