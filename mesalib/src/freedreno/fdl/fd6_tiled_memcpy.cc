/*
 * Copyright (c) 2023 Valve Corporation
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

#include "freedreno_layout.h"

#if DETECT_ARCH_AARCH64
#include <arm_neon.h>
#endif

/* The tiling scheme on Qualcomm consists of four levels:
 *
 * 1. The UBWC block. Normally these use a compressed encoding format with the
 *    compressed size stored in the corresponding metadata byte. However for
 *    uncompressed blocks, or blocks in a texture where UBWC is disabled, the
 *    pixels within the block are stored using a straightforward
 *    coordinate-interleaving scheme:
 *
 *    b7 b6 b5 b4 b3 b2 b1 b0
 *    -----------------------
 *    y2 x4 x3 x2 y1 x1 y0 x0
 *
 *    Pixel contents are always stored linearly, only the pixel offset is
 *    swizzled. UBWC blocks for most formats are smaller than 256 pixels and
 *    only use the first xN and yN, ignoring the higher bits.
 *
 *    There is a special case for single-sampled R8G8 formats, where the 32x8
 *    block is stored as a 32x8 R8 tile where the left half stores the R
 *    components for each pixel and the right half stores the G components.
 *    However non-compressed tiled R8G8 textures are not supported so we
 *    ignore it here.
 *
 * 2. The 256 byte tile. Most UBWC blocks are 256 bytes already, but UBWC
 *    blocks for some smaller formats are only 128 bytes, so 2x1 or 2x2 blocks
 *    are combined to get a 256 byte tile. This can also be thought of as
 *    re-adding bits that were dropped in the coordinate-interleaving scheme
 *    above, and we take advantage of this to fold this level into the
 *    previous one as we don't care about compression.
 *
 * 3. The 2K macrotile. This consists of 2x4 tiles, with a complicated
 *    xor-based bank swizzling scheme. There are two possible modes, chosen by
 *    the "macrotile mode" in RBBM_NC_MODE_CNTL. For formats with cpp of 16 or
 *    greater, both modes are identical and the scheme is this:
 *
 *    b0 = x0 ^ y1
 *    b1 = x0 ^ y1 ^ y0
 *    b2 = x0 ^ y0
 *
 *    For all formats with a cpp less than 16, additional higher-order bits
 *    are xor'ed into the upper 2 offset bits depending on the macrotile mode.
 *    In "4 channel" mode:
 *
 *    b1' = b1 ^ x1
 *
 *    and in "8 channel" mode:
 *
 *    b1' = b1 ^ x1
 *    b2' = b2 ^ x2 ^ y2
 *
 *    The macrotile stride is always a multiple of 2, so that pairs of 2K
 *    macrotiles can be considered 4K macrotiles with one additional offset
 *    bit:
 *
 *    b3 = x1
 *
 *    This is closer to the hardware representation as the base address is
 *    aligned to 4K. However for our purposes this can be folded into
 *    the next level:
 *
 * 4. Swizzled macrotile offset. The macrotile offset is usually linear,
 *    however with strides that are aligned to the number of DDR banks this
 *    can result in bank conflicts between vertically adjacent macrotiles that
 *    map to the same bank. This is mitigated by xor'ing up to 3 bits of the
 *    y offset into x based on how aligned the stride is before computing the
 *    offset, or equivalently xor'ing them into the final offset. The
 *    alignment is based on a value called the "highest bank bit" that is
 *    programmed by the kernel based on the memory bank configuration.
 *
 *    The kernel also chooses which bits of y to xor in, which are called
 *    "bank swizzle levels." The naming is weird, because the lowest level,
 *    level 1, actually involves the highest bit of y:
 *    - "Level 1 bank swizzling" swizzles bit 2 of the macrotile y offset into
 *    the highest bank bit plus 1 when the stride between macrotiles (in
 *    bytes) is a multiple of 2^{hbb + 2} where hbb is the highest bank bit.
 *    - "Level 2 bank swizzling" swizzles bit 0 of the macrotile y offset into
 *    the highest bank bit minus 1 when the stride is a multiple of 2^{hbb}.
 *    - "Level 3 bank swizzling" swizzles bit 1 of the macrotile y offset into
 *    the highest bank bit when the stride is a multiple of 2^{hbb + 1},
 *
 *    Level 1 bank swizzling is only enabled in UBWC 1.0 mode. Levels 2 and 3
 *    can be selectively disabled starting with UBWC 4.0.
 *
 * This implementation uses ideas from
 * https://fgiesen.wordpress.com/2011/01/17/texture-tiling-and-swizzling/.
 * Steps 1 and 2 map straightforwardly to the ideas explained there, but step
 * 3 is very different. Luckily the offset of a block can still be split into
 * a combination of values depending only on x and y, however they may be
 * overlapping and instead of adding them together we have to xor them
 * together.
 *
 * We choose the size of the innermost loop to be the size of a block, which
 * is 256 bytes and therefore larger than strictly necessary, for two reasons:
 * it simplifies the code a bit by not having to keep track of separate block
 * sizes and "inner" block sizes, and in some cases a cacheline-sized inner
 * tile wouldn't be wide enough to use ldp to get the fastest-possible 32 byte
 * load.
 */

#define USE_SLOW_PATH 0

static uint32_t
get_pixel_offset(uint32_t x, uint32_t y)
{
   return
      (x & 1) |
      (y & 1) << 1 |
      ((x & 2) >> 1) << 2 |
      ((y & 2) >> 1) << 3 |
      ((x & 0x1c) >> 2) << 4 |
      ((y & 4) >> 2) << 7;
}

/* Take the x and y block coordinates and return two masks which when combined
 * give us the block offset in bytes.  This includes the block offset within a
 * macrotile and the macrotile x offset, which is swizzled based on the
 * highest bank bit and enabled levels, but not the macrotile y offset which
 * has to be added separately.
 *
 * This partially depends on the macrotile mode and block_x_xormask is called
 * in the hot path, so we have to templatize it.
 */

template<enum fdl_macrotile_mode macrotile_mode>
static uint32_t
block_x_xormask(uint32_t x, uint32_t cpp);

template<>
uint32_t
block_x_xormask<FDL_MACROTILE_4_CHANNEL>(uint32_t x, uint32_t cpp)
{
   return (((x & 1) * 0b111) ^ (cpp < 16 ? (x & 0b010) : 0) ^ ((x >> 1) << 3)) << 8;
}

template<>
uint32_t
block_x_xormask<FDL_MACROTILE_8_CHANNEL>(uint32_t x, uint32_t cpp)
{
   return (((x & 1) * 0b111) ^ (cpp < 16 ? (x & 0b110) : 0) ^ ((x >> 1) << 3)) << 8;
}

template<enum fdl_macrotile_mode macrotile_mode>
static uint32_t
block_y_xormask(uint32_t y, uint32_t cpp, uint32_t bank_mask, uint32_t bank_shift);

template<>
uint32_t
block_y_xormask<FDL_MACROTILE_4_CHANNEL>(uint32_t y, uint32_t cpp,
                                         uint32_t bank_mask, 
                                         uint32_t bank_shift)
{
   return ((((y & 1) * 0b110) ^ (((y >> 1) & 1) * 0b011)) << 8) |
      ((y & bank_mask) << bank_shift);
}

template<>
uint32_t
block_y_xormask<FDL_MACROTILE_8_CHANNEL>(uint32_t y, uint32_t cpp,
                                         uint32_t bank_mask, 
                                         uint32_t bank_shift)
{
   return ((((y & 1) * 0b110) ^ (((y >> 1) & 1) * 0b011) ^
            (cpp < 16 ? (y & 0b100) : 0)) << 8) |
      ((y & bank_mask) << bank_shift);
}

/* Figure out how y is swizzled into x based on the UBWC config and block
 * stride and return values to be plugged into block_y_xormask().
 */

static uint32_t
get_bank_mask(uint32_t block_stride, uint32_t cpp,
              const struct fdl_ubwc_config *config)
{
   /* For some reason, for cpp=1 (or R8G8 media formats) the alignment
    * required is doubled.
    */
   unsigned offset = cpp == 1 ? 1 : 0;
   uint32_t mask = 0;
   if ((config->bank_swizzle_levels & 0x2) &&
       (block_stride & ((1u << (config->highest_bank_bit - 10 + offset)) - 1)) == 0)
      mask |= 0b100;
   if ((config->bank_swizzle_levels & 0x4) &&
       (block_stride & ((1u << (config->highest_bank_bit - 9 + offset)) - 1)) == 0)
      mask |= 0b1000;
   if ((config->bank_swizzle_levels & 0x1) &&
       (block_stride & ((1u << (config->highest_bank_bit - 8 + offset)) - 1)) == 0)
      mask |= 0b10000;
   return mask;
}

static uint32_t
get_bank_shift(const struct fdl_ubwc_config *config)
{
   return config->highest_bank_bit - 3;
}

#if USE_SLOW_PATH
static uint32_t
get_block_offset(uint32_t x, uint32_t y, unsigned block_stride, unsigned cpp,
                 const struct fdl_ubwc_config *config)
{
   uint32_t bank_mask = get_bank_mask(block_stride, cpp, config);
   unsigned bank_shift = get_bank_shift(config);
   uint32_t x_mask, y_mask;
   if (config->macrotile_mode == FDL_MACROTILE_4_CHANNEL) {
      x_mask = block_x_xormask<FDL_MACROTILE_4_CHANNEL>(x, cpp);
      y_mask = block_y_xormask<FDL_MACROTILE_4_CHANNEL>(y, cpp, bank_mask,
                                                        bank_shift);
   } else {
      x_mask = block_x_xormask<FDL_MACROTILE_8_CHANNEL>(x, cpp);
      y_mask = block_y_xormask<FDL_MACROTILE_8_CHANNEL>(y, cpp, bank_mask,
                                                        bank_shift);
   }
   uint32_t macrotile_y = y >> 2;
   uint32_t macrotile_stride = block_stride / 2;
   return ((x_mask ^ y_mask) >> 8) + ((macrotile_y * macrotile_stride) << 3);
}
#endif

static void
get_block_size(unsigned cpp, uint32_t *block_width,
               uint32_t *block_height)
{
   switch (cpp) {
   case 1:
      *block_width = 32;
      *block_height = 8;
      break;
   case 2:
      *block_width = 32;
      *block_height = 4;
      break;
   case 4:
      *block_width = 16;
      *block_height = 4;
      break;
   case 8:
      *block_width = 8;
      *block_height = 4;
      break;
   case 16:
      *block_width = 4;
      *block_height = 4;
      break;
   default:
      unreachable("unknown cpp");
   }
}

enum copy_dir {
   LINEAR_TO_TILED,
   TILED_TO_LINEAR,
};

template<unsigned cpp, enum copy_dir direction,
   enum fdl_macrotile_mode macrotile_mode>
static void
memcpy_small(uint32_t x_start, uint32_t y_start,
             uint32_t width, uint32_t height,
             char *tiled, char *linear,
             uint32_t linear_pitch, uint32_t block_stride,
             const struct fdl_ubwc_config *config)
{
   unsigned block_width, block_height;
   get_block_size(cpp, &block_width, &block_height);
   const uint32_t block_size = 256;

   uint32_t bank_mask = get_bank_mask(block_stride, cpp, config);
   uint32_t bank_shift = get_bank_shift(config);
   uint32_t x_mask = (get_pixel_offset(~0u, 0)) & (block_size / cpp - 1);
   uint32_t y_mask = (get_pixel_offset(0, ~0u)) & (block_size / cpp - 1);

   /* The pitch between vertically adjacent 2K macrotiles. */
   uint32_t macrotile_pitch = (block_stride / 2) * 2048;

   uint32_t x_block_start = x_start / block_width;
   uint32_t y_block_start = y_start / block_height;

   tiled += (y_block_start >> 2) * macrotile_pitch;

   uint32_t x_pixel_start = get_pixel_offset(x_start % block_width, 0);
   uint32_t y_pixel_start = get_pixel_offset(0, y_start % block_height);

   uint32_t y_block = y_block_start;
   uint32_t y_pixel = y_pixel_start;
   uint32_t y_xormask =
      block_y_xormask<macrotile_mode>(y_block, cpp, bank_mask, bank_shift);
   for (uint32_t y = 0; y < height; y++) {
      uint32_t x_block = x_block_start;
      uint32_t x_pixel = x_pixel_start;
      uint32_t block_offset =
         block_x_xormask<macrotile_mode>(x_block, cpp) ^ y_xormask;

      char *tiled_line = tiled + y_pixel * cpp;
      char *linear_pixel = linear;

      for (uint32_t x = 0; x < width; x++) {
         char *tiled_pixel = tiled_line + x_pixel * cpp + block_offset;

         if (direction == LINEAR_TO_TILED)
            memcpy(tiled_pixel, linear_pixel, cpp);
         else
            memcpy(linear_pixel, tiled_pixel, cpp);

         x_pixel = (x_pixel - x_mask) & x_mask;
         linear_pixel += cpp;

         if (x_pixel == 0) {
            x_block++;
            block_offset =
               block_x_xormask<macrotile_mode>(x_block, cpp) ^ y_xormask;
         }
      }

      y_pixel = (y_pixel - y_mask) & y_mask;
      if (y_pixel == 0) {
         y_block++;
         y_xormask =
            block_y_xormask<macrotile_mode>(y_block, cpp, bank_mask, bank_shift);
         if ((y_block & 3) == 0) {
            tiled += macrotile_pitch;
         }
      }

      linear += linear_pitch;
   }
}

typedef void (*copy_fn)(char *tiled, char *linear, uint32_t linear_pitch);

typedef uint8_t pixel8_t __attribute__((vector_size(8), aligned(8)));
typedef uint8_t pixel8a1_t __attribute__((vector_size(8), aligned(1)));
typedef uint8_t pixel16_t __attribute__((vector_size(16), aligned(16)));
typedef uint8_t pixel16a1_t __attribute__((vector_size(16), aligned(1)));

/* We use memcpy_small as a fallback for copying a tile when there isn't
 * optimized assembly, which requires a config, but because we're just copying
 * a tile it doesn't matter which config we pass. Just pass an arbitrary valid
 * config.
 */
static const struct fdl_ubwc_config dummy_config = {
   .highest_bank_bit = 13,
};

/* We use handwritten assembly for the smaller cpp's because gcc is too dumb
 * to register allocate the vector registers without inserting extra moves,
 * and it can't use the post-increment register mode so it emits too many add
 * instructions. This means a ~10% performance regression compared to the
 * hand-written assembly in the cpp=4 case.
 */

static void
linear_to_tiled_1cpp(char *_tiled, char *_linear, uint32_t linear_pitch)
{
#if DETECT_ARCH_AARCH64
   uint32_t *tiled = (uint32_t *)_tiled;
   for (unsigned y = 0; y < 2; y++, _linear += 4 * linear_pitch) {
      uint16x8_t *linear0 = (uint16x8_t *)_linear;
      uint16x8_t *linear1 = (uint16x8_t *)(_linear + linear_pitch);
      uint16x8_t *linear2 = (uint16x8_t *)(_linear + 2 * linear_pitch);
      uint16x8_t *linear3 = (uint16x8_t *)(_linear + 3 * linear_pitch);
      asm volatile(
          "ldp q0, q4, [%2]\n"
          "ldp q1, q5, [%3]\n"
          "ldp q2, q6, [%4]\n"
          "ldp q3, q7, [%5]\n"
          "zip1 v8.8h, v0.8h, v1.8h\n"
          "zip1 v9.8h, v2.8h, v3.8h\n"
          "zip2 v10.8h, v0.8h, v1.8h\n"
          "zip2 v11.8h, v2.8h, v3.8h\n"
          "zip1 v12.8h, v4.8h, v5.8h\n"
          "zip1 v13.8h, v6.8h, v7.8h\n"
          "zip2 v14.8h, v4.8h, v5.8h\n"
          "zip2 v15.8h, v6.8h, v7.8h\n"
          "st2 {v8.2d, v9.2d}, [%0], #32\n"
          "st2 {v10.2d, v11.2d}, [%0], #32\n"
          "st2 {v12.2d, v13.2d}, [%0], #32\n"
          "st2 {v14.2d, v15.2d}, [%0], #32\n"
          : "=r"(tiled)
          : "0"(tiled), "r"(linear0), "r"(linear1), "r"(linear2), "r"(linear3)
          : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
            "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15");
   }
#else
   memcpy_small<1, LINEAR_TO_TILED, FDL_MACROTILE_4_CHANNEL>(
      0, 0, 32, 8, _tiled, _linear, linear_pitch, 0, &dummy_config);
#endif
}

static void
tiled_to_linear_1cpp(char *_tiled, char *_linear, uint32_t linear_pitch)
{
#if DETECT_ARCH_AARCH64
   uint32_t *tiled = (uint32_t *)_tiled;
   for (unsigned y = 0; y < 2; y++, _linear += 4 * linear_pitch) {
      uint16x8_t *linear0 = (uint16x8_t *)_linear;
      uint16x8_t *linear1 = (uint16x8_t *)(_linear + linear_pitch);
      uint16x8_t *linear2 = (uint16x8_t *)(_linear + 2 * linear_pitch);
      uint16x8_t *linear3 = (uint16x8_t *)(_linear + 3 * linear_pitch);
      asm volatile(
          "ld2 {v8.2d, v9.2d}, [%0], #32\n"
          "ld2 {v10.2d, v11.2d}, [%0], #32\n"
          "ld2 {v12.2d, v13.2d}, [%0], #32\n"
          "ld2 {v14.2d, v15.2d}, [%0], #32\n"
          "uzp1 v0.8h, v8.8h, v10.8h\n"
          "uzp2 v1.8h, v8.8h, v10.8h\n"
          "uzp1 v2.8h, v9.8h, v11.8h\n"
          "uzp2 v3.8h, v9.8h, v11.8h\n"
          "uzp1 v4.8h, v12.8h, v14.8h\n"
          "uzp2 v5.8h, v12.8h, v14.8h\n"
          "uzp1 v6.8h, v13.8h, v15.8h\n"
          "uzp2 v7.8h, v13.8h, v15.8h\n"
          "stp q0, q4, [%2]\n"
          "stp q1, q5, [%3]\n"
          "stp q2, q6, [%4]\n"
          "stp q3, q7, [%5]\n"
          : "=r"(tiled)
          : "0"(tiled), "r"(linear0), "r"(linear1), "r"(linear2), "r"(linear3)
          : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
            "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15");
   }
#else
   memcpy_small<1, TILED_TO_LINEAR, FDL_MACROTILE_4_CHANNEL>(
      0, 0, 32, 8, _tiled, _linear, linear_pitch, 0, &dummy_config);
#endif
}

static void
linear_to_tiled_2cpp(char *_tiled, char *_linear, uint32_t linear_pitch)
{
#if DETECT_ARCH_AARCH64
   uint32_t *tiled = (uint32_t *)_tiled;
   for (unsigned x = 0; x < 2; x++, _linear += 32) {
      uint32x4_t *linear0 = (uint32x4_t *)_linear;
      uint32x4_t *linear1 = (uint32x4_t *)(_linear + linear_pitch);
      uint32x4_t *linear2 = (uint32x4_t *)(_linear + 2 * linear_pitch);
      uint32x4_t *linear3 = (uint32x4_t *)(_linear + 3 * linear_pitch);
      asm volatile(
          "ldp q0, q4, [%2]\n"
          "ldp q1, q5, [%3]\n"
          "ldp q2, q6, [%4]\n"
          "ldp q3, q7, [%5]\n"
          "zip1 v8.4s, v0.4s, v1.4s\n"
          "zip1 v9.4s, v2.4s, v3.4s\n"
          "zip2 v10.4s, v0.4s, v1.4s\n"
          "zip2 v11.4s, v2.4s, v3.4s\n"
          "zip1 v12.4s, v4.4s, v5.4s\n"
          "zip1 v13.4s, v6.4s, v7.4s\n"
          "zip2 v14.4s, v4.4s, v5.4s\n"
          "zip2 v15.4s, v6.4s, v7.4s\n"
          "stp q8, q9, [%0], #32\n"
          "stp q10, q11, [%0], #32\n"
          "stp q12, q13, [%0], #32\n"
          "stp q14, q15, [%0], #32\n"
          : "=r"(tiled)
          : "0"(tiled), "r"(linear0), "r"(linear1), "r"(linear2), "r"(linear3)
          : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
            "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15");
   }
#else
   memcpy_small<2, LINEAR_TO_TILED, FDL_MACROTILE_4_CHANNEL>(
      0, 0, 32, 4, _tiled, _linear, linear_pitch, 0, &dummy_config);
#endif
}

static void
tiled_to_linear_2cpp(char *_tiled, char *_linear, uint32_t linear_pitch)
{
#if DETECT_ARCH_AARCH64
   uint32_t *tiled = (uint32_t *)_tiled;
   for (unsigned x = 0; x < 2; x++, _linear += 32) {
      uint32x4_t *linear0 = (uint32x4_t *)_linear;
      uint32x4_t *linear1 = (uint32x4_t *)(_linear + linear_pitch);
      uint32x4_t *linear2 = (uint32x4_t *)(_linear + 2 * linear_pitch);
      uint32x4_t *linear3 = (uint32x4_t *)(_linear + 3 * linear_pitch);
      asm volatile(
          "ldp q8, q9, [%0], #32\n"
          "ldp q10, q11, [%0], #32\n"
          "ldp q12, q13, [%0], #32\n"
          "ldp q14, q15, [%0], #32\n"
          "uzp1 v0.4s, v8.4s, v10.4s\n"
          "uzp2 v1.4s, v8.4s, v10.4s\n"
          "uzp1 v2.4s, v9.4s, v11.4s\n"
          "uzp2 v3.4s, v9.4s, v11.4s\n"
          "uzp1 v4.4s, v12.4s, v14.4s\n"
          "uzp2 v5.4s, v12.4s, v14.4s\n"
          "uzp1 v6.4s, v13.4s, v15.4s\n"
          "uzp2 v7.4s, v13.4s, v15.4s\n"
          "stp q0, q4, [%2]\n"
          "stp q1, q5, [%3]\n"
          "stp q2, q6, [%4]\n"
          "stp q3, q7, [%5]\n"
          : "=r"(tiled)
          : "0"(tiled), "r"(linear0), "r"(linear1), "r"(linear2), "r"(linear3)
          : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
            "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15");
   }
#else
   memcpy_small<2, LINEAR_TO_TILED, FDL_MACROTILE_4_CHANNEL>(
      0, 0, 32, 4, _tiled, _linear, linear_pitch, 0, &dummy_config);
#endif
}

static void
linear_to_tiled_4cpp(char *_tiled, char *_linear, uint32_t linear_pitch)
{
#if DETECT_ARCH_AARCH64
   uint64_t *tiled = (uint64_t *)_tiled;
   uint64x2_t *linear0 = (uint64x2_t *)_linear;
   uint64x2_t *linear1 = (uint64x2_t *)(_linear + linear_pitch);
   uint64x2_t *linear2 = (uint64x2_t *)(_linear + 2 * linear_pitch);
   uint64x2_t *linear3 = (uint64x2_t *)(_linear + 3 * linear_pitch);

   asm volatile(
       "ldp q0, q4, [%2]\n"
       "ldp q1, q5, [%3]\n"
       "ldp q2, q6, [%4]\n"
       "ldp q3, q7, [%5]\n"
       "ldp q8, q12, [%2, #32]\n"
       "ldp q9, q13, [%3, #32]\n"
       "ldp q10, q14, [%4, #32]\n"
       "ldp q11, q15, [%5, #32]\n"
       "st2 {v0.2d, v1.2d}, [%0], #32\n"
       "st2 {v2.2d, v3.2d}, [%0], #32\n"
       "st2 {v4.2d, v5.2d}, [%0], #32\n"
       "st2 {v6.2d, v7.2d}, [%0], #32\n"
       "st2 {v8.2d, v9.2d}, [%0], #32\n"
       "st2 {v10.2d, v11.2d}, [%0], #32\n"
       "st2 {v12.2d, v13.2d}, [%0], #32\n"
       "st2 {v14.2d, v15.2d}, [%0], #32\n"
       : "=r"(tiled)
       : "0"(tiled), "r"(linear0), "r"(linear1), "r"(linear2), "r"(linear3)
       : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
         "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15");
#else
   pixel8_t *tiled = (pixel8_t *)_tiled;
   for (unsigned x = 0; x < 4; x++, _linear += 4 * 4, tiled += 8) {
      pixel8a1_t *linear0 = (pixel8a1_t *)_linear;
      pixel8a1_t *linear1 = (pixel8a1_t *)(_linear + linear_pitch);
      pixel8a1_t *linear2 = (pixel8a1_t *)(_linear + 2 * linear_pitch);
      pixel8a1_t *linear3 = (pixel8a1_t *)(_linear + 3 * linear_pitch);
      pixel8_t p000 = linear0[0];
      pixel8_t p100 = linear0[1];
      pixel8_t p001 = linear1[0];
      pixel8_t p101 = linear1[1];
      pixel8_t p010 = linear2[0];
      pixel8_t p110 = linear2[1];
      pixel8_t p011 = linear3[0];
      pixel8_t p111 = linear3[1];
      tiled[0] = p000;
      tiled[1] = p001;
      tiled[2] = p100;
      tiled[3] = p101;
      tiled[4] = p010;
      tiled[5] = p011;
      tiled[6] = p110;
      tiled[7] = p111;
   }
#endif
}

static void
tiled_to_linear_4cpp(char *_tiled, char *_linear, uint32_t linear_pitch)
{
#if DETECT_ARCH_AARCH64
   uint64_t *tiled = (uint64_t *)_tiled;
   uint64x2_t *linear0 = (uint64x2_t *)_linear;
   uint64x2_t *linear1 = (uint64x2_t *)(_linear + linear_pitch);
   uint64x2_t *linear2 = (uint64x2_t *)(_linear + 2 * linear_pitch);
   uint64x2_t *linear3 = (uint64x2_t *)(_linear + 3 * linear_pitch);

   asm volatile(
       "ld2 {v0.2d, v1.2d}, [%0], #32\n"
       "ld2 {v2.2d, v3.2d}, [%0], #32\n"
       "ld2 {v4.2d, v5.2d}, [%0], #32\n"
       "ld2 {v6.2d, v7.2d}, [%0], #32\n"
       "ld2 {v8.2d, v9.2d}, [%0], #32\n"
       "ld2 {v10.2d, v11.2d}, [%0], #32\n"
       "ld2 {v12.2d, v13.2d}, [%0], #32\n"
       "ld2 {v14.2d, v15.2d}, [%0], #32\n"
       "stp q0, q4, [%2]\n"
       "stp q1, q5, [%3]\n"
       "stp q2, q6, [%4]\n"
       "stp q3, q7, [%5]\n"
       "stp q8, q12, [%2, #32]\n"
       "stp q9, q13, [%3, #32]\n"
       "stp q10, q14, [%4, #32]\n"
       "stp q11, q15, [%5, #32]\n"
       : "=r"(tiled)
       : "0"(tiled), "r"(linear0), "r"(linear1), "r"(linear2), "r"(linear3)
       : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
         "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15");
#else
   pixel8_t *tiled = (pixel8_t *)_tiled;
   for (unsigned x = 0; x < 4; x++, _linear += 4 * 4, tiled += 8) {
      pixel8a1_t *linear0 = (pixel8a1_t *)_linear;
      pixel8a1_t *linear1 = (pixel8a1_t *)(_linear + linear_pitch);
      pixel8a1_t *linear2 = (pixel8a1_t *)(_linear + 2 * linear_pitch);
      pixel8a1_t *linear3 = (pixel8a1_t *)(_linear + 3 * linear_pitch);
      pixel8_t p000 = tiled[0];
      pixel8_t p001 = tiled[1];
      pixel8_t p100 = tiled[2];
      pixel8_t p101 = tiled[3];
      pixel8_t p010 = tiled[4];
      pixel8_t p011 = tiled[5];
      pixel8_t p110 = tiled[6];
      pixel8_t p111 = tiled[7];
      linear0[0] = p000;
      linear0[1] = p100;
      linear1[0] = p001;
      linear1[1] = p101;
      linear2[0] = p010;
      linear2[1] = p110;
      linear3[0] = p011;
      linear3[1] = p111;
   }
#endif
}

static void
linear_to_tiled_8cpp(char *_tiled, char *_linear, uint32_t linear_pitch)
{
   pixel16_t *tiled = (pixel16_t *)_tiled;

   for (unsigned x = 0; x < 2; x++, _linear += 4 * 8) {
      for (unsigned y = 0; y < 2; y++, tiled += 4) {
         pixel16a1_t *linear0 = (pixel16a1_t *)(_linear + 2 * y * linear_pitch);
         pixel16a1_t *linear1 = (pixel16a1_t *)(_linear + (2 * y + 1) * linear_pitch);
         pixel16_t p00 = linear0[0];
         pixel16_t p10 = linear0[1];
         pixel16_t p01 = linear1[0];
         pixel16_t p11 = linear1[1];
         tiled[0] = p00;
         tiled[1] = p01;
         tiled[2] = p10;
         tiled[3] = p11;
      }
   }
}

static void
tiled_to_linear_8cpp(char *_tiled, char *_linear, uint32_t linear_pitch)
{
   pixel16_t *tiled = (pixel16_t *)_tiled;

   for (unsigned x = 0; x < 2; x++, _linear += 4 * 8) {
      for (unsigned y = 0; y < 2; y++, tiled += 4) {
         pixel16a1_t *linear0 = (pixel16a1_t *)(_linear + 2 * y * linear_pitch);
         pixel16a1_t *linear1 = (pixel16a1_t *)(_linear + (2 * y + 1) * linear_pitch);
         pixel16_t p00 = tiled[0];
         pixel16_t p01 = tiled[1];
         pixel16_t p10 = tiled[2];
         pixel16_t p11 = tiled[3];
         linear0[0] = p00;
         linear0[1] = p10;
         linear1[0] = p01;
         linear1[1] = p11;
      }
   }
}

static void
linear_to_tiled_16cpp(char *_tiled, char *_linear, uint32_t linear_pitch)
{
   pixel16_t *tiled = (pixel16_t *)_tiled;

   for (unsigned y = 0; y < 2; y++, _linear += 2 * linear_pitch) {
      for (unsigned x = 0; x < 2; x++, tiled += 4) {
         pixel16a1_t *linear0 = (pixel16a1_t *)(_linear + 2 * 16 * x);
         pixel16a1_t *linear1 = (pixel16a1_t *)(_linear + linear_pitch + 2 * 16 * x);
         pixel16_t p00 = linear0[0];
         pixel16_t p10 = linear0[1];
         pixel16_t p01 = linear1[0];
         pixel16_t p11 = linear1[1];
         tiled[0] = p00;
         tiled[1] = p10;
         tiled[2] = p01;
         tiled[3] = p11;
      }
   }
}

static void
tiled_to_linear_16cpp(char *_tiled, char *_linear, uint32_t linear_pitch)
{
   pixel16_t *tiled = (pixel16_t *)_tiled;

   for (unsigned y = 0; y < 2; y++, _linear += 2 * linear_pitch) {
      for (unsigned x = 0; x < 2; x++, tiled += 4) {
         pixel16a1_t *linear0 = (pixel16a1_t *)(_linear + 2 * 16 * x);
         pixel16a1_t *linear1 = (pixel16a1_t *)(_linear + linear_pitch + 2 * 16 * x);
         pixel16_t p00 = tiled[0];
         pixel16_t p10 = tiled[1];
         pixel16_t p01 = tiled[2];
         pixel16_t p11 = tiled[3];
         linear0[0] = p00;
         linear0[1] = p10;
         linear1[0] = p01;
         linear1[1] = p11;
      }
   }
}

template<unsigned cpp, enum copy_dir direction, copy_fn copy_block,
   enum fdl_macrotile_mode macrotile_mode>
static void
memcpy_large(uint32_t x_start, uint32_t y_start,
             uint32_t width, uint32_t height,
             char *tiled, char *linear,
             uint32_t linear_pitch, uint32_t block_stride,
             const fdl_ubwc_config *config)
{
   unsigned block_width, block_height;
   get_block_size(cpp, &block_width, &block_height);

   /* The region to copy is divided into 9 parts:
    *
    *              x_start x_aligned_start    x_aligned_end x_end
    *
    *          y_start /--------------------------------------\
    *                  |         |                  |         |
    *  y_aligned_start |--------------------------------------|
    *                  |         |                  |         |
    *                  |         |   aligned area   |         |
    *                  |         |                  |         |
    *    y_aligned_end |--------------------------------------|
    *                  |         |                  |         |
    *            y_end \--------------------------------------/
    *
    * The aligned area consists of aligned blocks that we can use our
    * optimized copy function on, but the rest consists of misaligned pieces
    * of blocks.
    */

   uint32_t x_end = x_start + width;
   uint32_t x_aligned_start = align(x_start, block_width);
   uint32_t x_aligned_end = ROUND_DOWN_TO(x_end, block_width);

   uint32_t y_end = y_start + height;
   uint32_t y_aligned_start = align(y_start, block_height);
   uint32_t y_aligned_end = ROUND_DOWN_TO(y_end, block_height);

   /* If we don't cover any full tiles, use the small loop */
   if (x_aligned_end <= x_aligned_start || y_aligned_end <= y_aligned_start) {
      memcpy_small<cpp, direction, macrotile_mode>(
         x_start, y_start, width, height, tiled, linear, linear_pitch,
         block_stride, config);
      return;
   }

   /* Handle the top third */
   if (y_start != y_aligned_start) {
      memcpy_small<cpp, direction, macrotile_mode>(
         x_start, y_start, width, y_aligned_start - y_start, tiled, linear,
         linear_pitch, block_stride, config);
      linear += (y_aligned_start - y_start) * linear_pitch;
   }

   /* Handle left of the aligned block */
   char *linear_aligned = linear;
   if (x_start != x_aligned_start) {
      memcpy_small<cpp, direction, macrotile_mode>(
         x_start, y_aligned_start, x_aligned_start - x_start,
         y_aligned_end - y_aligned_start, tiled, linear, linear_pitch,
         block_stride, config);
      linear_aligned = linear + (x_aligned_start - x_start) * cpp;
   }

   /* Handle the main part */
   uint32_t macrotile_pitch = (block_stride / 2) * 2048;
   uint32_t bank_mask = get_bank_mask(block_stride, cpp, config);
   uint32_t bank_shift = get_bank_shift(config);
   char *tiled_aligned =
      tiled + macrotile_pitch * (y_aligned_start / (block_height * 4));

   for (unsigned y_block = y_aligned_start / block_height;
        y_block < y_aligned_end / block_height;) {
      uint32_t y_xormask =
         block_y_xormask<macrotile_mode>(y_block, cpp, bank_mask, bank_shift);
      char *linear_block = linear_aligned;

      for (unsigned x_block = x_aligned_start / block_width;
           x_block < x_aligned_end / block_width; x_block++) {
         uint32_t block_offset =
            block_x_xormask<macrotile_mode>(x_block, cpp) ^ y_xormask;
         copy_block(tiled_aligned + block_offset, linear_block, linear_pitch);
         linear_block += block_width * cpp;
      }

      linear_aligned += block_height * linear_pitch;

      y_block++;
      if ((y_block & 3) == 0)
         tiled_aligned += macrotile_pitch;
   }

   /* Handle right of the aligned block */
   if (x_end != x_aligned_end) {
      char *linear_end =
         linear + (x_aligned_end - x_start) * cpp;
      memcpy_small<cpp, direction, macrotile_mode>(
         x_aligned_end, y_aligned_start, x_end - x_aligned_end,
         y_aligned_end - y_aligned_start, tiled, linear_end, linear_pitch,
         block_stride, config);
   }

   /* Handle the bottom third */
   linear += (y_aligned_end - y_aligned_start) * linear_pitch;
   if (y_end != y_aligned_end) {
      memcpy_small<cpp, direction, macrotile_mode>(
         x_start, y_aligned_end, width, y_end - y_aligned_end,
         tiled, linear, linear_pitch, block_stride,
         config);
   }
}

void
fdl6_memcpy_linear_to_tiled(uint32_t x_start, uint32_t y_start,
                            uint32_t width, uint32_t height,
                            char *dst, const char *src,
                            const struct fdl_layout *dst_layout,
                            unsigned dst_miplevel,
                            uint32_t src_pitch,
                            const struct fdl_ubwc_config *config)
{
   unsigned block_width, block_height;
   uint32_t cpp = dst_layout->cpp;
   get_block_size(cpp, &block_width, &block_height);
   uint32_t block_stride =
      fdl_pitch(dst_layout, dst_miplevel) / (block_width * dst_layout->cpp);
   uint32_t block_size = 256;
   assert(block_size == block_width * block_height * dst_layout->cpp);
   assert(config->macrotile_mode != FDL_MACROTILE_INVALID);

#if USE_SLOW_PATH
   for (uint32_t y = 0; y < height; y++) {
      uint32_t y_block = (y + y_start) / block_height;
      uint32_t y_pixel = (y + y_start) % block_height;
      for (uint32_t x = 0; x < width; x++) {
         uint32_t x_block = (x + x_start) / block_width;
         uint32_t x_pixel = (x + x_start) % block_width;

         uint32_t block_offset = 
            get_block_offset(x_block, y_block, block_stride, cpp,
                             config);
         uint32_t pixel_offset = get_pixel_offset(x_pixel, y_pixel);

         memcpy(dst + block_size * block_offset + cpp * pixel_offset,
                src + y * src_pitch + x * cpp, cpp);
      }
   }
#else
   switch (cpp) {
#define CASE(case_cpp)                                                        \
   case case_cpp:                                                             \
      if (config->macrotile_mode == FDL_MACROTILE_4_CHANNEL) {                \
         memcpy_large<case_cpp, LINEAR_TO_TILED,                              \
            linear_to_tiled_##case_cpp##cpp, FDL_MACROTILE_4_CHANNEL>(        \
            x_start, y_start, width, height, dst, (char *)src, src_pitch,     \
            block_stride, config);                                            \
      } else {                                                                \
         memcpy_large<case_cpp, LINEAR_TO_TILED,                              \
            linear_to_tiled_##case_cpp##cpp,  FDL_MACROTILE_8_CHANNEL>(       \
            x_start, y_start, width, height, dst, (char *)src, src_pitch,     \
            block_stride, config);                                            \
      }                                                                       \
      break;
   CASE(1)
   CASE(2)
   CASE(4)
   CASE(8)
   CASE(16)
#undef CASE
   default:
      unreachable("unknown cpp");
   }
#endif
}

void
fdl6_memcpy_tiled_to_linear(uint32_t x_start, uint32_t y_start,
                            uint32_t width, uint32_t height,
                            char *dst, const char *src,
                            const struct fdl_layout *src_layout,
                            unsigned src_miplevel,
                            uint32_t dst_pitch,
                            const struct fdl_ubwc_config *config)
{
   unsigned block_width, block_height;
   unsigned cpp = src_layout->cpp;
   get_block_size(cpp, &block_width, &block_height);
   uint32_t block_stride =
      fdl_pitch(src_layout, src_miplevel) / (block_width * src_layout->cpp);
   uint32_t block_size = 256;
   assert(block_size == block_width * block_height * src_layout->cpp);
   assert(config->macrotile_mode != FDL_MACROTILE_INVALID);

#if USE_SLOW_PATH
   for (uint32_t y = 0; y < height; y++) {
      uint32_t y_block = (y + y_start) / block_height;
      uint32_t y_pixel = (y + y_start) % block_height;
      for (uint32_t x = 0; x < width; x++) {
         uint32_t x_block = (x + x_start) / block_width;
         uint32_t x_pixel = (x + x_start) % block_width;

	 uint32_t block_offset =
            get_block_offset(x_block, y_block, block_stride, src_layout->cpp,
                             config);
	 uint32_t pixel_offset = get_pixel_offset(x_pixel, y_pixel);

         memcpy(dst + y * dst_pitch + x * src_layout->cpp,
                src + block_size * block_offset + src_layout->cpp * pixel_offset,
                src_layout->cpp);
      }
   }
#else
   switch (cpp) {
#define CASE(case_cpp)                                                        \
   case case_cpp:                                                             \
      if (config->macrotile_mode == FDL_MACROTILE_4_CHANNEL) {                \
         memcpy_large<case_cpp, TILED_TO_LINEAR,                              \
            tiled_to_linear_##case_cpp##cpp, FDL_MACROTILE_4_CHANNEL>(        \
            x_start, y_start, width, height, (char *)src, dst, dst_pitch,     \
            block_stride, config);                                            \
      } else {                                                                \
         memcpy_large<case_cpp, TILED_TO_LINEAR,                              \
            tiled_to_linear_##case_cpp##cpp, FDL_MACROTILE_8_CHANNEL>(        \
            x_start, y_start, width, height, (char *)src, dst, dst_pitch,     \
            block_stride, config);                                            \
      }                                                                       \
      break;
   CASE(1)
   CASE(2)
   CASE(4)
   CASE(8)
   CASE(16)
#undef CASE
   default:
      unreachable("unknown cpp");
   }
#endif
}
