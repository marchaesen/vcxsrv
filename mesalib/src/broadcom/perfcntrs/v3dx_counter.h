/*
 * Copyright © 2024 Raspberry Pi Ltd
 * SPDX-License-Identifier: MIT
 */

/* This file generates the per-v3d-version function prototypes. */

struct v3d_device_info;
struct v3d_perfcntr_desc;
struct v3d_perfcntrs;

unsigned v3dX(perfcounters_num)(const struct v3d_device_info *devinfo);
struct v3d_perfcntr_desc *v3dX(perfcounters_get)(struct v3d_perfcntrs *perfcounters, unsigned index);
