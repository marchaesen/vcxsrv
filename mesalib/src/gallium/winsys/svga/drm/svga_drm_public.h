/*
 * Copyright (c) 2010-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file
 * VMware SVGA DRM winsys public interface. Used by targets to create a stack.
 *
 * @author Jakob Bornecrantz Fonseca <jakob@vmware.com>
 */

#ifndef SVGA_DRM_PUBLIC_H_
#define SVGA_DRM_PUBLIC_H_

struct svga_winsys_screen;

struct svga_winsys_screen *
svga_drm_winsys_screen_create(int fd);

#endif /* SVGA_PUBLIC_H_ */
