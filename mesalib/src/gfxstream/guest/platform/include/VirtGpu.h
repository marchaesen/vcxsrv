/*
 * Copyright 2022 Google
 * SPDX-License-Identifier: MIT
 */
#ifndef VIRTGPU_DEVICE_H
#define VIRTGPU_DEVICE_H

#include <cstdint>
#include <memory>

#include "virtgpu_gfxstream_protocol.h"

// See virgl_hw.h and p_defines.h
#define VIRGL_FORMAT_B8G8R8A8_UNORM 1
#define VIRGL_FORMAT_B5G6R5_UNORM 7
#define VIRGL_FORMAT_R10G10B10A2_UNORM 8
#define VIRGL_FORMAT_R8_UNORM 64
#define VIRGL_FORMAT_R8G8B8_UNORM 66
#define VIRGL_FORMAT_R8G8B8A8_UNORM 67
#define VIRGL_FORMAT_R16G16B16A16_FLOAT 94
#define VIRGL_FORMAT_YV12 163
#define VIRGL_FORMAT_YV16 164
#define VIRGL_FORMAT_IYUV 165
#define VIRGL_FORMAT_NV12 166
#define VIRGL_FORMAT_NV21 167

#define VIRGL_BIND_RENDER_TARGET (1 << 1)
#define VIRGL_BIND_CUSTOM (1 << 17)
#define VIRGL_BIND_LINEAR (1 << 22)

#define PIPE_BUFFER 0
#define PIPE_TEXTURE_2D 2

enum VirtGpuParamId : uint32_t {
    kParam3D = 0,
    kParamCapsetFix = 1,
    kParamResourceBlob = 2,
    kParamHostVisible = 3,
    kParamCrossDevice = 4,
    kParamContextInit = 5,
    kParamSupportedCapsetIds = 6,
    kParamExplicitDebugName = 7,
    // Experimental, not in upstream Linux
    kParamFencePassing = 8,
    kParamCreateGuestHandle = 9,
    kParamMax = 10,
};

enum VirtGpuExecBufferFlags : uint32_t {
    kFenceIn = 0x0001,
    kFenceOut = 0x0002,
    kRingIdx = 0x0004,
    kShareableIn = 0x0008,
    kShareableOut = 0x0010,
};

enum VirtGpuCapset {
    kCapsetNone = 0,
    kCapsetVirgl = 1,
    kCapsetVirgl2 = 2,
    kCapsetGfxStreamVulkan = 3,
    kCapsetVenus = 4,
    kCapsetCrossDomain = 5,
    kCapsetDrm = 6,
    kCapsetGfxStreamMagma = 7,
    kCapsetGfxStreamGles = 8,
    kCapsetGfxStreamComposer = 9,
};

// Try to keep aligned with vulkan-cereal / rutabaga.
enum VirtGpuHandleType {
    kMemHandleOpaqueFd = 0x0001,
    kMemHandleDmabuf = 0x0002,
    kMemHandleOpaqueWin32 = 0x0003,
    kMemHandleShm = 0x0004,
    kMemHandleZircon = 0x0008,
    kFenceHandleOpaqueFd = 0x0010,
    kFenceHandleSyncFd = 0x0020,
    kFenceHandleOpaqueWin32 = 0x0040,
    kFenceHandleZircon = 0x0080,
};

enum VirtGpuResourceFlags : uint32_t {
    kBlobFlagMappable = 0x0001,
    kBlobFlagShareable = 0x0002,
    kBlobFlagCrossDevice = 0x0004,
    kBlobFlagCreateGuestHandle = 0x0008,
};

enum VirtGpuResourceMem {
    kBlobMemGuest = 0x0001,
    kBlobMemHost3d = 0x0002,
    kBlobMemHost3dGuest = 0x0003,
};

struct VirtGpuExternalHandle {
    int64_t osHandle;
    enum VirtGpuHandleType type;
};

struct VirtGpuExecBuffer {
    void* command;
    uint32_t command_size;
    uint32_t ring_idx;
    enum VirtGpuExecBufferFlags flags;
    struct VirtGpuExternalHandle handle;
};

struct VirtGpuParam {
    uint64_t param;
    const char* name;
    uint64_t value;
};

struct VirtGpuCreateBlob {
    uint64_t size;
    enum VirtGpuResourceFlags flags;
    enum VirtGpuResourceMem blobMem;
    uint64_t blobId;

    uint8_t* blobCmd;
    uint32_t blobCmdSize;
};

struct VirtGpuCaps {
    uint64_t params[kParamMax];
    struct vulkanCapset vulkanCapset;
    struct magmaCapset magmaCapset;
    struct glesCapset glesCapset;
    struct composerCapset composerCapset;
};

#define INVALID_DESCRIPTOR -1

class VirtGpuResourceMapping;
class VirtGpuResource;
using VirtGpuResourcePtr = std::shared_ptr<VirtGpuResource>;
using VirtGpuResourceMappingPtr = std::shared_ptr<VirtGpuResourceMapping>;

class VirtGpuResource {
   public:
    virtual ~VirtGpuResource() {}

    // The `intoRaw()` function drops ownerships of the OS-handle underlying
    // the resource.  It is the responsibility of the caller to manage lifetimes
    // of the virtio-gpu resource.  This function is mostly for gfxstream EGL
    // compatibility and shouldn't be used elsewhere.
    virtual void intoRaw(){};

    virtual uint32_t getResourceHandle() const = 0;
    virtual uint32_t getBlobHandle() const = 0;
    virtual uint64_t getSize() const = 0;
    virtual int wait() = 0;

    virtual VirtGpuResourceMappingPtr createMapping(void) = 0;
    virtual int exportBlob(struct VirtGpuExternalHandle& handle) = 0;

    virtual int transferFromHost(uint32_t x, uint32_t y, uint32_t w, uint32_t h) = 0;
    virtual int transferFromHost(uint32_t offset, uint32_t size) {
        return transferFromHost(offset, 0, size, 1);
    }

    virtual int transferToHost(uint32_t x, uint32_t y, uint32_t w, uint32_t h) = 0;
    virtual int transferToHost(uint32_t offset, uint32_t size) {
        return transferToHost(offset, 0, size, 1);
    }
};

class VirtGpuResourceMapping {
   public:
    virtual ~VirtGpuResourceMapping(void) {}

    virtual uint8_t* asRawPtr(void) = 0;
};

class VirtGpuDevice {
  public:
   static VirtGpuDevice* getInstance(enum VirtGpuCapset capset = kCapsetNone,
                                     int32_t descriptor = INVALID_DESCRIPTOR);
   static void resetInstance();

   VirtGpuDevice(enum VirtGpuCapset capset) : mCapset(capset) {}
   virtual ~VirtGpuDevice() {}

   enum VirtGpuCapset capset() { return mCapset; }

   virtual int64_t getDeviceHandle(void) = 0;

   virtual struct VirtGpuCaps getCaps(void) = 0;

   virtual VirtGpuResourcePtr createBlob(const struct VirtGpuCreateBlob& blobCreate) = 0;
   virtual VirtGpuResourcePtr createResource(uint32_t width, uint32_t height, uint32_t stride,
                                             uint32_t size, uint32_t virglFormat, uint32_t target,
                                             uint32_t bind) = 0;
   virtual VirtGpuResourcePtr importBlob(const struct VirtGpuExternalHandle& handle) = 0;

   virtual int execBuffer(struct VirtGpuExecBuffer& execbuffer, const VirtGpuResource* blob) = 0;

  private:
   enum VirtGpuCapset mCapset;
};

VirtGpuDevice* kumquatCreateVirtGpuDevice(enum VirtGpuCapset capset = kCapsetNone, int fd = -1);
VirtGpuDevice* osCreateVirtGpuDevice(enum VirtGpuCapset capset = kCapsetNone, int fd = -1);

VirtGpuDevice* createPlatformVirtGpuDevice(enum VirtGpuCapset capset = kCapsetNone, int fd = -1);

// HACK: We can use gfxstream::guest::EnumFlags, but we'll have to do more guest
// refactorings to figure out our end goal.  We can either depend more on base or
// try to transition to something else (b:202552093) [atleast for guests].
constexpr enum VirtGpuResourceFlags operator|(const enum VirtGpuResourceFlags self,
                                              const enum VirtGpuResourceFlags other) {
    return (enum VirtGpuResourceFlags)(uint32_t(self) | uint32_t(other));
}

constexpr enum  VirtGpuExecBufferFlags operator |(const enum VirtGpuExecBufferFlags self,
                                                  const enum VirtGpuExecBufferFlags other) {
    return (enum VirtGpuExecBufferFlags)(uint32_t(self) | uint32_t(other));
}

#endif
