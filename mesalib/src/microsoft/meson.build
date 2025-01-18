# Copyright © Microsoft Corporation
# SPDX-License-Identifier: MIT

subdir('compiler')
if with_microsoft_clc
  subdir('clc')
endif
if with_spirv_to_dxil or with_microsoft_vk
  subdir('spirv_to_dxil')
endif
if with_microsoft_vk
  subdir('vulkan')
endif
