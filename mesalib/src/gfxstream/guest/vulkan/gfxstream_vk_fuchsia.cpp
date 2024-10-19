/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include <cutils/log.h>
#include <fidl/fuchsia.logger/cpp/wire.h>
#include <lib/syslog/structured_backend/cpp/fuchsia_syslog.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <lib/zxio/zxio.h>
#include <unistd.h>
#include <zircon/system/public/zircon/process.h>

#include <cstdarg>

#include "ResourceTracker.h"
#include "TraceProviderFuchsia.h"
#include "services/service_connector.h"

namespace {

zx::socket g_log_socket = zx::socket(ZX_HANDLE_INVALID);

typedef VkResult(VKAPI_PTR* PFN_vkOpenInNamespaceAddr)(const char* pName, uint32_t handle);

PFN_vkOpenInNamespaceAddr g_vulkan_connector;

zx_koid_t GetKoid(zx_handle_t handle) {
    zx_info_handle_basic_t info;
    zx_status_t status =
        zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

static zx_koid_t pid = GetKoid(zx_process_self());

static thread_local zx_koid_t tid = GetKoid(zx_thread_self());

cpp17::optional<cpp17::string_view> CStringToStringView(const char* cstr) {
    if (!cstr) {
        return cpp17::nullopt;
    }
    return cstr;
}

const char* StripDots(const char* path) {
    while (strncmp(path, "../", 3) == 0) {
        path += 3;
    }
    return path;
}

const char* StripPath(const char* path) {
    auto p = strrchr(path, '/');
    if (p) {
        return p + 1;
    } else {
        return path;
    }
}

const char* StripFile(const char* file, FuchsiaLogSeverity severity) {
    return severity > FUCHSIA_LOG_INFO ? StripDots(file) : StripPath(file);
}

extern "C" void gfxstream_fuchsia_log(int8_t severity, const char* tag, const char* file, int line,
                                      const char* format, va_list va) {
    if (!g_log_socket.is_valid()) {
        abort();
    }
    fuchsia_syslog::LogBuffer buffer;
    constexpr size_t kFormatStringLength = 1024;
    char fmt_string[kFormatStringLength];
    fmt_string[kFormatStringLength - 1] = 0;
    int n = kFormatStringLength;
    // Format
    // Number of bytes written not including null terminator
    int count = vsnprintf(fmt_string, n, format, va) + 1;
    if (count < 0) {
        // No message to write.
        return;
    }

    if (count >= n) {
        // truncated
        constexpr char kEllipsis[] = "...";
        constexpr size_t kEllipsisSize = sizeof(kEllipsis);
        snprintf(fmt_string + kFormatStringLength - 1 - kEllipsisSize, kEllipsisSize, kEllipsis);
    }

    if (file) {
        file = StripFile(file, severity);
    }
    buffer.BeginRecord(severity, CStringToStringView(file), line, fmt_string, g_log_socket.borrow(),
                       0, pid, tid);
    if (tag) {
        buffer.WriteKeyValue("tag", tag);
    }
    buffer.FlushRecord();
}

zx_handle_t LocalConnectToServiceFunction(const char* pName) {
    zx::channel remote_endpoint, local_endpoint;
    zx_status_t status;
    if ((status = zx::channel::create(0, &remote_endpoint, &local_endpoint)) != ZX_OK) {
        ALOGE("zx::channel::create failed: %d", status);
        return ZX_HANDLE_INVALID;
    }
    if ((status = g_vulkan_connector(pName, remote_endpoint.release())) != ZX_OK) {
        ALOGE("vulkan_connector failed: %d", status);
        return ZX_HANDLE_INVALID;
    }
    return local_endpoint.release();
}

}  // namespace

class VulkanDevice {
   public:
    VulkanDevice() : mHostSupportsGoldfish(IsAccessible(QEMU_PIPE_PATH)) {
        InitTraceProvider();
        gfxstream::vk::ResourceTracker::get();
    }

    static void InitLogger();

    static bool IsAccessible(const char* name) {
        zx_handle_t handle = GetConnectToServiceFunction()(name);
        if (handle == ZX_HANDLE_INVALID) return false;

        zxio_storage_t io_storage;
        zx_status_t status = zxio_create(handle, &io_storage);
        if (status != ZX_OK) return false;

        status = zxio_close(&io_storage.io, /*should_wait=*/true);
        if (status != ZX_OK) return false;

        return true;
    }

    static VulkanDevice& GetInstance() {
        static VulkanDevice g_instance;
        return g_instance;
    }

   private:
    void InitTraceProvider();

    TraceProviderFuchsia mTraceProvider;
    const bool mHostSupportsGoldfish;
};

void VulkanDevice::InitLogger() {
    auto log_socket = ([]() -> std::optional<zx::socket> {
        fidl::ClientEnd<fuchsia_logger::LogSink> channel{
            zx::channel{GetConnectToServiceFunction()("/svc/fuchsia.logger.LogSink")}};
        if (!channel.is_valid()) return std::nullopt;

        zx::socket local_socket, remote_socket;
        zx_status_t status = zx::socket::create(ZX_SOCKET_DATAGRAM, &local_socket, &remote_socket);
        if (status != ZX_OK) return std::nullopt;

        auto result = fidl::WireCall(channel)->ConnectStructured(std::move(remote_socket));

        if (!result.ok()) return std::nullopt;

        return local_socket;
    })();
    if (!log_socket) return;

    g_log_socket = std::move(*log_socket);
}

void VulkanDevice::InitTraceProvider() {
    if (!mTraceProvider.Initialize()) {
        ALOGE("Trace provider failed to initialize");
    }
}

extern "C" __attribute__((visibility("default"))) void vk_icdInitializeOpenInNamespaceCallback(
    PFN_vkOpenInNamespaceAddr callback) {
    g_vulkan_connector = callback;
    SetConnectToServiceFunction(&LocalConnectToServiceFunction);

    VulkanDevice::InitLogger();

    ALOGV("Gfxstream on Fuchsia initialized");
}
