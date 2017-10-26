include Makefile.sources

top_srcdir=../..
srcdir=.
builddir=.

INCLUDES += \
	$(top_srcdir)/include \
	$(top_srcdir)/src \
	$(top_srcdir)/src/mapi \
	$(top_srcdir)/src/mesa/ \
	$(top_builddir)/src/compiler/glsl\
	$(top_srcdir)/src/compiler/glsl\
	$(top_srcdir)/src/compiler/glsl/glcpp\
	$(top_builddir)/src/compiler/nir \
	$(top_srcdir)/src/compiler/nir \
	$(top_srcdir)/src/compiler/spirv \
	$(top_srcdir)/src/gallium/include \
	$(top_srcdir)/src/gallium/auxiliary \
	$(top_srcdir)/src/gtest/include \

libcompiler_la_SOURCES = $(LIBCOMPILER_FILES)

glsl_libglcpp_la_SOURCES =				\
	glsl/glcpp/glcpp-lex.c				\
	glsl/glcpp/glcpp-parse.c			\
	glsl/glcpp/glcpp-parse.h			\
	$(LIBGLCPP_FILES)

glsl_glcpp_glcpp_SOURCES =				\
	glsl/glcpp/glcpp.c

glsl_libglsl_la_SOURCES =				\
	$(LIBGLSL_GENERATED_FILES)			\
	$(LIBGLSL_FILES)				\
	$(LIBGLSL_SHADER_CACHE_FILES)

glsl_libstandalone_la_SOURCES = \
	$(GLSL_COMPILER_CXX_FILES)

glsl_compiler_SOURCES = \
	glsl/main.cpp

BISON_PKGDATADIR=../../../tools/mhmake/src/bisondata
export BISON_PKGDATADIR

YACC_GEN = bison.exe -v
LEX_GEN = flex.exe --nounistd
PYTHON_GEN = python

glsl/glsl_parser.cpp: glsl/glsl_parser.yy
	$(YACC_GEN) -o $@ -p "_mesa_glsl_" --defines=$(builddir)/glsl/glsl_parser.h $(srcdir)/glsl/glsl_parser.yy
glsl/glsl_parser.h: glsl/glsl_parser.yy
	$(YACC_GEN) -o $@ -p "_mesa_glsl_" --defines=$(builddir)/glsl/glsl_parser.h $(srcdir)/glsl/glsl_parser.yy

glsl/glsl_lexer.cpp: glsl/glsl_lexer.ll
	$(LEX_GEN) -o $@ $(srcdir)/glsl/glsl_lexer.ll

glsl/glcpp/glcpp-parse.c: glsl/glcpp/glcpp-parse.y
	$(YACC_GEN) -o $@ -p "glcpp_parser_" --defines=$(builddir)/glsl/glcpp/glcpp-parse.h $(srcdir)/glsl/glcpp/glcpp-parse.y
glsl/glcpp/glcpp-parse.h: glsl/glcpp/glcpp-parse.y
	$(YACC_GEN) -o $@ -p "glcpp_parser_" --defines=$(builddir)/glsl/glcpp/glcpp-parse.h $(srcdir)/glsl/glcpp/glcpp-parse.y

glsl/glcpp/glcpp-lex.c: glsl/glcpp/glcpp-lex.l
	$(LEX_GEN) -o $@ $(srcdir)/glsl/glcpp/glcpp-lex.l

glsl/ir_expression_operation.h: glsl/ir_expression_operation.py
	$(PYTHON_GEN) $(srcdir)/glsl/ir_expression_operation.py enum > $@

glsl/ir_expression_operation_constant.h: glsl/ir_expression_operation.py
	$(PYTHON_GEN) $(srcdir)/glsl/ir_expression_operation.py constant > $@

glsl/ir_expression_operation_strings.h: glsl/ir_expression_operation.py
	$(PYTHON_GEN) $(srcdir)/glsl/ir_expression_operation.py strings > $@

nir_libnir_la_SOURCES =					\
	$(NIR_FILES)					\
	$(SPIRV_FILES)					\
	$(SPIRV_GENERATED_FILES) 			\
	$(NIR_GENERATED_FILES)

nir/nir_builder_opcodes.h: nir/nir_opcodes.py nir/nir_builder_opcodes_h.py
	$(PYTHON_GEN) $(srcdir)/nir/nir_builder_opcodes_h.py > $@

nir/nir_constant_expressions.c: nir/nir_opcodes.py nir/nir_constant_expressions.py
	$(PYTHON_GEN) $(srcdir)/nir/nir_constant_expressions.py > $@

nir/nir_opcodes.h: nir/nir_opcodes.py nir/nir_opcodes_h.py
	$(PYTHON_GEN) $(srcdir)/nir/nir_opcodes_h.py > $@

nir/nir_opcodes.c: nir/nir_opcodes.py nir/nir_opcodes_c.py
	$(PYTHON_GEN) $(srcdir)/nir/nir_opcodes_c.py > $@

nir/nir_opt_algebraic.c: nir/nir_opt_algebraic.py nir/nir_algebraic.py
	$(PYTHON_GEN) $(srcdir)/nir/nir_opt_algebraic.py > $@

spirv/spirv_info.c: spirv/spirv_info_c.py spirv/spirv.core.grammar.json
	$(PYTHON_GEN) $(srcdir)/spirv/spirv_info_c.py $(srcdir)/spirv/spirv.core.grammar.json $@

spirv2nir_SOURCES = \
	spirv/spirv2nir.c

INCLUDES += \
	$(top_builddir)/src/compiler/nir		\
	$(top_srcdir)/src/compiler/nir		\
	$(top_srcdir)/src/compiler/spirv

DEFINES = INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC

LIBRARY = libcompiler

CSRCS := $(notdir $(subst /,$/,$(libcompiler_la_SOURCES)))
CSRCS += $(notdir $(subst /,$/,$(glsl_libglsl_la_SOURCES)))
CSRCS += $(notdir $(subst /,$/,$(glsl_libglcpp_la_SOURCES)))
CSRCS := $(CSRCS:%.h=)

vpath %.c glsl:glsl/glcpp
vpath %.cpp glsl

