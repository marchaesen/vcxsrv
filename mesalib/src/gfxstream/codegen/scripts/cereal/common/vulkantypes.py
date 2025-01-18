# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT
from typing import Dict, Optional, List, Set, Union
from xml.etree.ElementTree import Element

from generator import noneStr

from copy import copy
from dataclasses import dataclass
from string import whitespace

# Holds information about core Vulkan objects
# and the API calls that are used to create/destroy each one.
class HandleInfo(object):
    def __init__(self, name, createApis, destroyApis):
        self.name = name
        self.createApis = createApis
        self.destroyApis = destroyApis

    def isCreateApi(self, apiName):
        return apiName == self.createApis or (apiName in self.createApis)

    def isDestroyApi(self, apiName):
        if self.destroyApis is None:
            return False
        return apiName == self.destroyApis or (apiName in self.destroyApis)

DISPATCHABLE_HANDLE_TYPES = [
    "VkInstance",
    "VkPhysicalDevice",
    "VkDevice",
    "VkQueue",
    "VkCommandBuffer",
]

NON_DISPATCHABLE_HANDLE_TYPES = [
    "VkDeviceMemory",
    "VkBuffer",
    "VkBufferView",
    "VkImage",
    "VkImageView",
    "VkShaderModule",
    "VkDescriptorPool",
    "VkDescriptorSetLayout",
    "VkDescriptorSet",
    "VkSampler",
    "VkPipeline",
    "VkPipelineLayout",
    "VkRenderPass",
    "VkFramebuffer",
    "VkPipelineCache",
    "VkCommandPool",
    "VkFence",
    "VkSemaphore",
    "VkEvent",
    "VkQueryPool",
    "VkSamplerYcbcrConversion",
    "VkSamplerYcbcrConversionKHR",
    "VkDescriptorUpdateTemplate",
    "VkSurfaceKHR",
    "VkSwapchainKHR",
    "VkDisplayKHR",
    "VkDisplayModeKHR",
    "VkObjectTableNVX",
    "VkIndirectCommandsLayoutNVX",
    "VkValidationCacheEXT",
    "VkDebugReportCallbackEXT",
    "VkDebugUtilsMessengerEXT",
    "VkAccelerationStructureNV",
    "VkIndirectCommandsLayoutNV",
    "VkAccelerationStructureKHR",
    "VkPrivateDataSlot",
]

CUSTOM_HANDLE_CREATE_TYPES = [
    "VkPhysicalDevice",
    "VkQueue",
    "VkPipeline",
    "VkDeviceMemory",
    "VkDescriptorSet",
    "VkCommandBuffer",
    "VkRenderPass",
]

HANDLE_TYPES = list(sorted(list(set(DISPATCHABLE_HANDLE_TYPES +
                                    NON_DISPATCHABLE_HANDLE_TYPES + CUSTOM_HANDLE_CREATE_TYPES))))

HANDLE_INFO = {}

for h in HANDLE_TYPES:
    if h in CUSTOM_HANDLE_CREATE_TYPES:
        if h == "VkPhysicalDevice":
            HANDLE_INFO[h] = \
                HandleInfo(
                    "VkPhysicalDevice",
                    "vkEnumeratePhysicalDevices", None)
        if h == "VkQueue":
            HANDLE_INFO[h] = \
                HandleInfo(
                    "VkQueue",
                    ["vkGetDeviceQueue", "vkGetDeviceQueue2"],
                    None)
        if h == "VkPipeline":
            HANDLE_INFO[h] = \
                HandleInfo(
                    "VkPipeline",
                    ["vkCreateGraphicsPipelines", "vkCreateComputePipelines"],
                    "vkDestroyPipeline")
        if h == "VkDeviceMemory":
            HANDLE_INFO[h] = \
                HandleInfo("VkDeviceMemory",
                           "vkAllocateMemory", ["vkFreeMemory", "vkFreeMemorySyncGOOGLE"])
        if h == "VkDescriptorSet":
            HANDLE_INFO[h] = \
                HandleInfo("VkDescriptorSet", "vkAllocateDescriptorSets",
                           "vkFreeDescriptorSets")
        if h == "VkCommandBuffer":
            HANDLE_INFO[h] = \
                HandleInfo("VkCommandBuffer", "vkAllocateCommandBuffers",
                           "vkFreeCommandBuffers")
        if h == "VkRenderPass":
            HANDLE_INFO[h] = \
                HandleInfo(
                    "VkRenderPass",
                    ["vkCreateRenderPass", "vkCreateRenderPass2", "vkCreateRenderPass2KHR"],
                    "vkDestroyRenderPass")
    else:
        HANDLE_INFO[h] = \
            HandleInfo(h, "vkCreate" + h[2:], "vkDestroy" + h[2:])

EXCLUDED_APIS = [
    "vkEnumeratePhysicalDeviceGroups",
]

EXPLICITLY_ABI_PORTABLE_TYPES = [
    "VkResult",
    "VkBool32",
    "VkSampleMask",
    "VkFlags",
    "VkDeviceSize",
]

EXPLICITLY_ABI_NON_PORTABLE_TYPES = [
    "size_t"
]

NON_ABI_PORTABLE_TYPE_CATEGORIES = [
    "handle",
    "funcpointer",
]

# A class for holding the parameter indices corresponding to various
# attributes about a VkDeviceMemory, such as the handle, size, offset, etc.
@dataclass
class DeviceMemoryInfoParameterIndices:
    handle: int = -1
    offset: int = -1
    size: int = -1
    typeIndex: int = -1
    typeBits: int = -1

DEVICE_MEMORY_STRUCTS = {
    "VkMemoryAllocateInfo": {"1": DeviceMemoryInfoParameterIndices(typeIndex = 3)},
    "VkMemoryRequirements": {"1": DeviceMemoryInfoParameterIndices(typeBits = 2)},
    "VkMappedMemoryRange": {"1": DeviceMemoryInfoParameterIndices(handle = 2, offset = 3, size = 4)},
    "VkSparseMemoryBind": {"1": DeviceMemoryInfoParameterIndices(handle = 2, offset = 3)},
    "VkSparseImageMemoryBind": {"1": DeviceMemoryInfoParameterIndices(handle = 3, offset = 4)},
    "VkWin32KeyedMutexAcquireReleaseInfoNV": {"1": DeviceMemoryInfoParameterIndices(handle = 3), "2": DeviceMemoryInfoParameterIndices(handle = 7)},
    "VkMemoryWin32HandlePropertiesKHR": {"1": DeviceMemoryInfoParameterIndices(typeBits = 2)},
    "VkMemoryGetWin32HandleInfoKHR": {"1": DeviceMemoryInfoParameterIndices(handle = 2)},
    "VkMemoryFdPropertiesKHR": {"1": DeviceMemoryInfoParameterIndices(typeBits = 2)},
    "VkMemoryGetFdInfoKHR": {"1": DeviceMemoryInfoParameterIndices(handle = 2)},
    "VkWin32KeyedMutexAcquireReleaseInfoKHR": {"1": DeviceMemoryInfoParameterIndices(handle = 3), "2": DeviceMemoryInfoParameterIndices(handle = 7)},
    "VkBindBufferMemoryInfo": {"1": DeviceMemoryInfoParameterIndices(handle = 3, offset = 4)},
    "VkBindImageMemoryInfo": {"1": DeviceMemoryInfoParameterIndices(handle = 3, offset = 4)},
    "VkMemoryHostPointerPropertiesEXT": {"1": DeviceMemoryInfoParameterIndices(typeBits = 2)},
    "VkAndroidHardwareBufferPropertiesANDROID": {"1": DeviceMemoryInfoParameterIndices(typeBits = 3)},
    "VkMemoryGetAndroidHardwareBufferInfoANDROID": {"1": DeviceMemoryInfoParameterIndices(handle = 2)},
    "VkBindAccelerationStructureMemoryInfoNV": {"1": DeviceMemoryInfoParameterIndices(handle = 3, offset = 4)},
    "VkDeviceMemoryOpaqueCaptureAddressInfo": {"1": DeviceMemoryInfoParameterIndices(handle = 2)},
}

DEVICE_MEMORY_COMMANDS = {
    "vkFreeMemory": {"1": DeviceMemoryInfoParameterIndices(handle = 1)},
    "vkMapMemory": {"1": DeviceMemoryInfoParameterIndices(handle = 1)},
    "vkUnmapMemory": {"1": DeviceMemoryInfoParameterIndices(handle = 1)},
    "vkGetDeviceMemoryCommitment": {"1": DeviceMemoryInfoParameterIndices(handle = 1, offset = 2)},
    "vkBindBufferMemory": {"1": DeviceMemoryInfoParameterIndices(handle = 2, offset = 3)},
    "vkBindImageMemory": {"1": DeviceMemoryInfoParameterIndices(handle = 2, offset = 3)},
    "vkGetBlobGOOGLE": {"1": DeviceMemoryInfoParameterIndices(handle = 1)},
    "vkGetMemoryWin32HandleNV": {"1": DeviceMemoryInfoParameterIndices(handle = 1)},
    "vkMapMemoryIntoAddressSpaceGOOGLE": {"1": DeviceMemoryInfoParameterIndices(handle = 1)},
    "vkGetMemoryHostAddressInfoGOOGLE": {"1": DeviceMemoryInfoParameterIndices(handle = 1)},
    "vkFreeMemorySyncGOOGLE": {"1": DeviceMemoryInfoParameterIndices(handle = 1)},
}

TRIVIAL_TRANSFORMED_TYPES = [
    "VkPhysicalDeviceExternalImageFormatInfo",
    "VkPhysicalDeviceExternalBufferInfo",
    "VkExternalMemoryImageCreateInfo",
    "VkExternalMemoryBufferCreateInfo",
    "VkExportMemoryAllocateInfo",
    "VkExternalImageFormatProperties",
    "VkExternalBufferProperties",
]

NON_TRIVIAL_TRANSFORMED_TYPES = [
    "VkExternalMemoryProperties",
    "VkImageCreateInfo",
]

TRANSFORMED_TYPES = TRIVIAL_TRANSFORMED_TYPES + NON_TRIVIAL_TRANSFORMED_TYPES

STRUCT_STREAM_FEATURE = {
    "VkPhysicalDeviceShaderFloat16Int8Features": "VULKAN_STREAM_FEATURE_SHADER_FLOAT16_INT8_BIT",
    "VkPhysicalDeviceShaderFloat16Int8FeaturesKHR": "VULKAN_STREAM_FEATURE_SHADER_FLOAT16_INT8_BIT",
    "VkPhysicalDeviceFloat16Int8FeaturesKHR": "VULKAN_STREAM_FEATURE_SHADER_FLOAT16_INT8_BIT",
}

STRUCT_MEMBER_STREAM_FEATURE = {
    "VkGraphicsPipelineCreateInfo.pVertexInputState": "VULKAN_STREAM_FEATURE_IGNORED_HANDLES_BIT",
    "VkGraphicsPipelineCreateInfo.pInputAssemblyState": "VULKAN_STREAM_FEATURE_IGNORED_HANDLES_BIT",
    "VkGraphicsPipelineCreateInfo.pRasterizationState": "VULKAN_STREAM_FEATURE_IGNORED_HANDLES_BIT",
}

STRUCT_ENV_STR = {
    "VkGraphicsPipelineCreateInfo": {
        "hasTessellation": "(arrayany pStages 0 stageCount (lambda ((s VkPipelineShaderStageCreateInfo)) (or (eq (getfield s stage) VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) (eq (getfield s stage) VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT))))",
        "hasRasterization" : "(or (if (eq 0 pRasterizationState) 0 (not (getfield pRasterizationState rasterizerDiscardEnable))) (if (eq 0 pDynamicState) 0 (arrayany (getfield pDynamicState pDynamicStates) 0 (getfield pDynamicState dynamicStateCount) (lambda ((s VkDynamicState)) (eq s VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE)))))"
    },
}

STRUCT_MEMBER_FILTER_VAR = {
    "VkGraphicsPipelineCreateInfo.pTessellationState": "hasTessellation",
    "VkGraphicsPipelineCreateInfo.pViewportState": "hasRasterization",
    "VkGraphicsPipelineCreateInfo.pMultisampleState": "hasRasterization",
    "VkGraphicsPipelineCreateInfo.pDepthStencilState": "hasRasterization",
    "VkGraphicsPipelineCreateInfo.pColorBlendState": "hasRasterization",
    "VkWriteDescriptorSet.pImageInfo": "descriptorType",
    "VkWriteDescriptorSet.pBufferInfo": "descriptorType",
    "VkWriteDescriptorSet.pTexelBufferView": "descriptorType",
    "VkFramebufferCreateInfo.pAttachments": "flags",
}

STRUCT_MEMBER_FILTER_VALS = {
    "VkWriteDescriptorSet.pImageInfo": [
        "VK_DESCRIPTOR_TYPE_SAMPLER",
        "VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER",
        "VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE",
        "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE",
        "VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT"
    ],
    "VkWriteDescriptorSet.pBufferInfo": [
        "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER",
        "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC",
        "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER",
        "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC",
    ],
    "VkWriteDescriptorSet.pTexelBufferView": [
        "VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER",
        "VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER",
    ],
}

STRUCT_MEMBER_FILTER_FUNC = {
    "VkFramebufferCreateInfo.pAttachments": "(eq (bitwise_and flags VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT) 0)",
}

# vk.xml added optional to some of the existing fields. For backward compatibility
# we need to ignore those optionals.
# We might want to add more complex safety checks in future.
STRUCT_MEMBER_IGNORE_OPTIONAL = {
    "VkSubmitInfo.pWaitDstStageMask",
    "VkPipelineLayoutCreateInfo.pSetLayouts",
    "VkGraphicsPipelineCreateInfo.pStages",
    "VkPipelineColorBlendStateCreateInfo.pAttachments",
    "VkFramebufferCreateInfo.attachmentCount",
    "VkFramebufferCreateInfo.pAttachments",
    "VkVideoProfileInfoKHR.chromaBitDepth",
    "VkVideoDecodeInfoKHR.pSetupReferenceSlot",
    "vkCmdBindDescriptorSets.pDescriptorSets",
    "vkCmdBindDescriptorSets.local_pDescriptorSets",
    "vkCmdBindVertexBuffers.pBuffers",
    "vkCmdBindVertexBuffers.local_pBuffers",
    "vkCmdClearColorImage.pColor",
    "vkCmdClearColorImage.local_pColor",
}

# Holds information about a Vulkan type instance (i.e., not a type definition).
# Type instances are used as struct field definitions or function parameters,
# to be later fed to code generation.
# VulkanType instances can be constructed in two ways:
# 1. From an XML tag with <type> / <param> tags in vk.xml,
#    using makeVulkanTypeFromXMLTag
# 2. User-defined instances with makeVulkanTypeSimple.
class VulkanType(object):

    def __init__(self):
        self.parent: Optional[VulkanType] = None
        self.typeName: str = ""

        self.isTransformed = False

        self.paramName: Optional[str] = None

        self.lenExpr: Optional[str] = None  # Value of the `len` attribute in the spec
        self.isOptional: bool = False
        self.optionalStr: Optional[str] = None  # Value of the `optional` attribute in the spec

        self.isConst = False

        # "" means it's not a static array, otherwise this is the total size of
        # all elements. e.g. staticArrExpr of "x[3][2][8]" will be "((3)*(2)*(8))".
        self.staticArrExpr = ""
        # "" means it's not a static array, otherwise it's the raw expression
        # of static array size, which can be one-dimensional or multi-dimensional.
        self.rawStaticArrExpr = ""

        self.pointerIndirectionLevels = 0  # 0 means not pointer
        self.isPointerToConstPointer = False

        self.primitiveEncodingSize = None

        self.deviceMemoryInfoParameterIndices = None

        # Annotations
        # Environment annotation for binding current
        # variables to sub-structures
        self.binds = {}

        # Device memory annotations

        # self.deviceMemoryAttrib/Val stores
        # device memory info attributes
        self.deviceMemoryAttrib = None
        self.deviceMemoryVal = None

        # Filter annotations
        self.filterVar = None
        self.filterVals = None
        self.filterFunc = None
        self.filterOtherwise = None

        # Stream feature
        self.streamFeature = None

        # All other annotations
        self.attribs = {}

        self.nonDispatchableHandleCreate = False
        self.nonDispatchableHandleDestroy = False
        self.dispatchHandle = False
        self.dispatchableHandleCreate = False
        self.dispatchableHandleDestroy = False


    def __str__(self,):
        return ("(vulkantype %s %s paramName %s len %s optional? %s "
                "staticArrExpr %s)") % (
            self.typeName + ("*" * self.pointerIndirectionLevels) +
            ("ptr2constptr" if self.isPointerToConstPointer else ""), "const"
            if self.isConst else "nonconst", self.paramName, self.lenExpr,
            self.isOptional, self.staticArrExpr)

    def isString(self):
        return self.pointerIndirectionLevels == 1 and (self.typeName == "char")

    def isArrayOfStrings(self):
        return self.isPointerToConstPointer and (self.typeName == "char")

    def primEncodingSize(self):
        return self.primitiveEncodingSize

    # Utility functions to make codegen life easier.
    # This method derives the correct "count" expression if possible.
    # Otherwise, returns None or "null-terminated" if a string.
    def getLengthExpression(self):
        if self.staticArrExpr != "":
            return self.staticArrExpr
        if self.lenExpr:
            # Use a simple lookup table for latexmath.
            known_expressions = {
                r"latexmath:[\lceil{\mathit{samples} \over 32}\rceil]":
                    "int(samples / 32)",
                r"latexmath:[2 \times \mathtt{VK\_UUID\_SIZE}]": "2 * VK_UUID_SIZE",
            }
            if self.lenExpr in known_expressions:
                return known_expressions[self.lenExpr]
            return self.lenExpr
        return None

    # Can we just pass this to functions expecting T*
    def accessibleAsPointer(self):
        if self.staticArrExpr != "":
            return True
        if self.pointerIndirectionLevels > 0:
            return True
        return False

    # Rough attempt to infer where a type could be an output.
    # Good for inferring which things need to be marshaled in
    # versus marshaled out for Vulkan API calls
    def possiblyOutput(self,):
        return self.pointerIndirectionLevels > 0 and (not self.isConst)

    def isVoidWithNoSize(self,):
        return self.typeName == "void" and self.pointerIndirectionLevels == 0

    def getCopy(self,):
        return copy(self)

    def getTransformed(self, isConstChoice=None, ptrIndirectionChoice=None):
        res = self.getCopy()

        if isConstChoice is not None:
            res.isConst = isConstChoice
        if ptrIndirectionChoice is not None:
            res.pointerIndirectionLevels = ptrIndirectionChoice

        return res

    def getWithCustomName(self):
        return self.getTransformed(
            ptrIndirectionChoice=self.pointerIndirectionLevels + 1)

    def getForAddressAccess(self):
        return self.getTransformed(
            ptrIndirectionChoice=self.pointerIndirectionLevels + 1)

    def getForValueAccess(self):
        if self.typeName == "void" and self.pointerIndirectionLevels == 1:
            asUint8Type = self.getCopy()
            asUint8Type.typeName = "uint8_t"
            return asUint8Type.getForValueAccess()
        return self.getTransformed(
            ptrIndirectionChoice=self.pointerIndirectionLevels - 1)

    def getForNonConstAccess(self):
        return self.getTransformed(isConstChoice=False)

    def withModifiedName(self, newName):
        res = self.getCopy()
        res.paramName = newName
        return res

    def isNextPointer(self):
        return self.paramName == "pNext"

    def isSigned(self):
        return self.typeName in ["int", "int8_t", "int16_t", "int32_t", "int64_t"]

    def isEnum(self, typeInfo):
        return typeInfo.categoryOf(self.typeName) == "enum"

    def isBitmask(self, typeInfo):
        return typeInfo.categoryOf(self.typeName) == "enum"

    # Only deals with 'core' handle types here.
    def isDispatchableHandleType(self):
        return self.typeName in DISPATCHABLE_HANDLE_TYPES

    def isNonDispatchableHandleType(self):
        return self.typeName in NON_DISPATCHABLE_HANDLE_TYPES

    def isHandleType(self):
        return self.isDispatchableHandleType() or \
               self.isNonDispatchableHandleType()

    def isCreatedBy(self, api):
        if self.shouldSkip():
            return False
        if self.typeName in HANDLE_INFO.keys():
            nonKhrRes = HANDLE_INFO[self.typeName].isCreateApi(api.name)
            if nonKhrRes:
                return True
            if len(api.name) > 3 and "KHR" == api.name[-3:]:
                return HANDLE_INFO[self.typeName].isCreateApi(api.name[:-3])

        if self.typeName == "VkImage" and api.name == "vkCreateImageWithRequirementsGOOGLE":
            return True

        if self.typeName == "VkBuffer" and api.name == "vkCreateBufferWithRequirementsGOOGLE":
            return True

        return False

    def isDestroyedBy(self, api):
        if self.shouldSkip():
            return False
        if self.typeName in HANDLE_INFO.keys():
            nonKhrRes = HANDLE_INFO[self.typeName].isDestroyApi(api.name)
            if nonKhrRes:
                return True
            if len(api.name) > 3 and "KHR" == api.name[-3:]:
                return HANDLE_INFO[self.typeName].isDestroyApi(api.name[:-3])

        return False

    def isSimpleValueType(self, typeInfo):
        if typeInfo.isCompoundType(self.typeName):
            return False
        if self.isString() or self.isArrayOfStrings():
            return False
        if self.staticArrExpr or self.pointerIndirectionLevels > 0:
            return False
        return True

    def getStructEnumExpr(self,):
        return None

    def getPrintFormatSpecifier(self):
        kKnownTypePrintFormatSpecifiers = {
            'float': '%f',
            'int': '%d',
            'int32_t': '%d',
            'size_t': '%ld',
            'uint16_t': '%d',
            'uint32_t': '%d',
            'uint64_t': '%ld',
            'VkBool32': '%d',
            'VkDeviceSize': '%ld',
            'VkFormat': '%d',
            'VkImageLayout': '%d',
        }

        if self.pointerIndirectionLevels > 0 or self.isHandleType():
            return '%p'

        if self.typeName in kKnownTypePrintFormatSpecifiers:
            return kKnownTypePrintFormatSpecifiers[self.typeName]

        if self.typeName.endswith('Flags'):
            # Based on `typedef uint32_t VkFlags;`
            return '%d'

        return None
    def isOptionalPointer(self) -> bool:
        return self.isOptional and \
               (not self.isForceOptional()) and\
               self.pointerIndirectionLevels > 0 and \
               (not self.isNextPointer())

    def isForceOptional(self) -> bool:
        """
        Returns true if we should generate a placeholder for null.

        Vulkan updates change certain pointers from non-optional to
        optional. We want to keep our encoder/decoder backward compatible.
        Thus we should generate a placeholder for such APIs.
        """
        return self.getFullName() in STRUCT_MEMBER_IGNORE_OPTIONAL

    def getFullName(self) -> str:
        if self.parent is None:
            return self.paramName
        return f"{self.parent.name}.{self.paramName}" 

    def getProtectStreamFeature(self) -> Optional[str]:
        key = self.getFullName()
        if key in STRUCT_MEMBER_STREAM_FEATURE.keys():
            return STRUCT_MEMBER_STREAM_FEATURE[key]
        return None

    def shouldSkip(self) -> bool:
        return ("api" in self.attribs.keys()
                and not "vulkan" == self.attribs["api"])

# Is an S-expression w/ the following spec:
# From https://gist.github.com/pib/240957
class Atom(object):
    def __init__(self, name):
        self.name = name
    def __repr__(self,):
        return self.name

def parse_sexp(sexp):
    atom_end = set('()"\'') | set(whitespace)
    stack, i, length = [[]], 0, len(sexp)
    while i < length:
        c = sexp[i]

        reading = type(stack[-1])
        if reading == list:
            if   c == '(': stack.append([])
            elif c == ')':
                stack[-2].append(stack.pop())
                if stack[-1][0] == ('quote',): stack[-2].append(stack.pop())
            elif c == '"': stack.append('')
            elif c == "'": stack.append([('quote',)])
            elif c in whitespace: pass
            else: stack.append(Atom(c))
        elif reading == str:
            if   c == '"':
                stack[-2].append(stack.pop())
                if stack[-1][0] == ('quote',): stack[-2].append(stack.pop())
            elif c == '\\':
                i += 1
                stack[-1] += sexp[i]
            else: stack[-1] += c
        elif reading == Atom:
            if c in atom_end:
                atom = stack.pop()
                if atom.name[0].isdigit(): stack[-1].append(eval(atom.name))
                else: stack[-1].append(atom)
                if stack[-1][0] == ('quote',): stack[-2].append(stack.pop())
                continue
            else: stack[-1] = Atom(stack[-1].name + c)
        i += 1

    return stack.pop()

class FuncExprVal(object):
    def __init__(self, val):
        self.val = val
    def __repr__(self,):
        return self.val.__repr__()

class FuncExpr(object):
    def __init__(self, name, args):
        self.name = name
        self.args = args
    def __repr__(self,):
        if len(self.args) == 0:
            return "(%s)" % (self.name.__repr__())
        else:
            return "(%s %s)" % (self.name.__repr__(), " ".join(map(lambda x: x.__repr__(), self.args)))

class FuncLambda(object):
    def __init__(self, vs, body):
        self.vs = vs
        self.body = body
    def __repr__(self,):
        return "(L (%s) %s)" % (" ".join(map(lambda x: x.__repr__(), self.vs)), self.body.__repr__())

class FuncLambdaParam(object):
    def __init__(self, name, typ):
        self.name = name
        self.typ = typ
    def __repr__(self,):
        return "%s : %s" % (self.name, self.typ)

def parse_func_expr(parsed_sexp):
    if len(parsed_sexp) != 1:
        print("Error: parsed # expressions != 1: %d" % (len(parsed_sexp)))
        raise

    e = parsed_sexp[0]

    def parse_lambda_param(e):
        return FuncLambdaParam(e[0].name, e[1].name)

    def parse_one(exp):
        if list == type(exp):
            if "lambda" == exp[0].__repr__():
                return FuncLambda(list(map(parse_lambda_param, exp[1])), parse_one(exp[2]))
            else:
                return FuncExpr(exp[0], list(map(parse_one, exp[1:])))
        else:
            return FuncExprVal(exp)

    return parse_one(e)

def parseFilterFuncExpr(expr):
    res = parse_func_expr(parse_sexp(expr))
    return res

def parseLetBodyExpr(expr):
    res = parse_func_expr(parse_sexp(expr))
    return res

def makeVulkanTypeFromXMLTag(typeInfo, parentName: str, tag: Element) -> VulkanType:
    res = VulkanType()

    # Process the length expression

    if tag.attrib.get("len") is not None:
        lengths = tag.attrib.get("len").split(",")
        res.lenExpr = lengths[0]

    # Calculate static array expression

    nametag = tag.find("name")
    enumtag = tag.find("enum")

    if enumtag is not None:
        res.staticArrExpr = enumtag.text
    elif nametag is not None:
        res.rawStaticArrExpr = noneStr(nametag.tail)

        dimensions = res.rawStaticArrExpr.count('[')
        if dimensions == 1:
            res.staticArrExpr = res.rawStaticArrExpr[1:-1]
        elif dimensions > 1:
            arraySizes = res.rawStaticArrExpr[1:-1].split('][')
            res.staticArrExpr = '(' + \
                '*'.join(f'({size})' for size in arraySizes) + ')'

    # Determine const

    beforeTypePart = noneStr(tag.text)

    if "const" in beforeTypePart:
        res.isConst = True

    # Calculate type and pointer info
    for elem in tag:
        if elem.tag == "name":
            res.paramName = elem.text
        if elem.tag == "type":
            duringTypePart = noneStr(elem.text)
            afterTypePart = noneStr(elem.tail)
            # Now we know enough to fill some stuff in
            res.typeName = duringTypePart

            if res.typeName in TRANSFORMED_TYPES:
                res.isTransformed = True

            # This only handles pointerIndirectionLevels == 2
            # along with optional constant pointer for the inner part.
            for c in afterTypePart:
                if c == "*":
                    res.pointerIndirectionLevels += 1
            if "const" in afterTypePart and res.pointerIndirectionLevels == 2:
                res.isPointerToConstPointer = True

            # If void*, treat like it's not a pointer
            # if duringTypePart == "void":
            # res.pointerIndirectionLevels -= 1

    # Calculate optionality (based on validitygenerator.py)
    if tag.attrib.get("optional") is not None:
        res.isOptional = True
        res.optionalStr = tag.attrib.get("optional")

    # If no validity is being generated, it usually means that
    # validity is complex and not absolute, so let's say yes.
    if tag.attrib.get("noautovalidity") is not None:
        res.isOptional = True

    # If this is a structure extension, it is optional.
    if tag.attrib.get("structextends") is not None:
        res.isOptional = True

    # If this is a pNext pointer, it is optional.
    if res.paramName == "pNext":
        res.isOptional = True

    res.primitiveEncodingSize = typeInfo.getPrimitiveEncodingSize(res.typeName)

    # Annotations: Environment binds
    if tag.attrib.get("binds") is not None:
        bindPairs = map(lambda x: x.strip(), tag.attrib.get("binds").split(","))
        bindPairsSplit = map(lambda p: p.split(":"), bindPairs)
        res.binds = dict(map(lambda sp: (sp[0].strip(), sp[1].strip()), bindPairsSplit))

    # Annotations: Filters
    structMemberName = f"{parentName}.{res.paramName}"
    if structMemberName in STRUCT_MEMBER_FILTER_VAR.keys():
        res.filterVar = STRUCT_MEMBER_FILTER_VAR[structMemberName]

    if structMemberName in STRUCT_MEMBER_FILTER_VALS.keys():
        res.filterVals = STRUCT_MEMBER_FILTER_VALS[structMemberName]

    if structMemberName in STRUCT_MEMBER_FILTER_FUNC.keys():
        res.filterFunc = parseFilterFuncExpr(STRUCT_MEMBER_FILTER_FUNC[structMemberName])

    if tag.attrib.get("filterOtherwise") is not None:
        res.Otherwise = tag.attrib.get("filterOtherwise")

    # store all other attribs here
    res.attribs = dict(tag.attrib)

    return res


def makeVulkanTypeSimple(isConst,
                         typeName,
                         ptrIndirectionLevels,
                         paramName=None):
    res = VulkanType()

    res.typeName = typeName
    res.isConst = isConst
    res.pointerIndirectionLevels = ptrIndirectionLevels
    res.isPointerToConstPointer = False
    res.paramName = paramName
    res.primitiveEncodingSize = None

    return res


# Classes for describing aggregate types (unions, structs) and API calls.
class VulkanCompoundType(object):

    def __init__(self, name: str, members: List[VulkanType], isUnion=False, structEnumExpr=None, structExtendsExpr=None, feature=None, initialEnv={}, optional=None):
        self.name: str = name
        self.typeName: str = name
        self.members: List[VulkanType] = members
        self.environment = initialEnv
        self.isUnion = isUnion
        self.structEnumExpr = structEnumExpr
        self.structExtendsExpr = structExtendsExpr
        self.feature = feature
        if name in DEVICE_MEMORY_STRUCTS:
            self.deviceMemoryInfoParameterIndices = DEVICE_MEMORY_STRUCTS[name]
        else:
            self.deviceMemoryInfoParameterIndices = None
        self.isTransformed = name in TRANSFORMED_TYPES
        self.copy = None
        self.optionalStr = optional

    def initCopies(self):
        self.copy = self

        for m in self.members:
            m.parent = self.copy

    def getMember(self, memberName) -> Optional[VulkanType]:
        for m in self.members:
            if m.paramName == memberName:
                return m
        return None

    def getStructEnumExpr(self,):
        return self.structEnumExpr

    def getProtectStreamFeature(self) -> Optional[str]:
        if not self.name in STRUCT_STREAM_FEATURE.keys():
            return None
        return STRUCT_STREAM_FEATURE[self.name]


class VulkanAPI(object):

    def __init__(self, name: str, retType: VulkanType, parameters, origName=None):
        self.name: str = name
        self.origName = name
        self.retType: VulkanType = retType
        self.parameters: List[VulkanType] = list(filter(lambda param: not param.shouldSkip(), parameters))

        if name in DEVICE_MEMORY_COMMANDS.keys():
            self.deviceMemoryInfoParameterIndices = DEVICE_MEMORY_COMMANDS[name]
        else:
            self.deviceMemoryInfoParameterIndices = None

        self.copy = None

        self.isTransformed = name in TRANSFORMED_TYPES

        if origName:
            self.origName = origName

    def initCopies(self):
        self.copy = self

        for m in self.parameters:
            m.parent = self.copy

    def getCopy(self,):
        return copy(self)

    def getParameter(self, parameterName):
        for p in self.parameters:
            if p.paramName == parameterName:
                return p
        return None

    def withModifiedName(self, newName):
        res = VulkanAPI(newName, self.retType, self.parameters)
        return res

    def getRetVarExpr(self):
        if self.retType.typeName == "void":
            return None
        return "%s_%s_return" % (self.name, self.retType.typeName)

    def getRetTypeExpr(self):
        return self.retType.typeName

    def withCustomParameters(self, customParams):
        res = self.getCopy()
        res.parameters = customParams
        return res

    def withCustomReturnType(self, retType):
        res = self.getCopy()
        res.retType = retType
        return res

# Whether or not special handling of virtual elements
# such as VkDeviceMemory is needed.
def vulkanTypeNeedsTransform(structOrApi):
    return structOrApi.deviceMemoryInfoParameterIndices != None

def vulkanTypeGetNeededTransformTypes(structOrApi):
    res = []
    if structOrApi.deviceMemoryInfoParameterIndices != None:
        res.append("devicememory")
    return res

def vulkanTypeforEachSubType(structOrApi, f):
    toLoop = None
    if type(structOrApi) == VulkanCompoundType:
        toLoop = structOrApi.members
    if type(structOrApi) == VulkanAPI:
        toLoop = structOrApi.parameters

    for (i, x) in enumerate(toLoop):
        f(i, x)

# Parses everything about Vulkan types into a Python readable format.
class VulkanTypeInfo(object):

    def __init__(self, generator):
        self.generator = generator
        self.categories: Set[str] = set([])

        # Tracks what Vulkan type is part of what category.
        self.typeCategories: Dict[str, str] = {}

        # Tracks the primitive encoding size for each type, if applicable.
        self.encodingSizes: Dict[str, Optional[int]] = {}

        self.structs: Dict[str, VulkanCompoundType] = {}
        self.apis: Dict[str, VulkanAPI] = {}

        # Maps bitmask types to the enum type used for the flags
        # E.g. "VkImageAspectFlags" -> "VkImageAspectFlagBits"
        self.bitmasks: Dict[str, str] = {}

        # Maps all enum names to their values.
        # For aliases, the value is the name of the canonical enum
        self.enumValues: Dict[str, Union[int, str]] = {}

        # Maps enum to their xml element
        self.enumElem = {}

        self.feature = None

    def initType(self, name: str, category: str):
        self.categories.add(category)
        self.typeCategories[name] = category
        self.encodingSizes[name] = self.setPrimitiveEncodingSize(name)

    def categoryOf(self, name):
        return self.typeCategories[name]

    def getPrimitiveEncodingSize(self, name):
        return self.encodingSizes[name]

    # Queries relating to categories of Vulkan types.
    def isHandleType(self, name):
        return self.typeCategories.get(name) == "handle"

    def isCompoundType(self, name: str):
        return self.typeCategories.get(name) in ["struct", "union"]

    # Gets the best size in bytes
    # for encoding/decoding a particular Vulkan type.
    # If not applicable, returns None.
    def setPrimitiveEncodingSize(self, name: str) -> Optional[int]:
        baseEncodingSizes = {
            "void": 8,
            "char": 1,
            "float": 4,
            "uint8_t": 1,
            "uint16_t": 2,
            "uint32_t": 4,
            "uint64_t": 8,
            "int": 4,
            "int8_t": 1,
            "int16_t": 2,
            "int32_t": 4,
            "int64_t": 8,
            "size_t": 8,
            "ssize_t": 8,
            "VkBool32": 4,
            "zx_handle_t": 4,
        }

        if name in baseEncodingSizes:
            return baseEncodingSizes[name]

        category = self.typeCategories[name]

        if category in [None, "api", "include", "define", "struct", "union"]:
            return None

        # Handles are pointers so they must be 8 bytes. Basetype includes VkDeviceSize which is 8 bytes.
        if category in ["handle", "basetype", "funcpointer"]:
            return 8

        if category in ["enum", "bitmask"]:
            return 4

    def isNonAbiPortableType(self, typeName):
        if typeName in EXPLICITLY_ABI_PORTABLE_TYPES:
            return False

        if typeName in EXPLICITLY_ABI_NON_PORTABLE_TYPES:
            return True

        category = self.typeCategories[typeName]
        return category in NON_ABI_PORTABLE_TYPE_CATEGORIES

    def onBeginFeature(self, featureName, featureType):
        self.feature = featureName

    def onEndFeature(self):
        self.feature = None

    def onGenType(self, typeinfo, name, alias):
        category = typeinfo.elem.get("category")
        self.initType(name, category)

        if category in ["struct", "union"]:
            self.onGenStruct(typeinfo, name, alias)

        if category == "bitmask":
            self.bitmasks[name] = typeinfo.elem.get("requires")

    def onGenStruct(self, typeinfo, typeName, alias):
        if not alias:
            members: List[VulkanType] = []

            structExtendsExpr = typeinfo.elem.get("structextends")

            structEnumExpr = None

            initialEnv = {}
            envStr = typeinfo.elem.get("exists")
            if envStr != None:
                comma_separated = envStr.split(",")
                name_type_pairs = map(lambda cs: tuple(map(lambda t: t.strip(), cs.split(":"))), comma_separated)
                for (name, typ) in name_type_pairs:
                    initialEnv[name] = {
                        "type" : typ,
                        "binding" : None,
                        "structmember" : False,
                        "body" : None,
                    }

            if typeName in STRUCT_ENV_STR.keys():
                name_body_pairs = STRUCT_ENV_STR[typeName]
                for (name, body) in name_body_pairs.items():
                    initialEnv[name] = {
                        "type" : "uint32_t",
                        "binding" : name,
                        "structmember" : False,
                        "body" : parseLetBodyExpr(body)
                    }

            for member in typeinfo.elem.findall(".//member"):
                if "api" in member.attrib.keys() and not "vulkan" == member.attrib["api"]:
                    continue
                vulkanType = makeVulkanTypeFromXMLTag(self, typeName, member)
                initialEnv[vulkanType.paramName] = {
                    "type": vulkanType.typeName,
                    "binding": vulkanType.paramName,
                    "structmember": True,
                    "body": None,
                }
                members.append(vulkanType)
                if vulkanType.typeName == "VkStructureType" and \
                   member.get("values"):
                   structEnumExpr = member.get("values")

            self.structs[typeName] = \
                VulkanCompoundType(
                    typeName,
                    members,
                    isUnion = self.categoryOf(typeName) == "union",
                    structEnumExpr = structEnumExpr,
                    structExtendsExpr = structExtendsExpr,
                    feature = self.feature,
                    initialEnv = initialEnv,
                    optional = typeinfo.elem.get("optional", None))
            self.structs[typeName].initCopies()

    def onGenGroup(self, groupinfo, groupName, _alias=None):
        self.initType(groupName, "enum")
        enums = groupinfo.elem.findall("enum")
        for enum in enums:
            intVal, strVal = self.generator.enumToValue(enum, True)
            self.enumValues[enum.get('name')] = intVal if intVal is not None else strVal
            self.enumElem[enum.get('name')] = enum


    def onGenEnum(self, enuminfo, name: str, alias):
        self.initType(name, "enum")
        value: str = enuminfo.elem.get("value")
        self.enumElem[name] = enuminfo.elem
        if value and value.isdigit():
            self.enumValues[name] = int(value)
        elif value and value[0] == '"' and value[-1] == '"':
            self.enumValues[name] = value[1:-1]
        elif alias is not None:
            self.enumValues[name] = alias
        else:
            # There's about a dozen cases of using the bitwise NOT operator (e.g.: `(~0U)`, `(~0ULL)`)
            # to concisely represent large values. Just ignore them for now.
            # In the future, we can add a lookup table to convert these to int
            return

    def onGenCmd(self, cmdinfo, name, _alias):
        self.initType(name, "api")

        proto = cmdinfo.elem.find("proto")
        params = cmdinfo.elem.findall("param")

        self.apis[name] = \
            VulkanAPI(
                name,
                makeVulkanTypeFromXMLTag(self, name, proto),
                list(map(lambda p: makeVulkanTypeFromXMLTag(self, name, p),
                         params)))
        self.apis[name].initCopies()

    def onEnd(self):
        pass

def hasNullOptionalStringFeature(forEachType):
    return (hasattr(forEachType, "onCheckWithNullOptionalStringFeature")) and \
           (hasattr(forEachType, "endCheckWithNullOptionalStringFeature")) and \
           (hasattr(forEachType, "finalCheckWithNullOptionalStringFeature"))


# General function to iterate over a vulkan type and call code that processes
# each of its sub-components, if any.
def iterateVulkanType(typeInfo: VulkanTypeInfo, vulkanType: VulkanType, forEachType):
    if not vulkanType.isArrayOfStrings():
        if vulkanType.isPointerToConstPointer:
            return False

    if vulkanType.shouldSkip():
        return False

    forEachType.registerTypeInfo(typeInfo)

    needCheck = vulkanType.isOptionalPointer()

    if typeInfo.isCompoundType(vulkanType.typeName) and not vulkanType.isNextPointer():

        if needCheck:
            forEachType.onCheck(vulkanType)

        forEachType.onCompoundType(vulkanType)

        if needCheck:
            forEachType.endCheck(vulkanType)

    else:
        if vulkanType.isString():
            if needCheck and hasNullOptionalStringFeature(forEachType):
                forEachType.onCheckWithNullOptionalStringFeature(vulkanType)
                forEachType.onString(vulkanType)
                forEachType.endCheckWithNullOptionalStringFeature(vulkanType)
                forEachType.onString(vulkanType)
                forEachType.finalCheckWithNullOptionalStringFeature(vulkanType)
            elif needCheck:
                forEachType.onCheck(vulkanType)
                forEachType.onString(vulkanType)
                forEachType.endCheck(vulkanType)
            else:
                forEachType.onString(vulkanType)

        elif vulkanType.isArrayOfStrings():
            forEachType.onStringArray(vulkanType)

        elif vulkanType.staticArrExpr:
            forEachType.onStaticArr(vulkanType)

        elif vulkanType.isNextPointer():
            if needCheck:
                forEachType.onCheck(vulkanType)
            forEachType.onStructExtension(vulkanType)
            if needCheck:
                forEachType.endCheck(vulkanType)

        elif vulkanType.pointerIndirectionLevels > 0:
            if needCheck:
                forEachType.onCheck(vulkanType)
            forEachType.onPointer(vulkanType)
            if needCheck:
                forEachType.endCheck(vulkanType)

        else:
            forEachType.onValue(vulkanType)

    return True

class VulkanTypeIterator(object):
    def __init__(self,):
        self.typeInfo = None

    def registerTypeInfo(self, typeInfo):
        self.typeInfo = typeInfo

def vulkanTypeGetStructFieldLengthInfo(structInfo, vulkanType):
    def getSpecialCaseVulkanStructFieldLength(structInfo, vulkanType):
        cases = [
            {
                "structName": "VkShaderModuleCreateInfo",
                "field": "pCode",
                "lenExpr": "codeSize",
                "postprocess": lambda expr: "(%s / 4)" % expr
            },
            {
                "structName": "VkPipelineMultisampleStateCreateInfo",
                "field": "pSampleMask",
                "lenExpr": "rasterizationSamples",
                "postprocess": lambda expr: "(((%s) + 31) / 32)" % expr
            },
        ]

        for c in cases:
            if (structInfo.name, vulkanType.paramName) == (c["structName"], c["field"]):
                return c

        return None

    specialCaseAccess = getSpecialCaseVulkanStructFieldLength(structInfo, vulkanType)

    if specialCaseAccess is not None:
        return specialCaseAccess

    lenExpr = vulkanType.getLengthExpression()

    if lenExpr is None:
        return None

    return {
        "structName": structInfo.name,
        "field": vulkanType.typeName,
        "lenExpr": lenExpr,
        "postprocess": lambda expr: expr}


class VulkanTypeProtobufInfo(object):
    def __init__(self, typeInfo, structInfo, vulkanType):
        self.needsMessage = typeInfo.isCompoundType(vulkanType.typeName)
        self.isRepeatedString = vulkanType.isArrayOfStrings()
        self.isString = vulkanType.isString() or (
            vulkanType.typeName == "char" and (vulkanType.staticArrExpr != ""))

        if structInfo is not None:
            self.lengthInfo = vulkanTypeGetStructFieldLengthInfo(
                structInfo, vulkanType)
        else:
            self.lengthInfo = vulkanType.getLengthExpression()

        self.protobufType = None
        self.origTypeCategory = typeInfo.categoryOf(vulkanType.typeName)

        self.isExtensionStruct = \
            vulkanType.typeName == "void" and \
            vulkanType.pointerIndirectionLevels > 0 and \
            vulkanType.paramName == "pNext"

        if self.needsMessage:
            return

        if typeInfo.categoryOf(vulkanType.typeName) in ["enum", "bitmask"]:
            self.protobufType = "uint32"

        if typeInfo.categoryOf(vulkanType.typeName) in ["funcpointer", "handle", "define"]:
            self.protobufType = "uint64"

        if typeInfo.categoryOf(vulkanType.typeName) in ["basetype"]:
            baseTypeMapping = {
                "VkFlags" : "uint32",
                "VkBool32" : "uint32",
                "VkDeviceSize" : "uint64",
                "VkSampleMask" : "uint32",
            }
            self.protobufType = baseTypeMapping[vulkanType.typeName]

        if typeInfo.categoryOf(vulkanType.typeName) == None:

            otherTypeMapping = {
                "void" : "uint64",
                "char" : "uint8",
                "size_t" : "uint64",
                "float" : "float",
                "uint8_t" : "uint32",
                "uint16_t" : "uint32",
                "int32_t" : "int32",
                "uint32_t" : "uint32",
                "uint64_t" : "uint64",
                "VkDeviceSize" : "uint64",
                "VkSampleMask" : "uint32",
            }

            if vulkanType.typeName in otherTypeMapping:
                self.protobufType = otherTypeMapping[vulkanType.typeName]
            else:
                self.protobufType = "uint64"


        protobufCTypeMapping = {
            "uint8" : "uint8_t",
            "uint32" : "uint32_t",
            "int32" : "int32_t",
            "uint64" : "uint64_t",
            "float" : "float",
            "string" : "const char*",
        }

        self.protobufCType = protobufCTypeMapping[self.protobufType]

