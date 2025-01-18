# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT
from copy import copy

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
from .wrapperdefs import API_PREFIX_RESERVEDMARSHAL
from .wrapperdefs import API_PREFIX_RESERVEDUNMARSHAL

from .marshalingdefs import CUSTOM_MARSHAL_TYPES
class VulkanReservedMarshalingCodegen(VulkanTypeIterator):
    def __init__(self,
                 cgen,
                 variant,
                 streamVarName,
                 rootTypeVarName,
                 inputVarName,
                 ptrVarName,
                 marshalPrefix,
                 handlemapPrefix,
                 direction = "write",
                 forApiOutput = False,
                 dynAlloc = False,
                 mapHandles = True,
                 handleMapOverwrites = False,
                 doFiltering = True,
                 stackVar=None,
                 stackArrSize=None):
        self.cgen = cgen
        self.variant = variant
        self.direction = direction
        self.processSimple = "write" if self.direction == "write" else "read"
        self.forApiOutput = forApiOutput

        self.checked = False

        self.streamVarName = streamVarName
        self.rootTypeVarName = rootTypeVarName
        self.inputVarName = inputVarName
        self.ptrVar = ptrVarName
        self.marshalPrefix = marshalPrefix
        self.handlemapPrefix = handlemapPrefix

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

        self.stackVar = stackVar
        self.stackArrSize = stackArrSize

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

    def genPtrIncr(self, sizeExpr):
        self.cgen.stmt("*%s += %s" % (self.ptrVar, sizeExpr))

    def genMemcpyAndIncr(self, varname, cast, toStreamExpr, sizeExpr, toBe = False, actualSize = 4):
        if self.direction == "write":
            self.cgen.stmt("memcpy(*%s, %s%s, %s)" % (varname, cast, toStreamExpr, sizeExpr))
        else:
            self.cgen.stmt("memcpy(%s%s, *%s, %s)" % (cast, toStreamExpr, varname, sizeExpr))

        if toBe:
            streamPrefix = "to"
            if "read" == self.direction:
                streamPrefix = "from"

            streamMethod = streamPrefix

            if 1 == actualSize:
                streamMethod += "Byte"
            elif 2 == actualSize:
                streamMethod += "Be16"
            elif 4 == actualSize:
                streamMethod += "Be32"
            elif 8 == actualSize:
                streamMethod += "Be64"
            else:
                pass

            streamNamespace = "android::base"
            if self.direction == "write":
                self.cgen.stmt("%s::Stream::%s((uint8_t*)*%s)" % (streamNamespace, streamMethod, varname))
            else:
                self.cgen.stmt("%s::Stream::%s((uint8_t*)%s)" % (streamNamespace, streamMethod, toStreamExpr))

        self.genPtrIncr(sizeExpr)

    def genStreamCall(self, vulkanType, toStreamExpr, sizeExpr):
        varname = self.ptrVar
        cast = self.makeCastExpr(self.getTypeForStreaming(vulkanType))
        self.genMemcpyAndIncr(varname, cast, toStreamExpr, sizeExpr)

    def genPrimitiveStreamCall(self, vulkanType, access):
        varname = self.ptrVar
        self.cgen.memcpyPrimitive(
            self.typeInfo,
            "(*" + varname + ")",
            access,
            vulkanType,
            self.variant,
            direction=self.direction)
        self.genPtrIncr(str(self.cgen.countPrimitive(
            self.typeInfo,
            vulkanType)))

    def genHandleMappingCall(self, vulkanType, access, lenAccess, lenAccessGuard):

        if lenAccess is None:
            lenAccess = "1"
            handle64Bytes = "8"
        else:
            handle64Bytes = "%s * 8" % lenAccess

        handle64Var = self.cgen.var()
        if lenAccess != "1":
            self.cgen.beginIf(lenAccess)
            self.cgen.stmt("uint8_t* %s_ptr = (uint8_t*)(*%s)" % (handle64Var, self.ptrVar))
            handle64VarAccess = handle64Var
            handle64VarType = \
                makeVulkanTypeSimple(False, "uint64_t", 1, paramName=handle64Var)
        else:
            self.cgen.stmt("uint64_t %s" % handle64Var)
            handle64VarAccess = "&%s" % handle64Var
            handle64VarType = \
                makeVulkanTypeSimple(False, "uint64_t", 0, paramName=handle64Var)

        if "" == self.handlemapPrefix:
            mapFunc = ("(%s)" % vulkanType.typeName)
            mapFunc64 = ("(%s)" % "uint64_t")
        else:
            mapFunc = self.handlemapPrefix + vulkanType.typeName
            mapFunc64 = mapFunc

        if self.direction == "write":
            if self.handleMapOverwrites:
                self.cgen.stmt(
                    "static_assert(8 == sizeof(%s), \"handle map overwrite requres %s to be 8 bytes long\")" % \
                            (vulkanType.typeName, vulkanType.typeName))
                if "1" == lenAccess:
                    self.cgen.stmt("*%s = (%s)%s(*%s)" % (access, vulkanType.typeName, mapFunc, access))
                    self.genStreamCall(vulkanType, access, "8 * %s" % lenAccess)
                else:
                    if lenAccessGuard is not None:
                        self.cgen.beginIf(lenAccessGuard)
                    self.cgen.beginFor("uint32_t k = 0", "k < %s" % lenAccess, "++k")
                    self.cgen.stmt("%s[k] = (%s)%s(%s[k])" % (access, vulkanType.typeName, mapFunc, access))
                    self.cgen.endFor()
                    if lenAccessGuard is not None:
                        self.cgen.endIf()
                    self.genPtrIncr("8 * %s" % lenAccess)
            else:
                if "1" == lenAccess:
                    self.cgen.stmt("*%s = %s((*%s))" % (handle64VarAccess, mapFunc64, access))
                    self.genStreamCall(handle64VarType, handle64VarAccess, handle64Bytes)
                else:
                    if lenAccessGuard is not None:
                        self.cgen.beginIf(lenAccessGuard)
                    self.cgen.beginFor("uint32_t k = 0", "k < %s" % lenAccess, "++k")
                    self.cgen.stmt("uint64_t tmpval = %s(%s[k])" % (mapFunc64, access))
                    self.cgen.stmt("memcpy(%s_ptr + k * 8, &tmpval, sizeof(uint64_t))" % (handle64Var))
                    self.cgen.endFor()
                    if lenAccessGuard is not None:
                        self.cgen.endIf()
                    self.genPtrIncr("8 * %s" % lenAccess)
        else:
            if "1" == lenAccess:
                self.genStreamCall(handle64VarType, handle64VarAccess, handle64Bytes)
                self.cgen.stmt("*%s%s = (%s)%s((%s)(*%s))" % (
                    self.makeCastExpr(vulkanType.getForNonConstAccess()), access,
                    vulkanType.typeName, mapFunc, vulkanType.typeName, handle64VarAccess))
            else:
                self.genPtrIncr("8 * %s" % lenAccess)
                if lenAccessGuard is not None:
                    self.cgen.beginIf(lenAccessGuard)
                self.cgen.beginFor("uint32_t k = 0", "k < %s" % lenAccess, "++k")
                self.cgen.stmt("uint64_t tmpval; memcpy(&tmpval, %s_ptr + k * 8, sizeof(uint64_t))" % handle64Var)
                self.cgen.stmt("*((%s%s) + k) = tmpval ? (%s)%s((%s)tmpval) : VK_NULL_HANDLE" % (
                    self.makeCastExpr(vulkanType.getForNonConstAccess()), access,
                    vulkanType.typeName, mapFunc, vulkanType.typeName))
                if lenAccessGuard is not None:
                    self.cgen.endIf()
                self.cgen.endFor()

        if lenAccess != "1":
            self.cgen.endIf()

    def doAllocSpace(self, vulkanType):
        if self.dynAlloc and self.direction == "read":
            access = self.exprAccessor(vulkanType)
            lenAccess = self.lenAccessor(vulkanType)
            sizeof = self.cgen.sizeofExpr(vulkanType.getForValueAccess())
            if lenAccess:
                bytesExpr = "%s * %s" % (lenAccess, sizeof)
            else:
                bytesExpr = sizeof
                lenAccess = "1"

            if self.stackVar:
                if self.stackArrSize != lenAccess:
                    self.cgen.beginIf("%s <= %s" % (lenAccess, self.stackArrSize))

                self.cgen.stmt(
                        "%s = %s%s" % (access, self.makeCastExpr(vulkanType.getForNonConstAccess()), self.stackVar))

                if self.stackArrSize != lenAccess:
                    self.cgen.endIf()
                    self.cgen.beginElse()

                if self.stackArrSize != lenAccess:
                    self.cgen.stmt(
                            "%s->alloc((void**)&%s, %s)" %
                            (self.streamVarName,
                                access, bytesExpr))

                if self.stackArrSize != lenAccess:
                    self.cgen.endIf()
            else:
                self.cgen.stmt(
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

        featureExpr = self.getOptionalStringFeatureExpr(vulkanType)
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

        self.beginFilterGuard(vulkanType)

        if vulkanType.pointerIndirectionLevels > 0:
            self.doAllocSpace(vulkanType)

        if lenAccess is not None:
            loopVar = "i"
            access = "%s + %s" % (access, loopVar)
            forInit = "uint32_t %s = 0" % loopVar
            forCond = "%s < (uint32_t)%s" % (loopVar, lenAccess)
            forIncr = "++%s" % loopVar
            self.cgen.beginFor(forInit, forCond, forIncr)

        accessWithCast = "%s(%s)" % (self.makeCastExpr(
            self.getTypeForStreaming(vulkanType)), access)

        callParams = [self.streamVarName, self.rootTypeVarName, accessWithCast, self.ptrVar]

        for (bindName, localName) in vulkanType.binds.items():
            callParams.append(self.getEnvAccessExpr(localName))

        self.cgen.funcCall(None, self.marshalPrefix + vulkanType.typeName,
                           callParams)

        if lenAccess is not None:
            self.cgen.endFor()

        if self.direction == "read":
            self.endFilterGuard(vulkanType, "%s = 0" % self.exprAccessor(vulkanType))
        else:
            self.endFilterGuard(vulkanType)

    def onString(self, vulkanType):
        access = self.exprAccessor(vulkanType)

        if self.direction == "write":
            self.cgen.beginBlock()
            self.cgen.stmt("uint32_t l = %s ? strlen(%s): 0" % (access, access))
            self.genMemcpyAndIncr(self.ptrVar, "(uint32_t*)" ,"&l", "sizeof(uint32_t)", toBe = True, actualSize = 4)
            self.genMemcpyAndIncr(self.ptrVar, "(char*)", access, "l")
            self.cgen.endBlock()
        else:
            castExpr = \
                self.makeCastExpr( \
                    self.getTypeForStreaming( \
                        vulkanType.getForAddressAccess()))
            self.cgen.stmt( \
                "%s->loadStringInPlaceWithStreamPtr(%s&%s, %s)" % (self.streamVarName, castExpr, access, self.ptrVar))

    def onStringArray(self, vulkanType):
        access = self.exprAccessor(vulkanType)
        lenAccess = self.lenAccessor(vulkanType)
        lenAccessGuard = self.lenAccessorGuard(vulkanType)

        if self.direction == "write":
            self.cgen.beginBlock()

            self.cgen.stmt("uint32_t c = 0")
            if lenAccessGuard is not None:
                self.cgen.beginIf(lenAccessGuard)
            self.cgen.stmt("c = %s" % (lenAccess))
            if lenAccessGuard is not None:
                self.cgen.endIf()
            self.genMemcpyAndIncr(self.ptrVar, "(uint32_t*)" ,"&c", "sizeof(uint32_t)", toBe = True, actualSize = 4)

            self.cgen.beginFor("uint32_t i = 0", "i < c", "++i")
            self.cgen.stmt("uint32_t l = %s ? strlen(%s[i]): 0" % (access, access))
            self.genMemcpyAndIncr(self.ptrVar, "(uint32_t*)" ,"&l", "sizeof(uint32_t)", toBe = True, actualSize = 4)
            self.cgen.beginIf("l")
            self.genMemcpyAndIncr(self.ptrVar, "(char*)", "(%s[i])" % access, "l")
            self.cgen.endIf()
            self.cgen.endFor()

            self.cgen.endBlock()
        else:
            castExpr = \
                self.makeCastExpr( \
                    self.getTypeForStreaming( \
                        vulkanType.getForAddressAccess()))

            self.cgen.stmt("%s->loadStringArrayInPlaceWithStreamPtr(%s&%s, %s)" % (self.streamVarName, castExpr, access, self.ptrVar))

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
        self.cgen.beginIf("%s == VK_STRUCTURE_TYPE_MAX_ENUM" % self.rootTypeVarName)
        self.cgen.stmt("%s = %s" % (self.rootTypeVarName, sTypeAccess))
        self.cgen.endIf()

        if self.direction == "read" and self.dynAlloc:
            self.cgen.stmt("uint32_t %s" % sizeVar)

            self.genMemcpyAndIncr(self.ptrVar, "(uint32_t*)", "&" + sizeVar, "sizeof(uint32_t)", toBe = True, actualSize = 4)

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
                [self.streamVarName, self.rootTypeVarName, castedAccessExpr, self.ptrVar])
            self.cgen.endIf()
        else:

            self.cgen.funcCall(None, self.marshalPrefix + "extension_struct",
                [self.streamVarName, self.rootTypeVarName, castedAccessExpr, self.ptrVar])

    def onPointer(self, vulkanType):
        access = self.exprAccessor(vulkanType)

        lenAccess = self.lenAccessor(vulkanType)
        lenAccessGuard = self.lenAccessorGuard(vulkanType)

        self.beginFilterGuard(vulkanType)
        self.doAllocSpace(vulkanType)

        if vulkanType.isHandleType() and self.mapHandles:
            self.genHandleMappingCall(
                vulkanType, access, lenAccess, lenAccessGuard)
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
                vulkanType.getForAddressAccess(), access, "1", None)
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

class VulkanReservedMarshaling(VulkanWrapperGenerator):

    def __init__(self, module, typeInfo, variant="host"):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)

        self.cgenHeader = CodeGen()
        self.cgenImpl = CodeGen()

        self.variant = variant

        self.currentFeature = None
        self.apiOpcodes = {}
        self.dynAlloc = self.variant != "guest"
        self.ptrVarName = "ptr"
        self.ptrVarType = makeVulkanTypeSimple(False, "uint8_t", 2, self.ptrVarName)
        self.ptrVarTypeUnmarshal = makeVulkanTypeSimple(False, "uint8_t", 2, self.ptrVarName)

        if self.variant == "guest":
            self.marshalingParams = PARAMETERS_MARSHALING_GUEST
        else:
            self.marshalingParams = PARAMETERS_MARSHALING

        self.writeCodegen = \
            VulkanReservedMarshalingCodegen(
                None,
                self.variant,
                VULKAN_STREAM_VAR_NAME,
                ROOT_TYPE_VAR_NAME,
                MARSHAL_INPUT_VAR_NAME,
                self.ptrVarName,
                API_PREFIX_RESERVEDMARSHAL,
                "get_host_u64_" if "guest" == self.variant else "",
                direction = "write")

        self.readCodegen = \
            VulkanReservedMarshalingCodegen(
                None,
                self.variant,
                VULKAN_STREAM_VAR_NAME,
                ROOT_TYPE_VAR_NAME,
                UNMARSHAL_INPUT_VAR_NAME,
                self.ptrVarName,
                API_PREFIX_RESERVEDUNMARSHAL,
                "unbox_" if "host" == self.variant else "",
                direction = "read",
                dynAlloc=self.dynAlloc)

        self.knownDefs = {}

        self.extensionMarshalPrototype = \
            VulkanAPI(API_PREFIX_RESERVEDMARSHAL + "extension_struct",
                      STREAM_RET_TYPE,
                      self.marshalingParams +
                      [STRUCT_EXTENSION_PARAM, self.ptrVarType])

        self.extensionUnmarshalPrototype = \
            VulkanAPI(API_PREFIX_RESERVEDUNMARSHAL + "extension_struct",
                      STREAM_RET_TYPE,
                      self.marshalingParams +
                      [STRUCT_EXTENSION_PARAM_FOR_WRITE, self.ptrVarTypeUnmarshal])

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
            if self.variant != "host":
                self.module.appendHeader(
                    self.cgenHeader.makeFuncAlias(API_PREFIX_RESERVEDMARSHAL + name,
                                                  API_PREFIX_RESERVEDMARSHAL + alias))
            if self.variant != "guest":
                self.module.appendHeader(
                    self.cgenHeader.makeFuncAlias(API_PREFIX_RESERVEDUNMARSHAL + name,
                                                  API_PREFIX_RESERVEDUNMARSHAL + alias))

        if category in ["struct", "union"] and not alias:

            structInfo = self.typeInfo.structs[name]

            marshalParams = self.marshalingParams + \
                [makeVulkanTypeSimple(True, name, 1, MARSHAL_INPUT_VAR_NAME),
                        self.ptrVarType]

            freeParams = []
            letParams = []

            for (envname, bindingInfo) in list(sorted(structInfo.environment.items(), key = lambda kv: kv[0])):
                if None == bindingInfo["binding"]:
                    freeParams.append(makeVulkanTypeSimple(True, bindingInfo["type"], 0, envname))
                else:
                    if not bindingInfo["structmember"]:
                        letParams.append(makeVulkanTypeSimple(True, bindingInfo["type"], 0, envname))

            marshalPrototype = \
                VulkanAPI(API_PREFIX_RESERVEDMARSHAL + name,
                          STREAM_RET_TYPE,
                          marshalParams + freeParams)

            marshalPrototypeNoFilter = \
                VulkanAPI(API_PREFIX_RESERVEDMARSHAL + name,
                          STREAM_RET_TYPE,
                          marshalParams)

            def structMarshalingCustom(cgen):
                self.writeCodegen.cgen = cgen
                self.writeCodegen.currentStructInfo = structInfo
                marshalingCode = \
                    CUSTOM_MARSHAL_TYPES[name]["common"] + \
                    CUSTOM_MARSHAL_TYPES[name]["reservedmarshaling"].format(
                        streamVarName=self.writeCodegen.streamVarName,
                        rootTypeVarName=self.writeCodegen.rootTypeVarName,
                        inputVarName=self.writeCodegen.inputVarName,
                        newInputVarName=self.writeCodegen.inputVarName + "_new")
                for line in marshalingCode.split('\n'):
                    cgen.line(line)

            def structMarshalingDef(cgen):
                self.writeCodegen.cgen = cgen
                self.writeCodegen.currentStructInfo = structInfo
                self.writeCodegen.cgen.stmt("(void)%s" % VULKAN_STREAM_VAR_NAME)
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
                self.writeCodegen.cgen.stmt("(void)%s" % VULKAN_STREAM_VAR_NAME)
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

            if self.variant != "host":
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
                VulkanAPI(API_PREFIX_RESERVEDUNMARSHAL + name,
                          STREAM_RET_TYPE,
                          self.marshalingParams + [makeVulkanTypeSimple(False, name, 1, UNMARSHAL_INPUT_VAR_NAME), self.ptrVarTypeUnmarshal] + freeParams)

            unmarshalPrototypeNoFilter = \
                VulkanAPI(API_PREFIX_RESERVEDUNMARSHAL + name,
                          STREAM_RET_TYPE,
                          self.marshalingParams + [makeVulkanTypeSimple(False, name, 1, UNMARSHAL_INPUT_VAR_NAME), self.ptrVarTypeUnmarshal])

            def structUnmarshalingCustom(cgen):
                self.readCodegen.cgen = cgen
                self.readCodegen.currentStructInfo = structInfo
                unmarshalingCode = \
                    CUSTOM_MARSHAL_TYPES[name]["common"] + \
                    CUSTOM_MARSHAL_TYPES[name]["reservedunmarshaling"].format(
                        streamVarName=self.readCodegen.streamVarName,
                        rootTypeVarName=self.readCodegen.rootTypeVarName,
                        inputVarName=self.readCodegen.inputVarName,
                        newInputVarName=self.readCodegen.inputVarName + "_new")
                for line in unmarshalingCode.split('\n'):
                    cgen.line(line)

            def structUnmarshalingDef(cgen):
                self.readCodegen.cgen = cgen
                self.readCodegen.currentStructInfo = structInfo
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
                if category == "struct":
                    # unmarshal 'let' parameters first
                    for letp in letParams:
                        iterateVulkanType(self.typeInfo, letp, self.readCodegen)
                    for member in structInfo.members:
                        iterateVulkanType(self.typeInfo, member, self.readCodegen)
                if category == "union":
                    iterateVulkanType(self.typeInfo, structInfo.members[0], self.readCodegen)
                self.readCodegen.doFiltering = True

            if self.variant != "guest":
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

    def doExtensionStructMarshalingCodegen(self, cgen, retType, extParam, forEach, funcproto, direction):
        accessVar = "structAccess"
        sizeVar = "currExtSize"
        cgen.stmt("VkInstanceCreateInfo* %s = (VkInstanceCreateInfo*)(%s)" % (accessVar, extParam.paramName))
        cgen.stmt("uint32_t %s = %s(%s->getFeatureBits(), %s, %s)" % (sizeVar, EXTENSION_SIZE_WITH_STREAM_FEATURES_API_NAME, VULKAN_STREAM_VAR_NAME, ROOT_TYPE_VAR_NAME, extParam.paramName))

        cgen.beginIf("!%s && %s" % (sizeVar, extParam.paramName))

        cgen.line("// unknown struct extension; skip and call on its pNext field");
        cgen.funcCall(None, funcproto.name, ["vkStream", ROOT_TYPE_VAR_NAME, "(void*)%s->pNext" % accessVar, self.ptrVarName])
        cgen.stmt("return")

        cgen.endIf()
        cgen.beginElse()

        cgen.line("// known or null extension struct")

        streamNamespace = "android::base"

        if direction == "write":
            cgen.stmt("memcpy(*%s, &%s, sizeof(uint32_t));" % (self.ptrVarName, sizeVar))
            cgen.stmt("%s::Stream::toBe32((uint8_t*)*%s); *%s += sizeof(uint32_t)" % (streamNamespace, self.ptrVarName, self.ptrVarName))
        elif not self.dynAlloc:
            cgen.stmt("memcpy(&%s, *%s, sizeof(uint32_t));" % (sizeVar, self.ptrVarName))
            cgen.stmt("%s::Stream::fromBe32((uint8_t*)&%s); *%s += sizeof(uint32_t)" % (streamNamespace, sizeVar, self.ptrVarName))

        cgen.beginIf("!%s" % (sizeVar))
        cgen.line("// exit if this was a null extension struct (size == 0 in this branch)")
        cgen.stmt("return")
        cgen.endIf()

        cgen.endIf()

        # Now we can do stream stuff
        if direction == "write":
            cgen.stmt("memcpy(*%s, %s, sizeof(VkStructureType)); *%s += sizeof(VkStructureType)" % (self.ptrVarName, extParam.paramName, self.ptrVarName))
        elif not self.dynAlloc:
            cgen.stmt("uint64_t pNext_placeholder")
            placeholderAccess = "(&pNext_placeholder)"
            cgen.stmt("memcpy(%s, *%s, sizeof(VkStructureType)); *%s += sizeof(VkStructureType)" % (placeholderAccess, self.ptrVarName, self.ptrVarName))
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
            cgen.funcCall(None, API_PREFIX_RESERVEDMARSHAL + ext.name,
                          [VULKAN_STREAM_VAR_NAME, ROOT_TYPE_VAR_NAME, castedAccess, self.ptrVarName])

        def forEachExtensionUnmarshal(ext, castedAccess, cgen):
            cgen.funcCall(None, API_PREFIX_RESERVEDUNMARSHAL + ext.name,
                          [VULKAN_STREAM_VAR_NAME, ROOT_TYPE_VAR_NAME, castedAccess, self.ptrVarName])

        if self.variant != "host":
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

        if self.variant != "guest":
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
