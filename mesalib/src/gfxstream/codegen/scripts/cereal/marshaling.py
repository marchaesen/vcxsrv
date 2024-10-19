# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT

from copy import copy
import hashlib, sys

from .common.codegen import CodeGen, VulkanAPIWrapper
from .common.vulkantypes import \
        VulkanAPI, makeVulkanTypeSimple, iterateVulkanType, VulkanTypeIterator, Atom, FuncExpr, FuncExprVal, FuncLambda

from .wrapperdefs import VulkanWrapperGenerator
from .wrapperdefs import VULKAN_STREAM_VAR_NAME
from .wrapperdefs import ROOT_TYPE_VAR_NAME, ROOT_TYPE_PARAM
from .wrapperdefs import STREAM_RET_TYPE
from .wrapperdefs import MARSHAL_INPUT_VAR_NAME
from .wrapperdefs import UNMARSHAL_INPUT_VAR_NAME
from .wrapperdefs import PARAMETERS_MARSHALING
from .wrapperdefs import PARAMETERS_MARSHALING_GUEST
from .wrapperdefs import STYPE_OVERRIDE
from .wrapperdefs import STRUCT_EXTENSION_PARAM, STRUCT_EXTENSION_PARAM_FOR_WRITE, EXTENSION_SIZE_WITH_STREAM_FEATURES_API_NAME
from .wrapperdefs import API_PREFIX_MARSHAL
from .wrapperdefs import API_PREFIX_UNMARSHAL

from .marshalingdefs import KNOWN_FUNCTION_OPCODES, CUSTOM_MARSHAL_TYPES

class VulkanMarshalingCodegen(VulkanTypeIterator):

    def __init__(self,
                 cgen,
                 streamVarName,
                 rootTypeVarName,
                 inputVarName,
                 marshalPrefix,
                 direction = "write",
                 forApiOutput = False,
                 dynAlloc = False,
                 mapHandles = True,
                 handleMapOverwrites = False,
                 doFiltering = True):
        self.cgen = cgen
        self.direction = direction
        self.processSimple = "write" if self.direction == "write" else "read"
        self.forApiOutput = forApiOutput

        self.checked = False

        self.streamVarName = streamVarName
        self.rootTypeVarName = rootTypeVarName
        self.inputVarName = inputVarName
        self.marshalPrefix = marshalPrefix

        self.exprAccessor = lambda t: self.cgen.generalAccess(t, parentVarName = self.inputVarName, asPtr = True)
        self.exprValueAccessor = lambda t: self.cgen.generalAccess(t, parentVarName = self.inputVarName, asPtr = False)
        self.exprPrimitiveValueAccessor = lambda t: self.cgen.generalAccess(t, parentVarName = self.inputVarName, asPtr = False)
        self.lenAccessor = lambda t: self.cgen.generalLengthAccess(t, parentVarName = self.inputVarName)
        self.lenAccessorGuard = lambda t: self.cgen.generalLengthAccessGuard(
            t, parentVarName=self.inputVarName)
        self.filterVarAccessor = lambda t: self.cgen.filterVarAccess(t, parentVarName = self.inputVarName)

        self.dynAlloc = dynAlloc
        self.mapHandles = mapHandles
        self.handleMapOverwrites = handleMapOverwrites
        self.doFiltering = doFiltering

    def getTypeForStreaming(self, vulkanType):
        res = copy(vulkanType)

        if not vulkanType.accessibleAsPointer():
            res = res.getForAddressAccess()

        if vulkanType.staticArrExpr:
            res = res.getForAddressAccess()

        if self.direction == "write":
            return res
        else:
            return res.getForNonConstAccess()

    def makeCastExpr(self, vulkanType):
        return "(%s)" % (
            self.cgen.makeCTypeDecl(vulkanType, useParamName=False))

    def genStreamCall(self, vulkanType, toStreamExpr, sizeExpr):
        varname = self.streamVarName
        func = self.processSimple
        cast = self.makeCastExpr(self.getTypeForStreaming(vulkanType))

        self.cgen.stmt(
            "%s->%s(%s%s, %s)" % (varname, func, cast, toStreamExpr, sizeExpr))

    def genPrimitiveStreamCall(self, vulkanType, access):
        varname = self.streamVarName

        self.cgen.streamPrimitive(
            self.typeInfo,
            varname,
            access,
            vulkanType,
            direction=self.direction)

    def genHandleMappingCall(self, vulkanType, access, lenAccess):

        if lenAccess is None:
            lenAccess = "1"
            handle64Bytes = "8"
        else:
            handle64Bytes = "%s * 8" % lenAccess

        handle64Var = self.cgen.var()
        if lenAccess != "1":
            self.cgen.beginIf(lenAccess)
            self.cgen.stmt("uint64_t* %s" % handle64Var)
            self.cgen.stmt(
                "%s->alloc((void**)&%s, %s * 8)" % \
                (self.streamVarName, handle64Var, lenAccess))
            handle64VarAccess = handle64Var
            handle64VarType = \
                makeVulkanTypeSimple(False, "uint64_t", 1, paramName=handle64Var)
        else:
            self.cgen.stmt("uint64_t %s" % handle64Var)
            handle64VarAccess = "&%s" % handle64Var
            handle64VarType = \
                makeVulkanTypeSimple(False, "uint64_t", 0, paramName=handle64Var)

        if self.direction == "write":
            if self.handleMapOverwrites:
                self.cgen.stmt(
                    "static_assert(8 == sizeof(%s), \"handle map overwrite requires %s to be 8 bytes long\")" % \
                            (vulkanType.typeName, vulkanType.typeName))
                self.cgen.stmt(
                    "%s->handleMapping()->mapHandles_%s((%s*)%s, %s)" %
                    (self.streamVarName, vulkanType.typeName, vulkanType.typeName,
                    access, lenAccess))
                self.genStreamCall(vulkanType, access, "8 * %s" % lenAccess)
            else:
                self.cgen.stmt(
                    "%s->handleMapping()->mapHandles_%s_u64(%s, %s, %s)" %
                    (self.streamVarName, vulkanType.typeName,
                    access,
                    handle64VarAccess, lenAccess))
                self.genStreamCall(handle64VarType, handle64VarAccess, handle64Bytes)
        else:
            self.genStreamCall(handle64VarType, handle64VarAccess, handle64Bytes)
            self.cgen.stmt(
                "%s->handleMapping()->mapHandles_u64_%s(%s, %s%s, %s)" %
                (self.streamVarName, vulkanType.typeName,
                handle64VarAccess,
                self.makeCastExpr(vulkanType.getForNonConstAccess()), access,
                lenAccess))

        if lenAccess != "1":
            self.cgen.endIf()

    def doAllocSpace(self, vulkanType):
        if self.dynAlloc and self.direction == "read":
            access = self.exprAccessor(vulkanType)
            lenAccess = self.lenAccessor(vulkanType)
            sizeof = self.cgen.sizeofExpr( \
                         vulkanType.getForValueAccess())
            if lenAccess:
                bytesExpr = "%s * %s" % (lenAccess, sizeof)
            else:
                bytesExpr = sizeof

            self.cgen.stmt( \
                "%s->alloc((void**)&%s, %s)" %
                    (self.streamVarName,
                     access, bytesExpr))

    def getOptionalStringFeatureExpr(self, vulkanType):
        streamFeature = vulkanType.getProtectStreamFeature()
        if streamFeature is None:
            return None
        return "%s->getFeatureBits() & %s" % (self.streamVarName, streamFeature)

    def onCheck(self, vulkanType):

        if self.forApiOutput:
            return

        featureExpr = self.getOptionalStringFeatureExpr(vulkanType);

        self.checked = True

        access = self.exprAccessor(vulkanType)

        needConsistencyCheck = False

        self.cgen.line("// WARNING PTR CHECK")
        if (self.dynAlloc and self.direction == "read") or self.direction == "write":
            checkAccess = self.exprAccessor(vulkanType)
            addrExpr = "&" + checkAccess
            sizeExpr = self.cgen.sizeofExpr(vulkanType)
        else:
            checkName = "check_%s" % vulkanType.paramName
            self.cgen.stmt("%s %s" % (
                self.cgen.makeCTypeDecl(vulkanType, useParamName = False), checkName))
            checkAccess = checkName
            addrExpr = "&" + checkAccess
            sizeExpr = self.cgen.sizeofExpr(vulkanType)
            needConsistencyCheck = True

        if featureExpr is not None:
            self.cgen.beginIf(featureExpr)

        self.genPrimitiveStreamCall(
            vulkanType,
            checkAccess)

        if featureExpr is not None:
            self.cgen.endIf()

        if featureExpr is not None:
            self.cgen.beginIf("(!(%s) || %s)" % (featureExpr, access))
        else:
            self.cgen.beginIf(access)

        if needConsistencyCheck and featureExpr is None:
            self.cgen.beginIf("!(%s)" % checkName)
            self.cgen.stmt(
                "fprintf(stderr, \"fatal: %s inconsistent between guest and host\\n\")" % (access))
            self.cgen.endIf()


    def onCheckWithNullOptionalStringFeature(self, vulkanType):
        self.cgen.beginIf("%s->getFeatureBits() & VULKAN_STREAM_FEATURE_NULL_OPTIONAL_STRINGS_BIT" % self.streamVarName)
        self.onCheck(vulkanType)

    def endCheckWithNullOptionalStringFeature(self, vulkanType):
        self.endCheck(vulkanType)
        self.cgen.endIf()
        self.cgen.beginElse()

    def finalCheckWithNullOptionalStringFeature(self, vulkanType):
        self.cgen.endElse()

    def endCheck(self, vulkanType):

        if self.checked:
            self.cgen.endIf()
            self.checked = False

    def genFilterFunc(self, filterfunc, env):

        def loop(expr, lambdaEnv={}):
            def do_func(expr):
                fnamestr = expr.name.name
                if "not" == fnamestr:
                    return "!(%s)" % (loop(expr.args[0], lambdaEnv))
                if "eq" == fnamestr:
                    return "(%s == %s)" % (loop(expr.args[0], lambdaEnv), loop(expr.args[1], lambdaEnv))
                if "and" == fnamestr:
                    return "(%s && %s)" % (loop(expr.args[0], lambdaEnv), loop(expr.args[1], lambdaEnv))
                if "or" == fnamestr:
                    return "(%s || %s)" % (loop(expr.args[0], lambdaEnv), loop(expr.args[1], lambdaEnv))
                if "bitwise_and" == fnamestr:
                    return "(%s & %s)" % (loop(expr.args[0], lambdaEnv), loop(expr.args[1], lambdaEnv))
                if "getfield" == fnamestr:
                    ptrlevels = get_ptrlevels(expr.args[0].val.name)
                    if ptrlevels == 0:
                        return "%s.%s" % (loop(expr.args[0], lambdaEnv), expr.args[1].val)
                    else:
                        return "(%s(%s)).%s" % ("*" * ptrlevels, loop(expr.args[0], lambdaEnv), expr.args[1].val)

                if "if" == fnamestr:
                    return "((%s) ? (%s) : (%s))" % (loop(expr.args[0], lambdaEnv), loop(expr.args[1], lambdaEnv), loop(expr.args[2], lambdaEnv))

                return "%s(%s)" % (fnamestr, ", ".join(map(lambda e: loop(e, lambdaEnv), expr.args)))

            def do_expratom(atomname, lambdaEnv= {}):
                if lambdaEnv.get(atomname, None) is not None:
                    return atomname

                enventry = env.get(atomname, None)
                if None != enventry:
                    return self.getEnvAccessExpr(atomname)
                return atomname

            def get_ptrlevels(atomname, lambdaEnv= {}):
                if lambdaEnv.get(atomname, None) is not None:
                    return 0

                enventry = env.get(atomname, None)
                if None != enventry:
                    return self.getPointerIndirectionLevels(atomname)

                return 0

            def do_exprval(expr, lambdaEnv= {}):
                expratom = expr.val

                if Atom == type(expratom):
                    return do_expratom(expratom.name, lambdaEnv)

                return "%s" % expratom

            def do_lambda(expr, lambdaEnv= {}):
                params = expr.vs
                body = expr.body
                newEnv = {}

                for (k, v) in lambdaEnv.items():
                    newEnv[k] = v

                for p in params:
                    newEnv[p.name] = p.typ

                return "[](%s) { return %s; }" % (", ".join(list(map(lambda p: "%s %s" % (p.typ, p.name), params))), loop(body, lambdaEnv=newEnv))

            if FuncExpr == type(expr):
                return do_func(expr)
            if FuncLambda == type(expr):
                return do_lambda(expr)
            elif FuncExprVal == type(expr):
                return do_exprval(expr)

        return loop(filterfunc)

    def beginFilterGuard(self, vulkanType):
        if vulkanType.filterVar == None:
            return

        if self.doFiltering == False:
            return

        filterVarAccess = self.getEnvAccessExpr(vulkanType.filterVar)

        filterValsExpr = None
        filterFuncExpr = None
        filterExpr = None

        filterFeature = "%s->getFeatureBits() & VULKAN_STREAM_FEATURE_IGNORED_HANDLES_BIT" % self.streamVarName

        if None != vulkanType.filterVals:
            filterValsExpr = " || ".join(map(lambda filterval: "(%s == %s)" % (filterval, filterVarAccess), vulkanType.filterVals))

        if None != vulkanType.filterFunc:
            filterFuncExpr = self.genFilterFunc(vulkanType.filterFunc, self.currentStructInfo.environment)

        if None != filterValsExpr and None != filterFuncExpr:
            filterExpr = "%s || %s" % (filterValsExpr, filterFuncExpr)
        elif None == filterValsExpr and None == filterFuncExpr:
            # Assume is bool
            self.cgen.beginIf(filterVarAccess)
        elif None != filterValsExpr:
            self.cgen.beginIf("(!(%s) || (%s))" % (filterFeature, filterValsExpr))
        elif None != filterFuncExpr:
            self.cgen.beginIf("(!(%s) || (%s))" % (filterFeature, filterFuncExpr))

    def endFilterGuard(self, vulkanType, cleanupExpr=None):
        if vulkanType.filterVar == None:
            return

        if self.doFiltering == False:
            return

        if cleanupExpr == None:
            self.cgen.endIf()
        else:
            self.cgen.endIf()
            self.cgen.beginElse()
            self.cgen.stmt(cleanupExpr)
            self.cgen.endElse()

    def getEnvAccessExpr(self, varName):
        parentEnvEntry = self.currentStructInfo.environment.get(varName, None)

        if parentEnvEntry != None:
            isParentMember = parentEnvEntry["structmember"]

            if isParentMember:
                envAccess = self.exprValueAccessor(list(filter(lambda member: member.paramName == varName, self.currentStructInfo.members))[0])
            else:
                envAccess = varName
            return envAccess

        return None

    def getPointerIndirectionLevels(self, varName):
        parentEnvEntry = self.currentStructInfo.environment.get(varName, None)

        if parentEnvEntry != None:
            isParentMember = parentEnvEntry["structmember"]

            if isParentMember:
                return list(filter(lambda member: member.paramName == varName, self.currentStructInfo.members))[0].pointerIndirectionLevels
            else:
                return 0
            return 0

        return 0


    def onCompoundType(self, vulkanType):

        access = self.exprAccessor(vulkanType)
        lenAccess = self.lenAccessor(vulkanType)
        lenAccessGuard = self.lenAccessorGuard(vulkanType)

        self.beginFilterGuard(vulkanType)

        if vulkanType.pointerIndirectionLevels > 0:
            self.doAllocSpace(vulkanType)

        if lenAccess is not None:
            if lenAccessGuard is not None:
                self.cgen.beginIf(lenAccessGuard)
            loopVar = "i"
            access = "%s + %s" % (access, loopVar)
            forInit = "uint32_t %s = 0" % loopVar
            forCond = "%s < (uint32_t)%s" % (loopVar, lenAccess)
            forIncr = "++%s" % loopVar
            self.cgen.beginFor(forInit, forCond, forIncr)

        accessWithCast = "%s(%s)" % (self.makeCastExpr(
            self.getTypeForStreaming(vulkanType)), access)

        callParams = [self.streamVarName, self.rootTypeVarName, accessWithCast]

        for (bindName, localName) in vulkanType.binds.items():
            callParams.append(self.getEnvAccessExpr(localName))

        self.cgen.funcCall(None, self.marshalPrefix + vulkanType.typeName,
                           callParams)

        if lenAccess is not None:
            self.cgen.endFor()
            if lenAccessGuard is not None:
                self.cgen.endIf()

        if self.direction == "read":
            self.endFilterGuard(vulkanType, "%s = 0" % self.exprAccessor(vulkanType))
        else:
            self.endFilterGuard(vulkanType)

    def onString(self, vulkanType):

        access = self.exprAccessor(vulkanType)

        if self.direction == "write":
            self.cgen.stmt("%s->putString(%s)" % (self.streamVarName, access))
        else:
            castExpr = \
                self.makeCastExpr( \
                    self.getTypeForStreaming( \
                        vulkanType.getForAddressAccess()))

            self.cgen.stmt( \
                "%s->loadStringInPlace(%s&%s)" % (self.streamVarName, castExpr, access))

    def onStringArray(self, vulkanType):

        access = self.exprAccessor(vulkanType)
        lenAccess = self.lenAccessor(vulkanType)

        if self.direction == "write":
            self.cgen.stmt("saveStringArray(%s, %s, %s)" % (self.streamVarName,
                                                            access, lenAccess))
        else:
            castExpr = \
                self.makeCastExpr( \
                    self.getTypeForStreaming( \
                        vulkanType.getForAddressAccess()))

            self.cgen.stmt("%s->loadStringArrayInPlace(%s&%s)" % (self.streamVarName, castExpr, access))

    def onStaticArr(self, vulkanType):
        access = self.exprValueAccessor(vulkanType)
        lenAccess = self.lenAccessor(vulkanType)
        finalLenExpr = "%s * %s" % (lenAccess, self.cgen.sizeofExpr(vulkanType))
        self.genStreamCall(vulkanType, access, finalLenExpr)

    # Old version VkEncoder may have some sType values conflict with VkDecoder
    # of new versions. For host decoder, it should not carry the incorrect old
    # sType values to the |forUnmarshaling| struct. Instead it should overwrite
    # the sType value.
    def overwriteSType(self, vulkanType):
        if self.direction == "read":
            sTypeParam = copy(vulkanType)
            sTypeParam.paramName = "sType"
            sTypeAccess = self.exprAccessor(sTypeParam)

            typeName = vulkanType.parent.typeName
            if typeName in STYPE_OVERRIDE:
                self.cgen.stmt("%s = %s" %
                               (sTypeAccess, STYPE_OVERRIDE[typeName]))

    def onStructExtension(self, vulkanType):
        self.overwriteSType(vulkanType)

        sTypeParam = copy(vulkanType)
        sTypeParam.paramName = "sType"

        access = self.exprAccessor(vulkanType)
        sizeVar = "%s_size" % vulkanType.paramName

        if self.direction == "read":
            castedAccessExpr = "(%s)(%s)" % ("void*", access)
        else:
            castedAccessExpr = access

        sTypeAccess = self.exprAccessor(sTypeParam)
        self.cgen.beginIf("%s == VK_STRUCTURE_TYPE_MAX_ENUM" %
                          self.rootTypeVarName)
        self.cgen.stmt("%s = %s" % (self.rootTypeVarName, sTypeAccess))
        self.cgen.endIf()

        if self.direction == "read" and self.dynAlloc:
            self.cgen.stmt("size_t %s" % sizeVar)
            self.cgen.stmt("%s = %s->getBe32()" % \
                (sizeVar, self.streamVarName))
            self.cgen.stmt("%s = nullptr" % access)
            self.cgen.beginIf(sizeVar)
            self.cgen.stmt( \
                    "%s->alloc((void**)&%s, sizeof(VkStructureType))" %
                    (self.streamVarName, access))

            self.genStreamCall(vulkanType, access, "sizeof(VkStructureType)")
            self.cgen.stmt("VkStructureType extType = *(VkStructureType*)(%s)" % access)
            self.cgen.stmt( \
                "%s->alloc((void**)&%s, %s(%s->getFeatureBits(), %s, %s))" %
                (self.streamVarName, access, EXTENSION_SIZE_WITH_STREAM_FEATURES_API_NAME, self.streamVarName, self.rootTypeVarName, access))
            self.cgen.stmt("*(VkStructureType*)%s = extType" % access)

            self.cgen.funcCall(None, self.marshalPrefix + "extension_struct",
                               [self.streamVarName, self.rootTypeVarName, castedAccessExpr])
            self.cgen.endIf()
        else:

            self.cgen.funcCall(None, self.marshalPrefix + "extension_struct",
                               [self.streamVarName, self.rootTypeVarName, castedAccessExpr])


    def onPointer(self, vulkanType):
        access = self.exprAccessor(vulkanType)

        lenAccess = self.lenAccessor(vulkanType)
        lenAccessGuard = self.lenAccessorGuard(vulkanType)

        self.beginFilterGuard(vulkanType)
        self.doAllocSpace(vulkanType)

        if vulkanType.isHandleType() and self.mapHandles:
            self.genHandleMappingCall(vulkanType, access, lenAccess)
        else:
            if self.typeInfo.isNonAbiPortableType(vulkanType.typeName):
                if lenAccess is not None:
                    if lenAccessGuard is not None:
                        self.cgen.beginIf(lenAccessGuard)
                    self.cgen.beginFor("uint32_t i = 0", "i < (uint32_t)%s" % lenAccess, "++i")
                    self.genPrimitiveStreamCall(vulkanType.getForValueAccess(), "%s[i]" % access)
                    self.cgen.endFor()
                    if lenAccessGuard is not None:
                        self.cgen.endIf()
                else:
                    self.genPrimitiveStreamCall(vulkanType.getForValueAccess(), "(*%s)" % access)
            else:
                if lenAccess is not None:
                    finalLenExpr = "%s * %s" % (
                        lenAccess, self.cgen.sizeofExpr(vulkanType.getForValueAccess()))
                else:
                    finalLenExpr = "%s" % (
                        self.cgen.sizeofExpr(vulkanType.getForValueAccess()))
                self.genStreamCall(vulkanType, access, finalLenExpr)

        if self.direction == "read":
            self.endFilterGuard(vulkanType, "%s = 0" % access)
        else:
            self.endFilterGuard(vulkanType)

    def onValue(self, vulkanType):
        self.beginFilterGuard(vulkanType)

        if vulkanType.isHandleType() and self.mapHandles:
            access = self.exprAccessor(vulkanType)
            self.genHandleMappingCall(
                vulkanType.getForAddressAccess(), access, "1")
        elif self.typeInfo.isNonAbiPortableType(vulkanType.typeName):
            access = self.exprPrimitiveValueAccessor(vulkanType)
            self.genPrimitiveStreamCall(vulkanType, access)
        else:
            access = self.exprAccessor(vulkanType)
            self.genStreamCall(vulkanType, access, self.cgen.sizeofExpr(vulkanType))

        self.endFilterGuard(vulkanType)

    def streamLetParameter(self, structInfo, letParamInfo):
        filterFeature = "%s->getFeatureBits() & VULKAN_STREAM_FEATURE_IGNORED_HANDLES_BIT" % self.streamVarName
        self.cgen.stmt("%s %s = 1" % (letParamInfo.typeName, letParamInfo.paramName))

        self.cgen.beginIf(filterFeature)

        if self.direction == "write":
            bodyExpr = self.currentStructInfo.environment[letParamInfo.paramName]["body"]
            self.cgen.stmt("%s = %s" % (letParamInfo.paramName, self.genFilterFunc(bodyExpr, self.currentStructInfo.environment)))

        self.genPrimitiveStreamCall(letParamInfo, letParamInfo.paramName)

        self.cgen.endIf()


class VulkanMarshaling(VulkanWrapperGenerator):

    def __init__(self, module, typeInfo, variant="host"):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)

        self.cgenHeader = CodeGen()
        self.cgenImpl = CodeGen()

        self.variant = variant

        self.currentFeature = None
        self.apiOpcodes = {}
        self.dynAlloc = self.variant != "guest"

        if self.variant == "guest":
            self.marshalingParams = PARAMETERS_MARSHALING_GUEST
        else:
            self.marshalingParams = PARAMETERS_MARSHALING

        self.writeCodegen = \
            VulkanMarshalingCodegen(
                None,
                VULKAN_STREAM_VAR_NAME,
                ROOT_TYPE_VAR_NAME,
                MARSHAL_INPUT_VAR_NAME,
                API_PREFIX_MARSHAL,
                direction = "write")

        self.readCodegen = \
            VulkanMarshalingCodegen(
                None,
                VULKAN_STREAM_VAR_NAME,
                ROOT_TYPE_VAR_NAME,
                UNMARSHAL_INPUT_VAR_NAME,
                API_PREFIX_UNMARSHAL,
                direction = "read",
                dynAlloc=self.dynAlloc)

        self.knownDefs = {}

        # Begin Vulkan API opcodes from something high
        # that is not going to interfere with renderControl
        # opcodes
        self.beginOpcodeOld = 20000
        self.endOpcodeOld = 30000

        self.beginOpcode = 200000000
        self.endOpcode = 300000000
        self.knownOpcodes = set()

        self.extensionMarshalPrototype = \
            VulkanAPI(API_PREFIX_MARSHAL + "extension_struct",
                      STREAM_RET_TYPE,
                      self.marshalingParams +
                      [STRUCT_EXTENSION_PARAM])

        self.extensionUnmarshalPrototype = \
            VulkanAPI(API_PREFIX_UNMARSHAL + "extension_struct",
                      STREAM_RET_TYPE,
                      self.marshalingParams +
                      [STRUCT_EXTENSION_PARAM_FOR_WRITE])

    def onBegin(self,):
        VulkanWrapperGenerator.onBegin(self)
        self.module.appendImpl(self.cgenImpl.makeFuncDecl(self.extensionMarshalPrototype))
        self.module.appendImpl(self.cgenImpl.makeFuncDecl(self.extensionUnmarshalPrototype))

    def onBeginFeature(self, featureName, featureType):
        VulkanWrapperGenerator.onBeginFeature(self, featureName, featureType)
        self.currentFeature = featureName

    def onGenType(self, typeXml, name, alias):
        VulkanWrapperGenerator.onGenType(self, typeXml, name, alias)

        if name in self.knownDefs:
            return

        category = self.typeInfo.categoryOf(name)

        if category in ["struct", "union"] and alias:
            self.module.appendHeader(
                self.cgenHeader.makeFuncAlias(API_PREFIX_MARSHAL + name,
                                              API_PREFIX_MARSHAL + alias))
            self.module.appendHeader(
                self.cgenHeader.makeFuncAlias(API_PREFIX_UNMARSHAL + name,
                                              API_PREFIX_UNMARSHAL + alias))

        if category in ["struct", "union"] and not alias:

            structInfo = self.typeInfo.structs[name]

            marshalParams = self.marshalingParams + \
                [makeVulkanTypeSimple(True, name, 1, MARSHAL_INPUT_VAR_NAME)]

            freeParams = []
            letParams = []

            for (envname, bindingInfo) in list(sorted(structInfo.environment.items(), key = lambda kv: kv[0])):
                if None == bindingInfo["binding"]:
                    freeParams.append(makeVulkanTypeSimple(True, bindingInfo["type"], 0, envname))
                else:
                    if not bindingInfo["structmember"]:
                        letParams.append(makeVulkanTypeSimple(True, bindingInfo["type"], 0, envname))

            marshalPrototype = \
                VulkanAPI(API_PREFIX_MARSHAL + name,
                          STREAM_RET_TYPE,
                          marshalParams + freeParams)

            marshalPrototypeNoFilter = \
                VulkanAPI(API_PREFIX_MARSHAL + name,
                          STREAM_RET_TYPE,
                          marshalParams)

            def structMarshalingCustom(cgen):
                self.writeCodegen.cgen = cgen
                self.writeCodegen.currentStructInfo = structInfo
                self.writeCodegen.cgen.stmt("(void)%s" % ROOT_TYPE_VAR_NAME)

                marshalingCode = \
                    CUSTOM_MARSHAL_TYPES[name]["common"] + \
                    CUSTOM_MARSHAL_TYPES[name]["marshaling"].format(
                        streamVarName=self.writeCodegen.streamVarName,
                        rootTypeVarName=self.writeCodegen.rootTypeVarName,
                        inputVarName=self.writeCodegen.inputVarName,
                        newInputVarName=self.writeCodegen.inputVarName + "_new")
                for line in marshalingCode.split('\n'):
                    cgen.line(line)

            def structMarshalingDef(cgen):
                self.writeCodegen.cgen = cgen
                self.writeCodegen.currentStructInfo = structInfo
                self.writeCodegen.cgen.stmt("(void)%s" % ROOT_TYPE_VAR_NAME)

                if category == "struct":
                    # marshal 'let' parameters first
                    for letp in letParams:
                        self.writeCodegen.streamLetParameter(self.typeInfo, letp)

                    for member in structInfo.members:
                        iterateVulkanType(self.typeInfo, member, self.writeCodegen)
                if category == "union":
                    iterateVulkanType(self.typeInfo, structInfo.members[0], self.writeCodegen)

            def structMarshalingDefNoFilter(cgen):
                self.writeCodegen.cgen = cgen
                self.writeCodegen.currentStructInfo = structInfo
                self.writeCodegen.doFiltering = False
                self.writeCodegen.cgen.stmt("(void)%s" % ROOT_TYPE_VAR_NAME)

                if category == "struct":
                    # marshal 'let' parameters first
                    for letp in letParams:
                        self.writeCodegen.streamLetParameter(self.typeInfo, letp)

                    for member in structInfo.members:
                        iterateVulkanType(self.typeInfo, member, self.writeCodegen)
                if category == "union":
                    iterateVulkanType(self.typeInfo, structInfo.members[0], self.writeCodegen)
                self.writeCodegen.doFiltering = True

            self.module.appendHeader(
                self.cgenHeader.makeFuncDecl(marshalPrototype))

            if name in CUSTOM_MARSHAL_TYPES:
                self.module.appendImpl(
                    self.cgenImpl.makeFuncImpl(
                        marshalPrototype, structMarshalingCustom))
            else:
                self.module.appendImpl(
                    self.cgenImpl.makeFuncImpl(
                        marshalPrototype, structMarshalingDef))

            if freeParams != []:
                self.module.appendHeader(
                    self.cgenHeader.makeFuncDecl(marshalPrototypeNoFilter))
                self.module.appendImpl(
                    self.cgenImpl.makeFuncImpl(
                        marshalPrototypeNoFilter, structMarshalingDefNoFilter))

            unmarshalPrototype = \
                VulkanAPI(API_PREFIX_UNMARSHAL + name,
                          STREAM_RET_TYPE,
                          self.marshalingParams + [makeVulkanTypeSimple(False, name, 1, UNMARSHAL_INPUT_VAR_NAME)] + freeParams)

            unmarshalPrototypeNoFilter = \
                VulkanAPI(API_PREFIX_UNMARSHAL + name,
                          STREAM_RET_TYPE,
                          self.marshalingParams + [makeVulkanTypeSimple(False, name, 1, UNMARSHAL_INPUT_VAR_NAME)])

            def structUnmarshalingCustom(cgen):
                self.readCodegen.cgen = cgen
                self.readCodegen.currentStructInfo = structInfo
                self.writeCodegen.cgen.stmt("(void)%s" % ROOT_TYPE_VAR_NAME)

                unmarshalingCode = \
                    CUSTOM_MARSHAL_TYPES[name]["common"] + \
                    CUSTOM_MARSHAL_TYPES[name]["unmarshaling"].format(
                        streamVarName=self.readCodegen.streamVarName,
                        rootTypeVarName=self.readCodegen.rootTypeVarName,
                        inputVarName=self.readCodegen.inputVarName,
                        newInputVarName=self.readCodegen.inputVarName + "_new")
                for line in unmarshalingCode.split('\n'):
                    cgen.line(line)

            def structUnmarshalingDef(cgen):
                self.readCodegen.cgen = cgen
                self.readCodegen.currentStructInfo = structInfo
                self.writeCodegen.cgen.stmt("(void)%s" % ROOT_TYPE_VAR_NAME)

                if category == "struct":
                    # unmarshal 'let' parameters first
                    for letp in letParams:
                        self.readCodegen.streamLetParameter(self.typeInfo, letp)

                    for member in structInfo.members:
                        iterateVulkanType(self.typeInfo, member, self.readCodegen)
                if category == "union":
                    iterateVulkanType(self.typeInfo, structInfo.members[0], self.readCodegen)

            def structUnmarshalingDefNoFilter(cgen):
                self.readCodegen.cgen = cgen
                self.readCodegen.currentStructInfo = structInfo
                self.readCodegen.doFiltering = False
                self.writeCodegen.cgen.stmt("(void)%s" % ROOT_TYPE_VAR_NAME)

                if category == "struct":
                    # unmarshal 'let' parameters first
                    for letp in letParams:
                        iterateVulkanType(self.typeInfo, letp, self.readCodegen)
                    for member in structInfo.members:
                        iterateVulkanType(self.typeInfo, member, self.readCodegen)
                if category == "union":
                    iterateVulkanType(self.typeInfo, structInfo.members[0], self.readCodegen)
                self.readCodegen.doFiltering = True

            self.module.appendHeader(
                self.cgenHeader.makeFuncDecl(unmarshalPrototype))

            if name in CUSTOM_MARSHAL_TYPES:
                self.module.appendImpl(
                    self.cgenImpl.makeFuncImpl(
                        unmarshalPrototype, structUnmarshalingCustom))
            else:
                self.module.appendImpl(
                    self.cgenImpl.makeFuncImpl(
                        unmarshalPrototype, structUnmarshalingDef))

            if freeParams != []:
                self.module.appendHeader(
                    self.cgenHeader.makeFuncDecl(unmarshalPrototypeNoFilter))
                self.module.appendImpl(
                    self.cgenImpl.makeFuncImpl(
                        unmarshalPrototypeNoFilter, structUnmarshalingDefNoFilter))

    def onGenCmd(self, cmdinfo, name, alias):
        VulkanWrapperGenerator.onGenCmd(self, cmdinfo, name, alias)
        if name in KNOWN_FUNCTION_OPCODES:
            opcode = KNOWN_FUNCTION_OPCODES[name]
        else:
            hashCode = hashlib.sha256(name.encode()).hexdigest()[:8]
            hashInt = int(hashCode, 16)
            opcode = self.beginOpcode + hashInt % (self.endOpcode - self.beginOpcode)
            hasHashCollision = False
            while opcode in self.knownOpcodes:
                hasHashCollision = True
                opcode += 1
            if hasHashCollision:
                print("Hash collision occurred on function '{}'. "
                      "Please add the following line to marshalingdefs.py:".format(name), file=sys.stderr)
                print("----------------------", file=sys.stderr)
                print("    \"{}\": {},".format(name, opcode), file=sys.stderr)
                print("----------------------", file=sys.stderr)

        self.module.appendHeader(
            "#define OP_%s %d\n" % (name, opcode))
        self.apiOpcodes[name] = (opcode, self.currentFeature)
        self.knownOpcodes.add(opcode)

    def doExtensionStructMarshalingCodegen(self, cgen, retType, extParam, forEach, funcproto, direction):
        accessVar = "structAccess"
        sizeVar = "currExtSize"
        cgen.stmt("VkInstanceCreateInfo* %s = (VkInstanceCreateInfo*)(%s)" % (accessVar, extParam.paramName))
        cgen.stmt("size_t %s = %s(%s->getFeatureBits(), %s, %s)" % (sizeVar,
                                                                    EXTENSION_SIZE_WITH_STREAM_FEATURES_API_NAME, VULKAN_STREAM_VAR_NAME, ROOT_TYPE_VAR_NAME, extParam.paramName))

        cgen.beginIf("!%s && %s" % (sizeVar, extParam.paramName))

        cgen.line("// unknown struct extension; skip and call on its pNext field");
        cgen.funcCall(None, funcproto.name, [
                      "vkStream", ROOT_TYPE_VAR_NAME, "(void*)%s->pNext" % accessVar])
        cgen.stmt("return")

        cgen.endIf()
        cgen.beginElse()

        cgen.line("// known or null extension struct")

        if direction == "write":
            cgen.stmt("vkStream->putBe32(%s)" % sizeVar)
        elif not self.dynAlloc:
            cgen.stmt("vkStream->getBe32()");

        cgen.beginIf("!%s" % (sizeVar))
        cgen.line("// exit if this was a null extension struct (size == 0 in this branch)")
        cgen.stmt("return")
        cgen.endIf()

        cgen.endIf()

        # Now we can do stream stuff
        if direction == "write":
            cgen.stmt("vkStream->write(%s, sizeof(VkStructureType))" % extParam.paramName)
        elif not self.dynAlloc:
            cgen.stmt("uint64_t pNext_placeholder")
            placeholderAccess = "(&pNext_placeholder)"
            cgen.stmt("vkStream->read((void*)(&pNext_placeholder), sizeof(VkStructureType))")
            cgen.stmt("(void)pNext_placeholder")

        def fatalDefault(cgen):
            cgen.line("// fatal; the switch is only taken if the extension struct is known")
            if self.variant != "guest":
                cgen.stmt("fprintf(stderr, \" %s, Unhandled Vulkan structure type %s [%d], aborting.\\n\", __func__, string_VkStructureType(VkStructureType(structType)), structType)")
            cgen.stmt("abort()")
            pass

        self.emitForEachStructExtension(
            cgen,
            retType,
            extParam,
            forEach,
            defaultEmit=fatalDefault,
            rootTypeVar=ROOT_TYPE_PARAM)

    def onEnd(self,):
        VulkanWrapperGenerator.onEnd(self)

        def forEachExtensionMarshal(ext, castedAccess, cgen):
            cgen.funcCall(None, API_PREFIX_MARSHAL + ext.name,
                          [VULKAN_STREAM_VAR_NAME, ROOT_TYPE_VAR_NAME, castedAccess])

        def forEachExtensionUnmarshal(ext, castedAccess, cgen):
            cgen.funcCall(None, API_PREFIX_UNMARSHAL + ext.name,
                          [VULKAN_STREAM_VAR_NAME, ROOT_TYPE_VAR_NAME, castedAccess])

        self.module.appendImpl(
            self.cgenImpl.makeFuncImpl(
                self.extensionMarshalPrototype,
                lambda cgen: self.doExtensionStructMarshalingCodegen(
                    cgen,
                    STREAM_RET_TYPE,
                    STRUCT_EXTENSION_PARAM,
                    forEachExtensionMarshal,
                    self.extensionMarshalPrototype,
                    "write")))

        self.module.appendImpl(
            self.cgenImpl.makeFuncImpl(
                self.extensionUnmarshalPrototype,
                lambda cgen: self.doExtensionStructMarshalingCodegen(
                    cgen,
                    STREAM_RET_TYPE,
                    STRUCT_EXTENSION_PARAM_FOR_WRITE,
                    forEachExtensionUnmarshal,
                    self.extensionUnmarshalPrototype,
                    "read")))

        opcode2stringPrototype = \
            VulkanAPI("api_opcode_to_string",
                          makeVulkanTypeSimple(True, "char", 1, "none"),
                          [ makeVulkanTypeSimple(True, "uint32_t", 0, "opcode") ])

        self.module.appendHeader(
            self.cgenHeader.makeFuncDecl(opcode2stringPrototype))

        def emitOpcode2StringImpl(apiOpcodes, cgen):
            cgen.line("switch(opcode)")
            cgen.beginBlock()

            currFeature = None

            for (name, (opcodeNum, feature)) in sorted(apiOpcodes.items(), key = lambda x : x[1][0]):
                if not currFeature:
                    cgen.leftline("#ifdef %s" % feature)
                    currFeature = feature

                if currFeature and feature != currFeature:
                    cgen.leftline("#endif")
                    cgen.leftline("#ifdef %s" % feature)
                    currFeature = feature

                cgen.line("case OP_%s:" % name)
                cgen.beginBlock()
                cgen.stmt("return \"OP_%s\"" % name)
                cgen.endBlock()

            if currFeature:
                cgen.leftline("#endif")

            cgen.line("default:")
            cgen.beginBlock()
            cgen.stmt("return \"OP_UNKNOWN_API_CALL\"")
            cgen.endBlock()

            cgen.endBlock()

        self.module.appendImpl(
            self.cgenImpl.makeFuncImpl(
                opcode2stringPrototype,
                lambda cgen: emitOpcode2StringImpl(self.apiOpcodes, cgen)))

        self.module.appendHeader(
            "#define OP_vkFirst_old %d\n" % (self.beginOpcodeOld))
        self.module.appendHeader(
            "#define OP_vkLast_old %d\n" % (self.endOpcodeOld))
        self.module.appendHeader(
            "#define OP_vkFirst %d\n" % (self.beginOpcode))
        self.module.appendHeader(
            "#define OP_vkLast %d\n" % (self.endOpcode))
