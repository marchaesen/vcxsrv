//
// Copyright 2012 Francisco Jerez
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//

#include <algorithm>
#include "core/device.hpp"
#include "core/platform.hpp"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "spirv/invocation.hpp"
#include "util/bitscan.h"
#include "util/u_debug.h"
#include "spirv/invocation.hpp"
#include "nir/invocation.hpp"
#include <fstream>

using namespace clover;

namespace {
   template<typename T>
   std::vector<T>
   get_compute_param(pipe_screen *pipe, pipe_shader_ir ir_format,
                     pipe_compute_cap cap) {
      int sz = pipe->get_compute_param(pipe, ir_format, cap, NULL);
      std::vector<T> v(sz / sizeof(T));

      pipe->get_compute_param(pipe, ir_format, cap, &v.front());
      return v;
   }
}

device::device(clover::platform &platform, pipe_loader_device *ldev) :
   platform(platform), clc_cache(NULL), ldev(ldev) {
   unsigned major = 1, minor = 1;
   debug_get_version_option("CLOVER_DEVICE_VERSION_OVERRIDE", &major, &minor);
   version = CL_MAKE_VERSION(major, minor, 0);

   major = 1, minor = 1;
   debug_get_version_option("CLOVER_DEVICE_CLC_VERSION_OVERRIDE", &major, &minor);
   clc_version = CL_MAKE_VERSION(major, minor, 0);

   pipe = pipe_loader_create_screen(ldev);
   if (pipe && pipe->get_param(pipe, PIPE_CAP_COMPUTE)) {
      if (supports_ir(PIPE_SHADER_IR_NATIVE))
         return;
#ifdef HAVE_CLOVER_SPIRV
      if (supports_ir(PIPE_SHADER_IR_NIR_SERIALIZED)) {
         nir::check_for_libclc(*this);
         clc_cache = nir::create_clc_disk_cache();
         clc_nir = lazy<std::shared_ptr<nir_shader>>([&] () { std::string log; return std::shared_ptr<nir_shader>(nir::load_libclc_nir(*this, log), ralloc_free); });
         return;
      }
#endif
   }
   if (pipe)
      pipe->destroy(pipe);
   throw error(CL_INVALID_DEVICE);
}

device::~device() {
   if (clc_cache)
      disk_cache_destroy(clc_cache);
   if (pipe)
      pipe->destroy(pipe);
   if (ldev)
      pipe_loader_release(&ldev, 1);
}

bool
device::operator==(const device &dev) const {
   return this == &dev;
}

cl_device_type
device::type() const {
   switch (ldev->type) {
   case PIPE_LOADER_DEVICE_SOFTWARE:
      return CL_DEVICE_TYPE_CPU;
   case PIPE_LOADER_DEVICE_PCI:
   case PIPE_LOADER_DEVICE_PLATFORM:
      return CL_DEVICE_TYPE_GPU;
   default:
      unreachable("Unknown device type.");
   }
}

cl_uint
device::vendor_id() const {
   switch (ldev->type) {
   case PIPE_LOADER_DEVICE_SOFTWARE:
   case PIPE_LOADER_DEVICE_PLATFORM:
      return 0;
   case PIPE_LOADER_DEVICE_PCI:
      return ldev->u.pci.vendor_id;
   default:
      unreachable("Unknown device type.");
   }
}

size_t
device::max_images_read() const {
   return PIPE_MAX_SHADER_SAMPLER_VIEWS;
}

size_t
device::max_images_write() const {
   return PIPE_MAX_SHADER_IMAGES;
}

size_t
device::max_image_buffer_size() const {
   return pipe->get_param(pipe, PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE);
}

cl_uint
device::max_image_size() const {
   return pipe->get_param(pipe, PIPE_CAP_MAX_TEXTURE_2D_SIZE);
}

cl_uint
device::max_image_size_3d() const {
   return 1 << (pipe->get_param(pipe, PIPE_CAP_MAX_TEXTURE_3D_LEVELS) - 1);
}

size_t
device::max_image_array_number() const {
   return pipe->get_param(pipe, PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS);
}

cl_uint
device::max_samplers() const {
   return pipe->get_shader_param(pipe, PIPE_SHADER_COMPUTE,
                                 PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS);
}

cl_ulong
device::max_mem_global() const {
   return get_compute_param<uint64_t>(pipe, ir_format(),
                                      PIPE_COMPUTE_CAP_MAX_GLOBAL_SIZE)[0];
}

cl_ulong
device::max_mem_local() const {
   return get_compute_param<uint64_t>(pipe, ir_format(),
                                      PIPE_COMPUTE_CAP_MAX_LOCAL_SIZE)[0];
}

cl_ulong
device::max_mem_input() const {
   return get_compute_param<uint64_t>(pipe, ir_format(),
                                      PIPE_COMPUTE_CAP_MAX_INPUT_SIZE)[0];
}

cl_ulong
device::max_const_buffer_size() const {
   return pipe->get_shader_param(pipe, PIPE_SHADER_COMPUTE,
                                 PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE);
}

cl_uint
device::max_const_buffers() const {
   return pipe->get_shader_param(pipe, PIPE_SHADER_COMPUTE,
                                 PIPE_SHADER_CAP_MAX_CONST_BUFFERS);
}

size_t
device::max_threads_per_block() const {
   return get_compute_param<uint64_t>(
      pipe, ir_format(), PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK)[0];
}

cl_ulong
device::max_mem_alloc_size() const {
   return get_compute_param<uint64_t>(pipe, ir_format(),
                                      PIPE_COMPUTE_CAP_MAX_MEM_ALLOC_SIZE)[0];
}

cl_uint
device::max_clock_frequency() const {
   return get_compute_param<uint32_t>(pipe, ir_format(),
                                      PIPE_COMPUTE_CAP_MAX_CLOCK_FREQUENCY)[0];
}

cl_uint
device::max_compute_units() const {
   return get_compute_param<uint32_t>(pipe, ir_format(),
                                      PIPE_COMPUTE_CAP_MAX_COMPUTE_UNITS)[0];
}

cl_uint
device::max_printf_buffer_size() const {
   return 1024 * 1024;
}

bool
device::image_support() const {
   return get_compute_param<uint32_t>(pipe, ir_format(),
                                      PIPE_COMPUTE_CAP_IMAGES_SUPPORTED)[0];
}

bool
device::has_doubles() const {
   return pipe->get_param(pipe, PIPE_CAP_DOUBLES);
}

bool
device::has_halves() const {
   return pipe->get_shader_param(pipe, PIPE_SHADER_COMPUTE,
                                 PIPE_SHADER_CAP_FP16);
}

bool
device::has_int64_atomics() const {
   return pipe->get_shader_param(pipe, PIPE_SHADER_COMPUTE,
                                 PIPE_SHADER_CAP_INT64_ATOMICS);
}

bool
device::has_unified_memory() const {
   return pipe->get_param(pipe, PIPE_CAP_UMA);
}

size_t
device::mem_base_addr_align() const {
   uint64_t page_size = 0;
   os_get_page_size(&page_size);
   return std::max((size_t)page_size, sizeof(cl_long) * 16);
}

cl_device_svm_capabilities
device::svm_support() const {
   // Without CAP_RESOURCE_FROM_USER_MEMORY SVM and CL_MEM_USE_HOST_PTR
   // interactions won't work according to spec as clover manages a GPU side
   // copy of the host data.
   //
   // The biggest problem are memory buffers created with CL_MEM_USE_HOST_PTR,
   // but the application and/or the kernel updates the memory via SVM and not
   // the cl_mem buffer.
   // We can't even do proper tracking on what memory might have been accessed
   // as the host ptr to the buffer could be within a SVM region, where through
   // the CL API there is no reliable way of knowing if a certain cl_mem buffer
   // was accessed by a kernel or not and the runtime can't reliably know from
   // which side the GPU buffer content needs to be updated.
   //
   // Another unsolvable scenario is a cl_mem object passed by cl_mem reference
   // and SVM pointer into the same kernel at the same time.
   if (allows_user_pointers() && pipe->get_param(pipe, PIPE_CAP_SYSTEM_SVM))
      // we can emulate all lower levels if we support fine grain system
      return CL_DEVICE_SVM_FINE_GRAIN_SYSTEM |
             CL_DEVICE_SVM_COARSE_GRAIN_BUFFER |
             CL_DEVICE_SVM_FINE_GRAIN_BUFFER;
   return 0;
}

bool
device::allows_user_pointers() const {
   return pipe->get_param(pipe, PIPE_CAP_RESOURCE_FROM_USER_MEMORY) ||
          pipe->get_param(pipe, PIPE_CAP_RESOURCE_FROM_USER_MEMORY_COMPUTE_ONLY);
}

std::vector<size_t>
device::max_block_size() const {
   auto v = get_compute_param<uint64_t>(pipe, ir_format(),
                                        PIPE_COMPUTE_CAP_MAX_BLOCK_SIZE);
   return { v.begin(), v.end() };
}

cl_uint
device::subgroup_size() const {
   return get_compute_param<uint32_t>(pipe, ir_format(),
                                      PIPE_COMPUTE_CAP_SUBGROUP_SIZE)[0];
}

cl_uint
device::address_bits() const {
   return get_compute_param<uint32_t>(pipe, ir_format(),
                                      PIPE_COMPUTE_CAP_ADDRESS_BITS)[0];
}

std::string
device::device_name() const {
   return pipe->get_name(pipe);
}

std::string
device::vendor_name() const {
   return pipe->get_device_vendor(pipe);
}

enum pipe_shader_ir
device::ir_format() const {
   if (supports_ir(PIPE_SHADER_IR_NATIVE))
      return PIPE_SHADER_IR_NATIVE;

   assert(supports_ir(PIPE_SHADER_IR_NIR_SERIALIZED));
   return PIPE_SHADER_IR_NIR_SERIALIZED;
}

std::string
device::ir_target() const {
   std::vector<char> target = get_compute_param<char>(
      pipe, ir_format(), PIPE_COMPUTE_CAP_IR_TARGET);
   return { target.data() };
}

enum pipe_endian
device::endianness() const {
   return (enum pipe_endian)pipe->get_param(pipe, PIPE_CAP_ENDIANNESS);
}

std::string
device::device_version_as_string() const {
   static const std::string version_string =
      std::to_string(CL_VERSION_MAJOR(version)) + "." +
      std::to_string(CL_VERSION_MINOR(version));
   return version_string;
}

std::string
device::device_clc_version_as_string() const {
   static const std::string version_string =
      std::to_string(CL_VERSION_MAJOR(clc_version)) + "." +
      std::to_string(CL_VERSION_MINOR(clc_version));
   return version_string;
}

bool
device::supports_ir(enum pipe_shader_ir ir) const {
   return pipe->get_shader_param(pipe, PIPE_SHADER_COMPUTE,
                                 PIPE_SHADER_CAP_SUPPORTED_IRS) & (1 << ir);
}

std::vector<cl_name_version>
device::supported_extensions() const {
   std::vector<cl_name_version> vec;

   vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 0, 0), "cl_khr_byte_addressable_store" } );
   vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 0, 0), "cl_khr_global_int32_base_atomics" } );
   vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 0, 0), "cl_khr_global_int32_extended_atomics" } );
   vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 0, 0), "cl_khr_local_int32_base_atomics" } );
   vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 0, 0), "cl_khr_local_int32_extended_atomics" } );
   if (has_int64_atomics()) {
      vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 0, 0), "cl_khr_int64_base_atomics" } );
      vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 0, 0), "cl_khr_int64_extended_atomics" } );
   }
   if (has_doubles())
      vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 0, 0), "cl_khr_fp64" } );
   if (has_halves())
      vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 0, 0), "cl_khr_fp16" } );
   if (svm_support())
      vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 0, 0), "cl_arm_shared_virtual_memory" } );
   if (!clover::spirv::supported_versions().empty() &&
       supports_ir(PIPE_SHADER_IR_NIR_SERIALIZED))
      vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 0, 0), "cl_khr_il_program" } );
   vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 0, 0), "cl_khr_extended_versioning" } );
   return vec;
}

std::string
device::supported_extensions_as_string() const {
   static std::string extensions_string;

   if (!extensions_string.empty())
      return extensions_string;

   const auto extension_list = supported_extensions();
   for (const auto &extension : extension_list) {
      if (!extensions_string.empty())
         extensions_string += " ";
      extensions_string += extension.name;
   }
   return extensions_string;
}

std::vector<cl_name_version>
device::supported_il_versions() const {
   return clover::spirv::supported_versions();
}

const void *
device::get_compiler_options(enum pipe_shader_ir ir) const {
   return pipe->get_compiler_options(pipe, ir, PIPE_SHADER_COMPUTE);
}

cl_version
device::device_version() const {
   return version;
}

cl_version
device::device_clc_version() const {
   return clc_version;
}

std::vector<cl_name_version>
device::opencl_c_all_versions() const {
   std::vector<cl_name_version> vec;
   vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 0, 0), "OpenCL C" } );
   vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 1, 0), "OpenCL C" } );

   if (CL_VERSION_MAJOR(clc_version) == 1 &&
       CL_VERSION_MINOR(clc_version) == 2)
      vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 2, 0), "OpenCL C" } );
   if (CL_VERSION_MAJOR(clc_version) == 3) {
      vec.push_back( cl_name_version{ CL_MAKE_VERSION(1, 2, 0), "OpenCL C" } );
      vec.push_back( cl_name_version{ CL_MAKE_VERSION(3, 0, 0), "OpenCL C" } );
   }
   return vec;
}

std::vector<cl_name_version>
device::opencl_c_features() const {
   std::vector<cl_name_version> vec;

   vec.push_back( cl_name_version {CL_MAKE_VERSION(3, 0, 0), "__opencl_c_int64" });
   if (has_doubles())
      vec.push_back( cl_name_version {CL_MAKE_VERSION(3, 0, 0), "__opencl_c_fp64" });

   return vec;
}
