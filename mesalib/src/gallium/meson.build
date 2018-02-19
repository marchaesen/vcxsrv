# Copyright © 2017 Dylan Baker
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

inc_gallium_drivers = include_directories('drivers')
inc_gallium_winsys = include_directories('winsys')

subdir('auxiliary')
subdir('auxiliary/pipe-loader')
subdir('drivers/ddebug')
subdir('drivers/noop')
subdir('drivers/trace')
subdir('drivers/rbug')
subdir('winsys/sw/null')
if with_dri
  subdir('winsys/sw/dri')
else
  libswdri = []
endif
if with_gallium_drisw_kms
  subdir('winsys/sw/kms-dri')
else
  libswkmsdri = []
endif
subdir('winsys/sw/wrapper')
if with_platform_haiku
  subdir('winsys/sw/hgl')
endif
if with_gallium_swr
  if meson.version().version_compare('< 0.44.0')
    error('SWR requires meson 0.44.0 or greater.')
  endif
  subdir('drivers/swr')
else
  driver_swr = declare_dependency()
endif
if with_gallium_softpipe
  subdir('drivers/softpipe')
  if with_llvm
    subdir('drivers/llvmpipe')
  endif
else
  driver_swrast = declare_dependency()
endif
if with_gallium_r300 or with_gallium_radeonsi or with_gallium_r600
  subdir('winsys/radeon/drm')
endif
if with_gallium_r300
  subdir('drivers/r300')
else
  driver_r300 = declare_dependency()
endif
if with_gallium_r600
  subdir('drivers/r600')
else
  driver_r600 = declare_dependency()
endif
if with_gallium_radeonsi
  subdir('winsys/amdgpu/drm')
  subdir('drivers/radeon')
  subdir('drivers/radeonsi')
else
  driver_radeonsi = declare_dependency()
endif
if with_gallium_nouveau
  subdir('winsys/nouveau/drm')
  subdir('drivers/nouveau')
else
  driver_nouveau = declare_dependency()
endif
if with_gallium_freedreno
  subdir('winsys/freedreno/drm')
  subdir('drivers/freedreno')
else
  driver_freedreno = declare_dependency()
endif
if with_gallium_vc4
  subdir('winsys/vc4/drm')
  subdir('drivers/vc4')
else
  driver_vc4 = declare_dependency()
endif
if with_gallium_pl111
  subdir('winsys/pl111/drm')
else
  driver_pl111 = declare_dependency()
endif
if with_gallium_vc5
  subdir('winsys/vc5/drm')
  subdir('drivers/vc5')
else
  driver_vc5 = declare_dependency()
endif
if with_gallium_etnaviv
  subdir('winsys/etnaviv/drm')
  subdir('drivers/etnaviv')
else
  driver_etnaviv = declare_dependency()
endif
if with_gallium_imx
  subdir('winsys/imx/drm')
else
  driver_imx = declare_dependency()
endif
if with_gallium_i915
  subdir('winsys/i915/drm')
  subdir('drivers/i915')
else
  driver_i915 = declare_dependency()
endif
if with_gallium_svga
  subdir('winsys/svga/drm')
  subdir('drivers/svga')
else
  driver_svga = declare_dependency()
endif
if with_gallium_virgl
  subdir('winsys/virgl/drm')
  subdir('winsys/virgl/vtest')
  subdir('drivers/virgl')
else
  driver_virgl = declare_dependency()
endif
if with_gallium_vdpau
  subdir('state_trackers/vdpau')
endif
if with_gallium_xvmc
  subdir('state_trackers/xvmc')
endif
if with_gallium_omx
  subdir('state_trackers/omx_bellagio')
endif
if with_gallium_va
  subdir('state_trackers/va')
endif
if with_gallium_xa
  subdir('state_trackers/xa')
endif
if with_gallium_st_nine
  subdir('state_trackers/nine')
endif
if with_platform_haiku
  subdir('state_trackers/hgl')
endif
if with_gallium_opencl
  # TODO: this isn't really clover specific, but ATM clover is the only
  # consumer
  subdir('targets/pipe-loader')

  if meson.version().version_compare('< 0.44.0')
    error('OpenCL requires meson 0.44.0 or greater.')
  endif
  subdir('state_trackers/clover')
  subdir('targets/opencl')
endif
if with_dri
  subdir('state_trackers/dri')
  subdir('targets/dri')
endif
if with_osmesa == 'gallium'
  subdir('state_trackers/osmesa')
  subdir('targets/osmesa')
endif
if with_glx == 'gallium-xlib'
  subdir('winsys/sw/xlib')
  subdir('state_trackers/glx/xlib')
  subdir('targets/libgl-xlib')
endif
if with_gallium_vdpau
  subdir('targets/vdpau')
endif
if with_gallium_xvmc
  subdir('targets/xvmc')
endif
if with_gallium_omx
  subdir('targets/omx-bellagio')
endif
if with_gallium_va
  subdir('targets/va')
endif
if with_gallium_xa
  subdir('targets/xa')
endif
if with_platform_haiku
  subdir('targets/haiku-softpipe')
endif
if with_gallium_st_nine
  subdir('targets/d3dadapter9')
endif
# TODO: tests
