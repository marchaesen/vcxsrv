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
 */

#include "nir.h"
#include "gl_nir_linker.h"
#include "compiler/glsl/ir_uniform.h" /* for gl_uniform_storage */
#include "linker_util.h"
#include "main/context.h"
#include "main/mtypes.h"

/* This file do the common link for GLSL uniforms, using NIR, instead of IR as
 * the counter-part glsl/link_uniforms.cpp
 *
 * Also note that this is tailored for ARB_gl_spirv needs and particularities
 * (like need to work/link without name available, explicit location for
 * normal uniforms as mandatory, and so on).
 */

#define UNMAPPED_UNIFORM_LOC ~0u

static void
nir_setup_uniform_remap_tables(struct gl_context *ctx,
                               struct gl_shader_program *prog)
{
   prog->UniformRemapTable = rzalloc_array(prog,
                                           struct gl_uniform_storage *,
                                           prog->NumUniformRemapTable);
   union gl_constant_value *data =
      rzalloc_array(prog->data,
                    union gl_constant_value, prog->data->NumUniformDataSlots);
   if (!prog->UniformRemapTable || !data) {
      linker_error(prog, "Out of memory during linking.\n");
      return;
   }
   prog->data->UniformDataSlots = data;

   prog->data->UniformDataDefaults =
         rzalloc_array(prog->data->UniformStorage,
                       union gl_constant_value, prog->data->NumUniformDataSlots);

   unsigned data_pos = 0;

   /* Reserve all the explicit locations of the active uniforms. */
   for (unsigned i = 0; i < prog->data->NumUniformStorage; i++) {
      struct gl_uniform_storage *uniform = &prog->data->UniformStorage[i];

      if (prog->data->UniformStorage[i].remap_location == UNMAPPED_UNIFORM_LOC)
         continue;

      /* How many new entries for this uniform? */
      const unsigned entries = MAX2(1, uniform->array_elements);
      unsigned num_slots = glsl_get_component_slots(uniform->type);

      uniform->storage = &data[data_pos];

      /* Set remap table entries point to correct gl_uniform_storage. */
      for (unsigned j = 0; j < entries; j++) {
         unsigned element_loc = uniform->remap_location + j;
         prog->UniformRemapTable[element_loc] = uniform;

         data_pos += num_slots;
      }
   }

   /* Reserve locations for rest of the uniforms. */
   link_util_update_empty_uniform_locations(prog);

   for (unsigned i = 0; i < prog->data->NumUniformStorage; i++) {
      struct gl_uniform_storage *uniform = &prog->data->UniformStorage[i];

      if (uniform->is_shader_storage)
         continue;

      /* Built-in uniforms should not get any location. */
      if (uniform->builtin)
         continue;

      /* Explicit ones have been set already. */
      if (uniform->remap_location != UNMAPPED_UNIFORM_LOC)
         continue;

      /* How many entries for this uniform? */
      const unsigned entries = MAX2(1, uniform->array_elements);

      unsigned location =
         link_util_find_empty_block(prog, &prog->data->UniformStorage[i]);

      if (location == -1) {
         location = prog->NumUniformRemapTable;

         /* resize remap table to fit new entries */
         prog->UniformRemapTable =
            reralloc(prog,
                     prog->UniformRemapTable,
                     struct gl_uniform_storage *,
                     prog->NumUniformRemapTable + entries);
         prog->NumUniformRemapTable += entries;
      }

      /* set the base location in remap table for the uniform */
      uniform->remap_location = location;

      unsigned num_slots = glsl_get_component_slots(uniform->type);

      uniform->storage = &data[data_pos];

      /* Set remap table entries point to correct gl_uniform_storage. */
      for (unsigned j = 0; j < entries; j++) {
         unsigned element_loc = uniform->remap_location + j;
         prog->UniformRemapTable[element_loc] = uniform;

         data_pos += num_slots;
      }
   }
}

static void
mark_stage_as_active(struct gl_uniform_storage *uniform,
                     unsigned stage)
{
   uniform->active_shader_mask |= 1 << stage;
}

/**
 * Finds, returns, and updates the stage info for any uniform in UniformStorage
 * defined by @var. In general this is done using the explicit location,
 * except:
 *
 * * UBOs/SSBOs: as they lack explicit location, binding is used to locate
 *   them. That means that more that one entry at the uniform storage can be
 *   found. In that case all of them are updated, and the first entry is
 *   returned, in order to update the location of the nir variable.
 *
 * * Special uniforms: like atomic counters. They lack a explicit location,
 *   so they are skipped. They will be handled and assigned a location later.
 *
 */
static struct gl_uniform_storage *
find_and_update_previous_uniform_storage(struct gl_shader_program *prog,
                                         nir_variable *var,
                                         unsigned stage)
{
   if (nir_variable_is_in_block(var)) {
      struct gl_uniform_storage *uniform = NULL;

      unsigned num_blks = nir_variable_is_in_ubo(var) ?
         prog->data->NumUniformBlocks :
         prog->data->NumShaderStorageBlocks;

      struct gl_uniform_block *blks = nir_variable_is_in_ubo(var) ?
         prog->data->UniformBlocks : prog->data->ShaderStorageBlocks;

      for (unsigned i = 0; i < prog->data->NumUniformStorage; i++) {
         /* UniformStorage contains both variables from ubos and ssbos */
         if ( prog->data->UniformStorage[i].is_shader_storage !=
              nir_variable_is_in_ssbo(var))
            continue;

         int block_index = prog->data->UniformStorage[i].block_index;
         if (block_index != -1) {
            assert(block_index < num_blks);

            if (var->data.binding == blks[block_index].Binding) {
               if (!uniform)
                  uniform = &prog->data->UniformStorage[i];
               mark_stage_as_active(&prog->data->UniformStorage[i],
                                      stage);
            }
         }
      }

      return uniform;
   }

   /* Beyond blocks, there are still some corner cases of uniforms without
    * location (ie: atomic counters) that would have a initial location equal
    * to -1. We just return on that case. Those uniforms will be handled
    * later.
    */
   if (var->data.location == -1)
      return NULL;

   /* TODO: following search can be problematic with shaders with a lot of
    * uniforms. Would it be better to use some type of hash
    */
   for (unsigned i = 0; i < prog->data->NumUniformStorage; i++) {
      if (prog->data->UniformStorage[i].remap_location == var->data.location) {
         mark_stage_as_active(&prog->data->UniformStorage[i], stage);

         return &prog->data->UniformStorage[i];
      }
   }

   return NULL;
}

/* Used to build a tree representing the glsl_type so that we can have a place
 * to store the next index for opaque types. Array types are expanded so that
 * they have a single child which is used for all elements of the array.
 * Struct types have a child for each member. The tree is walked while
 * processing a uniform so that we can recognise when an opaque type is
 * encountered a second time in order to reuse the same range of indices that
 * was reserved the first time. That way the sampler indices can be arranged
 * so that members of an array are placed sequentially even if the array is an
 * array of structs containing other opaque members.
 */
struct type_tree_entry {
   /* For opaque types, this will be the next index to use. If we haven’t
    * encountered this member yet, it will be UINT_MAX.
    */
   unsigned next_index;
   unsigned array_size;
   struct type_tree_entry *parent;
   struct type_tree_entry *next_sibling;
   struct type_tree_entry *children;
};

struct nir_link_uniforms_state {
   /* per-whole program */
   unsigned num_hidden_uniforms;
   unsigned num_values;
   unsigned max_uniform_location;
   unsigned next_sampler_index;
   unsigned next_image_index;

   /* per-shader stage */
   unsigned num_shader_samplers;
   unsigned num_shader_images;
   unsigned num_shader_uniform_components;
   unsigned shader_samplers_used;
   unsigned shader_shadow_samplers;
   struct gl_program_parameter_list *params;

   /* per-variable */
   nir_variable *current_var;
   int offset;
   bool var_is_in_block;
   int top_level_array_size;
   int top_level_array_stride;
   int main_uniform_storage_index;

   struct type_tree_entry *current_type;
};

static struct type_tree_entry *
build_type_tree_for_type(const struct glsl_type *type)
{
   struct type_tree_entry *entry = malloc(sizeof *entry);

   entry->array_size = 1;
   entry->next_index = UINT_MAX;
   entry->children = NULL;
   entry->next_sibling = NULL;
   entry->parent = NULL;

   if (glsl_type_is_array(type)) {
      entry->array_size = glsl_get_length(type);
      entry->children = build_type_tree_for_type(glsl_get_array_element(type));
      entry->children->parent = entry;
   } else if (glsl_type_is_struct_or_ifc(type)) {
      struct type_tree_entry *last = NULL;

      for (unsigned i = 0; i < glsl_get_length(type); i++) {
         const struct glsl_type *field_type = glsl_get_struct_field(type, i);
         struct type_tree_entry *field_entry =
            build_type_tree_for_type(field_type);

         if (last == NULL)
            entry->children = field_entry;
         else
            last->next_sibling = field_entry;

         field_entry->parent = entry;

         last = field_entry;
      }
   }

   return entry;
}

static void
free_type_tree(struct type_tree_entry *entry)
{
   struct type_tree_entry *p, *next;

   for (p = entry->children; p; p = next) {
      next = p->next_sibling;
      free_type_tree(p);
   }

   free(entry);
}

static unsigned
get_next_index(struct nir_link_uniforms_state *state,
               const struct gl_uniform_storage *uniform,
               unsigned *next_index)
{
   /* If we’ve already calculated an index for this member then we can just
    * offset from there.
    */
   if (state->current_type->next_index == UINT_MAX) {
      /* Otherwise we need to reserve enough indices for all of the arrays
       * enclosing this member.
       */

      unsigned array_size = 1;

      for (const struct type_tree_entry *p = state->current_type;
           p;
           p = p->parent) {
         array_size *= p->array_size;
      }

      state->current_type->next_index = *next_index;
      *next_index += array_size;
   }

   unsigned index = state->current_type->next_index;

   state->current_type->next_index += MAX2(1, uniform->array_elements);

   return index;
}

static void
add_parameter(struct gl_uniform_storage *uniform,
              struct gl_context *ctx,
              struct gl_shader_program *prog,
              const struct glsl_type *type,
              struct nir_link_uniforms_state *state)
{
   if (!state->params || uniform->is_shader_storage || glsl_contains_opaque(type))
      return;

   unsigned num_params = glsl_get_aoa_size(type);
   num_params = MAX2(num_params, 1);
   num_params *= glsl_get_matrix_columns(glsl_without_array(type));

   bool is_dual_slot = glsl_type_is_dual_slot(glsl_without_array(type));
   if (is_dual_slot)
      num_params *= 2;

   struct gl_program_parameter_list *params = state->params;
   int base_index = params->NumParameters;
   _mesa_reserve_parameter_storage(params, num_params);

   if (ctx->Const.PackedDriverUniformStorage) {
      for (unsigned i = 0; i < num_params; i++) {
         unsigned dmul = glsl_type_is_64bit(glsl_without_array(type)) ? 2 : 1;
         unsigned comps = glsl_get_vector_elements(glsl_without_array(type)) * dmul;
         if (is_dual_slot) {
            if (i & 0x1)
               comps -= 4;
            else
               comps = 4;
         }

         _mesa_add_parameter(params, PROGRAM_UNIFORM, NULL, comps,
                             glsl_get_gl_type(type), NULL, NULL, false);
      }
   } else {
      for (unsigned i = 0; i < num_params; i++) {
         _mesa_add_parameter(params, PROGRAM_UNIFORM, NULL, 4,
                             glsl_get_gl_type(type), NULL, NULL, true);
      }
   }

   /* Each Parameter will hold the index to the backing uniform storage.
    * This avoids relying on names to match parameters and uniform
    * storages.
    */
   for (unsigned i = 0; i < num_params; i++) {
      struct gl_program_parameter *param = &params->Parameters[base_index + i];
      param->UniformStorageIndex = uniform - prog->data->UniformStorage;
      param->MainUniformStorageIndex = state->main_uniform_storage_index;
   }
}

/**
 * Creates the neccessary entries in UniformStorage for the uniform. Returns
 * the number of locations used or -1 on failure.
 */
static int
nir_link_uniform(struct gl_context *ctx,
                 struct gl_shader_program *prog,
                 struct gl_program *stage_program,
                 gl_shader_stage stage,
                 const struct glsl_type *type,
                 const struct glsl_type *parent_type,
                 unsigned index_in_parent,
                 int location,
                 struct nir_link_uniforms_state *state)
{
   struct gl_uniform_storage *uniform = NULL;

   if (parent_type == state->current_var->type &&
       nir_variable_is_in_ssbo(state->current_var)) {
      /* Type is the top level SSBO member */
      if (glsl_type_is_array(type) &&
          (glsl_type_is_array(glsl_get_array_element(type)) ||
           glsl_type_is_struct_or_ifc(glsl_get_array_element(type)))) {
         /* Type is a top-level array (array of aggregate types) */
         state->top_level_array_size = glsl_get_length(type);
         state->top_level_array_stride = glsl_get_explicit_stride(type);
      } else {
         state->top_level_array_size = 1;
         state->top_level_array_stride = 0;
      }
   }

   /* gl_uniform_storage can cope with one level of array, so if the type is a
    * composite type or an array where each element occupies more than one
    * location than we need to recursively process it.
    */
   if (glsl_type_is_struct_or_ifc(type) ||
       (glsl_type_is_array(type) &&
        (glsl_type_is_array(glsl_get_array_element(type)) ||
         glsl_type_is_struct_or_ifc(glsl_get_array_element(type))))) {
      int location_count = 0;
      struct type_tree_entry *old_type = state->current_type;
      unsigned int struct_base_offset = state->offset;

      state->current_type = old_type->children;

      for (unsigned i = 0; i < glsl_get_length(type); i++) {
         const struct glsl_type *field_type;

         if (glsl_type_is_struct_or_ifc(type)) {
            field_type = glsl_get_struct_field(type, i);
            /* Use the offset inside the struct only for variables backed by
             * a buffer object. For variables not backed by a buffer object,
             * offset is -1.
             */
            if (state->var_is_in_block) {
               state->offset =
                  struct_base_offset + glsl_get_struct_field_offset(type, i);
            }
         } else {
            field_type = glsl_get_array_element(type);
         }

         int entries = nir_link_uniform(ctx, prog, stage_program, stage,
                                        field_type, type, i, location,
                                        state);
         if (entries == -1)
            return -1;

         if (location != -1)
            location += entries;
         location_count += entries;

         if (glsl_type_is_struct_or_ifc(type))
            state->current_type = state->current_type->next_sibling;
      }

      state->current_type = old_type;

      return location_count;
   } else {
      /* Create a new uniform storage entry */
      prog->data->UniformStorage =
         reralloc(prog->data,
                  prog->data->UniformStorage,
                  struct gl_uniform_storage,
                  prog->data->NumUniformStorage + 1);
      if (!prog->data->UniformStorage) {
         linker_error(prog, "Out of memory during linking.\n");
         return -1;
      }

      if (state->main_uniform_storage_index == -1)
         state->main_uniform_storage_index = prog->data->NumUniformStorage;

      uniform = &prog->data->UniformStorage[prog->data->NumUniformStorage];
      prog->data->NumUniformStorage++;

      /* Initialize its members */
      memset(uniform, 0x00, sizeof(struct gl_uniform_storage));
      /* ARB_gl_spirv: names are considered optional debug info, so the linker
       * needs to work without them, and returning them is optional. For
       * simplicity we ignore names.
       */
      uniform->name = NULL;

      const struct glsl_type *type_no_array = glsl_without_array(type);
      if (glsl_type_is_array(type)) {
         uniform->type = type_no_array;
         uniform->array_elements = glsl_get_length(type);
      } else {
         uniform->type = type;
         uniform->array_elements = 0;
      }
      uniform->top_level_array_size = state->top_level_array_size;
      uniform->top_level_array_stride = state->top_level_array_stride;

      uniform->active_shader_mask |= 1 << stage;

      if (location >= 0) {
         /* Uniform has an explicit location */
         uniform->remap_location = location;
      } else {
         uniform->remap_location = UNMAPPED_UNIFORM_LOC;
      }

      uniform->hidden = state->current_var->data.how_declared == nir_var_hidden;
      if (uniform->hidden)
         state->num_hidden_uniforms++;

      uniform->is_shader_storage = nir_variable_is_in_ssbo(state->current_var);

      /* Set fields whose default value depend on the variable being inside a
       * block.
       *
       * From the OpenGL 4.6 spec, 7.3 Program objects:
       *
       * "For the property ARRAY_STRIDE, ... For active variables not declared
       * as an array of basic types, zero is written to params. For active
       * variables not backed by a buffer object, -1 is written to params,
       * regardless of the variable type."
       *
       * "For the property MATRIX_STRIDE, ... For active variables not declared
       * as a matrix or array of matrices, zero is written to params. For active
       * variables not backed by a buffer object, -1 is written to params,
       * regardless of the variable type."
       *
       * For the property IS_ROW_MAJOR, ... For active variables backed by a
       * buffer object, declared as a single matrix or array of matrices, and
       * stored in row-major order, one is written to params. For all other
       * active variables, zero is written to params.
       */
      uniform->array_stride = -1;
      uniform->matrix_stride = -1;
      uniform->row_major = false;

      if (state->var_is_in_block) {
         uniform->array_stride = glsl_type_is_array(type) ?
            glsl_get_explicit_stride(type) : 0;

         if (glsl_type_is_matrix(type)) {
            assert(parent_type);
            uniform->matrix_stride = glsl_get_explicit_stride(type);

            uniform->row_major = glsl_matrix_type_is_row_major(type);
         } else {
            uniform->matrix_stride = 0;
         }
      }

      uniform->offset = state->var_is_in_block ? state->offset : -1;

      int buffer_block_index = -1;
      /* If the uniform is inside a uniform block determine its block index by
       * comparing the bindings, we can not use names.
       */
      if (state->var_is_in_block) {
         struct gl_uniform_block *blocks = nir_variable_is_in_ssbo(state->current_var) ?
            prog->data->ShaderStorageBlocks : prog->data->UniformBlocks;

         int num_blocks = nir_variable_is_in_ssbo(state->current_var) ?
            prog->data->NumShaderStorageBlocks : prog->data->NumUniformBlocks;

         for (unsigned i = 0; i < num_blocks; i++) {
            if (state->current_var->data.binding == blocks[i].Binding) {
               buffer_block_index = i;
               break;
            }
         }
         assert(buffer_block_index >= 0);

         /* Compute the next offset. */
         state->offset += glsl_get_explicit_size(type, true);
      }

      uniform->block_index = buffer_block_index;

      /* @FIXME: the initialization of the following will be done as we
       * implement support for their specific features, like SSBO, atomics,
       * etc.
       */
      uniform->builtin = false;
      uniform->atomic_buffer_index = -1;
      uniform->is_bindless = false;

      /* The following are not for features not supported by ARB_gl_spirv */
      uniform->num_compatible_subroutines = 0;

      unsigned entries = MAX2(1, uniform->array_elements);

      if (glsl_type_is_sampler(type_no_array)) {
         int sampler_index =
            get_next_index(state, uniform, &state->next_sampler_index);

         state->num_shader_samplers++;

         uniform->opaque[stage].active = true;
         uniform->opaque[stage].index = sampler_index;

         const unsigned shadow = glsl_sampler_type_is_shadow(type_no_array);

         for (unsigned i = sampler_index;
              i < MIN2(state->next_sampler_index, MAX_SAMPLERS);
              i++) {
            stage_program->sh.SamplerTargets[i] =
               glsl_get_sampler_target(type_no_array);
            state->shader_samplers_used |= 1U << i;
            state->shader_shadow_samplers |= shadow << i;
         }
      } else if (glsl_type_is_image(type_no_array)) {
         /* @FIXME: image_index should match that of the same image
          * uniform in other shaders. This means we need to match image
          * uniforms by location (GLSL does it by variable name, but we
          * want to avoid that).
          */
         int image_index = state->next_image_index;
         state->next_image_index += entries;

         state->num_shader_images++;

         uniform->opaque[stage].active = true;
         uniform->opaque[stage].index = image_index;

         /* Set image access qualifiers */
         enum gl_access_qualifier image_access =
            state->current_var->data.image.access;
         const GLenum access =
            (image_access & ACCESS_NON_WRITEABLE) ?
            ((image_access & ACCESS_NON_READABLE) ? GL_NONE :
                                                    GL_READ_ONLY) :
            ((image_access & ACCESS_NON_READABLE) ? GL_WRITE_ONLY :
                                                    GL_READ_WRITE);
         for (unsigned i = image_index;
              i < MIN2(state->next_image_index, MAX_IMAGE_UNIFORMS);
              i++) {
            stage_program->sh.ImageAccess[i] = access;
         }
      }

      unsigned values = glsl_get_component_slots(type);
      state->num_shader_uniform_components += values;
      state->num_values += values;

      if (uniform->remap_location != UNMAPPED_UNIFORM_LOC &&
          state->max_uniform_location < uniform->remap_location + entries)
         state->max_uniform_location = uniform->remap_location + entries;

      if (!state->var_is_in_block)
         add_parameter(uniform, ctx, prog, type, state);

      return MAX2(uniform->array_elements, 1);
   }
}

bool
gl_nir_link_uniforms(struct gl_context *ctx,
                     struct gl_shader_program *prog,
                     bool fill_parameters)
{
   /* First free up any previous UniformStorage items */
   ralloc_free(prog->data->UniformStorage);
   prog->data->UniformStorage = NULL;
   prog->data->NumUniformStorage = 0;

   /* Iterate through all linked shaders */
   struct nir_link_uniforms_state state = {0,};

   for (unsigned shader_type = 0; shader_type < MESA_SHADER_STAGES; shader_type++) {
      struct gl_linked_shader *sh = prog->_LinkedShaders[shader_type];
      if (!sh)
         continue;

      nir_shader *nir = sh->Program->nir;
      assert(nir);

      state.num_shader_samplers = 0;
      state.num_shader_images = 0;
      state.num_shader_uniform_components = 0;
      state.shader_samplers_used = 0;
      state.shader_shadow_samplers = 0;
      state.params = fill_parameters ? sh->Program->Parameters : NULL;

      nir_foreach_variable(var, &nir->uniforms) {
         struct gl_uniform_storage *uniform = NULL;

         /* Check if the uniform has been processed already for
          * other stage. If so, validate they are compatible and update
          * the active stage mask.
          */
         uniform = find_and_update_previous_uniform_storage(prog, var, shader_type);
         if (uniform) {
            var->data.location = uniform - prog->data->UniformStorage;

            if (!state.var_is_in_block)
               add_parameter(uniform, ctx, prog, var->type, &state);

            continue;
         }

         int location = var->data.location;
         /* From now on the variable’s location will be its uniform index */
         var->data.location = prog->data->NumUniformStorage;

         state.current_var = var;
         state.offset = 0;
         state.var_is_in_block = nir_variable_is_in_block(var);
         state.top_level_array_size = 0;
         state.top_level_array_stride = 0;
         state.main_uniform_storage_index = -1;

         /*
          * From ARB_program_interface spec, issue (16):
          *
          * "RESOLVED: We will follow the default rule for enumerating block
          *  members in the OpenGL API, which is:
          *
          *  * If a variable is a member of an interface block without an
          *    instance name, it is enumerated using just the variable name.
          *
          *  * If a variable is a member of an interface block with an
          *    instance name, it is enumerated as "BlockName.Member", where
          *    "BlockName" is the name of the interface block (not the
          *    instance name) and "Member" is the name of the variable.
          *
          * For example, in the following code:
          *
          * uniform Block1 {
          *   int member1;
          * };
          * uniform Block2 {
          *   int member2;
          * } instance2;
          * uniform Block3 {
          *  int member3;
          * } instance3[2];  // uses two separate buffer bindings
          *
          * the three uniforms (if active) are enumerated as "member1",
          * "Block2.member2", and "Block3.member3"."
          *
          * Note that in the last example, with an array of ubo, only one
          * uniform is generated. For that reason, while unrolling the
          * uniforms of a ubo, or the variables of a ssbo, we need to treat
          * arrays of instance as a single block.
          */
         const struct glsl_type *type = var->type;
         if (state.var_is_in_block && glsl_type_is_array(type)) {
            type = glsl_without_array(type);
         }

         struct type_tree_entry *type_tree =
            build_type_tree_for_type(type);
         state.current_type = type_tree;

         int res = nir_link_uniform(ctx, prog, sh->Program, shader_type, type,
                                    NULL, 0,
                                    location,
                                    &state);

         free_type_tree(type_tree);

         if (res == -1)
            return false;
      }

      sh->Program->SamplersUsed = state.shader_samplers_used;
      sh->shadow_samplers = state.shader_shadow_samplers;
      sh->Program->info.num_textures = state.num_shader_samplers;
      sh->Program->info.num_images = state.num_shader_images;
      sh->num_uniform_components = state.num_shader_uniform_components;
      sh->num_combined_uniform_components = sh->num_uniform_components;
   }

   prog->data->NumHiddenUniforms = state.num_hidden_uniforms;
   prog->NumUniformRemapTable = state.max_uniform_location;
   prog->data->NumUniformDataSlots = state.num_values;

   nir_setup_uniform_remap_tables(ctx, prog);
   gl_nir_set_uniform_initializers(ctx, prog);

   return true;
}
