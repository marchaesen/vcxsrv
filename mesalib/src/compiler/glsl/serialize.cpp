/*
 * Copyright © 2014 Intel Corporation
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

/**
 * \file serialize.cpp
 *
 * GLSL serialization
 *
 * Supports serializing and deserializing glsl programs using a blob.
 */

#include "compiler/glsl_types.h"
#include "compiler/shader_info.h"
#include "ir_uniform.h"
#include "main/mtypes.h"
#include "main/shaderobj.h"
#include "program/program.h"
#include "string_to_uint_map.h"
#include "util/bitscan.h"


static void
write_subroutines(struct blob *metadata, struct gl_shader_program *prog)
{
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      struct gl_linked_shader *sh = prog->_LinkedShaders[i];
      if (!sh)
         continue;

      struct gl_program *glprog = sh->Program;

      blob_write_uint32(metadata, glprog->sh.NumSubroutineUniforms);
      blob_write_uint32(metadata, glprog->sh.MaxSubroutineFunctionIndex);
      blob_write_uint32(metadata, glprog->sh.NumSubroutineFunctions);
      for (unsigned j = 0; j < glprog->sh.NumSubroutineFunctions; j++) {
         int num_types = glprog->sh.SubroutineFunctions[j].num_compat_types;

         blob_write_string(metadata, glprog->sh.SubroutineFunctions[j].name);
         blob_write_uint32(metadata, glprog->sh.SubroutineFunctions[j].index);
         blob_write_uint32(metadata, num_types);

         for (int k = 0; k < num_types; k++) {
            encode_type_to_blob(metadata,
                                glprog->sh.SubroutineFunctions[j].types[k]);
         }
      }
   }
}

static void
read_subroutines(struct blob_reader *metadata, struct gl_shader_program *prog)
{
   struct gl_subroutine_function *subs;

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      struct gl_linked_shader *sh = prog->_LinkedShaders[i];
      if (!sh)
         continue;

      struct gl_program *glprog = sh->Program;

      glprog->sh.NumSubroutineUniforms = blob_read_uint32(metadata);
      glprog->sh.MaxSubroutineFunctionIndex = blob_read_uint32(metadata);
      glprog->sh.NumSubroutineFunctions = blob_read_uint32(metadata);

      subs = rzalloc_array(prog, struct gl_subroutine_function,
                           glprog->sh.NumSubroutineFunctions);
      glprog->sh.SubroutineFunctions = subs;

      for (unsigned j = 0; j < glprog->sh.NumSubroutineFunctions; j++) {
         subs[j].name = ralloc_strdup(prog, blob_read_string (metadata));
         subs[j].index = (int) blob_read_uint32(metadata);
         subs[j].num_compat_types = (int) blob_read_uint32(metadata);

         subs[j].types = rzalloc_array(prog, const struct glsl_type *,
                                       subs[j].num_compat_types);
         for (int k = 0; k < subs[j].num_compat_types; k++) {
            subs[j].types[k] = decode_type_from_blob(metadata);
         }
      }
   }
}

static void
write_buffer_block(struct blob *metadata, struct gl_uniform_block *b)
{
   blob_write_string(metadata, b->Name);
   blob_write_uint32(metadata, b->NumUniforms);
   blob_write_uint32(metadata, b->Binding);
   blob_write_uint32(metadata, b->UniformBufferSize);
   blob_write_uint32(metadata, b->stageref);

   for (unsigned j = 0; j < b->NumUniforms; j++) {
      blob_write_string(metadata, b->Uniforms[j].Name);
      blob_write_string(metadata, b->Uniforms[j].IndexName);
      encode_type_to_blob(metadata, b->Uniforms[j].Type);
      blob_write_uint32(metadata, b->Uniforms[j].Offset);
   }
}

static void
write_buffer_blocks(struct blob *metadata, struct gl_shader_program *prog)
{
   blob_write_uint32(metadata, prog->data->NumUniformBlocks);
   blob_write_uint32(metadata, prog->data->NumShaderStorageBlocks);

   for (unsigned i = 0; i < prog->data->NumUniformBlocks; i++) {
      write_buffer_block(metadata, &prog->data->UniformBlocks[i]);
   }

   for (unsigned i = 0; i < prog->data->NumShaderStorageBlocks; i++) {
      write_buffer_block(metadata, &prog->data->ShaderStorageBlocks[i]);
   }

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      struct gl_linked_shader *sh = prog->_LinkedShaders[i];
      if (!sh)
         continue;

      struct gl_program *glprog = sh->Program;

      blob_write_uint32(metadata, glprog->info.num_ubos);
      blob_write_uint32(metadata, glprog->info.num_ssbos);

      for (unsigned j = 0; j < glprog->info.num_ubos; j++) {
         uint32_t offset =
            glprog->sh.UniformBlocks[j] - prog->data->UniformBlocks;
         blob_write_uint32(metadata, offset);
      }

      for (unsigned j = 0; j < glprog->info.num_ssbos; j++) {
         uint32_t offset = glprog->sh.ShaderStorageBlocks[j] -
            prog->data->ShaderStorageBlocks;
         blob_write_uint32(metadata, offset);
      }
   }
}

static void
read_buffer_block(struct blob_reader *metadata, struct gl_uniform_block *b,
                  struct gl_shader_program *prog)
{
      b->Name = ralloc_strdup(prog->data, blob_read_string (metadata));
      b->NumUniforms = blob_read_uint32(metadata);
      b->Binding = blob_read_uint32(metadata);
      b->UniformBufferSize = blob_read_uint32(metadata);
      b->stageref = blob_read_uint32(metadata);

      b->Uniforms =
         rzalloc_array(prog->data, struct gl_uniform_buffer_variable,
                       b->NumUniforms);
      for (unsigned j = 0; j < b->NumUniforms; j++) {
         b->Uniforms[j].Name = ralloc_strdup(prog->data,
                                             blob_read_string (metadata));

         char *index_name = blob_read_string(metadata);
         if (strcmp(b->Uniforms[j].Name, index_name) == 0) {
            b->Uniforms[j].IndexName = b->Uniforms[j].Name;
         } else {
            b->Uniforms[j].IndexName = ralloc_strdup(prog->data, index_name);
         }

         b->Uniforms[j].Type = decode_type_from_blob(metadata);
         b->Uniforms[j].Offset = blob_read_uint32(metadata);
      }
}

static void
read_buffer_blocks(struct blob_reader *metadata,
                   struct gl_shader_program *prog)
{
   prog->data->NumUniformBlocks = blob_read_uint32(metadata);
   prog->data->NumShaderStorageBlocks = blob_read_uint32(metadata);

   prog->data->UniformBlocks =
      rzalloc_array(prog->data, struct gl_uniform_block,
                    prog->data->NumUniformBlocks);

   prog->data->ShaderStorageBlocks =
      rzalloc_array(prog->data, struct gl_uniform_block,
                    prog->data->NumShaderStorageBlocks);

   for (unsigned i = 0; i < prog->data->NumUniformBlocks; i++) {
      read_buffer_block(metadata, &prog->data->UniformBlocks[i], prog);
   }

   for (unsigned i = 0; i < prog->data->NumShaderStorageBlocks; i++) {
      read_buffer_block(metadata, &prog->data->ShaderStorageBlocks[i], prog);
   }

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      struct gl_linked_shader *sh = prog->_LinkedShaders[i];
      if (!sh)
         continue;

      struct gl_program *glprog = sh->Program;

      glprog->info.num_ubos = blob_read_uint32(metadata);
      glprog->info.num_ssbos = blob_read_uint32(metadata);

      glprog->sh.UniformBlocks =
         rzalloc_array(glprog, gl_uniform_block *, glprog->info.num_ubos);
      glprog->sh.ShaderStorageBlocks =
         rzalloc_array(glprog, gl_uniform_block *, glprog->info.num_ssbos);

      for (unsigned j = 0; j < glprog->info.num_ubos; j++) {
         uint32_t offset = blob_read_uint32(metadata);
         glprog->sh.UniformBlocks[j] = prog->data->UniformBlocks + offset;
      }

      for (unsigned j = 0; j < glprog->info.num_ssbos; j++) {
         uint32_t offset = blob_read_uint32(metadata);
         glprog->sh.ShaderStorageBlocks[j] =
            prog->data->ShaderStorageBlocks + offset;
      }
   }
}

static void
write_atomic_buffers(struct blob *metadata, struct gl_shader_program *prog)
{
   blob_write_uint32(metadata, prog->data->NumAtomicBuffers);

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (prog->_LinkedShaders[i]) {
         struct gl_program *glprog = prog->_LinkedShaders[i]->Program;
         blob_write_uint32(metadata, glprog->info.num_abos);
      }
   }

   for (unsigned i = 0; i < prog->data->NumAtomicBuffers; i++) {
      blob_write_uint32(metadata, prog->data->AtomicBuffers[i].Binding);
      blob_write_uint32(metadata, prog->data->AtomicBuffers[i].MinimumSize);
      blob_write_uint32(metadata, prog->data->AtomicBuffers[i].NumUniforms);

      blob_write_bytes(metadata, prog->data->AtomicBuffers[i].StageReferences,
                       sizeof(prog->data->AtomicBuffers[i].StageReferences));

      for (unsigned j = 0; j < prog->data->AtomicBuffers[i].NumUniforms; j++) {
         blob_write_uint32(metadata, prog->data->AtomicBuffers[i].Uniforms[j]);
      }
   }
}

static void
read_atomic_buffers(struct blob_reader *metadata,
                     struct gl_shader_program *prog)
{
   prog->data->NumAtomicBuffers = blob_read_uint32(metadata);
   prog->data->AtomicBuffers =
      rzalloc_array(prog, gl_active_atomic_buffer,
                    prog->data->NumAtomicBuffers);

   struct gl_active_atomic_buffer **stage_buff_list[MESA_SHADER_STAGES];
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (prog->_LinkedShaders[i]) {
         struct gl_program *glprog = prog->_LinkedShaders[i]->Program;

         glprog->info.num_abos = blob_read_uint32(metadata);
         glprog->sh.AtomicBuffers =
            rzalloc_array(glprog, gl_active_atomic_buffer *,
                          glprog->info.num_abos);
         stage_buff_list[i] = glprog->sh.AtomicBuffers;
      }
   }

   for (unsigned i = 0; i < prog->data->NumAtomicBuffers; i++) {
      prog->data->AtomicBuffers[i].Binding = blob_read_uint32(metadata);
      prog->data->AtomicBuffers[i].MinimumSize = blob_read_uint32(metadata);
      prog->data->AtomicBuffers[i].NumUniforms = blob_read_uint32(metadata);

      blob_copy_bytes(metadata,
                      (uint8_t *) &prog->data->AtomicBuffers[i].StageReferences,
                      sizeof(prog->data->AtomicBuffers[i].StageReferences));

      prog->data->AtomicBuffers[i].Uniforms = rzalloc_array(prog, unsigned,
         prog->data->AtomicBuffers[i].NumUniforms);

      for (unsigned j = 0; j < prog->data->AtomicBuffers[i].NumUniforms; j++) {
         prog->data->AtomicBuffers[i].Uniforms[j] = blob_read_uint32(metadata);
      }

      for (unsigned j = 0; j < MESA_SHADER_STAGES; j++) {
         if (prog->data->AtomicBuffers[i].StageReferences[j]) {
            *stage_buff_list[j] = &prog->data->AtomicBuffers[i];
            stage_buff_list[j]++;
         }
      }
   }
}

static void
write_xfb(struct blob *metadata, struct gl_shader_program *shProg)
{
   struct gl_program *prog = shProg->last_vert_prog;

   if (!prog) {
      blob_write_uint32(metadata, ~0u);
      return;
   }

   struct gl_transform_feedback_info *ltf = prog->sh.LinkedTransformFeedback;

   blob_write_uint32(metadata, prog->info.stage);

   blob_write_uint32(metadata, ltf->NumOutputs);
   blob_write_uint32(metadata, ltf->ActiveBuffers);
   blob_write_uint32(metadata, ltf->NumVarying);

   blob_write_bytes(metadata, ltf->Outputs,
                    sizeof(struct gl_transform_feedback_output) *
                       ltf->NumOutputs);

   for (int i = 0; i < ltf->NumVarying; i++) {
      blob_write_string(metadata, ltf->Varyings[i].Name);
      blob_write_uint32(metadata, ltf->Varyings[i].Type);
      blob_write_uint32(metadata, ltf->Varyings[i].BufferIndex);
      blob_write_uint32(metadata, ltf->Varyings[i].Size);
      blob_write_uint32(metadata, ltf->Varyings[i].Offset);
   }

   blob_write_bytes(metadata, ltf->Buffers,
                    sizeof(struct gl_transform_feedback_buffer) *
                       MAX_FEEDBACK_BUFFERS);
}

static void
read_xfb(struct blob_reader *metadata, struct gl_shader_program *shProg)
{
   unsigned xfb_stage = blob_read_uint32(metadata);

   if (xfb_stage == ~0u)
      return;

   struct gl_program *prog = shProg->_LinkedShaders[xfb_stage]->Program;
   struct gl_transform_feedback_info *ltf =
      rzalloc(prog, struct gl_transform_feedback_info);

   prog->sh.LinkedTransformFeedback = ltf;
   shProg->last_vert_prog = prog;

   ltf->NumOutputs = blob_read_uint32(metadata);
   ltf->ActiveBuffers = blob_read_uint32(metadata);
   ltf->NumVarying = blob_read_uint32(metadata);

   ltf->Outputs = rzalloc_array(prog, struct gl_transform_feedback_output,
                                ltf->NumOutputs);

   blob_copy_bytes(metadata, (uint8_t *) ltf->Outputs,
                   sizeof(struct gl_transform_feedback_output) *
                      ltf->NumOutputs);

   ltf->Varyings = rzalloc_array(prog,
                                 struct gl_transform_feedback_varying_info,
                                 ltf->NumVarying);

   for (int i = 0; i < ltf->NumVarying; i++) {
      ltf->Varyings[i].Name = ralloc_strdup(prog, blob_read_string(metadata));
      ltf->Varyings[i].Type = blob_read_uint32(metadata);
      ltf->Varyings[i].BufferIndex = blob_read_uint32(metadata);
      ltf->Varyings[i].Size = blob_read_uint32(metadata);
      ltf->Varyings[i].Offset = blob_read_uint32(metadata);
   }

   blob_copy_bytes(metadata, (uint8_t *) ltf->Buffers,
                   sizeof(struct gl_transform_feedback_buffer) *
                      MAX_FEEDBACK_BUFFERS);
}

static bool
has_uniform_storage(struct gl_shader_program *prog, unsigned idx)
{
   if (!prog->data->UniformStorage[idx].builtin &&
       !prog->data->UniformStorage[idx].is_shader_storage &&
       prog->data->UniformStorage[idx].block_index == -1)
      return true;

   return false;
}

static void
write_uniforms(struct blob *metadata, struct gl_shader_program *prog)
{
   blob_write_uint32(metadata, prog->SamplersValidated);
   blob_write_uint32(metadata, prog->data->NumUniformStorage);
   blob_write_uint32(metadata, prog->data->NumUniformDataSlots);

   for (unsigned i = 0; i < prog->data->NumUniformStorage; i++) {
      encode_type_to_blob(metadata, prog->data->UniformStorage[i].type);
      blob_write_uint32(metadata, prog->data->UniformStorage[i].array_elements);
      blob_write_string(metadata, prog->data->UniformStorage[i].name);
      blob_write_uint32(metadata, prog->data->UniformStorage[i].builtin);
      blob_write_uint32(metadata, prog->data->UniformStorage[i].remap_location);
      blob_write_uint32(metadata, prog->data->UniformStorage[i].block_index);
      blob_write_uint32(metadata, prog->data->UniformStorage[i].atomic_buffer_index);
      blob_write_uint32(metadata, prog->data->UniformStorage[i].offset);
      blob_write_uint32(metadata, prog->data->UniformStorage[i].array_stride);
      blob_write_uint32(metadata, prog->data->UniformStorage[i].hidden);
      blob_write_uint32(metadata, prog->data->UniformStorage[i].is_shader_storage);
      blob_write_uint32(metadata, prog->data->UniformStorage[i].active_shader_mask);
      blob_write_uint32(metadata, prog->data->UniformStorage[i].matrix_stride);
      blob_write_uint32(metadata, prog->data->UniformStorage[i].row_major);
      blob_write_uint32(metadata, prog->data->UniformStorage[i].is_bindless);
      blob_write_uint32(metadata,
                        prog->data->UniformStorage[i].num_compatible_subroutines);
      blob_write_uint32(metadata,
                        prog->data->UniformStorage[i].top_level_array_size);
      blob_write_uint32(metadata,
                        prog->data->UniformStorage[i].top_level_array_stride);

     if (has_uniform_storage(prog, i)) {
         blob_write_uint32(metadata, prog->data->UniformStorage[i].storage -
                                     prog->data->UniformDataSlots);
      }

      blob_write_bytes(metadata, prog->data->UniformStorage[i].opaque,
                       sizeof(prog->data->UniformStorage[i].opaque));
   }

   /* Here we cache all uniform values. We do this to retain values for
    * uniforms with initialisers and also hidden uniforms that may be lowered
    * constant arrays. We could possibly just store the values we need but for
    * now we just store everything.
    */
   blob_write_uint32(metadata, prog->data->NumHiddenUniforms);
   for (unsigned i = 0; i < prog->data->NumUniformStorage; i++) {
      if (has_uniform_storage(prog, i)) {
         unsigned vec_size =
            prog->data->UniformStorage[i].type->component_slots() *
            MAX2(prog->data->UniformStorage[i].array_elements, 1);
         unsigned slot =
            prog->data->UniformStorage[i].storage -
            prog->data->UniformDataSlots;
         blob_write_bytes(metadata, &prog->data->UniformDataDefaults[slot],
                          sizeof(union gl_constant_value) * vec_size);
      }
   }
}

static void
read_uniforms(struct blob_reader *metadata, struct gl_shader_program *prog)
{
   struct gl_uniform_storage *uniforms;
   union gl_constant_value *data;

   prog->SamplersValidated = blob_read_uint32(metadata);
   prog->data->NumUniformStorage = blob_read_uint32(metadata);
   prog->data->NumUniformDataSlots = blob_read_uint32(metadata);

   uniforms = rzalloc_array(prog->data, struct gl_uniform_storage,
                            prog->data->NumUniformStorage);
   prog->data->UniformStorage = uniforms;

   data = rzalloc_array(uniforms, union gl_constant_value,
                        prog->data->NumUniformDataSlots);
   prog->data->UniformDataSlots = data;
   prog->data->UniformDataDefaults =
      rzalloc_array(uniforms, union gl_constant_value,
                    prog->data->NumUniformDataSlots);

   prog->UniformHash = new string_to_uint_map;

   for (unsigned i = 0; i < prog->data->NumUniformStorage; i++) {
      uniforms[i].type = decode_type_from_blob(metadata);
      uniforms[i].array_elements = blob_read_uint32(metadata);
      uniforms[i].name = ralloc_strdup(prog, blob_read_string (metadata));
      uniforms[i].builtin = blob_read_uint32(metadata);
      uniforms[i].remap_location = blob_read_uint32(metadata);
      uniforms[i].block_index = blob_read_uint32(metadata);
      uniforms[i].atomic_buffer_index = blob_read_uint32(metadata);
      uniforms[i].offset = blob_read_uint32(metadata);
      uniforms[i].array_stride = blob_read_uint32(metadata);
      uniforms[i].hidden = blob_read_uint32(metadata);
      uniforms[i].is_shader_storage = blob_read_uint32(metadata);
      uniforms[i].active_shader_mask = blob_read_uint32(metadata);
      uniforms[i].matrix_stride = blob_read_uint32(metadata);
      uniforms[i].row_major = blob_read_uint32(metadata);
      uniforms[i].is_bindless = blob_read_uint32(metadata);
      uniforms[i].num_compatible_subroutines = blob_read_uint32(metadata);
      uniforms[i].top_level_array_size = blob_read_uint32(metadata);
      uniforms[i].top_level_array_stride = blob_read_uint32(metadata);
      prog->UniformHash->put(i, uniforms[i].name);

      if (has_uniform_storage(prog, i)) {
         uniforms[i].storage = data + blob_read_uint32(metadata);
      }

      memcpy(uniforms[i].opaque,
             blob_read_bytes(metadata, sizeof(uniforms[i].opaque)),
             sizeof(uniforms[i].opaque));
   }

   /* Restore uniform values. */
   prog->data->NumHiddenUniforms = blob_read_uint32(metadata);
   for (unsigned i = 0; i < prog->data->NumUniformStorage; i++) {
      if (has_uniform_storage(prog, i)) {
         unsigned vec_size =
            prog->data->UniformStorage[i].type->component_slots() *
            MAX2(prog->data->UniformStorage[i].array_elements, 1);
         unsigned slot =
            prog->data->UniformStorage[i].storage -
            prog->data->UniformDataSlots;
         blob_copy_bytes(metadata,
                         (uint8_t *) &prog->data->UniformDataSlots[slot],
                         sizeof(union gl_constant_value) * vec_size);

        assert(vec_size + prog->data->UniformStorage[i].storage <=
               data +  prog->data->NumUniformDataSlots);
      }
   }

   memcpy(prog->data->UniformDataDefaults, prog->data->UniformDataSlots,
          sizeof(union gl_constant_value) * prog->data->NumUniformDataSlots);
}

enum uniform_remap_type
{
   remap_type_inactive_explicit_location,
   remap_type_null_ptr,
   remap_type_uniform_offset
};

static void
write_uniform_remap_table_entry(struct blob *metadata,
                                gl_uniform_storage *uniform_storage,
                                gl_uniform_storage *entry)
{
   if (entry == INACTIVE_UNIFORM_EXPLICIT_LOCATION) {
      blob_write_uint32(metadata, remap_type_inactive_explicit_location);
   } else if (entry == NULL) {
      blob_write_uint32(metadata, remap_type_null_ptr);
   } else {
      blob_write_uint32(metadata, remap_type_uniform_offset);

      uint32_t offset = entry - uniform_storage;
      blob_write_uint32(metadata, offset);
   }
}

static void
write_uniform_remap_tables(struct blob *metadata,
                           struct gl_shader_program *prog)
{
   blob_write_uint32(metadata, prog->NumUniformRemapTable);

   for (unsigned i = 0; i < prog->NumUniformRemapTable; i++) {
      write_uniform_remap_table_entry(metadata, prog->data->UniformStorage,
                                      prog->UniformRemapTable[i]);
   }

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      struct gl_linked_shader *sh = prog->_LinkedShaders[i];
      if (sh) {
         struct gl_program *glprog = sh->Program;
         blob_write_uint32(metadata, glprog->sh.NumSubroutineUniformRemapTable);

         for (unsigned j = 0; j < glprog->sh.NumSubroutineUniformRemapTable; j++) {
            write_uniform_remap_table_entry(metadata,
                                            prog->data->UniformStorage,
                                            glprog->sh.SubroutineUniformRemapTable[j]);
         }
      }
   }
}

static void
read_uniform_remap_table_entry(struct blob_reader *metadata,
                               gl_uniform_storage *uniform_storage,
                               gl_uniform_storage **entry,
                               enum uniform_remap_type type)
{
   if (type == remap_type_inactive_explicit_location) {
      *entry = INACTIVE_UNIFORM_EXPLICIT_LOCATION;
   } else if (type == remap_type_null_ptr) {
      *entry = NULL;
   } else {
      uint32_t uni_offset = blob_read_uint32(metadata);
      *entry = uniform_storage + uni_offset;
   }
}

static void
read_uniform_remap_tables(struct blob_reader *metadata,
                          struct gl_shader_program *prog)
{
   prog->NumUniformRemapTable = blob_read_uint32(metadata);

   prog->UniformRemapTable = rzalloc_array(prog, struct gl_uniform_storage *,
                                           prog->NumUniformRemapTable);

   for (unsigned i = 0; i < prog->NumUniformRemapTable; i++) {
      enum uniform_remap_type type =
         (enum uniform_remap_type) blob_read_uint32(metadata);

      read_uniform_remap_table_entry(metadata, prog->data->UniformStorage,
                                     &prog->UniformRemapTable[i], type);
   }

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      struct gl_linked_shader *sh = prog->_LinkedShaders[i];
      if (sh) {
         struct gl_program *glprog = sh->Program;
         glprog->sh.NumSubroutineUniformRemapTable = blob_read_uint32(metadata);

         glprog->sh.SubroutineUniformRemapTable =
            rzalloc_array(glprog, struct gl_uniform_storage *,
                          glprog->sh.NumSubroutineUniformRemapTable);

         for (unsigned j = 0; j < glprog->sh.NumSubroutineUniformRemapTable; j++) {
            enum uniform_remap_type type =
               (enum uniform_remap_type) blob_read_uint32(metadata);

            read_uniform_remap_table_entry(metadata,
                                           prog->data->UniformStorage,
                                           &glprog->sh.SubroutineUniformRemapTable[j],
                                           type);
         }
      }
   }
}

struct whte_closure
{
   struct blob *blob;
   size_t num_entries;
};

static void
write_hash_table_entry(const char *key, unsigned value, void *closure)
{
   struct whte_closure *whte = (struct whte_closure *) closure;

   blob_write_string(whte->blob, key);
   blob_write_uint32(whte->blob, value);

   whte->num_entries++;
}

static void
write_hash_table(struct blob *metadata, struct string_to_uint_map *hash)
{
   size_t offset;
   struct whte_closure whte;

   whte.blob = metadata;
   whte.num_entries = 0;

   offset = metadata->size;

   /* Write a placeholder for the hashtable size. */
   blob_write_uint32 (metadata, 0);

   hash->iterate(write_hash_table_entry, &whte);

   /* Overwrite with the computed number of entries written. */
   blob_overwrite_uint32 (metadata, offset, whte.num_entries);
}

static void
read_hash_table(struct blob_reader *metadata, struct string_to_uint_map *hash)
{
   size_t i, num_entries;
   const char *key;
   uint32_t value;

   num_entries = blob_read_uint32 (metadata);

   for (i = 0; i < num_entries; i++) {
      key = blob_read_string(metadata);
      value = blob_read_uint32(metadata);

      hash->put(value, key);
   }
}

static void
write_hash_tables(struct blob *metadata, struct gl_shader_program *prog)
{
   write_hash_table(metadata, prog->AttributeBindings);
   write_hash_table(metadata, prog->FragDataBindings);
   write_hash_table(metadata, prog->FragDataIndexBindings);
}

static void
read_hash_tables(struct blob_reader *metadata, struct gl_shader_program *prog)
{
   read_hash_table(metadata, prog->AttributeBindings);
   read_hash_table(metadata, prog->FragDataBindings);
   read_hash_table(metadata, prog->FragDataIndexBindings);
}

static void
write_shader_subroutine_index(struct blob *metadata,
                              struct gl_linked_shader *sh,
                              struct gl_program_resource *res)
{
   assert(sh);

   for (unsigned j = 0; j < sh->Program->sh.NumSubroutineFunctions; j++) {
      if (strcmp(((gl_subroutine_function *)res->Data)->name,
                 sh->Program->sh.SubroutineFunctions[j].name) == 0) {
         blob_write_uint32(metadata, j);
         break;
      }
   }
}

static void
get_shader_var_and_pointer_sizes(size_t *s_var_size, size_t *s_var_ptrs,
                                 const gl_shader_variable *var)
{
   *s_var_size = sizeof(gl_shader_variable);
   *s_var_ptrs =
      sizeof(var->type) +
      sizeof(var->interface_type) +
      sizeof(var->outermost_struct_type) +
      sizeof(var->name);
}

static void
write_program_resource_data(struct blob *metadata,
                            struct gl_shader_program *prog,
                            struct gl_program_resource *res)
{
   struct gl_linked_shader *sh;

   switch(res->Type) {
   case GL_PROGRAM_INPUT:
   case GL_PROGRAM_OUTPUT: {
      const gl_shader_variable *var = (gl_shader_variable *)res->Data;

      encode_type_to_blob(metadata, var->type);
      encode_type_to_blob(metadata, var->interface_type);
      encode_type_to_blob(metadata, var->outermost_struct_type);

      blob_write_string(metadata, var->name);

      size_t s_var_size, s_var_ptrs;
      get_shader_var_and_pointer_sizes(&s_var_size, &s_var_ptrs, var);

      /* Write gl_shader_variable skipping over the pointers */
      blob_write_bytes(metadata, ((char *)var) + s_var_ptrs,
                       s_var_size - s_var_ptrs);
      break;
   }
   case GL_UNIFORM_BLOCK:
      for (unsigned i = 0; i < prog->data->NumUniformBlocks; i++) {
         if (strcmp(((gl_uniform_block *)res->Data)->Name,
                    prog->data->UniformBlocks[i].Name) == 0) {
            blob_write_uint32(metadata, i);
            break;
         }
      }
      break;
   case GL_SHADER_STORAGE_BLOCK:
      for (unsigned i = 0; i < prog->data->NumShaderStorageBlocks; i++) {
         if (strcmp(((gl_uniform_block *)res->Data)->Name,
                    prog->data->ShaderStorageBlocks[i].Name) == 0) {
            blob_write_uint32(metadata, i);
            break;
         }
      }
      break;
   case GL_BUFFER_VARIABLE:
   case GL_VERTEX_SUBROUTINE_UNIFORM:
   case GL_GEOMETRY_SUBROUTINE_UNIFORM:
   case GL_FRAGMENT_SUBROUTINE_UNIFORM:
   case GL_COMPUTE_SUBROUTINE_UNIFORM:
   case GL_TESS_CONTROL_SUBROUTINE_UNIFORM:
   case GL_TESS_EVALUATION_SUBROUTINE_UNIFORM:
   case GL_UNIFORM:
      for (unsigned i = 0; i < prog->data->NumUniformStorage; i++) {
         if (strcmp(((gl_uniform_storage *)res->Data)->name,
                    prog->data->UniformStorage[i].name) == 0) {
            blob_write_uint32(metadata, i);
            break;
         }
      }
      break;
   case GL_ATOMIC_COUNTER_BUFFER:
      for (unsigned i = 0; i < prog->data->NumAtomicBuffers; i++) {
         if (((gl_active_atomic_buffer *)res->Data)->Binding ==
             prog->data->AtomicBuffers[i].Binding) {
            blob_write_uint32(metadata, i);
            break;
         }
      }
      break;
   case GL_TRANSFORM_FEEDBACK_BUFFER:
      for (unsigned i = 0; i < MAX_FEEDBACK_BUFFERS; i++) {
         if (((gl_transform_feedback_buffer *)res->Data)->Binding ==
             prog->last_vert_prog->sh.LinkedTransformFeedback->Buffers[i].Binding) {
            blob_write_uint32(metadata, i);
            break;
         }
      }
      break;
   case GL_TRANSFORM_FEEDBACK_VARYING:
      for (int i = 0; i < prog->last_vert_prog->sh.LinkedTransformFeedback->NumVarying; i++) {
         if (strcmp(((gl_transform_feedback_varying_info *)res->Data)->Name,
                    prog->last_vert_prog->sh.LinkedTransformFeedback->Varyings[i].Name) == 0) {
            blob_write_uint32(metadata, i);
            break;
         }
      }
      break;
   case GL_VERTEX_SUBROUTINE:
   case GL_TESS_CONTROL_SUBROUTINE:
   case GL_TESS_EVALUATION_SUBROUTINE:
   case GL_GEOMETRY_SUBROUTINE:
   case GL_FRAGMENT_SUBROUTINE:
   case GL_COMPUTE_SUBROUTINE:
      sh =
         prog->_LinkedShaders[_mesa_shader_stage_from_subroutine(res->Type)];
      write_shader_subroutine_index(metadata, sh, res);
      break;
   default:
      assert(!"Support for writing resource not yet implemented.");
   }
}

static void
read_program_resource_data(struct blob_reader *metadata,
                           struct gl_shader_program *prog,
                           struct gl_program_resource *res)
{
   struct gl_linked_shader *sh;

   switch(res->Type) {
   case GL_PROGRAM_INPUT:
   case GL_PROGRAM_OUTPUT: {
      gl_shader_variable *var = ralloc(prog, struct gl_shader_variable);

      var->type = decode_type_from_blob(metadata);
      var->interface_type = decode_type_from_blob(metadata);
      var->outermost_struct_type = decode_type_from_blob(metadata);

      var->name = ralloc_strdup(prog, blob_read_string(metadata));

      size_t s_var_size, s_var_ptrs;
      get_shader_var_and_pointer_sizes(&s_var_size, &s_var_ptrs, var);

      blob_copy_bytes(metadata, ((uint8_t *) var) + s_var_ptrs,
                      s_var_size - s_var_ptrs);

      res->Data = var;
      break;
   }
   case GL_UNIFORM_BLOCK:
      res->Data = &prog->data->UniformBlocks[blob_read_uint32(metadata)];
      break;
   case GL_SHADER_STORAGE_BLOCK:
      res->Data = &prog->data->ShaderStorageBlocks[blob_read_uint32(metadata)];
      break;
   case GL_BUFFER_VARIABLE:
   case GL_VERTEX_SUBROUTINE_UNIFORM:
   case GL_GEOMETRY_SUBROUTINE_UNIFORM:
   case GL_FRAGMENT_SUBROUTINE_UNIFORM:
   case GL_COMPUTE_SUBROUTINE_UNIFORM:
   case GL_TESS_CONTROL_SUBROUTINE_UNIFORM:
   case GL_TESS_EVALUATION_SUBROUTINE_UNIFORM:
   case GL_UNIFORM:
      res->Data = &prog->data->UniformStorage[blob_read_uint32(metadata)];
      break;
   case GL_ATOMIC_COUNTER_BUFFER:
      res->Data = &prog->data->AtomicBuffers[blob_read_uint32(metadata)];
      break;
   case GL_TRANSFORM_FEEDBACK_BUFFER:
      res->Data = &prog->last_vert_prog->
         sh.LinkedTransformFeedback->Buffers[blob_read_uint32(metadata)];
      break;
   case GL_TRANSFORM_FEEDBACK_VARYING:
      res->Data = &prog->last_vert_prog->
         sh.LinkedTransformFeedback->Varyings[blob_read_uint32(metadata)];
      break;
   case GL_VERTEX_SUBROUTINE:
   case GL_TESS_CONTROL_SUBROUTINE:
   case GL_TESS_EVALUATION_SUBROUTINE:
   case GL_GEOMETRY_SUBROUTINE:
   case GL_FRAGMENT_SUBROUTINE:
   case GL_COMPUTE_SUBROUTINE:
      sh =
         prog->_LinkedShaders[_mesa_shader_stage_from_subroutine(res->Type)];
      res->Data =
         &sh->Program->sh.SubroutineFunctions[blob_read_uint32(metadata)];
      break;
   default:
      assert(!"Support for reading resource not yet implemented.");
   }
}

static void
write_program_resource_list(struct blob *metadata,
                            struct gl_shader_program *prog)
{
   blob_write_uint32(metadata, prog->data->NumProgramResourceList);

   for (unsigned i = 0; i < prog->data->NumProgramResourceList; i++) {
      blob_write_uint32(metadata, prog->data->ProgramResourceList[i].Type);
      write_program_resource_data(metadata, prog,
                                  &prog->data->ProgramResourceList[i]);
      blob_write_bytes(metadata,
                       &prog->data->ProgramResourceList[i].StageReferences,
                       sizeof(prog->data->ProgramResourceList[i].StageReferences));
   }
}

static void
read_program_resource_list(struct blob_reader *metadata,
                           struct gl_shader_program *prog)
{
   prog->data->NumProgramResourceList = blob_read_uint32(metadata);

   prog->data->ProgramResourceList =
      ralloc_array(prog->data, gl_program_resource,
                   prog->data->NumProgramResourceList);

   for (unsigned i = 0; i < prog->data->NumProgramResourceList; i++) {
      prog->data->ProgramResourceList[i].Type = blob_read_uint32(metadata);
      read_program_resource_data(metadata, prog,
                                 &prog->data->ProgramResourceList[i]);
      blob_copy_bytes(metadata,
                      (uint8_t *) &prog->data->ProgramResourceList[i].StageReferences,
                      sizeof(prog->data->ProgramResourceList[i].StageReferences));
   }
}

static void
write_shader_parameters(struct blob *metadata,
                        struct gl_program_parameter_list *params)
{
   blob_write_uint32(metadata, params->NumParameters);
   blob_write_uint32(metadata, params->NumParameterValues);
   uint32_t i = 0;

   while (i < params->NumParameters) {
      struct gl_program_parameter *param = &params->Parameters[i];

      blob_write_uint32(metadata, param->Type);
      blob_write_string(metadata, param->Name);
      blob_write_uint32(metadata, param->Size);
      blob_write_uint32(metadata, param->DataType);
      blob_write_bytes(metadata, param->StateIndexes,
                       sizeof(param->StateIndexes));

      i++;
   }

   blob_write_bytes(metadata, params->ParameterValues,
                    sizeof(gl_constant_value) * params->NumParameterValues);

   blob_write_bytes(metadata, params->ParameterValueOffset,
                    sizeof(uint32_t) * params->NumParameters);

   blob_write_uint32(metadata, params->StateFlags);
}

static void
read_shader_parameters(struct blob_reader *metadata,
                       struct gl_program_parameter_list *params)
{
   gl_state_index16 state_indexes[STATE_LENGTH];
   uint32_t i = 0;
   uint32_t num_parameters = blob_read_uint32(metadata);
   uint32_t num_parameters_values = blob_read_uint32(metadata);

   _mesa_reserve_parameter_storage(params, num_parameters);
   while (i < num_parameters) {
      gl_register_file type = (gl_register_file) blob_read_uint32(metadata);
      const char *name = blob_read_string(metadata);
      unsigned size = blob_read_uint32(metadata);
      unsigned data_type = blob_read_uint32(metadata);
      blob_copy_bytes(metadata, (uint8_t *) state_indexes,
                      sizeof(state_indexes));

      _mesa_add_parameter(params, type, name, size, data_type,
                          NULL, state_indexes, false);

      i++;
   }

   blob_copy_bytes(metadata, (uint8_t *) params->ParameterValues,
                   sizeof(gl_constant_value) * num_parameters_values);

   blob_copy_bytes(metadata, (uint8_t *) params->ParameterValueOffset,
                   sizeof(uint32_t) * num_parameters);

   params->StateFlags = blob_read_uint32(metadata);
}

static void
write_shader_metadata(struct blob *metadata, gl_linked_shader *shader)
{
   assert(shader->Program);
   struct gl_program *glprog = shader->Program;
   unsigned i;

   blob_write_bytes(metadata, glprog->TexturesUsed,
                    sizeof(glprog->TexturesUsed));
   blob_write_uint64(metadata, glprog->SamplersUsed);

   blob_write_bytes(metadata, glprog->SamplerUnits,
                    sizeof(glprog->SamplerUnits));
   blob_write_bytes(metadata, glprog->sh.SamplerTargets,
                    sizeof(glprog->sh.SamplerTargets));
   blob_write_uint32(metadata, glprog->ShadowSamplers);

   blob_write_bytes(metadata, glprog->sh.ImageAccess,
                    sizeof(glprog->sh.ImageAccess));
   blob_write_bytes(metadata, glprog->sh.ImageUnits,
                    sizeof(glprog->sh.ImageUnits));

   size_t ptr_size = sizeof(GLvoid *);

   blob_write_uint32(metadata, glprog->sh.NumBindlessSamplers);
   blob_write_uint32(metadata, glprog->sh.HasBoundBindlessSampler);
   for (i = 0; i < glprog->sh.NumBindlessSamplers; i++) {
      blob_write_bytes(metadata, &glprog->sh.BindlessSamplers[i],
                       sizeof(struct gl_bindless_sampler) - ptr_size);
   }

   blob_write_uint32(metadata, glprog->sh.NumBindlessImages);
   blob_write_uint32(metadata, glprog->sh.HasBoundBindlessImage);
   for (i = 0; i < glprog->sh.NumBindlessImages; i++) {
      blob_write_bytes(metadata, &glprog->sh.BindlessImages[i],
                       sizeof(struct gl_bindless_image) - ptr_size);
   }

   blob_write_bytes(metadata, &glprog->sh.fs.BlendSupport,
                    sizeof(glprog->sh.fs.BlendSupport));

   write_shader_parameters(metadata, glprog->Parameters);

   assert((glprog->driver_cache_blob == NULL) ==
          (glprog->driver_cache_blob_size == 0));
   blob_write_uint32(metadata, (uint32_t)glprog->driver_cache_blob_size);
   if (glprog->driver_cache_blob_size > 0) {
      blob_write_bytes(metadata, glprog->driver_cache_blob,
                       glprog->driver_cache_blob_size);
   }
}

static void
read_shader_metadata(struct blob_reader *metadata,
                     struct gl_program *glprog,
                     gl_linked_shader *linked)
{
   unsigned i;

   blob_copy_bytes(metadata, (uint8_t *) glprog->TexturesUsed,
                   sizeof(glprog->TexturesUsed));
   glprog->SamplersUsed = blob_read_uint64(metadata);

   blob_copy_bytes(metadata, (uint8_t *) glprog->SamplerUnits,
                   sizeof(glprog->SamplerUnits));
   blob_copy_bytes(metadata, (uint8_t *) glprog->sh.SamplerTargets,
                   sizeof(glprog->sh.SamplerTargets));
   glprog->ShadowSamplers = blob_read_uint32(metadata);

   blob_copy_bytes(metadata, (uint8_t *) glprog->sh.ImageAccess,
                   sizeof(glprog->sh.ImageAccess));
   blob_copy_bytes(metadata, (uint8_t *) glprog->sh.ImageUnits,
                   sizeof(glprog->sh.ImageUnits));

   size_t ptr_size = sizeof(GLvoid *);

   glprog->sh.NumBindlessSamplers = blob_read_uint32(metadata);
   glprog->sh.HasBoundBindlessSampler = blob_read_uint32(metadata);
   if (glprog->sh.NumBindlessSamplers > 0) {
      glprog->sh.BindlessSamplers =
         rzalloc_array(glprog, gl_bindless_sampler,
                       glprog->sh.NumBindlessSamplers);

      for (i = 0; i < glprog->sh.NumBindlessSamplers; i++) {
         blob_copy_bytes(metadata, (uint8_t *) &glprog->sh.BindlessSamplers[i],
                         sizeof(struct gl_bindless_sampler) - ptr_size);
      }
   }

   glprog->sh.NumBindlessImages = blob_read_uint32(metadata);
   glprog->sh.HasBoundBindlessImage = blob_read_uint32(metadata);
   if (glprog->sh.NumBindlessImages > 0) {
      glprog->sh.BindlessImages =
         rzalloc_array(glprog, gl_bindless_image,
                       glprog->sh.NumBindlessImages);

      for (i = 0; i < glprog->sh.NumBindlessImages; i++) {
         blob_copy_bytes(metadata, (uint8_t *) &glprog->sh.BindlessImages[i],
                        sizeof(struct gl_bindless_image) - ptr_size);
      }
   }

   blob_copy_bytes(metadata, (uint8_t *) &glprog->sh.fs.BlendSupport,
                   sizeof(glprog->sh.fs.BlendSupport));

   glprog->Parameters = _mesa_new_parameter_list();
   read_shader_parameters(metadata, glprog->Parameters);

   glprog->driver_cache_blob_size = (size_t)blob_read_uint32(metadata);
   if (glprog->driver_cache_blob_size > 0) {
      glprog->driver_cache_blob =
         (uint8_t*)ralloc_size(glprog, glprog->driver_cache_blob_size);
      blob_copy_bytes(metadata, glprog->driver_cache_blob,
                      glprog->driver_cache_blob_size);
   }
}

static void
get_shader_info_and_pointer_sizes(size_t *s_info_size, size_t *s_info_ptrs,
                                  shader_info *info)
{
   *s_info_size = sizeof(shader_info);
   *s_info_ptrs = sizeof(info->name) + sizeof(info->label);
}

static void
create_linked_shader_and_program(struct gl_context *ctx,
                                 gl_shader_stage stage,
                                 struct gl_shader_program *prog,
                                 struct blob_reader *metadata)
{
   struct gl_program *glprog;

   struct gl_linked_shader *linked = rzalloc(NULL, struct gl_linked_shader);
   linked->Stage = stage;

   glprog = ctx->Driver.NewProgram(ctx, _mesa_shader_stage_to_program(stage),
                                   prog->Name, false);
   glprog->info.stage = stage;
   linked->Program = glprog;

   read_shader_metadata(metadata, glprog, linked);

   glprog->info.name = ralloc_strdup(glprog, blob_read_string(metadata));
   glprog->info.label = ralloc_strdup(glprog, blob_read_string(metadata));

   size_t s_info_size, s_info_ptrs;
   get_shader_info_and_pointer_sizes(&s_info_size, &s_info_ptrs,
                                     &glprog->info);

   /* Restore shader info */
   blob_copy_bytes(metadata, ((uint8_t *) &glprog->info) + s_info_ptrs,
                   s_info_size - s_info_ptrs);

   _mesa_reference_shader_program_data(ctx, &glprog->sh.data, prog->data);
   _mesa_reference_program(ctx, &linked->Program, glprog);
   prog->_LinkedShaders[stage] = linked;
}

extern "C" void
serialize_glsl_program(struct blob *blob, struct gl_context *ctx,
                       struct gl_shader_program *prog)
{
   blob_write_bytes(blob, prog->data->sha1, sizeof(prog->data->sha1));

   write_uniforms(blob, prog);

   write_hash_tables(blob, prog);

   blob_write_uint32(blob, prog->data->Version);
   blob_write_uint32(blob, prog->data->linked_stages);

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      struct gl_linked_shader *sh = prog->_LinkedShaders[i];
      if (sh) {
         write_shader_metadata(blob, sh);

         if (sh->Program->info.name)
            blob_write_string(blob, sh->Program->info.name);
         else
            blob_write_string(blob, "");

         if (sh->Program->info.label)
            blob_write_string(blob, sh->Program->info.label);
         else
            blob_write_string(blob, "");

         size_t s_info_size, s_info_ptrs;
         get_shader_info_and_pointer_sizes(&s_info_size, &s_info_ptrs,
                                           &sh->Program->info);

         /* Store shader info */
         blob_write_bytes(blob,
                          ((char *) &sh->Program->info) + s_info_ptrs,
                          s_info_size - s_info_ptrs);
      }
   }

   write_xfb(blob, prog);

   write_uniform_remap_tables(blob, prog);

   write_atomic_buffers(blob, prog);

   write_buffer_blocks(blob, prog);

   write_subroutines(blob, prog);

   write_program_resource_list(blob, prog);
}

extern "C" bool
deserialize_glsl_program(struct blob_reader *blob, struct gl_context *ctx,
                         struct gl_shader_program *prog)
{
   /* Fixed function programs generated by Mesa can't be serialized. */
   if (prog->Name == 0)
      return false;

   assert(prog->data->UniformStorage == NULL);

   blob_copy_bytes(blob, prog->data->sha1, sizeof(prog->data->sha1));

   read_uniforms(blob, prog);

   read_hash_tables(blob, prog);

   prog->data->Version = blob_read_uint32(blob);
   prog->data->linked_stages = blob_read_uint32(blob);

   unsigned mask = prog->data->linked_stages;
   while (mask) {
      const int j = u_bit_scan(&mask);
      create_linked_shader_and_program(ctx, (gl_shader_stage) j, prog,
                                       blob);
   }

   read_xfb(blob, prog);

   read_uniform_remap_tables(blob, prog);

   read_atomic_buffers(blob, prog);

   read_buffer_blocks(blob, prog);

   read_subroutines(blob, prog);

   read_program_resource_list(blob, prog);

   return !blob->overrun;
}
