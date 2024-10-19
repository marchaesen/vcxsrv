/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "hk_physical_device.h"

VkResult hk_init_wsi(struct hk_physical_device *pdev);
void hk_finish_wsi(struct hk_physical_device *pdev);
