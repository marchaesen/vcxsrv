include Makefile.sources
include $(top_srcdir)/src/gallium/Automake.inc

noinst_LTLIBRARIES = libgallium.la

AM_CFLAGS = \
	-I$(top_srcdir)/src/loader \
	-I$(top_builddir)/src/compiler/nir \
	-I$(top_srcdir)/src/gallium/auxiliary/util \
	$(GALLIUM_CFLAGS) \
	$(VISIBILITY_CFLAGS) \
	$(MSVC2013_COMPAT_CFLAGS)

AM_CXXFLAGS = \
	$(VISIBILITY_CXXFLAGS) \
	$(MSVC2013_COMPAT_CXXFLAGS)

libgallium_la_SOURCES = \
	$(C_SOURCES) \
	$(NIR_SOURCES) \
	$(GENERATED_SOURCES)

if HAVE_MESA_LLVM

AM_CFLAGS += \
	$(LLVM_CFLAGS)

AM_CXXFLAGS += \
	$(GALLIUM_CFLAGS) \
	$(LLVM_CXXFLAGS)

libgallium_la_SOURCES += \
	$(GALLIVM_SOURCES)

endif

MKDIR_GEN = $(AM_V_at)$(MKDIR_P) $(@D)
PYTHON_GEN =  $(AM_V_GEN)$(PYTHON2) $(PYTHON_FLAGS)

indices/u_indices_gen.c: indices/u_indices_gen.py
	$(MKDIR_GEN)
	$(PYTHON_GEN) $(srcdir)/indices/u_indices_gen.py > $@

indices/u_unfilled_gen.c: indices/u_unfilled_gen.py
	$(MKDIR_GEN)
	$(PYTHON_GEN) $(srcdir)/indices/u_unfilled_gen.py > $@

util/u_format_table.c: util/u_format_table.py \
                       util/u_format_pack.py \
                       util/u_format_parse.py \
                       util/u_format.csv
	$(MKDIR_GEN)
	$(PYTHON_GEN) $(srcdir)/util/u_format_table.py $(srcdir)/util/u_format.csv > $@

noinst_LTLIBRARIES += libgalliumvl_stub.la
libgalliumvl_stub_la_SOURCES = \
	$(VL_STUB_SOURCES)

if NEED_GALLIUM_VL

COMMON_VL_CFLAGS = \
	$(AM_CFLAGS) \
	$(VL_CFLAGS) \
	$(DRI2PROTO_CFLAGS) \
	$(LIBDRM_CFLAGS)

noinst_LTLIBRARIES += libgalliumvl.la

libgalliumvl_la_CFLAGS = \
	$(COMMON_VL_CFLAGS)

libgalliumvl_la_SOURCES = \
	$(VL_SOURCES)

if NEED_GALLIUM_VL_WINSYS

noinst_LTLIBRARIES += libgalliumvlwinsys.la

libgalliumvlwinsys_la_CFLAGS = \
	$(COMMON_VL_CFLAGS)

libgalliumvlwinsys_la_SOURCES = \
	$(VL_WINSYS_SOURCES)

if HAVE_DRI3

libgalliumvlwinsys_la_SOURCES += \
	$(VL_WINSYS_DRI3_SOURCES)

endif

endif

endif

EXTRA_DIST = \
	SConscript \
	indices/u_indices.c \
	indices/u_unfilled_indices.c \
	indices/u_indices_gen.py \
	indices/u_unfilled_gen.py \
	postprocess/ADDING \
	rbug/README \
	target-helpers \
	util/u_format.csv \
	util/u_format_pack.py \
	util/u_format_parse.py \
	util/u_format_table.py
