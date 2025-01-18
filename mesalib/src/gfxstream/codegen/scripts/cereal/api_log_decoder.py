# Copyright 2023 Google LLC
# SPDX-License-Identifier: MIT

import os
from typing import List, Set, Dict, Optional

from . import VulkanType, VulkanCompoundType
from .wrapperdefs import VulkanWrapperGenerator


class ApiLogDecoder(VulkanWrapperGenerator):
    """
    This class generates decoding logic for the graphics API logs captured by
    [GfxApiLogger](http://source/play-internal/battlestar/aosp/device/generic/vulkan-cereal/base/GfxApiLogger.h)

    This allows developers to see a pretty-printed version of the API log data when using
    print_gfx_logs.py
    """

    # List of Vulkan APIs that we will generate decoding logic for
    generated_apis = [
        "vkAcquireImageANDROID",
        "vkAllocateMemory",
        "vkBeginCommandBufferAsyncGOOGLE",
        "vkBindBufferMemory",
        "vkBindImageMemory",
        "vkCmdBeginRenderPass",
        "vkCmdBindDescriptorSets",
        "vkCmdBindIndexBuffer",
        "vkCmdBindPipeline",
        "vkCmdBindVertexBuffers",
        "vkCmdClearAttachments",
        "vkCmdClearColorImage",
        "vkCmdCopyBufferToImage",
        "vkCmdCopyImageToBuffer",
        "vkCmdDraw",
        "vkCmdDrawIndexed",
        "vkCmdEndRenderPass",
        "vkCmdPipelineBarrier",
        "vkCmdPipelineBarrier2",
        "vkCmdSetScissor",
        "vkCmdSetViewport",
        "vkCollectDescriptorPoolIdsGOOGLE",
        "vkCreateBufferWithRequirementsGOOGLE",
        "vkCreateDescriptorPool",
        "vkCreateDescriptorSetLayout",
        "vkCreateFence",
        "vkCreateFramebuffer",
        "vkCreateGraphicsPipelines",
        "vkCreateImageView",
        "vkCreateImageWithRequirementsGOOGLE",
        "vkCreatePipelineCache",
        "vkCreateRenderPass",
        "vkCreateSampler",
        "vkCreateSemaphore",
        "vkCreateShaderModule",
        "vkDestroyBuffer",
        "vkDestroyCommandPool",
        "vkDestroyDescriptorPool",
        "vkDestroyDescriptorSetLayout",
        "vkDestroyDevice",
        "vkDestroyFence",
        "vkDestroyFramebuffer",
        "vkDestroyImage",
        "vkDestroyImageView",
        "vkDestroyInstance",
        "vkDestroyPipeline",
        "vkDestroyPipelineCache",
        "vkDestroyPipelineLayout",
        "vkDestroyRenderPass",
        "vkDestroySemaphore",
        "vkDestroyShaderModule",
        "vkEndCommandBufferAsyncGOOGLE",
        "vkFreeCommandBuffers",
        "vkFreeMemory",
        "vkFreeMemorySyncGOOGLE",
        "vkGetFenceStatus",
        "vkGetMemoryHostAddressInfoGOOGLE",
        "vkGetBlobGOOGLE",
        "vkGetPhysicalDeviceFormatProperties",
        "vkGetPhysicalDeviceProperties2KHR",
        "vkGetPipelineCacheData",
        "vkGetSemaphoreGOOGLE",
        "vkGetSwapchainGrallocUsageANDROID",
        "vkQueueCommitDescriptorSetUpdatesGOOGLE",
        "vkQueueFlushCommandsGOOGLE",
        "vkQueueSignalReleaseImageANDROIDAsyncGOOGLE",
        "vkQueueSubmitAsyncGOOGLE",
        "vkQueueWaitIdle",
        "vkResetFences",
        "vkWaitForFences",
    ]

    def __init__(self, module, typeInfo):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)
        self.typeInfo = typeInfo

        # Set of Vulkan structs that we need to write decoding logic for
        self.structs: Set[str] = set()

        # Maps enum group names to the list of enums in the group, for all enum groups in the spec
        # E.g.:  "VkResult": ["VK_SUCCESS", "VK_NOT_READY", "VK_TIMEOUT", etc...]
        self.all_enums: Dict[str, List[str]] = {}

        # Set of Vulkan enums that we need to write decoding logic for
        self.needed_enums: Set[str] = {"VkStructureType"}

    def onBegin(self):
        self.module.append("""
#####################################################################################################
# Pretty-printer functions for Vulkan data structures
# THIS FILE IS AUTO-GENERATED - DO NOT EDIT
#
# To re-generate this file, run generate-vulkan-sources.sh
#####################################################################################################

""".lstrip())

    def onGenGroup(self, groupinfo, groupName, alias=None):
        """Called for each enum group in the spec"""
        for enum in groupinfo.elem.findall("enum"):
            self.all_enums[groupName] = self.all_enums.get(groupName, []) + [enum.get('name')]

    def onEnd(self):
        for api_name in sorted(self.generated_apis):
            self.process_api(api_name)
        self.process_structs()
        self.process_enums()

    def process_api(self, api_name):
        """Main entry point to generate decoding logic for each Vulkan API"""
        api = self.typeInfo.apis[api_name]
        self.module.append('def OP_{}(printer, indent: int):\n'.format(api_name))

        # Decode the sequence number. All commands have sequence numbers, except those handled
        # by VkSubdecoder.cpp. The logic here is a bit of a hack since it's based on the command
        # name. Ideally, we would detect whether a particular command is part of a subdecode block
        # in the decoding script.
        if not api_name.startswith("vkCmd") and api_name != "vkBeginCommandBufferAsyncGOOGLE":
            self.module.append('    printer.write_int("seqno: ", 4, indent)\n')

        for param in api.parameters:
            # Add any structs that this API uses to the list of structs to write decoding logic for
            if self.typeInfo.isCompoundType(param.typeName):
                self.structs.add(param.typeName)

            # Don't try to print the pData field of vkQueueFlushCommandsGOOGLE, those are the
            # commands processed as part of the subdecode pass
            if api.name == "vkQueueFlushCommandsGOOGLE" and param.paramName == "pData":
                continue

            # Write out decoding logic for that parameter
            self.process_type(param)

        # Finally, add a return statement. This is needed in case the API has no parameters.
        self.module.append('    return\n\n')

    def process_structs(self):
        """Writes decoding logic for all the structs that we use"""

        # self.structs now contains all the structs used directly by the Vulkan APIs we use.
        # Recursively expand this set to add all the structs used by these structs.
        copy = self.structs.copy()
        self.structs.clear()
        for struct_name in copy:
            self.expand_needed_structs(struct_name)

        # Now we have the full list of structs that we need to write decoding logic for.
        # Write a decoder for each of them
        for struct_name in sorted(self.structs):
            struct = self.typeInfo.structs[struct_name]
            self.module.append('def struct_{}(printer, indent: int):\n'.format(struct_name))
            for member in self.get_members(struct):
                self.process_type(member)
            self.module.append('\n')

    def expand_needed_structs(self, struct_name: str):
        """
        Recursively adds all the structs used by a given struct to the list of structs to process
        """
        if struct_name in self.structs:
            return
        self.structs.add(struct_name)
        struct = self.typeInfo.structs[struct_name]
        for member in self.get_members(struct):
            if self.typeInfo.isCompoundType(member.typeName):
                self.expand_needed_structs(member.typeName)

    def get_members(self, struct: VulkanCompoundType):
        """
        Returns the members of a struct/union that we need to process.
        For structs, returns the list of all members
        For unions, returns a list with just the first member.
        """
        return struct.members[0:1] if struct.isUnion else struct.members

    def process_type(self, type: VulkanType):
        """
        Writes decoding logic for a single Vulkan type. This could be the parameter in a Vulkan API,
        or a struct member.
        """
        if type.typeName == "VkStructureType":
            self.module.append(
                '    printer.write_stype_and_pnext("{}", indent)\n'.format(
                    type.parent.structEnumExpr))
            return

        if type.isNextPointer():
            return

        if type.paramName == "commandBuffer":
            if type.parent.name != "vkQueueFlushCommandsGOOGLE":
                return

        # Enums
        if type.isEnum(self.typeInfo):
            self.needed_enums.add(type.typeName)
            self.module.append(
                '    printer.write_enum("{}", {}, indent)\n'.format(
                    type.paramName, type.typeName))
            return

        # Bitmasks
        if type.isBitmask(self.typeInfo):
            enum_type = self.typeInfo.bitmasks.get(type.typeName)
            if enum_type:
                self.needed_enums.add(enum_type)
                self.module.append(
                    '    printer.write_flags("{}", {}, indent)\n'.format(
                        type.paramName, enum_type))
                return
            # else, fall through and let the primitive type logic handle it

        # Structs or unions
        if self.typeInfo.isCompoundType(type.typeName):
            self.module.append(
                '    printer.write_struct("{name}", struct_{type}, {optional}, {count}, indent)\n'
                    .format(name=type.paramName,
                            type=type.typeName,
                            optional=type.isOptionalPointer(),
                            count=self.get_length_expression(type)))
            return

        # Null-terminated strings
        if type.isString():
            self.module.append('    printer.write_string("{}", None, indent)\n'.format(
                type.paramName))
            return

        # Arrays of primitive types
        if type.staticArrExpr and type.primitiveEncodingSize and type.primitiveEncodingSize <= 8:
            # Array sizes are specified either as a number, or as an enum value
            array_size = int(type.staticArrExpr) if type.staticArrExpr.isdigit() \
                else self.typeInfo.enumValues.get(type.staticArrExpr)
            assert array_size is not None, type.staticArrExpr

            if type.typeName == "char":
                self.module.append(
                    '    printer.write_string("{}", {}, indent)\n'.format(
                        type.paramName, array_size))
            elif type.typeName == "float":
                self.module.append(
                    '    printer.write_float("{}", indent, count={})\n'
                        .format(type.paramName, array_size))
            else:
                self.module.append(
                    '    printer.write_int("{name}", {int_size}, indent, signed={signed}, count={array_size})\n'
                        .format(name=type.paramName,
                                array_size=array_size,
                                int_size=type.primitiveEncodingSize,
                                signed=type.isSigned()))
            return

        # Pointers
        if type.pointerIndirectionLevels > 0:
            # Assume that all uint32* are always serialized directly rather than passed by pointers.
            # This is probably not always true (e.g. out params) - fix this as needed.
            size = 4 if type.primitiveEncodingSize == 4 else 8
            self.module.append(
                '    {name} = printer.write_int("{name}", {size}, indent, optional={opt}, count={count}, big_endian={big_endian})\n'
                    .format(name=type.paramName,
                            size=size,
                            opt=type.isOptionalPointer(),
                            count=self.get_length_expression(type),
                            big_endian=self.using_big_endian(type)))
            return

        # Primitive types (ints, floats)
        if type.isSimpleValueType(self.typeInfo) and type.primitiveEncodingSize:
            if type.typeName == "float":
                self.module.append(
                    '    printer.write_float("{name}", indent)\n'.format(name=type.paramName))
            else:
                self.module.append(
                    '    {name} = printer.write_int("{name}", {size}, indent, signed={signed}, big_endian={big_endian})\n'.format(
                        name=type.paramName,
                        size=type.primitiveEncodingSize,
                        signed=type.isSigned(),
                        big_endian=self.using_big_endian(type))
                )
            return

        raise NotImplementedError(
            "No decoding logic for {} {}".format(type.typeName, type.paramName))

    def using_big_endian(self, type: VulkanType):
        """For some reason gfxstream serializes some types as big endian"""
        return type.typeName == "size_t"

    def get_length_expression(self, type: VulkanType) -> Optional[str]:
        """Returns the length expression for a given type"""
        if type.lenExpr is None:
            return None

        if type.lenExpr.isalpha():
            return type.lenExpr

        # There are a couple of instances in the spec where we use a math expression to express the
        # length (e.g. VkPipelineMultisampleStateCreateInfo). CodeGen().generalLengthAccess() has
        # logic o parse these expressions correctly, but for now,we just use a simple lookup table.
        known_expressions = {
            r"latexmath:[\lceil{\mathit{rasterizationSamples} \over 32}\rceil]":
                "int(rasterizationSamples / 32)",
            r"latexmath:[\textrm{codeSize} \over 4]": "int(codeSize / 4)",
            r"null-terminated": None
        }
        if type.lenExpr in known_expressions:
            return known_expressions[type.lenExpr]

        raise NotImplementedError("Unknown length expression: " + type.lenExpr)

    def process_enums(self):
        """
        For each Vulkan enum that we use, write out a python dictionary mapping the enum values back
        to the enum name as a string
        """
        for enum_name in sorted(self.needed_enums):
            self.module.append('{} = {{\n'.format(enum_name))
            for identifier in self.all_enums[enum_name]:
                value = self.typeInfo.enumValues.get(identifier)
                if value is not None and isinstance(value, int):
                    self.module.append('    {}: "{}",\n'.format(value, identifier))
            self.module.append('}\n\n')
