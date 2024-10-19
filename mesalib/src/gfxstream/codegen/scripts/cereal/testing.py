# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT

from copy import copy

from .common.codegen import CodeGen
from .common.vulkantypes import \
        VulkanAPI, makeVulkanTypeSimple, iterateVulkanType, VulkanTypeIterator

from .wrapperdefs import VulkanWrapperGenerator
from .wrapperdefs import EQUALITY_VAR_NAMES
from .wrapperdefs import EQUALITY_ON_FAIL_VAR
from .wrapperdefs import EQUALITY_ON_FAIL_VAR_TYPE
from .wrapperdefs import EQUALITY_RET_TYPE
from .wrapperdefs import API_PREFIX_EQUALITY
from .wrapperdefs import STRUCT_EXTENSION_PARAM, STRUCT_EXTENSION_PARAM2

class VulkanEqualityCodegen(VulkanTypeIterator):

    def __init__(self, cgen, inputVars, onFailCompareVar, prefix):
        self.cgen = cgen
        self.inputVars = inputVars
        self.onFailCompareVar = onFailCompareVar
        self.prefix = prefix

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

        self.lenAccessGuardLhs = makeLengthAccessGuard(self.inputVars[0])
        self.lenAccessGuardRhs = makeLengthAccessGuard(self.inputVars[1])

        self.checked = False

    def getTypeForCompare(self, vulkanType):
        res = copy(vulkanType)

        if not vulkanType.accessibleAsPointer():
            res = res.getForAddressAccess()

        if vulkanType.staticArrExpr:
            res = res.getForAddressAccess()

        return res

    def makeCastExpr(self, vulkanType):
        return "(%s)" % (
            self.cgen.makeCTypeDecl(vulkanType, useParamName=False))

    def makeEqualExpr(self, lhs, rhs):
        return "(%s) == (%s)" % (lhs, rhs)

    def makeEqualBufExpr(self, lhs, rhs, size):
        return "(memcmp(%s, %s, %s) == 0)" % (lhs, rhs, size)

    def makeEqualStringExpr(self, lhs, rhs):
        return "(strcmp(%s, %s) == 0)" % (lhs, rhs)

    def makeBothNotNullExpr(self, lhs, rhs):
        return "(%s) && (%s)" % (lhs, rhs)

    def makeBothNullExpr(self, lhs, rhs):
        return "!(%s) && !(%s)" % (lhs, rhs)

    def compareWithConsequence(self, compareExpr, vulkanType, errMsg=""):
        self.cgen.stmt("if (!(%s)) { %s(\"%s (Error: %s)\"); }" %
                       (compareExpr, self.onFailCompareVar,
                        self.exprAccessorValueLhs(vulkanType), errMsg))

    def onCheck(self, vulkanType):

        self.checked = True

        accessLhs = self.exprAccessorLhs(vulkanType)
        accessRhs = self.exprAccessorRhs(vulkanType)

        bothNull = self.makeBothNullExpr(accessLhs, accessRhs)
        bothNotNull = self.makeBothNotNullExpr(accessLhs, accessRhs)
        nullMatchExpr = "(%s) || (%s)" % (bothNull, bothNotNull)

        self.compareWithConsequence( \
            nullMatchExpr,
            vulkanType,
            "Mismatch in optional field")

        skipStreamInternal = vulkanType.typeName == "void"

        if skipStreamInternal:
            return

        self.cgen.beginIf("%s && %s" % (accessLhs, accessRhs))

    def endCheck(self, vulkanType):

        skipStreamInternal = vulkanType.typeName == "void"
        if skipStreamInternal:
            return

        if self.checked:
            self.cgen.endIf()
            self.checked = False

    def onCompoundType(self, vulkanType):
        accessLhs = self.exprAccessorLhs(vulkanType)
        accessRhs = self.exprAccessorRhs(vulkanType)

        lenAccessLhs = self.lenAccessorLhs(vulkanType)
        lenAccessRhs = self.lenAccessorRhs(vulkanType)

        lenAccessGuardLhs = self.lenAccessGuardLhs(vulkanType)
        lenAccessGuardRhs = self.lenAccessGuardRhs(vulkanType)

        needNullCheck = vulkanType.pointerIndirectionLevels > 0

        if needNullCheck:
            bothNotNullExpr = self.makeBothNotNullExpr(accessLhs, accessRhs)
            self.cgen.beginIf(bothNotNullExpr)

        if lenAccessLhs is not None:
            equalLenExpr = self.makeEqualExpr(lenAccessLhs, lenAccessRhs)

            self.compareWithConsequence( \
                equalLenExpr,
                vulkanType, "Lengths not equal")

            loopVar = "i"
            accessLhs = "%s + %s" % (accessLhs, loopVar)
            accessRhs = "%s + %s" % (accessRhs, loopVar)
            forInit = "uint32_t %s = 0" % loopVar
            forCond = "%s < (uint32_t)%s" % (loopVar, lenAccessLhs)
            forIncr = "++%s" % loopVar

            if needNullCheck:
                self.cgen.beginIf(equalLenExpr)

            if lenAccessGuardLhs is not None:
                self.cgen.beginIf(lenAccessGuardLhs)

            self.cgen.beginFor(forInit, forCond, forIncr)

        self.cgen.funcCall(None, self.prefix + vulkanType.typeName,
                           [accessLhs, accessRhs, self.onFailCompareVar])

        if lenAccessLhs is not None:
            self.cgen.endFor()
            if lenAccessGuardLhs is not None:
                self.cgen.endIf()
            if needNullCheck:
                self.cgen.endIf()

        if needNullCheck:
            self.cgen.endIf()

    def onString(self, vulkanType):
        accessLhs = self.exprAccessorLhs(vulkanType)
        accessRhs = self.exprAccessorRhs(vulkanType)

        bothNullExpr = self.makeBothNullExpr(accessLhs, accessRhs)
        bothNotNullExpr = self.makeBothNotNullExpr(accessLhs, accessRhs)
        nullMatchExpr = "(%s) || (%s)" % (bothNullExpr, bothNotNullExpr)

        self.compareWithConsequence( \
            nullMatchExpr,
            vulkanType,
            "Mismatch in string pointer nullness")

        self.cgen.beginIf(bothNotNullExpr)

        self.compareWithConsequence(
            self.makeEqualStringExpr(accessLhs, accessRhs),
            vulkanType, "Unequal strings")

        self.cgen.endIf()

    def onStringArray(self, vulkanType):
        accessLhs = self.exprAccessorLhs(vulkanType)
        accessRhs = self.exprAccessorRhs(vulkanType)

        lenAccessLhs = self.lenAccessorLhs(vulkanType)
        lenAccessRhs = self.lenAccessorRhs(vulkanType)

        lenAccessGuardLhs = self.lenAccessGuardLhs(vulkanType)
        lenAccessGuardRhs = self.lenAccessGuardRhs(vulkanType)

        bothNullExpr = self.makeBothNullExpr(accessLhs, accessRhs)
        bothNotNullExpr = self.makeBothNotNullExpr(accessLhs, accessRhs)
        nullMatchExpr = "(%s) || (%s)" % (bothNullExpr, bothNotNullExpr)

        self.compareWithConsequence( \
            nullMatchExpr,
            vulkanType,
            "Mismatch in string array pointer nullness")

        equalLenExpr = self.makeEqualExpr(lenAccessLhs, lenAccessRhs)

        self.compareWithConsequence( \
            equalLenExpr,
            vulkanType, "Lengths not equal in string array")

        self.compareWithConsequence( \
            equalLenExpr,
            vulkanType, "Lengths not equal in string array")

        self.cgen.beginIf("%s && %s" % (equalLenExpr, bothNotNullExpr))

        loopVar = "i"
        accessLhs = "*(%s + %s)" % (accessLhs, loopVar)
        accessRhs = "*(%s + %s)" % (accessRhs, loopVar)
        forInit = "uint32_t %s = 0" % loopVar
        forCond = "%s < (uint32_t)%s" % (loopVar, lenAccessLhs)
        forIncr = "++%s" % loopVar

        if lenAccessGuardLhs is not None:
            self.cgen.beginIf(lenAccessGuardLhs)

        self.cgen.beginFor(forInit, forCond, forIncr)

        self.compareWithConsequence(
            self.makeEqualStringExpr(accessLhs, accessRhs),
            vulkanType, "Unequal string in string array")

        self.cgen.endFor()

        if lenAccessGuardLhs is not None:
            self.cgen.endIf()

        self.cgen.endIf()

    def onStaticArr(self, vulkanType):
        accessLhs = self.exprAccessorLhs(vulkanType)
        accessRhs = self.exprAccessorRhs(vulkanType)

        lenAccessLhs = self.lenAccessorLhs(vulkanType)

        finalLenExpr = "%s * %s" % (lenAccessLhs,
                                    self.cgen.sizeofExpr(vulkanType))

        self.compareWithConsequence(
            self.makeEqualBufExpr(accessLhs, accessRhs, finalLenExpr),
            vulkanType, "Unequal static array")

    def onStructExtension(self, vulkanType):
        lhs = self.exprAccessorLhs(vulkanType)
        rhs = self.exprAccessorRhs(vulkanType)

        self.cgen.beginIf(lhs)
        self.cgen.funcCall(None, self.prefix + "extension_struct",
                           [lhs, rhs, self.onFailCompareVar])
        self.cgen.endIf()

    def onPointer(self, vulkanType):
        accessLhs = self.exprAccessorLhs(vulkanType)
        accessRhs = self.exprAccessorRhs(vulkanType)

        skipStreamInternal = vulkanType.typeName == "void"
        if skipStreamInternal:
            return

        lenAccessLhs = self.lenAccessorLhs(vulkanType)
        lenAccessRhs = self.lenAccessorRhs(vulkanType)

        if lenAccessLhs is not None:
            self.compareWithConsequence( \
                self.makeEqualExpr(lenAccessLhs, lenAccessRhs),
                vulkanType, "Lengths not equal")

            finalLenExpr = "%s * %s" % (lenAccessLhs,
                                        self.cgen.sizeofExpr(
                                            vulkanType.getForValueAccess()))
        else:
            finalLenExpr = self.cgen.sizeofExpr(vulkanType.getForValueAccess())

        self.compareWithConsequence(
            self.makeEqualBufExpr(accessLhs, accessRhs, finalLenExpr),
            vulkanType, "Unequal dyn array")

    def onValue(self, vulkanType):
        accessLhs = self.exprAccessorValueLhs(vulkanType)
        accessRhs = self.exprAccessorValueRhs(vulkanType)
        self.compareWithConsequence(
            self.makeEqualExpr(accessLhs, accessRhs), vulkanType,
            "Value not equal")


class VulkanTesting(VulkanWrapperGenerator):

    def __init__(self, module, typeInfo):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)

        self.codegen = CodeGen()

        self.equalityCodegen = \
            VulkanEqualityCodegen(
                None,
                EQUALITY_VAR_NAMES,
                EQUALITY_ON_FAIL_VAR,
                API_PREFIX_EQUALITY)

        self.knownDefs = {}

        self.extensionTestingPrototype = \
            VulkanAPI(API_PREFIX_EQUALITY + "extension_struct",
                      EQUALITY_RET_TYPE,
                      [STRUCT_EXTENSION_PARAM,
                       STRUCT_EXTENSION_PARAM2,
                       EQUALITY_ON_FAIL_VAR_TYPE])

    def onBegin(self,):
        VulkanWrapperGenerator.onBegin(self)
        self.module.appendImpl(self.codegen.makeFuncDecl(
            self.extensionTestingPrototype))

    def onGenType(self, typeXml, name, alias):
        VulkanWrapperGenerator.onGenType(self, typeXml, name, alias)

        if name in self.knownDefs:
            return

        category = self.typeInfo.categoryOf(name)

        if category in ["struct", "union"] and alias:
            self.module.appendHeader(
                self.codegen.makeFuncAlias(API_PREFIX_EQUALITY + name,
                                           API_PREFIX_EQUALITY + alias))

        if category in ["struct", "union"] and not alias:

            structInfo = self.typeInfo.structs[name]

            typeFromName = \
                lambda varname: makeVulkanTypeSimple(True, name, 1, varname)

            compareParams = \
                list(map(typeFromName, EQUALITY_VAR_NAMES)) + \
                [EQUALITY_ON_FAIL_VAR_TYPE]

            comparePrototype = \
                VulkanAPI(API_PREFIX_EQUALITY + name,
                          EQUALITY_RET_TYPE,
                          compareParams)

            def structCompareDef(cgen):
                self.equalityCodegen.cgen = cgen
                for member in structInfo.members:
                    iterateVulkanType(self.typeInfo, member,
                                      self.equalityCodegen)

            self.module.appendHeader(
                self.codegen.makeFuncDecl(comparePrototype))
            self.module.appendImpl(
                self.codegen.makeFuncImpl(comparePrototype, structCompareDef))

    def onGenCmd(self, cmdinfo, name, alias):
        VulkanWrapperGenerator.onGenCmd(self, cmdinfo, name, alias)

    def onEnd(self,):
        VulkanWrapperGenerator.onEnd(self)

        def forEachExtensionCompare(ext, castedAccess, cgen):
            cgen.funcCall(None, API_PREFIX_EQUALITY + ext.name,
                          [castedAccess,
                           cgen.makeReinterpretCast(
                               STRUCT_EXTENSION_PARAM2.paramName, ext.name),
                           EQUALITY_ON_FAIL_VAR])

        self.module.appendImpl(
            self.codegen.makeFuncImpl(
                self.extensionTestingPrototype,
                lambda cgen: self.emitForEachStructExtension(
                    cgen,
                    EQUALITY_RET_TYPE,
                    STRUCT_EXTENSION_PARAM,
                    forEachExtensionCompare)))
