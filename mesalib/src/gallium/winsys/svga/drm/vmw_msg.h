/*
 * Copyright (c) 2016-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef _VMW_MSG_H
#define _VMW_MSG_H

/**
 * vmw_host_log: Sends a log message to the host
 *
 * @log: NULL terminated string
 *
 */
void vmw_svga_winsys_host_log(struct svga_winsys_screen *sws, const char *log);

#endif

