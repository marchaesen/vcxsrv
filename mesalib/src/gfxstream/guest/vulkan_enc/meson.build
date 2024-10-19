# Copyright 2022 Android Open Source Project
# SPDX-License-Identifier: MIT

output_host_path = meson.current_build_dir() + '/host/vulkan/cereal'
guest_enc_path = meson.current_build_dir()

gfxstream_guest_vk_autogen = custom_target(
    'gfxstream_guest_vk_autogen',
    output : [
      'VkEncoder.h',
      'goldfish_vk_counting_guest.h',
      'goldfish_vk_deepcopy_guest.h',
      'goldfish_vk_marshaling_guest.h',
      'goldfish_vk_extension_structs_guest.h',
      'goldfish_vk_reserved_marshaling_guest.h',
      'goldfish_vk_transform_guest.h',
      'goldfish_vk_counting_guest.cpp',
      'goldfish_vk_deepcopy_guest.cpp',
      'goldfish_vk_extension_structs_guest.cpp',
      'goldfish_vk_marshaling_guest.cpp',
      'goldfish_vk_reserved_marshaling_guest.cpp',
      'goldfish_vk_transform_guest.cpp',
      'VkEncoder.cpp',
      'func_table.cpp',
    ],
    env: {'GFXSTREAM_NO_CLANG_FMT': '1', 'CEREAL_VARIANT' : 'guest',
          'GFXSTREAM_GUEST_ENCODER_DIR': guest_enc_path},
    input: [genvk, vk_api_xml, vk_gfxstream_xml],
    command: [prog_python, '@INPUT0@', '-registry', '@INPUT1@', '-registryGfxstream', '@INPUT2@',
              'cereal', '-o', output_host_path],
)

vulkan_gfxstream_h = custom_target(
    'vulkan_gfxstream_header',
    output : [
      'vulkan_gfxstream.h',
    ],
    env: {'GFXSTREAM_NO_CLANG_FMT': '1', 'CEREAL_VARIANT' : 'guest'},
    input: [genvk, vk_gfxstream_xml],
    command: [prog_python, '@INPUT0@', '-registry', '@INPUT1@', 'vulkan_gfxstream.h',
              '-o', guest_enc_path],
)

gfxstream_vk_entrypoints = custom_target(
  'gfxstream_vk_entrypoints',
  input : [vk_entrypoints_gen, vk_api_xml],
  output : ['gfxstream_vk_entrypoints.h', 'gfxstream_vk_entrypoints.c'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'gfxstream_vk',
    '--beta', with_vulkan_beta.to_string()
  ],
)

files_lib_vulkan_enc = files(
  'CommandBufferStagingStream.cpp',
  'DescriptorSetVirtualization.cpp',
  'HostVisibleMemoryVirtualization.cpp',
  'ResourceTracker.cpp',
  'Resources.cpp',
  'Validation.cpp',
  'VulkanHandleMapping.cpp',
  'VulkanStreamGuest.cpp',
  'gfxstream_vk_private.cpp',
  'GfxStreamVulkanConnection.cpp',
)
