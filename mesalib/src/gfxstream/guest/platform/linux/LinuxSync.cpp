/*
 * Copyright 2023 Google
 * SPDX-License-Identifier: MIT
 */

#include "LinuxSync.h"

#include <unistd.h>

#include "util/libsync.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/u_process.h"

namespace gfxstream {

LinuxSyncHelper::LinuxSyncHelper() {}

int LinuxSyncHelper::wait(int syncFd, int timeoutMilliseconds) {
    return sync_wait(syncFd, timeoutMilliseconds);
}

void LinuxSyncHelper::debugPrint(int syncFd) {
    struct sync_file_info* info = sync_file_info(syncFd);
    if (!info) {
        mesa_loge("failed to get sync file info");
        return;
    }

    struct sync_fence_info* fence_info = (struct sync_fence_info*)info->sync_fence_info;

    for (uint32_t i = 0; i < info->num_fences; i++) {
        uint64_t time_ms = DIV_ROUND_UP(fence_info[i].timestamp_ns, 1e6);
        mesa_logi("[%s] Fence: %s, status: %i, timestamp (ms): %llu", util_get_process_name(),
                  info->name, fence_info[i].status, (unsigned long long)time_ms);
    }

    free(info);
}

int LinuxSyncHelper::dup(int syncFd) { return ::dup(syncFd); }

int LinuxSyncHelper::close(int syncFd) { return ::close(syncFd); }

SyncHelper* osCreateSyncHelper() { return new LinuxSyncHelper(); }

}  // namespace gfxstream
