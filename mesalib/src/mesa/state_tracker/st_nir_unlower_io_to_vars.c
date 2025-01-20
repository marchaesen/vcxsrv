/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "st_nir.h"
#include "nir_builder.h"

struct io_desc {
   bool is_per_vertex;
   bool is_output;
   bool is_store;
   bool is_indirect;
   bool is_compact;
   bool is_xfb;
   unsigned component;
   unsigned num_slots;
   nir_io_semantics sem;
   nir_variable_mode mode;
   nir_src location_src;
   nir_intrinsic_instr *baryc;
};

#define VAR_INDEX_INTERP_AT_PIXEL   1
#define VAR_INTERP_UNDEF            INTERP_MODE_COUNT

static bool var_is_per_vertex(gl_shader_stage stage, nir_variable *var)
{
   return ((stage == MESA_SHADER_TESS_CTRL ||
            stage == MESA_SHADER_GEOMETRY) &&
           var->data.mode & nir_var_shader_in) ||
          (((stage == MESA_SHADER_TESS_CTRL && var->data.mode & nir_var_shader_out) ||
            (stage == MESA_SHADER_TESS_EVAL && var->data.mode & nir_var_shader_in)) &&
           !(var->data.location == VARYING_SLOT_TESS_LEVEL_INNER ||
             var->data.location == VARYING_SLOT_TESS_LEVEL_OUTER ||
             (var->data.location >= VARYING_SLOT_PATCH0 &&
              var->data.location <= VARYING_SLOT_PATCH31)));
}

static const struct glsl_type *
get_var_slot_type(gl_shader_stage stage, nir_variable *var)
{
   if (var_is_per_vertex(stage, var)) {
      assert(glsl_type_is_array(var->type));
      return var->type->fields.array;
   } else {
      return var->type;
   }
}

static unsigned
get_var_num_slots(gl_shader_stage stage, nir_variable *var,
                  bool is_driver_location)
{
   const struct glsl_type *type = get_var_slot_type(stage, var);

   assert(!glsl_type_is_array(type) || type->length > 0);

   if (var->data.compact) {
      assert(glsl_type_is_array(type));
      return DIV_ROUND_UP(type->length, 4);
   } else if (is_driver_location &&
              glsl_type_is_dual_slot(glsl_without_array(var->type))) {
      assert(!glsl_type_is_array(type));
      return 2;
   } else {
      return glsl_type_is_array(type) ? type->length : 1;
   }
}

static bool
is_compact(nir_shader *nir, bool is_output, unsigned location)
{
   return nir->options->compact_arrays &&
          (nir->info.stage != MESA_SHADER_VERTEX || is_output) &&
          (nir->info.stage != MESA_SHADER_FRAGMENT || !is_output) &&
          (location == VARYING_SLOT_CLIP_DIST0 ||
           location == VARYING_SLOT_CLIP_DIST1 ||
           location == VARYING_SLOT_CULL_DIST0 ||
           location == VARYING_SLOT_CULL_DIST1 ||
           location == VARYING_SLOT_TESS_LEVEL_OUTER ||
           location == VARYING_SLOT_TESS_LEVEL_INNER);
}

/* Get information about the intrinsic. */
static bool
parse_intrinsic(nir_shader *nir, nir_intrinsic_instr *intr,
                struct io_desc *desc, nir_variable **var)
{
   memset(desc, 0, sizeof(*desc));

   switch (intr->intrinsic) {
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_interpolated_input:
      break;
   case nir_intrinsic_load_per_vertex_input:
      desc->is_per_vertex = true;
      break;
   case nir_intrinsic_load_output:
      desc->is_output = true;
      break;
   case nir_intrinsic_load_per_vertex_output:
      desc->is_output = true;
      desc->is_per_vertex = true;
      break;
   case nir_intrinsic_store_output:
      desc->is_output = true;
      desc->is_store = true;
      break;
   case nir_intrinsic_store_per_vertex_output:
      desc->is_output = true;
      desc->is_per_vertex = true;
      desc->is_store = true;
      break;
   default:
      return false;
   }

   desc->component = nir_intrinsic_component(intr);
   desc->sem = nir_intrinsic_io_semantics(intr);
   desc->mode = desc->is_output ? nir_var_shader_out : nir_var_shader_in;
   desc->location_src = *nir_get_io_offset_src(intr);
   desc->is_indirect = !nir_src_is_const(desc->location_src);
   desc->is_compact = is_compact(nir, desc->is_output, desc->sem.location);
   desc->is_xfb = nir_instr_xfb_write_mask(intr) != 0;
   desc->num_slots = desc->is_compact ? DIV_ROUND_UP(desc->sem.num_slots, 4)
                                      : desc->sem.num_slots;

   /* Variables can't represent high 16 bits. */
   assert(!desc->sem.high_16bits);

   /* Validate assumptions about indirect. */
   if (desc->is_indirect) {
      assert(desc->sem.num_slots > 1);
   } else if (desc->is_compact) {
      assert(desc->sem.num_slots <= 8);
      assert(nir_src_as_uint(desc->location_src) <= 1);
   } else {
      assert(desc->sem.num_slots == 1);
      assert(nir_src_as_uint(desc->location_src) == 0);
   }

   if (intr->intrinsic == nir_intrinsic_load_interpolated_input &&
       intr->src[0].ssa->parent_instr->type == nir_instr_type_intrinsic)
      desc->baryc = nir_instr_as_intrinsic(intr->src[0].ssa->parent_instr);

   /* Find the variable if it exists. */
   *var = NULL;

   nir_foreach_variable_with_modes(iter, nir, desc->mode) {
      unsigned end_location = iter->data.location +
                              get_var_num_slots(nir->info.stage, iter, false);
      assert(iter->data.location < end_location);

      /* Test if the variables intersect. */
      if (MAX2(desc->sem.location, iter->data.location) <
          MIN2(desc->sem.location + desc->num_slots, end_location) &&
          desc->sem.dual_source_blend_index == iter->data.index) {
         *var = iter;
         break;
      }
   }

   return true;
}

/* Gather which components are used, so that we know how many vector elements
 * the variables should have.
 */
static bool
gather_component_masks(nir_builder *b, nir_intrinsic_instr *intr, void *opaque)
{
   uint8_t *component_masks = (uint8_t *)opaque;
   nir_shader *nir = b->shader;
   struct io_desc desc;
   nir_variable *var;

   if (!parse_intrinsic(nir, intr, &desc, &var))
      return false;

   assert(NUM_TOTAL_VARYING_SLOTS <= 127);
   uint8_t mask, index;

   mask = (desc.is_store ? nir_intrinsic_write_mask(intr) :
                           nir_def_components_read(&intr->def)) <<
          nir_intrinsic_component(intr);

   index = desc.sem.location + (desc.is_output ? NUM_TOTAL_VARYING_SLOTS : 0);
   component_masks[index] |= mask;

   /* Ensure front and back colors have the same component masks */
   int8_t alternate_location = -1;
   switch (desc.sem.location) {
   case VARYING_SLOT_COL0: alternate_location = VARYING_SLOT_BFC0; break;
   case VARYING_SLOT_COL1: alternate_location = VARYING_SLOT_BFC1; break;
   case VARYING_SLOT_BFC0: alternate_location = VARYING_SLOT_COL0; break;
   case VARYING_SLOT_BFC1: alternate_location = VARYING_SLOT_COL1; break;
   default: break;
   }
   if (alternate_location >= 0) {
      uint8_t index2 = alternate_location + (desc.is_output ? NUM_TOTAL_VARYING_SLOTS : 0);
      component_masks[index2] |= mask;
   }

   return true;
}

/* Variables are created in a separate pass because a single instruction might
 * not describe them completely, so we might have to redefine variables as we
 * parse more instructions.
 *
 * For example, if there is indirect indexing after direct indexing, variables
 * are created as single-slot for the direct indexing first, and then they must
 * be recreated/expanded when indirect indexing is found.
 *
 * Similarly, a normal load might imply that it's vec2 or dvec2, but the next
 * load with high_dvec2=1 implies that it's dvec4.
 *
 * Similarly, both center and centroid interpolation can occur, which means
 * the declaration should declare center and use load_deref, while the centroid
 * load should be interp_deref_at_centroid.
 */
static bool
create_vars(nir_builder *b, nir_intrinsic_instr *intr, void *opaque)
{
   uint8_t *component_masks = (uint8_t *)opaque;
   nir_shader *nir = b->shader;
   struct io_desc desc;
   nir_variable *var;

   if (!parse_intrinsic(nir, intr, &desc, &var))
      return false;

   if (var && desc.is_indirect && !desc.is_compact) {
      const struct glsl_type *type = get_var_slot_type(nir->info.stage, var);

      /* If the variable exists, but it's declared as a non-array because it had
       * direct access first, ignore it. We'll recreate it as an array.
       *
       * If there are 2 arrays in different components (e.g. one in X and
       * another in Y) and they occupy the same vec4, they might not start
       * on the same location, but we merge them into a single variable.
       */
      if (!glsl_type_is_array(type) ||
          desc.sem.location != var->data.location ||
          desc.num_slots != get_var_num_slots(nir->info.stage, var, false))
         var = NULL;
   }

   if (!var) {
      nir_alu_type type = desc.is_store ? nir_intrinsic_src_type(intr) :
                                          nir_intrinsic_dest_type(intr);
      enum glsl_base_type base_type;
      unsigned num_components = 0;
      const struct glsl_type *var_type = NULL;

      /* Bool outputs are represented as uint. */
      if (type == nir_type_bool32)
         type = nir_type_uint32;

      base_type = nir_get_glsl_base_type_for_nir_type(type);

      if (nir->info.stage == MESA_SHADER_FRAGMENT && desc.is_output) {
         /* FS outputs. */
         switch (desc.sem.location) {
         case FRAG_RESULT_DEPTH:
         case FRAG_RESULT_STENCIL:
         case FRAG_RESULT_SAMPLE_MASK:
            num_components = 1;
            break;
         }
      } else if (nir->info.stage == MESA_SHADER_VERTEX && !desc.is_output) {
         /* VS inputs. */
         /* freedreno/a530-traces requires this. */
         num_components = 4;
      } else {
         /* Varyings. */
         if (desc.is_compact) {
            unsigned component, decl_size;

            switch (desc.sem.location) {
            case VARYING_SLOT_TESS_LEVEL_OUTER:
               var_type = glsl_array_type(glsl_float_type(), 4, sizeof(float));
               break;
            case VARYING_SLOT_TESS_LEVEL_INNER:
               var_type = glsl_array_type(glsl_float_type(), 2, sizeof(float));
               break;
            case VARYING_SLOT_CLIP_DIST0:
            case VARYING_SLOT_CLIP_DIST1:
            case VARYING_SLOT_CULL_DIST0:
            case VARYING_SLOT_CULL_DIST1:
               if (nir->options->io_options &
                   nir_io_separate_clip_cull_distance_arrays) {
                  decl_size = desc.sem.location >= VARYING_SLOT_CULL_DIST0 ?
                                 nir->info.cull_distance_array_size :
                                 nir->info.clip_distance_array_size;
               } else {
                  decl_size = nir->info.clip_distance_array_size +
                              nir->info.cull_distance_array_size;
               }
               component = (desc.sem.location == VARYING_SLOT_CLIP_DIST1 ||
                            desc.sem.location == VARYING_SLOT_CULL_DIST1) * 4 +
                           desc.component;
               assert(component < decl_size);
               var_type = glsl_array_type(glsl_float_type(), decl_size,
                                          sizeof(float));
               break;
            default:
               unreachable("unexpected varying slot");
            }
         } else {
            switch (desc.sem.location) {
            case VARYING_SLOT_POS:
               /* d3d12 requires this. */
               num_components = 4;
               break;
            case VARYING_SLOT_PSIZ:
            case VARYING_SLOT_FOGC:
            case VARYING_SLOT_PRIMITIVE_ID:
            case VARYING_SLOT_LAYER:
            case VARYING_SLOT_VIEWPORT:
            case VARYING_SLOT_VIEWPORT_MASK:
            case VARYING_SLOT_FACE:
               num_components = 1;
               break;
            case VARYING_SLOT_TESS_LEVEL_INNER:
            case VARYING_SLOT_PNTC:
               num_components = 2;
               break;
            }
         }
      }

      /* Set the vector size based on which components are used. */
      if (!desc.is_compact && !num_components) {
         for (unsigned i = 0; i < desc.sem.num_slots; i++) {
            unsigned index = desc.sem.location + i +
                             (desc.is_output ? NUM_TOTAL_VARYING_SLOTS : 0);
            unsigned n = util_last_bit(component_masks[index]);
            num_components = MAX2(num_components, n);
         }
      }

      if (!var_type) {
         assert(!desc.is_compact);
         var_type = glsl_vector_type(base_type, num_components);

         if (desc.is_indirect)
            var_type = glsl_array_type(var_type, desc.sem.num_slots, 0);
      }

      unsigned num_vertices = 0;

      if (desc.is_per_vertex) {
         if (nir->info.stage == MESA_SHADER_TESS_CTRL)
            num_vertices = desc.is_output ? nir->info.tess.tcs_vertices_out : 32;
         else if (nir->info.stage == MESA_SHADER_TESS_EVAL && !desc.is_output)
            num_vertices = 32;
         else if (nir->info.stage == MESA_SHADER_GEOMETRY && !desc.is_output)
            num_vertices = mesa_vertices_per_prim(nir->info.gs.input_primitive);
         else
            unreachable("unexpected shader stage for per-vertex IO");

         var_type = glsl_array_type(var_type, num_vertices, 0);
      }

      const char *name = intr->name;
      if (!name) {
         if (nir->info.stage == MESA_SHADER_VERTEX && !desc.is_output)
            name = gl_vert_attrib_name(desc.sem.location);
         else if (nir->info.stage == MESA_SHADER_FRAGMENT && desc.is_output)
            name = gl_frag_result_name(desc.sem.location);
         else
            name = gl_varying_slot_name_for_stage(desc.sem.location, nir->info.stage);
      }

      var = nir_variable_create(nir, desc.mode, var_type, name);
      var->data.location = desc.sem.location;
      /* If this is the high half of dvec4, the driver location should point
       * to the low half of dvec4.
       */
      var->data.driver_location = nir_intrinsic_base(intr) -
                                  (desc.sem.high_dvec2 ? 1 : 0);
      var->data.compact = desc.is_compact;
      var->data.precision = desc.sem.medium_precision ? GLSL_PRECISION_MEDIUM
                                                      : GLSL_PRECISION_HIGH;
      var->data.index = desc.sem.dual_source_blend_index;
      var->data.patch =
         !desc.is_per_vertex &&
         ((nir->info.stage == MESA_SHADER_TESS_CTRL && desc.is_output) ||
          (nir->info.stage == MESA_SHADER_TESS_EVAL && !desc.is_output));
      var->data.interpolation = VAR_INTERP_UNDEF;
      var->data.always_active_io = desc.is_xfb;

      /* If the variable is an array accessed indirectly, remove any variables
       * we may have created up to this point that overlap with it.
       */
      if (desc.is_indirect) {
         unsigned var_num_slots = get_var_num_slots(nir->info.stage, var, false);
         unsigned var_end_location = var->data.location + var_num_slots;

         nir_foreach_variable_with_modes_safe(iter, nir, desc.mode) {
            unsigned iter_num_slots =
               get_var_num_slots(nir->info.stage, iter, false);
            unsigned iter_end_location = iter->data.location + iter_num_slots;

            if (iter != var &&
                iter->data.index == var->data.index &&
                /* Test if the variables intersect. */
                MAX2(iter->data.location, var->data.location) <
                MIN2(iter_end_location,
                     var_end_location)) {
               /* Compact variables shouldn't end up here. */
               assert(!desc.is_compact);

               /* If the array variables overlap, but don't start on the same
                * location, we merge them.
                */
               if (iter->data.location < var->data.location ||
                   iter_end_location > var_end_location) {
                  var->data.location = MIN2(var->data.location,
                                            iter->data.location);
                  var->data.driver_location = MIN2(var->data.driver_location,
                                                   iter->data.driver_location);

                  const struct glsl_type *elem_type = var->type;

                  if (var_is_per_vertex(nir->info.stage, var)) {
                     assert(glsl_type_is_array(elem_type));
                     elem_type = elem_type->fields.array;
                  }

                  assert(glsl_type_is_array(elem_type));
                  elem_type = elem_type->fields.array;
                  assert(!glsl_type_is_array(elem_type));

                  unsigned end_location = MAX2(iter_end_location,
                                               var_end_location);
                  unsigned new_num_slots = end_location - var->data.location;

                  var->type = glsl_array_type(elem_type, new_num_slots, 0);

                  if (var_is_per_vertex(nir->info.stage, var)) {
                     assert(num_vertices);
                     var->type = glsl_array_type(var->type, num_vertices, 0);
                  }
               }

               /* Preserve variable fields from individual variables. */
               var->data.invariant |= iter->data.invariant;
               var->data.stream |= iter->data.stream;
               var->data.per_view |= iter->data.per_view;
               var->data.fb_fetch_output |= iter->data.fb_fetch_output;
               var->data.access |= iter->data.access;
               var->data.always_active_io |= iter->data.always_active_io;

               if (var->data.interpolation == VAR_INTERP_UNDEF)
                  var->data.interpolation = iter->data.interpolation;
               else
                  assert(var->data.interpolation == iter->data.interpolation);

               if (desc.baryc) {
                  /* This can only contain VAR_INDEX_INTERP_AT_PIXEL. */
                  var->index = iter->index;
                  var->data.centroid = iter->data.centroid;
                  var->data.sample = iter->data.sample;
               }
               exec_node_remove(&iter->node);
            }
         }
      }
   }

   /* Some semantics are dependent on the instruction or component. */
   var->data.invariant |= desc.sem.invariant;
   var->data.stream |= (desc.sem.gs_streams << (desc.component * 2));
   if (var->data.stream)
      var->data.stream |= NIR_STREAM_PACKED;
   var->data.per_view |= desc.sem.per_view;
   var->data.always_active_io |= desc.is_xfb;

   if (desc.sem.fb_fetch_output) {
      var->data.fb_fetch_output = 1;
      if (desc.sem.fb_fetch_output_coherent)
         var->data.access |= ACCESS_COHERENT;
   }

   if (desc.sem.high_dvec2) {
      assert(!desc.is_store);
      assert(!desc.is_indirect); /* TODO: indirect dvec4 VS inputs unhandled */
      var->type = glsl_dvec4_type();
   }

   if (desc.baryc) {
      if (var->data.interpolation == VAR_INTERP_UNDEF)
         var->data.interpolation = nir_intrinsic_interp_mode(desc.baryc);
      else
         assert(var->data.interpolation == nir_intrinsic_interp_mode(desc.baryc));

      switch (desc.baryc->intrinsic) {
      case nir_intrinsic_load_barycentric_pixel:
         var->index = VAR_INDEX_INTERP_AT_PIXEL;
         break;
      case nir_intrinsic_load_barycentric_at_offset:
      case nir_intrinsic_load_barycentric_at_sample:
         break;
      case nir_intrinsic_load_barycentric_centroid:
         var->data.centroid = true;
         break;
      case nir_intrinsic_load_barycentric_sample:
         assert(var->index != VAR_INDEX_INTERP_AT_PIXEL);
         var->data.sample = true;
         break;
      default:
         unreachable("unexpected barycentric intrinsic");
      }

      if (var->index == VAR_INDEX_INTERP_AT_PIXEL) {
         /* Centroid interpolation will use interp_deref_at_centroid. */
         var->data.centroid = false;
         assert(!var->data.sample);
      }
   } else {
      enum glsl_interp_mode flat_mode =
         nir->info.stage == MESA_SHADER_FRAGMENT && !desc.is_output ?
            INTERP_MODE_FLAT : INTERP_MODE_NONE;

      if (var->data.interpolation == VAR_INTERP_UNDEF)
         var->data.interpolation = flat_mode;
      else
         assert(var->data.interpolation == flat_mode);
   }

   return true;
}

static bool
unlower_io_to_vars(nir_builder *b, nir_intrinsic_instr *intr, void *opaque)
{
   struct io_desc desc;
   nir_variable *var;

   if (!parse_intrinsic(b->shader, intr, &desc, &var))
      return false;

   b->cursor = nir_after_instr(&intr->instr);

   /* Create the deref. */
   assert(var);
   nir_deref_instr *deref = nir_build_deref_var(b, var);

   if (desc.is_per_vertex) {
      deref = nir_build_deref_array(b, deref,
                                    nir_get_io_arrayed_index_src(intr)->ssa);
   }

   /* Compact variables have a dedicated codepath. */
   if (var->data.compact) {
      unsigned mask = desc.is_store ? nir_intrinsic_write_mask(intr) :
                                      BITFIELD_MASK(intr->def.num_components);
      nir_def *chan[4];

      u_foreach_bit(bit, mask) {
         nir_def *loc_index = desc.location_src.ssa;

         /* In store_output, compact tess levels interpret the location src
          * as the indirect component index, while compact clip/cull distances
          * interpret the location src as the vec4 index. Convert it to
          * the component index for store_deref.
          */
         if (desc.sem.location >= VARYING_SLOT_CLIP_DIST0 &&
             desc.sem.location <= VARYING_SLOT_CULL_DIST1)
            loc_index = nir_imul_imm(b, loc_index, 4);

         nir_def *index =
            nir_iadd_imm(b, loc_index,
                         (desc.sem.location - var->data.location) * 4 +
                         desc.component + bit);

         nir_deref_instr *deref_elem = nir_build_deref_array(b, deref, index);
         assert(!glsl_type_is_array(deref_elem->type));

         if (desc.is_store) {
            nir_build_store_deref(b, &deref_elem->def,
                                  nir_channel(b,intr->src[0].ssa, bit),
                                  .write_mask = 0x1,
                                  .access = var->data.access);
         } else {
            assert(bit < ARRAY_SIZE(chan));
            chan[bit] = nir_load_deref_with_access(b, deref_elem,
                                                   var->data.access);
         }
      }

      if (!desc.is_store) {
         nir_def_rewrite_uses(&intr->def,
                              nir_vec(b, chan, intr->def.num_components));
      }

      nir_instr_remove(&intr->instr);
      return true;
   }

   if (get_var_num_slots(b->shader->info.stage, var, false) > 1) {
      nir_def *index = nir_imm_int(b, desc.sem.location - var->data.location);
      if (desc.is_indirect)
         index = nir_iadd(b, index, desc.location_src.ssa);

      deref = nir_build_deref_array(b, deref, index);
   }

   /* We shouldn't need any other array dereferencies. */
   assert(!glsl_type_is_array(deref->type));
   unsigned num_components = deref->type->vector_elements;

   if (desc.is_store) {
      unsigned writemask = nir_intrinsic_write_mask(intr) << desc.component;
      nir_def *value = intr->src[0].ssa;

      if (desc.component) {
         unsigned new_num_components = desc.component + value->num_components;
         unsigned swizzle[4] = {0};
         assert(new_num_components <= 4);

         /* Move components within the vector to the right because we only
          * have vec4 stores. The writemask skips the extra components at
          * the beginning.
          *
          * For component = 1: .xyz -> .xxyz
          * For component = 2: .xy  -> .xxxy
          * For component = 3: .x   -> .xxxx
          */
         for (unsigned i = 1; i < value->num_components; i++)
            swizzle[desc.component + i] = i;

         value = nir_swizzle(b, value, swizzle, new_num_components);
      }

      value = nir_resize_vector(b, value, num_components);

      /* virgl requires scalarized TESS_LEVEL stores because originally
       * the GLSL compiler never vectorized them. Doing 1 store per bit of
       * the writemask is enough to make virgl work.
       */
      if (desc.sem.location == VARYING_SLOT_TESS_LEVEL_OUTER ||
          desc.sem.location == VARYING_SLOT_TESS_LEVEL_INNER) {
         u_foreach_bit(i, writemask) {
            nir_build_store_deref(b, &deref->def, value,
                                  .write_mask = BITFIELD_BIT(i),
                                  .access = var->data.access);
         }
      } else {
         nir_build_store_deref(b, &deref->def, value,
                               .write_mask = writemask,
                               .access = var->data.access);
      }
   } else {
      nir_def *load;

      if (deref->type == glsl_dvec4_type()) {
         /* Load dvec4, but extract low or high half as vec4. */
         load = nir_load_deref_with_access(b, deref, var->data.access);
         load = nir_extract_bits(b, &load, 1, desc.sem.high_dvec2 ? 128 : 0,
                                 4, 32);
      } else {
         nir_intrinsic_op baryc = desc.baryc ? desc.baryc->intrinsic :
                                               nir_num_intrinsics;

         if (baryc == nir_intrinsic_load_barycentric_centroid &&
             var->index == VAR_INDEX_INTERP_AT_PIXEL) {
            /* Both pixel and centroid interpolation occurs, so the latter
             * must use interp_deref_at_centroid.
             */
            load = nir_interp_deref_at_centroid(b, num_components,
                                                intr->def.bit_size,
                                                &deref->def);
         } else if (baryc == nir_intrinsic_load_barycentric_at_offset) {
            load = nir_interp_deref_at_offset(b, num_components,
                                              intr->def.bit_size, &deref->def,
                                              desc.baryc->src[0].ssa);
         } else if (baryc == nir_intrinsic_load_barycentric_at_sample) {
            load = nir_interp_deref_at_sample(b, num_components,
                                              intr->def.bit_size, &deref->def,
                                              desc.baryc->src[0].ssa);
         } else {
            load = nir_load_deref_with_access(b, deref, var->data.access);
         }
      }

      load = nir_pad_vec4(b, load);
      load = nir_channels(b, load, BITFIELD_RANGE(desc.component,
                                                  intr->def.num_components));
      nir_def_rewrite_uses(&intr->def, load);
   }

   nir_instr_remove(&intr->instr);
   return true;
}

bool
st_nir_unlower_io_to_vars(nir_shader *nir)
{
   if (nir->info.stage == MESA_SHADER_COMPUTE)
      return false;

   /* Flexible interpolation is not supported by this pass. If you want to
    * enable flexible interpolation for your driver, it has to stop consuming
    * IO variables.
    */
   assert(!(nir->options->io_options &
            nir_io_has_flexible_input_interpolation_except_flat));
   assert(!(nir->options->io_options &
            nir_io_mix_convergent_flat_with_interpolated));

   nir_foreach_variable_with_modes(var, nir, nir_var_shader_in | nir_var_shader_out) {
      unreachable("the shader should have no IO variables");
   }

   /* Some drivers can't handle holes in driver locations (bases), so
    * recompute them.
    */
   nir_variable_mode modes =
      nir_var_shader_out |
      (nir->info.stage != MESA_SHADER_VERTEX ? nir_var_shader_in : 0);
   bool progress = nir_recompute_io_bases(nir, modes);

   /* Gather component masks. */
   uint8_t component_masks[NUM_TOTAL_VARYING_SLOTS * 2] = {0};
   if (!nir_shader_intrinsics_pass(nir, gather_component_masks,
                                   nir_metadata_all, component_masks)) {
      nir->info.io_lowered = false; /* Nothing to do. */
      return progress;
   }

   /* Create IO variables. */
   if (!nir_shader_intrinsics_pass(nir, create_vars, nir_metadata_all,
                                   component_masks)) {
      nir->info.io_lowered = false; /* Nothing to do. */
      return progress;
   }

   /* Unlower IO using the created variables. */
   ASSERTED bool lower_progress =
      nir_shader_intrinsics_pass(nir, unlower_io_to_vars,
                                 nir_metadata_control_flow, NULL);
   assert(lower_progress);
   nir->info.io_lowered = false;

   /* Count IO variables. */
   nir->num_inputs = 0;
   nir_foreach_variable_with_modes(var, nir, nir_var_shader_in) {
      nir->num_inputs += get_var_num_slots(nir->info.stage, var, true);
   }

   nir->num_outputs = 0;
   nir_foreach_variable_with_modes(var, nir, nir_var_shader_out) {
      nir->num_outputs += get_var_num_slots(nir->info.stage, var, true);
   }

   /* llvmpipe and other drivers require that variables are sorted by location,
    * otherwise a lot of tests fails.
    *
    * It looks like location and driver_location are not the only values that
    * determine behavior. The order in which the variables are declared also
    * affect behavior.
    */
   unsigned varying_var_mask =
      nir_var_shader_in |
      (nir->info.stage != MESA_SHADER_FRAGMENT ? nir_var_shader_out : 0);
   nir_sort_variables_by_location(nir, varying_var_mask);

   /* Fix locations and info for dual-slot VS inputs. Intel needs this.
    * All other drivers only use driver_location.
    */
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      unsigned num_dual_slots = 0;
      nir->num_inputs = 0;
      nir->info.inputs_read = 0;

      nir_foreach_variable_with_modes(var, nir, nir_var_shader_in) {
         var->data.location += num_dual_slots;
         nir->info.inputs_read |= BITFIELD64_BIT(var->data.location);
         nir->num_inputs++;

         if (glsl_type_is_dual_slot(glsl_without_array(var->type))) {
            num_dual_slots++;
            nir->info.inputs_read |= BITFIELD64_BIT(var->data.location + 1);
            nir->num_inputs++;
         }
      }
   }

   return true;
}
