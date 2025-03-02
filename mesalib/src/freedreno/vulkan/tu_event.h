/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_EVENT_H
#define TU_EVENT_H

#include "tu_common.h"
#include "tu_suballoc.h"

struct tu_event
{
   struct vk_object_base base;
   struct tu_suballoc_bo bo;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(tu_event, base, VkEvent, VK_OBJECT_TYPE_EVENT)

#endif /* TU_EVENT_H */
