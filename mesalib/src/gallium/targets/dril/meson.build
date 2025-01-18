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

dril_c_args = []
dril_ld_args = []
dril_link_depends = []
dril_drivers = []

dri_sym = configure_file(input : 'dri.sym.in', output : 'dri.sym', configuration : sym_config)

if with_ld_version_script
  dril_ld_args += ['-Wl,--version-script', join_paths(meson.current_build_dir(), 'dri.sym')]
  dril_link_depends += dri_sym
endif
if with_ld_dynamic_list
  dril_ld_args += ['-Wl,--dynamic-list', join_paths(meson.current_source_dir(), '../dri.dyn')]
  dril_link_depends += files('../dri.dyn')
endif

dril_dri = shared_library(
  'dril_dri',
  files('dril_target.c'),
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_util, inc_gallium_drivers,
    inc_gallium_winsys,
  ],
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_build_id, ld_args_gc_sections, dril_ld_args],
  link_depends : dril_link_depends,
  link_with : [
    libgallium,
    libgbm,
  ],
  dependencies : [
    idep_mesautil,
  ],
  # Will be deleted during installation, see install_megadrivers.py
  install : true,
  install_dir : dri_drivers_path,
  name_suffix : libname_suffix,
)

foreach d : [[with_gallium_kmsro, [
               'armada-drm_dri.so',
               'exynos_dri.so',
               'gm12u320_dri.so',
               'hdlcd_dri.so',
               'hx8357d_dri.so',
               'ili9163_dri.so',
               'ili9225_dri.so',
               'ili9341_dri.so',
               'ili9486_dri.so',
               'imx-drm_dri.so',
               'imx-dcss_dri.so',
               'imx-lcdif_dri.so',
               'ingenic-drm_dri.so',
               'kirin_dri.so',
               'komeda_dri.so',
               'mali-dp_dri.so',
               'mcde_dri.so',
               'mediatek_dri.so',
               'meson_dri.so',
               'mi0283qt_dri.so',
               'mxsfb-drm_dri.so',
               'panel-mipi-dbi_dri.so',
               'pl111_dri.so',
               'rcar-du_dri.so',
               'repaper_dri.so',
               'rockchip_dri.so',
               'rzg2l-du_dri.so',
               'ssd130x_dri.so',
               'st7586_dri.so',
               'st7735r_dri.so',
               'sti_dri.so',
               'stm_dri.so',
               'sun4i-drm_dri.so',
               'udl_dri.so',
               'vkms_dri.so',
               'zynqmp-dpsub_dri.so',
             ]],
             [with_gallium_radeonsi, 'radeonsi_dri.so'],
             [with_gallium_nouveau, 'nouveau_dri.so'],
             [with_gallium_freedreno, ['msm_dri.so', 'kgsl_dri.so']],
             [with_gallium_swrast, 'swrast_dri.so'],
             [with_gallium_swrast and with_gallium_drisw_kms, 'kms_swrast_dri.so'],
             [with_gallium_v3d, 'v3d_dri.so'],
             [with_gallium_vc4, 'vc4_dri.so'],
             [with_gallium_panfrost, ['panfrost_dri.so', 'panthor_dri.so']],
             [with_gallium_etnaviv, 'etnaviv_dri.so'],
             [with_gallium_tegra, 'tegra_dri.so'],
             [with_gallium_crocus, 'crocus_dri.so'],
             [with_gallium_iris, 'iris_dri.so'],
             [with_gallium_i915, 'i915_dri.so'],
             [with_gallium_r300, 'r300_dri.so'],
             [with_gallium_r600, 'r600_dri.so'],
             [with_gallium_svga, 'vmwgfx_dri.so'],
             [with_gallium_virgl or
               (with_gallium_freedreno and freedreno_kmds.contains('virtio')) or
               (with_gallium_radeonsi and with_amdgpu_virtio),
               'virtio_gpu_dri.so'],
             [with_gallium_lima, 'lima_dri.so'],
             [with_gallium_d3d12, 'd3d12_dri.so'],
             [with_gallium_zink, 'zink_dri.so'],
             [with_gallium_asahi, 'asahi_dri.so']]
  if d[0]
    dril_drivers += d[1]
  endif
endforeach

# This only works on Unix-like oses, which is probably fine for dri
prog_ln = find_program('ln', required : false)
if prog_ln.found()
  foreach d : dril_drivers
    custom_target(
      d,
      output : d,
      command : [prog_ln, '-sf', 'libdril_dri.so', '@OUTPUT@'],
      build_by_default : true,
    )
  endforeach
endif

if dril_drivers.length() > 0
  meson.add_install_script(
    install_megadrivers,
    dril_dri.full_path(),
    dri_drivers_path,
    dril_drivers,
    install_tag : 'runtime',
  )
endif
