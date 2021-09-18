# Copyright © 2017 Dylan Baker
# Copyright © 2018 Intel Corporation

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

# TODO: support non-static targets
# Static targets are always enabled in autotools (unless you modify
# configure.ac)

gallium_dri_c_args = []
gallium_dri_ld_args = []
gallium_dri_link_depends = []
gallium_dri_drivers = []

if with_ld_version_script
  gallium_dri_ld_args += ['-Wl,--version-script', join_paths(meson.current_source_dir(), 'dri.sym')]
  gallium_dri_link_depends += files('dri.sym')
endif
if with_ld_dynamic_list
  gallium_dri_ld_args += ['-Wl,--dynamic-list', join_paths(meson.current_source_dir(), '../dri-vdpau.dyn')]
  gallium_dri_link_depends += files('../dri-vdpau.dyn')
endif

libgallium_dri = shared_library(
  'gallium_dri',
  files('target.c'),
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_util, inc_dri_common, inc_gallium_drivers,
    inc_gallium_winsys, include_directories('../../frontends/dri'),
  ],
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_build_id, ld_args_gc_sections, gallium_dri_ld_args],
  link_depends : gallium_dri_link_depends,
  link_with : [
    libdri, libmesa_gallium, libdricommon, libmegadriver_stub, libgalliumvl,
    libgallium, libglapi, libpipe_loader_static, libws_null, libwsw, libswdri,
    libswkmsdri,
  ],
  dependencies : [
    dep_selinux, dep_libdrm, dep_llvm, dep_thread, idep_xmlconfig, idep_mesautil,
    driver_swrast, driver_r300, driver_r600, driver_radeonsi, driver_nouveau,
    driver_kmsro, driver_v3d, driver_vc4, driver_freedreno, driver_etnaviv,
    driver_tegra, driver_i915, driver_svga, driver_virgl,
    driver_swr, driver_panfrost, driver_iris, driver_lima, driver_zink, driver_d3d12,
    driver_asahi, driver_crocus
  ],
  # Will be deleted during installation, see install_megadrivers.py
  install : true,
  install_dir : dri_drivers_path,
  name_suffix : 'so',
)

foreach d : [[with_gallium_kmsro, [
               'armada-drm_dri.so',
               'exynos_dri.so',
               'hx8357d_dri.so',
               'ili9225_dri.so',
               'ili9341_dri.so',
               'imx-drm_dri.so',
               'imx-dcss_dri.so',
               'ingenic-drm_dri.so',
               'kirin_dri.so',
               'mali-dp_dri.so',
               'mcde_dri.so',
               'mediatek_dri.so',
               'meson_dri.so',
               'mi0283qt_dri.so',
               'mxsfb-drm_dri.so',
               'pl111_dri.so',
               'repaper_dri.so',
               'rockchip_dri.so',
               'st7586_dri.so',
               'st7735r_dri.so',
               'stm_dri.so',
	       'sun4i-drm_dri.so',
             ]],
             [with_gallium_radeonsi, 'radeonsi_dri.so'],
             [with_gallium_nouveau, 'nouveau_dri.so'],
             [with_gallium_freedreno, ['msm_dri.so', 'kgsl_dri.so']],
             [with_gallium_softpipe or with_gallium_swr, 'swrast_dri.so'],
             [with_gallium_softpipe and with_gallium_drisw_kms, 'kms_swrast_dri.so'],
             [with_gallium_v3d, 'v3d_dri.so'],
             [with_gallium_vc4, 'vc4_dri.so'],
             [with_gallium_panfrost, 'panfrost_dri.so'],
             [with_gallium_etnaviv, 'etnaviv_dri.so'],
             [with_gallium_tegra, 'tegra_dri.so'],
             [with_gallium_crocus, 'crocus_dri.so'],
             [with_gallium_iris, 'iris_dri.so'],
             [with_gallium_i915, 'i915_dri.so'],
             [with_gallium_r300, 'r300_dri.so'],
             [with_gallium_r600, 'r600_dri.so'],
             [with_gallium_svga, 'vmwgfx_dri.so'],
             [with_gallium_virgl, 'virtio_gpu_dri.so'],
             [with_gallium_lima, 'lima_dri.so'],
             [with_gallium_zink, 'zink_dri.so'],
             [with_gallium_d3d12, 'd3d12_dri.so'],
             [with_gallium_asahi, 'asahi_dri.so']]
  if d[0]
    gallium_dri_drivers += d[1]
  endif
endforeach

meson.add_install_script(
  install_megadrivers_py.path(),
  libgallium_dri.full_path(),
  dri_drivers_path,
  gallium_dri_drivers,
)
