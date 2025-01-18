/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_OQ_H
#define PANVK_CMD_OQ_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "genxml/gen_macros.h"

struct panvk_occlusion_query_state {
#if PAN_ARCH >= 10
   uint64_t syncobj;
#endif
   uint64_t ptr;
   enum mali_occlusion_mode mode;
};

#endif