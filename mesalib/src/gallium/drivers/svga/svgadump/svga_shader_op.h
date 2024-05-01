/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file
 * SVGA Shader Token Opcode Info
 * 
 * @author Michal Krol <michal@vmware.com>
 */

#ifndef SVGA_SHADER_OP_H
#define SVGA_SHADER_OP_H

struct sh_opcode_info
{
   const char *mnemonic;
   unsigned num_dst:8;
   unsigned num_src:8;
   unsigned pre_dedent:1;
   unsigned post_indent:1;
   unsigned svga_opcode:16;
};

const struct sh_opcode_info *svga_opcode_info( unsigned op );

#endif /* SVGA_SHADER_OP_H */
