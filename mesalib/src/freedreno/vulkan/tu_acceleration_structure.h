/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef TU_ACCELERATION_STRUCT_H
#define TU_ACCELERATION_STRUCT_H

#include "tu_common.h"

VkResult tu_init_null_accel_struct(struct tu_device *device);

extern const vk_acceleration_structure_build_ops tu_as_build_ops;

#endif
