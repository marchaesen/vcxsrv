use crate::pipe::context::*;

use mesa_rust_gen::*;

use std::os::raw::c_void;
use std::ptr;

pub struct PipeTransfer<'a> {
    pipe: *mut pipe_transfer,
    res: *mut pipe_resource,
    ptr: *mut c_void,
    is_buffer: bool,
    ctx: &'a PipeContext,
}

// SAFETY: Transfers are safe to send between threads
unsafe impl Send for PipeTransfer<'_> {}

impl<'a> Drop for PipeTransfer<'a> {
    fn drop(&mut self) {
        if self.is_buffer {
            self.ctx.buffer_unmap(self.pipe);
        } else {
            self.ctx.texture_unmap(self.pipe);
        }
        unsafe { pipe_resource_reference(&mut self.res, ptr::null_mut()) };
    }
}

impl<'a> PipeTransfer<'a> {
    pub(super) fn new(
        ctx: &'a PipeContext,
        is_buffer: bool,
        pipe: *mut pipe_transfer,
        ptr: *mut c_void,
    ) -> Self {
        let mut res: *mut pipe_resource = ptr::null_mut();
        unsafe { pipe_resource_reference(&mut res, (*pipe).resource) }

        Self {
            pipe: pipe,
            res: res,
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
