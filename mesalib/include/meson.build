# Copyright © 2017 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

inc_drm_uapi = include_directories('drm-uapi')
inc_vulkan = include_directories('vulkan')
inc_d3d9 = include_directories('D3D9')
inc_gl_internal = include_directories('GL/internal')
inc_haikugl = include_directories('HaikuGL')

if with_gles1
  install_headers(
    'GLES/egl.h', 'GLES/gl.h', 'GLES/glext.h', 'GLES/glplatform.h',
    subdir : 'GLES',
  )
endif

if with_gles2
  install_headers(
    'GLES2/gl2.h', 'GLES2/gl2ext.h', 'GLES2/gl2platform.h',
    subdir : 'GLES2',
  )
  install_headers(
    'GLES3/gl3.h', 'GLES3/gl31.h', 'GLES3/gl32.h', 'GLES3/gl3ext.h',
    'GLES3/gl3platform.h',
    subdir : 'GLES3',
  )
endif

if with_gles1 or with_gles2 or with_egl
  install_headers('KHR/khrplatform.h', subdir : 'KHR')
endif

if with_opengl
  install_headers(
    'GL/gl.h', 'GL/glext.h', 'GL/glcorearb.h', 'GL/gl_mangle.h',
    subdir : 'GL',
  )
endif

if with_glx != 'disabled'
  install_headers('GL/glx.h', 'GL/glxext.h', 'GL/glx_mangle.h', subdir : 'GL')
endif

if with_osmesa != 'none'
  install_headers('GL/osmesa.h', subdir : 'GL')
endif

if with_egl
  install_headers(
    'EGL/eglext.h', 'EGL/egl.h', 'EGL/eglextchromium.h', 'EGL/eglmesaext.h',
    'EGL/eglplatform.h',
    subdir : 'EGL',
  )
endif

if with_dri
  install_headers('GL/internal/dri_interface.h', subdir : 'GL/internal')
endif

if with_gallium_st_nine
  install_headers(
    'd3dadapter/d3dadapter9.h', 'd3dadapter/drm.h', 'd3dadapter/present.h',
    subdir : 'd3dadapter',
  )
endif

if with_platform_haiku
  install_headers(
    'HaikuGL/GLRenderer.h', 'HaikuGL/GLView.h', 'HaikuGL/OpenGLKit.h',
    subdir : 'opengl',
  )
endif

# Only install the headers if we are building a stand alone implementation and
# not an ICD enabled implementation
if with_gallium_opencl and not with_opencl_icd
  install_headers(
    'CL/cl.h',
    'CL/cl.hpp',
    'CL/cl_d3d10.h',
    'CL/cl_d3d11.h',
    'CL/cl_dx9_media_sharing.h',
    'CL/cl_egl.h',
    'CL/cl_ext.h',
    'CL/cl_gl.h',
    'CL/cl_gl_ext.h',
    'CL/cl_platform.h',
    'CL/opencl.h',
    subdir: 'CL'
  )
endif
