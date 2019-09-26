/*
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
 *
 * Authors:
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "util/u_math.h"
#include "util/macros.h"
#include "pan_encoder.h"

/* Mali GPUs are tiled-mode renderers, rather than immediate-mode.
 * Conceptually, the screen is divided into 16x16 tiles. Vertex shaders run.
 * Then, a fixed-function hardware block (the tiler) consumes the gl_Position
 * results. For each triangle specified, it marks each containing tile as
 * containing that triangle. This set of "triangles per tile" form the "polygon
 * list". Finally, the rasterization unit consumes the polygon list to invoke
 * the fragment shader.
 *
 * In practice, it's a bit more complicated than this. 16x16 is the logical
 * tile size, but Midgard features "hierarchical tiling", where power-of-two
 * multiples of the base tile size can be used: hierarchy level 0 (16x16),
 * level 1 (32x32), level 2 (64x64), per public information about Midgard's
 * tiling. In fact, tiling goes up to 4096x4096 (!), although in practice
 * 128x128 is the largest usually used (though higher modes are enabled).  The
 * idea behind hierarchical tiling is to use low tiling levels for small
 * triangles and high levels for large triangles, to minimize memory bandwidth
 * and repeated fragment shader invocations (the former issue inherent to
 * immediate-mode rendering and the latter common in traditional tilers).
 *
 * The tiler itself works by reading varyings in and writing a polygon list
 * out. Unfortunately (for us), both of these buffers are managed in main
 * memory; although they ideally will be cached, it is the drivers'
 * responsibility to allocate these buffers. Varying buffer allocation is
 * handled elsewhere, as it is not tiler specific; the real issue is allocating
 * the polygon list.
 *
 * This is hard, because from the driver's perspective, we have no information
 * about what geometry will actually look like on screen; that information is
 * only gained from running the vertex shader. (Theoretically, we could run the
 * vertex shaders in software as a prepass, or in hardware with transform
 * feedback as a prepass, but either idea is ludicrous on so many levels).
 *
 * Instead, Mali uses a bit of a hybrid approach, splitting the polygon list
 * into three distinct pieces. First, the driver statically determines which
 * tile hierarchy levels to use (more on that later). At this point, we know the
 * framebuffer dimensions and all the possible tilings of the framebuffer, so
 * we know exactly how many tiles exist across all hierarchy levels. The first
 * piece of the polygon list is the header, which is exactly 8 bytes per tile,
 * plus padding and a small 64-byte prologue. (If that doesn't remind you of
 * AFBC, it should. See pan_afbc.c for some fun parallels). The next part is
 * the polygon list body, which seems to contain 512 bytes per tile, again
 * across every level of the hierarchy. These two parts form the polygon list
 * buffer. This buffer has a statically determinable size, approximately equal
 * to the # of tiles across all hierarchy levels * (8 bytes + 512 bytes), plus
 * alignment / minimum restrictions / etc.
 *
 * The third piece is the easy one (for us): the tiler heap. In essence, the
 * tiler heap is a gigantic slab that's as big as could possibly be necessary
 * in the worst case imaginable. Just... a gigantic allocation that we give a
 * start and end pointer to. What's the catch? The tiler heap is lazily
 * allocated; that is, a huge amount of memory is _reserved_, but only a tiny
 * bit is actually allocated upfront. The GPU just keeps using the
 * unallocated-but-reserved portions as it goes along, generating page faults
 * if it goes beyond the allocation, and then the kernel is instructed to
 * expand the allocation on page fault (known in the vendor kernel as growable
 * memory). This is quite a bit of bookkeeping of its own, but that task is
 * pushed to kernel space and we can mostly ignore it here, just remembering to
 * set the GROWABLE flag so the kernel actually uses this path rather than
 * allocating a gigantic amount up front and burning a hole in RAM.
 *
 * As far as determining which hierarchy levels to use, the simple answer is
 * that right now, we don't. In the tiler configuration fields (consistent from
 * the earliest Midgard's SFBD through the latest Bifrost traces we have),
 * there is a hierarchy_mask field, controlling which levels (tile sizes) are
 * enabled. Ideally, the hierarchical tiling dream -- mapping big polygons to
 * big tiles and small polygons to small tiles -- would be realized here as
 * well. As long as there are polygons at all needing tiling, we always have to
 * have big tiles available, in case there are big polygons. But we don't
 * necessarily need small tiles available. Ideally, when there are small
 * polygons, small tiles are enabled (to avoid waste from putting small
 * triangles in the big tiles); when there are not, small tiles are disabled to
 * avoid enabling more levels than necessary, which potentially costs in memory
 * bandwidth / power / tiler performance.
 *
 * Of course, the driver has to figure this out statically. When tile
 * hiearchies are actually established, this occurs by the tiler in
 * fixed-function hardware, after the vertex shaders have run and there is
 * sufficient information to figure out the size of triangles. The driver has
 * no such luxury, again barring insane hacks like additionally running the
 * vertex shaders in software or in hardware via transform feedback. Thus, for
 * the driver, we need a heuristic approach.
 *
 * There are lots of heuristics to guess triangle size statically you could
 * imagine, but one approach shines as particularly simple-stupid: assume all
 * on-screen triangles are equal size and spread equidistantly throughout the
 * screen. Let's be clear, this is NOT A VALID ASSUMPTION. But if we roll with
 * it, then we see:
 *
 *      Triangle Area   = (Screen Area / # of triangles)
 *                      = (Width * Height) / (# of triangles)
 *
 * Or if you prefer, we can also make a third CRAZY assumption that we only draw
 * right triangles with edges parallel/perpendicular to the sides of the screen
 * with no overdraw, forming a triangle grid across the screen:
 *
 * |--w--|
 *  _____   |
 * | /| /|  |
 * |/_|/_|  h
 * | /| /|  |
 * |/_|/_|  |
 *
 * Then you can use some middle school geometry and algebra to work out the
 * triangle dimensions. I started working on this, but realised I didn't need
 * to to make my point, but couldn't bare to erase that ASCII art. Anyway.
 *
 * POINT IS, by considering the ratio of screen area and triangle count, we can
 * estimate the triangle size. For a small size, use small bins; for a large
 * size, use large bins. Intuitively, this metric makes sense: when there are
 * few triangles on a large screen, you're probably compositing a UI and
 * therefore the triangles are large; when there are a lot of triangles on a
 * small screen, you're probably rendering a 3D mesh and therefore the
 * triangles are tiny. (Or better said -- there will be tiny triangles, even if
 * there are also large triangles. There have to be unless you expect crazy
 * overdraw. Generally, it's better to allow more small bin sizes than
 * necessary than not allow enough.)
 *
 * From this heuristic (or whatever), we determine the minimum allowable tile
 * size, and we use that to decide the hierarchy masking, selecting from the
 * minimum "ideal" tile size to the maximum tile size (2048x2048 in practice).
 *
 * Once we have that mask and the framebuffer dimensions, we can compute the
 * size of the statically-sized polygon list structures, allocate them, and go!
 *
 */

/* Hierarchical tiling spans from 16x16 to 4096x4096 tiles */

#define MIN_TILE_SIZE 16
#define MAX_TILE_SIZE 4096

/* Constants as shifts for easier power-of-two iteration */

#define MIN_TILE_SHIFT util_logbase2(MIN_TILE_SIZE)
#define MAX_TILE_SHIFT util_logbase2(MAX_TILE_SIZE)

/* The hierarchy has a 64-byte prologue */
#define PROLOGUE_SIZE 0x40

/* For each tile (across all hierarchy levels), there is 8 bytes of header */
#define HEADER_BYTES_PER_TILE 0x8

/* Likewise, each tile per level has 512 bytes of body */
#define FULL_BYTES_PER_TILE 0x200

/* Absent any geometry, the minimum size of the header */
#define MINIMUM_HEADER_SIZE 0x200

/* Mask of valid hierarchy levels: one bit for each level from min...max
 * inclusive */
#define HIERARCHY_MASK (((MAX_TILE_SIZE / MIN_TILE_SIZE) << 1) - 1)

/* If the width-x-height framebuffer is divided into tile_size-x-tile_size
 * tiles, how many tiles are there? Rounding up in each direction. For the
 * special case of tile_size=16, this aligns with the usual Midgard count.
 * tile_size must be a power-of-two. Not really repeat code from AFBC/checksum,
 * because those care about the stride (not just the overall count) and only at
 * a a fixed-tile size (not any of a number of power-of-twos) */

static unsigned
pan_tile_count(unsigned width, unsigned height, unsigned tile_size)
{
        unsigned aligned_width = ALIGN_POT(width, tile_size);
        unsigned aligned_height = ALIGN_POT(height, tile_size);

        unsigned tile_count_x = aligned_width / tile_size;
        unsigned tile_count_y = aligned_height / tile_size;

        return tile_count_x * tile_count_y;
}

/* For `masked_count` of the smallest tile sizes masked out, computes how the
 * size of the polygon list header. We iterate the tile sizes (16x16 through
 * 2048x2048, if nothing is masked; (16*2^masked_count)x(16*2^masked_count)
 * through 2048x2048 more generally. For each tile size, we figure out how many
 * tiles there are at this hierarchy level and therefore many bytes this level
 * is, leaving us with a byte count for each level. We then just sum up the
 * byte counts across the levels to find a byte count for all levels. */

static unsigned
panfrost_raw_segment_size(
                unsigned width,
                unsigned height,
                unsigned masked_count,
                unsigned end_level,
                unsigned bytes_per_tile)
{
        unsigned size = PROLOGUE_SIZE;

        /* Normally we start at 16x16 tiles (MIN_TILE_SHIFT), but we add more
         * if anything is masked off */

        unsigned start_level = MIN_TILE_SHIFT + masked_count;

        /* Iterate hierarchy levels / tile sizes */

        for (unsigned i = start_level; i <= end_level; ++i) {
                /* Shift from a level to a tile size */
                unsigned tile_size = (1 << i);

                unsigned tile_count = pan_tile_count(width, height, tile_size);
                unsigned level_count = bytes_per_tile * tile_count;

                size += level_count;
        }

        /* This size will be used as an offset, so ensure it's aligned */
        return ALIGN_POT(size, 512);
}

/* Given a hierarchy mask and a framebuffer size, compute the size of one of
 * the segments (header or body) */

static unsigned
panfrost_segment_size(
                unsigned width, unsigned height,
                unsigned mask, unsigned bytes_per_tile)
{
        /* The tiler-disabled case should have been handled by the caller */
        assert(mask);

        /* Some levels are enabled. Ensure that only smaller levels are
         * disabled and there are no gaps. Theoretically the hardware is more
         * flexible, but there's no known reason to use other configurations
         * and this keeps the code simple. Since we know the 0x80 or 0x100 bit
         * is set, ctz(mask) will return the number of masked off levels. */

        unsigned masked_count = __builtin_ctz(mask);

        assert(mask & (0x80 | 0x100));
        assert(((mask >> masked_count) & ((mask >> masked_count) + 1)) == 0);

        /* Figure out the top level */
        unsigned unused_count = __builtin_clz(mask);
        unsigned top_bit = ((8 * sizeof(mask)) - 1) - unused_count;

        /* We don't have bits for nonexistant levels below 16x16 */
        unsigned top_level = top_bit + 4;

        /* Everything looks good. Use the number of trailing zeroes we found to
         * figure out how many smaller levels are disabled to compute the
         * actual header size */

        return panfrost_raw_segment_size(width, height,
                        masked_count, top_level, bytes_per_tile);
}


/* Given a hierarchy mask and a framebuffer size, compute the header size */

unsigned
panfrost_tiler_header_size(unsigned width, unsigned height, unsigned mask)
{
        mask &= HIERARCHY_MASK;

        /* If no hierarchy levels are enabled, that means there is no geometry
         * for the tiler to process, so use a minimum size. Used for clears */

        if (mask == 0x00)
                return MINIMUM_HEADER_SIZE;

        return panfrost_segment_size(width, height, mask, HEADER_BYTES_PER_TILE);
}

/* The combined header/body is sized similarly (but it is significantly
 * larger), except that it can be empty when the tiler disabled, rather than
 * getting clamped to a minimum size.
 */

unsigned
panfrost_tiler_full_size(unsigned width, unsigned height, unsigned mask)
{
        mask &= HIERARCHY_MASK;

        if (mask == 0x00)
                return MINIMUM_HEADER_SIZE;

        return panfrost_segment_size(width, height, mask, FULL_BYTES_PER_TILE);
}

/* In the future, a heuristic to choose a tiler hierarchy mask would go here.
 * At the moment, we just default to 0xFF, which enables all possible hierarchy
 * levels. Overall this yields good performance but presumably incurs a cost in
 * memory bandwidth / power consumption / etc, at least on smaller scenes that
 * don't really need all the smaller levels enabled */

unsigned
panfrost_choose_hierarchy_mask(
        unsigned width, unsigned height,
        unsigned vertex_count)
{
        /* If there is no geometry, we don't bother enabling anything */

        if (!vertex_count)
                return 0x00;

        /* Otherwise, default everything on. TODO: Proper tests */

        return 0xFF;
}
