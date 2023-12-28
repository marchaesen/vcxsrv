LIBRARY=libwinglx

INCLUDES += $(OBJDIR) ..\..\..\glx .. ..\..\.. $(MHMAKECONF)\gl\include $(MHMAKECONF)\mesalib\include
DEFINES += GLAPIENTRY=__stdcall _GLAPI_NO_EXPORTS

DEFINES += XWIN_GLX_WINDOWS
DEFINES += RELOCATE_PROJECTROOT
INCLUDES += ..\..\..\miext\rootless
DEFINES += HAVE_XWIN_CONFIG_H PTW32_STATIC_LIB PIXMAN_API=

CSRCS = \
	winpriv.c \
	glwrap.c \
	indirect.c \
	wgl_ext_api.c

.PHONY: getspecfiles

getspecfiles:
	del -e wglext.h
	wget https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/api/GL/wglext.h
	del -e glxext.h
	wget https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/api/GL/glxext.h
	del -e glext.h
	wget https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/api/GL/glext.h
	del -e genheaders.py
	wget https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/xml/genheaders.py
	del -e reg.py
	wget https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/xml/reg.py
	del -e gl.xml
	wget https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/xml/gl.xml
	del -e wgl.xml
	wget https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/xml/wgl.xml

$(OBJDIR)\generated_gl_wrappers.c: gen_gl_wrappers.py gl.xml gen_gl_wrappers.py reg.py
	$(PYTHON3) gen_gl_wrappers.py -outfile $@ -staticwrappers -dispatchheader ../../../glx/dispatch.h

$(OBJDIR)\generated_wgl_wrappers.c: gen_gl_wrappers.py wgl.xml gen_gl_wrappers.py reg.py
	$(PYTHON3) gen_gl_wrappers.py -outfile $@ -registry wgl.xml -prefix wgl -preresolve

