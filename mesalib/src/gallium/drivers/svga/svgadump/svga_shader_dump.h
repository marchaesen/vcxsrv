/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file
 * SVGA Shader Dump Facilities
 * 
 * @author Michal Krol <michal@vmware.com>
 */

#ifndef SVGA_SHADER_DUMP_H
#define SVGA_SHADER_DUMP_H

void
svga_shader_dump(
   const unsigned *assem,
   unsigned dwords,
   unsigned do_binary );

#endif /* SVGA_SHADER_DUMP_H */
