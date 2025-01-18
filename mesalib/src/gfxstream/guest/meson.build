# Copyright 2022 Android Open Source Project
# SPDX-License-Identifier: MIT

#===============#
# Options       #
#===============#

with_vulkan_icd_dir = get_option('vulkan-icd-dir')
if with_vulkan_icd_dir == ''
  with_vulkan_icd_dir = join_paths(get_option('datadir'), 'vulkan/icd.d')
endif

#===============#
# Configuration #
#===============#
gfxstream_guest_args = []

# Our internal guest build
gfxstream_guest_args += '-DLINUX_GUEST_BUILD'
# Include the gfxstream private VkStructureType definitions
gfxstream_guest_args += '-DVK_GFXSTREAM_STRUCTURE_TYPE_EXT'

#===============#
# Dependencies  #
#===============#

dl_dep = cc.find_library('dl', required: false)
drm_dep = dependency('libdrm')
thread_dep = dependency('threads')
aemu_base_dep = dependency('aemu_base')

with_kumquat = false
virtgpu_kumquat_dep = dependency('virtgpu_kumquat_ffi', required: false)
if virtgpu_kumquat_dep.found()
  with_kumquat = true
endif

#===============#
# Includes      #
#===============#

inc_include_mesa = include_directories('../../../include')
inc_vulkan_headers = include_directories('../../../include/vulkan')
inc_vulkan_enc = include_directories('vulkan_enc')

#================#
# Mesa compat    #
#================#
# dep_libdrm = drm_dep
# dep_dl = dl_dep
# dep_thread = thread_dep

#================#
# Subdirectories #
#================#
subdir('iostream')
subdir('platform')
subdir('GoldfishAddressSpace')
subdir('connection-manager')
subdir('vulkan_enc')
subdir('vulkan')
