/*
 * Copyright 2023 Google
 * SPDX-License-Identifier: MIT
 */

#include "GoldfishAddressSpaceStream.h"

#include "goldfish_address_space.h"
#include "util/log.h"

AddressSpaceStream* createGoldfishAddressSpaceStream(size_t ignored_bufSize) {
    // Ignore incoming ignored_bufSize
    (void)ignored_bufSize;

    auto handle = goldfish_address_space_open();
    address_space_handle_t child_device_handle;

    if (!goldfish_address_space_set_subdevice_type(handle, GoldfishAddressSpaceSubdeviceType::Graphics, &child_device_handle)) {
        mesa_loge("AddressSpaceStream::create failed (initial device create)\n");
        goldfish_address_space_close(handle);
        return nullptr;
    }

    struct address_space_ping request;
    request.metadata = ASG_GET_RING;
    if (!goldfish_address_space_ping(child_device_handle, &request)) {
        mesa_loge("AddressSpaceStream::create failed (get ring)\n");
        goldfish_address_space_close(child_device_handle);
        return nullptr;
    }

    uint64_t ringOffset = request.metadata;

    request.metadata = ASG_GET_BUFFER;
    if (!goldfish_address_space_ping(child_device_handle, &request)) {
        mesa_loge("AddressSpaceStream::create failed (get buffer)\n");
        goldfish_address_space_close(child_device_handle);
        return nullptr;
    }

    uint64_t bufferOffset = request.metadata;
    uint64_t bufferSize = request.size;

    if (!goldfish_address_space_claim_shared(
        child_device_handle, ringOffset, sizeof(asg_ring_storage))) {
        mesa_loge("AddressSpaceStream::create failed (claim ring storage)\n");
        goldfish_address_space_close(child_device_handle);
        return nullptr;
    }

    if (!goldfish_address_space_claim_shared(
        child_device_handle, bufferOffset, bufferSize)) {
        mesa_loge("AddressSpaceStream::create failed (claim buffer storage)\n");
        goldfish_address_space_unclaim_shared(child_device_handle, ringOffset);
        goldfish_address_space_close(child_device_handle);
        return nullptr;
    }

    char* ringPtr = (char*)goldfish_address_space_map(
        child_device_handle, ringOffset, sizeof(struct asg_ring_storage));

    if (!ringPtr) {
        mesa_loge("AddressSpaceStream::create failed (map ring storage)\n");
        goldfish_address_space_unclaim_shared(child_device_handle, bufferOffset);
        goldfish_address_space_unclaim_shared(child_device_handle, ringOffset);
        goldfish_address_space_close(child_device_handle);
        return nullptr;
    }

    char* bufferPtr = (char*)goldfish_address_space_map(
        child_device_handle, bufferOffset, bufferSize);

    if (!bufferPtr) {
        mesa_loge("AddressSpaceStream::create failed (map buffer storage)\n");
        goldfish_address_space_unmap(ringPtr, sizeof(struct asg_ring_storage));
        goldfish_address_space_unclaim_shared(child_device_handle, bufferOffset);
        goldfish_address_space_unclaim_shared(child_device_handle, ringOffset);
        goldfish_address_space_close(child_device_handle);
        return nullptr;
    }

    struct asg_context context =
        asg_context_create(
            ringPtr, bufferPtr, bufferSize);

    request.metadata = ASG_SET_VERSION;
    request.size = 1; // version 1

    if (!goldfish_address_space_ping(child_device_handle, &request)) {
        mesa_loge("AddressSpaceStream::create failed (get buffer)\n");
        goldfish_address_space_unmap(bufferPtr, bufferSize);
        goldfish_address_space_unmap(ringPtr, sizeof(struct asg_ring_storage));
        goldfish_address_space_unclaim_shared(child_device_handle, bufferOffset);
        goldfish_address_space_unclaim_shared(child_device_handle, ringOffset);
        goldfish_address_space_close(child_device_handle);
        return nullptr;
    }

    uint32_t version = request.size;

    context.ring_config->transfer_mode = 1;
    context.ring_config->host_consumed_pos = 0;
    context.ring_config->guest_write_pos = 0;

    struct address_space_ops ops = {
        .open = goldfish_address_space_open,
        .close = goldfish_address_space_close,
        .claim_shared = goldfish_address_space_claim_shared,
        .unclaim_shared = goldfish_address_space_unclaim_shared,
        .map = goldfish_address_space_map,
        .unmap = goldfish_address_space_unmap,
        .set_subdevice_type = goldfish_address_space_set_subdevice_type,
        .ping = goldfish_address_space_ping,
    };

    AddressSpaceStream* res = new AddressSpaceStream(child_device_handle, version, context,
                                                     ringOffset, bufferOffset, ops);

    return res;
}
