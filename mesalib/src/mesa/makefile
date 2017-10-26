srcdir=.
builddir=.
top_srcdir=../..
top_builddir=../..

ifdef HAVE_XLIB_GLX
#SUBDIRS += drivers/x11
endif

ifdef HAVE_DRICOMMON
#SUBDIRS += drivers/dri
endif

ifdef HAVE_OSMESA
#SUBDIRS += drivers/osmesa
endif

include Makefile.sources

BUILT_SOURCES = \
	main/get_hash.h \
	main/format_fallback.c \
	main/format_info.h \
	main/format_pack.c \
	main/format_unpack.c \
	program/program_parse.tab.c \
	program/lex.yy.c

PYTHON_GEN = python

main/get_hash.h: ../mapi/glapi/gen/gl_and_es_API.xml main/get_hash_params.py \
                 main/get_hash_generator.py
	$(PYTHON_GEN) $(srcdir)/main/get_hash_generator.py \
		-f $(srcdir)/../mapi/glapi/gen/gl_and_es_API.xml > $@

main/format_fallback.c: main/format_fallback.py \
                        main/format_parser.py \
	                main/formats.csv
	$(PYTHON_GEN) $(srcdir)/main/format_fallback.py $(srcdir)/main/formats.csv $@

main/format_info.h: main/formats.csv \
                    main/format_parser.py main/format_info.py
	$(PYTHON_GEN) $(srcdir)/main/format_info.py $(srcdir)/main/formats.csv > $@

main/format_pack.c: main/format_pack.py main/formats.csv \
                    main/format_parser.py
	$(PYTHON_GEN) $(srcdir)/main/format_pack.py $(srcdir)/main/formats.csv > $@

main/format_unpack.c: main/format_unpack.py main/formats.csv	\
                      main/format_parser.py
	$(PYTHON_GEN) $(srcdir)/main/format_unpack.py $(srcdir)/main/formats.csv > $@

main/formats.c: main/format_info.h

MESA_ASM_FILES_FOR_ARCH =

ifdef HAVE_X86_ASM
noinst_PROGRAMS = gen_matypes

gen_matypes_SOURCES = x86/gen_matypes.c
BUILT_SOURCES += matypes.h

ifdef HAVE_X86_64_ASM
MESA_ASM_FILES_FOR_ARCH += $(X86_64_FILES)
AM_CPPFLAGS += -I$(builddir)/x86-64 -I$(srcdir)/x86-64
else
MESA_ASM_FILES_FOR_ARCH += $(X86_FILES)
AM_CPPFLAGS += -I$(builddir)/x86 -I$(srcdir)/x86
endif
endif

libmesa_la_SOURCES = \
	$(MESA_FILES) \
	$(PROGRAM_FILES) \
	$(PROGRAM_NIR_FILES) \
	$(MESA_ASM_FILES_FOR_ARCH)

libmesagallium_la_SOURCES = \
	$(MESA_GALLIUM_FILES) \
	$(PROGRAM_FILES) \
	$(PROGRAM_NIR_FILES) \
	$(MESA_ASM_FILES_FOR_ARCH)

libmesa_sse41_la_SOURCES = \
	$(X86_SSE41_FILES)

YACC_GEN = bison.exe -v
LEX_GEN = flex.exe --nounistd

program/lex.yy.c: program/program_lexer.l
	$(LEX_GEN) -o $@ $(srcdir)/program/program_lexer.l

program/program_parse.tab.c: program/program_parse.y
	$(YACC_GEN) -o $@ -p "_mesa_program_" --defines=$(builddir)/program/program_parse.tab.h $(srcdir)/program/program_parse.y
program/program_parse.tab.h: program/program_parse.y
	$(YACC_GEN) -o $@ -p "_mesa_program_" --defines=$(builddir)/program/program_parse.tab.h $(srcdir)/program/program_parse.y


ifdef GEN_ASM_OFFSETS
matypes.h: $(gen_matypes_SOURCES)
	$(AM_V_GEN)$(COMPILE) $< -DASM_OFFSETS -S -o - | \
		sed -n '/^->/{s:^->::;/[$$]/{s:^:#define :;s:[$$]::};p}' > $@
else
matypes.h: gen_matypes
	./gen_matypes > $@
endif

DEFINES = SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC

LIBRARY = libmesa

CSRCS := $(notdir $(subst /,$/,$(libmesa_la_SOURCES)))
CSRCS := $(CSRCS:%.h=)

INCLUDE_DIRS:=$(INCLUDE_DIRS:-I%=%)

INCLUDES=$(INCLUDE_DIRS)

vpath %.c main:math:vbo:tnl:swrast:swrast_setup:drivers/common:program

vpath %.cpp main:program


