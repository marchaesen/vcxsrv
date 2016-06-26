# Copyright © 2012 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

SUBDIRS = . main/tests

if HAVE_XLIB_GLX
SUBDIRS += drivers/x11
endif

if HAVE_DRICOMMON
SUBDIRS += drivers/dri
endif

if HAVE_OSMESA
SUBDIRS += drivers/osmesa
endif

gldir = $(includedir)/GL
gl_HEADERS = $(top_srcdir)/include/GL/*.h

include Makefile.sources

EXTRA_DIST = \
	drivers/SConscript \
	main/format_info.py \
	main/format_pack.py \
	main/format_parser.py \
	main/format_unpack.py \
	main/formats.csv \
	main/get_hash_generator.py \
	main/get_hash_params.py \
	program/program_lexer.l \
	program/program_parse.y \
	SConscript \
	swrast/NOTES \
	swrast_setup/NOTES \
	tnl/NOTES \
	tnl_dd

BUILT_SOURCES = \
	main/get_hash.h \
	main/format_info.h \
	main/format_pack.c \
	main/format_unpack.c \
	program/program_parse.tab.c \
	program/lex.yy.c
CLEANFILES = \
	$(BUILT_SOURCES) \
	program/program_parse.tab.h

PYTHON_GEN = $(AM_V_GEN)$(PYTHON2) $(PYTHON_FLAGS)

main/get_hash.h: ../mapi/glapi/gen/gl_and_es_API.xml main/get_hash_params.py \
                 main/get_hash_generator.py
	$(PYTHON_GEN) $(srcdir)/main/get_hash_generator.py \
		-f $(srcdir)/../mapi/glapi/gen/gl_and_es_API.xml > $@

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

noinst_LTLIBRARIES = $(ARCH_LIBS)
if NEED_LIBMESA
noinst_LTLIBRARIES += libmesa.la
else
check_LTLIBRARIES = libmesa.la
endif
if HAVE_GALLIUM
noinst_LTLIBRARIES += libmesagallium.la
endif

AM_CPPFLAGS = $(DEFINES) $(INCLUDE_DIRS)
AM_CFLAGS = \
	$(LLVM_CFLAGS) \
	$(VISIBILITY_CFLAGS) \
	$(MSVC2013_COMPAT_CFLAGS)
AM_CXXFLAGS = \
	$(LLVM_CFLAGS) \
	$(VISIBILITY_CXXFLAGS) \
	$(MSVC2013_COMPAT_CXXFLAGS)

ARCH_LIBS =

if SSE41_SUPPORTED
ARCH_LIBS += libmesa_sse41.la
endif

MESA_ASM_FILES_FOR_ARCH =

if HAVE_X86_ASM
noinst_PROGRAMS = gen_matypes

gen_matypes_SOURCES = x86/gen_matypes.c
BUILT_SOURCES += matypes.h

if HAVE_X86_64_ASM
MESA_ASM_FILES_FOR_ARCH += $(X86_64_FILES)
AM_CPPFLAGS += -I$(builddir)/x86-64 -I$(srcdir)/x86-64
else
MESA_ASM_FILES_FOR_ARCH += $(X86_FILES)
AM_CPPFLAGS += -I$(builddir)/x86 -I$(srcdir)/x86
endif
endif
if HAVE_SPARC_ASM
MESA_ASM_FILES_FOR_ARCH += $(SPARC_FILES)
AM_CPPFLAGS += -I$(builddir)/sparc -I$(srcdir)/sparc
endif

libmesa_la_SOURCES = \
	$(MESA_FILES) \
	$(PROGRAM_FILES) \
	$(PROGRAM_NIR_FILES) \
	$(MESA_ASM_FILES_FOR_ARCH)

libmesa_la_LIBADD = \
	$(top_builddir)/src/compiler/glsl/libglsl.la \
	$(ARCH_LIBS)

libmesagallium_la_SOURCES = \
	$(MESA_GALLIUM_FILES) \
	$(PROGRAM_FILES) \
	$(PROGRAM_NIR_FILES) \
	$(MESA_ASM_FILES_FOR_ARCH)

libmesagallium_la_LIBADD = \
	$(top_builddir)/src/compiler/glsl/libglsl.la \
	$(ARCH_LIBS)

libmesa_sse41_la_SOURCES = \
	$(X86_SSE41_FILES)

libmesa_sse41_la_CFLAGS = $(AM_CFLAGS) $(SSE41_CFLAGS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gl.pc

MKDIR_GEN = $(AM_V_at)$(MKDIR_P) $(@D)
YACC_GEN = $(AM_V_GEN)$(YACC) $(YFLAGS)
LEX_GEN = $(AM_V_GEN)$(LEX) $(LFLAGS)

program/lex.yy.c: program/program_lexer.l
	$(MKDIR_GEN)
	$(LEX_GEN) -o $@ $(srcdir)/program/program_lexer.l

program/program_parse.tab.c program/program_parse.tab.h: program/program_parse.y
	$(MKDIR_GEN)
	$(YACC_GEN) -o $@ -p "_mesa_program_" --defines=$(builddir)/program/program_parse.tab.h $(srcdir)/program/program_parse.y

if GEN_ASM_OFFSETS
matypes.h: $(gen_matypes_SOURCES)
	$(AM_V_GEN)$(COMPILE) $< -DASM_OFFSETS -S -o - | \
		sed -n '/^->/{s:^->::;/[$$]/{s:^:#define :;s:[$$]::};p}' > $@
else
matypes.h: gen_matypes
	$(AM_V_GEN)./gen_matypes > $@
endif

# Emacs tags
tags:
	etags `find . -name \*.[ch]` $(top_srcdir)/include/GL/*.h
