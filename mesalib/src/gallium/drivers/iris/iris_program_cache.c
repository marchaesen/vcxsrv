/*
 * Copyright © 2017 Intel Corporation
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
 * @file iris_program_cache.c
 *
 * The in-memory program cache.  This is basically a hash table mapping
 * API-specified shaders and a state key to a compiled variant.  It also
 * takes care of uploading shader assembly into a BO for use on the GPU.
 */

#include <stdio.h>
#include <errno.h>
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/u_atomic.h"
#include "util/u_upload_mgr.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "intel/common/intel_disasm.h"
#include "intel/compiler/brw_compiler.h"
#include "intel/compiler/brw_eu.h"
#include "intel/compiler/brw_nir.h"
#include "iris_context.h"
#include "iris_resource.h"

struct keybox {
   uint16_t size;
   enum iris_program_cache_id cache_id;
   uint8_t data[0];
};

static struct keybox *
make_keybox(void *mem_ctx,
            enum iris_program_cache_id cache_id,
            const void *key,
            uint32_t key_size)
{
   struct keybox *keybox =
      ralloc_size(mem_ctx, sizeof(struct keybox) + key_size);

   keybox->cache_id = cache_id;
   keybox->size = key_size;
   memcpy(keybox->data, key, key_size);

   return keybox;
}

static uint32_t
keybox_hash(const void *void_key)
{
   const struct keybox *key = void_key;
   return _mesa_hash_data(&key->cache_id, key->size + sizeof(key->cache_id));
}

static bool
keybox_equals(const void *void_a, const void *void_b)
{
   const struct keybox *a = void_a, *b = void_b;
   if (a->size != b->size)
      return false;

   return memcmp(a->data, b->data, a->size) == 0;
}

struct iris_compiled_shader *
iris_find_cached_shader(struct iris_context *ice,
                        enum iris_program_cache_id cache_id,
                        uint32_t key_size,
                        const void *key)
{
   struct keybox *keybox = make_keybox(NULL, cache_id, key, key_size);
   struct hash_entry *entry =
      _mesa_hash_table_search(ice->shaders.cache, keybox);

   ralloc_free(keybox);

   return entry ? entry->data : NULL;
}

void
iris_delete_shader_variant(struct iris_compiled_shader *shader)
{
   pipe_resource_reference(&shader->assembly.res, NULL);
   ralloc_free(shader);
}

struct iris_compiled_shader *
iris_upload_shader(struct iris_screen *screen,
                   struct iris_uncompiled_shader *ish,
                   struct hash_table *driver_shaders,
                   struct u_upload_mgr *uploader,
                   enum iris_program_cache_id cache_id,
                   uint32_t key_size,
                   const void *key,
                   const void *assembly,
                   struct brw_stage_prog_data *prog_data,
                   uint32_t *streamout,
                   enum brw_param_builtin *system_values,
                   unsigned num_system_values,
                   unsigned kernel_input_size,
                   unsigned num_cbufs,
                   const struct iris_binding_table *bt)
{
   const struct gen_device_info *devinfo = &screen->devinfo;

   void *mem_ctx = ish ? NULL : (void *) driver_shaders;
   struct iris_compiled_shader *shader =
      rzalloc_size(mem_ctx, sizeof(struct iris_compiled_shader) +
                   screen->vtbl.derived_program_state_size(cache_id));

   pipe_reference_init(&shader->ref, 1);

   shader->assembly.res = NULL;
   u_upload_alloc(uploader, 0, prog_data->program_size, 64,
                  &shader->assembly.offset, &shader->assembly.res,
                  &shader->map);
   memcpy(shader->map, assembly, prog_data->program_size);

   struct iris_resource *res = (void *) shader->assembly.res;
   uint64_t shader_data_addr = res->bo->gtt_offset +
                               shader->assembly.offset +
                               prog_data->const_data_offset;

   struct brw_shader_reloc_value reloc_values[] = {
      {
         .id = IRIS_SHADER_RELOC_CONST_DATA_ADDR_LOW,
         .value = shader_data_addr,
      },
      {
         .id = IRIS_SHADER_RELOC_CONST_DATA_ADDR_HIGH,
         .value = shader_data_addr >> 32,
      },
   };
   brw_write_shader_relocs(&screen->devinfo, shader->map, prog_data,
                           reloc_values, ARRAY_SIZE(reloc_values));

   shader->prog_data = prog_data;
   shader->streamout = streamout;
   shader->system_values = system_values;
   shader->num_system_values = num_system_values;
   shader->kernel_input_size = kernel_input_size;
   shader->num_cbufs = num_cbufs;
   shader->bt = *bt;

   ralloc_steal(shader, shader->prog_data);
   ralloc_steal(shader->prog_data, (void *)prog_data->relocs);
   ralloc_steal(shader->prog_data, prog_data->param);
   ralloc_steal(shader->prog_data, prog_data->pull_param);
   ralloc_steal(shader, shader->streamout);
   ralloc_steal(shader, shader->system_values);

   /* Store the 3DSTATE shader packets and other derived state. */
   screen->vtbl.store_derived_program_state(devinfo, cache_id, shader);

   if (ish) {
      assert(key_size <= sizeof(union iris_any_prog_key));
      memcpy(&shader->key, key, key_size);

      simple_mtx_lock(&ish->lock);

      /* While unlikely, it's possible that another thread concurrently
       * compiled the same variant.  Make sure no one beat us to it; if
       * they did, return the existing one and discard our new one.
       */
      list_for_each_entry(struct iris_compiled_shader, existing,
                          &ish->variants, link) {
         if (memcmp(&existing->key, key, key_size) == 0) {
            iris_delete_shader_variant(shader);
            simple_mtx_unlock(&ish->lock);
            return existing;
         }
      }

      /* Append our new variant to the shader's variant list. */
      list_addtail(&shader->link, &ish->variants);

      simple_mtx_unlock(&ish->lock);
   } else {
      struct keybox *keybox = make_keybox(shader, cache_id, key, key_size);
      _mesa_hash_table_insert(driver_shaders, keybox, shader);
   }

   return shader;
}

bool
iris_blorp_lookup_shader(struct blorp_batch *blorp_batch,
                         const void *key, uint32_t key_size,
                         uint32_t *kernel_out, void *prog_data_out)
{
   struct blorp_context *blorp = blorp_batch->blorp;
   struct iris_context *ice = blorp->driver_ctx;
   struct iris_batch *batch = blorp_batch->driver_batch;
   struct iris_compiled_shader *shader =
      iris_find_cached_shader(ice, IRIS_CACHE_BLORP, key_size, key);

   if (!shader)
      return false;

   struct iris_bo *bo = iris_resource_bo(shader->assembly.res);
   *kernel_out =
      iris_bo_offset_from_base_address(bo) + shader->assembly.offset;
   *((void **) prog_data_out) = shader->prog_data;

   iris_use_pinned_bo(batch, bo, false, IRIS_DOMAIN_NONE);

   return true;
}

bool
iris_blorp_upload_shader(struct blorp_batch *blorp_batch, uint32_t stage,
                         const void *key, uint32_t key_size,
                         const void *kernel, UNUSED uint32_t kernel_size,
                         const struct brw_stage_prog_data *prog_data_templ,
                         UNUSED uint32_t prog_data_size,
                         uint32_t *kernel_out, void *prog_data_out)
{
   struct blorp_context *blorp = blorp_batch->blorp;
   struct iris_context *ice = blorp->driver_ctx;
   struct iris_batch *batch = blorp_batch->driver_batch;
   struct iris_screen *screen = batch->screen;

   void *prog_data = ralloc_size(NULL, prog_data_size);
   memcpy(prog_data, prog_data_templ, prog_data_size);

   struct iris_binding_table bt;
   memset(&bt, 0, sizeof(bt));

   struct iris_compiled_shader *shader =
      iris_upload_shader(screen, NULL, ice->shaders.cache,
                         ice->shaders.uploader_driver,
                         IRIS_CACHE_BLORP, key_size, key, kernel,
                         prog_data, NULL, NULL, 0, 0, 0, &bt);

   struct iris_bo *bo = iris_resource_bo(shader->assembly.res);
   *kernel_out =
      iris_bo_offset_from_base_address(bo) + shader->assembly.offset;
   *((void **) prog_data_out) = shader->prog_data;

   iris_use_pinned_bo(batch, bo, false, IRIS_DOMAIN_NONE);

   return true;
}

void
iris_init_program_cache(struct iris_context *ice)
{
   ice->shaders.cache =
      _mesa_hash_table_create(ice, keybox_hash, keybox_equals);

   ice->shaders.uploader_driver =
      u_upload_create(&ice->ctx, 16384, PIPE_BIND_CUSTOM, PIPE_USAGE_IMMUTABLE,
                      IRIS_RESOURCE_FLAG_SHADER_MEMZONE);
   ice->shaders.uploader_unsync =
      u_upload_create(&ice->ctx, 16384, PIPE_BIND_CUSTOM, PIPE_USAGE_IMMUTABLE,
                      IRIS_RESOURCE_FLAG_SHADER_MEMZONE);
}

void
iris_destroy_program_cache(struct iris_context *ice)
{
   for (int i = 0; i < MESA_SHADER_STAGES; i++) {
      iris_shader_variant_reference(&ice->shaders.prog[i], NULL);
   }
   iris_shader_variant_reference(&ice->shaders.last_vue_shader, NULL);

   hash_table_foreach(ice->shaders.cache, entry) {
      struct iris_compiled_shader *shader = entry->data;
      iris_delete_shader_variant(shader);
   }

   u_upload_destroy(ice->shaders.uploader_driver);
   u_upload_destroy(ice->shaders.uploader_unsync);

   ralloc_free(ice->shaders.cache);
}
