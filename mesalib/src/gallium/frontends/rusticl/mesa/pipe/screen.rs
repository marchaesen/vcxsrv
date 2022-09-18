use crate::compiler::nir::NirShader;
use crate::pipe::context::*;
use crate::pipe::device::*;
use crate::pipe::resource::*;
use crate::util::disk_cache::*;

use mesa_rust_gen::*;
use mesa_rust_util::string::*;

use std::convert::TryInto;
use std::mem::size_of;
use std::os::raw::c_void;
use std::ptr;
use std::sync::Arc;

#[derive(PartialEq)]
pub struct PipeScreen {
    ldev: PipeLoaderDevice,
    screen: *mut pipe_screen,
}

// until we have a better solution
pub trait ComputeParam<T> {
    fn compute_param(&self, cap: pipe_compute_cap) -> T;
}

macro_rules! compute_param_impl {
    ($ty:ty) => {
        impl ComputeParam<$ty> for PipeScreen {
            fn compute_param(&self, cap: pipe_compute_cap) -> $ty {
                let size = self.compute_param_wrapped(cap, ptr::null_mut());
                let mut d = [0; size_of::<$ty>()];
                assert_eq!(size as usize, d.len());
                self.compute_param_wrapped(cap, d.as_mut_ptr().cast());
                <$ty>::from_ne_bytes(d)
            }
        }
    };
}

compute_param_impl!(u32);
compute_param_impl!(u64);

impl ComputeParam<Vec<u64>> for PipeScreen {
    fn compute_param(&self, cap: pipe_compute_cap) -> Vec<u64> {
        let size = self.compute_param_wrapped(cap, ptr::null_mut());
        let elems = (size / 8) as usize;

        let mut res: Vec<u64> = Vec::new();
        let mut d: Vec<u8> = vec![0; size as usize];

        self.compute_param_wrapped(cap, d.as_mut_ptr().cast());
        for i in 0..elems {
            let offset = i * 8;
            let slice = &d[offset..offset + 8];
            res.push(u64::from_ne_bytes(slice.try_into().expect("")));
        }
        res
    }
}

impl PipeScreen {
    pub(super) fn new(ldev: PipeLoaderDevice, screen: *mut pipe_screen) -> Option<Arc<Self>> {
        if screen.is_null() || !has_required_cbs(screen) {
            return None;
        }

        Some(Arc::new(Self { ldev, screen }))
    }

    pub fn create_context(self: &Arc<Self>) -> Option<PipeContext> {
        PipeContext::new(
            unsafe {
                (*self.screen).context_create.unwrap()(
                    self.screen,
                    ptr::null_mut(),
                    PIPE_CONTEXT_COMPUTE_ONLY,
                )
            },
            self,
        )
    }

    fn resource_create(&self, tmpl: &pipe_resource) -> Option<PipeResource> {
        PipeResource::new(
            unsafe { (*self.screen).resource_create.unwrap()(self.screen, tmpl) },
            false,
        )
    }

    fn resource_create_from_user(
        &self,
        tmpl: &pipe_resource,
        mem: *mut c_void,
    ) -> Option<PipeResource> {
        unsafe {
            if let Some(func) = (*self.screen).resource_from_user_memory {
                PipeResource::new(func(self.screen, tmpl, mem), true)
            } else {
                None
            }
        }
    }

    pub fn resource_create_buffer(&self, size: u32) -> Option<PipeResource> {
        let mut tmpl = pipe_resource::default();

        tmpl.set_target(pipe_texture_target::PIPE_BUFFER);
        tmpl.width0 = size;
        tmpl.height0 = 1;
        tmpl.depth0 = 1;
        tmpl.array_size = 1;
        tmpl.bind = PIPE_BIND_GLOBAL;

        self.resource_create(&tmpl)
    }

    pub fn resource_create_buffer_from_user(
        &self,
        size: u32,
        mem: *mut c_void,
    ) -> Option<PipeResource> {
        let mut tmpl = pipe_resource::default();

        tmpl.set_target(pipe_texture_target::PIPE_BUFFER);
        tmpl.width0 = size;
        tmpl.height0 = 1;
        tmpl.depth0 = 1;
        tmpl.array_size = 1;
        tmpl.bind = PIPE_BIND_GLOBAL;

        self.resource_create_from_user(&tmpl, mem)
    }

    pub fn resource_create_texture(
        &self,
        width: u32,
        height: u16,
        depth: u16,
        array_size: u16,
        target: pipe_texture_target,
        format: pipe_format,
    ) -> Option<PipeResource> {
        let mut tmpl = pipe_resource::default();

        tmpl.set_target(target);
        tmpl.set_format(format);
        tmpl.width0 = width;
        tmpl.height0 = height;
        tmpl.depth0 = depth;
        tmpl.array_size = array_size;
        tmpl.bind = PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_SHADER_IMAGE;

        self.resource_create(&tmpl)
    }

    pub fn resource_create_texture_from_user(
        &self,
        width: u32,
        height: u16,
        depth: u16,
        array_size: u16,
        target: pipe_texture_target,
        format: pipe_format,
        mem: *mut c_void,
    ) -> Option<PipeResource> {
        let mut tmpl = pipe_resource::default();

        tmpl.set_target(target);
        tmpl.set_format(format);
        tmpl.width0 = width;
        tmpl.height0 = height;
        tmpl.depth0 = depth;
        tmpl.array_size = array_size;
        tmpl.bind = PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_SHADER_IMAGE;

        self.resource_create_from_user(&tmpl, mem)
    }

    pub fn param(&self, cap: pipe_cap) -> i32 {
        unsafe { (*self.screen).get_param.unwrap()(self.screen, cap) }
    }

    pub fn shader_param(&self, t: pipe_shader_type, cap: pipe_shader_cap) -> i32 {
        unsafe { (*self.screen).get_shader_param.unwrap()(self.screen, t, cap) }
    }

    fn compute_param_wrapped(&self, cap: pipe_compute_cap, ptr: *mut c_void) -> i32 {
        let s = &mut unsafe { *self.screen };
        unsafe {
            s.get_compute_param.unwrap()(self.screen, pipe_shader_ir::PIPE_SHADER_IR_NIR, cap, ptr)
        }
    }

    pub fn name(&self) -> String {
        unsafe {
            let s = *self.screen;
            c_string_to_string(s.get_name.unwrap()(self.screen))
        }
    }

    pub fn device_vendor(&self) -> String {
        unsafe {
            let s = *self.screen;
            c_string_to_string(s.get_device_vendor.unwrap()(self.screen))
        }
    }

    pub fn device_type(&self) -> pipe_loader_device_type {
        unsafe { *self.ldev.ldev }.type_
    }

    pub fn is_format_supported(
        &self,
        format: pipe_format,
        target: pipe_texture_target,
        bindings: u32,
    ) -> bool {
        let s = &mut unsafe { *self.screen };
        unsafe { s.is_format_supported.unwrap()(self.screen, format, target, 0, 0, bindings) }
    }

    pub fn nir_shader_compiler_options(
        &self,
        shader: pipe_shader_type,
    ) -> *const nir_shader_compiler_options {
        unsafe {
            (*self.screen).get_compiler_options.unwrap()(
                self.screen,
                pipe_shader_ir::PIPE_SHADER_IR_NIR,
                shader,
            )
            .cast()
        }
    }

    pub fn shader_cache(&self) -> Option<DiskCacheBorrowed> {
        let s = &mut unsafe { *self.screen };

        let ptr = if let Some(func) = s.get_disk_shader_cache {
            unsafe { func(self.screen) }
        } else {
            ptr::null_mut()
        };

        DiskCacheBorrowed::from_ptr(ptr)
    }

    pub fn finalize_nir(&self, nir: &NirShader) {
        let s = &mut unsafe { *self.screen };
        if let Some(func) = s.finalize_nir {
            unsafe {
                func(self.screen, nir.get_nir().cast());
            }
        }
    }

    pub(super) fn unref_fence(&self, mut fence: *mut pipe_fence_handle) {
        unsafe {
            let s = &mut *self.screen;
            s.fence_reference.unwrap()(s, &mut fence, ptr::null_mut());
        }
    }

    pub(super) fn fence_finish(&self, fence: *mut pipe_fence_handle) {
        unsafe {
            let s = &mut *self.screen;
            s.fence_finish.unwrap()(s, ptr::null_mut(), fence, PIPE_TIMEOUT_INFINITE as u64);
        }
    }
}

impl Drop for PipeScreen {
    fn drop(&mut self) {
        unsafe {
            (*self.screen).destroy.unwrap()(self.screen);
        }
    }
}

fn has_required_cbs(screen: *mut pipe_screen) -> bool {
    let s = unsafe { *screen };
    s.context_create.is_some()
        && s.destroy.is_some()
        && s.fence_finish.is_some()
        && s.fence_reference.is_some()
        && s.get_compiler_options.is_some()
        && s.get_compute_param.is_some()
        && s.get_name.is_some()
        && s.get_param.is_some()
        && s.get_shader_param.is_some()
        && s.is_format_supported.is_some()
        && s.resource_create.is_some()
}
