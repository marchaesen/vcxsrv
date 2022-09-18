use crate::compiler::nir::*;
use crate::pipe::fence::*;
use crate::pipe::resource::*;
use crate::pipe::screen::*;
use crate::pipe::transfer::*;

use mesa_rust_gen::*;

use std::os::raw::*;
use std::ptr;
use std::ptr::*;
use std::sync::Arc;

pub struct PipeContext {
    pipe: NonNull<pipe_context>,
    screen: Arc<PipeScreen>,
}

unsafe impl Send for PipeContext {}
unsafe impl Sync for PipeContext {}

#[repr(u32)]
pub enum RWFlags {
    RD = pipe_map_flags::PIPE_MAP_READ.0,
    WR = pipe_map_flags::PIPE_MAP_WRITE.0,
    RW = pipe_map_flags::PIPE_MAP_READ_WRITE.0,
}

impl From<RWFlags> for pipe_map_flags {
    fn from(rw: RWFlags) -> Self {
        pipe_map_flags(rw as u32)
    }
}

impl PipeContext {
    pub(super) fn new(context: *mut pipe_context, screen: &Arc<PipeScreen>) -> Option<Self> {
        let s = Self {
            pipe: NonNull::new(context)?,
            screen: screen.clone(),
        };

        if !has_required_cbs(unsafe { s.pipe.as_ref() }) {
            assert!(false, "Context missing features. This should never happen!");
            return None;
        }

        Some(s)
    }

    pub fn buffer_subdata(
        &self,
        res: &PipeResource,
        offset: c_uint,
        data: *const c_void,
        size: c_uint,
    ) {
        unsafe {
            self.pipe.as_ref().buffer_subdata.unwrap()(
                self.pipe.as_ptr(),
                res.pipe(),
                pipe_map_flags::PIPE_MAP_WRITE.0, // TODO PIPE_MAP_x
                offset,
                size,
                data,
            )
        }
    }

    pub fn texture_subdata(
        &self,
        res: &PipeResource,
        bx: &pipe_box,
        data: *const c_void,
        stride: u32,
        layer_stride: u32,
    ) {
        unsafe {
            self.pipe.as_ref().texture_subdata.unwrap()(
                self.pipe.as_ptr(),
                res.pipe(),
                0,
                pipe_map_flags::PIPE_MAP_WRITE.0, // TODO PIPE_MAP_x
                bx,
                data,
                stride,
                layer_stride,
            )
        }
    }

    pub fn clear_buffer(&self, res: &PipeResource, pattern: &[u8], offset: u32, size: u32) {
        unsafe {
            self.pipe.as_ref().clear_buffer.unwrap()(
                self.pipe.as_ptr(),
                res.pipe(),
                offset,
                size,
                pattern.as_ptr().cast(),
                pattern.len() as i32,
            )
        }
    }

    pub fn clear_texture(&self, res: &PipeResource, pattern: &[u8], bx: &pipe_box) {
        unsafe {
            self.pipe.as_ref().clear_texture.unwrap()(
                self.pipe.as_ptr(),
                res.pipe(),
                0,
                bx,
                pattern.as_ptr().cast(),
            )
        }
    }

    pub fn resource_copy_region(
        &self,
        src: &PipeResource,
        dst: &PipeResource,
        dst_offset: &[u32; 3],
        bx: &pipe_box,
    ) {
        unsafe {
            self.pipe.as_ref().resource_copy_region.unwrap()(
                self.pipe.as_ptr(),
                dst.pipe(),
                0,
                dst_offset[0],
                dst_offset[1],
                dst_offset[2],
                src.pipe(),
                0,
                bx,
            )
        }
    }

    pub fn buffer_map(
        &self,
        res: &PipeResource,
        offset: i32,
        size: i32,
        block: bool,
        rw: RWFlags,
    ) -> PipeTransfer {
        let mut b = pipe_box::default();
        let mut out: *mut pipe_transfer = ptr::null_mut();

        b.x = offset;
        b.width = size;
        b.height = 1;
        b.depth = 1;

        let flags = match block {
            false => pipe_map_flags::PIPE_MAP_UNSYNCHRONIZED,
            true => pipe_map_flags(0),
        } | rw.into();

        let ptr = unsafe {
            self.pipe.as_ref().buffer_map.unwrap()(
                self.pipe.as_ptr(),
                res.pipe(),
                0,
                flags.0,
                &b,
                &mut out,
            )
        };

        PipeTransfer::new(true, out, ptr)
    }

    pub(super) fn buffer_unmap(&self, tx: *mut pipe_transfer) {
        unsafe { self.pipe.as_ref().buffer_unmap.unwrap()(self.pipe.as_ptr(), tx) };
    }

    pub fn texture_map(
        &self,
        res: &PipeResource,
        bx: &pipe_box,
        block: bool,
        rw: RWFlags,
    ) -> PipeTransfer {
        let mut out: *mut pipe_transfer = ptr::null_mut();

        let flags = match block {
            false => pipe_map_flags::PIPE_MAP_UNSYNCHRONIZED,
            true => pipe_map_flags(0),
        } | rw.into();

        let ptr = unsafe {
            self.pipe.as_ref().texture_map.unwrap()(
                self.pipe.as_ptr(),
                res.pipe(),
                0,
                flags.0,
                bx,
                &mut out,
            )
        };

        PipeTransfer::new(false, out, ptr)
    }

    pub(super) fn texture_unmap(&self, tx: *mut pipe_transfer) {
        unsafe { self.pipe.as_ref().texture_unmap.unwrap()(self.pipe.as_ptr(), tx) };
    }

    pub fn blit(&self, src: &PipeResource, dst: &PipeResource) {
        let mut blit_info = pipe_blit_info::default();
        blit_info.src.resource = src.pipe();
        blit_info.dst.resource = dst.pipe();

        println!("blit not implemented!");

        unsafe { self.pipe.as_ref().blit.unwrap()(self.pipe.as_ptr(), &blit_info) }
    }

    pub fn create_compute_state(
        &self,
        nir: &NirShader,
        input_mem: u32,
        local_mem: u32,
    ) -> *mut c_void {
        let state = pipe_compute_state {
            ir_type: pipe_shader_ir::PIPE_SHADER_IR_NIR,
            prog: nir.dup_for_driver().cast(),
            req_input_mem: input_mem,
            req_local_mem: local_mem,
            req_private_mem: 0,
        };
        unsafe { self.pipe.as_ref().create_compute_state.unwrap()(self.pipe.as_ptr(), &state) }
    }

    pub fn bind_compute_state(&self, state: *mut c_void) {
        unsafe { self.pipe.as_ref().bind_compute_state.unwrap()(self.pipe.as_ptr(), state) }
    }

    pub fn delete_compute_state(&self, state: *mut c_void) {
        unsafe { self.pipe.as_ref().delete_compute_state.unwrap()(self.pipe.as_ptr(), state) }
    }

    pub fn create_sampler_state(&self, state: &pipe_sampler_state) -> *mut c_void {
        unsafe { self.pipe.as_ref().create_sampler_state.unwrap()(self.pipe.as_ptr(), state) }
    }

    pub fn bind_sampler_states(&self, samplers: &[*mut c_void]) {
        let mut samplers = samplers.to_owned();
        unsafe {
            self.pipe.as_ref().bind_sampler_states.unwrap()(
                self.pipe.as_ptr(),
                pipe_shader_type::PIPE_SHADER_COMPUTE,
                0,
                samplers.len() as u32,
                samplers.as_mut_ptr(),
            )
        }
    }

    pub fn clear_sampler_states(&self, count: u32) {
        unsafe {
            self.pipe.as_ref().bind_sampler_states.unwrap()(
                self.pipe.as_ptr(),
                pipe_shader_type::PIPE_SHADER_COMPUTE,
                0,
                count,
                ptr::null_mut(),
            )
        }
    }

    pub fn delete_sampler_state(&self, ptr: *mut c_void) {
        unsafe { self.pipe.as_ref().delete_sampler_state.unwrap()(self.pipe.as_ptr(), ptr) }
    }

    pub fn launch_grid(&self, work_dim: u32, block: [u32; 3], grid: [u32; 3], input: &[u8]) {
        let info = pipe_grid_info {
            pc: 0,
            input: input.as_ptr().cast(),
            work_dim: work_dim,
            block: block,
            last_block: [0; 3],
            grid: grid,
            grid_base: [0; 3],
            indirect: ptr::null_mut(),
            indirect_offset: 0,
        };
        unsafe { self.pipe.as_ref().launch_grid.unwrap()(self.pipe.as_ptr(), &info) }
    }

    pub fn set_global_binding(&self, res: &[Option<Arc<PipeResource>>], out: &mut [*mut u32]) {
        let mut res: Vec<_> = res
            .iter()
            .map(|o| o.as_ref().map_or(ptr::null_mut(), |r| r.pipe()))
            .collect();
        unsafe {
            self.pipe.as_ref().set_global_binding.unwrap()(
                self.pipe.as_ptr(),
                0,
                res.len() as u32,
                res.as_mut_ptr(),
                out.as_mut_ptr(),
            )
        }
    }

    pub fn create_sampler_view(
        &self,
        res: &PipeResource,
        format: pipe_format,
    ) -> *mut pipe_sampler_view {
        let template = res.pipe_sampler_view_template(format);
        unsafe {
            self.pipe.as_ref().create_sampler_view.unwrap()(
                self.pipe.as_ptr(),
                res.pipe(),
                &template,
            )
        }
    }

    pub fn clear_global_binding(&self, count: u32) {
        unsafe {
            self.pipe.as_ref().set_global_binding.unwrap()(
                self.pipe.as_ptr(),
                0,
                count,
                ptr::null_mut(),
                ptr::null_mut(),
            )
        }
    }

    pub fn set_sampler_views(&self, views: &mut [*mut pipe_sampler_view]) {
        unsafe {
            self.pipe.as_ref().set_sampler_views.unwrap()(
                self.pipe.as_ptr(),
                pipe_shader_type::PIPE_SHADER_COMPUTE,
                0,
                views.len() as u32,
                0,
                false,
                views.as_mut_ptr(),
            )
        }
    }

    pub fn clear_sampler_views(&self, count: u32) {
        unsafe {
            self.pipe.as_ref().set_sampler_views.unwrap()(
                self.pipe.as_ptr(),
                pipe_shader_type::PIPE_SHADER_COMPUTE,
                0,
                count,
                0,
                false,
                ptr::null_mut(),
            )
        }
    }

    pub fn sampler_view_destroy(&self, view: *mut pipe_sampler_view) {
        unsafe { self.pipe.as_ref().sampler_view_destroy.unwrap()(self.pipe.as_ptr(), view) }
    }

    pub fn set_shader_images(&self, images: &[pipe_image_view]) {
        unsafe {
            self.pipe.as_ref().set_shader_images.unwrap()(
                self.pipe.as_ptr(),
                pipe_shader_type::PIPE_SHADER_COMPUTE,
                0,
                images.len() as u32,
                0,
                images.as_ptr(),
            )
        }
    }

    pub fn clear_shader_images(&self, count: u32) {
        unsafe {
            self.pipe.as_ref().set_shader_images.unwrap()(
                self.pipe.as_ptr(),
                pipe_shader_type::PIPE_SHADER_COMPUTE,
                0,
                count,
                0,
                ptr::null_mut(),
            )
        }
    }

    pub fn memory_barrier(&self, barriers: u32) {
        unsafe { self.pipe.as_ref().memory_barrier.unwrap()(self.pipe.as_ptr(), barriers) }
    }

    pub fn flush(&self) -> PipeFence {
        unsafe {
            let mut fence = ptr::null_mut();
            self.pipe.as_ref().flush.unwrap()(self.pipe.as_ptr(), &mut fence, 0);
            PipeFence::new(fence, &self.screen)
        }
    }
}

impl Drop for PipeContext {
    fn drop(&mut self) {
        unsafe {
            self.pipe.as_ref().destroy.unwrap()(self.pipe.as_ptr());
        }
    }
}

fn has_required_cbs(c: &pipe_context) -> bool {
    c.destroy.is_some()
        && c.bind_compute_state.is_some()
        && c.bind_sampler_states.is_some()
        && c.blit.is_some()
        && c.buffer_map.is_some()
        && c.buffer_subdata.is_some()
        && c.buffer_unmap.is_some()
        && c.clear_buffer.is_some()
        && c.clear_texture.is_some()
        && c.create_compute_state.is_some()
        && c.delete_compute_state.is_some()
        && c.delete_sampler_state.is_some()
        && c.flush.is_some()
        && c.launch_grid.is_some()
        && c.memory_barrier.is_some()
        && c.resource_copy_region.is_some()
        && c.sampler_view_destroy.is_some()
        && c.set_global_binding.is_some()
        && c.set_sampler_views.is_some()
        && c.set_shader_images.is_some()
        && c.texture_map.is_some()
        && c.texture_subdata.is_some()
        && c.texture_unmap.is_some()
}
