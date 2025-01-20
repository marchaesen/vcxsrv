# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT
import copy

from .common.codegen import CodeGen, VulkanWrapperGenerator
from .common.vulkantypes import \
        VulkanAPI, makeVulkanTypeSimple, iterateVulkanType

from .marshaling import VulkanMarshalingCodegen
from .reservedmarshaling import VulkanReservedMarshalingCodegen
from .counting import VulkanCountingCodegen
from .handlemap import HandleMapCodegen
from .deepcopy import DeepcopyCodegen
from .transform import TransformCodegen, genTransformsForVulkanType

from .wrapperdefs import API_PREFIX_RESERVEDMARSHAL
from .wrapperdefs import API_PREFIX_MARSHAL
from .wrapperdefs import API_PREFIX_UNMARSHAL
from .wrapperdefs import ROOT_TYPE_DEFAULT_VALUE
from .wrapperdefs import VULKAN_STREAM_TYPE_GUEST

encoder_decl_preamble = """

class VkEncoder {
public:
    VkEncoder(gfxstream::guest::IOStream* stream);
    ~VkEncoder();

#include "VkEncoder.h.inl"
"""

encoder_decl_postamble = """
private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};
"""

encoder_impl_preamble ="""

using namespace gfxstream::vk;

using gfxstream::aemu::BumpPool;

#include "VkEncoder.cpp.inl"

#define VALIDATE_RET(retType, success, validate) \\
    retType goldfish_vk_validateResult = validate; \\
    if (goldfish_vk_validateResult != success) return goldfish_vk_validateResult; \\

#define VALIDATE_VOID(validate) \\
    VkResult goldfish_vk_validateResult = validate; \\
    if (goldfish_vk_validateResult != VK_SUCCESS) return; \\

"""

STREAM = "stream"
RESOURCES = "sResourceTracker"
POOL = "pool"

ENCODER_PREVALIDATED_APIS = [
    "vkFlushMappedMemoryRanges",
    "vkInvalidateMappedMemoryRanges",
]

ENCODER_CUSTOM_RESOURCE_PREPROCESS = [
    "vkMapMemoryIntoAddressSpaceGOOGLE",
    "vkDestroyDevice",
]

ENCODER_CUSTOM_RESOURCE_POSTPROCESS = [
    "vkCreateInstance",
    "vkCreateDevice",
    "vkMapMemoryIntoAddressSpaceGOOGLE",
    "vkGetPhysicalDeviceFeatures2",
    "vkGetPhysicalDeviceFeatures2KHR",
    "vkGetPhysicalDeviceProperties",
    "vkGetPhysicalDeviceProperties2",
    "vkGetPhysicalDeviceProperties2KHR",
    "vkCreateDescriptorUpdateTemplate",
    "vkCreateDescriptorUpdateTemplateKHR",
    "vkGetPhysicalDeviceExternalSemaphoreProperties",
    "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR",
    "vkGetDeviceQueue",
    "vkGetDeviceQueue2",
]

ENCODER_EXPLICIT_FLUSHED_APIS = [
    "vkEndCommandBufferAsyncGOOGLE",
    "vkQueueSubmitAsyncGOOGLE",
    "vkQueueBindSparseAsyncGOOGLE",
    "vkQueueWaitIdleAsyncGOOGLE",
    "vkQueueSignalReleaseImageANDROID",
    "vkDestroyDevice",
]

SUCCESS_RET_TYPES = {
    "VkResult" : "VK_SUCCESS",
    "void" : None,
    # TODO: Put up success results for other return types here.
}

ENCODER_THIS_PARAM = makeVulkanTypeSimple(False, "VkEncoder", 1, "this")

# Common components of encoding a Vulkan API call
def make_event_handler_call(
    handler_access,
    api,
    context_param,
    input_result_param,
    cgen,
    suffix=""):
    extraParams = [context_param.paramName]
    if input_result_param:
        extraParams.append(input_result_param)
    return cgen.makeCallExpr( \
               "%s->on_%s%s" % (handler_access, api.name, suffix),
               extraParams + \
                       [p.paramName for p in api.parameters[:-1]])

def emit_custom_pre_validate(typeInfo, api, cgen):
    if api.name in ENCODER_PREVALIDATED_APIS:
        callExpr = \
            make_event_handler_call( \
                "mImpl->validation()", api,
                ENCODER_THIS_PARAM,
                SUCCESS_RET_TYPES[api.getRetTypeExpr()],
                cgen)

        if api.getRetTypeExpr() == "void":
            cgen.stmt("VALIDATE_VOID(%s)" % callExpr)
        else:
            cgen.stmt("VALIDATE_RET(%s, %s, %s)" % \
                (api.getRetTypeExpr(),
                 SUCCESS_RET_TYPES[api.getRetTypeExpr()],
                 callExpr))

def emit_custom_resource_preprocess(typeInfo, api, cgen):
    if api.name in ENCODER_CUSTOM_RESOURCE_PREPROCESS:
        cgen.stmt( \
            make_event_handler_call( \
                "sResourceTracker", api,
                ENCODER_THIS_PARAM,
                SUCCESS_RET_TYPES[api.getRetTypeExpr()],
                cgen, suffix="_pre"))

def emit_custom_resource_postprocess(typeInfo, api, cgen):
    if api.name in ENCODER_CUSTOM_RESOURCE_POSTPROCESS:
        cgen.stmt(make_event_handler_call( \
            "sResourceTracker",
            api,
            ENCODER_THIS_PARAM,
            api.getRetVarExpr(),
            cgen))

def emit_count_marshal(typeInfo, param, cgen):
    res = \
        iterateVulkanType(
            typeInfo, param,
            VulkanCountingCodegen( \
                cgen, "sFeatureBits", param.paramName, "countPtr", ROOT_TYPE_DEFAULT_VALUE,
               "count_"))
    if not res:
        cgen.stmt("(void)%s" % param.paramName)

def emit_marshal(typeInfo, param, cgen):
    forOutput = param.isHandleType() and ("out" in param.inout)
    if forOutput:
        cgen.stmt("/* is handle, possibly out */")

    res = \
        iterateVulkanType(
            typeInfo, param,
            VulkanReservedMarshalingCodegen( \
                cgen, "guest", STREAM, ROOT_TYPE_DEFAULT_VALUE, param.paramName, "streamPtrPtr",
               API_PREFIX_RESERVEDMARSHAL,
               "" if forOutput else "get_host_u64_",
               direction="write"))
    if not res:
        cgen.stmt("(void)%s" % param.paramName)

    if forOutput:
        cgen.stmt("/* is handle, possibly out */")

def emit_unmarshal(typeInfo, param, cgen):
    iterateVulkanType(
        typeInfo, param,
        VulkanMarshalingCodegen( \
            cgen, STREAM, ROOT_TYPE_DEFAULT_VALUE, param.paramName,
           API_PREFIX_UNMARSHAL, direction="read"))

def emit_deepcopy(typeInfo, param, cgen):
    res = \
        iterateVulkanType(typeInfo, param, DeepcopyCodegen(
            cgen, [param.paramName, "local_" + param.paramName], "pool", ROOT_TYPE_DEFAULT_VALUE, "deepcopy_"))
    if not res:
        cgen.stmt("(void)%s" % param.paramName)

def emit_transform(typeInfo, param, cgen, variant="tohost"):
    res = \
        iterateVulkanType(typeInfo, param, TransformCodegen( \
            cgen, param.paramName, "sResourceTracker", "transform_%s_" % variant, variant))
    if not res:
        cgen.stmt("(void)%s" % param.paramName)

def emit_handlemap_create(typeInfo, param, cgen):
    iterateVulkanType(typeInfo, param, HandleMapCodegen(
        cgen, None, "sResourceTracker", "handlemap_",
        lambda vtype: typeInfo.isHandleType(vtype.typeName)
    ))

def custom_encoder_args(api):
    params = ["this"]
    if api.getRetVarExpr() is not None:
        params.append(api.getRetVarExpr())
    return params

def emit_handlemap_destroy(typeInfo, param, cgen):
    iterateVulkanType(typeInfo, param, HandleMapCodegen(
        cgen, None, "sResourceTracker->destroyMapping()", "handlemap_",
        lambda vtype: typeInfo.isHandleType(vtype.typeName)
    ))

class EncodingParameters(object):
    def __init__(self, api):
        self.localCopied = []
        self.toWrite = []
        self.toRead = []
        self.toCreate = []
        self.toDestroy = []

        for param in api.parameters:
            param.action = None
            param.inout = "in"

            if param.paramName == "doLock":
                continue

            if param.possiblyOutput():
                param.inout += "out"
                self.toWrite.append(param)
                self.toRead.append(param)
                if param.isCreatedBy(api):
                    self.toCreate.append(param)
                    param.action = "create"
            else:

                if param.paramName == "doLock":
                    continue

                if param.isDestroyedBy(api):
                    self.toDestroy.append(param)
                    param.action = "destroy"
                localCopyParam = \
                    param.getForNonConstAccess().withModifiedName( \
                        "local_" + param.paramName)
                self.localCopied.append((param, localCopyParam))
                self.toWrite.append(localCopyParam)

def emit_parameter_encode_preamble_write(typeInfo, api, cgen):
    emit_custom_pre_validate(typeInfo, api, cgen);
    emit_custom_resource_preprocess(typeInfo, api, cgen);

    cgen.stmt("auto %s = mImpl->stream()" % STREAM)
    cgen.stmt("auto %s = mImpl->pool()" % POOL)
    # cgen.stmt("%s->setHandleMapping(%s->unwrapMapping())" % (STREAM, RESOURCES))

    encodingParams = EncodingParameters(api)
    for (_, localCopyParam) in encodingParams.localCopied:
        cgen.stmt(cgen.makeRichCTypeDecl(localCopyParam))

def emit_parameter_encode_copy_unwrap_count(typeInfo, api, cgen, customUnwrap=None):
    encodingParams = EncodingParameters(api)

    for (origParam, localCopyParam) in encodingParams.localCopied:
        shouldCustomCopy = \
            customUnwrap and \
            origParam.paramName in customUnwrap and \
            "copyOp" in customUnwrap[origParam.paramName]

        shouldCustomMap = \
            customUnwrap and \
            origParam.paramName in customUnwrap and \
            "mapOp" in customUnwrap[origParam.paramName]

        if shouldCustomCopy:
            customUnwrap[origParam.paramName]["copyOp"](cgen, origParam, localCopyParam)
        else:
            # if this is a pointer type and we don't do custom copy nor unwrap,
            # and the transform doesn't end up doing anything,
            # don't deepcopy, just cast it.

            avoidDeepcopy = False

            if origParam.pointerIndirectionLevels > 0:
                testCgen = CodeGen()
                genTransformsForVulkanType("sResourceTracker", origParam, lambda p: testCgen.generalAccess(p, parentVarName = None, asPtr = True), lambda p: testCgen.generalLengthAccess(p, parentVarName = None), testCgen)
                emit_transform(typeInfo, origParam, testCgen, variant="tohost")
                if "" == testCgen.swapCode():
                    avoidDeepcopy = True
            if avoidDeepcopy:
                cgen.line("// Avoiding deepcopy for %s" % origParam.paramName)
                cgen.stmt("%s = (%s%s)%s" % (localCopyParam.paramName, localCopyParam.typeName, "*" * origParam.pointerIndirectionLevels, origParam.paramName))
            else:
                emit_deepcopy(typeInfo, origParam, cgen)

    for (origParam, localCopyParam) in encodingParams.localCopied:
        shouldCustomMap = \
            customUnwrap and \
            origParam.paramName in customUnwrap and \
            "mapOp" in customUnwrap[origParam.paramName]

        if shouldCustomMap:
            customUnwrap[origParam.paramName]["mapOp"](cgen, origParam, localCopyParam)
        else:
            if localCopyParam.typeName == "VkAllocationCallbacks":
                cgen.stmt("%s = nullptr" % localCopyParam.paramName)

    apiForTransform = \
        api.withCustomParameters( \
            map(lambda p: p[1], \
                encodingParams.localCopied))

    # Apply transforms if applicable.
    # Apply transform to API itself:
    genTransformsForVulkanType(
        "sResourceTracker",
        apiForTransform,
        lambda p: cgen.generalAccess(p, parentVarName = None, asPtr = True),
        lambda p: cgen.generalLengthAccess(p, parentVarName = None),
        cgen)

    # For all local copied parameters, run the transforms
    for localParam in apiForTransform.parameters:
        if "doLock" in localParam.paramName:
            continue
        emit_transform(typeInfo, localParam, cgen, variant="tohost")

    cgen.stmt("size_t count = 0")
    cgen.stmt("size_t* countPtr = &count")
    cgen.beginBlock()

    # Use counting stream to calculate the packet size.
    for p in encodingParams.toWrite:
        emit_count_marshal(typeInfo, p, cgen)

    cgen.endBlock()

def is_cmdbuf_dispatch(api):
    return "VkCommandBuffer" == api.parameters[0].typeName

def emit_parameter_encode_write_packet_info(typeInfo, api, cgen):
    # Seqno and skipping dispatch serialize are for use with VULKAN_STREAM_FEATURE_QUEUE_SUBMIT_WITH_COMMANDS_BIT
    doSeqno = True
    doDispatchSerialize = True

    if is_cmdbuf_dispatch(api):
        doSeqno = False
        doDispatchSerialize = False

    if doSeqno:
        cgen.stmt("uint32_t packetSize_%s = 4 + 4 + (queueSubmitWithCommandsEnabled ? 4 : 0) + count" % (api.name))
    else:
        cgen.stmt("uint32_t packetSize_%s = 4 + 4 + count" % (api.name))

    if not doDispatchSerialize:
        cgen.stmt("if (queueSubmitWithCommandsEnabled) packetSize_%s -= 8" % api.name)

    cgen.stmt("uint8_t* streamPtr = %s->reserve(packetSize_%s)" % (STREAM, api.name))
    cgen.stmt("uint8_t* packetBeginPtr = streamPtr")
    cgen.stmt("uint8_t** streamPtrPtr = &streamPtr")
    cgen.stmt("uint32_t opcode_%s = OP_%s" % (api.name, api.name))

    if doSeqno:
        cgen.stmt("uint32_t seqno; if (queueSubmitWithCommandsEnabled) seqno = ResourceTracker::nextSeqno()")

    cgen.stmt("memcpy(streamPtr, &opcode_%s, sizeof(uint32_t)); streamPtr += sizeof(uint32_t)" % api.name)
    cgen.stmt("memcpy(streamPtr, &packetSize_%s, sizeof(uint32_t)); streamPtr += sizeof(uint32_t)" % api.name)

    if doSeqno:
        cgen.line("if (queueSubmitWithCommandsEnabled) { memcpy(streamPtr, &seqno, sizeof(uint32_t)); streamPtr += sizeof(uint32_t); }")

def emit_parameter_encode_do_parameter_write(typeInfo, api, cgen):
    encodingParams = EncodingParameters(api)

    dispatchDone = False

    for p in encodingParams.toWrite:
        if is_cmdbuf_dispatch(api) and not dispatchDone:
            cgen.beginIf("!queueSubmitWithCommandsEnabled")
            emit_marshal(typeInfo, p, cgen)
            cgen.endIf()
        else:
            emit_marshal(typeInfo, p, cgen)

        dispatchDone = True

def emit_parameter_encode_read(typeInfo, api, cgen):
    encodingParams = EncodingParameters(api)

    for p in encodingParams.toRead:
        if p.action == "create":
            cgen.stmt(
                "%s->setHandleMapping(%s->createMapping())" % \
                (STREAM, RESOURCES))
        emit_unmarshal(typeInfo, p, cgen)
        if p.action == "create":
            cgen.stmt(
                "%s->unsetHandleMapping()" % STREAM)
        emit_transform(typeInfo, p, cgen, variant="fromhost")

def emit_post(typeInfo, api, cgen):
    encodingParams = EncodingParameters(api)

    emit_custom_resource_postprocess(typeInfo, api, cgen)

    for p in encodingParams.toDestroy:
        emit_handlemap_destroy(typeInfo, p, cgen)

    doSeqno = True
    if is_cmdbuf_dispatch(api):
        doSeqno = False

    retType = api.getRetTypeExpr()

    if api.name in ENCODER_EXPLICIT_FLUSHED_APIS:
        cgen.stmt("stream->flush()");
        return

    if doSeqno:
        if retType == "void":
            encodingParams = EncodingParameters(api)
            if 0 == len(encodingParams.toRead):
                cgen.stmt("stream->flush()");

def emit_pool_free(cgen):
    cgen.stmt("++encodeCount")
    cgen.beginIf("0 == encodeCount % POOL_CLEAR_INTERVAL")
    cgen.stmt("pool->freeAll()")
    cgen.stmt("%s->clearPool()" % STREAM)
    cgen.endIf()

def emit_return_unmarshal(typeInfo, api, cgen):

    retType = api.getRetTypeExpr()

    if retType == "void":
        return

    retVar = api.getRetVarExpr()
    cgen.stmt("%s %s = (%s)0" % (retType, retVar, retType))
    cgen.stmt("%s->read(&%s, %s)" % \
              (STREAM, retVar, cgen.sizeofExpr(api.retType)))

def emit_return(typeInfo, api, cgen):
    if api.getRetTypeExpr() == "void":
        return

    retVar = api.getRetVarExpr()
    cgen.stmt("return %s" % retVar)

def emit_lock(cgen):
    cgen.stmt("(void)doLock");
    cgen.stmt("bool queueSubmitWithCommandsEnabled = sFeatureBits & VULKAN_STREAM_FEATURE_QUEUE_SUBMIT_WITH_COMMANDS_BIT")
    cgen.stmt("if (!queueSubmitWithCommandsEnabled && doLock) this->lock()")

def emit_unlock(cgen):
    cgen.stmt("if (!queueSubmitWithCommandsEnabled && doLock) this->unlock()")

def emit_debug_log(typeInfo, api, cgen):
    logFormat = []
    logVargs = []
    for param in api.parameters:
        if param.paramName == "doLock":
            continue

        paramFormatSpecifier = param.getPrintFormatSpecifier()
        if not paramFormatSpecifier:
            continue

        logFormat.append(param.paramName + ":" + paramFormatSpecifier)
        logVargs.append(param.paramName)

    logFormatStr = ", ".join(logFormat)
    logVargsStr = ", ".join(logVargs)

def emit_default_encoding(typeInfo, api, cgen):
    emit_debug_log(typeInfo, api, cgen)
    emit_lock(cgen)
    emit_parameter_encode_preamble_write(typeInfo, api, cgen)
    emit_parameter_encode_copy_unwrap_count(typeInfo, api, cgen)
    emit_parameter_encode_write_packet_info(typeInfo, api, cgen)
    emit_parameter_encode_do_parameter_write(typeInfo, api, cgen)
    emit_parameter_encode_read(typeInfo, api, cgen)
    emit_return_unmarshal(typeInfo, api, cgen)
    emit_post(typeInfo, api, cgen)
    emit_pool_free(cgen)
    emit_unlock(cgen)
    emit_return(typeInfo, api, cgen)

## Custom encoding definitions##################################################

def emit_only_goldfish_custom(typeInfo, api, cgen):
    emit_lock(cgen)
    cgen.vkApiCall( \
        api,
        customPrefix="sResourceTracker->on_",
        customParameters=custom_encoder_args(api) + \
                [p.paramName for p in api.parameters[:-1]])
    emit_unlock(cgen)
    emit_return(typeInfo, api, cgen)

def emit_only_resource_event(typeInfo, api, cgen):
    cgen.stmt("(void)doLock");
    input_result = None
    retExpr = api.getRetVarExpr()

    if retExpr:
        retType = api.getRetTypeExpr()
        input_result = SUCCESS_RET_TYPES[retType]
        cgen.stmt("%s %s = (%s)0" % (retType, retExpr, retType))

    cgen.stmt(
        (("%s = " % retExpr) if retExpr else "") +
        make_event_handler_call(
            "sResourceTracker",
            api,
            ENCODER_THIS_PARAM,
            input_result, cgen))

    if retExpr:
        emit_return(typeInfo, api, cgen)

def emit_with_custom_unwrap(custom):
    def call(typeInfo, api, cgen):
        emit_lock(cgen)
        emit_parameter_encode_preamble_write(typeInfo, api, cgen)
        emit_parameter_encode_copy_unwrap_count(
            typeInfo, api, cgen, customUnwrap=custom)
        emit_parameter_encode_write_packet_info(typeInfo, api, cgen)
        emit_parameter_encode_do_parameter_write(typeInfo, api, cgen)
        emit_parameter_encode_read(typeInfo, api, cgen)
        emit_return_unmarshal(typeInfo, api, cgen)
        emit_pool_free(cgen)
        emit_unlock(cgen)
        emit_return(typeInfo, api, cgen)
    return call

def encode_vkFlushMappedMemoryRanges(typeInfo, api, cgen):
    emit_lock(cgen)
    emit_parameter_encode_preamble_write(typeInfo, api, cgen)
    emit_parameter_encode_copy_unwrap_count(typeInfo, api, cgen)

    def emit_flush_ranges(streamVar):
        cgen.beginIf("!sResourceTracker->usingDirectMapping()")
        cgen.beginFor("uint32_t i = 0", "i < memoryRangeCount", "++i")
        cgen.stmt("auto range = pMemoryRanges[i]")
        cgen.stmt("auto memory = pMemoryRanges[i].memory")
        cgen.stmt("auto size = pMemoryRanges[i].size")
        cgen.stmt("auto offset = pMemoryRanges[i].offset")
        cgen.stmt("uint64_t streamSize = 0")
        cgen.stmt("if (!memory) { %s->write(&streamSize, sizeof(uint64_t)); continue; }" % streamVar)
        cgen.stmt("auto hostPtr = sResourceTracker->getMappedPointer(memory)")
        cgen.stmt("auto actualSize = size == VK_WHOLE_SIZE ? sResourceTracker->getMappedSize(memory) : size")
        cgen.stmt("if (!hostPtr) { %s->write(&streamSize, sizeof(uint64_t)); continue; }" % streamVar)
        cgen.stmt("streamSize = actualSize")
        cgen.stmt("%s->write(&streamSize, sizeof(uint64_t))" % streamVar)
        cgen.stmt("uint8_t* targetRange = hostPtr + offset")
        cgen.stmt("%s->write(targetRange, actualSize)" % streamVar)
        cgen.endFor()
        cgen.endIf()

    emit_parameter_encode_write_packet_info(typeInfo, api, cgen)
    emit_parameter_encode_do_parameter_write(typeInfo, api, cgen)

    emit_flush_ranges(STREAM)

    emit_parameter_encode_read(typeInfo, api, cgen)
    emit_return_unmarshal(typeInfo, api, cgen)
    emit_pool_free(cgen)
    emit_unlock(cgen)
    emit_return(typeInfo, api, cgen)

def encode_vkInvalidateMappedMemoryRanges(typeInfo, api, cgen):
    emit_lock(cgen)
    emit_parameter_encode_preamble_write(typeInfo, api, cgen)
    emit_parameter_encode_copy_unwrap_count(typeInfo, api, cgen)
    emit_parameter_encode_write_packet_info(typeInfo, api, cgen)
    emit_parameter_encode_do_parameter_write(typeInfo, api, cgen)
    emit_parameter_encode_read(typeInfo, api, cgen)
    emit_return_unmarshal(typeInfo, api, cgen)

    def emit_invalidate_ranges(streamVar):
        cgen.beginIf("!sResourceTracker->usingDirectMapping()")
        cgen.beginFor("uint32_t i = 0", "i < memoryRangeCount", "++i")
        cgen.stmt("auto range = pMemoryRanges[i]")
        cgen.stmt("auto memory = pMemoryRanges[i].memory")
        cgen.stmt("auto size = pMemoryRanges[i].size")
        cgen.stmt("auto offset = pMemoryRanges[i].offset")
        cgen.stmt("uint64_t streamSize = 0")
        cgen.stmt("if (!memory) { %s->read(&streamSize, sizeof(uint64_t)); continue; }" % streamVar)
        cgen.stmt("auto hostPtr = sResourceTracker->getMappedPointer(memory)")
        cgen.stmt("auto actualSize = size == VK_WHOLE_SIZE ? sResourceTracker->getMappedSize(memory) : size")
        cgen.stmt("if (!hostPtr) { %s->read(&streamSize, sizeof(uint64_t)); continue; }" % streamVar)
        cgen.stmt("streamSize = actualSize")
        cgen.stmt("%s->read(&streamSize, sizeof(uint64_t))" % streamVar)
        cgen.stmt("uint8_t* targetRange = hostPtr + offset")
        cgen.stmt("%s->read(targetRange, actualSize)" % streamVar)
        cgen.endFor()
        cgen.endIf()

    emit_invalidate_ranges(STREAM)
    emit_pool_free(cgen)
    emit_unlock(cgen)
    emit_return(typeInfo, api, cgen)

def emit_manual_inline(typeInfo, api, cgen):
    cgen.line("#include \"%s_encode_impl.cpp.inl\"" % api.name)

def unwrap_vkCreateImage_pCreateInfo():
    def mapOp(cgen, orig, local):
        cgen.stmt("sResourceTracker->unwrap_vkCreateImage_pCreateInfo(%s, %s)" %
                  (orig.paramName, local.paramName))
    return { "pCreateInfo" : { "mapOp" : mapOp } }

def unwrap_vkBindImageMemory2_pBindInfos():
    def mapOp(cgen, orig, local):
        cgen.stmt("sResourceTracker->unwrap_VkBindImageMemory2_pBindInfos(bindInfoCount, %s, %s)" %
                  (orig.paramName, local.paramName))
    return { "pBindInfos" : { "mapOp" : mapOp } }

def unwrap_vkAcquireImageANDROID_nativeFenceFd():
    def mapOp(cgen, orig, local):
        cgen.stmt("sResourceTracker->unwrap_vkAcquireImageANDROID_nativeFenceFd(%s, &%s)" %
                  (orig.paramName, local.paramName))
    return { "nativeFenceFd" : { "mapOp" : mapOp } }

custom_encodes = {
    "vkMapMemory" : emit_only_resource_event,
    "vkUnmapMemory" : emit_only_resource_event,
    "vkFlushMappedMemoryRanges" : encode_vkFlushMappedMemoryRanges,
    "vkInvalidateMappedMemoryRanges" : encode_vkInvalidateMappedMemoryRanges,
    "vkCreateImage" : emit_with_custom_unwrap(unwrap_vkCreateImage_pCreateInfo()),
    "vkCreateImageWithRequirementsGOOGLE" : emit_with_custom_unwrap(unwrap_vkCreateImage_pCreateInfo()),
    "vkBindImageMemory2": emit_with_custom_unwrap(unwrap_vkBindImageMemory2_pBindInfos()),
    "vkAcquireImageANDROID" : emit_with_custom_unwrap(unwrap_vkAcquireImageANDROID_nativeFenceFd()),
    "vkQueueFlushCommandsGOOGLE" : emit_manual_inline,
}

class VulkanEncoder(VulkanWrapperGenerator):
    def __init__(self, module, typeInfo):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)

        self.typeInfo = typeInfo

        self.cgenHeader = CodeGen()
        self.cgenHeader.incrIndent()

        self.cgenImpl = CodeGen()

    def onBegin(self,):
        self.module.appendHeader(encoder_decl_preamble)
        self.module.appendImpl(encoder_impl_preamble)

    def onGenCmd(self, cmdinfo, name, alias):
        VulkanWrapperGenerator.onGenCmd(self, cmdinfo, name, alias)

        api = copy.deepcopy(self.typeInfo.apis[name])
        api.parameters.append(makeVulkanTypeSimple(False, "uint32_t", 0, "doLock"))

        self.cgenHeader.stmt(self.cgenHeader.makeFuncProto(api))
        apiImpl = api.withModifiedName("VkEncoder::" + api.name)

        self.module.appendHeader(self.cgenHeader.swapCode())

        def emit_function_impl(cgen):
            if api.name in custom_encodes.keys():
                custom_encodes[api.name](self.typeInfo, api, cgen)
            else:
                emit_default_encoding(self.typeInfo, api, cgen)

        self.module.appendImpl(self.cgenImpl.makeFuncImpl(apiImpl, emit_function_impl))

    def onEnd(self,):
        self.module.appendHeader(encoder_decl_postamble)
        self.cgenHeader.decrIndent()
