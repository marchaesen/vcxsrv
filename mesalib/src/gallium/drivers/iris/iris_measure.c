/*
 * Copyright © 2019 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file iris_measure.c
 */

#include <stdio.h>
#include "util/debug.h"
#include "util/list.h"
#include "util/crc32.h"
#include "iris_context.h"
#include "iris_defines.h"

void
iris_init_screen_measure(struct iris_screen *screen)
{
   struct intel_measure_device *measure_device = &screen->measure;

   memset(measure_device, 0, sizeof(*measure_device));
   intel_measure_init(measure_device);
   struct intel_measure_config *config = measure_device->config;
   if (config == NULL)
      return;

   /* the final member of intel_measure_ringbuffer is a zero-length array of
    * intel_measure_buffered_result objects.  Allocate additional space for
    * the buffered objects based on the run-time configurable buffer_size
    */
   const size_t rb_bytes = sizeof(struct intel_measure_ringbuffer) +
      config->buffer_size * sizeof(struct intel_measure_buffered_result);
   struct intel_measure_ringbuffer *rb = rzalloc_size(screen, rb_bytes);
   measure_device->ringbuffer = rb;
}

static struct intel_measure_config *
config_from_screen(struct iris_screen *screen)
{
   return screen->measure.config;
}

static struct intel_measure_config *
config_from_context(struct iris_context *ice)
{
   return ((struct iris_screen *) ice->ctx.screen)->measure.config;
}

void
iris_destroy_screen_measure(struct iris_screen *screen)
{
   if (!config_from_screen(screen))
      return;

   struct intel_measure_device *measure_device = &screen->measure;

   if (measure_device->config->file &&
       measure_device->config->file != stderr)
      fclose(screen->measure.config->file);

   ralloc_free(measure_device->ringbuffer);
   measure_device->ringbuffer = NULL;
}


void
iris_init_batch_measure(struct iris_context *ice, struct iris_batch *batch)
{
   const struct intel_measure_config *config = config_from_context(ice);
   struct iris_screen *screen = batch->screen;
   struct iris_bufmgr *bufmgr = screen->bufmgr;

   if (!config)
      return;

   /* the final member of iris_measure_batch is a zero-length array of
    * intel_measure_snapshot objects.  Create additional space for the
    * snapshot objects based on the run-time configurable batch_size
    */
   const size_t batch_bytes = sizeof(struct iris_measure_batch) +
      config->batch_size * sizeof(struct intel_measure_snapshot);
   assert(batch->measure == NULL);
   batch->measure = malloc(batch_bytes);
   memset(batch->measure, 0, batch_bytes);
   struct iris_measure_batch *measure = batch->measure;

   measure->bo = iris_bo_alloc(bufmgr, "measure",
                               config->batch_size * sizeof(uint64_t), 1,
                               IRIS_MEMZONE_OTHER, BO_ALLOC_ZEROED);
   measure->base.timestamps = iris_bo_map(NULL, measure->bo, MAP_READ);
   measure->base.framebuffer =
      (uintptr_t)util_hash_crc32(&ice->state.framebuffer,
                                 sizeof(ice->state.framebuffer));
}

void
iris_destroy_batch_measure(struct iris_measure_batch *batch)
{
   if (!batch)
      return;
   iris_bo_unmap(batch->bo);
   iris_bo_unreference(batch->bo);
   batch->bo = NULL;
   free(batch);
}

static void
measure_start_snapshot(struct iris_context *ice,
                       struct iris_batch *batch,
                       enum intel_measure_snapshot_type type,
                       const char *event_name,
                       uint32_t count)
{
   struct intel_measure_batch *measure_batch = &batch->measure->base;
   const struct intel_measure_config *config = config_from_context(ice);
   const struct iris_screen *screen = (void *) ice->ctx.screen;
   const unsigned screen_frame = screen->measure.frame;

   /* if the command buffer is not associated with a frame, associate it with
    * the most recent acquired frame
    */
   if (measure_batch->frame == 0)
      measure_batch->frame = screen_frame;

   uintptr_t framebuffer = measure_batch->framebuffer;

   if (measure_batch->index == config->batch_size) {
      /* Snapshot buffer is full.  The batch must be flushed before additional
       * snapshots can be taken.
       */
      static bool warned = false;
      if (unlikely(!warned)) {
         fprintf(config->file,
                 "WARNING: batch size exceeds INTEL_MEASURE limit: %d. "
                 "Data has been dropped. "
                 "Increase setting with INTEL_MEASURE=batch_size={count}\n",
                 config->batch_size);
         warned = true;
      }
      return;
   }

   unsigned index = measure_batch->index++;
   assert(index < config->batch_size);
   iris_emit_pipe_control_write(batch, "measurement snapshot",
                                PIPE_CONTROL_WRITE_TIMESTAMP |
                                PIPE_CONTROL_CS_STALL,
                                batch->measure->bo, index * sizeof(uint64_t), 0ull);
   if (event_name == NULL)
      event_name = intel_measure_snapshot_string(type);

   struct intel_measure_snapshot *snapshot = &(measure_batch->snapshots[index]);
   memset(snapshot, 0, sizeof(*snapshot));
   snapshot->type = type;
   snapshot->count = (unsigned) count;
   snapshot->event_count = measure_batch->event_count;
   snapshot->event_name = event_name;
   snapshot->framebuffer = framebuffer;

   if (type == INTEL_SNAPSHOT_COMPUTE) {
      snapshot->cs = (uintptr_t) ice->shaders.prog[MESA_SHADER_COMPUTE];
   } else {
      snapshot->vs  = (uintptr_t) ice->shaders.prog[MESA_SHADER_VERTEX];
      snapshot->tcs = (uintptr_t) ice->shaders.prog[MESA_SHADER_TESS_CTRL];
      snapshot->tes = (uintptr_t) ice->shaders.prog[MESA_SHADER_TESS_EVAL];
      snapshot->gs  = (uintptr_t) ice->shaders.prog[MESA_SHADER_GEOMETRY];
      snapshot->fs  = (uintptr_t) ice->shaders.prog[MESA_SHADER_FRAGMENT];
   }
}

static void
measure_end_snapshot(struct iris_batch *batch,
                     uint32_t event_count)
{
   struct intel_measure_batch *measure_batch = &batch->measure->base;

   unsigned index = measure_batch->index++;
   assert(index % 2 == 1);

   iris_emit_pipe_control_write(batch, "measurement snapshot",
                                PIPE_CONTROL_WRITE_TIMESTAMP |
                                PIPE_CONTROL_CS_STALL,
                                batch->measure->bo,
                                index * sizeof(uint64_t), 0ull);

   struct intel_measure_snapshot *snapshot = &(measure_batch->snapshots[index]);
   memset(snapshot, 0, sizeof(*snapshot));
   snapshot->type = INTEL_SNAPSHOT_END;
   snapshot->event_count = event_count;
}

static bool
state_changed(const struct iris_context *ice,
              const struct iris_batch *batch,
              enum intel_measure_snapshot_type type)
{
   uintptr_t vs=0, tcs=0, tes=0, gs=0, fs=0, cs=0;

   if (type == INTEL_SNAPSHOT_COMPUTE) {
      cs = (uintptr_t) ice->shaders.prog[MESA_SHADER_COMPUTE];
   } else if (type == INTEL_SNAPSHOT_DRAW) {
      vs  = (uintptr_t) ice->shaders.prog[MESA_SHADER_VERTEX];
      tcs = (uintptr_t) ice->shaders.prog[MESA_SHADER_TESS_CTRL];
      tes = (uintptr_t) ice->shaders.prog[MESA_SHADER_TESS_EVAL];
      gs  = (uintptr_t) ice->shaders.prog[MESA_SHADER_GEOMETRY];
      fs  = (uintptr_t) ice->shaders.prog[MESA_SHADER_FRAGMENT];
   }
   /* else blorp, all programs NULL */

   return intel_measure_state_changed(&batch->measure->base,
                                      vs, tcs, tes, gs, fs, cs);
}

static void
iris_measure_renderpass(struct iris_context *ice)
{
   const struct intel_measure_config *config = config_from_context(ice);
   struct intel_measure_batch *batch =
      &ice->batches[IRIS_BATCH_RENDER].measure->base;

   if (!config)
      return;
   uint32_t framebuffer_crc = util_hash_crc32(&ice->state.framebuffer,
                                              sizeof(ice->state.framebuffer));
   if (framebuffer_crc == batch->framebuffer)
      return;
   bool filtering = config->flags & INTEL_MEASURE_RENDERPASS;
   if (filtering && batch->index % 2 == 1) {
      /* snapshot for previous renderpass was not ended */
      measure_end_snapshot(&ice->batches[IRIS_BATCH_RENDER],
                           batch->event_count);
      batch->event_count = 0;
   }

   batch->framebuffer = framebuffer_crc;
}

void
_iris_measure_snapshot(struct iris_context *ice,
                       struct iris_batch *batch,
                       enum intel_measure_snapshot_type type,
                       const struct pipe_draw_info *draw,
                       const struct pipe_draw_indirect_info *indirect,
                       const struct pipe_draw_start_count_bias *sc)
{

   const struct intel_measure_config *config = config_from_context(ice);
   struct intel_measure_batch* measure_batch = &batch->measure->base;

   assert(config);
   if (!config->enabled)
      return;
   if (measure_batch == NULL)
      return;

   assert(type != INTEL_SNAPSHOT_END);
   iris_measure_renderpass(ice);

   if (!state_changed(ice, batch, type)) {
      /* filter out this event */
      return;
   }

   /* increment event count */
   ++measure_batch->event_count;
   if (measure_batch->event_count == 1 ||
       measure_batch->event_count == config->event_interval + 1) {
      /* the first event of an interval */
      if (measure_batch->index % 2) {
         /* end the previous event */
         measure_end_snapshot(batch, measure_batch->event_count - 1);
      }
      measure_batch->event_count = 1;

      const char *event_name = NULL;
      int count = 0;
      if (sc)
         count = sc->count;

      if (draw != NULL) {
         const struct shader_info *fs_info =
            iris_get_shader_info(ice, MESA_SHADER_FRAGMENT);
         if (fs_info && fs_info->name && strncmp(fs_info->name, "st/", 2) == 0) {
            event_name = fs_info->name;
         } else if (indirect) {
            event_name = "DrawIndirect";
            if (indirect->count_from_stream_output) {
               event_name = "DrawTransformFeedback";
            }
         }
         else if (draw->index_size)
            event_name = "DrawElements";
         else
            event_name = "DrawArrays";
         count = count * (draw->instance_count ? draw->instance_count : 1);
      }

      measure_start_snapshot(ice, batch, type, event_name, count);
      return;
   }
}

void
iris_destroy_ctx_measure(struct iris_context *ice)
{
   /* All outstanding snapshots must be collected before the context is
    * destroyed.
    */
   struct iris_screen *screen = (struct iris_screen *) ice->ctx.screen;
   intel_measure_gather(&screen->measure, &screen->devinfo);
}

void
iris_measure_batch_end(struct iris_context *ice, struct iris_batch *batch)
{
   const struct intel_measure_config *config = config_from_context(ice);
   struct iris_screen *screen = (struct iris_screen *) ice->ctx.screen;
   struct iris_measure_batch *iris_measure_batch = batch->measure;
   struct intel_measure_batch *measure_batch = &iris_measure_batch->base;
   struct intel_measure_device *measure_device = &screen->measure;

   if (!config)
      return;
   if (!config->enabled)
      return;

   assert(measure_batch);
   assert(measure_device);

   static unsigned batch_count = 0;
   measure_batch->batch_count = p_atomic_inc_return(&batch_count);

   if (measure_batch->index % 2) {
      /* We hit the end of the batch, but never terminated our section of
       * drawing with the same render target or shaders.  End it now.
       */
      measure_end_snapshot(batch, measure_batch->event_count);
   }

   if (measure_batch->index == 0)
      return;

   /* enqueue snapshot for gathering */
   pthread_mutex_lock(&measure_device->mutex);
   list_addtail(&iris_measure_batch->base.link, &measure_device->queued_snapshots);
   batch->measure = NULL;
   pthread_mutex_unlock(&measure_device->mutex);
   /* init new measure_batch */
   iris_init_batch_measure(ice, batch);

   static int interval = 0;
   if (++interval > 10) {
      intel_measure_gather(measure_device, &screen->devinfo);
      interval = 0;
   }
}

void
iris_measure_frame_end(struct iris_context *ice)
{
   struct iris_screen *screen = (struct iris_screen *) ice->ctx.screen;
   struct intel_measure_device *measure_device = &screen->measure;
   const struct intel_measure_config *config = measure_device->config;

   if (!config)
      return;

   /* increment frame counter */
   intel_measure_frame_transition(p_atomic_inc_return(&measure_device->frame));

   intel_measure_gather(measure_device, &screen->devinfo);
}
