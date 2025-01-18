/*
 * Copyright 2022 Google
 * SPDX-License-Identifier: MIT
 */

#ifndef VIRTGPU_GFXSTREAM_PROTOCOL_H
#define VIRTGPU_GFXSTREAM_PROTOCOL_H

#include <stdint.h>

// See definitions in rutabaga_gfx_ffi.h
#define VIRTGPU_CAPSET_VIRGL 1
#define VIRTGPU_CAPSET_VIRGL2 2
#define VIRTGPU_CAPSET_GFXSTREAM_VULKAN 3
#define VIRTGPU_CAPSET_VENUS 4
#define VIRTGPU_CAPSET_CROSS_DOMAIN 5
#define VIRTGPU_CAPSET_DRM 6
#define VIRTGPU_CAPSET_GFXSTREAM_MAGMA 7
#define VIRTGPU_CAPSET_GFXSTREAM_GLES 8
#define VIRTGPU_CAPSET_GFXSTREAM_COMPOSER 9

// Address Space Graphics contexts
#define GFXSTREAM_CONTEXT_CREATE                0x1001
#define GFXSTREAM_CONTEXT_PING                  0x1002
#define GFXSTREAM_CONTEXT_PING_WITH_RESPONSE    0x1003

// Native Sync FD
#define GFXSTREAM_CREATE_EXPORT_SYNC            0x9000
#define GFXSTREAM_CREATE_IMPORT_SYNC            0x9001

// Vulkan Sync
#define GFXSTREAM_CREATE_EXPORT_SYNC_VK         0xa000
#define GFXSTREAM_CREATE_IMPORT_SYNC_VK         0xa001
#define GFXSTREAM_CREATE_QSRI_EXPORT_VK         0xa002
#define GFXSTREAM_RESOURCE_CREATE_3D            0xa003
#define GFXSTREAM_ACQUIRE_SYNC                  0xa004

// clang-format off
// A placeholder command to ensure virtio-gpu completes
#define GFXSTREAM_PLACEHOLDER_COMMAND_VK        0xf002
// clang-format on

struct gfxstreamHeader {
    uint32_t opCode;
};

struct gfxstreamContextCreate {
    struct gfxstreamHeader hdr;
    uint32_t resourceId;
};

struct gfxstreamContextPing {
    struct gfxstreamHeader hdr;
    uint32_t resourceId;
};

struct gfxstreamCreateExportSync {
    struct gfxstreamHeader hdr;
    uint32_t syncHandleLo;
    uint32_t syncHandleHi;
};

struct gfxstreamCreateExportSyncVK {
    struct gfxstreamHeader hdr;
    uint32_t deviceHandleLo;
    uint32_t deviceHandleHi;
    uint32_t fenceHandleLo;
    uint32_t fenceHandleHi;
};

struct gfxstreamCreateQSRIExportVK {
    struct gfxstreamHeader hdr;
    uint32_t imageHandleLo;
    uint32_t imageHandleHi;
};

struct gfxstreamPlaceholderCommandVk {
    struct gfxstreamHeader hdr;
    uint32_t pad;
    uint32_t padding;
};

struct gfxstreamResourceCreate3d {
    struct gfxstreamHeader hdr;
    uint32_t target;
    uint32_t format;
    uint32_t bind;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t arraySize;
    uint32_t lastLevel;
    uint32_t nrSamples;
    uint32_t flags;
    uint32_t pad;
    uint64_t blobId;
};

struct gfxstreamAcquireSync {
    struct gfxstreamHeader hdr;
    uint32_t padding;
    uint64_t syncId;
};

struct vulkanCapset {
    uint32_t protocolVersion;

    // ASG Ring Parameters
    uint32_t ringSize;
    uint32_t bufferSize;

    uint32_t colorBufferMemoryIndex;
    uint32_t deferredMapping;
    uint32_t blobAlignment;
    uint32_t noRenderControlEnc;
    uint32_t alwaysBlob;
    uint32_t externalSync;
    uint32_t virglSupportedFormats[16];
    uint32_t vulkanBatchedDescriptorSetUpdate;
};

struct magmaCapset {
    uint32_t protocolVersion;
    // ASG Ring Parameters
    uint32_t ringSize;
    uint32_t bufferSize;
    uint32_t blobAlignment;
};

struct glesCapset {
    uint32_t protocolVersion;
    // ASG Ring Parameters
    uint32_t ringSize;
    uint32_t bufferSize;
    uint32_t blobAlignment;
};

struct composerCapset {
    uint32_t protocolVersion;
    // ASG Ring Parameters
    uint32_t ringSize;
    uint32_t bufferSize;
    uint32_t blobAlignment;
};

#endif
