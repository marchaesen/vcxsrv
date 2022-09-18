use mesa_rust_gen::*;
use rusticl_opencl_gen::*;

pub fn cl_mem_type_to_texture_target(mem_type: cl_mem_object_type) -> pipe_texture_target {
    match mem_type {
        CL_MEM_OBJECT_IMAGE1D => pipe_texture_target::PIPE_TEXTURE_1D,
        CL_MEM_OBJECT_IMAGE2D => pipe_texture_target::PIPE_TEXTURE_2D,
        CL_MEM_OBJECT_IMAGE3D => pipe_texture_target::PIPE_TEXTURE_3D,
        CL_MEM_OBJECT_IMAGE1D_ARRAY => pipe_texture_target::PIPE_TEXTURE_1D_ARRAY,
        CL_MEM_OBJECT_IMAGE2D_ARRAY => pipe_texture_target::PIPE_TEXTURE_2D_ARRAY,
        CL_MEM_OBJECT_IMAGE1D_BUFFER => pipe_texture_target::PIPE_TEXTURE_1D,
        _ => pipe_texture_target::PIPE_TEXTURE_2D,
    }
}
