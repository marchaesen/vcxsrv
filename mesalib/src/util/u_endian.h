/**************************************************************************
 * 
 * Copyright 2007-2008 VMware, Inc.
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
#ifndef U_ENDIAN_H
#define U_ENDIAN_H

#ifdef HAVE_ENDIAN_H
#include <endian.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
# define PIPE_ARCH_LITTLE_ENDIAN
#elif __BYTE_ORDER == __BIG_ENDIAN
# define PIPE_ARCH_BIG_ENDIAN
#endif

#elif defined(__APPLE__)
#include <machine/endian.h>

#if __DARWIN_BYTE_ORDER == __DARWIN_LITTLE_ENDIAN
# define PIPE_ARCH_LITTLE_ENDIAN
#elif __DARWIN_BYTE_ORDER == __DARWIN_BIG_ENDIAN
# define PIPE_ARCH_BIG_ENDIAN
#endif

#elif defined(__sun)
#include <sys/isa_defs.h>

#if defined(_LITTLE_ENDIAN)
# define PIPE_ARCH_LITTLE_ENDIAN
#elif defined(_BIG_ENDIAN)
# define PIPE_ARCH_BIG_ENDIAN
#endif

#elif defined(__OpenBSD__) || defined(__NetBSD__) || \
      defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/types.h>
#include <machine/endian.h>

#if _BYTE_ORDER == _LITTLE_ENDIAN
# define PIPE_ARCH_LITTLE_ENDIAN
#elif _BYTE_ORDER == _BIG_ENDIAN
# define PIPE_ARCH_BIG_ENDIAN
#endif

#elif defined(_MSC_VER)

#define PIPE_ARCH_LITTLE_ENDIAN

#endif

#endif
