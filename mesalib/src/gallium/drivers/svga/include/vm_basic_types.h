/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**********************************************************
 *
 * Copyright (c) 2024 Broadcom.
 * The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
 *
 **********************************************************/
#ifndef VM_BASIC_TYPES_H
#define VM_BASIC_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t uint32;
typedef int32_t int32;
typedef uint64_t uint64;
typedef uint16_t uint16;
typedef int16_t int16;
typedef uint8_t uint8;
typedef int8_t int8;

typedef uint64 PA;
typedef uint32 PPN;
typedef uint32 PPN32;
typedef uint64 PPN64;

typedef bool Bool;

#define MAX_UINT64 UINT64_MAX
#define MAX_UINT32 UINT32_MAX
#define MAX_UINT16 UINT16_MAX

#define CONST64U(x) x##ULL

#ifndef MBYTES_SHIFT
#define MBYTES_SHIFT 20
#endif
#ifndef MBYTES_2_BYTES
#define MBYTES_2_BYTES(_nbytes) ((uint64)(_nbytes) << MBYTES_SHIFT)
#endif

#define INLINE inline

#endif
