/*
 * Copyright (c) 2009-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef SVGA_DUMP_H_
#define SVGA_DUMP_H_

#include "util/compiler.h"

void            
svga_dump_command(uint32_t cmd_id, const void *data, uint32_t size);

void
svga_dump_commands(const void *commands, uint32_t size);

#endif /* SVGA_DUMP_H_ */
