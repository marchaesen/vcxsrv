/*
 * Copyright 2023 Google
 * SPDX-License-Identifier: MIT
 */

#include "VirtGpuKumquatSync.h"

#include <unistd.h>

namespace gfxstream {

VirtGpuKumquatSyncHelper::VirtGpuKumquatSyncHelper() {}

int VirtGpuKumquatSyncHelper::wait(int syncFd, int timeoutMilliseconds) {
    (void)timeoutMilliseconds;
    // So far, syncfds are EventFd in the Kumquat layer. This may change
    uint64_t count = 1;
    ssize_t bytes_read = read(syncFd, &count, sizeof(count));

    if (bytes_read < 0) {
        return bytes_read;
    }

    // A successful read decrements the eventfd's counter to zero.  In
    // case the eventfd is waited on again, or a dup is waited on, we
    // have to write to the eventfd for the next read.
    ssize_t bytes_written = write(syncFd, &count, sizeof(count));
    if (bytes_written < 0) {
        return bytes_written;
    }

    return 0;
}

int VirtGpuKumquatSyncHelper::dup(int syncFd) { return ::dup(syncFd); }

void VirtGpuKumquatSyncHelper::debugPrint(int syncFd) { return; }

int VirtGpuKumquatSyncHelper::close(int syncFd) { return ::close(syncFd); }

SyncHelper* kumquatCreateSyncHelper() { return new VirtGpuKumquatSyncHelper(); }

}  // namespace gfxstream
