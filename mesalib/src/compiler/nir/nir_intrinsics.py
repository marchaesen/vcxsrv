#
# Copyright (C) 2018 Red Hat
# Copyright (C) 2014 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#

# This file defines all the available intrinsics in one place.
#
# The Intrinsic class corresponds one-to-one with nir_intrinsic_info
# structure.

class Intrinsic(object):
   """Class that represents all the information about an intrinsic opcode.
   NOTE: this must be kept in sync with nir_intrinsic_info.
   """
   def __init__(self, name, src_components, dest_components, num_variables,
                indices, flags, sysval):
       """Parameters:

       - name: the intrinsic name
       - src_components: list of the number of components per src, 0 means
         vectorized instruction with number of components given in the
         num_components field in nir_intrinsic_instr.
       - dest_components: number of destination components, -1 means no
         dest, 0 means number of components given in num_components field
         in nir_intrinsic_instr.
       - num_variables: the number of variables
       - indices: list of constant indicies
       - flags: list of semantic flags
       - sysval: is this a system-value intrinsic
       """
       assert isinstance(name, str)
       assert isinstance(src_components, list)
       if src_components:
           assert isinstance(src_components[0], int)
       assert isinstance(dest_components, int)
       assert isinstance(num_variables, int)
       assert isinstance(indices, list)
       if indices:
           assert isinstance(indices[0], str)
       assert isinstance(flags, list)
       if flags:
           assert isinstance(flags[0], str)
       assert isinstance(sysval, bool)

       self.name = name
       self.num_srcs = len(src_components)
       self.src_components = src_components
       self.has_dest = (dest_components >= 0)
       self.dest_components = dest_components
       self.num_variables = num_variables
       self.num_indices = len(indices)
       self.indices = indices
       self.flags = flags
       self.sysval = sysval

#
# Possible indices:
#

# A constant 'base' value that is added to an offset src:
BASE = "NIR_INTRINSIC_BASE"
# For store instructions, a writemask:
WRMASK = "NIR_INTRINSIC_WRMASK"
# The stream-id for GS emit_vertex/end_primitive intrinsics:
STREAM_ID = "NIR_INTRINSIC_STREAM_ID"
# The clip-plane id for load_user_clip_plane intrinsics:
UCP_ID = "NIR_INTRINSIC_UCP_ID"
# The amount of data, starting from BASE, that this instruction
# may access.  This is used to provide bounds if the offset is
# not constant.
RANGE = "NIR_INTRINSIC_RANGE"
# The vulkan descriptor set binding for vulkan_resource_index
# intrinsic
DESC_SET = "NIR_INTRINSIC_DESC_SET"
# The vulkan descriptor set binding for vulkan_resource_index
# intrinsic
BINDING = "NIR_INTRINSIC_BINDING"
# Component offset
COMPONENT = "NIR_INTRINSIC_COMPONENT"
# Interpolation mode (only meaningful for FS inputs)
INTERP_MODE = "NIR_INTRINSIC_INTERP_MODE"
# A binary nir_op to use when performing a reduction or scan operation
REDUCTION_OP = "NIR_INTRINSIC_REDUCTION_OP"
# Cluster size for reduction operations
CLUSTER_SIZE = "NIR_INTRINSIC_CLUSTER_SIZE"

#
# Possible flags:
#

CAN_ELIMINATE = "NIR_INTRINSIC_CAN_ELIMINATE"
CAN_REORDER   = "NIR_INTRINSIC_CAN_REORDER"

INTR_OPCODES = {}

def intrinsic(name, src_comp=[], dest_comp=-1, num_vars=0, indices=[],
              flags=[], sysval=False):
    assert name not in INTR_OPCODES
    INTR_OPCODES[name] = Intrinsic(name, src_comp, dest_comp, num_vars,
                                   indices, flags, sysval)

intrinsic("nop", flags=[CAN_ELIMINATE])

intrinsic("load_var", dest_comp=0, num_vars=1, flags=[CAN_ELIMINATE])
intrinsic("store_var", src_comp=[0], num_vars=1, indices=[WRMASK])
intrinsic("copy_var", num_vars=2)

# Interpolation of input.  The interp_var_at* intrinsics are similar to the
# load_var intrinsic acting on a shader input except that they interpolate
# the input differently.  The at_sample and at_offset intrinsics take an
# additional source that is an integer sample id or a vec2 position offset
# respectively.

intrinsic("interp_var_at_centroid", dest_comp=0, num_vars=1,
          flags=[ CAN_ELIMINATE, CAN_REORDER])
intrinsic("interp_var_at_sample", src_comp=[1], dest_comp=0, num_vars=1,
          flags=[CAN_ELIMINATE, CAN_REORDER])
intrinsic("interp_var_at_offset", src_comp=[2], dest_comp=0, num_vars=1,
          flags=[CAN_ELIMINATE, CAN_REORDER])

# Ask the driver for the size of a given buffer. It takes the buffer index
# as source.
intrinsic("get_buffer_size", src_comp=[1], dest_comp=1,
          flags=[CAN_ELIMINATE, CAN_REORDER])

# a barrier is an intrinsic with no inputs/outputs but which can't be moved
# around/optimized in general
def barrier(name):
    intrinsic(name)

barrier("barrier")
barrier("discard")

# Memory barrier with semantics analogous to the memoryBarrier() GLSL
# intrinsic.
barrier("memory_barrier")

# Shader clock intrinsic with semantics analogous to the clock2x32ARB()
# GLSL intrinsic.
# The latter can be used as code motion barrier, which is currently not
# feasible with NIR.
intrinsic("shader_clock", dest_comp=2, flags=[CAN_ELIMINATE])

# Shader ballot intrinsics with semantics analogous to the
#
#    ballotARB()
#    readInvocationARB()
#    readFirstInvocationARB()
#
# GLSL functions from ARB_shader_ballot.
intrinsic("ballot", src_comp=[1], dest_comp=0, flags=[CAN_ELIMINATE])
intrinsic("read_invocation", src_comp=[0, 1], dest_comp=0, flags=[CAN_ELIMINATE])
intrinsic("read_first_invocation", src_comp=[0], dest_comp=0, flags=[CAN_ELIMINATE])

# Additional SPIR-V ballot intrinsics
#
# These correspond to the SPIR-V opcodes
#
#    OpGroupUniformElect
#    OpSubgroupFirstInvocationKHR
intrinsic("elect", dest_comp=1, flags=[CAN_ELIMINATE])
intrinsic("first_invocation", dest_comp=1, flags=[CAN_ELIMINATE])

# Memory barrier with semantics analogous to the compute shader
# groupMemoryBarrier(), memoryBarrierAtomicCounter(), memoryBarrierBuffer(),
# memoryBarrierImage() and memoryBarrierShared() GLSL intrinsics.
barrier("group_memory_barrier")
barrier("memory_barrier_atomic_counter")
barrier("memory_barrier_buffer")
barrier("memory_barrier_image")
barrier("memory_barrier_shared")
barrier("begin_invocation_interlock")
barrier("end_invocation_interlock")

# A conditional discard, with a single boolean source.
intrinsic("discard_if", src_comp=[1])

# ARB_shader_group_vote intrinsics
intrinsic("vote_any", src_comp=[1], dest_comp=1, flags=[CAN_ELIMINATE])
intrinsic("vote_all", src_comp=[1], dest_comp=1, flags=[CAN_ELIMINATE])
intrinsic("vote_feq", src_comp=[0], dest_comp=1, flags=[CAN_ELIMINATE])
intrinsic("vote_ieq", src_comp=[0], dest_comp=1, flags=[CAN_ELIMINATE])

# Ballot ALU operations from SPIR-V.
#
# These operations work like their ALU counterparts except that the operate
# on a uvec4 which is treated as a 128bit integer.  Also, they are, in
# general, free to ignore any bits which are above the subgroup size.
intrinsic("ballot_bitfield_extract", src_comp=[4, 1], dest_comp=1, flags=[CAN_ELIMINATE])
intrinsic("ballot_bit_count_reduce", src_comp=[4], dest_comp=1, flags=[CAN_ELIMINATE])
intrinsic("ballot_bit_count_inclusive", src_comp=[4], dest_comp=1, flags=[CAN_ELIMINATE])
intrinsic("ballot_bit_count_exclusive", src_comp=[4], dest_comp=1, flags=[CAN_ELIMINATE])
intrinsic("ballot_find_lsb", src_comp=[4], dest_comp=1, flags=[CAN_ELIMINATE])
intrinsic("ballot_find_msb", src_comp=[4], dest_comp=1, flags=[CAN_ELIMINATE])

# Shuffle operations from SPIR-V.
intrinsic("shuffle", src_comp=[0, 1], dest_comp=0, flags=[CAN_ELIMINATE])
intrinsic("shuffle_xor", src_comp=[0, 1], dest_comp=0, flags=[CAN_ELIMINATE])
intrinsic("shuffle_up", src_comp=[0, 1], dest_comp=0, flags=[CAN_ELIMINATE])
intrinsic("shuffle_down", src_comp=[0, 1], dest_comp=0, flags=[CAN_ELIMINATE])

# Quad operations from SPIR-V.
intrinsic("quad_broadcast", src_comp=[0, 1], dest_comp=0, flags=[CAN_ELIMINATE])
intrinsic("quad_swap_horizontal", src_comp=[0], dest_comp=0, flags=[CAN_ELIMINATE])
intrinsic("quad_swap_vertical", src_comp=[0], dest_comp=0, flags=[CAN_ELIMINATE])
intrinsic("quad_swap_diagonal", src_comp=[0], dest_comp=0, flags=[CAN_ELIMINATE])

intrinsic("reduce", src_comp=[0], dest_comp=0, indices=[REDUCTION_OP, CLUSTER_SIZE],
          flags=[CAN_ELIMINATE])
intrinsic("inclusive_scan", src_comp=[0], dest_comp=0, indices=[REDUCTION_OP],
          flags=[CAN_ELIMINATE])
intrinsic("exclusive_scan", src_comp=[0], dest_comp=0, indices=[REDUCTION_OP],
          flags=[CAN_ELIMINATE])

# Basic Geometry Shader intrinsics.
#
# emit_vertex implements GLSL's EmitStreamVertex() built-in.  It takes a single
# index, which is the stream ID to write to.
#
# end_primitive implements GLSL's EndPrimitive() built-in.
intrinsic("emit_vertex",   indices=[STREAM_ID])
intrinsic("end_primitive", indices=[STREAM_ID])

# Geometry Shader intrinsics with a vertex count.
#
# Alternatively, drivers may implement these intrinsics, and use
# nir_lower_gs_intrinsics() to convert from the basic intrinsics.
#
# These maintain a count of the number of vertices emitted, as an additional
# unsigned integer source.
intrinsic("emit_vertex_with_counter", src_comp=[1], indices=[STREAM_ID])
intrinsic("end_primitive_with_counter", src_comp=[1], indices=[STREAM_ID])
intrinsic("set_vertex_count", src_comp=[1])

# Atomic counters
#
# The *_var variants take an atomic_uint nir_variable, while the other,
# lowered, variants take a constant buffer index and register offset.

def atomic(name, flags=[]):
    intrinsic(name + "_var", dest_comp=1, num_vars=1, flags=flags)
    intrinsic(name, src_comp=[1], dest_comp=1, indices=[BASE], flags=flags)

def atomic2(name):
    intrinsic(name + "_var", src_comp=[1], dest_comp=1, num_vars=1)
    intrinsic(name, src_comp=[1, 1], dest_comp=1, indices=[BASE])

def atomic3(name):
    intrinsic(name + "_var", src_comp=[1, 1], dest_comp=1, num_vars=1)
    intrinsic(name, src_comp=[1, 1, 1], dest_comp=1, indices=[BASE])

atomic("atomic_counter_inc")
atomic("atomic_counter_dec")
atomic("atomic_counter_read", flags=[CAN_ELIMINATE])
atomic2("atomic_counter_add")
atomic2("atomic_counter_min")
atomic2("atomic_counter_max")
atomic2("atomic_counter_and")
atomic2("atomic_counter_or")
atomic2("atomic_counter_xor")
atomic2("atomic_counter_exchange")
atomic3("atomic_counter_comp_swap")

# Image load, store and atomic intrinsics.
#
# All image intrinsics take an image target passed as a nir_variable.  Image
# variables contain a number of memory and layout qualifiers that influence
# the semantics of the intrinsic.
#
# All image intrinsics take a four-coordinate vector and a sample index as
# first two sources, determining the location within the image that will be
# accessed by the intrinsic.  Components not applicable to the image target
# in use are undefined.  Image store takes an additional four-component
# argument with the value to be written, and image atomic operations take
# either one or two additional scalar arguments with the same meaning as in
# the ARB_shader_image_load_store specification.
intrinsic("image_var_load", src_comp=[4, 1], dest_comp=4, num_vars=1,
          flags=[CAN_ELIMINATE])
intrinsic("image_var_store", src_comp=[4, 1, 4], num_vars=1)
intrinsic("image_var_atomic_add",  src_comp=[4, 1, 1], dest_comp=1, num_vars=1)
intrinsic("image_var_atomic_min",  src_comp=[4, 1, 1], dest_comp=1, num_vars=1)
intrinsic("image_var_atomic_max",  src_comp=[4, 1, 1], dest_comp=1, num_vars=1)
intrinsic("image_var_atomic_and",  src_comp=[4, 1, 1], dest_comp=1, num_vars=1)
intrinsic("image_var_atomic_or",   src_comp=[4, 1, 1], dest_comp=1, num_vars=1)
intrinsic("image_var_atomic_xor",  src_comp=[4, 1, 1], dest_comp=1, num_vars=1)
intrinsic("image_var_atomic_exchange",  src_comp=[4, 1, 1], dest_comp=1, num_vars=1)
intrinsic("image_var_atomic_comp_swap", src_comp=[4, 1, 1, 1], dest_comp=1, num_vars=1)
intrinsic("image_var_size",    dest_comp=0, num_vars=1, flags=[CAN_ELIMINATE, CAN_REORDER])
intrinsic("image_var_samples", dest_comp=1, num_vars=1, flags=[CAN_ELIMINATE, CAN_REORDER])

# Vulkan descriptor set intrinsics
#
# The Vulkan API uses a different binding model from GL.  In the Vulkan
# API, all external resources are represented by a tuple:
#
# (descriptor set, binding, array index)
#
# where the array index is the only thing allowed to be indirect.  The
# vulkan_surface_index intrinsic takes the descriptor set and binding as
# its first two indices and the array index as its source.  The third
# index is a nir_variable_mode in case that's useful to the backend.
#
# The intended usage is that the shader will call vulkan_surface_index to
# get an index and then pass that as the buffer index ubo/ssbo calls.
#
# The vulkan_resource_reindex intrinsic takes a resource index in src0
# (the result of a vulkan_resource_index or vulkan_resource_reindex) which
# corresponds to the tuple (set, binding, index) and computes an index
# corresponding to tuple (set, binding, idx + src1).
intrinsic("vulkan_resource_index", src_comp=[1], dest_comp=1,
          indices=[DESC_SET, BINDING], flags=[CAN_ELIMINATE, CAN_REORDER])
intrinsic("vulkan_resource_reindex", src_comp=[1, 1], dest_comp=1,
          flags=[CAN_ELIMINATE, CAN_REORDER])

# variable atomic intrinsics
#
# All of these variable atomic memory operations read a value from memory,
# compute a new value using one of the operations below, write the new value
# to memory, and return the original value read.
#
# All operations take 1 source except CompSwap that takes 2. These sources
# represent:
#
# 0: The data parameter to the atomic function (i.e. the value to add
#    in shared_atomic_add, etc).
# 1: For CompSwap only: the second data parameter.
#
# All operations take 1 variable deref.
intrinsic("var_atomic_add",  src_comp=[1], dest_comp=1, num_vars=1)
intrinsic("var_atomic_imin", src_comp=[1], dest_comp=1, num_vars=1)
intrinsic("var_atomic_umin", src_comp=[1], dest_comp=1, num_vars=1)
intrinsic("var_atomic_imax", src_comp=[1], dest_comp=1, num_vars=1)
intrinsic("var_atomic_umax", src_comp=[1], dest_comp=1, num_vars=1)
intrinsic("var_atomic_and",  src_comp=[1], dest_comp=1, num_vars=1)
intrinsic("var_atomic_or",   src_comp=[1], dest_comp=1, num_vars=1)
intrinsic("var_atomic_xor",  src_comp=[1], dest_comp=1, num_vars=1)
intrinsic("var_atomic_exchange", src_comp=[1], dest_comp=1, num_vars=1)
intrinsic("var_atomic_comp_swap", src_comp=[1, 1], dest_comp=1, num_vars=1)

# SSBO atomic intrinsics
#
# All of the SSBO atomic memory operations read a value from memory,
# compute a new value using one of the operations below, write the new
# value to memory, and return the original value read.
#
# All operations take 3 sources except CompSwap that takes 4. These
# sources represent:
#
# 0: The SSBO buffer index.
# 1: The offset into the SSBO buffer of the variable that the atomic
#    operation will operate on.
# 2: The data parameter to the atomic function (i.e. the value to add
#    in ssbo_atomic_add, etc).
# 3: For CompSwap only: the second data parameter.
intrinsic("ssbo_atomic_add",  src_comp=[1, 1, 1], dest_comp=1)
intrinsic("ssbo_atomic_imin", src_comp=[1, 1, 1], dest_comp=1)
intrinsic("ssbo_atomic_umin", src_comp=[1, 1, 1], dest_comp=1)
intrinsic("ssbo_atomic_imax", src_comp=[1, 1, 1], dest_comp=1)
intrinsic("ssbo_atomic_umax", src_comp=[1, 1, 1], dest_comp=1)
intrinsic("ssbo_atomic_and",  src_comp=[1, 1, 1], dest_comp=1)
intrinsic("ssbo_atomic_or",   src_comp=[1, 1, 1], dest_comp=1)
intrinsic("ssbo_atomic_xor",  src_comp=[1, 1, 1], dest_comp=1)
intrinsic("ssbo_atomic_exchange", src_comp=[1, 1, 1], dest_comp=1)
intrinsic("ssbo_atomic_comp_swap", src_comp=[1, 1, 1, 1], dest_comp=1)

# CS shared variable atomic intrinsics
#
# All of the shared variable atomic memory operations read a value from
# memory, compute a new value using one of the operations below, write the
# new value to memory, and return the original value read.
#
# All operations take 2 sources except CompSwap that takes 3. These
# sources represent:
#
# 0: The offset into the shared variable storage region that the atomic
#    operation will operate on.
# 1: The data parameter to the atomic function (i.e. the value to add
#    in shared_atomic_add, etc).
# 2: For CompSwap only: the second data parameter.
intrinsic("shared_atomic_add",  src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("shared_atomic_imin", src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("shared_atomic_umin", src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("shared_atomic_imax", src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("shared_atomic_umax", src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("shared_atomic_and",  src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("shared_atomic_or",   src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("shared_atomic_xor",  src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("shared_atomic_exchange", src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("shared_atomic_comp_swap", src_comp=[1, 1, 1], dest_comp=1, indices=[BASE])

def system_value(name, dest_comp, indices=[]):
    intrinsic("load_" + name, [], dest_comp, 0, indices,
              flags=[CAN_ELIMINATE, CAN_REORDER], sysval=True)

system_value("frag_coord", 4)
system_value("front_face", 1)
system_value("vertex_id", 1)
system_value("vertex_id_zero_base", 1)
system_value("first_vertex", 1)
system_value("is_indexed_draw", 1)
system_value("base_vertex", 1)
system_value("instance_id", 1)
system_value("base_instance", 1)
system_value("draw_id", 1)
system_value("sample_id", 1)
system_value("sample_pos", 2)
system_value("sample_mask_in", 1)
system_value("primitive_id", 1)
system_value("invocation_id", 1)
system_value("tess_coord", 3)
system_value("tess_level_outer", 4)
system_value("tess_level_inner", 2)
system_value("patch_vertices_in", 1)
system_value("local_invocation_id", 3)
system_value("local_invocation_index", 1)
system_value("work_group_id", 3)
system_value("user_clip_plane", 4, indices=[UCP_ID])
system_value("num_work_groups", 3)
system_value("helper_invocation", 1)
system_value("alpha_ref_float", 1)
system_value("layer_id", 1)
system_value("view_index", 1)
system_value("subgroup_size", 1)
system_value("subgroup_invocation", 1)
system_value("subgroup_eq_mask", 0)
system_value("subgroup_ge_mask", 0)
system_value("subgroup_gt_mask", 0)
system_value("subgroup_le_mask", 0)
system_value("subgroup_lt_mask", 0)
system_value("num_subgroups", 1)
system_value("subgroup_id", 1)
system_value("local_group_size", 3)
system_value("global_invocation_id", 3)

# Blend constant color values.  Float values are clamped.#
system_value("blend_const_color_r_float", 1)
system_value("blend_const_color_g_float", 1)
system_value("blend_const_color_b_float", 1)
system_value("blend_const_color_a_float", 1)
system_value("blend_const_color_rgba8888_unorm", 1)
system_value("blend_const_color_aaaa8888_unorm", 1)

# Barycentric coordinate intrinsics.
#
# These set up the barycentric coordinates for a particular interpolation.
# The first three are for the simple cases: pixel, centroid, or per-sample
# (at gl_SampleID).  The next two handle interpolating at a specified
# sample location, or interpolating with a vec2 offset,
#
# The interp_mode index should be either the INTERP_MODE_SMOOTH or
# INTERP_MODE_NOPERSPECTIVE enum values.
#
# The vec2 value produced by these intrinsics is intended for use as the
# barycoord source of a load_interpolated_input intrinsic.

def barycentric(name, src_comp=[]):
    intrinsic("load_barycentric_" + name, src_comp=src_comp, dest_comp=2,
              indices=[INTERP_MODE], flags=[CAN_ELIMINATE, CAN_REORDER])

# no sources.  const_index[] = { interp_mode }
barycentric("pixel")
barycentric("centroid")
barycentric("sample")
# src[] = { sample_id }.  const_index[] = { interp_mode }
barycentric("at_sample", [1])
# src[] = { offset.xy }.  const_index[] = { interp_mode }
barycentric("at_offset", [2])

# Load operations pull data from some piece of GPU memory.  All load
# operations operate in terms of offsets into some piece of theoretical
# memory.  Loads from externally visible memory (UBO and SSBO) simply take a
# byte offset as a source.  Loads from opaque memory (uniforms, inputs, etc.)
# take a base+offset pair where the base (const_index[0]) gives the location
# of the start of the variable being loaded and and the offset source is a
# offset into that variable.
#
# Uniform load operations have a second "range" index that specifies the
# range (starting at base) of the data from which we are loading.  If
# const_index[1] == 0, then the range is unknown.
#
# Some load operations such as UBO/SSBO load and per_vertex loads take an
# additional source to specify which UBO/SSBO/vertex to load from.
#
# The exact address type depends on the lowering pass that generates the
# load/store intrinsics.  Typically, this is vec4 units for things such as
# varying slots and float units for fragment shader inputs.  UBO and SSBO
# offsets are always in bytes.

def load(name, num_srcs, indices=[], flags=[]):
    intrinsic("load_" + name, [1] * num_srcs, dest_comp=0, indices=indices,
              flags=flags)

# src[] = { offset }. const_index[] = { base, range }
load("uniform", 1, [BASE, RANGE], [CAN_ELIMINATE, CAN_REORDER])
# src[] = { buffer_index, offset }. No const_index
load("ubo", 2, flags=[CAN_ELIMINATE, CAN_REORDER])
# src[] = { offset }. const_index[] = { base, component }
load("input", 1, [BASE, COMPONENT], [CAN_ELIMINATE, CAN_REORDER])
# src[] = { vertex, offset }. const_index[] = { base, component }
load("per_vertex_input", 2, [BASE, COMPONENT], [CAN_ELIMINATE, CAN_REORDER])
# src[] = { barycoord, offset }. const_index[] = { base, component }
intrinsic("load_interpolated_input", src_comp=[2, 1], dest_comp=0,
          indices=[BASE, COMPONENT], flags=[CAN_ELIMINATE, CAN_REORDER])

# src[] = { buffer_index, offset }. No const_index
load("ssbo", 2, flags=[CAN_ELIMINATE])
# src[] = { offset }. const_index[] = { base, component }
load("output", 1, [BASE, COMPONENT], flags=[CAN_ELIMINATE])
# src[] = { vertex, offset }. const_index[] = { base }
load("per_vertex_output", 2, [BASE, COMPONENT], [CAN_ELIMINATE])
# src[] = { offset }. const_index[] = { base }
load("shared", 1, [BASE], [CAN_ELIMINATE])
# src[] = { offset }. const_index[] = { base, range }
load("push_constant", 1, [BASE, RANGE], [CAN_ELIMINATE, CAN_REORDER])

# Stores work the same way as loads, except now the first source is the value
# to store and the second (and possibly third) source specify where to store
# the value.  SSBO and shared memory stores also have a write mask as
# const_index[0].

def store(name, num_srcs, indices=[], flags=[]):
    intrinsic("store_" + name, [0] + ([1] * (num_srcs - 1)), indices=indices, flags=flags)

# src[] = { value, offset }. const_index[] = { base, write_mask, component }
store("output", 2, [BASE, WRMASK, COMPONENT])
# src[] = { value, vertex, offset }.
# const_index[] = { base, write_mask, component }
store("per_vertex_output", 3, [BASE, WRMASK, COMPONENT])
# src[] = { value, block_index, offset }. const_index[] = { write_mask }
store("ssbo", 3, [WRMASK])
# src[] = { value, offset }. const_index[] = { base, write_mask }
store("shared", 2, [BASE, WRMASK])
