#ifndef ZINK_INLINES_H
#define ZINK_INLINES_H

/* these go here to avoid include hell */
static inline void
zink_select_draw_vbo(struct zink_context *ctx)
{
   ctx->base.draw_vbo = ctx->draw_vbo[ctx->pipeline_changed[0]];
   assert(ctx->base.draw_vbo);
}

static inline void
zink_select_launch_grid(struct zink_context *ctx)
{
   ctx->base.launch_grid = ctx->launch_grid[ctx->pipeline_changed[1]];
   assert(ctx->base.launch_grid);
}

#endif
