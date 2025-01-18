/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HELPERS_H
#define HELPERS_H

#include <stdbool.h>
#include <stdint.h>

bool
util_lower_clearsize_to_dword(const void *clear_value, int *clear_value_size,
                              uint32_t *out);

#endif
