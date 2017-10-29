# Copyright © 2017 Dylan Baker

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
subdir('drivers/radeon')
subdir('drivers/radeonsi')
subdir('drivers/nouveau')
subdir('drivers/freedreno')
subdir('drivers/softpipe')
if with_gallium_vc4
  subdir('drivers/vc4')
endif
if with_gallium_vc5
  subdir('drivers/vc5')
endif
if with_gallium_etnaviv
  subdir('drivers/etnaviv')
endif
subdir('drivers/llvmpipe')
subdir('winsys/sw/null')
subdir('winsys/sw/dri')
subdir('winsys/sw/kms-dri')
subdir('winsys/sw/wrapper')
if with_gallium_vc4
  subdir('winsys/pl111/drm')
endif
subdir('winsys/radeon/drm')
subdir('winsys/amdgpu/drm')
subdir('winsys/nouveau/drm')
subdir('winsys/freedreno/drm')
if with_gallium_vc4
  subdir('winsys/vc4/drm')
endif
if with_gallium_vc5
  subdir('winsys/vc5/drm')
endif
if with_gallium_etnaviv
  subdir('winsys/etnaviv/drm')
endif
if with_gallium_imx
  subdir('winsys/imx/drm')
endif
subdir('state_trackers/dri')
if with_osmesa == 'gallium'
  subdir('state_trackers/osmesa')
endif
# TODO: i915
# TODO: SVGA
# TODO: r300
# TODO: r600
# TODO: SWR
# TODO: virgl
# TODO: winsys/sw/xlib
# TODO: clover
if with_dri and with_gallium
  subdir('targets/dri')
endif
if with_osmesa == 'gallium'
  subdir('targets/osmesa')
endif
# TODO: xlib-glx
# TODO: OMX
# TODO: VA
# TODO: vdpau
# TODO: xa
# TODO: xvmc
# TODO: nine
# TODO: tests
