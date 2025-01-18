/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */

static ResourceTracker* sResourceTracker = nullptr;
static uint32_t sFeatureBits = 0;
static constexpr uint32_t kWatchdogBufferMax = 1'000;

#if defined(__ANDROID__)
#include <cutils/properties.h>
#endif

class VkEncoder::Impl {
   public:
    Impl(gfxstream::guest::IOStream* stream) : m_stream(stream), m_logEncodes(false) {
        if (!sResourceTracker) sResourceTracker = ResourceTracker::get();
        m_stream.incStreamRef();
#if defined(__ANDROID__)
        const char* emuVkLogEncodesPropName = "qemu.vk.log";
        char encodeProp[PROPERTY_VALUE_MAX];
        if (property_get(emuVkLogEncodesPropName, encodeProp, nullptr) > 0) {
            m_logEncodes = atoi(encodeProp) > 0;
        }
#endif
        sFeatureBits = m_stream.getFeatureBits();
    }

    ~Impl() { m_stream.decStreamRef(); }

    VulkanCountingStream* countingStream() { return &m_countingStream; }
    VulkanStreamGuest* stream() { return &m_stream; }
    BumpPool* pool() { return &m_pool; }
    ResourceTracker* resources() { return ResourceTracker::get(); }
    Validation* validation() { return &m_validation; }

    void log(const char* text) {
        if (!m_logEncodes) return;
    }

    void flush() {
        lock();
        m_stream.flush();
        unlock();
    }

    // can be recursive
    void lock() {
        while (mLock.test_and_set(std::memory_order_acquire))
            ;
    }

    void unlock() { mLock.clear(std::memory_order_release); }

   private:
    VulkanCountingStream m_countingStream;
    VulkanStreamGuest m_stream;
    BumpPool m_pool;

    Validation m_validation;
    bool m_logEncodes;
    std::atomic_flag mLock = ATOMIC_FLAG_INIT;
};

VkEncoder::~VkEncoder() {}

VkEncoder::VkEncoder(gfxstream::guest::IOStream* stream)
    : mImpl(new VkEncoder::Impl(stream)) {}

void VkEncoder::flush() { mImpl->flush(); }

void VkEncoder::lock() { mImpl->lock(); }

void VkEncoder::unlock() { mImpl->unlock(); }

void VkEncoder::incRef() { __atomic_add_fetch(&refCount, 1, __ATOMIC_SEQ_CST); }

bool VkEncoder::decRef() {
    if (0 == __atomic_sub_fetch(&refCount, 1, __ATOMIC_SEQ_CST)) {
        delete this;
        return true;
    }
    return false;
}

std::string VkEncoder::getPacketContents(const uint8_t* ptr, size_t len) {
    std::string result;
    result.reserve(3 * len);
    char buf[4];
    for (size_t i = 0; i < len; i++) {
        std::snprintf(buf, 4, " %02X", ptr[i]);
        result.append(buf, 3);
    }
    return result;
}
