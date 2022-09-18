/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdbool.h>

#include "pvr_hw_pass.h"
#include "pvr_private.h"
#include "vk_alloc.h"

void pvr_destroy_renderpass_hwsetup(struct pvr_device *device,
                                    struct pvr_renderpass_hwsetup *hw_setup)
{
   vk_free(&device->vk.alloc, hw_setup);
}

struct pvr_renderpass_hwsetup *
pvr_create_renderpass_hwsetup(struct pvr_device *device,
                              struct pvr_render_pass *pass,
                              bool disable_merge)
{
   struct pvr_renderpass_hwsetup_eot_surface *eot_surface;
   enum pvr_renderpass_surface_initop *color_initops;
   struct pvr_renderpass_hwsetup_subpass *subpasses;
   struct pvr_renderpass_hwsetup_render *renders;
   struct pvr_renderpass_colorinit *color_inits;
   struct pvr_renderpass_hwsetup *hw_setup;
   struct pvr_renderpass_hw_map *subpass_map;
   struct usc_mrt_resource *mrt_resources;

   VK_MULTIALLOC(ma);
   vk_multialloc_add(&ma, &hw_setup, __typeof__(*hw_setup), 1);
   vk_multialloc_add(&ma, &renders, __typeof__(*renders), 1);
   vk_multialloc_add(&ma, &color_inits, __typeof__(*color_inits), 1);
   vk_multialloc_add(&ma, &subpass_map, __typeof__(*subpass_map), 1);
   vk_multialloc_add(&ma, &mrt_resources, __typeof__(*mrt_resources), 2);
   vk_multialloc_add(&ma, &subpasses, __typeof__(*subpasses), 1);
   vk_multialloc_add(&ma, &eot_surface, __typeof__(*eot_surface), 1);
   vk_multialloc_add(&ma,
                     &color_initops,
                     __typeof__(*color_initops),
                     pass->subpasses[0].color_count);
   /* Note, no more multialloc slots available (maximum supported is 8). */

   if (!vk_multialloc_zalloc(&ma,
                             &device->vk.alloc,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE)) {
      return NULL;
   }

   /* FIXME: Remove hardcoding of hw_setup structure. */
   subpasses[0].z_replicate = -1;
   subpasses[0].depth_initop = RENDERPASS_SURFACE_INITOP_CLEAR;
   subpasses[0].stencil_clear = false;
   subpasses[0].driver_id = 0;
   color_initops[0] = RENDERPASS_SURFACE_INITOP_NOP;
   subpasses[0].color_initops = color_initops;
   subpasses[0].load_op = NULL;
   renders[0].subpass_count = 1;
   renders[0].subpasses = subpasses;

   renders[0].sample_count = 1;
   renders[0].ds_surface_id = 1;
   renders[0].depth_init = RENDERPASS_SURFACE_INITOP_CLEAR;
   renders[0].stencil_init = RENDERPASS_SURFACE_INITOP_NOP;

   mrt_resources[0].type = USC_MRT_RESOURCE_TYPE_OUTPUT_REGISTER;
   mrt_resources[0].u.reg.out_reg = 0;
   mrt_resources[0].u.reg.offset = 0;
   renders[0].init_setup.render_targets_count = 1;
   renders[0].init_setup.mrt_resources = &mrt_resources[0];

   color_inits[0].op = RENDERPASS_SURFACE_INITOP_CLEAR;
   color_inits[0].driver_id = 0;
   renders[0].color_init_count = 1;
   renders[0].color_init = color_inits;

   mrt_resources[1].type = USC_MRT_RESOURCE_TYPE_OUTPUT_REGISTER;
   mrt_resources[1].u.reg.out_reg = 0;
   mrt_resources[1].u.reg.offset = 0;
   renders[0].eot_setup.render_targets_count = 1;
   renders[0].eot_setup.mrt_resources = &mrt_resources[1];

   eot_surface->mrt_index = 0;
   eot_surface->attachment_index = 0;
   eot_surface->need_resolve = false;
   eot_surface->resolve_type = PVR_RESOLVE_TYPE_INVALID;
   eot_surface->src_attachment_index = 0;
   renders[0].eot_surfaces = eot_surface;
   renders[0].eot_surface_count = 1;

   renders[0].output_regs_count = 1;
   renders[0].tile_buffers_count = 0;
   renders[0].client_data = NULL;
   hw_setup->render_count = 1;
   hw_setup->renders = renders;

   subpass_map->render = 0;
   subpass_map->subpass = 0;
   hw_setup->subpass_map = subpass_map;

   return hw_setup;
}
