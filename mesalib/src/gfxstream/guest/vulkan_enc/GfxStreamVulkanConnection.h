/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef GFXSTREAM_VULKAN_CONNECTION_H
#define GFXSTREAM_VULKAN_CONNECTION_H

#include <memory>

#include "GfxStreamConnection.h"
#include "VkEncoder.h"

class GfxStreamVulkanConnection : public GfxStreamConnection {
   public:
    GfxStreamVulkanConnection(gfxstream::guest::IOStream* stream);
    virtual ~GfxStreamVulkanConnection();
    void* getEncoder() override;

   private:
    std::unique_ptr<gfxstream::vk::VkEncoder> mVkEnc;
};

#endif
