/*
 * Copyright 2025 Mesa3D authors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "Sync.h"

namespace gfxstream {

class WindowsSyncHelper : public SyncHelper {
   public:
    WindowsSyncHelper();

    int wait(int syncFd, int timeoutMilliseconds) override;

    void debugPrint(int syncFd) override;

    int dup(int syncFd) override;

    int close(int syncFd) override;
};

}  // namespace gfxstream
