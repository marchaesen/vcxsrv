/*
 * Copyright 2023 Google
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <stddef.h>

namespace gfxstream {

// Abstraction around libsync for testing.
class SyncHelper {
   public:
    virtual ~SyncHelper() {}

    virtual int wait(int syncFd, int timeoutMilliseconds) = 0;

    virtual void debugPrint(int syncFd) = 0;

    virtual int dup(int syncFd) = 0;

    virtual int close(int syncFd) = 0;
};

SyncHelper* osCreateSyncHelper();
SyncHelper* kumquatCreateSyncHelper();
SyncHelper* createPlatformSyncHelper();

}  // namespace gfxstream
