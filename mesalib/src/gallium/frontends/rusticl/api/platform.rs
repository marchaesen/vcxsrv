use crate::api::icd::CLResult;
use crate::api::util::*;
use crate::core::platform::*;
use crate::core::version::*;

use mesa_rust_util::ptr::*;
use rusticl_opencl_gen::*;
use rusticl_proc_macros::cl_entrypoint;
use rusticl_proc_macros::cl_info_entrypoint;

use std::ffi::CStr;

#[cl_info_entrypoint(clGetPlatformInfo)]
unsafe impl CLInfo<cl_platform_info> for cl_platform_id {
    fn query(&self, q: cl_platform_info, v: CLInfoValue) -> CLResult<CLInfoRes> {
        self.get_ref()?;
        match q {
            CL_PLATFORM_EXTENSIONS => v.write::<&str>(&Platform::get().extension_string),
            CL_PLATFORM_EXTENSIONS_WITH_VERSION => {
                v.write::<&[cl_name_version]>(&Platform::get().extensions)
            }
            CL_PLATFORM_HOST_TIMER_RESOLUTION => v.write::<cl_ulong>(1),
            CL_PLATFORM_ICD_SUFFIX_KHR => v.write::<&CStr>(c"MESA"),
            CL_PLATFORM_NAME => v.write::<&CStr>(c"rusticl"),
            CL_PLATFORM_NUMERIC_VERSION => v.write::<cl_version>(CLVersion::Cl3_0.into()),
            CL_PLATFORM_PROFILE => v.write::<&CStr>(c"FULL_PROFILE"),
            CL_PLATFORM_VENDOR => v.write::<&CStr>(c"Mesa/X.org"),
            // OpenCL<space><major_version.minor_version><space><platform-specific information>
            CL_PLATFORM_VERSION => v.write::<&CStr>(c"OpenCL 3.0 "),
            // CL_INVALID_VALUE if param_name is not one of the supported values
            _ => Err(CL_INVALID_VALUE),
        }
    }
}

#[cl_entrypoint(clGetPlatformIDs)]
fn get_platform_ids(
    num_entries: cl_uint,
    platforms: *mut cl_platform_id,
    num_platforms: *mut cl_uint,
) -> CLResult<()> {
    // CL_INVALID_VALUE if num_entries is equal to zero and platforms is not NULL
    if num_entries == 0 && !platforms.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // or if both num_platforms and platforms are NULL."
    if num_platforms.is_null() && platforms.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // run initialization code once
    Platform::init_once();

    // platforms returns a list of OpenCL platforms available for access through the Khronos ICD Loader.
    // The cl_platform_id values returned in platforms are ICD compatible and can be used to identify a
    // specific OpenCL platform. If the platforms argument is NULL, then this argument is ignored. The
    // number of OpenCL platforms returned is the minimum of the value specified by num_entries or the
    // number of OpenCL platforms available.
    platforms.write_checked(Platform::get().as_ptr());

    // num_platforms returns the number of OpenCL platforms available. If num_platforms is NULL, then
    // this argument is ignored.
    num_platforms.write_checked(1);

    Ok(())
}

#[cl_entrypoint(clUnloadPlatformCompiler)]
fn unload_platform_compiler(platform: cl_platform_id) -> CLResult<()> {
    platform.get_ref()?;
    // TODO unload the compiler
    Ok(())
}

#[test]
fn test_get_platform_info() {
    let mut s: usize = 0;
    let mut r = get_platform_info(
        ptr::null(),
        CL_PLATFORM_EXTENSIONS,
        0,
        ptr::null_mut(),
        &mut s,
    );
    assert!(r.is_ok());
    assert!(s > 0);

    let mut v: Vec<u8> = vec![0; s];
    r = get_platform_info(
        ptr::null(),
        CL_PLATFORM_EXTENSIONS,
        s,
        v.as_mut_ptr().cast(),
        &mut s,
    );

    assert!(r.is_ok());
    assert_eq!(s, v.len());
    assert!(!v[0..s - 2].contains(&0));
    assert_eq!(v[s - 1], 0);
}
