/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_MACROS_H
#define PANVK_MACROS_H

#include <assert.h>

#ifdef HAVE_VALGRIND
#include <memcheck.h>
#include <valgrind.h>
#define VG(x) x
#else
#define VG(x)
#endif

#include "vk_log.h"

static inline VkResult
panvk_catch_indirect_alloc_failure(VkResult error)
{
   /* errno is set to -ENOMEM in the kmod allocator callback when an allocation
    * fails. When that's the case, the allocation failure takes precedence on
    * the original error code. We also reset errno before leaving so we don't
    * end up reporting the same allocation failure twice. */
   if (errno == -ENOMEM) {
      errno = 0;
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   return error;
}

#define panvk_error(obj, error)                                                \
   vk_error(obj, panvk_catch_indirect_alloc_failure(error))

#define panvk_errorf(obj, error, ...)                                          \
   vk_errorf(obj, panvk_catch_indirect_alloc_failure(error), __VA_ARGS__)

#define panvk_stub() assert(!"stub")

#define panvk_arch_name(name, version) panvk_##version##_##name

#define panvk_arch_dispatch(arch, name, ...)                                   \
   do {                                                                        \
      switch (arch) {                                                          \
      case 6:                                                                  \
         panvk_arch_name(name, v6)(__VA_ARGS__);                               \
         break;                                                                \
      case 7:                                                                  \
         panvk_arch_name(name, v7)(__VA_ARGS__);                               \
         break;                                                                \
      case 10:                                                                 \
         panvk_arch_name(name, v10)(__VA_ARGS__);                              \
         break;                                                                \
      default:                                                                 \
         unreachable("Unsupported architecture");                              \
      }                                                                        \
   } while (0)

#define panvk_arch_dispatch_ret(arch, name, ret, ...)                          \
   do {                                                                        \
      switch (arch) {                                                          \
      case 6:                                                                  \
         ret = panvk_arch_name(name, v6)(__VA_ARGS__);                         \
         break;                                                                \
      case 7:                                                                  \
         ret = panvk_arch_name(name, v7)(__VA_ARGS__);                         \
         break;                                                                \
      case 10:                                                                 \
         ret = panvk_arch_name(name, v10)(__VA_ARGS__);                        \
         break;                                                                \
      default:                                                                 \
         unreachable("Unsupported architecture");                              \
      }                                                                        \
   } while (0)

#ifdef PAN_ARCH
#if PAN_ARCH == 6
#define panvk_per_arch(name) panvk_arch_name(name, v6)
#elif PAN_ARCH == 7
#define panvk_per_arch(name) panvk_arch_name(name, v7)
#elif PAN_ARCH == 9
#define panvk_per_arch(name) panvk_arch_name(name, v9)
#elif PAN_ARCH == 10
#define panvk_per_arch(name) panvk_arch_name(name, v10)
#else
#error "Unsupported arch"
#endif
#endif

#endif
