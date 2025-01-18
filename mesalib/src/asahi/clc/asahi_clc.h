/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* The libagx printf/abort buffer addresses is fixed at compile-time for
 * simplicity.
 */
#define LIBAGX_PRINTF_BUFFER_ADDRESS (1ull << 36)
#define LIBAGX_PRINTF_BUFFER_SIZE    (16384)
