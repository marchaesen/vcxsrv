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

#ifndef PVR_HW_PASS_H
#define PVR_HW_PASS_H

#include <stdbool.h>
#include <stdint.h>

struct pvr_device;
struct pvr_render_pass;

enum pvr_renderpass_surface_initop {
   RENDERPASS_SURFACE_INITOP_CLEAR,
   RENDERPASS_SURFACE_INITOP_LOAD,
   RENDERPASS_SURFACE_INITOP_NOP,
};

struct pvr_renderpass_hwsetup_subpass {
   /* If >=0 then copy the depth into this pixel output for all fragment
    * programs in the subpass.
    */
   int32_t z_replicate;

   /* The operation to perform on the depth at the start of the subpass. Loads
    * are deferred to subpasses when depth has been replicated
    */
   enum pvr_renderpass_surface_initop depth_initop;

   /* If true then clear the stencil at the start of the subpass. */
   bool stencil_clear;

   /* Driver Id from the input pvr_render_subpass structure. */
   uint32_t driver_id;

   /* For each color attachment to the subpass: the operation to perform at
    * the start of the subpass.
    */
   enum pvr_renderpass_surface_initop *color_initops;

   struct pvr_load_op *load_op;
};

struct pvr_renderpass_colorinit {
   /* Source surface for the operation. */
   uint32_t driver_id;

   /* Type of operation: either clear or load. */
   enum pvr_renderpass_surface_initop op;
};

/* FIXME: Adding these USC enums and structures here for now to avoid adding
 * usc.h header. Needs to be moved to compiler specific header.
 */
/* Specifies the location of render target writes. */
enum usc_mrt_resource_type {
   USC_MRT_RESOURCE_TYPE_INVALID = 0, /* explicitly treat 0 as invalid */
   USC_MRT_RESOURCE_TYPE_OUTPUT_REGISTER,
   USC_MRT_RESOURCE_TYPE_MEMORY,
};

struct usc_mrt_resource {
   /* Resource type allocated for render target. */
   enum usc_mrt_resource_type type;

   union {
      /* If type == USC_MRT_RESOURCE_TYPE_OUTPUT_REGISTER. */
      struct {
         /* The output register to use. */
         uint32_t out_reg;

         /* The offset in bytes into the output register. */
         uint32_t offset;
      } reg;

      /* If type == USC_MRT_RESOURCE_TYPE_MEMORY. */
      struct {
         /* The number of the tile buffer to use. */
         uint32_t tile_buffer;

         /* The offset in dwords within the tile buffer. */
         uint32_t offset_in_dwords;
      } mem;
   } u;
};

struct usc_mrt_setup {
   /* Number of render targets present. */
   uint32_t render_targets_count;

   /* Array of MRT resources allocated for each render target. The number of
    * elements is determined by usc_mrt_setup::render_targets_count.
    */
   struct usc_mrt_resource *mrt_resources;
};

enum pvr_resolve_type {
   PVR_RESOLVE_TYPE_INVALID = 0, /* explicitly treat 0 as invalid */
   PVR_RESOLVE_TYPE_PBE,
   PVR_RESOLVE_TYPE_TRANSFER,
};

struct pvr_renderpass_hwsetup_eot_surface {
   /* MRT index to store from. Also used to index into
    * usc_mrt_setup::mrt_resources.
    */
   uint32_t mrt_index;

   /* Index of pvr_render_pass_info::attachments to store into. */
   uint32_t attachment_index;

   /* True if the surface should be resolved. */
   bool need_resolve;

   /* How the surface should be resolved at the end of a render. Only valid if
    * pvr_renderpass_hwsetup_eot_surface::need_resolve is set to true.
    */
   enum pvr_resolve_type resolve_type;

   /* Index of pvr_render_pass_info::attachments to resolve from. Only valid if
    * pvr_renderpass_hwsetup_eot_surface::need_resolve is set to true.
    */
   uint32_t src_attachment_index;
};

struct pvr_renderpass_hwsetup_render {
   /* Number of pixel output registers to allocate for this render. */
   uint32_t output_regs_count;

   /* Number of tile buffers to allocate for this render. */
   uint32_t tile_buffers_count;

   /* Number of subpasses in this render. */
   uint32_t subpass_count;

   /* Description of each subpass. */
   struct pvr_renderpass_hwsetup_subpass *subpasses;

   /* The sample count of every color attachment (or depth attachment if
    * z-only) in this render
    */
   uint32_t sample_count;

   /* Driver Id for the surface to use for depth/stencil load/store in this
    * render.
    */
   int32_t ds_surface_id;

   /* Operation on the on-chip depth at the start of the render.
    * Either load from 'ds_surface_id', clear using 'ds_surface_id' or leave
    * uninitialized.
    */
   enum pvr_renderpass_surface_initop depth_init;

   /* Operation on the on-chip stencil at the start of the render. */
   enum pvr_renderpass_surface_initop stencil_init;

   /* For each operation: the destination in the on-chip color storage. */
   struct usc_mrt_setup init_setup;

   /* Count of operations on on-chip color storage at the start of the render.
    */
   uint32_t color_init_count;

   /* How to initialize render targets at the start of the render. */
   struct pvr_renderpass_colorinit *color_init;

   /* Describes the location of the source data for each stored surface. */
   struct usc_mrt_setup eot_setup;

   struct pvr_renderpass_hwsetup_eot_surface *eot_surfaces;
   uint32_t eot_surface_count;

   void *client_data;
};

struct pvr_renderpass_hw_map {
   uint32_t render;
   uint32_t subpass;
};

struct pvr_renderpass_hwsetup {
   /* Number of renders. */
   uint32_t render_count;

   /* Description of each render. */
   struct pvr_renderpass_hwsetup_render *renders;

   /* Maps indices from pvr_render_pass::subpasses to the
    * pvr_renderpass_hwsetup_render/pvr_renderpass_hwsetup_subpass relative to
    * that render where the subpass is scheduled.
    */
   struct pvr_renderpass_hw_map *subpass_map;
};

struct pvr_renderpass_hwsetup *
pvr_create_renderpass_hwsetup(struct pvr_device *device,
                              struct pvr_render_pass *pass,
                              bool disable_merge);
void pvr_destroy_renderpass_hwsetup(struct pvr_device *device,
                                    struct pvr_renderpass_hwsetup *hw_setup);

#endif /* PVR_HW_PASS_H */
