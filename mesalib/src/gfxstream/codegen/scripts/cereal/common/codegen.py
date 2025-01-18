# Copyright 2023 Google LLC
# SPDX-License-Identifier: MIT
from .vulkantypes import VulkanType, VulkanTypeInfo, VulkanCompoundType, VulkanAPI
from collections import OrderedDict
from copy import copy
from pathlib import Path, PurePosixPath

import os
import sys
import shutil
import subprocess
import re

# Class capturing a single file


class SingleFileModule(object):
    def __init__(self, suffix, directory, basename, customAbsDir=None, suppress=False):
        self.directory = directory
        self.basename = basename
        self.customAbsDir = customAbsDir
        self.suffix = suffix
        self.file = None

        self.preamble = ""
        self.postamble = ""

        self.suppress = suppress

    def begin(self, globalDir):
        if self.suppress:
            return

        # Create subdirectory, if needed
        if self.customAbsDir:
            absDir = self.customAbsDir
        else:
            absDir = os.path.join(globalDir, self.directory)

        filename = os.path.join(absDir, self.basename)

        self.file = open(filename + self.suffix, "w", encoding="utf-8")
        self.file.write(self.preamble)

    def append(self, toAppend):
        if self.suppress:
            return

        self.file.write(toAppend)

    def end(self):
        if self.suppress:
            return

        self.file.write(self.postamble)
        self.file.close()

    def getMakefileSrcEntry(self):
        return ""

    def getCMakeSrcEntry(self):
        return ""

# Class capturing a .cpp file and a .h file (a "C++ module")


class Module(object):

    def __init__(
            self, directory, basename, customAbsDir=None, suppress=False, implOnly=False,
            headerOnly=False, suppressFeatureGuards=False):
        self._headerFileModule = SingleFileModule(
            ".h", directory, basename, customAbsDir, suppress or implOnly)
        self._implFileModule = SingleFileModule(
            ".cpp", directory, basename, customAbsDir, suppress or headerOnly)

        self._headerOnly = headerOnly
        self._implOnly = implOnly

        self.directory = directory
        self.basename = basename
        self._customAbsDir = customAbsDir

        self.suppressFeatureGuards = suppressFeatureGuards

    @property
    def suppress(self):
        raise AttributeError("suppress is write only")

    @suppress.setter
    def suppress(self, value: bool):
        self._headerFileModule.suppress = self._implOnly or value
        self._implFileModule.suppress = self._headerOnly or value

    @property
    def headerPreamble(self) -> str:
        return self._headerFileModule.preamble

    @headerPreamble.setter
    def headerPreamble(self, value: str):
        self._headerFileModule.preamble = value

    @property
    def headerPostamble(self) -> str:
        return self._headerFileModule.postamble

    @headerPostamble.setter
    def headerPostamble(self, value: str):
        self._headerFileModule.postamble = value

    @property
    def implPreamble(self) -> str:
        return self._implFileModule.preamble

    @implPreamble.setter
    def implPreamble(self, value: str):
        self._implFileModule.preamble = value

    @property
    def implPostamble(self) -> str:
        return self._implFileModule.postamble

    @implPostamble.setter
    def implPostamble(self, value: str):
        self._implFileModule.postamble = value

    def getMakefileSrcEntry(self):
        if self._customAbsDir:
            return self.basename + ".cpp \\\n"
        dirName = self.directory
        baseName = self.basename
        joined = os.path.join(dirName, baseName)
        return "    " + joined + ".cpp \\\n"

    def getCMakeSrcEntry(self):
        if self._customAbsDir:
            return "\n" + self.basename + ".cpp "
        dirName = Path(self.directory)
        baseName = Path(self.basename)
        joined = PurePosixPath(dirName / baseName)
        return "\n    " + str(joined) + ".cpp "

    def begin(self, globalDir):
        self._headerFileModule.begin(globalDir)
        self._implFileModule.begin(globalDir)

    def appendHeader(self, toAppend):
        self._headerFileModule.append(toAppend)

    def appendImpl(self, toAppend):
        self._implFileModule.append(toAppend)

    def end(self):
        self._headerFileModule.end()
        self._implFileModule.end()

        # Removes empty ifdef blocks with a regex query over the file
        # which are mainly introduced by extensions with no functions or variables
        def remove_empty_ifdefs(filename: Path):
            """Removes empty #ifdef blocks from a C++ file."""

            # Load file contents
            with open(filename, 'r') as file:
                content = file.read()

            # Regular Expression Pattern
            pattern = r"#ifdef\s+(\w+)\s*(?://.*)?\s*\n\s*#endif\s*(?://.*)?\s*"

            # Replace Empty Blocks
            modified_content = re.sub(pattern, "", content)

            # Save file back
            with open(filename, 'w') as file:
                file.write(modified_content)

        clang_format_command = shutil.which('clang-format')

        def formatFile(filename: Path):
            if "GFXSTREAM_NO_CLANG_FMT" in os.environ:
                return
            assert (clang_format_command is not None)
            assert (subprocess.call([clang_format_command, "-i",
                    "--style=file", str(filename.resolve())]) == 0)

        if not self._headerFileModule.suppress:
            filename = Path(self._headerFileModule.file.name)
            remove_empty_ifdefs(filename)
            formatFile(filename)

        if not self._implFileModule.suppress:
            filename = Path(self._implFileModule.file.name)
            remove_empty_ifdefs(filename)
            formatFile(filename)


class PyScript(SingleFileModule):
    def __init__(self, directory, basename, customAbsDir=None, suppress=False):
        super().__init__(".py", directory, basename, customAbsDir, suppress)


# Class capturing a .proto protobuf definition file
class Proto(SingleFileModule):

    def __init__(self, directory, basename, customAbsDir=None, suppress=False):
        super().__init__(".proto", directory, basename, customAbsDir, suppress)

    def getMakefileSrcEntry(self):
        super().getMakefileSrcEntry()
        if self.customAbsDir:
            return self.basename + ".proto \\\n"
        dirName = self.directory
        baseName = self.basename
        joined = os.path.join(dirName, baseName)
        return "    " + joined + ".proto \\\n"

    def getCMakeSrcEntry(self):
        super().getCMakeSrcEntry()
        if self.customAbsDir:
            return "\n" + self.basename + ".proto "

        dirName = self.directory
        baseName = self.basename
        joined = os.path.join(dirName, baseName)
        return "\n    " + joined + ".proto "

class CodeGen(object):

    def __init__(self,):
        self.code = ""
        self.indentLevel = 0
        self.gensymCounter = [-1]

    def var(self, prefix="cgen_var"):
        self.gensymCounter[-1] += 1
        res = "%s_%s" % (prefix, '_'.join(str(i) for i in self.gensymCounter if i >= 0))
        return res

    def swapCode(self,):
        res = "%s" % self.code
        self.code = ""
        return res

    def indent(self,extra=0):
        return "".join("    " * (self.indentLevel + extra))

    def incrIndent(self,):
        self.indentLevel += 1

    def decrIndent(self,):
        if self.indentLevel > 0:
            self.indentLevel -= 1

    def beginBlock(self, bracketPrint=True):
        if bracketPrint:
            self.code += self.indent() + "{\n"
        self.indentLevel += 1
        self.gensymCounter.append(-1)

    def endBlock(self,bracketPrint=True):
        self.indentLevel -= 1
        if bracketPrint:
            self.code += self.indent() + "}\n"
        del self.gensymCounter[-1]

    def beginIf(self, cond):
        self.code += self.indent() + "if (" + cond + ")\n"
        self.beginBlock()

    def beginElse(self, cond = None):
        if cond is not None:
            self.code += \
                self.indent() + \
                "else if (" + cond + ")\n"
        else:
            self.code += self.indent() + "else\n"
        self.beginBlock()

    def endElse(self):
        self.endBlock()

    def endIf(self):
        self.endBlock()

    def beginSwitch(self, switchvar):
        self.code += self.indent() + "switch (" + switchvar + ")\n"
        self.beginBlock()

    def switchCase(self, switchval, blocked = False):
        self.code += self.indent() + "case %s:" % switchval
        self.beginBlock(bracketPrint = blocked)

    def switchCaseBreak(self, switchval, blocked = False):
        self.code += self.indent() + "case %s:" % switchval
        self.endBlock(bracketPrint = blocked)

    def switchCaseDefault(self, blocked = False):
        self.code += self.indent() + "default:" % switchval
        self.beginBlock(bracketPrint = blocked)

    def endSwitch(self):
        self.endBlock()

    def beginWhile(self, cond):
        self.code += self.indent() + "while (" + cond + ")\n"
        self.beginBlock()

    def endWhile(self):
        self.endBlock()

    def beginFor(self, initial, condition, increment):
        self.code += \
            self.indent() + "for (" + \
            "; ".join([initial, condition, increment]) + \
            ")\n"
        self.beginBlock()

    def endFor(self):
        self.endBlock()

    def beginLoop(self, loopVarType, loopVar, loopInit, loopBound):
        self.beginFor(
            "%s %s = %s" % (loopVarType, loopVar, loopInit),
            "%s < %s" % (loopVar, loopBound),
            "++%s" % (loopVar))

    def endLoop(self):
        self.endBlock()

    def stmt(self, code):
        self.code += self.indent() + code + ";\n"

    def line(self, code):
        self.code += self.indent() + code + "\n"

    def leftline(self, code):
        self.code += code + "\n"

    def makeCallExpr(self, funcName, parameters):
        return funcName + "(%s)" % (", ".join(parameters))

    def funcCall(self, lhs, funcName, parameters):
        res = self.indent()

        if lhs is not None:
            res += lhs + " = "

        res += self.makeCallExpr(funcName, parameters) + ";\n"
        self.code += res

    def funcCallRet(self, _lhs, funcName, parameters):
        res = self.indent()
        res += "return " + self.makeCallExpr(funcName, parameters) + ";\n"
        self.code += res

    # Given a VulkanType object, generate a C type declaration
    # with optional parameter name:
    # [const] [typename][*][const*] [paramName]
    def makeCTypeDecl(self, vulkanType, useParamName=True):
        constness = "const " if vulkanType.isConst else ""
        typeName = vulkanType.typeName

        if vulkanType.pointerIndirectionLevels == 0:
            ptrSpec = ""
        elif vulkanType.isPointerToConstPointer:
            ptrSpec = "* const*" if vulkanType.isConst else "**"
            if vulkanType.pointerIndirectionLevels > 2:
                ptrSpec += "*" * (vulkanType.pointerIndirectionLevels - 2)
        else:
            ptrSpec = "*" * vulkanType.pointerIndirectionLevels

        if useParamName and (vulkanType.paramName is not None):
            paramStr = (" " + vulkanType.paramName)
        else:
            paramStr = ""

        return "%s%s%s%s" % (constness, typeName, ptrSpec, paramStr)

    def makeRichCTypeDecl(self, vulkanType, useParamName=True):
        constness = "const " if vulkanType.isConst else ""
        typeName = vulkanType.typeName

        if vulkanType.pointerIndirectionLevels == 0:
            ptrSpec = ""
        elif vulkanType.isPointerToConstPointer:
            ptrSpec = "* const*" if vulkanType.isConst else "**"
            if vulkanType.pointerIndirectionLevels > 2:
                ptrSpec += "*" * (vulkanType.pointerIndirectionLevels - 2)
        else:
            ptrSpec = "*" * vulkanType.pointerIndirectionLevels

        if useParamName and (vulkanType.paramName is not None):
            paramStr = (" " + vulkanType.paramName)
        else:
            paramStr = ""

        if vulkanType.staticArrExpr:
            staticArrInfo = "[%s]" % vulkanType.staticArrExpr
        else:
            staticArrInfo = ""

        return "%s%s%s%s%s" % (constness, typeName, ptrSpec, paramStr, staticArrInfo)

    # Given a VulkanAPI object, generate the C function protype:
    # <returntype> <funcname>(<parameters>)
    def makeFuncProto(self, vulkanApi, useParamName=True):

        protoBegin = "%s %s" % (self.makeCTypeDecl(
            vulkanApi.retType, useParamName=False), vulkanApi.name)

        def getFuncArgDecl(param):
            if param.staticArrExpr:
                return self.makeCTypeDecl(param, useParamName=useParamName) + ("[%s]" % param.staticArrExpr)
            else:
                return self.makeCTypeDecl(param, useParamName=useParamName)

        protoParams = "(\n    %s)" % ((",\n%s" % self.indent(1)).join(
            list(map(
                getFuncArgDecl,
                vulkanApi.parameters))))

        return protoBegin + protoParams

    def makeFuncAlias(self, nameDst, nameSrc):
        return "DEFINE_ALIAS_FUNCTION({}, {})\n\n".format(nameSrc, nameDst)

    def makeFuncDecl(self, vulkanApi):
        return self.makeFuncProto(vulkanApi) + ";\n\n"

    def makeFuncImpl(self, vulkanApi, codegenFunc):
        self.swapCode()

        self.line(self.makeFuncProto(vulkanApi))
        self.beginBlock()
        codegenFunc(self)
        self.endBlock()

        return self.swapCode() + "\n"

    def emitFuncImpl(self, vulkanApi, codegenFunc):
        self.line(self.makeFuncProto(vulkanApi))
        self.beginBlock()
        codegenFunc(self)
        self.endBlock()

    def makeStructAccess(self,
                         vulkanType,
                         structVarName,
                         asPtr=True,
                         structAsPtr=True,
                         accessIndex=None):

        deref = "->" if structAsPtr else "."

        indexExpr = (" + %s" % accessIndex) if accessIndex else ""

        addrOfExpr = "" if vulkanType.accessibleAsPointer() or (
            not asPtr) else "&"

        return "%s%s%s%s%s" % (addrOfExpr, structVarName, deref,
                               vulkanType.paramName, indexExpr)

    def makeRawLengthAccess(self, vulkanType):
        lenExpr = vulkanType.getLengthExpression()

        if not lenExpr:
            return None, None

        if lenExpr == "null-terminated":
            return "strlen(%s)" % vulkanType.paramName, None

        return lenExpr, None

    def makeLengthAccessFromStruct(self,
                                   structInfo,
                                   vulkanType,
                                   structVarName,
                                   asPtr=True):
        # Handle special cases first
        # Mostly when latexmath is involved
        def handleSpecialCases(structInfo, vulkanType, structVarName, asPtr):
            cases = [
                {
                    "structName": "VkShaderModuleCreateInfo",
                    "field": "pCode",
                    "lenExprMember": "codeSize",
                    "postprocess": lambda expr: "(%s / 4)" % expr
                },
                {
                    "structName": "VkPipelineMultisampleStateCreateInfo",
                    "field": "pSampleMask",
                    "lenExprMember": "rasterizationSamples",
                    "postprocess": lambda expr: "(((%s) + 31) / 32)" % expr
                },
                {
                    "structName": "VkAccelerationStructureVersionInfoKHR",
                    "field": "pVersionData",
                    "lenExprMember": "",
                    "postprocess": lambda _: "2*VK_UUID_SIZE"
                },
            ]

            for c in cases:
                if (structInfo.name, vulkanType.paramName) == (c["structName"],
                                                               c["field"]):
                    deref = "->" if asPtr else "."
                    expr = "%s%s%s" % (structVarName, deref,
                                       c["lenExprMember"])
                    lenAccessGuardExpr = "%s" % structVarName
                    return c["postprocess"](expr), lenAccessGuardExpr

            return None, None

        specialCaseAccess = \
            handleSpecialCases(
                structInfo, vulkanType, structVarName, asPtr)

        if specialCaseAccess != (None, None):
            return specialCaseAccess

        lenExpr = vulkanType.getLengthExpression()

        if not lenExpr:
            return None, None

        deref = "->" if asPtr else "."
        lenAccessGuardExpr = "%s" % (

            structVarName) if deref else None
        if lenExpr == "null-terminated":
            return "strlen(%s%s%s)" % (structVarName, deref,
                                       vulkanType.paramName), lenAccessGuardExpr

        if not structInfo.getMember(lenExpr):
            return self.makeRawLengthAccess(vulkanType)

        return "%s%s%s" % (structVarName, deref, lenExpr), lenAccessGuardExpr

    def makeLengthAccessFromApi(self, api, vulkanType):
        # Handle special cases first
        # Mostly when :: is involved
        def handleSpecialCases(vulkanType):
            lenExpr = vulkanType.getLengthExpression()

            if lenExpr is None:
                return None, None

            if "::" in lenExpr:
                structVarName, memberVarName = lenExpr.split("::")
                lenAccessGuardExpr = "%s" % (structVarName)
                return "%s->%s" % (structVarName, memberVarName), lenAccessGuardExpr
            return None, None

        specialCaseAccess = handleSpecialCases(vulkanType)

        if specialCaseAccess != (None, None):
            return specialCaseAccess

        lenExpr = vulkanType.getLengthExpression()

        if not lenExpr:
            return None, None

        lenExprInfo = api.getParameter(lenExpr)

        if not lenExprInfo:
            return self.makeRawLengthAccess(vulkanType)

        if lenExpr == "null-terminated":
            return "strlen(%s)" % vulkanType.paramName(), None
        else:
            deref = "*" if lenExprInfo.pointerIndirectionLevels > 0 else ""
            lenAccessGuardExpr = "%s" % lenExpr if deref else None
            return "(%s(%s))" % (deref, lenExpr), lenAccessGuardExpr

    def accessParameter(self, param, asPtr=True):
        if asPtr:
            if param.pointerIndirectionLevels > 0:
                return param.paramName
            else:
                return "&%s" % param.paramName
        else:
            return param.paramName

    def sizeofExpr(self, vulkanType):
        return "sizeof(%s)" % (
            self.makeCTypeDecl(vulkanType, useParamName=False))

    def generalAccess(self,
                      vulkanType,
                      parentVarName=None,
                      asPtr=True,
                      structAsPtr=True):
        if vulkanType.parent is None:
            if parentVarName is None:
                return self.accessParameter(vulkanType, asPtr=asPtr)
            else:
                return self.accessParameter(vulkanType.withModifiedName(parentVarName), asPtr=asPtr)

        if isinstance(vulkanType.parent, VulkanCompoundType):
            return self.makeStructAccess(
                vulkanType, parentVarName, asPtr=asPtr, structAsPtr=structAsPtr)

        if isinstance(vulkanType.parent, VulkanAPI):
            if parentVarName is None:
                return self.accessParameter(vulkanType, asPtr=asPtr)
            else:
                return self.accessParameter(vulkanType.withModifiedName(parentVarName), asPtr=asPtr)

        os.abort("Could not find a way to access Vulkan type %s" %
                 vulkanType.name)

    def makeLengthAccess(self, vulkanType, parentVarName="parent"):
        if vulkanType.parent is None:
            return self.makeRawLengthAccess(vulkanType)

        if isinstance(vulkanType.parent, VulkanCompoundType):
            return self.makeLengthAccessFromStruct(
                vulkanType.parent, vulkanType, parentVarName, asPtr=True)

        if isinstance(vulkanType.parent, VulkanAPI):
            return self.makeLengthAccessFromApi(vulkanType.parent, vulkanType)

        os.abort("Could not find a way to access length of Vulkan type %s" %
                 vulkanType.name)

    def generalLengthAccess(self, vulkanType, parentVarName="parent"):
        return self.makeLengthAccess(vulkanType, parentVarName)[0]

    def generalLengthAccessGuard(self, vulkanType, parentVarName="parent"):
        return self.makeLengthAccess(vulkanType, parentVarName)[1]

    def vkApiCall(self, api, customPrefix="", globalStatePrefix="", customParameters=None, checkForDeviceLost=False, checkForOutOfMemory=False):
        callLhs = None

        retTypeName = api.getRetTypeExpr()
        retVar = None

        if retTypeName != "void":
            retVar = api.getRetVarExpr()
            self.stmt("%s %s = (%s)0" % (retTypeName, retVar, retTypeName))
            callLhs = retVar

        if customParameters is None:
            self.funcCall(
            callLhs, customPrefix + api.name, [p.paramName for p in api.parameters])
        else:
            self.funcCall(
                callLhs, customPrefix + api.name, customParameters)

        if retTypeName == "VkResult" and checkForDeviceLost:
            self.stmt("if ((%s) == VK_ERROR_DEVICE_LOST) %sDeviceLost()" % (callLhs, globalStatePrefix))

        if retTypeName == "VkResult" and checkForOutOfMemory:
            if api.name == "vkAllocateMemory":
                self.stmt(
                    "%sCheckOutOfMemory(%s, opcode, context, std::make_optional<uint64_t>(pAllocateInfo->allocationSize))"
                    % (globalStatePrefix, callLhs))
            else:
                self.stmt(
                    "%sCheckOutOfMemory(%s, opcode, context)"
                    % (globalStatePrefix, callLhs))

        return (retTypeName, retVar)

    def makeCheckVkSuccess(self, expr):
        return "((%s) == VK_SUCCESS)" % expr

    def makeReinterpretCast(self, varName, typeName, const=True):
        return "reinterpret_cast<%s%s*>(%s)" % \
               ("const " if const else "", typeName, varName)

    def validPrimitive(self, typeInfo, typeName):
        size = typeInfo.getPrimitiveEncodingSize(typeName)
        return size != None

    def makePrimitiveStreamMethod(self, typeInfo, typeName, direction="write"):
        if not self.validPrimitive(typeInfo, typeName):
            return None

        size = typeInfo.getPrimitiveEncodingSize(typeName)
        prefix = "put" if direction == "write" else "get"
        suffix = None
        if size == 1:
            suffix = "Byte"
        elif size == 2:
            suffix = "Be16"
        elif size == 4:
            suffix = "Be32"
        elif size == 8:
            suffix = "Be64"

        if suffix:
            return prefix + suffix

        return None

    def makePrimitiveStreamMethodInPlace(self, typeInfo, typeName, direction="write"):
        if not self.validPrimitive(typeInfo, typeName):
            return None

        size = typeInfo.getPrimitiveEncodingSize(typeName)
        prefix = "to" if direction == "write" else "from"
        suffix = None
        if size == 1:
            suffix = "Byte"
        elif size == 2:
            suffix = "Be16"
        elif size == 4:
            suffix = "Be32"
        elif size == 8:
            suffix = "Be64"

        if suffix:
            return prefix + suffix

        return None

    def streamPrimitive(self, typeInfo, streamVar, accessExpr, accessType, direction="write"):
        accessTypeName = accessType.typeName

        if accessType.pointerIndirectionLevels == 0 and not self.validPrimitive(typeInfo, accessTypeName):
            print("Tried to stream a non-primitive type: %s" % accessTypeName)
            os.abort()

        needPtrCast = False

        if accessType.pointerIndirectionLevels > 0:
            streamSize = 8
            streamStorageVarType = "uint64_t"
            needPtrCast = True
            streamMethod = "putBe64" if direction == "write" else "getBe64"
        else:
            streamSize = typeInfo.getPrimitiveEncodingSize(accessTypeName)
            if streamSize == 1:
                streamStorageVarType = "uint8_t"
            elif streamSize == 2:
                streamStorageVarType = "uint16_t"
            elif streamSize == 4:
                streamStorageVarType = "uint32_t"
            elif streamSize == 8:
                streamStorageVarType = "uint64_t"
            streamMethod = self.makePrimitiveStreamMethod(
                typeInfo, accessTypeName, direction=direction)

        streamStorageVar = self.var()

        accessCast = self.makeRichCTypeDecl(accessType, useParamName=False)

        ptrCast = "(uintptr_t)" if needPtrCast else ""

        if direction == "read":
            self.stmt("%s = (%s)%s%s->%s()" %
                      (accessExpr,
                       accessCast,
                       ptrCast,
                       streamVar,
                       streamMethod))
        else:
            self.stmt("%s %s = (%s)%s%s" %
                      (streamStorageVarType, streamStorageVar,
                       streamStorageVarType, ptrCast, accessExpr))
            self.stmt("%s->%s(%s)" %
                      (streamVar, streamMethod, streamStorageVar))

    def memcpyPrimitive(self, typeInfo, streamVar, accessExpr, accessType, variant, direction="write"):
        accessTypeName = accessType.typeName

        if accessType.pointerIndirectionLevels == 0 and not self.validPrimitive(typeInfo, accessTypeName):
            print("Tried to stream a non-primitive type: %s" % accessTypeName)
            os.abort()

        needPtrCast = False

        streamSize = 8

        if accessType.pointerIndirectionLevels > 0:
            streamSize = 8
            streamStorageVarType = "uint64_t"
            needPtrCast = True
            streamMethod = "toBe64" if direction == "write" else "fromBe64"
        else:
            streamSize = typeInfo.getPrimitiveEncodingSize(accessTypeName)
            if streamSize == 1:
                streamStorageVarType = "uint8_t"
            elif streamSize == 2:
                streamStorageVarType = "uint16_t"
            elif streamSize == 4:
                streamStorageVarType = "uint32_t"
            elif streamSize == 8:
                streamStorageVarType = "uint64_t"
            streamMethod = self.makePrimitiveStreamMethodInPlace(
                typeInfo, accessTypeName, direction=direction)

        streamStorageVar = self.var()

        accessCast = self.makeRichCTypeDecl(accessType, useParamName=False)

        if direction == "read":
            accessCast = self.makeRichCTypeDecl(
                accessType.getForNonConstAccess(), useParamName=False)

        ptrCast = "(uintptr_t)" if needPtrCast else ""
        if variant == "guest":
            streamNamespace = "gfxstream::aemu"
        else:
            streamNamespace = "android::base"

        if direction == "read":
            self.stmt("memcpy((%s*)&%s, %s, %s)" %
                      (accessCast,
                       accessExpr,
                       streamVar,
                       str(streamSize)))
            self.stmt("%s::Stream::%s((uint8_t*)&%s)" % (
                streamNamespace,
                streamMethod,
                accessExpr))
        else:
            self.stmt("%s %s = (%s)%s%s" %
                      (streamStorageVarType, streamStorageVar,
                       streamStorageVarType, ptrCast, accessExpr))
            self.stmt("memcpy(%s, &%s, %s)" %
                      (streamVar, streamStorageVar, str(streamSize)))
            self.stmt("%s::Stream::%s((uint8_t*)%s)" % (
                streamNamespace,
                streamMethod,
                streamVar))

    def countPrimitive(self, typeInfo, accessType):
        accessTypeName = accessType.typeName

        if accessType.pointerIndirectionLevels == 0 and not self.validPrimitive(typeInfo, accessTypeName):
            print("Tried to count a non-primitive type: %s" % accessTypeName)
            os.abort()

        needPtrCast = False

        if accessType.pointerIndirectionLevels > 0:
            streamSize = 8
        else:
            streamSize = typeInfo.getPrimitiveEncodingSize(accessTypeName)

        return streamSize

# Class to wrap a Vulkan API call.
#
# The user gives a generic callback, |codegenDef|,
# that takes a CodeGen object and a VulkanAPI object as arguments.
# codegenDef uses CodeGen along with the VulkanAPI object
# to generate the function body.
class VulkanAPIWrapper(object):

    def __init__(self,
                 customApiPrefix,
                 extraParameters=None,
                 returnTypeOverride=None,
                 codegenDef=None):
        self.customApiPrefix = customApiPrefix
        self.extraParameters = extraParameters
        self.returnTypeOverride = returnTypeOverride

        self.codegen = CodeGen()

        self.definitionFunc = codegenDef

        # Private function

        def makeApiFunc(self, typeInfo, apiName):
            customApi = copy(typeInfo.apis[apiName])
            customApi.name = self.customApiPrefix + customApi.name
            if self.extraParameters is not None:
                if isinstance(self.extraParameters, list):
                    customApi.parameters = \
                        self.extraParameters + customApi.parameters
                else:
                    os.abort(
                        "Type of extra parameters to custom API not valid. Expected list, got %s" % type(
                            self.extraParameters))

            if self.returnTypeOverride is not None:
                customApi.retType = self.returnTypeOverride
            return customApi

        self.makeApi = makeApiFunc

    def setCodegenDef(self, codegenDefFunc):
        self.definitionFunc = codegenDefFunc

    def makeDecl(self, typeInfo, apiName):
        return self.codegen.makeFuncProto(
            self.makeApi(self, typeInfo, apiName)) + ";\n\n"

    def makeDefinition(self, typeInfo, apiName, isStatic=False):
        vulkanApi = self.makeApi(self, typeInfo, apiName)

        self.codegen.swapCode()
        self.codegen.beginBlock()

        if self.definitionFunc is None:
            print("ERROR: No definition found for (%s, %s)" %
                  (vulkanApi.name, self.customApiPrefix))
            sys.exit(1)

        self.definitionFunc(self.codegen, vulkanApi)

        self.codegen.endBlock()

        return ("static " if isStatic else "") + self.codegen.makeFuncProto(
            vulkanApi) + "\n" + self.codegen.swapCode() + "\n"

# Base class for wrapping all Vulkan API objects.  These work with Vulkan
# Registry generators and have gen* triggers.  They tend to contain
# VulkanAPIWrapper objects to make it easier to generate the code.
class VulkanWrapperGenerator(object):

    def __init__(self, module: Module, typeInfo: VulkanTypeInfo):
        self.module: Module = module
        self.typeInfo: VulkanTypeInfo = typeInfo
        self.extensionStructTypes = OrderedDict()

    def onBegin(self):
        pass

    def onEnd(self):
        pass

    def onBeginFeature(self, featureName, featureType):
        pass

    def onFeatureNewCmd(self, cmdName):
        pass

    def onEndFeature(self):
        pass

    def onGenType(self, typeInfo, name, alias):
        category = self.typeInfo.categoryOf(name)
        if category in ["struct", "union"] and not alias:
            structInfo = self.typeInfo.structs[name]
            if structInfo.structExtendsExpr:
                self.extensionStructTypes[name] = structInfo
        pass

    def onGenStruct(self, typeInfo, name, alias):
        pass

    def onGenGroup(self, groupinfo, groupName, alias=None):
        pass

    def onGenEnum(self, enuminfo, name, alias):
        pass

    def onGenCmd(self, cmdinfo, name, alias):
        pass

    # Below Vulkan structure types may correspond to multiple Vulkan structs
    # due to a conflict between different Vulkan registries. In order to get
    # the correct Vulkan struct type, we need to check the type of its "root"
    # struct as well.
    ROOT_TYPE_MAPPING = {
        "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT": {
            "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2": "VkPhysicalDeviceFragmentDensityMapFeaturesEXT",
            "VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO": "VkPhysicalDeviceFragmentDensityMapFeaturesEXT",
            "VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO": "VkImportColorBufferGOOGLE",
            "default": "VkPhysicalDeviceFragmentDensityMapFeaturesEXT",
        },
        "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT": {
            "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2": "VkPhysicalDeviceFragmentDensityMapPropertiesEXT",
            "VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO": "VkCreateBlobGOOGLE",
            "default": "VkPhysicalDeviceFragmentDensityMapPropertiesEXT",
        },
        "VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT": {
            "VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO": "VkRenderPassFragmentDensityMapCreateInfoEXT",
            "VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2": "VkRenderPassFragmentDensityMapCreateInfoEXT",
            "VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO": "VkImportBufferGOOGLE",
            "default": "VkRenderPassFragmentDensityMapCreateInfoEXT",
        },
    }

    def emitForEachStructExtension(self, cgen, retType, triggerVar, forEachFunc, autoBreak=True, defaultEmit=None, nullEmit=None, rootTypeVar=None):
        def readStructType(structTypeName, structVarName, cgen):
            cgen.stmt("uint32_t %s = (uint32_t)%s(%s)" % \
                (structTypeName, "goldfish_vk_struct_type", structVarName))

        def castAsStruct(varName, typeName, const=True):
            return "reinterpret_cast<%s%s*>(%s)" % \
                   ("const " if const else "", typeName, varName)

        def doDefaultReturn(cgen):
            if retType.typeName == "void":
                cgen.stmt("return")
            else:
                cgen.stmt("return (%s)0" % retType.typeName)

        cgen.beginIf("!%s" % triggerVar.paramName)
        if nullEmit is None:
            doDefaultReturn(cgen)
        else:
            nullEmit(cgen)
        cgen.endIf()

        readStructType("structType", triggerVar.paramName, cgen)

        cgen.line("switch(structType)")
        cgen.beginBlock()

        currFeature = None

        for ext in self.extensionStructTypes.values():
            if not currFeature:
                cgen.leftline("#ifdef %s" % ext.feature)
                currFeature = ext.feature

            if currFeature and ext.feature != currFeature:
                cgen.leftline("#endif")
                cgen.leftline("#ifdef %s" % ext.feature)
                currFeature = ext.feature

            enum = ext.structEnumExpr
            protect = None
            if enum in self.typeInfo.enumElem:
                protect = self.typeInfo.enumElem[enum].get("protect", default=None)
                if protect is not None:
                    cgen.leftline("#ifdef %s" % protect)

            cgen.line("case %s:" % enum)
            cgen.beginBlock()

            if rootTypeVar is not None and enum in VulkanWrapperGenerator.ROOT_TYPE_MAPPING:
                cgen.line("switch(%s)" % rootTypeVar.paramName)
                cgen.beginBlock()
                kv = VulkanWrapperGenerator.ROOT_TYPE_MAPPING[enum]
                for k in kv:
                    v = self.extensionStructTypes[kv[k]]
                    if k == "default":
                        cgen.line("%s:" % k)
                    else:
                        cgen.line("case %s:" % k)
                    cgen.beginBlock()
                    castedAccess = castAsStruct(
                        triggerVar.paramName, v.name, const=triggerVar.isConst)
                    forEachFunc(v, castedAccess, cgen)
                    cgen.line("break;")
                    cgen.endBlock()
                cgen.endBlock()
            else:
                castedAccess = castAsStruct(
                    triggerVar.paramName, ext.name, const=triggerVar.isConst)
                forEachFunc(ext, castedAccess, cgen)

            if autoBreak:
                cgen.stmt("break")
            cgen.endBlock()

            if protect is not None:
                cgen.leftline("#endif // %s" % protect)

        if currFeature:
            cgen.leftline("#endif")

        cgen.line("default:")
        cgen.beginBlock()
        if defaultEmit is None:
            doDefaultReturn(cgen)
        else:
            defaultEmit(cgen)
        cgen.endBlock()

        cgen.endBlock()

    def emitForEachStructExtensionGeneral(self, cgen, forEachFunc, doFeatureIfdefs=False):
        currFeature = None

        for (i, ext) in enumerate(self.extensionStructTypes.values()):
            if doFeatureIfdefs:
                if not currFeature:
                    cgen.leftline("#ifdef %s" % ext.feature)
                    currFeature = ext.feature

                if currFeature and ext.feature != currFeature:
                    cgen.leftline("#endif")
                    cgen.leftline("#ifdef %s" % ext.feature)
                    currFeature = ext.feature

            forEachFunc(i, ext, cgen)

        if doFeatureIfdefs:
            if currFeature:
                cgen.leftline("#endif")
