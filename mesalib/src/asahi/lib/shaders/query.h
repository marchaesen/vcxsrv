/*
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2024 Valve Corporation
 * Copyright 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "libagx.h"

#pragma once

struct libagx_copy_query_push {
   GLOBAL(uint32_t) availability;
   GLOBAL(uint64_t) results;
   GLOBAL(uint16_t) oq_index;
   uint64_t dst_addr;
   uint64_t dst_stride;
   uint32_t first_query;

   /* Flags. Could specialize the shader? */
   uint16_t partial;
   uint16_t _64;
   uint16_t with_availability;
};
