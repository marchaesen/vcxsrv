/**********************************************************
 * Copyright 2014 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

#include "draw/draw_context.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_bitmask.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_text.h"

#include "svga_context.h"
#include "svga_cmd.h"
#include "svga_debug.h"
#include "svga_shader.h"
#include "svga_streamout.h"

static void *
svga_create_gs_state(struct pipe_context *pipe,
                     const struct pipe_shader_state *templ)
{
   struct svga_context *svga = svga_context(pipe);
   struct svga_geometry_shader *gs = CALLOC_STRUCT(svga_geometry_shader);

   if (!gs)
      return NULL;

   SVGA_STATS_TIME_PUSH(svga_sws(svga), SVGA_STATS_TIME_CREATEGS);

   gs->base.tokens = tgsi_dup_tokens(templ->tokens);

   /* Collect basic info that we'll need later:
    */
   tgsi_scan_shader(gs->base.tokens, &gs->base.info);

   gs->draw_shader = draw_create_geometry_shader(svga->swtnl.draw, templ);

   gs->base.id = svga->debug.shader_id++;

   gs->generic_outputs = svga_get_generic_outputs_mask(&gs->base.info);

   /* check for any stream output declarations */
   if (templ->stream_output.num_outputs) {
      gs->base.stream_output = svga_create_stream_output(svga, &gs->base,
                                                         &templ->stream_output);
   }

   SVGA_STATS_TIME_POP(svga_sws(svga));
   return gs;
}


static void
svga_bind_gs_state(struct pipe_context *pipe, void *shader)
{
   struct svga_geometry_shader *gs = (struct svga_geometry_shader *)shader;
   struct svga_context *svga = svga_context(pipe);

   svga->curr.user_gs = gs;
   svga->dirty |= SVGA_NEW_GS;
}


static void
svga_delete_gs_state(struct pipe_context *pipe, void *shader)
{
   struct svga_context *svga = svga_context(pipe);
   struct svga_geometry_shader *gs = (struct svga_geometry_shader *)shader;
   struct svga_geometry_shader *next_gs;
   struct svga_shader_variant *variant, *tmp;

   svga_hwtnl_flush_retry(svga);

   /* Start deletion from the original geometry shader state */
   if (gs->base.parent != NULL)
      gs = (struct svga_geometry_shader *)gs->base.parent;

   /* Free the list of geometry shaders */
   while (gs) {
      next_gs = (struct svga_geometry_shader *)gs->base.next;

      if (gs->base.stream_output != NULL)
         svga_delete_stream_output(svga, gs->base.stream_output);

      draw_delete_geometry_shader(svga->swtnl.draw, gs->draw_shader);

      for (variant = gs->base.variants; variant; variant = tmp) {
         tmp = variant->next;

         /* Check if deleting currently bound shader */
         if (variant == svga->state.hw_draw.gs) {
            SVGA_RETRY(svga, svga_set_shader(svga, SVGA3D_SHADERTYPE_GS, NULL));
            svga->state.hw_draw.gs = NULL;
         }

         svga_destroy_shader_variant(svga, variant);
      }

      FREE((void *)gs->base.tokens);
      FREE(gs);
      gs = next_gs;
   }
}


void
svga_init_gs_functions(struct svga_context *svga)
{
   svga->pipe.create_gs_state = svga_create_gs_state;
   svga->pipe.bind_gs_state = svga_bind_gs_state;
   svga->pipe.delete_gs_state = svga_delete_gs_state;
}
