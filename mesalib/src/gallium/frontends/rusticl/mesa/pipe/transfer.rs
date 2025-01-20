use crate::pipe::context::*;

use mesa_rust_gen::*;

use std::os::raw::c_void;
use std::ptr;

pub struct PipeTransfer<'a> {
    pipe: *mut pipe_transfer,
    ptr: *mut c_void,
    is_buffer: bool,
    ctx: &'a PipeContext,
}

// SAFETY: Transfers are safe to send between threads
unsafe impl Send for PipeTransfer<'_> {}

impl Drop for PipeTransfer<'_> {
    fn drop(&mut self) {
        // we need to copy the pointer here as the driver frees the pipe_transfer object.
        let mut res = unsafe { (*self.pipe).resource };
        if self.is_buffer {
            self.ctx.buffer_unmap(self.pipe);
        } else {
            self.ctx.texture_unmap(self.pipe);
        }
        unsafe { pipe_resource_reference(&mut res, ptr::null_mut()) };
    }
}

impl<'a> PipeTransfer<'a> {
    pub(super) fn new(
        ctx: &'a PipeContext,
        is_buffer: bool,
        pipe: *mut pipe_transfer,
        ptr: *mut c_void,
    ) -> Self {
        unsafe { pipe_resource_reference(&mut ptr::null_mut(), (*pipe).resource) }

        Self {
            pipe: pipe,
            ptr: ptr,
            is_buffer: is_buffer,
            ctx: ctx,
        }
    }

    pub fn ptr(&self) -> *mut c_void {
        self.ptr
    }

    pub fn row_pitch(&self) -> u32 {
        unsafe { (*self.pipe).stride }
    }

    pub fn slice_pitch(&self) -> usize {
        unsafe { (*self.pipe).layer_stride }
    }

    pub fn bx(&self) -> &pipe_box {
        unsafe { &(*self.pipe).box_ }
    }
}
