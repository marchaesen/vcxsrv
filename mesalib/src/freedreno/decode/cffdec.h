/*
 * Copyright (c) 2012 Rob Clark <robdclark@gmail.com>
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

#ifndef __CFFDEC_H__
#define __CFFDEC_H__

#include <stdbool.h>

enum query_mode {
   /* default mode, dump all queried regs on each draw: */
   QUERY_ALL = 0,

   /* only dump if any of the queried regs were written
    * since last draw:
    */
   QUERY_WRITTEN,

   /* only dump if any of the queried regs changed since
    * last draw:
    */
   QUERY_DELTA,
};

struct cffdec_options {
   unsigned gpu_id;
   int draw_filter;
   int color;
   int dump_shaders;
   int summary;
   int allregs;
   int dump_textures;
   int decode_markers;
   char *script;

   int query_compare; /* binning vs SYSMEM/GMEM compare mode */
   int query_mode;    /* enum query_mode */
   char **querystrs;
   int nquery;

   /* In "once" mode, only decode a cmdstream buffer once (per draw
    * mode, in the case of a6xx+ where a single cmdstream buffer can
    * be used for both binning and draw pass), rather than each time
    * encountered (ie. once per tile/bin in GMEM draw passes)
    */
   int once;

   /* for crashdec, where we know CP_IBx_REM_SIZE, we can use this
    * to highlight the cmdstream not parsed yet, to make it easier
    * to see how far along the CP is.
    */
   struct {
      uint64_t base;
      uint32_t rem;
   } ibs[4];
};

void printl(int lvl, const char *fmt, ...);
const char *pktname(unsigned opc);
uint32_t regbase(const char *name);
const char *regname(uint32_t regbase, int color);
bool reg_written(uint32_t regbase);
uint32_t reg_lastval(uint32_t regbase);
uint32_t reg_val(uint32_t regbase);
void reg_set(uint32_t regbase, uint32_t val);
void reset_regs(void);
void cffdec_init(const struct cffdec_options *options);
void dump_register_val(uint32_t regbase, uint32_t dword, int level);
void dump_commands(uint32_t *dwords, uint32_t sizedwords, int level);

#endif /* __CFFDEC_H__ */
