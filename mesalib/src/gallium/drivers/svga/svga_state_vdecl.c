/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#include "util/u_inlines.h"
#include "pipe/p_defines.h"
#include "util/u_math.h"
#include "util/u_upload_mgr.h"

#include "svga_context.h"
#include "svga_state.h"
#include "svga_draw.h"
#include "svga_tgsi.h"
#include "svga_screen.h"
#include "svga_shader.h"
#include "svga_resource_buffer.h"
#include "svga_hw_reg.h"



static enum pipe_error
emit_hw_vs_vdecl(struct svga_context *svga, uint64_t dirty)
{
   const struct pipe_vertex_element *ve = svga->curr.velems->velem;
   SVGA3dVertexDecl decls[SVGA3D_INPUTREG_MAX];
   unsigned buffer_indexes[SVGA3D_INPUTREG_MAX];
   unsigned i;
   unsigned neg_bias = 0;

   assert(svga->curr.velems->count >=
          svga->curr.vs->base.info.num_inputs);

   /**
    * We can't set the VDECL offset to something negative, so we
    * must calculate a common negative additional index bias, and modify
    * the VDECL offsets accordingly so they *all* end up positive.
    *
    * Note that the exact value of the negative index bias is not that
    * important, since we compensate for it when we calculate the vertex
    * buffer offset below. The important thing is that all vertex buffer
    * offsets remain positive.
    *
    * Note that we use a negative bias variable in order to make the
    * rounding maths more easy to follow, and to avoid int / unsigned
    * confusion.
    */

   for (i = 0; i < svga->curr.velems->count; i++) {
      const struct pipe_vertex_buffer *vb =
         &svga->curr.vb[ve[i].vertex_buffer_index];
      struct svga_buffer *buffer;
      unsigned int offset = vb->buffer_offset + ve[i].src_offset;
      unsigned tmp_neg_bias = 0;

      if (!vb->buffer.resource)
         continue;

      buffer = svga_buffer(vb->buffer.resource);
      if (buffer->uploaded.start > offset) {
         tmp_neg_bias = buffer->uploaded.start - offset;
         if (ve[i].src_stride)
            tmp_neg_bias = (tmp_neg_bias + ve[i].src_stride - 1) / ve[i].src_stride;
         neg_bias = MAX2(neg_bias, tmp_neg_bias);
      }
   }

   for (i = 0; i < svga->curr.velems->count; i++) {
      const struct pipe_vertex_buffer *vb =
         &svga->curr.vb[ve[i].vertex_buffer_index];
      unsigned usage, index;
      struct svga_buffer *buffer;

      if (!vb->buffer.resource)
         continue;

      buffer = svga_buffer(vb->buffer.resource);
      svga_generate_vdecl_semantics( i, &usage, &index );

      /* SVGA_NEW_VELEMENT
       */
      decls[i].identity.type = svga->curr.velems->decl_type[i];
      decls[i].identity.method = SVGA3D_DECLMETHOD_DEFAULT;
      decls[i].identity.usage = usage;
      decls[i].identity.usageIndex = index;
      decls[i].array.stride = ve[i].src_stride;

      /* Compensate for partially uploaded vbo, and
       * for the negative index bias.
       */
      decls[i].array.offset = (vb->buffer_offset
                           + ve[i].src_offset
			   + neg_bias * ve[i].src_stride
			   - buffer->uploaded.start);

      assert(decls[i].array.offset >= 0);

      buffer_indexes[i] = ve[i].vertex_buffer_index;

      assert(!buffer->uploaded.buffer);
   }

   svga_hwtnl_vertex_decls(svga->hwtnl,
                           svga->curr.velems->count,
                           decls,
                           buffer_indexes,
                           svga->curr.velems->id);

   svga_hwtnl_vertex_buffers(svga->hwtnl,
                             svga->curr.num_vertex_buffers,
                             svga->curr.vb);

   svga_hwtnl_set_index_bias( svga->hwtnl, -(int) neg_bias );
   return PIPE_OK;
}


static enum pipe_error
emit_hw_vdecl(struct svga_context *svga, uint64_t dirty)
{
   /* SVGA_NEW_NEED_SWTNL
    */
   if (svga->state.sw.need_swtnl)
      return PIPE_OK; /* Do not emit during swtnl */

   return emit_hw_vs_vdecl( svga, dirty );
}


struct svga_tracked_state svga_hw_vdecl =
{
   "hw vertex decl state (hwtnl version)",
   ( SVGA_NEW_NEED_SWTNL |
     SVGA_NEW_VELEMENT |
     SVGA_NEW_VBUFFER |
     SVGA_NEW_RAST |
     SVGA_NEW_FS |
     SVGA_NEW_VS ),
   emit_hw_vdecl
};
