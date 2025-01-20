/*
 * Copyright 2025 Mesa3D authors
 * SPDX-License-Identifier: MIT
 */

#include "WindowsSync.h"

namespace gfxstream {

WindowsSyncHelper::WindowsSyncHelper() {}

int WindowsSyncHelper::wait(int syncFd, int timeoutMilliseconds) {
    return -1;  // stub constant
}

void WindowsSyncHelper::debugPrint(int syncFd) {}

int WindowsSyncHelper::dup(int syncFd) {
    return -1;  // stub constant
}

int WindowsSyncHelper::close(int syncFd) {
    return -1;  // stub constant
}

SyncHelper* osCreateSyncHelper() {
    return nullptr;  // stub constant
}

}  // namespace gfxstream
