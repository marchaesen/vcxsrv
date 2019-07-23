/* Author(s):
 *   Connor Abbott
 *   Alyssa Rosenzweig
 *
 * Copyright (c) 2013 Connor Abbott (connor@abbott.cx)
 * Copyright (c) 2018 Alyssa Rosenzweig (alyssa@rosenzweig.io)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef __midgard_parse_h__
#define __midgard_parse_h__

/* Additional metadata for parsing Midgard binaries, not needed for compilation */

static midgard_word_type midgard_word_types[16] = {
        midgard_word_type_unknown,    /* 0x0 */
        midgard_word_type_unknown,    /* 0x1 */
        midgard_word_type_texture,    /* 0x2 */
        midgard_word_type_texture,    /* 0x3 */
        midgard_word_type_unknown,    /* 0x4 */
        midgard_word_type_load_store, /* 0x5 */
        midgard_word_type_unknown,    /* 0x6 */
        midgard_word_type_unknown,    /* 0x7 */
        midgard_word_type_alu,        /* 0x8 */
        midgard_word_type_alu,        /* 0x9 */
        midgard_word_type_alu,        /* 0xA */
        midgard_word_type_alu,        /* 0xB */
        midgard_word_type_alu,        /* 0xC */
        midgard_word_type_alu,        /* 0xD */
        midgard_word_type_alu,        /* 0xE */
        midgard_word_type_alu,        /* 0xF */
};

static unsigned midgard_word_size[16] = {
        0, /* 0x0 */
        0, /* 0x1 */
        1, /* 0x2 */
        1, /* 0x3 */
        0, /* 0x4 */
        1, /* 0x5 */
        0, /* 0x6 */
        0, /* 0x7 */
        1, /* 0x8 */
        2, /* 0x9 */
        3, /* 0xA */
        4, /* 0xB */
        1, /* 0xC */
        2, /* 0xD */
        3, /* 0xE */
        4, /* 0xF */
};

#endif
