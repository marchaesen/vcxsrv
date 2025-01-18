/*
 * Copyright 2023 Google
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <inttypes.h>
#include <stddef.h>

#if defined(__Fuchsia__)
    typedef void* address_space_handle_t;
#else
    typedef int address_space_handle_t;
#endif

enum AddressSpaceSubdeviceType {
    NoSubdevice = -1,
    Graphics = 0,
    Media = 1,
    HostMemoryAllocator = 5,
    SharedSlotsHostMemoryAllocator = 6,
    VirtioGpuGraphics = 10,
};

// We also expose the ping info struct that is shared between host and guest.
struct address_space_ping {
    uint64_t offset;
    uint64_t size;
    uint64_t metadata;
    uint32_t resourceId;
    uint32_t wait_fd;
    uint32_t wait_flags;
    uint32_t direction;
};

// typedef/struct to abstract over goldfish vs virtio-gpu implementations
typedef address_space_handle_t (*address_space_open_t)(void);
typedef void (*address_space_close_t)(address_space_handle_t);

typedef bool (*address_space_allocate_t)(
    address_space_handle_t, size_t size, uint64_t* phys_addr, uint64_t* offset);
typedef bool (*address_space_free_t)(
    address_space_handle_t, uint64_t offset);

typedef bool (*address_space_claim_shared_t)(
    address_space_handle_t, uint64_t offset, uint64_t size);
typedef bool (*address_space_unclaim_shared_t)(
    address_space_handle_t, uint64_t offset);

// pgoff is the offset into the page to return in the result
typedef void* (*address_space_map_t)(
    address_space_handle_t, uint64_t offset, uint64_t size, uint64_t pgoff);
typedef void (*address_space_unmap_t)(void* ptr, uint64_t size);

typedef bool (*address_space_set_subdevice_type_t)(
    address_space_handle_t, AddressSpaceSubdeviceType type, address_space_handle_t*);
typedef bool (*address_space_ping_t)(
    address_space_handle_t, struct address_space_ping*);

struct address_space_ops {
    address_space_open_t open;
    address_space_close_t close;
    address_space_claim_shared_t claim_shared;
    address_space_unclaim_shared_t unclaim_shared;
    address_space_map_t map;
    address_space_unmap_t unmap;
    address_space_set_subdevice_type_t set_subdevice_type;
    address_space_ping_t ping;
};
