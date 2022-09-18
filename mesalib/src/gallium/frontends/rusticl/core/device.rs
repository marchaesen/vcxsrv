use crate::api::icd::*;
use crate::api::util::*;
use crate::core::format::*;
use crate::core::util::*;
use crate::core::version::*;
use crate::impl_cl_type_trait;

use mesa_rust::compiler::clc::*;
use mesa_rust::compiler::nir::*;
use mesa_rust::pipe::context::*;
use mesa_rust::pipe::device::load_screens;
use mesa_rust::pipe::fence::*;
use mesa_rust::pipe::resource::*;
use mesa_rust::pipe::screen::*;
use mesa_rust::pipe::transfer::*;
use mesa_rust_gen::*;
use rusticl_opencl_gen::*;

use std::cmp::max;
use std::cmp::min;
use std::collections::HashMap;
use std::convert::TryInto;
use std::env;
use std::os::raw::*;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::MutexGuard;

pub struct Device {
    pub base: CLObjectBase<CL_INVALID_DEVICE>,
    pub screen: Arc<PipeScreen>,
    pub cl_version: CLVersion,
    pub clc_version: CLVersion,
    pub clc_versions: Vec<cl_name_version>,
    pub custom: bool,
    pub embedded: bool,
    pub extension_string: String,
    pub extensions: Vec<cl_name_version>,
    pub clc_features: Vec<cl_name_version>,
    pub formats: HashMap<cl_image_format, HashMap<cl_mem_object_type, cl_mem_flags>>,
    pub lib_clc: NirShader,
    helper_ctx: Mutex<PipeContext>,
}

pub trait HelperContextWrapper {
    #[must_use]
    fn exec<F>(&self, func: F) -> PipeFence
    where
        F: Fn(&HelperContext);

    fn buffer_map_async(&self, res: &PipeResource, offset: i32, size: i32) -> PipeTransfer;
    fn texture_map_async(&self, res: &PipeResource, bx: &pipe_box) -> PipeTransfer;
    fn unmap(&self, tx: PipeTransfer);
}

pub struct HelperContext<'a> {
    lock: MutexGuard<'a, PipeContext>,
}

impl<'a> HelperContext<'a> {
    pub fn buffer_subdata(
        &self,
        res: &PipeResource,
        offset: c_uint,
        data: *const c_void,
        size: c_uint,
    ) {
        self.lock.buffer_subdata(res, offset, data, size)
    }

    pub fn texture_subdata(
        &self,
        res: &PipeResource,
        bx: &pipe_box,
        data: *const c_void,
        stride: u32,
        layer_stride: u32,
    ) {
        self.lock
            .texture_subdata(res, bx, data, stride, layer_stride)
    }
}

impl<'a> HelperContextWrapper for HelperContext<'a> {
    fn exec<F>(&self, func: F) -> PipeFence
    where
        F: Fn(&HelperContext),
    {
        func(self);
        self.lock.flush()
    }

    fn buffer_map_async(&self, res: &PipeResource, offset: i32, size: i32) -> PipeTransfer {
        self.lock.buffer_map(res, offset, size, false, RWFlags::RW)
    }

    fn texture_map_async(&self, res: &PipeResource, bx: &pipe_box) -> PipeTransfer {
        self.lock.texture_map(res, bx, false, RWFlags::RW)
    }

    fn unmap(&self, tx: PipeTransfer) {
        tx.with_ctx(&self.lock);
    }
}

impl_cl_type_trait!(cl_device_id, Device, CL_INVALID_DEVICE);

impl Device {
    fn new(screen: Arc<PipeScreen>) -> Option<Arc<Device>> {
        if !Self::check_valid(&screen) {
            return None;
        }

        let lib_clc = spirv::SPIRVBin::get_lib_clc(&screen);
        if lib_clc.is_none() {
            eprintln!("Libclc failed to load. Please make sure it is installed and provides spirv-mesa3d-.spv and/or spirv64-mesa3d-.spv");
        }

        let mut d = Self {
            base: CLObjectBase::new(),
            helper_ctx: Mutex::new(screen.create_context().unwrap()),
            screen: screen,
            cl_version: CLVersion::Cl3_0,
            clc_version: CLVersion::Cl3_0,
            clc_versions: Vec::new(),
            custom: false,
            embedded: false,
            extension_string: String::from(""),
            extensions: Vec::new(),
            clc_features: Vec::new(),
            formats: HashMap::new(),
            lib_clc: lib_clc?,
        };

        d.fill_format_tables();

        // check if we are embedded or full profile first
        d.embedded = d.check_embedded_profile();

        // check if we have to report it as a custom device
        d.custom = d.check_custom();

        // query supported extensions
        d.fill_extensions();

        // now figure out what version we are
        d.check_version();

        Some(Arc::new(d))
    }

    fn fill_format_tables(&mut self) {
        for f in FORMATS {
            let mut fs = HashMap::new();
            for t in CL_IMAGE_TYPES {
                let mut flags: cl_uint = 0;
                if self.screen.is_format_supported(
                    f.pipe,
                    cl_mem_type_to_texture_target(t),
                    PIPE_BIND_SAMPLER_VIEW,
                ) {
                    flags |= CL_MEM_READ_ONLY;
                }
                if self.screen.is_format_supported(
                    f.pipe,
                    cl_mem_type_to_texture_target(t),
                    PIPE_BIND_SHADER_IMAGE,
                ) {
                    flags |= CL_MEM_WRITE_ONLY;
                    // TODO: enable once we support it
                    // flags |= CL_MEM_KERNEL_READ_AND_WRITE;
                }
                if self.screen.is_format_supported(
                    f.pipe,
                    cl_mem_type_to_texture_target(t),
                    PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_SHADER_IMAGE,
                ) {
                    flags |= CL_MEM_READ_WRITE;
                }
                fs.insert(t, flags as cl_mem_flags);
            }
            self.formats.insert(f.cl_image_format, fs);
        }
    }

    fn check_valid(screen: &PipeScreen) -> bool {
        if screen.param(pipe_cap::PIPE_CAP_COMPUTE) == 0 ||
         // even though we use PIPE_SHADER_IR_NIR, PIPE_SHADER_IR_NIR_SERIALIZED marks CL support by the driver
         screen.shader_param(pipe_shader_type::PIPE_SHADER_COMPUTE, pipe_shader_cap::PIPE_SHADER_CAP_SUPPORTED_IRS) & (1 << (pipe_shader_ir::PIPE_SHADER_IR_NIR_SERIALIZED as i32)) == 0
        {
            return false;
        }

        // CL_DEVICE_MAX_PARAMETER_SIZE
        // For this minimum value, only a maximum of 128 arguments can be passed to a kernel
        if ComputeParam::<u64>::compute_param(
            screen,
            pipe_compute_cap::PIPE_COMPUTE_CAP_MAX_INPUT_SIZE,
        ) < 128
        {
            return false;
        }
        true
    }

    fn check_custom(&self) -> bool {
        // Max size of memory object allocation in bytes. The minimum value is
        // max(min(1024 × 1024 × 1024, 1/4th of CL_DEVICE_GLOBAL_MEM_SIZE), 32 × 1024 × 1024)
        // for devices that are not of type CL_DEVICE_TYPE_CUSTOM.
        let mut limit = min(1024 * 1024 * 1024, self.global_mem_size());
        limit = max(limit, 32 * 1024 * 1024);
        if self.max_mem_alloc() < limit {
            return true;
        }

        // CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS
        // The minimum value is 3 for devices that are not of type CL_DEVICE_TYPE_CUSTOM.
        if self.max_grid_dimensions() < 3 {
            return true;
        }

        if self.embedded {
            // CL_DEVICE_MAX_PARAMETER_SIZE
            // The minimum value is 256 bytes for devices that are not of type CL_DEVICE_TYPE_CUSTOM.
            if self.param_max_size() < 256 {
                return true;
            }

            // CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE
            // The minimum value is 1 KB for devices that are not of type CL_DEVICE_TYPE_CUSTOM.
            if self.const_max_size() < 1024 {
                return true;
            }

            // TODO
            // CL_DEVICE_MAX_CONSTANT_ARGS
            // The minimum value is 4 for devices that are not of type CL_DEVICE_TYPE_CUSTOM.

            // CL_DEVICE_LOCAL_MEM_SIZE
            // The minimum value is 1 KB for devices that are not of type CL_DEVICE_TYPE_CUSTOM.
            if self.local_mem_size() < 1024 {
                return true;
            }
        } else {
            // CL 1.0 spec:
            // CL_DEVICE_MAX_PARAMETER_SIZE
            // The minimum value is 256 for devices that are not of type CL_DEVICE_TYPE_CUSTOM.
            if self.param_max_size() < 256 {
                return true;
            }

            // CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE
            // The minimum value is 64 KB for devices that are not of type CL_DEVICE_TYPE_CUSTOM.
            if self.const_max_size() < 64 * 1024 {
                return true;
            }

            // TODO
            // CL_DEVICE_MAX_CONSTANT_ARGS
            // The minimum value is 8 for devices that are not of type CL_DEVICE_TYPE_CUSTOM.

            // CL 1.0 spec:
            // CL_DEVICE_LOCAL_MEM_SIZE
            // The minimum value is 16 KB for devices that are not of type CL_DEVICE_TYPE_CUSTOM.
            if self.local_mem_size() < 16 * 1024 {
                return true;
            }
        }

        false
    }

    fn check_embedded_profile(&self) -> bool {
        if self.image_supported() {
            // The minimum value is 16 if CL_DEVICE_IMAGE_SUPPORT is CL_TRUE
            if self.max_samplers() < 16 ||
            // The minimum value is 128 if CL_DEVICE_IMAGE_SUPPORT is CL_TRUE
            self.image_read_count() < 128 ||
            // The minimum value is 64 if CL_DEVICE_IMAGE_SUPPORT is CL_TRUE
            self.image_write_count() < 64 ||
            // The minimum value is 16384 if CL_DEVICE_IMAGE_SUPPORT is CL_TRUE
            self.image_2d_size() < 16384 ||
            // The minimum value is 2048 if CL_DEVICE_IMAGE_SUPPORT is CL_TRUE
            self.image_array_size() < 2048 ||
            // The minimum value is 65536 if CL_DEVICE_IMAGE_SUPPORT is CL_TRUE
            self.image_buffer_size() < 65536
            {
                return true;
            }

            // TODO check req formats
        }
        !self.long_supported()
    }

    fn parse_env_device_type() -> Option<cl_device_type> {
        let mut val = env::var("RUSTICL_DEVICE_TYPE").ok()?;
        val.make_ascii_lowercase();
        Some(
            match &*val {
                "accelerator" => CL_DEVICE_TYPE_ACCELERATOR,
                "cpu" => CL_DEVICE_TYPE_CPU,
                "custom" => CL_DEVICE_TYPE_CUSTOM,
                "gpu" => CL_DEVICE_TYPE_GPU,
                _ => return None,
            }
            .into(),
        )
    }

    fn parse_env_version() -> Option<CLVersion> {
        let val = env::var("RUSTICL_CL_VERSION").ok()?;
        let (major, minor) = val.split_once('.')?;
        let major = major.parse().ok()?;
        let minor = minor.parse().ok()?;
        mk_cl_version(major, minor, 0).try_into().ok()
    }

    // TODO add CLC checks
    fn check_version(&mut self) {
        let exts: Vec<&str> = self.extension_string.split(' ').collect();
        let mut res = CLVersion::Cl3_0;

        if self.embedded {
            if self.image_supported() {
                let supports_array_writes = !FORMATS
                    .iter()
                    .filter(|f| f.req_for_embeded_read_or_write)
                    .map(|f| self.formats.get(&f.cl_image_format).unwrap())
                    .map(|f| f.get(&CL_MEM_OBJECT_IMAGE2D_ARRAY).unwrap())
                    .any(|f| *f & cl_mem_flags::from(CL_MEM_WRITE_ONLY) == 0);
                if self.image_3d_size() < 2048 || !supports_array_writes {
                    res = CLVersion::Cl1_2;
                }
            }
        }

        // TODO: check image 1D, 1Dbuffer, 1Darray and 2Darray support explicitly
        if self.image_supported() {
            // The minimum value is 256 if CL_DEVICE_IMAGE_SUPPORT is CL_TRUE
            if self.image_array_size() < 256 ||
            // The minimum value is 2048 if CL_DEVICE_IMAGE_SUPPORT is CL_TRUE
            self.image_buffer_size() < 2048
            {
                res = CLVersion::Cl1_1;
            }
        }

        if self.embedded {
            // The minimum value for the EMBEDDED profile is 1 KB.
            if self.printf_buffer_size() < 1024 {
                res = CLVersion::Cl1_1;
            }
        } else {
            // The minimum value for the FULL profile is 1 MB.
            if self.printf_buffer_size() < 1024 * 1024 {
                res = CLVersion::Cl1_1;
            }
        }

        if !exts.contains(&"cl_khr_byte_addressable_store")
         || !exts.contains(&"cl_khr_global_int32_base_atomics")
         || !exts.contains(&"cl_khr_global_int32_extended_atomics")
         || !exts.contains(&"cl_khr_local_int32_base_atomics")
         || !exts.contains(&"cl_khr_local_int32_extended_atomics")
         // The following modifications are made to the OpenCL 1.1 platform layer and runtime (sections 4 and 5):
         // The minimum FULL_PROFILE value for CL_DEVICE_MAX_PARAMETER_SIZE increased from 256 to 1024 bytes
         || self.param_max_size() < 1024
         // The minimum FULL_PROFILE value for CL_DEVICE_LOCAL_MEM_SIZE increased from 16 KB to 32 KB.
         || self.local_mem_size() < 32 * 1024
        {
            res = CLVersion::Cl1_0;
        }

        if let Some(val) = Self::parse_env_version() {
            res = val;
        }

        if res >= CLVersion::Cl3_0 {
            self.clc_versions
                .push(mk_cl_version_ext(3, 0, 0, "OpenCL C"));
        }

        if res >= CLVersion::Cl1_2 {
            self.clc_versions
                .push(mk_cl_version_ext(1, 2, 0, "OpenCL C"));
        }

        if res >= CLVersion::Cl1_1 {
            self.clc_versions
                .push(mk_cl_version_ext(1, 1, 0, "OpenCL C"));
        }

        if res >= CLVersion::Cl1_0 {
            self.clc_versions
                .push(mk_cl_version_ext(1, 0, 0, "OpenCL C"));
        }

        self.cl_version = res;
        self.clc_version = min(CLVersion::Cl1_2, res);
    }

    fn fill_extensions(&mut self) {
        let mut exts_str: Vec<String> = Vec::new();
        let mut exts = Vec::new();
        let mut feats = Vec::new();
        let mut add_ext = |major, minor, patch, ext: &str, feat: &str| {
            if !ext.is_empty() {
                exts.push(mk_cl_version_ext(major, minor, patch, ext));
                exts_str.push(ext.to_owned());
            }

            if !feat.is_empty() {
                feats.push(mk_cl_version_ext(major, minor, patch, feat));
            }
        };

        // add extensions all drivers support
        add_ext(1, 0, 0, "cl_khr_byte_addressable_store", "");
        add_ext(1, 0, 0, "cl_khr_global_int32_base_atomics", "");
        add_ext(1, 0, 0, "cl_khr_global_int32_extended_atomics", "");
        // TODO spirv
        // add_ext(1, 0, 0, "cl_khr_il_program", "");
        add_ext(1, 0, 0, "cl_khr_local_int32_base_atomics", "");
        add_ext(1, 0, 0, "cl_khr_local_int32_extended_atomics", "");

        if self.doubles_supported() {
            add_ext(1, 0, 0, "cl_khr_fp64", "__opencl_c_fp64");
        }

        if self.long_supported() {
            let ext = if self.embedded { "cles_khr_int64" } else { "" };

            add_ext(1, 0, 0, ext, "__opencl_c_int64");
        }

        if self.image_supported() {
            add_ext(1, 0, 0, "", "__opencl_c_images");

            if self.image_read_write_supported() {
                add_ext(1, 0, 0, "", "__opencl_c_read_write_images");
            }

            if self.image_3d_write_supported() {
                add_ext(
                    1,
                    0,
                    0,
                    "cl_khr_3d_image_writes",
                    "__opencl_c_3d_image_writes",
                );
            }
        }

        self.extensions = exts;
        self.clc_features = feats;
        self.extension_string = exts_str.join(" ");
    }

    fn shader_param(&self, cap: pipe_shader_cap) -> i32 {
        self.screen
            .shader_param(pipe_shader_type::PIPE_SHADER_COMPUTE, cap)
    }

    pub fn all() -> Vec<Arc<Device>> {
        load_screens().into_iter().filter_map(Device::new).collect()
    }

    pub fn address_bits(&self) -> cl_uint {
        self.screen
            .compute_param(pipe_compute_cap::PIPE_COMPUTE_CAP_ADDRESS_BITS)
    }

    pub fn const_max_size(&self) -> cl_ulong {
        self.screen
            .param(pipe_cap::PIPE_CAP_MAX_SHADER_BUFFER_SIZE_UINT) as u64
    }

    pub fn device_type(&self, internal: bool) -> cl_device_type {
        if let Some(env) = Self::parse_env_device_type() {
            return env;
        }

        if self.custom {
            return CL_DEVICE_TYPE_CUSTOM as cl_device_type;
        }
        let mut res = match self.screen.device_type() {
            pipe_loader_device_type::PIPE_LOADER_DEVICE_SOFTWARE => CL_DEVICE_TYPE_CPU,
            pipe_loader_device_type::PIPE_LOADER_DEVICE_PCI => CL_DEVICE_TYPE_GPU,
            pipe_loader_device_type::PIPE_LOADER_DEVICE_PLATFORM => CL_DEVICE_TYPE_GPU,
            pipe_loader_device_type::NUM_PIPE_LOADER_DEVICE_TYPES => CL_DEVICE_TYPE_CUSTOM,
        };

        if internal && res == CL_DEVICE_TYPE_GPU {
            res |= CL_DEVICE_TYPE_DEFAULT;
        }

        res as cl_device_type
    }

    pub fn doubles_supported(&self) -> bool {
        false
        /*
        if self.screen.param(pipe_cap::PIPE_CAP_DOUBLES) == 0 {
            return false;
        }
        let nir_options = self
            .screen
            .nir_shader_compiler_options(pipe_shader_type::PIPE_SHADER_COMPUTE);
        !bit_check(
            unsafe { *nir_options }.lower_doubles_options as u32,
            nir_lower_doubles_options::nir_lower_fp64_full_software as u32,
        )
        */
    }

    pub fn long_supported(&self) -> bool {
        self.screen.param(pipe_cap::PIPE_CAP_INT64) == 1
    }

    pub fn global_mem_size(&self) -> cl_ulong {
        self.screen
            .compute_param(pipe_compute_cap::PIPE_COMPUTE_CAP_MAX_GLOBAL_SIZE)
    }

    pub fn image_2d_size(&self) -> usize {
        self.screen.param(pipe_cap::PIPE_CAP_MAX_TEXTURE_2D_SIZE) as usize
    }

    pub fn image_3d_size(&self) -> usize {
        1 << (self.screen.param(pipe_cap::PIPE_CAP_MAX_TEXTURE_3D_LEVELS) - 1)
    }

    pub fn image_3d_supported(&self) -> bool {
        self.screen.param(pipe_cap::PIPE_CAP_MAX_TEXTURE_3D_LEVELS) != 0
    }

    pub fn image_array_size(&self) -> usize {
        self.screen
            .param(pipe_cap::PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS) as usize
    }

    pub fn image_base_address_alignment(&self) -> cl_uint {
        0
    }

    pub fn image_buffer_size(&self) -> usize {
        self.screen
            .param(pipe_cap::PIPE_CAP_MAX_TEXEL_BUFFER_ELEMENTS_UINT) as usize
    }

    pub fn image_read_count(&self) -> cl_uint {
        self.shader_param(pipe_shader_cap::PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS) as cl_uint
    }

    pub fn image_supported(&self) -> bool {
        // TODO check CL_DEVICE_IMAGE_SUPPORT reqs
        self.shader_param(pipe_shader_cap::PIPE_SHADER_CAP_MAX_SHADER_IMAGES) != 0 &&
      // The minimum value is 8 if CL_DEVICE_IMAGE_SUPPORT is CL_TRUE
      self.image_read_count() >= 8 &&
      // The minimum value is 8 if CL_DEVICE_IMAGE_SUPPORT is CL_TRUE
      self.image_write_count() >= 8 &&
      // The minimum value is 2048 if CL_DEVICE_IMAGE_SUPPORT is CL_TRUE
      self.image_2d_size() >= 2048
    }

    pub fn image_read_write_supported(&self) -> bool {
        !FORMATS
            .iter()
            .filter(|f| f.req_for_full_read_and_write)
            .map(|f| self.formats.get(&f.cl_image_format).unwrap())
            .map(|f| f.get(&CL_MEM_OBJECT_IMAGE3D).unwrap())
            .any(|f| *f & cl_mem_flags::from(CL_MEM_KERNEL_READ_AND_WRITE) == 0)
    }

    pub fn image_3d_write_supported(&self) -> bool {
        !FORMATS
            .iter()
            .filter(|f| f.req_for_3d_image_write_ext)
            .map(|f| self.formats.get(&f.cl_image_format).unwrap())
            .map(|f| f.get(&CL_MEM_OBJECT_IMAGE3D).unwrap())
            .any(|f| *f & cl_mem_flags::from(CL_MEM_WRITE_ONLY) == 0)
    }

    pub fn image_write_count(&self) -> cl_uint {
        self.shader_param(pipe_shader_cap::PIPE_SHADER_CAP_MAX_SHADER_IMAGES) as cl_uint
    }

    pub fn little_endian(&self) -> bool {
        let endianness = self.screen.param(pipe_cap::PIPE_CAP_ENDIANNESS);
        endianness == (pipe_endian::PIPE_ENDIAN_LITTLE as i32)
    }

    pub fn local_mem_size(&self) -> cl_ulong {
        self.screen
            .compute_param(pipe_compute_cap::PIPE_COMPUTE_CAP_MAX_LOCAL_SIZE)
    }

    pub fn max_block_sizes(&self) -> Vec<usize> {
        let v: Vec<u64> = self
            .screen
            .compute_param(pipe_compute_cap::PIPE_COMPUTE_CAP_MAX_BLOCK_SIZE);
        v.into_iter().map(|v| v as usize).collect()
    }

    pub fn max_clock_freq(&self) -> cl_uint {
        self.screen
            .compute_param(pipe_compute_cap::PIPE_COMPUTE_CAP_MAX_CLOCK_FREQUENCY)
    }

    pub fn max_compute_units(&self) -> cl_uint {
        self.screen
            .compute_param(pipe_compute_cap::PIPE_COMPUTE_CAP_MAX_COMPUTE_UNITS)
    }

    pub fn max_grid_dimensions(&self) -> cl_uint {
        ComputeParam::<u64>::compute_param(
            self.screen.as_ref(),
            pipe_compute_cap::PIPE_COMPUTE_CAP_GRID_DIMENSION,
        ) as cl_uint
    }

    pub fn max_mem_alloc(&self) -> cl_ulong {
        self.screen
            .compute_param(pipe_compute_cap::PIPE_COMPUTE_CAP_MAX_MEM_ALLOC_SIZE)
    }

    pub fn max_samplers(&self) -> cl_uint {
        self.shader_param(pipe_shader_cap::PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS) as cl_uint
    }

    pub fn max_threads_per_block(&self) -> usize {
        ComputeParam::<u64>::compute_param(
            self.screen.as_ref(),
            pipe_compute_cap::PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK,
        ) as usize
    }

    pub fn param_max_size(&self) -> usize {
        ComputeParam::<u64>::compute_param(
            self.screen.as_ref(),
            pipe_compute_cap::PIPE_COMPUTE_CAP_MAX_INPUT_SIZE,
        ) as usize
    }

    pub fn printf_buffer_size(&self) -> usize {
        1024 * 1024
    }

    pub fn screen(&self) -> &Arc<PipeScreen> {
        &self.screen
    }

    pub fn subgroups(&self) -> u32 {
        ComputeParam::<u32>::compute_param(
            self.screen.as_ref(),
            pipe_compute_cap::PIPE_COMPUTE_CAP_SUBGROUP_SIZE,
        )
    }

    pub fn unified_memory(&self) -> bool {
        self.screen.param(pipe_cap::PIPE_CAP_UMA) == 1
    }

    pub fn vendor_id(&self) -> cl_uint {
        let id = self.screen.param(pipe_cap::PIPE_CAP_VENDOR_ID);
        if id == -1 {
            return 0;
        }
        id as u32
    }

    pub fn helper_ctx(&self) -> impl HelperContextWrapper + '_ {
        HelperContext {
            lock: self.helper_ctx.lock().unwrap(),
        }
    }

    pub fn cl_features(&self) -> clc_optional_features {
        clc_optional_features {
            fp16: false,
            fp64: self.doubles_supported(),
            int64: self.long_supported(),
            images: self.image_supported(),
            images_read_write: self.image_read_write_supported(),
            images_write_3d: self.image_3d_write_supported(),
            intel_subgroups: false,
            subgroups: false,
        }
    }
}
