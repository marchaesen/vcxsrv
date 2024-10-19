# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT

from .common.codegen import CodeGen
from .common.vulkantypes import \
        VulkanAPI, makeVulkanTypeSimple, iterateVulkanType, VulkanTypeIterator

from .wrapperdefs import VulkanWrapperGenerator
from .wrapperdefs import STRUCT_EXTENSION_PARAM, STRUCT_EXTENSION_PARAM_FOR_WRITE, EXTENSION_SIZE_API_NAME

class DeepcopyCodegen(VulkanTypeIterator):
    def __init__(self, cgen, inputVars, poolVarName, rootVarName, prefix, skipValues=False):
        self.cgen = cgen
        self.inputVars = inputVars
        self.prefix = prefix
        self.poolVarName = poolVarName
        self.rootVarName = rootVarName
        self.skipValues = skipValues

        def makeAccess(varName, asPtr = True):
            return lambda t: self.cgen.generalAccess(t, parentVarName = varName, asPtr = asPtr)

        def makeLengthAccess(varName):
            return lambda t: self.cgen.generalLengthAccess(t, parentVarName = varName)

        def makeLengthAccessGuard(varName):
            return lambda t: self.cgen.generalLengthAccessGuard(t, parentVarName=varName)

        self.exprAccessorLhs = makeAccess(self.inputVars[0])
        self.exprAccessorRhs = makeAccess(self.inputVars[1])

        self.exprAccessorValueLhs = makeAccess(self.inputVars[0], asPtr = False)
        self.exprAccessorValueRhs = makeAccess(self.inputVars[1], asPtr = False)

        self.lenAccessorLhs = makeLengthAccess(self.inputVars[0])
        self.lenAccessorRhs = makeLengthAccess(self.inputVars[1])

        self.lenAccessorGuardLhs = makeLengthAccessGuard(self.inputVars[0])
        self.lenAccessorGuardRhs = makeLengthAccessGuard(self.inputVars[1])

        self.checked = False

    def needSkip(self, vulkanType):
        return False

    def makeCastExpr(self, vulkanType):
        return "(%s)" % (
            self.cgen.makeCTypeDecl(vulkanType, useParamName=False))

    def makeNonConstCastForCopy(self, access, vulkanType):
        if vulkanType.staticArrExpr:
            casted = "%s(%s)" % (self.makeCastExpr(vulkanType.getForAddressAccess().getForNonConstAccess()), access)
        elif vulkanType.accessibleAsPointer():
            casted = "%s(%s)" % (self.makeCastExpr(vulkanType.getForNonConstAccess()), access)
        else:
            casted = "%s(%s)" % (self.makeCastExpr(vulkanType.getForAddressAccess().getForNonConstAccess()), access)
        return casted

    def makeAllocBytesExpr(self, lenAccess, vulkanType):
        sizeof = self.cgen.sizeofExpr( \
                     vulkanType.getForValueAccess())
        if lenAccess:
            bytesExpr = "%s * %s" % (lenAccess, sizeof)
        else:
            bytesExpr = sizeof

        return bytesExpr

    def onCheck(self, vulkanType):
        pass

    def endCheck(self, vulkanType):
        pass

    def onCompoundType(self, vulkanType):

        if self.needSkip(vulkanType):
            self.cgen.line("// TODO: Unsupported : %s" %
                           self.cgen.makeCTypeDecl(vulkanType))
            return

        accessLhs = self.exprAccessorLhs(vulkanType)
        accessRhs = self.exprAccessorRhs(vulkanType)

        lenAccessLhs = self.lenAccessorLhs(vulkanType)
        lenAccessRhs = self.lenAccessorRhs(vulkanType)

        lenAccessorGuardLhs = self.lenAccessorGuardLhs(vulkanType)
        lenAccessorGuardRhs = self.lenAccessorGuardRhs(vulkanType)

        isPtr = vulkanType.pointerIndirectionLevels > 0

        if lenAccessorGuardLhs is not None:
            self.cgen.beginIf(lenAccessorGuardLhs)

        if isPtr:
            self.cgen.stmt("%s = nullptr" % accessRhs)
            self.cgen.beginIf(accessLhs)

            self.cgen.stmt( \
                "%s = %s%s->alloc(%s)" % \
                (accessRhs, self.makeCastExpr(vulkanType.getForNonConstAccess()),
                 self.poolVarName, self.makeAllocBytesExpr(lenAccessLhs, vulkanType)))

        if lenAccessLhs is not None:

            loopVar = "i"
            accessLhs = "%s + %s" % (accessLhs, loopVar)
            forInit = "uint32_t %s = 0" % loopVar
            forCond = "%s < (uint32_t)%s" % (loopVar, lenAccessLhs)
            forIncr = "++%s" % loopVar

            if isPtr:
                # Avoid generating a self-assign.
                if lenAccessRhs != lenAccessLhs:
                    self.cgen.stmt("%s = %s" % (lenAccessRhs, lenAccessLhs))

            accessRhs = "%s + %s" % (accessRhs, loopVar)
            self.cgen.beginFor(forInit, forCond, forIncr)


        accessRhsCasted = self.makeNonConstCastForCopy(accessRhs, vulkanType)

        self.cgen.funcCall(None, self.prefix + vulkanType.typeName,
                           [self.poolVarName, self.rootVarName, accessLhs, accessRhsCasted])

        if lenAccessLhs is not None:
            self.cgen.endFor()

        if isPtr:
            self.cgen.endIf()

        if lenAccessorGuardLhs is not None:
            self.cgen.endIf()

    def onString(self, vulkanType):
        accessLhs = self.exprAccessorLhs(vulkanType)
        accessRhs = self.exprAccessorRhs(vulkanType)

        self.cgen.stmt("%s = nullptr" % accessRhs)
        self.cgen.beginIf(accessLhs)

        self.cgen.stmt( \
            "%s = %s->strDup(%s)" % \
            (accessRhs,
             self.poolVarName,
             accessLhs))

        self.cgen.endIf()

    def onStringArray(self, vulkanType):
        accessLhs = self.exprAccessorLhs(vulkanType)
        accessRhs = self.exprAccessorRhs(vulkanType)

        lenAccessLhs = self.lenAccessorLhs(vulkanType)

        self.cgen.stmt("%s = nullptr" % accessRhs)
        self.cgen.beginIf("%s && %s" % (accessLhs, lenAccessLhs))

        self.cgen.stmt( \
            "%s = %s->strDupArray(%s, %s)" % \
            (accessRhs,
             self.poolVarName,
             accessLhs,
             lenAccessLhs))

        self.cgen.endIf()

    def onStaticArr(self, vulkanType):
        accessLhs = self.exprAccessorValueLhs(vulkanType)
        accessRhs = self.exprAccessorValueRhs(vulkanType)

        lenAccessLhs = self.lenAccessorLhs(vulkanType)

        bytesExpr = self.makeAllocBytesExpr(lenAccessLhs, vulkanType)
        self.cgen.stmt("memcpy(%s, %s, %s)" % (accessRhs, accessLhs, bytesExpr))

    def onStructExtension(self, vulkanType):

        lhs = self.exprAccessorLhs(vulkanType)
        rhs = self.exprAccessorRhs(vulkanType)

        rhsExpr = "(%s)(%s)" % ("void*", rhs)

        nextVar = "from_%s" % vulkanType.paramName
        sizeVar = "%s_size" % vulkanType.paramName

        self.cgen.beginIf("%s == VK_STRUCTURE_TYPE_MAX_ENUM" %
                          self.rootVarName)
        self.cgen.stmt("%s = from->sType" % self.rootVarName)
        self.cgen.endIf()

        self.cgen.stmt("const void* %s = %s" % (nextVar, self.inputVars[0]))
        self.cgen.stmt("size_t %s = 0u" % sizeVar)
        self.cgen.beginWhile("!%s && %s" % (sizeVar, nextVar))
        self.cgen.stmt("%s = static_cast<const vk_struct_common*>(%s)->%s" % (
            nextVar, nextVar, vulkanType.paramName
        ))
        self.cgen.stmt("%s = %s(%s, %s)" % (
            sizeVar, EXTENSION_SIZE_API_NAME, self.rootVarName, nextVar))
        self.cgen.endWhile()
        
        self.cgen.stmt("%s = nullptr" % rhs)

        self.cgen.beginIf(sizeVar)

        self.cgen.stmt( \
            "%s = %s%s->alloc(%s)" % \
            (rhs, self.makeCastExpr(vulkanType.getForNonConstAccess()), self.poolVarName, sizeVar))

        self.cgen.funcCall(None, self.prefix + "extension_struct",
                           [self.poolVarName, self.rootVarName, nextVar, rhsExpr])

        self.cgen.endIf()

    def onPointer(self, vulkanType):

        accessLhs = self.exprAccessorLhs(vulkanType)
        accessRhs = self.exprAccessorRhs(vulkanType)

        if self.needSkip(vulkanType):
            self.cgen.stmt("%s = nullptr" % accessRhs)
            return

        lenAccessLhs = self.lenAccessorLhs(vulkanType)

        self.cgen.stmt("%s = nullptr" % accessRhs)
        self.cgen.beginIf(accessLhs)

        bytesExpr = self.makeAllocBytesExpr(lenAccessLhs, vulkanType)

        self.cgen.stmt( \
            "%s = %s%s->dupArray(%s, %s)" % \
            (accessRhs,
             self.makeCastExpr(vulkanType.getForNonConstAccess()),
             self.poolVarName,
             accessLhs,
             bytesExpr))

        self.cgen.endIf()

    def onValue(self, vulkanType):
        if self.skipValues:
            return

        accessLhs = self.exprAccessorValueLhs(vulkanType)
        accessRhs = self.exprAccessorValueRhs(vulkanType)

        self.cgen.stmt("%s = %s" % (accessRhs, accessLhs))

class VulkanDeepcopy(VulkanWrapperGenerator):

    def __init__(self, module, typeInfo):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)

        self.codegen = CodeGen()

        self.deepcopyPrefix = "deepcopy_"
        self.deepcopyVars = ["from", "to"]
        self.deepcopyAllocatorVarName = "alloc"
        self.deepcopyAllocatorParam = \
            makeVulkanTypeSimple(False, "Allocator", 1,
                                 self.deepcopyAllocatorVarName)
        self.deepcopyRootVarName = "rootType"
        self.deepcopyRootParam = \
            makeVulkanTypeSimple(False, "VkStructureType",
                                 0, self.deepcopyRootVarName)
        self.voidType = makeVulkanTypeSimple(False, "void", 0)

        self.deepcopyCodegen = \
            DeepcopyCodegen(
                None,
                self.deepcopyVars,
                self.deepcopyAllocatorVarName,
                self.deepcopyRootVarName,
                self.deepcopyPrefix,
                skipValues=True)

        self.knownDefs = {}

        self.extensionDeepcopyPrototype = \
            VulkanAPI(self.deepcopyPrefix + "extension_struct",
                      self.voidType,
                      [self.deepcopyAllocatorParam,
                       self.deepcopyRootParam,
                       STRUCT_EXTENSION_PARAM,
                       STRUCT_EXTENSION_PARAM_FOR_WRITE])

    def onBegin(self,):
        VulkanWrapperGenerator.onBegin(self)
        self.module.appendImpl(self.codegen.makeFuncDecl(
            self.extensionDeepcopyPrototype))

    def onGenType(self, typeXml, name, alias):
        VulkanWrapperGenerator.onGenType(self, typeXml, name, alias)

        if name in self.knownDefs:
            return

        category = self.typeInfo.categoryOf(name)

        if category in ["struct", "union"] and alias:
            self.module.appendHeader(
                self.codegen.makeFuncAlias(self.deepcopyPrefix + name,
                                           self.deepcopyPrefix + alias))

        if category in ["struct", "union"] and not alias:

            structInfo = self.typeInfo.structs[name]

            typeFromName = \
                lambda varname: \
                    makeVulkanTypeSimple(varname == "from", name, 1, varname)

            deepcopyParams = \
                [self.deepcopyAllocatorParam, self.deepcopyRootParam] + \
                 list(map(typeFromName, self.deepcopyVars))

            deepcopyPrototype = \
                VulkanAPI(self.deepcopyPrefix + name,
                          self.voidType,
                          deepcopyParams)

            def structDeepcopyDef(cgen):
                self.deepcopyCodegen.cgen = cgen
                canSimplyAssign = True
                for member in structInfo.members:
                    if not member.isSimpleValueType(self.typeInfo):
                        canSimplyAssign = False

                cgen.stmt("(void)%s" % self.deepcopyAllocatorVarName)
                cgen.stmt("(void)%s" % self.deepcopyRootVarName)
                cgen.stmt("*to = *from")
                if canSimplyAssign:
                    pass
                else:
                    for member in structInfo.members:
                        iterateVulkanType(self.typeInfo, member,
                                          self.deepcopyCodegen)

            self.module.appendHeader(
                self.codegen.makeFuncDecl(deepcopyPrototype))
            self.module.appendImpl(
                self.codegen.makeFuncImpl(deepcopyPrototype, structDeepcopyDef))

    def onGenCmd(self, cmdinfo, name, alias):
        VulkanWrapperGenerator.onGenCmd(self, cmdinfo, name, alias)

    def onEnd(self,):
        VulkanWrapperGenerator.onEnd(self)

        def deepcopyDstExpr(cgen, typeName):
            return cgen.makeReinterpretCast( \
                       STRUCT_EXTENSION_PARAM_FOR_WRITE.paramName,
                       typeName, const=False)

        def forEachExtensionDeepcopy(ext, castedAccess, cgen):
            cgen.funcCall(None, self.deepcopyPrefix + ext.name,
                          [self.deepcopyAllocatorVarName,
                           self.deepcopyRootVarName,
                           castedAccess, deepcopyDstExpr(cgen, ext.name)])

        self.module.appendImpl(
            self.codegen.makeFuncImpl(
                self.extensionDeepcopyPrototype,
                lambda cgen: self.emitForEachStructExtension(
                    cgen,
                    self.voidType,
                    STRUCT_EXTENSION_PARAM,
                    forEachExtensionDeepcopy,
                    rootTypeVar=self.deepcopyRootParam)))
