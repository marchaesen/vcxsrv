/*
 * Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef LIBPAN_H
#define LIBPAN_H

#ifndef __OPENCL_VERSION__
#ifndef PAN_ARCH
#error "PAN_ARCH needs to be defined for this header to work!"
#endif

/* We now include binding definition */
#if (PAN_ARCH == 4)
#include "libpan_v5.h"
#elif (PAN_ARCH == 5)
#include "libpan_v5.h"
#elif (PAN_ARCH == 6)
#include "libpan_v6.h"
#elif (PAN_ARCH == 7)
#include "libpan_v7.h"
#elif (PAN_ARCH == 9)
#include "libpan_v9.h"
#elif (PAN_ARCH == 10)
#include "libpan_v10.h"
#else
#error "Unsupported architecture for libpan"
#endif

#endif /* __OPENCL_VERSION__ */

#endif /* LIBPAN_H */
