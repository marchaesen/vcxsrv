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

project(
  'mesa',
  ['c', 'cpp'],
  version : run_command(
    [find_program('python', 'python2', 'python3'), 'bin/meson_get_version.py']
  ).stdout(),
  license : 'MIT',
  meson_version : '>= 0.42',
  default_options : ['buildtype=debugoptimized', 'c_std=c99', 'cpp_std=c++11']
)

# Arguments for the preprocessor, put these in a separate array from the C and
# C++ (cpp in meson terminology) arguments since they need to be added to the
# default arguments for both C and C++.
pre_args = [
  '-D__STDC_CONSTANT_MACROS',
  '-D__STDC_FORMAT_MACROS',
  '-D__STDC_LIMIT_MACROS',
  '-DVERSION="@0@"'.format(meson.project_version()),
  '-DPACKAGE_VERSION=VERSION',
  '-DPACKAGE_BUGREPORT="https://bugs.freedesktop.org/enter_bug.cgi?product=Mesa"',
]

with_vulkan_icd_dir = get_option('vulkan-icd-dir')
with_tests = get_option('build-tests')
with_valgrind = get_option('valgrind')
with_libunwind = get_option('libunwind')
with_asm = get_option('asm')
with_osmesa = get_option('osmesa')
with_swr_arches = get_option('swr-arches').split(',')
with_tools = get_option('tools').split(',')
if with_tools.contains('all')
  with_tools = ['freedreno', 'glsl', 'intel', 'nir', 'nouveau']
endif
if get_option('texture-float')
  pre_args += '-DTEXTURE_FLOAT_ENABLED'
  message('WARNING: Floating-point texture enabled. Please consult docs/patents.txt and your lawyer before building mesa.')
endif

dri_drivers_path = get_option('dri-drivers-path')
if dri_drivers_path == ''
  dri_drivers_path = join_paths(get_option('libdir'), 'dri')
endif
dri_search_path = get_option('dri-search-path')
if dri_search_path == ''
  dri_search_path = join_paths(get_option('prefix'), dri_drivers_path)
endif

with_gles1 = get_option('gles1')
with_gles2 = get_option('gles2')
with_opengl = get_option('opengl')
with_any_opengl = with_opengl or with_gles1 or with_gles2
# Only build shared_glapi if at least one OpenGL API is enabled
with_shared_glapi = get_option('shared-glapi') and with_any_opengl


# shared-glapi is required if at least two OpenGL APIs are being built
if not with_shared_glapi
  if ((with_gles1 and with_gles2) or (with_gles1 and with_opengl)
      or (with_gles2 and with_opengl))
    error('shared-glapi required for building two or more of OpenGL, OpenGL ES 1.x, OpenGL ES 2.x')
  endif
endif

# We require OpenGL for OpenGL ES
if (with_gles1 or with_gles2) and not with_opengl
  error('building OpenGL ES without OpenGL is not supported.')
endif

with_dri = false
with_dri_i915 = false
with_dri_i965 = false
with_dri_r100 = false
with_dri_r200 = false
with_dri_nouveau = false
with_dri_swrast = false
_drivers = get_option('dri-drivers')
if _drivers == 'auto'
  if host_machine.system() == 'linux'
    # TODO: PPC, Sparc
    if ['x86', 'x86_64'].contains(host_machine.cpu_family())
      _drivers = 'i915,i965,r100,r200,nouveau'
    else
      error('Unknown architecture. Please pass -Ddri-drivers to set driver options. Patches gladly accepted to fix this.')
    endif
  elif ['darwin', 'windows', 'cygwin', 'haiku'].contains(host_machine.system())
    # only swrast would make sense here, but gallium swrast is a much better default
    _drivers = ''
  else
    error('Unknown OS. Please pass -Ddri-drivers to set driver options. Patches gladly accepted to fix this.')
  endif
endif
if _drivers != ''
  _split = _drivers.split(',')
  with_dri_i915 = _split.contains('i915')
  with_dri_i965 = _split.contains('i965')
  with_dri_r100 = _split.contains('r100')
  with_dri_r200 = _split.contains('r200')
  with_dri_nouveau = _split.contains('nouveau')
  with_dri_swrast = _split.contains('swrast')
  with_dri = true
endif

with_gallium = false
with_gallium_pl111 = false
with_gallium_radeonsi = false
with_gallium_r300 = false
with_gallium_r600 = false
with_gallium_nouveau = false
with_gallium_freedreno = false
with_gallium_softpipe = false
with_gallium_vc4 = false
with_gallium_vc5 = false
with_gallium_etnaviv = false
with_gallium_imx = false
with_gallium_i915 = false
with_gallium_svga = false
with_gallium_virgl = false
with_gallium_swr = false
_drivers = get_option('gallium-drivers')
if _drivers == 'auto'
  if host_machine.system() == 'linux'
    # TODO: PPC, Sparc
    if ['x86', 'x86_64'].contains(host_machine.cpu_family())
      _drivers = 'r300,r600,radeonsi,nouveau,virgl,svga,swrast'
    elif ['arm', 'aarch64'].contains(host_machine.cpu_family())
      _drivers = 'pl111,vc4,vc5,freedreno,etnaviv,imx,virgl,svga,swrast'
    else
      error('Unknown architecture. Please pass -Dgallium-drivers to set driver options. Patches gladly accepted to fix this.')
    endif
  elif ['darwin', 'windows', 'cygwin', 'haiku'].contains(host_machine.system())
    _drivers = 'swrast'
  else
    error('Unknown OS. Please pass -Dgallium-drivers to set driver options. Patches gladly accepted to fix this.')
  endif
endif
if _drivers != ''
  _split = _drivers.split(',')
  with_gallium_pl111 = _split.contains('pl111')
  with_gallium_radeonsi = _split.contains('radeonsi')
  with_gallium_r300 = _split.contains('r300')
  with_gallium_r600 = _split.contains('r600')
  with_gallium_nouveau = _split.contains('nouveau')
  with_gallium_freedreno = _split.contains('freedreno')
  with_gallium_softpipe = _split.contains('swrast')
  with_gallium_vc4 = _split.contains('vc4')
  with_gallium_vc5 = _split.contains('vc5')
  with_gallium_etnaviv = _split.contains('etnaviv')
  with_gallium_imx = _split.contains('imx')
  with_gallium_i915 = _split.contains('i915')
  with_gallium_svga = _split.contains('svga')
  with_gallium_virgl = _split.contains('virgl')
  with_gallium_swr = _split.contains('swr')
  with_gallium = true
endif

with_intel_vk = false
with_amd_vk = false
with_any_vk = false
_vulkan_drivers = get_option('vulkan-drivers')
if _vulkan_drivers == 'auto'
  if host_machine.system() == 'linux'
    if host_machine.cpu_family().startswith('x86')
      _vulkan_drivers = 'amd,intel'
    else
      error('Unknown architecture. Please pass -Dvulkan-drivers to set driver options. Patches gladly accepted to fix this.')
    endif
  elif ['darwin', 'windows', 'cygwin', 'haiku'].contains(host_machine.system())
    # No vulkan driver supports windows or macOS currently
    _vulkan_drivers = ''
  else
    error('Unknown OS. Please pass -Dvulkan-drivers to set driver options. Patches gladly accepted to fix this.')
  endif
endif
if _vulkan_drivers != ''
  _split = _vulkan_drivers.split(',')
  with_intel_vk = _split.contains('intel')
  with_amd_vk = _split.contains('amd')
  with_any_vk = with_amd_vk or with_intel_vk
endif

if with_dri_swrast and (with_gallium_softpipe or with_gallium_swr)
  error('Only one swrast provider can be built')
endif
if with_dri_i915 and with_gallium_i915
  error('Only one i915 provider can be built')
endif
if with_gallium_imx and not with_gallium_etnaviv
  error('IMX driver requires etnaviv driver')
endif
if with_gallium_pl111 and not with_gallium_vc4
  error('pl111 driver requires vc4 driver')
endif

dep_libdrm_intel = []
if with_dri_i915 or with_gallium_i915
  dep_libdrm_intel = dependency('libdrm_intel', version : '>= 2.4.75')
endif

system_has_kms_drm = ['openbsd', 'netbsd', 'freebsd', 'dragonfly', 'linux'].contains(host_machine.system())

if host_machine.system() == 'darwin'
  with_dri_platform = 'apple'
elif ['windows', 'cygwin'].contains(host_machine.system())
  with_dri_platform = 'windows'
elif system_has_kms_drm
  with_dri_platform = 'drm'
else
  # FIXME: haiku doesn't use dri, and xlib doesn't use dri, probably should
  # assert here that one of those cases has been met.
  # FIXME: GNU (hurd) ends up here as well, but meson doesn't officially
  # support Hurd at time of writing (2017/11)
  # FIXME: illumos ends up here as well
  with_dri_platform = 'none'
endif

with_platform_android = false
with_platform_wayland = false
with_platform_x11 = false
with_platform_drm = false
with_platform_surfaceless = false
egl_native_platform = ''
_platforms = get_option('platforms')
if _platforms == 'auto'
  if system_has_kms_drm
    _platforms = 'x11,wayland,drm,surfaceless'
  elif ['darwin', 'windows', 'cygwin'].contains(host_machine.system())
    _platforms = 'x11,surfaceless'
  elif ['haiku'].contains(host_machine.system())
    _platforms = 'haiku'
  else
    error('Unknown OS. Please pass -Dplatforms to set platforms. Patches gladly accepted to fix this.')
  endif
endif
if _platforms != ''
  _split = _platforms.split(',')
  with_platform_android = _split.contains('android')
  with_platform_x11 = _split.contains('x11')
  with_platform_wayland = _split.contains('wayland')
  with_platform_drm = _split.contains('drm')
  with_platform_haiku = _split.contains('haiku')
  with_platform_surfaceless = _split.contains('surfaceless')
  egl_native_platform = _split[0]
endif

with_glx = get_option('glx')
if with_glx == 'auto'
  if with_dri
    with_glx = 'dri'
  elif with_platform_haiku
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

if not (with_dri or with_gallium or with_glx == 'xlib' or with_glx == 'gallium-xlib')
  with_gles1 = false
  with_gles2 = false
  with_opengl = false
  with_any_opengl = false
  with_shared_glapi = false
endif

with_gbm = get_option('gbm')
if with_gbm == 'auto' and with_dri  # TODO: or gallium
  with_gbm = system_has_kms_drm
elif with_gbm == 'true'
  if not system_has_kms_drm
    error('GBM only supports DRM/KMS platforms')
  endif
  with_gbm = true
else
  with_gbm = false
endif

_egl = get_option('egl')
if _egl == 'auto'
  with_egl = with_dri and with_shared_glapi and egl_native_platform != ''
elif _egl == 'true'
  if not with_dri
    error('EGL requires dri')
  elif not with_shared_glapi
    error('EGL requires shared-glapi')
  elif egl_native_platform == ''
    error('No platforms specified, consider -Dplatforms=drm,x11 at least')
  endif
  with_egl = true
else
  with_egl = false
endif

if with_egl and not (with_platform_drm or with_platform_surfaceless)
  if with_gallium_radeonsi
    error('RadeonSI requires drm or surfaceless platform when using EGL')
  endif
  if with_gallium_virgl
    error('Virgl requires drm or surfaceless platform when using EGL')
  endif
endif

pre_args += '-DGLX_USE_TLS'
if with_glx != 'disabled'
  if not (with_platform_x11 and with_any_opengl)
    if with_glx == 'auto'
      with_glx = 'disabled'
    else
      error('Cannot build GLX support without X11 platform support and at least one OpenGL API')
    endif
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
  elif with_glx == 'dri' and not with_dri
    error('dri based GLX requires at least one DRI driver')
  endif
endif

with_glvnd = get_option('glvnd')
if with_glvnd
  if with_glx == 'xlib' or with_glx == 'gallium-xlib'
    error('Cannot build glvnd support for GLX that is not DRI based.')
  elif with_glx == 'disabled' and not with_egl
    error('glvnd requires DRI based GLX and/or EGL')
  endif
endif

# TODO: toggle for this
with_glx_direct = true

if with_vulkan_icd_dir == ''
  with_vulkan_icd_dir = join_paths(get_option('datadir'), 'vulkan/icd.d')
endif

with_dri2 = (with_dri or with_any_vk) and with_dri_platform == 'drm'
with_dri3 = get_option('dri3')
if with_dri3 == 'auto'
  with_dri3 = system_has_kms_drm and with_dri2
else
  with_dri3 = with_dri3 == 'true'
endif

if with_any_vk and (with_platform_x11 and not with_dri3)
  error('Vulkan drivers require dri3 for X11 support')
endif
if with_dri or with_gallium
  if with_glx == 'disabled' and not with_egl and not with_platform_haiku
    error('building dri or gallium drivers require at least one window system')
  endif
endif

prog_pkgconfig = find_program('pkg-config')

_vdpau = get_option('gallium-vdpau')
if not system_has_kms_drm
  if _vdpau == 'true'
    error('VDPAU state tracker can only be build on unix-like OSes.')
  else
    _vdpau = 'false'
  endif
elif not with_platform_x11
  if _vdpau == 'true'
    error('VDPAU state tracker requires X11 support.')
  else
    _vdpau = 'false'
  endif
elif not (with_gallium_r300 or with_gallium_r600 or with_gallium_radeonsi or
          with_gallium_nouveau)
  if _vdpau == 'true'
    error('VDPAU state tracker requires at least one of the following gallium drivers: r300, r600, radeonsi, nouveau.')
  else
    _vdpau = 'false'
  endif
elif _vdpau == 'auto'
  _vdpau = 'true'
endif
with_gallium_vdpau = _vdpau == 'true'
dep_vdpau = []
if with_gallium_vdpau
  dep_vdpau = dependency('vdpau', version : '>= 1.1')
  dep_vdpau = declare_dependency(
    compile_args : run_command(prog_pkgconfig, ['vdpau', '--cflags']).stdout().split()
  )
endif

if with_gallium_vdpau
  pre_args += '-DHAVE_ST_VDPAU'
endif
vdpau_drivers_path = get_option('vdpau-libs-path')
if vdpau_drivers_path == ''
  vdpau_drivers_path = join_paths(get_option('libdir'), 'vdpau')
endif

_xvmc = get_option('gallium-xvmc')
if not system_has_kms_drm
  if _xvmc == 'true'
    error('XVMC state tracker can only be build on unix-like OSes.')
  else
    _xvmc = 'false'
  endif
elif not with_platform_x11
  if _xvmc == 'true'
    error('XVMC state tracker requires X11 support.')
  else
    _xvmc = 'false'
  endif
elif not (with_gallium_r600 or with_gallium_nouveau)
  if _xvmc == 'true'
    error('XVMC state tracker requires at least one of the following gallium drivers: r600, nouveau.')
  else
    _xvmc = 'false'
  endif
elif _xvmc == 'auto'
  _xvmc = 'true'
endif
with_gallium_xvmc = _xvmc == 'true'
dep_xvmc = []
if with_gallium_xvmc
  dep_xvmc = dependency('xvmc', version : '>= 1.0.6')
endif

xvmc_drivers_path = get_option('xvmc-libs-path')
if xvmc_drivers_path == ''
  xvmc_drivers_path = get_option('libdir')
endif

_omx = get_option('gallium-omx')
if not system_has_kms_drm
  if _omx == 'true'
    error('OMX state tracker can only be built on unix-like OSes.')
  else
    _omx = 'false'
  endif
elif not (with_platform_x11 or with_platform_drm)
  if _omx == 'true'
    error('OMX state tracker requires X11 or drm platform support.')
  else
    _omx = 'false'
  endif
elif not (with_gallium_r600 or with_gallium_radeonsi or with_gallium_nouveau)
  if _omx == 'true'
    error('OMX state tracker requires at least one of the following gallium drivers: r600, radeonsi, nouveau.')
  else
    _omx = 'false'
  endif
elif _omx == 'auto'
  _omx = 'true'
endif
with_gallium_omx = _omx == 'true'
dep_omx = []
if with_gallium_omx
  dep_omx = dependency('libomxil-bellagio')
endif

omx_drivers_path = get_option('omx-libs-path')
if with_gallium_omx
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
if not system_has_kms_drm
  if _va == 'true'
    error('VA state tracker can only be built on unix-like OSes.')
  else
    _va = 'false'
  endif
elif not (with_platform_x11 or with_platform_drm)
  if _va == 'true'
    error('VA state tracker requires X11 or drm or wayland platform support.')
  else
    _va = 'false'
  endif
elif not (with_gallium_r600 or with_gallium_radeonsi or with_gallium_nouveau)
  if _va == 'true'
    error('VA state tracker requires at least one of the following gallium drivers: r600, radeonsi, nouveau.')
  else
    _va = 'false'
  endif
elif _va == 'auto'
  _va = 'true'
endif
with_gallium_va = _va == 'true'
dep_va = []
if with_gallium_va
  dep_va = dependency('libva', version : '>= 0.38.0')
  dep_va_headers = declare_dependency(
    compile_args : run_command(prog_pkgconfig, ['libva', '--cflags']).stdout().split()
  )
endif

va_drivers_path = get_option('va-libs-path')
if va_drivers_path == ''
  va_drivers_path = join_paths(get_option('libdir'), 'dri')
endif

_xa = get_option('gallium-xa')
if not system_has_kms_drm
  if _xa == 'true'
    error('XA state tracker can only be built on unix-like OSes.')
  else
    _xa = 'false'
  endif
elif not (with_gallium_nouveau or with_gallium_freedreno or with_gallium_i915
          or with_gallium_svga)
  if _xa == 'true'
    error('XA state tracker requires at least one of the following gallium drivers: nouveau, freedreno, i915, svga.')
  else
    _xa = 'false'
  endif
endif
with_gallium_xa = _xa != 'false'

d3d_drivers_path = get_option('d3d-drivers-path')
if d3d_drivers_path == ''
  d3d_drivers_path = join_paths(get_option('libdir'), 'd3d')
endif

with_gallium_st_nine =  get_option('gallium-nine')
if with_gallium_st_nine
  if not with_gallium_softpipe
    error('The nine state tracker requires gallium softpipe/llvmpipe.')
  elif not (with_gallium_radeonsi or with_gallium_nouveau or with_gallium_r600
            or with_gallium_r300 or with_gallium_svga or with_gallium_i915)
    error('The nine state tracker requires at least on non-swrast gallium driver.')
  endif
  if not with_dri3
    error('Using nine with wine requires dri3')
  endif
endif

_opencl = get_option('gallium-opencl')
if _opencl != 'disabled'
  if not with_gallium
    error('OpenCL Clover implementation requires at least one gallium driver.')
  endif

  # TODO: alitvec?
  dep_clc = dependency('libclc')
  with_gallium_opencl = true
  with_opencl_icd = _opencl == 'icd'
else
  dep_clc = []
  with_gallium_opencl = false
  with_gallium_icd = false
endif

gl_pkgconfig_c_flags = []
if with_platform_x11
  if with_any_vk or (with_glx == 'dri' and with_dri_platform == 'drm')
    pre_args += '-DHAVE_X11_PLATFORM'
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
  pre_args += '-DMESA_EGL_NO_X11_HEADERS'
  gl_pkgconfig_c_flags += '-DMESA_EGL_NO_X11_HEADERS'
endif
if with_platform_drm
  if with_egl and not with_gbm
    error('EGL drm platform requires gbm')
  endif
  pre_args += '-DHAVE_DRM_PLATFORM'
endif
if with_platform_surfaceless
  pre_args += '-DHAVE_SURFACELESS_PLATFORM'
endif
if with_platform_android
  dep_android = [
    dependency('cutils'),
    dependency('hardware'),
    dependency('sync'),
  ]
  pre_args += '-DHAVE_ANDROID_PLATFORM'
endif
if with_platform_haiku
  pre_args += '-DHAVE_HAIKU_PLATFORM'
endif

prog_python2 = find_program('python2')
has_mako = run_command(prog_python2, '-c', 'import mako')
if has_mako.returncode() != 0
  error('Python (2.x) mako module required to build mesa.')
endif

cc = meson.get_compiler('c')
if cc.get_id() == 'gcc' and cc.version().version_compare('< 4.4.6')
  error('When using GCC, version 4.4.6 or later is required.')
endif

# Define DEBUG for debug builds only (debugoptimized is not included on this one)
if get_option('buildtype') == 'debug'
  pre_args += '-DDEBUG'
endif

if get_option('shader-cache')
  pre_args += '-DENABLE_SHADER_CACHE'
elif with_amd_vk
  error('Radv requires shader cache support')
endif

# Check for GCC style builtins
foreach b : ['bswap32', 'bswap64', 'clz', 'clzll', 'ctz', 'expect', 'ffs',
             'ffsll', 'popcount', 'popcountll', 'unreachable']
  if cc.has_function(b)
    pre_args += '-DHAVE___BUILTIN_@0@'.format(b.to_upper())
  endif
endforeach

# check for GCC __attribute__
foreach a : ['const', 'flatten', 'malloc', 'pure', 'unused',
             'warn_unused_result', 'weak',]
  if cc.compiles('int foo(void) __attribute__((@0@));'.format(a),
                 name : '__attribute__((@0@))'.format(a))
    pre_args += '-DHAVE_FUNC_ATTRIBUTE_@0@'.format(a.to_upper())
  endif
endforeach
if cc.compiles('int foo(const char *p, ...) __attribute__((format(printf, 1, 2)));',
               name : '__attribute__((format(...)))')
  pre_args += '-DHAVE_FUNC_ATTRIBUTE_FORMAT'
endif
if cc.compiles('struct __attribute__((packed)) foo { int bar; };',
               name : '__attribute__((packed))')
  pre_args += '-DHAVE_FUNC_ATTRIBUTE_PACKED'
endif
if cc.compiles('int *foo(void) __attribute__((returns_nonnull));',
               name : '__attribute__((returns_nonnull))')
  pre_args += '-DHAVE_FUNC_ATTRIBUTE_RETURNS_NONNULL'
endif
if cc.compiles('''int foo_def(void) __attribute__((visibility("default")));
                  int foo_hid(void) __attribute__((visibility("hidden")));
                  int foo_int(void) __attribute__((visibility("internal")));
                  int foo_pro(void) __attribute__((visibility("protected")));''',
               name : '__attribute__((visibility(...)))')
  pre_args += '-DHAVE_FUNC_ATTRIBUTE_VISIBILITY'
endif
if cc.compiles('int foo(void) { return 0; } int bar(void) __attribute__((alias("foo")));',
               name : '__attribute__((alias(...)))')
  pre_args += '-DHAVE_FUNC_ATTRIBUTE_ALIAS'
endif
if cc.compiles('int foo(void) __attribute__((__noreturn__));',
               name : '__attribute__((__noreturn__))')
  pre_args += '-DHAVE_FUNC_ATTRIBUTE_NORETURN'
endif

# TODO: this is very incomplete
if ['linux', 'cygwin'].contains(host_machine.system())
  pre_args += '-D_GNU_SOURCE'
endif

# Check for generic C arguments
c_args = []
foreach a : ['-Wall', '-Werror=implicit-function-declaration',
             '-Werror=missing-prototypes', '-fno-math-errno',
             '-fno-trapping-math', '-Qunused-arguments']
  if cc.has_argument(a)
    c_args += a
  endif
endforeach
c_vis_args = []
if cc.has_argument('-fvisibility=hidden')
  c_vis_args += '-fvisibility=hidden'
endif

# Check for generic C++ arguments
cpp = meson.get_compiler('cpp')
cpp_args = []
foreach a : ['-Wall', '-fno-math-errno', '-fno-trapping-math',
             '-Qunused-arguments']
  if cpp.has_argument(a)
    cpp_args += a
  endif
endforeach

# For some reason, the test for -Wno-foo always succeeds with gcc, even if the
# option is not supported. Hence, check for -Wfoo instead.
if cpp.has_argument('-Wnon-virtual-dtor')
  cpp_args += '-Wno-non-virtual-dtor'
endif

no_override_init_args = []
foreach a : ['override-init', 'initializer-overrides']
  if cc.has_argument('-W' + a)
    no_override_init_args += '-Wno-' + a
  endif
endforeach

cpp_vis_args = []
if cpp.has_argument('-fvisibility=hidden')
  cpp_vis_args += '-fvisibility=hidden'
endif

# Check for C and C++ arguments for MSVC2013 compatibility. These are only used
# in parts of the mesa code base that need to compile with old versions of
# MSVC, mainly common code
c_msvc_compat_args = []
cpp_msvc_compat_args = []
foreach a : ['-Werror=pointer-arith', '-Werror=vla']
  if cc.has_argument(a)
    c_msvc_compat_args += a
  endif
  if cpp.has_argument(a)
    cpp_msvc_compat_args += a
  endif
endforeach

if host_machine.cpu_family().startswith('x86')
  pre_args += '-DUSE_SSE41'
  with_sse41 = true
  sse41_args = ['-msse4.1']

  # GCC on x86 (not x86_64) with -msse* assumes a 16 byte aligned stack, but
  # that's not guaranteed
  if host_machine.cpu_family() == 'x86'
    sse41_args += '-mstackrealign'
  endif
else
  with_sse41 = false
  sse41_args = []
endif

# Check for GCC style atomics
if cc.compiles('int main() { int n; return __atomic_load_n(&n, __ATOMIC_ACQUIRE); }',
               name : 'GCC atomic builtins')
  pre_args += '-DUSE_GCC_ATOMIC_BUILTINS'
endif
if not cc.links('''#include <stdint.h>
                   uint64_t v;
                   int main() {
                     return __sync_add_and_fetch(&v, (uint64_t)1);
                   }''',
                name : 'GCC 64bit atomics')
  pre_args += '-DMISSING_64_BIT_ATOMICS'
endif

# TODO: endian
# TODO: powr8
# TODO: shared/static? Is this even worth doing?

# Building x86 assembly code requires running x86 binaries. It is possible for
# x86_64 OSes to run x86 binaries, so don't disable asm in those cases
# TODO: it should be possible to use an exe_wrapper to run the binary during
# the build. 
if meson.is_cross_build() 
  if not (build_machine.cpu_family() == 'x86_64' and host_machine.cpu_family() == 'x86'
          and build_machine.system() == host_machine.system())
    message('Cross compiling to x86 from non-x86, disabling asm')
    with_asm = false
  endif
endif

with_asm_arch = ''
if with_asm
  # TODO: SPARC and PPC
  if host_machine.cpu_family() == 'x86'
    if system_has_kms_drm
      with_asm_arch = 'x86'
      pre_args += ['-DUSE_X86_ASM', '-DUSE_MMX_ASM', '-DUSE_3DNOW_ASM',
                   '-DUSE_SSE_ASM']
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
  endif
endif

# Check for standard headers and functions
if cc.has_header_symbol('sys/sysmacros.h', 'major')
  pre_args += '-DMAJOR_IN_SYSMACROS'
elif cc.has_header_symbol('sys/mkdev.h', 'major')
  pre_args += '-DMAJOR_IN_MKDEV'
endif

foreach h : ['xlocale.h', 'sys/sysctl.h', 'linux/futex.h']
  if cc.has_header(h)
    pre_args += '-DHAVE_@0@'.format(h.to_upper().underscorify())
  endif
endforeach

foreach f : ['strtof', 'mkostemp', 'posix_memalign', 'timespec_get', 'memfd_create']
  if cc.has_function(f)
    pre_args += '-DHAVE_@0@'.format(f.to_upper())
  endif
endforeach

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
    extra_args : pre_args,
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
ld_args_build_id = []
if build_machine.system() != 'darwin'
   ld_args_build_id += '-Wl,--build-id=sha1'
endif

# check for dl support
if cc.has_function('dlopen')
  dep_dl = []
else
  dep_dl = cc.find_library('dl')
endif
if cc.has_function('dladdr', dependencies : dep_dl)
  # This is really only required for megadrivers
  pre_args += '-DHAVE_DLADDR'
endif

if cc.has_function('dl_iterate_phdr')
  pre_args += '-DHAVE_DL_ITERATE_PHDR'
elif with_intel_vk
  error('Intel "Anvil" Vulkan driver requires the dl_iterate_phdr function')
elif with_dri_i965 and get_option('shader-cache')
  error('Intel i965 GL driver requires dl_iterate_phdr when built with shader caching.')
endif

# Determine whether or not the rt library is needed for time functions
if cc.has_function('clock_gettime')
  dep_clock = []
else
  dep_clock = cc.find_library('rt')
endif

with_gallium_drisw_kms = false
dep_libdrm = dependency('libdrm', version : '>= 2.4.75',
                        required : with_dri2 or with_dri3)
if dep_libdrm.found()
  pre_args += '-DHAVE_LIBDRM'
  if with_dri_platform == 'drm' and with_dri
    with_gallium_drisw_kms = true
  endif
endif

# TODO: some of these may be conditional
dep_zlib = dependency('zlib', version : '>= 1.2.3')
pre_args += '-DHAVE_ZLIB'
dep_thread = dependency('threads')
if dep_thread.found() and host_machine.system() != 'windows'
  pre_args += '-DHAVE_PTHREAD'
endif
if with_amd_vk or with_gallium_radeonsi or with_gallium_r600 or with_gallium_opencl
  dep_elf = dependency('libelf', required : false)
  if not dep_elf.found()
    dep_elf = cc.find_library('elf')
  endif
else
  dep_elf = []
endif
dep_expat = dependency('expat')
# this only exists on linux so either this is linux and it will be found, or
# its not linux and and wont
dep_m = cc.find_library('m', required : false)

dep_libdrm_amdgpu = []
dep_libdrm_radeon = []
dep_libdrm_nouveau = []
dep_libdrm_etnaviv = []
dep_libdrm_freedreno = []
if with_amd_vk or with_gallium_radeonsi
  dep_libdrm_amdgpu = dependency('libdrm_amdgpu', version : '>= 2.4.90')
endif
if (with_gallium_radeonsi or with_dri_r100 or with_dri_r200 or
    with_gallium_r300 or with_gallium_r600)
  dep_libdrm_radeon = dependency('libdrm_radeon', version : '>= 2.4.71')
endif
if with_gallium_nouveau or with_dri_nouveau
  dep_libdrm_nouveau = dependency('libdrm_nouveau', version : '>= 2.4.66')
endif
if with_gallium_etnaviv
  dep_libdrm_etnaviv = dependency('libdrm_etnaviv', version : '>= 2.4.82')
endif
if with_gallium_freedreno
  dep_libdrm_freedreno = dependency('libdrm_freedreno', version : '>= 2.4.89')
endif

llvm_modules = ['bitwriter', 'engine', 'mcdisassembler', 'mcjit']
if with_amd_vk or with_gallium_radeonsi or with_gallium_r600
  llvm_modules += ['amdgpu', 'bitreader', 'ipo']
  if with_gallium_r600
    llvm_modules += 'asmparser'
  endif
endif
if with_gallium_opencl
  llvm_modules += [
    'all-targets', 'linker', 'coverage', 'instrumentation', 'ipo', 'irreader',
    'lto', 'option', 'objcarcopts', 'profiledata',
  ]
  # TODO: optional modules
endif

if with_amd_vk or with_gallium_radeonsi or with_gallium_swr
  _llvm_version = '>= 4.0.0'
elif with_gallium_opencl or with_gallium_r600
  _llvm_version = '>= 3.9.0'
else
  _llvm_version = '>= 3.3.0'
endif

_llvm = get_option('llvm')
if _llvm == 'auto'
  dep_llvm = dependency(
    'llvm', version : _llvm_version, modules : llvm_modules,
    required : with_amd_vk or with_gallium_radeonsi or with_gallium_swr or with_gallium_opencl,
  )
  with_llvm = dep_llvm.found()
elif _llvm == 'true'
  dep_llvm = dependency('llvm', version : _llvm_version, modules : llvm_modules)
  with_llvm = true
else
  dep_llvm = []
  with_llvm = false
endif
if with_llvm
  _llvm_version = dep_llvm.version().split('.')
  # Development versions of LLVM have an 'svn' or 'git' suffix, we don't want
  # that for our version checks.
  # svn suffixes are stripped by meson as of 0.43, and git suffixes are
  # strippped as of 0.44, but we support older meson versions.
  _llvm_patch = _llvm_version[2]
  if _llvm_patch.endswith('svn')
    _llvm_patch = _llvm_patch.split('s')[0]
  elif _llvm_patch.contains('git')
    _llvm_patch = _llvm_patch.split('g')[0]
  endif
  pre_args += [
    '-DHAVE_LLVM=0x0@0@0@1@'.format(_llvm_version[0], _llvm_version[1]),
    '-DMESA_LLVM_VERSION_PATCH=@0@'.format(_llvm_patch),
  ]
elif with_amd_vk or with_gallium_radeonsi or with_gallium_swr
  error('The following drivers require LLVM: Radv, RadeonSI, SWR. One of these is enabled, but LLVM is disabled.')
endif

dep_glvnd = []
if with_glvnd
  dep_glvnd = dependency('libglvnd', version : '>= 0.2.0')
  pre_args += '-DUSE_LIBGLVND=1'
endif

if with_valgrind != 'false'
  dep_valgrind = dependency('valgrind', required : with_valgrind == 'true')
  if dep_valgrind.found()
    pre_args += '-DHAVE_VALGRIND'
  endif
else
  dep_valgrind = []
endif

# pthread stubs. Lets not and say we didn't

prog_bison = find_program('bison', required : with_any_opengl)
prog_flex = find_program('flex', required : with_any_opengl)

dep_selinux = []
if get_option('selinux')
  dep_selinux = dependency('libselinux')
  pre_args += '-DMESA_SELINUX'
endif

# TODO: llvm-prefix and llvm-shared-libs

if with_libunwind != 'false'
  dep_unwind = dependency('libunwind', required : with_libunwind == 'true')
  if dep_unwind.found()
    pre_args += '-DHAVE_LIBUNWIND'
  endif
else
  dep_unwind = []
endif

# TODO: gallium-hud

if with_osmesa != 'none'
  if with_osmesa == 'classic' and not with_dri_swrast
    error('OSMesa classic requires dri (classic) swrast.')
  endif
  if with_osmesa == 'gallium' and not with_gallium_softpipe
    error('OSMesa gallium requires gallium softpipe or llvmpipe.')
  endif
  osmesa_lib_name = 'OSMesa'
  osmesa_bits = get_option('osmesa-bits')
  if osmesa_bits != '8'
    if with_dri or with_glx != 'disabled'
      error('OSMesa bits must be 8 if building glx or dir based drivers')
    endif
    osmesa_lib_name = osmesa_lib_name + osmesa_bits
    pre_args += [
      '-DCHAN_BITS=@0@'.format(osmesa_bits), '-DDEFAULT_SOFTWARE_DEPTH_BITS=31'
    ]
  endif
endif

# TODO: symbol mangling

if with_platform_wayland
  prog_wl_scanner = find_program('wayland-scanner')
  dep_wl_protocols = dependency('wayland-protocols', version : '>= 1.8')
  dep_wayland_client = dependency('wayland-client', version : '>=1.11')
  dep_wayland_server = dependency('wayland-server', version : '>=1.11')
  wayland_dmabuf_xml = join_paths(
    dep_wl_protocols.get_pkgconfig_variable('pkgdatadir'), 'unstable',
    'linux-dmabuf', 'linux-dmabuf-unstable-v1.xml'
  )
  pre_args += ['-DHAVE_WAYLAND_PLATFORM', '-DWL_HIDE_DEPRECATED']
else
  prog_wl_scanner = []
  dep_wl_protocols = []
  dep_wayland_client = []
  dep_wayland_server = []
  wayland_dmabuf_xml = ''
endif

dep_x11 = []
dep_xext = []
dep_xdamage = []
dep_xfixes = []
dep_x11_xcb = []
dep_xcb = []
dep_xcb_glx = []
dep_xcb_dri2 = []
dep_xcb_dri3 = []
dep_dri2proto = []
dep_glproto = []
dep_xxf86vm = []
dep_xcb_dri3 = []
dep_xcb_present = []
dep_xcb_sync = []
dep_xcb_xfixes = []
dep_xshmfence = []
if with_platform_x11
  if with_glx == 'xlib' or with_glx == 'gallium-xlib'
    dep_x11 = dependency('x11')
    dep_xext = dependency('xext')
    dep_xcb = dependency('xcb')
  elif with_glx == 'dri'
    dep_x11 = dependency('x11')
    dep_xext = dependency('xext')
    dep_xdamage = dependency('xdamage', version : '>= 1.1')
    dep_xfixes = dependency('xfixes')
    dep_xcb_glx = dependency('xcb-glx', version : '>= 1.8.1')
    dep_xxf86vm = dependency('xxf86vm', required : false)
  endif
  if (with_any_vk or with_glx == 'dri' or
       (with_gallium_vdpau or with_gallium_xvmc or with_gallium_omx or
        with_gallium_xa))
    dep_xcb = dependency('xcb')
    dep_x11_xcb = dependency('x11-xcb')
  endif
  if with_any_vk or (with_glx == 'dri' and with_dri_platform == 'drm')
    dep_xcb_dri2 = dependency('xcb-dri2', version : '>= 1.8')

    if with_dri3
      pre_args += '-DHAVE_DRI3'
      dep_xcb_dri3 = dependency('xcb-dri3')
      dep_xcb_present = dependency('xcb-present')
      dep_xcb_sync = dependency('xcb-sync')
      dep_xshmfence = dependency('xshmfence', version : '>= 1.1')
    endif
  endif
  if with_glx == 'dri'
    if with_dri_platform == 'drm'
      dep_dri2proto = dependency('dri2proto', version : '>= 2.8')
    endif
    dep_glproto = dependency('glproto', version : '>= 1.4.14')
  endif
  if with_egl
    dep_xcb_xfixes = dependency('xcb-xfixes')
  endif
endif

if get_option('gallium-extra-hud')
  pre_args += '-DHAVE_GALLIUM_EXTRA_HUD=1'
endif

_sensors = get_option('lmsensors')
if _sensors != 'false'
  dep_lmsensors = cc.find_library('libsensors', required : _sensors == 'true')
  if dep_lmsensors.found()
    pre_args += '-DHAVE_LIBSENSORS=1'
  endif
else
  dep_lmsensors = []
endif

# TODO: gallium tests

# TODO: various libdirs

# TODO: gallium driver dirs

# FIXME: this is a workaround for #2326
prog_touch = find_program('touch')
dummy_cpp = custom_target(
  'dummy_cpp',
  output : 'dummy.cpp',
  command : [prog_touch, '@OUTPUT@'],
)

foreach a : pre_args
  add_project_arguments(a, language : ['c', 'cpp'])
endforeach
foreach a : c_args
  add_project_arguments(a, language : ['c'])
endforeach
foreach a : cpp_args
  add_project_arguments(a, language : ['cpp'])
endforeach

inc_include = include_directories('include')

gl_priv_reqs = [
  'x11', 'xext', 'xdamage >= 1.1', 'xfixes', 'x11-xcb', 'xcb',
  'xcb-glx >= 1.8.1']
if dep_libdrm.found()
  gl_priv_reqs += 'libdrm >= 2.4.75'
endif
if dep_xxf86vm != [] and dep_xxf86vm.found()
  gl_priv_reqs += 'xxf86vm'
endif
if with_dri_platform == 'drm'
  gl_priv_reqs += 'xcb-dri2 >= 1.8'
endif

gl_priv_libs = []
if dep_thread.found()
  gl_priv_libs += ['-lpthread', '-pthread']
endif
if dep_m.found()
  gl_priv_libs += '-lm'
endif
if dep_dl != [] and dep_dl.found()
  gl_priv_libs += '-ldl'
endif

pkg = import('pkgconfig')

subdir('include')
subdir('bin')
subdir('src')
