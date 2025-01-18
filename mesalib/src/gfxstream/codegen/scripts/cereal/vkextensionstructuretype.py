# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT

from .wrapperdefs import VulkanWrapperGenerator


class VulkanExtensionStructureType(VulkanWrapperGenerator):
    def __init__(self, extensionName: str, module, typeInfo):
        super().__init__(module, typeInfo)
        self._extensionName = extensionName

    def onGenGroup(self, groupinfo, groupName, alias=None):
        super().onGenGroup(groupinfo, groupName, alias)
        elem = groupinfo.elem
        if (not elem.get('type') == 'enum'):
            return
        if (not elem.get('name') == 'VkStructureType'):
            return
        extensionEnumFactoryMacro = f'{self._extensionName.upper()}_ENUM'
        for enum in elem.findall(f"enum[@extname='{self._extensionName}']"):
            name = enum.get('name')
            offset = enum.get('offset')
            self.module.appendHeader(
                f"#define {name} {extensionEnumFactoryMacro}(VkStructureType, {offset})\n")


class VulkanGfxstreamStructureType(VulkanExtensionStructureType):
    def __init__(self, module, typeInfo):
        super().__init__('VK_GOOGLE_gfxstream', module, typeInfo)


class VulkanAndroidNativeBufferStructureType(VulkanExtensionStructureType):
    def __init__(self, module, typeInfo):
        super().__init__('VK_ANDROID_native_buffer', module, typeInfo)
