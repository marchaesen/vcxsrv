SHAREDLIB = swrastwgl_dri

INCLUDES += $(OBJDIR) ..\..\.. ..
DEFINES += INSERVER HAVE_XWIN_CONFIG_H PTW32_STATIC_LIB

ifeq ($(DEBUG),1)
LINKLIBS += ..\..\..\obj\servdebug\vcxsrv_dbg.lib
else
LINKLIBS += ..\..\..\obj\servrelease\vcxsrv.lib
endif

CSRCS = swrastwgl_dri.c glwrap.c wgl_ext_api.c

$(OBJDIR)\generated_gl_wrappers.c: gen_gl_wrappers.py gl.spec gl.tm
	gen_gl_wrappers --spec=gl.spec --typemap=gl.tm --dispatch-header=../../../glx/dispatch.h --staticwrappers > $@

$(OBJDIR)\generated_wgl_wrappers.c: gen_gl_wrappers.py wglext.spec wgl.tm
	gen_gl_wrappers --spec=wglext.spec --typemap=wgl.tm --prefix=wgl --preresolve > $@
