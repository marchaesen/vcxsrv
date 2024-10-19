from .common.codegen import CodeGen, VulkanWrapperGenerator
from .common.vulkantypes import \
        VulkanAPI, makeVulkanTypeSimple, iterateVulkanType
from .common.vulkantypes import EXCLUDED_APIS
from .common.vulkantypes import HANDLE_TYPES

import copy
import re

RESOURCE_TRACKER_ENTRIES = [
    "vkEnumerateInstanceExtensionProperties",
    "vkEnumerateDeviceExtensionProperties",
    "vkEnumeratePhysicalDevices",
    "vkAllocateMemory",
    "vkFreeMemory",
    "vkCreateImage",
    "vkDestroyImage",
    "vkGetImageMemoryRequirements",
    "vkGetImageMemoryRequirements2",
    "vkGetImageMemoryRequirements2KHR",
    "vkGetImageSubresourceLayout",
    "vkBindImageMemory",
    "vkBindImageMemory2",
    "vkBindImageMemory2KHR",
    "vkCreateBuffer",
    "vkDestroyBuffer",
    "vkGetBufferMemoryRequirements",
    "vkGetBufferMemoryRequirements2",
    "vkGetBufferMemoryRequirements2KHR",
    "vkBindBufferMemory",
    "vkBindBufferMemory2",
    "vkBindBufferMemory2KHR",
    "vkCreateSemaphore",
    "vkDestroySemaphore",
    "vkQueueSubmit",
    "vkQueueSubmit2",
    "vkQueueWaitIdle",
    "vkImportSemaphoreFdKHR",
    "vkGetSemaphoreFdKHR",
    # Warning: These need to be defined in vk.xml (currently no-op) {
    "vkGetMemoryFuchsiaHandleKHR",
    "vkGetMemoryFuchsiaHandlePropertiesKHR",
    "vkGetSemaphoreFuchsiaHandleKHR",
    "vkImportSemaphoreFuchsiaHandleKHR",
    # } end Warning: These need to be defined in vk.xml (currently no-op)
    "vkGetAndroidHardwareBufferPropertiesANDROID",
    "vkGetMemoryAndroidHardwareBufferANDROID",
    "vkGetMemoryFdKHR",
    "vkGetMemoryFdPropertiesKHR",
    "vkCreateSamplerYcbcrConversion",
    "vkDestroySamplerYcbcrConversion",
    "vkCreateSamplerYcbcrConversionKHR",
    "vkDestroySamplerYcbcrConversionKHR",
    "vkUpdateDescriptorSetWithTemplate",
    "vkUpdateDescriptorSetWithTemplateKHR",
    "vkGetPhysicalDeviceImageFormatProperties2",
    "vkGetPhysicalDeviceImageFormatProperties2KHR",
    "vkBeginCommandBuffer",
    "vkEndCommandBuffer",
    "vkResetCommandBuffer",
    "vkCreateImageView",
    "vkCreateSampler",
    "vkGetPhysicalDeviceExternalFenceProperties",
    "vkGetPhysicalDeviceExternalFencePropertiesKHR",
    "vkGetPhysicalDeviceExternalBufferProperties",
    "vkGetPhysicalDeviceExternalBufferPropertiesKHR",
    "vkCreateFence",
    "vkResetFences",
    "vkImportFenceFdKHR",
    "vkGetFenceFdKHR",
    "vkGetFenceStatus",
    "vkWaitForFences",
    "vkCreateDescriptorPool",
    "vkDestroyDescriptorPool",
    "vkResetDescriptorPool",
    "vkAllocateDescriptorSets",
    "vkFreeDescriptorSets",
    "vkCreateDescriptorSetLayout",
    "vkCmdExecuteCommands",
    "vkCmdBindDescriptorSets",
    "vkDestroyDescriptorSetLayout",
    "vkAllocateCommandBuffers",
    "vkQueueSignalReleaseImageANDROID",
    "vkCmdPipelineBarrier",
    "vkCreateGraphicsPipelines",
    # Fuchsia
    "vkGetMemoryZirconHandleFUCHSIA",
    "vkGetMemoryZirconHandlePropertiesFUCHSIA",
    "vkGetSemaphoreZirconHandleFUCHSIA",
    "vkImportSemaphoreZirconHandleFUCHSIA",
    "vkCreateBufferCollectionFUCHSIA",
    "vkDestroyBufferCollectionFUCHSIA",
    "vkSetBufferCollectionImageConstraintsFUCHSIA",
    "vkSetBufferCollectionBufferConstraintsFUCHSIA",
    "vkGetBufferCollectionPropertiesFUCHSIA",
]

SUCCESS_VAL = {
    "VkResult" : ["VK_SUCCESS"],
}

HANDWRITTEN_ENTRY_POINTS = [
    # Instance/device/physical-device special-handling, dispatch tables, etc..
    "vkCreateInstance",
    "vkDestroyInstance",
    "vkGetInstanceProcAddr",
    "vkEnumerateInstanceVersion",
    "vkEnumerateInstanceLayerProperties",
    "vkEnumerateInstanceExtensionProperties",
    "vkEnumerateDeviceExtensionProperties",
    "vkGetDeviceProcAddr",
    "vkEnumeratePhysicalDevices",
    "vkEnumeratePhysicalDeviceGroups",
    "vkCreateDevice",
    "vkDestroyDevice",
    # Manual alloc/free + vk_*_init/free() call w/ special params
    "vkGetDeviceQueue",
    "vkGetDeviceQueue2",
    # Command pool/buffer handling
    "vkCreateCommandPool",
    "vkDestroyCommandPool",
    "vkAllocateCommandBuffers",
    "vkResetCommandPool",
    "vkFreeCommandBuffers",
    "vkResetCommandPool",
    # Transform feedback
    "vkCmdBeginTransformFeedbackEXT",
    # Special cases to handle struct translations in the pNext chain
    # TODO: Make a codegen module (use deepcopy as reference) to make this more robust
    "vkAllocateMemory",
    "vkUpdateDescriptorSets",
]

# Handles that need to be translated to/from their corresponding gfxstream object types
HANDLES_TRANSLATE = {
    "VkInstance",
    "VkPhysicalDevice",
    "VkDevice",
    "VkQueue",
    "VkCommandPool",
    "VkCommandBuffer",
    "VkFence",
    "VkSemaphore",
    # TODO: Still need this translation to avoid descriptorSets crash
    "VkBuffer",
}

# Types that have a corresponding method for transforming
# an input list to its internal counterpart
TYPES_TRANSFORM_LIST_METHOD = {
    "VkFence",
    "VkSemaphore",
    "VkSemaphoreSubmitInfo",
}

def is_cmdbuf_dispatch(api):
    return "VkCommandBuffer" == api.parameters[0].typeName

def is_queue_dispatch(api):
    return "VkQueue" == api.parameters[0].typeName

def getCreateParam(api):
    for param in api.parameters:
        if param.isCreatedBy(api):
            return param
    return None

def getDestroyParam(api):
    for param in api.parameters:
        if param.isDestroyedBy(api):
            return param
    return None

# i.e. VkQueryPool --> vk_query_pool
def typeNameToMesaType(typeName):
    vkTypeNameRegex = "(?<=[a-z])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])"
    words = re.split(vkTypeNameRegex, typeName)
    outputType = "vk"
    for word in words[1:]:
        outputType += "_"
        outputType += word.lower()
    return outputType

def typeNameToBaseName(typeName):
    return typeNameToMesaType(typeName)[len("vk_"):]

def paramNameToObjectName(paramName):
    return "gfxstream_%s" % paramName

def typeNameToVkObjectType(typeName):
    return "VK_OBJECT_TYPE_%s" % typeNameToBaseName(typeName).upper()

def typeNameToObjectType(typeName):
    return "gfxstream_vk_%s" % typeNameToBaseName(typeName)

def transformListFuncName(typeName):
    return "transform%sList" % (typeName)

def isAllocatorParam(param):
    ALLOCATOR_TYPE_NAME = "VkAllocationCallbacks"
    return (param.pointerIndirectionLevels == 1
            and param.isConst
            and param.typeName == ALLOCATOR_TYPE_NAME)

def isArrayParam(param):
    return (1 == param.pointerIndirectionLevels
            and param.isConst
            and "len" in param.attribs)

INTERNAL_OBJECT_NAME = "internal_object"

class VulkanFuncTable(VulkanWrapperGenerator):
    def __init__(self, module, typeInfo):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)
        self.typeInfo = typeInfo
        self.cgen = CodeGen()
        self.entries = []
        self.entryFeatures = []
        self.cmdToFeatureType = {}
        self.feature = None
        self.featureType = None

    def onBegin(self,):
        cgen = self.cgen
        self.module.appendImpl(cgen.swapCode())
        pass

    def onBeginFeature(self, featureName, featureType):
        self.feature = featureName
        self.featureType = featureType

    def onEndFeature(self):
        self.feature = None
        self.featureType = None

    def onFeatureNewCmd(self, name):
        self.cmdToFeatureType[name] = self.featureType

    def onGenCmd(self, cmdinfo, name, alias):
        typeInfo = self.typeInfo
        cgen = self.cgen
        api = typeInfo.apis[name]
        self.entries.append(api)
        self.entryFeatures.append(self.feature)
        self.loopVars = ["i", "j", "k", "l", "m", "n"]
        self.loopVarIndex = 0

        def getNextLoopVar():
            if self.loopVarIndex >= len(self.loopVars):
                raise
            loopVar = self.loopVars[self.loopVarIndex]
            self.loopVarIndex += 1
            return loopVar

        def isCompoundType(typeName):
            return typeInfo.isCompoundType(typeName)

        def handleTranslationRequired(typeName):
            return typeName in HANDLE_TYPES and typeName in HANDLES_TRANSLATE

        def translationRequired(typeName):
            if isCompoundType(typeName):
                struct = typeInfo.structs[typeName]
                for member in struct.members:
                    if translationRequired(member.typeName):
                        return True
                return False
            else:
                return handleTranslationRequired(typeName)

        def genDestroyGfxstreamObjects():
            destroyParam = getDestroyParam(api)
            if not destroyParam:
                return
            if not translationRequired(destroyParam.typeName):
                return
            objectName = paramNameToObjectName(destroyParam.paramName)
            allocatorParam = "NULL"
            for p in api.parameters:
                if isAllocatorParam(p):
                    allocatorParam = p.paramName
            deviceParam = api.parameters[0]
            if "VkDevice" != deviceParam.typeName:
                print("ERROR: Unhandled non-VkDevice parameters[0]: %s (for API: %s)" %(deviceParam.typeName, api.name))
                raise
            # call vk_object_free() directly
            mesaObjectDestroy = "(void *)%s" % objectName
            cgen.funcCall(
                None,
                "vk_object_free",
                ["&%s->vk" % paramNameToObjectName(deviceParam.paramName), allocatorParam, mesaObjectDestroy]
            )

        def genMesaObjectAlloc(allocCallLhs):
            deviceParam = api.parameters[0]
            if "VkDevice" != deviceParam.typeName:
                print("ERROR: Unhandled non-VkDevice parameters[0]: %s (for API: %s)" %(deviceParam.typeName, api.name))
                raise
            allocatorParam = "NULL"
            for p in api.parameters:
                if isAllocatorParam(p):
                    allocatorParam = p.paramName
            createParam = getCreateParam(api)
            objectType = typeNameToObjectType(createParam.typeName)
            # Call vk_object_zalloc directly
            cgen.funcCall(
                allocCallLhs,
                "(%s *)vk_object_zalloc" % objectType,
                ["&%s->vk" % paramNameToObjectName(deviceParam.paramName), allocatorParam, ("sizeof(%s)" % objectType), typeNameToVkObjectType(createParam.typeName)]
            )

        # Alloc/create gfxstream_vk_* object
        def genCreateGfxstreamObjects():
            createParam = getCreateParam(api)
            if not createParam:
                return False
            if not handleTranslationRequired(createParam.typeName):
                return False
            objectType = "struct %s" % typeNameToObjectType(createParam.typeName)
            callLhs = "%s *%s" % (objectType, paramNameToObjectName(createParam.paramName))
            genMesaObjectAlloc(callLhs)

            retVar = api.getRetVarExpr()
            if retVar:
                retTypeName = api.getRetTypeExpr()
                # ex: vkCreateBuffer_VkResult_return = gfxstream_buffer ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
                cgen.stmt("%s = %s ? %s : %s" %
                          (retVar, paramNameToObjectName(createParam.paramName), SUCCESS_VAL[retTypeName][0], "VK_ERROR_OUT_OF_HOST_MEMORY"))
            return True

        def genVkFromHandle(param, fromName):
            objectName = paramNameToObjectName(param.paramName)
            cgen.stmt("VK_FROM_HANDLE(%s, %s, %s)" %
                      (typeNameToObjectType(param.typeName), objectName, fromName))
            return objectName

        def genGetGfxstreamHandles():
            createParam = getCreateParam(api)
            for param in api.parameters:
                if not handleTranslationRequired(param.typeName):
                    continue
                elif isArrayParam(param):
                    continue
                elif param != createParam:
                    if param.pointerIndirectionLevels > 0:
                        print("ERROR: Unhandled pointerIndirectionLevels > 1 for API %s (param %s)" % (api.name, param.paramName))
                        raise
                    genVkFromHandle(param, param.paramName)

        def internalNestedParamName(param):
            parentName = ""
            if param.parent:
                parentName = "_%s" % param.parent.typeName
            return "internal%s_%s" % (parentName, param.paramName)

        def genInternalArrayDeclarations(param, countParamName, nestLevel=0):
            internalArray = None
            if 0 == nestLevel:
                internalArray = "internal_%s" % param.paramName
                cgen.stmt("std::vector<%s> %s(%s)" % (param.typeName, internalArray, countParamName))
            elif 1 == nestLevel or 2 == nestLevel:
                internalArray = internalNestedParamName(param)
                if isArrayParam(param):
                    cgen.stmt("std::vector<std::vector<%s>> %s" % (param.typeName, internalArray))
                else:
                    cgen.stmt("std::vector<%s> %s" % (param.typeName, internalArray))
            else:
                print("ERROR: nestLevel > 2 not verified.")
                raise
            if isCompoundType(param.typeName):
                for member in typeInfo.structs[param.typeName].members:
                    if translationRequired(member.typeName):
                        if handleTranslationRequired(member.typeName) and not isArrayParam(member):
                            # No declarations for non-array handleType
                            continue
                        genInternalArrayDeclarations(member, countParamName, nestLevel + 1)
            return internalArray

        def genInternalCompoundType(param, outName, inName, currLoopVar):
            nextLoopVar = None
            cgen.stmt("%s = %s" % (outName, inName))
            for member in typeInfo.structs[param.typeName].members:
                if not translationRequired(member.typeName):
                    continue
                cgen.line("/* %s::%s */" % (param.typeName, member.paramName))
                nestedOutName = ("%s[%s]" % (internalNestedParamName(member), currLoopVar))
                if isArrayParam(member):
                    countParamName = "%s.%s" % (outName, member.attribs["len"])
                    inArrayName = "%s.%s" % (outName, member.paramName)
                    cgen.stmt("%s.push_back(std::vector<%s>())" % (internalNestedParamName(member), member.typeName))
                    if member.typeName in TYPES_TRANSFORM_LIST_METHOD:
                        # Use the corresponding transformList call
                        cgen.funcCall(nestedOutName, transformListFuncName(member.typeName), [inArrayName, countParamName])
                        cgen.stmt("%s = %s.data()" % (inArrayName, nestedOutName))
                        cgen.stmt("%s = %s.size()" % (countParamName, nestedOutName))
                    else:
                        # Standard translation
                        cgen.stmt("%s.resize(%s)" % (nestedOutName, countParamName))
                        if not nextLoopVar:
                            nextLoopVar = getNextLoopVar()
                        internalArray = genInternalArray(member, countParamName, nestedOutName, inArrayName, nextLoopVar)
                        cgen.stmt("%s = %s" %(inArrayName, internalArray))
                elif isCompoundType(member.typeName):
                    memberFullName = "%s.%s" % (outName, member.paramName)
                    if 1 == member.pointerIndirectionLevels:
                        cgen.beginIf(memberFullName)
                        inParamName = "%s[0]" % memberFullName
                        genInternalCompoundType(member, nestedOutName, inParamName, currLoopVar)
                        cgen.stmt("%s.%s = &%s" % (outName, member.paramName,  nestedOutName))
                    else:
                        cgen.beginBlock()
                        genInternalCompoundType(member, nestedOutName, memberFullName, currLoopVar)
                        cgen.stmt("%s.%s = %s" % (outName, member.paramName,  nestedOutName))
                    cgen.endBlock()
                else:
                    # Replace member with internal object
                    replaceName = "%s.%s" % (outName, member.paramName)
                    if member.isOptional:
                        cgen.beginIf(replaceName)
                    gfxstreamObject = genVkFromHandle(member, replaceName)
                    cgen.stmt("%s = %s->%s" % (replaceName, gfxstreamObject, INTERNAL_OBJECT_NAME))
                    if member.isOptional:
                        cgen.endIf()

        def genInternalArray(param, countParamName, outArrayName, inArrayName, loopVar):
            if param.typeName in TYPES_TRANSFORM_LIST_METHOD:
                # Use the corresponding transformList call
                cgen.funcCall(outArrayName, transformListFuncName(param.typeName), [inArrayName, countParamName])
                cgen.stmt("%s = %s.data()" % (inArrayName, outArrayName))
                cgen.stmt("%s = %s.size()" % (countParamName, outArrayName))
            else:
                cgen.beginFor("uint32_t %s = 0" % loopVar, "%s < %s" % (loopVar, countParamName), "++%s" % loopVar)
                if param.isOptional:
                    cgen.beginIf(inArrayName)
                if isCompoundType(param.typeName):
                    genInternalCompoundType(param, ("%s[%s]" % (outArrayName, loopVar)), "%s[%s]" % (inArrayName, loopVar), loopVar)
                else:
                    gfxstreamObject = genVkFromHandle(param, "%s[%s]" % (inArrayName, loopVar))
                    cgen.stmt("%s[%s] = %s->%s" % (outArrayName, loopVar, gfxstreamObject, INTERNAL_OBJECT_NAME))
                if param.isOptional:
                    cgen.endIf()
                cgen.endFor()
            return "%s.data()" % outArrayName

        # Translate params into params needed for gfxstream-internal
        #  encoder/resource-tracker calls
        def getEncoderOrResourceTrackerParams():
            createParam = getCreateParam(api)
            outParams = copy.deepcopy(api.parameters)
            nextLoopVar = getNextLoopVar()
            for param in outParams:
                if not translationRequired(param.typeName):
                    continue
                elif isArrayParam(param) or isCompoundType(param.typeName):
                    if param.possiblyOutput():
                        print("ERROR: Unhandled CompoundType / Array output for API %s (param %s)" % (api.name, param.paramName))
                        raise
                    if 1 != param.pointerIndirectionLevels or not param.isConst:
                        print("ERROR: Compound type / array input is not 'const <type>*' (API: %s, paramName: %s)" % (api.name, param.paramName))
                        raise
                    countParamName = "1"
                    if "len" in param.attribs:
                        countParamName = param.attribs["len"]
                    internalArrayName = genInternalArrayDeclarations(param, countParamName)
                    param.paramName = genInternalArray(param, countParamName, internalArrayName, param.paramName, nextLoopVar)
                elif 0 == param.pointerIndirectionLevels:
                    if param.isOptional:
                        param.paramName = ("%s ? %s->%s : VK_NULL_HANDLE" % (paramNameToObjectName(param.paramName), paramNameToObjectName(param.paramName), INTERNAL_OBJECT_NAME))
                    else:
                        param.paramName = ("%s->%s" % (paramNameToObjectName(param.paramName), INTERNAL_OBJECT_NAME))
                elif createParam and param.paramName == createParam.paramName:
                    param.paramName = ("&%s->%s" % (paramNameToObjectName(param.paramName), INTERNAL_OBJECT_NAME))
                else:
                    print("ERROR: Unknown handling for param: %s (API: %s)" % (param, api.name))
                    raise
            return outParams

        def genEncoderOrResourceTrackerCall(declareResources=True):
            if is_cmdbuf_dispatch(api):
                cgen.stmt("auto vkEnc = gfxstream::vk::ResourceTracker::getCommandBufferEncoder(%s->%s)" % (paramNameToObjectName(api.parameters[0].paramName), INTERNAL_OBJECT_NAME))
            elif is_queue_dispatch(api):
                cgen.stmt("auto vkEnc = gfxstream::vk::ResourceTracker::getQueueEncoder(%s->%s)" % (paramNameToObjectName(api.parameters[0].paramName), INTERNAL_OBJECT_NAME))
            else:
                cgen.stmt("auto vkEnc = gfxstream::vk::ResourceTracker::getThreadLocalEncoder()")
            callLhs = None
            retTypeName = api.getRetTypeExpr()
            if retTypeName != "void":
                callLhs = api.getRetVarExpr()

            # Get parameter list modded for gfxstream-internal call
            parameters = getEncoderOrResourceTrackerParams()
            if name in RESOURCE_TRACKER_ENTRIES:
                if declareResources:
                    cgen.stmt("auto resources = gfxstream::vk::ResourceTracker::get()")
                cgen.funcCall(
                    callLhs, "resources->" + "on_" + api.name,
                    ["vkEnc"] + SUCCESS_VAL.get(retTypeName, []) + \
                    [p.paramName for p in parameters])
            else:
                cgen.funcCall(
                    callLhs, "vkEnc->" + api.name, [p.paramName for p in parameters] + ["true /* do lock */"])

        def genReturnExpression():
            retTypeName = api.getRetTypeExpr()
            # Set the createParam output, if applicable
            createParam = getCreateParam(api)
            if createParam and handleTranslationRequired(createParam.typeName):
                if 1 != createParam.pointerIndirectionLevels:
                    print("ERROR: Unhandled pointerIndirectionLevels != 1 in return for API %s (createParam %s)" % api.name, createParam.paramName)
                    raise
                # ex: *pBuffer = gfxstream_vk_buffer_to_handle(gfxstream_buffer)
                cgen.funcCall(
                    "*%s" % createParam.paramName,
                    "%s_to_handle" % typeNameToObjectType(createParam.typeName),
                    [paramNameToObjectName(createParam.paramName)]
                )

            if retTypeName != "void":
                cgen.stmt("return %s" % api.getRetVarExpr())

        def genGfxstreamEntry(declareResources=True):
            cgen.stmt("MESA_TRACE_SCOPE(\"%s\")" % api.name)
            # declare returnVar
            retTypeName = api.getRetTypeExpr()
            retVar = api.getRetVarExpr()
            if retVar:
                cgen.stmt("%s %s = (%s)0" % (retTypeName, retVar, retTypeName))
            # Check non-null destroy param for free/destroy calls
            destroyParam = getDestroyParam(api)
            if destroyParam:
                cgen.beginIf("VK_NULL_HANDLE == %s" % destroyParam.paramName)
                if api.getRetTypeExpr() != "void":
                    cgen.stmt("return %s" % api.getRetVarExpr())
                else:
                    cgen.stmt("return")
                cgen.endIf()
            # Translate handles
            genGetGfxstreamHandles()
            # Translation/creation of objects
            createdObject = genCreateGfxstreamObjects()
            # Make encoder/resource-tracker call
            if retVar and createdObject:
                cgen.beginIf("%s == %s" % (SUCCESS_VAL[retTypeName][0], retVar))
            else:
                cgen.beginBlock()
            genEncoderOrResourceTrackerCall()
            cgen.endBlock()
            # Destroy gfxstream objects
            genDestroyGfxstreamObjects()
            # Set output / return variables
            genReturnExpression()

        api_entry = api.withModifiedName("gfxstream_vk_" + api.name[2:])
        if api.name not in HANDWRITTEN_ENTRY_POINTS:
            cgen.line(self.cgen.makeFuncProto(api_entry))
            cgen.beginBlock()
            genGfxstreamEntry()
            cgen.endBlock()
            self.module.appendImpl(cgen.swapCode())


    def onEnd(self,):
        pass

    def isDeviceDispatch(self, api):
        # TODO(230793667): improve the heuristic and just use "cmdToFeatureType"
        return (len(api.parameters) > 0 and
            "VkDevice" == api.parameters[0].typeName) or (
            "VkCommandBuffer" == api.parameters[0].typeName and
            self.cmdToFeatureType.get(api.name, "") == "device")
