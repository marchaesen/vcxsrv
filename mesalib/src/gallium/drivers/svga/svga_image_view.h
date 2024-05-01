/*
 * Copyright (c) 2022-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef SVGA_IMAGE_VIEW_H
#define SVGA_IMAGE_VIEW_H

struct svga_image_view {
   struct pipe_image_view desc;
   struct pipe_resource *resource;
   unsigned uav_index;
};

void
svga_init_shader_image_functions(struct svga_context *svga);

void
svga_cleanup_shader_image_state(struct svga_context *svga);

SVGA3dUAViewId
svga_create_uav(struct svga_context *svga,
                SVGA3dUAViewDesc *desc,
                SVGA3dSurfaceFormat svga_format,
                unsigned resourceDim,
                struct svga_winsys_surface *surf);

void
svga_destroy_uav(struct svga_context *svga);

enum pipe_error
svga_rebind_uav(struct svga_context *svga);

enum pipe_error
svga_validate_image_view_resources(struct svga_context *svga, unsigned count,
                                   struct svga_image_view *images,
                                   bool rebind);

SVGA3dUAViewId
svga_create_uav_image(struct svga_context *svga,
                      const struct pipe_image_view *image);

void
svga_uav_cache_purge_image_views(struct svga_context *svga);

#endif /* SVGA_IMAGE_VIEW_H */
