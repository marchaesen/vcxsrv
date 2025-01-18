# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT

from .common.codegen import CodeGen
from .common.vulkantypes import \
        VulkanAPI, makeVulkanTypeSimple, iterateVulkanType

from .wrapperdefs import VulkanWrapperGenerator
from .wrapperdefs import STRUCT_EXTENSION_PARAM
from .wrapperdefs import STRUCT_EXTENSION_PARAM_FOR_WRITE
from .wrapperdefs import EXTENSION_SIZE_API_NAME
from .wrapperdefs import EXTENSION_SIZE_WITH_STREAM_FEATURES_API_NAME
from .wrapperdefs import STRUCT_TYPE_API_NAME

class VulkanExtensionStructs(VulkanWrapperGenerator):

    def __init__(self, module, typeInfo, variant="host"):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)

        self.codegen = CodeGen()

        self.variant = variant

        self.structTypeRetType = \
            makeVulkanTypeSimple(False, "uint32_t", 0)

        self.rootTypeVarName = "rootType"
        self.rootTypeParam = \
            makeVulkanTypeSimple(False, "VkStructureType",
                                 0, self.rootTypeVarName)
        self.structTypePrototype = \
            VulkanAPI(STRUCT_TYPE_API_NAME,
                      self.structTypeRetType,
                      [STRUCT_EXTENSION_PARAM])

        self.extensionStructSizeRetType = \
            makeVulkanTypeSimple(False, "size_t", 0)
        self.extensionStructSizePrototype = \
            VulkanAPI(EXTENSION_SIZE_API_NAME,
                      self.extensionStructSizeRetType,
                      [self.rootTypeParam, STRUCT_EXTENSION_PARAM])

        self.streamFeaturesType = makeVulkanTypeSimple(False, "uint32_t", 0, "streamFeatures")

        self.extensionStructSizeWithStreamFeaturesPrototype = \
            VulkanAPI(EXTENSION_SIZE_WITH_STREAM_FEATURES_API_NAME,
                      self.extensionStructSizeRetType,
                      [self.streamFeaturesType, self.rootTypeParam, STRUCT_EXTENSION_PARAM])
    def onBegin(self,):
        VulkanWrapperGenerator.onBegin(self)
        self.module.appendHeader(self.codegen.makeFuncDecl(
            self.structTypePrototype))
        self.module.appendHeader(self.codegen.makeFuncDecl(
            self.extensionStructSizePrototype))
        self.module.appendHeader(self.codegen.makeFuncDecl(
            self.extensionStructSizeWithStreamFeaturesPrototype))

    def onGenType(self, typeXml, name, alias):
        VulkanWrapperGenerator.onGenType(self, typeXml, name, alias)

    def onGenCmd(self, cmdinfo, name, alias):
        VulkanWrapperGenerator.onGenCmd(self, cmdinfo, name, alias)

    def onEnd(self,):
        VulkanWrapperGenerator.onEnd(self)

        def castAsStruct(varName, typeName, const=True):
            return "reinterpret_cast<%s%s*>(%s)" % \
                   ("const " if const else "", typeName, varName)

        def structTypeImpl(cgen):
            cgen.stmt(
                "const uint32_t asStructType = *(%s)" %
                (castAsStruct(STRUCT_EXTENSION_PARAM.paramName, "uint32_t")))
            cgen.stmt("return asStructType")

        self.module.appendImpl(
            self.codegen.makeFuncImpl(
                self.structTypePrototype, structTypeImpl))

        def forEachExtensionReturnSize(ext, _, cgen):
            cgen.stmt("return sizeof(%s)" % ext.name)

        def forEachExtensionReturnSizeProtectedByFeature(ext, _, cgen):
            streamFeature = ext.getProtectStreamFeature()
            if streamFeature is None:
                cgen.stmt("return sizeof(%s)" % ext.name)
                return
            cgen.beginIf("%s & %s" % ("streamFeatures", streamFeature))
            cgen.stmt("return sizeof(%s)" % ext.name)
            cgen.endIf()
            cgen.beginElse()
            cgen.stmt("return 0")
            cgen.endIf()

        def defaultAbortEmit(cgen):
            # The 'structType' name and return behavior are defined in
            # emitForEachStructExtension and not accessible here. Consequently,
            # this is a copy-paste from there and must be updated accordingly.
            # NOTE: No need for %% if no substitution is made.
            cgen.stmt("fprintf(stderr, \"Unhandled Vulkan structure type %s [%d], aborting.\\n\", string_VkStructureType(VkStructureType(structType)), structType)")
            cgen.stmt("GFXSTREAM_ABORT(::emugl::FatalError(::emugl::ABORT_REASON_OTHER))")
            cgen.stmt("return static_cast<%s>(0)" % self.extensionStructSizeRetType.typeName)

        self.module.appendImpl(
            self.codegen.makeFuncImpl(
                self.extensionStructSizePrototype,
                lambda cgen: self.emitForEachStructExtension(
                    cgen,
                    self.extensionStructSizeRetType,
                    STRUCT_EXTENSION_PARAM,
                    forEachExtensionReturnSize, autoBreak=False,
                    defaultEmit=(defaultAbortEmit if self.variant == "host" else None),
                    rootTypeVar=self.rootTypeParam)))

        self.module.appendImpl(
            self.codegen.makeFuncImpl(
                self.extensionStructSizeWithStreamFeaturesPrototype,
                lambda cgen: self.emitForEachStructExtension(
                    cgen,
                    self.extensionStructSizeRetType,
                    STRUCT_EXTENSION_PARAM,
                    forEachExtensionReturnSizeProtectedByFeature, autoBreak=False,
                    defaultEmit=(defaultAbortEmit if self.variant == "host" else None),
                    rootTypeVar=self.rootTypeParam)))
