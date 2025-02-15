use crate::compiler::nir::NirShader;
use crate::pipe::context::*;
use crate::pipe::device::*;
use crate::pipe::resource::*;
use crate::util::disk_cache::*;

use mesa_rust_gen::*;
use mesa_rust_util::has_required_feature;
use mesa_rust_util::ptr::ThreadSafeCPtr;

use std::ffi::CStr;
use std::os::raw::c_schar;
use std::os::raw::c_uchar;
use std::os::raw::c_void;
use std::ptr;
use std::sync::Arc;

#[derive(PartialEq)]
pub struct PipeScreen {
    ldev: PipeLoaderDevice,
    screen: ThreadSafeCPtr<pipe_screen>,
}

pub const UUID_SIZE: usize = PIPE_UUID_SIZE as usize;
const LUID_SIZE: usize = PIPE_LUID_SIZE as usize;

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum ResourceType {
    Normal,
    Staging,
}

impl ResourceType {
    fn apply(&self, tmpl: &mut pipe_resource) {
        match self {
            Self::Staging => {
                tmpl.set_usage(pipe_resource_usage::PIPE_USAGE_STAGING);
                tmpl.flags |= PIPE_RESOURCE_FLAG_MAP_PERSISTENT | PIPE_RESOURCE_FLAG_MAP_COHERENT;
                tmpl.bind |= PIPE_BIND_LINEAR;
            }
            Self::Normal => {}
        }
    }
}

impl PipeScreen {
    pub(super) fn new(ldev: PipeLoaderDevice, screen: *mut pipe_screen) -> Option<Self> {
        if screen.is_null() || !has_required_cbs(screen) {
            return None;
        }

        Some(Self {
            ldev,
            // SAFETY: `pipe_screen` is considered a thread-safe type
            screen: unsafe { ThreadSafeCPtr::new(screen)? },
        })
    }

    fn screen(&self) -> &pipe_screen {
        // SAFETY: We own the pointer, so it's valid for every caller of this function as we are
        //         responsible of freeing it.
        unsafe { self.screen.as_ref() }
    }

    pub fn caps(&self) -> &pipe_caps {
        &self.screen().caps
    }

    pub fn create_context(self: &Arc<Self>) -> Option<PipeContext> {
        PipeContext::new(
            unsafe {
                self.screen().context_create.unwrap()(
                    self.screen.as_ptr(),
                    ptr::null_mut(),
                    PIPE_CONTEXT_COMPUTE_ONLY | PIPE_CONTEXT_NO_LOD_BIAS,
                )
            },
            self,
        )
    }

    fn resource_create(&self, tmpl: &pipe_resource) -> Option<PipeResource> {
        PipeResource::new(
            unsafe { self.screen().resource_create.unwrap()(self.screen.as_ptr(), tmpl) },
            false,
        )
    }

    fn resource_create_from_user(
        &self,
        tmpl: &pipe_resource,
        mem: *mut c_void,
    ) -> Option<PipeResource> {
        PipeResource::new(
            unsafe { self.screen().resource_from_user_memory?(self.screen.as_ptr(), tmpl, mem) },
            true,
        )
    }

    pub fn resource_create_buffer(
        &self,
        size: u32,
        res_type: ResourceType,
        pipe_bind: u32,
    ) -> Option<PipeResource> {
        let mut tmpl = pipe_resource::default();

        tmpl.set_target(pipe_texture_target::PIPE_BUFFER);
        tmpl.width0 = size;
        tmpl.height0 = 1;
        tmpl.depth0 = 1;
        tmpl.array_size = 1;
        tmpl.bind = pipe_bind;

        res_type.apply(&mut tmpl);

        self.resource_create(&tmpl)
    }

    pub fn resource_create_buffer_from_user(
        &self,
        size: u32,
        mem: *mut c_void,
        pipe_bind: u32,
    ) -> Option<PipeResource> {
        let mut tmpl = pipe_resource::default();

        tmpl.set_target(pipe_texture_target::PIPE_BUFFER);
        tmpl.width0 = size;
        tmpl.height0 = 1;
        tmpl.depth0 = 1;
        tmpl.array_size = 1;
        tmpl.bind = pipe_bind;

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
        res_type: ResourceType,
        support_image: bool,
    ) -> Option<PipeResource> {
        let mut tmpl = pipe_resource::default();

        tmpl.set_target(target);
        tmpl.set_format(format);
        tmpl.width0 = width;
        tmpl.height0 = height;
        tmpl.depth0 = depth;
        tmpl.array_size = array_size;
        tmpl.bind = PIPE_BIND_SAMPLER_VIEW;

        if support_image {
            tmpl.bind |= PIPE_BIND_SHADER_IMAGE;
        }

        res_type.apply(&mut tmpl);

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
        support_image: bool,
    ) -> Option<PipeResource> {
        let mut tmpl = pipe_resource::default();

        tmpl.set_target(target);
        tmpl.set_format(format);
        tmpl.width0 = width;
        tmpl.height0 = height;
        tmpl.depth0 = depth;
        tmpl.array_size = array_size;
        tmpl.bind = PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_LINEAR;

        if support_image {
            tmpl.bind |= PIPE_BIND_SHADER_IMAGE;
        }

        self.resource_create_from_user(&tmpl, mem)
    }

    pub fn resource_import_dmabuf(
        &self,
        handle: u32,
        modifier: u64,
        target: pipe_texture_target,
        format: pipe_format,
        stride: u32,
        width: u32,
        height: u16,
        depth: u16,
        array_size: u16,
        support_image: bool,
    ) -> Option<PipeResource> {
        let mut tmpl = pipe_resource::default();
        let mut handle = winsys_handle {
            type_: WINSYS_HANDLE_TYPE_FD,
            handle: handle,
            modifier: modifier,
            format: format as u64,
            stride: stride,
            ..Default::default()
        };

        tmpl.set_target(target);
        tmpl.set_format(format);
        tmpl.width0 = width;
        tmpl.height0 = height;
        tmpl.depth0 = depth;
        tmpl.array_size = array_size;

        if target == pipe_texture_target::PIPE_BUFFER {
            tmpl.bind = PIPE_BIND_GLOBAL
        } else {
            tmpl.bind = PIPE_BIND_SAMPLER_VIEW;
            if support_image {
                tmpl.bind |= PIPE_BIND_SHADER_IMAGE;
            }
        }

        unsafe {
            PipeResource::new(
                self.screen().resource_from_handle.unwrap()(
                    self.screen.as_ptr(),
                    &tmpl,
                    &mut handle,
                    0,
                ),
                false,
            )
        }
    }

    pub fn shader_caps(&self, t: pipe_shader_type) -> &pipe_shader_caps {
        &self.screen().shader_caps[t as usize]
    }

    pub fn compute_caps(&self) -> &pipe_compute_caps {
        &self.screen().compute_caps
    }

    pub fn driver_name(&self) -> &CStr {
        self.ldev.driver_name()
    }

    pub fn name(&self) -> &CStr {
        unsafe { CStr::from_ptr(self.screen().get_name.unwrap()(self.screen.as_ptr())) }
    }

    pub fn device_node_mask(&self) -> Option<u32> {
        unsafe { Some(self.screen().get_device_node_mask?(self.screen.as_ptr())) }
    }

    pub fn device_uuid(&self) -> Option<[c_uchar; UUID_SIZE]> {
        let mut uuid = [0; UUID_SIZE];
        let ptr = uuid.as_mut_ptr();
        unsafe {
            self.screen().get_device_uuid?(self.screen.as_ptr(), ptr.cast());
        }

        Some(uuid)
    }

    pub fn device_luid(&self) -> Option<[c_uchar; LUID_SIZE]> {
        let mut luid = [0; LUID_SIZE];
        let ptr = luid.as_mut_ptr();
        unsafe { self.screen().get_device_luid?(self.screen.as_ptr(), ptr.cast()) }

        Some(luid)
    }

    pub fn device_vendor(&self) -> &CStr {
        unsafe {
            CStr::from_ptr(self.screen().get_device_vendor.unwrap()(
                self.screen.as_ptr(),
            ))
        }
    }

    pub fn device_type(&self) -> pipe_loader_device_type {
        self.ldev.device_type()
    }

    pub fn driver_uuid(&self) -> Option<[c_schar; UUID_SIZE]> {
        let mut uuid = [0; UUID_SIZE];
        let ptr = uuid.as_mut_ptr();
        unsafe {
            self.screen().get_driver_uuid?(self.screen.as_ptr(), ptr.cast());
        }

        Some(uuid)
    }

    pub fn cl_cts_version(&self) -> &CStr {
        unsafe {
            let ptr = self
                .screen()
                .get_cl_cts_version
                .map_or(ptr::null(), |get_cl_cts_version| {
                    get_cl_cts_version(self.screen.as_ptr())
                });
            if ptr.is_null() {
                // this string is good enough to pass the CTS
                c"v0000-01-01-00"
            } else {
                CStr::from_ptr(ptr)
            }
        }
    }

    pub fn is_format_supported(
        &self,
        format: pipe_format,
        target: pipe_texture_target,
        bindings: u32,
    ) -> bool {
        unsafe {
            self.screen().is_format_supported.unwrap()(
                self.screen.as_ptr(),
                format,
                target,
                0,
                0,
                bindings,
            )
        }
    }

    pub fn get_timestamp(&self) -> u64 {
        unsafe {
            self.screen()
                .get_timestamp
                .unwrap_or(u_default_get_timestamp)(self.screen.as_ptr())
        }
    }

    pub fn is_res_handle_supported(&self) -> bool {
        self.screen().resource_from_handle.is_some() && self.screen().resource_get_handle.is_some()
    }

    pub fn nir_shader_compiler_options(
        &self,
        shader: pipe_shader_type,
    ) -> *const nir_shader_compiler_options {
        unsafe {
            self.screen().get_compiler_options.unwrap()(
                self.screen.as_ptr(),
                pipe_shader_ir::PIPE_SHADER_IR_NIR,
                shader,
            )
            .cast()
        }
    }

    pub fn shader_cache(&self) -> Option<DiskCacheBorrowed> {
        let ptr = unsafe { self.screen().get_disk_shader_cache?(self.screen.as_ptr()) };

        DiskCacheBorrowed::from_ptr(ptr)
    }

    /// returns true if finalize_nir was called
    pub fn finalize_nir(&self, nir: &NirShader) -> bool {
        if let Some(func) = self.screen().finalize_nir {
            unsafe {
                func(self.screen.as_ptr(), nir.get_nir().cast());
            }
            true
        } else {
            false
        }
    }

    pub(super) fn unref_fence(&self, mut fence: *mut pipe_fence_handle) {
        unsafe {
            self.screen().fence_reference.unwrap()(
                self.screen.as_ptr(),
                &mut fence,
                ptr::null_mut(),
            );
        }
    }

    pub(super) fn fence_finish(&self, fence: *mut pipe_fence_handle) {
        unsafe {
            self.screen().fence_finish.unwrap()(
                self.screen.as_ptr(),
                ptr::null_mut(),
                fence,
                OS_TIMEOUT_INFINITE as u64,
            );
        }
    }

    pub fn query_memory_info(&self) -> Option<pipe_memory_info> {
        let mut info = pipe_memory_info::default();
        unsafe {
            self.screen().query_memory_info?(self.screen.as_ptr(), &mut info);
        }
        Some(info)
    }
}

impl Drop for PipeScreen {
    fn drop(&mut self) {
        unsafe { self.screen().destroy.unwrap()(self.screen.as_ptr()) }
    }
}

fn has_required_cbs(screen: *mut pipe_screen) -> bool {
    let screen = unsafe { *screen };
    // Use '&' to evaluate all features and to not stop
    // on first missing one to list all missing features.
    has_required_feature!(screen, context_create)
        & has_required_feature!(screen, destroy)
        & has_required_feature!(screen, fence_finish)
        & has_required_feature!(screen, fence_reference)
        & has_required_feature!(screen, get_compiler_options)
        & has_required_feature!(screen, get_name)
        & has_required_feature!(screen, is_format_supported)
        & has_required_feature!(screen, resource_create)
}
