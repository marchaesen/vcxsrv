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


#include "util/u_debug.h"
#include "u_debug_gallium.h"
#include "u_dump.h"
#include "u_format.h"

#ifdef DEBUG

void
debug_print_format(const char *msg, unsigned fmt)
{
   debug_printf("%s: %s\n", msg, util_format_name(fmt));
}


/**
 * Print PIPE_TRANSFER_x flags with a message.
 */
void
debug_print_transfer_flags(const char *msg, unsigned usage)
{
   debug_printf("%s: ", msg);
   util_dump_transfer_usage(stdout, usage);
   printf("\n");
}


/**
 * Print PIPE_BIND_x flags with a message.
 */
void
debug_print_bind_flags(const char *msg, unsigned usage)
{
   static const struct debug_named_value names[] = {
      DEBUG_NAMED_VALUE(PIPE_BIND_DEPTH_STENCIL),
      DEBUG_NAMED_VALUE(PIPE_BIND_RENDER_TARGET),
      DEBUG_NAMED_VALUE(PIPE_BIND_BLENDABLE),
      DEBUG_NAMED_VALUE(PIPE_BIND_SAMPLER_VIEW),
      DEBUG_NAMED_VALUE(PIPE_BIND_VERTEX_BUFFER),
      DEBUG_NAMED_VALUE(PIPE_BIND_INDEX_BUFFER),
      DEBUG_NAMED_VALUE(PIPE_BIND_CONSTANT_BUFFER),
      DEBUG_NAMED_VALUE(PIPE_BIND_DISPLAY_TARGET),
      DEBUG_NAMED_VALUE(PIPE_BIND_STREAM_OUTPUT),
      DEBUG_NAMED_VALUE(PIPE_BIND_CURSOR),
      DEBUG_NAMED_VALUE(PIPE_BIND_CUSTOM),
      DEBUG_NAMED_VALUE(PIPE_BIND_GLOBAL),
      DEBUG_NAMED_VALUE(PIPE_BIND_SHADER_BUFFER),
      DEBUG_NAMED_VALUE(PIPE_BIND_SHADER_IMAGE),
      DEBUG_NAMED_VALUE(PIPE_BIND_COMPUTE_RESOURCE),
      DEBUG_NAMED_VALUE(PIPE_BIND_COMMAND_ARGS_BUFFER),
      DEBUG_NAMED_VALUE(PIPE_BIND_SCANOUT),
      DEBUG_NAMED_VALUE(PIPE_BIND_SHARED),
      DEBUG_NAMED_VALUE(PIPE_BIND_LINEAR),
      DEBUG_NAMED_VALUE_END
   };

   debug_printf("%s: %s\n", msg, debug_dump_flags(names, usage));
}


/**
 * Print PIPE_USAGE_x enum values with a message.
 */
void
debug_print_usage_enum(const char *msg, enum pipe_resource_usage usage)
{
   static const struct debug_named_value names[] = {
      DEBUG_NAMED_VALUE(PIPE_USAGE_DEFAULT),
      DEBUG_NAMED_VALUE(PIPE_USAGE_IMMUTABLE),
      DEBUG_NAMED_VALUE(PIPE_USAGE_DYNAMIC),
      DEBUG_NAMED_VALUE(PIPE_USAGE_STREAM),
      DEBUG_NAMED_VALUE(PIPE_USAGE_STAGING),
      DEBUG_NAMED_VALUE_END
   };

   debug_printf("%s: %s\n", msg, debug_dump_enum(names, usage));
}

#endif
