/* Copied from libion:
 *  https://android.googlesource.com/platform/system/memory/libion/
 *
 * This header was automatically generated from a Linux kernel header
 * of the same name, to make information necessary for userspace to
 * call into the kernel available to libc.  It contains only constants,
 * structures, and macros generated from the original header, and thus,
 * contains no copyrightable information.
 */

#ifndef _UAPI_LINUX_ION_NEW_H
#define _UAPI_LINUX_ION_NEW_H
#include <linux/ioctl.h>
#include <linux/types.h>
#define ION_NUM_HEAP_IDS (sizeof(unsigned int) * 8)
enum ion_heap_type_ext {
    ION_HEAP_TYPE_CUSTOM_EXT = 16,
    ION_HEAP_TYPE_MAX = 31,
};
enum ion_heap_id {
    ION_HEAP_SYSTEM = (1 << ION_HEAP_TYPE_SYSTEM),
    ION_HEAP_DMA_START = (ION_HEAP_SYSTEM << 1),
    ION_HEAP_DMA_END = (ION_HEAP_DMA_START << 7),
    ION_HEAP_CUSTOM_START = (ION_HEAP_DMA_END << 1),
    ION_HEAP_CUSTOM_END = (ION_HEAP_CUSTOM_START << 22),
};
#define ION_NUM_MAX_HEAPS (32)
struct ion_new_allocation_data {
    __u64 len;
    __u32 heap_id_mask;
    __u32 flags;
    __u32 fd;
    __u32 unused;
};
#define MAX_HEAP_NAME 32
struct ion_heap_data {
    char name[MAX_HEAP_NAME];
    __u32 type;
    __u32 heap_id;
    __u32 reserved0;
    __u32 reserved1;
    __u32 reserved2;
};
struct ion_heap_query {
    __u32 cnt;
    __u32 reserved0;
    __u64 heaps;
    __u32 reserved1;
    __u32 reserved2;
};
#define ION_IOC_MAGIC 'I'
#define ION_IOC_NEW_ALLOC _IOWR(ION_IOC_MAGIC, 0, struct ion_new_allocation_data)
#define ION_IOC_HEAP_QUERY _IOWR(ION_IOC_MAGIC, 8, struct ion_heap_query)
#define ION_IOC_ABI_VERSION _IOR(ION_IOC_MAGIC, 9, __u32)
#endif
