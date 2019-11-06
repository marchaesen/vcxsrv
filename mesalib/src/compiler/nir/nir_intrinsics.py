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
   def __init__(self, name, src_components, dest_components,
                indices, flags, sysval, bit_sizes):
       """Parameters:

       - name: the intrinsic name
       - src_components: list of the number of components per src, 0 means
         vectorized instruction with number of components given in the
         num_components field in nir_intrinsic_instr.
       - dest_components: number of destination components, -1 means no
         dest, 0 means number of components given in num_components field
         in nir_intrinsic_instr.
       - indices: list of constant indicies
       - flags: list of semantic flags
       - sysval: is this a system-value intrinsic
       - bit_sizes: allowed dest bit_sizes
       """
       assert isinstance(name, str)
       assert isinstance(src_components, list)
       if src_components:
           assert isinstance(src_components[0], int)
       assert isinstance(dest_components, int)
       assert isinstance(indices, list)
       if indices:
           assert isinstance(indices[0], str)
       assert isinstance(flags, list)
       if flags:
           assert isinstance(flags[0], str)
       assert isinstance(sysval, bool)
       if bit_sizes:
           assert isinstance(bit_sizes[0], int)

       self.name = name
       self.num_srcs = len(src_components)
       self.src_components = src_components
       self.has_dest = (dest_components >= 0)
       self.dest_components = dest_components
       self.num_indices = len(indices)
       self.indices = indices
       self.flags = flags
       self.sysval = sysval
       self.bit_sizes = bit_sizes

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
# Parameter index for a load_param intrinsic
PARAM_IDX = "NIR_INTRINSIC_PARAM_IDX"
# Image dimensionality for image intrinsics
IMAGE_DIM = "NIR_INTRINSIC_IMAGE_DIM"
# Non-zero if we are accessing an array image
IMAGE_ARRAY = "NIR_INTRINSIC_IMAGE_ARRAY"
# Access qualifiers for image and memory access intrinsics
ACCESS = "NIR_INTRINSIC_ACCESS"
DST_ACCESS = "NIR_INTRINSIC_DST_ACCESS"
SRC_ACCESS = "NIR_INTRINSIC_SRC_ACCESS"
# Image format for image intrinsics
FORMAT = "NIR_INTRINSIC_FORMAT"
# Offset or address alignment
ALIGN_MUL = "NIR_INTRINSIC_ALIGN_MUL"
ALIGN_OFFSET = "NIR_INTRINSIC_ALIGN_OFFSET"
# The vulkan descriptor type for vulkan_resource_index
DESC_TYPE = "NIR_INTRINSIC_DESC_TYPE"
# The nir_alu_type of a uniform/input/output
TYPE = "NIR_INTRINSIC_TYPE"
# The swizzle mask for quad_swizzle_amd & masked_swizzle_amd
SWIZZLE_MASK = "NIR_INTRINSIC_SWIZZLE_MASK"
# Driver location of attribute
DRIVER_LOCATION = "NIR_INTRINSIC_DRIVER_LOCATION"
# Ordering and visibility of a memory operation
MEMORY_SEMANTICS = "NIR_INTRINSIC_MEMORY_SEMANTICS"
# Modes affected by a memory operation
MEMORY_MODES = "NIR_INTRINSIC_MEMORY_MODES"
# Scope of a memory operation
MEMORY_SCOPE = "NIR_INTRINSIC_MEMORY_SCOPE"

#
# Possible flags:
#

CAN_ELIMINATE = "NIR_INTRINSIC_CAN_ELIMINATE"
CAN_REORDER   = "NIR_INTRINSIC_CAN_REORDER"

INTR_OPCODES = {}

# Defines a new NIR intrinsic.  By default, the intrinsic will have no sources
# and no destination.
#
# You can set dest_comp=n to enable a destination for the intrinsic, in which
# case it will have that many components, or =0 for "as many components as the
# NIR destination value."
#
# Set src_comp=n to enable sources for the intruction.  It can be an array of
# component counts, or (for convenience) a scalar component count if there's
# only one source.  If a component count is 0, it will be as many components as
# the intrinsic has based on the dest_comp.
def intrinsic(name, src_comp=[], dest_comp=-1, indices=[],
              flags=[], sysval=False, bit_sizes=[]):
    assert name not in INTR_OPCODES
    INTR_OPCODES[name] = Intrinsic(name, src_comp, dest_comp,
                                   indices, flags, sysval, bit_sizes)

intrinsic("nop", flags=[CAN_ELIMINATE])

intrinsic("load_param", dest_comp=0, indices=[PARAM_IDX], flags=[CAN_ELIMINATE])

intrinsic("load_deref", dest_comp=0, src_comp=[-1],
          indices=[ACCESS], flags=[CAN_ELIMINATE])
intrinsic("store_deref", src_comp=[-1, 0], indices=[WRMASK, ACCESS])
intrinsic("copy_deref", src_comp=[-1, -1], indices=[DST_ACCESS, SRC_ACCESS])

# Interpolation of input.  The interp_deref_at* intrinsics are similar to the
# load_var intrinsic acting on a shader input except that they interpolate the
# input differently.  The at_sample and at_offset intrinsics take an
# additional source that is an integer sample id or a vec2 position offset
# respectively.

intrinsic("interp_deref_at_centroid", dest_comp=0, src_comp=[1],
          flags=[ CAN_ELIMINATE, CAN_REORDER])
intrinsic("interp_deref_at_sample", src_comp=[1, 1], dest_comp=0,
          flags=[CAN_ELIMINATE, CAN_REORDER])
intrinsic("interp_deref_at_offset", src_comp=[1, 2], dest_comp=0,
          flags=[CAN_ELIMINATE, CAN_REORDER])

# Gets the length of an unsized array at the end of a buffer
intrinsic("deref_buffer_array_length", src_comp=[-1], dest_comp=1,
          flags=[CAN_ELIMINATE, CAN_REORDER])

# Ask the driver for the size of a given buffer. It takes the buffer index
# as source.
intrinsic("get_buffer_size", src_comp=[-1], dest_comp=1,
          flags=[CAN_ELIMINATE, CAN_REORDER])

# a barrier is an intrinsic with no inputs/outputs but which can't be moved
# around/optimized in general
def barrier(name):
    intrinsic(name)

barrier("barrier")
barrier("discard")

# Demote fragment shader invocation to a helper invocation.  Any stores to
# memory after this instruction are suppressed and the fragment does not write
# outputs to the framebuffer.  Unlike discard, demote needs to ensure that
# derivatives will still work for invocations that were not demoted.
#
# As specified by SPV_EXT_demote_to_helper_invocation.
barrier("demote")
intrinsic("is_helper_invocation", dest_comp=1, flags=[CAN_ELIMINATE])


# Memory barrier with semantics analogous to the memoryBarrier() GLSL
# intrinsic.
barrier("memory_barrier")

# Memory barrier with explicit scope.  Follows the semantics of SPIR-V
# OpMemoryBarrier, used to implement Vulkan Memory Model.  Storage that the
# barrierr applies is represented using NIR variable modes.
intrinsic("scoped_memory_barrier",
          indices=[MEMORY_SEMANTICS, MEMORY_MODES, MEMORY_SCOPE])

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

# A conditional discard/demote, with a single boolean source.
intrinsic("discard_if", src_comp=[1])
intrinsic("demote_if", src_comp=[1])

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

# AMD shader ballot operations
intrinsic("quad_swizzle_amd", src_comp=[0], dest_comp=0, indices=[SWIZZLE_MASK],
          flags=[CAN_ELIMINATE])
intrinsic("masked_swizzle_amd", src_comp=[0], dest_comp=0, indices=[SWIZZLE_MASK],
          flags=[CAN_ELIMINATE])
intrinsic("write_invocation_amd", src_comp=[0, 0, 1], dest_comp=0, flags=[CAN_ELIMINATE])
intrinsic("mbcnt_amd", src_comp=[1], dest_comp=1, flags=[CAN_ELIMINATE])

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
    intrinsic(name + "_deref", src_comp=[-1], dest_comp=1, flags=flags)
    intrinsic(name, src_comp=[1], dest_comp=1, indices=[BASE], flags=flags)

def atomic2(name):
    intrinsic(name + "_deref", src_comp=[-1, 1], dest_comp=1)
    intrinsic(name, src_comp=[1, 1], dest_comp=1, indices=[BASE])

def atomic3(name):
    intrinsic(name + "_deref", src_comp=[-1, 1, 1], dest_comp=1)
    intrinsic(name, src_comp=[1, 1, 1], dest_comp=1, indices=[BASE])

atomic("atomic_counter_inc")
atomic("atomic_counter_pre_dec")
atomic("atomic_counter_post_dec")
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
# All image intrinsics come in three versions.  One which take an image target
# passed as a deref chain as the first source, one which takes an index as the
# first source, and one which takes a bindless handle as the first source.
# In the first version, the image variable contains the memory and layout
# qualifiers that influence the semantics of the intrinsic.  In the second and
# third, the image format and access qualifiers are provided as constant
# indices.
#
# All image intrinsics take a four-coordinate vector and a sample index as
# 2nd and 3rd sources, determining the location within the image that will be
# accessed by the intrinsic.  Components not applicable to the image target
# in use are undefined.  Image store takes an additional four-component
# argument with the value to be written, and image atomic operations take
# either one or two additional scalar arguments with the same meaning as in
# the ARB_shader_image_load_store specification.
def image(name, src_comp=[], **kwargs):
    intrinsic("image_deref_" + name, src_comp=[1] + src_comp,
              indices=[ACCESS], **kwargs)
    intrinsic("image_" + name, src_comp=[1] + src_comp,
              indices=[IMAGE_DIM, IMAGE_ARRAY, FORMAT, ACCESS], **kwargs)
    intrinsic("bindless_image_" + name, src_comp=[1] + src_comp,
              indices=[IMAGE_DIM, IMAGE_ARRAY, FORMAT, ACCESS], **kwargs)

image("load", src_comp=[4, 1], dest_comp=0, flags=[CAN_ELIMINATE])
image("store", src_comp=[4, 1, 0])
image("atomic_add",  src_comp=[4, 1, 1], dest_comp=1)
image("atomic_imin",  src_comp=[4, 1, 1], dest_comp=1)
image("atomic_umin",  src_comp=[4, 1, 1], dest_comp=1)
image("atomic_imax",  src_comp=[4, 1, 1], dest_comp=1)
image("atomic_umax",  src_comp=[4, 1, 1], dest_comp=1)
image("atomic_and",  src_comp=[4, 1, 1], dest_comp=1)
image("atomic_or",   src_comp=[4, 1, 1], dest_comp=1)
image("atomic_xor",  src_comp=[4, 1, 1], dest_comp=1)
image("atomic_exchange",  src_comp=[4, 1, 1], dest_comp=1)
image("atomic_comp_swap", src_comp=[4, 1, 1, 1], dest_comp=1)
image("atomic_fadd",  src_comp=[1, 4, 1, 1], dest_comp=1)
image("size",    dest_comp=0, flags=[CAN_ELIMINATE, CAN_REORDER])
image("samples", dest_comp=1, flags=[CAN_ELIMINATE, CAN_REORDER])
image("atomic_inc_wrap",  src_comp=[4, 1, 1], dest_comp=1)
image("atomic_dec_wrap",  src_comp=[4, 1, 1], dest_comp=1)

# Intel-specific query for loading from the brw_image_param struct passed
# into the shader as a uniform.  The variable is a deref to the image
# variable. The const index specifies which of the six parameters to load.
intrinsic("image_deref_load_param_intel", src_comp=[1], dest_comp=0,
          indices=[BASE], flags=[CAN_ELIMINATE, CAN_REORDER])
image("load_raw_intel", src_comp=[1], dest_comp=0,
      flags=[CAN_ELIMINATE])
image("store_raw_intel", src_comp=[1, 0])

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
intrinsic("vulkan_resource_index", src_comp=[1], dest_comp=0,
          indices=[DESC_SET, BINDING, DESC_TYPE],
          flags=[CAN_ELIMINATE, CAN_REORDER])
intrinsic("vulkan_resource_reindex", src_comp=[0, 1], dest_comp=0,
          indices=[DESC_TYPE], flags=[CAN_ELIMINATE, CAN_REORDER])
intrinsic("load_vulkan_descriptor", src_comp=[-1], dest_comp=0,
          indices=[DESC_TYPE], flags=[CAN_ELIMINATE, CAN_REORDER])

# variable atomic intrinsics
#
# All of these variable atomic memory operations read a value from memory,
# compute a new value using one of the operations below, write the new value
# to memory, and return the original value read.
#
# All operations take 2 sources except CompSwap that takes 3. These sources
# represent:
#
# 0: A deref to the memory on which to perform the atomic
# 1: The data parameter to the atomic function (i.e. the value to add
#    in shared_atomic_add, etc).
# 2: For CompSwap only: the second data parameter.
intrinsic("deref_atomic_add",  src_comp=[-1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("deref_atomic_imin", src_comp=[-1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("deref_atomic_umin", src_comp=[-1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("deref_atomic_imax", src_comp=[-1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("deref_atomic_umax", src_comp=[-1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("deref_atomic_and",  src_comp=[-1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("deref_atomic_or",   src_comp=[-1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("deref_atomic_xor",  src_comp=[-1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("deref_atomic_exchange", src_comp=[-1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("deref_atomic_comp_swap", src_comp=[-1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("deref_atomic_fadd",  src_comp=[-1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("deref_atomic_fmin",  src_comp=[-1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("deref_atomic_fmax",  src_comp=[-1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("deref_atomic_fcomp_swap", src_comp=[-1, 1, 1], dest_comp=1, indices=[ACCESS])

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
intrinsic("ssbo_atomic_add",  src_comp=[1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("ssbo_atomic_imin", src_comp=[1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("ssbo_atomic_umin", src_comp=[1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("ssbo_atomic_imax", src_comp=[1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("ssbo_atomic_umax", src_comp=[1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("ssbo_atomic_and",  src_comp=[1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("ssbo_atomic_or",   src_comp=[1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("ssbo_atomic_xor",  src_comp=[1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("ssbo_atomic_exchange", src_comp=[1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("ssbo_atomic_comp_swap", src_comp=[1, 1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("ssbo_atomic_fadd", src_comp=[1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("ssbo_atomic_fmin", src_comp=[1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("ssbo_atomic_fmax", src_comp=[1, 1, 1], dest_comp=1, indices=[ACCESS])
intrinsic("ssbo_atomic_fcomp_swap", src_comp=[1, 1, 1, 1], dest_comp=1, indices=[ACCESS])

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
intrinsic("shared_atomic_fadd",  src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("shared_atomic_fmin",  src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("shared_atomic_fmax",  src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("shared_atomic_fcomp_swap", src_comp=[1, 1, 1], dest_comp=1, indices=[BASE])

# Global atomic intrinsics
#
# All of the shared variable atomic memory operations read a value from
# memory, compute a new value using one of the operations below, write the
# new value to memory, and return the original value read.
#
# All operations take 2 sources except CompSwap that takes 3. These
# sources represent:
#
# 0: The memory address that the atomic operation will operate on.
# 1: The data parameter to the atomic function (i.e. the value to add
#    in shared_atomic_add, etc).
# 2: For CompSwap only: the second data parameter.
intrinsic("global_atomic_add",  src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("global_atomic_imin", src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("global_atomic_umin", src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("global_atomic_imax", src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("global_atomic_umax", src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("global_atomic_and",  src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("global_atomic_or",   src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("global_atomic_xor",  src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("global_atomic_exchange", src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("global_atomic_comp_swap", src_comp=[1, 1, 1], dest_comp=1, indices=[BASE])
intrinsic("global_atomic_fadd",  src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("global_atomic_fmin",  src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("global_atomic_fmax",  src_comp=[1, 1], dest_comp=1, indices=[BASE])
intrinsic("global_atomic_fcomp_swap", src_comp=[1, 1, 1], dest_comp=1, indices=[BASE])

def system_value(name, dest_comp, indices=[], bit_sizes=[32]):
    intrinsic("load_" + name, [], dest_comp, indices,
              flags=[CAN_ELIMINATE, CAN_REORDER], sysval=True,
              bit_sizes=bit_sizes)

system_value("frag_coord", 4)
system_value("point_coord", 2)
system_value("front_face", 1, bit_sizes=[1, 32])
system_value("vertex_id", 1)
system_value("vertex_id_zero_base", 1)
system_value("first_vertex", 1)
system_value("is_indexed_draw", 1)
system_value("base_vertex", 1)
system_value("instance_id", 1)
system_value("base_instance", 1)
system_value("draw_id", 1)
system_value("sample_id", 1)
# sample_id_no_per_sample is like sample_id but does not imply per-
# sample shading.  See the lower_helper_invocation option.
system_value("sample_id_no_per_sample", 1)
system_value("sample_pos", 2)
system_value("sample_mask_in", 1)
system_value("primitive_id", 1)
system_value("invocation_id", 1)
system_value("tess_coord", 3)
system_value("tess_level_outer", 4)
system_value("tess_level_inner", 2)
system_value("tess_level_outer_default", 4)
system_value("tess_level_inner_default", 2)
system_value("patch_vertices_in", 1)
system_value("local_invocation_id", 3)
system_value("local_invocation_index", 1)
system_value("work_group_id", 3)
system_value("user_clip_plane", 4, indices=[UCP_ID])
system_value("num_work_groups", 3)
system_value("helper_invocation", 1, bit_sizes=[1, 32])
system_value("alpha_ref_float", 1)
system_value("layer_id", 1)
system_value("view_index", 1)
system_value("subgroup_size", 1)
system_value("subgroup_invocation", 1)
system_value("subgroup_eq_mask", 0, bit_sizes=[32, 64])
system_value("subgroup_ge_mask", 0, bit_sizes=[32, 64])
system_value("subgroup_gt_mask", 0, bit_sizes=[32, 64])
system_value("subgroup_le_mask", 0, bit_sizes=[32, 64])
system_value("subgroup_lt_mask", 0, bit_sizes=[32, 64])
system_value("num_subgroups", 1)
system_value("subgroup_id", 1)
system_value("local_group_size", 3)
system_value("global_invocation_id", 3, bit_sizes=[32, 64])
system_value("global_invocation_index", 1, bit_sizes=[32, 64])
system_value("work_dim", 1)
# Driver-specific viewport scale/offset parameters.
#
# VC4 and V3D need to emit a scaled version of the position in the vertex
# shaders for binning, and having system values lets us move the math for that
# into NIR.
#
# Panfrost needs to implement all coordinate transformation in the
# vertex shader; system values allow us to share this routine in NIR.
system_value("viewport_x_scale", 1)
system_value("viewport_y_scale", 1)
system_value("viewport_z_scale", 1)
system_value("viewport_z_offset", 1)
system_value("viewport_scale", 3)
system_value("viewport_offset", 3)

# Blend constant color values.  Float values are clamped. Vectored versions are
# provided as well for driver convenience

system_value("blend_const_color_r_float", 1)
system_value("blend_const_color_g_float", 1)
system_value("blend_const_color_b_float", 1)
system_value("blend_const_color_a_float", 1)
system_value("blend_const_color_rgba", 4)
system_value("blend_const_color_rgba8888_unorm", 1)
system_value("blend_const_color_aaaa8888_unorm", 1)

# System values for gl_Color, for radeonsi which interpolates these in the
# shader prolog to handle two-sided color without recompiles and therefore
# doesn't handle these in the main shader part like normal varyings.
system_value("color0", 4)
system_value("color1", 4)

# System value for internal compute shaders in radeonsi.
system_value("user_data_amd", 4)

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

# no sources.
barycentric("pixel")
barycentric("centroid")
barycentric("sample")
# src[] = { sample_id }.
barycentric("at_sample", [1])
# src[] = { offset.xy }.
barycentric("at_offset", [2])

# Load sample position:
#
# Takes a sample # and returns a sample position.  Used for lowering
# interpolateAtSample() to interpolateAtOffset()
intrinsic("load_sample_pos_from_id", src_comp=[1], dest_comp=2,
          flags=[CAN_ELIMINATE, CAN_REORDER])

# Loads what I believe is the primitive size, for scaling ij to pixel size:
intrinsic("load_size_ir3", dest_comp=1, flags=[CAN_ELIMINATE, CAN_REORDER])

# Fragment shader input interpolation delta intrinsic.
#
# For hw where fragment shader input interpolation is handled in shader, the
# load_fs_input_interp deltas intrinsics can be used to load the input deltas
# used for interpolation as follows:
#
#    vec3 iid = load_fs_input_interp_deltas(varying_slot)
#    vec2 bary = load_barycentric_*(...)
#    float result = iid.x + iid.y * bary.y + iid.z * bary.x

intrinsic("load_fs_input_interp_deltas", src_comp=[1], dest_comp=3,
          indices=[BASE, COMPONENT], flags=[CAN_ELIMINATE, CAN_REORDER])

# Load operations pull data from some piece of GPU memory.  All load
# operations operate in terms of offsets into some piece of theoretical
# memory.  Loads from externally visible memory (UBO and SSBO) simply take a
# byte offset as a source.  Loads from opaque memory (uniforms, inputs, etc.)
# take a base+offset pair where the nir_intrinsic_base() gives the location
# of the start of the variable being loaded and and the offset source is a
# offset into that variable.
#
# Uniform load operations have a nir_intrinsic_range() index that specifies the
# range (starting at base) of the data from which we are loading.  If
# range == 0, then the range is unknown.
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

# src[] = { offset }.
load("uniform", 1, [BASE, RANGE, TYPE], [CAN_ELIMINATE, CAN_REORDER])
# src[] = { buffer_index, offset }.
load("ubo", 2, [ACCESS, ALIGN_MUL, ALIGN_OFFSET], flags=[CAN_ELIMINATE, CAN_REORDER])
# src[] = { offset }.
load("input", 1, [BASE, COMPONENT, TYPE], [CAN_ELIMINATE, CAN_REORDER])
# src[] = { vertex, offset }.
load("per_vertex_input", 2, [BASE, COMPONENT], [CAN_ELIMINATE, CAN_REORDER])
# src[] = { barycoord, offset }.
intrinsic("load_interpolated_input", src_comp=[2, 1], dest_comp=0,
          indices=[BASE, COMPONENT], flags=[CAN_ELIMINATE, CAN_REORDER])

# src[] = { buffer_index, offset }.
load("ssbo", 2, [ACCESS, ALIGN_MUL, ALIGN_OFFSET], [CAN_ELIMINATE])
# src[] = { offset }.
load("output", 1, [BASE, COMPONENT], flags=[CAN_ELIMINATE])
# src[] = { vertex, offset }.
load("per_vertex_output", 2, [BASE, COMPONENT], [CAN_ELIMINATE])
# src[] = { offset }.
load("shared", 1, [BASE, ALIGN_MUL, ALIGN_OFFSET], [CAN_ELIMINATE])
# src[] = { offset }.
load("push_constant", 1, [BASE, RANGE], [CAN_ELIMINATE, CAN_REORDER])
# src[] = { offset }.
load("constant", 1, [BASE, RANGE], [CAN_ELIMINATE, CAN_REORDER])
# src[] = { address }.
load("global", 1, [ACCESS, ALIGN_MUL, ALIGN_OFFSET], [CAN_ELIMINATE])
# src[] = { address }.
load("kernel_input", 1, [BASE, RANGE, ALIGN_MUL, ALIGN_OFFSET], [CAN_ELIMINATE, CAN_REORDER])
# src[] = { offset }.
load("scratch", 1, [ALIGN_MUL, ALIGN_OFFSET], [CAN_ELIMINATE])

# Stores work the same way as loads, except now the first source is the value
# to store and the second (and possibly third) source specify where to store
# the value.  SSBO and shared memory stores also have a
# nir_intrinsic_write_mask()

def store(name, num_srcs, indices=[], flags=[]):
    intrinsic("store_" + name, [0] + ([1] * (num_srcs - 1)), indices=indices, flags=flags)

# src[] = { value, offset }.
store("output", 2, [BASE, WRMASK, COMPONENT, TYPE])
# src[] = { value, vertex, offset }.
store("per_vertex_output", 3, [BASE, WRMASK, COMPONENT])
# src[] = { value, block_index, offset }
store("ssbo", 3, [WRMASK, ACCESS, ALIGN_MUL, ALIGN_OFFSET])
# src[] = { value, offset }.
store("shared", 2, [BASE, WRMASK, ALIGN_MUL, ALIGN_OFFSET])
# src[] = { value, address }.
store("global", 2, [WRMASK, ACCESS, ALIGN_MUL, ALIGN_OFFSET])
# src[] = { value, offset }.
store("scratch", 2, [ALIGN_MUL, ALIGN_OFFSET, WRMASK])

# IR3-specific version of most SSBO intrinsics. The only different
# compare to the originals is that they add an extra source to hold
# the dword-offset, which is needed by the backend code apart from
# the byte-offset already provided by NIR in one of the sources.
#
# NIR lowering pass 'ir3_nir_lower_io_offset' will replace the
# original SSBO intrinsics by these, placing the computed
# dword-offset always in the last source.
#
# The float versions are not handled because those are not supported
# by the backend.
intrinsic("store_ssbo_ir3",  src_comp=[0, 1, 1, 1],
          indices=[WRMASK, ACCESS, ALIGN_MUL, ALIGN_OFFSET])
intrinsic("load_ssbo_ir3",  src_comp=[1, 1, 1], dest_comp=0,
          indices=[ACCESS, ALIGN_MUL, ALIGN_OFFSET], flags=[CAN_ELIMINATE])
intrinsic("ssbo_atomic_add_ir3",        src_comp=[1, 1, 1, 1],    dest_comp=1)
intrinsic("ssbo_atomic_imin_ir3",       src_comp=[1, 1, 1, 1],    dest_comp=1)
intrinsic("ssbo_atomic_umin_ir3",       src_comp=[1, 1, 1, 1],    dest_comp=1)
intrinsic("ssbo_atomic_imax_ir3",       src_comp=[1, 1, 1, 1],    dest_comp=1)
intrinsic("ssbo_atomic_umax_ir3",       src_comp=[1, 1, 1, 1],    dest_comp=1)
intrinsic("ssbo_atomic_and_ir3",        src_comp=[1, 1, 1, 1],    dest_comp=1)
intrinsic("ssbo_atomic_or_ir3",         src_comp=[1, 1, 1, 1],    dest_comp=1)
intrinsic("ssbo_atomic_xor_ir3",        src_comp=[1, 1, 1, 1],    dest_comp=1)
intrinsic("ssbo_atomic_exchange_ir3",   src_comp=[1, 1, 1, 1],    dest_comp=1)
intrinsic("ssbo_atomic_comp_swap_ir3",  src_comp=[1, 1, 1, 1, 1], dest_comp=1)

# System values for freedreno geometry shaders.
system_value("vs_primitive_stride_ir3", 1)
system_value("vs_vertex_stride_ir3", 1)
system_value("gs_header_ir3", 1)
system_value("primitive_location_ir3", 1, indices=[DRIVER_LOCATION])

# IR3-specific load/store intrinsics. These access a buffer used to pass data
# between geometry stages - perhaps it's explicit access to the vertex cache.

# src[] = { value, offset }.
store("shared_ir3", 2, [BASE, WRMASK, ALIGN_MUL, ALIGN_OFFSET])
# src[] = { offset }.
load("shared_ir3", 1, [BASE, ALIGN_MUL, ALIGN_OFFSET], [CAN_ELIMINATE])

# Intrinsics used by the Midgard/Bifrost blend pipeline. These are defined
# within a blend shader to read/write the raw value from the tile buffer,
# without applying any format conversion in the process. If the shader needs
# usable pixel values, it must apply format conversions itself.
#
# These definitions are generic, but they are explicitly vendored to prevent
# other drivers from using them, as their semantics is defined in terms of the
# Midgard/Bifrost hardware tile buffer and may not line up with anything sane.
# One notable divergence is sRGB, which is asymmetric: raw_input_pan requires
# an sRGB->linear conversion, but linear values should be written to
# raw_output_pan and the hardware handles linear->sRGB.

# src[] = { value }
store("raw_output_pan", 1, [])
load("raw_output_pan", 0, [], [CAN_ELIMINATE, CAN_REORDER])

# V3D-specific instrinc for tile buffer color reads.
#
# The hardware requires that we read the samples and components of a pixel
# in order, so we cannot eliminate or remove any loads in a sequence.
#
# src[] = { render_target }
# BASE = sample index
load("tlb_color_v3d", 1, [BASE, COMPONENT], [])

# V3D-specific instrinc for per-sample tile buffer color writes.
#
# The driver backend needs to identify per-sample color writes and emit
# specific code for them.
#
# src[] = { value, render_target }
# BASE = sample index
store("tlb_sample_color_v3d", 2, [BASE, COMPONENT, TYPE], [])
