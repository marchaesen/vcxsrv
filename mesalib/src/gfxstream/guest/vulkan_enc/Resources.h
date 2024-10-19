/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if defined(__ANDROID__) || defined(__Fuchsia__)
#include <hardware/hwvulkan.h>
#elif defined(__linux__)
#include <vulkan/vk_icd.h>
#endif
#include <inttypes.h>
#include <vulkan/vulkan.h>

#include <functional>

#include "VulkanHandles.h"

namespace gfxstream {
namespace guest {
class IOStream;
}  // namespace guest
}  // namespace gfxstream

namespace gfxstream {
namespace vk {
class VkEncoder;
struct DescriptorPoolAllocationInfo;
struct ReifiedDescriptorSet;
struct DescriptorSetLayoutInfo;
}  // namespace vk
}  // namespace gfxstream

extern "C" {

struct goldfish_vk_object_list {
    void* obj;
    struct goldfish_vk_object_list* next;
};

#if defined(__ANDROID__) || defined(__Fuchsia__)
#define DECLARE_HWVULKAN_DISPATCH hwvulkan_dispatch_t dispatch;
#elif defined(__linux__)
#define DECLARE_HWVULKAN_DISPATCH VK_LOADER_DATA loaderData;
#else
#define DECLARE_HWVULKAN_DISPATCH
#endif

#define GOLDFISH_VK_DEFINE_DISPATCHABLE_HANDLE_STRUCT(type) \
    struct goldfish_##type {                                \
        DECLARE_HWVULKAN_DISPATCH                           \
        uint64_t underlying;                                \
        gfxstream::vk::VkEncoder* lastUsedEncoder;          \
        uint32_t sequenceNumber;                            \
        gfxstream::vk::VkEncoder* privateEncoder;           \
        gfxstream::guest::IOStream* privateStream;          \
        uint32_t flags;                                     \
        struct goldfish_vk_object_list* poolObjects;        \
        struct goldfish_vk_object_list* subObjects;         \
        struct goldfish_vk_object_list* superObjects;       \
        void* userPtr;                                      \
    };

#define GOLDFISH_VK_DEFINE_TRIVIAL_NON_DISPATCHABLE_HANDLE_STRUCT(type) \
    struct goldfish_##type {                                            \
        uint64_t underlying;                                            \
        struct goldfish_vk_object_list* poolObjects;                    \
        struct goldfish_vk_object_list* subObjects;                     \
        struct goldfish_vk_object_list* superObjects;                   \
        void* userPtr;                                                  \
    };

#define GOLDFISH_VK_NEW_FROM_HOST_DECL(type) type new_from_host_##type(type);

#define GOLDFISH_VK_AS_GOLDFISH_DECL(type) struct goldfish_##type* as_goldfish_##type(type);

#define GOLDFISH_VK_GET_HOST_DECL(type) type get_host_##type(type);

#define GOLDFISH_VK_DELETE_GOLDFISH_DECL(type) void delete_goldfish_##type(type);

#define GOLDFISH_VK_IDENTITY_DECL(type) type vk_handle_identity_##type(type);

#define GOLDFISH_VK_NEW_FROM_HOST_U64_DECL(type) type new_from_host_u64_##type(uint64_t);

#define GOLDFISH_VK_GET_HOST_U64_DECL(type) uint64_t get_host_u64_##type(type);

GOLDFISH_VK_LIST_AUTODEFINED_STRUCT_DISPATCHABLE_HANDLE_TYPES(
    GOLDFISH_VK_DEFINE_DISPATCHABLE_HANDLE_STRUCT)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_NEW_FROM_HOST_DECL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_AS_GOLDFISH_DECL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_DECL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_DELETE_GOLDFISH_DECL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_IDENTITY_DECL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_NEW_FROM_HOST_U64_DECL)
GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_U64_DECL)

GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_NEW_FROM_HOST_DECL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_AS_GOLDFISH_DECL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_DECL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_DELETE_GOLDFISH_DECL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_IDENTITY_DECL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_NEW_FROM_HOST_U64_DECL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(GOLDFISH_VK_GET_HOST_U64_DECL)
GOLDFISH_VK_LIST_AUTODEFINED_STRUCT_NON_DISPATCHABLE_HANDLE_TYPES(
    GOLDFISH_VK_DEFINE_TRIVIAL_NON_DISPATCHABLE_HANDLE_STRUCT)

struct goldfish_VkDescriptorPool {
    uint64_t underlying;
    gfxstream::vk::DescriptorPoolAllocationInfo* allocInfo;
};

struct goldfish_VkDescriptorSet {
    uint64_t underlying;
    gfxstream::vk::ReifiedDescriptorSet* reified;
};

struct goldfish_VkDescriptorSetLayout {
    uint64_t underlying;
    gfxstream::vk::DescriptorSetLayoutInfo* layoutInfo;
};

struct goldfish_VkCommandBuffer {
    DECLARE_HWVULKAN_DISPATCH
    uint64_t underlying;
    gfxstream::vk::VkEncoder* lastUsedEncoder;
    uint32_t sequenceNumber;
    gfxstream::vk::VkEncoder* privateEncoder;
    gfxstream::guest::IOStream* privateStream;
    uint32_t flags;
    struct goldfish_vk_object_list* poolObjects;
    struct goldfish_vk_object_list* subObjects;
    struct goldfish_vk_object_list* superObjects;
    void* userPtr;
    bool isSecondary;
    VkDevice device;
};

}  // extern "C"

namespace gfxstream {
namespace vk {

void appendObject(struct goldfish_vk_object_list** begin, void* val);
void eraseObject(struct goldfish_vk_object_list** begin, void* val);
void eraseObjects(struct goldfish_vk_object_list** begin);
void forAllObjects(struct goldfish_vk_object_list* begin, std::function<void(void*)> func);

}  // namespace vk
}  // namespace gfxstream
