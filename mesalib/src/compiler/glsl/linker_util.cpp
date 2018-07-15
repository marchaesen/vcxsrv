/*
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */
#include "main/mtypes.h"
#include "linker_util.h"
#include "util/set.h"
#include "ir_uniform.h" /* for gl_uniform_storage */

/* Utility methods shared between the GLSL IR and the NIR */

bool
link_util_add_program_resource(struct gl_shader_program *prog,
                               struct set *resource_set,
                               GLenum type, const void *data, uint8_t stages)
{
   assert(data);

   /* If resource already exists, do not add it again. */
   if (_mesa_set_search(resource_set, data))
      return true;

   prog->data->ProgramResourceList =
      reralloc(prog->data,
               prog->data->ProgramResourceList,
               gl_program_resource,
               prog->data->NumProgramResourceList + 1);

   if (!prog->data->ProgramResourceList) {
      linker_error(prog, "Out of memory during linking.\n");
      return false;
   }

   struct gl_program_resource *res =
      &prog->data->ProgramResourceList[prog->data->NumProgramResourceList];

   res->Type = type;
   res->Data = data;
   res->StageReferences = stages;

   prog->data->NumProgramResourceList++;

   _mesa_set_add(resource_set, data);

   return true;
}

/**
 * Search through the list of empty blocks to find one that fits the current
 * uniform.
 */
int
link_util_find_empty_block(struct gl_shader_program *prog,
                           struct gl_uniform_storage *uniform)
{
   const unsigned entries = MAX2(1, uniform->array_elements);

   foreach_list_typed(struct empty_uniform_block, block, link,
                      &prog->EmptyUniformLocations) {
      /* Found a block with enough slots to fit the uniform */
      if (block->slots == entries) {
         unsigned start = block->start;
         exec_node_remove(&block->link);
         ralloc_free(block);

         return start;
      /* Found a block with more slots than needed. It can still be used. */
      } else if (block->slots > entries) {
         unsigned start = block->start;
         block->start += entries;
         block->slots -= entries;

         return start;
      }
   }

   return -1;
}

void
link_util_update_empty_uniform_locations(struct gl_shader_program *prog)
{
   struct empty_uniform_block *current_block = NULL;

   for (unsigned i = 0; i < prog->NumUniformRemapTable; i++) {
      /* We found empty space in UniformRemapTable. */
      if (prog->UniformRemapTable[i] == NULL) {
         /* We've found the beginning of a new continous block of empty slots */
         if (!current_block || current_block->start + current_block->slots != i) {
            current_block = rzalloc(prog, struct empty_uniform_block);
            current_block->start = i;
            exec_list_push_tail(&prog->EmptyUniformLocations,
                                &current_block->link);
         }

         /* The current block continues, so we simply increment its slots */
         current_block->slots++;
      }
   }
}
