/*
 * Copyright 2016 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef ANDROID_INCLUDE_HARDWARE_GOLDFISH_SYNC_H
#define ANDROID_INCLUDE_HARDWARE_GOLDFISH_SYNC_H

#define GOLDFISH_SYNC_VULKAN_SEMAPHORE_SYNC 0x00000001
#define GOLDFISH_SYNC_VULKAN_QSRI 0x00000002

#include <errno.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>

// Make it conflict with ioctls that are not likely to be used
// in the emulator.
//
// '@'	00-0F	linux/radeonfb.h	conflict!
// '@'	00-0F	drivers/video/aty/aty128fb.c	conflict!
#define GOLDFISH_SYNC_IOC_MAGIC	'@'

struct goldfish_sync_ioctl_info {
    uint64_t host_glsync_handle_in;
    uint64_t host_syncthread_handle_in;
    int fence_fd_out;
};

#define GOLDFISH_SYNC_IOC_QUEUE_WORK	_IOWR(GOLDFISH_SYNC_IOC_MAGIC, 0, struct goldfish_sync_ioctl_info)
#define GOLDFISH_SYNC_IOC_SIGNAL	_IOWR(GOLDFISH_SYNC_IOC_MAGIC, 1, struct goldfish_sync_ioctl_info)

static __inline__ int goldfish_sync_open() {
    return open("/dev/goldfish_sync", O_RDWR);
}

static __inline__ int goldfish_sync_close(int sync_fd) {
    return close(sync_fd);
}

static unsigned int sQueueWorkIoctlCmd = GOLDFISH_SYNC_IOC_QUEUE_WORK;

// If we are running on a 64-bit kernel.
static unsigned int sQueueWorkIoctlCmd64Kernel = 0xc0184000;

static __inline__ int goldfish_sync_queue_work(int goldfish_sync_fd,
                                                uint64_t host_glsync,
                                                uint64_t host_thread,
                                                int* fd_out) {

    struct goldfish_sync_ioctl_info info;
    int err;

    info.host_glsync_handle_in = host_glsync;
    info.host_syncthread_handle_in = host_thread;
    info.fence_fd_out = -1;

    err = ioctl(goldfish_sync_fd, sQueueWorkIoctlCmd, &info);

    if (err < 0 && errno == ENOTTY) {
        sQueueWorkIoctlCmd = sQueueWorkIoctlCmd64Kernel;
        err = ioctl(goldfish_sync_fd, sQueueWorkIoctlCmd, &info);
        if (err < 0) {
            sQueueWorkIoctlCmd = GOLDFISH_SYNC_IOC_QUEUE_WORK;
        }
    }

    if (fd_out) *fd_out = info.fence_fd_out;

    return err;
}

static __inline__ int goldfish_sync_signal(int goldfish_sync_fd) {
    return ioctl(goldfish_sync_fd, GOLDFISH_SYNC_IOC_SIGNAL, 0);
}

#endif
