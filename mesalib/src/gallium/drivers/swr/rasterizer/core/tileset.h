/****************************************************************************
 * Copyright (C) 2018 Intel Corporation.   All Rights Reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * @file tileset.h
 *
 * @brief Custom bitset class for managing locked tiles
 *
 ******************************************************************************/
#pragma once

struct TileSet
{
    ~TileSet()
    {
        if (m_bits)
        {
            AlignedFree(m_bits);
        }
    }
    INLINE void set(size_t idx)
    {
        _grow(idx);
        size_t& word = _get_word(idx);
        word |= (size_t(1) << (idx & BITS_OFFSET));
        m_maxSet = std::max(m_maxSet, idx + 1);
    }
    INLINE bool get(size_t idx)
    {
        if (idx >= m_size)
        {
            return false;
        }
        size_t word = _get_word(idx);
        return 0 != (word & (size_t(1) << (idx & BITS_OFFSET)));
    }

    INLINE void clear()
    {
        if (m_maxSet)
        {
            size_t num_words = (m_maxSet + BITS_OFFSET) / BITS_PER_WORD;
            memset(m_bits, 0, sizeof(size_t) * num_words);
            m_maxSet = 0;
        }
    }

private:
    static const size_t BITS_PER_WORD = sizeof(size_t) * 8;
    static const size_t BITS_OFFSET   = BITS_PER_WORD - 1;

    size_t  m_size   = 0;
    size_t  m_maxSet = 0;
    size_t* m_bits   = nullptr;

    INLINE size_t& _get_word(size_t idx) { return m_bits[idx / BITS_PER_WORD]; }

    void _grow(size_t idx)
    {
        if (idx < m_size)
        {
            return;
        }

        size_t  new_size   = (1 + idx + BITS_OFFSET) & ~BITS_OFFSET;
        size_t  num_words  = new_size / BITS_PER_WORD;
        size_t* newBits    = (size_t*)AlignedMalloc(sizeof(size_t) * num_words, 64);
        size_t  copy_words = 0;

        if (m_bits)
        {
            copy_words = (m_size + BITS_OFFSET) / BITS_PER_WORD;
            num_words -= copy_words;
            memcpy(newBits, m_bits, copy_words * sizeof(size_t));

            AlignedFree(m_bits);
        }

        m_bits = newBits;
        m_size = new_size;

        memset(&m_bits[copy_words], 0, sizeof(size_t) * num_words);
    }
};
