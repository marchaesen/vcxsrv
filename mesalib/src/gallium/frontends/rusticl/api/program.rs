use crate::api::icd::*;
use crate::api::types::*;
use crate::api::util::*;
use crate::core::device::*;
use crate::core::program::*;

use mesa_rust::compiler::clc::*;
use mesa_rust_util::string::*;
use rusticl_opencl_gen::*;

use std::ffi::CStr;
use std::ffi::CString;
use std::os::raw::c_char;
use std::ptr;
use std::slice;
use std::sync::Arc;

impl CLInfo<cl_program_info> for cl_program {
    fn query(&self, q: cl_program_info, vals: &[u8]) -> CLResult<Vec<u8>> {
        let prog = self.get_ref()?;
        Ok(match q {
            CL_PROGRAM_BINARIES => cl_prop::<Vec<*mut u8>>(prog.binaries(vals)),
            CL_PROGRAM_BINARY_SIZES => cl_prop::<Vec<usize>>(prog.bin_sizes()),
            CL_PROGRAM_CONTEXT => {
                // Note we use as_ptr here which doesn't increase the reference count.
                let ptr = Arc::as_ptr(&prog.context);
                cl_prop::<cl_context>(cl_context::from_ptr(ptr))
            }
            CL_PROGRAM_DEVICES => {
                cl_prop::<&Vec<cl_device_id>>(
                    &prog
                        .devs
                        .iter()
                        .map(|d| {
                            // Note we use as_ptr here which doesn't increase the reference count.
                            cl_device_id::from_ptr(Arc::as_ptr(d))
                        })
                        .collect(),
                )
            }
            CL_PROGRAM_IL => prog.il.clone(),
            CL_PROGRAM_KERNEL_NAMES => cl_prop::<String>(prog.kernels().join(";")),
            CL_PROGRAM_NUM_DEVICES => cl_prop::<cl_uint>(prog.devs.len() as cl_uint),
            CL_PROGRAM_NUM_KERNELS => cl_prop::<usize>(prog.kernels().len()),
            CL_PROGRAM_REFERENCE_COUNT => cl_prop::<cl_uint>(self.refcnt()?),
            CL_PROGRAM_SCOPE_GLOBAL_CTORS_PRESENT => cl_prop::<cl_bool>(CL_FALSE),
            CL_PROGRAM_SCOPE_GLOBAL_DTORS_PRESENT => cl_prop::<cl_bool>(CL_FALSE),
            CL_PROGRAM_SOURCE => cl_prop::<&CStr>(prog.src.as_c_str()),
            // CL_INVALID_VALUE if param_name is not one of the supported values
            _ => return Err(CL_INVALID_VALUE),
        })
    }
}

impl CLInfoObj<cl_program_build_info, cl_device_id> for cl_program {
    fn query(&self, d: cl_device_id, q: cl_program_build_info) -> CLResult<Vec<u8>> {
        let prog = self.get_ref()?;
        let dev = d.get_arc()?;
        Ok(match q {
            CL_PROGRAM_BINARY_TYPE => cl_prop::<cl_program_binary_type>(prog.bin_type(&dev)),
            CL_PROGRAM_BUILD_GLOBAL_VARIABLE_TOTAL_SIZE => cl_prop::<usize>(0),
            CL_PROGRAM_BUILD_LOG => cl_prop::<String>(prog.log(&dev)),
            CL_PROGRAM_BUILD_OPTIONS => cl_prop::<String>(prog.options(&dev)),
            CL_PROGRAM_BUILD_STATUS => cl_prop::<cl_build_status>(prog.status(&dev)),
            // CL_INVALID_VALUE if param_name is not one of the supported values
            _ => return Err(CL_INVALID_VALUE),
        })
    }
}

fn validate_devices(
    device_list: *const cl_device_id,
    num_devices: cl_uint,
    default: &[Arc<Device>],
) -> CLResult<Vec<Arc<Device>>> {
    let mut devs = cl_device_id::get_arc_vec_from_arr(device_list, num_devices)?;

    // If device_list is a NULL value, the compile is performed for all devices associated with
    // program.
    if devs.is_empty() {
        devs = default.to_vec();
    }

    Ok(devs)
}

fn call_cb(
    pfn_notify: Option<ProgramCB>,
    program: cl_program,
    user_data: *mut ::std::os::raw::c_void,
) {
    if let Some(cb) = pfn_notify {
        unsafe { cb(program, user_data) };
    }
}

pub fn create_program_with_source(
    context: cl_context,
    count: cl_uint,
    strings: *mut *const c_char,
    lengths: *const usize,
) -> CLResult<cl_program> {
    let c = context.get_arc()?;

    // CL_INVALID_VALUE if count is zero or if strings ...
    if count == 0 || strings.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // ... or any entry in strings is NULL.
    let srcs = unsafe { slice::from_raw_parts(strings, count as usize) };
    if srcs.contains(&ptr::null()) {
        return Err(CL_INVALID_VALUE);
    }

    let mut source = String::new();
    // we don't want encoding or any other problems with the source to prevent compilations, so
    // just use CString::from_vec_unchecked and to_string_lossy
    for i in 0..count as usize {
        unsafe {
            if lengths.is_null() || *lengths.add(i) == 0 {
                source.push_str(&CStr::from_ptr(*strings.add(i)).to_string_lossy());
            } else {
                let l = *lengths.add(i);
                let arr = slice::from_raw_parts(*strings.add(i).cast(), l);
                source.push_str(&CString::from_vec_unchecked(arr.to_vec()).to_string_lossy());
            }
        }
    }

    Ok(cl_program::from_arc(Program::new(
        &c,
        &c.devs,
        CString::new(source).map_err(|_| CL_INVALID_VALUE)?,
    )))
}

pub fn create_program_with_binary(
    context: cl_context,
    num_devices: cl_uint,
    device_list: *const cl_device_id,
    lengths: *const usize,
    binaries: *mut *const ::std::os::raw::c_uchar,
    binary_status: *mut cl_int,
) -> CLResult<cl_program> {
    let c = context.get_arc()?;
    let devs = cl_device_id::get_arc_vec_from_arr(device_list, num_devices)?;

    // CL_INVALID_VALUE if device_list is NULL or num_devices is zero.
    if devs.is_empty() {
        return Err(CL_INVALID_VALUE);
    }

    // CL_INVALID_VALUE if lengths or binaries is NULL
    if lengths.is_null() || binaries.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // CL_INVALID_DEVICE if any device in device_list is not in the list of devices associated with
    // context.
    if !devs.iter().all(|d| c.devs.contains(d)) {
        return Err(CL_INVALID_DEVICE);
    }

    let lengths = unsafe { slice::from_raw_parts(lengths, num_devices as usize) };
    let binaries = unsafe { slice::from_raw_parts(binaries, num_devices as usize) };

    // now device specific stuff
    let mut err = 0;
    let mut bins: Vec<&[u8]> = vec![&[]; num_devices as usize];
    for i in 0..num_devices as usize {
        let mut dev_err = 0;

        // CL_INVALID_VALUE if lengths[i] is zero or if binaries[i] is a NULL value
        if lengths[i] == 0 || binaries[i].is_null() {
            dev_err = CL_INVALID_VALUE;
        }

        if !binary_status.is_null() {
            unsafe { binary_status.add(i).write(dev_err) };
        }

        // just return the last one
        err = dev_err;
        bins[i] = unsafe { slice::from_raw_parts(binaries[i], lengths[i] as usize) };
    }

    if err != 0 {
        return Err(err);
    }

    Ok(cl_program::from_arc(Program::from_bins(c, devs, &bins)))
    //• CL_INVALID_BINARY if an invalid program binary was encountered for any device. binary_status will return specific status for each device.
}

pub fn create_program_with_il(
    context: cl_context,
    il: *const ::std::os::raw::c_void,
    length: usize,
) -> CLResult<cl_program> {
    let _c = context.get_arc()?;

    // CL_INVALID_VALUE if il is NULL or if length is zero.
    if il.is_null() || length == 0 {
        return Err(CL_INVALID_VALUE);
    }

    //    let spirv = unsafe { slice::from_raw_parts(il.cast(), length) };
    // TODO SPIR-V
    //    Ok(cl_program::from_arc(Program::from_spirv(c, spirv)))
    Err(CL_INVALID_OPERATION)
}

pub fn build_program(
    program: cl_program,
    num_devices: cl_uint,
    device_list: *const cl_device_id,
    options: *const c_char,
    pfn_notify: Option<ProgramCB>,
    user_data: *mut ::std::os::raw::c_void,
) -> CLResult<()> {
    let mut res = true;
    let p = program.get_ref()?;
    let devs = validate_devices(device_list, num_devices, &p.devs)?;

    check_cb(&pfn_notify, user_data)?;

    // CL_INVALID_OPERATION if there are kernel objects attached to program.
    if p.active_kernels() {
        return Err(CL_INVALID_OPERATION);
    }

    // CL_BUILD_PROGRAM_FAILURE if there is a failure to build the program executable. This error
    // will be returned if clBuildProgram does not return until the build has completed.
    for dev in devs {
        res &= p.build(&dev, c_string_to_string(options));
    }

    call_cb(pfn_notify, program, user_data);

    //• CL_INVALID_BINARY if program is created with clCreateProgramWithBinary and devices listed in device_list do not have a valid program binary loaded.
    //• CL_INVALID_BUILD_OPTIONS if the build options specified by options are invalid.
    //• CL_INVALID_OPERATION if the build of a program executable for any of the devices listed in device_list by a previous call to clBuildProgram for program has not completed.
    //• CL_INVALID_OPERATION if program was not created with clCreateProgramWithSource, clCreateProgramWithIL or clCreateProgramWithBinary.

    if res {
        Ok(())
    } else {
        Err(CL_BUILD_PROGRAM_FAILURE)
    }
}

pub fn compile_program(
    program: cl_program,
    num_devices: cl_uint,
    device_list: *const cl_device_id,
    options: *const c_char,
    num_input_headers: cl_uint,
    input_headers: *const cl_program,
    header_include_names: *mut *const c_char,
    pfn_notify: Option<ProgramCB>,
    user_data: *mut ::std::os::raw::c_void,
) -> CLResult<()> {
    let mut res = true;
    let p = program.get_ref()?;
    let devs = validate_devices(device_list, num_devices, &p.devs)?;

    check_cb(&pfn_notify, user_data)?;

    // CL_INVALID_VALUE if num_input_headers is zero and header_include_names or input_headers are
    // not NULL or if num_input_headers is not zero and header_include_names or input_headers are
    // NULL.
    if num_input_headers == 0 && (!header_include_names.is_null() || !input_headers.is_null())
        || num_input_headers != 0 && (header_include_names.is_null() || input_headers.is_null())
    {
        return Err(CL_INVALID_VALUE);
    }

    let mut headers = Vec::new();
    for h in 0..num_input_headers as usize {
        unsafe {
            headers.push(spirv::CLCHeader {
                name: CStr::from_ptr(*header_include_names.add(h)).to_owned(),
                source: &(*input_headers.add(h)).get_ref()?.src,
            });
        }
    }

    // CL_INVALID_OPERATION if program has no source or IL available, i.e. it has not been created
    // with clCreateProgramWithSource or clCreateProgramWithIL.
    if p.is_binary() {
        return Err(CL_INVALID_OPERATION);
    }

    // CL_INVALID_OPERATION if there are kernel objects attached to program.
    if p.active_kernels() {
        return Err(CL_INVALID_OPERATION);
    }

    // CL_COMPILE_PROGRAM_FAILURE if there is a failure to compile the program source. This error
    // will be returned if clCompileProgram does not return until the compile has completed.
    for dev in devs {
        res &= p.compile(&dev, c_string_to_string(options), &headers);
    }

    call_cb(pfn_notify, program, user_data);

    // • CL_INVALID_COMPILER_OPTIONS if the compiler options specified by options are invalid.
    // • CL_INVALID_OPERATION if the compilation or build of a program executable for any of the devices listed in device_list by a previous call to clCompileProgram or clBuildProgram for program has not completed.

    if res {
        Ok(())
    } else {
        Err(CL_COMPILE_PROGRAM_FAILURE)
    }
}

pub fn link_program(
    context: cl_context,
    num_devices: cl_uint,
    device_list: *const cl_device_id,
    options: *const ::std::os::raw::c_char,
    num_input_programs: cl_uint,
    input_programs: *const cl_program,
    pfn_notify: Option<ProgramCB>,
    user_data: *mut ::std::os::raw::c_void,
) -> CLResult<(cl_program, cl_int)> {
    let c = context.get_arc()?;
    let devs = validate_devices(device_list, num_devices, &c.devs)?;
    let progs = cl_program::get_arc_vec_from_arr(input_programs, num_input_programs)?;

    check_cb(&pfn_notify, user_data)?;

    // CL_INVALID_VALUE if num_input_programs is zero and input_programs is NULL
    if progs.is_empty() {
        return Err(CL_INVALID_VALUE);
    }

    // CL_INVALID_DEVICE if any device in device_list is not in the list of devices associated with
    // context.
    if !devs.iter().all(|d| c.devs.contains(d)) {
        return Err(CL_INVALID_DEVICE);
    }

    // CL_INVALID_OPERATION if the compilation or build of a program executable for any of the
    // devices listed in device_list by a previous call to clCompileProgram or clBuildProgram for
    // program has not completed.
    for d in &devs {
        if progs
            .iter()
            .map(|p| p.status(d))
            .any(|s| s != CL_BUILD_SUCCESS as cl_build_status)
        {
            return Err(CL_INVALID_OPERATION);
        }
    }

    // CL_LINK_PROGRAM_FAILURE if there is a failure to link the compiled binaries and/or libraries.
    let res = Program::link(c, &devs, &progs, c_string_to_string(options));
    let code = if devs
        .iter()
        .map(|d| res.status(d))
        .all(|s| s == CL_BUILD_SUCCESS as cl_build_status)
    {
        CL_SUCCESS as cl_int
    } else {
        CL_LINK_PROGRAM_FAILURE
    };

    let res = cl_program::from_arc(res);

    call_cb(pfn_notify, res, user_data);
    Ok((res, code))

    //• CL_INVALID_LINKER_OPTIONS if the linker options specified by options are invalid.
    //• CL_INVALID_OPERATION if the rules for devices containing compiled binaries or libraries as described in input_programs argument above are not followed.
}

pub fn set_program_specialization_constant(
    program: cl_program,
    _spec_id: cl_uint,
    _spec_size: usize,
    spec_value: *const ::std::os::raw::c_void,
) -> CLResult<()> {
    let _program = program.get_ref()?;

    // CL_INVALID_PROGRAM if program is not a valid program object created from an intermediate
    // language (e.g. SPIR-V)
    // TODO: or if the intermediate language does not support specialization constants.
    //    if program.il.is_empty() {
    //        Err(CL_INVALID_PROGRAM)?
    //    }

    // TODO: CL_INVALID_VALUE if spec_size does not match the size of the specialization constant in the module,

    // or if spec_value is NULL.
    if spec_value.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    Err(CL_INVALID_OPERATION)

    //• CL_INVALID_SPEC_ID if spec_id is not a valid specialization constant identifier.
}

pub fn set_program_release_callback(
    _program: cl_program,
    _pfn_notify: ::std::option::Option<ProgramCB>,
    _user_data: *mut ::std::os::raw::c_void,
) -> CLResult<()> {
    Err(CL_INVALID_OPERATION)
}
