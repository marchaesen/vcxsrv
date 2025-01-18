/*
 * Copyright (c) 2009-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @author Jose Fonseca <jfonseca@vmware.com>
 */


#ifndef VMW_CONTEXT_H_
#define VMW_CONTEXT_H_

#include <stdio.h>
#include "util/compiler.h"

struct svga_winsys_screen;
struct svga_winsys_context;
struct pipe_context;
struct pipe_screen;


/** Set to 1 to get extra debug info/output */
#define VMW_DEBUG 0

#if VMW_DEBUG
#define vmw_printf debug_printf
#define VMW_FUNC  debug_printf("%s\n", __func__)
#else
#define VMW_FUNC
#define vmw_printf(...)
#endif


/**
 * Called when an error/failure is encountered.
 * We want these messages reported for all build types.
 */
#define vmw_error(...)  fprintf(stderr, "VMware: " __VA_ARGS__)


struct svga_winsys_context *
vmw_svga_winsys_context_create(struct svga_winsys_screen *sws);

struct vmw_svga_winsys_surface;


void
vmw_swc_surface_clear_reference(struct svga_winsys_context *swc,
                                struct vmw_svga_winsys_surface *vsurf);

void
vmw_swc_unref(struct svga_winsys_context *swc);

void
vmw_swc_surface_clear_userspace_id(struct svga_winsys_context *swc,
                                   uint32_t sid);

uint32_t
vmw_swc_surface_add_userspace_id(struct svga_winsys_context *swc);

#endif /* VMW_CONTEXT_H_ */
