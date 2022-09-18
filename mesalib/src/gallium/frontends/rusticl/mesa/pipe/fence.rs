use crate::pipe::screen::*;

use mesa_rust_gen::*;

use std::sync::Arc;

pub struct PipeFence {
    fence: *mut pipe_fence_handle,
    screen: Arc<PipeScreen>,
}

impl PipeFence {
    pub fn new(fence: *mut pipe_fence_handle, screen: &Arc<PipeScreen>) -> Self {
        Self {
            fence: fence,
            screen: screen.clone(),
        }
    }

    pub fn wait(&self) {
        self.screen.fence_finish(self.fence);
    }
}

impl Drop for PipeFence {
    fn drop(&mut self) {
        self.screen.unref_fence(self.fence);
    }
}
