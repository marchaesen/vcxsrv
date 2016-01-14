/*
 * Copyright © 2013 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "glsl_parser_extras.h"
#include "ir.h"
#include "ir_uniform.h"
#include "linker.h"
#include "program/hash_table.h"
#include "main/macros.h"

namespace {
   /*
    * Atomic counter as seen by the program.
    */
   struct active_atomic_counter {
      unsigned uniform_loc;
      ir_variable *var;
   };

   /*
    * Atomic counter buffer referenced by the program.  There is a one
    * to one correspondence between these and the objects that can be
    * queried using glGetActiveAtomicCounterBufferiv().
    */
   struct active_atomic_buffer {
      active_atomic_buffer()
         : counters(0), num_counters(0), stage_references(), size(0)
      {}

      ~active_atomic_buffer()
      {
         free(counters);
      }

      void push_back(unsigned uniform_loc, ir_variable *var)
      {
         active_atomic_counter *new_counters;

         new_counters = (active_atomic_counter *)
            realloc(counters, sizeof(active_atomic_counter) *
                    (num_counters + 1));

         if (new_counters == NULL) {
            _mesa_error_no_memory(__func__);
            return;
         }

         counters = new_counters;
         counters[num_counters].uniform_loc = uniform_loc;
         counters[num_counters].var = var;
         num_counters++;
      }

      active_atomic_counter *counters;
      unsigned num_counters;
      unsigned stage_references[MESA_SHADER_STAGES];
      unsigned size;
   };

   int
   cmp_actives(const void *a, const void *b)
   {
      const active_atomic_counter *const first = (active_atomic_counter *) a;
      const active_atomic_counter *const second = (active_atomic_counter *) b;

      return int(first->var->data.offset) - int(second->var->data.offset);
   }

   bool
   check_atomic_counters_overlap(const ir_variable *x, const ir_variable *y)
   {
      return ((x->data.offset >= y->data.offset &&
               x->data.offset < y->data.offset + y->type->atomic_size()) ||
              (y->data.offset >= x->data.offset &&
               y->data.offset < x->data.offset + x->type->atomic_size()));
   }

   void
   process_atomic_variable(const glsl_type *t, struct gl_shader_program *prog,
                           unsigned *uniform_loc, ir_variable *var,
                           active_atomic_buffer *const buffers,
                           unsigned *num_buffers, int *offset,
                           const unsigned shader_stage)
   {
      /* FIXME: Arrays of arrays get counted separately. For example:
       * x1[3][3][2] = 9 counters
       * x2[3][2]    = 3 counters
       * x3[2]       = 1 counter
       *
       * However this code marks all the counters as active even when they
       * might not be used.
       */
      if (t->is_array() && t->fields.array->is_array()) {
         for (unsigned i = 0; i < t->length; i++) {
            process_atomic_variable(t->fields.array, prog, uniform_loc,
                                    var, buffers, num_buffers, offset,
                                    shader_stage);
         }
      } else {
         active_atomic_buffer *buf = &buffers[var->data.binding];
         gl_uniform_storage *const storage =
            &prog->UniformStorage[*uniform_loc];

         /* If this is the first time the buffer is used, increment
          * the counter of buffers used.
          */
         if (buf->size == 0)
            (*num_buffers)++;

         buf->push_back(*uniform_loc, var);

         buf->stage_references[shader_stage]++;
         buf->size = MAX2(buf->size, *offset + t->atomic_size());

         storage->offset = *offset;
         *offset += t->atomic_size();

         (*uniform_loc)++;
      }
   }

   active_atomic_buffer *
   find_active_atomic_counters(struct gl_context *ctx,
                               struct gl_shader_program *prog,
                               unsigned *num_buffers)
   {
      active_atomic_buffer *const buffers =
         new active_atomic_buffer[ctx->Const.MaxAtomicBufferBindings];

      *num_buffers = 0;

      for (unsigned i = 0; i < MESA_SHADER_STAGES; ++i) {
         struct gl_shader *sh = prog->_LinkedShaders[i];
         if (sh == NULL)
            continue;

         foreach_in_list(ir_instruction, node, sh->ir) {
            ir_variable *var = node->as_variable();

            if (var && var->type->contains_atomic()) {
               int offset = var->data.offset;
               unsigned uniform_loc = var->data.location;
               process_atomic_variable(var->type, prog, &uniform_loc,
                                       var, buffers, num_buffers, &offset, i);
            }
         }
      }

      for (unsigned i = 0; i < ctx->Const.MaxAtomicBufferBindings; i++) {
         if (buffers[i].size == 0)
            continue;

         qsort(buffers[i].counters, buffers[i].num_counters,
               sizeof(active_atomic_counter),
               cmp_actives);

         for (unsigned j = 1; j < buffers[i].num_counters; j++) {
            /* If an overlapping counter found, it must be a reference to the
             * same counter from a different shader stage.
             */
            if (check_atomic_counters_overlap(buffers[i].counters[j-1].var,
                                              buffers[i].counters[j].var)
                && strcmp(buffers[i].counters[j-1].var->name,
                          buffers[i].counters[j].var->name) != 0) {
               linker_error(prog, "Atomic counter %s declared at offset %d "
                            "which is already in use.",
                            buffers[i].counters[j].var->name,
                            buffers[i].counters[j].var->data.offset);
            }
         }
      }
      return buffers;
   }
}

void
link_assign_atomic_counter_resources(struct gl_context *ctx,
                                     struct gl_shader_program *prog)
{
   unsigned num_buffers;
   unsigned num_atomic_buffers[MESA_SHADER_STAGES] = {};
   active_atomic_buffer *abs =
      find_active_atomic_counters(ctx, prog, &num_buffers);

   prog->AtomicBuffers = rzalloc_array(prog, gl_active_atomic_buffer,
                                       num_buffers);
   prog->NumAtomicBuffers = num_buffers;

   unsigned i = 0;
   for (unsigned binding = 0;
        binding < ctx->Const.MaxAtomicBufferBindings;
        binding++) {

      /* If the binding was not used, skip.
       */
      if (abs[binding].size == 0)
         continue;

      active_atomic_buffer &ab = abs[binding];
      gl_active_atomic_buffer &mab = prog->AtomicBuffers[i];

      /* Assign buffer-specific fields. */
      mab.Binding = binding;
      mab.MinimumSize = ab.size;
      mab.Uniforms = rzalloc_array(prog->AtomicBuffers, GLuint,
                                   ab.num_counters);
      mab.NumUniforms = ab.num_counters;

      /* Assign counter-specific fields. */
      for (unsigned j = 0; j < ab.num_counters; j++) {
         ir_variable *const var = ab.counters[j].var;
         gl_uniform_storage *const storage =
            &prog->UniformStorage[ab.counters[j].uniform_loc];

         mab.Uniforms[j] = ab.counters[j].uniform_loc;
         if (!var->data.explicit_binding)
            var->data.binding = i;

         storage->atomic_buffer_index = i;
         storage->offset = var->data.offset;
         storage->array_stride = (var->type->is_array() ?
                                  var->type->without_array()->atomic_size() : 0);
         if (!var->type->is_matrix())
            storage->matrix_stride = 0;
      }

      /* Assign stage-specific fields. */
      for (unsigned j = 0; j < MESA_SHADER_STAGES; ++j) {
         if (ab.stage_references[j]) {
            mab.StageReferences[j] = GL_TRUE;
            num_atomic_buffers[j]++;
         } else {
            mab.StageReferences[j] = GL_FALSE;
         }
      }

      i++;
   }

   /* Store a list pointers to atomic buffers per stage and store the index
    * to the intra-stage buffer list in uniform storage.
    */
   for (unsigned j = 0; j < MESA_SHADER_STAGES; ++j) {
      if (prog->_LinkedShaders[j] && num_atomic_buffers[j] > 0) {
         prog->_LinkedShaders[j]->NumAtomicBuffers = num_atomic_buffers[j];
         prog->_LinkedShaders[j]->AtomicBuffers =
            rzalloc_array(prog, gl_active_atomic_buffer *,
                          num_atomic_buffers[j]);

         unsigned intra_stage_idx = 0;
         for (unsigned i = 0; i < num_buffers; i++) {
            struct gl_active_atomic_buffer *atomic_buffer =
               &prog->AtomicBuffers[i];
            if (atomic_buffer->StageReferences[j]) {
               prog->_LinkedShaders[j]->AtomicBuffers[intra_stage_idx] =
                  atomic_buffer;

               for (unsigned u = 0; u < atomic_buffer->NumUniforms; u++) {
                  prog->UniformStorage[atomic_buffer->Uniforms[u]].opaque[j].index =
                     intra_stage_idx;
                  prog->UniformStorage[atomic_buffer->Uniforms[u]].opaque[j].active =
                     true;
               }

               intra_stage_idx++;
            }
         }
      }
   }

   delete [] abs;
   assert(i == num_buffers);
}

void
link_check_atomic_counter_resources(struct gl_context *ctx,
                                    struct gl_shader_program *prog)
{
   unsigned num_buffers;
   active_atomic_buffer *const abs =
      find_active_atomic_counters(ctx, prog, &num_buffers);
   unsigned atomic_counters[MESA_SHADER_STAGES] = {};
   unsigned atomic_buffers[MESA_SHADER_STAGES] = {};
   unsigned total_atomic_counters = 0;
   unsigned total_atomic_buffers = 0;

   /* Sum the required resources.  Note that this counts buffers and
    * counters referenced by several shader stages multiple times
    * against the combined limit -- That's the behavior the spec
    * requires.
    */
   for (unsigned i = 0; i < ctx->Const.MaxAtomicBufferBindings; i++) {
      if (abs[i].size == 0)
         continue;

      for (unsigned j = 0; j < MESA_SHADER_STAGES; ++j) {
         const unsigned n = abs[i].stage_references[j];

         if (n) {
            atomic_counters[j] += n;
            total_atomic_counters += n;
            atomic_buffers[j]++;
            total_atomic_buffers++;
         }
      }
   }

   /* Check that they are within the supported limits. */
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (atomic_counters[i] > ctx->Const.Program[i].MaxAtomicCounters)
         linker_error(prog, "Too many %s shader atomic counters",
                      _mesa_shader_stage_to_string(i));

      if (atomic_buffers[i] > ctx->Const.Program[i].MaxAtomicBuffers)
         linker_error(prog, "Too many %s shader atomic counter buffers",
                      _mesa_shader_stage_to_string(i));
   }

   if (total_atomic_counters > ctx->Const.MaxCombinedAtomicCounters)
      linker_error(prog, "Too many combined atomic counters");

   if (total_atomic_buffers > ctx->Const.MaxCombinedAtomicBuffers)
      linker_error(prog, "Too many combined atomic buffers");

   delete [] abs;
}
