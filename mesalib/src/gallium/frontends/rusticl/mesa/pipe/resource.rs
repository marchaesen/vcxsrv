use mesa_rust_gen::*;

use std::ptr;

pub struct PipeResource {
    pipe: *mut pipe_resource,
    pub is_user: bool,
}

impl PipeResource {
    pub fn new(res: *mut pipe_resource, is_user: bool) -> Option<Self> {
        if res.is_null() {
            return None;
        }

        Some(Self {
            pipe: res,
            is_user: is_user,
        })
    }

    pub(super) fn pipe(&self) -> *mut pipe_resource {
        self.pipe
    }

    fn as_ref(&self) -> &pipe_resource {
        unsafe { self.pipe.as_ref().unwrap() }
    }

    pub fn pipe_image_view(&self, format: pipe_format, read_write: bool) -> pipe_image_view {
        let u = if self.as_ref().target() == pipe_texture_target::PIPE_BUFFER {
            pipe_image_view__bindgen_ty_1 {
                buf: pipe_image_view__bindgen_ty_1__bindgen_ty_2 {
                    offset: 0,
                    size: self.as_ref().width0,
                },
            }
        } else {
            let mut tex = pipe_image_view__bindgen_ty_1__bindgen_ty_1::default();
            tex.set_level(0);
            tex.set_first_layer(0);
            if self.as_ref().target() == pipe_texture_target::PIPE_TEXTURE_3D {
                tex.set_last_layer((self.as_ref().depth0 - 1).into());
            } else if self.as_ref().array_size > 0 {
                tex.set_last_layer((self.as_ref().array_size - 1).into());
            } else {
                tex.set_last_layer(0);
            }

            pipe_image_view__bindgen_ty_1 { tex: tex }
        };

        let shader_access = if read_write {
            PIPE_IMAGE_ACCESS_READ_WRITE
        } else {
            PIPE_IMAGE_ACCESS_WRITE
        } as u16;

        pipe_image_view {
            resource: self.pipe(),
            format: format,
            access: 0,
            shader_access: shader_access,
            u: u,
        }
    }

    pub fn pipe_sampler_view_template(&self, format: pipe_format) -> pipe_sampler_view {
        let mut res = pipe_sampler_view::default();
        unsafe {
            u_sampler_view_default_template(&mut res, self.pipe, format);
        }

        if res.target() == pipe_texture_target::PIPE_BUFFER {
            res.u.buf.size = self.as_ref().width0;
        }

        res
    }
}

impl Drop for PipeResource {
    fn drop(&mut self) {
        unsafe { pipe_resource_reference(&mut self.pipe, ptr::null_mut()) }
    }
}
