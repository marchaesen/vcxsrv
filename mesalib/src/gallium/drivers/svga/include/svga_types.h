/*
 * Copyright (c) 1998-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0 OR MIT
 */

#ifndef _SVGA_TYPES_H_
#define _SVGA_TYPES_H_

#include "util/compiler.h"

#ifndef __HAIKU__
typedef int64_t int64;
typedef uint64_t uint64;

typedef int32_t int32;
typedef uint32_t uint32;

typedef int16_t int16;
typedef uint16_t uint16;

typedef int8_t int8;
typedef uint8_t uint8;
#else
#include <OS.h>
#endif /* HAIKU */

typedef uint8_t Bool;

typedef uint64    PA;
typedef uint32    PPN;
typedef uint64    PPN64;

#undef MAX_UINT32
#define MAX_UINT32 0xffffffffU

#endif /* _SVGA_TYPES_H_ */

