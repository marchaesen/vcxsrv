use crate::api::util::*;

use mesa_rust_gen::pipe_format;
use rusticl_opencl_gen::*;

pub struct RusticlImageFormat {
    pub cl_image_format: cl_image_format,
    pub req_for_full_read_or_write: bool,
    pub req_for_embeded_read_or_write: bool,
    pub req_for_full_read_and_write: bool,
    pub req_for_3d_image_write_ext: bool,
    pub pipe: pipe_format,
}

pub const fn rusticl_image_format(
    cl_image_format: cl_image_format,
    req_for_full_read_or_write: bool,
    req_for_embeded_read_or_write: bool,
    req_for_full_read_and_write: bool,
    req_for_3d_image_write_ext: bool,
    pipe: pipe_format,
) -> RusticlImageFormat {
    RusticlImageFormat {
        cl_image_format: cl_image_format,
        req_for_full_read_or_write: req_for_full_read_or_write,
        req_for_embeded_read_or_write: req_for_embeded_read_or_write,
        req_for_full_read_and_write: req_for_full_read_and_write,
        req_for_3d_image_write_ext: req_for_3d_image_write_ext,
        pipe: pipe,
    }
}

pub const FORMATS: &[RusticlImageFormat] = &[
    rusticl_image_format(
        cl_image_format(CL_R, CL_HALF_FLOAT),
        false,
        false,
        true,
        false,
        pipe_format::PIPE_FORMAT_R16_FLOAT,
    ),
    rusticl_image_format(
        cl_image_format(CL_R, CL_FLOAT),
        false,
        false,
        true,
        false,
        pipe_format::PIPE_FORMAT_R32_FLOAT,
    ),
    rusticl_image_format(
        cl_image_format(CL_R, CL_SIGNED_INT8),
        false,
        false,
        true,
        false,
        pipe_format::PIPE_FORMAT_R8_SINT,
    ),
    rusticl_image_format(
        cl_image_format(CL_R, CL_SIGNED_INT16),
        false,
        false,
        true,
        false,
        pipe_format::PIPE_FORMAT_R16_SINT,
    ),
    rusticl_image_format(
        cl_image_format(CL_R, CL_SIGNED_INT32),
        false,
        false,
        true,
        false,
        pipe_format::PIPE_FORMAT_R32_SINT,
    ),
    rusticl_image_format(
        cl_image_format(CL_R, CL_UNORM_INT8),
        false,
        false,
        true,
        false,
        pipe_format::PIPE_FORMAT_R8_UNORM,
    ),
    rusticl_image_format(
        cl_image_format(CL_R, CL_UNORM_INT16),
        false,
        false,
        false,
        false,
        pipe_format::PIPE_FORMAT_R16_UNORM,
    ),
    rusticl_image_format(
        cl_image_format(CL_R, CL_UNSIGNED_INT8),
        false,
        false,
        true,
        false,
        pipe_format::PIPE_FORMAT_R8_UINT,
    ),
    rusticl_image_format(
        cl_image_format(CL_R, CL_UNSIGNED_INT16),
        false,
        false,
        true,
        false,
        pipe_format::PIPE_FORMAT_R16_UINT,
    ),
    rusticl_image_format(
        cl_image_format(CL_R, CL_UNSIGNED_INT32),
        false,
        false,
        true,
        false,
        pipe_format::PIPE_FORMAT_R32_UINT,
    ),
    rusticl_image_format(
        cl_image_format(CL_RGBA, CL_HALF_FLOAT),
        true,
        true,
        true,
        true,
        pipe_format::PIPE_FORMAT_R16G16B16A16_FLOAT,
    ),
    rusticl_image_format(
        cl_image_format(CL_RGBA, CL_FLOAT),
        true,
        true,
        true,
        true,
        pipe_format::PIPE_FORMAT_R32G32B32A32_FLOAT,
    ),
    rusticl_image_format(
        cl_image_format(CL_RGBA, CL_SIGNED_INT8),
        true,
        true,
        true,
        true,
        pipe_format::PIPE_FORMAT_R8G8B8A8_SINT,
    ),
    rusticl_image_format(
        cl_image_format(CL_RGBA, CL_SIGNED_INT16),
        true,
        true,
        true,
        true,
        pipe_format::PIPE_FORMAT_R16G16B16A16_SINT,
    ),
    rusticl_image_format(
        cl_image_format(CL_RGBA, CL_SIGNED_INT32),
        true,
        true,
        true,
        true,
        pipe_format::PIPE_FORMAT_R32G32B32A32_SINT,
    ),
    rusticl_image_format(
        cl_image_format(CL_RGBA, CL_UNORM_INT8),
        true,
        true,
        true,
        true,
        pipe_format::PIPE_FORMAT_R8G8B8A8_UNORM,
    ),
    rusticl_image_format(
        cl_image_format(CL_RGBA, CL_UNORM_INT16),
        true,
        true,
        false,
        true,
        pipe_format::PIPE_FORMAT_R16G16B16A16_UNORM,
    ),
    rusticl_image_format(
        cl_image_format(CL_RGBA, CL_UNSIGNED_INT8),
        true,
        true,
        true,
        true,
        pipe_format::PIPE_FORMAT_R8G8B8A8_UINT,
    ),
    rusticl_image_format(
        cl_image_format(CL_RGBA, CL_UNSIGNED_INT16),
        true,
        true,
        true,
        true,
        pipe_format::PIPE_FORMAT_R16G16B16A16_UINT,
    ),
    rusticl_image_format(
        cl_image_format(CL_RGBA, CL_UNSIGNED_INT32),
        true,
        true,
        true,
        true,
        pipe_format::PIPE_FORMAT_R32G32B32A32_UINT,
    ),
    rusticl_image_format(
        cl_image_format(CL_BGRA, CL_UNORM_INT8),
        true,
        false,
        false,
        true,
        pipe_format::PIPE_FORMAT_B8G8R8A8_UNORM,
    ),
];

pub trait CLFormatInfo {
    fn channels(&self) -> Option<u8>;
    fn format_info(&self) -> Option<(u8, bool)>;
    fn to_pipe_format(&self) -> Option<pipe_format>;

    fn channel_size(&self) -> Option<u8> {
        if let Some(packed) = self.is_packed() {
            assert!(!packed);
            self.format_info().map(|i| i.0)
        } else {
            None
        }
    }

    fn packed_size(&self) -> Option<u8> {
        if let Some(packed) = self.is_packed() {
            assert!(packed);
            self.format_info().map(|i| i.0)
        } else {
            None
        }
    }

    fn is_packed(&self) -> Option<bool> {
        self.format_info().map(|i| i.1)
    }

    fn pixel_size(&self) -> Option<u8> {
        if let Some(packed) = self.is_packed() {
            if packed {
                self.packed_size()
            } else {
                self.channels().zip(self.channel_size()).map(|(c, s)| c * s)
            }
        } else {
            None
        }
    }
}

impl CLFormatInfo for cl_image_format {
    #[allow(non_upper_case_globals)]
    fn channels(&self) -> Option<u8> {
        match self.image_channel_order {
            CL_R | CL_A | CL_DEPTH | CL_INTENSITY | CL_LUMINANCE => Some(1),
            CL_RG | CL_RA | CL_Rx => Some(2),
            CL_RGB | CL_RGx | CL_sRGB => Some(3),
            CL_RGBA | CL_ARGB | CL_BGRA | CL_ABGR | CL_RGBx | CL_sRGBA | CL_sBGRA | CL_sRGBx => {
                Some(4)
            }
            _ => None,
        }
    }

    fn format_info(&self) -> Option<(u8, bool)> {
        match self.image_channel_data_type {
            CL_SIGNED_INT8 | CL_UNSIGNED_INT8 | CL_SNORM_INT8 | CL_UNORM_INT8 => Some((1, false)),
            CL_SIGNED_INT16 | CL_UNSIGNED_INT16 | CL_SNORM_INT16 | CL_UNORM_INT16
            | CL_HALF_FLOAT => Some((2, false)),
            CL_SIGNED_INT32 | CL_UNSIGNED_INT32 | CL_FLOAT => Some((4, false)),
            CL_UNORM_SHORT_555 | CL_UNORM_SHORT_565 => Some((2, true)),
            CL_UNORM_INT_101010 | CL_UNORM_INT_101010_2 => Some((4, true)),
            _ => None,
        }
    }

    fn to_pipe_format(&self) -> Option<pipe_format> {
        FORMATS
            .iter()
            .find(|f| f.cl_image_format == *self)
            .map(|f| f.pipe)
    }
}
