/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include <lib/zxio/zxio.h>
#include <services/service_connector.h>
#include <string.h>

#include "os_dirent.h"
#include "util/log.h"

struct os_dir {
    ~os_dir() {
        if (dir_iterator_init_) {
            zxio_dirent_iterator_destroy(&iterator_);
        }
        if (zxio_init_) {
            zxio_close(&io_storage_.io, /*should_wait=*/true);
        }
    }

    // Always consumes |dir_channel|
    bool Init(zx_handle_t dir_channel) {
        zx_status_t status = zxio_create(dir_channel, &io_storage_);
        if (status != ZX_OK) {
            mesa_loge("zxio_create failed: %d", status);
            return false;
        }

        zxio_init_ = true;

        status = zxio_dirent_iterator_init(&iterator_, &io_storage_.io);
        if (status != ZX_OK) {
            mesa_loge("zxio_dirent_iterator_init failed: %d", status);
            return false;
        }

        dir_iterator_init_ = true;
        return true;
    }

    bool Next(struct os_dirent* entry) {
        // dirent is an in-out parameter.
        // name must be initialized to point to a buffer of at least ZXIO_MAX_FILENAME bytes.
        static_assert(sizeof(entry->d_name) >= ZXIO_MAX_FILENAME);
        zxio_dirent_t dirent = {.name = entry->d_name};

        zx_status_t status = zxio_dirent_iterator_next(&iterator_, &dirent);
        if (status != ZX_OK) {
            if (status != ZX_ERR_NOT_FOUND)
                mesa_loge("zxio_dirent_iterator_next failed: %d", status);
            return false;
        }

        entry->d_ino = dirent.has.id ? dirent.id : OS_INO_UNKNOWN;
        entry->d_name[dirent.name_length] = '\0';

        return true;
    }

   private:
    bool zxio_init_ = false;
    bool dir_iterator_init_ = false;
    zxio_storage_t io_storage_;
    zxio_dirent_iterator_t iterator_;
};

os_dir_t* os_opendir(const char* path) {
    zx_handle_t dir_channel = GetConnectToServiceFunction()(path);
    if (dir_channel == ZX_HANDLE_INVALID) {
        mesa_loge("fuchsia_open(%s) failed", path);
        return nullptr;
    }

    auto dir = new os_dir();

    if (!dir->Init(dir_channel)) {
        delete dir;
        return nullptr;
    }

    return dir;
}

int os_closedir(os_dir_t* dir) {
    delete dir;
    return 0;
}

struct os_dirent* os_readdir(os_dir_t* dir) {
    static struct os_dirent dirent = {};
    return reinterpret_cast<os_dirent*>(dir->Next(&dirent)) ? &dirent : nullptr;
}
