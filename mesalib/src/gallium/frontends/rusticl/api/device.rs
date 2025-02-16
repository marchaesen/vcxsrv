use crate::api::icd::*;
use crate::api::types::IdpAccelProps;
use crate::api::util::*;
use crate::core::device::*;
use crate::core::platform::*;
use crate::core::version::*;

use mesa_rust_gen::*;
use mesa_rust_util::ptr::*;
use rusticl_opencl_gen::*;
use rusticl_proc_macros::cl_entrypoint;
use rusticl_proc_macros::cl_info_entrypoint;

use std::cmp::min;
use std::ffi::CStr;
use std::mem::size_of;
use std::ptr;

const SPIRV_SUPPORT_STRING: &CStr =
    c"SPIR-V_1.0 SPIR-V_1.1 SPIR-V_1.2 SPIR-V_1.3 SPIR-V_1.4 SPIR-V_1.5 SPIR-V_1.6";
const SPIRV_SUPPORT: [cl_name_version; 7] = [
    mk_cl_version_ext(1, 0, 0, "SPIR-V"),
    mk_cl_version_ext(1, 1, 0, "SPIR-V"),
    mk_cl_version_ext(1, 2, 0, "SPIR-V"),
    mk_cl_version_ext(1, 3, 0, "SPIR-V"),
    mk_cl_version_ext(1, 4, 0, "SPIR-V"),
    mk_cl_version_ext(1, 5, 0, "SPIR-V"),
    mk_cl_version_ext(1, 6, 0, "SPIR-V"),
];
type ClDevIdpAccelProps = cl_device_integer_dot_product_acceleration_properties_khr;

#[cl_info_entrypoint(clGetDeviceInfo)]
unsafe impl CLInfo<cl_device_info> for cl_device_id {
    fn query(&self, q: cl_device_info, v: CLInfoValue) -> CLResult<CLInfoRes> {
        let dev = Device::ref_from_raw(*self)?;

        // curses you CL_DEVICE_INTEGER_DOT_PRODUCT_ACCELERATION_PROPERTIES_4x8BIT_PACKED_KHR
        #[allow(non_upper_case_globals)]
        match q {
            CL_DEVICE_ADDRESS_BITS => v.write::<cl_uint>(dev.address_bits()),
            CL_DEVICE_ATOMIC_FENCE_CAPABILITIES => v.write::<cl_device_atomic_capabilities>(
                (CL_DEVICE_ATOMIC_ORDER_RELAXED
                    | CL_DEVICE_ATOMIC_ORDER_ACQ_REL
                    | CL_DEVICE_ATOMIC_SCOPE_WORK_GROUP)
                    as cl_device_atomic_capabilities,
            ),
            CL_DEVICE_ATOMIC_MEMORY_CAPABILITIES => v.write::<cl_device_atomic_capabilities>(
                (CL_DEVICE_ATOMIC_ORDER_RELAXED | CL_DEVICE_ATOMIC_SCOPE_WORK_GROUP)
                    as cl_device_atomic_capabilities,
            ),
            CL_DEVICE_AVAILABLE => v.write::<bool>(true),
            CL_DEVICE_BUILT_IN_KERNELS => v.write::<&CStr>(c""),
            CL_DEVICE_BUILT_IN_KERNELS_WITH_VERSION => v.write::<&[cl_name_version]>(&[]),
            CL_DEVICE_COMPILER_AVAILABLE => v.write::<bool>(true),
            CL_DEVICE_CROSS_DEVICE_SHARED_MEM_CAPABILITIES_INTEL => {
                v.write::<cl_device_device_enqueue_capabilities>(0)
            }
            CL_DEVICE_DEVICE_ENQUEUE_CAPABILITIES => {
                v.write::<cl_device_device_enqueue_capabilities>(0)
            }
            CL_DEVICE_DEVICE_MEM_CAPABILITIES_INTEL => {
                v.write::<cl_device_unified_shared_memory_capabilities_intel>(0)
            }
            CL_DEVICE_DOUBLE_FP_CONFIG => v.write::<cl_device_fp_config>(
                if dev.fp64_supported() {
                    let mut fp64_config = CL_FP_FMA
                        | CL_FP_ROUND_TO_NEAREST
                        | CL_FP_ROUND_TO_ZERO
                        | CL_FP_ROUND_TO_INF
                        | CL_FP_INF_NAN
                        | CL_FP_DENORM;
                    if dev.fp64_is_softfp() {
                        fp64_config |= CL_FP_SOFT_FLOAT;
                    }
                    fp64_config
                } else {
                    0
                }
                .into(),
            ),
            CL_DEVICE_ENDIAN_LITTLE => v.write::<bool>(dev.little_endian()),
            CL_DEVICE_ERROR_CORRECTION_SUPPORT => v.write::<bool>(false),
            CL_DEVICE_EXECUTION_CAPABILITIES => {
                v.write::<cl_device_exec_capabilities>(CL_EXEC_KERNEL.into())
            }
            CL_DEVICE_EXTENSIONS => v.write::<&str>(&dev.extension_string),
            CL_DEVICE_EXTENSIONS_WITH_VERSION => v.write::<&[cl_name_version]>(&dev.extensions),
            CL_DEVICE_GENERIC_ADDRESS_SPACE_SUPPORT => v.write::<bool>(false),
            CL_DEVICE_GLOBAL_MEM_CACHE_TYPE => v.write::<cl_device_mem_cache_type>(CL_NONE),
            CL_DEVICE_GLOBAL_MEM_CACHE_SIZE => v.write::<cl_ulong>(0),
            CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE => v.write::<cl_uint>(0),
            CL_DEVICE_GLOBAL_MEM_SIZE => v.write::<cl_ulong>(dev.global_mem_size()),
            CL_DEVICE_GLOBAL_VARIABLE_PREFERRED_TOTAL_SIZE => v.write::<usize>(0),
            // Provided by the cl_khr_fp16 extension.
            CL_DEVICE_HALF_FP_CONFIG if dev.fp16_supported() => {
                v.write::<cl_device_fp_config>((CL_FP_ROUND_TO_NEAREST | CL_FP_INF_NAN).into())
            }
            CL_DEVICE_HOST_MEM_CAPABILITIES_INTEL => {
                v.write::<cl_device_unified_shared_memory_capabilities_intel>(0)
            }
            CL_DEVICE_HOST_UNIFIED_MEMORY => v.write::<bool>(dev.unified_memory()),
            CL_DEVICE_IL_VERSION => v.write::<&CStr>(SPIRV_SUPPORT_STRING),
            CL_DEVICE_ILS_WITH_VERSION => v.write::<Vec<cl_name_version>>(SPIRV_SUPPORT.to_vec()),
            CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT => {
                v.write::<cl_uint>(dev.image_base_address_alignment())
            }
            CL_DEVICE_IMAGE_MAX_ARRAY_SIZE => v.write::<usize>(dev.image_array_size()),
            CL_DEVICE_IMAGE_MAX_BUFFER_SIZE => v.write::<usize>(dev.image_buffer_max_size_pixels()),
            CL_DEVICE_IMAGE_PITCH_ALIGNMENT => v.write::<cl_uint>(dev.image_pitch_alignment()),
            CL_DEVICE_IMAGE_SUPPORT => v.write::<bool>(dev.caps.has_images),
            CL_DEVICE_IMAGE2D_MAX_HEIGHT => v.write::<usize>(dev.caps.image_2d_size as usize),
            CL_DEVICE_IMAGE2D_MAX_WIDTH => v.write::<usize>(dev.caps.image_2d_size as usize),
            CL_DEVICE_IMAGE3D_MAX_HEIGHT => v.write::<usize>(dev.image_3d_size()),
            CL_DEVICE_IMAGE3D_MAX_WIDTH => v.write::<usize>(dev.image_3d_size()),
            CL_DEVICE_IMAGE3D_MAX_DEPTH => v.write::<usize>(dev.image_3d_size()),
            CL_DEVICE_INTEGER_DOT_PRODUCT_CAPABILITIES_KHR => {
                v.write::<cl_device_integer_dot_product_capabilities_khr>(
                    (CL_DEVICE_INTEGER_DOT_PRODUCT_INPUT_4x8BIT_PACKED_KHR
                        | CL_DEVICE_INTEGER_DOT_PRODUCT_INPUT_4x8BIT_KHR)
                        .into(),
                )
            }
            CL_DEVICE_INTEGER_DOT_PRODUCT_ACCELERATION_PROPERTIES_8BIT_KHR => v
                .write::<ClDevIdpAccelProps>({
                    let pack = dev.pack_32_4x8_supported();
                    let sdot = dev.sdot_4x8_supported() && pack;
                    let udot = dev.udot_4x8_supported() && pack;
                    let sudot = dev.sudot_4x8_supported() && pack;
                    let sdot_sat = dev.sdot_4x8_sat_supported() && pack;
                    let udot_sat = dev.udot_4x8_sat_supported() && pack;
                    let sudot_sat = dev.sudot_4x8_sat_supported() && pack;
                    IdpAccelProps::new(
                        sdot.into(),
                        udot.into(),
                        sudot.into(),
                        sdot_sat.into(),
                        udot_sat.into(),
                        sudot_sat.into(),
                    )
                }),
            CL_DEVICE_INTEGER_DOT_PRODUCT_ACCELERATION_PROPERTIES_4x8BIT_PACKED_KHR => {
                v.write::<ClDevIdpAccelProps>({
                    IdpAccelProps::new(
                        dev.sdot_4x8_supported().into(),
                        dev.udot_4x8_supported().into(),
                        dev.sudot_4x8_supported().into(),
                        dev.sdot_4x8_sat_supported().into(),
                        dev.udot_4x8_sat_supported().into(),
                        dev.sudot_4x8_sat_supported().into(),
                    )
                })
            }

            CL_DEVICE_LATEST_CONFORMANCE_VERSION_PASSED => {
                v.write::<&CStr>(dev.screen().cl_cts_version())
            }
            CL_DEVICE_LINKER_AVAILABLE => v.write::<bool>(true),
            CL_DEVICE_LOCAL_MEM_SIZE => v.write::<cl_ulong>(dev.local_mem_size()),
            // TODO add query for CL_LOCAL vs CL_GLOBAL
            CL_DEVICE_LOCAL_MEM_TYPE => v.write::<cl_device_local_mem_type>(CL_GLOBAL),
            CL_DEVICE_LUID_KHR => v.write::<[cl_uchar; CL_LUID_SIZE_KHR as usize]>(
                dev.screen().device_luid().unwrap_or_default(),
            ),
            CL_DEVICE_LUID_VALID_KHR => {
                v.write::<cl_bool>(dev.screen().device_luid().is_some().into())
            }
            CL_DEVICE_MAX_CLOCK_FREQUENCY => v.write::<cl_uint>(dev.max_clock_freq()),
            CL_DEVICE_MAX_COMPUTE_UNITS => v.write::<cl_uint>(dev.max_compute_units()),
            // TODO atm implemented as mem_const
            CL_DEVICE_MAX_CONSTANT_ARGS => v.write::<cl_uint>(dev.const_max_count()),
            CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE => v.write::<cl_ulong>(dev.const_max_size()),
            CL_DEVICE_MAX_GLOBAL_VARIABLE_SIZE => v.write::<usize>(0),
            CL_DEVICE_MAX_MEM_ALLOC_SIZE => v.write::<cl_ulong>(dev.max_mem_alloc()),
            CL_DEVICE_MAX_NUM_SUB_GROUPS => v.write::<cl_uint>(if dev.subgroups_supported() {
                dev.max_subgroups()
            } else {
                0
            }),
            CL_DEVICE_MAX_ON_DEVICE_EVENTS => v.write::<cl_uint>(0),
            CL_DEVICE_MAX_ON_DEVICE_QUEUES => v.write::<cl_uint>(0),
            CL_DEVICE_MAX_PARAMETER_SIZE => v.write::<usize>(dev.param_max_size()),
            CL_DEVICE_MAX_PIPE_ARGS => v.write::<cl_uint>(0),
            CL_DEVICE_MAX_READ_IMAGE_ARGS => v.write::<cl_uint>(dev.caps.max_read_images),
            CL_DEVICE_MAX_READ_WRITE_IMAGE_ARGS => v.write::<cl_uint>(if dev.caps.has_rw_images {
                dev.caps.max_write_images
            } else {
                0
            }),
            CL_DEVICE_MAX_SAMPLERS => v.write::<cl_uint>(dev.max_samplers()),
            CL_DEVICE_MAX_WORK_GROUP_SIZE => v.write::<usize>(dev.max_threads_per_block()),
            CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS => v.write::<cl_uint>(dev.max_grid_dimensions()),
            CL_DEVICE_MAX_WORK_ITEM_SIZES => v.write::<Vec<usize>>(dev.max_block_sizes()),
            CL_DEVICE_MAX_WRITE_IMAGE_ARGS => v.write::<cl_uint>(dev.caps.max_write_images),
            // TODO proper retrival from devices
            CL_DEVICE_MEM_BASE_ADDR_ALIGN => v.write::<cl_uint>(0x1000),
            CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE => {
                v.write::<cl_uint>(16 * size_of::<cl_ulong>() as cl_uint)
            }
            CL_DEVICE_NAME => v.write::<&CStr>(dev.screen().name()),
            CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR => v.write::<cl_uint>(1),
            CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE => v.write::<cl_uint>(dev.fp64_supported().into()),
            CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT => v.write::<cl_uint>(1),
            CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF => v.write::<cl_uint>(dev.fp16_supported().into()),
            CL_DEVICE_NATIVE_VECTOR_WIDTH_INT => v.write::<cl_uint>(1),
            CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG => v.write::<cl_uint>(1),
            CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT => v.write::<cl_uint>(1),
            CL_DEVICE_NODE_MASK_KHR => {
                v.write::<cl_uint>(dev.screen().device_node_mask().unwrap_or_default())
            }
            CL_DEVICE_NON_UNIFORM_WORK_GROUP_SUPPORT => v.write::<bool>(false),
            CL_DEVICE_NUMERIC_VERSION => v.write::<cl_version>(dev.cl_version.into()),
            CL_DEVICE_OPENCL_C_ALL_VERSIONS => v.write::<&[cl_name_version]>(&dev.clc_versions),
            CL_DEVICE_OPENCL_C_FEATURES => v.write::<&[cl_name_version]>(&dev.clc_features),
            CL_DEVICE_OPENCL_C_NUMERIC_VERSION_KHR => {
                v.write::<cl_version_khr>(dev.clc_version.into())
            }
            CL_DEVICE_OPENCL_C_VERSION => {
                v.write::<&str>(&format!("OpenCL C {} ", dev.clc_version.api_str()))
            }
            // TODO subdevice support
            CL_DEVICE_PARENT_DEVICE => v.write::<cl_device_id>(cl_device_id::from_ptr(ptr::null())),
            CL_DEVICE_PARTITION_AFFINITY_DOMAIN => v.write::<cl_device_affinity_domain>(0),
            CL_DEVICE_PARTITION_MAX_SUB_DEVICES => v.write::<cl_uint>(0),
            CL_DEVICE_PARTITION_PROPERTIES => v.write::<&[cl_device_partition_property]>(&[0]),
            CL_DEVICE_PARTITION_TYPE => v.write::<&[cl_device_partition_property]>(&[]),
            CL_DEVICE_PCI_BUS_INFO_KHR => {
                v.write::<cl_device_pci_bus_info_khr>(dev.pci_info().ok_or(CL_INVALID_VALUE)?)
            }
            CL_DEVICE_PIPE_MAX_ACTIVE_RESERVATIONS => v.write::<cl_uint>(0),
            CL_DEVICE_PIPE_MAX_PACKET_SIZE => v.write::<cl_uint>(0),
            CL_DEVICE_PIPE_SUPPORT => v.write::<bool>(false),
            CL_DEVICE_PLATFORM => v.write::<cl_platform_id>(Platform::get().as_ptr()),
            CL_DEVICE_PREFERRED_GLOBAL_ATOMIC_ALIGNMENT => v.write::<cl_uint>(0),
            CL_DEVICE_PREFERRED_INTEROP_USER_SYNC => v.write::<bool>(true),
            CL_DEVICE_PREFERRED_LOCAL_ATOMIC_ALIGNMENT => v.write::<cl_uint>(0),
            CL_DEVICE_PREFERRED_PLATFORM_ATOMIC_ALIGNMENT => v.write::<cl_uint>(0),
            CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR => v.write::<cl_uint>(1),
            CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE => {
                v.write::<cl_uint>(dev.fp64_supported().into())
            }
            CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT => v.write::<cl_uint>(1),
            CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF => {
                v.write::<cl_uint>(dev.fp16_supported().into())
            }
            CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT => v.write::<cl_uint>(1),
            CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG => v.write::<cl_uint>(1),
            CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT => v.write::<cl_uint>(1),
            CL_DEVICE_PREFERRED_WORK_GROUP_SIZE_MULTIPLE => {
                v.write::<usize>(dev.subgroup_sizes()[0])
            }
            CL_DEVICE_PRINTF_BUFFER_SIZE => v.write::<usize>(dev.printf_buffer_size()),
            CL_DEVICE_PROFILE => v.write::<&CStr>(if dev.embedded {
                c"EMBEDDED_PROFILE"
            } else {
                c"FULL_PROFILE"
            }),
            CL_DEVICE_PROFILING_TIMER_RESOLUTION => {
                v.write::<usize>(dev.caps.timer_resolution as usize)
            }
            CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE => v.write::<cl_uint>(0),
            CL_DEVICE_QUEUE_ON_DEVICE_PREFERRED_SIZE => v.write::<cl_uint>(0),
            CL_DEVICE_QUEUE_ON_DEVICE_PROPERTIES => v.write::<cl_command_queue_properties>(0),
            CL_DEVICE_QUEUE_ON_HOST_PROPERTIES => v.write::<cl_command_queue_properties>(
                (CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE).into(),
            ),
            CL_DEVICE_REFERENCE_COUNT => v.write::<cl_uint>(1),
            CL_DEVICE_SHARED_SYSTEM_MEM_CAPABILITIES_INTEL => {
                v.write::<cl_device_unified_shared_memory_capabilities_intel>(0)
            }
            CL_DEVICE_SINGLE_DEVICE_SHARED_MEM_CAPABILITIES_INTEL => {
                v.write::<cl_device_unified_shared_memory_capabilities_intel>(0)
            }
            CL_DEVICE_SINGLE_FP_CONFIG => v.write::<cl_device_fp_config>(
                (CL_FP_ROUND_TO_NEAREST | CL_FP_INF_NAN) as cl_device_fp_config,
            ),
            CL_DEVICE_SUB_GROUP_INDEPENDENT_FORWARD_PROGRESS => v.write::<bool>(false),
            CL_DEVICE_SUB_GROUP_SIZES_INTEL => {
                v.write::<Vec<usize>>(if dev.subgroups_supported() {
                    dev.subgroup_sizes()
                } else {
                    vec![0; 1]
                })
            }
            CL_DEVICE_SVM_CAPABILITIES | CL_DEVICE_SVM_CAPABILITIES_ARM => {
                v.write::<cl_device_svm_capabilities>(
                    if dev.svm_supported() {
                        CL_DEVICE_SVM_COARSE_GRAIN_BUFFER
                            | CL_DEVICE_SVM_FINE_GRAIN_BUFFER
                            | CL_DEVICE_SVM_FINE_GRAIN_SYSTEM
                    } else {
                        0
                    }
                    .into(),
                )
            }
            CL_DEVICE_TYPE => {
                // CL_DEVICE_TYPE_DEFAULT ... will never be returned in CL_DEVICE_TYPE for any
                // OpenCL device.
                v.write::<cl_device_type>((dev.device_type & !CL_DEVICE_TYPE_DEFAULT).into())
            }
            CL_DEVICE_UUID_KHR => v.write::<[cl_uchar; CL_UUID_SIZE_KHR as usize]>(
                dev.screen().device_uuid().unwrap_or_default(),
            ),
            CL_DEVICE_VENDOR => v.write::<&CStr>(dev.screen().device_vendor()),
            CL_DEVICE_VENDOR_ID => v.write::<cl_uint>(dev.vendor_id()),
            CL_DEVICE_VERSION => v.write::<&str>(&format!("OpenCL {} ", dev.cl_version.api_str())),
            CL_DRIVER_UUID_KHR => v.write::<[cl_char; CL_UUID_SIZE_KHR as usize]>(
                dev.screen().driver_uuid().unwrap_or_default(),
            ),
            CL_DRIVER_VERSION => v.write::<&CStr>(unsafe { CStr::from_ptr(mesa_version_string()) }),
            CL_DEVICE_WORK_GROUP_COLLECTIVE_FUNCTIONS_SUPPORT => v.write::<bool>(false),
            // CL_INVALID_VALUE if param_name is not one of the supported values
            // CL_INVALID_VALUE [...] if param_name is a value that is available as an extension and the corresponding extension is not supported by the device.
            _ => Err(CL_INVALID_VALUE),
        }
    }
}

#[cl_entrypoint(clGetDeviceIDs)]
fn get_device_ids(
    platform: cl_platform_id,
    device_type: cl_device_type,
    num_entries: cl_uint,
    devices: *mut cl_device_id,
    num_devices: *mut cl_uint,
) -> CLResult<()> {
    // CL_INVALID_PLATFORM if platform is not a valid platform.
    platform.get_ref()?;

    // CL_INVALID_DEVICE_TYPE if device_type is not a valid value.
    check_cl_device_type(device_type)?;

    // CL_INVALID_VALUE if num_entries is equal to zero and devices is not NULL
    if num_entries == 0 && !devices.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // CL_INVALID_VALUE [...] if both num_devices and devices are NULL.
    if num_devices.is_null() && devices.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    let devs = get_devs_for_type(device_type);
    // CL_DEVICE_NOT_FOUND if no OpenCL devices that matched device_type were found
    if devs.is_empty() {
        return Err(CL_DEVICE_NOT_FOUND);
    }

    // num_devices returns the number of OpenCL devices available that match device_type. If
    // num_devices is NULL, this argument is ignored.
    num_devices.write_checked(devs.len() as cl_uint);

    if !devices.is_null() {
        let n = min(num_entries as usize, devs.len());

        #[allow(clippy::needless_range_loop)]
        for i in 0..n {
            unsafe {
                *devices.add(i) = cl_device_id::from_ptr(devs[i]);
            }
        }
    }

    Ok(())
}

#[cl_entrypoint(clRetainDevice)]
fn retain_device(_device: cl_device_id) -> CLResult<()> {
    Ok(())
}

#[cl_entrypoint(clReleaseDevice)]
fn release_device(_device: cl_device_id) -> CLResult<()> {
    Ok(())
}

#[cl_entrypoint(clCreateSubDevices)]
fn create_sub_devices(
    _device: cl_device_id,
    _properties: *const cl_device_partition_property,
    _num_devices: cl_uint,
    _out_devices: *mut cl_device_id,
    _num_devices_ret: *mut cl_uint,
) -> CLResult<()> {
    // CL_INVALID_VALUE if values specified in properties are not valid or
    // if values specified in properties are valid but not supported by the
    // device.
    Err(CL_INVALID_VALUE)
}

#[cl_entrypoint(clGetDeviceAndHostTimer)]
fn get_device_and_host_timer(
    device: cl_device_id,
    device_timestamp: *mut cl_ulong,
    host_timestamp: *mut cl_ulong,
) -> CLResult<()> {
    if device_timestamp.is_null() {
        // CL_INVALID_VALUE if host_timestamp or device_timestamp is NULL
        return Err(CL_INVALID_VALUE);
    }

    get_host_timer(device, host_timestamp)?;
    // There is a requirement that the two timestamps
    // are synchronised, but don't need to be the same,
    // but as it is, the same timestamp is the best to
    // use for both

    // Safe because null check on device_timestamp above
    // and host_timestamp null check in get_host_timer
    unsafe {
        *device_timestamp = *host_timestamp;
    };

    Ok(())
}

#[cl_entrypoint(clGetHostTimer)]
fn get_host_timer(device_id: cl_device_id, host_timestamp: *mut cl_ulong) -> CLResult<()> {
    if host_timestamp.is_null() {
        // CL_INVALID_VALUE if host_timestamp is NULL
        return Err(CL_INVALID_VALUE);
    }

    let device = Device::ref_from_raw(device_id)?;

    if !device.caps.has_timestamp {
        // CL_INVALID_OPERATION if the platform associated with device does not support device and host timer synchronization
        return Err(CL_INVALID_OPERATION);
    }

    // Currently the best clock we have for the host_timestamp
    host_timestamp.write_checked(device.screen().get_timestamp());

    Ok(())
}

#[cl_entrypoint(clSetDefaultDeviceCommandQueue)]
fn set_default_device_command_queue(
    _context: cl_context,
    _device: cl_device_id,
    _command_queue: cl_command_queue,
) -> CLResult<()> {
    Err(CL_INVALID_OPERATION)
}
