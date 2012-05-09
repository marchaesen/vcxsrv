noinst_LTLIBRARIES = libXwinGLX.la

libXwinGLX_la_SOURCES = \
	winpriv.c \
	winpriv.h \
	glwindows.h \
	glwrap.c \
	indirect.c \
	wgl_ext_api.c

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

INCLUDES = -I$(top_srcdir)/miext/rootless

AM_CFLAGS = -DHAVE_XWIN_CONFIG_H $(DIX_CFLAGS) \
            $(XWINMODULES_CFLAGS) \
            -I$(top_srcdir) \
            -I$(top_srcdir)/hw/xwin/

if XWIN_GLX_WINDOWS

generated_gl_wrappers.c: gen_gl_wrappers.py $(KHRONOS_SPEC_DIR)/gl.spec $(KHRONOS_SPEC_DIR)/gl.tm
	$(AM_V_GEN)$(srcdir)/gen_gl_wrappers.py --spec=$(KHRONOS_SPEC_DIR)/gl.spec --typemap=$(KHRONOS_SPEC_DIR)/gl.tm --dispatch-header=$(top_srcdir)/glx/dispatch.h --staticwrappers >generated_gl_wrappers.c

generated_wgl_wrappers.c: gen_gl_wrappers.py $(KHRONOS_SPEC_DIR)/wglext.spec $(KHRONOS_SPEC_DIR)/wgl.tm
	$(AM_V_GEN)$(srcdir)/gen_gl_wrappers.py --spec=$(KHRONOS_SPEC_DIR)/wglext.spec --typemap=$(KHRONOS_SPEC_DIR)/wgl.tm --prefix=wgl --preresolve >generated_wgl_wrappers.c
endif

BUILT_SOURCES = generated_gl_wrappers.c generated_wgl_wrappers.c
CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gen_gl_wrappers.py
