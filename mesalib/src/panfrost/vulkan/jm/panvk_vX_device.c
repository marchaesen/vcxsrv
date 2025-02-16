/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_device.h"

VkResult
panvk_per_arch(device_check_status)(struct vk_device *vk_dev)
{
   struct panvk_device *dev = to_panvk_device(vk_dev);
   return panvk_common_check_status(dev);
}
