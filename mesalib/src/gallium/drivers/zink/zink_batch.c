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

   if (vkResetCommandPool(screen->dev, bs->cmdpool, 0) != VK_SUCCESS)
      debug_printf("vkResetCommandPool failed\n");

   zink_fence_clear_resources(screen, &bs->fence);

   set_foreach_remove(bs->active_queries, entry) {
      struct zink_query *query = (void*)entry->key;
      zink_prune_query(screen, query);
   }

   set_foreach_remove(bs->surfaces, entry) {
      struct zink_surface *surf = (struct zink_surface *)entry->key;
      zink_batch_usage_unset(&surf->batch_uses, bs->fence.batch_id);
      zink_surface_reference(screen, &surf, NULL);
   }
   set_foreach_remove(bs->bufferviews, entry) {
      struct zink_buffer_view *buffer_view = (struct zink_buffer_view *)entry->key;
      zink_batch_usage_unset(&buffer_view->batch_uses, bs->fence.batch_id);
      zink_buffer_view_reference(screen, &buffer_view, NULL);
   }

   util_dynarray_foreach(&bs->zombie_samplers, VkSampler, samp) {
      vkDestroySampler(screen->dev, *samp, NULL);
   }
   util_dynarray_clear(&bs->zombie_samplers);
   util_dynarray_clear(&bs->persistent_resources);

   set_foreach(bs->desc_sets, entry) {
      struct zink_descriptor_set *zds = (void*)entry->key;
      zink_batch_usage_unset(&zds->batch_uses, bs->fence.batch_id);
      /* reset descriptor pools when no bs is using this program to avoid
       * having some inactive program hogging a billion descriptors
       */
      pipe_reference(&zds->reference, NULL);
      zink_descriptor_set_recycle(zds);
      _mesa_set_remove(bs->desc_sets, entry);
   }

   set_foreach_remove(bs->programs, entry) {
      struct zink_program *pg = (struct zink_program*)entry->key;
      if (pg->is_compute) {
         struct zink_compute_program *comp = (struct zink_compute_program*)pg;
         bool in_use = comp == ctx->curr_compute;
         if (zink_compute_program_reference(screen, &comp, NULL) && in_use)
            ctx->curr_compute = NULL;
      } else {
         struct zink_gfx_program *prog = (struct zink_gfx_program*)pg;
         bool in_use = prog == ctx->curr_program;
         if (zink_gfx_program_reference(screen, &prog, NULL) && in_use)
            ctx->curr_program = NULL;
      }
   }

   set_foreach(bs->fbs, entry) {
      struct zink_framebuffer *fb = (void*)entry->key;
      zink_framebuffer_reference(screen, &fb, NULL);
      _mesa_set_remove(bs->fbs, entry);
   }

   bs->flush_res = NULL;

   bs->descs_used = 0;
   ctx->resource_size -= bs->resource_size;
   bs->resource_size = 0;

   /* only reset submitted here so that tc fence desync can pick up the 'completed' flag
    * before the state is reused
    */
   bs->fence.submitted = false;
   zink_screen_update_last_finished(screen, bs->fence.batch_id);
   bs->fence.batch_id = 0;
}

void
zink_clear_batch_state(struct zink_context *ctx, struct zink_batch_state *bs)
{
   bs->fence.completed = true;
   zink_reset_batch_state(ctx, bs);
}

void
zink_batch_reset_all(struct zink_context *ctx)
{
   simple_mtx_lock(&ctx->batch_mtx);
   hash_table_foreach(&ctx->batch_states, entry) {
      struct zink_batch_state *bs = entry->data;
      bs->fence.completed = true;
      zink_reset_batch_state(ctx, bs);
      _mesa_hash_table_remove(&ctx->batch_states, entry);
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

   if (bs->fence.fence)
      vkDestroyFence(screen->dev, bs->fence.fence, NULL);

   if (bs->cmdbuf)
      vkFreeCommandBuffers(screen->dev, bs->cmdpool, 1, &bs->cmdbuf);
   if (bs->cmdpool)
      vkDestroyCommandPool(screen->dev, bs->cmdpool, NULL);

   _mesa_set_destroy(bs->fbs, NULL);
   util_dynarray_fini(&bs->zombie_samplers);
   _mesa_set_destroy(bs->surfaces, NULL);
   _mesa_set_destroy(bs->bufferviews, NULL);
   _mesa_set_destroy(bs->programs, NULL);
   _mesa_set_destroy(bs->desc_sets, NULL);
   _mesa_set_destroy(bs->active_queries, NULL);
   simple_mtx_destroy(&bs->fence.resource_mtx);
   ralloc_free(bs);
}

static struct zink_batch_state *
create_batch_state(struct zink_context *ctx)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_batch_state *bs = rzalloc(NULL, struct zink_batch_state);
   bs->have_timelines = ctx->have_timelines;
   VkCommandPoolCreateInfo cpci = {};
   cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   cpci.queueFamilyIndex = screen->gfx_queue;
   cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
   if (vkCreateCommandPool(screen->dev, &cpci, NULL, &bs->cmdpool) != VK_SUCCESS)
      goto fail;

   VkCommandBufferAllocateInfo cbai = {};
   cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   cbai.commandPool = bs->cmdpool;
   cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
   cbai.commandBufferCount = 1;

   if (vkAllocateCommandBuffers(screen->dev, &cbai, &bs->cmdbuf) != VK_SUCCESS)
      goto fail;

#define SET_CREATE_OR_FAIL(ptr) \
   ptr = _mesa_pointer_set_create(bs); \
   if (!ptr) \
      goto fail

   bs->ctx = ctx;
   pipe_reference_init(&bs->reference, 1);

   SET_CREATE_OR_FAIL(bs->fbs);
   SET_CREATE_OR_FAIL(bs->fence.resources);
   SET_CREATE_OR_FAIL(bs->surfaces);
   SET_CREATE_OR_FAIL(bs->bufferviews);
   SET_CREATE_OR_FAIL(bs->programs);
   SET_CREATE_OR_FAIL(bs->desc_sets);
   SET_CREATE_OR_FAIL(bs->active_queries);
   util_dynarray_init(&bs->zombie_samplers, NULL);
   util_dynarray_init(&bs->persistent_resources, NULL);

   VkFenceCreateInfo fci = {};
   fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

   if (vkCreateFence(screen->dev, &fci, NULL, &bs->fence.fence) != VK_SUCCESS)
      goto fail;

   simple_mtx_init(&bs->fence.resource_mtx, mtx_plain);
   util_queue_fence_init(&bs->flush_completed);

   return bs;
fail:
   zink_batch_state_destroy(screen, bs);
   return NULL;
}

static inline bool
find_unused_state(struct hash_entry *entry)
{
   struct zink_fence *fence = entry->data;
   /* we can't reset these from fence_finish because threads */
   bool completed = p_atomic_read(&fence->completed);
   bool submitted = p_atomic_read(&fence->submitted);
   return submitted && completed;
}

static struct zink_batch_state *
get_batch_state(struct zink_context *ctx, struct zink_batch *batch)
{
   struct zink_batch_state *bs = NULL;

   simple_mtx_lock(&ctx->batch_mtx);
   if (util_dynarray_num_elements(&ctx->free_batch_states, struct zink_batch_state*))
      bs = util_dynarray_pop(&ctx->free_batch_states, struct zink_batch_state*);
   if (!bs) {
      hash_table_foreach(&ctx->batch_states, he) {
         struct zink_fence *fence = he->data;
         if (zink_screen_check_last_finished(zink_screen(ctx->base.screen), fence->batch_id) || find_unused_state(he)) {
            bs = he->data;
            _mesa_hash_table_remove(&ctx->batch_states, he);
            break;
         }
      }
   }
   simple_mtx_unlock(&ctx->batch_mtx);
   if (bs) {
      if (bs->fence.submitted && !bs->fence.completed)
         /* this fence is already done, so we need vulkan to release the cmdbuf */
         zink_vkfence_wait(zink_screen(ctx->base.screen), &bs->fence, PIPE_TIMEOUT_INFINITE);
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
   struct zink_screen *screen = zink_screen(ctx->base.screen);

   if (ctx->have_timelines && screen->last_finished > ctx->curr_batch && ctx->curr_batch == 1) {
      if (!zink_screen_init_semaphore(screen)) {
         debug_printf("timeline init failed, things are about to go dramatically wrong.");
         ctx->have_timelines = false;
      }
   }

   batch->state = get_batch_state(ctx, batch);
   assert(batch->state);

   batch->has_work = false;
}

void
zink_start_batch(struct zink_context *ctx, struct zink_batch *batch)
{
   zink_reset_batch(ctx, batch);

   VkCommandBufferBeginInfo cbbi = {};
   cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   if (vkBeginCommandBuffer(batch->state->cmdbuf, &cbbi) != VK_SUCCESS)
      debug_printf("vkBeginCommandBuffer failed\n");

   batch->state->fence.batch_id = ctx->curr_batch;
   batch->state->fence.completed = false;
   if (ctx->last_fence) {
      struct zink_batch_state *last_state = zink_batch_state(ctx->last_fence);
      batch->last_batch_id = last_state->fence.batch_id;
   } else {
      if (zink_screen(ctx->base.screen)->threaded)
         util_queue_init(&batch->flush_queue, "zfq", 8, 1, UTIL_QUEUE_INIT_RESIZE_IF_FULL);
   }
   if (!ctx->queries_disabled)
      zink_resume_queries(ctx, batch);
}

static void
post_submit(void *data, int thread_index)
{
   struct zink_batch_state *bs = data;

   if (bs->is_device_lost) {
      if (bs->ctx->reset.reset)
         bs->ctx->reset.reset(bs->ctx->reset.data, PIPE_GUILTY_CONTEXT_RESET);
      zink_screen(bs->ctx->base.screen)->device_lost = true;
   }
}

static void
submit_queue(void *data, int thread_index)
{
   struct zink_batch_state *bs = data;
   VkSubmitInfo si = {};
   uint64_t batch_id = bs->fence.batch_id;
   si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   si.waitSemaphoreCount = 0;
   si.pWaitSemaphores = NULL;
   si.signalSemaphoreCount = 0;
   si.pSignalSemaphores = NULL;
   si.pWaitDstStageMask = NULL;
   si.commandBufferCount = 1;
   si.pCommandBuffers = &bs->cmdbuf;

   VkTimelineSemaphoreSubmitInfo tsi = {};
   if (bs->have_timelines) {
      tsi.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
      si.pNext = &tsi;
      tsi.signalSemaphoreValueCount = 1;
      tsi.pSignalSemaphoreValues = &batch_id;
      si.signalSemaphoreCount = 1;
      si.pSignalSemaphores = &zink_screen(bs->ctx->base.screen)->sem;
   }

   struct wsi_memory_signal_submit_info mem_signal = {
      .sType = VK_STRUCTURE_TYPE_WSI_MEMORY_SIGNAL_SUBMIT_INFO_MESA,
      .pNext = si.pNext,
   };

   if (bs->flush_res && zink_screen(bs->ctx->base.screen)->needs_mesa_flush_wsi) {
      mem_signal.memory = bs->flush_res->scanout_obj ? bs->flush_res->scanout_obj->mem : bs->flush_res->obj->mem;
      si.pNext = &mem_signal;
   }

   if (vkQueueSubmit(bs->queue, 1, &si, bs->fence.fence) != VK_SUCCESS) {
      debug_printf("ZINK: vkQueueSubmit() failed\n");
      bs->is_device_lost = true;
   }
   p_atomic_set(&bs->fence.submitted, true);
}


/* TODO: remove for wsi */
static void
copy_scanout(struct zink_context *ctx, struct zink_resource *res)
{
   VkImageCopy region = {};
   struct pipe_box box = {0, 0, 0,
                          u_minify(res->base.b.width0, 0),
                          u_minify(res->base.b.height0, 0), res->base.b.array_size};
   box.depth = util_num_layers(&res->base.b, 0);
   struct pipe_box *src_box = &box;
   unsigned dstz = 0;

   if (!res->scanout_dirty)
      return;
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
   zink_resource_image_barrier(ctx, NULL, res, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

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
   vkCmdPipelineBarrier(
      ctx->batch.state->cmdbuf,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,
      0, NULL,
      0, NULL,
      1, &imb
   );

   vkCmdCopyImage(ctx->batch.state->cmdbuf, res->obj->image, res->layout,
                  res->scanout_obj->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  1, &region);
   imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   imb.dstAccessMask = 0;
   imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   imb.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
   vkCmdPipelineBarrier(
      ctx->batch.state->cmdbuf,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0,
      0, NULL,
      0, NULL,
      1, &imb
   );
   /* separate flag to avoid annoying validation errors for new scanout objs */
   res->scanout_obj_init = true;
   res->scanout_dirty = false;
}

void
zink_end_batch(struct zink_context *ctx, struct zink_batch *batch)
{
   if (batch->state->flush_res)
      copy_scanout(ctx, batch->state->flush_res);

   if (!ctx->queries_disabled)
      zink_suspend_queries(ctx, batch);

   if (vkEndCommandBuffer(batch->state->cmdbuf) != VK_SUCCESS) {
      debug_printf("vkEndCommandBuffer failed\n");
      return;
   }
   vkResetFences(zink_screen(ctx->base.screen)->dev, 1, &batch->state->fence.fence);

   struct zink_screen *screen = zink_screen(ctx->base.screen);
   while (util_dynarray_contains(&batch->state->persistent_resources, struct zink_resource_object*)) {
      struct zink_resource_object *obj = util_dynarray_pop(&batch->state->persistent_resources, struct zink_resource_object*);
       assert(!obj->offset);
       VkMappedMemoryRange range = {
          VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
          NULL,
          obj->mem,
          obj->offset,
          VK_WHOLE_SIZE,
       };
       vkFlushMappedMemoryRanges(screen->dev, 1, &range);
   }

   simple_mtx_lock(&ctx->batch_mtx);
   ctx->last_fence = &batch->state->fence;
   _mesa_hash_table_insert_pre_hashed(&ctx->batch_states, batch->state->fence.batch_id, (void*)(uintptr_t)batch->state->fence.batch_id, batch->state);
   simple_mtx_unlock(&ctx->batch_mtx);
   ctx->resource_size += batch->state->resource_size;

   if (screen->device_lost)
      return;

   if (util_queue_is_initialized(&batch->flush_queue)) {
      batch->state->queue = batch->thread_queue;
      util_queue_add_job(&batch->flush_queue, batch->state, &batch->state->flush_completed,
                         submit_queue, post_submit, 0);
   } else {
      batch->state->queue = batch->queue;
      submit_queue(batch->state, 0);
      post_submit(batch->state, 0);
   }
}

void
zink_batch_reference_resource_rw(struct zink_batch *batch, struct zink_resource *res, bool write)
{
   /* u_transfer_helper unrefs the stencil buffer when the depth buffer is unrefed,
    * so we add an extra ref here to the stencil buffer to compensate
    */
   struct zink_resource *stencil = NULL;

   if (!res->obj->is_buffer && res->aspect == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
      zink_get_depth_stencil_resources((struct pipe_resource*)res, NULL, &stencil);

   /* if the resource already has usage of any sort set for this batch, we can skip hashing */
   if (res->obj->reads.usage != batch->state->fence.batch_id &&
       res->obj->writes.usage != batch->state->fence.batch_id) {
      bool found = false;
      _mesa_set_search_and_add(batch->state->fence.resources, res->obj, &found);
      if (!found) {
         pipe_reference(NULL, &res->obj->reference);
         if (!batch->last_batch_id || !zink_batch_usage_matches(&res->obj->reads, batch->last_batch_id))
            /* only add resource usage if it's "new" usage, though this only checks the most recent usage
             * and not all pending usages
             */
            batch->state->resource_size += res->obj->size;
         if (stencil) {
            pipe_reference(NULL, &stencil->obj->reference);
            if (!batch->last_batch_id || !zink_batch_usage_matches(&stencil->obj->reads, batch->last_batch_id))
               batch->state->resource_size += stencil->obj->size;
         }
      }
       }
   if (write) {
      if (res->obj->writes.usage != batch->state->fence.batch_id) {
         if (stencil)
            zink_batch_usage_set(&stencil->obj->writes, batch->state->fence.batch_id);
         zink_batch_usage_set(&res->obj->writes, batch->state->fence.batch_id);
      }
      res->scanout_dirty = !!res->scanout_obj;
   } else {
      if (res->obj->reads.usage != batch->state->fence.batch_id) {
         if (stencil)
            zink_batch_usage_set(&stencil->obj->reads, batch->state->fence.batch_id);
         zink_batch_usage_set(&res->obj->reads, batch->state->fence.batch_id);
      }
   }
   /* multiple array entries are fine */
   if (res->obj->persistent_maps)
      util_dynarray_append(&batch->state->persistent_resources, struct zink_resource_object*, res->obj);

   batch->has_work = true;
}

static bool
ptr_add_usage(struct zink_batch *batch, struct set *s, void *ptr, struct zink_batch_usage *u)
{
   bool found = false;
   if (u->usage == batch->state->fence.batch_id)
      return false;
   _mesa_set_search_and_add(s, ptr, &found);
   assert(!found);
   zink_batch_usage_set(u, batch->state->fence.batch_id);
   return true;
}

void
zink_batch_reference_bufferview(struct zink_batch *batch, struct zink_buffer_view *buffer_view)
{
   if (!ptr_add_usage(batch, batch->state->bufferviews, buffer_view, &buffer_view->batch_uses))
      return;
   pipe_reference(NULL, &buffer_view->reference);
   batch->has_work = true;
}

void
zink_batch_reference_surface(struct zink_batch *batch, struct zink_surface *surface)
{
   if (!ptr_add_usage(batch, batch->state->surfaces, surface, &surface->batch_uses))
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
zink_batch_reference_framebuffer(struct zink_batch *batch,
                                 struct zink_framebuffer *fb)
{
   bool found;
   _mesa_set_search_or_add(batch->state->fbs, fb, &found);
   if (!found)
      pipe_reference(NULL, &fb->reference);
}

void
zink_batch_reference_program(struct zink_batch *batch,
                             struct zink_program *pg)
{
   if (!ptr_add_usage(batch, batch->state->programs, pg, &pg->batch_uses))
      return;
   pipe_reference(NULL, &pg->reference);
   batch->has_work = true;
}

bool
zink_batch_add_desc_set(struct zink_batch *batch, struct zink_descriptor_set *zds)
{
   if (!ptr_add_usage(batch, batch->state->desc_sets, zds, &zds->batch_uses))
      return false;
   pipe_reference(NULL, &zds->reference);
   return true;
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

void
zink_batch_usage_set(struct zink_batch_usage *u, uint32_t batch_id)
{
   p_atomic_set(&u->usage, batch_id);
}

bool
zink_batch_usage_matches(struct zink_batch_usage *u, uint32_t batch_id)
{
   uint32_t usage = p_atomic_read(&u->usage);
   return usage == batch_id;
}

bool
zink_batch_usage_exists(struct zink_batch_usage *u)
{
   uint32_t usage = p_atomic_read(&u->usage);
   return !!usage;
}
