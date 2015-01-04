AUTOMAKE_OPTIONS = subdir-objects

if HAVE_LOADER_GALLIUM
SUBDIRS := pipe-loader
endif

include Makefile.sources
include $(top_srcdir)/src/gallium/Automake.inc

noinst_LTLIBRARIES = libgallium.la

AM_CFLAGS = \
	-I$(top_srcdir)/src/gallium/auxiliary/util \
	$(GALLIUM_CFLAGS) \
	$(VISIBILITY_CFLAGS)

AM_CXXFLAGS = $(VISIBILITY_CXXFLAGS)

libgallium_la_SOURCES = \
	$(C_SOURCES) \
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

indices/u_indices_gen.c: $(srcdir)/indices/u_indices_gen.py
	$(AM_V_at)$(MKDIR_P) indices
	$(AM_V_GEN) $(PYTHON2) $< > $@

indices/u_unfilled_gen.c: $(srcdir)/indices/u_unfilled_gen.py
	$(AM_V_at)$(MKDIR_P) indices
	$(AM_V_GEN) $(PYTHON2) $< > $@

util/u_format_table.c: $(srcdir)/util/u_format_table.py $(srcdir)/util/u_format_pack.py $(srcdir)/util/u_format_parse.py $(srcdir)/util/u_format.csv
	$(AM_V_at)$(MKDIR_P) util
	$(AM_V_GEN) $(PYTHON2) $(srcdir)/util/u_format_table.py $(srcdir)/util/u_format.csv > $@


noinst_LTLIBRARIES += libgalliumvl_stub.la
libgalliumvl_stub_la_SOURCES = \
	$(VL_STUB_SOURCES)

if NEED_GALLIUM_VL

noinst_LTLIBRARIES += libgalliumvl.la

libgalliumvl_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(VL_CFLAGS) \
	$(LIBDRM_CFLAGS) \
	$(GALLIUM_PIPE_LOADER_DEFINES) \
	-DPIPE_SEARCH_DIR=\"$(libdir)/gallium-pipe\"

if HAVE_GALLIUM_STATIC_TARGETS
libgalliumvl_la_CFLAGS += \
	-DGALLIUM_STATIC_TARGETS=1

endif # HAVE_GALLIUM_STATIC_TARGETS

libgalliumvl_la_SOURCES = \
	$(VL_SOURCES)

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
