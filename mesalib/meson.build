# Copyright © 2017-2020 Intel Corporation

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

project(
  'mesa',
  ['c', 'cpp'],
  version : run_command(
    [find_program('python3', 'python'), 'bin/meson_get_version.py'],
    check : true
  ).stdout(),
  license : 'MIT',
  meson_version : '>= 0.52',
  default_options : ['buildtype=debugoptimized', 'b_ndebug=if-release', 'c_std=c11', 'cpp_std=c++14']
)

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

null_dep = dependency('', required : false)

if get_option('layout') != 'mirror'
  error('`mirror` is the only build directory layout supported')
endif

# Arguments for the preprocessor, put these in a separate array from the C and
# C++ (cpp in meson terminology) arguments since they need to be added to the
# default arguments for both C and C++.
pre_args = [
  '-D__STDC_CONSTANT_MACROS',
  '-D__STDC_FORMAT_MACROS',
  '-D__STDC_LIMIT_MACROS',
  '-DPACKAGE_VERSION="@0@"'.format(meson.project_version()),
  '-DPACKAGE_BUGREPORT="https://gitlab.freedesktop.org/mesa/mesa/-/issues"',
]
c_args = []
cpp_args = []

with_moltenvk_dir = get_option('moltenvk-dir')
with_vulkan_icd_dir = get_option('vulkan-icd-dir')
with_tests = get_option('build-tests')
with_aco_tests = get_option('build-aco-tests')
with_glx_read_only_text = get_option('glx-read-only-text')
with_glx_direct = get_option('glx-direct')
with_osmesa = get_option('osmesa')
with_swr_arches = get_option('swr-arches')
with_vulkan_overlay_layer = get_option('vulkan-layers').contains('overlay')
with_vulkan_device_select_layer = get_option('vulkan-layers').contains('device-select')
with_tools = get_option('tools')
if with_tools.contains('all')
  with_tools = [
    'drm-shim',
    'etnaviv',
    'freedreno',
    'glsl',
    'intel',
    'intel-ui',
    'lima',
    'nir',
    'nouveau',
    'xvmc',
    'asahi',
  ]
endif
with_clc = false

with_any_vulkan_layers = get_option('vulkan-layers').length() != 0
with_intel_tools = with_tools.contains('intel') or with_tools.contains('intel-ui')
with_imgui = with_intel_tools or with_vulkan_overlay_layer

dri_drivers_path = get_option('dri-drivers-path')
if dri_drivers_path == ''
  dri_drivers_path = join_paths(get_option('prefix'), get_option('libdir'), 'dri')
endif
dri_search_path = get_option('dri-search-path')
if dri_search_path == ''
  dri_search_path = dri_drivers_path
endif

gbm_backends_path = get_option('gbm-backends-path')
if gbm_backends_path == ''
  gbm_backends_path = join_paths(get_option('prefix'), get_option('libdir'), 'gbm')
endif

with_gles1 = get_option('gles1')
if with_gles1 == 'true'
  with_gles1 = 'enabled'
  warning('gles1 option "true" deprecated, please use "enabled" instead.')
elif with_gles1 == 'false'
  with_gles1 = 'disabled'
  warning('gles1 option "false" deprecated, please use "disabled" instead.')
endif
with_gles2 = get_option('gles2')
if with_gles2 == 'true'
  with_gles2 = 'enabled'
  warning('gles2 option "true" deprecated, please use "enabled" instead.')
elif with_gles2 == 'false'
  with_gles2 = 'disabled'
  warning('gles2 option "false" deprecated, please use "disabled" instead.')
endif
if host_machine.system() == 'windows'
  if with_gles1 == 'auto'
    with_gles1 = 'disabled'
  endif
  if with_gles2 == 'auto'
    with_gles2 = 'disabled'
  endif
endif
with_opengl = get_option('opengl')

# Default shared glapi off for windows, on elsewhere.
_sg = get_option('shared-glapi')
if _sg == 'true'
  _sg = 'enabled'
  warning('shared-glapi option "true" deprecated, please use "enabled" instead.')
elif _sg == 'false'
  _sg = 'disabled'
  warning('shared-glapi option "false" deprecated, please use "disabled" instead.')
endif
if _sg == 'auto'
  with_shared_glapi = host_machine.system() != 'windows'
else
  with_shared_glapi = _sg == 'enabled'
endif

# shared-glapi is required if at least two OpenGL APIs are being built
if not with_shared_glapi
  if ((with_gles1 == 'enabled' and with_gles2 == 'enabled') or
      (with_gles1 == 'enabled' and with_opengl) or
      (with_gles2 == 'enabled' and with_opengl))
    error('shared-glapi required for building two or more of OpenGL, OpenGL ES 1.x, OpenGL ES 2.x')
  endif
  with_gles1 = 'disabled'
  with_gles2 = 'disabled'
endif

# We require OpenGL for OpenGL ES
if not with_opengl
  if (with_gles1 == 'enabled' or with_gles2 == 'enabled') and not with_opengl
    error('building OpenGL ES without OpenGL is not supported.')
  endif
  with_gles1 = 'disabled'
  with_gles2 = 'disabled'
endif

with_gles1 = with_gles1 != 'disabled'
with_gles2 = with_gles2 != 'disabled'
with_any_opengl = with_opengl or with_gles1 or with_gles2
# Only build shared_glapi if at least one OpenGL API is enabled
with_shared_glapi = with_shared_glapi and with_any_opengl

system_has_kms_drm = ['openbsd', 'netbsd', 'freebsd', 'gnu/kfreebsd', 'dragonfly', 'linux', 'sunos'].contains(host_machine.system())

dri_drivers = get_option('dri-drivers')
if dri_drivers.contains('auto')
  if system_has_kms_drm
    # TODO: PPC, Sparc
    if ['x86', 'x86_64'].contains(host_machine.cpu_family())
      dri_drivers = ['i915', 'i965', 'r100', 'r200', 'nouveau']
    elif ['arm', 'aarch64'].contains(host_machine.cpu_family())
      dri_drivers = []
    elif ['mips', 'mips64', 'riscv32', 'riscv64'].contains(host_machine.cpu_family())
      dri_drivers = ['r100', 'r200', 'nouveau']
    else
      error('Unknown architecture @0@. Please pass -Ddri-drivers to set driver options. Patches gladly accepted to fix this.'.format(
            host_machine.cpu_family()))
    endif
  elif ['darwin', 'windows', 'cygwin', 'haiku'].contains(host_machine.system())
    # only swrast would make sense here, but gallium swrast is a much better default
    dri_drivers = []
  else
    error('Unknown OS @0@. Please pass -Ddri-drivers to set driver options. Patches gladly accepted to fix this.'.format(
          host_machine.system()))
  endif
endif

with_dri_i915 = dri_drivers.contains('i915')
with_dri_i965 = dri_drivers.contains('i965')
with_dri_r100 = dri_drivers.contains('r100')
with_dri_r200 = dri_drivers.contains('r200')
with_dri_nouveau = dri_drivers.contains('nouveau')

with_dri = dri_drivers.length() != 0

gallium_drivers = get_option('gallium-drivers')
if gallium_drivers.contains('auto')
  if system_has_kms_drm
    # TODO: PPC, Sparc
    if ['x86', 'x86_64'].contains(host_machine.cpu_family())
      gallium_drivers = [
        'r300', 'r600', 'radeonsi', 'nouveau', 'virgl', 'svga', 'swrast',
        'iris', 'crocus'
      ]
    elif ['arm', 'aarch64'].contains(host_machine.cpu_family())
      gallium_drivers = [
        'v3d', 'vc4', 'freedreno', 'etnaviv', 'nouveau',
        'tegra', 'virgl', 'lima', 'panfrost', 'swrast'
      ]
    elif ['mips', 'mips64', 'riscv32', 'riscv64'].contains(host_machine.cpu_family())
      gallium_drivers = [
        'r300', 'r600', 'radeonsi', 'nouveau', 'virgl', 'swrast'
      ]
    else
      error('Unknown architecture @0@. Please pass -Dgallium-drivers to set driver options. Patches gladly accepted to fix this.'.format(
            host_machine.cpu_family()))
    endif
  elif ['darwin', 'windows', 'cygwin', 'haiku'].contains(host_machine.system())
    gallium_drivers = ['swrast']
  else
    error('Unknown OS @0@. Please pass -Dgallium-drivers to set driver options. Patches gladly accepted to fix this.'.format(
          host_machine.system()))
  endif
endif
with_gallium_radeonsi = gallium_drivers.contains('radeonsi')
with_gallium_r300 = gallium_drivers.contains('r300')
with_gallium_r600 = gallium_drivers.contains('r600')
with_gallium_nouveau = gallium_drivers.contains('nouveau')
with_gallium_freedreno = gallium_drivers.contains('freedreno')
with_gallium_softpipe = gallium_drivers.contains('swrast')
with_gallium_vc4 = gallium_drivers.contains('vc4')
with_gallium_v3d = gallium_drivers.contains('v3d')
with_gallium_panfrost = gallium_drivers.contains('panfrost')
with_gallium_etnaviv = gallium_drivers.contains('etnaviv')
with_gallium_tegra = gallium_drivers.contains('tegra')
with_gallium_crocus = gallium_drivers.contains('crocus')
with_gallium_iris = gallium_drivers.contains('iris')
with_gallium_i915 = gallium_drivers.contains('i915')
with_gallium_svga = gallium_drivers.contains('svga')
with_gallium_virgl = gallium_drivers.contains('virgl')
with_gallium_swr = gallium_drivers.contains('swr')
with_gallium_lima = gallium_drivers.contains('lima')
with_gallium_zink = gallium_drivers.contains('zink')
with_gallium_d3d12 = gallium_drivers.contains('d3d12')
with_gallium_asahi = gallium_drivers.contains('asahi')

with_gallium = gallium_drivers.length() != 0
with_gallium_kmsro = with_gallium_v3d or with_gallium_vc4 or with_gallium_etnaviv or with_gallium_panfrost or with_gallium_lima or with_gallium_freedreno

if with_gallium and system_has_kms_drm
  _glx = get_option('glx')
  _egl = get_option('egl')
  if _glx == 'dri' or _egl == 'enabled' or (_glx == 'disabled' and _egl != 'disabled')
    with_dri = true
  endif
endif

_vulkan_drivers = get_option('vulkan-drivers')
if _vulkan_drivers.contains('auto')
  if system_has_kms_drm
    if host_machine.cpu_family().startswith('x86')
      _vulkan_drivers = ['amd', 'intel', 'swrast']
    elif ['arm', 'aarch64'].contains(host_machine.cpu_family())
      _vulkan_drivers = ['swrast']
    elif ['mips', 'mips64', 'riscv32', 'riscv64'].contains(host_machine.cpu_family())
      _vulkan_drivers = ['amd', 'swrast']
    else
      error('Unknown architecture @0@. Please pass -Dvulkan-drivers to set driver options. Patches gladly accepted to fix this.'.format(
            host_machine.cpu_family()))
    endif
  elif ['darwin', 'windows', 'cygwin', 'haiku'].contains(host_machine.system())
    # No vulkan driver supports windows or macOS currently
    _vulkan_drivers = []
  else
    error('Unknown OS @0@. Please pass -Dvulkan-drivers to set driver options. Patches gladly accepted to fix this.'.format(
          host_machine.system()))
  endif
endif

with_intel_vk = _vulkan_drivers.contains('intel')
with_amd_vk = _vulkan_drivers.contains('amd')
with_freedreno_vk = _vulkan_drivers.contains('freedreno')
with_panfrost_vk = _vulkan_drivers.contains('panfrost')
with_swrast_vk = _vulkan_drivers.contains('swrast')
with_virtio_vk = _vulkan_drivers.contains('virtio-experimental')
with_freedreno_kgsl = get_option('freedreno-kgsl')
with_broadcom_vk = _vulkan_drivers.contains('broadcom')
with_any_vk = _vulkan_drivers.length() != 0

with_any_broadcom = with_gallium_vc4 or with_gallium_v3d or with_broadcom_vk
with_any_intel = with_dri_i965 or with_intel_vk or with_gallium_iris or with_gallium_crocus

if with_swrast_vk and not with_gallium_softpipe
  error('swrast vulkan requires gallium swrast')
endif
if with_dri_i915 and with_gallium_i915
  error('Only one i915 provider can be built')
endif
if with_gallium_tegra and not with_gallium_nouveau
  error('tegra driver requires nouveau driver')
endif
if with_aco_tests and not with_amd_vk
  error('ACO tests require Radv')
endif

dep_clang = dependency(
  'clang',
  method : 'cmake',
  static : true,
  modules : [
    'clangBasic', 'clangCodeGen', 'clangDriver', 'clangFrontend', 'clangFrontendTool',
    'clangHandleCXX', 'clangHandleLLVM',
  ],
  required : get_option('microsoft-clc'),
)
with_microsoft_clc = dep_clang.found()
with_clc = dep_clang.found()

with_spirv_to_dxil = get_option('spirv-to-dxil')

if host_machine.system() == 'darwin'
  with_dri_platform = 'apple'
  pre_args += '-DBUILDING_MESA'
elif ['windows', 'cygwin'].contains(host_machine.system())
  with_dri_platform = 'windows'
elif system_has_kms_drm
  with_dri_platform = 'drm'
else
  # FIXME: haiku doesn't use dri, and xlib doesn't use dri, probably should
  # assert here that one of those cases has been met.
  # FIXME: illumos ends up here as well
  with_dri_platform = 'none'
endif

_platforms = get_option('platforms')
if _platforms.contains('auto')
  if system_has_kms_drm
    _platforms = ['x11', 'wayland']
  elif ['darwin', 'cygwin'].contains(host_machine.system())
    _platforms = ['x11']
  elif ['haiku'].contains(host_machine.system())
    _platforms = ['haiku']
  elif host_machine.system() == 'windows'
    _platforms = ['windows']
  else
    error('Unknown OS @0@. Please pass -Dplatforms to set platforms. Patches gladly accepted to fix this.'.format(
          host_machine.system()))
  endif
endif

with_platform_android = _platforms.contains('android')
with_platform_x11 = _platforms.contains('x11')
with_platform_wayland = _platforms.contains('wayland')
with_platform_haiku = _platforms.contains('haiku')
with_platform_windows = _platforms.contains('windows')

with_glx = get_option('glx')
if with_glx == 'auto'
  if with_platform_android
    with_glx = 'disabled'
  elif with_dri
    with_glx = 'dri'
  elif with_platform_haiku
    with_glx = 'disabled'
  elif host_machine.system() == 'windows'
    with_glx = 'disabled'
  elif with_gallium
    # Even when building just gallium drivers the user probably wants dri
    with_glx = 'dri'
  elif with_platform_x11 and with_any_opengl and not with_any_vk
    # The automatic behavior should not be to turn on xlib based glx when
    # building only vulkan drivers
    with_glx = 'xlib'
  else
    with_glx = 'disabled'
  endif
endif
if with_glx == 'dri'
   if with_gallium
      with_dri = true
   endif
endif

if not (with_dri or with_gallium or with_glx != 'disabled')
  with_gles1 = false
  with_gles2 = false
  with_opengl = false
  with_any_opengl = false
  with_shared_glapi = false
endif

_gbm = get_option('gbm')
if _gbm == 'true'
  _gbm = 'enabled'
  warning('gbm option "true" deprecated, please use "enabled" instead.')
elif _gbm == 'false'
  _gbm = 'disabled'
  warning('gbm option "false" deprecated, please use "disabled" instead.')
endif
if _gbm == 'auto'
  with_gbm = system_has_kms_drm and with_dri
else
  with_gbm = _gbm == 'enabled'
endif
if with_gbm and not system_has_kms_drm
  error('GBM only supports DRM/KMS platforms')
endif

_xlib_lease = get_option('xlib-lease')
if _xlib_lease == 'true'
  _xlib_lease = 'enabled'
  warning('xlib_lease option "true" deprecated, please use "enabled" instead.')
elif _xlib_lease == 'false'
  _xlib_lease = 'disabled'
  warning('xlib_lease option "false" deprecated, please use "disabled" instead.')
endif
if _xlib_lease == 'auto'
  with_xlib_lease = with_platform_x11 and system_has_kms_drm
else
  with_xlib_lease = _xlib_lease == 'enabled'
endif

_egl = get_option('egl')
if _egl == 'true'
  _egl = 'enabled'
  warning('egl option "true" deprecated, please use "enabled" instead.')
elif _egl == 'false'
  _egl = 'disabled'
  warning('egl option "false" deprecated, please use "disabled" instead.')
endif
if _egl == 'auto'
  with_egl = (
    host_machine.system() != 'darwin' and
    (with_platform_windows or with_dri) and
    with_shared_glapi
  )
elif _egl == 'enabled'
  if not with_dri and not with_platform_haiku and not with_platform_windows
    error('EGL requires dri, haiku, or windows')
  elif not with_shared_glapi
    error('EGL requires shared-glapi')
  elif not ['disabled', 'dri'].contains(with_glx)
    error('EGL requires dri, but a GLX is being built without dri')
  elif host_machine.system() == 'darwin'
    error('EGL is not available on MacOS')
  endif
  with_egl = true
else
  with_egl = false
endif

if with_egl
  _platforms += 'surfaceless'
  if with_gbm and not with_platform_android
    _platforms += 'drm'
  endif
endif

egl_native_platform = get_option('egl-native-platform')
if egl_native_platform.contains('auto')
  if _platforms.length() != 0
    egl_native_platform = _platforms[0]
  else
    egl_native_platform = 'surfaceless'
  endif
endif

if with_egl and not _platforms.contains(egl_native_platform)
  error('-Degl-native-platform does not specify an enabled platform')
endif

# Android uses emutls for versions <= P/28. For USE_ELF_TLS we need ELF TLS.
use_elf_tls = false
if (not ['freebsd', 'openbsd', 'haiku'].contains(host_machine.system()) and
    (not with_platform_android or get_option('platform-sdk-version') >= 29) and
    (not with_platform_windows or not with_shared_glapi))
  pre_args += '-DUSE_ELF_TLS'
  use_elf_tls = true

  if with_platform_android
    # By default the NDK compiler, at least, emits emutls references instead of
    # ELF TLS, even when building targeting newer API levels.  Make it actually do
    # ELF TLS instead.
    c_args += '-fno-emulated-tls'
    cpp_args += '-fno-emulated-tls'
  endif
endif

if with_glx != 'disabled'
  if not (with_platform_x11 and with_any_opengl)
    error('Cannot build GLX support without X11 platform support and at least one OpenGL API')
  elif with_glx == 'gallium-xlib'
    if not with_gallium
      error('Gallium-xlib based GLX requires at least one gallium driver')
    elif not with_gallium_softpipe
      error('Gallium-xlib based GLX requires softpipe or llvmpipe.')
    elif with_dri
      error('gallium-xlib conflicts with any dri driver')
    endif
  elif with_glx == 'xlib'
    if with_dri
      error('xlib conflicts with any dri driver')
    endif
  elif with_glx == 'dri'
    if not with_shared_glapi
      error('dri based GLX requires shared-glapi')
    endif
  endif
endif

with_glvnd = get_option('glvnd')
glvnd_vendor_name = get_option('glvnd-vendor-name')
if with_glvnd
  if with_platform_windows
    error('glvnd cannot be used on Windows')
  elif with_glx == 'xlib' or with_glx == 'gallium-xlib'
    error('Cannot build glvnd support for GLX that is not DRI based.')
  elif with_glx == 'disabled' and not with_egl
    error('glvnd requires DRI based GLX and/or EGL')
  endif
  if get_option('egl-lib-suffix') != ''
    error('''EGL lib suffix can't be used with libglvnd''')
  endif
endif

if with_vulkan_icd_dir == ''
  with_vulkan_icd_dir = join_paths(get_option('datadir'), 'vulkan/icd.d')
endif

# GNU/Hurd includes egl_dri2, without drm.
with_dri2 = (with_dri or with_any_vk) and (with_dri_platform == 'drm' or
  host_machine.system() == 'gnu')
_dri3 = get_option('dri3')
if _dri3 == 'true'
  _dri3 = 'enabled'
  warning('dri3 option "true" deprecated, please use "enabled" instead.')
elif _dri3 == 'false'
  _dri3 = 'disabled'
  warning('dri3 option "false" deprecated, please use "disabled" instead.')
endif
if _dri3 == 'auto'
  with_dri3 = system_has_kms_drm and with_dri2
else
  with_dri3 = _dri3 == 'enabled'
endif

if with_any_vk and (with_platform_x11 and not with_dri3)
  error('Vulkan drivers require dri3 for X11 support')
endif
if with_dri
  if with_glx == 'disabled' and not with_egl and not with_gbm
    error('building dri drivers require at least one windowing system')
  endif
endif

if with_gallium_kmsro and (with_platform_x11 and not with_dri3)
  error('kmsro requires dri3 for X11 support')
endif

_vdpau = get_option('gallium-vdpau')
if _vdpau == 'true'
  _vdpau = 'enabled'
  warning('gallium-vdpau option "true" deprecated, please use "enabled" instead.')
elif _vdpau == 'false'
  _vdpau = 'disabled'
  warning('gallium-vdpau option "false" deprecated, please use "disabled" instead.')
endif
if not system_has_kms_drm
  if _vdpau == 'enabled'
    error('VDPAU state tracker can only be build on unix-like OSes.')
  else
    _vdpau = 'disabled'
  endif
elif not with_platform_x11
  if _vdpau == 'enabled'
    error('VDPAU state tracker requires X11 support.')
  else
    _vdpau = 'disabled'
  endif
elif not (with_gallium_r300 or with_gallium_r600 or with_gallium_radeonsi or
          with_gallium_nouveau)
  if _vdpau == 'enabled'
    error('VDPAU state tracker requires at least one of the following gallium drivers: r300, r600, radeonsi, nouveau.')
  else
    _vdpau = 'disabled'
  endif
endif
dep_vdpau = null_dep
with_gallium_vdpau = false
if _vdpau != 'disabled'
  dep_vdpau = dependency('vdpau', version : '>= 1.1', required : _vdpau == 'enabled')
  if dep_vdpau.found()
    dep_vdpau = dep_vdpau.partial_dependency(compile_args : true)
    with_gallium_vdpau = true
  endif
endif

if with_gallium_vdpau
  pre_args += '-DHAVE_ST_VDPAU'
endif
vdpau_drivers_path = get_option('vdpau-libs-path')
if vdpau_drivers_path == ''
  vdpau_drivers_path = join_paths(get_option('libdir'), 'vdpau')
endif

if with_gallium_zink
  dep_vulkan = dependency('vulkan')
endif

dep_dxheaders = null_dep
if with_gallium_d3d12 or with_microsoft_clc
  dep_dxheaders = dependency('DirectX-Headers', fallback : ['DirectX-Headers', 'dep_dxheaders'],
    required : with_gallium_d3d12
  ) 
endif

if with_vulkan_overlay_layer or with_aco_tests
  prog_glslang = find_program('glslangValidator')
endif

_xvmc = get_option('gallium-xvmc')
if _xvmc == 'true'
  _xvmc = 'enabled'
  warning('gallium-xvmc option "true" deprecated, please use "enabled" instead.')
elif _xvmc == 'false'
  _xvmc = 'disabled'
  warning('gallium-xvmc option "false" deprecated, please use "disabled" instead.')
endif
if not system_has_kms_drm
  if _xvmc == 'enabled'
    error('XVMC state tracker can only be build on unix-like OSes.')
  else
    _xvmc = 'disabled'
  endif
elif not with_platform_x11
  if _xvmc == 'enabled'
    error('XVMC state tracker requires X11 support.')
  else
    _xvmc = 'disabled'
  endif
elif not (with_gallium_r600 or with_gallium_nouveau)
  if _xvmc == 'enabled'
    error('XVMC state tracker requires at least one of the following gallium drivers: r600, nouveau.')
  else
    _xvmc = 'disabled'
  endif
endif
dep_xvmc = null_dep
dep_xv = null_dep
with_gallium_xvmc = false
if _xvmc != 'disabled'
  dep_xvmc = dependency('xvmc', version : '>= 1.0.6', required : _xvmc == 'enabled')
  dep_xv = dependency('xv', required : _xvmc == 'enabled')
  with_gallium_xvmc = dep_xvmc.found() and dep_xv.found()
endif

xvmc_drivers_path = get_option('xvmc-libs-path')
if xvmc_drivers_path == ''
  xvmc_drivers_path = get_option('libdir')
endif

_omx = get_option('gallium-omx')
if not system_has_kms_drm
  if ['auto', 'disabled'].contains(_omx)
    _omx = 'disabled'
  else
    error('OMX state tracker can only be built on unix-like OSes.')
  endif
elif not (with_gallium_r600 or with_gallium_radeonsi or with_gallium_nouveau)
  if ['auto', 'disabled'].contains(_omx)
    _omx = 'disabled'
  else
    error('OMX state tracker requires at least one of the following gallium drivers: r600, radeonsi, nouveau.')
  endif
endif
with_gallium_omx = _omx
dep_omx = null_dep
dep_omx_other = []
if ['auto', 'bellagio'].contains(_omx)
  dep_omx = dependency(
    'libomxil-bellagio', required : _omx == 'bellagio'
  )
  if dep_omx.found()
    with_gallium_omx = 'bellagio'
  endif
endif
if ['auto', 'tizonia'].contains(_omx)
  if with_dri and with_egl
    dep_omx = dependency(
      'libtizonia', version : '>= 0.10.0',
      required : _omx == 'tizonia',
    )
    dep_omx_other = [
      dependency('libtizplatform', required : _omx == 'tizonia'),
      dependency('tizilheaders', required : _omx == 'tizonia'),
    ]
    if dep_omx.found() and dep_omx_other[0].found() and dep_omx_other[1].found()
      with_gallium_omx = 'tizonia'
    endif
  elif _omx == 'tizonia'
    error('OMX-Tizonia state tracker requires dri and egl')
  endif
endif
if _omx == 'auto'
  with_gallium_omx = 'disabled'
else
  with_gallium_omx = _omx
endif

pre_args += [
  '-DENABLE_ST_OMX_BELLAGIO=' + (with_gallium_omx == 'bellagio' ? '1' : '0'),
  '-DENABLE_ST_OMX_TIZONIA=' + (with_gallium_omx == 'tizonia' ? '1' : '0'),
]


omx_drivers_path = get_option('omx-libs-path')

if with_gallium_omx != 'disabled'
  # Figure out where to put the omx driver.
  # FIXME: this could all be vastly simplified by adding a 'defined_variable'
  # argument to meson's get_pkgconfig_variable method.
  if omx_drivers_path == ''
    _omx_libdir = dep_omx.get_pkgconfig_variable('libdir')
    _omx_drivers_dir = dep_omx.get_pkgconfig_variable('pluginsdir')
    if _omx_libdir == get_option('libdir')
      omx_drivers_path = _omx_drivers_dir
    else
      _omx_base_dir = []
      # This will fail on windows. Does OMX run on windows?
      _omx_libdir = _omx_libdir.split('/')
      _omx_drivers_dir = _omx_drivers_dir.split('/')
      foreach o : _omx_drivers_dir
        if not _omx_libdir.contains(o)
          _omx_base_dir += o
        endif
      endforeach
      omx_drivers_path = join_paths(get_option('libdir'), _omx_base_dir)
    endif
  endif
endif

_va = get_option('gallium-va')
if _va == 'true'
  _va = 'enabled'
  warning('gallium-va option "true" deprecated, please use "enabled" instead.')
elif _va == 'false'
  _va = 'disabled'
  warning('gallium-va option "false" deprecated, please use "disabled" instead.')
endif
if not system_has_kms_drm
  if _va == 'enabled'
    error('VA state tracker can only be built on unix-like OSes.')
  else
    _va = 'disabled'
  endif
elif not (with_gallium_r600 or with_gallium_radeonsi or with_gallium_nouveau)
  if _va == 'enabled'
    error('VA state tracker requires at least one of the following gallium drivers: r600, radeonsi, nouveau.')
  else
    _va = 'disabled'
  endif
endif
with_gallium_va = false
dep_va = null_dep
if _va != 'disabled'
  dep_va = dependency('libva', version : '>= 1.8.0', required : _va == 'enabled')
  if dep_va.found()
    dep_va_headers = dep_va.partial_dependency(compile_args : true)
    with_gallium_va = true
    if cc.has_header_symbol('va/va.h', 'VASurfaceAttribDRMFormatModifiers',
                            dependencies: dep_va_headers)
      pre_args += '-DHAVE_VA_SURFACE_ATTRIB_DRM_FORMAT_MODIFIERS'
    endif
  endif
endif

va_drivers_path = get_option('va-libs-path')
if va_drivers_path == ''
  va_drivers_path = join_paths(get_option('libdir'), 'dri')
endif

_xa = get_option('gallium-xa')
if _xa == 'true'
  _xa = 'enabled'
  warning('gallium-xa option "true" deprecated, please use "enabled" instead.')
elif _xa == 'false'
  _xa = 'disabled'
  warning('gallium-xa option "false" deprecated, please use "disabled" instead.')
endif
if not system_has_kms_drm
  if _xa == 'enabled'
    error('XA state tracker can only be built on unix-like OSes.')
  else
    _xa = 'disabled'
  endif
elif not (with_gallium_nouveau or with_gallium_freedreno or with_gallium_i915
          or with_gallium_svga)
  if _xa == 'enabled'
    error('XA state tracker requires at least one of the following gallium drivers: nouveau, freedreno, i915, svga.')
  else
    _xa = 'disabled'
  endif
endif
with_gallium_xa = _xa != 'disabled'

d3d_drivers_path = get_option('d3d-drivers-path')
if d3d_drivers_path == ''
  d3d_drivers_path = join_paths(get_option('prefix'), get_option('libdir'), 'd3d')
endif

with_gallium_st_nine =  get_option('gallium-nine')
if with_gallium_st_nine
  if not with_gallium_softpipe
    error('The nine state tracker requires gallium softpipe/llvmpipe.')
  elif not (with_gallium_radeonsi or with_gallium_nouveau or with_gallium_r600
            or with_gallium_r300 or with_gallium_svga or with_gallium_i915
            or with_gallium_iris or with_gallium_crocus)
    error('The nine state tracker requires at least one non-swrast gallium driver.')
  endif
  if not with_dri3
    error('Using nine with wine requires dri3')
  endif
endif
with_gallium_st_d3d10umd =  get_option('gallium-d3d10umd')
if with_gallium_st_d3d10umd
  if not with_gallium_softpipe
    error('The d3d10umd state tracker requires gallium softpipe/llvmpipe.')
  endif
endif
_power8 = get_option('power8')
if _power8 == 'true'
  _power8 = 'enabled'
  warning('power8 option "true" deprecated, please use "enabled" instead.')
elif _power8 == 'false'
  _power8 = 'disabled'
  warning('power8 option "false" deprecated, please use "disabled" instead.')
endif
if _power8 != 'disabled'
  # on old versions of meson the cpu family would return as ppc64le on little
  # endian power8, this was changed in 0.48 such that the family would always
  # be ppc64 regardless of endianness, and then the machine.endian() value
  # should be checked. Since we support versions < 0.48 we need to use
  # startswith.
  if host_machine.cpu_family().startswith('ppc64') and host_machine.endian() == 'little'
    if cc.get_id() == 'gcc' and cc.version().version_compare('< 4.8')
      error('Altivec is not supported with gcc version < 4.8.')
    endif
    if cc.compiles('''
        #include <altivec.h>
        int main() {
          vector unsigned char r;
          vector unsigned int v = vec_splat_u32 (1);
          r = __builtin_vec_vgbbd ((vector unsigned char) v);
          return 0;
        }''',
        args : '-mpower8-vector',
        name : 'POWER8 intrinsics')
      pre_args += ['-D_ARCH_PWR8', '-mpower8-vector']
    elif get_option('power8') == 'enabled'
      error('POWER8 intrinsic support required but not found.')
    endif
  endif
endif

_opencl = get_option('gallium-opencl')
if _opencl != 'disabled'
  if not with_gallium
    error('OpenCL Clover implementation requires at least one gallium driver.')
  endif

  with_clc = true
  with_gallium_opencl = true
  with_opencl_icd = _opencl == 'icd'
else
  with_gallium_opencl = false
  with_opencl_icd = false
endif

dep_clc = null_dep
if with_clc
  dep_clc = dependency('libclc')
endif

gl_pkgconfig_c_flags = []
if with_platform_x11
  if with_any_vk or with_egl or (with_glx == 'dri' and with_dri_platform == 'drm')
    pre_args += '-DHAVE_X11_PLATFORM'
    pre_args += '-DHAVE_XCB_PLATFORM'
  endif
  if with_glx == 'xlib' or with_glx == 'gallium-xlib'
    pre_args += '-DUSE_XSHM'
  else
    pre_args += '-DGLX_INDIRECT_RENDERING'
    if with_glx_direct
      pre_args += '-DGLX_DIRECT_RENDERING'
    endif
    if with_dri_platform == 'drm'
      pre_args += '-DGLX_USE_DRM'
    elif with_dri_platform == 'apple'
      pre_args += '-DGLX_USE_APPLEGL'
    elif with_dri_platform == 'windows'
      pre_args += '-DGLX_USE_WINDOWSGL'
    endif
  endif
else
  pre_args += '-DEGL_NO_X11'
  gl_pkgconfig_c_flags += '-DEGL_NO_X11'
endif
if with_gbm and not with_platform_android
  pre_args += '-DHAVE_DRM_PLATFORM'
endif
if with_platform_windows
  pre_args += '-DHAVE_WINDOWS_PLATFORM'
endif

with_android_stub = get_option('android-stub')
if with_android_stub and not with_platform_android
  error('`-D android-stub=true` makes no sense without `-D platforms=android`')
endif

if with_platform_android
  dep_android_mapper4 = null_dep
  if not with_android_stub
    dep_android = [
      dependency('cutils'),
      dependency('hardware'),
      dependency('sync'),
      dependency('backtrace')
    ]
    if get_option('platform-sdk-version') >= 26
      dep_android += dependency('nativewindow')
    endif
    if get_option('platform-sdk-version') >= 30
      dep_android_mapper4 = dependency('android.hardware.graphics.mapper', version : '>= 4.0', required : false)
    endif
  endif
  pre_args += [
    '-DHAVE_ANDROID_PLATFORM',
    '-DANDROID',
    '-DANDROID_API_LEVEL=' + get_option('platform-sdk-version').to_string()
  ]
endif
if with_platform_haiku
  pre_args += '-DHAVE_HAIKU_PLATFORM'
endif

prog_python = import('python').find_installation('python3')
has_mako = run_command(
  prog_python, '-c',
  '''
from distutils.version import StrictVersion
import mako
assert StrictVersion(mako.__version__) > StrictVersion("0.8.0")
  ''')
if has_mako.returncode() != 0
  error('Python (3.x) mako module >= 0.8.0 required to build mesa.')
endif

if cc.get_id() == 'gcc' and cc.version().version_compare('< 4.4.6')
  error('When using GCC, version 4.4.6 or later is required.')
endif

# Support systems without ETIME (e.g. FreeBSD)
if cc.get_define('ETIME', prefix : '#include <errno.h>') == ''
  pre_args += '-DETIME=ETIMEDOUT'
endif

# Define DEBUG for debug builds only (debugoptimized is not included on this one)
if get_option('buildtype') == 'debug'
  pre_args += '-DDEBUG'
endif

with_shader_cache = false
_shader_cache = get_option('shader-cache')
if _shader_cache == 'true'
  _shader_cache = 'enabled'
  warning('shader_cache option "true" deprecated, please use "enabled" instead.')
elif _shader_cache == 'false'
  _shader_cache = 'disabled'
  warning('shader_cache option "false" deprecated, please use "disabled" instead.')
endif
if _shader_cache != 'disabled'
  if host_machine.system() == 'windows'
    if _shader_cache == 'enabled'
      error('Shader Cache does not currently work on Windows')
    endif
  else
    pre_args += '-DENABLE_SHADER_CACHE'
    if not get_option('shader-cache-default')
      pre_args += '-DSHADER_CACHE_DISABLE_BY_DEFAULT'
    endif
    with_shader_cache = true
  endif
endif

if with_shader_cache
  shader_cache_max_size = get_option('shader-cache-max-size')
  if shader_cache_max_size != ''
    pre_args += '-DMESA_GLSL_CACHE_MAX_SIZE="@0@"'.format(shader_cache_max_size)
  endif
endif

# Check for GCC style builtins
foreach b : ['bswap32', 'bswap64', 'clz', 'clzll', 'ctz', 'expect', 'ffs',
             'ffsll', 'popcount', 'popcountll', 'unreachable', 'types_compatible_p']
  if cc.has_function(b)
    pre_args += '-DHAVE___BUILTIN_@0@'.format(b.to_upper())
  endif
endforeach

# check for GCC __attribute__
_attributes = [
  'const', 'flatten', 'malloc', 'pure', 'unused', 'warn_unused_result',
  'weak', 'format', 'packed', 'returns_nonnull', 'alias', 'noreturn',
]
foreach a : cc.get_supported_function_attributes(_attributes)
  pre_args += '-DHAVE_FUNC_ATTRIBUTE_@0@'.format(a.to_upper())
endforeach
if cc.has_function_attribute('visibility:hidden')
  pre_args += '-DHAVE_FUNC_ATTRIBUTE_VISIBILITY'
endif
if cc.compiles('__uint128_t foo(void) { return 0; }',
               name : '__uint128_t')
  pre_args += '-DHAVE_UINT128'
endif

# TODO: this is very incomplete
if ['linux', 'cygwin', 'gnu', 'freebsd', 'gnu/kfreebsd', 'haiku'].contains(host_machine.system())
  pre_args += '-D_GNU_SOURCE'
elif host_machine.system() == 'sunos'
  pre_args += '-D__EXTENSIONS__'
elif host_machine.system() == 'windows'
  pre_args += [
    '-D_WINDOWS', '-D_WIN32_WINNT=0x0A00', '-DWINVER=0x0A00',
    '-DPIPE_SUBSYSTEM_WINDOWS_USER',
    '-D_USE_MATH_DEFINES',  # XXX: scons didn't use this for mingw
  ]
  if cc.get_id() == 'msvc'
    pre_args += [
      '-DVC_EXTRALEAN',
      '-D_CRT_SECURE_NO_WARNINGS',
      '-D_CRT_SECURE_NO_DEPRECATE',
      '-D_SCL_SECURE_NO_WARNINGS',
      '-D_SCL_SECURE_NO_DEPRECATE',
      '-D_ALLOW_KEYWORD_MACROS',
      '-D_HAS_EXCEPTIONS=0', # Tell C++ STL to not use exceptions
      '-DNOMINMAX',
    ]
  else
    pre_args += ['-D__MSVCRT_VERSION__=0x0700']
  endif
elif host_machine.system() == 'openbsd'
  pre_args += '-D_ISOC11_SOURCE'
endif

# Check for generic C arguments
c_msvc_compat_args = []
no_override_init_args = []
cpp_msvc_compat_args = []
if cc.get_id() == 'msvc'
  foreach a : ['/wd4018',  # signed/unsigned mismatch
               '/wd4056',  # overflow in floating-point constant arithmetic
               '/wd4244',  # conversion from 'type1' to 'type2', possible loss of data
               '/wd4267',  # 'var' : conversion from 'size_t' to 'type', possible loss of data
               '/wd4305',  # trancation from 'type1' to 'type2'
               '/wd4351',  # new behavior: elements of array 'array' will be default initialized
               '/wd4756',  # overflow in constant arithmetic
               '/wd4800',  # forcing value to bool 'true' or 'false' (performance warning)
               '/wd4996',  # disabled deprecated POSIX name warnings
               '/wd4291',  # no matching operator delete found
               '/wd4146',  # unary minus operator applied to unsigned type, result still unsigned
               '/wd4200',  # nonstandard extension used: zero-sized array in struct/union
               '/wd4624',  # destructor was implicitly defined as deleted [from LLVM]
               '/wd4309',  # 'initializing': truncation of constant value
               '/wd4838',  # conversion from 'int' to 'const char' requires a narrowing conversion
               '/wd5105',  # macro expansion producing 'defined' has undefined behavior (winbase.h, need Windows SDK upgrade)
               '/we4020',  # Error when passing the wrong number of parameters
               '/we4024',  # Error when passing different type of parameter
               '/Zc:__cplusplus', #Set __cplusplus macro to match the /std:c++<version> on the command line
              ]
    if cc.has_argument(a)
      c_args += a
    endif
    if cpp.has_argument(a)
      cpp_args += a
    endif
  endforeach
else
  _trial = [
    '-Werror=implicit-function-declaration',
    '-Werror=missing-prototypes',
    '-Werror=return-type',
    '-Werror=empty-body',
    '-Werror=incompatible-pointer-types',
    '-Werror=int-conversion',
    '-Wimplicit-fallthrough',
    '-Werror=thread-safety',
    '-Wno-missing-field-initializers',
    '-Wno-format-truncation',
    '-fno-math-errno',
    '-fno-trapping-math',
    '-Qunused-arguments',
    '-fno-common',
  ]
  # MinGW chokes on format specifiers and I can't get it all working
  if not (cc.get_id() == 'gcc' and host_machine.system() == 'windows')
    _trial += ['-Werror=format', '-Wformat-security']
  endif
  foreach a : _trial
    if cc.has_argument(a)
      c_args += a
    endif
  endforeach

  _trial = [
    '-Werror=return-type',
    '-Werror=empty-body',
    '-Wno-non-virtual-dtor',
    '-Wno-missing-field-initializers',
    '-Wno-format-truncation',
    '-fno-math-errno',
    '-fno-trapping-math',
    '-Qunused-arguments',
    # Some classes use custom new operator which zeroes memory, however
    # gcc does aggressive dead-store elimination which threats all writes
    # to the memory before the constructor as "dead stores".
    # For now we disable this optimization.
    '-flifetime-dse=1',
  ]
  # MinGW chokes on format specifiers and I can't get it all working
  if not (cc.get_id() == 'gcc' and host_machine.system() == 'windows')
    _trial += ['-Werror=format', '-Wformat-security']
  endif
  foreach a : _trial
    if cpp.has_argument(a)
      cpp_args += a
    endif
  endforeach

  foreach a : ['-Wno-override-init', '-Wno-initializer-overrides']
    if cc.has_argument(a)
      no_override_init_args += a
    endif
  endforeach

  # Check for C and C++ arguments for MSVC compatibility. These are only used
  # in parts of the mesa code base that need to compile with MSVC, mainly
  # common code
  foreach a : ['-Werror=pointer-arith', '-Werror=gnu-empty-initializer']
    if cc.has_argument(a)
      c_msvc_compat_args += a
    endif
    if cpp.has_argument(a)
      cpp_msvc_compat_args += a
    endif
  endforeach

  if cc.has_argument('-Wmicrosoft-enum-value')  # Clang
    c_args += '-Wno-microsoft-enum-value'
    cpp_args += '-Wno-microsoft-enum-value'
  endif
endif

# set linker arguments
if host_machine.system() == 'windows'
  if cc.get_id() == 'msvc'
    add_project_link_arguments(
      '/fixed:no',
      '/dynamicbase',
      '/nxcompat',
      language : ['c', 'cpp'],
    )
    if get_option('buildtype') != 'debug'
      add_project_link_arguments(
        '/incremental:no',
        language : ['c', 'cpp'],
      )
    endif
  else
    add_project_link_arguments(
      '-Wl,--nxcompat',
      '-Wl,--dynamicbase',
      '-static-libgcc',
      '-static-libstdc++',
      language : ['c', 'cpp'],
    )
  endif
endif

if host_machine.cpu_family().startswith('x86') and cc.get_id() != 'msvc'
  pre_args += '-DUSE_SSE41'
  with_sse41 = true
  sse41_args = ['-msse4.1']

  if host_machine.cpu_family() == 'x86'
    if get_option('sse2')
      # These settings make generated GCC code match MSVC and follow
      # GCC advice on https://gcc.gnu.org/wiki/FloatingPointMath#x86note
      #
      # NOTE: We need to ensure stack is realigned given that we
      # produce shared objects, and have no control over the stack
      # alignment policy of the application. Therefore we need
      # -mstackrealign or -mincoming-stack-boundary=2.
      #
      # XXX: We could have SSE without -mstackrealign if we always used
      # __attribute__((force_align_arg_pointer)), but that's not
      # always the case.
      c_args += ['-msse2', '-mfpmath=sse', '-mstackrealign']
    else
      # GCC on x86 (not x86_64) with -msse* assumes a 16 byte aligned stack, but
      # that's not guaranteed
      sse41_args += '-mstackrealign'
    endif
  endif
else
  with_sse41 = false
  sse41_args = []
endif

# Check for GCC style atomics
dep_atomic = null_dep

if cc.compiles('''#include <stdint.h>
                  int main() {
                    struct {
                      uint64_t *v;
                    } x;
                    return (int)__atomic_load_n(x.v, __ATOMIC_ACQUIRE) &
                           (int)__atomic_add_fetch(x.v, (uint64_t)1, __ATOMIC_ACQ_REL);

                  }''',
               name : 'GCC atomic builtins')
  pre_args += '-DUSE_GCC_ATOMIC_BUILTINS'

  # Not all atomic calls can be turned into lock-free instructions, in which
  # GCC will make calls into the libatomic library. Check whether we need to
  # link with -latomic.
  #
  # This can happen for 64-bit atomic operations on 32-bit architectures such
  # as ARM.
  if not cc.links('''#include <stdint.h>
                     int main() {
                       struct {
                         uint64_t *v;
                       } x;
                       return (int)__atomic_load_n(x.v, __ATOMIC_ACQUIRE) &
                              (int)__atomic_add_fetch(x.v, (uint64_t)1, __ATOMIC_ACQ_REL);
                     }''',
                  name : 'GCC atomic builtins required -latomic')
    dep_atomic = cc.find_library('atomic')
  endif
endif
if not cc.links('''#include <stdint.h>
                   uint64_t v;
                   int main() {
                     return __sync_add_and_fetch(&v, (uint64_t)1);
                   }''',
                dependencies : dep_atomic,
                name : 'GCC 64bit atomics')
  pre_args += '-DMISSING_64BIT_ATOMICS'
endif

dep_ws2_32 = cc.find_library('ws2_32', required : with_platform_windows)

# TODO: shared/static? Is this even worth doing?

with_asm_arch = ''
if host_machine.cpu_family() == 'x86'
  if system_has_kms_drm or host_machine.system() == 'gnu'
    with_asm_arch = 'x86'
    pre_args += ['-DUSE_X86_ASM', '-DUSE_MMX_ASM', '-DUSE_3DNOW_ASM',
                 '-DUSE_SSE_ASM']

    if with_glx_read_only_text
      pre_args += ['-DGLX_X86_READONLY_TEXT']
    endif
  endif
elif host_machine.cpu_family() == 'x86_64'
  if system_has_kms_drm
    with_asm_arch = 'x86_64'
    pre_args += ['-DUSE_X86_64_ASM']
  endif
elif host_machine.cpu_family() == 'arm'
  if system_has_kms_drm
    with_asm_arch = 'arm'
    pre_args += ['-DUSE_ARM_ASM']
  endif
elif host_machine.cpu_family() == 'aarch64'
  if system_has_kms_drm
    with_asm_arch = 'aarch64'
    pre_args += ['-DUSE_AARCH64_ASM']
  endif
elif host_machine.cpu_family() == 'sparc64'
  if system_has_kms_drm
    with_asm_arch = 'sparc'
    pre_args += ['-DUSE_SPARC_ASM']
  endif
elif host_machine.cpu_family().startswith('ppc64') and host_machine.endian() == 'little'
  if system_has_kms_drm
    with_asm_arch = 'ppc64le'
    pre_args += ['-DUSE_PPC64LE_ASM']
  endif
elif host_machine.cpu_family() == 'mips64' and host_machine.endian() == 'little'
  if system_has_kms_drm
    with_asm_arch = 'mips64el'
    pre_args += ['-DUSE_MIPS64EL_ASM']
  endif
endif

# Check for standard headers and functions
if (cc.has_header_symbol('sys/sysmacros.h', 'major') and
  cc.has_header_symbol('sys/sysmacros.h', 'minor') and
  cc.has_header_symbol('sys/sysmacros.h', 'makedev'))
  pre_args += '-DMAJOR_IN_SYSMACROS'
endif
if (cc.has_header_symbol('sys/mkdev.h', 'major') and
  cc.has_header_symbol('sys/mkdev.h', 'minor') and
  cc.has_header_symbol('sys/mkdev.h', 'makedev'))
  pre_args += '-DMAJOR_IN_MKDEV'
endif

if cc.check_header('sched.h')
  pre_args += '-DHAS_SCHED_H'
  if cc.has_function('sched_getaffinity')
    pre_args += '-DHAS_SCHED_GETAFFINITY'
  endif
endif

if not ['linux'].contains(host_machine.system())
  # Deprecated on Linux and requires <sys/types.h> on FreeBSD and OpenBSD
  if cc.check_header('sys/sysctl.h', prefix : '#include <sys/types.h>')
    pre_args += '-DHAVE_SYS_SYSCTL_H'
  endif
endif

foreach h : ['xlocale.h', 'linux/futex.h', 'endian.h', 'dlfcn.h', 'sys/shm.h', 'cet.h', 'pthread_np.h']
  if cc.check_header(h)
    pre_args += '-DHAVE_@0@'.format(h.to_upper().underscorify())
  endif
endforeach

foreach f : ['strtof', 'mkostemp', 'timespec_get', 'memfd_create', 'random_r',
             'flock', 'strtok_r', 'getrandom', 'qsort_r', 'qsort_s']
  if cc.has_function(f)
    pre_args += '-DHAVE_@0@'.format(f.to_upper())
  endif
endforeach

if cc.has_header_symbol('errno.h', 'program_invocation_name',
                        args : '-D_GNU_SOURCE')
   pre_args += '-DHAVE_PROGRAM_INVOCATION_NAME'
elif with_tools.contains('intel')
  error('Intel tools require the program_invocation_name variable')
endif

# MinGW provides a __builtin_posix_memalign function, but not a posix_memalign.
# This means that this check will succeed, but then compilation will later
# fail. MSVC doesn't have this function at all, so only check for it on
# non-windows platforms.
if host_machine.system() != 'windows'
  if cc.has_function('posix_memalign')
    pre_args += '-DHAVE_POSIX_MEMALIGN'
  endif
endif

if cc.has_member('struct dirent', 'd_type', prefix: '''#include <sys/types.h>
   #include <dirent.h>''')
   pre_args += '-DHAVE_DIRENT_D_TYPE'
endif

# strtod locale support
if cc.links('''
    #define _GNU_SOURCE
    #include <stdlib.h>
    #include <locale.h>
    #ifdef HAVE_XLOCALE_H
    #include <xlocale.h>
    #endif
    int main() {
      locale_t loc = newlocale(LC_CTYPE_MASK, "C", NULL);
      const char *s = "1.0";
      char *end;
      double d = strtod_l(s, end, loc);
      float f = strtof_l(s, end, loc);
      freelocale(loc);
      return 0;
    }''',
    args : pre_args,
    name : 'strtod has locale support')
  pre_args += '-DHAVE_STRTOD_L'
endif

# Check for some linker flags
ld_args_bsymbolic = []
if cc.links('int main() { return 0; }', args : '-Wl,-Bsymbolic', name : 'Bsymbolic')
  ld_args_bsymbolic += '-Wl,-Bsymbolic'
endif
ld_args_gc_sections = []
if cc.links('static char unused() { return 5; } int main() { return 0; }',
            args : '-Wl,--gc-sections', name : 'gc-sections')
  ld_args_gc_sections += '-Wl,--gc-sections'
endif
with_ld_version_script = false
if cc.links('int main() { return 0; }',
            args : '-Wl,--version-script=@0@'.format(
              join_paths(meson.source_root(), 'build-support/conftest.map')),
            name : 'version-script')
  with_ld_version_script = true
endif
with_ld_dynamic_list = false
if cc.links('int main() { return 0; }',
            args : '-Wl,--dynamic-list=@0@'.format(
              join_paths(meson.source_root(), 'build-support/conftest.dyn')),
            name : 'dynamic-list')
  with_ld_dynamic_list = true
endif

ld_args_build_id = cc.get_supported_link_arguments('-Wl,--build-id=sha1')

# check for dl support
dep_dl = null_dep
if not cc.has_function('dlopen')
  dep_dl = cc.find_library('dl', required : host_machine.system() != 'windows')
endif
if cc.has_function('dladdr', dependencies : dep_dl)
  # This is really only required for megadrivers
  pre_args += '-DHAVE_DLADDR'
endif

if cc.has_function('dl_iterate_phdr')
  pre_args += '-DHAVE_DL_ITERATE_PHDR'
elif with_intel_vk
  error('Intel "Anvil" Vulkan driver requires the dl_iterate_phdr function')
elif with_dri_i965 and with_shader_cache
  error('Intel i965 GL driver requires dl_iterate_phdr when built with shader caching.')
endif

# Determine whether or not the rt library is needed for time functions
if host_machine.system() == 'windows' or cc.has_function('clock_gettime')
  dep_clock = null_dep
else
  dep_clock = cc.find_library('rt')
endif

dep_zlib = dependency('zlib', version : '>= 1.2.3',
                      fallback : ['zlib', 'zlib_dep'],
                      required : get_option('zlib'))
if dep_zlib.found()
  pre_args += '-DHAVE_ZLIB'
endif

_zstd = get_option('zstd')
if _zstd == 'true'
  _zstd = 'enabled'
  warning('zstd option "true" deprecated, please use "enabled" instead.')
elif _zstd == 'false'
  _zstd = 'disabled'
  warning('zstd option "false" deprecated, please use "disabled" instead.')
endif
if _zstd != 'disabled'
  dep_zstd = dependency('libzstd', required : _zstd == 'enabled')
  if dep_zstd.found()
    pre_args += '-DHAVE_ZSTD'
  endif
else
  dep_zstd = null_dep
endif

with_compression = dep_zlib.found() or dep_zstd.found()
if with_compression
  pre_args += '-DHAVE_COMPRESSION'
elif with_shader_cache
  error('Shader Cache requires compression')
endif

dep_thread = dependency('threads')
if dep_thread.found() and host_machine.system() != 'windows'
  pre_args += '-DHAVE_PTHREAD'
  if host_machine.system() != 'netbsd' and cc.has_function(
      'pthread_setaffinity_np',
      dependencies : dep_thread,
      prefix : '#include <pthread.h>',
      args : '-D_GNU_SOURCE')
    pre_args += '-DHAVE_PTHREAD_SETAFFINITY'
  endif
endif
if host_machine.system() == 'darwin'
  dep_expat = meson.get_compiler('c').find_library('expat')
elif host_machine.system() != 'windows'
  dep_expat = dependency('expat', fallback : ['expat', 'expat_dep'],
                         required: not with_platform_android)
else
  dep_expat = null_dep
endif
# this only exists on linux so either this is linux and it will be found, or
# it's not linux and wont
dep_m = cc.find_library('m', required : false)

if host_machine.system() == 'windows'
  dep_regex = meson.get_compiler('c').find_library('regex', required : false)
  if not dep_regex.found()
    dep_regex = declare_dependency(compile_args : ['-DNO_REGEX'])
  endif
else
  dep_regex = null_dep
endif

if with_platform_haiku
  dep_network = cc.find_library('network')
endif

# Check for libdrm. Various drivers have different libdrm version requirements,
# but we always want to use the same version for all libdrm modules. That means
# even if driver foo requires 2.4.0 and driver bar requires 2.4.3, if foo and
# bar are both on use 2.4.3 for both of them
dep_libdrm_amdgpu = null_dep
dep_libdrm_radeon = null_dep
dep_libdrm_nouveau = null_dep
dep_libdrm_intel = null_dep

_drm_amdgpu_ver = '2.4.107'
_drm_radeon_ver = '2.4.71'
_drm_nouveau_ver = '2.4.102'
_drm_intel_ver = '2.4.75'
_drm_ver = '2.4.81'

_libdrm_checks = [
  ['intel', with_dri_i915 or with_gallium_i915],
  ['amdgpu', (with_amd_vk and not with_platform_windows) or with_gallium_radeonsi],
  ['radeon', (with_gallium_radeonsi or with_dri_r100 or with_dri_r200 or
              with_gallium_r300 or with_gallium_r600)],
  ['nouveau', (with_gallium_nouveau or with_dri_nouveau)],
]

# VC4 only needs core libdrm support of this version, not a libdrm_vc4
# library.
if with_gallium_vc4
  _drm_ver = '2.4.89'
endif

# etnaviv only needs core libdrm
if with_gallium_etnaviv
  _drm_ver = '2.4.89'
endif

# Loop over the enables versions and get the highest libdrm requirement for all
# active drivers.
_drm_blame = ''
foreach d : _libdrm_checks
  ver = get_variable('_drm_@0@_ver'.format(d[0]))
  if d[1] and ver.version_compare('>' + _drm_ver)
    _drm_ver = ver
    _drm_blame = d[0]
  endif
endforeach
if _drm_blame != ''
  message('libdrm @0@ needed because @1@ has the highest requirement'.format(_drm_ver, _drm_blame))
endif

# Then get each libdrm module
foreach d : _libdrm_checks
  if d[1]
    set_variable(
      'dep_libdrm_' + d[0],
      dependency('libdrm_' + d[0], version : '>=' + _drm_ver)
    )
  endif
endforeach

with_gallium_drisw_kms = false
dep_libdrm = dependency(
  'libdrm', version : '>=' + _drm_ver,
  # GNU/Hurd includes egl_dri2, without drm.
  required : (with_dri2 and host_machine.system() != 'gnu') or with_dri3
)
if dep_libdrm.found()
  pre_args += '-DHAVE_LIBDRM'
  if with_dri_platform == 'drm' and with_dri
    with_gallium_drisw_kms = true
  endif
endif

llvm_modules = ['bitwriter', 'engine', 'mcdisassembler', 'mcjit', 'core', 'executionengine', 'scalaropts', 'transformutils', 'instcombine']
llvm_optional_modules = ['coroutines']
if with_amd_vk or with_gallium_radeonsi or with_gallium_r600
  llvm_modules += ['amdgpu', 'native', 'bitreader', 'ipo']
  if with_gallium_r600
    llvm_modules += 'asmparser'
  endif
endif
if with_gallium_opencl
  llvm_modules += [
    'linker', 'coverage', 'instrumentation', 'ipo', 'irreader',
    'lto', 'option', 'objcarcopts', 'profiledata'
  ]
  llvm_optional_modules += ['frontendopenmp']
endif
if with_microsoft_clc
  llvm_modules += ['target', 'linker', 'irreader', 'option', 'libdriver']
endif
if with_tests or with_gallium_softpipe
  llvm_modules += 'native'
endif

if with_amd_vk or with_gallium_radeonsi
  _llvm_version = '>= 11.0.0'
elif with_microsoft_clc
  _llvm_version = '>= 10.0.0'
elif with_gallium_opencl
  _llvm_version = '>= 8.0.0'
elif with_gallium_swr
  _llvm_version = '>= 6.0.0'
else
  _llvm_version = '>= 3.9.0'
endif

_shared_llvm = get_option('shared-llvm')
if _shared_llvm == 'true'
  _shared_llvm = 'enabled'
  warning('shared_llvm option "true" deprecated, please use "enabled" instead.')
elif _shared_llvm == 'false'
  _shared_llvm = 'disabled'
  warning('shared_llvm option "false" deprecated, please use "disabled" instead.')
endif
if _shared_llvm == 'auto'
  _shared_llvm = (host_machine.system() != 'windows')
else
  _shared_llvm = (_shared_llvm == 'enabled')
endif
_llvm = get_option('llvm')
if _llvm == 'true'
  _llvm = 'enabled'
  warning('llvm option "true" deprecated, please use "enabled" instead.')
elif _llvm == 'false'
  _llvm = 'disabled'
  warning('llvm option "false" deprecated, please use "disabled" instead.')
endif

# the cmake method can only link statically, so don't attempt to use it if we
# want to link dynamically. Before 0.54.0 meson will try cmake even when shared
# linking is requested, so we need to force the config-tool method to be used
# in that case, but in 0.54.0 meson won't try the cmake method if shared
# linking is requested.
_llvm_method = 'auto'
if meson.version().version_compare('< 0.54.0') and _shared_llvm
  _llvm_method = 'config-tool'
endif

dep_llvm = null_dep
with_llvm = false
draw_with_llvm = get_option('draw-use-llvm')
if _llvm != 'disabled'
  dep_llvm = dependency(
    'llvm',
    version : _llvm_version,
    modules : llvm_modules,
    optional_modules : llvm_optional_modules,
    required : (
      with_amd_vk or with_gallium_radeonsi or with_gallium_swr or
      with_gallium_opencl or with_microsoft_clc or _llvm == 'enabled'
    ),
    static : not _shared_llvm,
    method : _llvm_method,
    fallback : ['llvm', 'dep_llvm'],
    include_type : 'system',
  )
  with_llvm = dep_llvm.found()
endif
if with_llvm
  pre_args += '-DLLVM_AVAILABLE'
  pre_args += '-DMESA_LLVM_VERSION_STRING="@0@"'.format(dep_llvm.version())
  pre_args += '-DLLVM_IS_SHARED=@0@'.format(_shared_llvm.to_int())

  if draw_with_llvm
    pre_args += '-DDRAW_LLVM_AVAILABLE'
  elif with_swrast_vk
    error('Lavapipe requires LLVM draw support.')
  elif with_gallium_swr
    error('SWR requires LLVM draw support.')
  endif

  # LLVM can be built without rtti, turning off rtti changes the ABI of C++
  # programs, so we need to build all C++ code in mesa without rtti as well to
  # ensure that linking works.
  #
  # In meson 0.51.0 we can use cmake to find LLVM in addittion to meson's
  # builtin llvm-config based finder. A new generic variable getter method
  # has also been added, so we'll use that if we can, to cover the cmake case.
  if dep_llvm.type_name() == 'internal'
    _rtti = subproject('llvm').get_variable('has_rtti', true)
  else
    # The CMake finder will return 'ON', the llvm-config will return 'YES'
    _rtti = ['ON', 'YES'].contains(dep_llvm.get_variable(cmake : 'LLVM_ENABLE_RTTI', configtool: 'has-rtti'))
  endif
  if not _rtti
    if with_gallium_opencl
      error('The Clover OpenCL state tracker requires rtti, you need to turn off clover or use an LLVM built with LLVM_ENABLE_RTTI.')
    endif
    if cc.get_id() == 'msvc'
      cpp_args += '/GR-'
    else
      cpp_args += '-fno-rtti'
    endif
  endif

  if cc.get_id() == 'msvc'
    # Suppress "/DELAYLOAD:ole32.dll/shell32.dll ignored" warnings that LLVM adds
    add_project_link_arguments(
      '/ignore:4199',
      language : ['c', 'cpp'],
    )
  endif
elif with_amd_vk or with_gallium_radeonsi or with_gallium_swr or with_swrast_vk
  error('The following drivers require LLVM: Radv, RadeonSI, SWR, Lavapipe. One of these is enabled, but LLVM is disabled.')
elif with_gallium_opencl
  error('The OpenCL "Clover" state tracker requires LLVM, but LLVM is disabled.')
elif with_microsoft_clc
  error('The Microsoft CLC compiler requires LLVM, but LLVM is disabled.')
else
  draw_with_llvm = false
endif

with_opencl_spirv = (_opencl != 'disabled' and get_option('opencl-spirv')) or with_microsoft_clc
if with_opencl_spirv
  chosen_llvm_version_array = dep_llvm.version().split('.')
  chosen_llvm_version_major = chosen_llvm_version_array[0].to_int()
  chosen_llvm_version_minor = chosen_llvm_version_array[1].to_int()

  # Require an SPIRV-LLVM-Translator version compatible with the chosen LLVM
  # one.
  _llvmspirvlib_version = [
    # This first version check is still needed as maybe LLVM 8.0 was picked but
    # we do not want to accept SPIRV-LLVM-Translator 8.0.0.1 as that version does
    # not have the required API and those are only available starting from
    # 8.0.1.3.
    '>= 8.0.1.3',
    '>= @0@.@1@'.format(chosen_llvm_version_major, chosen_llvm_version_minor),
    '< @0@.@1@'.format(chosen_llvm_version_major, chosen_llvm_version_minor + 1) ]

  dep_spirv_tools = dependency('SPIRV-Tools', required : true, version : '>= 2018.0')
  # LLVMSPIRVLib is available at https://github.com/KhronosGroup/SPIRV-LLVM-Translator
  dep_llvmspirvlib = dependency('LLVMSPIRVLib', required : true, version : _llvmspirvlib_version)
else
  dep_spirv_tools = null_dep
  dep_llvmspirvlib = null_dep
endif

with_opencl_native = _opencl != 'disabled' and get_option('opencl-native')

if (with_amd_vk or with_gallium_radeonsi or
    (with_gallium_opencl and with_opencl_native) or
    (with_gallium_r600 and with_llvm))
  if with_platform_windows
    dep_elf = dependency('libelf', required : false, fallback : ['libelf', 'libelf_dep'])
  else
    dep_elf = dependency('libelf', required : false)
  endif
  if not dep_elf.found()
    dep_elf = cc.find_library('elf')
  endif
else
  dep_elf = null_dep
endif

dep_glvnd = null_dep
if with_glvnd
  dep_glvnd = dependency('libglvnd', version : '>= 1.3.2')
  pre_args += '-DUSE_LIBGLVND=1'
endif

_valgrind = get_option('valgrind')
if _valgrind == 'true'
  _valgrind = 'enabled'
  warning('valgrind option "true" deprecated, please use "enabled" instead.')
elif _valgrind == 'false'
  _valgrind = 'disabled'
  warning('valgrind option "false" deprecated, please use "disabled" instead.')
endif
if _valgrind != 'disabled'
  dep_valgrind = dependency('valgrind', required : _valgrind == 'enabled')
  if dep_valgrind.found()
    pre_args += '-DHAVE_VALGRIND'
  endif
else
  dep_valgrind = null_dep
endif

# AddressSanitizer's leak reports need all the symbols to be present at exit to
# decode well, which runs afoul of our dlopen()/dlclose()ing of the DRI drivers.
# Set a flag so we can skip the dlclose for asan builds.
if ['address', 'address,undefined'].contains(get_option('b_sanitize'))
  asan_c_args = ['-DBUILT_WITH_ASAN=1']
else
  asan_c_args = ['-DBUILT_WITH_ASAN=0']
endif

yacc_is_bison = true

if build_machine.system() == 'windows'
  # Prefer the winflexbison versions, they're much easier to install and have
  # better windows support.

  prog_flex = find_program('win_flex', required : false)
  if prog_flex.found()
    # windows compatibility (uses <io.h> instead of <unistd.h> and _isatty,
    # _fileno functions)
    prog_flex = [prog_flex, '--wincompat']
    if get_option('c_std') == 'c99'
      prog_flex += '-D__STDC_VERSION__=199901'
    endif
  else
    prog_flex = [find_program('flex', 'lex', required : with_any_opengl)]
  endif
  # Force flex to use const keyword in prototypes, as relies on __cplusplus or
  # __STDC__ macro to determine whether it's safe to use const keyword, but
  # MSVC only defines __STDC_VERSION__ for C11/C17.
  prog_flex += '-DYY_USE_CONST='

  prog_flex_cpp = prog_flex
  if get_option('c_std') != 'c99'
    # Convince win_flex to use <inttypes.h> for C++ files
    prog_flex_cpp += '-D__STDC_VERSION__=199901'
  endif

  prog_bison = find_program('win_bison', required : false)
  if not prog_bison.found()
    prog_bison = find_program('bison', 'yacc', required : with_any_opengl)
  endif
else
  prog_bison = find_program('bison', required : false)

  if not prog_bison.found()
    prog_bison = find_program('byacc', required : with_any_opengl)
    yacc_is_bison = false
  endif

  # Disable deprecated keyword warnings, since we have to use them for
  # old-bison compat.  See discussion in
  # https://gitlab.freedesktop.org/mesa/mesa/merge_requests/2161
  if find_program('bison', required : false, version : '> 2.3').found()
    prog_bison = [prog_bison, '-Wno-deprecated']
  endif

  prog_flex = find_program('flex', required : with_any_opengl)
  prog_flex_cpp = prog_flex
endif

dep_selinux = null_dep
if get_option('selinux')
  if get_option('execmem') != true
    warning('execmem option is disabled, selinux will not be able to use execmem.')
  endif
  dep_selinux = dependency('libselinux')
  pre_args += '-DMESA_SELINUX'
endif

if get_option('execmem')
  pre_args += '-DMESA_EXECMEM'
endif

_libunwind = get_option('libunwind')
if _libunwind == 'true'
  _libunwind = 'enabled'
  warning('libunwind option "true" deprecated, please use "enabled" instead.')
elif _libunwind == 'false'
  _libunwind = 'disabled'
  warning('libunwind option "false" deprecated, please use "disabled" instead.')
endif
if _libunwind != 'disabled' and not with_platform_android
  if host_machine.system() == 'darwin'
    dep_unwind = meson.get_compiler('c').find_library('System')
  else
    dep_unwind = dependency('libunwind', required : _libunwind == 'enabled')
  endif
  if dep_unwind.found()
    pre_args += '-DHAVE_LIBUNWIND'
  endif
else
  dep_unwind = null_dep
endif

if with_osmesa
  if not with_gallium_softpipe
    error('OSMesa gallium requires gallium softpipe or llvmpipe.')
  endif
  if host_machine.system() == 'windows'
    osmesa_lib_name = 'osmesa'
  else
    osmesa_lib_name = 'OSMesa'
  endif
  osmesa_bits = get_option('osmesa-bits')
  if osmesa_bits != '8'
    if with_dri or with_glx != 'disabled'
      error('OSMesa bits must be 8 if building glx or dri based drivers')
    endif
    osmesa_lib_name = osmesa_lib_name + osmesa_bits
    pre_args += [
      '-DCHAN_BITS=@0@'.format(osmesa_bits), '-DDEFAULT_SOFTWARE_DEPTH_BITS=31'
    ]
  endif
endif

# TODO: symbol mangling

if with_platform_wayland
  dep_wl_scanner = dependency('wayland-scanner', native: true)
  prog_wl_scanner = find_program(dep_wl_scanner.get_pkgconfig_variable('wayland_scanner'))
  if dep_wl_scanner.version().version_compare('>= 1.15')
    wl_scanner_arg = 'private-code'
  else
    wl_scanner_arg = 'code'
  endif
  dep_wl_protocols = dependency('wayland-protocols', version : '>= 1.8')
  dep_wayland_client = dependency('wayland-client', version : '>=1.18')
  dep_wayland_server = dependency('wayland-server', version : '>=1.18')
  if with_egl
    dep_wayland_egl = dependency('wayland-egl-backend', version : '>= 3')
    dep_wayland_egl_headers = dep_wayland_egl.partial_dependency(compile_args : true)
  endif
  wayland_dmabuf_xml = join_paths(
    dep_wl_protocols.get_pkgconfig_variable('pkgdatadir'), 'unstable',
    'linux-dmabuf', 'linux-dmabuf-unstable-v1.xml'
  )
  pre_args += ['-DHAVE_WAYLAND_PLATFORM', '-DWL_HIDE_DEPRECATED']
endif

dep_x11 = null_dep
dep_xext = null_dep
dep_xfixes = null_dep
dep_x11_xcb = null_dep
dep_xcb = null_dep
dep_xcb_glx = null_dep
dep_xcb_dri2 = null_dep
dep_xcb_dri3 = null_dep
dep_dri2proto = null_dep
dep_glproto = null_dep
dep_xxf86vm = null_dep
dep_xcb_dri3 = null_dep
dep_xcb_present = null_dep
dep_xcb_sync = null_dep
dep_xcb_xfixes = null_dep
dep_xshmfence = null_dep
dep_xcb_xrandr = null_dep
dep_xcb_shm = null_dep
dep_xlib_xrandr = null_dep
dep_openmp = null_dep

# Even if we find OpenMP, Gitlab CI fails to link with gcc/i386 and clang/anyarch.
if host_machine.cpu_family() == 'x86_64' and cc.get_id() == 'gcc'
  dep_openmp = dependency('openmp', required : false)
  if dep_openmp.found()
    pre_args += ['-DHAVE_OPENMP']
  endif
endif

if with_platform_x11
  if with_glx == 'xlib' or with_glx == 'gallium-xlib'
    dep_x11 = dependency('x11')
    dep_xext = dependency('xext')
    dep_xcb = dependency('xcb')
  elif with_glx == 'dri'
    dep_x11 = dependency('x11')
    dep_xext = dependency('xext')
    dep_xfixes = dependency('xfixes', version : '>= 2.0')
    dep_xcb_glx = dependency('xcb-glx', version : '>= 1.8.1')
    dep_xcb_shm = dependency('xcb-shm')
  endif
  if (with_any_vk or with_glx == 'dri' or with_egl or
       (with_gallium_vdpau or with_gallium_xvmc or with_gallium_va or
        with_gallium_omx != 'disabled'))
    dep_xcb = dependency('xcb')
    dep_x11_xcb = dependency('x11-xcb')
    if with_dri_platform == 'drm' and not dep_libdrm.found()
      error('libdrm required for gallium video statetrackers when using x11')
    endif
  endif
  if with_any_vk or with_egl or (with_glx == 'dri' and with_dri_platform == 'drm')
    dep_xcb_dri2 = dependency('xcb-dri2', version : '>= 1.8')

    if with_dri3
      pre_args += '-DHAVE_DRI3'
      dep_xcb_dri3 = dependency('xcb-dri3')
      dep_xcb_present = dependency('xcb-present')
      # until xcb-dri3 has been around long enough to make a hard-dependency:
      if (dep_xcb_dri3.version().version_compare('>= 1.13') and
          dep_xcb_present.version().version_compare('>= 1.13'))
        pre_args += '-DHAVE_DRI3_MODIFIERS'
      endif
      dep_xcb_shm = dependency('xcb-shm')
      dep_xcb_sync = dependency('xcb-sync')
      dep_xshmfence = dependency('xshmfence', version : '>= 1.1')
    endif
  endif
  if with_glx == 'dri' or with_glx == 'gallium-xlib'
    dep_glproto = dependency('glproto', version : '>= 1.4.14')
  endif
  if with_glx == 'dri'
    if with_dri_platform == 'drm'
      dep_dri2proto = dependency('dri2proto', version : '>= 2.8')
      if with_glx_direct
        dep_xxf86vm = dependency('xxf86vm')
      endif
    endif
  endif
  if (with_egl or
      with_dri3 or (
      with_gallium_vdpau or with_gallium_xvmc or with_gallium_xa or
      with_gallium_omx != 'disabled'))
    dep_xcb_xfixes = dependency('xcb-xfixes')
  endif
  if with_xlib_lease or with_any_vk
    dep_xcb_xrandr = dependency('xcb-randr')
  endif
  if with_xlib_lease
    dep_xlib_xrandr = dependency('xrandr', version : '>= 1.3')
  endif
endif

if get_option('gallium-extra-hud')
  pre_args += '-DHAVE_GALLIUM_EXTRA_HUD=1'
endif

_sensors = get_option('lmsensors')
if _sensors == 'true'
  _sensors = 'enabled'
  warning('lmsensors option "true" deprecated, please use "enabled" instead.')
elif _sensors == 'false'
  _sensors = 'disabled'
  warning('lmsensors option "false" deprecated, please use "disabled" instead.')
endif
if _sensors != 'disabled'
  dep_lmsensors = cc.find_library('sensors', required : _sensors == 'enabled')
  if dep_lmsensors.found()
    pre_args += '-DHAVE_LIBSENSORS=1'
  endif
else
  dep_lmsensors = null_dep
endif

_shader_replacement = get_option('custom-shader-replacement')
if _shader_replacement == ''
else
  pre_args += '-DCUSTOM_SHADER_REPLACEMENT'
endif

with_perfetto = get_option('perfetto')
with_datasources = get_option('datasources')
with_any_datasource = with_datasources.length() != 0
if with_perfetto
  dep_perfetto = dependency('perfetto', fallback: ['perfetto', 'dep_perfetto'])
  pre_args += '-DHAVE_PERFETTO'
endif

# If the compiler supports it, put function and data symbols in their
# own sections and GC the sections after linking.  This lets drivers
# drop shared code unused by that specific driver (particularly
# relevant for Vulkan drivers).
if cc.has_link_argument('-Wl,--gc-sections')
  foreach a: ['-ffunction-sections', '-fdata-sections']
    if cc.has_argument(a)
      add_project_arguments(a, language : ['c', 'cpp'])
    endif
  endforeach
endif

foreach a : pre_args
  add_project_arguments(a, language : ['c', 'cpp'])
endforeach
foreach a : c_args
  add_project_arguments(a, language : ['c'])
endforeach
foreach a : cpp_args
  add_project_arguments(a, language : ['cpp'])
endforeach

gl_priv_reqs = []

if with_glx == 'xlib' or with_glx == 'gallium-xlib'
  gl_priv_reqs += ['x11', 'xext', 'xcb']
elif with_glx == 'dri'
  gl_priv_reqs += [
    'x11', 'xext', 'xfixes', 'x11-xcb', 'xcb',
    'xcb-glx >= 1.8.1']
  if with_dri_platform == 'drm'
    gl_priv_reqs += 'xcb-dri2 >= 1.8'
    if with_glx_direct
      gl_priv_reqs += 'xxf86vm'
    endif
  endif
endif
if dep_libdrm.found()
  gl_priv_reqs += 'libdrm >= 2.4.75'
endif

gl_priv_libs = []
if dep_thread.found()
  gl_priv_libs += ['-lpthread', '-pthread']
endif
if dep_m.found()
  gl_priv_libs += '-lm'
endif
if dep_dl.found()
  gl_priv_libs += '-ldl'
endif

# FIXME: autotools lists this as incomplete
gbm_priv_libs = []
if dep_dl.found()
  gbm_priv_libs += '-ldl'
endif

pkg = import('pkgconfig')

if host_machine.system() == 'windows'
  prog_dumpbin = find_program('dumpbin', required : false)
  with_symbols_check = prog_dumpbin.found() and with_tests
  if with_symbols_check
    symbols_check_args = ['--dumpbin', prog_dumpbin.path()]
  endif
else
  prog_nm = find_program('nm')
  with_symbols_check = with_tests
  symbols_check_args = ['--nm', prog_nm.path()]
endif

# This quirk needs to be applied to sources with functions defined in assembly
# as GCC LTO drops them. See: https://bugs.freedesktop.org/show_bug.cgi?id=109391
gcc_lto_quirk = (cc.get_id() == 'gcc') ? ['-fno-lto'] : []

subdir('include')
subdir('bin')
subdir('src')

# Meson 0.49 and earlier seems to have a bug that fails to evaluate the string-
# formatting below unless the first argument is passed as a variable. This has
# been fixed in Meson 0.50 and beyond, but we need to keep it like this for now
# for backwards compatibility.
_with_opengl_string = with_opengl ? 'yes' : 'no'

lines = ['',
  'prefix:          ' + get_option('prefix'),
  'libdir:          ' + get_option('libdir'),
  'includedir:      ' + get_option('includedir'),
  '',
  'OpenGL:          @0@ (ES1: @1@ ES2: @2@)'.format(_with_opengl_string,
                                                    with_gles1 ? 'yes' : 'no',
                                                    with_gles2 ? 'yes' : 'no'),
]

if with_osmesa
  lines += ''
  lines += 'OSMesa:          lib' + osmesa_lib_name
else
  lines += 'OSMesa:          no'
endif

if with_dri
  lines += ''
  lines += 'DRI platform:    ' + with_dri_platform
  if dri_drivers.length() != 0 and dri_drivers != ['']
    lines += 'DRI drivers:     ' + ' '.join(dri_drivers)
  else
    lines += 'DRI drivers:     no'
  endif
  lines += 'DRI driver dir:  ' + dri_drivers_path
endif

if with_glx != 'disabled'
  lines += ''
  if with_glx == 'dri'
    lines += 'GLX:             DRI-based'
  elif with_glx == 'xlib'
    lines += 'GLX:             Xlib-based'
  elif with_glx == 'gallium-xlib'
    lines += 'GLX:             Xlib-based (Gallium)'
  else
    lines += 'GLX:             ' + with_glx
  endif
endif

lines += ''
lines += 'EGL:             ' + (with_egl ? 'yes' : 'no')
if with_egl
  egl_drivers = []
  if with_dri
    egl_drivers += 'builtin:egl_dri2'
  endif
  if with_dri3
    egl_drivers += 'builtin:egl_dri3'
  endif
  if with_platform_windows
    egl_drivers += 'builtin:wgl'
  endif
  lines += 'EGL drivers:     ' + ' '.join(egl_drivers)
endif
if with_egl or with_any_vk
  _platforms += 'surfaceless'
  if with_gbm and not with_platform_android
    _platforms += 'drm'
  endif
  lines += 'EGL/Vulkan/VL platforms:   ' + ' '.join(_platforms)
endif
lines += 'GBM:             ' + (with_gbm ? 'yes' : 'no')
if with_gbm
  lines += 'GBM backends path: ' + gbm_backends_path
endif

lines += ''
if with_any_vk
  lines += 'Vulkan drivers:  ' + ' '.join(_vulkan_drivers)
  lines += 'Vulkan ICD dir:  ' + with_vulkan_icd_dir
  if with_any_vulkan_layers
    lines += 'Vulkan layers:   ' + ' '.join(get_option('vulkan-layers'))
  endif
else
  lines += 'Vulkan drivers:  no'
endif

lines += ''
if with_llvm
  lines += 'llvm:            yes'
  lines += 'llvm-version:    ' + dep_llvm.version()
else
  lines += 'llvm:            no'
endif

lines += ''
if with_gallium
  lines += 'Gallium drivers: ' + ' '.join(gallium_drivers)
  gallium_st = ['mesa']
  if with_gallium_xa
    gallium_st += 'xa'
  endif
  if with_gallium_xvmc
    gallium_st += 'xvmc'
  endif
  if with_gallium_vdpau
    gallium_st += 'vdpau'
  endif
  if with_gallium_omx != 'disabled'
    gallium_st += 'omx' + with_gallium_omx
  endif
  if with_gallium_va
    gallium_st += 'va'
  endif
  if with_gallium_st_nine
    gallium_st += 'nine'
  endif
  if with_gallium_opencl
    gallium_st += 'clover'
  endif
  lines += 'Gallium st:      ' + ' '.join(gallium_st)
else
  lines += 'Gallium:         no'
endif

lines += 'HUD lmsensors:   ' + (dep_lmsensors.found() ? 'yes' : 'no')

lines += ''
lines += 'Shared-glapi:    ' + (with_shared_glapi ? 'yes' : 'no')

lines += ''
lines += 'Perfetto:        ' + (with_perfetto ? 'yes' : 'no')
if with_any_datasource
  lines += 'Perfetto ds:     ' + ' '.join(with_datasources)
endif


indent = '        '
summary = indent + ('\n' + indent).join(lines)
message('Configuration summary:\n@0@\n'.format(summary))
