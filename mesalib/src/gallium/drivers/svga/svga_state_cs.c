/*
 * Copyright (c) 2022-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_bitmask.h"
#include "translate/translate.h"

#include "svga_context.h"
#include "svga_cmd.h"
#include "svga_shader.h"
#include "svga_tgsi.h"


/**
 * Create compute shader compile key.
 */
static void
make_cs_key(struct svga_context *svga,
            struct svga_compile_key *key)
{
   struct svga_compute_shader *cs = svga->curr.cs;

   memset(key, 0, sizeof *key);

   svga_init_shader_key_common(svga, PIPE_SHADER_COMPUTE, &cs->base, key);

   key->cs.grid_size[0] = svga->curr.grid_info.size[0];
   key->cs.grid_size[1] = svga->curr.grid_info.size[1];
   key->cs.grid_size[2] = svga->curr.grid_info.size[2];
   key->cs.mem_size = cs->shared_mem_size;

   if (svga->curr.grid_info.indirect && cs->base.info.uses_grid_size) {
      struct pipe_transfer *transfer = NULL;
      const void *map = NULL;
      map = pipe_buffer_map(&svga->pipe, svga->curr.grid_info.indirect,
                            PIPE_MAP_READ, &transfer);
      memcpy(key->cs.grid_size, map, 3 * sizeof(uint));
      pipe_buffer_unmap(&svga->pipe, transfer);
   }
}


/**
 * Emit current compute shader to device.
 */
static enum pipe_error
emit_hw_cs(struct svga_context *svga, uint64_t dirty)
{
   struct svga_shader_variant *variant;
   struct svga_compute_shader *cs = svga->curr.cs;
   enum pipe_error ret = PIPE_OK;
   struct svga_compile_key key;

   assert(svga_have_sm5(svga));

   SVGA_STATS_TIME_PUSH(svga_sws(svga), SVGA_STATS_TIME_EMITCS);

   if (!cs) {
      if (svga->state.hw_draw.cs != NULL) {

         /** The previous compute shader is made inactive.
          *  Needs to unbind the compute shader.
          */
         ret = svga_set_shader(svga, SVGA3D_SHADERTYPE_CS, NULL);
         if (ret != PIPE_OK)
            goto done;
         svga->state.hw_draw.cs = NULL;
      }
      goto done;
   }

   make_cs_key(svga, &key);

   /* See if we already have a CS variant that matches the key */
   variant = svga_search_shader_key(&cs->base, &key);

   if (!variant) {
      ret = svga_compile_shader(svga, &cs->base, &key, &variant);
      if (ret != PIPE_OK)
         goto done;
   }

   if (variant != svga->state.hw_draw.cs) {
      /* Bind the new variant */
      ret = svga_set_shader(svga, SVGA3D_SHADERTYPE_CS, variant);
      if (ret != PIPE_OK)
         goto done;

      svga->rebind.flags.cs = false;
      svga->dirty |= SVGA_NEW_CS_VARIANT;
      svga->state.hw_draw.cs = variant;
   }

done:
   SVGA_STATS_TIME_POP(svga_sws(svga));
   return ret;
}

struct svga_tracked_state svga_hw_cs =
{
   "compute shader",
   (SVGA_NEW_CS |
    SVGA_NEW_TEXTURE_BINDING |
    SVGA_NEW_SAMPLER |
    SVGA_NEW_CS_RAW_BUFFER),
   emit_hw_cs
};
