/*
 * Copyright 2019 Google
 * SPDX-License-Identifier: MIT
 */

#include "goldfish_address_space.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <memory>

#include "util/log.h"

// See virgl_hw.h and p_defines.h
#define VIRGL_FORMAT_R8_UNORM 64
#define VIRGL_BIND_CUSTOM (1 << 17)
#define PIPE_BUFFER 0

#ifdef PAGE_SIZE
constexpr size_t kPageSize = PAGE_SIZE;
#else
static const size_t kPageSize = getpagesize();
#endif

namespace {

struct goldfish_address_space_allocate_block {
    __u64 size;
    __u64 offset;
    __u64 phys_addr;
};

struct goldfish_address_space_claim_shared {
    __u64 offset;
    __u64 size;
};

#define GOLDFISH_ADDRESS_SPACE_IOCTL_MAGIC		'G'
#define GOLDFISH_ADDRESS_SPACE_IOCTL_OP(OP, T)		_IOWR(GOLDFISH_ADDRESS_SPACE_IOCTL_MAGIC, OP, T)
#define GOLDFISH_ADDRESS_SPACE_IOCTL_ALLOCATE_BLOCK	GOLDFISH_ADDRESS_SPACE_IOCTL_OP(10, struct goldfish_address_space_allocate_block)
#define GOLDFISH_ADDRESS_SPACE_IOCTL_DEALLOCATE_BLOCK	GOLDFISH_ADDRESS_SPACE_IOCTL_OP(11, __u64)
#define GOLDFISH_ADDRESS_SPACE_IOCTL_PING		GOLDFISH_ADDRESS_SPACE_IOCTL_OP(12, struct address_space_ping)
#define GOLDFISH_ADDRESS_SPACE_IOCTL_CLAIM_SHARED		GOLDFISH_ADDRESS_SPACE_IOCTL_OP(13, struct goldfish_address_space_claim_shared)
#define GOLDFISH_ADDRESS_SPACE_IOCTL_UNCLAIM_SHARED		GOLDFISH_ADDRESS_SPACE_IOCTL_OP(14, __u64)

const char GOLDFISH_ADDRESS_SPACE_DEVICE_NAME[] = "/dev/goldfish_address_space";

const int HOST_MEMORY_ALLOCATOR_COMMAND_ALLOCATE_ID = 1;
const int HOST_MEMORY_ALLOCATOR_COMMAND_UNALLOCATE_ID = 2;

int create_address_space_fd()
{
    return ::open(GOLDFISH_ADDRESS_SPACE_DEVICE_NAME, O_RDWR);
}

long ioctl_allocate(int fd, struct goldfish_address_space_allocate_block *request)
{
    return ::ioctl(fd, GOLDFISH_ADDRESS_SPACE_IOCTL_ALLOCATE_BLOCK, request);
}

long ioctl_deallocate(int fd, uint64_t offset)
{
    return ::ioctl(fd, GOLDFISH_ADDRESS_SPACE_IOCTL_DEALLOCATE_BLOCK, &offset);
}

long ioctl_ping(int fd, struct address_space_ping *request)
{
    return ::ioctl(fd, GOLDFISH_ADDRESS_SPACE_IOCTL_PING, request);
}

long set_address_space_subdevice_type(int fd, uint64_t type)
{
    struct address_space_ping request;
    ::memset(&request, 0, sizeof(request));
    request.resourceId = sizeof(request);
    request.metadata = type;

    long ret = ioctl_ping(fd, &request);
    if (ret) {
        return ret;
    }

    return request.metadata;
}

long ioctl_claim_shared(int fd, struct goldfish_address_space_claim_shared *request)
{
    return ::ioctl(fd, GOLDFISH_ADDRESS_SPACE_IOCTL_CLAIM_SHARED, request);
}

long ioctl_unclaim_shared(int fd, uint64_t offset)
{
    return ::ioctl(fd, GOLDFISH_ADDRESS_SPACE_IOCTL_UNCLAIM_SHARED, &offset);
}

}  // namespace

GoldfishAddressSpaceBlockProvider::GoldfishAddressSpaceBlockProvider(GoldfishAddressSpaceSubdeviceType subdevice)
  : m_handle(create_address_space_fd())
{
    if ((subdevice != GoldfishAddressSpaceSubdeviceType::NoSubdevice) && is_opened()) {
        const long ret = set_address_space_subdevice_type(m_handle, subdevice);
        if (ret != 0 && ret != subdevice) {  // TODO: retire the 'ret != subdevice' check
            mesa_loge("%s: set_address_space_subdevice_type failed for device_type=%lu, ret=%ld",
                      __func__, static_cast<unsigned long>(subdevice), ret);
            close();
        }
    }
}

GoldfishAddressSpaceBlockProvider::~GoldfishAddressSpaceBlockProvider()
{
    if (is_opened()) {
        ::close(m_handle);
    }
}

bool GoldfishAddressSpaceBlockProvider::is_opened() const
{
    return m_handle >= 0;
}

void GoldfishAddressSpaceBlockProvider::close()
{
    if (is_opened()) {
        ::close(m_handle);
        m_handle = -1;
    }
}

address_space_handle_t GoldfishAddressSpaceBlockProvider::release()
{
    address_space_handle_t handle = m_handle;
    m_handle = -1;
    return handle;
}

void GoldfishAddressSpaceBlockProvider::closeHandle(address_space_handle_t handle)
{
    ::close(handle);
}

GoldfishAddressSpaceBlock::GoldfishAddressSpaceBlock()
    : m_handle(-1)
    , m_mmaped_ptr(NULL)
    , m_phys_addr(0)
    , m_host_addr(0)
    , m_offset(0)
    , m_size(0) {}

GoldfishAddressSpaceBlock::~GoldfishAddressSpaceBlock()
{
    destroy();
}

GoldfishAddressSpaceBlock &GoldfishAddressSpaceBlock::operator=(const GoldfishAddressSpaceBlock &rhs)
{
    m_mmaped_ptr = rhs.m_mmaped_ptr;
    m_phys_addr = rhs.m_phys_addr;
    m_host_addr = rhs.m_host_addr;
    m_offset = rhs.m_offset;
    m_size = rhs.m_size;
    m_handle = rhs.m_handle;

    return *this;
}

bool GoldfishAddressSpaceBlock::allocate(GoldfishAddressSpaceBlockProvider *provider, size_t size)
{
    destroy();

    if (!provider->is_opened()) {
        return false;
    }

    struct goldfish_address_space_allocate_block request;
    ::memset(&request, 0, sizeof(request));
    request.size = size;

    long res = ioctl_allocate(provider->m_handle, &request);
    if (res) {
        return false;
    } else {
        m_phys_addr = request.phys_addr;
        m_offset = request.offset;
        m_size = request.size;
        m_handle = provider->m_handle;
        m_is_shared_mapping = false;

        return true;
    }
}

bool GoldfishAddressSpaceBlock::claimShared(GoldfishAddressSpaceBlockProvider *provider, uint64_t offset, uint64_t size)
{
    destroy();

    if (!provider->is_opened()) {
        return false;
    }

    struct goldfish_address_space_claim_shared request;
    request.offset = offset;
    request.size = size;
    long res = ioctl_claim_shared(provider->m_handle, &request);

    if (res) {
        return false;
    }

    m_offset = offset;
    m_size = size;
    m_handle = provider->m_handle;
    m_is_shared_mapping = true;

    return true;
}

uint64_t GoldfishAddressSpaceBlock::physAddr() const
{
    return m_phys_addr;
}

uint64_t GoldfishAddressSpaceBlock::hostAddr() const
{
    return m_host_addr;
}

void *GoldfishAddressSpaceBlock::mmap(uint64_t host_addr)
{
    if (m_size == 0) {
        mesa_loge("%s: called with zero size\n", __func__);
        return NULL;
    }
    if (m_mmaped_ptr) {
        mesa_loge("'mmap' called for an already mmaped address block");
        ::abort();
    }

    void *result;
    const int res = memoryMap(NULL, m_size, m_handle, m_offset, &result);
    if (res) {
        mesa_loge(
            "%s: host memory map failed with size 0x%llx "
            "off 0x%llx errno %d\n",
            __func__, (unsigned long long)m_size, (unsigned long long)m_offset, res);
        return NULL;
    } else {
        m_mmaped_ptr = result;
        m_host_addr = host_addr;
        return guestPtr();
    }
}

void *GoldfishAddressSpaceBlock::guestPtr() const
{
    return reinterpret_cast<char *>(m_mmaped_ptr) + (m_host_addr & (kPageSize - 1));
}

void GoldfishAddressSpaceBlock::destroy()
{
    if (m_mmaped_ptr && m_size) {
        memoryUnmap(m_mmaped_ptr, m_size);
        m_mmaped_ptr = NULL;
    }

    if (m_size) {
        long res = -EINVAL;

        if (m_is_shared_mapping) {
            res = ioctl_unclaim_shared(m_handle, m_offset);
            if (res) {
                mesa_loge("ioctl_unclaim_shared failed, res=%ld", res);
                ::abort();
            }
        } else {
            res = ioctl_deallocate(m_handle, m_offset);
            if (res) {
                mesa_loge("ioctl_deallocate failed, res=%ld", res);
                ::abort();
            }
        }

        m_is_shared_mapping = false;

        m_phys_addr = 0;
        m_host_addr = 0;
        m_offset = 0;
        m_size = 0;
    }
}

void GoldfishAddressSpaceBlock::release()
{
    m_handle = -1;
    m_mmaped_ptr = NULL;
    m_phys_addr = 0;
    m_host_addr = 0;
    m_offset = 0;
    m_size = 0;
}

int GoldfishAddressSpaceBlock::memoryMap(void *addr,
                                         size_t len,
                                         address_space_handle_t fd,
                                         uint64_t off,
                                         void** dst) {
    void* ptr = ::mmap64(addr, len, PROT_WRITE, MAP_SHARED, fd, off);
    if (MAP_FAILED == ptr) {
        return errno;
    } else {
        *dst = ptr;
        return 0;
    }
}

void GoldfishAddressSpaceBlock::memoryUnmap(void *ptr, size_t size)
{
    ::munmap(ptr, size);
}

GoldfishAddressSpaceHostMemoryAllocator::GoldfishAddressSpaceHostMemoryAllocator(bool useSharedSlots)
  : m_provider(useSharedSlots
        ? GoldfishAddressSpaceSubdeviceType::SharedSlotsHostMemoryAllocator
        : GoldfishAddressSpaceSubdeviceType::HostMemoryAllocator),
    m_useSharedSlots(useSharedSlots)
{}

bool GoldfishAddressSpaceHostMemoryAllocator::is_opened() const { return m_provider.is_opened(); }

long GoldfishAddressSpaceHostMemoryAllocator::hostMalloc(GoldfishAddressSpaceBlock *block, size_t size)
{
    if (size == 0) {
        return -EINVAL;
    }
    if (block->size() > 0) {
        return -EINVAL;
    }
    if (!m_provider.is_opened()) {
        return -ENODEV;
    }

    struct address_space_ping request;
    if (m_useSharedSlots) {
        // shared memory slots are supported
        ::memset(&request, 0, sizeof(request));
        request.resourceId = sizeof(request);
        request.size = size;
        request.metadata = HOST_MEMORY_ALLOCATOR_COMMAND_ALLOCATE_ID;

        long ret = ioctl_ping(m_provider.m_handle, &request);
        if (ret) {
            return ret;
        }
        ret = static_cast<long>(request.metadata);
        if (ret) {
            return ret;
        }

        block->claimShared(&m_provider, request.offset, request.size);
    } else {
        // shared memory slots are not supported
        if (!block->allocate(&m_provider, size)) {
            return -ENOMEM;
        }

        ::memset(&request, 0, sizeof(request));
        request.resourceId = sizeof(request);
        request.offset = block->offset();
        request.size = block->size();
        request.metadata = HOST_MEMORY_ALLOCATOR_COMMAND_ALLOCATE_ID;

        long ret = ioctl_ping(m_provider.m_handle, &request);
        if (ret) {
            return ret;
        }
        ret = static_cast<long>(request.metadata);
        if (ret) {
            return ret;
        }
    }

    block->mmap(0);
    return 0;
}

void GoldfishAddressSpaceHostMemoryAllocator::hostFree(GoldfishAddressSpaceBlock *block)
{
    if (block->size() == 0) {
        return;
    }

    if (!m_provider.is_opened()) {
        mesa_loge("%s: device is not available", __func__);
        ::abort();
    }

    if (block->guestPtr()) {
        struct address_space_ping request;
        ::memset(&request, 0, sizeof(request));
        request.resourceId = sizeof(request);
        request.offset = block->offset();
        request.metadata = HOST_MEMORY_ALLOCATOR_COMMAND_UNALLOCATE_ID;

        const long ret = ioctl_ping(m_provider.m_handle, &request);
        if (ret) {
            mesa_loge("%s: ioctl_ping failed, ret=%ld", __func__, ret);
            ::abort();
        }
    }

    block->replace(NULL);
}

address_space_handle_t goldfish_address_space_open() {
    return ::open(GOLDFISH_ADDRESS_SPACE_DEVICE_NAME, O_RDWR);
}

void goldfish_address_space_close(address_space_handle_t handle) {
    ::close(handle);
}

bool goldfish_address_space_allocate(
    address_space_handle_t handle,
    size_t size, uint64_t* phys_addr, uint64_t* offset) {

    struct goldfish_address_space_allocate_block request;
    ::memset(&request, 0, sizeof(request));
    request.size = size;

    long res = ioctl_allocate(handle, &request);

    if (res) return false;

    *phys_addr = request.phys_addr;
    *offset = request.offset;
    return true;
}

bool goldfish_address_space_free(
    address_space_handle_t handle, uint64_t offset) {

    long res = ioctl_deallocate(handle, offset);

    if (res) {
        mesa_loge("ioctl_deallocate failed, res=%ld", res);
        ::abort();
    }

    return true;
}

bool goldfish_address_space_claim_shared(
    address_space_handle_t handle, uint64_t offset, uint64_t size) {

    struct goldfish_address_space_claim_shared request;
    request.offset = offset;
    request.size = size;
    long res = ioctl_claim_shared(handle, &request);

    if (res) return false;

    return true;
}

bool goldfish_address_space_unclaim_shared(
        address_space_handle_t handle, uint64_t offset) {
    long res = ioctl_unclaim_shared(handle, offset);
    if (res) {
        mesa_loge("ioctl_unclaim_shared failed, res=%ld", res);
        ::abort();
    }

    return true;
}

// pgoff is the offset into the page to return in the result
void* goldfish_address_space_map(
    address_space_handle_t handle,
    uint64_t offset, uint64_t size,
    uint64_t pgoff) {

    void* res = ::mmap64(0, size, PROT_WRITE, MAP_SHARED, handle, offset);

    if (res == MAP_FAILED) {
        mesa_loge("%s: failed to map. errno: %d\n", __func__, errno);
        return 0;
    }

    return (void*)(((char*)res) + (uintptr_t)(pgoff & (kPageSize - 1)));
}

void goldfish_address_space_unmap(void* ptr, uint64_t size) {
    void* pagePtr = (void*)(((uintptr_t)ptr) & ~(kPageSize - 1));
    ::munmap(pagePtr, size);
}

bool goldfish_address_space_set_subdevice_type(
    address_space_handle_t handle, GoldfishAddressSpaceSubdeviceType type,
    address_space_handle_t* handle_out) {
    struct address_space_ping request;
    request.metadata = (uint64_t)type;
    *handle_out = handle;
    return goldfish_address_space_ping(handle, &request);
}

bool goldfish_address_space_ping(
    address_space_handle_t handle,
    struct address_space_ping* ping) {
    long res = ioctl_ping(handle, ping);

    if (res) {
        mesa_loge("%s: ping failed: errno: %d\n", __func__, errno);
        return false;
    }

    return true;
}

void GoldfishAddressSpaceBlock::replace(GoldfishAddressSpaceBlock *other)
{
    destroy();

    if (other) {
        *this = *other;
        *other = GoldfishAddressSpaceBlock();
    }
}
