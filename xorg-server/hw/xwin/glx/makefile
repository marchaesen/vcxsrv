LIBRARY=libwinglx

INCLUDES += $(OBJDIR) ..\..\..\glx .. ..\..\..
DEFINES += GLAPIENTRY=__stdcall _GLAPI_NO_EXPORTS

DEFINES += XWIN_CLIPBOARD
DEFINES += XWIN_MULTIWINDOW
DEFINES += XWIN_GLX_WINDOWS
DEFINES += XWIN_RANDR
DEFINES += RELOCATE_PROJECTROOT
INCLUDES += ..\..\..\miext\rootless
DEFINES += HAVE_XWIN_CONFIG_H PTW32_STATIC_LIB

CSRCS = \
	winpriv.c \
	glwrap.c \
	indirect.c \
	wgl_ext_api.c

.PHONY: getspecfiles

getspecfiles:
	-del wglext.h
	wget http://www.opengl.org/registry/api/GL/wglext.h
	-del glxext.h
	wget http://www.opengl.org/registry/api/GL/glxext.h
	-del glext.h
	wget http://www.opengl.org/registry/api/GL/glext.h
	-del genheaders.py
	wget --no-check-certificate https://cvs.khronos.org/svn/repos/ogl/trunk/doc/registry/public/api/genheaders.py
	-del reg.py
	wget --no-check-certificate https://cvs.khronos.org/svn/repos/ogl/trunk/doc/registry/public/api/reg.py
	-del gl.xml
	wget --no-check-certificate https://cvs.khronos.org/svn/repos/ogl/trunk/doc/registry/public/api/gl.xml
	-del wgl.xml
	wget --no-check-certificate https://cvs.khronos.org/svn/repos/ogl/trunk/doc/registry/public/api/wgl.xml

$(OBJDIR)\generated_gl_wrappers.c: gen_gl_wrappers.py gl.xml gen_gl_wrappers.py reg.py
	$(PYTHON3) gen_gl_wrappers.py -outfile $@ -staticwrappers -dispatchheader ../../../glx/dispatch.h

$(OBJDIR)\generated_wgl_wrappers.c: gen_gl_wrappers.py wgl.xml gen_gl_wrappers.py reg.py
	$(PYTHON3) gen_gl_wrappers.py -outfile $@ -registry wgl.xml -prefix wgl -preresolve

