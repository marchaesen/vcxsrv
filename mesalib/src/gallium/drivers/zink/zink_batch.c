#include "zink_batch.h"

#include "zink_context.h"
#include "zink_fence.h"
#include "zink_framebuffer.h"
#include "zink_query.h"
#include "zink_program.h"
#include "zink_render_pass.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "zink_surface.h"

#include "util/hash_table.h"
#include "util/u_debug.h"
#include "util/set.h"

#ifdef VK_USE_PLATFORM_METAL_EXT
#include "QuartzCore/CAMetalLayer.h"
#endif
#include "wsi_common.h"

void
debug_describe_zink_batch_state(char *buf, const struct zink_batch_state *ptr)
{
   sprintf(buf, "zink_batch_state");
}

void
zink_reset_batch_state(struct zink_context *ctx, struct zink_batch_state *bs)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);

   if (VKSCR(ResetCommandPool)(screen->dev, bs->cmdpool, 0) != VK_SUCCESS)
      debug_printf("vkResetCommandPool failed\n");

   /* unref all used resources */
   set_foreach_remove(bs->resources, entry) {
      struct zink_resource_object *obj = (struct zink_resource_object *)entry->key;
      if (!zink_resource_object_usage_unset(obj, bs)) {
         obj->unordered_barrier = false;
         obj->access = 0;
         obj->access_stage = 0;
      }
      util_dynarray_append(&bs->unref_resources, struct zink_resource_object*, obj);
   }

   for (unsigned i = 0; i < 2; i++) {
      while (util_dynarray_contains(&bs->bindless_releases[i], uint32_t)) {
         uint32_t handle = util_dynarray_pop(&bs->bindless_releases[i], uint32_t);
         bool is_buffer = ZINK_BINDLESS_IS_BUFFER(handle);
         struct util_idalloc *ids = i ? &ctx->di.bindless[is_buffer].img_slots : &ctx->di.bindless[is_buffer].tex_slots;
         util_idalloc_free(ids, is_buffer ? handle - ZINK_MAX_BINDLESS_HANDLES : handle);
      }
   }

   set_foreach_remove(bs->active_queries, entry) {
      struct zink_query *query = (void*)entry->key;
      zink_prune_query(screen, bs, query);
   }

   set_foreach_remove(bs->surfaces, entry) {
      struct zink_surface *surf = (struct zink_surface *)entry->key;
      zink_batch_usage_unset(&surf->batch_uses, bs);
      zink_surface_reference(screen, &surf, NULL);
   }
   set_foreach_remove(bs->bufferviews, entry) {
      struct zink_buffer_view *buffer_view = (struct zink_buffer_view *)entry->key;
      zink_batch_usage_unset(&buffer_view->batch_uses, bs);
      zink_buffer_view_reference(screen, &buffer_view, NULL);
   }

   util_dynarray_foreach(&bs->dead_framebuffers, struct zink_framebuffer*, fb) {
      zink_framebuffer_reference(screen, fb, NULL);
   }
   util_dynarray_clear(&bs->dead_framebuffers);
   util_dynarray_foreach(&bs->zombie_samplers, VkSampler, samp) {
      VKSCR(DestroySampler)(screen->dev, *samp, NULL);
   }
   util_dynarray_clear(&bs->zombie_samplers);
   util_dynarray_clear(&bs->persistent_resources);

   screen->batch_descriptor_reset(screen, bs);

   set_foreach_remove(bs->programs, entry) {
      struct zink_program *pg = (struct zink_program*)entry->key;
      zink_batch_usage_unset(&pg->batch_uses, bs);
      if (pg->is_compute) {
         struct zink_compute_program *comp = (struct zink_compute_program*)pg;
         zink_compute_program_reference(ctx, &comp, NULL);
      } else {
         struct zink_gfx_program *prog = (struct zink_gfx_program*)pg;
         zink_gfx_program_reference(ctx, &prog, NULL);
      }
   }

   pipe_resource_reference(&bs->flush_res, NULL);

   bs->resource_size = 0;
   bs->signal_semaphore = VK_NULL_HANDLE;
   util_dynarray_clear(&bs->wait_semaphores);
   util_dynarray_clear(&bs->wait_semaphore_stages);

   /* only reset submitted here so that tc fence desync can pick up the 'completed' flag
    * before the state is reused
    */
   bs->fence.submitted = false;
   bs->has_barriers = false;
   bs->scanout_flush = false;
   if (bs->fence.batch_id)
      zink_screen_update_last_finished(screen, bs->fence.batch_id);
   bs->submit_count++;
   bs->fence.batch_id = 0;
   bs->usage.usage = 0;
   bs->next = NULL;
}

static void
unref_resources(struct zink_screen *screen, struct zink_batch_state *bs)
{
   while (util_dynarray_contains(&bs->unref_resources, struct zink_resource_object*)) {
      struct zink_resource_object *obj = util_dynarray_pop(&bs->unref_resources, struct zink_resource_object*);
      zink_resource_object_reference(screen, &obj, NULL);
   }
}

void
zink_clear_batch_state(struct zink_context *ctx, struct zink_batch_state *bs)
{
   bs->fence.completed = true;
   zink_reset_batch_state(ctx, bs);
   unref_resources(zink_screen(ctx->base.screen), bs);
}

static void
pop_batch_state(struct zink_context *ctx)
{
   const struct zink_batch_state *bs = ctx->batch_states;
   ctx->batch_states = bs->next;
   ctx->batch_states_count--;
   if (ctx->last_fence == &bs->fence)
      ctx->last_fence = NULL;
}

void
zink_batch_reset_all(struct zink_context *ctx)
{
   simple_mtx_lock(&ctx->batch_mtx);
   while (ctx->batch_states) {
      struct zink_batch_state *bs = ctx->batch_states;
      bs->fence.completed = true;
      pop_batch_state(ctx);
      zink_reset_batch_state(ctx, bs);
      util_dynarray_append(&ctx->free_batch_states, struct zink_batch_state *, bs);
   }
   simple_mtx_unlock(&ctx->batch_mtx);
}

void
zink_batch_state_destroy(struct zink_screen *screen, struct zink_batch_state *bs)
{
   if (!bs)
      return;

   util_queue_fence_destroy(&bs->flush_completed);

   cnd_destroy(&bs->usage.flush);
   mtx_destroy(&bs->usage.mtx);

   if (bs->fence.fence)
      VKSCR(DestroyFence)(screen->dev, bs->fence.fence, NULL);

   if (bs->cmdbuf)
      VKSCR(FreeCommandBuffers)(screen->dev, bs->cmdpool, 1, &bs->cmdbuf);
   if (bs->barrier_cmdbuf)
      VKSCR(FreeCommandBuffers)(screen->dev, bs->cmdpool, 1, &bs->barrier_cmdbuf);
   if (bs->cmdpool)
      VKSCR(DestroyCommandPool)(screen->dev, bs->cmdpool, NULL);

   util_dynarray_fini(&bs->zombie_samplers);
   util_dynarray_fini(&bs->dead_framebuffers);
   util_dynarray_fini(&bs->unref_resources);
   util_dynarray_fini(&bs->bindless_releases[0]);
   util_dynarray_fini(&bs->bindless_releases[1]);
   _mesa_set_destroy(bs->surfaces, NULL);
   _mesa_set_destroy(bs->bufferviews, NULL);
   _mesa_set_destroy(bs->programs, NULL);
   _mesa_set_destroy(bs->active_queries, NULL);
   screen->batch_descriptor_deinit(screen, bs);
   ralloc_free(bs);
}

static struct zink_batch_state *
create_batch_state(struct zink_context *ctx)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_batch_state *bs = rzalloc(NULL, struct zink_batch_state);
   bs->have_timelines = ctx->have_timelines;
   VkCommandPoolCreateInfo cpci = {0};
   cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   cpci.queueFamilyIndex = screen->gfx_queue;
   if (VKSCR(CreateCommandPool)(screen->dev, &cpci, NULL, &bs->cmdpool) != VK_SUCCESS)
      goto fail;

   VkCommandBufferAllocateInfo cbai = {0};
   cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   cbai.commandPool = bs->cmdpool;
   cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
   cbai.commandBufferCount = 1;

   if (VKSCR(AllocateCommandBuffers)(screen->dev, &cbai, &bs->cmdbuf) != VK_SUCCESS)
      goto fail;

   if (VKSCR(AllocateCommandBuffers)(screen->dev, &cbai, &bs->barrier_cmdbuf) != VK_SUCCESS)
      goto fail;

#define SET_CREATE_OR_FAIL(ptr) \
   ptr = _mesa_pointer_set_create(bs); \
   if (!ptr) \
      goto fail

   bs->ctx = ctx;

   SET_CREATE_OR_FAIL(bs->resources);
   SET_CREATE_OR_FAIL(bs->surfaces);
   SET_CREATE_OR_FAIL(bs->bufferviews);
   SET_CREATE_OR_FAIL(bs->programs);
   SET_CREATE_OR_FAIL(bs->active_queries);
   util_dynarray_init(&bs->wait_semaphores, NULL);
   util_dynarray_init(&bs->wait_semaphore_stages, NULL);
   util_dynarray_init(&bs->zombie_samplers, NULL);
   util_dynarray_init(&bs->dead_framebuffers, NULL);
   util_dynarray_init(&bs->persistent_resources, NULL);
   util_dynarray_init(&bs->unref_resources, NULL);
   util_dynarray_init(&bs->bindless_releases[0], NULL);
   util_dynarray_init(&bs->bindless_releases[1], NULL);

   cnd_init(&bs->usage.flush);
   mtx_init(&bs->usage.mtx, mtx_plain);

   if (!screen->batch_descriptor_init(screen, bs))
      goto fail;

   VkFenceCreateInfo fci = {0};
   fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

   if (VKSCR(CreateFence)(screen->dev, &fci, NULL, &bs->fence.fence) != VK_SUCCESS)
      goto fail;

   util_queue_fence_init(&bs->flush_completed);
   bs->queue = screen->threaded ? screen->thread_queue : screen->queue;

   return bs;
fail:
   zink_batch_state_destroy(screen, bs);
   return NULL;
}

static inline bool
find_unused_state(struct zink_batch_state *bs)
{
   struct zink_fence *fence = &bs->fence;
   /* we can't reset these from fence_finish because threads */
   bool completed = p_atomic_read(&fence->completed);
   bool submitted = p_atomic_read(&fence->submitted);
   return submitted && completed;
}

static struct zink_batch_state *
get_batch_state(struct zink_context *ctx, struct zink_batch *batch)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_batch_state *bs = NULL;

   simple_mtx_lock(&ctx->batch_mtx);
   if (util_dynarray_num_elements(&ctx->free_batch_states, struct zink_batch_state*))
      bs = util_dynarray_pop(&ctx->free_batch_states, struct zink_batch_state*);
   if (!bs && ctx->batch_states) {
      /* states are stored sequentially, so if the first one doesn't work, none of them will */
      if (zink_screen_check_last_finished(screen, ctx->batch_states->fence.batch_id) ||
          find_unused_state(ctx->batch_states)) {
         bs = ctx->batch_states;
         pop_batch_state(ctx);
      }
   }
   simple_mtx_unlock(&ctx->batch_mtx);
   if (bs) {
      if (bs->fence.submitted && !bs->fence.completed)
         /* this fence is already done, so we need vulkan to release the cmdbuf */
         zink_vkfence_wait(screen, &bs->fence, PIPE_TIMEOUT_INFINITE);
      zink_reset_batch_state(ctx, bs);
   } else {
      if (!batch->state) {
         /* this is batch init, so create a few more states for later use */
         for (int i = 0; i < 3; i++) {
            struct zink_batch_state *state = create_batch_state(ctx);
            util_dynarray_append(&ctx->free_batch_states, struct zink_batch_state *, state);
         }
      }
      bs = create_batch_state(ctx);
   }
   return bs;
}

void
zink_reset_batch(struct zink_context *ctx, struct zink_batch *batch)
{
   batch->state = get_batch_state(ctx, batch);
   assert(batch->state);

   batch->has_work = false;
}

void
zink_start_batch(struct zink_context *ctx, struct zink_batch *batch)
{
   zink_reset_batch(ctx, batch);

   batch->state->usage.unflushed = true;

   VkCommandBufferBeginInfo cbbi = {0};
   cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   if (VKCTX(BeginCommandBuffer)(batch->state->cmdbuf, &cbbi) != VK_SUCCESS)
      debug_printf("vkBeginCommandBuffer failed\n");
   if (VKCTX(BeginCommandBuffer)(batch->state->barrier_cmdbuf, &cbbi) != VK_SUCCESS)
      debug_printf("vkBeginCommandBuffer failed\n");

   batch->state->fence.completed = false;
   if (ctx->last_fence) {
      struct zink_batch_state *last_state = zink_batch_state(ctx->last_fence);
      batch->last_batch_usage = &last_state->usage;
   }

   if (!ctx->queries_disabled)
      zink_resume_queries(ctx, batch);
}

static void
post_submit(void *data, void *gdata, int thread_index)
{
   struct zink_batch_state *bs = data;
   struct zink_screen *screen = zink_screen(bs->ctx->base.screen);

   if (bs->is_device_lost) {
      if (bs->ctx->reset.reset)
         bs->ctx->reset.reset(bs->ctx->reset.data, PIPE_GUILTY_CONTEXT_RESET);
      screen->device_lost = true;
   } else if (bs->ctx->batch_states_count > screen->max_fences) {
      zink_screen_batch_id_wait(screen, bs->fence.batch_id - (screen->max_fences / 2), PIPE_TIMEOUT_INFINITE);
   }
}

static void
submit_queue(void *data, void *gdata, int thread_index)
{
   struct zink_batch_state *bs = data;
   struct zink_context *ctx = bs->ctx;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VkSubmitInfo si = {0};

   while (!bs->fence.batch_id)
      bs->fence.batch_id = p_atomic_inc_return(&screen->curr_batch);
   bs->usage.usage = bs->fence.batch_id;
   bs->usage.unflushed = false;

   if (ctx->have_timelines && screen->last_finished > bs->fence.batch_id && bs->fence.batch_id == 1) {
      if (!zink_screen_init_semaphore(screen)) {
         debug_printf("timeline init failed, things are about to go dramatically wrong.");
         ctx->have_timelines = false;
      }
   }

   VKSCR(ResetFences)(screen->dev, 1, &bs->fence.fence);

   uint64_t batch_id = bs->fence.batch_id;
   si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   si.waitSemaphoreCount = util_dynarray_num_elements(&bs->wait_semaphores, VkSemaphore);
   si.pWaitSemaphores = bs->wait_semaphores.data;
   si.pWaitDstStageMask = bs->wait_semaphore_stages.data;
   si.commandBufferCount = bs->has_barriers ? 2 : 1;
   VkCommandBuffer cmdbufs[2] = {
      bs->barrier_cmdbuf,
      bs->cmdbuf,
   };
   si.pCommandBuffers = bs->has_barriers ? cmdbufs : &cmdbufs[1];

   VkSemaphore signals[2];
   si.signalSemaphoreCount = !!bs->signal_semaphore;
   signals[0] = bs->signal_semaphore;
   si.pSignalSemaphores = signals;
   VkTimelineSemaphoreSubmitInfo tsi = {0};
   uint64_t signal_values[2] = {0};
   if (bs->have_timelines) {
      tsi.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
      si.pNext = &tsi;
      tsi.pSignalSemaphoreValues = signal_values;
      signal_values[si.signalSemaphoreCount] = batch_id;
      signals[si.signalSemaphoreCount++] = screen->sem;
      tsi.signalSemaphoreValueCount = si.signalSemaphoreCount;
   }

   struct wsi_memory_signal_submit_info mem_signal = {
      .sType = VK_STRUCTURE_TYPE_WSI_MEMORY_SIGNAL_SUBMIT_INFO_MESA,
      .pNext = si.pNext,
   };

   if (bs->flush_res && screen->needs_mesa_flush_wsi) {
      struct zink_resource *flush_res = zink_resource(bs->flush_res);
      mem_signal.memory = zink_bo_get_mem(flush_res->scanout_obj ? flush_res->scanout_obj->bo : flush_res->obj->bo);
      si.pNext = &mem_signal;
   }

   if (VKSCR(EndCommandBuffer)(bs->cmdbuf) != VK_SUCCESS) {
      debug_printf("vkEndCommandBuffer failed\n");
      bs->is_device_lost = true;
      goto end;
   }
   if (VKSCR(EndCommandBuffer)(bs->barrier_cmdbuf) != VK_SUCCESS) {
      debug_printf("vkEndCommandBuffer failed\n");
      bs->is_device_lost = true;
      goto end;
   }

   while (util_dynarray_contains(&bs->persistent_resources, struct zink_resource_object*)) {
      struct zink_resource_object *obj = util_dynarray_pop(&bs->persistent_resources, struct zink_resource_object*);
       VkMappedMemoryRange range = zink_resource_init_mem_range(screen, obj, 0, obj->size);
       VKSCR(FlushMappedMemoryRanges)(screen->dev, 1, &range);
   }

   simple_mtx_lock(&screen->queue_lock);
   if (VKSCR(QueueSubmit)(bs->queue, 1, &si, bs->fence.fence) != VK_SUCCESS) {
      debug_printf("ZINK: vkQueueSubmit() failed\n");
      bs->is_device_lost = true;
   }
   simple_mtx_unlock(&screen->queue_lock);
   bs->submit_count++;
end:
   cnd_broadcast(&bs->usage.flush);

   p_atomic_set(&bs->fence.submitted, true);
   unref_resources(screen, bs);
}


/* TODO: remove for wsi */
static void
copy_scanout(struct zink_batch_state *bs, struct zink_resource *res)
{
   if (!bs->scanout_flush)
      return;
   struct zink_context *ctx = bs->ctx;

   VkImageCopy region = {0};
   struct pipe_box box = {0, 0, 0,
                          u_minify(res->base.b.width0, 0),
                          u_minify(res->base.b.height0, 0), res->base.b.array_size};
   box.depth = util_num_layers(&res->base.b, 0);
   struct pipe_box *src_box = &box;
   unsigned dstz = 0;

   region.srcSubresource.aspectMask = res->aspect;
   region.srcSubresource.mipLevel = 0;
   switch (res->base.b.target) {
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_1D_ARRAY:
      /* these use layer */
      region.srcSubresource.baseArrayLayer = src_box->z;
      region.srcSubresource.layerCount = src_box->depth;
      region.srcOffset.z = 0;
      region.extent.depth = 1;
      break;
   case PIPE_TEXTURE_3D:
      /* this uses depth */
      region.srcSubresource.baseArrayLayer = 0;
      region.srcSubresource.layerCount = 1;
      region.srcOffset.z = src_box->z;
      region.extent.depth = src_box->depth;
      break;
   default:
      /* these must only copy one layer */
      region.srcSubresource.baseArrayLayer = 0;
      region.srcSubresource.layerCount = 1;
      region.srcOffset.z = 0;
      region.extent.depth = 1;
   }

   region.srcOffset.x = src_box->x;
   region.srcOffset.y = src_box->y;

   region.dstSubresource.aspectMask = res->aspect;
   region.dstSubresource.mipLevel = 0;
   switch (res->base.b.target) {
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_1D_ARRAY:
      /* these use layer */
      region.dstSubresource.baseArrayLayer = dstz;
      region.dstSubresource.layerCount = src_box->depth;
      region.dstOffset.z = 0;
      break;
   case PIPE_TEXTURE_3D:
      /* this uses depth */
      region.dstSubresource.baseArrayLayer = 0;
      region.dstSubresource.layerCount = 1;
      region.dstOffset.z = dstz;
      break;
   default:
      /* these must only copy one layer */
      region.dstSubresource.baseArrayLayer = 0;
      region.dstSubresource.layerCount = 1;
      region.dstOffset.z = 0;
   }

   region.dstOffset.x = 0;
   region.dstOffset.y = 0;
   region.extent.width = src_box->width;
   region.extent.height = src_box->height;

   VkImageMemoryBarrier imb1;
   zink_resource_image_barrier_init(&imb1, res, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
   VKCTX(CmdPipelineBarrier)(
      bs->cmdbuf,
      res->obj->access_stage ? res->obj->access_stage : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,
      0, NULL,
      0, NULL,
      1, &imb1
   );

   VkImageSubresourceRange isr = {
      res->aspect,
      0, VK_REMAINING_MIP_LEVELS,
      0, VK_REMAINING_ARRAY_LAYERS
   };
   VkImageMemoryBarrier imb = {
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      NULL,
      0,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      res->scanout_obj_init ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      res->scanout_obj->image,
      isr
   };
   VKCTX(CmdPipelineBarrier)(
      bs->cmdbuf,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,
      0, NULL,
      0, NULL,
      1, &imb
   );

   VKCTX(CmdCopyImage)(bs->cmdbuf, res->obj->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  res->scanout_obj->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  1, &region);
   imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   imb.dstAccessMask = 0;
   imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   imb.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
   VKCTX(CmdPipelineBarrier)(
      bs->cmdbuf,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0,
      0, NULL,
      0, NULL,
      1, &imb
   );
   /* separate flag to avoid annoying validation errors for new scanout objs */
   res->scanout_obj_init = true;
}

void
zink_end_batch(struct zink_context *ctx, struct zink_batch *batch)
{
   if (batch->state->flush_res)
      copy_scanout(batch->state, zink_resource(batch->state->flush_res));
   if (!ctx->queries_disabled)
      zink_suspend_queries(ctx, batch);

   tc_driver_internal_flush_notify(ctx->tc);

   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_batch_state *bs;

   simple_mtx_lock(&ctx->batch_mtx);
   if (ctx->oom_flush || ctx->batch_states_count > 10) {
      assert(!ctx->batch_states_count || ctx->batch_states);
      while (ctx->batch_states) {
         bs = ctx->batch_states;
         struct zink_fence *fence = &bs->fence;
         /* once an incomplete state is reached, no more will be complete */
         if (!zink_check_batch_completion(ctx, fence->batch_id, true))
            break;

         if (bs->fence.submitted && !bs->fence.completed)
            /* this fence is already done, so we need vulkan to release the cmdbuf */
            zink_vkfence_wait(screen, &bs->fence, PIPE_TIMEOUT_INFINITE);
         pop_batch_state(ctx);
         zink_reset_batch_state(ctx, bs);
         util_dynarray_append(&ctx->free_batch_states, struct zink_batch_state *, bs);
      }
      if (ctx->batch_states_count > 50)
         ctx->oom_flush = true;
   }

   bs = batch->state;
   if (ctx->last_fence)
      zink_batch_state(ctx->last_fence)->next = bs;
   else {
      assert(!ctx->batch_states);
      ctx->batch_states = bs;
   }
   ctx->last_fence = &bs->fence;
   ctx->batch_states_count++;
   simple_mtx_unlock(&ctx->batch_mtx);
   batch->work_count = 0;

   if (screen->device_lost)
      return;

   if (screen->threaded) {
      util_queue_add_job(&screen->flush_queue, bs, &bs->flush_completed,
                         submit_queue, post_submit, 0);
   } else {
      submit_queue(bs, NULL, 0);
      post_submit(bs, NULL, 0);
   }
}

void
zink_batch_resource_usage_set(struct zink_batch *batch, struct zink_resource *res, bool write)
{
   zink_resource_usage_set(res, batch->state, write);
   if (write && res->scanout_obj)
      batch->state->scanout_flush = true;
   /* multiple array entries are fine */
   if (!res->obj->coherent && res->obj->persistent_maps)
      util_dynarray_append(&batch->state->persistent_resources, struct zink_resource_object*, res->obj);

   batch->has_work = true;
}

void
zink_batch_reference_resource_rw(struct zink_batch *batch, struct zink_resource *res, bool write)
{
   /* if the resource already has usage of any sort set for this batch, */
   if (!zink_resource_usage_matches(res, batch->state) ||
       /* or if it's bound somewhere */
       !zink_resource_has_binds(res))
      /* then it already has a batch ref and doesn't need one here */
      zink_batch_reference_resource(batch, res);
   zink_batch_resource_usage_set(batch, res, write);
}

bool
batch_ptr_add_usage(struct zink_batch *batch, struct set *s, void *ptr)
{
   bool found = false;
   _mesa_set_search_or_add(s, ptr, &found);
   return !found;
}

ALWAYS_INLINE static void
check_oom_flush(struct zink_context *ctx, const struct zink_batch *batch)
{
   const VkDeviceSize resource_size = batch->state->resource_size;
   if (resource_size >= zink_screen(ctx->base.screen)->clamp_video_mem) {
       ctx->oom_flush = true;
       ctx->oom_stall = true;
    }
}

void
zink_batch_reference_resource(struct zink_batch *batch, struct zink_resource *res)
{
   if (!batch_ptr_add_usage(batch, batch->state->resources, res->obj))
      return;
   pipe_reference(NULL, &res->obj->reference);
   batch->state->resource_size += res->obj->size;
   check_oom_flush(batch->state->ctx, batch);
   batch->has_work = true;
}

void
zink_batch_reference_resource_move(struct zink_batch *batch, struct zink_resource *res)
{
   if (!batch_ptr_add_usage(batch, batch->state->resources, res->obj))
      return;
   batch->state->resource_size += res->obj->size;
   check_oom_flush(batch->state->ctx, batch);
   batch->has_work = true;
}

void
zink_batch_reference_bufferview(struct zink_batch *batch, struct zink_buffer_view *buffer_view)
{
   if (!batch_ptr_add_usage(batch, batch->state->bufferviews, buffer_view))
      return;
   pipe_reference(NULL, &buffer_view->reference);
   batch->has_work = true;
}

void
zink_batch_reference_surface(struct zink_batch *batch, struct zink_surface *surface)
{
   if (!batch_ptr_add_usage(batch, batch->state->surfaces, surface))
      return;
   struct pipe_surface *surf = NULL;
   pipe_surface_reference(&surf, &surface->base);
   batch->has_work = true;
}

void
zink_batch_reference_sampler_view(struct zink_batch *batch,
                                  struct zink_sampler_view *sv)
{
   if (sv->base.target == PIPE_BUFFER)
      zink_batch_reference_bufferview(batch, sv->buffer_view);
   else
      zink_batch_reference_surface(batch, sv->image_view);
}

void
zink_batch_reference_program(struct zink_batch *batch,
                             struct zink_program *pg)
{
   if (zink_batch_usage_matches(pg->batch_uses, batch->state) ||
       !batch_ptr_add_usage(batch, batch->state->programs, pg))
      return;
   pipe_reference(NULL, &pg->reference);
   zink_batch_usage_set(&pg->batch_uses, batch->state);
   batch->has_work = true;
}

void
zink_batch_reference_image_view(struct zink_batch *batch,
                                struct zink_image_view *image_view)
{
   if (image_view->base.resource->target == PIPE_BUFFER)
      zink_batch_reference_bufferview(batch, image_view->buffer_view);
   else
      zink_batch_reference_surface(batch, image_view->surface);
}

bool
zink_screen_usage_check_completion(struct zink_screen *screen, const struct zink_batch_usage *u)
{
   if (!zink_batch_usage_exists(u))
      return true;
   if (zink_batch_usage_is_unflushed(u))
      return false;

   return zink_screen_batch_id_wait(screen, u->usage, 0);
}

bool
zink_batch_usage_check_completion(struct zink_context *ctx, const struct zink_batch_usage *u)
{
   if (!zink_batch_usage_exists(u))
      return true;
   if (zink_batch_usage_is_unflushed(u))
      return false;
   return zink_check_batch_completion(ctx, u->usage, false);
}

void
zink_batch_usage_wait(struct zink_context *ctx, struct zink_batch_usage *u)
{
   if (!zink_batch_usage_exists(u))
      return;
   if (zink_batch_usage_is_unflushed(u)) {
      if (likely(u == &ctx->batch.state->usage))
         ctx->base.flush(&ctx->base, NULL, PIPE_FLUSH_HINT_FINISH);
      else { //multi-context
         mtx_lock(&u->mtx);
         cnd_wait(&u->flush, &u->mtx);
         mtx_unlock(&u->mtx);
      }
   }
   zink_wait_on_batch(ctx, u->usage);
}
