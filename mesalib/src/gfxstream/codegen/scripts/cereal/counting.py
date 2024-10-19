# Copyright 2023 Google LLC
# SPDX-License-Identifier: MIT

from copy import copy

from .common.codegen import CodeGen
from .common.vulkantypes import \
        VulkanAPI, makeVulkanTypeSimple, iterateVulkanType, VulkanTypeIterator, Atom, FuncExpr, FuncExprVal, FuncLambda

from .wrapperdefs import VulkanWrapperGenerator
from .wrapperdefs import ROOT_TYPE_VAR_NAME, ROOT_TYPE_PARAM
from .wrapperdefs import STRUCT_EXTENSION_PARAM, STRUCT_EXTENSION_PARAM_FOR_WRITE, EXTENSION_SIZE_WITH_STREAM_FEATURES_API_NAME

class VulkanCountingCodegen(VulkanTypeIterator):
    def __init__(self, cgen, featureBitsVar, toCountVar, countVar, rootTypeVar, prefix, forApiOutput=False, mapHandles=True, handleMapOverwrites=False, doFiltering=True):
        self.cgen = cgen
        self.featureBitsVar = featureBitsVar
        self.toCountVar = toCountVar
        self.rootTypeVar = rootTypeVar
        self.countVar = countVar
        self.prefix = prefix
        self.forApiOutput = forApiOutput

        self.exprAccessor = lambda t: self.cgen.generalAccess(t, parentVarName = self.toCountVar, asPtr = True)
        self.exprValueAccessor = lambda t: self.cgen.generalAccess(t, parentVarName = self.toCountVar, asPtr = False)
        self.exprPrimitiveValueAccessor = lambda t: self.cgen.generalAccess(t, parentVarName = self.toCountVar, asPtr = False)

        self.lenAccessor = lambda t: self.cgen.generalLengthAccess(t, parentVarName = self.toCountVar)
        self.lenAccessorGuard = lambda t: self.cgen.generalLengthAccessGuard(t, parentVarName = self.toCountVar)
        self.filterVarAccessor = lambda t: self.cgen.filterVarAccess(t, parentVarName = self.toCountVar)

        self.checked = False

        self.mapHandles = mapHandles
        self.handleMapOverwrites = handleMapOverwrites
        self.doFiltering = doFiltering

    def getTypeForStreaming(self, vulkanType):
        res = copy(vulkanType)

        if not vulkanType.accessibleAsPointer():
            res = res.getForAddressAccess()

        if vulkanType.staticArrExpr:
            res = res.getForAddressAccess()

        return res

    def makeCastExpr(self, vulkanType):
        return "(%s)" % (
            self.cgen.makeCTypeDecl(vulkanType, useParamName=False))

    def genCount(self, sizeExpr):
        self.cgen.stmt("*%s += %s" % (self.countVar, sizeExpr))

    def genPrimitiveStreamCall(self, vulkanType):
        self.genCount(str(self.cgen.countPrimitive(
            self.typeInfo,
            vulkanType)))

    def genHandleMappingCall(self, vulkanType, access, lenAccess):

        if lenAccess is None:
            lenAccess = "1"
            handle64Bytes = "8"
        else:
            handle64Bytes = "%s * 8" % lenAccess

        handle64Var = self.cgen.var()
        if lenAccess != "1":
            self.cgen.beginIf(lenAccess)
            # self.cgen.stmt("uint64_t* %s" % handle64Var)
            # self.cgen.stmt(
                # "%s->alloc((void**)&%s, %s * 8)" % \
                # (self.streamVarName, handle64Var, lenAccess))
            handle64VarAccess = handle64Var
            handle64VarType = \
                makeVulkanTypeSimple(False, "uint64_t", 1, paramName=handle64Var)
        else:
            self.cgen.stmt("uint64_t %s" % handle64Var)
            handle64VarAccess = "&%s" % handle64Var
            handle64VarType = \
                makeVulkanTypeSimple(False, "uint64_t", 0, paramName=handle64Var)

        if self.handleMapOverwrites:
            # self.cgen.stmt(
                # "static_assert(8 == sizeof(%s), \"handle map overwrite requres %s to be 8 bytes long\")" % \
                        # (vulkanType.typeName, vulkanType.typeName))
            # self.cgen.stmt(
                # "%s->handleMapping()->mapHandles_%s((%s*)%s, %s)" %
                # (self.streamVarName, vulkanType.typeName, vulkanType.typeName,
                # access, lenAccess))
            self.genCount("8 * %s" % lenAccess)
        else:
            # self.cgen.stmt(
                # "%s->handleMapping()->mapHandles_%s_u64(%s, %s, %s)" %
                # (self.streamVarName, vulkanType.typeName,
                # access,
                # handle64VarAccess, lenAccess))
            self.genCount(handle64Bytes)

        if lenAccess != "1":
            self.cgen.endIf()

    def doAllocSpace(self, vulkanType):
        pass

    def getOptionalStringFeatureExpr(self, vulkanType):
        feature = vulkanType.getProtectStreamFeature()
        if feature is None:
            return None
        return "%s & %s" % (self.featureBitsVar, feature)

    def onCheck(self, vulkanType):

        if self.forApiOutput:
            return

        featureExpr = self.getOptionalStringFeatureExpr(vulkanType);

        self.checked = True

        access = self.exprAccessor(vulkanType)

        needConsistencyCheck = False

        self.cgen.line("// WARNING PTR CHECK")
        checkAccess = self.exprAccessor(vulkanType)
        addrExpr = "&" + checkAccess
        sizeExpr = self.cgen.sizeofExpr(vulkanType)

        if featureExpr is not None:
            self.cgen.beginIf(featureExpr)

        self.genPrimitiveStreamCall(
            vulkanType)

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
        self.cgen.beginIf("%s & VULKAN_STREAM_FEATURE_NULL_OPTIONAL_STRINGS_BIT" % self.featureBitsVar)
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

        filterFeature = "%s & VULKAN_STREAM_FEATURE_IGNORED_HANDLES_BIT" % self.featureBitsVar

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

        callParams = [self.featureBitsVar,
                      self.rootTypeVar, accessWithCast, self.countVar]

        for (bindName, localName) in vulkanType.binds.items():
            callParams.append(self.getEnvAccessExpr(localName))

        self.cgen.funcCall(None, self.prefix + vulkanType.typeName,
                           callParams)

        if lenAccess is not None:
            self.cgen.endFor()
            if lenAccessGuard is not None:
                self.cgen.endIf()

        self.endFilterGuard(vulkanType)

    def onString(self, vulkanType):
        access = self.exprAccessor(vulkanType)
        self.genCount("sizeof(uint32_t) + (%s ? strlen(%s) : 0)" % (access, access))

    def onStringArray(self, vulkanType):
        access = self.exprAccessor(vulkanType)
        lenAccess = self.lenAccessor(vulkanType)
        lenAccessGuard = self.lenAccessorGuard(vulkanType)

        self.genCount("sizeof(uint32_t)")
        if lenAccessGuard is not None:
            self.cgen.beginIf(lenAccessGuard)
        self.cgen.beginFor("uint32_t i = 0", "i < %s" % lenAccess, "++i")
        self.cgen.stmt("size_t l = %s[i] ? strlen(%s[i]) : 0" % (access, access))
        self.genCount("sizeof(uint32_t) + (%s[i] ? strlen(%s[i]) : 0)" % (access, access))
        self.cgen.endFor()
        if lenAccessGuard is not None:
            self.cgen.endIf()

    def onStaticArr(self, vulkanType):
        access = self.exprValueAccessor(vulkanType)
        lenAccess = self.lenAccessor(vulkanType)
        lenAccessGuard = self.lenAccessorGuard(vulkanType)

        if lenAccessGuard is not None:
            self.cgen.beginIf(lenAccessGuard)
        finalLenExpr = "%s * %s" % (lenAccess, self.cgen.sizeofExpr(vulkanType))
        if lenAccessGuard is not None:
            self.cgen.endIf()
        self.genCount(finalLenExpr)

    def onStructExtension(self, vulkanType):
        sTypeParam = copy(vulkanType)
        sTypeParam.paramName = "sType"

        access = self.exprAccessor(vulkanType)
        sizeVar = "%s_size" % vulkanType.paramName

        castedAccessExpr = access

        sTypeAccess = self.exprAccessor(sTypeParam)
        self.cgen.beginIf("%s == VK_STRUCTURE_TYPE_MAX_ENUM" %
                          self.rootTypeVar)
        self.cgen.stmt("%s = %s" % (self.rootTypeVar, sTypeAccess))
        self.cgen.endIf()

        self.cgen.funcCall(None, self.prefix + "extension_struct",
                           [self.featureBitsVar, self.rootTypeVar, castedAccessExpr, self.countVar])


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
                    self.genPrimitiveStreamCall(vulkanType.getForValueAccess())
                    self.cgen.endFor()
                    if lenAccessGuard is not None:
                        self.cgen.endIf()
                else:
                    self.genPrimitiveStreamCall(vulkanType.getForValueAccess())
            else:
                if lenAccess is not None:
                    needLenAccessGuard = True
                    finalLenExpr = "%s * %s" % (
                        lenAccess, self.cgen.sizeofExpr(vulkanType.getForValueAccess()))
                else:
                    needLenAccessGuard = False
                    finalLenExpr = "%s" % (
                        self.cgen.sizeofExpr(vulkanType.getForValueAccess()))
                if needLenAccessGuard and lenAccessGuard is not None:
                    self.cgen.beginIf(lenAccessGuard)
                self.genCount(finalLenExpr)
                if needLenAccessGuard and lenAccessGuard is not None:
                    self.cgen.endIf()

        self.endFilterGuard(vulkanType)

    def onValue(self, vulkanType):
        self.beginFilterGuard(vulkanType)

        if vulkanType.isHandleType() and self.mapHandles:
            access = self.exprAccessor(vulkanType)
            self.genHandleMappingCall(
                vulkanType.getForAddressAccess(), access, "1")
        elif self.typeInfo.isNonAbiPortableType(vulkanType.typeName):
            access = self.exprPrimitiveValueAccessor(vulkanType)
            self.genPrimitiveStreamCall(vulkanType)
        else:
            access = self.exprAccessor(vulkanType)
            self.genCount(self.cgen.sizeofExpr(vulkanType))

        self.endFilterGuard(vulkanType)

    def streamLetParameter(self, structInfo, letParamInfo):
        filterFeature = "%s & VULKAN_STREAM_FEATURE_IGNORED_HANDLES_BIT" % (self.featureBitsVar)
        self.cgen.stmt("%s %s = 1" % (letParamInfo.typeName, letParamInfo.paramName))

        self.cgen.beginIf(filterFeature)

        bodyExpr = self.currentStructInfo.environment[letParamInfo.paramName]["body"]
        self.cgen.stmt("%s = %s" % (letParamInfo.paramName, self.genFilterFunc(bodyExpr, self.currentStructInfo.environment)))

        self.genPrimitiveStreamCall(letParamInfo)

        self.cgen.endIf()

class VulkanCounting(VulkanWrapperGenerator):

    def __init__(self, module, typeInfo):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)

        self.codegen = CodeGen()

        self.featureBitsVar = "featureBits"
        self.featureBitsVarType = makeVulkanTypeSimple(False, "uint32_t", 0, self.featureBitsVar)
        self.countingPrefix = "count_"
        self.countVars = ["toCount", "count"]
        self.countVarType = makeVulkanTypeSimple(False, "size_t", 1, self.countVars[1])
        self.voidType = makeVulkanTypeSimple(False, "void", 0)
        self.rootTypeVar = ROOT_TYPE_VAR_NAME

        self.countingCodegen = \
            VulkanCountingCodegen(
                self.codegen,
                self.featureBitsVar,
                self.countVars[0],
                self.countVars[1],
                self.rootTypeVar,
                self.countingPrefix)

        self.knownDefs = {}

        self.extensionCountingPrototype = \
            VulkanAPI(self.countingPrefix + "extension_struct",
                      self.voidType,
                      [self.featureBitsVarType,
                       ROOT_TYPE_PARAM,
                       STRUCT_EXTENSION_PARAM,
                       self.countVarType])

    def onBegin(self,):
        VulkanWrapperGenerator.onBegin(self)
        self.module.appendImpl(self.codegen.makeFuncDecl(
            self.extensionCountingPrototype))

    def onGenType(self, typeXml, name, alias):
        VulkanWrapperGenerator.onGenType(self, typeXml, name, alias)

        if name in self.knownDefs:
            return

        category = self.typeInfo.categoryOf(name)

        if category in ["struct", "union"] and alias:
            # TODO(liyl): might not work if freeParams != []
            self.module.appendHeader(
                self.codegen.makeFuncAlias(self.countingPrefix + name,
                                           self.countingPrefix + alias))

        if category in ["struct", "union"] and not alias:

            structInfo = self.typeInfo.structs[name]

            freeParams = []
            letParams = []

            for (envname, bindingInfo) in list(sorted(structInfo.environment.items(), key = lambda kv: kv[0])):
                if None == bindingInfo["binding"]:
                    freeParams.append(makeVulkanTypeSimple(True, bindingInfo["type"], 0, envname))
                else:
                    if not bindingInfo["structmember"]:
                        letParams.append(makeVulkanTypeSimple(True, bindingInfo["type"], 0, envname))

            typeFromName = \
                lambda varname: \
                    makeVulkanTypeSimple(True, name, 1, varname)

            countingParams = \
                [makeVulkanTypeSimple(False, "uint32_t", 0, self.featureBitsVar),
                 ROOT_TYPE_PARAM,
                 typeFromName(self.countVars[0]),
                 makeVulkanTypeSimple(False, "size_t", 1, self.countVars[1])]

            countingPrototype = \
                VulkanAPI(self.countingPrefix + name,
                          self.voidType,
                          countingParams + freeParams)

            countingPrototypeNoFilter = \
                VulkanAPI(self.countingPrefix + name,
                          self.voidType,
                          countingParams)

            def structCountingDef(cgen):
                self.countingCodegen.cgen = cgen
                self.countingCodegen.currentStructInfo = structInfo
                cgen.stmt("(void)%s" % self.featureBitsVar);
                cgen.stmt("(void)%s" % self.rootTypeVar);
                cgen.stmt("(void)%s" % self.countVars[0]);
                cgen.stmt("(void)%s" % self.countVars[1]);

                if category == "struct":
                    # marshal 'let' parameters first
                    for letp in letParams:
                        self.countingCodegen.streamLetParameter(self.typeInfo, letp)

                    for member in structInfo.members:
                        iterateVulkanType(self.typeInfo, member, self.countingCodegen)
                if category == "union":
                    iterateVulkanType(self.typeInfo, structInfo.members[0], self.countingCodegen)

            def structCountingDefNoFilter(cgen):
                self.countingCodegen.cgen = cgen
                self.countingCodegen.currentStructInfo = structInfo
                self.countingCodegen.doFiltering = False
                cgen.stmt("(void)%s" % self.featureBitsVar);
                cgen.stmt("(void)%s" % self.rootTypeVar);
                cgen.stmt("(void)%s" % self.countVars[0]);
                cgen.stmt("(void)%s" % self.countVars[1]);

                if category == "struct":
                    # marshal 'let' parameters first
                    for letp in letParams:
                        self.countingCodegen.streamLetParameter(self.typeInfo, letp)

                    for member in structInfo.members:
                        iterateVulkanType(self.typeInfo, member, self.countingCodegen)
                if category == "union":
                    iterateVulkanType(self.typeInfo, structInfo.members[0], self.countingCodegen)

                self.countingCodegen.doFiltering = True

            self.module.appendHeader(
                self.codegen.makeFuncDecl(countingPrototype))
            self.module.appendImpl(
                self.codegen.makeFuncImpl(countingPrototype, structCountingDef))

            if freeParams != []:
                self.module.appendHeader(
                    self.cgenHeader.makeFuncDecl(countingPrototypeNoFilter))
                self.module.appendImpl(
                    self.cgenImpl.makeFuncImpl(
                        countingPrototypeNoFilter, structCountingDefNoFilter))

    def onGenCmd(self, cmdinfo, name, alias):
        VulkanWrapperGenerator.onGenCmd(self, cmdinfo, name, alias)

    def doExtensionStructCountCodegen(self, cgen, extParam, forEach, funcproto):
        accessVar = "structAccess"
        sizeVar = "currExtSize"
        cgen.stmt("VkInstanceCreateInfo* %s = (VkInstanceCreateInfo*)(%s)" % (accessVar, extParam.paramName))
        cgen.stmt("size_t %s = %s(%s, %s, %s)" % (sizeVar, EXTENSION_SIZE_WITH_STREAM_FEATURES_API_NAME,
                                                  self.featureBitsVar, ROOT_TYPE_VAR_NAME, extParam.paramName))

        cgen.beginIf("!%s && %s" % (sizeVar, extParam.paramName))

        cgen.line("// unknown struct extension; skip and call on its pNext field");
        cgen.funcCall(None, funcproto.name, [
                      self.featureBitsVar, ROOT_TYPE_VAR_NAME, "(void*)%s->pNext" % accessVar, self.countVars[1]])
        cgen.stmt("return")

        cgen.endIf()
        cgen.beginElse()

        cgen.line("// known or null extension struct")

        cgen.stmt("*%s += sizeof(uint32_t)" % self.countVars[1])

        cgen.beginIf("!%s" % (sizeVar))
        cgen.line("// exit if this was a null extension struct (size == 0 in this branch)")
        cgen.stmt("return")
        cgen.endIf()

        cgen.endIf()

        cgen.stmt("*%s += sizeof(VkStructureType)" % self.countVars[1])

        def fatalDefault(cgen):
            cgen.line("// fatal; the switch is only taken if the extension struct is known");
            cgen.stmt("abort()")
            pass

        self.emitForEachStructExtension(
            cgen,
            makeVulkanTypeSimple(False, "void", 0, "void"),
            extParam,
            forEach,
            defaultEmit=fatalDefault,
            rootTypeVar=ROOT_TYPE_PARAM)

    def onEnd(self,):
        VulkanWrapperGenerator.onEnd(self)

        def forEachExtensionCounting(ext, castedAccess, cgen):
            cgen.funcCall(None, self.countingPrefix + ext.name,
                          [self.featureBitsVar, self.rootTypeVar, castedAccess, self.countVars[1]])

        self.module.appendImpl(
            self.codegen.makeFuncImpl(
                self.extensionCountingPrototype,
                lambda cgen: self.doExtensionStructCountCodegen(
                    cgen,
                    STRUCT_EXTENSION_PARAM,
                    forEachExtensionCounting,
                    self.extensionCountingPrototype)))
