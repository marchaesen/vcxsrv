/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
 * Copyright (c) 2008 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef _U_DEBUG_GALLIUM_H_
#define _U_DEBUG_GALLIUM_H_

#include "pipe/p_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

unsigned long
debug_memory_begin(void);

void 
debug_memory_end(unsigned long beginning);

#ifdef DEBUG
void debug_print_format(const char *msg, unsigned fmt);
#else
#define debug_print_format(_msg, _fmt) ((void)0)
#endif

#ifdef DEBUG

void
debug_print_transfer_flags(const char *msg, unsigned usage);

void
debug_print_bind_flags(const char *msg, unsigned usage);

void
debug_print_usage_enum(const char *msg, enum pipe_resource_usage usage);

#endif

#ifdef __cplusplus
}
#endif

#endif
