#include "zink_batch.h"

#include "zink_context.h"
#include "zink_fence.h"
#include "zink_framebuffer.h"
#include "zink_query.h"
#include "zink_program.h"
#include "zink_render_pass.h"
#include "zink_resource.h"
#include "zink_screen.h"

#include "util/hash_table.h"
#include "util/u_debug.h"
#include "util/set.h"

static void
reset_batch(struct zink_context *ctx, struct zink_batch *batch)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   batch->descs_left = ZINK_BATCH_DESC_SIZE;

   // cmdbuf hasn't been submitted before
   if (!batch->fence)
      return;

   zink_fence_finish(screen, batch->fence, PIPE_TIMEOUT_INFINITE);
   zink_fence_reference(screen, &batch->fence, NULL);

   zink_render_pass_reference(screen, &batch->rp, NULL);
   zink_framebuffer_reference(screen, &batch->fb, NULL);
   set_foreach(batch->programs, entry) {
      struct zink_gfx_program *prog = (struct zink_gfx_program*)entry->key;
      zink_gfx_program_reference(screen, &prog, NULL);
   }
   _mesa_set_clear(batch->programs, NULL);

   /* unref all used resources */
   set_foreach(batch->resources, entry) {
      struct pipe_resource *pres = (struct pipe_resource *)entry->key;
      pipe_resource_reference(&pres, NULL);
   }
   _mesa_set_clear(batch->resources, NULL);

   /* unref all used sampler-views */
   set_foreach(batch->sampler_views, entry) {
      struct pipe_sampler_view *pres = (struct pipe_sampler_view *)entry->key;
      pipe_sampler_view_reference(&pres, NULL);
   }
   _mesa_set_clear(batch->sampler_views, NULL);

   util_dynarray_foreach(&batch->zombie_samplers, VkSampler, samp) {
      vkDestroySampler(screen->dev, *samp, NULL);
   }
   util_dynarray_clear(&batch->zombie_samplers);

   if (vkResetDescriptorPool(screen->dev, batch->descpool, 0) != VK_SUCCESS)
      fprintf(stderr, "vkResetDescriptorPool failed\n");
}

void
zink_start_batch(struct zink_context *ctx, struct zink_batch *batch)
{
   reset_batch(ctx, batch);

   VkCommandBufferBeginInfo cbbi = {};
   cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   if (vkBeginCommandBuffer(batch->cmdbuf, &cbbi) != VK_SUCCESS)
      debug_printf("vkBeginCommandBuffer failed\n");

   if (!ctx->queries_disabled)
      zink_resume_queries(ctx, batch);
}

void
zink_end_batch(struct zink_context *ctx, struct zink_batch *batch)
{
   if (!ctx->queries_disabled)
      zink_suspend_queries(ctx, batch);

   if (vkEndCommandBuffer(batch->cmdbuf) != VK_SUCCESS) {
      debug_printf("vkEndCommandBuffer failed\n");
      return;
   }

   assert(batch->fence == NULL);
   batch->fence = zink_create_fence(ctx->base.screen, batch);
   if (!batch->fence)
      return;

   VkSubmitInfo si = {};
   si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   si.waitSemaphoreCount = 0;
   si.pWaitSemaphores = NULL;
   si.signalSemaphoreCount = 0;
   si.pSignalSemaphores = NULL;
   si.pWaitDstStageMask = NULL;
   si.commandBufferCount = 1;
   si.pCommandBuffers = &batch->cmdbuf;

   if (vkQueueSubmit(ctx->queue, 1, &si, batch->fence->fence) != VK_SUCCESS) {
      debug_printf("ZINK: vkQueueSubmit() failed\n");
      ctx->is_device_lost = true;

      if (ctx->reset.reset) {
         ctx->reset.reset(ctx->reset.data, PIPE_GUILTY_CONTEXT_RESET);
      }
   }
}

void
zink_batch_reference_resource_rw(struct zink_batch *batch, struct zink_resource *res, bool write)
{
   unsigned mask = write ? ZINK_RESOURCE_ACCESS_WRITE : ZINK_RESOURCE_ACCESS_READ;

   /* u_transfer_helper unrefs the stencil buffer when the depth buffer is unrefed,
    * so we add an extra ref here to the stencil buffer to compensate
    */
   struct zink_resource *stencil;

   zink_get_depth_stencil_resources((struct pipe_resource*)res, NULL, &stencil);


   struct set_entry *entry = _mesa_set_search(batch->resources, res);
   if (!entry) {
      entry = _mesa_set_add(batch->resources, res);
      pipe_reference(NULL, &res->base.reference);
      if (stencil)
         pipe_reference(NULL, &stencil->base.reference);
   }
   /* the batch_uses value for this batch is guaranteed to not be in use now because
    * reset_batch() waits on the fence and removes access before resetting
    */
   res->batch_uses[batch->batch_id] |= mask;

   if (stencil)
      stencil->batch_uses[batch->batch_id] |= mask;
}

void
zink_batch_reference_sampler_view(struct zink_batch *batch,
                                  struct zink_sampler_view *sv)
{
   struct set_entry *entry = _mesa_set_search(batch->sampler_views, sv);
   if (!entry) {
      entry = _mesa_set_add(batch->sampler_views, sv);
      pipe_reference(NULL, &sv->base.reference);
   }
}

void
zink_batch_reference_program(struct zink_batch *batch,
                             struct zink_gfx_program *prog)
{
   struct set_entry *entry = _mesa_set_search(batch->programs, prog);
   if (!entry) {
      entry = _mesa_set_add(batch->programs, prog);
      pipe_reference(NULL, &prog->reference);
   }
}
