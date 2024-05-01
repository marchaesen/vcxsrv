/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef SVGA_RESOURCE_H
#define SVGA_RESOURCE_H


#include "util/u_debug.h"

struct svga_context;
struct svga_screen;


void svga_init_screen_resource_functions(struct svga_screen *is);
void svga_init_resource_functions(struct svga_context *svga );


#endif /* SVGA_RESOURCE_H */
