# Copyright 2023 Google LLC
# SPDX-License-Identifier: MIT

from .common.codegen import CodeGen, VulkanWrapperGenerator
from .common.vulkantypes import VulkanAPI, makeVulkanTypeSimple, iterateVulkanType, VulkanTypeInfo,\
    VulkanType

from .marshaling import VulkanMarshalingCodegen
from .reservedmarshaling import VulkanReservedMarshalingCodegen
from .transform import TransformCodegen

from .wrapperdefs import API_PREFIX_MARSHAL
from .wrapperdefs import API_PREFIX_RESERVEDUNMARSHAL
from .wrapperdefs import MAX_PACKET_LENGTH
from .wrapperdefs import VULKAN_STREAM_TYPE
from .wrapperdefs import ROOT_TYPE_DEFAULT_VALUE
from .wrapperdefs import RELAXED_APIS


SKIPPED_DECODER_DELETES = [
    "vkFreeDescriptorSets",
]

DELAYED_DECODER_DELETES = [
    "vkDestroyPipelineLayout",
]

DELAYED_DECODER_DELETE_DICT_ENTRIES = [
    "vkDestroyShaderModule",
]

global_state_prefix = "m_state->on_"

decoder_decl_preamble = """

namespace gfxstream {
class IOStream;
}  // namespace gfxstream

namespace gfxstream {
namespace vk {

class VkDecoder {
public:
    VkDecoder();
    ~VkDecoder();
    void setForSnapshotLoad(bool forSnapshotLoad);
    size_t decode(void* buf, size_t bufsize, IOStream* stream,
                  const ProcessResources* processResources, const VkDecoderContext&);
private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

}  // namespace vk
}  // namespace gfxstream

"""

decoder_impl_preamble ="""
namespace gfxstream {
namespace vk {

using android::base::MetricEventBadPacketLength;
using android::base::MetricEventDuplicateSequenceNum;

class VkDecoder::Impl {
public:
    Impl() : m_logCalls(android::base::getEnvironmentVariable("ANDROID_EMU_VK_LOG_CALLS") == "1"),
             m_vk(vkDispatch()),
             m_state(VkDecoderGlobalState::get()),
             m_vkStream(nullptr, m_state->getFeatures()),
             m_vkMemReadingStream(nullptr, m_state->getFeatures()),
             m_boxedHandleUnwrapMapping(m_state),
             m_boxedHandleCreateMapping(m_state),
             m_boxedHandleDestroyMapping(m_state),
             m_boxedHandleUnwrapAndDeleteMapping(m_state),
             m_boxedHandleUnwrapAndDeletePreserveBoxedMapping(m_state),
             m_prevSeqno(std::nullopt),
             m_queueSubmitWithCommandsEnabled(m_state->getFeatures().VulkanQueueSubmitWithCommands.enabled) {}
    %s* stream() { return &m_vkStream; }
    VulkanMemReadingStream* readStream() { return &m_vkMemReadingStream; }

    void setForSnapshotLoad(bool forSnapshotLoad) {
        m_forSnapshotLoad = forSnapshotLoad;
    }

    size_t decode(void* buf, size_t bufsize, IOStream* stream,
                  const ProcessResources* processResources, const VkDecoderContext&);

private:
    bool m_logCalls;
    bool m_forSnapshotLoad = false;
    VulkanDispatch* m_vk;
    VkDecoderGlobalState* m_state;
    %s m_vkStream;
    VulkanMemReadingStream m_vkMemReadingStream;
    BoxedHandleUnwrapMapping m_boxedHandleUnwrapMapping;
    BoxedHandleCreateMapping m_boxedHandleCreateMapping;
    BoxedHandleDestroyMapping m_boxedHandleDestroyMapping;
    BoxedHandleUnwrapAndDeleteMapping m_boxedHandleUnwrapAndDeleteMapping;
    android::base::BumpPool m_pool;
    BoxedHandleUnwrapAndDeletePreserveBoxedMapping m_boxedHandleUnwrapAndDeletePreserveBoxedMapping;
    std::optional<uint32_t> m_prevSeqno;
    bool m_queueSubmitWithCommandsEnabled = false;
};

VkDecoder::VkDecoder() :
    mImpl(new VkDecoder::Impl()) { }

VkDecoder::~VkDecoder() = default;

void VkDecoder::setForSnapshotLoad(bool forSnapshotLoad) {
    mImpl->setForSnapshotLoad(forSnapshotLoad);
}

size_t VkDecoder::decode(void* buf, size_t bufsize, IOStream* stream,
                         const ProcessResources* processResources,
                         const VkDecoderContext& context) {
    return mImpl->decode(buf, bufsize, stream, processResources, context);
}

// VkDecoder::Impl::decode to follow
""" % (VULKAN_STREAM_TYPE, VULKAN_STREAM_TYPE)

decoder_impl_postamble = """

}  // namespace vk
}  // namespace gfxstream

"""

READ_STREAM = "vkReadStream"
WRITE_STREAM = "vkStream"

# Driver workarounds for APIs that don't work well multithreaded
driver_workarounds_global_lock_apis = [ \
    "vkCreatePipelineLayout",
    "vkDestroyPipelineLayout",
]

def emit_param_decl_for_reading(param, cgen):
    if param.staticArrExpr:
        cgen.stmt(
            cgen.makeRichCTypeDecl(param.getForNonConstAccess()))
    else:
        cgen.stmt(
            cgen.makeRichCTypeDecl(param))

def emit_unmarshal(typeInfo, param, cgen, output = False, destroy = False, noUnbox = False):
    if destroy:
        iterateVulkanType(typeInfo, param, VulkanReservedMarshalingCodegen(
            cgen,
            "host",
            READ_STREAM,
            ROOT_TYPE_DEFAULT_VALUE,
            param.paramName,
            "readStreamPtrPtr",
            API_PREFIX_RESERVEDUNMARSHAL,
            "",
            direction="read",
            dynAlloc=True))
        lenAccess = cgen.generalLengthAccess(param)
        lenAccessGuard = cgen.generalLengthAccessGuard(param)
        if None == lenAccess or "1" == lenAccess:
            cgen.stmt("boxed_%s_preserve = %s" % (param.paramName, param.paramName))
            cgen.stmt("%s = try_unbox_%s(%s)" % (param.paramName, param.typeName, param.paramName))
        else:
            if lenAccessGuard is not None:
                cgen.beginIf(lenAccessGuard)
            cgen.beginFor("uint32_t i = 0", "i < %s" % lenAccess, "++i")
            cgen.stmt("boxed_%s_preserve[i] = %s[i]" % (param.paramName, param.paramName))
            cgen.stmt("((%s*)(%s))[i] = try_unbox_%s(%s[i])" % (param.typeName, param.paramName, param.typeName, param.paramName))
            cgen.endFor()
            if lenAccessGuard is not None:
                cgen.endIf()
    else:
        if noUnbox:
            cgen.line("// No unbox for %s" % (param.paramName))
        iterateVulkanType(typeInfo, param, VulkanReservedMarshalingCodegen(
            cgen,
            "host",
            READ_STREAM,
            ROOT_TYPE_DEFAULT_VALUE,
            param.paramName,
            "readStreamPtrPtr",
            API_PREFIX_RESERVEDUNMARSHAL,
            "" if (output or noUnbox) else "unbox_",
            direction="read",
            dynAlloc=True))


def emit_dispatch_unmarshal(typeInfo: VulkanTypeInfo, param: VulkanType, cgen, globalWrapped):
    cgen.stmt("// Begin {} wrapped dispatchable handle unboxing for {}".format(
        "global" if globalWrapped else "non",
        param.paramName))

    iterateVulkanType(typeInfo, param, VulkanReservedMarshalingCodegen(
        cgen,
        "host",
        READ_STREAM,
        ROOT_TYPE_DEFAULT_VALUE,
        param.paramName,
        "readStreamPtrPtr",
        API_PREFIX_RESERVEDUNMARSHAL,
        "",
        direction="read",
        dynAlloc=True))

    if not globalWrapped:
        cgen.stmt("auto unboxed_%s = unbox_%s(%s)" %
                  (param.paramName, param.typeName, param.paramName))
        cgen.stmt("auto vk = dispatch_%s(%s)" %
                  (param.typeName, param.paramName))
        cgen.stmt("// End manual dispatchable handle unboxing for %s" % param.paramName)


def emit_transform(typeInfo, param, cgen, variant="tohost"):
    res = iterateVulkanType(typeInfo, param, TransformCodegen(
        cgen, param.paramName, "m_state", "transform_%s_" % variant, variant))
    if not res:
        cgen.stmt("(void)%s" % param.paramName)


def emit_marshal(typeInfo, param, cgen, handleMapOverwrites=False):
    iterateVulkanType(typeInfo, param, VulkanMarshalingCodegen(
        cgen,
        WRITE_STREAM,
        ROOT_TYPE_DEFAULT_VALUE,
        param.paramName,
        API_PREFIX_MARSHAL,
        direction="write",
        handleMapOverwrites=handleMapOverwrites))


class DecodingParameters(object):
    def __init__(self, api: VulkanAPI):
        self.params: list[VulkanType] = []
        self.toRead: list[VulkanType] = []
        self.toWrite: list[VulkanType] = []

        for i, param in enumerate(api.parameters):
            if i == 0 and param.isDispatchableHandleType():
                param.dispatchHandle = True

            if param.isNonDispatchableHandleType() and param.isCreatedBy(api):
                param.nonDispatchableHandleCreate = True

            if param.isNonDispatchableHandleType() and param.isDestroyedBy(api):
                param.nonDispatchableHandleDestroy = True

            if param.isDispatchableHandleType() and param.isCreatedBy(api):
                param.dispatchableHandleCreate = True

            if param.isDispatchableHandleType() and param.isDestroyedBy(api):
                param.dispatchableHandleDestroy = True

            self.toRead.append(param)

            if param.possiblyOutput():
                self.toWrite.append(param)

            self.params.append(param)


def emit_call_log(api, cgen):
    decodingParams = DecodingParameters(api)
    paramsToRead = decodingParams.toRead

    cgen.beginIf("m_logCalls")
    paramLogFormat = ""
    paramLogArgs = []
    for p in paramsToRead:
        paramLogFormat += "0x%llx "
    for p in paramsToRead:
        paramLogArgs.append("(unsigned long long)%s" % (p.paramName))
    cgen.stmt("fprintf(stderr, \"stream %%p: call %s %s\\n\", ioStream, %s)" % (api.name, paramLogFormat, ", ".join(paramLogArgs)))
    cgen.endIf()

def emit_decode_parameters(typeInfo: VulkanTypeInfo, api: VulkanAPI, cgen, globalWrapped=False):
    decodingParams = DecodingParameters(api)

    paramsToRead = decodingParams.toRead

    for p in paramsToRead:
        emit_param_decl_for_reading(p, cgen)

    for i, p in enumerate(paramsToRead):
        lenAccess = cgen.generalLengthAccess(p)

        if p.dispatchHandle:
            if api.name in DELAYED_DECODER_DELETE_DICT_ENTRIES:
                emit_dispatch_unmarshal(typeInfo, p, cgen, False)
            else:
                emit_dispatch_unmarshal(typeInfo, p, cgen, globalWrapped)
        else:
            destroy = p.nonDispatchableHandleDestroy or p.dispatchableHandleDestroy
            noUnbox = api.name in ["vkQueueFlushCommandsGOOGLE", "vkQueueFlushCommandsFromAuxMemoryGOOGLE"] and p.paramName == "commandBuffer"

            if p.nonDispatchableHandleDestroy or p.dispatchableHandleDestroy:
                destroy = True
                cgen.stmt("// Begin manual non dispatchable handle destroy unboxing for %s" % p.paramName)
                if None == lenAccess or "1" == lenAccess:
                    cgen.stmt("%s boxed_%s_preserve" % (p.typeName, p.paramName))
                else:
                    cgen.stmt("%s* boxed_%s_preserve; %s->alloc((void**)&boxed_%s_preserve, %s * sizeof(%s))" % (p.typeName, p.paramName, READ_STREAM, p.paramName, lenAccess, p.typeName))

            if p.possiblyOutput():
                cgen.stmt("// Begin manual dispatchable handle unboxing for %s" % p.paramName)
                cgen.stmt("%s->unsetHandleMapping()" % READ_STREAM)

            emit_unmarshal(typeInfo, p, cgen, output = p.possiblyOutput(), destroy = destroy, noUnbox = noUnbox)

    for p in paramsToRead:
        emit_transform(typeInfo, p, cgen, variant="tohost")

    emit_call_log(api, cgen)

def emit_dispatch_call(api, cgen):

    decodingParams = DecodingParameters(api)

    customParams = []

    delay = api.name in DELAYED_DECODER_DELETES

    for i, p in enumerate(api.parameters):
        customParam = p.paramName
        if decodingParams.params[i].dispatchHandle:
            customParam = "unboxed_%s" % p.paramName
        customParams.append(customParam)

    if delay:
        cgen.line("std::function<void()> delayed_remove_callback = [vk, %s]() {" % ", ".join(customParams))

    if api.name in driver_workarounds_global_lock_apis:
        if delay:
            cgen.stmt("auto state = VkDecoderGlobalState::get()")
            cgen.stmt("// state already locked")
        else:
            cgen.stmt("m_state->lock()")

    cgen.vkApiCall(api, customPrefix="vk->", customParameters=customParams, \
        globalStatePrefix=global_state_prefix, checkForDeviceLost=True,
        checkForOutOfMemory=True)

    if api.name in driver_workarounds_global_lock_apis:
        if not delay:
            cgen.stmt("m_state->unlock()")
        # for delayed remove, state is already locked, so we do not need to
        # unlock

    if delay:
        cgen.line("};")

def emit_global_state_wrapped_call(api, cgen, context):
    if api.name in DELAYED_DECODER_DELETES:
        print("Error: Cannot generate a global state wrapped call that is also a delayed delete (yet)");
        raise

    customParams = ["&m_pool"] + list(map(lambda p: p.paramName, api.parameters))
    if context:
        customParams += ["context"]
    cgen.vkApiCall(api, customPrefix=global_state_prefix, \
        customParameters=customParams, globalStatePrefix=global_state_prefix, \
        checkForDeviceLost=True, checkForOutOfMemory=True)

def emit_decode_parameters_writeback(typeInfo, api, cgen, autobox=True):
    decodingParams = DecodingParameters(api)

    paramsToWrite = decodingParams.toWrite

    cgen.stmt("%s->unsetHandleMapping()" % WRITE_STREAM)

    handleMapOverwrites = False

    for p in paramsToWrite:
        emit_transform(typeInfo, p, cgen, variant="fromhost")

        handleMapOverwrites = False

        if p.nonDispatchableHandleCreate or p.dispatchableHandleCreate:
            handleMapOverwrites = True

        if autobox and p.nonDispatchableHandleCreate:
            cgen.stmt("// Begin auto non dispatchable handle create for %s" % p.paramName)
            cgen.stmt("if (%s == VK_SUCCESS) %s->setHandleMapping(&m_boxedHandleCreateMapping)" % \
                      (api.getRetVarExpr(), WRITE_STREAM))

        if (not autobox) and p.nonDispatchableHandleCreate:
            cgen.stmt("// Begin manual non dispatchable handle create for %s" % p.paramName)
            cgen.stmt("%s->unsetHandleMapping()" % WRITE_STREAM)

        emit_marshal(typeInfo, p, cgen, handleMapOverwrites=handleMapOverwrites)

        if autobox and p.nonDispatchableHandleCreate:
            cgen.stmt("// Begin auto non dispatchable handle create for %s" % p.paramName)
            cgen.stmt("%s->setHandleMapping(&m_boxedHandleUnwrapMapping)" % WRITE_STREAM)

        if (not autobox) and p.nonDispatchableHandleCreate:
            cgen.stmt("// Begin manual non dispatchable handle create for %s" % p.paramName)
            cgen.stmt("%s->setHandleMapping(&m_boxedHandleUnwrapMapping)" % WRITE_STREAM)

def emit_decode_return_writeback(api, cgen):
    retTypeName = api.getRetTypeExpr()
    if retTypeName != "void":
        retVar = api.getRetVarExpr()
        cgen.stmt("%s->write(&%s, %s)" %
            (WRITE_STREAM, retVar, cgen.sizeofExpr(api.retType)))

def emit_decode_finish(api, cgen):
    decodingParams = DecodingParameters(api)
    retTypeName = api.getRetTypeExpr()
    paramsToWrite = decodingParams.toWrite

    if retTypeName != "void" or len(paramsToWrite) != 0:
        cgen.stmt("%s->commitWrite()" % WRITE_STREAM)

def emit_destroyed_handle_cleanup(api, cgen):
    decodingParams = DecodingParameters(api)
    paramsToRead = decodingParams.toRead

    skipDelete = api.name in SKIPPED_DECODER_DELETES

    if skipDelete:
        cgen.line("// Skipping handle cleanup for %s" % api.name)
        return

    for p in paramsToRead:
        if p.dispatchHandle:
            pass
        else:
            lenAccess = cgen.generalLengthAccess(p)
            lenAccessGuard = cgen.generalLengthAccess(p)
            destroy = p.nonDispatchableHandleDestroy or p.dispatchableHandleDestroy
            if destroy:
                if None == lenAccess or "1" == lenAccess:
                    if api.name in DELAYED_DECODER_DELETES:
                        cgen.stmt("delayed_delete_%s(boxed_%s_preserve, unboxed_device, delayed_remove_callback)" % (p.typeName, p.paramName))
                    elif api.name in DELAYED_DECODER_DELETE_DICT_ENTRIES:
                        cgen.stmt("delayed_delete_%s(boxed_%s_preserve, unboxed_device, nullptr)" % (p.typeName, p.paramName))
                    else:
                        cgen.stmt("delete_%s(boxed_%s_preserve)" % (p.typeName, p.paramName))
                else:
                    if lenAccessGuard is not None:
                        cgen.beginIf(lenAccessGuard)
                    cgen.beginFor("uint32_t i = 0", "i < %s" % lenAccess, "++i")
                    if api.name in DELAYED_DECODER_DELETES:
                        cgen.stmt("delayed_delete_%s(boxed_%s_preserve[i], unboxed_device, delayed_remove_callback)" % (p.typeName, p.paramName))
                    else:
                        cgen.stmt("delete_%s(boxed_%s_preserve[i])" % (p.typeName, p.paramName))
                    cgen.endFor()
                    if lenAccessGuard is not None:
                        cgen.endIf()

def emit_pool_free(cgen):
    cgen.stmt("%s->clearPool()" % READ_STREAM)

def emit_seqno_incr(api, cgen):
    cgen.stmt("if (m_queueSubmitWithCommandsEnabled) seqnoPtr->fetch_add(1, std::memory_order_seq_cst)")

def emit_snapshot(typeInfo, api, cgen):

    cgen.stmt("%s->setReadPos((uintptr_t)(*readStreamPtrPtr) - (uintptr_t)snapshotTraceBegin)" % READ_STREAM)
    cgen.stmt("size_t snapshotTraceBytes = %s->endTrace()" % READ_STREAM)

    additionalParams = [ \
        makeVulkanTypeSimple(True, "uint8_t", 1, "snapshotTraceBegin"),
        makeVulkanTypeSimple(False, "size_t", 0, "snapshotTraceBytes"),
        makeVulkanTypeSimple(False, "android::base::BumpPool", 1, "&m_pool"),
    ]

    retTypeName = api.getRetTypeExpr()
    if retTypeName != "void":
        retVar = api.getRetVarExpr()
        additionalParams.append(makeVulkanTypeSimple(False, retTypeName, 0, retVar))

    paramsForSnapshot = []

    decodingParams = DecodingParameters(api)

    for p in decodingParams.toRead:
        if p.nonDispatchableHandleDestroy or (not p.dispatchHandle and p.dispatchableHandleDestroy):
            paramsForSnapshot.append(p.withModifiedName("boxed_%s_preserve" % p.paramName))
        else:
            paramsForSnapshot.append(p)

    customParams = additionalParams + paramsForSnapshot

    apiForSnapshot = \
        api.withCustomReturnType(makeVulkanTypeSimple(False, "void", 0, "void")). \
            withCustomParameters(customParams)

    cgen.beginIf("m_state->snapshotsEnabled()")
    cgen.vkApiCall(apiForSnapshot, customPrefix="m_state->snapshot()->")
    cgen.endIf()

def emit_decoding(typeInfo, api, cgen, globalWrapped=False, context=False):
    isAcquire = api.name in RELAXED_APIS
    emit_decode_parameters(typeInfo, api, cgen, globalWrapped)

    if isAcquire:
        emit_seqno_incr(api, cgen)

    if globalWrapped:
        emit_global_state_wrapped_call(api, cgen, context)
    else:
        emit_dispatch_call(api, cgen)

    emit_decode_parameters_writeback(typeInfo, api, cgen, autobox=not globalWrapped)
    emit_decode_return_writeback(api, cgen)
    emit_decode_finish(api, cgen)
    emit_snapshot(typeInfo, api, cgen)
    emit_destroyed_handle_cleanup(api, cgen)
    emit_pool_free(cgen)

    if not isAcquire:
        emit_seqno_incr(api, cgen)

def emit_default_decoding(typeInfo, api, cgen):
    emit_decoding(typeInfo, api, cgen)

def emit_global_state_wrapped_decoding(typeInfo, api, cgen):
    emit_decoding(typeInfo, api, cgen, globalWrapped=True)

def emit_global_state_wrapped_decoding_with_context(typeInfo, api, cgen):
    emit_decoding(typeInfo, api, cgen, globalWrapped=True, context=True)

## Custom decoding definitions##################################################
def decode_vkFlushMappedMemoryRanges(typeInfo: VulkanTypeInfo, api, cgen):
    emit_decode_parameters(typeInfo, api, cgen)

    cgen.beginIf("!m_state->usingDirectMapping()")
    cgen.stmt("// This is to deal with a deficiency in the encoder,");
    cgen.stmt("// where usingDirectMapping fails to set the proper packet size,");
    cgen.stmt("// meaning we can read off the end of the packet.");
    cgen.stmt("uint64_t sizeLeft = end - *readStreamPtrPtr")
    cgen.beginFor("uint32_t i = 0", "i < memoryRangeCount", "++i")
    cgen.beginIf("sizeLeft < sizeof(uint64_t)")
    cgen.beginIf("m_prevSeqno")
    cgen.stmt("m_prevSeqno = m_prevSeqno.value() - 1")
    cgen.endIf()
    cgen.stmt("return ptr - (unsigned char*)buf;")
    cgen.endIf()
    cgen.stmt("auto range = pMemoryRanges[i]")
    cgen.stmt("auto memory = pMemoryRanges[i].memory")
    cgen.stmt("auto size = pMemoryRanges[i].size")
    cgen.stmt("auto offset = pMemoryRanges[i].offset")
    cgen.stmt("uint64_t readStream = 0")
    cgen.stmt("memcpy(&readStream, *readStreamPtrPtr, sizeof(uint64_t)); *readStreamPtrPtr += sizeof(uint64_t)")
    cgen.stmt("sizeLeft -= sizeof(uint64_t)")
    cgen.stmt("auto hostPtr = m_state->getMappedHostPointer(memory)")
    cgen.stmt("if (!hostPtr && readStream > 0) GFXSTREAM_ABORT(::emugl::FatalError(::emugl::ABORT_REASON_OTHER))")
    cgen.stmt("if (!hostPtr) continue")
    cgen.beginIf("sizeLeft < readStream")
    cgen.beginIf("m_prevSeqno")
    cgen.stmt("m_prevSeqno = m_prevSeqno.value() - 1")
    cgen.endIf()
    cgen.stmt("return ptr - (unsigned char*)buf;")
    cgen.endIf()
    cgen.stmt("sizeLeft -= readStream")
    cgen.stmt("uint8_t* targetRange = hostPtr + offset")
    cgen.stmt("memcpy(targetRange, *readStreamPtrPtr, readStream); *readStreamPtrPtr += readStream")
    cgen.stmt("packetLen += 8 + readStream")
    cgen.endFor()
    cgen.endIf()

    emit_dispatch_call(api, cgen)
    emit_decode_parameters_writeback(typeInfo, api, cgen)
    emit_decode_return_writeback(api, cgen)
    emit_decode_finish(api, cgen)
    emit_snapshot(typeInfo, api, cgen);
    emit_pool_free(cgen)
    emit_seqno_incr(api, cgen)

def decode_vkInvalidateMappedMemoryRanges(typeInfo, api, cgen):
    emit_decode_parameters(typeInfo, api, cgen)
    emit_dispatch_call(api, cgen)
    emit_decode_parameters_writeback(typeInfo, api, cgen)
    emit_decode_return_writeback(api, cgen)

    cgen.beginIf("!m_state->usingDirectMapping()")
    cgen.beginFor("uint32_t i = 0", "i < memoryRangeCount", "++i")
    cgen.stmt("auto range = pMemoryRanges[i]")
    cgen.stmt("auto memory = range.memory")
    cgen.stmt("auto size = range.size")
    cgen.stmt("auto offset = range.offset")
    cgen.stmt("auto hostPtr = m_state->getMappedHostPointer(memory)")
    cgen.stmt("auto actualSize = size == VK_WHOLE_SIZE ? m_state->getDeviceMemorySize(memory) : size")
    cgen.stmt("uint64_t writeStream = 0")
    cgen.stmt("if (!hostPtr) { %s->write(&writeStream, sizeof(uint64_t)); continue; }" % WRITE_STREAM)
    cgen.stmt("uint8_t* targetRange = hostPtr + offset")
    cgen.stmt("writeStream = actualSize")
    cgen.stmt("%s->write(&writeStream, sizeof(uint64_t))" % WRITE_STREAM)
    cgen.stmt("%s->write(targetRange, actualSize)" % WRITE_STREAM)
    cgen.endFor()
    cgen.endIf()

    emit_decode_finish(api, cgen)
    emit_snapshot(typeInfo, api, cgen);
    emit_pool_free(cgen)
    emit_seqno_incr(api, cgen)

def decode_unsupported_api(typeInfo, api, cgen):
    cgen.line(f"// Decoding {api.name} is not supported. This should not run.")
    cgen.stmt(f"fprintf(stderr, \"stream %p: fatal: decoding unsupported API {api.name}\\n\", ioStream)");
    cgen.stmt("__builtin_trap()")

custom_decodes = {
    "vkEnumerateInstanceVersion" : emit_global_state_wrapped_decoding,
    "vkCreateInstance" : emit_global_state_wrapped_decoding,
    "vkDestroyInstance" : emit_global_state_wrapped_decoding,
    "vkEnumeratePhysicalDevices" : emit_global_state_wrapped_decoding,

    "vkGetPhysicalDeviceFeatures" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceFeatures2" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceFeatures2KHR" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceFormatProperties" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceFormatProperties2" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceFormatProperties2KHR" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceImageFormatProperties" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceImageFormatProperties2" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceImageFormatProperties2KHR" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceProperties" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceProperties2" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceProperties2KHR" : emit_global_state_wrapped_decoding,

    "vkGetPhysicalDeviceMemoryProperties" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceMemoryProperties2" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceMemoryProperties2KHR" : emit_global_state_wrapped_decoding,

    "vkGetPhysicalDeviceExternalSemaphoreProperties" : emit_global_state_wrapped_decoding,
    "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR" : emit_global_state_wrapped_decoding,

    "vkEnumerateDeviceExtensionProperties" : emit_global_state_wrapped_decoding,

    "vkCreateBuffer" : emit_global_state_wrapped_decoding,
    "vkDestroyBuffer" : emit_global_state_wrapped_decoding,

    "vkBindBufferMemory" : emit_global_state_wrapped_decoding,
    "vkBindBufferMemory2" : emit_global_state_wrapped_decoding,
    "vkBindBufferMemory2KHR" : emit_global_state_wrapped_decoding,

    "vkCreateDevice" : emit_global_state_wrapped_decoding,
    "vkGetDeviceQueue" : emit_global_state_wrapped_decoding,
    "vkDestroyDevice" : emit_global_state_wrapped_decoding,

    "vkGetDeviceQueue2" : emit_global_state_wrapped_decoding,

    "vkBindImageMemory" : emit_global_state_wrapped_decoding,
    "vkBindImageMemory2" : emit_global_state_wrapped_decoding,
    "vkBindImageMemory2KHR" : emit_global_state_wrapped_decoding,

    "vkCreateImage" : emit_global_state_wrapped_decoding,
    "vkCreateImageView" : emit_global_state_wrapped_decoding,
    "vkCreateSampler" : emit_global_state_wrapped_decoding,
    "vkDestroyImage" : emit_global_state_wrapped_decoding,
    "vkDestroyImageView" : emit_global_state_wrapped_decoding,
    "vkDestroySampler" : emit_global_state_wrapped_decoding,
    "vkCmdCopyBufferToImage" : emit_global_state_wrapped_decoding_with_context,
    "vkCmdCopyImage" : emit_global_state_wrapped_decoding,
    "vkCmdCopyImageToBuffer" : emit_global_state_wrapped_decoding,
    "vkCmdCopyBufferToImage2" : emit_global_state_wrapped_decoding_with_context,
    "vkCmdCopyImage2" : emit_global_state_wrapped_decoding,
    "vkCmdCopyImageToBuffer2" : emit_global_state_wrapped_decoding,
    "vkGetImageMemoryRequirements" : emit_global_state_wrapped_decoding,
    "vkGetImageMemoryRequirements2" : emit_global_state_wrapped_decoding,
    "vkGetImageMemoryRequirements2KHR" : emit_global_state_wrapped_decoding,
    "vkGetBufferMemoryRequirements" : emit_global_state_wrapped_decoding,
    "vkGetBufferMemoryRequirements2": emit_global_state_wrapped_decoding,
    "vkGetBufferMemoryRequirements2KHR": emit_global_state_wrapped_decoding,

    "vkCreateDescriptorSetLayout" : emit_global_state_wrapped_decoding,
    "vkDestroyDescriptorSetLayout" : emit_global_state_wrapped_decoding,
    "vkCreateDescriptorPool" : emit_global_state_wrapped_decoding,
    "vkDestroyDescriptorPool" : emit_global_state_wrapped_decoding,
    "vkResetDescriptorPool" : emit_global_state_wrapped_decoding,
    "vkAllocateDescriptorSets" : emit_global_state_wrapped_decoding,
    "vkFreeDescriptorSets" : emit_global_state_wrapped_decoding,

    "vkUpdateDescriptorSets" : emit_global_state_wrapped_decoding,

    "vkCreateShaderModule": emit_global_state_wrapped_decoding,
    "vkDestroyShaderModule": emit_global_state_wrapped_decoding,
    "vkCreatePipelineCache": emit_global_state_wrapped_decoding,
    "vkDestroyPipelineCache": emit_global_state_wrapped_decoding,
    "vkCreateGraphicsPipelines": emit_global_state_wrapped_decoding,
    "vkDestroyPipeline": emit_global_state_wrapped_decoding,

    "vkAllocateMemory" : emit_global_state_wrapped_decoding,
    "vkFreeMemory" : emit_global_state_wrapped_decoding,
    "vkMapMemory" : emit_global_state_wrapped_decoding,
    "vkUnmapMemory" : emit_global_state_wrapped_decoding,
    "vkFlushMappedMemoryRanges" : decode_vkFlushMappedMemoryRanges,
    "vkInvalidateMappedMemoryRanges" : decode_vkInvalidateMappedMemoryRanges,

    "vkAllocateCommandBuffers" : emit_global_state_wrapped_decoding,
    "vkCmdExecuteCommands" : emit_global_state_wrapped_decoding,
    "vkQueueSubmit" : emit_global_state_wrapped_decoding,
    "vkQueueSubmit2" : emit_global_state_wrapped_decoding,
    "vkQueueWaitIdle" : emit_global_state_wrapped_decoding,
    "vkBeginCommandBuffer" : emit_global_state_wrapped_decoding_with_context,
    "vkEndCommandBuffer" : emit_global_state_wrapped_decoding_with_context,
    "vkResetCommandBuffer" : emit_global_state_wrapped_decoding,
    "vkFreeCommandBuffers" : emit_global_state_wrapped_decoding,
    "vkCreateCommandPool" : emit_global_state_wrapped_decoding,
    "vkDestroyCommandPool" : emit_global_state_wrapped_decoding,
    "vkResetCommandPool" : emit_global_state_wrapped_decoding,
    "vkCmdPipelineBarrier" : emit_global_state_wrapped_decoding,
    "vkCmdPipelineBarrier2" : emit_global_state_wrapped_decoding,
    "vkCmdBindPipeline" : emit_global_state_wrapped_decoding,
    "vkCmdBindDescriptorSets" : emit_global_state_wrapped_decoding,

    "vkCreateRenderPass" : emit_global_state_wrapped_decoding,
    "vkCreateRenderPass2" : emit_global_state_wrapped_decoding,
    "vkCreateRenderPass2KHR" : emit_global_state_wrapped_decoding,
    "vkDestroyRenderPass" : emit_global_state_wrapped_decoding,
    "vkCreateFramebuffer" : emit_global_state_wrapped_decoding,
    "vkDestroyFramebuffer" : emit_global_state_wrapped_decoding,
    "vkDestroyFramebuffer" : emit_global_state_wrapped_decoding,
    "vkCmdBeginRenderPass" : emit_global_state_wrapped_decoding,
    "vkCmdBeginRenderPass2" : emit_global_state_wrapped_decoding,
    "vkCmdBeginRenderPass2KHR" : emit_global_state_wrapped_decoding,

    "vkCreateSamplerYcbcrConversion": emit_global_state_wrapped_decoding,
    "vkDestroySamplerYcbcrConversion": emit_global_state_wrapped_decoding,

    # VK_ANDROID_native_buffer
    "vkGetSwapchainGrallocUsageANDROID" : emit_global_state_wrapped_decoding,
    "vkGetSwapchainGrallocUsage2ANDROID" : emit_global_state_wrapped_decoding,
    "vkAcquireImageANDROID" : emit_global_state_wrapped_decoding,
    "vkQueueSignalReleaseImageANDROID" : emit_global_state_wrapped_decoding,

    "vkCreateSemaphore" : emit_global_state_wrapped_decoding,
    "vkGetSemaphoreFdKHR" : emit_global_state_wrapped_decoding,
    "vkImportSemaphoreFdKHR" : emit_global_state_wrapped_decoding,
    "vkDestroySemaphore" : emit_global_state_wrapped_decoding,

    "vkCreateFence" : emit_global_state_wrapped_decoding,
    "vkResetFences" : emit_global_state_wrapped_decoding,
    "vkDestroyFence" : emit_global_state_wrapped_decoding,

    # VK_GOOGLE_gfxstream
    "vkFreeMemorySyncGOOGLE" : emit_global_state_wrapped_decoding,
    "vkMapMemoryIntoAddressSpaceGOOGLE" : emit_global_state_wrapped_decoding,
    "vkGetMemoryHostAddressInfoGOOGLE" : emit_global_state_wrapped_decoding,
    "vkGetBlobGOOGLE" : emit_global_state_wrapped_decoding,
    "vkGetSemaphoreGOOGLE" : emit_global_state_wrapped_decoding,

    # Descriptor update templates
    "vkCreateDescriptorUpdateTemplate" : emit_global_state_wrapped_decoding,
    "vkCreateDescriptorUpdateTemplateKHR" : emit_global_state_wrapped_decoding,
    "vkDestroyDescriptorUpdateTemplate" : emit_global_state_wrapped_decoding,
    "vkDestroyDescriptorUpdateTemplateKHR" : emit_global_state_wrapped_decoding,
    "vkUpdateDescriptorSetWithTemplateSizedGOOGLE" : emit_global_state_wrapped_decoding,
    "vkUpdateDescriptorSetWithTemplateSized2GOOGLE" : emit_global_state_wrapped_decoding,

    # VK_GOOGLE_gfxstream
    "vkBeginCommandBufferAsyncGOOGLE" : emit_global_state_wrapped_decoding_with_context,
    "vkEndCommandBufferAsyncGOOGLE" : emit_global_state_wrapped_decoding_with_context,
    "vkResetCommandBufferAsyncGOOGLE" : emit_global_state_wrapped_decoding,
    "vkCommandBufferHostSyncGOOGLE" : emit_global_state_wrapped_decoding,
    "vkCreateImageWithRequirementsGOOGLE" : emit_global_state_wrapped_decoding,
    "vkCreateBufferWithRequirementsGOOGLE" : emit_global_state_wrapped_decoding,
    "vkQueueHostSyncGOOGLE" : emit_global_state_wrapped_decoding,
    "vkQueueSubmitAsyncGOOGLE" : emit_global_state_wrapped_decoding,
    "vkQueueSubmitAsync2GOOGLE" : emit_global_state_wrapped_decoding,
    "vkQueueWaitIdleAsyncGOOGLE" : emit_global_state_wrapped_decoding,
    "vkQueueBindSparseAsyncGOOGLE" : emit_global_state_wrapped_decoding,
    "vkGetLinearImageLayoutGOOGLE" : emit_global_state_wrapped_decoding,
    "vkGetLinearImageLayout2GOOGLE" : emit_global_state_wrapped_decoding,
    "vkQueueFlushCommandsGOOGLE" : emit_global_state_wrapped_decoding_with_context,
    "vkQueueFlushCommandsFromAuxMemoryGOOGLE" : emit_global_state_wrapped_decoding_with_context,
    "vkQueueCommitDescriptorSetUpdatesGOOGLE" : emit_global_state_wrapped_decoding,
    "vkCollectDescriptorPoolIdsGOOGLE" : emit_global_state_wrapped_decoding,
    "vkQueueSignalReleaseImageANDROIDAsyncGOOGLE" : emit_global_state_wrapped_decoding,

    "vkQueueBindSparse" : emit_global_state_wrapped_decoding,

    # VK_KHR_xcb_surface
    "vkCreateXcbSurfaceKHR": decode_unsupported_api,
    "vkGetPhysicalDeviceXcbPresentationSupportKHR": decode_unsupported_api,

    # VK_EXT_metal_surface
    "vkCreateMetalSurfaceEXT": decode_unsupported_api,

    # VK_KHR_sampler_ycbcr_conversion
    "vkCreateSamplerYcbcrConversionKHR": emit_global_state_wrapped_decoding,
    "vkDestroySamplerYcbcrConversionKHR": emit_global_state_wrapped_decoding,

    #VK_KHR_copy_commands2
    "vkCmdCopyBufferToImage2KHR" : emit_global_state_wrapped_decoding_with_context,
    "vkCmdCopyImage2KHR" : emit_global_state_wrapped_decoding,
    "vkCmdCopyImageToBuffer2KHR" : emit_global_state_wrapped_decoding,

    # VK_KHR_device_group_creation / VK_VERSION_1_1
    "vkEnumeratePhysicalDeviceGroups" : emit_global_state_wrapped_decoding,
    "vkEnumeratePhysicalDeviceGroupsKHR" : emit_global_state_wrapped_decoding,
}

class VulkanDecoder(VulkanWrapperGenerator):
    def __init__(self, module, typeInfo):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)
        self.typeInfo: VulkanTypeInfo = typeInfo
        self.cgen = CodeGen()

    def onBegin(self,):
        self.module.appendImpl(
            "#define MAX_PACKET_LENGTH %s\n" % MAX_PACKET_LENGTH)
        self.module.appendHeader(decoder_decl_preamble)
        self.module.appendImpl(decoder_impl_preamble)

        self.module.appendImpl(
            """
size_t VkDecoder::Impl::decode(void* buf, size_t len, IOStream* ioStream,
                               const ProcessResources* processResources,
                               const VkDecoderContext& context)
""")

        self.cgen.beginBlock() # function body

        self.cgen.stmt("const char* processName = context.processName")
        self.cgen.stmt("auto& gfx_logger = *context.gfxApiLogger")
        self.cgen.stmt("auto* healthMonitor = context.healthMonitor")
        self.cgen.stmt("auto& metricsLogger = *context.metricsLogger")
        self.cgen.stmt("if (len < 8) return 0")
        self.cgen.stmt("unsigned char *ptr = (unsigned char *)buf")
        self.cgen.stmt("const unsigned char* const end = (const unsigned char*)buf + len")

        self.cgen.beginIf("m_forSnapshotLoad")
        self.cgen.stmt("ptr += m_state->setCreatedHandlesForSnapshotLoad(ptr)");
        self.cgen.endIf()
        self.cgen.line("while (end - ptr >= 8)")
        self.cgen.beginBlock() # while loop

        self.cgen.stmt("uint32_t opcode = *(uint32_t *)ptr")
        self.cgen.stmt("uint32_t packetLen = *(uint32_t *)(ptr + 4)")
        self.cgen.line("""
        // packetLen should be at least 8 (op code and packet length) and should not be excessively large
        if (packetLen < 8 || packetLen > MAX_PACKET_LENGTH) {
            WARN("Bad packet length %d detected, decode may fail", packetLen);
            metricsLogger.logMetricEvent(MetricEventBadPacketLength{ .len = packetLen });
        }
        """)
        self.cgen.stmt("if (end - ptr < packetLen) return ptr - (unsigned char*)buf")
        self.cgen.stmt("gfx_logger.record(ptr, std::min(size_t(packetLen + 8), size_t(end - ptr)))")

        self.cgen.stmt("stream()->setStream(ioStream)")
        self.cgen.stmt("VulkanStream* %s = stream()" % WRITE_STREAM)
        self.cgen.stmt("VulkanMemReadingStream* %s = readStream()" % READ_STREAM)
        self.cgen.stmt("%s->setBuf((uint8_t*)(ptr + 8))" % READ_STREAM)
        self.cgen.stmt("uint8_t* readStreamPtr = %s->getBuf(); uint8_t** readStreamPtrPtr = &readStreamPtr" % READ_STREAM)
        self.cgen.stmt("uint8_t* snapshotTraceBegin = %s->beginTrace()" % READ_STREAM)
        self.cgen.stmt("%s->setHandleMapping(&m_boxedHandleUnwrapMapping)" % READ_STREAM)
        self.cgen.line("""
        std::unique_ptr<EventHangMetadata::HangAnnotations> executionData =
            std::make_unique<EventHangMetadata::HangAnnotations>();
        if (healthMonitor) {
            executionData->insert(
                 {{"packet_length", std::to_string(packetLen)},
                 {"opcode", std::to_string(opcode)}});
            if (processName) {
                executionData->insert(
                    {{"renderthread_guest_process", std::string(processName)}});
            }
            if (m_prevSeqno) {
                executionData->insert({{"previous_seqno", std::to_string(m_prevSeqno.value())}});
            }
        }

        std::atomic<uint32_t>* seqnoPtr = processResources ?
                processResources->getSequenceNumberPtr() : nullptr;

        if (m_queueSubmitWithCommandsEnabled && ((opcode >= OP_vkFirst && opcode < OP_vkLast) || (opcode >= OP_vkFirst_old && opcode < OP_vkLast_old))) {
            uint32_t seqno;
            memcpy(&seqno, *readStreamPtrPtr, sizeof(uint32_t)); *readStreamPtrPtr += sizeof(uint32_t);
            if (healthMonitor) executionData->insert({{"seqno", std::to_string(seqno)}});
            if (m_prevSeqno  && seqno == m_prevSeqno.value()) {
                WARN(
                    "Seqno %d is the same as previously processed on thread %d. It might be a "
                    "duplicate command.",
                    seqno, getCurrentThreadId());
                metricsLogger.logMetricEvent(MetricEventDuplicateSequenceNum{ .opcode = opcode });
            }
            if (seqnoPtr && !m_forSnapshotLoad) {
                {
                    auto seqnoWatchdog =
                        WATCHDOG_BUILDER(healthMonitor,
                                         "RenderThread seqno loop")
                            .setHangType(EventHangMetadata::HangType::kRenderThread)
                            .setAnnotations(std::make_unique<EventHangMetadata::HangAnnotations>(*executionData))
                            /* Data gathered if this hangs*/
                            .setOnHangCallback([=]() {
                                auto annotations = std::make_unique<EventHangMetadata::HangAnnotations>();
                                annotations->insert({{"seqnoPtr", std::to_string(seqnoPtr->load(std::memory_order_seq_cst))}});
                                return annotations;
                            })
                            .build();
                    while ((seqno - seqnoPtr->load(std::memory_order_seq_cst) != 1)) {
                        #if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)))
                        _mm_pause();
                        #elif (defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__)))
                        __asm__ __volatile__("pause;");
                        #endif
                    }
                    m_prevSeqno = seqno;
                }
            }
        }
        """)

        self.cgen.line("""
        gfx_logger.recordCommandExecution();
        """)

        self.cgen.line("""
        auto executionWatchdog =
            WATCHDOG_BUILDER(healthMonitor, "RenderThread VkDecoder command execution")
                .setHangType(EventHangMetadata::HangType::kRenderThread)
                .setAnnotations(std::move(executionData))
                .build();
        """)

        self.cgen.stmt("auto vk = m_vk")

        self.cgen.line("switch (opcode)")
        self.cgen.beginBlock()  # switch stmt

        self.module.appendImpl(self.cgen.swapCode())

    def onGenCmd(self, cmdinfo, name, alias):
        typeInfo = self.typeInfo
        cgen = self.cgen
        api: VulkanAPI = typeInfo.apis[name]

        cgen.line("case OP_%s:" % name)
        cgen.beginBlock()
        cgen.stmt("GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_DECODER_CATEGORY, \"VkDecoder %s\")" % name)

        if api.name in custom_decodes.keys():
            custom_decodes[api.name](typeInfo, api, cgen)
        else:
            emit_default_decoding(typeInfo, api, cgen)

        cgen.stmt("break")
        cgen.endBlock()
        self.module.appendImpl(self.cgen.swapCode())

    def onEnd(self,):
        self.cgen.line("default:")
        self.cgen.beginBlock()
        self.cgen.stmt("m_pool.freeAll()")
        self.cgen.stmt("return ptr - (unsigned char *)buf")
        self.cgen.endBlock()

        self.cgen.endBlock() # switch stmt

        self.cgen.stmt("ptr += packetLen")
        self.cgen.endBlock() # while loop

        self.cgen.beginIf("m_forSnapshotLoad")
        self.cgen.stmt("m_state->clearCreatedHandlesForSnapshotLoad()");
        self.cgen.endIf()

        self.cgen.stmt("m_pool.freeAll()")
        self.cgen.stmt("return ptr - (unsigned char*)buf;")
        self.cgen.endBlock() # function body
        self.module.appendImpl(self.cgen.swapCode())
        self.module.appendImpl(decoder_impl_postamble)
