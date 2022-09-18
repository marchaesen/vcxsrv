use crate::api::icd::CLResult;
use crate::api::icd::DISPATCH;
use crate::api::util::*;
use crate::core::version::*;

use mesa_rust_util::ptr::*;
use rusticl_opencl_gen::*;

#[repr(C)]
#[allow(non_camel_case_types)]
pub struct _cl_platform_id {
    dispatch: &'static cl_icd_dispatch,
    extensions: [cl_name_version; 1],
}

impl CLInfo<cl_platform_info> for cl_platform_id {
    fn query(&self, q: cl_platform_info, _: &[u8]) -> CLResult<Vec<u8>> {
        let p = self.get_ref()?;
        Ok(match q {
            // TODO spirv
            CL_PLATFORM_EXTENSIONS => cl_prop("cl_khr_icd"),
            //            CL_PLATFORM_EXTENSIONS => cl_prop("cl_khr_icd cl_khr_il_program"),
            CL_PLATFORM_EXTENSIONS_WITH_VERSION => {
                cl_prop::<Vec<cl_name_version>>(p.extensions.to_vec())
            }
            CL_PLATFORM_HOST_TIMER_RESOLUTION => cl_prop::<cl_ulong>(0),
            CL_PLATFORM_ICD_SUFFIX_KHR => cl_prop("MESA"),
            CL_PLATFORM_NAME => cl_prop("rusticl"),
            CL_PLATFORM_NUMERIC_VERSION => cl_prop::<cl_version>(CLVersion::Cl3_0 as u32),
            CL_PLATFORM_PROFILE => cl_prop("FULL_PROFILE"),
            CL_PLATFORM_VENDOR => cl_prop("Mesa/X.org"),
            // OpenCL<space><major_version.minor_version><space><platform-specific information>
            CL_PLATFORM_VERSION => cl_prop("OpenCL 3.0 "),
            // CL_INVALID_VALUE if param_name is not one of the supported values
            _ => return Err(CL_INVALID_VALUE),
        })
    }
}

static PLATFORM: _cl_platform_id = _cl_platform_id {
    dispatch: &DISPATCH,
    extensions: [
        mk_cl_version_ext(1, 0, 0, "cl_khr_icd"),
        // TODO spirv
        //        mk_cl_version_ext(1, 0, 0, "cl_khr_il_program"),
    ],
};

pub fn get_platform() -> cl_platform_id {
    &PLATFORM as *const crate::api::platform::_cl_platform_id
        as *mut ::rusticl_opencl_gen::_cl_platform_id
}

pub trait GetPlatformRef {
    fn get_ref(&self) -> CLResult<&'static _cl_platform_id>;
}

impl GetPlatformRef for cl_platform_id {
    fn get_ref(&self) -> CLResult<&'static _cl_platform_id> {
        if !self.is_null() && *self == get_platform() {
            Ok(&PLATFORM)
        } else {
            Err(CL_INVALID_PLATFORM)
        }
    }
}

pub fn get_platform_ids(
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

    // platforms returns a list of OpenCL platforms available for access through the Khronos ICD Loader.
    // The cl_platform_id values returned in platforms are ICD compatible and can be used to identify a
    // specific OpenCL platform. If the platforms argument is NULL, then this argument is ignored. The
    // number of OpenCL platforms returned is the minimum of the value specified by num_entries or the
    // number of OpenCL platforms available.
    platforms.write_checked(get_platform());

    // num_platforms returns the number of OpenCL platforms available. If num_platforms is NULL, then
    // this argument is ignored.
    num_platforms.write_checked(1);

    Ok(())
}

pub fn unload_platform_compiler(platform: cl_platform_id) -> CLResult<()> {
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
