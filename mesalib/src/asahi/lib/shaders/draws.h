/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include "libagx.h"

#pragma once

struct libagx_predicate_indirect_push {
   GLOBAL(uint32_t) out;
   CONSTANT(uint32_t) in;
   CONSTANT(uint32_t) draw_count;
   uint32_t stride_el;
};
