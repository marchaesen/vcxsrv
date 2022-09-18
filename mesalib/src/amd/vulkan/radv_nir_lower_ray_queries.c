/*
 * Copyright © 2022 Konstantin Seurer
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

#include "nir/nir.h"
#include "nir/nir_builder.h"

#include "util/hash_table.h"

#include "radv_acceleration_structure.h"
#include "radv_private.h"
#include "radv_rt_common.h"
#include "radv_shader.h"

typedef struct {
   nir_variable *variable;
   unsigned array_length;
} rq_variable;

static rq_variable *
rq_variable_create(nir_shader *shader, nir_function_impl *impl, unsigned array_length,
                   const struct glsl_type *type, const char *name)
{
   rq_variable *result = ralloc(shader ? (void *)shader : (void *)impl, rq_variable);
   result->array_length = array_length;

   const struct glsl_type *variable_type = type;
   if (array_length != 1)
      variable_type = glsl_array_type(type, array_length, glsl_get_explicit_stride(type));

   if (shader) {
      result->variable = nir_variable_create(shader, nir_var_shader_temp, variable_type, name);
   } else {
      result->variable = nir_local_variable_create(impl, variable_type, name);
   }

   return result;
}

static nir_ssa_def *
nir_load_array(nir_builder *b, nir_variable *array, nir_ssa_def *index)
{
   return nir_load_deref(b, nir_build_deref_array(b, nir_build_deref_var(b, array), index));
}

static void
nir_store_array(nir_builder *b, nir_variable *array, nir_ssa_def *index, nir_ssa_def *value,
                unsigned writemask)
{
   nir_store_deref(b, nir_build_deref_array(b, nir_build_deref_var(b, array), index), value,
                   writemask);
}

static nir_ssa_def *
rq_load_var(nir_builder *b, nir_ssa_def *index, rq_variable *var)
{
   if (var->array_length == 1)
      return nir_load_var(b, var->variable);

   return nir_load_array(b, var->variable, index);
}

static void
rq_store_var(nir_builder *b, nir_ssa_def *index, rq_variable *var, nir_ssa_def *value,
             unsigned writemask)
{
   if (var->array_length == 1) {
      nir_store_var(b, var->variable, value, writemask);
   } else {
      nir_store_array(b, var->variable, index, value, writemask);
   }
}

static void
rq_copy_var(nir_builder *b, nir_ssa_def *index, rq_variable *dst, rq_variable *src, unsigned mask)
{
   rq_store_var(b, index, dst, rq_load_var(b, index, src), mask);
}

static nir_ssa_def *
rq_load_array(nir_builder *b, nir_ssa_def *index, rq_variable *var, nir_ssa_def *array_index)
{
   if (var->array_length == 1)
      return nir_load_array(b, var->variable, array_index);

   return nir_load_deref(
      b,
      nir_build_deref_array(
         b, nir_build_deref_array(b, nir_build_deref_var(b, var->variable), index), array_index));
}

static void
rq_store_array(nir_builder *b, nir_ssa_def *index, rq_variable *var, nir_ssa_def *array_index,
               nir_ssa_def *value, unsigned writemask)
{
   if (var->array_length == 1) {
      nir_store_array(b, var->variable, array_index, value, writemask);
   } else {
      nir_store_deref(
         b,
         nir_build_deref_array(
            b, nir_build_deref_array(b, nir_build_deref_var(b, var->variable), index), array_index),
         value, writemask);
   }
}

struct ray_query_traversal_vars {
   rq_variable *origin;
   rq_variable *direction;

   rq_variable *inv_dir;
   rq_variable *bvh_base;
   rq_variable *stack;
   rq_variable *top_stack;
};

struct ray_query_intersection_vars {
   rq_variable *primitive_id;
   rq_variable *geometry_id_and_flags;
   rq_variable *instance_id;
   rq_variable *instance_addr;
   rq_variable *intersection_type;
   rq_variable *opaque;
   rq_variable *frontface;
   rq_variable *custom_instance_and_mask;
   rq_variable *sbt_offset_and_flags;
   rq_variable *barycentrics;
   rq_variable *t;
};

struct ray_query_vars {
   rq_variable *accel_struct;
   rq_variable *flags;
   rq_variable *cull_mask;
   rq_variable *origin;
   rq_variable *tmin;
   rq_variable *direction;

   rq_variable *incomplete;

   struct ray_query_intersection_vars closest;
   struct ray_query_intersection_vars candidate;

   struct ray_query_traversal_vars trav;

   rq_variable *stack;
};

#define VAR_NAME(name)                                                                             \
   strcat(strcpy(ralloc_size(impl, strlen(base_name) + strlen(name) + 1), base_name), name)

static struct ray_query_traversal_vars
init_ray_query_traversal_vars(nir_shader *shader, nir_function_impl *impl, unsigned array_length,
                              const char *base_name)
{
   struct ray_query_traversal_vars result;

   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);

   result.origin = rq_variable_create(shader, impl, array_length, vec3_type, VAR_NAME("_origin"));
   result.direction =
      rq_variable_create(shader, impl, array_length, vec3_type, VAR_NAME("_direction"));

   result.inv_dir = rq_variable_create(shader, impl, array_length, vec3_type, VAR_NAME("_inv_dir"));
   result.bvh_base =
      rq_variable_create(shader, impl, array_length, glsl_uint64_t_type(), VAR_NAME("_bvh_base"));
   result.stack =
      rq_variable_create(shader, impl, array_length, glsl_uint_type(), VAR_NAME("_stack"));
   result.top_stack =
      rq_variable_create(shader, impl, array_length, glsl_uint_type(), VAR_NAME("_top_stack"));

   return result;
}

static struct ray_query_intersection_vars
init_ray_query_intersection_vars(nir_shader *shader, nir_function_impl *impl, unsigned array_length,
                                 const char *base_name)
{
   struct ray_query_intersection_vars result;

   const struct glsl_type *vec2_type = glsl_vector_type(GLSL_TYPE_FLOAT, 2);

   result.primitive_id =
      rq_variable_create(shader, impl, array_length, glsl_uint_type(), VAR_NAME("_primitive_id"));
   result.geometry_id_and_flags = rq_variable_create(shader, impl, array_length, glsl_uint_type(),
                                                     VAR_NAME("_geometry_id_and_flags"));
   result.instance_id =
      rq_variable_create(shader, impl, array_length, glsl_uint_type(), VAR_NAME("_instance_id"));
   result.instance_addr = rq_variable_create(shader, impl, array_length, glsl_uint64_t_type(),
                                             VAR_NAME("_instance_addr"));
   result.intersection_type = rq_variable_create(shader, impl, array_length, glsl_uint_type(),
                                                 VAR_NAME("_intersection_type"));
   result.opaque =
      rq_variable_create(shader, impl, array_length, glsl_bool_type(), VAR_NAME("_opaque"));
   result.frontface =
      rq_variable_create(shader, impl, array_length, glsl_bool_type(), VAR_NAME("_frontface"));
   result.custom_instance_and_mask = rq_variable_create(
      shader, impl, array_length, glsl_uint_type(), VAR_NAME("_custom_instance_and_mask"));
   result.sbt_offset_and_flags = rq_variable_create(shader, impl, array_length, glsl_uint_type(),
                                                    VAR_NAME("_sbt_offset_and_flags"));
   result.barycentrics =
      rq_variable_create(shader, impl, array_length, vec2_type, VAR_NAME("_barycentrics"));
   result.t = rq_variable_create(shader, impl, array_length, glsl_float_type(), VAR_NAME("_t"));

   return result;
}

static void
init_ray_query_vars(nir_shader *shader, nir_function_impl *impl, unsigned array_length,
                    struct ray_query_vars *dst, const char *base_name)
{
   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);

   dst->accel_struct = rq_variable_create(shader, impl, array_length, glsl_uint64_t_type(),
                                          VAR_NAME("_accel_struct"));
   dst->flags =
      rq_variable_create(shader, impl, array_length, glsl_uint_type(), VAR_NAME("_flags"));
   dst->cull_mask =
      rq_variable_create(shader, impl, array_length, glsl_uint_type(), VAR_NAME("_cull_mask"));
   dst->origin = rq_variable_create(shader, impl, array_length, vec3_type, VAR_NAME("_origin"));
   dst->tmin = rq_variable_create(shader, impl, array_length, glsl_float_type(), VAR_NAME("_tmin"));
   dst->direction =
      rq_variable_create(shader, impl, array_length, vec3_type, VAR_NAME("_direction"));

   dst->incomplete =
      rq_variable_create(shader, impl, array_length, glsl_bool_type(), VAR_NAME("_incomplete"));

   dst->closest =
      init_ray_query_intersection_vars(shader, impl, array_length, VAR_NAME("_closest"));
   dst->candidate =
      init_ray_query_intersection_vars(shader, impl, array_length, VAR_NAME("_candidate"));

   dst->trav = init_ray_query_traversal_vars(shader, impl, array_length, VAR_NAME("_top"));

   dst->stack = rq_variable_create(shader, impl, array_length,
                                   glsl_array_type(glsl_uint_type(), MAX_STACK_ENTRY_COUNT,
                                                   glsl_get_explicit_stride(glsl_uint_type())),
                                   VAR_NAME("_stack"));
}

#undef VAR_NAME

static void
lower_ray_query(nir_shader *shader, nir_function_impl *impl, nir_variable *ray_query,
                struct hash_table *ht)
{
   struct ray_query_vars *vars = ralloc(impl, struct ray_query_vars);

   unsigned array_length = 1;
   if (glsl_type_is_array(ray_query->type))
      array_length = glsl_get_length(ray_query->type);

   init_ray_query_vars(shader, impl, array_length, vars,
                       ray_query->name == NULL ? "" : ray_query->name);

   _mesa_hash_table_insert(ht, ray_query, vars);
}

static void
copy_candidate_to_closest(nir_builder *b, nir_ssa_def *index, struct ray_query_vars *vars)
{
   rq_copy_var(b, index, vars->closest.barycentrics, vars->candidate.barycentrics, 0x3);
   rq_copy_var(b, index, vars->closest.custom_instance_and_mask,
               vars->candidate.custom_instance_and_mask, 0x1);
   rq_copy_var(b, index, vars->closest.geometry_id_and_flags, vars->candidate.geometry_id_and_flags,
               0x1);
   rq_copy_var(b, index, vars->closest.instance_addr, vars->candidate.instance_addr, 0x1);
   rq_copy_var(b, index, vars->closest.instance_id, vars->candidate.instance_id, 0x1);
   rq_copy_var(b, index, vars->closest.intersection_type, vars->candidate.intersection_type, 0x1);
   rq_copy_var(b, index, vars->closest.opaque, vars->candidate.opaque, 0x1);
   rq_copy_var(b, index, vars->closest.frontface, vars->candidate.frontface, 0x1);
   rq_copy_var(b, index, vars->closest.sbt_offset_and_flags, vars->candidate.sbt_offset_and_flags,
               0x1);
   rq_copy_var(b, index, vars->closest.primitive_id, vars->candidate.primitive_id, 0x1);
   rq_copy_var(b, index, vars->closest.t, vars->candidate.t, 0x1);
}

static void
insert_terminate_on_first_hit(nir_builder *b, nir_ssa_def *index, struct ray_query_vars *vars,
                              bool break_on_terminate)
{
   nir_ssa_def *terminate_on_first_hit =
      nir_test_mask(b, rq_load_var(b, index, vars->flags), SpvRayFlagsTerminateOnFirstHitKHRMask);
   nir_push_if(b, terminate_on_first_hit);
   {
      rq_store_var(b, index, vars->incomplete, nir_imm_bool(b, false), 0x1);
      if (break_on_terminate)
         nir_jump(b, nir_jump_break);
   }
   nir_pop_if(b, NULL);
}

static void
lower_rq_confirm_intersection(nir_builder *b, nir_ssa_def *index, nir_intrinsic_instr *instr,
                              struct ray_query_vars *vars)
{
   copy_candidate_to_closest(b, index, vars);
   insert_terminate_on_first_hit(b, index, vars, false);
}

static void
lower_rq_generate_intersection(nir_builder *b, nir_ssa_def *index, nir_intrinsic_instr *instr,
                               struct ray_query_vars *vars)
{
   nir_push_if(b, nir_iand(b, nir_fge(b, rq_load_var(b, index, vars->closest.t), instr->src[1].ssa),
                           nir_fge(b, instr->src[1].ssa, rq_load_var(b, index, vars->tmin))));
   {
      copy_candidate_to_closest(b, index, vars);
      insert_terminate_on_first_hit(b, index, vars, false);
      rq_store_var(b, index, vars->closest.t, instr->src[1].ssa, 0x1);
   }
   nir_pop_if(b, NULL);
}

enum rq_intersection_type {
   intersection_type_none,
   intersection_type_triangle,
   intersection_type_aabb
};

static void
lower_rq_initialize(nir_builder *b, nir_ssa_def *index, nir_intrinsic_instr *instr,
                    struct ray_query_vars *vars)
{
   rq_store_var(b, index, vars->accel_struct, instr->src[1].ssa, 0x1);
   rq_store_var(b, index, vars->flags, instr->src[2].ssa, 0x1);
   rq_store_var(b, index, vars->cull_mask, nir_iand_imm(b, instr->src[3].ssa, 0xff), 0x1);

   rq_store_var(b, index, vars->origin, instr->src[4].ssa, 0x7);
   rq_store_var(b, index, vars->trav.origin, instr->src[4].ssa, 0x7);

   rq_store_var(b, index, vars->tmin, instr->src[5].ssa, 0x1);

   rq_store_var(b, index, vars->direction, instr->src[6].ssa, 0x7);
   rq_store_var(b, index, vars->trav.direction, instr->src[6].ssa, 0x7);

   nir_ssa_def *vec3ones = nir_channels(b, nir_imm_vec4(b, 1.0, 1.0, 1.0, 1.0), 0x7);
   rq_store_var(b, index, vars->trav.inv_dir, nir_fdiv(b, vec3ones, instr->src[6].ssa), 0x7);

   rq_store_var(b, index, vars->closest.t, instr->src[7].ssa, 0x1);
   rq_store_var(b, index, vars->closest.intersection_type, nir_imm_int(b, intersection_type_none),
                0x1);

   nir_ssa_def *accel_struct = rq_load_var(b, index, vars->accel_struct);

   nir_push_if(b, nir_ine_imm(b, accel_struct, 0));
   {
      rq_store_var(b, index, vars->trav.bvh_base, build_addr_to_node(b, accel_struct), 1);

      nir_ssa_def *bvh_root =
         nir_build_load_global(b, 1, 32, accel_struct, .access = ACCESS_NON_WRITEABLE,
                               .align_mul = 64, .align_offset = 0);

      rq_store_var(b, index, vars->trav.stack, nir_imm_int(b, 1), 0x1);
      rq_store_array(b, index, vars->stack, nir_imm_int(b, 0), bvh_root, 0x1);

      rq_store_var(b, index, vars->trav.top_stack, nir_imm_int(b, 0), 1);

      rq_store_var(b, index, vars->incomplete, nir_imm_bool(b, true), 0x1);
   }
   nir_push_else(b, NULL);
   {
      rq_store_var(b, index, vars->incomplete, nir_imm_bool(b, false), 0x1);
   }
   nir_pop_if(b, NULL);
}

static nir_ssa_def *
lower_rq_load(nir_builder *b, nir_ssa_def *index, struct ray_query_vars *vars,
              nir_ssa_def *committed, nir_ray_query_value value, unsigned column)
{
   switch (value) {
   case nir_ray_query_value_flags:
      return rq_load_var(b, index, vars->flags);
   case nir_ray_query_value_intersection_barycentrics:
      return nir_bcsel(b, committed, rq_load_var(b, index, vars->closest.barycentrics),
                       rq_load_var(b, index, vars->candidate.barycentrics));
   case nir_ray_query_value_intersection_candidate_aabb_opaque:
      return nir_iand(b, rq_load_var(b, index, vars->candidate.opaque),
                      nir_ieq_imm(b, rq_load_var(b, index, vars->candidate.intersection_type),
                                  intersection_type_aabb));
   case nir_ray_query_value_intersection_front_face:
      return nir_bcsel(b, committed, rq_load_var(b, index, vars->closest.frontface),
                       rq_load_var(b, index, vars->candidate.frontface));
   case nir_ray_query_value_intersection_geometry_index:
      return nir_iand_imm(
         b,
         nir_bcsel(b, committed, rq_load_var(b, index, vars->closest.geometry_id_and_flags),
                   rq_load_var(b, index, vars->candidate.geometry_id_and_flags)),
         0xFFFFFF);
   case nir_ray_query_value_intersection_instance_custom_index:
      return nir_iand_imm(
         b,
         nir_bcsel(b, committed, rq_load_var(b, index, vars->closest.custom_instance_and_mask),
                   rq_load_var(b, index, vars->candidate.custom_instance_and_mask)),
         0xFFFFFF);
   case nir_ray_query_value_intersection_instance_id:
      return nir_bcsel(b, committed, rq_load_var(b, index, vars->closest.instance_id),
                       rq_load_var(b, index, vars->candidate.instance_id));
   case nir_ray_query_value_intersection_instance_sbt_index:
      return nir_iand_imm(
         b,
         nir_bcsel(b, committed, rq_load_var(b, index, vars->closest.sbt_offset_and_flags),
                   rq_load_var(b, index, vars->candidate.sbt_offset_and_flags)),
         0xFFFFFF);
   case nir_ray_query_value_intersection_object_ray_direction: {
      nir_ssa_def *instance_node_addr =
         nir_bcsel(b, committed, rq_load_var(b, index, vars->closest.instance_addr),
                   rq_load_var(b, index, vars->candidate.instance_addr));
      nir_ssa_def *wto_matrix[3];
      nir_build_wto_matrix_load(b, instance_node_addr, wto_matrix);
      return nir_build_vec3_mat_mult(b, rq_load_var(b, index, vars->direction), wto_matrix, false);
   }
   case nir_ray_query_value_intersection_object_ray_origin: {
      nir_ssa_def *instance_node_addr =
         nir_bcsel(b, committed, rq_load_var(b, index, vars->closest.instance_addr),
                   rq_load_var(b, index, vars->candidate.instance_addr));
      nir_ssa_def *wto_matrix[] = {
         nir_build_load_global(b, 4, 32, nir_iadd_imm(b, instance_node_addr, 16), .align_mul = 64,
                               .align_offset = 16),
         nir_build_load_global(b, 4, 32, nir_iadd_imm(b, instance_node_addr, 32), .align_mul = 64,
                               .align_offset = 32),
         nir_build_load_global(b, 4, 32, nir_iadd_imm(b, instance_node_addr, 48), .align_mul = 64,
                               .align_offset = 48)};
      return nir_build_vec3_mat_mult_pre(b, rq_load_var(b, index, vars->origin), wto_matrix);
   }
   case nir_ray_query_value_intersection_object_to_world: {
      nir_ssa_def *instance_node_addr =
         nir_bcsel(b, committed, rq_load_var(b, index, vars->closest.instance_addr),
                   rq_load_var(b, index, vars->candidate.instance_addr));

      if (column == 3) {
         nir_ssa_def *wto_matrix[3];
         nir_build_wto_matrix_load(b, instance_node_addr, wto_matrix);

         nir_ssa_def *vals[3];
         for (unsigned i = 0; i < 3; ++i)
            vals[i] = nir_channel(b, wto_matrix[i], column);

         return nir_vec(b, vals, 3);
      }

      return nir_build_load_global(b, 3, 32, nir_iadd_imm(b, instance_node_addr, 92 + column * 12));
   }
   case nir_ray_query_value_intersection_primitive_index:
      return nir_bcsel(b, committed, rq_load_var(b, index, vars->closest.primitive_id),
                       rq_load_var(b, index, vars->candidate.primitive_id));
   case nir_ray_query_value_intersection_t:
      return nir_bcsel(b, committed, rq_load_var(b, index, vars->closest.t),
                       rq_load_var(b, index, vars->candidate.t));
   case nir_ray_query_value_intersection_type:
      return nir_bcsel(
         b, committed, rq_load_var(b, index, vars->closest.intersection_type),
         nir_iadd_imm(b, rq_load_var(b, index, vars->candidate.intersection_type), -1));
   case nir_ray_query_value_intersection_world_to_object: {
      nir_ssa_def *instance_node_addr =
         nir_bcsel(b, committed, rq_load_var(b, index, vars->closest.instance_addr),
                   rq_load_var(b, index, vars->candidate.instance_addr));

      nir_ssa_def *wto_matrix[3];
      nir_build_wto_matrix_load(b, instance_node_addr, wto_matrix);

      nir_ssa_def *vals[3];
      for (unsigned i = 0; i < 3; ++i)
         vals[i] = nir_channel(b, wto_matrix[i], column);

      if (column == 3)
         return nir_fneg(b, nir_build_vec3_mat_mult(b, nir_vec(b, vals, 3), wto_matrix, false));

      return nir_vec(b, vals, 3);
   }
   case nir_ray_query_value_tmin:
      return rq_load_var(b, index, vars->tmin);
   case nir_ray_query_value_world_ray_direction:
      return rq_load_var(b, index, vars->direction);
   case nir_ray_query_value_world_ray_origin:
      return rq_load_var(b, index, vars->origin);
   default:
      unreachable("Invalid nir_ray_query_value!");
   }

   return NULL;
}

static void
insert_traversal_triangle_case(struct radv_device *device, nir_builder *b, nir_ssa_def *index,
                               nir_ssa_def *result, struct ray_query_vars *vars,
                               nir_ssa_def *bvh_node)
{
   nir_ssa_def *dist = nir_channel(b, result, 0);
   nir_ssa_def *div = nir_channel(b, result, 1);
   dist = nir_fdiv(b, dist, div);
   nir_ssa_def *frontface = nir_flt(b, nir_imm_float(b, 0), div);
   nir_ssa_def *switch_ccw =
      nir_test_mask(b, rq_load_var(b, index, vars->candidate.sbt_offset_and_flags),
                    VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR << 24);
   frontface = nir_ixor(b, frontface, switch_ccw);
   rq_store_var(b, index, vars->candidate.frontface, frontface, 0x1);

   nir_ssa_def *not_cull = nir_inot(
      b, nir_test_mask(b, rq_load_var(b, index, vars->flags), SpvRayFlagsSkipTrianglesKHRMask));
   nir_ssa_def *not_facing_cull = nir_ieq_imm(
      b,
      nir_iand(b, rq_load_var(b, index, vars->flags),
               nir_bcsel(b, frontface, nir_imm_int(b, SpvRayFlagsCullFrontFacingTrianglesKHRMask),
                         nir_imm_int(b, SpvRayFlagsCullBackFacingTrianglesKHRMask))),
      0);

   not_cull = nir_iand(
      b, not_cull,
      nir_ior(b, not_facing_cull,
              nir_test_mask(b, rq_load_var(b, index, vars->candidate.sbt_offset_and_flags),
                            VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR << 24)));

   nir_push_if(b, nir_iand(b,
                           nir_iand(b, nir_fge(b, rq_load_var(b, index, vars->closest.t), dist),
                                    nir_fge(b, dist, rq_load_var(b, index, vars->tmin))),
                           not_cull));
   {
      nir_ssa_def *triangle_info =
         nir_build_load_global(b, 2, 32,
                               nir_iadd_imm(b, build_node_to_addr(device, b, bvh_node),
                                            offsetof(struct radv_bvh_triangle_node, triangle_id)));
      nir_ssa_def *primitive_id = nir_channel(b, triangle_info, 0);
      nir_ssa_def *geometry_id_and_flags = nir_channel(b, triangle_info, 1);
      nir_ssa_def *is_opaque =
         hit_is_opaque(b, rq_load_var(b, index, vars->candidate.sbt_offset_and_flags),
                       rq_load_var(b, index, vars->flags), geometry_id_and_flags);

      not_cull =
         nir_ieq_imm(b,
                     nir_iand(b, rq_load_var(b, index, vars->flags),
                              nir_bcsel(b, is_opaque, nir_imm_int(b, SpvRayFlagsCullOpaqueKHRMask),
                                        nir_imm_int(b, SpvRayFlagsCullNoOpaqueKHRMask))),
                     0);
      nir_push_if(b, not_cull);
      {
         nir_ssa_def *divs[2] = {div, div};
         nir_ssa_def *ij = nir_fdiv(b, nir_channels(b, result, 0xc), nir_vec(b, divs, 2));

         rq_store_var(b, index, vars->candidate.barycentrics, ij, 3);
         rq_store_var(b, index, vars->candidate.primitive_id, primitive_id, 1);
         rq_store_var(b, index, vars->candidate.geometry_id_and_flags, geometry_id_and_flags, 1);
         rq_store_var(b, index, vars->candidate.t, dist, 0x1);
         rq_store_var(b, index, vars->candidate.opaque, is_opaque, 0x1);
         rq_store_var(b, index, vars->candidate.intersection_type,
                      nir_imm_int(b, intersection_type_triangle), 0x1);

         nir_push_if(b, is_opaque);
         {
            copy_candidate_to_closest(b, index, vars);
            insert_terminate_on_first_hit(b, index, vars, true);
         }
         nir_push_else(b, NULL);
         {
            nir_jump(b, nir_jump_break);
         }
         nir_pop_if(b, NULL);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static void
insert_traversal_aabb_case(struct radv_device *device, nir_builder *b, nir_ssa_def *index,
                           struct ray_query_vars *vars, nir_ssa_def *bvh_node)
{
   nir_ssa_def *node_addr = build_node_to_addr(device, b, bvh_node);
   nir_ssa_def *triangle_info = nir_build_load_global(b, 2, 32, nir_iadd_imm(b, node_addr, 24));
   nir_ssa_def *primitive_id = nir_channel(b, triangle_info, 0);
   nir_ssa_def *geometry_id_and_flags = nir_channel(b, triangle_info, 1);
   nir_ssa_def *is_opaque =
      hit_is_opaque(b, rq_load_var(b, index, vars->candidate.sbt_offset_and_flags),
                    rq_load_var(b, index, vars->flags), geometry_id_and_flags);

   nir_ssa_def *not_skip_aabb = nir_inot(
      b, nir_test_mask(b, rq_load_var(b, index, vars->flags), SpvRayFlagsSkipAABBsKHRMask));
   nir_ssa_def *not_cull = nir_iand(
      b, not_skip_aabb,
      nir_ieq_imm(b,
                  nir_iand(b, rq_load_var(b, index, vars->flags),
                           nir_bcsel(b, is_opaque, nir_imm_int(b, SpvRayFlagsCullOpaqueKHRMask),
                                     nir_imm_int(b, SpvRayFlagsCullNoOpaqueKHRMask))),
                  0));
   nir_push_if(b, not_cull);
   {
      nir_ssa_def *vec3_zero = nir_channels(b, nir_imm_vec4(b, 0, 0, 0, 0), 0x7);
      nir_ssa_def *vec3_inf =
         nir_channels(b, nir_imm_vec4(b, INFINITY, INFINITY, INFINITY, 0), 0x7);

      nir_ssa_def *bvh_lo = nir_build_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, 0));
      nir_ssa_def *bvh_hi = nir_build_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, 12));

      bvh_lo = nir_fsub(b, bvh_lo, rq_load_var(b, index, vars->trav.origin));
      bvh_hi = nir_fsub(b, bvh_hi, rq_load_var(b, index, vars->trav.origin));
      nir_ssa_def *t_vec =
         nir_fmin(b, nir_fmul(b, bvh_lo, rq_load_var(b, index, vars->trav.inv_dir)),
                  nir_fmul(b, bvh_hi, rq_load_var(b, index, vars->trav.inv_dir)));
      nir_ssa_def *t2_vec =
         nir_fmax(b, nir_fmul(b, bvh_lo, rq_load_var(b, index, vars->trav.inv_dir)),
                  nir_fmul(b, bvh_hi, rq_load_var(b, index, vars->trav.inv_dir)));
      /* If we run parallel to one of the edges the range should be [0, inf) not [0,0] */
      t2_vec = nir_bcsel(b, nir_feq(b, rq_load_var(b, index, vars->trav.direction), vec3_zero),
                         vec3_inf, t2_vec);

      nir_ssa_def *t_min = nir_fmax(b, nir_channel(b, t_vec, 0), nir_channel(b, t_vec, 1));
      t_min = nir_fmax(b, t_min, nir_channel(b, t_vec, 2));

      nir_ssa_def *t_max = nir_fmin(b, nir_channel(b, t2_vec, 0), nir_channel(b, t2_vec, 1));
      t_max = nir_fmin(b, t_max, nir_channel(b, t2_vec, 2));

      nir_push_if(b, nir_iand(b, nir_fge(b, rq_load_var(b, index, vars->closest.t), t_min),
                              nir_fge(b, t_max, rq_load_var(b, index, vars->tmin))));
      {
         rq_store_var(b, index, vars->candidate.t,
                      nir_fmax(b, t_min, rq_load_var(b, index, vars->tmin)), 0x1);
         rq_store_var(b, index, vars->candidate.primitive_id, primitive_id, 1);
         rq_store_var(b, index, vars->candidate.geometry_id_and_flags, geometry_id_and_flags, 1);
         rq_store_var(b, index, vars->candidate.opaque, is_opaque, 0x1);
         rq_store_var(b, index, vars->candidate.intersection_type,
                      nir_imm_int(b, intersection_type_aabb), 0x1);

         nir_push_if(b, is_opaque);
         {
            copy_candidate_to_closest(b, index, vars);
         }
         nir_pop_if(b, NULL);

         nir_jump(b, nir_jump_break);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static nir_ssa_def *
lower_rq_proceed(nir_builder *b, nir_ssa_def *index, struct ray_query_vars *vars,
                 struct radv_device *device)
{
   nir_push_if(b, rq_load_var(b, index, vars->incomplete));
   {
      nir_ssa_def *desc = create_bvh_descriptor(b);
      nir_ssa_def *vec3ones = nir_channels(b, nir_imm_vec4(b, 1.0, 1.0, 1.0, 1.0), 0x7);

      nir_push_loop(b);
      {
         nir_push_if(b, nir_uge(b, rq_load_var(b, index, vars->trav.top_stack),
                                rq_load_var(b, index, vars->trav.stack)));
         {
            nir_push_if(b, nir_ieq_imm(b, rq_load_var(b, index, vars->trav.stack), 0));
            {
               rq_store_var(b, index, vars->incomplete, nir_imm_bool(b, false), 0x1);
               nir_jump(b, nir_jump_break);
            }
            nir_pop_if(b, NULL);

            rq_store_var(b, index, vars->trav.top_stack, nir_imm_int(b, 0), 1);
            rq_store_var(b, index, vars->trav.bvh_base,
                         build_addr_to_node(b, rq_load_var(b, index, vars->accel_struct)), 1);
            rq_store_var(b, index, vars->trav.origin, rq_load_var(b, index, vars->origin), 7);
            rq_store_var(b, index, vars->trav.direction, rq_load_var(b, index, vars->direction), 7);
            rq_store_var(b, index, vars->trav.inv_dir,
                         nir_fdiv(b, vec3ones, rq_load_var(b, index, vars->direction)), 7);
         }
         nir_pop_if(b, NULL);

         rq_store_var(b, index, vars->trav.stack,
                      nir_iadd_imm(b, rq_load_var(b, index, vars->trav.stack), -1), 1);

         nir_ssa_def *bvh_node =
            rq_load_array(b, index, vars->stack, rq_load_var(b, index, vars->trav.stack));
         nir_ssa_def *bvh_node_type = bvh_node;

         bvh_node =
            nir_iadd(b, rq_load_var(b, index, vars->trav.bvh_base), nir_u2u(b, bvh_node, 64));
         nir_ssa_def *intrinsic_result = NULL;
         if (!radv_emulate_rt(device->physical_device)) {
            intrinsic_result = nir_bvh64_intersect_ray_amd(
               b, 32, desc, nir_unpack_64_2x32(b, bvh_node), rq_load_var(b, index, vars->closest.t),
               rq_load_var(b, index, vars->trav.origin),
               rq_load_var(b, index, vars->trav.direction),
               rq_load_var(b, index, vars->trav.inv_dir));
         }

         /* if (node.type_flags & aabb) */
         nir_push_if(b, nir_ine_imm(b, nir_iand_imm(b, bvh_node_type, 4), 0));
         {
            /* if (node.type_flags & leaf) */
            nir_push_if(b, nir_ine_imm(b, nir_iand_imm(b, bvh_node_type, 2), 0));
            {
               /* custom */
               nir_push_if(b, nir_ine_imm(b, nir_iand_imm(b, bvh_node_type, 1), 0));
               {
                  insert_traversal_aabb_case(device, b, index, vars, bvh_node);
               }
               nir_push_else(b, NULL);
               {
                  /* instance */
                  nir_ssa_def *instance_node_addr = build_node_to_addr(device, b, bvh_node);
                  nir_ssa_def *instance_data = nir_build_load_global(
                     b, 4, 32, instance_node_addr, .align_mul = 64, .align_offset = 0);
                  nir_ssa_def *instance_and_mask = nir_channel(b, instance_data, 2);
                  nir_ssa_def *instance_mask = nir_ushr_imm(b, instance_and_mask, 24);

                  nir_push_if(
                     b,
                     nir_ieq_imm(
                        b, nir_iand(b, instance_mask, rq_load_var(b, index, vars->cull_mask)), 0));
                  {
                     nir_jump(b, nir_jump_continue);
                  }
                  nir_pop_if(b, NULL);

                  nir_ssa_def *wto_matrix[] = {
                     nir_build_load_global(b, 4, 32, nir_iadd_imm(b, instance_node_addr, 16),
                                           .align_mul = 64, .align_offset = 16),
                     nir_build_load_global(b, 4, 32, nir_iadd_imm(b, instance_node_addr, 32),
                                           .align_mul = 64, .align_offset = 32),
                     nir_build_load_global(b, 4, 32, nir_iadd_imm(b, instance_node_addr, 48),
                                           .align_mul = 64, .align_offset = 48)};
                  nir_ssa_def *instance_id =
                     nir_build_load_global(b, 1, 32, nir_iadd_imm(b, instance_node_addr, 88));

                  rq_store_var(b, index, vars->trav.top_stack,
                               rq_load_var(b, index, vars->trav.stack), 1);
                  rq_store_var(b, index, vars->trav.bvh_base,
                               build_addr_to_node(
                                  b, nir_pack_64_2x32(b, nir_channels(b, instance_data, 0x3))),
                               1);

                  rq_store_array(b, index, vars->stack, rq_load_var(b, index, vars->trav.stack),
                                 nir_iand_imm(b, nir_channel(b, instance_data, 0), 63), 0x1);
                  rq_store_var(b, index, vars->trav.stack,
                               nir_iadd_imm(b, rq_load_var(b, index, vars->trav.stack), 1), 1);

                  rq_store_var(b, index, vars->trav.origin,
                               nir_build_vec3_mat_mult_pre(b, rq_load_var(b, index, vars->origin),
                                                           wto_matrix),
                               7);
                  rq_store_var(b, index, vars->trav.direction,
                               nir_build_vec3_mat_mult(b, rq_load_var(b, index, vars->direction),
                                                       wto_matrix, false),
                               7);
                  rq_store_var(b, index, vars->trav.inv_dir,
                               nir_fdiv(b, vec3ones, rq_load_var(b, index, vars->trav.direction)),
                               7);

                  rq_store_var(b, index, vars->candidate.sbt_offset_and_flags,
                               nir_channel(b, instance_data, 3), 1);
                  rq_store_var(b, index, vars->candidate.custom_instance_and_mask,
                               instance_and_mask, 1);
                  rq_store_var(b, index, vars->candidate.instance_id, instance_id, 1);
                  rq_store_var(b, index, vars->candidate.instance_addr, instance_node_addr, 1);
               }
               nir_pop_if(b, NULL);
            }
            nir_push_else(b, NULL);
            {
               nir_ssa_def *result = intrinsic_result;
               if (!result) {
                  /* If we didn't run the intrinsic cause the hardware didn't support it,
                   * emulate ray/box intersection here */
                  result = intersect_ray_amd_software_box(
                     device, b, bvh_node, rq_load_var(b, index, vars->closest.t),
                     rq_load_var(b, index, vars->trav.origin),
                     rq_load_var(b, index, vars->trav.direction),
                     rq_load_var(b, index, vars->trav.inv_dir));
               }

               /* box */
               for (unsigned i = 4; i-- > 0;) {
                  nir_ssa_def *new_node = nir_channel(b, result, i);
                  nir_push_if(b, nir_ine_imm(b, new_node, 0xffffffff));
                  {
                     rq_store_array(b, index, vars->stack, rq_load_var(b, index, vars->trav.stack),
                                    new_node, 0x1);
                     rq_store_var(b, index, vars->trav.stack,
                                  nir_iadd_imm(b, rq_load_var(b, index, vars->trav.stack), 1), 1);
                  }
                  nir_pop_if(b, NULL);
               }
            }
            nir_pop_if(b, NULL);
         }
         nir_push_else(b, NULL);
         {
            nir_ssa_def *result = intrinsic_result;
            if (!result) {
               /* If we didn't run the intrinsic cause the hardware didn't support it,
                * emulate ray/tri intersection here */
               result = intersect_ray_amd_software_tri(device, b, bvh_node,
                                                       rq_load_var(b, index, vars->closest.t),
                                                       rq_load_var(b, index, vars->trav.origin),
                                                       rq_load_var(b, index, vars->trav.direction),
                                                       rq_load_var(b, index, vars->trav.inv_dir));
            }
            insert_traversal_triangle_case(device, b, index, result, vars, bvh_node);
         }
         nir_pop_if(b, NULL);
      }
      nir_pop_loop(b, NULL);
   }
   nir_pop_if(b, NULL);

   return rq_load_var(b, index, vars->incomplete);
}

static void
lower_rq_terminate(nir_builder *b, nir_ssa_def *index, nir_intrinsic_instr *instr,
                   struct ray_query_vars *vars)
{
   rq_store_var(b, index, vars->incomplete, nir_imm_bool(b, false), 0x1);
}

static bool
is_rq_intrinsic(nir_intrinsic_op intrinsic)
{
   switch (intrinsic) {
   case nir_intrinsic_rq_confirm_intersection:
   case nir_intrinsic_rq_generate_intersection:
   case nir_intrinsic_rq_initialize:
   case nir_intrinsic_rq_load:
   case nir_intrinsic_rq_proceed:
   case nir_intrinsic_rq_terminate:
      return true;
   default:
      return false;
   }
}

bool
radv_nir_lower_ray_queries(struct nir_shader *shader, struct radv_device *device)
{
   bool contains_ray_query = false;
   struct hash_table *query_ht = _mesa_pointer_hash_table_create(NULL);

   nir_foreach_variable_in_list (var, &shader->variables) {
      if (!var->data.ray_query)
         continue;

      lower_ray_query(shader, NULL, var, query_ht);
      contains_ray_query = true;
   }

   nir_foreach_function (function, shader) {
      if (!function->impl)
         continue;

      nir_builder builder;
      nir_builder_init(&builder, function->impl);

      nir_foreach_variable_in_list (var, &function->impl->locals) {
         if (!var->data.ray_query)
            continue;

         lower_ray_query(NULL, function->impl, var, query_ht);
         contains_ray_query = true;
      }

      if (!contains_ray_query)
         continue;

      nir_foreach_block (block, function->impl) {
         nir_foreach_instr_safe (instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);

            if (!is_rq_intrinsic(intrinsic->intrinsic))
               continue;

            nir_deref_instr *ray_query_deref =
               nir_instr_as_deref(intrinsic->src[0].ssa->parent_instr);
            nir_ssa_def *index = NULL;

            if (ray_query_deref->deref_type == nir_deref_type_array) {
               index = ray_query_deref->arr.index.ssa;
               ray_query_deref = nir_instr_as_deref(ray_query_deref->parent.ssa->parent_instr);
            }

            assert(ray_query_deref->deref_type == nir_deref_type_var);

            struct ray_query_vars *vars =
               (struct ray_query_vars *)_mesa_hash_table_search(query_ht, ray_query_deref->var)
                  ->data;

            builder.cursor = nir_before_instr(instr);

            nir_ssa_def *new_dest = NULL;

            switch (intrinsic->intrinsic) {
            case nir_intrinsic_rq_confirm_intersection:
               lower_rq_confirm_intersection(&builder, index, intrinsic, vars);
               break;
            case nir_intrinsic_rq_generate_intersection:
               lower_rq_generate_intersection(&builder, index, intrinsic, vars);
               break;
            case nir_intrinsic_rq_initialize:
               lower_rq_initialize(&builder, index, intrinsic, vars);
               break;
            case nir_intrinsic_rq_load:
               new_dest = lower_rq_load(&builder, index, vars, intrinsic->src[1].ssa,
                                        (nir_ray_query_value)nir_intrinsic_base(intrinsic),
                                        nir_intrinsic_column(intrinsic));
               break;
            case nir_intrinsic_rq_proceed:
               new_dest = lower_rq_proceed(&builder, index, vars, device);
               break;
            case nir_intrinsic_rq_terminate:
               lower_rq_terminate(&builder, index, intrinsic, vars);
               break;
            default:
               unreachable("Unsupported ray query intrinsic!");
            }

            if (new_dest)
               nir_ssa_def_rewrite_uses(&intrinsic->dest.ssa, new_dest);

            nir_instr_remove(instr);
            nir_instr_free(instr);
         }
      }

      nir_metadata_preserve(function->impl, nir_metadata_none);
   }

   ralloc_free(query_ht);

   return contains_ray_query;
}
