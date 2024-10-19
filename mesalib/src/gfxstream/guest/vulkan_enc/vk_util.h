/*
 * Copyright 2017 Intel
 * SPDX-License-Identifier: MIT
 */
#ifndef VK_UTIL_H
#define VK_UTIL_H

/* common inlines and macros for vulkan drivers */

#include <stdlib.h>
#include <vulkan/vulkan.h>

#include "vk_struct_id.h"

namespace {  // anonymous

struct vk_struct_common {
    VkStructureType sType;
    struct vk_struct_common* pNext;
};

struct vk_struct_chain_iterator {
    vk_struct_common* value;
};

#define vk_foreach_struct(__iter, __start)                                              \
    for (struct vk_struct_common* __iter = (struct vk_struct_common*)(__start); __iter; \
         __iter = __iter->pNext)

#define vk_foreach_struct_const(__iter, __start)                                            \
    for (const struct vk_struct_common* __iter = (const struct vk_struct_common*)(__start); \
         __iter; __iter = __iter->pNext)

/**
 * A wrapper for a Vulkan output array. A Vulkan output array is one that
 * follows the convention of the parameters to
 * vkGetPhysicalDeviceQueueFamilyProperties().
 *
 * Example Usage:
 *
 *    VkResult
 *    vkGetPhysicalDeviceQueueFamilyProperties(
 *       VkPhysicalDevice           physicalDevice,
 *       uint32_t*                  pQueueFamilyPropertyCount,
 *       VkQueueFamilyProperties*   pQueueFamilyProperties)
 *    {
 *       VK_OUTARRAY_MAKE(props, pQueueFamilyProperties,
 *                         pQueueFamilyPropertyCount);
 *
 *       vk_outarray_append(&props, p) {
 *          p->queueFlags = ...;
 *          p->queueCount = ...;
 *       }
 *
 *       vk_outarray_append(&props, p) {
 *          p->queueFlags = ...;
 *          p->queueCount = ...;
 *       }
 *
 *       return vk_outarray_status(&props);
 *    }
 */
struct __vk_outarray {
    /** May be null. */
    void* data;

    /**
     * Capacity, in number of elements. Capacity is unlimited (UINT32_MAX) if
     * data is null.
     */
    uint32_t cap;

    /**
     * Count of elements successfully written to the array. Every write is
     * considered successful if data is null.
     */
    uint32_t* filled_len;

    /**
     * Count of elements that would have been written to the array if its
     * capacity were sufficient. Vulkan functions often return VK_INCOMPLETE
     * when `*filled_len < wanted_len`.
     */
    uint32_t wanted_len;
};

static inline void __vk_outarray_init(struct __vk_outarray* a, void* data, uint32_t* len) {
    a->data = data;
    a->cap = *len;
    a->filled_len = len;
    *a->filled_len = 0;
    a->wanted_len = 0;

    if (a->data == NULL) a->cap = UINT32_MAX;
}

static inline VkResult __vk_outarray_status(const struct __vk_outarray* a) {
    if (*a->filled_len < a->wanted_len)
        return VK_INCOMPLETE;
    else
        return VK_SUCCESS;
}

static inline void* __vk_outarray_next(struct __vk_outarray* a, size_t elem_size) {
    void* p = NULL;

    a->wanted_len += 1;

    if (*a->filled_len >= a->cap) return NULL;

    if (a->data != NULL) p = ((uint8_t*)a->data) + (*a->filled_len) * elem_size;

    *a->filled_len += 1;

    return p;
}

#define vk_outarray(elem_t)        \
    struct {                       \
        struct __vk_outarray base; \
        elem_t meta[];             \
    }

#define vk_outarray_typeof_elem(a) __typeof__((a)->meta[0])
#define vk_outarray_sizeof_elem(a) sizeof((a)->meta[0])

#define vk_outarray_init(a, data, len) __vk_outarray_init(&(a)->base, (data), (len))

#define VK_OUTARRAY_MAKE(name, data, len)    \
    vk_outarray(__typeof__((data)[0])) name; \
    vk_outarray_init(&name, (data), (len))

#define VK_OUTARRAY_MAKE_TYPED(type, name, data, len) \
    vk_outarray(type) name;                           \
    vk_outarray_init(&name, (data), (len))

#define vk_outarray_status(a) __vk_outarray_status(&(a)->base)

#define vk_outarray_next(a) vk_outarray_next_typed(vk_outarray_typeof_elem(a), a)
#define vk_outarray_next_typed(type, a) \
    ((type*)__vk_outarray_next(&(a)->base, vk_outarray_sizeof_elem(a)))

/**
 * Append to a Vulkan output array.
 *
 * This is a block-based macro. For example:
 *
 *    vk_outarray_append(&a, elem) {
 *       elem->foo = ...;
 *       elem->bar = ...;
 *    }
 *
 * The array `a` has type `vk_outarray(elem_t) *`. It is usually declared with
 * VK_OUTARRAY_MAKE(). The variable `elem` is block-scoped and has type
 * `elem_t *`.
 *
 * The macro unconditionally increments the array's `wanted_len`. If the array
 * is not full, then the macro also increment its `filled_len` and then
 * executes the block. When the block is executed, `elem` is non-null and
 * points to the newly appended element.
 */
#define vk_outarray_append(a, elem) \
    for (vk_outarray_typeof_elem(a)* elem = vk_outarray_next(a); elem != NULL; elem = NULL)

#define vk_outarray_append_typed(type, a, elem) \
    for (type* elem = vk_outarray_next_typed(type, a); elem != NULL; elem = NULL)

static inline void* __vk_find_struct(void* start, VkStructureType sType) {
    vk_foreach_struct(s, start) {
        if (s->sType == sType) return s;
    }

    return NULL;
}

template <class T, class H>
T* vk_find_struct(H* head) {
    (void)vk_get_vk_struct_id<H>::id;
    return static_cast<T*>(__vk_find_struct(static_cast<void*>(head), vk_get_vk_struct_id<T>::id));
}

template <class T, class H>
const T* vk_find_struct(const H* head) {
    (void)vk_get_vk_struct_id<H>::id;
    return static_cast<const T*>(__vk_find_struct(const_cast<void*>(static_cast<const void*>(head)),
                                                  vk_get_vk_struct_id<T>::id));
}

#define VK_EXT_OFFSET (1000000000UL)
#define VK_ENUM_EXTENSION(__enum) \
    ((__enum) >= VK_EXT_OFFSET ? ((((__enum)-VK_EXT_OFFSET) / 1000UL) + 1) : 0)
#define VK_ENUM_OFFSET(__enum) ((__enum) >= VK_EXT_OFFSET ? ((__enum) % 1000) : (__enum))

template <class T>
T vk_make_orphan_copy(const T& vk_struct) {
    T copy = vk_struct;
    copy.pNext = NULL;
    return copy;
}

template <class T>
vk_struct_chain_iterator vk_make_chain_iterator(T* vk_struct) {
    (void)vk_get_vk_struct_id<T>::id;
    vk_struct_chain_iterator result = {reinterpret_cast<vk_struct_common*>(vk_struct)};
    return result;
}

template <class T>
void vk_append_struct(vk_struct_chain_iterator* i, T* vk_struct) {
    (void)vk_get_vk_struct_id<T>::id;

    vk_struct_common* p = i->value;
    if (p->pNext) {
        ::abort();
    }

    p->pNext = reinterpret_cast<vk_struct_common*>(vk_struct);
    vk_struct->pNext = NULL;

    *i = vk_make_chain_iterator(vk_struct);
}

bool vk_descriptor_type_has_descriptor_buffer(VkDescriptorType type) {
    switch (type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            return true;
        default:
            return false;
    }
}

}  // namespace

#endif /* VK_UTIL_H */
