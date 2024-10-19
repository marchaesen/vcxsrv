# Copyright © 2018 Rob Clark
# SPDX-License-Identifier: MIT

libfreedreno_drm_files = files(
  'freedreno_bo.c',
  'freedreno_bo_heap.c',
  'freedreno_bo_cache.c',
  'freedreno_device.c',
  'freedreno_drmif.h',
  'freedreno_pipe.c',
  'freedreno_priv.h',
  'freedreno_ringbuffer.c',
  'freedreno_ringbuffer.h',
  'freedreno_ringbuffer_sp.c',
  'freedreno_ringbuffer_sp.h',
  'freedreno_ringbuffer_sp_reloc.h',
)
libfreedreno_drm_flags = []
libfreedreno_drm_includes = [
  inc_freedreno,
  inc_include,
  inc_src,
]
libfreedreno_drm_deps = [
  dep_libdrm,
  dep_valgrind,
  idep_mesautil,
]

if with_perfetto
  libfreedreno_drm_deps += dep_perfetto
  libfreedreno_drm_files += 'freedreno_drm_perfetto.cc'
endif

# The header file ends up part of the build (but just a stub)
# in either case:
libfreedreno_drm_files += 'freedreno_drm_perfetto.h'

libfreedreno_drm_msm_files = files(
  'msm/msm_bo.c',
  'msm/msm_device.c',
  'msm/msm_pipe.c',
  'msm/msm_priv.h',
  'msm/msm_ringbuffer.c',
  'msm/msm_ringbuffer_sp.c',
)
libfreedreno_drm_files += libfreedreno_drm_msm_files

libfreedreno_drm_virtio_files = files(
  'virtio/virtio_bo.c',
  'virtio/virtio_device.c',
  'virtio/virtio_pipe.c',
  'virtio/virtio_priv.h',
  'virtio/virtio_ringbuffer.c',
)
if freedreno_kmds.contains('virtio')
  libfreedreno_drm_files += libfreedreno_drm_virtio_files
  libfreedreno_drm_flags += '-DHAVE_FREEDRENO_VIRTIO'
  libfreedreno_drm_includes += [
    inc_virtio_gpu,
    inc_virtio_vdrm,
  ]
endif

libfreedreno_drm = static_library(
  'freedreno_drm',
  [
    libfreedreno_drm_files,
    freedreno_xml_header_files,
  ],
  include_directories : libfreedreno_drm_includes,
  c_args : [no_override_init_args, libfreedreno_drm_flags],
  gnu_symbol_visibility : 'hidden',
  dependencies : libfreedreno_drm_deps,
  # TODO
  link_with : [libvdrm],
  build_by_default : false,
)

