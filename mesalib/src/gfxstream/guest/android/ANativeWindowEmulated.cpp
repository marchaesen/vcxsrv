/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "ANativeWindowEmulated.h"

#include "util/log.h"

namespace gfxstream {

EmulatedANativeWindow::EmulatedANativeWindow(
    uint32_t width, uint32_t height, uint32_t format,
    std::vector<std::unique_ptr<EmulatedAHardwareBuffer>> buffers)
    : mRefCount(1), mWidth(width), mHeight(height), mFormat(format), mBuffers(std::move(buffers)) {
    for (auto& buffer : mBuffers) {
        mBufferQueue.push_back(QueuedAHB{
            .ahb = buffer.get(),
            .fence = -1,
        });
    }
}

EGLNativeWindowType EmulatedANativeWindow::asEglNativeWindowType() {
    return reinterpret_cast<EGLNativeWindowType>(this);
}

uint32_t EmulatedANativeWindow::getWidth() const { return mWidth; }

uint32_t EmulatedANativeWindow::getHeight() const { return mHeight; }

int EmulatedANativeWindow::getFormat() const { return mFormat; }

int EmulatedANativeWindow::queueBuffer(EGLClientBuffer buffer, int fence) {
    auto ahb = reinterpret_cast<EmulatedAHardwareBuffer*>(buffer);

    mBufferQueue.push_back(QueuedAHB{
        .ahb = ahb,
        .fence = fence,
    });

    return 0;
}

int EmulatedANativeWindow::dequeueBuffer(EGLClientBuffer* buffer, int* fence) {
    auto queuedAhb = mBufferQueue.front();
    mBufferQueue.pop_front();

    *buffer = queuedAhb.ahb->asEglClientBuffer();
    *fence = queuedAhb.fence;
    return 0;
}

int EmulatedANativeWindow::cancelBuffer(EGLClientBuffer buffer) {
    auto ahb = reinterpret_cast<EmulatedAHardwareBuffer*>(buffer);

    mBufferQueue.push_back(QueuedAHB{
        .ahb = ahb,
        .fence = -1,
    });

    return 0;
}

void EmulatedANativeWindow::acquire() { ++mRefCount; }

void EmulatedANativeWindow::release() {
    --mRefCount;
    if (mRefCount == 0) {
        delete this;
    }
}

bool EmulatedANativeWindowHelper::isValid(EGLNativeWindowType window) {
    // TODO: maybe a registry of valid EmulatedANativeWindow-s?
    (void)window;
    return true;
}

bool EmulatedANativeWindowHelper::isValid(EGLClientBuffer buffer) {
    // TODO: maybe a registry of valid EmulatedAHardwareBuffer-s?
    (void)buffer;
    return true;
}

void EmulatedANativeWindowHelper::acquire(EGLNativeWindowType window) {
    auto* anw = reinterpret_cast<EmulatedANativeWindow*>(window);
    anw->acquire();
}

void EmulatedANativeWindowHelper::release(EGLNativeWindowType window) {
    auto* anw = reinterpret_cast<EmulatedANativeWindow*>(window);
    anw->release();
}

void EmulatedANativeWindowHelper::acquire(EGLClientBuffer buffer) {
    // TODO: maybe a registry of valid EmulatedAHardwareBuffer-s?
    (void)buffer;
}

void EmulatedANativeWindowHelper::release(EGLClientBuffer buffer) { (void)buffer; }

int EmulatedANativeWindowHelper::getConsumerUsage(EGLNativeWindowType window, int* usage) {
    (void)window;
    (void)usage;
    return 0;
}
void EmulatedANativeWindowHelper::setUsage(EGLNativeWindowType window, int usage) {
    (void)window;
    (void)usage;
}

int EmulatedANativeWindowHelper::getWidth(EGLNativeWindowType window) {
    auto anw = reinterpret_cast<EmulatedANativeWindow*>(window);
    return anw->getWidth();
}

int EmulatedANativeWindowHelper::getHeight(EGLNativeWindowType window) {
    auto anw = reinterpret_cast<EmulatedANativeWindow*>(window);
    return anw->getHeight();
}

int EmulatedANativeWindowHelper::getWidth(EGLClientBuffer buffer) {
    auto ahb = reinterpret_cast<EmulatedAHardwareBuffer*>(buffer);
    return ahb->getWidth();
}

int EmulatedANativeWindowHelper::getHeight(EGLClientBuffer buffer) {
    auto ahb = reinterpret_cast<EmulatedAHardwareBuffer*>(buffer);
    return ahb->getHeight();
}

int EmulatedANativeWindowHelper::getFormat(EGLClientBuffer buffer, Gralloc* helper) {
    (void)helper;

    auto ahb = reinterpret_cast<EmulatedAHardwareBuffer*>(buffer);
    return ahb->getAndroidFormat();
}

void EmulatedANativeWindowHelper::setSwapInterval(EGLNativeWindowType window, int interval) {
    mesa_loge("Unimplemented");
    (void)window;
    (void)interval;
}

int EmulatedANativeWindowHelper::queueBuffer(EGLNativeWindowType window, EGLClientBuffer buffer,
                                             int fence) {
    auto anw = reinterpret_cast<EmulatedANativeWindow*>(window);
    return anw->queueBuffer(buffer, fence);
}

int EmulatedANativeWindowHelper::dequeueBuffer(EGLNativeWindowType window, EGLClientBuffer* buffer,
                                               int* fence) {
    auto anw = reinterpret_cast<EmulatedANativeWindow*>(window);
    return anw->dequeueBuffer(buffer, fence);
}

int EmulatedANativeWindowHelper::cancelBuffer(EGLNativeWindowType window, EGLClientBuffer buffer) {
    auto anw = reinterpret_cast<EmulatedANativeWindow*>(window);
    return anw->cancelBuffer(buffer);
}

int EmulatedANativeWindowHelper::getHostHandle(EGLClientBuffer buffer, Gralloc*) {
    auto ahb = reinterpret_cast<EmulatedAHardwareBuffer*>(buffer);
    return ahb->getResourceId();
}

EGLNativeWindowType EmulatedANativeWindowHelper::createNativeWindowForTesting(Gralloc* gralloc,
                                                                              uint32_t width,
                                                                              uint32_t height) {
    std::vector<std::unique_ptr<EmulatedAHardwareBuffer>> buffers;
    for (int i = 0; i < 3; i++) {
        AHardwareBuffer* ahb = nullptr;
        if (gralloc->allocate(width, height, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM, -1, &ahb) != 0) {
            mesa_loge("Failed to allocate gralloc buffer.");
            return nullptr;
        }
        buffers.emplace_back(reinterpret_cast<EmulatedAHardwareBuffer*>(ahb));
    }
    return reinterpret_cast<EGLNativeWindowType>(
        new EmulatedANativeWindow(width, height, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM, std::move(buffers)));
}

ANativeWindowHelper* createPlatformANativeWindowHelper() {
    return new EmulatedANativeWindowHelper();
}

}  // namespace gfxstream
