/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_EVENT_H
#define PANVK_EVENT_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "vk_object.h"

#include "panvk_mempool.h"

struct panvk_event {
   struct vk_object_base base;

   /* v10 is lacking IAND/IOR instructions, which forces us to have one syncobj
    * per-subqueue instead of one syncobj on which subqueues would only
    * set/clear their bit. */
   struct panvk_priv_mem syncobjs;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_event, base, VkEvent, VK_OBJECT_TYPE_EVENT)

#endif
