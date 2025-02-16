use crate::api::icd::CLResult;
use crate::api::icd::DISPATCH;
use crate::core::device::*;
use crate::core::version::*;

use mesa_rust_gen::*;
use rusticl_opencl_gen::*;

use std::env;
use std::ptr;
use std::ptr::addr_of;
use std::ptr::addr_of_mut;
use std::sync::Once;

/// Maximum size a pixel can be across all supported image formats.
pub const MAX_PIXEL_SIZE_BYTES: u64 = 4 * 4;

#[repr(C)]
pub struct Platform {
    dispatch: &'static cl_icd_dispatch,
    pub devs: Vec<Device>,
}

pub enum PerfDebugLevel {
    None,
    Once,
    Spam,
}

pub struct PlatformDebug {
    pub allow_invalid_spirv: bool,
    pub clc: bool,
    pub max_grid_size: u32,
    pub nir: bool,
    pub no_variants: bool,
    pub perf: PerfDebugLevel,
    pub program: bool,
    pub reuse_context: bool,
    pub sync_every_event: bool,
    pub validate_spirv: bool,
}

pub struct PlatformFeatures {
    pub fp16: bool,
    pub fp64: bool,
}

static PLATFORM_ENV_ONCE: Once = Once::new();
static PLATFORM_ONCE: Once = Once::new();

macro_rules! gen_cl_exts {
    (@COUNT $e:expr) => { 1 };
    (@COUNT $e:expr, $($es:expr),+) => { 1 + gen_cl_exts!(@COUNT $($es),*) };

    (@CONCAT $e:tt) => { $e };
    (@CONCAT $e:tt, $($es:tt),+) => { concat!($e, ' ', gen_cl_exts!(@CONCAT $($es),*)) };

    ([$(($major:expr, $minor:expr, $patch:expr, $ext:tt)$(,)?)+]) => {
        pub static PLATFORM_EXTENSION_STR: &str = concat!(gen_cl_exts!(@CONCAT $($ext),*));
        pub static PLATFORM_EXTENSIONS: [cl_name_version; gen_cl_exts!(@COUNT $($ext),*)] = [
            $(mk_cl_version_ext($major, $minor, $patch, $ext)),+
        ];
    }
}
gen_cl_exts!([
    (1, 0, 0, "cl_khr_byte_addressable_store"),
    (1, 0, 0, "cl_khr_create_command_queue"),
    (1, 0, 0, "cl_khr_expect_assume"),
    (1, 0, 0, "cl_khr_extended_versioning"),
    (1, 0, 0, "cl_khr_icd"),
    (1, 0, 0, "cl_khr_il_program"),
    (1, 0, 0, "cl_khr_spirv_no_integer_wrap_decoration"),
    (1, 0, 0, "cl_khr_suggested_local_work_size"),
]);

static mut PLATFORM: Platform = Platform {
    dispatch: &DISPATCH,
    devs: Vec::new(),
};
static mut PLATFORM_DBG: PlatformDebug = PlatformDebug {
    allow_invalid_spirv: false,
    clc: false,
    max_grid_size: 0,
    nir: false,
    no_variants: false,
    perf: PerfDebugLevel::None,
    program: false,
    reuse_context: true,
    sync_every_event: false,
    validate_spirv: false,
};
static mut PLATFORM_FEATURES: PlatformFeatures = PlatformFeatures {
    fp16: false,
    fp64: false,
};

fn load_env() {
    // SAFETY: no other references exist at this point
    let debug = unsafe { &mut *addr_of_mut!(PLATFORM_DBG) };
    if let Ok(debug_flags) = env::var("RUSTICL_DEBUG") {
        for flag in debug_flags.split(',') {
            match flag {
                "allow_invalid_spirv" => debug.allow_invalid_spirv = true,
                "clc" => debug.clc = true,
                "nir" => debug.nir = true,
                "no_reuse_context" => debug.reuse_context = false,
                "no_variants" => debug.no_variants = true,
                "perf" => debug.perf = PerfDebugLevel::Once,
                "perfspam" => debug.perf = PerfDebugLevel::Spam,
                "program" => debug.program = true,
                "sync" => debug.sync_every_event = true,
                "validate" => debug.validate_spirv = true,
                "" => (),
                _ => eprintln!("Unknown RUSTICL_DEBUG flag found: {}", flag),
            }
        }
    }

    debug.max_grid_size = env::var("RUSTICL_MAX_WORK_GROUPS")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(u32::MAX);

    // SAFETY: no other references exist at this point
    let features = unsafe { &mut *addr_of_mut!(PLATFORM_FEATURES) };
    if let Ok(feature_flags) = env::var("RUSTICL_FEATURES") {
        for flag in feature_flags.split(',') {
            match flag {
                "fp16" => features.fp16 = true,
                "fp64" => features.fp64 = true,
                "" => (),
                _ => eprintln!("Unknown RUSTICL_FEATURES flag found: {}", flag),
            }
        }
    }
}

impl Platform {
    pub fn as_ptr(&self) -> cl_platform_id {
        ptr::from_ref(self) as cl_platform_id
    }

    pub fn get() -> &'static Self {
        debug_assert!(PLATFORM_ONCE.is_completed());
        // SAFETY: no mut references exist at this point
        unsafe { &*addr_of!(PLATFORM) }
    }

    pub fn dbg() -> &'static PlatformDebug {
        debug_assert!(PLATFORM_ENV_ONCE.is_completed());
        unsafe { &*addr_of!(PLATFORM_DBG) }
    }

    pub fn features() -> &'static PlatformFeatures {
        debug_assert!(PLATFORM_ENV_ONCE.is_completed());
        unsafe { &*addr_of!(PLATFORM_FEATURES) }
    }

    fn init(&mut self) {
        unsafe {
            glsl_type_singleton_init_or_ref();
        }

        self.devs = Device::all();
    }

    pub fn init_once() {
        PLATFORM_ENV_ONCE.call_once(load_env);
        // SAFETY: no concurrent static mut access due to std::Once
        #[allow(static_mut_refs)]
        PLATFORM_ONCE.call_once(|| unsafe { PLATFORM.init() });
    }
}

impl Drop for Platform {
    fn drop(&mut self) {
        unsafe {
            glsl_type_singleton_decref();
        }
    }
}

pub trait GetPlatformRef {
    fn get_ref(&self) -> CLResult<&'static Platform>;
}

impl GetPlatformRef for cl_platform_id {
    fn get_ref(&self) -> CLResult<&'static Platform> {
        if !self.is_null() && *self == Platform::get().as_ptr() {
            Ok(Platform::get())
        } else {
            Err(CL_INVALID_PLATFORM)
        }
    }
}

#[macro_export]
macro_rules! perf_warning {
    (@PRINT $format:tt, $($arg:tt)*) => {
        eprintln!(std::concat!("=== Rusticl perf warning: ", $format, " ==="), $($arg)*)
    };

    ($format:tt $(, $arg:tt)*) => {
        match $crate::core::platform::Platform::dbg().perf {
            $crate::core::platform::PerfDebugLevel::Once => {
                static PERF_WARN_ONCE: std::sync::Once = std::sync::Once::new();
                PERF_WARN_ONCE.call_once(|| {
                    perf_warning!(@PRINT $format, $($arg)*);
                })
            },
            $crate::core::platform::PerfDebugLevel::Spam => perf_warning!(@PRINT $format, $($arg)*),
            _ => (),
        }
    };
}
