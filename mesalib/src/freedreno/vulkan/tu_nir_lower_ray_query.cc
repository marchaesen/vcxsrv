/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "tu_shader.h"

#include "bvh/tu_build_interface.h"

#include "compiler/spirv/spirv.h"

#include "nir_builder.h"
#include "nir_deref.h"

enum rq_intersection_var_index {
   rq_intersection_primitive_id,
   rq_intersection_geometry_id,
   rq_intersection_origin,
   rq_intersection_direction,
   rq_intersection_instance,
   rq_intersection_type_flags,
   rq_intersection_sbt_offset,
   rq_intersection_barycentrics,
   rq_intersection_t,
};

static const glsl_type *
get_rq_intersection_type(void)
{
   struct glsl_struct_field fields[] = {
#define FIELD(_type, _name) \
      [rq_intersection_##_name] = glsl_struct_field(_type, #_name),
      FIELD(glsl_uint_type(), primitive_id)
      FIELD(glsl_uint_type(), geometry_id)
      FIELD(glsl_vec_type(3), origin)
      FIELD(glsl_vec_type(3), direction)
      FIELD(glsl_uint_type(), instance)
      FIELD(glsl_uint_type(), type_flags)
      FIELD(glsl_uint_type(), sbt_offset)
      FIELD(glsl_vec2_type(), barycentrics)
      FIELD(glsl_float_type(), t)
#undef FIELD
   };

   return glsl_struct_type(fields, ARRAY_SIZE(fields), "ray_query_intersection", false);
}

enum rq_var_index {
   rq_index_accel_struct_base,
   rq_index_root_bvh_base,
   rq_index_bvh_base,
   rq_index_flags,
   rq_index_tmin,
   rq_index_world_origin,
   rq_index_world_direction,
   rq_index_incomplete,
   rq_index_closest,
   rq_index_candidate,
   rq_index_stack_ptr,
   rq_index_top_stack,
   rq_index_stack_low_watermark,
   rq_index_current_node,
   rq_index_previous_node,
   rq_index_instance_top_node,
   rq_index_instance_bottom_node,
   rq_index_stack,
};

/* Driver-internal flag to indicate that we haven't found an intersection */
#define TU_INTERSECTION_TYPE_NO_INTERSECTION (1u << 0)

#define MAX_STACK_DEPTH 8

static const glsl_type *
get_rq_type(void)
{
   const glsl_type *intersection_type = get_rq_intersection_type();

   struct glsl_struct_field fields[] = {
#define FIELD(_type, _name) \
      [rq_index_##_name] = glsl_struct_field(_type, #_name),
      FIELD(glsl_uvec2_type(), accel_struct_base)
      FIELD(glsl_uvec2_type(), root_bvh_base)
      FIELD(glsl_uvec2_type(), bvh_base)
      FIELD(glsl_uint_type(), flags)
      FIELD(glsl_float_type(), tmin)
      FIELD(glsl_vec_type(3), world_origin)
      FIELD(glsl_vec_type(3), world_direction)
      FIELD(glsl_bool_type(), incomplete)
      FIELD(intersection_type, closest)
      FIELD(intersection_type, candidate)
      FIELD(glsl_uint_type(), stack_ptr)
      FIELD(glsl_uint_type(), top_stack)
      FIELD(glsl_uint_type(), stack_low_watermark)
      FIELD(glsl_uint_type(), current_node)
      FIELD(glsl_uint_type(), previous_node)
      FIELD(glsl_uint_type(), instance_top_node)
      FIELD(glsl_uint_type(), instance_bottom_node)
      FIELD(glsl_array_type(glsl_uvec2_type(), MAX_STACK_DEPTH, 0), stack)
#undef FIELD
   };

   return glsl_struct_type(fields, ARRAY_SIZE(fields), "ray_query", false);
}

struct rq_var {
   nir_variable *rq;

   nir_intrinsic_instr *initialization;
   nir_def *uav_index;
};

static void
lower_ray_query(nir_shader *shader, nir_function_impl *impl,
                nir_variable *ray_query, struct hash_table *ht)
{
   struct rq_var *var = rzalloc(ht, struct rq_var);
   const glsl_type *type = ray_query->type;

   const glsl_type *rq_type = glsl_type_wrap_in_arrays(get_rq_type(), type);

   if (impl)
      var->rq = nir_local_variable_create(impl, rq_type, "ray_query");
   else
      var->rq = nir_variable_create(shader, nir_var_shader_temp, rq_type, "ray_query");

   _mesa_hash_table_insert(ht, ray_query, var);
}

static nir_deref_instr *
get_rq_deref(nir_builder *b, struct hash_table *ht, nir_def *def,
             struct rq_var **rq_var_out)
{
   nir_deref_instr *deref = nir_instr_as_deref(def->parent_instr);

   nir_deref_path path;
   nir_deref_path_init(&path, deref, NULL);
   assert(path.path[0]->deref_type == nir_deref_type_var);

   nir_variable *opaque_var = nir_deref_instr_get_variable(path.path[0]);
   struct hash_entry *entry = _mesa_hash_table_search(ht, opaque_var);
   assert(entry);

   struct rq_var *rq = (struct rq_var *)entry->data;

   nir_deref_instr *out_deref = nir_build_deref_var(b, rq->rq);

   if (glsl_type_is_array(opaque_var->type)) {
      nir_deref_instr **p = &path.path[1];
      for (; *p; p++) {
         if ((*p)->deref_type == nir_deref_type_array) {
            nir_def *index = (*p)->arr.index.ssa;

            out_deref = nir_build_deref_array(b, out_deref, index);
         } else {
            unreachable("Unsupported deref type");
         }
      }
   }

   nir_deref_path_finish(&path);

   if (rq_var_out)
      *rq_var_out = rq;

   return out_deref;
}

static nir_def *
get_rq_initialize_uav_index(nir_intrinsic_instr *intr, struct rq_var *var)
{
   if (intr->src[1].ssa->parent_instr->type == nir_instr_type_intrinsic &&
       nir_instr_as_intrinsic(intr->src[1].ssa->parent_instr)->intrinsic ==
       nir_intrinsic_load_vulkan_descriptor) {
      return intr->src[1].ssa;
   } else {
      return NULL;
   }
}

/* Before we modify control flow, walk the shader and determine ray query
 * instructions for which we know the ray query has been initialized via a
 * descriptor instead of a pointer, and record the UAV descriptor.
 */
static void
calc_uav_index(nir_function_impl *impl, struct hash_table *ht)
{
   nir_metadata_require(impl, nir_metadata_dominance);

   nir_foreach_block (block, impl) {
      nir_foreach_instr (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

         nir_def *rq_def;
         switch (intr->intrinsic) {
         case nir_intrinsic_rq_initialize:
         case nir_intrinsic_rq_load:
         case nir_intrinsic_rq_proceed:
            rq_def = intr->src[0].ssa;
            break;
         default:
            continue;
         }

         nir_deref_instr *deref = nir_instr_as_deref(rq_def->parent_instr);

         if (deref->deref_type != nir_deref_type_var)
            continue;

         nir_variable *opaque_var = deref->var;
         struct hash_entry *entry = _mesa_hash_table_search(ht, opaque_var);
         assert(entry);

         struct rq_var *rq = (struct rq_var *)entry->data;

         if (intr->intrinsic == nir_intrinsic_rq_initialize) {
            rq->initialization = intr;
            rq->uav_index = get_rq_initialize_uav_index(intr, rq);
         } else {
            if (rq->initialization &&
                nir_block_dominates(rq->initialization->instr.block,
                                    block) && rq->uav_index) {
               _mesa_hash_table_insert(ht, instr, rq->uav_index);
            }
         }
      }
   }
}

/* Return a pointer to the TLAS descriptor, which is actually a UAV
 * descriptor, if we know that the ray query has been initialized via a
 * descriptor and not a pointer. If not known, returns NULL.
 */
static nir_def *
get_uav_index(nir_instr *cur_instr, struct hash_table *ht)
{
   struct hash_entry *entry = _mesa_hash_table_search(ht, cur_instr);
   if (entry)
      return (nir_def *)entry->data;
   return NULL;
}

/* Load some data from the TLAS header or instance descriptors. This uses the
 * UAV descriptor when available, via "uav_index" which should be obtained
 * from get_uav_index().
 */
static nir_def *
load_tlas(nir_builder *b, nir_def *tlas,
          nir_def *uav_index, nir_def *index,
          unsigned offset, unsigned components)
{
   if (uav_index) {
      return nir_load_uav_ir3(b, components, 32, uav_index,
                              nir_vec2(b, index, nir_imm_int(b, offset / 4)),
                              .access = (gl_access_qualifier)(
                                          ACCESS_NON_WRITEABLE |
                                          ACCESS_CAN_REORDER),
                              .align_mul = AS_RECORD_SIZE,
                              .align_offset = offset);
   } else {
      return nir_load_global_ir3(b, components, 32,
                                 tlas,
                                 nir_iadd_imm(b, nir_imul_imm(b, index, AS_RECORD_SIZE / 4),
                                              offset / 4),
                                 /* The required alignment of the
                                  * user-specified base from the Vulkan spec.
                                  */
                                 .align_mul = 256,
                                 .align_offset = 0);
   }
}

/* The first record is the TLAS header and the rest of the records are
 * instances, so we need to add 1 to the instance ID when reading data in an
 * instance.
 */
#define load_instance_offset(b, tlas, uav_index, instance, \
                             field, offset, components) \
   load_tlas(b, tlas, uav_index, nir_iadd_imm(b, instance, 1), \
             offsetof(struct tu_instance_descriptor, field) + offset, \
             components)

#define load_instance(b, tlas, uav_index, instance, field, components) \
   load_instance_offset(b, tlas, uav_index, instance, field, 0, components)

#define rq_deref(b, _rq, name) nir_build_deref_struct(b, _rq, rq_index_##name)
#define rq_load(b, _rq, name) nir_load_deref(b, rq_deref(b, _rq, name))
#define rq_store(b, _rq, name, val, wrmask) \
   nir_store_deref(b, rq_deref(b, _rq, name), val, wrmask)
#define rqi_deref(b, _rq, name) nir_build_deref_struct(b, _rq, rq_intersection_##name)
#define rqi_load(b, _rq, name) nir_load_deref(b, rqi_deref(b, _rq, name))
#define rqi_store(b, _rq, name, val, wrmask) \
   nir_store_deref(b, rqi_deref(b, _rq, name), val, wrmask)

static void
lower_rq_initialize(nir_builder *b, struct hash_table *ht,
                    nir_intrinsic_instr *intr)
{
   struct rq_var *var;
   nir_deref_instr *rq = get_rq_deref(b, ht, intr->src[0].ssa, &var);

   if (nir_instr_as_deref(intr->src[0].ssa->parent_instr)->deref_type ==
       nir_deref_type_var) {
      var->initialization = intr;
   } else {
      var->initialization = NULL;
   }

   nir_def *uav_index = get_rq_initialize_uav_index(intr, var);

   nir_def *tlas = intr->src[1].ssa;
   nir_def *flags = intr->src[2].ssa;
   nir_def *cull_mask = intr->src[3].ssa;
   nir_def *origin = intr->src[4].ssa;
   nir_def *tmin = intr->src[5].ssa;
   nir_def *direction = intr->src[6].ssa;
   nir_def *tmax = intr->src[7].ssa;

   nir_def *tlas_base;
   if (uav_index) {
      tlas_base = load_tlas(b, NULL, uav_index, nir_imm_int(b, 0),
                            offsetof(struct tu_accel_struct_header,
                                     self_ptr), 2);
   } else {
      tlas_base = nir_unpack_64_2x32(b, tlas);
   }

   rq_store(b, rq, accel_struct_base, tlas_base, 0x3);

   nir_def *root_bvh_base = load_tlas(b, tlas_base, uav_index, nir_imm_int(b, 0),
                                      offsetof(struct tu_accel_struct_header,
                                               bvh_ptr), 2);

   nir_deref_instr *closest = rq_deref(b, rq, closest);
   nir_deref_instr *candidate = rq_deref(b, rq, candidate);

   rq_store(b, rq, flags,
            /* Fill out initial fourth src of ray_intersection */
            nir_ior_imm(b,
                        nir_ior(b, nir_ishl_imm(b, flags, 4),
                                nir_ishl_imm(b, cull_mask, 16)),
                        0b1111), 0x1);

   rqi_store(b, candidate, origin, origin, 0x7);
   rqi_store(b, candidate, direction, direction, 0x7);

   rq_store(b, rq, tmin, tmin, 0x1);
   rq_store(b, rq, world_origin, origin, 0x7);
   rq_store(b, rq, world_direction, direction, 0x7);

   rqi_store(b, closest, t, tmax, 0x1);
   rqi_store(b, closest, type_flags, nir_imm_int(b, TU_INTERSECTION_TYPE_NO_INTERSECTION), 0x1);

   /* Make sure that instance data loads don't hang in case of a miss by setting a valid initial instance. */
   rqi_store(b, closest, instance, nir_imm_int(b, 0), 0x1);
   rqi_store(b, candidate, instance, nir_imm_int(b, 0), 0x1);

   rq_store(b, rq, root_bvh_base, root_bvh_base, 0x3);
   rq_store(b, rq, bvh_base, root_bvh_base, 0x3);

   rq_store(b, rq, stack_ptr, nir_imm_int(b, 0), 0x1);
   rq_store(b, rq, top_stack, nir_imm_int(b, -1), 0x1);
   rq_store(b, rq, stack_low_watermark, nir_imm_int(b, 0), 0x1);
   rq_store(b, rq, current_node, nir_imm_int(b, 0), 0x1);
   rq_store(b, rq, previous_node, nir_imm_int(b, VK_BVH_INVALID_NODE), 0x1);
   rq_store(b, rq, instance_top_node, nir_imm_int(b, VK_BVH_INVALID_NODE), 0x1);
   rq_store(b, rq, instance_bottom_node, nir_imm_int(b, VK_BVH_INVALID_NODE), 0x1);

   rq_store(b, rq, incomplete, nir_imm_true(b), 0x1);
}

static void
insert_terminate_on_first_hit(nir_builder *b, nir_deref_instr *rq)
{
   nir_def *terminate_on_first_hit =
      nir_test_mask(b, rq_load(b, rq, flags),
                    SpvRayFlagsTerminateOnFirstHitKHRMask << 4);
   nir_push_if(b, terminate_on_first_hit);
   {
      rq_store(b, rq, incomplete, nir_imm_false(b), 0x1);
   }
   nir_pop_if(b, NULL);
}

static void
lower_rq_confirm_intersection(nir_builder *b, struct hash_table *ht, nir_intrinsic_instr *intr)
{
   nir_deref_instr *rq = get_rq_deref(b, ht, intr->src[0].ssa, NULL);
   nir_copy_deref(b, rq_deref(b, rq, closest), rq_deref(b, rq, candidate));
   insert_terminate_on_first_hit(b, rq);
}

static void
lower_rq_generate_intersection(nir_builder *b, struct hash_table *ht, nir_intrinsic_instr *intr)
{
   nir_deref_instr *rq = get_rq_deref(b, ht, intr->src[0].ssa, NULL);
   nir_deref_instr *closest = rq_deref(b, rq, closest);
   nir_deref_instr *candidate = rq_deref(b, rq, candidate);

   nir_push_if(b, nir_iand(b, nir_fge(b, rqi_load(b, closest, t),
                                      intr->src[1].ssa),
                           nir_fge(b, intr->src[1].ssa,
                                   rq_load(b, rq, tmin))));
   {
      nir_copy_deref(b, closest, candidate);
      insert_terminate_on_first_hit(b, rq);
      rqi_store(b, closest, t, intr->src[1].ssa, 0x1);
   }
   nir_pop_if(b, NULL);
}

static void
lower_rq_terminate(nir_builder *b, struct hash_table *ht, nir_intrinsic_instr *intr)
{
   nir_deref_instr *rq = get_rq_deref(b, ht, intr->src[0].ssa, NULL);
   rq_store(b, rq, incomplete, nir_imm_false(b), 0x1);
}

static nir_def *
lower_rq_load(nir_builder *b, struct hash_table *ht, nir_intrinsic_instr *intr)
{
   struct rq_var *var;
   nir_deref_instr *rq = get_rq_deref(b, ht, intr->src[0].ssa, &var);
   nir_def *uav_index = get_uav_index(&intr->instr, ht);
   nir_def *tlas = rq_load(b, rq, accel_struct_base);
   nir_deref_instr *closest = rq_deref(b, rq, closest);
   nir_deref_instr *candidate = rq_deref(b, rq, candidate);
   bool committed = nir_intrinsic_committed(intr);
   nir_deref_instr *intersection = committed ? closest : candidate;

   uint32_t column = nir_intrinsic_column(intr);

   nir_ray_query_value value = nir_intrinsic_ray_query_value(intr);
   switch (value) {
   case nir_ray_query_value_flags: {
      nir_def *flags = rq_load(b, rq, flags);
      return nir_ubitfield_extract(b, flags, nir_imm_int(b, 4),
                                   nir_imm_int(b, 12));
   }
   case nir_ray_query_value_intersection_barycentrics:
      return rqi_load(b, intersection, barycentrics);
   case nir_ray_query_value_intersection_candidate_aabb_opaque:
      return nir_ieq_imm(b, nir_iand_imm(b, rqi_load(b, candidate, type_flags),
                                         TU_INTERSECTION_TYPE_AABB |
                                         TU_INTERSECTION_TYPE_NONOPAQUE |
                                         TU_INTERSECTION_TYPE_NO_INTERSECTION),
                         TU_INTERSECTION_TYPE_AABB);
   case nir_ray_query_value_intersection_front_face:
      return nir_inot(b, nir_test_mask(b, rqi_load(b, intersection, type_flags),
                                       TU_INTERSECTION_BACK_FACE));
   case nir_ray_query_value_intersection_geometry_index:
      return rqi_load(b, intersection, geometry_id);
   case nir_ray_query_value_intersection_instance_custom_index: {
      nir_def *instance = rqi_load(b, intersection, instance);
      return load_instance(b, tlas, uav_index, instance, custom_instance_index, 1);
   }
   case nir_ray_query_value_intersection_instance_id:
      return rqi_load(b, intersection, instance);
   case nir_ray_query_value_intersection_instance_sbt_index:
      return rqi_load(b, intersection, sbt_offset);
   case nir_ray_query_value_intersection_object_ray_direction:
      return rqi_load(b, intersection, direction);
   case nir_ray_query_value_intersection_object_ray_origin:
      return rqi_load(b, intersection, origin);
   case nir_ray_query_value_intersection_object_to_world: {
      nir_def *instance = rqi_load(b, intersection, instance);
      nir_def *rows[3];
      for (unsigned r = 0; r < 3; ++r)
         rows[r] = load_instance_offset(b, tlas, uav_index, instance,
                                        otw_matrix.values,
                                        r * 16, 4);

      return nir_vec3(b, nir_channel(b, rows[0], column), nir_channel(b, rows[1], column),
                      nir_channel(b, rows[2], column));
   }
   case nir_ray_query_value_intersection_primitive_index:
      return rqi_load(b, intersection, primitive_id);
   case nir_ray_query_value_intersection_t:
      return rqi_load(b, intersection, t);
   case nir_ray_query_value_intersection_type: {
      nir_def *intersection_type =
         nir_iand_imm(b, nir_ishr_imm(b, rqi_load(b, intersection, type_flags),
                                      util_logbase2(TU_INTERSECTION_TYPE_AABB)), 1);
      if (committed) {
         nir_def *has_intersection =
            nir_inot(b,
                     nir_test_mask(b, rqi_load(b, intersection, type_flags),
                                   TU_INTERSECTION_TYPE_NO_INTERSECTION));
         intersection_type = nir_iadd(b, intersection_type,
                                      nir_b2i32(b, has_intersection));
      }
      return intersection_type;
   }
   case nir_ray_query_value_intersection_world_to_object: {
      nir_def *instance = rqi_load(b, intersection, instance);
      nir_def *rows[3];
      for (unsigned r = 0; r < 3; ++r)
         rows[r] = load_instance_offset(b, tlas, uav_index, instance,
                                        wto_matrix.values, r * 16, 4);

      return nir_vec3(b, nir_channel(b, rows[0], column), nir_channel(b, rows[1], column),
                      nir_channel(b, rows[2], column));
   }
   case nir_ray_query_value_tmin:
      return rq_load(b, rq, tmin);
   case nir_ray_query_value_world_ray_direction:
      return rq_load(b, rq, world_direction);
   case nir_ray_query_value_world_ray_origin:
      return rq_load(b, rq, world_origin);
   default:
      unreachable("Invalid nir_ray_query_value!");
   }

   return NULL;
}

/* For the initialization of instance_bottom_node. Explicitly different than
 * VK_BVH_INVALID_NODE or any real node, to ensure we never exit an instance
 * when we're not in one.
 */
#define TU_BVH_NO_INSTANCE_ROOT 0xfffffffeu

nir_def *
nir_build_vec3_mat_mult(nir_builder *b, nir_def *vec, nir_def *matrix[], bool translation)
{
   nir_def *result_components[3] = {
      nir_channel(b, matrix[0], 3),
      nir_channel(b, matrix[1], 3),
      nir_channel(b, matrix[2], 3),
   };
   for (unsigned i = 0; i < 3; ++i) {
      for (unsigned j = 0; j < 3; ++j) {
         nir_def *v = nir_fmul(b, nir_channels(b, vec, 1 << j), nir_channels(b, matrix[i], 1 << j));
         result_components[i] = (translation || j) ? nir_fadd(b, result_components[i], v) : v;
      }
   }
   return nir_vec(b, result_components, 3);
}

static nir_def *
fetch_parent_node(nir_builder *b, nir_def *bvh, nir_def *node)
{
   nir_def *offset = nir_iadd_imm(b, nir_imul_imm(b, node, 4), 4);

   return nir_build_load_global(b, 1, 32, nir_isub(b, nir_pack_64_2x32(b, bvh),
                                                   nir_u2u64(b, offset)), .align_mul = 4);
}

static nir_def *
build_ray_traversal(nir_builder *b, nir_deref_instr *rq,
                    nir_def *tlas, nir_def *uav_index)
{
   nir_deref_instr *candidate = rq_deref(b, rq, candidate);
   nir_deref_instr *closest = rq_deref(b, rq, closest);

   nir_variable *incomplete = nir_local_variable_create(b->impl, glsl_bool_type(), "incomplete");
   nir_store_var(b, incomplete, nir_imm_true(b), 0x1);

   nir_push_loop(b);
   {
      /* Go up the stack if current_node == VK_BVH_INVALID_NODE */
      nir_push_if(b, nir_ieq_imm(b, rq_load(b, rq, current_node), VK_BVH_INVALID_NODE));
      {
         /* Early exit if we never overflowed the stack, to avoid having to backtrack to
          * the root for no reason. */
         nir_push_if(b, nir_ilt_imm(b, rq_load(b, rq, stack_ptr), 1));
         {
            nir_store_var(b, incomplete, nir_imm_false(b), 0x1);
            nir_jump(b, nir_jump_break);
         }
         nir_pop_if(b, NULL);

         nir_def *stack_instance_exit =
            nir_ige(b, rq_load(b, rq, top_stack), rq_load(b, rq, stack_ptr));
         nir_def *root_instance_exit =
            nir_ieq(b, rq_load(b, rq, previous_node), rq_load(b, rq, instance_bottom_node));
         nir_if *instance_exit = nir_push_if(b, nir_ior(b, stack_instance_exit, root_instance_exit));
         instance_exit->control = nir_selection_control_dont_flatten;
         {
            rq_store(b, rq, top_stack, nir_imm_int(b, -1), 1);
            rq_store(b, rq, previous_node, rq_load(b, rq, instance_top_node), 1);
            rq_store(b, rq, instance_bottom_node, nir_imm_int(b, TU_BVH_NO_INSTANCE_ROOT), 1);

            rq_store(b, rq, bvh_base, rq_load(b, rq, root_bvh_base), 3);
            rqi_store(b, candidate, origin, rq_load(b, rq, world_origin), 7);
            rqi_store(b, candidate, direction, rq_load(b, rq, world_direction), 7);
         }
         nir_pop_if(b, NULL);

         nir_push_if(
            b, nir_ige(b, rq_load(b, rq, stack_low_watermark), rq_load(b, rq, stack_ptr)));
         {
            /* Get the parent of the previous node using the parent pointers.
             * We will re-intersect the parent and figure out what index the
             * previous node was below.
             */
            nir_def *prev = rq_load(b, rq, previous_node);
            nir_def *bvh_addr = rq_load(b, rq, bvh_base);

            nir_def *parent = fetch_parent_node(b, bvh_addr, prev);
            nir_push_if(b, nir_ieq_imm(b, parent, VK_BVH_INVALID_NODE));
            {
               nir_store_var(b, incomplete, nir_imm_false(b), 0x1);
               nir_jump(b, nir_jump_break);
            }
            nir_pop_if(b, NULL);
            rq_store(b, rq, current_node, parent, 0x1);
         }
         nir_push_else(b, NULL);
         {
            /* Go up the stack and get the next child of the parent. */
            nir_def *stack_ptr = nir_iadd_imm(b, rq_load(b, rq, stack_ptr), -1);

            nir_def *stack_idx =
               nir_umod_imm(b, stack_ptr, MAX_STACK_DEPTH);
            nir_deref_instr *stack_deref =
               nir_build_deref_array(b, rq_deref(b, rq, stack), stack_idx);
            nir_def *stack_entry = nir_load_deref(b, stack_deref);
            nir_def *children_base = nir_channel(b, stack_entry, 0);
            nir_def *children = nir_channel(b, stack_entry, 1);

            nir_def *next_child_idx =
               nir_iadd_imm(b, nir_iand_imm(b, children, 0x1f), -3);
            
            nir_def *child_offset =
               nir_iand_imm(b, nir_ishr(b, children, next_child_idx), 0x7);
            nir_def *bvh_node = nir_iadd(b, children_base, child_offset);

            nir_push_if(b, nir_ieq_imm(b, next_child_idx, 8));
            {
               rq_store(b, rq, stack_ptr, stack_ptr, 1);
            }
            nir_push_else(b, NULL);
            {
               children = nir_bitfield_insert(b, children, next_child_idx,
                                              nir_imm_int(b, 0),
                                              nir_imm_int(b, 5));
               nir_store_deref(b, stack_deref,
                               nir_vec2(b, nir_undef(b, 1, 32), children),
                               0x2);
            }
            nir_pop_if(b, NULL);

            rq_store(b, rq, current_node, bvh_node, 0x1);
            /* We don't need previous_node when we have the stack. Indicate to
             * the internal intersection handling below that this isn't the
             * underflow case.
             */
            rq_store(b, rq, previous_node, nir_imm_int(b, VK_BVH_INVALID_NODE), 0x1);
         }
         nir_pop_if(b, NULL);
      }
      nir_push_else(b, NULL);
      {
         rq_store(b, rq, previous_node, nir_imm_int(b, VK_BVH_INVALID_NODE), 0x1);
      }
      nir_pop_if(b, NULL);

      nir_def *bvh_node = rq_load(b, rq, current_node);
      nir_def *bvh_base = rq_load(b, rq, bvh_base);

      nir_def *prev_node = rq_load(b, rq, previous_node);
      rq_store(b, rq, previous_node, bvh_node, 0x1);
      rq_store(b, rq, current_node, nir_imm_int(b, VK_BVH_INVALID_NODE), 0x1);

      nir_def *origin = rqi_load(b, candidate, origin);
      nir_def *tmin = rq_load(b, rq, tmin);
      nir_def *direction = rqi_load(b, candidate, direction);
      nir_def *tmax = rqi_load(b, closest, t);

      nir_def *intrinsic_result =
         nir_ray_intersection_ir3(b, 32, bvh_base, bvh_node,
                                  nir_vec8(b,
                                           nir_channel(b, origin, 0),
                                           nir_channel(b, origin, 1),
                                           nir_channel(b, origin, 2),
                                           tmin,
                                           nir_channel(b, direction, 0),
                                           nir_channel(b, direction, 1),
                                           nir_channel(b, direction, 2),
                                           tmax),
                                  rq_load(b, rq, flags));

      nir_def *intersection_flags = nir_channel(b, intrinsic_result, 0);
      nir_def *intersection_count =
         nir_ubitfield_extract_imm(b, intersection_flags, 4, 4);
      nir_def *intersection_id = nir_channel(b, intrinsic_result, 1);

      nir_push_if(b, nir_test_mask(b, intersection_flags,
                                   TU_INTERSECTION_TYPE_LEAF));
      {
         nir_def *processed_mask = nir_iand_imm(b, intersection_flags, 0xf);

         /* Keep processing the current node if the mask isn't yet 0 */
         rq_store(b, rq, current_node,
                  nir_bcsel(b, nir_ieq_imm(b, processed_mask, 0),
                            nir_imm_int(b, VK_BVH_INVALID_NODE),
                            bvh_node), 1);

         /* If the mask is 0, replace with the initial 0xf for the next
          * intersection.
          */
         processed_mask =
            nir_bcsel(b, nir_ieq_imm(b, processed_mask, 0),
                      nir_imm_int(b, 0xf), processed_mask);
         
         /* Replace the mask in the flags. */
         rq_store(b, rq, flags,
                  nir_bitfield_insert(b, rq_load(b, rq, flags),
                                      processed_mask, nir_imm_int(b, 0),
                                      nir_imm_int(b, 4)), 1);

         nir_push_if(b, nir_ieq_imm(b, intersection_count, 0));
         {
            nir_jump(b, nir_jump_continue);
         }
         nir_pop_if(b, NULL);

         nir_push_if(b, nir_test_mask(b, intersection_flags,
                                      TU_INTERSECTION_TYPE_TLAS));
         {
            /* instance */
            rqi_store(b, candidate, instance, intersection_id, 1);

            nir_def *wto_matrix[3];
            for (unsigned i = 0; i < 3; i++)
               wto_matrix[i] = load_instance_offset(b, tlas, uav_index,
                                                    intersection_id,
                                                    wto_matrix.values,
                                                    i * 16, 4);

            nir_def *sbt_offset_and_flags =
               load_instance(b, tlas, uav_index, intersection_id,
                             sbt_offset_and_flags, 1);
            nir_def *blas_bvh =
               load_instance(b, tlas, uav_index, intersection_id,
                             bvh_ptr, 2);

            nir_def *instance_flags = nir_iand_imm(b, sbt_offset_and_flags,
                                                   0xff000000);
            nir_def *sbt_offset = nir_iand_imm(b, sbt_offset_and_flags,
                                               0x00ffffff);
            nir_def *flags = rq_load(b, rq, flags);
            flags = nir_ior(b, nir_iand_imm(b, flags, 0x00ffffff),
                            instance_flags);
            rq_store(b, rq, flags, flags, 1);

            rqi_store(b, candidate, sbt_offset, sbt_offset, 1);

            rq_store(b, rq, top_stack, rq_load(b, rq, stack_ptr), 1);
            rq_store(b, rq, bvh_base, blas_bvh, 3);

            /* Push the instance root node onto the stack */
            rq_store(b, rq, current_node, nir_imm_int(b, 0), 0x1);
            rq_store(b, rq, instance_bottom_node, nir_imm_int(b, 0), 1);
            rq_store(b, rq, instance_top_node, bvh_node, 1);

            /* Transform the ray into object space */
            rqi_store(b, candidate, origin,
                      nir_build_vec3_mat_mult(b, rq_load(b, rq, world_origin),
                                              wto_matrix, true), 7);
            rqi_store(b, candidate, direction,
                      nir_build_vec3_mat_mult(b, rq_load(b, rq, world_direction),
                                              wto_matrix, false), 7);
         }
         nir_push_else(b, NULL);
         {
            /* AABB & triangle */
            rqi_store(b, candidate, type_flags,
                      nir_iand_imm(b, intersection_flags,
                                   TU_INTERSECTION_TYPE_AABB |
                                   TU_INTERSECTION_TYPE_NONOPAQUE |
                                   TU_INTERSECTION_BACK_FACE), 1);

            rqi_store(b, candidate, primitive_id, intersection_id, 1);

            /* TODO: Implement optimization to try to combine these into 1
             * 32-bit ID, for compressed nodes.
             *
             * load_global_ir3 doesn't have the required range so we have to
             * do the offset math ourselves.
             */
            nir_def *offset =
               nir_ior_imm(b, nir_imul_imm(b, nir_u2u64(b, bvh_node),
                                            sizeof(tu_leaf_node)),
                           offsetof(struct tu_leaf_node, geometry_id));
            nir_def *geometry_id_ptr = nir_iadd(b, nir_pack_64_2x32(b, bvh_base),
                                                offset);
            nir_def *geometry_id =
               nir_build_load_global(b, 1, 32, geometry_id_ptr,
                                     .access = ACCESS_NON_WRITEABLE,
                                     .align_mul = sizeof(struct tu_leaf_node),
                                     .align_offset = offsetof(struct tu_leaf_node,
                                                              geometry_id));
            rqi_store(b, candidate, geometry_id, geometry_id, 1);

            nir_push_if(b, nir_test_mask(b, intersection_flags,
                                         TU_INTERSECTION_TYPE_AABB));
            {
               nir_jump(b, nir_jump_break);
            }
            nir_push_else(b, NULL);
            {
               rqi_store(b, candidate, barycentrics,
                         nir_vec2(b, nir_channel(b, intrinsic_result, 3),
                                  nir_channel(b, intrinsic_result, 4)), 0x3);
               rqi_store(b, candidate, t, nir_channel(b, intrinsic_result,
                                                      2), 0x1);
               nir_push_if(b, nir_test_mask(b, intersection_flags,
                                            TU_INTERSECTION_TYPE_NONOPAQUE));
               {
                  nir_jump(b, nir_jump_break);
               }
               nir_push_else(b, NULL);
               {
                  nir_copy_deref(b, closest, candidate);
                  nir_def *terminate_on_first_hit =
                     nir_test_mask(b, rq_load(b, rq, flags),
                                   SpvRayFlagsTerminateOnFirstHitKHRMask << 4);
                  nir_push_if(b, terminate_on_first_hit);
                  {
                     nir_store_var(b, incomplete, nir_imm_false(b), 0x1);
                     nir_jump(b, nir_jump_break);
                  }
                  nir_pop_if(b, NULL);
               }
               nir_pop_if(b, NULL);
            }
            nir_pop_if(b, NULL);
         }
         nir_pop_if(b, NULL);
      }
      nir_push_else(b, NULL);
      {
         /* internal */
         nir_push_if(b, nir_ine_imm(b, intersection_count, 0));
         {
            nir_def *children = nir_channel(b, intrinsic_result, 3);

            nir_push_if(b, nir_ieq_imm(b, prev_node, VK_BVH_INVALID_NODE));
            {
               /* The children array returned by the HW is specially set up so
                * that we can do this to get the first child.
                */
               nir_def *first_child_offset =
                  nir_iand_imm(b, nir_ishr(b, children, children), 0x7);

               rq_store(b, rq, current_node,
                        nir_iadd(b, intersection_id, first_child_offset),
                        0x1);

               nir_push_if(b, nir_igt_imm(b, intersection_count, 1));
               {
                  nir_def *stack_ptr = rq_load(b, rq, stack_ptr);
                  nir_def *stack_idx = nir_umod_imm(b, stack_ptr,
                                                    MAX_STACK_DEPTH);
                  nir_def *stack_entry =
                     nir_vec2(b, intersection_id, children);
                  nir_store_deref(b,
                                  nir_build_deref_array(b, rq_deref(b, rq, stack),
                                                        stack_idx),
                                  stack_entry, 0x7);
                  rq_store(b, rq, stack_ptr,
                           nir_iadd_imm(b, rq_load(b, rq, stack_ptr), 1), 0x1);

                  nir_def *new_watermark =
                     nir_iadd_imm(b, rq_load(b, rq, stack_ptr),
                                  -MAX_STACK_DEPTH);
                  new_watermark = nir_imax(b, rq_load(b, rq,
                                                      stack_low_watermark),
                                           new_watermark);
                  rq_store(b, rq, stack_low_watermark, new_watermark, 0x1);
               }
               nir_pop_if(b, NULL);
            }
            nir_push_else(b, NULL);
            {
               /* The underflow case. We have the previous_node and an array
                * of intersecting children of its parent, and we need to find
                * its position in the array so that we can return the next
                * child in the array or VK_BVH_INVALID_NODE if it's the last
                * child.
                */
               nir_def *prev_offset =
                  nir_isub(b, prev_node, intersection_id);

               /* A bit-pattern with ones at the LSB of each child's
                * position.
                */
               uint32_t ones = 0b1001001001001001001001 << 8;

               /* Replicate prev_offset into the position of each child. */
               nir_def *prev_offset_repl =
                  nir_imul_imm(b, prev_offset, ones);

               /* a == b <=> a ^ b == 0. Reduce the problem to finding the
                * child whose bits are 0.
                */
               nir_def *diff = nir_ixor(b, prev_offset_repl, children);

               /* This magic formula comes from Hacker's Delight, section 6.1
                * "Find First 0-byte", adapted for 3-bit "bytes". The first
                * zero byte will be the lowest byte with 1 set in the highest
                * position (i.e. bit 2). We need to then subtract 2 to get the
                * current position and 5 to get the next position.
                */
               diff = nir_iand_imm(b, nir_iand(b, nir_iadd_imm(b, diff, -ones),
                                               nir_inot(b, diff)),
                                   ones << 2);
               diff = nir_find_lsb(b, diff);

               nir_def *next_offset =
                  nir_iand_imm(b, nir_ishr(b, children,
                                           nir_iadd_imm(b, diff, -5)),
                               0x7);

               nir_def *next =
                  nir_bcsel(b, nir_ieq_imm(b, diff, 8 + 2),
                            nir_imm_int(b, VK_BVH_INVALID_NODE),
                            nir_iadd(b, next_offset, intersection_id));
               rq_store(b, rq, current_node, next, 0x1);
            }
            nir_pop_if(b, NULL);
         }
         nir_pop_if(b, NULL);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_loop(b, NULL);

   return nir_load_var(b, incomplete);
}

static nir_def *
lower_rq_proceed(nir_builder *b, struct hash_table *ht, nir_intrinsic_instr *intr)
{
   struct rq_var *var;
   nir_deref_instr *rq = get_rq_deref(b, ht, intr->src[0].ssa, &var);
   nir_def *uav_index = get_uav_index(&intr->instr, ht);
   nir_def *tlas = rq_load(b, rq, accel_struct_base);

   nir_push_if(b, nir_load_deref(b, rq_deref(b, rq, incomplete)));
   {
      nir_def *incomplete = build_ray_traversal(b, rq, tlas, uav_index);
      nir_store_deref(b, rq_deref(b, rq, incomplete), incomplete, 0x1);
   }
   nir_pop_if(b, NULL);

   return nir_load_deref(b, rq_deref(b, rq, incomplete));
}

bool
tu_nir_lower_ray_queries(nir_shader *shader)
{
   bool progress = false;
   struct hash_table *query_ht = _mesa_pointer_hash_table_create(NULL);

   nir_foreach_variable_in_list (var, &shader->variables) {
      if (!var->data.ray_query)
         continue;

      lower_ray_query(shader, NULL, var, query_ht);

      progress = true;
   }

   nir_foreach_function (function, shader) {
      if (!function->impl)
         continue;

      nir_builder builder = nir_builder_create(function->impl);

      nir_foreach_variable_in_list (var, &function->impl->locals) {
         if (!var->data.ray_query)
            continue;

         lower_ray_query(shader, function->impl, var, query_ht);

         progress = true;
      }

      calc_uav_index(function->impl, query_ht);

      nir_foreach_block (block, function->impl) {
         nir_foreach_instr_safe (instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);

            if (!nir_intrinsic_is_ray_query(intrinsic->intrinsic))
               continue;

            builder.cursor = nir_before_instr(instr);

            nir_def *new_dest = NULL;

            switch (intrinsic->intrinsic) {
            case nir_intrinsic_rq_confirm_intersection:
               lower_rq_confirm_intersection(&builder, query_ht, intrinsic);
               break;
            case nir_intrinsic_rq_generate_intersection:
               lower_rq_generate_intersection(&builder, query_ht, intrinsic);
               break;
            case nir_intrinsic_rq_initialize:
               lower_rq_initialize(&builder, query_ht, intrinsic);
               break;
            case nir_intrinsic_rq_load:
               new_dest = lower_rq_load(&builder, query_ht, intrinsic);
               break;
            case nir_intrinsic_rq_proceed:
               new_dest = lower_rq_proceed(&builder, query_ht, intrinsic);
               break;
            case nir_intrinsic_rq_terminate:
               lower_rq_terminate(&builder, query_ht, intrinsic);
               break;
            default:
               unreachable("Unsupported ray query intrinsic!");
            }

            if (new_dest)
               nir_def_rewrite_uses(&intrinsic->def, new_dest);

            nir_instr_remove(instr);
            nir_instr_free(instr);

            progress = true;
         }
      }

      nir_metadata_preserve(function->impl, nir_metadata_none);
   }

   ralloc_free(query_ht);

   return progress;
}

