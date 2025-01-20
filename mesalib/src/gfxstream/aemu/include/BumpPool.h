/*
 * Copyright 2019 Google
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <inttypes.h>

#include <unordered_set>
#include <vector>

#include "AlignedBuf.h"
#include "Allocator.h"

namespace gfxstream {
namespace aemu {

// Class to make it easier to set up memory regions where it is fast
// to allocate buffers AND we don't care about freeing individual pieces,
// BUT it's necessary to preserve previous pointer values in between the first
// alloc() after a freeAll(), and the freeAll() itself, allowing some sloppy use of
// malloc in the first pass while we find out how much data was needed.
class BumpPool : public Allocator {
   public:
    BumpPool(size_t startingBytes = 4096) : mStorage(startingBytes / sizeof(uint64_t)) {}
    // All memory allocated by this pool
    // is automatically deleted when the pool
    // is deconstructed.
    ~BumpPool() { freeAll(); }

    void* alloc(size_t wantedSize) override {
        size_t wantedSizeRoundedUp =
            sizeof(uint64_t) * ((wantedSize + sizeof(uint64_t) - 1) / (sizeof(uint64_t)));

        mTotalWantedThisGeneration += wantedSizeRoundedUp;
        if (mAllocPos + wantedSizeRoundedUp > mStorage.size() * sizeof(uint64_t)) {
            mNeedRealloc = true;
            void* fallbackPtr = malloc(wantedSizeRoundedUp);
            mFallbackPtrs.insert(fallbackPtr);
            return fallbackPtr;
        }
        void* allocPtr = (void*)(((unsigned char*)mStorage.data()) + mAllocPos);
        mAllocPos += wantedSizeRoundedUp;
        return allocPtr;
    }

    void freeAll() {
        mAllocPos = 0;
        if (mNeedRealloc) {
            mStorage.resize((mTotalWantedThisGeneration * 2) / sizeof(uint64_t));
            mNeedRealloc = false;
            for (auto ptr : mFallbackPtrs) {
                free(ptr);
            }
            mFallbackPtrs.clear();
        }
        mTotalWantedThisGeneration = 0;
    }

   private:
    AlignedBuf<uint64_t, 8> mStorage;
    std::unordered_set<void*> mFallbackPtrs;
    size_t mAllocPos = 0;
    size_t mTotalWantedThisGeneration = 0;
    bool mNeedRealloc = false;
};

}  // namespace aemu
}  // namespace gfxstream
