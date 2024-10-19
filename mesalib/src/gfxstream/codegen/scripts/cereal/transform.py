# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT

from .common.codegen import CodeGen
from .common.vulkantypes import \
        VulkanCompoundType, VulkanAPI, makeVulkanTypeSimple, vulkanTypeNeedsTransform, vulkanTypeGetNeededTransformTypes, VulkanTypeIterator, iterateVulkanType, vulkanTypeforEachSubType, TRIVIAL_TRANSFORMED_TYPES, NON_TRIVIAL_TRANSFORMED_TYPES, TRANSFORMED_TYPES

from .wrapperdefs import VulkanWrapperGenerator
from .wrapperdefs import STRUCT_EXTENSION_PARAM, STRUCT_EXTENSION_PARAM_FOR_WRITE

def deviceMemoryTransform(resourceTrackerVarName, structOrApiInfo, getExpr, getLen, cgen, variant="tohost"):
    paramIndices = \
        structOrApiInfo.deviceMemoryInfoParameterIndices

    for _, info in paramIndices.items():
        orderedKeys = [
            "handle",
            "offset",
            "size",
            "typeIndex",
            "typeBits",]
        
        casts = {
            "handle" : "VkDeviceMemory*",
            "offset" : "VkDeviceSize*",
            "size" : "VkDeviceSize*",
            "typeIndex" : "uint32_t*",
            "typeBits" : "uint32_t*",
        }

        accesses = {
            "handle" : "nullptr",
            "offset" : "nullptr",
            "size" : "nullptr",
            "typeIndex" : "nullptr",
            "typeBits" : "nullptr",
        }

        lenAccesses = {
            "handle" : "0",
            "offset" : "0",
            "size" : "0",
            "typeIndex" : "0",
            "typeBits" : "0",
        }

        def doParam(i, vulkanType):
            access = getExpr(vulkanType)
            lenAccess = getLen(vulkanType)

            for k in orderedKeys:
                if i == info.__dict__[k]:
                    accesses[k] = access
                    if lenAccess is not None:
                        lenAccesses[k] = lenAccess
                    else:
                        lenAccesses[k] = "1"

        vulkanTypeforEachSubType(structOrApiInfo, doParam)

        callParams = ", ".join( \
            ["(%s)%s, %s" % (casts[k], accesses[k], lenAccesses[k]) \
                for k in orderedKeys])

        if variant == "tohost":
            cgen.stmt("%s->deviceMemoryTransform_tohost(%s)" % \
                (resourceTrackerVarName, callParams))
        else:
            cgen.stmt("%s->deviceMemoryTransform_fromhost(%s)" % \
                (resourceTrackerVarName, callParams))

def directTransform(resourceTrackerVarName, vulkanType, getExpr, getLen, cgen, variant="tohost"):
    access = getExpr(vulkanType)
    lenAccess = getLen(vulkanType)

    if lenAccess:
        finalLenAccess = lenAccess
    else:
        finalLenAccess = "1"

    cgen.stmt("%s->transformImpl_%s_%s(%s, %s)" % (resourceTrackerVarName,
                                                   vulkanType.typeName, variant, access, finalLenAccess))

def genTransformsForVulkanType(resourceTrackerVarName, structOrApiInfo, getExpr, getLen, cgen, variant="tohost"):
    for transform in vulkanTypeGetNeededTransformTypes(structOrApiInfo):
        if transform == "devicememory":
            deviceMemoryTransform( \
                resourceTrackerVarName,
                structOrApiInfo,
                getExpr, getLen, cgen, variant=variant)

class TransformCodegen(VulkanTypeIterator):
    def __init__(self, cgen, inputVar, resourceTrackerVarName, prefix, variant):
        self.cgen = cgen
        self.inputVar = inputVar
        self.prefix = prefix
        self.resourceTrackerVarName = resourceTrackerVarName

        def makeAccess(varName, asPtr = True):
            return lambda t: self.cgen.generalAccess(t, parentVarName = varName, asPtr = asPtr)

        def makeLengthAccess(varName):
            return lambda t: self.cgen.generalLengthAccess(t, parentVarName = varName)

        def makeLengthAccessGuard(varName):
            return lambda t: self.cgen.generalLengthAccessGuard(t, parentVarName=varName)

        self.exprAccessor = makeAccess(self.inputVar)
        self.exprAccessorValue = makeAccess(self.inputVar, asPtr = False)
        self.lenAccessor = makeLengthAccess(self.inputVar)
        self.lenAccessorGuard = makeLengthAccessGuard(self.inputVar)

        self.checked = False

        self.variant = variant

    def makeCastExpr(self, vulkanType):
        return "(%s)" % (
            self.cgen.makeCTypeDecl(vulkanType, useParamName=False))

    def asNonConstCast(self, access, vulkanType):
        if vulkanType.staticArrExpr:
            casted = "%s(%s)" % (self.makeCastExpr(vulkanType.getForAddressAccess().getForNonConstAccess()), access)
        elif vulkanType.accessibleAsPointer():
            casted = "%s(%s)" % (self.makeCastExpr(vulkanType.getForNonConstAccess()), access)
        else:
            casted = "%s(%s)" % (self.makeCastExpr(vulkanType.getForAddressAccess().getForNonConstAccess()), access)
        return casted

    def onCheck(self, vulkanType):
        pass

    def endCheck(self, vulkanType):
        pass

    def onCompoundType(self, vulkanType):

        access = self.exprAccessor(vulkanType)
        lenAccess = self.lenAccessor(vulkanType)
        lenAccessGuard = self.lenAccessorGuard(vulkanType)

        isPtr = vulkanType.pointerIndirectionLevels > 0

        if lenAccessGuard is not None:
            self.cgen.beginIf(lenAccessGuard)

        if isPtr:
            self.cgen.beginIf(access)

        if lenAccess is not None:

            loopVar = "i"
            access = "%s + %s" % (access, loopVar)
            forInit = "uint32_t %s = 0" % loopVar
            forCond = "%s < (uint32_t)%s" % (loopVar, lenAccess)
            forIncr = "++%s" % loopVar

            self.cgen.beginFor(forInit, forCond, forIncr)

        accessCasted = self.asNonConstCast(access, vulkanType)

        if vulkanType.isTransformed:
            directTransform(self.resourceTrackerVarName, vulkanType, self.exprAccessor, self.lenAccessor, self.cgen, variant=self.variant)

        self.cgen.funcCall(None, self.prefix + vulkanType.typeName,
                           [self.resourceTrackerVarName, accessCasted])

        if lenAccess is not None:
            self.cgen.endFor()

        if isPtr:
            self.cgen.endIf()

        if lenAccessGuard is not None:
            self.cgen.endIf()

    def onString(self, vulkanType):
        pass

    def onStringArray(self, vulkanType):
        pass

    def onStaticArr(self, vulkanType):
        pass

    def onStructExtension(self, vulkanType):
        access = self.exprAccessor(vulkanType)

        castedAccessExpr = "(%s)(%s)" % ("void*", access)
        self.cgen.beginIf(access)
        self.cgen.funcCall(None, self.prefix + "extension_struct",
                           [self.resourceTrackerVarName, castedAccessExpr])
        self.cgen.endIf()

    def onPointer(self, vulkanType):
        pass

    def onValue(self, vulkanType):
        pass


class VulkanTransform(VulkanWrapperGenerator):
    def __init__(self, module, typeInfo, resourceTrackerTypeName="ResourceTracker", resourceTrackerVarName="resourceTracker"):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)

        self.codegen = CodeGen()

        self.transformPrefix = "transform_"

        self.tohostpart = "tohost"
        self.fromhostpart = "fromhost"
        self.variants = [self.tohostpart, self.fromhostpart]

        self.toTransformVar = "toTransform"
        self.resourceTrackerTypeName = resourceTrackerTypeName
        self.resourceTrackerVarName = resourceTrackerVarName
        self.transformParam = \
            makeVulkanTypeSimple(False, self.resourceTrackerTypeName, 1,
                                 self.resourceTrackerVarName)
        self.voidType = makeVulkanTypeSimple(False, "void", 0)

        self.extensionTransformPrototypes = []

        for variant in self.variants:
            self.extensionTransformPrototypes.append( \
                VulkanAPI(self.transformPrefix + variant + "_extension_struct",
                          self.voidType,
                          [self.transformParam, STRUCT_EXTENSION_PARAM_FOR_WRITE]))

        self.knownStructs = {}
        self.needsTransform = set([])

    def onBegin(self,):
        VulkanWrapperGenerator.onBegin(self)
        # Set up a convenience macro fro the transformed structs
        # and forward-declare the resource tracker class
        self.codegen.stmt("class %s" % self.resourceTrackerTypeName)
        self.codegen.line("#define LIST_TRIVIAL_TRANSFORMED_TYPES(f) \\")
        for name in TRIVIAL_TRANSFORMED_TYPES:
            self.codegen.line("f(%s) \\" % name)
        self.codegen.line("")

        self.codegen.line("#define LIST_NON_TRIVIAL_TRANSFORMED_TYPES(f) \\")
        for name in NON_TRIVIAL_TRANSFORMED_TYPES:
            self.codegen.line("f(%s) \\" % name)
        self.codegen.line("")

        self.codegen.line("#define LIST_TRANSFORMED_TYPES(f) \\")
        self.codegen.line("LIST_TRIVIAL_TRANSFORMED_TYPES(f) \\")
        self.codegen.line("LIST_NON_TRIVIAL_TRANSFORMED_TYPES(f) \\")
        self.codegen.line("")

        self.module.appendHeader(self.codegen.swapCode())

        for prototype in self.extensionTransformPrototypes:
            self.module.appendImpl(self.codegen.makeFuncDecl(
                prototype))

    def onGenType(self, typeXml, name, alias):
        VulkanWrapperGenerator.onGenType(self, typeXml, name, alias)

        if name in self.knownStructs:
            return

        category = self.typeInfo.categoryOf(name)

        if category in ["struct", "union"] and alias:
            for variant in self.variants:
                self.module.appendHeader(
                    self.codegen.makeFuncAlias(self.transformPrefix + variant + "_" + name,
                                               self.transformPrefix + variant + "_" + alias))

        if category in ["struct", "union"] and not alias:
            structInfo = self.typeInfo.structs[name]
            self.knownStructs[name] = structInfo

            for variant in self.variants:
                api = VulkanAPI( \
                    self.transformPrefix + variant + "_" + name,
                    self.voidType,
                    [self.transformParam] + \
                    [makeVulkanTypeSimple( \
                        False, name, 1, self.toTransformVar)])

                transformer = TransformCodegen(
                    None,
                    self.toTransformVar,
                    self.resourceTrackerVarName,
                    self.transformPrefix + variant + "_",
                    variant)

                def funcDefGenerator(cgen):
                    transformer.cgen = cgen
                    for p in api.parameters:
                        cgen.stmt("(void)%s" % p.paramName)

                    genTransformsForVulkanType(
                        self.resourceTrackerVarName,
                        structInfo,
                        transformer.exprAccessor,
                        transformer.lenAccessor,
                        cgen,
                        variant=variant)

                    for member in structInfo.members:
                        iterateVulkanType(
                            self.typeInfo, member,
                            transformer)

                self.module.appendHeader(
                    self.codegen.makeFuncDecl(api))
                self.module.appendImpl(
                    self.codegen.makeFuncImpl(api, funcDefGenerator))


    def onGenCmd(self, cmdinfo, name, alias):
        VulkanWrapperGenerator.onGenCmd(self, cmdinfo, name, alias)

    def onEnd(self,):
        VulkanWrapperGenerator.onEnd(self)

        for (variant, prototype) in zip(self.variants, self.extensionTransformPrototypes):
            def forEachExtensionTransform(ext, castedAccess, cgen):
                if ext.isTransformed:
                    directTransform(self.resourceTrackerVarName, ext, lambda _ : castedAccess, lambda _ : "1", cgen, variant);
                cgen.funcCall(None, self.transformPrefix + variant + "_" + ext.name,
                              [self.resourceTrackerVarName, castedAccess])

            self.module.appendImpl(
                self.codegen.makeFuncImpl(
                    prototype,
                    lambda cgen: self.emitForEachStructExtension(
                        cgen,
                        self.voidType,
                        STRUCT_EXTENSION_PARAM_FOR_WRITE,
                        forEachExtensionTransform)))
