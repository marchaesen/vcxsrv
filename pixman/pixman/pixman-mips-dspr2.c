/*
 * Copyright (c) 2012
 *      MIPS Technologies, Inc., California.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the MIPS Technologies, Inc., nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE MIPS TECHNOLOGIES, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE MIPS TECHNOLOGIES, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Author:  Nemanja Lukic (nlukic@mips.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pixman-private.h"
#include "pixman-mips-dspr2.h"

PIXMAN_MIPS_BIND_FAST_PATH_SRC_DST (0, src_x888_8888,
                                    uint32_t, 1, uint32_t, 1)
PIXMAN_MIPS_BIND_FAST_PATH_SRC_DST (0, src_8888_0565,
                                    uint32_t, 1, uint16_t, 1)
PIXMAN_MIPS_BIND_FAST_PATH_SRC_DST (0, src_0565_8888,
                                    uint16_t, 1, uint32_t, 1)
PIXMAN_MIPS_BIND_FAST_PATH_SRC_DST (DO_FAST_MEMCPY, src_0565_0565,
                                    uint16_t, 1, uint16_t, 1)
PIXMAN_MIPS_BIND_FAST_PATH_SRC_DST (DO_FAST_MEMCPY, src_8888_8888,
                                    uint32_t, 1, uint32_t, 1)
PIXMAN_MIPS_BIND_FAST_PATH_SRC_DST (DO_FAST_MEMCPY, src_0888_0888,
                                    uint8_t, 3, uint8_t, 3)

static const pixman_fast_path_t mips_dspr2_fast_paths[] =
{
    PIXMAN_STD_FAST_PATH (SRC, r5g6b5,   null, r5g6b5,   mips_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, b5g6r5,   null, b5g6r5,   mips_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, a8r8g8b8, null, r5g6b5,   mips_composite_src_8888_0565),
    PIXMAN_STD_FAST_PATH (SRC, x8r8g8b8, null, r5g6b5,   mips_composite_src_8888_0565),
    PIXMAN_STD_FAST_PATH (SRC, a8b8g8r8, null, b5g6r5,   mips_composite_src_8888_0565),
    PIXMAN_STD_FAST_PATH (SRC, x8b8g8r8, null, b5g6r5,   mips_composite_src_8888_0565),
    PIXMAN_STD_FAST_PATH (SRC, r5g6b5,   null, a8r8g8b8, mips_composite_src_0565_8888),
    PIXMAN_STD_FAST_PATH (SRC, r5g6b5,   null, x8r8g8b8, mips_composite_src_0565_8888),
    PIXMAN_STD_FAST_PATH (SRC, b5g6r5,   null, a8b8g8r8, mips_composite_src_0565_8888),
    PIXMAN_STD_FAST_PATH (SRC, b5g6r5,   null, x8b8g8r8, mips_composite_src_0565_8888),
    PIXMAN_STD_FAST_PATH (SRC, a8r8g8b8, null, x8r8g8b8, mips_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, x8r8g8b8, null, x8r8g8b8, mips_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, a8b8g8r8, null, x8b8g8r8, mips_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, x8b8g8r8, null, x8b8g8r8, mips_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, a8r8g8b8, null, a8r8g8b8, mips_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, a8b8g8r8, null, a8b8g8r8, mips_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, x8r8g8b8, null, a8r8g8b8, mips_composite_src_x888_8888),
    PIXMAN_STD_FAST_PATH (SRC, x8b8g8r8, null, a8b8g8r8, mips_composite_src_x888_8888),
    PIXMAN_STD_FAST_PATH (SRC, r8g8b8,   null, r8g8b8,   mips_composite_src_0888_0888),

    { PIXMAN_OP_NONE },
};

pixman_implementation_t *
_pixman_implementation_create_mips_dspr2 (pixman_implementation_t *fallback)
{
    pixman_implementation_t *imp =
        _pixman_implementation_create (fallback, mips_dspr2_fast_paths);

    return imp;
}
