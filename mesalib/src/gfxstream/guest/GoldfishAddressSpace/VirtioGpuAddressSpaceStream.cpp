/*
 * Copyright 2023 Google
 * SPDX-License-Identifier: MIT
 */

#include "VirtioGpuAddressSpaceStream.h"

#include <errno.h>

#include "util/log.h"

static bool GetRingParamsFromCapset(enum VirtGpuCapset capset, const VirtGpuCaps& caps,
                                    uint32_t& ringSize, uint32_t& bufferSize,
                                    uint32_t& blobAlignment) {
    switch (capset) {
        case kCapsetGfxStreamVulkan:
            ringSize = caps.vulkanCapset.ringSize;
            bufferSize = caps.vulkanCapset.bufferSize;
            blobAlignment = caps.vulkanCapset.blobAlignment;
            break;
        case kCapsetGfxStreamMagma:
            ringSize = caps.magmaCapset.ringSize;
            bufferSize = caps.magmaCapset.bufferSize;
            blobAlignment = caps.magmaCapset.blobAlignment;
            break;
        case kCapsetGfxStreamGles:
            ringSize = caps.glesCapset.ringSize;
            bufferSize = caps.glesCapset.bufferSize;
            blobAlignment = caps.glesCapset.blobAlignment;
            break;
        case kCapsetGfxStreamComposer:
            ringSize = caps.composerCapset.ringSize;
            bufferSize = caps.composerCapset.bufferSize;
            blobAlignment = caps.composerCapset.blobAlignment;
            break;
        default:
            return false;
    }

    return true;
}

address_space_handle_t virtgpu_address_space_open() {
    return (address_space_handle_t)(-EINVAL);
}

void virtgpu_address_space_close(address_space_handle_t) {
    // Handle opened by VirtioGpuDevice wrapper
}

bool virtgpu_address_space_ping(address_space_handle_t, struct address_space_ping* info) {
    int ret;
    struct VirtGpuExecBuffer exec = {};
    VirtGpuDevice* instance = VirtGpuDevice::getInstance();
    struct gfxstreamContextPing ping = {};

    ping.hdr.opCode = GFXSTREAM_CONTEXT_PING;
    ping.resourceId = info->resourceId;

    exec.command = static_cast<void*>(&ping);
    exec.command_size = sizeof(ping);

    ret = instance->execBuffer(exec, nullptr);
    if (ret)
        return false;

    return true;
}

AddressSpaceStream* createVirtioGpuAddressSpaceStream(enum VirtGpuCapset capset) {
    VirtGpuResourcePtr pipe, blob;
    VirtGpuResourceMappingPtr pipeMapping, blobMapping;
    struct VirtGpuExecBuffer exec = {};
    struct VirtGpuCreateBlob blobCreate = {};
    struct gfxstreamContextCreate contextCreate = {};

    uint32_t ringSize = 0;
    uint32_t bufferSize = 0;
    uint32_t blobAlignment = 0;

    char* blobAddr, *bufferPtr;
    int ret;

    VirtGpuDevice* instance = VirtGpuDevice::getInstance();
    auto caps = instance->getCaps();

    if (!GetRingParamsFromCapset(capset, caps, ringSize, bufferSize, blobAlignment)) {
        mesa_loge("Failed to get ring parameters");
        return nullptr;
    }

    blobCreate.blobId = 0;
    blobCreate.blobMem = kBlobMemHost3d;
    blobCreate.flags = kBlobFlagMappable;
    blobCreate.size = ALIGN_POT(ringSize + bufferSize, blobAlignment);
    blob = instance->createBlob(blobCreate);
    if (!blob)
        return nullptr;

    // Context creation command
    contextCreate.hdr.opCode = GFXSTREAM_CONTEXT_CREATE;
    contextCreate.resourceId = blob->getResourceHandle();

    exec.command = static_cast<void*>(&contextCreate);
    exec.command_size = sizeof(contextCreate);

    ret = instance->execBuffer(exec, blob.get());
    if (ret)
        return nullptr;

    // Wait occurs on global timeline -- should we use context specific one?
    ret = blob->wait();
    if (ret)
        return nullptr;

    blobMapping = blob->createMapping();
    if (!blobMapping)
        return nullptr;

    blobAddr = reinterpret_cast<char*>(blobMapping->asRawPtr());

    bufferPtr = blobAddr + sizeof(struct asg_ring_storage);
    struct asg_context context = asg_context_create(blobAddr, bufferPtr, bufferSize);

    context.ring_config->transfer_mode = 1;
    context.ring_config->host_consumed_pos = 0;
    context.ring_config->guest_write_pos = 0;

    struct address_space_ops ops = {
        .open = virtgpu_address_space_open,
        .close = virtgpu_address_space_close,
        .ping = virtgpu_address_space_ping,
    };

    AddressSpaceStream* res =
        new AddressSpaceStream((address_space_handle_t)(-1), 1, context, 0, 0, ops);

    res->setMapping(blobMapping);
    res->setResourceId(contextCreate.resourceId);
    return res;
}
