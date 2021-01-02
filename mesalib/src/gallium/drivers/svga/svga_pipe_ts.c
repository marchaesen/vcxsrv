/**********************************************************
 * Copyright 2018-2020 VMware, Inc.  All rights reserved.
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

#include "pipe/p_context.h"
#include "util/u_memory.h"
#include "tgsi/tgsi_parse.h"

#include "svga_context.h"
#include "svga_shader.h"

static void
svga_set_tess_state(struct pipe_context *pipe,
                    const float default_outer_level[4],
                    const float default_inner_level[2])
{
   struct svga_context *svga = svga_context(pipe);
   unsigned i;

   for (i = 0; i < 4; i++) {
      svga->curr.default_tesslevels[i] = default_outer_level[i];
   }
   for (i = 0; i < 2; i++) {
      svga->curr.default_tesslevels[i + 4] = default_inner_level[i];
   }
}


static void *
svga_create_tcs_state(struct pipe_context *pipe,
                      const struct pipe_shader_state *templ)
{
   struct svga_context *svga = svga_context(pipe);
   struct svga_tcs_shader *tcs;

   tcs = CALLOC_STRUCT(svga_tcs_shader);
   if (!tcs)
      return NULL;

   SVGA_STATS_TIME_PUSH(svga_sws(svga), SVGA_STATS_TIME_CREATETCS);

   tcs->base.tokens = tgsi_dup_tokens(templ->tokens);

   /* Collect basic info that we'll need later:
    */
   tgsi_scan_shader(tcs->base.tokens, &tcs->base.info);

   tcs->base.id = svga->debug.shader_id++;

   tcs->generic_outputs = svga_get_generic_outputs_mask(&tcs->base.info);

   SVGA_STATS_TIME_POP(svga_sws(svga));
   return tcs;
}


static void
svga_bind_tcs_state(struct pipe_context *pipe, void *shader)
{
   struct svga_tcs_shader *tcs = (struct svga_tcs_shader *) shader;
   struct svga_context *svga = svga_context(pipe);

   if (tcs == svga->curr.tcs)
      return;

   svga->curr.tcs = tcs;
   svga->dirty |= SVGA_NEW_TCS;
}


static void
svga_delete_tcs_state(struct pipe_context *pipe, void *shader)
{
   struct svga_context *svga = svga_context(pipe);
   struct svga_tcs_shader *tcs = (struct svga_tcs_shader *) shader;
   struct svga_tcs_shader *next_tcs;
   struct svga_shader_variant *variant, *tmp;

   svga_hwtnl_flush_retry(svga);

   assert(tcs->base.parent == NULL);

   while (tcs) {
      next_tcs = (struct svga_tcs_shader *)tcs->base.next;
      for (variant = tcs->base.variants; variant; variant = tmp) {
         tmp = variant->next;

         /* Check if deleting currently bound shader */
         if (variant == svga->state.hw_draw.tcs) {
            SVGA_RETRY(svga, svga_set_shader(svga, SVGA3D_SHADERTYPE_HS, NULL));
            svga->state.hw_draw.tcs = NULL;
         }

         svga_destroy_shader_variant(svga, variant);
      }

      FREE((void *)tcs->base.tokens);
      FREE(tcs);
      tcs = next_tcs;
   }
}


void
svga_cleanup_tcs_state(struct svga_context *svga)
{
   if (svga->tcs.passthrough_tcs) {
      svga_delete_tcs_state(&svga->pipe, svga->tcs.passthrough_tcs);
   }
}


static void *
svga_create_tes_state(struct pipe_context *pipe,
                      const struct pipe_shader_state *templ)
{
   struct svga_context *svga = svga_context(pipe);
   struct svga_tes_shader *tes;

   tes = CALLOC_STRUCT(svga_tes_shader);
   if (!tes)
      return NULL;

   SVGA_STATS_TIME_PUSH(svga_sws(svga), SVGA_STATS_TIME_CREATETES);

   tes->base.tokens = tgsi_dup_tokens(templ->tokens);

   /* Collect basic info that we'll need later:
    */
   tgsi_scan_shader(tes->base.tokens, &tes->base.info);

   tes->base.id = svga->debug.shader_id++;

   tes->generic_inputs = svga_get_generic_inputs_mask(&tes->base.info);

   SVGA_STATS_TIME_POP(svga_sws(svga));
   return tes;
}


static void
svga_bind_tes_state(struct pipe_context *pipe, void *shader)
{
   struct svga_tes_shader *tes = (struct svga_tes_shader *) shader;
   struct svga_context *svga = svga_context(pipe);

   if (tes == svga->curr.tes)
      return;

   svga->curr.tes = tes;
   svga->dirty |= SVGA_NEW_TES;
}


static void
svga_delete_tes_state(struct pipe_context *pipe, void *shader)
{
   struct svga_context *svga = svga_context(pipe);
   struct svga_tes_shader *tes = (struct svga_tes_shader *) shader;
   struct svga_tes_shader *next_tes;
   struct svga_shader_variant *variant, *tmp;

   svga_hwtnl_flush_retry(svga);

   assert(tes->base.parent == NULL);

   while (tes) {
      next_tes = (struct svga_tes_shader *)tes->base.next;
      for (variant = tes->base.variants; variant; variant = tmp) {
         tmp = variant->next;

         /* Check if deleting currently bound shader */
         if (variant == svga->state.hw_draw.tes) {
            SVGA_RETRY(svga, svga_set_shader(svga, SVGA3D_SHADERTYPE_DS, NULL));
            svga->state.hw_draw.tes = NULL;
         }

         svga_destroy_shader_variant(svga, variant);
      }

      FREE((void *)tes->base.tokens);
      FREE(tes);
      tes = next_tes;
   }
}


void
svga_init_ts_functions(struct svga_context *svga)
{
   svga->pipe.set_tess_state = svga_set_tess_state;
   svga->pipe.create_tcs_state = svga_create_tcs_state;
   svga->pipe.bind_tcs_state = svga_bind_tcs_state;
   svga->pipe.delete_tcs_state = svga_delete_tcs_state;
   svga->pipe.create_tes_state = svga_create_tes_state;
   svga->pipe.bind_tes_state = svga_bind_tes_state;
   svga->pipe.delete_tes_state = svga_delete_tes_state;
}
