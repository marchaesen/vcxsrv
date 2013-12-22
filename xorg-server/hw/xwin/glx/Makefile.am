noinst_LTLIBRARIES = libXwinGLX.la
lib_LTLIBRARIES = libnativeGLthunk.la

libXwinGLX_la_SOURCES = \
	winpriv.c \
	winpriv.h \
	glwindows.h \
	glshim.c \
	indirect.c \
	wgl_ext_api.c \
	wgl_ext_api.h

libnativeGLthunk_la_SOURCES = \
	glthunk.c

if XWIN_MULTIWINDOW
DEFS_MULTIWINDOW = -DXWIN_MULTIWINDOW
endif

if XWIN_MULTIWINDOWEXTWM
DEFS_MULTIWINDOWEXTWM = -DXWIN_MULTIWINDOWEXTWM
endif

if XWIN_GLX_WINDOWS
DEFS_GLX_WINDOWS = -DXWIN_GLX_WINDOWS
endif

DEFS =  $(DEFS_MULTIWINDOW) $(DEFS_MULTIWINDOWEXTWM) $(DEFS_GLX_WINDOWS)

AM_CPPFLAGS = -I$(top_srcdir)/miext/rootless

AM_CFLAGS = -DHAVE_XWIN_CONFIG_H $(DIX_CFLAGS) \
            $(XWINMODULES_CFLAGS) \
            -I$(top_srcdir) \
            -I$(top_srcdir)/hw/xwin/

libnativeGLthunk_la_CFLAGS = $(AM_CFLAGS) -Wno-unused-function -Wno-missing-prototypes -Wno-missing-declarations
libnativeGLthunk_la_LDFLAGS = -shared -no-undefined -avoid-version -lopengl32 -export-symbols generated_gl_thunks.def
EXTRA_libnativeGLthunk_la_DEPENDENCIES = generated_gl_thunks.def

if XWIN_GLX_WINDOWS

if DEBUG
GENGLWRAPPERSOPTS=""
else
GENGLWRAPPERSOPTS="-nodebug"
endif

generated_wgl_wrappers.c: $(srcdir)/gen_gl_wrappers.py $(KHRONOS_SPEC_DIR)/wgl.xml $(KHRONOS_SPEC_DIR)/reg.py
	$(AM_V_GEN)PYTHONPATH=$(KHRONOS_SPEC_DIR) $(PYTHON3) $(srcdir)/gen_gl_wrappers.py -registry $(KHRONOS_SPEC_DIR)/wgl.xml -prefix wgl -wrapper -preresolve $(GENGLWRAPPERSOPTS) -outfile $@

generated_gl_shim.c: $(srcdir)/gen_gl_wrappers.py $(KHRONOS_SPEC_DIR)/gl.xml $(KHRONOS_SPEC_DIR)/reg.py
	$(AM_V_GEN)PYTHONPATH=$(KHRONOS_SPEC_DIR) $(PYTHON3) $(srcdir)/gen_gl_wrappers.py -registry $(KHRONOS_SPEC_DIR)/gl.xml -shim $(GENGLWRAPPERSOPTS) -outfile $@

generated_gl_thunks.c: $(srcdir)/gen_gl_wrappers.py $(KHRONOS_SPEC_DIR)/gl.xml $(KHRONOS_SPEC_DIR)/reg.py
	$(AM_V_GEN)PYTHONPATH=$(KHRONOS_SPEC_DIR) $(PYTHON3) $(srcdir)/gen_gl_wrappers.py -registry $(KHRONOS_SPEC_DIR)/gl.xml -thunk $(GENGLWRAPPERSOPTS) -outfile $@

generated_gl_thunks.def: $(srcdir)/gen_gl_wrappers.py $(KHRONOS_SPEC_DIR)/gl.xml $(KHRONOS_SPEC_DIR)/reg.py
	$(AM_V_GEN)PYTHONPATH=$(KHRONOS_SPEC_DIR) $(PYTHON3) $(srcdir)/gen_gl_wrappers.py -registry $(KHRONOS_SPEC_DIR)/gl.xml -thunkdefs $(GENGLWRAPPERSOPTS) -outfile $@
endif

BUILT_SOURCES = generated_gl_shim.c generated_gl_thunks.c generated_gl_thunks.def generated_wgl_wrappers.c
CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gen_gl_wrappers.py
