/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */

#ifndef ANDROID_INCLUDE_HARDWARE_GOLDFISH_ADDRESS_SPACE_H
#define ANDROID_INCLUDE_HARDWARE_GOLDFISH_ADDRESS_SPACE_H

#include <inttypes.h>
#include <stddef.h>

#ifdef __Fuchsia__
#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#endif

#include "address_space.h"

class GoldfishAddressSpaceBlock;
class GoldfishAddressSpaceHostMemoryAllocator;

using GoldfishAddressSpaceSubdeviceType = AddressSpaceSubdeviceType;

class GoldfishAddressSpaceBlockProvider {
public:
    GoldfishAddressSpaceBlockProvider(GoldfishAddressSpaceSubdeviceType subdevice);
    ~GoldfishAddressSpaceBlockProvider();

private:
    GoldfishAddressSpaceBlockProvider(const GoldfishAddressSpaceBlockProvider &rhs);
    GoldfishAddressSpaceBlockProvider &operator=(const GoldfishAddressSpaceBlockProvider &rhs);

    bool is_opened() const;
    void close();
    address_space_handle_t release();
    static void closeHandle(address_space_handle_t handle);

#ifdef __Fuchsia__
    ::fidl::WireSyncClient<fuchsia_hardware_goldfish::AddressSpaceDevice>
        m_device;
    ::fidl::WireSyncClient<fuchsia_hardware_goldfish::AddressSpaceChildDriver>
        m_child_driver;
#else // __Fuchsia__
    address_space_handle_t m_handle;
#endif // !__Fuchsia__

    friend class GoldfishAddressSpaceBlock;
    friend class GoldfishAddressSpaceHostMemoryAllocator;
};

class GoldfishAddressSpaceBlock {
public:
    GoldfishAddressSpaceBlock();
    ~GoldfishAddressSpaceBlock();

    bool allocate(GoldfishAddressSpaceBlockProvider *provider, size_t size);
    bool claimShared(GoldfishAddressSpaceBlockProvider *provider, uint64_t offset, uint64_t size);
    uint64_t physAddr() const;
    uint64_t hostAddr() const;
    uint64_t offset() const { return m_offset; }
    size_t size() const { return m_size; }
    void *mmap(uint64_t opaque);
    void *guestPtr() const;
    void replace(GoldfishAddressSpaceBlock *other);
    void release();
    static int memoryMap(void *addr, size_t len, address_space_handle_t fd, uint64_t off, void** dst);
    static void memoryUnmap(void *ptr, size_t size);

private:
    void destroy();
    GoldfishAddressSpaceBlock &operator=(const GoldfishAddressSpaceBlock &);

#ifdef __Fuchsia__
    ::fidl::WireSyncClient<fuchsia_hardware_goldfish::AddressSpaceChildDriver>*
        m_driver;
    uint32_t  m_vmo;
#else // __Fuchsia__
    address_space_handle_t m_handle;
#endif // !__Fuchsia__

    void     *m_mmaped_ptr;
    uint64_t  m_phys_addr;
    uint64_t  m_host_addr;
    uint64_t  m_offset;
    uint64_t  m_size;
    bool      m_is_shared_mapping;
};

class GoldfishAddressSpaceHostMemoryAllocator {
public:
    GoldfishAddressSpaceHostMemoryAllocator(bool useSharedSlots);

    long hostMalloc(GoldfishAddressSpaceBlock *block, size_t size);
    void hostFree(GoldfishAddressSpaceBlock *block);

    bool is_opened() const;
    address_space_handle_t release() { return m_provider.release(); }
    static void closeHandle(address_space_handle_t handle) { GoldfishAddressSpaceBlockProvider::closeHandle(handle); }

private:
    GoldfishAddressSpaceBlockProvider m_provider;
    bool                              m_useSharedSlots;
};

// Convenience functions that run address space driver api without wrapping in
// a class. Useful for when a client wants to manage the driver handle directly
// (e.g., mmaping() more than one region associated with a single handle will
// require different lifetime expectations versus GoldfishAddressSpaceBlock).

address_space_handle_t goldfish_address_space_open();
void goldfish_address_space_close(address_space_handle_t);

bool goldfish_address_space_allocate(
    address_space_handle_t, size_t size, uint64_t* phys_addr, uint64_t* offset);
bool goldfish_address_space_free(
    address_space_handle_t, uint64_t offset);

bool goldfish_address_space_claim_shared(
    address_space_handle_t, uint64_t offset, uint64_t size);
bool goldfish_address_space_unclaim_shared(
    address_space_handle_t, uint64_t offset);

// pgoff is the offset into the page to return in the result
void* goldfish_address_space_map(
    address_space_handle_t, uint64_t offset, uint64_t size, uint64_t pgoff = 0);
void goldfish_address_space_unmap(void* ptr, uint64_t size);

bool goldfish_address_space_set_subdevice_type(address_space_handle_t, GoldfishAddressSpaceSubdeviceType type, address_space_handle_t*);
bool goldfish_address_space_ping(address_space_handle_t, struct address_space_ping*);

#endif  // #ifndef ANDROID_INCLUDE_HARDWARE_GOLDFISH_ADDRESS_SPACE_H
