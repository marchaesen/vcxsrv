/*
 * Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef LIBPAN_SHADERS_H
#define LIBPAN_SHADERS_H

#ifndef PAN_ARCH
#error "PAN_ARCH needs to be defined for this header to work!"
#endif

#if (PAN_ARCH == 4)
#include "libpan_shaders_v4.h"
#elif (PAN_ARCH == 5)
#include "libpan_shaders_v5.h"
#elif (PAN_ARCH == 6)
#include "libpan_shaders_v6.h"
#elif (PAN_ARCH == 7)
#include "libpan_shaders_v7.h"
#elif (PAN_ARCH == 9)
#include "libpan_shaders_v9.h"
#elif (PAN_ARCH == 10)
#include "libpan_shaders_v10.h"
#else
#error "Unsupported architecture for libpan"
#endif

#endif
