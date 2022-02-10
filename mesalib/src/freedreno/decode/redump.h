/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
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

#ifndef REDUMP_H_
#define REDUMP_H_

#include "util/u_math.h"

enum rd_sect_type {
   RD_NONE,
   RD_TEST,           /* ascii text */
   RD_CMD,            /* ascii text */
   RD_GPUADDR,        /* u32 gpuaddr, u32 size */
   RD_CONTEXT,        /* raw dump */
   RD_CMDSTREAM,      /* raw dump */
   RD_CMDSTREAM_ADDR, /* gpu addr of cmdstream */
   RD_PARAM,          /* u32 param_type, u32 param_val, u32 bitlen */
   RD_FLUSH,          /* empty, clear previous params */
   RD_PROGRAM,        /* shader program, raw dump */
   RD_VERT_SHADER,
   RD_FRAG_SHADER,
   RD_BUFFER_CONTENTS,
   RD_GPU_ID,
   RD_CHIP_ID,
};

/* RD_PARAM types: */
enum rd_param_type {
   RD_PARAM_SURFACE_WIDTH,
   RD_PARAM_SURFACE_HEIGHT,
   RD_PARAM_SURFACE_PITCH,
   RD_PARAM_COLOR,
   RD_PARAM_BLIT_X,
   RD_PARAM_BLIT_Y,
   RD_PARAM_BLIT_WIDTH,
   RD_PARAM_BLIT_HEIGHT,
   RD_PARAM_BLIT_X2, /* BLIT_X + BLIT_WIDTH */
   RD_PARAM_BLIT_Y2, /* BLIT_Y + BLIT_WIDTH */
};

void rd_start(const char *name, const char *fmt, ...) __attribute__((weak));
void rd_end(void) __attribute__((weak));
void rd_write_section(enum rd_sect_type type, const void *buf, int sz)
   __attribute__((weak));

/* for code that should run with and without libwrap, use the following
 * macros which check if the fxns are present before calling
 */
#define RD_START(n, f, ...)                                                    \
   do {                                                                        \
      if (rd_start)                                                            \
         rd_start(n, f, ##__VA_ARGS__);                                        \
   } while (0)
#define RD_END()                                                               \
   do {                                                                        \
      if (rd_end)                                                              \
         rd_end();                                                             \
   } while (0)
#define RD_WRITE_SECTION(t, b, s)                                              \
   do {                                                                        \
      if (rd_write_section)                                                    \
         rd_write_section(t, b, s);                                            \
   } while (0)

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

#endif /* REDUMP_H_ */
