/*
 * Copyright (C) 2020 Collabora, Ltd.
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
 */

#include "util/ralloc.h"
#include "pan_partial_update.h"

static struct pan_rect
pan_make_rect(unsigned minx, unsigned miny, unsigned maxx, unsigned maxy)
{
        struct pan_rect r = {
                .minx = minx,
                .miny = miny,
                .maxx = maxx,
                .maxy = maxy
        };

        return r;
}

static struct pan_rect
pan_from_pipe(const struct pipe_box *p)
{
        return pan_make_rect(p->x, p->y, p->x + p->width, p->y + p->height);
}

/* Helper to clip a rect inside another box */

static struct pan_rect
pan_clip_rect(
        const struct pan_rect *r,
        const struct pan_rect *clip)
{
        unsigned minx = MAX2(r->minx, clip->minx);
        unsigned miny = MAX2(r->miny, clip->miny);
        unsigned maxx = MAX2(MIN2(r->maxx, clip->maxx), minx);
        unsigned maxy = MAX2(MIN2(r->maxy, clip->maxy), miny);

        return pan_make_rect(minx, miny, maxx, maxy);
}

/* Subtract d from r, yielding four (possibly degenerate) rectangles for each
 * bounding region */

static void
pan_subtract_from_rect(
                struct pan_rect *out,
                const struct pan_rect *r,
                const struct pan_rect *d)
{
        struct pan_rect dc = pan_clip_rect(r, d);

        out[0] = pan_make_rect(r->minx, r->miny, dc.minx, r->maxy);
        out[1] = pan_make_rect(dc.minx, r->miny, dc.maxx, dc.miny);
        out[2] = pan_make_rect(dc.maxx, r->miny, r->maxx, r->maxy);
        out[3] = pan_make_rect(dc.minx, dc.maxy, dc.maxx, r->maxy);
}

/* Subtract d from the set of rects R, returning the number of
 * (non-degenerate) rectangles returned.
 *
 * out must be at least sizeof(struct pan_rect) * R_len * 4 bytes
 *
 * Trivially satisfies: return value < (R_len * 4) */

static unsigned
pan_subtract_from_rects(
                struct pan_rect *out,
                const struct pan_rect *R,
                unsigned R_len,
                const struct pan_rect *d)
{
        unsigned count = 0;
        struct pan_rect temp[4];

        for (unsigned r = 0; r < R_len; ++r) {
                pan_subtract_from_rect(temp, &R[r], d);

                /* Copy the rectangles with nonzero area */
                for (unsigned i = 0; i < 4; ++i) {
                        if (temp[i].maxx <= temp[i].minx) continue;
                        if (temp[i].maxy <= temp[i].miny) continue;

                        out[count++] = temp[i];
                }
        }

        return count;
}

struct pan_rect *
pan_subtract_damage(
                void *memctx,
                unsigned initial_w, unsigned initial_h,
                unsigned nrects,
                const struct pipe_box *rects,
                unsigned *out_len)
{
        struct pan_rect *R = ralloc(memctx, struct pan_rect);
        *R = pan_make_rect(0, 0, initial_w, initial_h);
        unsigned R_len = 1;

        for (unsigned d = 0; d < nrects; ++d) {
                const struct pan_rect D = pan_from_pipe(&rects[d]);
                struct pan_rect *out = rzalloc_array(memctx, struct pan_rect,
                        R_len * 4);

                R_len = pan_subtract_from_rects(out, R, R_len, &D);
                ralloc_free(R);
                R = out;
        }

        *out_len = R_len;
        return R;
}
