/****************************************************************************
 * Copyright (C) 2016 Intel Corporation.   All Rights Reserved.
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
 * @file arena.h
 *
 * @brief RingBuffer
 *        The RingBuffer class manages all aspects of the ring buffer including
 *        the head/tail indices, etc.
 *
 ******************************************************************************/
#pragma once

template <typename T>
class RingBuffer
{
public:
    RingBuffer() : mpRingBuffer(nullptr), mNumEntries(0), mRingHead(0), mRingTail(0) {}

    ~RingBuffer() { Destroy(); }

    void Init(uint32_t numEntries)
    {
        SWR_ASSERT(numEntries > 0);
        SWR_ASSERT(((1ULL << 32) % numEntries) == 0,
                   "%d is not evenly divisible into 2 ^ 32.  Wrap errors will occur!",
                   numEntries);
        mNumEntries  = numEntries;
        mpRingBuffer = (T*)AlignedMalloc(sizeof(T) * numEntries, 64);
        SWR_ASSERT(mpRingBuffer != nullptr);
        memset((void*)mpRingBuffer, 0, sizeof(T) * numEntries);
    }

    void Destroy()
    {
        AlignedFree(mpRingBuffer);
        mpRingBuffer = nullptr;
    }

    T& operator[](const uint32_t index)
    {
        SWR_ASSERT(index < mNumEntries);
        return mpRingBuffer[index];
    }

    INLINE void Enqueue()
    {
        mRingHead++; // There's only one producer.
        // Assert to find wrap-around cases, NEVER ENABLE DURING CHECKIN!!
        // SWR_REL_ASSERT(mRingHead);
    }

    INLINE void Dequeue()
    {
        InterlockedIncrement(&mRingTail); // There are multiple consumers.
    }

    INLINE bool IsEmpty() { return (GetHead() == GetTail()); }

    INLINE bool IsFull()
    {
        uint32_t numEnqueued = GetHead() - GetTail();
        SWR_ASSERT(numEnqueued <= mNumEntries);

        return (numEnqueued == mNumEntries);
    }

    INLINE uint32_t GetTail() volatile { return mRingTail; }
    INLINE uint32_t GetHead() volatile { return mRingHead; }

protected:
    T*       mpRingBuffer;
    uint32_t mNumEntries;

    OSALIGNLINE(volatile uint32_t) mRingHead; // Consumer Counter
    OSALIGNLINE(volatile uint32_t) mRingTail; // Producer Counter
};
