/*
 * Copyright (c) 2010-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file
 * VMware SVGA public interface. Used by targets to create a stack.
 *
 * @author Jakob Bornecrantz Fonseca <jakob@vmware.com>
 */

#ifndef SVGA_PUBLIC_H_
#define SVGA_PUBLIC_H_

struct pipe_screen;
struct svga_winsys_screen;

struct pipe_screen *
svga_screen_create(struct svga_winsys_screen *sws);

#endif /* SVGA_PUBLIC_H_ */
