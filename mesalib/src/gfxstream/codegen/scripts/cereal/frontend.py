# Copyright 2018 Google LLC
# SPDX-License-Identifier: MIT

from .common.codegen import CodeGen, VulkanAPIWrapper
from .common.vulkantypes import \
        VulkanAPI, makeVulkanTypeSimple, iterateVulkanType

from .wrapperdefs import VulkanWrapperGenerator

from .wrapperdefs import API_PREFIX_VALIDATE
from .wrapperdefs import PARAMETERS_VALIDATE
from .wrapperdefs import VOID_TYPE
from .wrapperdefs import VALIDATE_RESULT_TYPE
from .wrapperdefs import VALIDATE_VAR_NAME
from .wrapperdefs import VALIDATE_GOOD_RESULT

from .wrapperdefs import VULKAN_STREAM_TYPE
from .wrapperdefs import VULKAN_STREAM_VAR_NAME

from .wrapperdefs import API_PREFIX_MARSHAL
from .wrapperdefs import API_PREFIX_FRONTEND

# Frontend
class VulkanFrontend(VulkanWrapperGenerator):

    def __init__(self, module, typeInfo):
        VulkanWrapperGenerator.__init__(self, module, typeInfo)

        def validateDefFunc(_codegen, _api):
            # TODO
            pass

        self.validateWrapper = \
            VulkanAPIWrapper(
                API_PREFIX_VALIDATE,
                PARAMETERS_VALIDATE,
                VOID_TYPE,
                validateDefFunc)

        def frontendDefFunc(codegen, api):
            retTypeName = api.retType.typeName

            codegen.stmt(
                "%s %s = %s" % (VALIDATE_RESULT_TYPE, VALIDATE_VAR_NAME,
                                VALIDATE_GOOD_RESULT))
            codegen.funcCall(None, API_PREFIX_VALIDATE + api.origName,
                             ["&%s" % VALIDATE_VAR_NAME] + list(
                                 map(lambda p: p.paramName, api.parameters)))

            codegen.beginIf(
                "%s != %s" % (VALIDATE_VAR_NAME, VALIDATE_GOOD_RESULT))
            if retTypeName == VALIDATE_RESULT_TYPE:
                codegen.stmt("return %s" % VALIDATE_VAR_NAME)
            elif retTypeName != "void":
                codegen.stmt("return (%s)0" % retTypeName)
            else:
                codegen.stmt("return")
            codegen.endIf()

            codegen.stmt("// VULKAN_STREAM_GET()")
            codegen.stmt("%s* %s = nullptr" % (VULKAN_STREAM_TYPE,
                                               VULKAN_STREAM_VAR_NAME))

            retLhs = None
            if retTypeName != "void":
                retLhs = retTypeName + " res"

            codegen.funcCall(retLhs, API_PREFIX_MARSHAL + api.origName,
                             [VULKAN_STREAM_VAR_NAME] + list(
                                 map(lambda p: p.paramName, api.parameters)))

            if retTypeName != "void":
                codegen.stmt("return res")

        self.frontendWrapper = \
            VulkanAPIWrapper(
                API_PREFIX_FRONTEND,
                [],
                None,
                frontendDefFunc)

    def onGenCmd(self, cmdinfo, name, alias):
        VulkanWrapperGenerator.onGenCmd(self, cmdinfo, name, alias)
        self.module.appendHeader(
            self.frontendWrapper.makeDecl(self.typeInfo, name))
        self.module.appendImpl(
            self.validateWrapper.makeDefinition(
                self.typeInfo, name, isStatic=True))
        self.module.appendImpl(
            self.frontendWrapper.makeDefinition(self.typeInfo, name))
