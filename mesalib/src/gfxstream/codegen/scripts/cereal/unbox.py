# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT

from .common.codegen import CodeGen
from .common.vulkantypes import \
        VulkanCompoundType, VulkanAPI, makeVulkanTypeSimple, vulkanTypeNeedsTransform, vulkanTypeGetNeededTransformTypes, VulkanTypeIterator, iterateVulkanType, vulkanTypeforEachSubType, TRANSFORMED_TYPES

from .wrapperdefs import VulkanWrapperGenerator
from .wrapperdefs import STRUCT_EXTENSION_PARAM, STRUCT_EXTENSION_PARAM_FOR_WRITE

# This is different from others; it operations solely in terms of deepcopy and handlemap
class VulkanUnbox(VulkanWrapperGenerator):
    def __init__(self, module, typeInfo):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)

        self.codegen = CodeGen()

        self.unboxPrefix = "unbox"
        self.toUnboxVar = "toUnbox"
        self.poolParam = \
            makeVulkanTypeSimple(False, "BumpPool", 1, "pool")

        self.knownStructs = {}
        self.needsTransform = set([])

    def onBegin(self,):
        VulkanWrapperGenerator.onBegin(self)

    def onGenType(self, typeXml, name, alias):
        VulkanWrapperGenerator.onGenType(self, typeXml, name, alias)

        if name in self.knownStructs:
            return

        category = self.typeInfo.categoryOf(name)

        if category in ["struct", "union"] and alias:
            self.module.appendHeader(
                self.codegen.makeFuncAlias(self.unboxPrefix + "_" + name,
                                           self.unboxPrefix + "_" + alias))

        if category in ["struct", "union"] and not alias:
            structInfo = self.typeInfo.structs[name]
            self.knownStructs[name] = structInfo

            api = VulkanAPI( \
                self.unboxPrefix + "_" + name,
                makeVulkanTypeSimple(False, name, 1),
                [self.poolParam] + \
                [makeVulkanTypeSimple( \
                    True, name, 1, self.toUnboxVar)])

            def funcDefGenerator(cgen):
                cgen.stmt("BoxedHandleUnwrapMapping unboxMapping")
                cgen.stmt("%s* res = (%s*)pool->alloc(sizeof(const %s))" % (name, name, name))
                cgen.stmt("deepcopy_%s(pool, %s, %s)" % (name, self.toUnboxVar, "res"))
                cgen.stmt("handlemap_%s(%s, %s)" % (name, "&unboxMapping", "res"))
                cgen.stmt("return res")

            self.module.appendHeader(
                self.codegen.makeFuncDecl(api))
            self.module.appendImpl(
                self.codegen.makeFuncImpl(api, funcDefGenerator))

    def onGenCmd(self, cmdinfo, name, alias):
        VulkanWrapperGenerator.onGenCmd(self, cmdinfo, name, alias)

    def onEnd(self,):
        VulkanWrapperGenerator.onEnd(self)
