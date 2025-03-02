/*
 *  sync abstraction
 *  Copyright 2015-2016 Collabora Ltd.
 *
 *  Based on the implementation from the Android Open Source Project,
 *
 *  Copyright 2012 Google, Inc
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 *  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 *  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *  OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _LIBSYNC_H
#define _LIBSYNC_H

#include <io.h>
#include <string.h>
#include "util/os_file.h"

/* accumulate fd2 into fd1.  If *fd1 is not a valid fd then dup fd2,
 * otherwise sync_merge() and close the old *fd1.  This can be used
 * to implement the pattern:
 *
 *    init()
 *    {
 *       batch.fence_fd = -1;
 *    }
 *
 *    // does *NOT* take ownership of fd
 *    server_sync(int fd)
 *    {
 *       if (sync_accumulate("foo", &batch.fence_fd, fd)) {
 *          ... error ...
 *       }
 *    }
 */
static inline int sync_accumulate(const char *name, int *fd1, int fd2)
{
	int ret;

	assert(fd2 >= 0);

	if (*fd1 < 0) {
		*fd1 = os_dupfd_cloexec(fd2);
		return 0;
	}

	close(*fd1);
	*fd1 = dup(fd2);

	return 0;
}

/* Helper macro to complain if fd is non-negative and not a valid fence fd.
 * Sprinkle this around to help catch fd lifetime issues.
 */
#if MESA_DEBUG
#  include "util/log.h"
#  define validate_fence_fd(fd) do {                                         \
      if (((fd) >= 0) && !sync_valid_fd(fd))                                 \
         mesa_loge("%s:%d: invalid fence fd: %d", __func__, __LINE__, (fd)); \
   } while (0)
#else
#  define validate_fence_fd(fd) do {} while (0)
#endif

#endif
