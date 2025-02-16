use crate::api::icd::*;
use crate::core::device::*;
use crate::core::event::*;
use crate::core::memory::*;
use crate::core::platform::*;
use crate::core::program::*;
use crate::core::queue::*;
use crate::impl_cl_type_trait;

use mesa_rust::compiler::clc::*;
use mesa_rust::compiler::nir::*;
use mesa_rust::nir_pass;
use mesa_rust::pipe::context::RWFlags;
use mesa_rust::pipe::resource::*;
use mesa_rust::pipe::screen::ResourceType;
use mesa_rust_gen::*;
use mesa_rust_util::math::*;
use mesa_rust_util::serialize::*;
use rusticl_opencl_gen::*;
use spirv::SpirvKernelInfo;

use std::cmp;
use std::collections::HashMap;
use std::convert::TryInto;
use std::ffi::CStr;
use std::fmt::Debug;
use std::fmt::Display;
use std::ops::Index;
use std::ops::Not;
use std::os::raw::c_void;
use std::ptr;
use std::slice;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::MutexGuard;
use std::sync::Weak;

// According to the CL spec we are not allowed to let any cl_kernel object hold any references on
// its arguments as this might make it unfeasible for applications to free the backing memory of
// memory objects allocated with `CL_USE_HOST_PTR`.
//
// However those arguments might temporarily get referenced by event objects, so we'll use Weak in
// order to upgrade the reference when needed. It's also safer to use Weak over raw pointers,
// because it makes it impossible to run into use-after-free issues.
//
// Technically we also need to do it for samplers, but there it's kinda pointless to take a weak
// reference as samplers don't have the same host_ptr or any similar problems as cl_mem objects.
#[derive(Clone)]
pub enum KernelArgValue {
    None,
    Buffer(Weak<Buffer>),
    Constant(Vec<u8>),
    Image(Weak<Image>),
    LocalMem(usize),
    Sampler(Arc<Sampler>),
}

#[repr(u8)]
#[derive(Hash, PartialEq, Eq, Clone, Copy)]
pub enum KernelArgType {
    Constant(/* size */ u16), // for anything passed by value
    Image,
    RWImage,
    Sampler,
    Texture,
    MemGlobal,
    MemConstant,
    MemLocal,
}

impl KernelArgType {
    fn deserialize(blob: &mut blob_reader) -> Option<Self> {
        // SAFETY: we get 0 on an overrun, but we verify that later and act accordingly.
        let res = match unsafe { blob_read_uint8(blob) } {
            0 => {
                // SAFETY: same here
                let size = unsafe { blob_read_uint16(blob) };
                KernelArgType::Constant(size)
            }
            1 => KernelArgType::Image,
            2 => KernelArgType::RWImage,
            3 => KernelArgType::Sampler,
            4 => KernelArgType::Texture,
            5 => KernelArgType::MemGlobal,
            6 => KernelArgType::MemConstant,
            7 => KernelArgType::MemLocal,
            _ => return None,
        };

        blob.overrun.not().then_some(res)
    }

    fn serialize(&self, blob: &mut blob) {
        unsafe {
            match self {
                KernelArgType::Constant(size) => {
                    blob_write_uint8(blob, 0);
                    blob_write_uint16(blob, *size)
                }
                KernelArgType::Image => blob_write_uint8(blob, 1),
                KernelArgType::RWImage => blob_write_uint8(blob, 2),
                KernelArgType::Sampler => blob_write_uint8(blob, 3),
                KernelArgType::Texture => blob_write_uint8(blob, 4),
                KernelArgType::MemGlobal => blob_write_uint8(blob, 5),
                KernelArgType::MemConstant => blob_write_uint8(blob, 6),
                KernelArgType::MemLocal => blob_write_uint8(blob, 7),
            };
        }
    }

    fn is_opaque(&self) -> bool {
        matches!(
            self,
            KernelArgType::Image
                | KernelArgType::RWImage
                | KernelArgType::Texture
                | KernelArgType::Sampler
        )
    }
}

#[derive(Hash, PartialEq, Eq, Clone)]
enum CompiledKernelArgType {
    APIArg(u32),
    ConstantBuffer,
    GlobalWorkOffsets,
    GlobalWorkSize,
    PrintfBuffer,
    InlineSampler((cl_addressing_mode, cl_filter_mode, bool)),
    FormatArray,
    OrderArray,
    WorkDim,
    WorkGroupOffsets,
    NumWorkgroups,
}

#[derive(Hash, PartialEq, Eq, Clone)]
pub struct KernelArg {
    spirv: spirv::SPIRVKernelArg,
    pub kind: KernelArgType,
    pub dead: bool,
}

impl KernelArg {
    fn from_spirv_nir(spirv: &[spirv::SPIRVKernelArg], nir: &mut NirShader) -> Vec<Self> {
        let nir_arg_map: HashMap<_, _> = nir
            .variables_with_mode(
                nir_variable_mode::nir_var_uniform | nir_variable_mode::nir_var_image,
            )
            .map(|v| (v.data.location, v))
            .collect();
        let mut res = Vec::new();

        for (i, s) in spirv.iter().enumerate() {
            let nir = nir_arg_map.get(&(i as i32)).unwrap();
            let kind = match s.address_qualifier {
                clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_PRIVATE => {
                    if unsafe { glsl_type_is_sampler(nir.type_) } {
                        KernelArgType::Sampler
                    } else {
                        let size = unsafe { glsl_get_cl_size(nir.type_) } as u16;
                        // nir types of non opaque types are never sized 0
                        KernelArgType::Constant(size)
                    }
                }
                clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_CONSTANT => {
                    KernelArgType::MemConstant
                }
                clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_LOCAL => {
                    KernelArgType::MemLocal
                }
                clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_GLOBAL => {
                    if unsafe { glsl_type_is_image(nir.type_) } {
                        let access = nir.data.access();
                        if access == gl_access_qualifier::ACCESS_NON_WRITEABLE.0 {
                            KernelArgType::Texture
                        } else if access == gl_access_qualifier::ACCESS_NON_READABLE.0 {
                            KernelArgType::Image
                        } else {
                            KernelArgType::RWImage
                        }
                    } else {
                        KernelArgType::MemGlobal
                    }
                }
            };

            res.push(Self {
                spirv: s.clone(),
                // we'll update it later in the 2nd pass
                kind: kind,
                dead: true,
            });
        }
        res
    }

    fn serialize(args: &[Self], blob: &mut blob) {
        unsafe {
            blob_write_uint16(blob, args.len() as u16);

            for arg in args {
                arg.spirv.serialize(blob);
                blob_write_uint8(blob, arg.dead.into());
                arg.kind.serialize(blob);
            }
        }
    }

    fn deserialize(blob: &mut blob_reader) -> Option<Vec<Self>> {
        // SAFETY: we check the overrun status, blob_read returns 0 in such a case.
        let len = unsafe { blob_read_uint16(blob) } as usize;
        let mut res = Vec::with_capacity(len);

        for _ in 0..len {
            let spirv = spirv::SPIRVKernelArg::deserialize(blob)?;
            // SAFETY: we check the overrun status
            let dead = unsafe { blob_read_uint8(blob) } != 0;
            let kind = KernelArgType::deserialize(blob)?;

            res.push(Self {
                spirv: spirv,
                kind: kind,
                dead: dead,
            });
        }

        blob.overrun.not().then_some(res)
    }
}

#[derive(Hash, PartialEq, Eq, Clone)]
struct CompiledKernelArg {
    kind: CompiledKernelArgType,
    /// The binding for image/sampler args, the offset into the input buffer
    /// for anything else.
    offset: u32,
    dead: bool,
}

impl CompiledKernelArg {
    fn assign_locations(compiled_args: &mut [Self], nir: &mut NirShader) {
        for var in nir.variables_with_mode(
            nir_variable_mode::nir_var_uniform | nir_variable_mode::nir_var_image,
        ) {
            let arg = &mut compiled_args[var.data.location as usize];
            let t = var.type_;

            arg.dead = false;
            arg.offset = if unsafe {
                glsl_type_is_image(t) || glsl_type_is_texture(t) || glsl_type_is_sampler(t)
            } {
                var.data.binding
            } else {
                var.data.driver_location
            };
        }
    }

    fn serialize(args: &[Self], blob: &mut blob) {
        unsafe {
            blob_write_uint16(blob, args.len() as u16);
            for arg in args {
                blob_write_uint32(blob, arg.offset);
                blob_write_uint8(blob, arg.dead.into());
                match arg.kind {
                    CompiledKernelArgType::ConstantBuffer => blob_write_uint8(blob, 0),
                    CompiledKernelArgType::GlobalWorkOffsets => blob_write_uint8(blob, 1),
                    CompiledKernelArgType::PrintfBuffer => blob_write_uint8(blob, 2),
                    CompiledKernelArgType::InlineSampler((addr_mode, filter_mode, norm)) => {
                        blob_write_uint8(blob, 3);
                        blob_write_uint8(blob, norm.into());
                        blob_write_uint32(blob, addr_mode);
                        blob_write_uint32(blob, filter_mode)
                    }
                    CompiledKernelArgType::FormatArray => blob_write_uint8(blob, 4),
                    CompiledKernelArgType::OrderArray => blob_write_uint8(blob, 5),
                    CompiledKernelArgType::WorkDim => blob_write_uint8(blob, 6),
                    CompiledKernelArgType::WorkGroupOffsets => blob_write_uint8(blob, 7),
                    CompiledKernelArgType::NumWorkgroups => blob_write_uint8(blob, 8),
                    CompiledKernelArgType::GlobalWorkSize => blob_write_uint8(blob, 9),
                    CompiledKernelArgType::APIArg(idx) => {
                        blob_write_uint8(blob, 10);
                        blob_write_uint32(blob, idx)
                    }
                };
            }
        }
    }

    fn deserialize(blob: &mut blob_reader) -> Option<Vec<Self>> {
        unsafe {
            let len = blob_read_uint16(blob) as usize;
            let mut res = Vec::with_capacity(len);

            for _ in 0..len {
                let offset = blob_read_uint32(blob);
                let dead = blob_read_uint8(blob) != 0;

                let kind = match blob_read_uint8(blob) {
                    0 => CompiledKernelArgType::ConstantBuffer,
                    1 => CompiledKernelArgType::GlobalWorkOffsets,
                    2 => CompiledKernelArgType::PrintfBuffer,
                    3 => {
                        let norm = blob_read_uint8(blob) != 0;
                        let addr_mode = blob_read_uint32(blob);
                        let filter_mode = blob_read_uint32(blob);
                        CompiledKernelArgType::InlineSampler((addr_mode, filter_mode, norm))
                    }
                    4 => CompiledKernelArgType::FormatArray,
                    5 => CompiledKernelArgType::OrderArray,
                    6 => CompiledKernelArgType::WorkDim,
                    7 => CompiledKernelArgType::WorkGroupOffsets,
                    8 => CompiledKernelArgType::NumWorkgroups,
                    9 => CompiledKernelArgType::GlobalWorkSize,
                    10 => {
                        let idx = blob_read_uint32(blob);
                        CompiledKernelArgType::APIArg(idx)
                    }
                    _ => return None,
                };

                res.push(Self {
                    kind: kind,
                    offset: offset,
                    dead: dead,
                });
            }

            Some(res)
        }
    }
}

#[derive(Clone, PartialEq, Eq, Hash)]
pub struct KernelInfo {
    pub args: Vec<KernelArg>,
    pub attributes_string: String,
    work_group_size: [usize; 3],
    work_group_size_hint: [u32; 3],
    subgroup_size: usize,
    num_subgroups: usize,
}

struct CSOWrapper {
    cso_ptr: *mut c_void,
    dev: &'static Device,
}

impl CSOWrapper {
    fn new(dev: &'static Device, nir: &NirShader) -> Self {
        let cso_ptr = dev
            .helper_ctx()
            .create_compute_state(nir, nir.shared_size());

        Self {
            cso_ptr: cso_ptr,
            dev: dev,
        }
    }

    fn get_cso_info(&self) -> pipe_compute_state_object_info {
        self.dev.helper_ctx().compute_state_info(self.cso_ptr)
    }
}

impl Drop for CSOWrapper {
    fn drop(&mut self) {
        self.dev.helper_ctx().delete_compute_state(self.cso_ptr);
    }
}

enum KernelDevStateVariant {
    Cso(CSOWrapper),
    Nir(NirShader),
}

#[derive(Debug, PartialEq)]
enum NirKernelVariant {
    /// Can be used under any circumstance.
    Default,

    /// Optimized variant making the following assumptions:
    ///  - global_id_offsets are 0
    ///  - workgroup_offsets are 0
    ///  - local_size is info.local_size_hint
    Optimized,
}

impl Display for NirKernelVariant {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        // this simply prints the enum name, so that's fine
        Debug::fmt(self, f)
    }
}

pub struct NirKernelBuilds {
    default_build: NirKernelBuild,
    optimized: Option<NirKernelBuild>,
    /// merged info with worst case values
    info: pipe_compute_state_object_info,
}

impl Index<NirKernelVariant> for NirKernelBuilds {
    type Output = NirKernelBuild;

    fn index(&self, index: NirKernelVariant) -> &Self::Output {
        match index {
            NirKernelVariant::Default => &self.default_build,
            NirKernelVariant::Optimized => self.optimized.as_ref().unwrap_or(&self.default_build),
        }
    }
}

impl NirKernelBuilds {
    fn new(default_build: NirKernelBuild, optimized: Option<NirKernelBuild>) -> Self {
        let mut info = default_build.info;
        if let Some(build) = &optimized {
            info.max_threads = cmp::min(info.max_threads, build.info.max_threads);
            info.simd_sizes &= build.info.simd_sizes;
            info.private_memory = cmp::max(info.private_memory, build.info.private_memory);
            info.preferred_simd_size =
                cmp::max(info.preferred_simd_size, build.info.preferred_simd_size);
        }

        Self {
            default_build: default_build,
            optimized: optimized,
            info: info,
        }
    }
}

struct NirKernelBuild {
    nir_or_cso: KernelDevStateVariant,
    constant_buffer: Option<Arc<PipeResource>>,
    info: pipe_compute_state_object_info,
    shared_size: u64,
    printf_info: Option<NirPrintfInfo>,
    compiled_args: Vec<CompiledKernelArg>,
}

// SAFETY: `CSOWrapper` is only safe to use if the device supports `pipe_caps.shareable_shaders` and
//         we make sure to set `nir_or_cso` to `KernelDevStateVariant::Cso` only if that's the case.
unsafe impl Send for NirKernelBuild {}
unsafe impl Sync for NirKernelBuild {}

impl NirKernelBuild {
    fn new(dev: &'static Device, mut out: CompilationResult) -> Self {
        let cso = CSOWrapper::new(dev, &out.nir);
        let info = cso.get_cso_info();
        let cb = Self::create_nir_constant_buffer(dev, &out.nir);
        let shared_size = out.nir.shared_size() as u64;
        let printf_info = out.nir.take_printf_info();

        let nir_or_cso = if !dev.shareable_shaders() {
            KernelDevStateVariant::Nir(out.nir)
        } else {
            KernelDevStateVariant::Cso(cso)
        };

        NirKernelBuild {
            nir_or_cso: nir_or_cso,
            constant_buffer: cb,
            info: info,
            shared_size: shared_size,
            printf_info: printf_info,
            compiled_args: out.compiled_args,
        }
    }

    fn create_nir_constant_buffer(dev: &Device, nir: &NirShader) -> Option<Arc<PipeResource>> {
        let buf = nir.get_constant_buffer();
        let len = buf.len() as u32;

        if len > 0 {
            // TODO bind as constant buffer
            let res = dev
                .screen()
                .resource_create_buffer(len, ResourceType::Normal, PIPE_BIND_GLOBAL)
                .unwrap();

            dev.helper_ctx()
                .exec(|ctx| ctx.buffer_subdata(&res, 0, buf.as_ptr().cast(), len))
                .wait();

            Some(Arc::new(res))
        } else {
            None
        }
    }
}

pub struct Kernel {
    pub base: CLObjectBase<CL_INVALID_KERNEL>,
    pub prog: Arc<Program>,
    pub name: String,
    values: Mutex<Vec<Option<KernelArgValue>>>,
    builds: HashMap<&'static Device, Arc<NirKernelBuilds>>,
    pub kernel_info: Arc<KernelInfo>,
}

impl_cl_type_trait!(cl_kernel, Kernel, CL_INVALID_KERNEL);

fn create_kernel_arr<T>(vals: &[usize], val: T) -> CLResult<[T; 3]>
where
    T: std::convert::TryFrom<usize> + Copy,
    <T as std::convert::TryFrom<usize>>::Error: std::fmt::Debug,
{
    let mut res = [val; 3];
    for (i, v) in vals.iter().enumerate() {
        res[i] = (*v).try_into().ok().ok_or(CL_OUT_OF_RESOURCES)?;
    }

    Ok(res)
}

#[derive(Clone)]
struct CompilationResult {
    nir: NirShader,
    compiled_args: Vec<CompiledKernelArg>,
}

impl CompilationResult {
    fn deserialize(reader: &mut blob_reader, d: &Device) -> Option<Self> {
        let nir = NirShader::deserialize(
            reader,
            d.screen()
                .nir_shader_compiler_options(pipe_shader_type::PIPE_SHADER_COMPUTE),
        )?;
        let compiled_args = CompiledKernelArg::deserialize(reader)?;

        Some(Self {
            nir: nir,
            compiled_args,
        })
    }

    fn serialize(&self, blob: &mut blob) {
        self.nir.serialize(blob);
        CompiledKernelArg::serialize(&self.compiled_args, blob);
    }
}

fn opt_nir(nir: &mut NirShader, dev: &Device, has_explicit_types: bool) {
    let nir_options = unsafe {
        &*dev
            .screen
            .nir_shader_compiler_options(pipe_shader_type::PIPE_SHADER_COMPUTE)
    };

    while {
        let mut progress = false;

        progress |= nir_pass!(nir, nir_copy_prop);
        progress |= nir_pass!(nir, nir_opt_copy_prop_vars);
        progress |= nir_pass!(nir, nir_opt_dead_write_vars);

        if nir_options.lower_to_scalar {
            nir_pass!(
                nir,
                nir_lower_alu_to_scalar,
                nir_options.lower_to_scalar_filter,
                ptr::null(),
            );
            nir_pass!(nir, nir_lower_phis_to_scalar, false);
        }

        progress |= nir_pass!(nir, nir_opt_deref);
        if has_explicit_types {
            progress |= nir_pass!(nir, nir_opt_memcpy);
        }
        progress |= nir_pass!(nir, nir_opt_dce);
        progress |= nir_pass!(nir, nir_opt_undef);
        progress |= nir_pass!(nir, nir_opt_constant_folding);
        progress |= nir_pass!(nir, nir_opt_cse);
        nir_pass!(nir, nir_split_var_copies);
        progress |= nir_pass!(nir, nir_lower_var_copies);
        progress |= nir_pass!(nir, nir_lower_vars_to_ssa);
        nir_pass!(nir, nir_lower_alu);
        progress |= nir_pass!(nir, nir_opt_phi_precision);
        progress |= nir_pass!(nir, nir_opt_algebraic);
        progress |= nir_pass!(
            nir,
            nir_opt_if,
            nir_opt_if_options::nir_opt_if_optimize_phi_true_false,
        );
        progress |= nir_pass!(nir, nir_opt_dead_cf);
        progress |= nir_pass!(nir, nir_opt_remove_phis);
        // we don't want to be too aggressive here, but it kills a bit of CFG
        progress |= nir_pass!(nir, nir_opt_peephole_select, 8, true, true);
        progress |= nir_pass!(
            nir,
            nir_lower_vec3_to_vec4,
            nir_variable_mode::nir_var_mem_generic | nir_variable_mode::nir_var_uniform,
        );

        if nir_options.max_unroll_iterations != 0 {
            progress |= nir_pass!(nir, nir_opt_loop_unroll);
        }
        nir.sweep_mem();
        progress
    } {}
}

/// # Safety
///
/// Only safe to call when `var` is a valid pointer to a valid [`nir_variable`]
unsafe extern "C" fn can_remove_var(var: *mut nir_variable, _: *mut c_void) -> bool {
    // SAFETY: It is the caller's responsibility to provide a valid and aligned pointer
    let var_type = unsafe { (*var).type_ };
    // SAFETY: `nir_variable`'s type invariant guarantees that the `type_` field is valid and
    // properly aligned.
    unsafe {
        !glsl_type_is_image(var_type)
            && !glsl_type_is_texture(var_type)
            && !glsl_type_is_sampler(var_type)
    }
}

const DV_OPTS: nir_remove_dead_variables_options = nir_remove_dead_variables_options {
    can_remove_var: Some(can_remove_var),
    can_remove_var_data: ptr::null_mut(),
};

fn compile_nir_to_args(
    dev: &Device,
    mut nir: NirShader,
    args: &[spirv::SPIRVKernelArg],
    lib_clc: &NirShader,
) -> (Vec<KernelArg>, NirShader) {
    // this is a hack until we support fp16 properly and check for denorms inside vstore/vload_half
    nir.preserve_fp16_denorms();

    // Set to rtne for now until drivers are able to report their preferred rounding mode, that also
    // matches what we report via the API.
    nir.set_fp_rounding_mode_rtne();

    nir_pass!(nir, nir_scale_fdiv);
    nir.set_workgroup_size_variable_if_zero();
    nir.structurize();
    nir_pass!(
        nir,
        nir_lower_variable_initializers,
        nir_variable_mode::nir_var_function_temp
    );

    while {
        let mut progress = false;
        nir_pass!(nir, nir_split_var_copies);
        progress |= nir_pass!(nir, nir_copy_prop);
        progress |= nir_pass!(nir, nir_opt_copy_prop_vars);
        progress |= nir_pass!(nir, nir_opt_dead_write_vars);
        progress |= nir_pass!(nir, nir_opt_deref);
        progress |= nir_pass!(nir, nir_opt_dce);
        progress |= nir_pass!(nir, nir_opt_undef);
        progress |= nir_pass!(nir, nir_opt_constant_folding);
        progress |= nir_pass!(nir, nir_opt_cse);
        progress |= nir_pass!(nir, nir_lower_vars_to_ssa);
        progress |= nir_pass!(nir, nir_opt_algebraic);
        progress
    } {}
    nir.inline(lib_clc);
    nir.cleanup_functions();
    // that should free up tons of memory
    nir.sweep_mem();

    nir_pass!(nir, nir_dedup_inline_samplers);

    let printf_opts = nir_lower_printf_options {
        ptr_bit_size: 0,
        hash_format_strings: false,
        max_buffer_size: dev.printf_buffer_size() as u32,
    };
    nir_pass!(nir, nir_lower_printf, &printf_opts);

    opt_nir(&mut nir, dev, false);

    (KernelArg::from_spirv_nir(args, &mut nir), nir)
}

fn compile_nir_prepare_for_variants(
    dev: &Device,
    nir: &mut NirShader,
    compiled_args: &mut Vec<CompiledKernelArg>,
) {
    // assign locations for inline samplers.
    // IMPORTANT: this needs to happen before nir_remove_dead_variables.
    let mut last_loc = -1;
    for v in nir
        .variables_with_mode(nir_variable_mode::nir_var_uniform | nir_variable_mode::nir_var_image)
    {
        if unsafe { !glsl_type_is_sampler(v.type_) } {
            last_loc = v.data.location;
            continue;
        }
        let s = unsafe { v.data.anon_1.sampler };
        if s.is_inline_sampler() != 0 {
            last_loc += 1;
            v.data.location = last_loc;

            compiled_args.push(CompiledKernelArg {
                kind: CompiledKernelArgType::InlineSampler(Sampler::nir_to_cl(
                    s.addressing_mode(),
                    s.filter_mode(),
                    s.normalized_coordinates(),
                )),
                offset: 0,
                dead: true,
            });
        } else {
            last_loc = v.data.location;
        }
    }

    nir_pass!(
        nir,
        nir_remove_dead_variables,
        nir_variable_mode::nir_var_uniform
            | nir_variable_mode::nir_var_image
            | nir_variable_mode::nir_var_mem_constant
            | nir_variable_mode::nir_var_mem_shared
            | nir_variable_mode::nir_var_function_temp,
        &DV_OPTS,
    );

    nir_pass!(nir, nir_lower_readonly_images_to_tex, true);
    nir_pass!(
        nir,
        nir_lower_cl_images,
        !dev.images_as_deref(),
        !dev.samplers_as_deref(),
    );

    nir_pass!(
        nir,
        nir_lower_vars_to_explicit_types,
        nir_variable_mode::nir_var_mem_constant,
        Some(glsl_get_cl_type_size_align),
    );

    // has to run before adding internal kernel arguments
    nir.extract_constant_initializers();

    // needed to convert variables to load intrinsics
    nir_pass!(nir, nir_lower_system_values);

    // Run here so we can decide if it makes sense to compile a variant, e.g. read system values.
    nir.gather_info();
}

fn compile_nir_variant(
    res: &mut CompilationResult,
    dev: &Device,
    variant: NirKernelVariant,
    args: &[KernelArg],
    name: &str,
) {
    let mut lower_state = rusticl_lower_state::default();
    let compiled_args = &mut res.compiled_args;
    let nir = &mut res.nir;

    let address_bits_ptr_type;
    let address_bits_base_type;
    let global_address_format;
    let shared_address_format;

    if dev.address_bits() == 64 {
        address_bits_ptr_type = unsafe { glsl_uint64_t_type() };
        address_bits_base_type = glsl_base_type::GLSL_TYPE_UINT64;
        global_address_format = nir_address_format::nir_address_format_64bit_global;
        shared_address_format = nir_address_format::nir_address_format_32bit_offset_as_64bit;
    } else {
        address_bits_ptr_type = unsafe { glsl_uint_type() };
        address_bits_base_type = glsl_base_type::GLSL_TYPE_UINT;
        global_address_format = nir_address_format::nir_address_format_32bit_global;
        shared_address_format = nir_address_format::nir_address_format_32bit_offset;
    }

    let nir_options = unsafe {
        &*dev
            .screen
            .nir_shader_compiler_options(pipe_shader_type::PIPE_SHADER_COMPUTE)
    };

    if variant == NirKernelVariant::Optimized {
        let wgsh = nir.workgroup_size_hint();
        if wgsh != [0; 3] {
            nir.set_workgroup_size(wgsh);
        }
    }

    let mut compute_options = nir_lower_compute_system_values_options::default();
    compute_options.set_has_global_size(true);
    if variant != NirKernelVariant::Optimized {
        compute_options.set_has_base_global_invocation_id(true);
        compute_options.set_has_base_workgroup_id(true);
    }
    nir_pass!(nir, nir_lower_compute_system_values, &compute_options);
    nir.gather_info();

    let mut add_var = |nir: &mut NirShader,
                       var_loc: &mut usize,
                       kind: CompiledKernelArgType,
                       glsl_type: *const glsl_type,
                       name| {
        *var_loc = compiled_args.len();
        compiled_args.push(CompiledKernelArg {
            kind: kind,
            offset: 0,
            dead: true,
        });
        nir.add_var(
            nir_variable_mode::nir_var_uniform,
            glsl_type,
            *var_loc,
            name,
        );
    };

    if nir.reads_sysval(gl_system_value::SYSTEM_VALUE_BASE_GLOBAL_INVOCATION_ID) {
        debug_assert_ne!(variant, NirKernelVariant::Optimized);
        add_var(
            nir,
            &mut lower_state.base_global_invoc_id_loc,
            CompiledKernelArgType::GlobalWorkOffsets,
            unsafe { glsl_vector_type(address_bits_base_type, 3) },
            c"base_global_invocation_id",
        )
    }

    if nir.reads_sysval(gl_system_value::SYSTEM_VALUE_GLOBAL_GROUP_SIZE) {
        add_var(
            nir,
            &mut lower_state.global_size_loc,
            CompiledKernelArgType::GlobalWorkSize,
            unsafe { glsl_vector_type(address_bits_base_type, 3) },
            c"global_size",
        )
    }

    if nir.reads_sysval(gl_system_value::SYSTEM_VALUE_BASE_WORKGROUP_ID) {
        debug_assert_ne!(variant, NirKernelVariant::Optimized);
        add_var(
            nir,
            &mut lower_state.base_workgroup_id_loc,
            CompiledKernelArgType::WorkGroupOffsets,
            unsafe { glsl_vector_type(address_bits_base_type, 3) },
            c"base_workgroup_id",
        );
    }

    if nir.reads_sysval(gl_system_value::SYSTEM_VALUE_NUM_WORKGROUPS) {
        add_var(
            nir,
            &mut lower_state.num_workgroups_loc,
            CompiledKernelArgType::NumWorkgroups,
            unsafe { glsl_vector_type(glsl_base_type::GLSL_TYPE_UINT, 3) },
            c"num_workgroups",
        );
    }

    if nir.has_constant() {
        add_var(
            nir,
            &mut lower_state.const_buf_loc,
            CompiledKernelArgType::ConstantBuffer,
            address_bits_ptr_type,
            c"constant_buffer_addr",
        );
    }
    if nir.has_printf() {
        add_var(
            nir,
            &mut lower_state.printf_buf_loc,
            CompiledKernelArgType::PrintfBuffer,
            address_bits_ptr_type,
            c"printf_buffer_addr",
        );
    }

    if nir.num_images() > 0 || nir.num_textures() > 0 {
        let count = nir.num_images() + nir.num_textures();

        add_var(
            nir,
            &mut lower_state.format_arr_loc,
            CompiledKernelArgType::FormatArray,
            unsafe { glsl_array_type(glsl_int16_t_type(), count as u32, 2) },
            c"image_formats",
        );

        add_var(
            nir,
            &mut lower_state.order_arr_loc,
            CompiledKernelArgType::OrderArray,
            unsafe { glsl_array_type(glsl_int16_t_type(), count as u32, 2) },
            c"image_orders",
        );
    }

    if nir.reads_sysval(gl_system_value::SYSTEM_VALUE_WORK_DIM) {
        add_var(
            nir,
            &mut lower_state.work_dim_loc,
            CompiledKernelArgType::WorkDim,
            unsafe { glsl_uint8_t_type() },
            c"work_dim",
        );
    }

    // need to run after first opt loop and remove_dead_variables to get rid of uneccessary scratch
    // memory
    nir_pass!(
        nir,
        nir_lower_vars_to_explicit_types,
        nir_variable_mode::nir_var_mem_shared
            | nir_variable_mode::nir_var_function_temp
            | nir_variable_mode::nir_var_shader_temp
            | nir_variable_mode::nir_var_uniform
            | nir_variable_mode::nir_var_mem_global
            | nir_variable_mode::nir_var_mem_generic,
        Some(glsl_get_cl_type_size_align),
    );

    opt_nir(nir, dev, true);
    nir_pass!(nir, nir_lower_memcpy);

    // we might have got rid of more function_temp or shared memory
    nir.reset_scratch_size();
    nir.reset_shared_size();
    nir_pass!(
        nir,
        nir_remove_dead_variables,
        nir_variable_mode::nir_var_function_temp | nir_variable_mode::nir_var_mem_shared,
        &DV_OPTS,
    );
    nir_pass!(
        nir,
        nir_lower_vars_to_explicit_types,
        nir_variable_mode::nir_var_function_temp
            | nir_variable_mode::nir_var_mem_shared
            | nir_variable_mode::nir_var_mem_generic,
        Some(glsl_get_cl_type_size_align),
    );

    nir_pass!(
        nir,
        nir_lower_explicit_io,
        nir_variable_mode::nir_var_mem_global | nir_variable_mode::nir_var_mem_constant,
        global_address_format,
    );

    nir_pass!(nir, rusticl_lower_intrinsics, &mut lower_state);
    nir_pass!(
        nir,
        nir_lower_explicit_io,
        nir_variable_mode::nir_var_mem_shared
            | nir_variable_mode::nir_var_function_temp
            | nir_variable_mode::nir_var_uniform,
        shared_address_format,
    );

    if nir_options.lower_int64_options.0 != 0 && !nir_options.late_lower_int64 {
        nir_pass!(nir, nir_lower_int64);
    }

    if nir_options.lower_uniforms_to_ubo {
        nir_pass!(nir, rusticl_lower_inputs);
    }

    nir_pass!(nir, nir_lower_convert_alu_types, None);

    opt_nir(nir, dev, true);

    /* before passing it into drivers, assign locations as drivers might remove nir_variables or
     * other things we depend on
     */
    CompiledKernelArg::assign_locations(compiled_args, nir);

    /* update the has_variable_shared_mem info as we might have DCEed all of them */
    nir.set_has_variable_shared_mem(compiled_args.iter().any(|arg| {
        if let CompiledKernelArgType::APIArg(idx) = arg.kind {
            args[idx as usize].kind == KernelArgType::MemLocal && !arg.dead
        } else {
            false
        }
    }));

    if Platform::dbg().nir {
        eprintln!("=== Printing nir variant '{variant}' for '{name}' before driver finalization");
        nir.print();
    }

    if dev.screen.finalize_nir(nir) {
        if Platform::dbg().nir {
            eprintln!(
                "=== Printing nir variant '{variant}' for '{name}' after driver finalization"
            );
            nir.print();
        }
    }

    nir_pass!(nir, nir_opt_dce);
    nir.sweep_mem();
}

fn compile_nir_remaining(
    dev: &Device,
    mut nir: NirShader,
    args: &[KernelArg],
    name: &str,
) -> (CompilationResult, Option<CompilationResult>) {
    // add all API kernel args
    let mut compiled_args: Vec<_> = (0..args.len())
        .map(|idx| CompiledKernelArg {
            kind: CompiledKernelArgType::APIArg(idx as u32),
            offset: 0,
            dead: true,
        })
        .collect();

    compile_nir_prepare_for_variants(dev, &mut nir, &mut compiled_args);
    if Platform::dbg().nir {
        eprintln!("=== Printing nir for '{name}' before specialization");
        nir.print();
    }

    let mut default_build = CompilationResult {
        nir: nir,
        compiled_args: compiled_args,
    };

    // check if we even want to compile a variant before cloning the compilation state
    let has_wgs_hint = default_build.nir.workgroup_size_variable()
        && default_build.nir.workgroup_size_hint() != [0; 3];
    let has_offsets = default_build
        .nir
        .reads_sysval(gl_system_value::SYSTEM_VALUE_GLOBAL_INVOCATION_ID);

    let mut optimized = (!Platform::dbg().no_variants && (has_offsets || has_wgs_hint))
        .then(|| default_build.clone());

    compile_nir_variant(
        &mut default_build,
        dev,
        NirKernelVariant::Default,
        args,
        name,
    );
    if let Some(optimized) = &mut optimized {
        compile_nir_variant(optimized, dev, NirKernelVariant::Optimized, args, name);
    }

    (default_build, optimized)
}

pub struct SPIRVToNirResult {
    pub kernel_info: KernelInfo,
    pub nir_kernel_builds: NirKernelBuilds,
}

impl SPIRVToNirResult {
    fn new(
        dev: &'static Device,
        kernel_info: &clc_kernel_info,
        args: Vec<KernelArg>,
        default_build: CompilationResult,
        optimized: Option<CompilationResult>,
    ) -> Self {
        // TODO: we _should_ be able to parse them out of the SPIR-V, but clc doesn't handle
        //       indirections yet.
        let nir = &default_build.nir;
        let wgs = nir.workgroup_size();
        let subgroup_size = nir.subgroup_size();
        let num_subgroups = nir.num_subgroups();

        let default_build = NirKernelBuild::new(dev, default_build);
        let optimized = optimized.map(|opt| NirKernelBuild::new(dev, opt));

        let kernel_info = KernelInfo {
            args: args,
            attributes_string: kernel_info.attribute_str(),
            work_group_size: [wgs[0] as usize, wgs[1] as usize, wgs[2] as usize],
            work_group_size_hint: kernel_info.local_size_hint,
            subgroup_size: subgroup_size as usize,
            num_subgroups: num_subgroups as usize,
        };

        Self {
            kernel_info: kernel_info,
            nir_kernel_builds: NirKernelBuilds::new(default_build, optimized),
        }
    }

    fn deserialize(bin: &[u8], d: &'static Device, kernel_info: &clc_kernel_info) -> Option<Self> {
        let mut reader = blob_reader::default();
        unsafe {
            blob_reader_init(&mut reader, bin.as_ptr().cast(), bin.len());
        }

        let args = KernelArg::deserialize(&mut reader)?;
        let default_build = CompilationResult::deserialize(&mut reader, d)?;

        // SAFETY: on overrun this returns 0
        let optimized = match unsafe { blob_read_uint8(&mut reader) } {
            0 => None,
            _ => Some(CompilationResult::deserialize(&mut reader, d)?),
        };

        reader
            .overrun
            .not()
            .then(|| SPIRVToNirResult::new(d, kernel_info, args, default_build, optimized))
    }

    // we can't use Self here as the nir shader might be compiled to a cso already and we can't
    // cache that.
    fn serialize(
        blob: &mut blob,
        args: &[KernelArg],
        default_build: &CompilationResult,
        optimized: &Option<CompilationResult>,
    ) {
        KernelArg::serialize(args, blob);
        default_build.serialize(blob);
        match optimized {
            Some(variant) => {
                unsafe { blob_write_uint8(blob, 1) };
                variant.serialize(blob);
            }
            None => unsafe {
                blob_write_uint8(blob, 0);
            },
        }
    }
}

pub(super) fn convert_spirv_to_nir(
    build: &ProgramBuild,
    name: &str,
    args: &[spirv::SPIRVKernelArg],
    dev: &'static Device,
) -> SPIRVToNirResult {
    let cache = dev.screen().shader_cache();
    let key = build.hash_key(dev, name);
    let spirv_info = build.spirv_info(name, dev).unwrap();

    cache
        .as_ref()
        .and_then(|cache| cache.get(&mut key?))
        .and_then(|entry| SPIRVToNirResult::deserialize(&entry, dev, spirv_info))
        .unwrap_or_else(|| {
            let nir = build.to_nir(name, dev);

            if Platform::dbg().nir {
                eprintln!("=== Printing nir for '{name}' after spirv_to_nir");
                nir.print();
            }

            let (mut args, nir) = compile_nir_to_args(dev, nir, args, &dev.lib_clc);
            let (default_build, optimized) = compile_nir_remaining(dev, nir, &args, name);

            for build in [Some(&default_build), optimized.as_ref()].into_iter() {
                let Some(build) = build else {
                    continue;
                };

                for arg in &build.compiled_args {
                    if let CompiledKernelArgType::APIArg(idx) = arg.kind {
                        args[idx as usize].dead &= arg.dead;
                    }
                }
            }

            if let Some(cache) = cache {
                let mut blob = blob::default();
                unsafe {
                    blob_init(&mut blob);
                    SPIRVToNirResult::serialize(&mut blob, &args, &default_build, &optimized);
                    let bin = slice::from_raw_parts(blob.data, blob.size);
                    cache.put(bin, &mut key.unwrap());
                    blob_finish(&mut blob);
                }
            }

            SPIRVToNirResult::new(dev, spirv_info, args, default_build, optimized)
        })
}

fn extract<'a, const S: usize>(buf: &'a mut &[u8]) -> &'a [u8; S] {
    let val;
    (val, *buf) = (*buf).split_at(S);
    // we split of 4 bytes and convert to [u8; 4], so this should be safe
    // use split_array_ref once it's stable
    val.try_into().unwrap()
}

impl Kernel {
    pub fn new(name: String, prog: Arc<Program>, prog_build: &ProgramBuild) -> Arc<Kernel> {
        let kernel_info = Arc::clone(prog_build.kernel_info.get(&name).unwrap());
        let builds = prog_build
            .builds
            .iter()
            .filter_map(|(&dev, b)| b.kernels.get(&name).map(|k| (dev, Arc::clone(k))))
            .collect();

        let values = vec![None; kernel_info.args.len()];
        Arc::new(Self {
            base: CLObjectBase::new(RusticlTypes::Kernel),
            prog: prog,
            name: name,
            values: Mutex::new(values),
            builds: builds,
            kernel_info: kernel_info,
        })
    }

    pub fn suggest_local_size(
        &self,
        d: &Device,
        work_dim: usize,
        grid: &mut [usize],
        block: &mut [usize],
    ) {
        let mut threads = self.max_threads_per_block(d);
        let dim_threads = d.max_block_sizes();
        let subgroups = self.preferred_simd_size(d);

        for i in 0..work_dim {
            let t = cmp::min(threads, dim_threads[i]);
            let gcd = gcd(t, grid[i]);

            block[i] = gcd;
            grid[i] /= gcd;

            // update limits
            threads /= block[i];
        }

        // if we didn't fill the subgroup we can do a bit better if we have threads remaining
        let total_threads = block.iter().take(work_dim).product::<usize>();
        if threads != 1 && total_threads < subgroups {
            for i in 0..work_dim {
                if grid[i] * total_threads < threads && grid[i] * block[i] <= dim_threads[i] {
                    block[i] *= grid[i];
                    grid[i] = 1;
                    // can only do it once as nothing is cleanly divisible
                    break;
                }
            }
        }
    }

    fn optimize_local_size(&self, d: &Device, grid: &mut [usize; 3], block: &mut [u32; 3]) {
        if !block.contains(&0) {
            for i in 0..3 {
                // we already made sure everything is fine
                grid[i] /= block[i] as usize;
            }
            return;
        }

        let mut usize_block = [0usize; 3];
        for i in 0..3 {
            usize_block[i] = block[i] as usize;
        }

        self.suggest_local_size(d, 3, grid, &mut usize_block);

        for i in 0..3 {
            block[i] = usize_block[i] as u32;
        }
    }

    // the painful part is, that host threads are allowed to modify the kernel object once it was
    // enqueued, so return a closure with all req data included.
    pub fn launch(
        self: &Arc<Self>,
        q: &Arc<Queue>,
        work_dim: u32,
        block: &[usize],
        grid: &[usize],
        offsets: &[usize],
    ) -> CLResult<EventSig> {
        // Clone all the data we need to execute this kernel
        let kernel_info = Arc::clone(&self.kernel_info);
        let arg_values = self.arg_values().clone();
        let nir_kernel_builds = Arc::clone(&self.builds[q.device]);

        let mut buffer_arcs = HashMap::new();
        let mut image_arcs = HashMap::new();

        // need to preprocess buffer and image arguments so we hold a strong reference until the
        // event was processed.
        for arg in arg_values.iter() {
            match arg {
                Some(KernelArgValue::Buffer(buffer)) => {
                    buffer_arcs.insert(
                        // we use the ptr as the key, and also cast it to usize so we don't need to
                        // deal with Send + Sync here.
                        buffer.as_ptr() as usize,
                        buffer.upgrade().ok_or(CL_INVALID_KERNEL_ARGS)?,
                    );
                }
                Some(KernelArgValue::Image(image)) => {
                    image_arcs.insert(
                        image.as_ptr() as usize,
                        image.upgrade().ok_or(CL_INVALID_KERNEL_ARGS)?,
                    );
                }
                _ => {}
            }
        }

        // operations we want to report errors to the clients
        let mut block = create_kernel_arr::<u32>(block, 1)?;
        let mut grid = create_kernel_arr::<usize>(grid, 1)?;
        let offsets = create_kernel_arr::<usize>(offsets, 0)?;

        let api_grid = grid;

        self.optimize_local_size(q.device, &mut grid, &mut block);

        Ok(Box::new(move |q, ctx| {
            let hw_max_grid: Vec<usize> = q.device.max_grid_size();

            let variant = if offsets == [0; 3]
                && grid[0] <= hw_max_grid[0]
                && grid[1] <= hw_max_grid[1]
                && grid[2] <= hw_max_grid[2]
                && (kernel_info.work_group_size_hint == [0; 3]
                    || block == kernel_info.work_group_size_hint)
            {
                NirKernelVariant::Optimized
            } else {
                NirKernelVariant::Default
            };

            let nir_kernel_build = &nir_kernel_builds[variant];
            let mut workgroup_id_offset_loc = None;
            let mut input = Vec::new();
            // Set it once so we get the alignment padding right
            let static_local_size: u64 = nir_kernel_build.shared_size;
            let mut variable_local_size: u64 = static_local_size;
            let printf_size = q.device.printf_buffer_size() as u32;
            let mut samplers = Vec::new();
            let mut iviews = Vec::new();
            let mut sviews = Vec::new();
            let mut tex_formats: Vec<u16> = Vec::new();
            let mut tex_orders: Vec<u16> = Vec::new();
            let mut img_formats: Vec<u16> = Vec::new();
            let mut img_orders: Vec<u16> = Vec::new();

            let null_ptr;
            let null_ptr_v3;
            if q.device.address_bits() == 64 {
                null_ptr = [0u8; 8].as_slice();
                null_ptr_v3 = [0u8; 24].as_slice();
            } else {
                null_ptr = [0u8; 4].as_slice();
                null_ptr_v3 = [0u8; 12].as_slice();
            };

            let mut resource_info = Vec::new();
            fn add_global<'a>(
                q: &Queue,
                input: &mut Vec<u8>,
                resource_info: &mut Vec<(&'a PipeResource, usize)>,
                res: &'a PipeResource,
                offset: usize,
            ) {
                resource_info.push((res, input.len()));
                if q.device.address_bits() == 64 {
                    let offset: u64 = offset as u64;
                    input.extend_from_slice(&offset.to_ne_bytes());
                } else {
                    let offset: u32 = offset as u32;
                    input.extend_from_slice(&offset.to_ne_bytes());
                }
            }

            fn add_sysval(q: &Queue, input: &mut Vec<u8>, vals: &[usize; 3]) {
                if q.device.address_bits() == 64 {
                    input.extend_from_slice(unsafe { as_byte_slice(&vals.map(|v| v as u64)) });
                } else {
                    input.extend_from_slice(unsafe { as_byte_slice(&vals.map(|v| v as u32)) });
                }
            }

            let mut printf_buf = None;
            if nir_kernel_build.printf_info.is_some() {
                let buf = q
                    .device
                    .screen
                    .resource_create_buffer(printf_size, ResourceType::Staging, PIPE_BIND_GLOBAL)
                    .unwrap();

                let init_data: [u8; 1] = [4];
                ctx.buffer_subdata(&buf, 0, init_data.as_ptr().cast(), init_data.len() as u32);

                printf_buf = Some(buf);
            }

            for arg in &nir_kernel_build.compiled_args {
                let is_opaque = if let CompiledKernelArgType::APIArg(idx) = arg.kind {
                    kernel_info.args[idx as usize].kind.is_opaque()
                } else {
                    false
                };

                if !is_opaque && arg.offset as usize > input.len() {
                    input.resize(arg.offset as usize, 0);
                }

                match arg.kind {
                    CompiledKernelArgType::APIArg(idx) => {
                        let api_arg = &kernel_info.args[idx as usize];
                        if api_arg.dead {
                            continue;
                        }

                        let Some(value) = &arg_values[idx as usize] else {
                            continue;
                        };

                        match value {
                            KernelArgValue::Constant(c) => input.extend_from_slice(c),
                            KernelArgValue::Buffer(buffer) => {
                                let buffer = &buffer_arcs[&(buffer.as_ptr() as usize)];
                                let rw = if api_arg.spirv.address_qualifier
                                    == clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_CONSTANT
                                {
                                    RWFlags::RD
                                } else {
                                    RWFlags::RW
                                };

                                let res = buffer.get_res_for_access(ctx, rw)?;
                                add_global(q, &mut input, &mut resource_info, res, buffer.offset());
                            }
                            KernelArgValue::Image(image) => {
                                let image = &image_arcs[&(image.as_ptr() as usize)];
                                let (formats, orders) = if api_arg.kind == KernelArgType::Image {
                                    iviews.push(image.image_view(ctx, false)?);
                                    (&mut img_formats, &mut img_orders)
                                } else if api_arg.kind == KernelArgType::RWImage {
                                    iviews.push(image.image_view(ctx, true)?);
                                    (&mut img_formats, &mut img_orders)
                                } else {
                                    sviews.push(image.sampler_view(ctx)?);
                                    (&mut tex_formats, &mut tex_orders)
                                };

                                let binding = arg.offset as usize;
                                assert!(binding >= formats.len());

                                formats.resize(binding, 0);
                                orders.resize(binding, 0);

                                formats.push(image.image_format.image_channel_data_type as u16);
                                orders.push(image.image_format.image_channel_order as u16);
                            }
                            KernelArgValue::LocalMem(size) => {
                                // TODO 32 bit
                                let pot = cmp::min(*size, 0x80);
                                variable_local_size = variable_local_size
                                    .next_multiple_of(pot.next_power_of_two() as u64);
                                if q.device.address_bits() == 64 {
                                    let variable_local_size: [u8; 8] =
                                        variable_local_size.to_ne_bytes();
                                    input.extend_from_slice(&variable_local_size);
                                } else {
                                    let variable_local_size: [u8; 4] =
                                        (variable_local_size as u32).to_ne_bytes();
                                    input.extend_from_slice(&variable_local_size);
                                }
                                variable_local_size += *size as u64;
                            }
                            KernelArgValue::Sampler(sampler) => {
                                samplers.push(sampler.pipe());
                            }
                            KernelArgValue::None => {
                                assert!(
                                    api_arg.kind == KernelArgType::MemGlobal
                                        || api_arg.kind == KernelArgType::MemConstant
                                );
                                input.extend_from_slice(null_ptr);
                            }
                        }
                    }
                    CompiledKernelArgType::ConstantBuffer => {
                        assert!(nir_kernel_build.constant_buffer.is_some());
                        let res = nir_kernel_build.constant_buffer.as_ref().unwrap();
                        add_global(q, &mut input, &mut resource_info, res, 0);
                    }
                    CompiledKernelArgType::GlobalWorkOffsets => {
                        add_sysval(q, &mut input, &offsets);
                    }
                    CompiledKernelArgType::WorkGroupOffsets => {
                        workgroup_id_offset_loc = Some(input.len());
                        input.extend_from_slice(null_ptr_v3);
                    }
                    CompiledKernelArgType::GlobalWorkSize => {
                        add_sysval(q, &mut input, &api_grid);
                    }
                    CompiledKernelArgType::PrintfBuffer => {
                        let res = printf_buf.as_ref().unwrap();
                        add_global(q, &mut input, &mut resource_info, res, 0);
                    }
                    CompiledKernelArgType::InlineSampler(cl) => {
                        samplers.push(Sampler::cl_to_pipe(cl));
                    }
                    CompiledKernelArgType::FormatArray => {
                        input.extend_from_slice(unsafe { as_byte_slice(&tex_formats) });
                        input.extend_from_slice(unsafe { as_byte_slice(&img_formats) });
                    }
                    CompiledKernelArgType::OrderArray => {
                        input.extend_from_slice(unsafe { as_byte_slice(&tex_orders) });
                        input.extend_from_slice(unsafe { as_byte_slice(&img_orders) });
                    }
                    CompiledKernelArgType::WorkDim => {
                        input.extend_from_slice(&[work_dim as u8; 1]);
                    }
                    CompiledKernelArgType::NumWorkgroups => {
                        input.extend_from_slice(unsafe {
                            as_byte_slice(&[grid[0] as u32, grid[1] as u32, grid[2] as u32])
                        });
                    }
                }
            }

            // subtract the shader local_size as we only request something on top of that.
            variable_local_size -= static_local_size;

            let samplers: Vec<_> = samplers
                .iter()
                .map(|s| ctx.create_sampler_state(s))
                .collect();

            let mut resources = Vec::with_capacity(resource_info.len());
            let mut globals: Vec<*mut u32> = Vec::with_capacity(resource_info.len());
            for (res, offset) in resource_info {
                resources.push(res);
                globals.push(unsafe { input.as_mut_ptr().byte_add(offset) }.cast());
            }

            let temp_cso;
            let cso = match &nir_kernel_build.nir_or_cso {
                KernelDevStateVariant::Cso(cso) => cso,
                KernelDevStateVariant::Nir(nir) => {
                    temp_cso = CSOWrapper::new(q.device, nir);
                    &temp_cso
                }
            };

            let sviews_len = sviews.len();
            ctx.bind_compute_state(cso.cso_ptr);
            ctx.bind_sampler_states(&samplers);
            ctx.set_sampler_views(sviews);
            ctx.set_shader_images(&iviews);
            ctx.set_global_binding(resources.as_slice(), &mut globals);

            for z in 0..grid[2].div_ceil(hw_max_grid[2]) {
                for y in 0..grid[1].div_ceil(hw_max_grid[1]) {
                    for x in 0..grid[0].div_ceil(hw_max_grid[0]) {
                        if let Some(workgroup_id_offset_loc) = workgroup_id_offset_loc {
                            let this_offsets =
                                [x * hw_max_grid[0], y * hw_max_grid[1], z * hw_max_grid[2]];

                            if q.device.address_bits() == 64 {
                                let val = this_offsets.map(|v| v as u64);
                                input[workgroup_id_offset_loc..workgroup_id_offset_loc + 24]
                                    .copy_from_slice(unsafe { as_byte_slice(&val) });
                            } else {
                                let val = this_offsets.map(|v| v as u32);
                                input[workgroup_id_offset_loc..workgroup_id_offset_loc + 12]
                                    .copy_from_slice(unsafe { as_byte_slice(&val) });
                            }
                        }

                        let this_grid = [
                            cmp::min(hw_max_grid[0], grid[0] - hw_max_grid[0] * x) as u32,
                            cmp::min(hw_max_grid[1], grid[1] - hw_max_grid[1] * y) as u32,
                            cmp::min(hw_max_grid[2], grid[2] - hw_max_grid[2] * z) as u32,
                        ];

                        ctx.update_cb0(&input)?;
                        ctx.launch_grid(work_dim, block, this_grid, variable_local_size as u32);

                        if Platform::dbg().sync_every_event {
                            ctx.flush().wait();
                        }
                    }
                }
            }

            ctx.clear_global_binding(globals.len() as u32);
            ctx.clear_sampler_views(sviews_len as u32);
            ctx.clear_sampler_states(samplers.len() as u32);

            ctx.bind_compute_state(ptr::null_mut());

            ctx.memory_barrier(PIPE_BARRIER_GLOBAL_BUFFER);

            samplers.iter().for_each(|s| ctx.delete_sampler_state(*s));

            if let Some(printf_buf) = &printf_buf {
                let tx = ctx
                    .buffer_map(printf_buf, 0, printf_size as i32, RWFlags::RD)
                    .ok_or(CL_OUT_OF_RESOURCES)?;
                let mut buf: &[u8] =
                    unsafe { slice::from_raw_parts(tx.ptr().cast(), printf_size as usize) };
                let length = u32::from_ne_bytes(*extract(&mut buf));

                // update our slice to make sure we don't go out of bounds
                buf = &buf[0..(length - 4) as usize];
                if let Some(pf) = &nir_kernel_build.printf_info {
                    pf.u_printf(buf)
                }
            }

            Ok(())
        }))
    }

    pub fn arg_values(&self) -> MutexGuard<Vec<Option<KernelArgValue>>> {
        self.values.lock().unwrap()
    }

    pub fn set_kernel_arg(&self, idx: usize, arg: KernelArgValue) -> CLResult<()> {
        self.values
            .lock()
            .unwrap()
            .get_mut(idx)
            .ok_or(CL_INVALID_ARG_INDEX)?
            .replace(arg);
        Ok(())
    }

    pub fn access_qualifier(&self, idx: cl_uint) -> cl_kernel_arg_access_qualifier {
        let aq = self.kernel_info.args[idx as usize].spirv.access_qualifier;

        if aq
            == clc_kernel_arg_access_qualifier::CLC_KERNEL_ARG_ACCESS_READ
                | clc_kernel_arg_access_qualifier::CLC_KERNEL_ARG_ACCESS_WRITE
        {
            CL_KERNEL_ARG_ACCESS_READ_WRITE
        } else if aq == clc_kernel_arg_access_qualifier::CLC_KERNEL_ARG_ACCESS_READ {
            CL_KERNEL_ARG_ACCESS_READ_ONLY
        } else if aq == clc_kernel_arg_access_qualifier::CLC_KERNEL_ARG_ACCESS_WRITE {
            CL_KERNEL_ARG_ACCESS_WRITE_ONLY
        } else {
            CL_KERNEL_ARG_ACCESS_NONE
        }
    }

    pub fn address_qualifier(&self, idx: cl_uint) -> cl_kernel_arg_address_qualifier {
        match self.kernel_info.args[idx as usize].spirv.address_qualifier {
            clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_PRIVATE => {
                CL_KERNEL_ARG_ADDRESS_PRIVATE
            }
            clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_CONSTANT => {
                CL_KERNEL_ARG_ADDRESS_CONSTANT
            }
            clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_LOCAL => {
                CL_KERNEL_ARG_ADDRESS_LOCAL
            }
            clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_GLOBAL => {
                CL_KERNEL_ARG_ADDRESS_GLOBAL
            }
        }
    }

    pub fn type_qualifier(&self, idx: cl_uint) -> cl_kernel_arg_type_qualifier {
        let tq = self.kernel_info.args[idx as usize].spirv.type_qualifier;
        let zero = clc_kernel_arg_type_qualifier(0);
        let mut res = CL_KERNEL_ARG_TYPE_NONE;

        if tq & clc_kernel_arg_type_qualifier::CLC_KERNEL_ARG_TYPE_CONST != zero {
            res |= CL_KERNEL_ARG_TYPE_CONST;
        }

        if tq & clc_kernel_arg_type_qualifier::CLC_KERNEL_ARG_TYPE_RESTRICT != zero {
            res |= CL_KERNEL_ARG_TYPE_RESTRICT;
        }

        if tq & clc_kernel_arg_type_qualifier::CLC_KERNEL_ARG_TYPE_VOLATILE != zero {
            res |= CL_KERNEL_ARG_TYPE_VOLATILE;
        }

        res.into()
    }

    pub fn work_group_size(&self) -> [usize; 3] {
        self.kernel_info.work_group_size
    }

    pub fn num_subgroups(&self) -> usize {
        self.kernel_info.num_subgroups
    }

    pub fn subgroup_size(&self) -> usize {
        self.kernel_info.subgroup_size
    }

    pub fn arg_name(&self, idx: cl_uint) -> Option<&CStr> {
        let name = &self.kernel_info.args[idx as usize].spirv.name;
        name.is_empty().not().then_some(name)
    }

    pub fn arg_type_name(&self, idx: cl_uint) -> Option<&CStr> {
        let type_name = &self.kernel_info.args[idx as usize].spirv.type_name;
        type_name.is_empty().not().then_some(type_name)
    }

    pub fn priv_mem_size(&self, dev: &Device) -> cl_ulong {
        self.builds.get(dev).unwrap().info.private_memory as cl_ulong
    }

    pub fn max_threads_per_block(&self, dev: &Device) -> usize {
        self.builds.get(dev).unwrap().info.max_threads as usize
    }

    pub fn preferred_simd_size(&self, dev: &Device) -> usize {
        self.builds.get(dev).unwrap().info.preferred_simd_size as usize
    }

    pub fn local_mem_size(&self, dev: &Device) -> cl_ulong {
        // TODO: take alignment into account?
        // this is purely informational so it shouldn't even matter
        let local =
            self.builds.get(dev).unwrap()[NirKernelVariant::Default].shared_size as cl_ulong;
        let args: cl_ulong = self
            .arg_values()
            .iter()
            .map(|arg| match arg {
                Some(KernelArgValue::LocalMem(val)) => *val as cl_ulong,
                // If the local memory size, for any pointer argument to the kernel declared with
                // the __local address qualifier, is not specified, its size is assumed to be 0.
                _ => 0,
            })
            .sum();

        local + args
    }

    pub fn has_svm_devs(&self) -> bool {
        self.prog.devs.iter().any(|dev| dev.svm_supported())
    }

    pub fn subgroup_sizes(&self, dev: &Device) -> Vec<usize> {
        SetBitIndices::from_msb(self.builds.get(dev).unwrap().info.simd_sizes)
            .map(|bit| 1 << bit)
            .collect()
    }

    pub fn subgroups_for_block(&self, dev: &Device, block: &[usize]) -> usize {
        let subgroup_size = self.subgroup_size_for_block(dev, block);
        if subgroup_size == 0 {
            return 0;
        }

        let threads: usize = block.iter().product();
        threads.div_ceil(subgroup_size)
    }

    pub fn subgroup_size_for_block(&self, dev: &Device, block: &[usize]) -> usize {
        let subgroup_sizes = self.subgroup_sizes(dev);
        if subgroup_sizes.is_empty() {
            return 0;
        }

        if subgroup_sizes.len() == 1 {
            return subgroup_sizes[0];
        }

        let block = [
            *block.first().unwrap_or(&1) as u32,
            *block.get(1).unwrap_or(&1) as u32,
            *block.get(2).unwrap_or(&1) as u32,
        ];

        // TODO: this _might_ bite us somewhere, but I think it probably doesn't matter
        match &self.builds.get(dev).unwrap()[NirKernelVariant::Default].nir_or_cso {
            KernelDevStateVariant::Cso(cso) => {
                dev.helper_ctx()
                    .compute_state_subgroup_size(cso.cso_ptr, &block) as usize
            }
            _ => {
                panic!()
            }
        }
    }
}

impl Clone for Kernel {
    fn clone(&self) -> Self {
        Self {
            base: CLObjectBase::new(RusticlTypes::Kernel),
            prog: Arc::clone(&self.prog),
            name: self.name.clone(),
            values: Mutex::new(self.arg_values().clone()),
            builds: self.builds.clone(),
            kernel_info: Arc::clone(&self.kernel_info),
        }
    }
}
