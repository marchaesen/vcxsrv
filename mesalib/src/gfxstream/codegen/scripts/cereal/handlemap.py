# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT

from .common.codegen import CodeGen
from .common.vulkantypes import \
        VulkanAPI, makeVulkanTypeSimple, iterateVulkanType, VulkanTypeIterator

from .wrapperdefs import VulkanWrapperGenerator
from .wrapperdefs import STRUCT_EXTENSION_PARAM, STRUCT_EXTENSION_PARAM_FOR_WRITE

class HandleMapCodegen(VulkanTypeIterator):
    def __init__(self, cgen, inputVar, handlemapVarName, prefix, isHandleFunc):
        self.cgen = cgen
        self.inputVar = inputVar
        self.prefix = prefix
        self.handlemapVarName = handlemapVarName

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
        self.isHandleFunc = isHandleFunc

    def needSkip(self, vulkanType):
        return False

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

        if self.needSkip(vulkanType):
            self.cgen.line("// TODO: Unsupported : %s" %
                           self.cgen.makeCTypeDecl(vulkanType))
            return
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
        self.cgen.funcCall(None, self.prefix + vulkanType.typeName,
                           [self.handlemapVarName, accessCasted])

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
        if not self.isHandleFunc(vulkanType):
            return

        accessLhs = self.exprAccessor(vulkanType)
        lenAccess = self.lenAccessor(vulkanType)

        self.cgen.stmt("%s->mapHandles_%s(%s%s, %s)" % \
            (self.handlemapVarName, vulkanType.typeName,
             self.makeCastExpr(vulkanType.getForAddressAccess().getForNonConstAccess()),
             accessLhs, lenAccess))

    def onStructExtension(self, vulkanType):
        access = self.exprAccessor(vulkanType)

        castedAccessExpr = "(%s)(%s)" % ("void*", access)
        self.cgen.beginIf(access)
        self.cgen.funcCall(None, self.prefix + "extension_struct",
                           [self.handlemapVarName, castedAccessExpr])
        self.cgen.endIf()

    def onPointer(self, vulkanType):
        if self.needSkip(vulkanType):
            return

        if not self.isHandleFunc(vulkanType):
            return

        access = self.exprAccessor(vulkanType)
        lenAccess = self.lenAccessor(vulkanType)
        lenAccess = "1" if lenAccess is None else lenAccess

        self.cgen.beginIf(access)

        self.cgen.stmt( \
            "%s->mapHandles_%s(%s%s, %s)" % \
            (self.handlemapVarName,
             vulkanType.typeName,
             self.makeCastExpr(vulkanType.getForNonConstAccess()),
             access,
             lenAccess))

        self.cgen.endIf()

    def onValue(self, vulkanType):
        if not self.isHandleFunc(vulkanType):
            return
        access = self.exprAccessor(vulkanType)
        self.cgen.stmt(
            "%s->mapHandles_%s(%s%s)" % \
            (self.handlemapVarName, vulkanType.typeName,
             self.makeCastExpr(vulkanType.getForAddressAccess().getForNonConstAccess()),
             access))

class VulkanHandleMap(VulkanWrapperGenerator):
    def __init__(self, module, typeInfo):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)

        self.codegen = CodeGen()

        self.handlemapPrefix = "handlemap_"
        self.toMapVar = "toMap"
        self.handlemapVarName = "handlemap"
        self.handlemapParam = \
            makeVulkanTypeSimple(False, "VulkanHandleMapping", 1,
                                 self.handlemapVarName)
        self.voidType = makeVulkanTypeSimple(False, "void", 0)

        self.handlemapCodegen = \
            HandleMapCodegen(
                None,
                self.toMapVar,
                self.handlemapVarName,
                self.handlemapPrefix,
                lambda vtype : typeInfo.isHandleType(vtype.typeName))

        self.knownDefs = {}

        self.extensionHandlemapPrototype = \
            VulkanAPI(self.handlemapPrefix + "extension_struct",
                      self.voidType,
                      [self.handlemapParam, STRUCT_EXTENSION_PARAM_FOR_WRITE])

    def onBegin(self,):
        VulkanWrapperGenerator.onBegin(self)
        self.module.appendImpl(self.codegen.makeFuncDecl(
            self.extensionHandlemapPrototype))

    def onGenType(self, typeXml, name, alias):
        VulkanWrapperGenerator.onGenType(self, typeXml, name, alias)

        if name in self.knownDefs:
            return

        category = self.typeInfo.categoryOf(name)

        if category in ["struct", "union"] and alias:
            self.module.appendHeader(
                self.codegen.makeFuncAlias(self.handlemapPrefix + name,
                                           self.handlemapPrefix + alias))

        if category in ["struct", "union"] and not alias:

            structInfo = self.typeInfo.structs[name]

            typeFromName = \
                lambda varname: \
                    makeVulkanTypeSimple(varname == "from", name, 1, varname)

            handlemapParams = \
                [self.handlemapParam] + \
                list(map(typeFromName, [self.toMapVar]))

            handlemapPrototype = \
                VulkanAPI(self.handlemapPrefix + name,
                          self.voidType,
                          handlemapParams)

            def funcDefGenerator(cgen):
                self.handlemapCodegen.cgen = cgen
                for p in handlemapParams:
                    cgen.stmt("(void)%s" % p.paramName)
                for member in structInfo.members:
                    iterateVulkanType(self.typeInfo, member,
                                      self.handlemapCodegen)

            self.module.appendHeader(
                self.codegen.makeFuncDecl(handlemapPrototype))
            self.module.appendImpl(
                self.codegen.makeFuncImpl(handlemapPrototype, funcDefGenerator))

    def onGenCmd(self, cmdinfo, name, alias):
        VulkanWrapperGenerator.onGenCmd(self, cmdinfo, name, alias)

    def onEnd(self,):
        VulkanWrapperGenerator.onEnd(self)

        def forEachExtensionHandlemap(ext, castedAccess, cgen):
            cgen.funcCall(None, self.handlemapPrefix + ext.name,
                          [self.handlemapVarName, castedAccess])

        self.module.appendImpl(
            self.codegen.makeFuncImpl(
                self.extensionHandlemapPrototype,
                lambda cgen: self.emitForEachStructExtension(
                    cgen,
                    self.voidType,
                    STRUCT_EXTENSION_PARAM_FOR_WRITE,
                    forEachExtensionHandlemap)))
