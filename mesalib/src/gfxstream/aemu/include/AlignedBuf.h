/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdio.h>

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace gfxstream {

/**
 * Do not abuse this by using any complicated T. Use it for POD or primitives
 */
template <class T, size_t align>
class AlignedBuf {
    constexpr static bool triviallyCopyable() {
#if (defined(__GNUC__) && !defined(__clang__) && __GNUC__ <= 4) || defined(__OLD_STD_VERSION__)
        // Older g++ doesn't support std::is_trivially_copyable.
        constexpr bool triviallyCopyable = std::has_trivial_copy_constructor<T>::value;
#else
        constexpr bool triviallyCopyable = std::is_trivially_copyable<T>::value;
#endif
        return triviallyCopyable;
    }
    static_assert(triviallyCopyable() && std::is_standard_layout<T>::value &&
                  std::is_trivially_default_constructible<T>::value);

   public:
    explicit AlignedBuf(size_t size) {
        static_assert(align && ((align & (align - 1)) == 0),
                      "AlignedBuf only supports power-of-2 aligments.");
        resizeImpl(size);
    }

    AlignedBuf(const AlignedBuf& other) : AlignedBuf(other.mSize) {
        if (other.mBuffer) {  // could have got moved out
            std::copy(other.mBuffer, other.mBuffer + other.mSize, mBuffer);
        }
    }

    AlignedBuf& operator=(const AlignedBuf& other) {
        if (this != &other) {
            AlignedBuf tmp(other);
            *this = std::move(tmp);
        }
        return *this;
    }

    AlignedBuf(AlignedBuf&& other) { *this = std::move(other); }

    AlignedBuf& operator=(AlignedBuf&& other) {
        mBuffer = other.mBuffer;
        mSize = other.mSize;

        other.mBuffer = nullptr;
        other.mSize = 0;

        return *this;
    }

    ~AlignedBuf() {
        if (mBuffer) freeImpl(mBuffer);
    }  // account for getting moved out

    void resize(size_t newSize) { resizeImpl(newSize); }

    size_t size() const { return mSize; }

    T* data() { return mBuffer; }

    T& operator[](size_t index) { return mBuffer[index]; }

    const T& operator[](size_t index) const { return mBuffer[index]; }

    bool operator==(const AlignedBuf& other) const {
        return 0 == std::memcmp(mBuffer, other.mBuffer, sizeof(T) * std::min(mSize, other.mSize));
    }

   private:
    T* getNewBuffer(size_t newSize) {
        if (newSize == 0) {
            return nullptr;
        }
        size_t pad = std::max(align, sizeof(T));
        size_t newSizeBytes = ((align - 1 + newSize * sizeof(T) + pad) / align) * align;
        return static_cast<T*>(reallocImpl(nullptr, newSizeBytes));
    }

    void resizeImpl(size_t newSize) {
        T* new_buffer = getNewBuffer(newSize);
        if (new_buffer && mBuffer) {
            size_t keepSize = std::min(newSize, mSize);
            std::copy(mBuffer, mBuffer + keepSize, new_buffer);
        }
        if (mBuffer) {
            freeImpl(mBuffer);
        }
        mBuffer = new_buffer;
        mSize = (new_buffer ? newSize : 0);
    }

    void* reallocImpl(void* oldPtr, size_t sizeBytes) {
        if (oldPtr) {
            freeImpl(oldPtr);
        }
        // Platform aligned malloc might not behave right
        // if we give it an alignment value smaller than sizeof(void*).
        size_t actualAlign = std::max(align, sizeof(void*));
#ifdef _WIN32
        return _aligned_malloc(sizeBytes, actualAlign);
#else
        void* res;
        if (posix_memalign(&res, actualAlign, sizeBytes)) {
            fprintf(stderr, "%s: failed to alloc aligned memory\n", __func__);
            abort();
        }
        return res;
#endif
    }

    void freeImpl(void* ptr) {
#ifdef _WIN32
        _aligned_free(ptr);
#else
        free(ptr);
#endif
    }

    T* mBuffer = nullptr;
    size_t mSize = 0;
};

// Convenience function for aligned malloc across platforms
void* aligned_buf_alloc(size_t align, size_t size);
void aligned_buf_free(void* buf);

}  // namespace gfxstream
