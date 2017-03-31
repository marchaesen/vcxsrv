# Mesa 3-D graphics library
#
# Copyright (C) 2010-2011 Chia-I Wu <olvaffe@gmail.com>
# Copyright (C) 2010-2011 LunarG Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

# included by glsl Android.mk for source generation

ifeq ($(LOCAL_MODULE_CLASS),)
LOCAL_MODULE_CLASS := STATIC_LIBRARIES
endif

intermediates := $(call local-generated-sources-dir)

LOCAL_SRC_FILES := $(LOCAL_SRC_FILES)

LOCAL_C_INCLUDES += \
	$(intermediates)/glsl \
	$(intermediates)/glsl/glcpp \
	$(LOCAL_PATH)/glsl \
	$(LOCAL_PATH)/glsl/glcpp

LOCAL_GENERATED_SOURCES += $(addprefix $(intermediates)/, \
	$(LIBGLCPP_GENERATED_FILES) \
	$(LIBGLSL_GENERATED_FILES))

LOCAL_EXPORT_C_INCLUDE_DIRS += \
	$(intermediates)/glsl

# Modules using libmesa_nir must set LOCAL_GENERATED_SOURCES to this
MESA_GEN_GLSL_H := $(addprefix $(call local-generated-sources-dir)/, \
	glsl/ir_expression_operation.h \
	glsl/ir_expression_operation_constant.h \
	glsl/ir_expression_operation_strings.h)

define local-l-or-ll-to-c-or-cpp
	@mkdir -p $(dir $@)
	@echo "Mesa Lex: $(PRIVATE_MODULE) <= $<"
	$(hide) $(LEX) --nounistd -o$@ $<
endef

define glsl_local-y-to-c-and-h
	@mkdir -p $(dir $@)
	@echo "Mesa Yacc: $(PRIVATE_MODULE) <= $<"
	$(hide) $(YACC) -o $@ -p "glcpp_parser_" $<
endef

YACC_HEADER_SUFFIX := .hpp

define local-yy-to-cpp-and-h
	@mkdir -p $(dir $@)
	@echo "Mesa Yacc: $(PRIVATE_MODULE) <= $<"
	$(hide) $(YACC) -p "_mesa_glsl_" -o $@ $<
	touch $(@:$1=$(YACC_HEADER_SUFFIX))
	echo '#ifndef '$(@F:$1=_h) > $(@:$1=.h)
	echo '#define '$(@F:$1=_h) >> $(@:$1=.h)
	cat $(@:$1=$(YACC_HEADER_SUFFIX)) >> $(@:$1=.h)
	echo '#endif' >> $(@:$1=.h)
	rm -f $(@:$1=$(YACC_HEADER_SUFFIX))
endef

$(intermediates)/glsl/glsl_lexer.cpp: $(LOCAL_PATH)/glsl/glsl_lexer.ll
	$(call local-l-or-ll-to-c-or-cpp)

$(intermediates)/glsl/glsl_parser.cpp: $(LOCAL_PATH)/glsl/glsl_parser.yy
	$(call local-yy-to-cpp-and-h,.cpp)

$(intermediates)/glsl/glsl_parser.h: $(intermediates)/glsl/glsl_parser.cpp

$(intermediates)/glsl/glcpp/glcpp-lex.c: $(LOCAL_PATH)/glsl/glcpp/glcpp-lex.l
	$(call local-l-or-ll-to-c-or-cpp)

$(intermediates)/glsl/glcpp/glcpp-parse.c: $(LOCAL_PATH)/glsl/glcpp/glcpp-parse.y
	$(call glsl_local-y-to-c-and-h)

$(LOCAL_PATH)/glsl/ir.h: $(intermediates)/glsl/ir_expression_operation.h

$(intermediates)/glsl/ir_expression_operation.h: $(LOCAL_PATH)/glsl/ir_expression_operation.py
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON2) $< enum > $@

$(intermediates)/glsl/ir_expression_operation_constant.h: $(LOCAL_PATH)/glsl/ir_expression_operation.py
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON2) $< constant > $@

$(intermediates)/glsl/ir_expression_operation_strings.h: $(LOCAL_PATH)/glsl/ir_expression_operation.py
	@mkdir -p $(dir $@)
	$(hide) $(MESA_PYTHON2) $< strings > $@
